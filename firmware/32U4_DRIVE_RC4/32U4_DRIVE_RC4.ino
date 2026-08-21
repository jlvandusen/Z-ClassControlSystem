// ============================================================
//  32U4_DRIVE_RC4  —  Body node (Feather 32u4)
//  2x dome-tilt servos, dome spin motor + encoder, DFPlayer audio
//
//  RC4 changes vs RC3 (dome-tilt jerkiness fixes):
//   1. FIXED: '#define FILTERMPU false' + '#ifdef FILTERMPU' was
//      ALWAYS TRUE (ifdef tests existence, not value). Now uses
//      '#if FILTERMPU' with 0/1.
//   2. Servos no longer jump straight to target on each packet.
//      A 15 ms easing tick slews current position toward target
//      at SERVO_MAX_DEG_PER_S with writeMicroseconds() for
//      ~0.1 deg resolution (was: integer-degree writes gated by a
//      1.8 deg hysteresis -> visible staircase steps).
//   3. map() on floats (integer truncation) replaced by fmap().
//   4. Joystick deadband now RESCALES beyond the deadband instead
//      of jumping from 0 to ~5 deg at the threshold.
//   5. RX drained every loop (apply latest packet), servo motion
//      continues between packets (was: motion only on packet).
//   6. sendToESP32() rate-limited to 20 Hz (was: every loop
//      iteration -> thousands of packets/s flooding the 74880
//      link, starving the ESP32 RX and adding latency).
//   7. Dome spin motor PWM is slew-limited (no clunk on reversals).
//   8. Watchdog timeout now actually 2 s (comment said 2 s, code
//      had 5 s).
// ============================================================

#include <Arduino.h>
#include <SerialTransfer.h>
#include <Servo.h>
#include "DFRobotDFPlayerMini.h"
#include "SoftwareSerial.h"
#include "BuildStamp.h"

// ------------------- CONFIG -------------------

#define REVERSE_LEFT_SERVO  false
#define REVERSE_RIGHT_SERVO false
#define REVERSE_AUTO_BALANCE false   // Reverse auto-balance direction
#define DOME_SERVO_LIMIT 80
#define DOME_SERVO_RETURN_PWM 120
#define JOYSTICK_DEADBAND 5
#define FILTERMPU 0                  // RC4: 0/1 value, tested with #if

// RC4: servo motion profile
const float SERVO_MAX_DEG_PER_S = 220.0f;  // slew limit (visible smoothness knob)
const float SERVO_SMOOTH_ALPHA  = 0.35f;   // target low-pass (0..1, higher = snappier)
const unsigned long SERVO_TICK_MS = 15;    // easing update period (~66 Hz)

// ------------------- PIN ASSIGNMENTS -------------------
#define DFPLAYER_TX_PIN      22
#define DFPLAYER_RX_PIN      9
#define DFPLAYER_BUSY_PIN    10
#define DFPLAYER_BAUD        9600
#define AUDIO_DEFAULT_VOLUME 25

#define leftServo_pin 12
#define rightServo_pin 11
#define domeMotor_pwm 3
#define domeMotor_pin_A 5
#define domeMotor_pin_B 6

#define motorEncoder_pin_A 2
#define motorEncoder_pin_B A0
#define hallEffectSensor_Pin 20

// ------------------- CONFIGURABLE DEFAULTS -------------------
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC4 DRIVE";
static const char* DEFAULT_REVISION_DATE = "2026-08-20";

const unsigned long SHOW_AFTER_MS = 5000;

bool shownAfterWait = false;
unsigned long bootMs = 0;

// ------------------- GLOBALS -------------------

unsigned long lastPacketTime = 0;
volatile long encoderCount = 0;
volatile int domeDirection = 0;
float angleOffset = 0;

bool servosWereAttached = true;

// ------------------- ENCODER CONFIG -------------------
#define FORWARD_ANGLE 180.0   // Forward reference angle in degrees
#define ZERO_TOLERANCE 5.0    // Deadband tolerance for return-to-center
#define SERVO_LIMIT 70.0      // Max allowed rotation from forward

const int ENCODER_COUNTS_PER_REV = 840;
const float COUNTS_PER_DEGREE = ENCODER_COUNTS_PER_REV / 360.0;

