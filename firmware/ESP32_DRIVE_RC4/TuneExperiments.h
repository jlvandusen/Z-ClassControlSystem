#pragma once
#include <Arduino.h>
#include "PIDController.h"

// ============================================================
//  RC4 rig experiments — run the droid in the roller cradle and
//  let the firmware measure its own physics.
//
//  step drive <pwm> <ms>     constant drive PWM, S2S holds center
//  step s2s <counts> <ms>    S2S target step, drive braked
//  autotune drive [amp]      relay autotune on pitch (default 60 PWM)
//  autotune s2s [amp]        relay autotune on roll (default 150 counts)
//  autotune apply            apply the suggested gains (then 'pid save')
//  autotune abort            stop any experiment
//
//  Relay autotune (Astrom–Hagglund): u = -amp*sign(error) forces a
//  bounded limit-cycle; from its period Tu and amplitude a,
//  Ku = 4*amp/(pi*a), then Ziegler–Nichols PID:
//  Kp=0.6*Ku, Ki=1.2*Ku/Tu, Kd=0.075*Ku*Tu.
//  If the oscillation RUNS AWAY instead of settling, the plant sign
//  is inverted for your wiring — rerun with a negative amp.
//
//  Safety: requires driveEnabled; aborts on |angle|>15 deg, joystick
//  grab, mode change, or 25 s timeout. Cradle/rig use only.
// ============================================================

// Provided by the main sketch
extern void applyDrivePWM(int pwm);
extern void applyS2SPWM(int pwm);
extern void brakeDrive();
extern void brakeS2S();
extern void brakeFlywheel();
extern PIDController drivePID;
extern PIDController s2sPID;
extern struct DriveConfig cfg;

enum ExpMode : uint8_t { EXP_NONE = 0, EXP_STEP_DRIVE, EXP_STEP_S2S, EXP_RELAY_DRIVE, EXP_RELAY_S2S };

struct ExperimentState {
  ExpMode mode = EXP_NONE;
  float amp = 0;                    // PWM (drive) or pot counts (s2s)
  unsigned long startMs = 0;
  unsigned long durMs = 0;          // steps only

  // relay bookkeeping
  int sign = 1;
  unsigned long lastCrossMs = 0;
  float peakAbs = 0;
  double sumHalfPeriodMs = 0, sumPeak = 0;
  uint8_t crossings = 0;            // total sign switches seen
  uint8_t counted = 0;              // measured half-cycles (first 2 skipped)

  // pending suggestion
  bool havePending = false;
  bool pendingIsDrive = false;
  float pKp = 0, pKi = 0, pKd = 0;
};

ExperimentState exper;

inline bool experimentActive() { return exper.mode != EXP_NONE; }

inline void abortExperiment(const char* why) {
  if (exper.mode == EXP_NONE) return;
  exper.mode = EXP_NONE;
  brakeDrive();
  brakeS2S();
  Serial.printf("[EXP] Aborted: %s\n", why);
  sendSoundCommand(Coms32u4, sendTo32u4, pickRandomAlert());   // RC4.6: audible alert (bank 80-89)
  blackboxFreeze(why);                                         // RC4.7: keep the run-up
}

inline void startStepExperiment(bool driveAxis, float amp, unsigned long durMs) {
  exper = ExperimentState{};
  exper.mode = driveAxis ? EXP_STEP_DRIVE : EXP_STEP_S2S;
  exper.amp = amp;
  exper.durMs = durMs;
  exper.startMs = millis();
  Serial.printf("[EXP] step %s amp=%.0f dur=%lums — hands off\n",
                driveAxis ? "drive" : "s2s", amp, durMs);
}

inline void startRelayExperiment(bool driveAxis, float amp) {
  exper = ExperimentState{};
  exper.mode = driveAxis ? EXP_RELAY_DRIVE : EXP_RELAY_S2S;
  exper.amp = amp;
  exper.startMs = millis();
  exper.lastCrossMs = millis();
  exper.sign = 1;
  Serial.printf("[EXP] relay autotune %s amp=%.0f — expect a small steady rock; 'autotune abort' to stop\n",
                driveAxis ? "drive(pitch)" : "s2s(roll)", amp);
}

inline void finishRelay() {
  bool driveAxis = (exper.mode == EXP_RELAY_DRIVE);
  float meanHalfMs = (float)(exper.sumHalfPeriodMs / exper.counted);
  float Tu = 2.0f * meanHalfMs / 1000.0f;                   // s
  float a = (float)(exper.sumPeak / exper.counted);          // deg
  float Ku = (4.0f * fabsf(exper.amp)) / (PI * a);           // (PWM or counts) per deg
  exper.pKp = 0.6f * Ku;
  exper.pKi = 1.2f * Ku / Tu;
  exper.pKd = 0.075f * Ku * Tu;
  exper.havePending = true;
  exper.pendingIsDrive = driveAxis;
  exper.mode = EXP_NONE;
  brakeDrive();
  brakeS2S();
  Serial.printf("[EXP] relay done: Tu=%.2fs amp=%.2fdeg Ku=%.1f\n", Tu, a, Ku);
  Serial.printf("[EXP] suggested %s PID: Kp=%.2f Ki=%.2f Kd=%.2f — 'autotune apply' to use, then 'pid save'\n",
                driveAxis ? "drive" : "s2s", exper.pKp, exper.pKi, exper.pKd);
}

