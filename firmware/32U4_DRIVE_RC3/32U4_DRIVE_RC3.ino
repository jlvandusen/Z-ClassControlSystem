
#include <Arduino.h>
#include <SerialTransfer.h>
#include <Servo.h>
#include "DFRobotDFPlayerMini.h"
#include "SoftwareSerial.h"

// ------------------- CONFIG -------------------

#define REVERSE_LEFT_SERVO  false
#define REVERSE_RIGHT_SERVO false
#define REVERSE_AUTO_BALANCE false   // Reverse auto-balance direction
#define DOME_SERVO_LIMIT 80
#define DOME_SERVO_RETURN_PWM 120
#define JOYSTICK_DEADBAND 5
#define AUTO_RETURN_DELAY 200
#define SERVO_HYSTERESIS 1.8
#define FILTERMPU false

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
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC3 DRIVE";
static const char* DEFAULT_REVISION_DATE = "2026-03-02";

const unsigned long SHOW_AFTER_MS = 5000;

bool shownAfterWait = false;
unsigned long bootMs = 0;


// ------------------- GLOBALS -------------------

unsigned long lastPacketTime = 0;
volatile long encoderCount = 0;
volatile int domeDirection = 0;
float angleOffset = 0;

static float filtPitch = 0.0f;
static float filtRoll  = 0.0f;

static bool lastPlayingState = false;          // Tracks previous BUSY state
static unsigned long lastStateChange = 0;      // For debounce timing
const unsigned long BUSY_DEBOUNCE_MS = 60;     // 60ms works well for most DFPlayer modules
bool servosWereAttached = true;  // Start assuming servos are attached after boot

// ------------------- ENCODER CONFIG -------------------
#define FORWARD_ANGLE 180.0   // Forward reference angle in degrees
#define ZERO_TOLERANCE 5.0    // Deadband tolerance for return-to-center
#define SERVO_LIMIT 70.0      // Max allowed rotation from forward


const int ENCODER_COUNTS_PER_REV = 840;
const float COUNTS_PER_DEGREE = ENCODER_COUNTS_PER_REV / 360.0;
// const float ZERO_TOLERANCE = 5.0; // degrees

SerialTransfer ComsESP32;
Servo leftServo, rightServo;
DFRobotDFPlayerMini mp3;
int currentVolume = AUDIO_DEFAULT_VOLUME;

// Global declaration
SoftwareSerial mp3Serial(DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

// ------------------- STATE -------------------
bool debugMode = false;
bool debugEncoder = false;      // For continuous encoder debug
bool silentMode = false;
bool bootSoundPlayed = false;
bool dfPlayerReady = false;
bool domeServoMode = false;     // For servo-limited dome spin mode


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
int16_t leftServo_0_Position  = 70;
int16_t rightServo_0_Position = 110;
double leftServoPosition  = leftServo_0_Position;
double rightServoPosition = rightServo_0_Position;
const int SERVO_MIN = -30;
const int SERVO_MAX = 30;
static double lastLeftPos  = leftServo_0_Position;
static double lastRightPos = rightServo_0_Position;

// ------------------- SETUP -------------------
void setup() {
    
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
      ; // Wait max 2 seconds for Serial
  }
  Serial.println(F("[BOOT] Serial ready"));

  bootMs = millis();

  // Display right after reboot (every boot)
  showBuildInfoSerial("BOOT");


  Serial1.begin(74880);
  ComsESP32.begin(Serial1);

  leftServo.attach(leftServo_pin);
  rightServo.attach(rightServo_pin);
  leftServo.write(leftServo_0_Position);
  rightServo.write(rightServo_0_Position);

  pinMode(DFPLAYER_BUSY_PIN, INPUT_PULLUP);
  // SoftwareSerial mp3Serial(DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  initAudio();
  
  pinMode(domeMotor_pin_A, OUTPUT);
  pinMode(domeMotor_pin_B, OUTPUT);
  pinMode(domeMotor_pwm, OUTPUT);

  pinMode(motorEncoder_pin_A, INPUT_PULLUP);
  pinMode(motorEncoder_pin_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(motorEncoder_pin_A), encoderISR, CHANGE);

  angleOffset = encoderCount / COUNTS_PER_DEGREE;
  Serial.println("[BOOT] Dome encoder initialized. Zero offset set.");


  if (!bootSoundPlayed && dfPlayerReady) {
      delay(1000);
      mp3.playMp3Folder(1);
      Serial.println("[AUDIO] Boot sound played.");
      bootSoundPlayed = true;
  } else Serial.println("[AUDIO] Boot sound not played.");

  Serial.println(F("Ready — Dome Controller Active"));
  
}

