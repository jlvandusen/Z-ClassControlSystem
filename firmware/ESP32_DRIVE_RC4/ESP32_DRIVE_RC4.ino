// ============================================================
//  ESP32_DRIVE_RC4  —  Drive master (Adafruit HUZZAH32 Feather)
//  Bluepad32 dual controllers, drive + S2S + flywheel motors,
//  SerialTransfer links to 32u4 (body) and Trinket M0 (IMU),
//  ESP-NOW link to dome ESP32.
//
//  RC4 changes vs RC3 (stability / smoothness / latency):
//   1. ONE control path. RC3 ran TWO parallel PID implementations
//      (autoBalanceControl() from the 20ms IMU tick AND the
//      drivePID/s2sPID branch inside handleMotorControl() every
//      loop) that both wrote the same motors with different
//      gains. Removed autoBalanceControl() and its globals.
//   2. Control runs on a fixed 10 ms (100 Hz) tick with measured
//      dt. RC3 computed PID at raw loop rate (~kHz) on 50 Hz IMU
//      data: the derivative was 0 most loops then spiked ~20x on
//      each new sample -> periodic kicks -> "jerky/untunable".
//   3. PIDController rewritten: derivative-on-measurement with
//      low-pass filter, integral anti-windup clamp, output
//      limits, dt supplied by caller.
//   4. Removed the hidden DEFAULT_DRIVE_GAIN x10 multiplier.
//      Your "Kp=15" was really Kp=150 -> full PWM at 1.7 deg,
//      i.e. bang-bang, not PID. Gains are now real PWM/degree.
//   5. Removed the 3-degree IMU deadzone from the CONTROL path
//      (it created a flat spot then a step -> limit cycling).
//      mpuDeadzone now only shapes what is SENT to the 32u4 for
//      dome display tilt.
//   6. S2S is now a CASCADED loop: outer roll PID outputs a
//      target pot position (counts), inner P loop servos the pot
//      to it. Joystick moves the same target -> smooth, bounded,
//      self-centering; stabilization stays active while steering.
//   7. Drive joystick BLENDS with the pitch PID instead of
//      replacing it (RC3 disabled stabilization the moment you
//      touched the stick). Expo curve + slew limiter for feel.
//   8. Non-blocking ESP-NOW. RC3 retried inside the control loop
//      with delay(10) x3 whenever the dome was off/asleep ->
//      ~30 ms stalls of the whole robot, repeatedly.
//   9. Serial links drained every loop (RC3 read at most one
//      packet per loop while the 32u4 flooded the link).
//  10. L2 throttle scale fixed: Bluepad32 brake() is 0..1023,
//      RC3 divided by 255 (saturated at ~25% pull).
//  11. BT keys are no longer wiped every boot (serial cmd
//      'bt forget' instead) and the blocking 5 s window is gone
//      -> controllers reconnect fast.
//  12. Telemetry stream ('telemetry on'): 20 Hz name:value line
//      for the Arduino Serial Plotter / BB8 Commander tuning.
//  13. Fixed brakeDrive()/brakeFlywheel() writing PIN_1 twice
//      and never clearing PIN_2.
//  14. cfgVersion bumped; PID fields auto-migrate to RC4
//      defaults on first boot (offsets/potCenter preserved).
//  15. BT/ESP-NOW radio collisions: coexistence now BALANCE (was
//      PREFER_WIFI = dome lights beat your controllers), WiFi TX
//      power 11dBm (was 19.5), and ESP-NOW traffic cut from a
//      continuous retry storm to change-driven + 1Hz heartbeat.
//
//  RC4.3 (2026-08-22) audio + controls
//   - PS is the only drive enable (tap) / force-disable (hold 2 s);
//     CIRCLE = sound 28. Pad connect -> track 1, pad loss -> track 100,
//     boot-cal done -> track 6 (pref sndcal, 0 = silent). pref snd* and
//     pref swing persist in NVS. Link control codes 125/126/127 (tracks
//     1..119). Integrating button debounce (45 ms), sounds only from a
//     connected+armed pad, randomSeed(esp_random()).
//  RC4.4 (2026-08-22) wireless console tunnel
//   - TeeSerial wraps Serial: console mirrored to the dome as TunnelOut
//     packets while armed; TunnelCmd lines from the dome are injected
//     into the normal parser. esp_now_set_wake_window(65535) so the
//     dome's packets are heard despite BT-mandated modem sleep.
//  RC4.5 (2026-08-23) dome motion lean
//   - send32u4.drivePwm = slewed commanded throttle, for the body's
//     'tilt lean' (dome tilts against the travel direction).
// ============================================================

#include <Arduino.h>
#include <SerialTransfer.h>
#include "ConfigTypes.h"
#include "BuildStamp.h"
#include <esp_now.h>
#include "esp_wifi.h"
#include <WiFi.h>
#include <Bluepad32.h>
#include <esp_coexist.h>
#include <Preferences.h>
#include "ControllerState.h"
#include "SoundMapping.h"
#include "PIDController.h"

// ================= RC4.4: ESP-NOW console tunnel (sealed-shell tuning) =====
// The dome (on the bench, USB to the PC) bridges bb8 <-> ESP-NOW <-> drive:
// unknown dome-console lines arrive here as TunnelCmd packets and are fed to
// the normal command parser; EVERYTHING this sketch prints is mirrored back
// as TunnelOut packets while the tunnel is armed (any TunnelCmd arms it for
// 60 s; the dome keepalives while bb8 is attached). `Serial` below is a tee
// wrapper — sketch code needs no changes.  Structs are hand-mirrored in
// ESP32_DOME_RC4.ino; sizes are the discriminator, keep them distinct.
struct __attribute__((packed)) TunnelCmd { uint8_t type, seq, len; char data[180]; uint16_t checksum; };
struct __attribute__((packed)) TunnelOut { uint8_t type, seq, len; char data[236]; uint16_t checksum; };
static const uint8_t TUNNEL_CMD_TYPE = 0xC1, TUNNEL_OUT_TYPE = 0xC2;
static uint16_t tunnelSumBytes(const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p; uint16_t s = 0;
  for (size_t i = 0; i + 2 < n; i++) s += b[i];
  return s;
}
#define tunnelSum(pkt) tunnelSumBytes(&(pkt), sizeof(pkt))

static HardwareSerial &UsbSerial = Serial;   // capture before the #define below

class TeeSerial : public Print {
public:
  // ---- output: USB always; tunnel ring while armed ----
  size_t write(uint8_t c) override { UsbSerial.write(c); push(c); return 1; }
  size_t write(const uint8_t *b, size_t n) override {
    UsbSerial.write(b, n);
    for (size_t i = 0; i < n; i++) push(b[i]);
    return n;
  }
  void begin(unsigned long baud) { UsbSerial.begin(baud); }
  void flush() { UsbSerial.flush(); }
  // ---- input: injected tunnel lines first, then real USB ----
  int available() { return injLen() + UsbSerial.available(); }
  int read() {
    portENTER_CRITICAL(&mux);
    int c = -1;
    if (injTail != injHead) { c = injBuf[injTail]; injTail = (injTail + 1) % sizeof(injBuf); }
    portEXIT_CRITICAL(&mux);
    return c >= 0 ? c : UsbSerial.read();
  }
  // ---- tunnel plumbing ----
  volatile unsigned long armedUntil = 0;
  bool armed() { return (long)(millis() - armedUntil) < 0; }
  void inject(const char *s, uint8_t n) {           // from the ESP-NOW RX task
    portENTER_CRITICAL(&mux);
    for (uint8_t i = 0; i < n; i++) {
      uint16_t nx = (injHead + 1) % sizeof(injBuf);
      if (nx == injTail) break;                     // full: drop the tail of the line
      injBuf[injHead] = s[i]; injHead = nx;
    }
    portEXIT_CRITICAL(&mux);
  }
  uint16_t pending() { portENTER_CRITICAL(&mux); uint16_t n = (uint16_t)((outHead - outTail + sizeof(outBuf)) % sizeof(outBuf)); portEXIT_CRITICAL(&mux); return n; }
  uint8_t drain(char *dst, uint8_t maxN) {
    portENTER_CRITICAL(&mux);
    uint8_t n = 0;
    while (n < maxN && outTail != outHead) { dst[n++] = outBuf[outTail]; outTail = (outTail + 1) % sizeof(outBuf); }
    portEXIT_CRITICAL(&mux);
    return n;
  }
private:
  void push(uint8_t c) {
    if (!armed()) return;
    portENTER_CRITICAL(&mux);
    uint16_t nx = (outHead + 1) % sizeof(outBuf);
    if (nx != outTail) { outBuf[outHead] = c; outHead = nx; }   // full: drop newest
    portEXIT_CRITICAL(&mux);
  }
  uint16_t injLen() { portENTER_CRITICAL(&mux); uint16_t n = (uint16_t)((injHead - injTail + sizeof(injBuf)) % sizeof(injBuf)); portEXIT_CRITICAL(&mux); return n; }
  char outBuf[1024]; volatile uint16_t outHead = 0, outTail = 0;
  char injBuf[256];  volatile uint16_t injHead = 0, injTail = 0;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
};
TeeSerial SerialTee;
#define Serial SerialTee
// ===========================================================================

// RC4.7: OTA-through-the-tunnel + black-box recorder (both print via the tee,
// so bb8 sees them over the wireless console too)
#include "OtaUpdate.h"
#include "BlackBox.h"

#define ENABLE_ESPNOW 1
#define WIFI_CHANNEL 11
#define HEARTBEAT_LED 2

