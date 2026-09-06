#pragma once
#include <Arduino.h>
#include <SerialTransfer.h>
#include "ConfigTypes.h"

// External variables

// External functions and variables from main file
extern void receiveFromTrinket();
extern bool imuHasSample;

extern SerialTransfer Coms32u4;
extern struct_messagempu mpudata;
extern send32u4 sendTo32u4;
extern DriveConfig cfg;
extern bool driveEnabled;
extern const uint8_t S2S_POT_PIN;


// External functions
extern void sendSoundCommand(SerialTransfer &coms, send32u4 &payload, uint16_t cmd);
extern bool saveConfig();
extern int soundBootCal;   // RC4.3: 'pref sndcal' — boot-cal-complete track, 0 = silent

// Calibration constants
const float STABLE_THRESHOLD = 2.5;
const uint32_t STABLE_TIME_MS = 3000;
const uint16_t SOUND_CAL_COMPLETE = 6;
const uint16_t SOUND_SAVE_PREFS = 5;

// Boot calibration

// RC4.7: if the Trinket is still booting or recovering from a power glitch,
// its packets may not arrive within one 3 s window. Rather than giving up and
// running with pitch/roll = 0, keep re-trying the window until the IMU comes
// online, up to this overall deadline.
const uint32_t BOOT_CAL_MAX_MS = 15000;

inline void serviceBootCalibration() {
  static bool bootCalibrating = true;
  static unsigned long bootCalFirstStart = 0;   // overall deadline anchor
  static unsigned long bootCalStart = 0;         // current window start
  static double sumPitch = 0.0, sumRoll = 0.0;
  static uint64_t sumPot = 0;
  static uint32_t sampleCount = 0;
  static uint8_t windowNum = 0;

  if (!bootCalibrating) return;

  // Start timer on first call
  if (bootCalStart == 0) {
    bootCalStart = millis();
    if (bootCalFirstStart == 0) bootCalFirstStart = bootCalStart;
    sumPitch = sumRoll = 0.0;
    sumPot = 0;
    sampleCount = 0;
    windowNum++;
    Serial.printf("[BOOT CAL] Collecting samples (window %u)...\n", windowNum);
  }

  // Actively read IMU data
  receiveFromTrinket();

  // Accumulate samples if IMU has data
  if (imuHasSample) {
    sumPitch += mpudata.pitch;
    sumRoll += mpudata.roll;
    sumPot += analogRead(S2S_POT_PIN);
    sampleCount++;
  }

  // After 3 seconds, compute averages and finish
  if (millis() - bootCalStart >= STABLE_TIME_MS) {
    if (sampleCount > 0) {
      cfg.pitchOffset = -(sumPitch / sampleCount);
      cfg.rollOffset = -(sumRoll / sampleCount);
      // RC4.7: DO NOT re-capture potCenter at boot. The S2S frame flops to one
      // side when the drive is disabled, so the boot pose is NOT the center —
      // capturing it here saved the flopped position as "center" every reset.
      // The center now persists from explicit 'cfg calibrate s2s' / 'cfg set
      // potcenter' (loaded from NVS in setup).
      sendSoundCommand(Coms32u4, sendTo32u4, soundBootCal);  // 0 = silent
      Serial.printf("[BOOT CAL] Completed: pitchOffset=%.2f rollOffset=%.2f (potCenter kept=%d) (samples=%u)\n",
                    cfg.pitchOffset, cfg.rollOffset, cfg.potCenter, sampleCount);
      bootCalibrating = false;
    } else if (millis() - bootCalFirstStart < BOOT_CAL_MAX_MS) {
      // No IMU yet — the Trinket may still be booting / recovering. Retry the
      // window instead of settling for pitch/roll = 0.
      Serial.println("[BOOT CAL] No IMU packets yet — waiting for Trinket...");
      bootCalStart = 0;   // re-arm a fresh window on the next call
    } else {
      Serial.println("[BOOT CAL] IMU never came online — using defaults (potCenter kept). Fix the IMU, then run 'cfg calibrate'.");
      cfg.pitchOffset = 0.0f;
      cfg.rollOffset = 0.0f;
      bootCalibrating = false;
    }
  }
}


// Manual calibration
inline void handleManualCalibration(bool driveUpHeld, bool domeUpHeld) {
  static unsigned long comboStart = 0;
  if (driveUpHeld && domeUpHeld) {
    if (comboStart == 0) comboStart = millis();
    if (millis() - comboStart >= STABLE_TIME_MS) {
      cfg.pitchOffset = -mpudata.pitch;
      cfg.rollOffset = -mpudata.roll;
      cfg.potCenter = analogRead(S2S_POT_PIN);
      saveConfig();
      sendSoundCommand(Coms32u4, sendTo32u4, SOUND_SAVE_PREFS);
      Serial.printf("[MANUAL CAL] Saved: pitchOffset=%.2f rollOffset=%.2f potCenter=%d\n",
                    cfg.pitchOffset, cfg.rollOffset, cfg.potCenter);
      comboStart = 0;
    }
  } else {
    comboStart = 0;
  }
}