// ------------------- LOOP -------------------
void loop() {
  // Handle incoming data from ESP32
  
  // Display after waiting (once)
  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
  }

  if (ComsESP32.available()) {
    ComsESP32.rxObj(incoming);
    lastPacketTime = millis();

    // if (calculateChecksum(incoming) == incoming.checksum) {

    handleDomeControl();


    handleDomeSpinMotor();
    handleAudio();
  } else if (ComsESP32.status < 0) {
    if (ComsESP32.status == CRC_ERROR) {
    }
  }

  // Watchdog: Stop servos if no packet for >2s
  if (millis() - lastPacketTime > 5000) {
    stopAllServos();
    stopDomeMotor();
    incoming.driveEnabled = false;
  }

  handleSerialCommands();
  printDebugInfo();
  // Drain DFPlayer responses to prevent buffer overflow
  while (mp3.available()) {
      uint8_t type = mp3.readType();
      int value   = mp3.read();  // Discard the value (or log it for debug)
  }
  sendToESP32();
}


void sendToESP32() {
    outgoing.dometilt = 0.0f; // Replace with actual tilt if needed
    outgoing.domedirection = domeDirection;
    outgoing.checksum = calculateChecksumOutgoing(outgoing);

    uint16_t bytes = ComsESP32.txObj(outgoing, 0);
    if (!ComsESP32.sendData(bytes)) {
        Serial.println("[ERROR] Failed to send data to ESP32");
    }
}



void initAudio() {
    mp3Serial.begin(DFPLAYER_BAUD);

    // Check if DFPlayer is busy or SD card missing
    if (digitalRead(DFPLAYER_BUSY_PIN) == LOW) {
        Serial.println("[AUDIO] DFPlayer busy or no SD card detected. Skipping init.");
        dfPlayerReady = false;  // Disable sound
        return; // Non-blocking exit
    }

    // Attempt initialization
    if (mp3.begin(mp3Serial)) {
        mp3.volume(currentVolume);
        dfPlayerReady = true;  // Enable sound
        Serial.println("[AUDIO] DFPlayer initialized.");
    } else {
        dfPlayerReady = false;  // Disable sound
        Serial.println("[AUDIO] DFPlayer init failed. Skipping audio.");
    }
    
    Serial.print("[DEBUG] DFPlayer init status: ");
    Serial.println(dfPlayerReady);
        
    Serial.print("[DEBUG] Busy pin state: ");
    Serial.println(digitalRead(DFPLAYER_BUSY_PIN));
}


void showBuildInfoSerial(const char* prefix)
{
  Serial.print(prefix);
  Serial.print(" | ");
  Serial.print(DEFAULT_REVISION);
  Serial.print(" | ");
  Serial.println(DEFAULT_REVISION_DATE);
}


// ------------------- FUNCTIONS -------------------
void encoderISR() {
  bool A = digitalRead(motorEncoder_pin_A);
  bool B = digitalRead(motorEncoder_pin_B);
  if (A == B) { domeDirection = -1; encoderCount--; }
  else { domeDirection = 1; encoderCount++; }
}