// ------------------- CONFIG -------------------
#define REVERSE_DRIVE false      // reverse drive motor direction
#define REVERSE_S2S false        // reverse S2S inner-loop direction
#define S2S_BALANCE_INVERT false // flip roll-PID contribution to S2S target
#define S2S_STICK_INVERT false   // flip joystick contribution to S2S target
#define DRIVE_BALANCE_INVERT false // flip pitch-PID contribution to drive
                                   // (use when joystick direction is right but
                                   //  balance pushes INTO the lean)

// ------------------- CONFIGURABLE DEFAULTS -------------------
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC4";
static const char* DEFAULT_REVISION_DATE = "2026-08-20";
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

// RC4 PID defaults — REAL units, no hidden gain.
//   Drive: PWM per degree of pitch error.
//   S2S (outer): pot counts per degree of roll error.
const float RC4_DRIVE_KP = 12.0f;
const float RC4_DRIVE_KI = 6.0f;
const float RC4_DRIVE_KD = 0.5f;
const float RC4_S2S_KP = 30.0f;
const float RC4_S2S_KI = 10.0f;
const float RC4_S2S_KD = 1.0f;

// Deadzone defaults
static const float  DEFAULT_MPU_DEADZONE = 3.0f;  // dome-display shaping only
static const int32_t DEFAULT_JOY_DEADZONE = 5;

// Preferred MAC defaults
static const uint8_t DEFAULT_PREF_DRIVE_MAC[6] = { 0x00, 0x06, 0xF5, 0x64, 0x60, 0x3E };
static const uint8_t DEFAULT_PREF_DOME_MAC[6] = { 0, 0, 0, 0, 0, 0 };

// RC4.2: preferred controller MACs are RUNTIME settable ('bt prefer ...',
// 'bb8 pair') and stored in NVS keys "pdrv"/"pdome" — separate from the
// config blob so no migration is needed. The defaults above apply when
// nothing is stored.
uint8_t prefDriveMac[6];
uint8_t prefDomeMac[6];

// MAC for ESPNOW (dome WiFi STA MAC)
uint8_t domeMACAddress[] = { 0xC4, 0x5B, 0xBE, 0x90, 0x6A, 0x24 };

// Config version — RC4 bump triggers one-time PID migration
static const uint32_t RC4_CFG_VERSION = 0x00010002u;

// S2S geometry
float s2sMaxDegrees = 70.0f;
const float S2S_FULL_SWING_DEGREES = 92.0f;
const int POT_FULL_SWING_COUNTS = 1000;
const float POT_COUNTS_PER_DEGREE = POT_FULL_SWING_COUNTS / S2S_FULL_SWING_DEGREES;

// RC4 control-loop constants
const uint32_t CONTROL_PERIOD_US = 10000;   // 100 Hz control tick
const float    DRIVE_SLEW_PWM_PER_S = 1500.0f;  // joystick drive slew
const float    DRIVE_EXPO = 0.30f;              // joystick expo (0=linear)
const int      S2S_POS_DEADBAND = 8;            // pot counts
const int      S2S_STICTION_PWM = 35;           // min effective S2S PWM
const int      DRIVE_MIN_PWM = 20;              // below this -> brake
const uint32_t IMU_STALE_MS = 500;              // autoBalance cutoff on IMU loss
float s2sInnerKp = 0.9f;                        // PWM per pot count
float maxJoyDrivePwm = 255.0f;                  // joystick drive authority

// RC4.1: state-feedback tracks, runtime-configurable ('pref sndon/sndoff')
// in case a track file is missing from the SD card
int soundDriveOn = 0;     // RC4.6: 0 = random quick blip 70-74 on ENABLE ('pref sndon <n>' pins a track)
int soundDriveOff = 0;    // RC4.6: 0 = random quick blip 70-74 on DISABLE
int soundShutdown = 61;   // RC4.6: DRIVE controller disconnect -> 0061 "shutdown" (0100 is the same clip)
int soundConnect = 1;     // RC4.3: a controller lands in the DRIVE slot -> MP3/0001.mp3 (startup)
int soundBootCal = 60;    // RC4.6: boot complete -> 0060 "bootup" (fires at boot-cal done); 0 = silent (pref sndcal)

// ------------------- PWM CONFIG -------------------
const int PWM_FREQ = 20000;
const int PWM_RES = 8;
const int DRIVE_CH = 0;
const int S2S_CH = 1;
const int FLYWHEEL_CH = 2;

// ------------------- Debug Variables -------------------
volatile uint32_t gDriveDuty = 0;
volatile uint32_t gS2SDuty = 0;
volatile bool gDriveDirFwd = false;
volatile bool gS2SDirFwd = false;
volatile int gFlywheelPWM = 0;
volatile bool gFlywheelDirFwd = false;

// ------------------- GLOBALS -------------------
struct_messagempu mpudata;
struct_messagedome domeData;
send32u4 sendTo32u4;
Rec32u4 recFrom32u4;
DriveConfig cfg;

bool pidTuneMode = false;
bool tuningDrivePID = false;
bool tuningS2SPID = false;
int tuningStep = 0;
unsigned long lastInputTime = 0;
static unsigned long comboStartTime = 0;

// Debug flags
bool debugAll = false;
bool debugMPU = false;
bool debug32u4 = false;
bool debugDome = false;
bool debugControllersFlag = false;
bool debugS2S = false;
bool debugDrive = false;
bool debugSound = false;
bool debugFlywheel = false;
bool debugTo32u4 = false;
bool debugFrom32u4 = false;
bool telemetryEnabled = false;
bool telemetryFast = false;   // RC4: 100 Hz stream for rig/system-ID captures

bool imuHasSample = false;

bool isPlaying = false;
bool flywheelMode = false;
GamepadPtr myControllers[2];  // [0] Drive, [1] Dome
ControllerState driveController, domeController;

bool driveEnabled = false, autoBalance = false, domeFunctionEnabled = false;
SerialTransfer Coms32u4, ComsTrinket;
Preferences prefs;

float lastBatteryVoltage = 4.0;

// RC4.7: idle personality + dome-battery watch (both persisted in NVS)
int   idleChatterSec = 0;     // 0 = off; else random chatter after this many idle seconds
float batLowVolts = 0.0f;     // 0 = off; else alert when the dome battery drops below
unsigned long lastSoundTriggerMs = 0;
uint16_t lastSoundCmd = SOUND_NONE;
const uint16_t SOUND_DEBOUNCE_MS = 250;

unsigned long last32u4Packet = 0;
unsigned long lastIMUUpdate = 0;

// RC4 control state
float potFiltered = 0.0f;
float drivePwmState = 0.0f;      // slewed joystick drive PWM
int   gS2STargetPot = 0;         // debug/telemetry
uint16_t loopHz = 0;             // measured loop rate

// PID controllers (RC4 implementation)
PIDController drivePID(RC4_DRIVE_KP, RC4_DRIVE_KI, RC4_DRIVE_KD);
PIDController s2sPID(RC4_S2S_KP, RC4_S2S_KI, RC4_S2S_KD);

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

void printControllersSummary() {
  Serial.printf("[INFO] Drive slot: %s | Dome slot: %s\n",
                myControllers[0] ? "connected" : "empty",
                myControllers[1] ? "connected" : "empty");
}

// RC4: expo curve for joystick feel (x in -1..1)
static inline float expoCurve(float x, float e) {
  return x * ((1.0f - e) + e * x * x);
}

// ------------------- Sound Sender -------------------
// RC4.1: sounds no longer ride a ONE-SHOT packet. The DFPlayer's
// SoftwareSerial on the 32u4 blocks interrupts ~1 ms per byte, which
// corrupts whatever link packet is arriving right then (CRC/PAYLOAD
// errors correlate exactly with audio activity) — a lost one-shot
// packet = sound silently dropped. Now the command rides the 50 Hz
// state stream with a sequence number (functionnumber), repeated in
// 5 consecutive packets; the 32u4 plays on sequence CHANGE, so any
// 1-of-5 arriving is enough and duplicates are ignored.
uint8_t gSoundSeq = 0;
int8_t gSoundRepeat = 0;

inline void sendSoundCommand(SerialTransfer& coms, send32u4& payload, uint16_t cmd) {
  if (cmd == SOUND_NONE) return;

  unsigned long now = millis();
  if (now - lastSoundTriggerMs < SOUND_DEBOUNCE_MS && cmd == lastSoundCmd) return;

  lastSoundTriggerMs = now;
  lastSoundCmd = cmd;

  if (++gSoundSeq == 0) gSoundSeq = 1;
  payload.soundcmd = (int8_t)cmd;
  payload.functionnumber = gSoundSeq;
  gSoundRepeat = 5;

  if (debugSound) {
    Serial.printf("[SoundSender] queued SoundCMD=%u seq=%u (5x repeat)\n", cmd, gSoundSeq);
  }
}

