#include <Arduino.h>
#include <SerialTransfer.h>
#include "ConfigTypes.h"
#include <esp_now.h>
#include "esp_wifi.h"
#include <WiFi.h>
#include <Bluepad32.h>
#include <esp_coexist.h>
#include <Preferences.h>
#include "ControllerState.h"
#include "SoundMapping.h"
#include "CalibrationModule.h"
#include "PIDController.h"

#define ENABLE_ESPNOW 1  // Set to 1 to enable ESPNOW, 0 to disable
#define WIFI_CHANNEL 11
#define HEARTBEAT_LED 2  // Built-in LED on many ESP32 boards
#define WIFI_TASKDELAY 100

// ------------------- CONFIG -------------------
#define REVERSE_DRIVE false  // Set to true to reverse drive direction
#define REVERSE_S2S false    // Set to true to reverse S2S direction

// ------------------- CONFIGURABLE DEFAULTS -------------------
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC3";
static const char* DEFAULT_REVISION_DATE = "2026-03-02";
const unsigned long SHOW_AFTER_MS = 5000;
bool shownAfterWait = false;
unsigned long bootMs = 0;

// PWM/Motor Pins
const uint8_t S2S_PWM = 33;
const uint8_t S2S_PIN_1 = 26;
const uint8_t S2S_PIN_2 = 25;
const uint8_t DRIVE_PWM = 21;
const uint8_t DRIVE_PIN_1 = 4;
const uint8_t DRIVE_PIN_2 = 27;
const uint8_t S2S_POT_PIN = 34;
const uint8_t FLYWHEEL_PWM = 15;
const uint8_t FLYWHEEL_PIN_1 = 32;
const uint8_t FLYWHEEL_PIN_2 = 14;

// Calibration defaults
static const int32_t DEFAULT_POT_CENTER = 1500;
static const float DEFAULT_PITCH_OFFSET = 0.0f;
static const float DEFAULT_ROLL_OFFSET = 0.0f;

// PID defaults

static const float DEFAULT_DRIVE_PK = 15.0f;  // was 1.0
static const float DEFAULT_DRIVE_KI = 0.02f;
static const float DEFAULT_DRIVE_KD = 0.5f;

static const float DEFAULT_S2S_PK = 15.0f;  // was 1.0
static const float DEFAULT_S2S_KI = 0.02f;
static const float DEFAULT_S2S_KD = 0.4f;

static const int32_t DEFAULT_DRIVE_GAIN = 10.0f;


// Deadzone defaults
static const int32_t DEFAULT_POT_DEADZONE = 50;
static const float DEFAULT_MPU_DEADZONE = 3.0f;
static const int32_t DEFAULT_JOY_DEADZONE = 5;

// Preferred MAC defaults
static const uint8_t DEFAULT_PREF_DRIVE_MAC[6] = { 0x00, 0x06, 0xF5, 0x64, 0x60, 0x3E };  // 00:06:F5:64:60:3E
static const uint8_t DEFAULT_PREF_DOME_MAC[6] = { 0, 0, 0, 0, 0, 0 };

// MAC for ESPNOW
uint8_t domeMACAddress[] = { 0xC4, 0x5B, 0xBE, 0x90, 0x6A, 0x24 }; //Dome WiFi STA MAC: C4:5B:BE:90:6A:24


// Config version
static const uint32_t DEFAULT_CFG_VERSION = 0x00010001u;

// S2S Swing Configuration
float s2sMaxDegrees = 70.0f;
const float S2S_FULL_SWING_DEGREES = 92.0f;
const int POT_FULL_SWING_COUNTS = 1000;
const int CENTER_DEADZONE = 50;


// ------------------- PWM CONFIG -------------------
const int PWM_FREQ = 20000;  // 20 kHz for quiet motor operation
const int PWM_RES = 8;       // 8-bit resolution (0–255)
const int DRIVE_CH = 0;      // PWM channel for drive motor
const int S2S_CH = 1;        // PWM channel for S2S motor
const int FLYWHEEL_CH = 2;   // PWM channel for flywheel motor

// ------------------- Debug Variables -------------------
volatile uint32_t gDriveDuty = 0;    // Current PWM duty for drive motor
volatile uint32_t gS2SDuty = 0;      // Current PWM duty for S2S motor
volatile bool gDriveDirFwd = false;  // Drive direction flag
volatile bool gS2SDirFwd = false;    // S2S direction flag

volatile int gFlywheelPWM = 0;
volatile bool gFlywheelDirFwd = false;


// ------------------- GLOBALS -------------------
// structs are located in ConfigTypes.h
struct_messagempu mpudata;
struct_messagedome domeData;
send32u4 sendTo32u4;
Rec32u4 recFrom32u4;
DriveConfig cfg;


volatile bool lastSendPending = false;
volatile bool lastSendSuccess = false;
unsigned long lastRetryTime = 0;
const uint16_t RETRY_INTERVAL_MS = 20; // 20ms between retries


bool pidTuneMode = false;
bool tuningDrivePID = false;
bool tuningS2SPID = false;
int tuningStep = 0;  // 0=waiting confirm, 1=Kp, 2=Ki, 3=Kd
unsigned long lastInputTime = 0;
static unsigned long comboStartTime = 0;

// Debug flags
bool debugAll = false;
bool debugMPU = false;
bool debug32u4 = false;
bool debugDome = false;
bool debugControllersFlag = false;  // renamed to avoid conflict
bool debugS2S = false;
bool debugDrive = false;
bool debugSound = false;
bool debugFlywheel = false;
bool debugTo32u4 = false;    // For data sent TO 32u4
bool debugFrom32u4 = false;  // For data received FROM 32u4

bool imuHasSample = false;
unsigned long bootStartMs = 0;
const uint32_t BOOT_CAL_TIMEOUT_MS = 5000;

bool bootCalibrating = true;
unsigned long bootCalStart = 0;

unsigned long upComboStart = 0;
bool upComboActive = false;

bool isPlaying = false;
bool flywheelMode = false;    // Global flag to enable/disable flywheel mode
GamepadPtr myControllers[2];  // [0] Drive, [1] Dome
ControllerState driveController, domeController;

bool driveEnabled = false, autoBalance = false, domeFunctionEnabled = false;
SerialTransfer Coms32u4, ComsTrinket;
Preferences prefs;

float lastBatteryVoltage = 4.0;  // Battery monitoring
unsigned long lastSoundTriggerMs = 0;
uint16_t lastSoundCmd = SOUND_NONE;
const uint16_t SOUND_DEBOUNCE_MS = 250;

unsigned long last32u4Packet = 0;  // Tracks last valid packet from 32u4
unsigned long lastIMUUpdate = 0;


// Use existing PID defaults for Drive (Pitch) and S2S (Roll)
float driveKp = DEFAULT_DRIVE_PK;
float driveKi = DEFAULT_DRIVE_KI;
float driveKd = DEFAULT_DRIVE_KD;

float s2sKp = DEFAULT_S2S_PK;
float s2sKi = DEFAULT_S2S_KI;
float s2sKd = DEFAULT_S2S_KD;

float pitchErrorSum = 0.0f;
float rollErrorSum = 0.0f;
float lastPitchError = 0.0f;
float lastRollError = 0.0f;

unsigned long lastBalanceUpdate = 0;

// Instantiate PID controllers
PIDController drivePID(DEFAULT_DRIVE_PK, DEFAULT_DRIVE_KI, DEFAULT_DRIVE_KD);
PIDController s2sPID(DEFAULT_S2S_PK, DEFAULT_S2S_KI, DEFAULT_S2S_KD);