void handleDomeControl() {

// Optional fallback if not defined elsewhere
#ifndef SERVO_HYSTERESIS
#define SERVO_HYSTERESIS 1.8
#endif

    if (!incoming.driveEnabled) {
        stopAllServos();
        return;
    }

    double tiltX = 0.0, tiltY = 0.0;
    float pitch = incoming.pitch;  // Default to raw
    float roll  = incoming.roll;   // Default to raw

#ifdef FILTERMPU
    // ── Filtering (low-lag exponential moving average) ─────────────
    static float filtPitch = 0.0f;
    static float filtRoll  = 0.0f;

    const float ALPHA = 0.35f;
    filtPitch = ALPHA * incoming.pitch + (1 - ALPHA) * filtPitch;
    filtRoll  = ALPHA * incoming.roll  + (1 - ALPHA) * filtRoll;

    pitch = filtPitch;
    roll  = filtRoll;

    // ── Force center when clearly at rest (joystick mode only) ─────
    if (!incoming.autoBalance && 
        abs(incoming.leftStickX) < 12 && 
        abs(incoming.leftStickY) < 12) {
        if (abs(pitch) < 1.8f && abs(roll) < 1.8f) {
            leftServoPosition  = leftServo_0_Position;
            rightServoPosition = rightServo_0_Position;

            // Hysteresis write + early return
            const double HYST = SERVO_HYSTERESIS;
            if (abs(leftServoPosition  - lastLeftPos)  > HYST) {
                leftServo.write((int)leftServoPosition);
                lastLeftPos = leftServoPosition;
            }
            if (abs(rightServoPosition - lastRightPos) > HYST) {
                rightServo.write((int)rightServoPosition);
                lastRightPos = rightServoPosition;
            }
            return;
        }
    }
#endif

    // ── Normal tilt calculation (uses filtered pitch/roll if enabled) ──
    if (incoming.autoBalance) {
        tiltX = map(roll,  -30, 30, SERVO_MIN, SERVO_MAX);
        tiltY = map(pitch, -30, 30, SERVO_MIN, SERVO_MAX);

        tiltX = REVERSE_AUTO_BALANCE ? -tiltX : tiltX;
        tiltY = REVERSE_AUTO_BALANCE ? tiltY : -tiltY;
    } else {
        const int STRONG_DEADZONE = 20;
        if (abs(incoming.leftStickX) > STRONG_DEADZONE) {
            tiltX = map(incoming.leftStickX, 128, -128, SERVO_MIN, SERVO_MAX);
        }
        if (abs(incoming.leftStickY) > STRONG_DEADZONE) {
            tiltY = map(incoming.leftStickY, 128, -128, SERVO_MIN, SERVO_MAX);
        }
    }

    // ── Calculate final positions ─────────────────────────────────────
    if (REVERSE_LEFT_SERVO) {
        leftServoPosition = constrain(leftServo_0_Position - tiltY - tiltX, 40, 100);
    } else {
        leftServoPosition = constrain(leftServo_0_Position + tiltY + tiltX, 40, 100);
    }

    if (REVERSE_RIGHT_SERVO) {
        rightServoPosition = constrain(rightServo_0_Position + tiltY - tiltX, 80, 140);
    } else {
        rightServoPosition = constrain(rightServo_0_Position - tiltY + tiltX, 80, 140);
    }

    // ── Hysteresis write only (always active) ────────────────────────
    const double HYST = SERVO_HYSTERESIS;

    if (abs(leftServoPosition  - lastLeftPos)  > HYST) {
        leftServo.write((int)leftServoPosition);
        lastLeftPos = leftServoPosition;
    }

    if (abs(rightServoPosition - lastRightPos) > HYST) {
        rightServo.write((int)rightServoPosition);
        lastRightPos = rightServoPosition;
    }
}



void handleDomeSpinMotor() {
    if (!incoming.driveEnabled) {
        stopDomeMotor();
        return;
    }

    domeServoMode = incoming.domeFunction;
    int speed = incoming.DomeSpin;

    // Calculate normalized angle
    float currentAngle = (encoderCount / COUNTS_PER_DEGREE) - angleOffset;
    while (currentAngle >= 360) currentAngle -= 360;
    while (currentAngle < 0) currentAngle += 360;

    if (domeServoMode) {
        // Servo mode active
        if (abs(speed) <= JOYSTICK_DEADBAND) {
            // No joystick input → return to FORWARD_ANGLE if outside tolerance
            if (abs(currentAngle - FORWARD_ANGLE) > ZERO_TOLERANCE) {
                bool clockwise = (currentAngle < FORWARD_ANGLE); // If less than forward, turn CW toward forward
                setDomeMotor(clockwise, DOME_SERVO_RETURN_PWM);
            } else {
                stopDomeMotor();
            }
            return;
        }

        // Joystick input → enforce ±SERVO_LIMIT from forward
        if ((currentAngle > FORWARD_ANGLE + SERVO_LIMIT) || (currentAngle < FORWARD_ANGLE - SERVO_LIMIT)) {
            stopDomeMotor(); // At limit
            return;
        }

        // Normal servo movement within bounds
        bool clockwise = (speed > 0);
        int pwmValue = map(abs(speed), 0, 128, 0, 255);
        setDomeMotor(clockwise, pwmValue);
        return;
    }

    // Normal mode (no servo limit)
    if (abs(speed) <= JOYSTICK_DEADBAND) {
        stopDomeMotor();
        return;
    }

    bool clockwise = (speed > 0);
    int pwmValue = map(abs(speed), 0, 128, 0, 255);
    setDomeMotor(clockwise, pwmValue);
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

void handleAudio() {
    if (!dfPlayerReady) {
        if (incoming.soundcmd != 0) {
            Serial.println("[AUDIO] Ignoring sound command - DFPlayer not ready");
            incoming.soundcmd = 0;
        }
        return;
    }

    // Read BUSY pin directly - simple and fast
    bool isPlayingNow = (digitalRead(DFPLAYER_BUSY_PIN) == LOW);

    // Update outgoing state immediately
    outgoing.isplaying = isPlayingNow;

    // Optional: light debug of state change (uncomment if needed)
    /*
    static bool lastDebugState = false;
    if (isPlayingNow != lastDebugState) {
        Serial.print(F("[AUDIO] Playback -> "));
        Serial.println(isPlayingNow ? "STARTED (BUSY LOW)" : "ENDED (BUSY HIGH)");
        lastDebugState = isPlayingNow;
    }
    */

    // Handle incoming sound commands
    static int lastTrack = 0;
    static unsigned long lastCommandTime = 0;

    if (incoming.soundcmd != 0 && millis() - lastCommandTime >= 250) {  // 250ms guard time
        Serial.print(F("[AUDIO] Received cmd = "));
        Serial.println(incoming.soundcmd);

        // Special commands
        if (incoming.soundcmd == 100) {
            silentMode = !silentMode;
            currentVolume = silentMode ? 0 : AUDIO_DEFAULT_VOLUME;
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Silent mode "));
            Serial.println(silentMode ? "ON" : "OFF");
        }
        else if (incoming.soundcmd == 92) {
            currentVolume = min(currentVolume + 1, 30);
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Volume up -> "));
            Serial.println(currentVolume);
        }
        else if (incoming.soundcmd == 93) {
            currentVolume = max(currentVolume - 1, 0);
            mp3.volume(currentVolume);
            Serial.print(F("[AUDIO] Volume down -> "));
            Serial.println(currentVolume);
        }
        // Normal sound playback
        else if (!silentMode && 
                 !isPlayingNow &&                    // only play when idle
                 incoming.soundcmd != lastTrack &&
                 incoming.soundcmd > 0 && 
                 incoming.soundcmd < 100) {
            
            Serial.print(F("[AUDIO] Playing folder track: "));
            Serial.println(incoming.soundcmd);
            
            mp3.playMp3Folder(incoming.soundcmd);
            lastTrack = incoming.soundcmd;
        }
        else {
            Serial.println(F("[AUDIO] Command ignored (silent / busy / same track / invalid)"));
        }

        incoming.soundcmd = 0;
        lastCommandTime = millis();
    }
}

uint16_t calculateChecksum(const RecFromESP32 &d) {
  const uint8_t* p = (const uint8_t*)&d;
  uint16_t s = 0;
  for (size_t i = 0; i < sizeof(d) - sizeof(d.checksum); i++) s += p[i];
  return s;
}

void stopAllServos() {
  leftServo.write(leftServo_0_Position);
  rightServo.write(rightServo_0_Position);
}

void handleSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "debug") {
      debugMode = !debugMode;
      Serial.println(debugMode ? F("DEBUG ON") : F("DEBUG OFF"));
    }
    else if (cmd == "center") {
      stopAllServos();
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
        angleOffset = (encoderCount / COUNTS_PER_DEGREE) - FORWARD_ANGLE;
        Serial.println(F("[ENCODER] Forward position set at 180°."));
    }
  }
}