SerialTransfer ComsESP32;
Servo leftServo, rightServo;
DFRobotDFPlayerMini mp3;
int currentVolume = AUDIO_DEFAULT_VOLUME;

SoftwareSerial mp3Serial(DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

// ------------------- STATE -------------------
bool debugMode = false;
bool debugEncoder = false;
bool silentMode = false;
bool bootSoundPlayed = false;
bool dfPlayerReady = false;
bool domeServoMode = false;

// ------------------- STRUCTS -------------------
#pragma pack(push, 1)
typedef struct {
  bool driveEnabled;
  bool driveReverse;
  bool autoBalance;
  bool domeFunction;
  int8_t DomeSpin, leftStickX, leftStickY, soundcmd;
  float pitch, roll;
  uint8_t functionnumber;
  uint16_t checksum;
} RecFromESP32;
#pragma pack(pop)
RecFromESP32 incoming;

#pragma pack(push, 1)
typedef struct {
    bool isplaying;
    bool domedirection;
    float dometilt;
    uint16_t checksum;
} SendToESP32;
#pragma pack(pop)

SendToESP32 outgoing;

uint16_t calculateChecksumOutgoing(const SendToESP32 &d) {
    const uint8_t* p = (const uint8_t*)&d;
    uint16_t s = 0;
    for (size_t i = 0; i < sizeof(d) - sizeof(d.checksum); i++) s += p[i];
    return s;
}

// Servo config
const int16_t leftServo_0_Position  = 70;
const int16_t rightServo_0_Position = 110;

// RC4: easing state — smoothed target and slewed actual, in degrees (float)
float leftTargetDeg   = leftServo_0_Position;
float rightTargetDeg  = rightServo_0_Position;
float leftSmoothDeg   = leftServo_0_Position;
float rightSmoothDeg  = rightServo_0_Position;
float leftActualDeg   = leftServo_0_Position;
float rightActualDeg  = rightServo_0_Position;

const int SERVO_MIN = -30;
const int SERVO_MAX = 30;

// RC4: dome spin slew state
int domeMotorPWM = 0;          // signed: + = clockwise
const int DOME_PWM_SLEW_PER_TICK = 18;   // per SERVO_TICK_MS (~full scale in 210 ms)

// RC4 fix: encoderCount is a 4-byte long updated in an ISR — reading it
// directly on 8-bit AVR is a torn read (random spin twitches). Always
// read through this atomic helper.
static inline long readEncoderAtomic() {
  noInterrupts();
  long v = encoderCount;
  interrupts();
  return v;
}

// RC4: float-safe map
static inline float fmap(float x, float inMin, float inMax, float outMin, float outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// RC4: degrees -> microseconds (Servo default attach range 544..2400)
static inline int degToUs(float deg) {
  if (deg < 0) deg = 0;
  if (deg > 180) deg = 180;
  return (int)(544.0f + deg * (2400.0f - 544.0f) / 180.0f);
}

// RC4: joystick deadband with rescale (continuous, no jump at threshold)
static inline float stickToTilt(int8_t v, int deadband) {
  float a = (float)abs(v);
  if (a <= deadband) return 0.0f;
  float scaled = (a - deadband) / (128.0f - deadband);   // 0..1
  if (scaled > 1.0f) scaled = 1.0f;
  // RC3 sign convention: +stick maps toward SERVO_MIN
  float t = scaled * SERVO_MAX;
  return (v > 0) ? -t : t;
}

// ------------------- SETUP -------------------
void setup() {

  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
      ; // Wait max 2 seconds for Serial
  }
  Serial.println(F("[BOOT] Serial ready"));

  bootMs = millis();
  showBuildInfoSerial("BOOT");

  Serial1.begin(74880);
  ComsESP32.begin(Serial1);

  leftServo.attach(leftServo_pin);
  rightServo.attach(rightServo_pin);
  leftServo.writeMicroseconds(degToUs(leftServo_0_Position));
  rightServo.writeMicroseconds(degToUs(rightServo_0_Position));

  pinMode(DFPLAYER_BUSY_PIN, INPUT_PULLUP);
  initAudio();

  pinMode(domeMotor_pin_A, OUTPUT);
  pinMode(domeMotor_pin_B, OUTPUT);
  pinMode(domeMotor_pwm, OUTPUT);

  pinMode(motorEncoder_pin_A, INPUT_PULLUP);
  pinMode(motorEncoder_pin_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(motorEncoder_pin_A), encoderISR, CHANGE);

  // RC4 fix: boot position IS the forward position. RC3 omitted
  // FORWARD_ANGLE here, so servo mode saw "0 deg" vs forward=180 and
  // commanded an unrequested half-revolution at startup.
  angleOffset = (encoderCount / COUNTS_PER_DEGREE) - FORWARD_ANGLE;
  Serial.println(F("[BOOT] Dome encoder initialized. Boot position = forward (180 deg)."));

  if (!bootSoundPlayed && dfPlayerReady) {
      delay(1000);
      mp3.playMp3Folder(1);
      Serial.println(F("[AUDIO] Boot sound played."));
      bootSoundPlayed = true;
  } else Serial.println(F("[AUDIO] Boot sound not played."));

  Serial.println(F("Ready — Dome Controller Active (RC4)"));
}

// ------------------- LOOP -------------------
void loop() {

  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
  }

  // RC4: print the banner whenever a serial monitor (re)attaches —
  // 32u4 USB-CDC exposes the host connection as (bool)Serial
  {
    static bool usbWasConnected = true;
    bool usbNow = (bool)Serial;
    if (usbNow && !usbWasConnected) showBuildInfoSerial("CONNECT");
    usbWasConnected = usbNow;
  }

  // RC4: drain ALL pending packets, keep the newest state
  bool gotPacket = false;
  while (ComsESP32.available()) {
    ComsESP32.rxObj(incoming);
    gotPacket = true;
  }
  if (gotPacket) {
    lastPacketTime = millis();
    updateTiltTargets();     // RC4: packets only update TARGETS
  }
  handleAudio();             // RC4.1: every loop — services latched sound
                             // commands even between packets

  // RC4: motion runs on its own clock, independent of packet arrival
  static unsigned long lastServoTick = 0;
  if (millis() - lastServoTick >= SERVO_TICK_MS) {
    lastServoTick = millis();
    serviceServoEasing();
    serviceDomeSpinMotor();
  }

  // Watchdog: neutral everything if no packet for >2s
  if (millis() - lastPacketTime > 2000) {
    leftTargetDeg  = leftServo_0_Position;
    rightTargetDeg = rightServo_0_Position;
    domeMotorPWM = 0;
    incoming.driveEnabled = false;
    incoming.DomeSpin = 0;
  }

  handleSerialCommands();
  printDebugInfo();

  // Drain DFPlayer responses to prevent buffer overflow
  while (mp3.available()) {
      mp3.readType();
      mp3.read();
  }

  // RC4: reply at 20 Hz, not every loop iteration
  static unsigned long lastReply = 0;
  if (millis() - lastReply >= 50) {
    lastReply = millis();
    sendToESP32();
  }
}