// ------------------- Motor Primitives -------------------
void initMotors() {
  pinMode(DRIVE_PIN_1, OUTPUT);
  pinMode(DRIVE_PIN_2, OUTPUT);
  pinMode(S2S_PIN_1, OUTPUT);
  pinMode(S2S_PIN_2, OUTPUT);
  pinMode(FLYWHEEL_PIN_1, OUTPUT);
  pinMode(FLYWHEEL_PIN_2, OUTPUT);

  ledcSetup(DRIVE_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(DRIVE_PWM, DRIVE_CH);

  ledcSetup(S2S_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(S2S_PWM, S2S_CH);

  ledcSetup(FLYWHEEL_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(FLYWHEEL_PWM, FLYWHEEL_CH);
}

// RC4: fixed — both direction pins LOW (RC3 wrote PIN_1 twice)
void brakeDrive() {
  digitalWrite(DRIVE_PIN_1, LOW);
  digitalWrite(DRIVE_PIN_2, LOW);
  ledcWrite(DRIVE_CH, 0);
  gDriveDuty = 0;
}
void brakeS2S() {
  digitalWrite(S2S_PIN_1, LOW);
  digitalWrite(S2S_PIN_2, LOW);
  ledcWrite(S2S_CH, 0);
  gS2SDuty = 0;
}
void brakeFlywheel() {
  digitalWrite(FLYWHEEL_PIN_1, LOW);
  digitalWrite(FLYWHEEL_PIN_2, LOW);
  ledcWrite(FLYWHEEL_CH, 0);
  gFlywheelPWM = 0;
}

// RC4: single signed-PWM writer per motor (sign conventions match RC3)
void applyDrivePWM(int pwm) {
  pwm = constrain(pwm, -255, 255);
  if (REVERSE_DRIVE) pwm = -pwm;
  if (abs(pwm) < DRIVE_MIN_PWM) { brakeDrive(); return; }
  digitalWrite(DRIVE_PIN_1, pwm > 0 ? HIGH : LOW);
  digitalWrite(DRIVE_PIN_2, pwm > 0 ? LOW : HIGH);
  ledcWrite(DRIVE_CH, abs(pwm));
  gDriveDuty = abs(pwm);
  gDriveDirFwd = (pwm > 0);
}

// positive PWM moves the pot value UP (same wiring as RC3 auto-center)
void applyS2SPWM(int pwm) {
  pwm = constrain(pwm, -255, 255);
  if (REVERSE_S2S) pwm = -pwm;
  if (pwm == 0) { brakeS2S(); return; }
  digitalWrite(S2S_PIN_1, pwm > 0 ? HIGH : LOW);
  digitalWrite(S2S_PIN_2, pwm > 0 ? LOW : HIGH);
  ledcWrite(S2S_CH, abs(pwm));
  gS2SDuty = abs(pwm);
  gS2SDirFwd = (pwm > 0);
}

void applyFlywheelPWM(int pwm) {
  pwm = constrain(pwm, -255, 255);
  if (abs(pwm) <= DEFAULT_JOY_DEADZONE) { brakeFlywheel(); return; }
  digitalWrite(FLYWHEEL_PIN_1, pwm < 0 ? HIGH : LOW);
  digitalWrite(FLYWHEEL_PIN_2, pwm < 0 ? LOW : HIGH);
  ledcWrite(FLYWHEEL_CH, abs(pwm));
  gFlywheelPWM = abs(pwm);
  gFlywheelDirFwd = (pwm < 0);
}

// ------------------- Rig experiments + Serial Commands -------------------
#include "TuneExperiments.h"
#include "SerialCommands.h"

// ------------------- Calibration -------------------
// RC4.1: per-axis calibration. mask bits: 1=pitch zero (drive axis),
// 2=roll zero, 4=pot center. 'cfg calibrate' = all three;
// 'cfg calibrate drive' = pitch only; 'cfg calibrate s2s' = roll+pot.
bool s2sCalibrating = false;
unsigned long s2sCalStartMs = 0;
uint32_t s2sCalSamples = 0;
uint64_t s2sCalSumPot = 0;
double s2sCalSumPitch = 0.0, s2sCalSumRoll = 0.0;
uint8_t calMask = 0x7;

void beginCalibration(uint8_t mask) {
  calMask = mask;
  s2sCalibrating = true;
  s2sCalStartMs = millis();
  s2sCalSamples = 0;
  s2sCalSumPot = 0;
  s2sCalSumPitch = 0.0;
  s2sCalSumRoll = 0.0;
  brakeDrive();
  brakeS2S();
  Serial.printf("[CAL] Calibrating%s%s%s — 3 s, keep the droid level and still\n",
                (mask & 1) ? " pitch-zero" : "",
                (mask & 2) ? " roll-zero" : "",
                (mask & 4) ? " pot-center" : "");
}

// legacy name (docs/operator card)
void beginS2SCenterCalibration() { beginCalibration(0x7); }

void finishS2SCalibration() {
  s2sCalibrating = false;
  if (s2sCalSamples > 0) {
    int32_t avgPot = (int32_t)(s2sCalSumPot / s2sCalSamples);
    double avgPitch = s2sCalSumPitch / s2sCalSamples;
    double avgRoll = s2sCalSumRoll / s2sCalSamples;
    if (calMask & 1) cfg.pitchOffset = (float)(-avgPitch);
    if (calMask & 2) cfg.rollOffset = (float)(-avgRoll);
    if (calMask & 4) cfg.potCenter = avgPot;
    saveConfig();
    Serial.printf("[CAL] Done (saved). pitchOffset=%.3f%s rollOffset=%.3f%s potCenter=%ld%s (samples=%lu)\n",
                  cfg.pitchOffset, (calMask & 1) ? "*" : "",
                  cfg.rollOffset, (calMask & 2) ? "*" : "",
                  (long)cfg.potCenter, (calMask & 4) ? "*" : "",
                  (unsigned long)s2sCalSamples);
  }
}

void serviceS2SCenterCalibration() {
  if (!s2sCalibrating) return;
  s2sCalSumPot += analogRead(S2S_POT_PIN);
  s2sCalSumPitch += mpudata.pitch;
  s2sCalSumRoll += mpudata.roll;
  s2sCalSamples++;
  if (millis() - s2sCalStartMs >= 3000UL) finishS2SCalibration();
}

#include "CalibrationModule.h"

// Save current offsets and pot center to NVS (combo: "capture pose as zero")
void savePrefs() {
  cfg.pitchOffset = -mpudata.pitch;
  cfg.rollOffset = -mpudata.roll;
  cfg.potCenter = analogRead(S2S_POT_PIN);

  cfg.driveKp = drivePID.getKp();
  cfg.driveKi = drivePID.getKi();
  cfg.driveKd = drivePID.getKd();
  cfg.s2sKp = s2sPID.getKp();
  cfg.s2sKi = s2sPID.getKi();
  cfg.s2sKd = s2sPID.getKd();

  if (saveConfig()) {
    sendSoundCommand(Coms32u4, sendTo32u4, 5);
    Serial.printf("[SAVE PREFS] pitchOffset=%.2f rollOffset=%.2f potCenter=%d\n",
                  cfg.pitchOffset, cfg.rollOffset, cfg.potCenter);
  } else {
    Serial.println(F("[ERROR] Failed to save prefs"));
  }
}

// RC4: save ONLY PID values (RC3 'pid save' also overwrote the offsets)
bool savePidOnly() {
  cfg.driveKp = drivePID.getKp();
  cfg.driveKi = drivePID.getKi();
  cfg.driveKd = drivePID.getKd();
  cfg.s2sKp = s2sPID.getKp();
  cfg.s2sKi = s2sPID.getKi();
  cfg.s2sKd = s2sPID.getKd();
  return saveConfig();
}

void resetPrefs() {
  resetConfigToDefaults();
  saveConfig();
  sendSoundCommand(Coms32u4, sendTo32u4, 7);
  Serial.println(F("[RESET PREFS] Defaults applied. Rebooting..."));
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
    return false;
  }
  // RC4: one-time migration — keep offsets/potCenter, reset PID to RC4 units
  if (cfg.cfgVersion != RC4_CFG_VERSION) {
    Serial.printf("[NVS] Migrating config 0x%08lX -> 0x%08lX (PID reset to RC4 defaults; offsets kept)\n",
                  (unsigned long)cfg.cfgVersion, (unsigned long)RC4_CFG_VERSION);
    cfg.driveKp = RC4_DRIVE_KP;
    cfg.driveKi = RC4_DRIVE_KI;
    cfg.driveKd = RC4_DRIVE_KD;
    cfg.s2sKp = RC4_S2S_KP;
    cfg.s2sKi = RC4_S2S_KI;
    cfg.s2sKd = RC4_S2S_KD;
    cfg.cfgVersion = RC4_CFG_VERSION;
    saveConfig();
  }
  return true;
}

void resetConfigToDefaults() {
  strncpy(cfg.revision, DEFAULT_REVISION, sizeof(cfg.revision));
  strncpy(cfg.revisionDate, DEFAULT_REVISION_DATE, sizeof(cfg.revisionDate));
  cfg.pitchOffset = 0.0f;
  cfg.rollOffset = 0.0f;
  cfg.potCenter = DEFAULT_POT_CENTER;
  cfg.mpuDeadzone = DEFAULT_MPU_DEADZONE;
  cfg.cfgVersion = RC4_CFG_VERSION;

  cfg.driveKp = RC4_DRIVE_KP;
  cfg.driveKi = RC4_DRIVE_KI;
  cfg.driveKd = RC4_DRIVE_KD;
  cfg.s2sKp = RC4_S2S_KP;
  cfg.s2sKi = RC4_S2S_KI;
  cfg.s2sKd = RC4_S2S_KD;
}

// ------------------- Callbacks -------------------
// ---- RC4.2: preferred-controller persistence + 'bt' console helpers ----
static void macToStr(const uint8_t* m, char* out) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static bool parseMacStr(const String& s, uint8_t* out) {
  unsigned v[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

void loadPrefMacs() {
  memcpy(prefDriveMac, DEFAULT_PREF_DRIVE_MAC, 6);
  memcpy(prefDomeMac, DEFAULT_PREF_DOME_MAC, 6);
  prefs.begin("drivecfg", true);
  if (prefs.getBytesLength("pdrv") == 6) prefs.getBytes("pdrv", prefDriveMac, 6);
  if (prefs.getBytesLength("pdome") == 6) prefs.getBytes("pdome", prefDomeMac, 6);
  // RC4.7: the dome BOARD's ESP-NOW MAC is NVS-configurable ('dome mac XX:..')
  // — a spare dome board no longer needs a source edit + reflash of the drive.
  if (prefs.getBytesLength("dmac") == 6) prefs.getBytes("dmac", domeMACAddress, 6);
  prefs.end();
}

void savePrefMacs() {
  prefs.begin("drivecfg", false);
  prefs.putBytes("pdrv", prefDriveMac, 6);
  prefs.putBytes("pdome", prefDomeMac, 6);
  prefs.end();
}

// RC4.7: repoint ESP-NOW at a different dome board at runtime (saved to NVS).
bool setDomeMac(const uint8_t* m) {
  esp_now_del_peer(domeMACAddress);        // may not exist yet — ignore result
  memcpy(domeMACAddress, m, 6);
  prefs.begin("drivecfg", false);
  prefs.putBytes("dmac", domeMACAddress, 6);
  prefs.end();
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, domeMACAddress, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// RC4.3: the sound prefs (sndon/off/shut/conn/cal) persist in NVS so they
// survive a reboot — otherwise 'pref sndcal 0' could never silence the boot
// chirp, which fires ~3 s into loop() (long after loadSoundPrefs() in setup).
void loadSoundPrefs() {
  prefs.begin("drivecfg", true);
  soundDriveOn  = prefs.getInt("sndon",   soundDriveOn);
  soundDriveOff = prefs.getInt("sndoff",  soundDriveOff);
  soundShutdown = prefs.getInt("sndshut", soundShutdown);
  soundConnect  = prefs.getInt("sndconn", soundConnect);
  soundBootCal  = prefs.getInt("sndcal",  soundBootCal);
  s2sMaxDegrees = prefs.getFloat("swing", s2sMaxDegrees);   // RC4.4: swing persists too
  maxJoyDrivePwm = prefs.getFloat("lean", maxJoyDrivePwm);  // RC4.7: these persist now too
  s2sInnerKp     = prefs.getFloat("innerkp", s2sInnerKp);
  idleChatterSec = prefs.getInt("idle", idleChatterSec);
  batLowVolts    = prefs.getFloat("batlow", batLowVolts);
  prefs.end();
}
void saveSoundPrefs() {
  prefs.begin("drivecfg", false);
  prefs.putInt("sndon",   soundDriveOn);
  prefs.putInt("sndoff",  soundDriveOff);
  prefs.putInt("sndshut", soundShutdown);
  prefs.putInt("sndconn", soundConnect);
  prefs.putInt("sndcal",  soundBootCal);
  prefs.putFloat("swing", s2sMaxDegrees);
  prefs.putFloat("lean", maxJoyDrivePwm);
  prefs.putFloat("innerkp", s2sInnerKp);
  prefs.putInt("idle", idleChatterSec);
  prefs.putFloat("batlow", batLowVolts);
  prefs.end();
}

void btPreferShow() {
  char a[18], b[18];
  macToStr(prefDriveMac, a);
  macToStr(prefDomeMac, b);
  bool dz = memcmp(prefDriveMac, "\0\0\0\0\0\0", 6) == 0;
  bool mz = memcmp(prefDomeMac, "\0\0\0\0\0\0", 6) == 0;
  Serial.printf("[BT] preferred DRIVE (primary):   %s\n", dz ? "(none - first pad to connect)" : a);
  Serial.printf("[BT] preferred DOME  (secondary): %s\n", mz ? "(none)" : b);
}

// 'bt list' — connected pads with their MACs and current slots
void btList() {
  bool any = false;
  for (int s = 0; s < 2; s++) {
    GamepadPtr gp = myControllers[s];
    if (!gp || !gp->isConnected()) continue;
    any = true;
    char m[18];
    macToStr(gp->getProperties().btaddr, m);
    Serial.printf("[BT] slot%d %-5s MAC=%s  model=%s\n", s, s == 0 ? "DRIVE" : "DOME", m, gp->getModelName().c_str());
  }
  if (!any) Serial.println(F("[BT] no controllers connected (press PS on a paired pad)"));
  btPreferShow();
}

// 'bt prefer drive|dome <MAC|slot0|slot1|none>'
void btPrefer(bool driveSlot, String arg) {
  arg.trim();
  uint8_t* target = driveSlot ? prefDriveMac : prefDomeMac;
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  if (arg == "none" || arg == "clear") {
    // zeros = unset
  } else if (arg == "slot0" || arg == "slot1") {
    int s = (arg == "slot0") ? 0 : 1;
    if (!myControllers[s] || !myControllers[s]->isConnected()) {
      Serial.printf("[BT] slot%d has no connected controller\n", s);
      return;
    }
    memcpy(mac, myControllers[s]->getProperties().btaddr, 6);
  } else if (!parseMacStr(arg, mac)) {
    Serial.println(F("[BT] usage: bt prefer drive|dome <XX:XX:XX:XX:XX:XX | slot0 | slot1 | none>"));
    return;
  }
  memcpy(target, mac, 6);
  savePrefMacs();
  Serial.printf("[BT] saved preferred %s controller\n", driveSlot ? "DRIVE" : "DOME");
  btPreferShow();
}

// RC4.3: play the startup clip whenever a controller lands in the DRIVE
// slot (first pairing, and every reconnect after a PS-hold power-off).
void onConnectedGamepad(GamepadPtr gp) {
  bool hadDrive = myControllers[0] != nullptr;
  assignConnectedGamepad(gp);
  if (!hadDrive && myControllers[0] != nullptr)
    sendSoundCommand(Coms32u4, sendTo32u4, soundConnect);   // pref sndconn
}

void assignConnectedGamepad(GamepadPtr gp) {
  uint8_t mac[6];
  memcpy(mac, gp->getProperties().btaddr, 6);

  Serial.printf("Controller connected: MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  bool driveMacUnset = memcmp(prefDriveMac, "\0\0\0\0\0\0", 6) == 0;
  bool domeMacUnset = memcmp(prefDomeMac, "\0\0\0\0\0\0", 6) == 0;

  if (!driveMacUnset && memcmp(mac, prefDriveMac, 6) == 0) {
    if (myControllers[0] != gp) {
      if (myControllers[0] && myControllers[0] != gp) {
        myControllers[1] = myControllers[0];
        Serial.println(F("[INFO] Previous controller moved to DOME slot"));
      }
      myControllers[0] = gp;
      Serial.println(F("[INFO] Assigned to DRIVE slot (preferred MAC)"));
    }
    return;
  }

  if (!domeMacUnset && memcmp(mac, prefDomeMac, 6) == 0) {
    if (myControllers[0] == nullptr) {
      myControllers[0] = gp;
      Serial.println(F("[INFO] Dome controller assigned to DRIVE slot (temporary)"));
    } else {
      myControllers[1] = gp;
      Serial.println(F("[INFO] Assigned to DOME slot (preferred MAC)"));
    }
    return;
  }

  if (myControllers[0] == nullptr) {
    myControllers[0] = gp;
    Serial.println(F("[INFO] Assigned to DRIVE slot (fallback)"));
  } else if (myControllers[1] == nullptr) {
    myControllers[1] = gp;
    Serial.println(F("[INFO] Assigned to DOME slot (fallback)"));
  } else {
    Serial.println(F("[INFO] Both slots occupied, ignoring extra controller"));
  }
}

void onDisconnectedGamepad(GamepadPtr gp) {
  bool wasDrive = (myControllers[0] == gp);
  bool wasDome = (myControllers[1] == gp);

  if (wasDrive) {
    myControllers[0] = nullptr;
    Serial.println(F("[INFO] DRIVE controller disconnected"));
    // RC4.1 safety: never keep driving on a vanished drive controller
    if (driveEnabled) {
      driveEnabled = false;
      autoBalance = false;
      domeFunctionEnabled = false;
      Serial.println(F("[SAFETY] Drive controller lost — drive DISABLED"));
      blackboxFreeze("drive pad lost");
    }
    // RC4.3: the disconnect itself is the "powered down" moment -> shutdown
    // clip (pref sndshut), whether or not the drive was still enabled.
    sendSoundCommand(Coms32u4, sendTo32u4, soundShutdown);
  }
  if (wasDome) {
    myControllers[1] = nullptr;
    Serial.println(F("[INFO] DOME controller disconnected"));
  }

  if (wasDrive && myControllers[1] != nullptr) {
    myControllers[0] = myControllers[1];
    myControllers[1] = nullptr;
    // RC4.1: promotion no longer silently hands a live drive to the dome
    // pad — drive stays DISABLED until deliberately re-enabled.
    Serial.println(F("[INFO] Dome controller promoted to DRIVE slot (drive remains DISABLED — PS to enable)"));
  }
}

volatile bool espnowSendInFlight = false;
void onEspNowSend(const uint8_t* mac, esp_now_send_status_t status) {
  espnowSendInFlight = false;
  if (debugDome) {
    Serial.print(F("[ESP-NOW] Send status: "));
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? F("SUCCESS") : F("FAIL"));
  }
}

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(struct_messagedome)) {
    struct_messagedome temp;
    memcpy(&temp, data, sizeof(temp));
    uint16_t calc = calculateChecksumDome(temp);
    if (calc != temp.checksum) return;
    lastBatteryVoltage = temp.bat;
  }
  // RC4.4: console command tunneled in from the dome bridge. len==0 is a
  // keepalive (arms the output mirror while bb8 is attached to the dome).
  else if (len == sizeof(TunnelCmd)) {
    TunnelCmd c;
    memcpy(&c, data, sizeof(c));
    if (c.type != TUNNEL_CMD_TYPE || c.len > sizeof(c.data)) return;
    if (tunnelSum(c) != c.checksum) return;
    static uint8_t lastSeq = 0;
    if (c.seq == lastSeq) return;               // dome retries dupes
    lastSeq = c.seq;
    SerialTee.armedUntil = millis() + 60000UL;
    if (c.len > 0) {
      SerialTee.inject(c.data, c.len);
      SerialTee.inject("\n", 1);
    }
  }
  // RC4.7: OTA firmware chunk relayed by the dome — queue it for loop()
  // (never write flash from the WiFi task).
  else if (len == sizeof(TunnelOta)) {
    TunnelOta o;
    memcpy(&o, data, sizeof(o));
    if (o.type != TUNNEL_OTA_TYPE || o.len > sizeof(o.data)) return;
    if (tunnelSumBytes(&o, sizeof(o)) != o.checksum) return;
    SerialTee.armedUntil = millis() + 60000UL;
    OtaRx::onPacket(o);
  }
}

// ------------------- Controller Update -------------------
void updateControllers() {
  driveController.update(myControllers[0]);
  domeController.update(myControllers[1]);
}

// ------------------- Sound Triggers -------------------
inline void handleSoundTriggers() {
  if (pidTuneMode) return;  // RC4: dpad/cross belong to the tuner while tuning

  static uint16_t lastSoundCmdSent = SOUND_NONE;

  // RC4.3: only resolve sounds from a controller that is ACTUALLY connected,
  // and only after it has been up for SOUND_ARM_MS. This kills the random
  // sounds heard at boot / with no pad: an unseeded random() makes
  // pickRandom1to30() return a fixed 6, and a stale or just-connecting
  // gamepad object could present a phantom D-pad-UP for a few reports.
  const unsigned long SOUND_ARM_MS = 400;
  unsigned long now = millis();
  bool driveOn = myControllers[0] && myControllers[0]->isConnected();
  bool domeOn  = myControllers[1] && myControllers[1]->isConnected();

  static bool prevDriveOn = false, prevDomeOn = false;
  static unsigned long driveArmAt = 0, domeArmAt = 0;
  if (driveOn && !prevDriveOn) driveArmAt = now + SOUND_ARM_MS;
  if (domeOn  && !prevDomeOn)  domeArmAt  = now + SOUND_ARM_MS;
  prevDriveOn = driveOn; prevDomeOn = domeOn;

  bool driveArmed = driveOn && (long)(now - driveArmAt) >= 0;
  bool domeArmed  = domeOn  && (long)(now - domeArmAt)  >= 0;

  if (!driveArmed && !domeArmed) { lastSoundCmdSent = SOUND_NONE; return; }

  uint16_t cmd = driveArmed ? resolveDriveControllerSound(driveController) : SOUND_NONE;
  if (cmd == SOUND_NONE && domeArmed) {
    cmd = resolveDomeControllerSound(domeController);
  }
  if (cmd != SOUND_NONE && cmd != lastSoundCmdSent) {
    sendSoundCommand(Coms32u4, sendTo32u4, cmd);
    lastSoundCmdSent = cmd;
  }
  if (cmd == SOUND_NONE) {
    lastSoundCmdSent = SOUND_NONE;
  }
}

// ------------------- RC4 Control Loop (100 Hz) -------------------
// Everything that writes the drive/S2S/flywheel motors lives HERE.
void runControl(float dt) {

  // Calibration owns the motors
  if (s2sCalibrating) {
    brakeDrive(); brakeS2S(); brakeFlywheel();
    sendTo32u4.DomeSpin = 0;
    return;
  }

  // Pot filter (EMA) — always maintained
  int potRaw = analogRead(S2S_POT_PIN);
  potFiltered += 0.3f * ((float)potRaw - potFiltered);

  // IMU staleness guard: never balance on dead data
  bool imuFresh = (millis() - lastIMUUpdate) < IMU_STALE_MS;
  if (autoBalance && !imuFresh && imuHasSample) {
    autoBalance = false;
    drivePID.reset();
    s2sPID.reset();
    Serial.println(F("[SAFETY] IMU stale — autoBalance disabled"));
    blackboxFreeze("IMU stale");
    sendSoundCommand(Coms32u4, sendTo32u4, pickRandomAlert());   // RC4.6: audible alert (bank 80-89)
  }

  // Special modes (same precedence as RC3)
  if (domeController.L1.held) {
    abortExperiment("flywheel mode engaged");
    brakeDrive();
    brakeS2S();
    applyFlywheelPWM(map(driveController.joyX, -127, 127, 255, -255));
    sendTo32u4.DomeSpin = 0;
    return;
  }

  if (driveController.L1.held) {
    abortExperiment("dome-spin mode engaged");
    brakeDrive();
    brakeS2S();
    brakeFlywheel();
    sendTo32u4.DomeSpin = applyDeadzoneAxis(driveController.joyX, DEFAULT_JOY_DEADZONE);
    return;
  }
  sendTo32u4.DomeSpin = 0;

  if (!driveEnabled) {
    abortExperiment("drive disabled");
    brakeDrive();
    brakeS2S();
    brakeFlywheel();
    drivePID.reset();
    s2sPID.reset();
    drivePwmState = 0;
    return;
  }

  // Corrected angles — NO deadzone in the control path (RC4 fix #5)
  float pitch = mpudata.pitch + cfg.pitchOffset;
  float roll  = mpudata.roll + cfg.rollOffset;

  // RC4: rig experiments (step / relay autotune) own the motors while active.
  // Grabbing a stick aborts back to normal control.
  if (experimentActive()) {
    if (abs(driveController.joyX) > 40 || abs(driveController.joyY) > 40) {
      abortExperiment("joystick grab");
    } else if (serviceExperiment(pitch, roll, potFiltered, (float)cfg.potCenter,
                                 s2sInnerKp, S2S_POS_DEADBAND, S2S_STICTION_PWM)) {
      brakeFlywheel();
      return;
    }
  }

  // ---------- DRIVE (pitch axis) ----------
  // Joystick: expo + L2 throttle scale + slew limit
  float joyNorm = 0.0f;
  if (abs(driveController.joyY) > DEFAULT_JOY_DEADZONE) {
    joyNorm = expoCurve(-driveController.joyY / 127.0f, DRIVE_EXPO);  // stick up = forward
  }
  float throttleScale = 0.5f + (driveController.L2 / 1023.0f) * 0.5f; // RC4 fix #10
  float joyTargetPwm = joyNorm * maxJoyDrivePwm * throttleScale;

  float maxStep = DRIVE_SLEW_PWM_PER_S * dt;
  float d = joyTargetPwm - drivePwmState;
  if (d >  maxStep) d =  maxStep;
  if (d < -maxStep) d = -maxStep;
  drivePwmState += d;

  int drivePWM;
  if (autoBalance && imuFresh) {
    // RC4 fix #7: stabilization stays active; joystick blends on top
    float pidOut = drivePID.compute(0.0f, pitch, dt);
    if (DRIVE_BALANCE_INVERT) pidOut = -pidOut;
    drivePWM = (int)constrain(drivePwmState + pidOut, -255.0f, 255.0f);
  } else {
    drivePWM = (int)drivePwmState;
  }
  applyDrivePWM(drivePWM);

  // ---------- S2S (roll axis) — cascaded position loop (RC4 fix #6) ----------
  float targetPot = (float)cfg.potCenter;

  // Joystick moves the target (RC3 direction preserved: stick left -> pot down)
  if (abs(driveController.joyX) > DEFAULT_JOY_DEADZONE) {
    float stickFrac = driveController.joyX / 127.0f;
    if (S2S_STICK_INVERT) stickFrac = -stickFrac;
    targetPot += stickFrac * s2sMaxDegrees * POT_COUNTS_PER_DEGREE;
  }

  if (autoBalance && imuFresh) {
    float maxCounts = s2sMaxDegrees * POT_COUNTS_PER_DEGREE;
    s2sPID.setOutputLimits(-maxCounts, maxCounts);
    float balOut = s2sPID.compute(0.0f, roll, dt);
    if (S2S_BALANCE_INVERT) balOut = -balOut;
    targetPot += balOut;
  }

  float maxCounts = s2sMaxDegrees * POT_COUNTS_PER_DEGREE;
  targetPot = constrain(targetPot, (float)cfg.potCenter - maxCounts, (float)cfg.potCenter + maxCounts);
  gS2STargetPot = (int)targetPot;

  // Inner position loop: P with deadband + stiction compensation
  float posErr = targetPot - potFiltered;
  int s2sPWM = 0;
  if (fabsf(posErr) > S2S_POS_DEADBAND) {
    s2sPWM = (int)constrain(s2sInnerKp * posErr, -255.0f, 255.0f);
    if (abs(s2sPWM) < S2S_STICTION_PWM) {
      s2sPWM = (s2sPWM > 0) ? S2S_STICTION_PWM : -S2S_STICTION_PWM;
    }
  }
  applyS2SPWM(s2sPWM);

  brakeFlywheel();
}

// ------------------- Lights (compute only — RC4 fix #8) -------------------
struct_messagedome espnowPending;
bool espnowDirty = false;

void handleDomeAndBodyLights() {
  static struct_messagedome lastQueued;

  // RC4.6: relay WHICH track is playing (not just a flag) so the dome can
  // flicker the PSI to that track's actual amplitude envelope. The playing
  // track = the last sound command sent within a few seconds of BUSY rising;
  // anything else (console 'play', stale) = 255 -> dome uses its generic
  // speech cadence. 0 = not playing.
  {
    static uint8_t playingTrack = 0;
    bool busy = recFrom32u4.isplaying;
    bool recentCmd = (lastSoundCmd != SOUND_NONE && lastSoundCmd < 120 &&
                      millis() - lastSoundTriggerMs < 4000);
    if (!busy)                    playingTrack = 0;
    else if (playingTrack == 0)   playingTrack = recentCmd ? (uint8_t)lastSoundCmd : 255;
    else if (recentCmd && millis() - lastSoundTriggerMs < 300)
                                  playingTrack = (uint8_t)lastSoundCmd;   // new sound interrupted the old
    domeData.psi = playingTrack;
  }

  // RC4: while tuning, cross/dpad belong to the tuner — no light anims
  if (pidTuneMode) domeData.anim = 0;
  else if (driveController.cross.pressed) domeData.anim = 1;
  else if (driveController.circle.pressed) domeData.anim = 2;
  else if (driveController.L1.pressed) domeData.anim = 3;
  else if (driveController.dpadUp.pressed) domeData.anim = 4;
  else if (driveController.dpadDown.pressed) domeData.anim = 5;
  else if (driveController.dpadLeft.pressed) domeData.anim = 6;
  else if (driveController.dpadRight.pressed) domeData.anim = 7;
  else domeData.anim = 0;

  domeData.bat = lastBatteryVoltage;
  domeData.checksum = calculateChecksumDome(domeData);

  // Latch changes; the non-blocking sender picks them up
  if (memcmp(&domeData, &lastQueued, sizeof(domeData)) != 0) {
    lastQueued = domeData;
    espnowPending = domeData;
    espnowDirty = true;
  }
}

// RC4: non-blocking ESP-NOW service — no delay(), no busy retry
void serviceEspNow() {
#if ENABLE_ESPNOW
  static unsigned long lastSendMs = 0;
  const unsigned long MIN_GAP_MS = 30;
  const unsigned long HEARTBEAT_MS = 1000;

  unsigned long now = millis();
  bool heartbeatDue = (now - lastSendMs) >= HEARTBEAT_MS;

  if ((espnowDirty || heartbeatDue) && !espnowSendInFlight && (now - lastSendMs) >= MIN_GAP_MS) {
    if (!espnowDirty) {
      espnowPending = domeData;  // heartbeat: freshest state
    }
    espnowSendInFlight = true;
    esp_err_t r = esp_now_send(domeMACAddress, (uint8_t*)&espnowPending, sizeof(espnowPending));
    if (r != ESP_OK) {
      espnowSendInFlight = false;  // will retry on next service pass
      if (debugDome) Serial.printf("[ESP-NOW] queue error %d\n", (int)r);
    } else {
      espnowDirty = false;
    }
    lastSendMs = now;
  }

  // RC4.4: mirror buffered console output to the dome bridge. Lights have
  // priority (handled above); tunnel chunks fill the gaps, one in flight,
  // >=12 ms apart (~80 pkt/s ceiling — telemetry fast fits with room over).
  static unsigned long lastTunnelMs = 0;
  if (!espnowSendInFlight && SerialTee.pending() > 0 && (now - lastTunnelMs) >= 12) {
    static TunnelOut tp;
    static uint8_t tseq = 0;
    tp.type = TUNNEL_OUT_TYPE;
    tp.seq = ++tseq;
    tp.len = SerialTee.drain(tp.data, sizeof(tp.data));
    tp.checksum = tunnelSum(tp);
    espnowSendInFlight = true;
    if (esp_now_send(domeMACAddress, (uint8_t*)&tp, sizeof(tp)) != ESP_OK)
      espnowSendInFlight = false;               // chunk lost; stream is best-effort
    lastTunnelMs = now;
  }
#endif
}

// ------------------- IMU / dome display values -------------------
inline void updateSendTiltValues() {
  // RC4.5: commanded (slewed, pre-balance) drive PWM for the dome motion-lean
  sendTo32u4.drivePwm = (int8_t)constrain(drivePwmState * (127.0f / 255.0f), -127.0f, 127.0f);
  // mpuDeadzone shapes only what the 32u4 uses for dome tilt display
  sendTo32u4.pitch = applyDeadzoneFloat(mpudata.pitch + cfg.pitchOffset, cfg.mpuDeadzone);
  sendTo32u4.roll = applyDeadzoneFloat(mpudata.roll + cfg.rollOffset, cfg.mpuDeadzone);
}

// ------------------- Setup -------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== BB-8 Drive System Starting (RC4) ==="));
  randomSeed(esp_random());   // RC4.3: real entropy — pickRandom1to30() was deterministic (always 6) unseeded

  bootMs = millis();
  showBuildInfoSerial("BOOT");

  loadPrefMacs();           // RC4.2: preferred controller MACs from NVS
  loadSoundPrefs();         // RC4.3: sound prefs from NVS (before boot cal chirps)
  if (loadConfig()) {
    Serial.println(F("[NVS] Config loaded successfully"));
  } else {
    Serial.println(F("[NVS] No valid config found, using defaults"));
    saveConfig();
  }
  drivePID.setTunings(cfg.driveKp, cfg.driveKi, cfg.driveKd);
  s2sPID.setTunings(cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
  drivePID.setOutputLimits(-255.0f, 255.0f);
  drivePID.setDerivativeTau(0.04f);
  s2sPID.setDerivativeTau(0.06f);
  Serial.printf("[PID] Drive: Kp=%.2f Ki=%.2f Kd=%.2f | S2S: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                cfg.driveKp, cfg.driveKi, cfg.driveKd, cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);

#ifdef analogReadResolution
  analogReadResolution(12);
#endif

  // RC4 fix #15 (BT/ESP-NOW collisions): RC3 used ESP_COEX_PREFER_WIFI,
  // which told the single shared 2.4GHz radio to prioritize ESP-NOW dome
  // lights OVER the Bluepad32 controllers. BALANCE keeps the controllers
  // responsive; ESP-NOW traffic is tiny in RC4 (dirty-latch + 1Hz
  // heartbeat) so it fits in the gaps. If controller lag persists, try
  // ESP_COEX_PREFER_BT.
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

  // RC4 fix #11: no forgetBluetoothKeys(), no blocking 5 s window.
  BP32.setup(&onConnectedGamepad, &onDisconnectedGamepad);
  BP32.enableNewBluetoothConnections(true);
  Serial.println(F("[BT] Ready — paired controllers will auto-reconnect ('bt forget' to re-pair)"));

  uint8_t bt_mac[6];
  esp_read_mac(bt_mac, ESP_MAC_BT);
  Serial.printf("[BT] Host MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);

  Serial2.begin(74880, SERIAL_8N1, 13, 12);  // 32u4 link
  Coms32u4.begin(Serial2);

  Serial1.begin(115200);  // Trinket IMU link
  ComsTrinket.begin(Serial1);

#if ENABLE_ESPNOW
  WiFi.mode(WIFI_STA);
  // RC4 fix #15: the dome is <1m away — max TX power just widens the
  // collision window with Bluepad32 BT. 11dBm is plenty inside the ball.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  uint8_t wifiMac[6];
  WiFi.macAddress(wifiMac);
  Serial.printf("[ESP-NOW] Drive WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X (dome masterMAC[] must match this)\n",
                wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb(onEspNowSend);
    esp_now_register_recv_cb(onEspNowRecv);  // RC4: battery telemetry now received
    // RC4.4: BT forces WiFi modem sleep to stay ON, which left the RX window
    // shut almost always — the dome's tunnel commands got no MAC ACK (3/3
    // FAIL measured). This keeps the connectionless-RX window open full-time
    // while coexistence still schedules BT; TX was never the problem.
    esp_now_set_wake_window(65535);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, domeMACAddress, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;

    Serial.println(esp_now_add_peer(&peer) == ESP_OK
                   ? F("[ESP-NOW] Peer added successfully")
                   : F("[ESP-NOW] Failed to add peer"));
  } else {
    Serial.println(F("[ESP-NOW] Init failed"));
  }
#endif

  initMotors();
  potFiltered = analogRead(S2S_POT_PIN);

  Serial.println(F("[CAL] Boot calibration pending..."));
}

// ------------------- Combos -------------------
void toggleDriveEnabled() {
  driveEnabled = !driveEnabled;
  Serial.printf("[TOGGLE] Drive %s\n", driveEnabled ? "ENABLED" : "DISABLED");
  if (!driveEnabled) {
    autoBalance = false;
    domeFunctionEnabled = false;
    Serial.println(F("[RESET] AutoBalance and DomeFunction disabled due to Drive OFF"));
  } else {
    drivePID.reset();
    s2sPID.reset();
  }
  // RC4.6: PS toggle acknowledgement = random quick blip (70-74), unless a
  // fixed track is pinned via pref sndon/sndoff.
  {
    int t = driveEnabled ? soundDriveOn : soundDriveOff;
    sendSoundCommand(Coms32u4, sendTo32u4, t > 0 ? (uint16_t)t : pickRandomPress());
  }
}

void handleControllerCombos() {
  static bool psDomeLock = false;
  static bool crossDriveLock = false;

  // RC4.3: CIRCLE no longer toggles Drive Enable — PS does (tap = toggle,
  // hold 2 s = force-disable). CIRCLE is a plain sound button now (track 28,
  // see SoundMapping.h); L1+CIRCLE is still the silent-mode toggle.
  // (The CIRCLE toggle existed for pads whose PS button Bluepad32 doesn't
  //  report; re-add it here if such a pad turns up.)

  // RC4.1: PS tap = toggle drive; PS held >= 2 s = FORCE DISABLE.
  // Holding PS is how you power the remote off — the long-hold guarantees
  // the droid ends up disabled before the controller drops, instead of
  // the tap-toggle racing you into a random state.
  {
    static unsigned long psHoldStart = 0;
    static bool psActionDone = false;
    if (driveController.ps.held) {
      if (psHoldStart == 0) { psHoldStart = millis(); psActionDone = false; }
      if (!psActionDone && millis() - psHoldStart >= 2000) {
        psActionDone = true;
        if (driveEnabled) {
          driveEnabled = false;
          autoBalance = false;
          domeFunctionEnabled = false;
          Serial.println(F("[TOGGLE] Drive FORCE-DISABLED (PS held 2s)"));
          sendSoundCommand(Coms32u4, sendTo32u4,
                           soundDriveOff > 0 ? (uint16_t)soundDriveOff : pickRandomPress());
        }
      }
    } else {
      if (psHoldStart != 0 && !psActionDone) {
        toggleDriveEnabled();   // released before 2 s -> tap
      }
      psHoldStart = 0;
      psActionDone = false;
    }
  }

  if (domeController.ps.pressed && !psDomeLock) {
    domeFunctionEnabled = !domeFunctionEnabled;
    Serial.printf("[TOGGLE] Dome Function %s\n", domeFunctionEnabled ? "ENABLED" : "DISABLED");
    psDomeLock = true;
  }
  if (!domeController.ps.held) psDomeLock = false;

  // RC4: cross is the tuner's button while tuning — don't also toggle balance
  if (!pidTuneMode && driveController.cross.pressed && !crossDriveLock) {
    autoBalance = !autoBalance;
    Serial.printf("[TOGGLE] Auto Balance %s\n", autoBalance ? "ENABLED" : "DISABLED");
    if (autoBalance) {
      drivePID.reset();
      s2sPID.reset();
    }
    crossDriveLock = true;
  }
  if (!driveController.cross.held) crossDriveLock = false;

  flywheelMode = domeController.L1.held;

  if (bothControllersUpHeld(driveController, domeController)) {
    savePrefs();
  }
  if (bothControllersDownHeld(driveController, domeController)) {
    resetPrefs();
  }

  if (driveController.L1.held && driveController.dpadUp.held) {
    if (comboStartTime == 0) comboStartTime = millis();
    if (millis() - comboStartTime >= 3000 && !pidTuneMode) {
      pidTuneMode = true;
      tuningDrivePID = true;
      tuningS2SPID = false;
      tuningStep = 0;
      Serial.println(F("[PID TUNE] Drive PID tuning mode activated."));
      lastInputTime = millis();
    }
  } else if (driveController.L1.held && driveController.dpadLeft.held) {
    if (comboStartTime == 0) comboStartTime = millis();
    if (millis() - comboStartTime >= 3000 && !pidTuneMode) {
      pidTuneMode = true;
      tuningS2SPID = true;
      tuningDrivePID = false;
      tuningStep = 0;
      Serial.println(F("[PID TUNE] S2S PID tuning mode activated."));
      lastInputTime = millis();
    }
  } else {
    comboStartTime = 0;
  }
}

// RC4: tuning only ADJUSTS GAINS — normal control keeps running so you
// feel each change live (RC3 ran its own private PID+motor writes here).
void handlePIDTuning() {
  if (!pidTuneMode) return;

  if (millis() - lastInputTime > 15000) {
    Serial.println(F("\n[PID TUNE] Timeout. Cancelling tuning."));
    pidTuneMode = false;
    tuningDrivePID = tuningS2SPID = false;
    return;
  }

  PIDController* targetPID = tuningDrivePID ? &drivePID : &s2sPID;
  const char* pidName = tuningDrivePID ? "Drive" : "S2S";
  float kpStep = tuningDrivePID ? 0.5f : 2.0f;
  float kiStep = tuningDrivePID ? 0.5f : 1.0f;
  float kdStep = 0.05f;
  float currentVal = 0;

  static bool promptShown = false;

  switch (tuningStep) {
    case 0:
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] %s PID tuning mode.\n", pidName);
        Serial.println(F("Configure PID? Press CROSS to continue or wait to cancel."));
        promptShown = true;
      }
      if (driveController.cross.pressed) {
        tuningStep = 1;
        lastInputTime = millis();
        Serial.println(F("[PID TUNE] Starting Kp adjustment..."));
        promptShown = false;
      }
      break;

    case 1:  // Kp
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Kp: %.2f (UP/DOWN, CROSS to continue)\n", targetPID->getKp());
        promptShown = true;
      }
      currentVal = targetPID->getKp();
      if (driveController.dpadUp.pressed) {
        currentVal += kpStep;
        targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
        Serial.printf("[PID TUNE] Kp = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal = max(0.0f, currentVal - kpStep);
        targetPID->setTunings(currentVal, targetPID->getKi(), targetPID->getKd());
        Serial.printf("[PID TUNE] Kp = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKp = targetPID->getKp();
        else cfg.s2sKp = targetPID->getKp();
        tuningStep = 2;
        Serial.println(F("[PID TUNE] Kp saved. Moving to Ki..."));
        lastInputTime = millis();
        promptShown = false;
      }
      break;

    case 2:  // Ki
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Ki: %.2f (UP/DOWN, CROSS to continue)\n", targetPID->getKi());
        promptShown = true;
      }
      currentVal = targetPID->getKi();
      if (driveController.dpadUp.pressed) {
        currentVal += kiStep;
        targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
        Serial.printf("[PID TUNE] Ki = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal = max(0.0f, currentVal - kiStep);
        targetPID->setTunings(targetPID->getKp(), currentVal, targetPID->getKd());
        Serial.printf("[PID TUNE] Ki = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKi = targetPID->getKi();
        else cfg.s2sKi = targetPID->getKi();
        tuningStep = 3;
        Serial.println(F("[PID TUNE] Ki saved. Moving to Kd..."));
        lastInputTime = millis();
        promptShown = false;
      }
      break;

    case 3:  // Kd
      if (!promptShown) {
        Serial.printf("\n[PID TUNE] Adjust Kd: %.2f (UP/DOWN, CROSS to finish)\n", targetPID->getKd());
        promptShown = true;
      }
      currentVal = targetPID->getKd();
      if (driveController.dpadUp.pressed) {
        currentVal += kdStep;
        targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
        Serial.printf("[PID TUNE] Kd = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.dpadDown.pressed) {
        currentVal = max(0.0f, currentVal - kdStep);
        targetPID->setTunings(targetPID->getKp(), targetPID->getKi(), currentVal);
        Serial.printf("[PID TUNE] Kd = %.2f\n", currentVal);
        lastInputTime = millis();
      }
      if (driveController.cross.pressed) {
        if (tuningDrivePID) cfg.driveKd = targetPID->getKd();
        else cfg.s2sKd = targetPID->getKd();
        savePidOnly();  // RC4: does NOT overwrite offsets
        Serial.printf("[PID TUNE] Final %s PID: Kp=%.2f Ki=%.2f Kd=%.2f (saved)\n",
                      pidName, targetPID->getKp(), targetPID->getKi(), targetPID->getKd());
        pidTuneMode = false;
        tuningDrivePID = tuningS2SPID = false;
        promptShown = false;
      }
      break;
  }
}

// ------------------- Comms -------------------
void sendTo32u4Data() {
  sendTo32u4.driveEnabled = driveEnabled ? 1 : 0;
  sendTo32u4.autoBalance = autoBalance ? 1 : 0;
  sendTo32u4.domeFunction = domeFunctionEnabled ? 1 : 0;

  sendTo32u4.leftStickX = domeController.joyX;
  sendTo32u4.leftStickY = domeController.joyY;

  updateSendTiltValues();

  sendTo32u4.checksum = calculateChecksumSend(sendTo32u4);

  uint16_t bytes = Coms32u4.txObj(sendTo32u4, 0);
  Coms32u4.sendData(bytes);

  if (debugTo32u4) {
    Serial.printf("[TO 32u4] driveEnabled=%d autoBalance=%d domeFunction=%d DomeSpin=%d LX=%d LY=%d pitch=%.2f roll=%.2f soundcmd=%d\n",
                  sendTo32u4.driveEnabled, sendTo32u4.autoBalance, sendTo32u4.domeFunction,
                  sendTo32u4.DomeSpin, sendTo32u4.leftStickX, sendTo32u4.leftStickY,
                  sendTo32u4.pitch, sendTo32u4.roll, sendTo32u4.soundcmd);
  }

  // RC4.1: keep the pending sound in the stream for 5 packets, then clear
  if (gSoundRepeat > 0 && --gSoundRepeat == 0) {
    sendTo32u4.soundcmd = SOUND_NONE;
  }
}

// RC4: drain the link — always consume every parsed packet
uint32_t g32u4RxCount = 0;      // RC4.1: link statistics for 'debug 32u4'
uint32_t g32u4CrcErrors = 0;

void receiveFrom32u4() {
  while (Coms32u4.available()) {
    Coms32u4.rxObj(recFrom32u4);
    isPlaying = recFrom32u4.isplaying;
    last32u4Packet = millis();
    g32u4RxCount++;
  }
  if (Coms32u4.status == CRC_ERROR) {
    g32u4CrcErrors++;
  }
}

void receiveFromTrinket() {
  static unsigned long lastErrorPrint = 0;
  static int crcErrorCount = 0;

  while (ComsTrinket.available()) {
    ComsTrinket.rxObj(mpudata);
    imuHasSample = true;
    lastIMUUpdate = millis();
  }
  if (ComsTrinket.status == CRC_ERROR) {
    crcErrorCount++;
    if (millis() - lastErrorPrint > 5000) {
      Serial.printf("[ERROR] Trinket CRC_ERROR (count=%d)\n", crcErrorCount);
      lastErrorPrint = millis();
    }
  }
}

void showBuildInfoSerial(const char* prefix) {
  Serial.print(prefix);
  Serial.print(F(" | "));
  Serial.print(DEFAULT_REVISION);
  Serial.print(F(" | build "));
  Serial.print(BB8_BUILD_NUM);
  Serial.print(F(" | "));
  Serial.print(F(BB8_BUILD_DATE));
  Serial.print(F(" | git "));
  Serial.println(F(BB8_BUILD_GIT));
}

// ------------------- Debug / Telemetry -------------------
void printDebugInfo() {
  // RC4: all debug output rate-limited to 10 Hz
  static unsigned long lastDbg = 0;
  if (millis() - lastDbg < 100) return;
  bool any = debugTo32u4 || debugFrom32u4 || debug32u4 || debugMPU || debugS2S || debugDrive || debugControllersFlag || debugFlywheel;
  if (!any) return;
  lastDbg = millis();

  if (debugFrom32u4) {
    Serial.printf("[FROM 32u4] dometilt=%.2f isplaying=%d domedirection=%d\n",
                  recFrom32u4.dometilt, recFrom32u4.isplaying, recFrom32u4.domedirection);
  }
  // RC4.1: 'debug 32u4' = link HEALTH once per second (the old flag only
  // gated CRC-error prints, so a healthy or dead link both showed nothing)
  if (debug32u4) {
    static unsigned long lastLink = 0;
    static uint32_t lastCount = 0;
    if (millis() - lastLink >= 1000) {
      uint32_t pps = g32u4RxCount - lastCount;
      lastCount = g32u4RxCount;
      lastLink = millis();
      long age = last32u4Packet ? (long)(millis() - last32u4Packet) : -1;
      Serial.printf("[32u4-LINK] rx=%lu pkt/s lastPkt=%ldms crcErrs=%lu isplaying=%d domedir=%d %s\n",
                    (unsigned long)pps, age, (unsigned long)g32u4CrcErrors,
                    recFrom32u4.isplaying, recFrom32u4.domedirection,
                    (age < 0) ? "(NEVER — check wiring/baud)" : (age > 500 ? "(STALE)" : "OK"));
    }
  }
  if (debugMPU) {
    Serial.printf("[MPU] rawX=%.2f rawY=%.2f rawZ=%.2f pitch=%.2f roll=%.2f pot=%d\n",
                  mpudata.rawX, mpudata.rawY, mpudata.rawZ,
                  mpudata.pitch, mpudata.roll, (int)potFiltered);
  }
  if (debugS2S) {
    Serial.printf("[S2S] PWM=%u Dir=%s pot=%d target=%d roll=%.2f\n",
                  gS2SDuty, (gS2SDirFwd ? "FWD" : "REV"), (int)potFiltered,
                  gS2STargetPot, mpudata.roll + cfg.rollOffset);
  }
  if (debugDrive) {
    Serial.printf("[Drive] PWM=%u Dir=%s pitch=%.2f joySlew=%.0f\n",
                  gDriveDuty, (gDriveDirFwd ? "FWD" : "REV"),
                  mpudata.pitch + cfg.pitchOffset, drivePwmState);
  }
  if (debugFlywheel) {
    Serial.printf("[Flywheel] PWM=%d Direction=%s Mode=%d\n",
                  gFlywheelPWM, gFlywheelDirFwd ? "FWD" : "REV", flywheelMode);
  }
  if (debugControllersFlag) {
    Serial.printf("Drive| RX=%5d RY=%5d LX=%4d LY=%4d L2=%4d PS=%d L1=%d X=%d O=%d U=%d D=%d L=%d R=%d\n",
                  driveController.rawX, driveController.rawY,
                  driveController.joyX, driveController.joyY, driveController.L2,
                  driveController.ps.held, driveController.L1.held,
                  driveController.cross.held, driveController.circle.held,
                  driveController.dpadUp.held, driveController.dpadDown.held,
                  driveController.dpadLeft.held, driveController.dpadRight.held);
    Serial.printf("Dome | RX=%5d RY=%5d LX=%4d LY=%4d L2=%4d PS=%d L1=%d X=%d O=%d U=%d D=%d L=%d R=%d\n",
                  domeController.rawX, domeController.rawY,
                  domeController.joyX, domeController.joyY, domeController.L2,
                  domeController.ps.held, domeController.L1.held,
                  domeController.cross.held, domeController.circle.held,
                  domeController.dpadUp.held, domeController.dpadDown.held,
                  domeController.dpadLeft.held, domeController.dpadRight.held);
  }
}

// RC4 fix #12: plotter/tool-friendly telemetry — 20 Hz, or 100 Hz in
// 'telemetry fast' mode for rig captures / system ID. t = millis for a
// proper time base offline; exp = active experiment mode (0 = none).
void serviceTelemetry() {
  if (!telemetryEnabled) return;
  static unsigned long lastTlm = 0;
  unsigned long interval = telemetryFast ? 10 : 50;
  if (millis() - lastTlm < interval) return;
  lastTlm = millis();

  Serial.printf("t:%lu,exp:%d,", (unsigned long)millis(), (int)exper.mode);
  Serial.printf("pitch:%.2f,roll:%.2f,pot:%d,tgt:%d,drv:%d,s2s:%d,fly:%d,en:%d,bal:%d,jx:%d,jy:%d,hz:%u\n",
                mpudata.pitch + cfg.pitchOffset,
                mpudata.roll + cfg.rollOffset,
                (int)potFiltered, gS2STargetPot,
                (int)(gDriveDirFwd ? gDriveDuty : -(int)gDriveDuty),
                (int)(gS2SDirFwd ? gS2SDuty : -(int)gS2SDuty),
                gFlywheelPWM,
                driveEnabled ? 1 : 0, autoBalance ? 1 : 0,
                driveController.joyX, driveController.joyY,
                loopHz);
}

// ------------------- Loop -------------------
// RC4.7: idle personality + dome-battery watch. Chatter only with a pad
// connected (same anti-phantom rule as handleSoundTriggers) and only after
// the sticks/buttons have been quiet for the configured time.
void serviceExtras() {
  unsigned long now = millis();

  static unsigned long nextChatterAt = 0;
  bool padOn = myControllers[0] && myControllers[0]->isConnected();
  if (idleChatterSec > 0 && padOn &&
      (now - lastInputTime) > (unsigned long)idleChatterSec * 1000UL) {
    if (nextChatterAt == 0) nextChatterAt = now + (unsigned long)random(5000, 20000);
    if ((long)(now - nextChatterAt) >= 0) {
      sendSoundCommand(Coms32u4, sendTo32u4, pickRandom1to30());
      nextChatterAt = now + (unsigned long)random(15000, 45000);
    }
  } else {
    nextChatterAt = 0;
  }

  static unsigned long lastBatWarnMs = 0;
  if (batLowVolts > 0.5f && lastBatteryVoltage > 0.5f &&
      lastBatteryVoltage < batLowVolts && now - lastBatWarnMs > 60000UL) {
    lastBatWarnMs = now;
    Serial.printf("[BAT] dome battery %.2f V is below the %.2f V threshold\n",
                  lastBatteryVoltage, batLowVolts);
    if (padOn) sendSoundCommand(Coms32u4, sendTo32u4, pickRandomAlert());
  }
}

void loop() {

  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
  }

  // Loop-rate measurement
  static unsigned long hzWindowStart = 0;
  static uint16_t hzCount = 0;
  hzCount++;
  if (millis() - hzWindowStart >= 1000) {
    loopHz = hzCount;
    hzCount = 0;
    hzWindowStart = millis();
  }

  serviceBootCalibration();

  BP32.update();
  updateControllers();
  handleControllerCombos();
  handlePIDTuning();          // gain adjustments only; control keeps running

  receiveFromTrinket();       // drain IMU link
  receiveFrom32u4();          // drain 32u4 link
  serviceS2SCenterCalibration();

  // RC4 fix #2: fixed-rate control tick with measured dt
  static uint32_t lastCtrlUs = 0;
  uint32_t nowUs = micros();
  if (nowUs - lastCtrlUs >= CONTROL_PERIOD_US) {
    float dt = (lastCtrlUs == 0) ? 0.01f : (nowUs - lastCtrlUs) / 1000000.0f;
    lastCtrlUs = nowUs;
    runControl(dt);
  }

  handleSoundTriggers();

  handleDomeAndBodyLights();  // compute + latch only
  serviceEspNow();            // non-blocking send

  // Send to 32u4 at 50 Hz (RC3 was 20 Hz — smoother dome tilt)
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 20) {
    sendTo32u4Data();
    lastSend = millis();
  }

  // RC4: non-blocking command reader (readStringUntil could stall the
  // control loop up to 1 s on a partial line)
  {
    static String cmdBuf;
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n') {
        cmdBuf.trim();
        if (cmdBuf.length()) handleSerialCommand(cmdBuf);
        cmdBuf = "";
      } else if (c != '\r' && cmdBuf.length() < 96) {
        cmdBuf += c;
      }
    }
  }

  printDebugInfo();
  serviceTelemetry();

  // RC4.7 services: OTA writes, macro steps, idle/battery, black box
  OtaRx::service();
  macroService();
  serviceExtras();
  BlackBox::record(mpudata.pitch + cfg.pitchOffset, mpudata.roll + cfg.rollOffset,
                   (int)potFiltered, gS2STargetPot, drivePwmState,
                   (uint8_t)((driveEnabled ? 1 : 0) | (autoBalance ? 2 : 0)));

  vTaskDelay(1);
}