inline void applyPendingTune() {
  if (!exper.havePending) {
    Serial.println(F("[EXP] Nothing to apply — run 'autotune drive' or 'autotune s2s' first."));
    return;
  }
  if (exper.pendingIsDrive) {
    drivePID.setTunings(exper.pKp, exper.pKi, exper.pKd);
    cfg.driveKp = exper.pKp; cfg.driveKi = exper.pKi; cfg.driveKd = exper.pKd;
    drivePID.reset();
  } else {
    s2sPID.setTunings(exper.pKp, exper.pKi, exper.pKd);
    cfg.s2sKp = exper.pKp; cfg.s2sKi = exper.pKi; cfg.s2sKd = exper.pKd;
    s2sPID.reset();
  }
  Serial.printf("[EXP] Applied %s PID Kp=%.2f Ki=%.2f Kd=%.2f (use 'pid save' to persist)\n",
                exper.pendingIsDrive ? "drive" : "s2s", exper.pKp, exper.pKi, exper.pKd);
  exper.havePending = false;
}

// Called from runControl() AFTER pitch/roll are computed. Returns true
// while the experiment owns the motors this tick.
inline bool serviceExperiment(float pitch, float roll, float potFiltered,
                              float potCenter, float innerKp,
                              int posDeadband, int stictionPwm) {
  if (exper.mode == EXP_NONE) return false;

  unsigned long now = millis();
  unsigned long elapsed = now - exper.startMs;

  // Safety envelope
  if (fabsf(pitch) > 15.0f || fabsf(roll) > 15.0f) { abortExperiment("angle > 15 deg"); return false; }
  if (elapsed > 25000UL) { abortExperiment("timeout"); return false; }

  // Local S2S position hold helper (same law as the main inner loop)
  auto s2sHold = [&](float target) {
    float e = target - potFiltered;
    int pwm = 0;
    if (fabsf(e) > posDeadband) {
      pwm = (int)constrain(innerKp * e, -255.0f, 255.0f);
      if (abs(pwm) < stictionPwm) pwm = (pwm > 0) ? stictionPwm : -stictionPwm;
    }
    applyS2SPWM(pwm);
  };

  switch (exper.mode) {
    case EXP_STEP_DRIVE:
      if (elapsed >= exper.durMs) {
        Serial.println(F("[EXP] step complete"));
        exper.mode = EXP_NONE;
        brakeDrive();
        return false;
      }
      applyDrivePWM((int)exper.amp);
      s2sHold(potCenter);
      return true;

    case EXP_STEP_S2S:
      if (elapsed >= exper.durMs) {
        Serial.println(F("[EXP] step complete"));
        exper.mode = EXP_NONE;
        brakeS2S();
        return false;
      }
      brakeDrive();
      s2sHold(potCenter + exper.amp);
      return true;

    case EXP_RELAY_DRIVE:
    case EXP_RELAY_S2S: {
      bool driveAxis = (exper.mode == EXP_RELAY_DRIVE);
      float e = driveAxis ? pitch : roll;
      const float HYST = 0.3f;

      // track the peak of this half-cycle
      if (fabsf(e) > exper.peakAbs) exper.peakAbs = fabsf(e);

      // sign switch with hysteresis
      int wantSign = exper.sign;
      if (exper.sign > 0 && e > HYST) wantSign = -1;   // error positive -> push negative
      if (exper.sign < 0 && e < -HYST) wantSign = 1;
      if (wantSign != exper.sign) {
        unsigned long half = now - exper.lastCrossMs;
        exper.lastCrossMs = now;
        exper.sign = wantSign;
        exper.crossings++;
        if (exper.crossings > 2 && half > 40) {        // skip startup, ignore chatter
          exper.sumHalfPeriodMs += half;
          exper.sumPeak += exper.peakAbs;
          exper.counted++;
        }
        exper.peakAbs = 0;
        if (exper.counted >= 8) { finishRelay(); return false; }
      }

      float u = exper.sign * exper.amp;                // relay output
      if (driveAxis) {
        applyDrivePWM((int)u);
        s2sHold(potCenter);
      } else {
        brakeDrive();
        s2sHold(potCenter + u);
      }
      return true;
    }

    default:
      exper.mode = EXP_NONE;
      return false;
  }
}
