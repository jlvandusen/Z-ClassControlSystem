// ============================================================
//  TrinketM0_MPU_RC4  —  IMU node (Trinket M0 + MPU6050)
//  RC4 changes vs RC3/BASE:
//   1. FIXED: calibration never completed (inner >= check was
//      unreachable inside the outer < check) -> bias was never
//      applied. Restructured into a state machine.
//   2. FIXED: incoming command packets no longer overwrite the
//      live mpudata struct (received into a separate struct).
//   3. Output rate raised 50 Hz -> 100 Hz (10 ms) for the
//      balance loop; MPU DLPF raised 21 Hz -> 44 Hz to cut lag.
//   4. Removed delay(1); pacing is handled by the 10 ms gate.
// ============================================================

#include <SerialTransfer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Kalman.h>  // TKJElectronics Kalman filter library

SerialTransfer ComsESP32;

// ------------------- EXACT STRUCT FROM ESP32 -------------------
#pragma pack(push, 1)
typedef struct struct_messagempu {
  float rawX = 0.0f;
  float rawY = 0.0f;
  float rawZ = 0.0f;
  float pitch = 0.0f;
  float roll  = 0.0f;
  byte functionnumber = 0;      // 99 = debug toggle command
  uint16_t checksum = 0;
} struct_messagempu;
#pragma pack(pop)
struct_messagempu mpudata;
struct_messagempu rxCommand;    // RC4: separate receive buffer

Adafruit_MPU6050 mpu;
Kalman kalmanPitch;
Kalman kalmanRoll;

// RC4 fix: gyro axes were SWAPPED in RC3 (pitch used gyro.x, roll used
// gyro.y). pitch = atan2(-ax, sqrt(ay^2+az^2)) is rotation about the Y
// axis -> pairs with gyro.y; roll = atan2(ay, az) is rotation about X
// -> pairs with gyro.x. Wrong pairing makes the Kalman filter lag and
// overshoot during any rotation. If an angle now runs away while
// rotating, flip that axis's sign below (do NOT swap axes back).
const int8_t GYRO_PITCH_SIGN = 1;
const int8_t GYRO_ROLL_SIGN  = 1;

// RC4: calibration state machine
const unsigned long CALIB_DURATION = 2000;
float pitchBias = 0.0f;
float rollBias  = 0.0f;
double calibSumPitch = 0.0, calibSumRoll = 0.0;
uint32_t calibCount = 0;
unsigned long calibStart = 0;
bool calibrated = false;

bool debugMode = false;

const long BAUD_ESP32 = 115200;
const unsigned long SAMPLE_INTERVAL_MS = 10;   // RC4: 100 Hz

void setup() {
  Serial.begin(115200);
  Serial1.begin(BAUD_ESP32);
  ComsESP32.begin(Serial1);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println(F("\n=== Trinket M0 IMU Node RC4 ==="));

  if (!mpu.begin()) {
    Serial.println(F("[FATAL] MPU6050 not found!"));
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH); delay(150);
      digitalWrite(LED_BUILTIN, LOW);  delay(150);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);   // RC4: was 21 Hz (too laggy for balance)

  kalmanPitch.setQangle(0.001);
  kalmanPitch.setQbias(0.003);
  kalmanPitch.setRmeasure(0.03);
  kalmanRoll.setQangle(0.001);
  kalmanRoll.setQbias(0.003);
  kalmanRoll.setRmeasure(0.03);

  calibStart = millis();
  calibrated = false;

  Serial.println(F("Calibrating 2s — hold flat!"));
}

void loop() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < SAMPLE_INTERVAL_MS) return;
  lastUpdate = millis();

  readAndFilterIMU();

  // ---------- RC4: calibration state machine (actually completes) ----------
  if (!calibrated) {
    if (millis() - calibStart < CALIB_DURATION) {
      calibSumPitch += mpudata.pitch;
      calibSumRoll  += mpudata.roll;
      calibCount++;
      // Report zeros while calibrating so the ESP32 doesn't react
      mpudata.pitch = 0.0f;
      mpudata.roll  = 0.0f;
    } else {
      if (calibCount > 0) {
        pitchBias = (float)(calibSumPitch / calibCount);
        rollBias  = (float)(calibSumRoll  / calibCount);
      }
      calibrated = true;
      Serial.print(F("[CALIB] Done — Pitch bias: "));
      Serial.print(pitchBias, 2);
      Serial.print(F(" Roll bias: "));
      Serial.println(rollBias, 2);
    }
  } else {
    mpudata.pitch -= pitchBias;
    mpudata.roll  -= rollBias;
  }

  // Safety
  if (isnan(mpudata.pitch) || isnan(mpudata.roll)) {
    return;
  }

  mpudata.functionnumber = 0;
  mpudata.checksum = calculateChecksum(mpudata);

  uint16_t bytes = ComsESP32.txObj(mpudata, 0);
  ComsESP32.sendData(bytes);

  if (debugMode) {
    Serial.print(F("[IMU] P="));
    Serial.print(mpudata.pitch, 2);
    Serial.print(F(" R="));
    Serial.print(mpudata.roll, 2);
    Serial.print(F(" | RawX="));
    Serial.print(mpudata.rawX, 1);
    Serial.print(F(" Y="));
    Serial.print(mpudata.rawY, 1);
    Serial.print(F(" Z="));
    Serial.println(mpudata.rawZ, 1);
  }

  // RC4: receive commands into rxCommand — never clobber live IMU data
  if (ComsESP32.available()) {
    ComsESP32.rxObj(rxCommand);
    if (rxCommand.functionnumber == 99) {
      debugMode = !debugMode;
      Serial.println(debugMode ?
        F("*** Trinket DEBUG ENABLED ***") :
        F("*** Trinket DEBUG DISABLED ***"));
      digitalWrite(LED_BUILTIN, debugMode);
    }
  }
}

void readAndFilterIMU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  mpudata.rawX = a.acceleration.x;
  mpudata.rawY = a.acceleration.y;
  mpudata.rawZ = a.acceleration.z;

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  float accPitch = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0 / PI;
  float accRoll  = atan2(ay, az) * 180.0 / PI;

  static unsigned long lastTime = 0;
  if (lastTime == 0) {
    lastTime = micros();
    kalmanPitch.setAngle(accPitch);
    kalmanRoll.setAngle(accRoll);
    mpudata.pitch = accPitch;
    mpudata.roll  = accRoll;
    return;
  }

  unsigned long now = micros();
  float dt = (now - lastTime) / 1000000.0;
  lastTime = now;
  if (dt > 0.1) dt = 0.1;

  // RC4 fix: correct axis pairing (see note at GYRO_*_SIGN above)
  float gyroPitchRate = g.gyro.y * 180.0 / PI * GYRO_PITCH_SIGN;
  float gyroRollRate  = g.gyro.x * 180.0 / PI * GYRO_ROLL_SIGN;

  mpudata.pitch = kalmanPitch.getAngle(accPitch, gyroPitchRate, dt);
  mpudata.roll  = kalmanRoll.getAngle(accRoll,  gyroRollRate,  dt);
}

uint16_t calculateChecksum(const struct_messagempu &data) {
  const uint8_t* ptr = (const uint8_t*)&data;
  uint16_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - sizeof(data.checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}
