#include <SerialTransfer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Kalman.h>  // TKJElectronics Kalman filter - place in sketch folder

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

Adafruit_MPU6050 mpu;
Kalman kalmanPitch;
Kalman kalmanRoll;

const int8_t GYRO_PITCH_SIGN = 1;   // Flip if pitch drifts wrong
const int8_t GYRO_ROLL_SIGN  = -1;  // Usually -1

const unsigned long CALIB_DURATION = 2000;
float pitchBias = 0.0f;
float rollBias  = 0.0f;
int calibCount = 0;
unsigned long calibStart = 0;
bool calibrated = false;

bool debugMode = false;

const long BAUD_ESP32 = 115200;

void setup() {
  Serial.begin(115200);
  Serial1.begin(BAUD_ESP32);
  ComsESP32.begin(Serial1);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println(F("\n=== Trinket M0 IMU Node ==="));

  if (!mpu.begin()) {
    Serial.println(F("[FATAL] MPU6050 not found!"));
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH); delay(150);
      digitalWrite(LED_BUILTIN, LOW);  delay(150);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

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
  // if (millis() - lastUpdate < 10) return;
  if (millis() - lastUpdate < 20) return; // 50Hz instead of 100Hz
  lastUpdate = millis();

  readAndFilterIMU();

  // Calibration (your original logic)
  if (!calibrated && millis() - calibStart < CALIB_DURATION) {
    pitchBias += mpudata.pitch;
    rollBias  += mpudata.roll;
    calibCount++;

    mpudata.pitch = 0.0f;
    mpudata.roll  = 0.0f;

    if (millis() - calibStart >= CALIB_DURATION) {
      if (calibCount > 0) {
        pitchBias /= calibCount;
        rollBias  /= calibCount;
      }
      calibrated = true;
      Serial.printf("[CALIB] Done — Pitch bias: %.2f° Roll bias: %.2f°\n", pitchBias, rollBias);
    }
  }
  else if (calibrated) {
    mpudata.pitch -= pitchBias;
    mpudata.roll  -= rollBias;
  }

  // Safety
  if (isnan(mpudata.pitch) || isnan(mpudata.roll)) {
    return;
  }

  mpudata.functionnumber = 0;

  mpudata.checksum = calculateChecksum(mpudata);

  // Correct send logic
  uint16_t bytes = ComsESP32.txObj(mpudata, 0);
  ComsESP32.sendData(bytes);
  delay(1); // Prevent flooding

  // PRINT DEBUG IMMEDIATELY AFTER SEND (safe!)
if (debugMode) {
  Serial.printf("[IMU] P=%.2f deg R=%.2f deg | RawX=%.1f Y=%.1f Z=%.1f\n",
                mpudata.pitch, mpudata.roll,
                mpudata.rawX, mpudata.rawY, mpudata.rawZ);
  Serial.println("[TEST] Debug print executed!");
}

  // RECEIVE LAST (may overwrite mpudata, but we already printed/sent)
  if (ComsESP32.available()) {
    ComsESP32.rxObj(mpudata);
    if (mpudata.functionnumber == 99) {
      debugMode = !debugMode;
      Serial.println(debugMode ?
        F("*** Trinket DEBUG ENABLED ***") :
        F("*** Trinket DEBUG DISABLED ***"));
      digitalWrite(LED_BUILTIN, debugMode);
    }
  }
  
// Serial.print("Trinket sizeof(struct_messagempu): ");
// Serial.println(sizeof(struct_messagempu));

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

  float gyroPitchRate = g.gyro.x * 180.0 / PI * GYRO_PITCH_SIGN;
  float gyroRollRate  = g.gyro.y * 180.0 / PI * GYRO_ROLL_SIGN;

  mpudata.pitch = kalmanPitch.getAngle(accPitch, gyroPitchRate, dt);
  mpudata.roll  = kalmanRoll.getAngle(accRoll,  gyroRollRate,  dt);
  if (debugMode) {
  Serial.printf("[IMU] P=%.2f° R=%.2f° | RawX=%.1f Y=%.1f Z=%.1f\n",
                mpudata.pitch, mpudata.roll,
                mpudata.rawX, mpudata.rawY, mpudata.rawZ);
}
}

uint16_t calculateChecksum(const struct_messagempu &data) {
  const uint8_t* ptr = (const uint8_t*)&data;
  uint16_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - sizeof(data.checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}