void sendToESP32() {
    outgoing.isplaying = dfPlayerReady && (digitalRead(DFPLAYER_BUSY_PIN) == LOW);
    outgoing.dometilt = 0.0f;
    outgoing.domedirection = domeDirection > 0;
    outgoing.checksum = calculateChecksumOutgoing(outgoing);

    uint16_t bytes = ComsESP32.txObj(outgoing, 0);
    ComsESP32.sendData(bytes);
}

void initAudio() {
    mp3Serial.begin(DFPLAYER_BAUD);

    if (digitalRead(DFPLAYER_BUSY_PIN) == LOW) {
        Serial.println(F("[AUDIO] DFPlayer busy or no SD card detected. Skipping init."));
        dfPlayerReady = false;
        return;
    }

    // RC4 fix: ACK off — DFPlayer ACK replies arrive on SoftwareSerial RX,
    // whose bit-banged receive disables interrupts ~1 ms per byte and
    // visibly jitters the Servo library's timer pulses (tilt twitch).
    if (mp3.begin(mp3Serial, /*isACK=*/false, /*doReset=*/true)) {
        mp3.volume(currentVolume);
        dfPlayerReady = true;
        Serial.println(F("[AUDIO] DFPlayer initialized."));
    } else {
        dfPlayerReady = false;
        Serial.println(F("[AUDIO] DFPlayer init failed. Skipping audio."));
    }
}

