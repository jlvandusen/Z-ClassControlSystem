
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
  SOUND_TOGGLE_DRIVE = 60,        // RC4.1: plays on drive ENABLE
  SOUND_DRIVE_DISABLED = 61,      // RC4.1: plays on drive DISABLE (state-aware)
  SOUND_TOGGLE_REVERSE = 61,
  SOUND_TOGGLE_DOME_SERVO_MODE = 62,
  SOUND_TOGGLE_BALANCE = 63,

  // RC4.3: link CONTROL codes live above every playable track (tracks are
  // 1..119 on the wire; the field is an int8_t). They used to be 92/93/100,
  // which collided with real files (MP3/0100.mp3 is the shutdown clip).
  SOUND_CTRL_VOL_UP   = 125,
  SOUND_CTRL_VOL_DOWN = 126,
  SOUND_CTRL_SILENT   = 127,



  // Special
  SOUND_SET_ZERO = 91,
  SOUND_DRIVE_REVERSE = 92
};

// ---- Random Picker ----
static inline uint16_t pickRandom1to30() {   // chatter bank 1-31 (name is historical)
  return (uint16_t)random(1, 32);
}

// RC4.6: mood banks (being populated on the SD - a roll that lands on a
// not-yet-added track is just silent).
//   40-49 excited / smart-ass        70-79 button / quick-press blips
//   80-89 errors / alerts (fired by safety events, not buttons)
static inline uint16_t pickRandomExcited() {
  return (uint16_t)random(40, 50);
}
static inline uint16_t pickRandomAlert() {
  return (uint16_t)random(80, 90);
}
static inline uint16_t pickRandomButton() {
  return (uint16_t)random(70, 80);
}
// PS enable/disable acknowledgement: quick blips 70-74 only (75-79 stay free
// for the L2+LEFT roll and future use).
static inline uint16_t pickRandomPress() {
  return (uint16_t)random(70, 75);
}


// ---- Drive Controller Sound Mapping ----
static uint16_t resolveDriveControllerSound(const ControllerState &cur) {
  // L1 + D-pad combos

  if (cur.L2 > 50) {
    if (cur.dpadUp.pressed) return SOUND_CTRL_VOL_UP;     // L2+UP: volume +
    if (cur.dpadDown.pressed) return SOUND_CTRL_VOL_DOWN;  // L2+DOWN: volume -
    if (cur.dpadRight.pressed) return pickRandomExcited(); // L2+RIGHT: excited/smart-ass bank (40-49)
    if (cur.dpadLeft.pressed) return pickRandomButton();    // L2+LEFT: button/quick-press bank (70-79)
  }

  if (cur.L1.held) {
    if (cur.dpadUp.pressed) return 10;  // Shifted sounds
    if (cur.dpadRight.pressed) return 11;
    if (cur.dpadDown.pressed) return 12;
    if (cur.dpadLeft.pressed) return 13;
    if (cur.circle.pressed) return SOUND_CTRL_SILENT;  // L1+CIRCLE: silent-mode toggle
  }

  // Single D-pad
  if (cur.dpadUp.pressed) return pickRandom1to30();  // Random sound
  if (cur.dpadRight.pressed) return 3;
  if (cur.dpadDown.pressed) return 4;
  if (cur.dpadLeft.pressed) return 5;

  // PS is not mapped here — the drive enable/disable sound is STATE-AWARE
  // and fires from toggleDriveEnabled() itself (pref sndon/sndoff), so it
  // also covers force-disable and controller-loss.

  // RC4.3: CIRCLE is a plain sound button (it used to duplicate PS as the
  // drive toggle). Same clip as the dome pad's CIRCLE.
  if (cur.circle.pressed) return 28;

  // Cross toggles balance
  if (cur.cross.pressed) return SOUND_TOGGLE_BALANCE;

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
