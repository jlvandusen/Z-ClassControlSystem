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

// Calibration constants
const float STABLE_THRESHOLD = 2.5;
const uint32_t STABLE_TIME_MS = 3000;
const uint16_t SOUND_CAL_COMPLETE = 6;
const uint16_t SOUND_SAVE_PREFS = 5;

// Boot calibration

inline void serviceBootCalibration() {
  static bool bootCalibrating = true;
  static unsigned long bootCalStart = 0;
  static double sumPitch = 0.0, sumRoll = 0.0;
  static uint64_t sumPot = 0;
  static uint32_t sampleCount = 0;

  if (!bootCalibrating) return;

  // Start timer on first call
  if (bootCalStart == 0) {
    bootCalStart = millis();
    sumPitch = sumRoll = 0.0;
    sumPot = 0;
    sampleCount = 0;
    Serial.println("[BOOT CAL] Collecting samples...");
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
      cfg.potCenter = (int32_t)(sumPot / sampleCount);
      // RC4: RAM only — RC3 wrote NVS on EVERY boot, clobbering a good
      // saved calibration with the boot pose and wearing flash. Persist
      // explicitly via the both-dpad-up combo or "cfg save".
      sendSoundCommand(Coms32u4, sendTo32u4, SOUND_CAL_COMPLETE);
      Serial.printf("[BOOT CAL] Completed: pitchOffset=%.2f rollOffset=%.2f potCenter=%d (samples=%u)\n",
                    cfg.pitchOffset, cfg.rollOffset, cfg.potCenter, sampleCount);
    } else {
      Serial.println("[BOOT CAL] No IMU samples collected! Using defaults.");
      cfg.pitchOffset = 0.0f;
      cfg.rollOffset = 0.0f;
      cfg.potCenter = analogRead(S2S_POT_PIN);

    }
    bootCalibrating = false;
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