void showBuildInfoSerial(const char* prefix)
{
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

// ------------------- FUNCTIONS -------------------
void encoderISR() {
  bool A = digitalRead(motorEncoder_pin_A);
  bool B = digitalRead(motorEncoder_pin_B);
  if (A == B) { domeDirection = -1; encoderCount--; }
  else { domeDirection = 1; encoderCount++; }
}

// RC4: compute tilt TARGETS from the freshest packet (no servo writes here)
void updateTiltTargets() {

    if (!incoming.driveEnabled) {
        leftTargetDeg  = leftServo_0_Position;
        rightTargetDeg = rightServo_0_Position;
        return;
    }

    float tiltX = 0.0f, tiltY = 0.0f;
    float pitch = incoming.pitch;
    float roll  = incoming.roll;

#if FILTERMPU
    static float filtPitch = 0.0f;
    static float filtRoll  = 0.0f;
    const float ALPHA = 0.35f;
    filtPitch = ALPHA * incoming.pitch + (1 - ALPHA) * filtPitch;
    filtRoll  = ALPHA * incoming.roll  + (1 - ALPHA) * filtRoll;
    pitch = filtPitch;
    roll  = filtRoll;
#endif

    if (incoming.autoBalance) {
        // RC4: float math, clamped (was integer map() truncation)
        tiltX = constrain(roll,  (float)SERVO_MIN, (float)SERVO_MAX);
        tiltY = constrain(pitch, (float)SERVO_MIN, (float)SERVO_MAX);

        tiltX = REVERSE_AUTO_BALANCE ? -tiltX : tiltX;
        tiltY = REVERSE_AUTO_BALANCE ? tiltY : -tiltY;
    } else {
        // RC4: continuous deadband rescale (was jump at threshold 20)
        tiltX = stickToTilt(incoming.leftStickX, 12);
        tiltY = stickToTilt(incoming.leftStickY, 12);
    }

    float l, r;
    if (REVERSE_LEFT_SERVO) {
        l = leftServo_0_Position - tiltY - tiltX;
    } else {
        l = leftServo_0_Position + tiltY + tiltX;
    }
    if (REVERSE_RIGHT_SERVO) {
        r = rightServo_0_Position + tiltY - tiltX;
    } else {
        r = rightServo_0_Position - tiltY + tiltX;
    }

    leftTargetDeg  = constrain(l, 40.0f, 100.0f);
    rightTargetDeg = constrain(r, 80.0f, 140.0f);
}

// RC4: easing tick — low-pass the target, slew the output, write in µs
void serviceServoEasing() {
  // 1) smooth the target (kills packet-rate steps)
  leftSmoothDeg  += SERVO_SMOOTH_ALPHA * (leftTargetDeg  - leftSmoothDeg);
  rightSmoothDeg += SERVO_SMOOTH_ALPHA * (rightTargetDeg - rightSmoothDeg);

  // 2) slew-limit actual motion
  const float maxStep = SERVO_MAX_DEG_PER_S * (SERVO_TICK_MS / 1000.0f);

  float dl = leftSmoothDeg - leftActualDeg;
  if (dl >  maxStep) dl =  maxStep;
  if (dl < -maxStep) dl = -maxStep;
  leftActualDeg += dl;

  float dr = rightSmoothDeg - rightActualDeg;
  if (dr >  maxStep) dr =  maxStep;
  if (dr < -maxStep) dr = -maxStep;
  rightActualDeg += dr;

  // 3) µs-resolution writes (was integer degrees + 1.8° hysteresis)
  leftServo.writeMicroseconds(degToUs(leftActualDeg));
  rightServo.writeMicroseconds(degToUs(rightActualDeg));
}

// RC4: dome spin with PWM slew limiting; runs on the easing tick
void serviceDomeSpinMotor() {
    int targetPWM = 0;   // signed

    if (incoming.driveEnabled) {
        domeServoMode = incoming.domeFunction;
        int speed = incoming.DomeSpin;

        float currentAngle = (readEncoderAtomic() / COUNTS_PER_DEGREE) - angleOffset;
        while (currentAngle >= 360) currentAngle -= 360;
        while (currentAngle < 0) currentAngle += 360;

        if (domeServoMode) {
            if (abs(speed) <= JOYSTICK_DEADBAND) {
                // Return to forward if outside tolerance
                if (fabs(currentAngle - FORWARD_ANGLE) > ZERO_TOLERANCE) {
                    bool clockwise = (currentAngle < FORWARD_ANGLE);
                    targetPWM = clockwise ? DOME_SERVO_RETURN_PWM : -DOME_SERVO_RETURN_PWM;
                }
            } else if ((currentAngle > FORWARD_ANGLE + SERVO_LIMIT) ||
                       (currentAngle < FORWARD_ANGLE - SERVO_LIMIT)) {
                targetPWM = 0;  // at limit
            } else {
                int mag = map(abs(speed), 0, 128, 0, 255);
                targetPWM = (speed > 0) ? mag : -mag;
            }
        } else {
            if (abs(speed) > JOYSTICK_DEADBAND) {
                int mag = map(abs(speed), 0, 128, 0, 255);
                targetPWM = (speed > 0) ? mag : -mag;
            }
        }
    }

    // Slew toward target
    int delta = targetPWM - domeMotorPWM;
    if (delta >  DOME_PWM_SLEW_PER_TICK) delta =  DOME_PWM_SLEW_PER_TICK;
    if (delta < -DOME_PWM_SLEW_PER_TICK) delta = -DOME_PWM_SLEW_PER_TICK;
    domeMotorPWM += delta;

    if (domeMotorPWM == 0) {
        stopDomeMotor();
    } else {
        setDomeMotor(domeMotorPWM > 0, abs(domeMotorPWM));
    }
}

void setDomeMotor(bool clockwise, int pwm) {
  digitalWrite(domeMotor_pin_A, clockwise ? LOW : HIGH);
  digitalWrite(domeMotor_pin_B, clockwise ? HIGH : LOW);
  analogWrite(domeMotor_pwm, pwm);
}

void stopDomeMotor() {
  analogWrite(domeMotor_pwm, 0);
  digitalWrite(domeMotor_pin_A, LOW);
  digitalWrite(domeMotor_pin_B, LOW);
}

// RC4.1: fixed "toggle sound only plays every few presses".
// RC3 stacked three suppressors here:
//   - 'soundcmd != lastTrack' silently blocked ANY repeat of the same
//     sound until a different one played in between (the main culprit),
//   - '!isPlayingNow' DROPPED commands while a clip was still playing
//     (long MP3s widened that window),
//   - deferred commands sat in incoming.soundcmd, where the next 50 Hz
//     state packet (soundcmd=0) overwrote them ~20 ms later.
// Now: commands are LATCHED immediately, repeats are allowed, and a new
// command plays at once (interrupting the current clip — right feel for
// toggle feedback).
int8_t pendingSound = 0;

void handleAudio() {
    // RC4.1: sound commands arrive REPEATED in 5 consecutive state packets
    // with a sequence number in functionnumber (DFPlayer SoftwareSerial
    // activity corrupts occasional link packets — any 1-of-5 arriving is
    // enough). Latch on sequence CHANGE only, so repeats are ignored.
    static uint8_t lastSoundSeq = 0;
    if (incoming.soundcmd != 0 && incoming.functionnumber != lastSoundSeq) {
        lastSoundSeq = incoming.functionnumber;
        pendingSound = incoming.soundcmd;
    }

    if (!dfPlayerReady) {
        if (pendingSound != 0) {
            Serial.println(F("[AUDIO] Ignoring sound command - DFPlayer not ready"));
            pendingSound = 0;
        }
        return;
    }

    static unsigned long lastCommandTime = 0;

    if (pendingSound != 0 && millis() - lastCommandTime >= 250) {
        int cmdv = pendingSound;
        pendingSound = 0;
        lastCommandTime = millis();

        Serial.print(F("[AUDIO] Received cmd = "));
        Serial.println(cmdv);

        if (cmdv == 100) {
            silentMode = !silentMode;
            currentVolume = silentMode ? 0 : AUDIO_DEFAULT_VOLUME;
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Silent mode "));
            Serial.println(silentMode ? F("ON") : F("OFF"));
        }
        else if (cmdv == 92) {
            currentVolume = min(currentVolume + 1, 30);
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Volume up -> "));
            Serial.println(currentVolume);
        }
        else if (cmdv == 93) {
            currentVolume = max(currentVolume - 1, 0);
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Volume down -> "));
            Serial.println(currentVolume);
        }
        else if (!silentMode && cmdv > 0 && cmdv < 100) {
            Serial.print(F("[AUDIO] Playing folder track: "));
            Serial.println(cmdv);
            mp3.playMp3Folder(cmdv);
        }
        else {
            Serial.println(F("[AUDIO] Command ignored (silent / invalid)"));
        }
    }
}

void handleSerialCommands() {
  // RC4 fix: readStringUntil() could block the whole loop for up to 1 s
  // on a partial line. Accumulate characters non-blocking instead.
  static String cmdBuf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c != '\n') {
      if (c != '\r' && cmdBuf.length() < 64) cmdBuf += c;
      continue;
    }
    String cmd = cmdBuf;
    cmdBuf = "";
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "version") {
      showBuildInfoSerial("VERSION");
    }
    else if (cmd == "debug") {
      debugMode = !debugMode;
      Serial.println(debugMode ? F("DEBUG ON") : F("DEBUG OFF"));
    }
    else if (cmd == "center") {
      leftTargetDeg  = leftServo_0_Position;
      rightTargetDeg = rightServo_0_Position;
    }
    else if (cmd.startsWith("play")) {
      int track = cmd.substring(5).toInt();
      if (track > 0) mp3.playMp3Folder(track);
    }
    else if (cmd == "debug encoder") {
      debugEncoder = !debugEncoder;
      Serial.println(debugEncoder ? F("[ENCODER DEBUG] ON") : F("[ENCODER DEBUG] OFF"));
    }
    else if (cmd == "set zero") {
        angleOffset = (readEncoderAtomic() / COUNTS_PER_DEGREE) - FORWARD_ANGLE;
        Serial.println(F("[ENCODER] Forward position set at 180 deg."));
    }
    else if (cmd == "help") {
        Serial.println(F("Commands: help, version, debug, debug encoder, center, set zero, play <n>"));
    }
    else if (cmd.length() > 0) {
        // RC4.1: never answer with silence — old firmware did, making it
        // impossible to tell 'unknown command' from 'not listening'
        Serial.println(F("[?] Unknown command. Type 'help'."));
    }
  }  // while Serial.available
}