// ------------------- Utility Functions -------------------
static inline int8_t applyDeadzoneAxis(int8_t v, int dz = 10) {
  return (abs(v) <= dz) ? 0 : v;
}
static inline float applyDeadzoneFloat(float v, float dz) {
  return (fabsf(v) <= dz) ? 0.0f : v;
}

uint16_t calculateChecksumSend(const send32u4& d) {
  const uint8_t* p = (const uint8_t*)&d;
  uint16_t s = 0;
  for (size_t i = 0; i < sizeof(d) - 2; i++) s += p[i];
  return s;
}

uint16_t calculateChecksumDome(const struct_messagedome& d) {
  const uint8_t* p = (const uint8_t*)&d;
  uint16_t s = 0;
  for (size_t i = 0; i < sizeof(d) - 2; i++) s += p[i];
  return s;
}


// Placeholder for controller summary function
void printControllersSummary() {
  Serial.println(F("[INFO] Controller summary not implemented yet."));
  // You can later add logic to print connected controllers
}


// ------------------- Sound Sender -------------------

inline void sendSoundCommand(SerialTransfer& coms, send32u4& payload, uint16_t cmd) {
  if (cmd == SOUND_NONE) return;

  unsigned long now = millis();
  if (now - lastSoundTriggerMs < SOUND_DEBOUNCE_MS && cmd == lastSoundCmd) return;

  lastSoundTriggerMs = now;
  lastSoundCmd = cmd;

  payload.soundcmd = cmd;
  payload.checksum = calculateChecksumSend(payload);

  uint16_t bytes = coms.txObj(payload, 0);
  coms.sendData(bytes);

  // Reset soundcmd so next packet doesn't repeat
  payload.soundcmd = SOUND_NONE;

  if (debugSound) {
    Serial.printf("[SoundSender] Sent SoundCMD=%u\n", cmd);
  }
}

// ------------------- Motor Control -------------------

