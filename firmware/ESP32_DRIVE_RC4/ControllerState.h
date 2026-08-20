
#pragma once
#include <Arduino.h>
#include <Bluepad32.h>

// ------------------- Button State -------------------
struct ButtonState {
  bool current = false;
  bool previous = false;
  bool pressed = false;
  bool released = false;
  bool held = false;
  unsigned long lastChange = 0;

  void update(bool raw, unsigned long now, uint16_t debounceMs = 120) {
    pressed = false;
    released = false;

    if (raw != current && (now - lastChange) >= debounceMs) {
      previous = current;
      current = raw;
      lastChange = now;

      if (current && !previous) pressed = true;
      if (!current && previous) released = true;
    }
    held = current;
  }
};

// ------------------- Controller State -------------------
struct ControllerState {
  ButtonState L1, L3, cross, circle;
  ButtonState dpadUp, dpadDown, dpadLeft, dpadRight;
  ButtonState ps;
  int8_t joyX = 0, joyY = 0;
  int16_t rawX = 0, rawY = 0;
  int16_t L2 = 0;

  void update(GamepadPtr gp) {
    unsigned long now = millis();
    if (!gp || !gp->isConnected()) {
      // RC4: neutralize on disconnect — RC3 latched the last stick/button
      // values, so a controller dropout kept the robot driving.
      joyX = joyY = 0; rawX = rawY = 0; L2 = 0;
      ps.update(false, now); L1.update(false, now); L3.update(false, now);
      cross.update(false, now); circle.update(false, now);
      dpadUp.update(false, now); dpadDown.update(false, now);
      dpadLeft.update(false, now); dpadRight.update(false, now);
      return;
    }

    ps.update(gp->miscButtons() & MISC_BUTTON_SYSTEM, now);
    L1.update(gp->l1(), now);
    L3.update(gp->miscButtons() & MISC_BUTTON_SELECT, now);
    cross.update(gp->a(), now);
    circle.update(gp->b(), now);

    uint8_t d = gp->dpad();
    dpadUp.update((d & DPAD_UP) != 0, now);     // RC4: bitmask — diagonals register both axes
    dpadDown.update((d & DPAD_DOWN) != 0, now);
    dpadLeft.update((d & DPAD_LEFT) != 0, now);
    dpadRight.update((d & DPAD_RIGHT) != 0, now);

    rawX = constrain(gp->axisX(), -508, 512);
    rawY = constrain(gp->axisY(), -508, 512);

    joyX = constrain(rawX / 4, -127, 127);
    joyY = constrain(rawY / 4, -127, 127);

    if (abs(joyX) < 10) joyX = 0;
    if (abs(joyY) < 10) joyY = 0;

    L2 = gp->brake();
  }
};

// ------------------- Combo Helpers -------------------
inline bool bothControllersUpHeld(ControllerState &driveCtrl, ControllerState &domeCtrl, unsigned long durationMs = 3000) {
  static unsigned long comboStart = 0;

  if (driveCtrl.dpadUp.held && domeCtrl.dpadUp.held) {
    if (comboStart == 0) comboStart = millis();
    if (millis() - comboStart >= durationMs) {
      comboStart = 0;  // Reset after detection
      return true;
    }
  } else {
    comboStart = 0;  // Reset if either released
  }
  return false;
}

inline bool bothControllersDownHeld(ControllerState &driveCtrl, ControllerState &domeCtrl, unsigned long durationMs = 3000) {
  static unsigned long comboStart = 0;
  if (driveCtrl.dpadDown.held && domeCtrl.dpadDown.held) {
    if (comboStart == 0) comboStart = millis();
    if (millis() - comboStart >= durationMs) {
      comboStart = 0;
      return true;
    }
  } else {
    comboStart = 0;
  }
  return false;
}
