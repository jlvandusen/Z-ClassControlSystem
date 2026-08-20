
#pragma once
#include <Arduino.h>
#include <Bluepad32.h>
#include "ControllerState.h"  // Include for ControllerState

// ---- Sound Command Enum ----
enum SoundCommand : uint16_t {
  SOUND_NONE = 0,
  SOUND_RANDOM = 9,
  SOUND_STARTUP = 1,

  // Toggles
  SOUND_TOGGLE_DRIVE = 60,
  SOUND_TOGGLE_REVERSE = 61,
  SOUND_TOGGLE_DOME_SERVO_MODE = 62,
  SOUND_TOGGLE_BALANCE = 63,



  // Special
  SOUND_SET_ZERO = 91,
  SOUND_DRIVE_REVERSE = 92
};

// ---- Random Picker ----
static inline uint16_t pickRandom1to30() {
  return (uint16_t)random(1, 31);
}


// ---- Drive Controller Sound Mapping ----
static uint16_t resolveDriveControllerSound(const ControllerState &cur) {
  // L1 + D-pad combos

  if (cur.L2 > 50) {
    if (cur.dpadUp.pressed) return 92;    // SOUND_DRIVE_REVERSE
    if (cur.dpadDown.pressed) return 93;  // Assign a new sound for DOWN
  }

  if (cur.L1.held) {
    if (cur.dpadUp.pressed) return 10;  // Shifted sounds
    if (cur.dpadRight.pressed) return 11;
    if (cur.dpadDown.pressed) return 12;
    if (cur.dpadLeft.pressed) return 13;
  }

  // Single D-pad
  if (cur.dpadUp.pressed) return pickRandom1to30();  // Random sound
  if (cur.dpadRight.pressed) return 3;
  if (cur.dpadDown.pressed) return 4;
  if (cur.dpadLeft.pressed) return 5;

  // Circle
  if (cur.circle.pressed) return 100;

  // Cross toggles balance
  if (cur.cross.pressed) return SOUND_TOGGLE_BALANCE;

  // PS toggles drive
  if (cur.ps.pressed) return SOUND_TOGGLE_DRIVE;

  // L3 toggles reverse drive
  if (cur.L3.pressed) return SOUND_TOGGLE_REVERSE;

  return SOUND_NONE;
}

// ---- Dome Controller Sound Mapping ----
static uint16_t resolveDomeControllerSound(const ControllerState &cur) {
  if (cur.L1.held) {
    if (cur.dpadUp.pressed) return 16;
    if (cur.dpadRight.pressed) return 17;
    if (cur.dpadDown.pressed) return 18;
    if (cur.dpadLeft.pressed) return 19;
  }

  if (cur.dpadUp.pressed) return pickRandom1to30();  // Random sound
  if (cur.dpadRight.pressed) return 21;
  if (cur.dpadDown.pressed) return 22;
  if (cur.dpadLeft.pressed) return 23;

  if (cur.circle.pressed) return 28;

  if (cur.cross.pressed) return SOUND_RANDOM;

  if (cur.ps.pressed) return SOUND_TOGGLE_DOME_SERVO_MODE;

  // L3 currently does nothing
  return SOUND_NONE;
}