void initMotors() {
  // Set pin modes for motor direction pins
  pinMode(DRIVE_PIN_1, OUTPUT);
  pinMode(DRIVE_PIN_2, OUTPUT);
  pinMode(S2S_PIN_1, OUTPUT);
  pinMode(S2S_PIN_2, OUTPUT);
  pinMode(FLYWHEEL_PIN_1, OUTPUT);
  pinMode(FLYWHEEL_PIN_2, OUTPUT);

  // Configure PWM channels using named constants
  ledcSetup(DRIVE_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(DRIVE_PWM, DRIVE_CH);

  ledcSetup(S2S_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(S2S_PWM, S2S_CH);

  ledcSetup(FLYWHEEL_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(FLYWHEEL_PWM, FLYWHEEL_CH);
}


void brakeDrive() {
  digitalWrite(DRIVE_PIN_1, LOW);
  digitalWrite(DRIVE_PIN_1, LOW);
  ledcWrite(DRIVE_CH, 0);
}
void brakeS2S() {
  digitalWrite(S2S_PIN_1, LOW);
  digitalWrite(S2S_PIN_2, LOW);
  ledcWrite(S2S_CH, 0);
}
void brakeFlywheel() {
  digitalWrite(FLYWHEEL_PIN_1, LOW);
  digitalWrite(FLYWHEEL_PIN_1, LOW);
  ledcWrite(FLYWHEEL_CH, 0);
}

void driveFromStickY(int8_t ly) {

  // Base speed map (-127..127 -> -255..255)
  int speed = map(ly, -127, 127, 255, -255);
  speed = constrain(speed, -255, 255);

  // SCALE WITH L2 → half speed at L2 = 0
  float throttleScale = 0.5 + (driveController.L2 / 255.0f) * 0.5;
  speed = speed * throttleScale;     // new scaled speed
  speed = constrain(speed, -255, 255);

  if (abs(speed) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(DRIVE_PIN_1, speed < 0 ? LOW : HIGH);
    digitalWrite(DRIVE_PIN_2, speed < 0 ? HIGH : LOW);
    ledcWrite(DRIVE_CH, abs(speed));
    gDriveDuty = abs(speed);
    gDriveDirFwd = (speed > 0);
  } else {
    brakeDrive();
  }
}

void controlS2S(int8_t lx) {
  if (lx == 0) {
    brakeS2S();
    return;
  }

  int speed = map(lx, -127, 127, 255, -255);
  speed = constrain(speed, -255, 255);

  // SCALE WITH L2, same as drive
  float throttleScale = 0.5 + (driveController.L2 / 255.0f) * 0.5;
  speed = speed * throttleScale;
  speed = constrain(speed, -255, 255);

  digitalWrite(S2S_PIN_1, speed > 0 ? LOW : HIGH);
  digitalWrite(S2S_PIN_2, speed > 0 ? HIGH : LOW);
  ledcWrite(S2S_CH, abs(speed));
}

void controlFlywheel(int8_t lx) {
  int speed = map(lx, -127, 127, 255, -255);
  speed = constrain(speed, -255, 255);

  if (abs(speed) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(FLYWHEEL_PIN_1, speed < 0 ? HIGH : LOW);
    digitalWrite(FLYWHEEL_PIN_2, speed < 0 ? LOW : HIGH);
    ledcWrite(FLYWHEEL_CH, abs(speed));
    gFlywheelPWM = abs(speed);
    gFlywheelDirFwd = (speed < 0);
  } else {
    brakeFlywheel();
  }
}



// ------------------- Serial Debug -------------------
#include "SerialCommands.h"

// ------------------- Calibration -------------------
bool s2sCalibrating = false;
unsigned long s2sCalStartMs = 0;
uint32_t s2sCalSamples = 0;
uint64_t s2sCalSumPot = 0;
double s2sCalSumPitch = 0.0, s2sCalSumRoll = 0.0;

void beginS2SCenterCalibration() {
  s2sCalibrating = true;
  s2sCalStartMs = millis();
  s2sCalSamples = 0;
  s2sCalSumPot = 0;
  s2sCalSumPitch = 0.0;
  s2sCalSumRoll = 0.0;
  brakeDrive();
  brakeS2S();
  Serial.println(F("[CAL] Started calibration"));
}

void finishS2SCalibration() {
  s2sCalibrating = false;
  if (s2sCalSamples > 0) {
    int32_t avgPot = (int32_t)(s2sCalSumPot / s2sCalSamples);
    double avgPitch = s2sCalSumPitch / s2sCalSamples;
    double avgRoll = s2sCalSumRoll / s2sCalSamples;
    cfg.potCenter = avgPot;
    cfg.pitchOffset = (float)(-avgPitch);
    cfg.rollOffset = (float)(-avgRoll);
    saveConfig();
    Serial.printf("[CAL] Done. potCenter=%ld pitchOffset=%.3f rollOffset=%.3f\n", (long)avgPot, cfg.pitchOffset, cfg.rollOffset);
  }
}

void serviceS2SCenterCalibration() {
  if (!s2sCalibrating) return;
  s2sCalSumPot += analogRead(34);
  s2sCalSumPitch += mpudata.pitch;
  s2sCalSumRoll += mpudata.roll;
  s2sCalSamples++;
  if (millis() - s2sCalStartMs >= 3000UL) finishS2SCalibration();
}


// Save current offsets and pot center to NVS
void savePrefs() {
  cfg.pitchOffset = -mpudata.pitch;
  cfg.rollOffset = -mpudata.roll;
  cfg.potCenter = analogRead(S2S_POT_PIN);

  // Save current PID tunings using getters
  cfg.driveKp = drivePID.getKp();
  cfg.driveKi = drivePID.getKi();
  cfg.driveKd = drivePID.getKd();

  cfg.s2sKp = s2sPID.getKp();
  cfg.s2sKi = s2sPID.getKi();
  cfg.s2sKd = s2sPID.getKd();

  if (saveConfig()) {
    sendSoundCommand(Coms32u4, sendTo32u4, 5);  // Success sound
    Serial.printf("[SAVE PREFS] pitchOffset=%.2f rollOffset=%.2f potCenter=%d\n",
                  cfg.pitchOffset, cfg.rollOffset, cfg.potCenter);
    Serial.printf("[SAVE PREFS] Drive PID: Kp=%.2f Ki=%.2f Kd=%.2f | S2S PID: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  cfg.driveKp, cfg.driveKi, cfg.driveKd,
                  cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
  } else {
    Serial.println("[ERROR] Failed to save prefs");
  }
}



void resetPrefs() {
  resetConfigToDefaults();
  saveConfig();
  sendSoundCommand(Coms32u4, sendTo32u4, 7);  // Reset sound
  Serial.println("[RESET PREFS] Defaults applied. Rebooting...");
  delay(1000);
  ESP.restart();
}


// ------------------- NVS Config -------------------
bool saveConfig() {
  prefs.begin("drivecfg", false);
  bool ok = (prefs.putBytes("config", &cfg, sizeof(cfg)) == sizeof(cfg));
  prefs.end();
  return ok;
}

bool loadConfig() {
  prefs.begin("drivecfg", true);
  size_t n = prefs.getBytesLength("config");
  bool ok = false;
  if (n == sizeof(cfg)) ok = (prefs.getBytes("config", &cfg, sizeof(cfg)) == sizeof(cfg));
  prefs.end();
  if (!ok) {
    Serial.println(F("[NVS] No valid config, applying defaults"));
    resetConfigToDefaults();
  }
  return ok;
}


void resetConfigToDefaults() {
  strncpy(cfg.revision, "Joe Drive Rev 1.0", sizeof(cfg.revision));
  strncpy(cfg.revisionDate, "2026-01-07", sizeof(cfg.revisionDate));
  cfg.pitchOffset = 0.0f;
  cfg.rollOffset = 0.0f;
  cfg.potCenter = 1500;
  cfg.mpuDeadzone = 3.0f;
  cfg.cfgVersion = 0x00010001u;

  // PID defaults
  cfg.driveKp = DEFAULT_DRIVE_PK;
  cfg.driveKi = DEFAULT_DRIVE_KI;
  cfg.driveKd = DEFAULT_DRIVE_KD;

  cfg.s2sKp = DEFAULT_S2S_PK;
  cfg.s2sKi = DEFAULT_S2S_KI;
  cfg.s2sKd = DEFAULT_S2S_KD;
}


// ------------------- Callbacks -------------------
void onConnectedGamepad(GamepadPtr gp) {
  uint8_t mac[6];
  memcpy(mac, gp->getProperties().btaddr, 6);

  Serial.printf("Controller connected: MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  bool driveMacUnset = memcmp(DEFAULT_PREF_DRIVE_MAC, "\0\0\0\0\0\0", 6) == 0;
  bool domeMacUnset = memcmp(DEFAULT_PREF_DOME_MAC, "\0\0\0\0\0\0", 6) == 0;

  // Preferred Drive Controller
  if (!driveMacUnset && memcmp(mac, DEFAULT_PREF_DRIVE_MAC, 6) == 0) {
    if (myControllers[0] != gp) {
      // If slot 0 is occupied, move that controller to slot 1
      if (myControllers[0] && myControllers[0] != gp) {
        myControllers[1] = myControllers[0];
        Serial.println("[INFO] Previous controller moved to DOME slot");
      }
      myControllers[0] = gp;
      Serial.println("[INFO] Assigned to DRIVE slot (preferred MAC)");
    }
    return;
  }

  // Preferred Dome Controller
  if (!domeMacUnset && memcmp(mac, DEFAULT_PREF_DOME_MAC, 6) == 0) {
    if (myControllers[0] == nullptr) {
      // No Drive controller yet → assign Dome as temporary Drive
      myControllers[0] = gp;
      Serial.println("[INFO] Dome controller assigned to DRIVE slot (temporary)");
    } else {
      // Drive slot already taken → assign Dome to slot 1
      myControllers[1] = gp;
      Serial.println("[INFO] Assigned to DOME slot (preferred MAC)");
    }
    return;
  }

  // Fallback logic
  if (myControllers[0] == nullptr) {
    myControllers[0] = gp;
    Serial.println("[INFO] Assigned to DRIVE slot (fallback)");
  } else if (myControllers[1] == nullptr) {
    myControllers[1] = gp;
    Serial.println("[INFO] Assigned to DOME slot (fallback)");
  } else {
    Serial.println("[INFO] Both slots occupied, ignoring extra controller");
  }
}



void onDisconnectedGamepad(GamepadPtr gp) {
  bool wasDrive = (myControllers[0] == gp);
  bool wasDome = (myControllers[1] == gp);

  // Clear the slot for the disconnected controller
  if (wasDrive) {
    myControllers[0] = nullptr;
    Serial.println("[INFO] DRIVE controller disconnected");
  }
  if (wasDome) {
    myControllers[1] = nullptr;
    Serial.println("[INFO] DOME controller disconnected");
  }

  // Reassign if needed
  if (wasDrive) {
    // If Drive controller disconnected, try to promote Dome controller to slot 0
    if (myControllers[1] != nullptr) {
      myControllers[0] = myControllers[1];
      myControllers[1] = nullptr;
      Serial.println("[INFO] Dome controller promoted to DRIVE slot");
    }
  }

  // Debug which slots are now free
  if (myControllers[0] == nullptr) {
    Serial.println("[INFO] DRIVE slot is now free");
  }
  if (myControllers[1] == nullptr) {
    Serial.println("[INFO] DOME slot is now free");
  }
}


void onEspNowSend(const uint8_t* mac, esp_now_send_status_t status) {
  
    lastSendPending = false;
    lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  if (debugDome) {
  Serial.print("[ESP-NOW] Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
  }

}

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(struct_messagedome)) {
    struct_messagedome temp;
    memcpy(&temp, data, sizeof(temp));
    uint16_t calc = calculateChecksumDome(temp);
    if (calc != temp.checksum) {
      Serial.println("[ESP-NOW ERROR] Checksum mismatch!");
      return;
    }
    domeData = temp;
    lastBatteryVoltage = domeData.bat;
  }
}


// ------------------- Controller Update -------------------
void updateControllers() {
  driveController.update(myControllers[0]);
  domeController.update(myControllers[1]);
}

// ------------------- Sound Triggers -------------------
inline void handleSoundTriggers() {
  static uint16_t lastSoundCmdSent = SOUND_NONE;

  // Resolve sound command from controllers
  uint16_t cmd = resolveDriveControllerSound(driveController);
  if (cmd == SOUND_NONE) {
    cmd = resolveDomeControllerSound(domeController);
  }
  if (cmd != SOUND_NONE && cmd != lastSoundCmdSent) {
    sendSoundCommand(Coms32u4, sendTo32u4, cmd);
    lastSoundCmdSent = cmd;
  }
  // Reset lastSoundCmdSent when no button is pressed
  if (cmd == SOUND_NONE) {
    lastSoundCmdSent = SOUND_NONE;
  }
}


// ------------------- Motor Logic -------------------

void handleMotorControl() {
  // Special modes first
  if (domeController.L1.held) {
    brakeDrive();
    brakeS2S();
    controlFlywheel(driveController.joyX);
    sendTo32u4.DomeSpin = 0;
    return;
  }

  if (driveController.L1.held) {
    brakeDrive();
    brakeS2S();
    brakeFlywheel();
    sendTo32u4.DomeSpin = applyDeadzoneAxis(driveController.joyX, DEFAULT_JOY_DEADZONE);
    return;
  }

  // AutoBalance ON
  if (autoBalance && driveEnabled && !s2sCalibrating) {
    float correctedPitch = mpudata.pitch + cfg.pitchOffset;
    float correctedRoll = mpudata.roll + cfg.rollOffset;

    correctedPitch = applyDeadzoneFloat(correctedPitch, cfg.mpuDeadzone);
    correctedRoll = applyDeadzoneFloat(correctedRoll, cfg.mpuDeadzone);

    // PID outputs
    float pitchOutput = drivePID.compute(0.0f - correctedPitch);
    float rollOutput = s2sPID.compute(0.0f - correctedRoll);

    int drivePWM = constrain((int)(pitchOutput * DEFAULT_DRIVE_GAIN), -255, 255);
    int s2sPWM = constrain((int)(rollOutput * DEFAULT_DRIVE_GAIN), -255, 255);

    // Optional joystick override
    if (abs(driveController.joyY) > DEFAULT_JOY_DEADZONE) {
      int joyPWM = map(driveController.joyY, -127, 127, 255, -255);
      drivePWM = joyPWM;  // Full override (or blend if preferred)
    }
    if (abs(driveController.joyX) > DEFAULT_JOY_DEADZONE) {
      int joyPWM = map(driveController.joyX, -127, 127, 255, -255);
      s2sPWM = joyPWM;  // Full override
    }

    // Apply Drive
    if (abs(drivePWM) > DEFAULT_JOY_DEADZONE) {
      digitalWrite(DRIVE_PIN_1, drivePWM > 0 ? HIGH : LOW);
      digitalWrite(DRIVE_PIN_2, drivePWM > 0 ? LOW : HIGH);
      ledcWrite(DRIVE_CH, abs(drivePWM));
      gDriveDuty = abs(drivePWM);
      gDriveDirFwd = (drivePWM > 0);
    } else {
      brakeDrive();
    }

    // Apply S2S
    if (abs(s2sPWM) > DEFAULT_JOY_DEADZONE) {
      digitalWrite(S2S_PIN_1, s2sPWM > 0 ? HIGH : LOW);
      digitalWrite(S2S_PIN_2, s2sPWM > 0 ? LOW : HIGH);
      ledcWrite(S2S_CH, abs(s2sPWM));
      gS2SDuty = abs(s2sPWM);
      gS2SDirFwd = (s2sPWM > 0);
    } else {
      brakeS2S();
    }

    sendTo32u4.DomeSpin = 0;
    return;  // Skip normal joystick logic
  }

  // AutoBalance OFF → joystick + auto-level for S2S
  if (driveEnabled && !s2sCalibrating) {
    // Drive from joystick
    driveFromStickY(driveController.joyY);

    // S2S logic
    int8_t lx = driveController.joyX;
    int potValue = analogRead(S2S_POT_PIN);

    if (abs(lx) > DEFAULT_JOY_DEADZONE) {
      // Joystick actively controlling S2S
      controlS2S(lx);
    } else {
      // Joystick neutral → auto-center
      int error = cfg.potCenter - potValue;

      if (abs(error) > 5) {
        int correction = (abs(error) > 50) ? error * 2 : error;
        correction = constrain(correction, -255, 255);

        digitalWrite(S2S_PIN_1, correction > 0 ? HIGH : LOW);
        digitalWrite(S2S_PIN_2, correction > 0 ? LOW : HIGH);
        ledcWrite(S2S_CH, abs(correction));
      } else {
        brakeS2S();
      }
    }

    brakeFlywheel();
    sendTo32u4.DomeSpin = 0;
  } else {
    brakeDrive();
    brakeS2S();
    brakeFlywheel();
    sendTo32u4.DomeSpin = 0;
  }
}

// ------------------- Lights -------------------
void handleDomeAndBodyLights() {
  static struct_messagedome lastSentData;  // Track last sent state

  // Update domeData based on current state
  domeData.psi = recFrom32u4.isplaying ? 1 : 0;

  // Animation logic
  if (driveController.cross.pressed) domeData.anim = 1;
  else if (driveController.circle.pressed) domeData.anim = 2;
  else if (driveController.L1.pressed) domeData.anim = 3;
  else if (driveController.dpadUp.pressed) domeData.anim = 4;
  else if (driveController.dpadDown.pressed) domeData.anim = 5;
  else if (driveController.dpadLeft.pressed) domeData.anim = 6;
  else if (driveController.dpadRight.pressed) domeData.anim = 7;
  else domeData.anim = 0;

  domeData.bat = lastBatteryVoltage;  // Optional: send battery voltage
  domeData.checksum = calculateChecksumDome(domeData);

  // ✅ Only send if data changed
  if (memcmp(&domeData, &lastSentData, sizeof(domeData)) != 0) {
    esp_err_t result = ESP_FAIL;

    // ✅ Retry up to 3 times if send fails
    for (int i = 0; i < 3; i++) {
      result = esp_now_send(domeMACAddress, (uint8_t*)&domeData, sizeof(domeData));
      if (result == ESP_OK) break;
      delay(10);  // Short pause before retry
    }

    // ✅ Debug output
    if (debugDome || debugAll) {
      Serial.println(F("[ESP-NOW] Sending updated domeData:"));
      Serial.printf("  PSI Flicker: %s\n", domeData.psi ? "ON" : "OFF");
      Serial.printf("  Animation: %d\n", domeData.anim);
      Serial.printf("  Battery: %.2f V\n", domeData.bat);
      Serial.printf("  Checksum: 0x%04X\n", domeData.checksum);
      Serial.printf("  Send status: %s\n", (result == ESP_OK) ? "SUCCESS" : "FAIL");
    }

    // ✅ Update last sent state
    lastSentData = domeData;
  }
}

// ------------------- Auto Balance  -------------------

void autoBalanceControl(float correctedPitch, float correctedRoll) {
  unsigned long now = millis();
  float dt = (now - lastBalanceUpdate) / 1000.0f;  // seconds
  if (dt <= 0) dt = 0.01f;                         // Prevent division by zero
  lastBalanceUpdate = now;

  // Compute errors (target = 0 degrees)
  float pitchError = 0.0f - correctedPitch;  // Pitch controls Drive
  float rollError = 0.0f - correctedRoll;    // Roll controls S2S

  // Integrate errors
  pitchErrorSum += pitchError * dt;
  rollErrorSum += rollError * dt;

  // Derivative terms
  float pitchDerivative = (pitchError - lastPitchError) / dt;
  float rollDerivative = (rollError - lastRollError) / dt;

  lastPitchError = pitchError;
  lastRollError = rollError;

  // PID outputs
  float pitchOutput = (driveKp * pitchError) + (driveKi * pitchErrorSum) + (driveKd * pitchDerivative);
  float rollOutput = (s2sKp * rollError) + (s2sKi * rollErrorSum) + (s2sKd * rollDerivative);

  // Map outputs to motor PWM
  // int drivePWM = constrain((int)pitchOutput, -255, 255);
  int drivePWM = constrain((int)(pitchOutput * DEFAULT_DRIVE_GAIN), -255, 255);
  int s2sPWM = constrain((int)rollOutput, -255, 255);

  // Apply Drive motor correction
  if (abs(drivePWM) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(DRIVE_PIN_1, drivePWM > 0 ? HIGH : LOW);
    digitalWrite(DRIVE_PIN_2, drivePWM > 0 ? LOW : HIGH);
    ledcWrite(DRIVE_CH, abs(drivePWM));
  } else {
    brakeDrive();
  }

  // Apply S2S motor correction
  if (abs(s2sPWM) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(S2S_PIN_1, s2sPWM > 0 ? HIGH : LOW);
    digitalWrite(S2S_PIN_2, s2sPWM > 0 ? LOW : HIGH);
    ledcWrite(S2S_CH, abs(s2sPWM));
  } else {
    brakeS2S();
  }
}



// ------------------- IMU -------------------

inline void updateIMUValues() {
  float correctedPitch = mpudata.pitch + cfg.pitchOffset;
  float correctedRoll = mpudata.roll + cfg.rollOffset;

  correctedPitch = applyDeadzoneFloat(correctedPitch, cfg.mpuDeadzone);
  correctedRoll = applyDeadzoneFloat(correctedRoll, cfg.mpuDeadzone);

  sendTo32u4.pitch = correctedPitch;
  sendTo32u4.roll = correctedRoll;

  if (autoBalance) {
    autoBalanceControl(correctedPitch, correctedRoll);
  }
}



// inline void updateIMUValues() {
//     // Compute corrected values
//     float correctedPitch = mpudata.pitch + cfg.pitchOffset;
//     float correctedRoll  = mpudata.roll + cfg.rollOffset;

//     // Apply deadzone
//     correctedPitch = applyDeadzoneFloat(correctedPitch, cfg.mpuDeadzone);
//     correctedRoll  = applyDeadzoneFloat(correctedRoll, cfg.mpuDeadzone);

//     // Assign to sendTo32u4 for transmission
//     sendTo32u4.pitch = correctedPitch;
//     sendTo32u4.roll  = correctedRoll;

//     // If autoBalance is enabled, use these values for control logic
//     if (autoBalance) {
//         // Example: PID or motor correction logic
//         // autoBalanceControl(correctedPitch, correctedRoll);
//     }
// }


// inline void updateIMUValues() {
//   sendTo32u4.pitch = applyDeadzoneFloat(mpudata.pitch + cfg.pitchOffset, cfg.mpuDeadzone);
//   sendTo32u4.roll = applyDeadzoneFloat(mpudata.roll + cfg.rollOffset, cfg.mpuDeadzone);

// //   float correctedPitch = mpudata.pitch + cfg.pitchOffset;
// //   float correctedRoll = mpudata.roll + cfg.rollOffset;
// }

// ------------------- Setup -------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== BB-8 Drive System Starting ==="));

  bootMs = millis();

  // Display right after reboot (every boot)
  showBuildInfoSerial("BOOT");

  // Load configuration from NVS or apply defaults
  if (loadConfig()) {
    drivePID.setTunings(cfg.driveKp, cfg.driveKi, cfg.driveKd);
    s2sPID.setTunings(cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);

    Serial.printf("[PID] Loaded Drive PID: Kp=%.2f Ki=%.2f Kd=%.2f\n", cfg.driveKp, cfg.driveKi, cfg.driveKd);
    Serial.printf("[PID] Loaded S2S PID: Kp=%.2f Ki=%.2f Kd=%.2f\n", cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
    Serial.println(F("[NVS] Config loaded successfully"));
  } else {
    Serial.println(F("[NVS] No valid config found, using defaults"));
    saveConfig();
  }

#ifdef analogReadResolution
  analogReadResolution(12);
#endif

  // Bluetooth coexistence preference
  esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

  // Initialize Bluepad32 for gamepad input
  BP32.setup(&onConnectedGamepad, &onDisconnectedGamepad);

  // Clear old Bluetooth keys
  BP32.forgetBluetoothKeys();
  Serial.println("[BT] Cleared old Bluetooth keys");

  // Temporarily block new connections for 5 seconds
  BP32.enableNewBluetoothConnections(false);
  Serial.println("[BT] Blocking new connections for 5s...");
  delay(5000);
  BP32.enableNewBluetoothConnections(true);
  Serial.println("[BT] New connections enabled");

  // ✅ Lock Wi-Fi channel AFTER Bluetooth setup
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Print host Bluetooth MAC for pairing
  uint8_t bt_mac[6];
  esp_read_mac(bt_mac, ESP_MAC_BT);
  char hostMacStr[18];
  sprintf(hostMacStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);
  Serial.printf("[BT] Host MAC: %s\n", hostMacStr);

  // Initialize SerialTransfer for communication with 32u4 and Trinket
  Serial2.begin(74880, SERIAL_8N1, 13, 12);  // RX/TX pins for 32u4 -- 74800
  Coms32u4.begin(Serial2);

  Serial1.begin(115200);  // For Trinket IMU
  ComsTrinket.begin(Serial1);
  

#if ENABLE_ESPNOW
  WiFi.mode(WIFI_STA);
    // In setup(), after WiFi.mode(WIFI_STA):
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Max TX power for better reliability
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);  // Try channel 11 (or 13) to reduce BT interference
  // Print drive's WiFi MAC for verification
  uint8_t wifiMac[6];
  WiFi.macAddress(wifiMac);
  Serial.printf("[ESP-NOW] Drive WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);
  Serial.println("[ESP-NOW] Verify this matches dome's masterMAC[]. Dome MAC should match drive's domeMACAddress[].");
  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb(onEspNowSend);
    // esp_now_register_recv_cb(onEspNowRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, domeMACAddress, 6);
    peer.channel = WIFI_CHANNEL;  // Match dome channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) == ESP_OK) {
      Serial.println("[ESP-NOW] Peer added successfully");
    } else {
      Serial.println("[ESP-NOW] Failed to add peer");
    }

    Serial.println("[ESP-NOW] Ready");

  } else {
    Serial.println("[ESP-NOW] Init failed");
  }
#endif

  // Initialize motors
  initMotors();
  Serial.print("[DIAG] sizeof(send32u4) = ");
Serial.println(sizeof(send32u4));

  Serial.println(F("[CAL] Boot calibration pending..."));
}

void handleControllerCombos() {
  static bool psDriveLock = false;
  static bool psDomeLock = false;
  static bool crossDriveLock = false;

  // Toggle Drive Enable with Drive Controller PS
  if (driveController.ps.pressed && !psDriveLock) {
    driveEnabled = !driveEnabled;
    Serial.printf("[TOGGLE] Drive %s\n", driveEnabled ? "ENABLED" : "DISABLED");

    // If drive is disabled, reset dependent states
    if (!driveEnabled) {
      autoBalance = false;          // Reset AutoBalance
      domeFunctionEnabled = false;  // Reset Dome Function
      Serial.println("[RESET] AutoBalance and DomeFunction disabled due to Drive OFF");
    }

    psDriveLock = true;
  }
  if (!driveController.ps.held) psDriveLock = false;

  // Toggle Dome Function with Dome Controller PS
  if (domeController.ps.pressed && !psDomeLock) {
    domeFunctionEnabled = !domeFunctionEnabled;
    Serial.printf("[TOGGLE] Dome Function %s\n", domeFunctionEnabled ? "ENABLED" : "DISABLED");
    psDomeLock = true;
  }
  if (!domeController.ps.held) psDomeLock = false;

  // Toggle Auto Balance with Drive Controller CROSS
  if (driveController.cross.pressed && !crossDriveLock) {
    autoBalance = !autoBalance;
    Serial.printf("[TOGGLE] Auto Balance %s\n", autoBalance ? "ENABLED" : "DISABLED");
    crossDriveLock = true;
  }
  if (!driveController.cross.held) crossDriveLock = false;

  // Flywheel mode from Dome Controller L1 (held)
  flywheelMode = domeController.L1.held ? true : false;

  if (bothControllersUpHeld(driveController, domeController)) {
    savePrefs();  // Save current offsets and pot center
  }

  if (bothControllersDownHeld(driveController, domeController)) {
    resetPrefs();  // Reset to defaults and reboot
  }

  if (driveController.L1.held && driveController.dpadUp.held) {
    if (comboStartTime == 0) comboStartTime = millis();
    if (millis() - comboStartTime >= 3000 && !pidTuneMode) {
      pidTuneMode = true;
      tuningDrivePID = true;
      tuningStep = 0;
      Serial.println("[PID TUNE] Drive PID tuning mode activated.");
      lastInputTime = millis();
    }
  } else if (driveController.L1.held && driveController.dpadLeft.held) {
    if (comboStartTime == 0) comboStartTime = millis();
    if (millis() - comboStartTime >= 3000 && !pidTuneMode) {
      pidTuneMode = true;
      tuningS2SPID = true;
      tuningStep = 0;
      Serial.println("[PID TUNE] S2S PID tuning mode activated.");
      lastInputTime = millis();
    }
  } else {
    comboStartTime = 0;  // reset if combo released
  }
}


void handlePIDTuning() {
  if (!pidTuneMode) return;

  // Ignore other combo checks during tuning
  // Ensure motors stay active for feedback
  float correctedPitch = mpudata.pitch + cfg.pitchOffset;
  float correctedRoll = mpudata.roll + cfg.rollOffset;

  correctedPitch = applyDeadzoneFloat(correctedPitch, cfg.mpuDeadzone);
  correctedRoll = applyDeadzoneFloat(correctedRoll, cfg.mpuDeadzone);

  float pitchOutput = drivePID.compute(0.0f - correctedPitch);
  float rollOutput = s2sPID.compute(0.0f - correctedRoll);

  int drivePWM = constrain((int)(pitchOutput * DEFAULT_DRIVE_GAIN), -255, 255);
  int s2sPWM = constrain((int)(rollOutput * DEFAULT_DRIVE_GAIN), -255, 255);

  // Apply motors so you see response
  if (abs(drivePWM) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(DRIVE_PIN_1, drivePWM > 0 ? HIGH : LOW);
    digitalWrite(DRIVE_PIN_2, drivePWM > 0 ? LOW : HIGH);
    ledcWrite(DRIVE_CH, abs(drivePWM));
  } else brakeDrive();

  if (abs(s2sPWM) > DEFAULT_JOY_DEADZONE) {
    digitalWrite(S2S_PIN_1, s2sPWM > 0 ? HIGH : LOW);
    digitalWrite(S2S_PIN_2, s2sPWM > 0 ? LOW : HIGH);
    ledcWrite(S2S_CH, abs(s2sPWM));
  } else brakeS2S();

  // Timeout check
  if (millis() - lastInputTime > 5000) {
    Serial.println("\n[PID TUNE] Timeout. Cancelling tuning.");
    pidTuneMode = false;
    tuningDrivePID = tuningS2SPID = false;
    return;
  }

  PIDController* targetPID = tuningDrivePID ? &drivePID : &s2sPID;
  const char* pidName = tuningDrivePID ? "Drive" : "S2S";
  float currentVal = 0;

  // Static flag for one-time prompt per step
  static bool promptShown = false;

  switch (tuningStep) {
    case 0:
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] %s PID tuning mode.\n", pidName);
        Serial.println("Configure PID? Press CROSS to continue or wait to cancel.");
        promptShown = true;
      }
      if (driveController.cross.pressed) {
        tuningStep = 1;
        lastInputTime = millis();
        Serial.println("[PID TUNE] Starting Kp adjustment...");
        promptShown = false;  // Reset for next stage
      }
      break;

    case 1:  // Kp
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Kp: %.2f\n", targetPID->getKp());
        Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and continue.");
        promptShown = true;
      }
      currentVal = targetPID->getKp();
      if (driveController.dpadUp.pressed) {
        currentVal += 0.5;
        targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
        Serial.printf("[PID TUNE] Kp increased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal -= 0.5;
        if (currentVal < 0) currentVal = 0;
        targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
        Serial.printf("[PID TUNE] Kp decreased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKp = currentVal;
        else cfg.s2sKp = currentVal;
        tuningStep = 2;
        Serial.println("[PID TUNE] Kp saved. Moving to Ki...");
        lastInputTime = millis();
        promptShown = false;
      }
      break;

    case 2:  // Ki
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Ki: %.2f\n", targetPID->getKi());
        Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and continue.");
        promptShown = true;
      }
      currentVal = targetPID->getKi();
      if (driveController.dpadUp.pressed) {
        currentVal += 0.05;
        targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
        Serial.printf("[PID TUNE] Ki increased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal -= 0.05;
        if (currentVal < 0) currentVal = 0;
        targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
        Serial.printf("[PID TUNE] Ki decreased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKi = currentVal;
        else cfg.s2sKi = currentVal;
        tuningStep = 3;
        Serial.println("[PID TUNE] Ki saved. Moving to Kd...");
        lastInputTime = millis();
        promptShown = false;
      }
      break;

    case 3:  // Kd
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Kd: %.2f\n", targetPID->getKd());
        Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and finish.");
        promptShown = true;
      }
      currentVal = targetPID->getKd();
      if (driveController.dpadUp.pressed) {
        currentVal += 0.05;
        targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
        Serial.printf("[PID TUNE] Kd increased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal -= 0.05;
        if (currentVal < 0) currentVal = 0;
        targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
        Serial.printf("[PID TUNE] Kd decreased to %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKd = currentVal;
        else cfg.s2sKd = currentVal;
        Serial.println("[PID TUNE] Kd saved. All values updated.");
        savePrefs();
        Serial.printf("[PID TUNE] Final %s PID: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                      pidName, targetPID->getKp(), targetPID->getKi(), targetPID->getKd());
        pidTuneMode = false;
        tuningDrivePID = tuningS2SPID = false;
        promptShown = false;  // Reset for next time
      }
      break;
  }
}


// void handlePIDTuning() {
//   if (!pidTuneMode) return;

//   // Timeout check
//   if (millis() - lastInputTime > 5000) {
//     Serial.println("\n[PID TUNE] Timeout. Cancelling tuning.");
//     pidTuneMode = false;
//     tuningDrivePID = tuningS2SPID = false;
//     return;
//   }

//   PIDController* targetPID = tuningDrivePID ? &drivePID : &s2sPID;
//   const char* pidName = tuningDrivePID ? "Drive" : "S2S";
//   float currentVal = 0;

//   switch (tuningStep) {
//     case 0:
//       Serial.printf("\n[PID TUNE] %s PID tuning mode.\n", pidName);
//       Serial.println("Configure PID? Press CROSS to continue or wait to cancel.");
//       if (driveController.cross.pressed) {
//         tuningStep = 1;
//         lastInputTime = millis();
//         Serial.println("[PID TUNE] Starting Kp adjustment...");
//       }
//       break;

//     case 1:  // Kp
//       currentVal = targetPID->getKp();
//       Serial.printf("\n[PID TUNE] Adjust Kp: %.2f\n", currentVal);
//       Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and continue.");
//       if (driveController.dpadUp.pressed) {
//         currentVal += 0.5;
//         targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
//         Serial.printf("[PID TUNE] Kp increased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.dpadDown.pressed) {
//         currentVal -= 0.5;
//         if (currentVal < 0) currentVal = 0;
//         targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
//         Serial.printf("[PID TUNE] Kp decreased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.cross.pressed) {
//         if (tuningDrivePID) cfg.driveKp = currentVal;
//         else cfg.s2sKp = currentVal;
//         tuningStep = 2;
//         Serial.println("[PID TUNE] Kp saved. Moving to Ki...");
//         lastInputTime = millis();
//       }
//       break;

//     case 2:  // Ki
//       currentVal = targetPID->getKi();
//       Serial.printf("\n[PID TUNE] Adjust Ki: %.2f\n", currentVal);
//       Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and continue.");
//       if (driveController.dpadUp.pressed) {
//         currentVal += 0.05;
//         targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
//         Serial.printf("[PID TUNE] Ki increased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.dpadDown.pressed) {
//         currentVal -= 0.05;
//         if (currentVal < 0) currentVal = 0;
//         targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
//         Serial.printf("[PID TUNE] Ki decreased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.cross.pressed) {
//         if (tuningDrivePID) cfg.driveKi = currentVal;
//         else cfg.s2sKi = currentVal;
//         tuningStep = 3;
//         Serial.println("[PID TUNE] Ki saved. Moving to Kd...");
//         lastInputTime = millis();
//       }
//       break;

//     case 3:  // Kd
//       currentVal = targetPID->getKd();
//       Serial.printf("\n[PID TUNE] Adjust Kd: %.2f\n", currentVal);
//       Serial.println("Use D-Pad UP/DOWN to change value. Press CROSS to save and finish.");
//       if (driveController.dpadUp.pressed) {
//         currentVal += 0.05;
//         targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
//         Serial.printf("[PID TUNE] Kd increased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.dpadDown.pressed) {
//         currentVal -= 0.05;
//         if (currentVal < 0) currentVal = 0;
//         targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
//         Serial.printf("[PID TUNE] Kd decreased to %.2f\n", currentVal);
//         lastInputTime = millis();
//       }
//       if (driveController.cross.pressed) {
//         if (tuningDrivePID) cfg.driveKd = currentVal;
//         else cfg.s2sKd = currentVal;
//         Serial.println("[PID TUNE] Kd saved. All values updated.");
//         savePrefs();
//         Serial.printf("[PID TUNE] Final %s PID: Kp=%.2f Ki=%.2f Kd=%.2f\n",
//                       pidName, targetPID->getKp(), targetPID->getKi(), targetPID->getKd());
//         pidTuneMode = false;
//         tuningDrivePID = tuningS2SPID = false;
//       }
//       break;
//   }
// }



void showControllers() {
  // Update both controllers
  driveController.update(myControllers[0]);
  domeController.update(myControllers[1]);

  Serial.println("==============================================================================================================================");
  Serial.println("Controller | RAWX  RAWY | LX    LY    L2  | PS(H/P/R) L1(H/P/R) CROSS(H/P/R) CIRCLE(H/P/R) UP(H/P/R) DOWN(H/P/R) LEFT(H/P/R) RIGHT(H/P/R)");

  // Drive Controller Row
  Serial.printf("Drive      | %5d %5d | %5d %5d %5d |  %d/%d/%d    %d/%d/%d    %d/%d/%d       %d/%d/%d       %d/%d/%d    %d/%d/%d      %d/%d/%d       %d/%d/%d\n",
                driveController.rawX, driveController.rawY,
                driveController.joyX, driveController.joyY, driveController.L2,
                driveController.ps.held, driveController.ps.pressed, driveController.ps.released,
                driveController.L1.held, driveController.L1.pressed, driveController.L1.released,
                driveController.cross.held, driveController.cross.pressed, driveController.cross.released,
                driveController.circle.held, driveController.circle.pressed, driveController.circle.released,
                driveController.dpadUp.held, driveController.dpadUp.pressed, driveController.dpadUp.released,
                driveController.dpadDown.held, driveController.dpadDown.pressed, driveController.dpadDown.released,
                driveController.dpadLeft.held, driveController.dpadLeft.pressed, driveController.dpadLeft.released,
                driveController.dpadRight.held, driveController.dpadRight.pressed, driveController.dpadRight.released);

  // Dome Controller Row
  Serial.printf("Dome       | %5d %5d | %5d %5d %5d |  %d/%d/%d    %d/%d/%d    %d/%d/%d       %d/%d/%d       %d/%d/%d    %d/%d/%d      %d/%d/%d       %d/%d/%d\n",
                domeController.rawX, domeController.rawY,
                domeController.joyX, domeController.joyY, domeController.L2,
                domeController.ps.held, domeController.ps.pressed, domeController.ps.released,
                domeController.L1.held, domeController.L1.pressed, domeController.L1.released,
                domeController.cross.held, domeController.cross.pressed, domeController.cross.released,
                domeController.circle.held, domeController.circle.pressed, domeController.circle.released,
                domeController.dpadUp.held, domeController.dpadUp.pressed, domeController.dpadUp.released,
                domeController.dpadDown.held, domeController.dpadDown.pressed, domeController.dpadDown.released,
                domeController.dpadLeft.held, domeController.dpadLeft.pressed, domeController.dpadLeft.released,
                domeController.dpadRight.held, domeController.dpadRight.pressed, domeController.dpadRight.released);

  Serial.println("==============================================================================================================================\n");
}


void sendDomeDataUntilSuccess() {
    static unsigned long lastAttempt = 0;

    // If last send was successful, do nothing
    if (lastSendSuccess) return;

    // If not pending and enough time passed, retry
    if (!lastSendPending && millis() - lastAttempt >= RETRY_INTERVAL_MS) {
        esp_err_t result = esp_now_send(domeMACAddress, (uint8_t*)&domeData, sizeof(domeData));
        if (result == ESP_OK) {
            lastSendPending = true;
        }
        lastAttempt = millis();
    }
}

void sendTo32u4Data() {
  // Always update all fields
  sendTo32u4.driveEnabled = driveEnabled ? 1 : 0;
  sendTo32u4.autoBalance = autoBalance ? 1 : 0;
  sendTo32u4.domeFunction = domeFunctionEnabled ? 1 : 0;

  // DomeSpin is now controlled in handleMotorControl(), so just keep its current value
  // Joystick values from Dome Controller for tilt
  sendTo32u4.leftStickX = domeController.joyX;
  sendTo32u4.leftStickY = domeController.joyY;

  // Always send pitch and roll (with deadzone applied)
  sendTo32u4.pitch = applyDeadzoneFloat(mpudata.pitch + cfg.pitchOffset, cfg.mpuDeadzone);
  sendTo32u4.roll = applyDeadzoneFloat(mpudata.roll + cfg.rollOffset, cfg.mpuDeadzone);

  // Compute checksum before sending
  sendTo32u4.checksum = calculateChecksumSend(sendTo32u4);

  // Send packet to 32u4
  uint16_t bytes = Coms32u4.txObj(sendTo32u4, 0);
  if (!Coms32u4.sendData(bytes)) {
    Serial.println("[ERROR] 32u4 TX failed");
  }

  // Debug output if enabled
  if (debugTo32u4) {
    Serial.printf("[TO 32u4] driveEnabled=%d autoBalance=%d domeFunction=%d DomeSpin=%d LX=%d LY=%d pitch=%.2f roll=%.2f soundcmd=%d\n",
                  sendTo32u4.driveEnabled, sendTo32u4.autoBalance, sendTo32u4.domeFunction,
                  sendTo32u4.DomeSpin, sendTo32u4.leftStickX, sendTo32u4.leftStickY,
                  sendTo32u4.pitch, sendTo32u4.roll, sendTo32u4.soundcmd);
  }

  // Reset soundcmd after sending (debounced elsewhere)
  sendTo32u4.soundcmd = SOUND_NONE;
}



void receiveFrom32u4() {
  if (Coms32u4.available()) {
    Coms32u4.rxObj(recFrom32u4);
    isPlaying = recFrom32u4.isplaying;
  } else if (Coms32u4.status < 0) {
    Serial.print("[ERROR] 32u4 RX: ");
    if (Coms32u4.status == CRC_ERROR) Serial.println("CRC_ERROR");
    else if (Coms32u4.status == PAYLOAD_ERROR) Serial.println("PAYLOAD_ERROR");
    else Serial.printf("Unknown error code: %d\n", Coms32u4.status);
  } else if (Coms32u4.status == CRC_ERROR) {
    Serial.println("[ERROR] 32u4 CRC_ERROR");
  }
}


void receiveFromTrinket() {
  static unsigned long lastErrorPrint = 0;
  static int crcErrorCount = 0;

  if (ComsTrinket.available()) {
    ComsTrinket.rxObj(mpudata);
    imuHasSample = true;
    lastIMUUpdate = millis();  // Track last IMU update
  } else if (ComsTrinket.status == CRC_ERROR) {
    crcErrorCount++;
    if (millis() - lastErrorPrint > 5000) {  // Print every 5 sec
      Serial.printf("[ERROR] Trinket CRC_ERROR (count=%d)\n", crcErrorCount);
      lastErrorPrint = millis();
    }
  }
}


void showBuildInfoSerial(const char* prefix)
{
  Serial.print(prefix);
  Serial.print(" | ");
  Serial.print(DEFAULT_REVISION);
  Serial.print(" | ");
  Serial.println(DEFAULT_REVISION_DATE);
}



void printDebugInfo() {
  // Debug 32u4

  // Debug TO 32u4
  if (debugTo32u4) {
    Serial.printf("[TO 32u4] driveEnabled=%d autoBalance=%d domeFunction=%d DomeSpin=%d LX=%d LY=%d pitch=%.2f roll=%.2f soundcmd=%d\n",
                  sendTo32u4.driveEnabled, sendTo32u4.autoBalance, sendTo32u4.domeFunction,
                  sendTo32u4.DomeSpin, sendTo32u4.leftStickX, sendTo32u4.leftStickY,
                  sendTo32u4.pitch, sendTo32u4.roll, sendTo32u4.soundcmd);
  }

  // Debug FROM 32u4
  if (debugFrom32u4) {
    Serial.printf("[FROM 32u4] dometilt=%.2f isplaying=%d domedirection=%d\n",
                  recFrom32u4.dometilt, recFrom32u4.isplaying, recFrom32u4.domedirection);
  }

  // Debug MPU
  if (debugMPU) {
    int potValue = analogRead(S2S_POT_PIN);
    Serial.printf("[MPU] rawX=%.2f rawY=%.2f rawZ=%.2f pitch=%.2f roll=%.2f pot=%d\n",
                  mpudata.rawX, mpudata.rawY, mpudata.rawZ,
                  mpudata.pitch, mpudata.roll, potValue);
  }

  // Debug S2S
  if (debugS2S) {
    int potValue = analogRead(S2S_POT_PIN);
    Serial.printf("[S2S] PWM=%u Dir=%s pot=%d pitch=%.2f roll=%.2f\n",
                  gS2SDuty, (gS2SDirFwd ? "FWD" : "REV"), potValue,
                  mpudata.pitch, mpudata.roll);
  }

  // Debug Drive
  if (debugDrive) {
    Serial.printf("[Drive] PWM=%u Dir=%s pitch=%.2f roll=%.2f\n",
                  gDriveDuty, (gDriveDirFwd ? "FWD" : "REV"),
                  mpudata.pitch, mpudata.roll);
  }

  // Debug Controllers
  if (debugControllersFlag) {
    Serial.println("==============================================================================================================================");
    Serial.println("Controller | RAWX  RAWY | LX    LY    L2  | PS(H/P/R) L1(H/P/R) CROSS(H/P/R) CIRCLE(H/P/R) UP(H/P/R) DOWN(H/P/R) LEFT(H/P/R) RIGHT(H/P/R)");

    Serial.printf("Drive      | %5d %5d | %5d %5d %5d |  %d/%d/%d    %d/%d/%d    %d/%d/%d       %d/%d/%d       %d/%d/%d    %d/%d/%d      %d/%d/%d       %d/%d/%d\n",
                  driveController.rawX, driveController.rawY,
                  driveController.joyX, driveController.joyY, driveController.L2,
                  driveController.ps.held, driveController.ps.pressed, driveController.ps.released,
                  driveController.L1.held, driveController.L1.pressed, driveController.L1.released,
                  driveController.cross.held, driveController.cross.pressed, driveController.cross.released,
                  driveController.circle.held, driveController.circle.pressed, driveController.circle.released,
                  driveController.dpadUp.held, driveController.dpadUp.pressed, driveController.dpadUp.released,
                  driveController.dpadDown.held, driveController.dpadDown.pressed, driveController.dpadDown.released,
                  driveController.dpadLeft.held, driveController.dpadLeft.pressed, driveController.dpadLeft.released,
                  driveController.dpadRight.held, driveController.dpadRight.pressed, driveController.dpadRight.released);

    Serial.printf("Dome       | %5d %5d | %5d %5d %5d |  %d/%d/%d    %d/%d/%d    %d/%d/%d       %d/%d/%d       %d/%d/%d    %d/%d/%d      %d/%d/%d       %d/%d/%d\n",
                  domeController.rawX, domeController.rawY,
                  domeController.joyX, domeController.joyY, domeController.L2,
                  domeController.ps.held, domeController.ps.pressed, domeController.ps.released,
                  domeController.L1.held, domeController.L1.pressed, domeController.L1.released,
                  domeController.cross.held, domeController.cross.pressed, domeController.cross.released,
                  domeController.circle.held, domeController.circle.pressed, domeController.circle.released,
                  domeController.dpadUp.held, domeController.dpadUp.pressed, domeController.dpadUp.released,
                  domeController.dpadDown.held, domeController.dpadDown.pressed, domeController.dpadDown.released,
                  domeController.dpadLeft.held, domeController.dpadLeft.pressed, domeController.dpadLeft.released,
                  domeController.dpadRight.held, domeController.dpadRight.pressed, domeController.dpadRight.released);

    Serial.println("==============================================================================================================================\n");
  }


  if (debugFlywheel) {
    Serial.printf("[Flywheel] PWM=%d Direction=%s Mode=%d\n",
                  gFlywheelPWM, gFlywheelDirFwd ? "FWD" : "REV", flywheelMode);
  }
}



// ------------------- Loop -------------------

void loop() {

  // Display after waiting (once)
  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
  }

  serviceBootCalibration();  // Runs until calibration completes

  // Always update controllers first
  BP32.update();
  printDebugInfo();
  updateControllers();
  handleControllerCombos();

  if (pidTuneMode) {
    handlePIDTuning();
    return;  // Skip normal loop actions while tuning
  }

  printDebugInfo();

  receiveFrom32u4();
  // IMU updates (every 20ms)
  static unsigned long lastIMU = 0;
  if (millis() - lastIMU > 20) {
    updateIMUValues();
    lastIMU = millis();
  }

  // Motor control immediately after controller logic
  handleMotorControl();

  // Sound triggers
  handleSoundTriggers();

#if ENABLE_ESPNOW
  // Dome lights only if ESPNOW enabled
  handleDomeAndBodyLights();
  sendDomeDataUntilSuccess(); // Keep retrying until success
#endif
  // Send to 32u4 (every 50ms)

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    sendTo32u4Data();
    lastSend = millis();
  }

  // Receive IMU data from Trinket (non-blocking)
  if (ComsTrinket.available()) {
    ComsTrinket.rxObj(mpudata);
    imuHasSample = true;
    lastIMUUpdate = millis();  // Track IMU freshness
  } else if (ComsTrinket.status == CRC_ERROR) {
    static unsigned long lastErrorPrint = 0;
    static int crcErrorCount = 0;
    crcErrorCount++;
    if (millis() - lastErrorPrint > 5000) {
      Serial.printf("[ERROR] Trinket CRC_ERROR (count=%d)\n", crcErrorCount);
      lastErrorPrint = millis();
    }
  }
  // Serial command handler
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    handleSerialCommand(cmd);
  }
  vTaskDelay(1);
}