void printDebugInfo() {
  if (debugEncoder) {
    // Calculate normalized angle
    float currentAngle = (encoderCount / COUNTS_PER_DEGREE) - angleOffset;
    while (currentAngle >= 360) currentAngle -= 360;
    while (currentAngle < 0) currentAngle += 360;

    Serial.print(F("[ENCODER] Raw="));
    Serial.print(encoderCount);
    Serial.print(F(" | Angle="));
    Serial.print(currentAngle, 2);
    Serial.print(F("° | Direction="));
    Serial.print((currentAngle > FORWARD_ANGLE) ? "Right" : "Left");
    Serial.print(F(" | domeServoMode="));
    Serial.println(domeServoMode ? "TRUE" : "FALSE");
  }
  else if (debugMode) {
    Serial.print(F("[DBG] "));
    Serial.print(F("drive=")); Serial.print(incoming.driveEnabled);
    Serial.print(F(" autoBal=")); Serial.print(incoming.autoBalance);
    Serial.print(F(" domeFunc=")); Serial.print(incoming.domeFunction);
    Serial.print(F(" DomeSpin=")); Serial.print(incoming.DomeSpin);
    Serial.print(F(" LX=")); Serial.print(incoming.leftStickX);
    Serial.print(F(" LY=")); Serial.print(incoming.leftStickY);
    Serial.print(F(" pitch=")); Serial.print(incoming.pitch, 1);
    Serial.print(F(" roll=")); Serial.print(incoming.roll, 1);
    Serial.print(F(" lastPlaying=")); Serial.print(lastPlayingState);
    Serial.print(F(" BUSY=")); Serial.print(digitalRead(DFPLAYER_BUSY_PIN));
    Serial.print(F(" ServoL=")); Serial.print((int)leftServoPosition);
    Serial.print(F(" ServoR=")); Serial.print((int)rightServoPosition);
    Serial.print(F(" DomeDir=")); Serial.print((domeDirection > 0) ? "Right" : "Left");
    Serial.print(F(" Enc=")); Serial.print(encoderCount);
    Serial.print(F(" Vol=")); Serial.print(currentVolume);
    Serial.print(F(" Silent=")); Serial.println(silentMode);
  }
}