void printDebugInfo() {
  // RC4: debug prints rate-limited to 10 Hz (were every loop)
  static unsigned long lastDbg = 0;
  if (millis() - lastDbg < 100) return;

  if (debugEncoder) {
    lastDbg = millis();
    float currentAngle = (readEncoderAtomic() / COUNTS_PER_DEGREE) - angleOffset;
    while (currentAngle >= 360) currentAngle -= 360;
    while (currentAngle < 0) currentAngle += 360;

    Serial.print(F("[ENCODER] Raw="));
    Serial.print(readEncoderAtomic());
    Serial.print(F(" | Angle="));
    Serial.print(currentAngle, 2);
    Serial.print(F(" | Dir="));
    Serial.print((currentAngle > FORWARD_ANGLE) ? F("Right") : F("Left"));
    Serial.print(F(" | domeServoMode="));
    Serial.println(domeServoMode ? F("TRUE") : F("FALSE"));
  }
  else if (debugMode) {
    lastDbg = millis();
    Serial.print(F("[DBG] "));
    Serial.print(F("drive=")); Serial.print(incoming.driveEnabled);
    Serial.print(F(" autoBal=")); Serial.print(incoming.autoBalance);
    Serial.print(F(" domeFunc=")); Serial.print(incoming.domeFunction);
    Serial.print(F(" DomeSpin=")); Serial.print(incoming.DomeSpin);
    Serial.print(F(" LX=")); Serial.print(incoming.leftStickX);
    Serial.print(F(" LY=")); Serial.print(incoming.leftStickY);
    Serial.print(F(" pitch=")); Serial.print(incoming.pitch, 1);
    Serial.print(F(" roll=")); Serial.print(incoming.roll, 1);
    Serial.print(F(" BUSY=")); Serial.print(digitalRead(DFPLAYER_BUSY_PIN));
    Serial.print(F(" ServoL=")); Serial.print(leftActualDeg, 1);
    Serial.print(F(" ServoR=")); Serial.print(rightActualDeg, 1);
    Serial.print(F(" DomePWM=")); Serial.print(domeMotorPWM);
    Serial.print(F(" Enc=")); Serial.print(readEncoderAtomic());
    Serial.print(F(" Vol=")); Serial.print(currentVolume);
    Serial.print(F(" Silent=")); Serial.println(silentMode);
  }
}
