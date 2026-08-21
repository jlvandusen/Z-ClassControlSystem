
#pragma once
#include <Arduino.h>
#include "PIDController.h"

// RC4: same command surface as RC3 plus:
//   telemetry on/off   - 20 Hz plotter-friendly telemetry stream
//   bt forget          - forget BT keys on demand (RC3 wiped them EVERY boot)
//   pref lean <deg>    - max joystick lean/authority for balance blending
//   pid save           - now saves ONLY PID values (RC3 also overwrote the
//                        pitch/roll offsets with the current pose)

// External helpers from main code
extern void showBuildInfoSerial(const char* prefix);
extern bool saveConfig();
extern bool loadConfig();
extern void resetConfigToDefaults();
extern void beginS2SCenterCalibration();
extern void printControllersSummary();
extern bool savePidOnly();

// Debug flags
extern bool debugAll, debugMPU, debug32u4, debugDome, debugControllersFlag, debugS2S, debugDrive;
extern bool debugSound, debugFlywheel, debugTo32u4, debugFrom32u4;
extern bool telemetryEnabled;
extern bool telemetryFast;
extern bool driveEnabled;

// Rig experiments (TuneExperiments.h — included before this header)
extern void startStepExperiment(bool driveAxis, float amp, unsigned long durMs);
extern void startRelayExperiment(bool driveAxis, float amp);
extern void abortExperiment(const char* why);
extern void applyPendingTune();

// Tunables
extern float s2sMaxDegrees;
extern float maxJoyDrivePwm;
extern float s2sInnerKp;

// PID
extern PIDController drivePID;
extern PIDController s2sPID;

// Config + IMU
extern struct struct_messagempu mpudata;
extern struct DriveConfig cfg;

// Defaults (for pid reset)
extern const float RC4_DRIVE_KP, RC4_DRIVE_KI, RC4_DRIVE_KD;
extern const float RC4_S2S_KP, RC4_S2S_KI, RC4_S2S_KD;

// ---------- Help menu ----------
inline void printHelpMenu() {
  Serial.println(F("\n=== Available Commands (RC4) ==="));
  Serial.println(F("help or commands      - Show this help menu"));
  Serial.println(F("version               - Show firmware revision/date"));
  Serial.println(F("telemetry on|off      - 20Hz telemetry stream (Serial-Plotter friendly)"));
  Serial.println(F("telemetry fast        - 100Hz stream for rig captures / system ID"));
  Serial.println(F("step drive <pwm> <ms> - constant drive PWM step (rig only)"));
  Serial.println(F("step s2s <cnt> <ms>   - S2S target step in pot counts (rig only)"));
  Serial.println(F("autotune drive [amp]  - relay autotune pitch loop (default 60 PWM)"));
  Serial.println(F("autotune s2s [amp]    - relay autotune roll loop (default 150 counts)"));
  Serial.println(F("autotune apply        - apply suggested gains (then 'pid save')"));
  Serial.println(F("autotune abort        - stop any running experiment"));
  Serial.println(F("bt forget             - Forget Bluetooth keys (re-pair controllers)"));
  Serial.println(F("debug                 - Toggle ALL debug"));
  Serial.println(F("debug mpu             - Toggle MPU debug"));
  Serial.println(F("debug 32u4            - Toggle 32u4 debug"));
  Serial.println(F("debug dome            - Toggle Dome ESPNOW debug"));
  Serial.println(F("debug controllers     - Toggle controller input debug"));
  Serial.println(F("debug s2s             - Toggle S2S debug"));
  Serial.println(F("debug drive           - Toggle Drive debug"));
  Serial.println(F("debug sound           - Toggle sound debug"));
  Serial.println(F("debug flywheel        - Toggle Flywheel debug"));
  Serial.println(F("debug to32u4          - Toggle TX->32u4 debug"));
  Serial.println(F("debug from32u4        - Toggle RX<-32u4 debug"));
  Serial.println(F("cfg show              - Show current config"));
  Serial.println(F("cfg save              - Save config to NVS"));
  Serial.println(F("cfg load              - Load config from NVS"));
  Serial.println(F("cfg reset             - Reset config to defaults and save"));
  Serial.println(F("cfg set revision <text>"));
  Serial.println(F("cfg set date <YYYY-MM-DD>"));
  Serial.println(F("cfg set pitchoffset <float>"));
  Serial.println(F("cfg set rolloffset <float>"));
  Serial.println(F("cfg set potcenter <int>"));
  Serial.println(F("cfg set mpudeadzone <float>   (RC4: display deadzone only, NOT in the PID path)"));
  Serial.println(F("cfg calibrate s2scenter - Start 3s calibration"));
  Serial.println(F("pref swing <float>    - Set S2S swing limit in deg (default 70)"));
  Serial.println(F("pref lean <float>     - Max joystick drive authority in PWM (default 255)"));
  Serial.println(F("pref innerkp <float>  - S2S position-loop Kp in PWM/count (default 0.9)"));
  Serial.println(F("pid set drive kp|ki|kd <val>  - Drive PID (PWM per deg / deg*s / deg per s)"));
  Serial.println(F("pid set s2s kp|ki|kd <val>    - S2S outer PID (pot counts per deg ...)"));
  Serial.println(F("pid show              - Show current PID values"));
  Serial.println(F("pid save              - Save PID values to NVS (only PID)"));
  Serial.println(F("pid reset             - Reset PID values to RC4 defaults"));
}

// ---------- Command handler ----------
inline void handleSerialCommand(const String &cmd) {
  if (cmd == "help" || cmd == "commands") {
    printHelpMenu();
  } else if (cmd == "version") {
    showBuildInfoSerial("VERSION");
  } else if (cmd == "show controllers") {
    printControllersSummary();
  } else if (cmd == "telemetry on") {
    telemetryEnabled = true;
    telemetryFast = false;
    Serial.println(F("[TLM] Telemetry ON (20 Hz)"));
  } else if (cmd == "telemetry fast") {
    telemetryEnabled = true;
    telemetryFast = true;
    Serial.println(F("[TLM] Telemetry FAST (100 Hz) — best with other debug off"));
  } else if (cmd == "telemetry off") {
    telemetryEnabled = false;
    telemetryFast = false;
    Serial.println(F("[TLM] Telemetry OFF"));
  }

  // ---- rig experiments (roller cradle) ----
  else if (cmd.startsWith("step drive") || cmd.startsWith("step s2s")) {
    bool driveAxis = cmd.startsWith("step drive");
    String rest = cmd.substring(driveAxis ? 10 : 8);
    rest.trim();
    int sp = rest.indexOf(' ');
    float amp = rest.toFloat();
    long dur = (sp > 0) ? rest.substring(sp + 1).toInt() : 2000;
    if (!driveEnabled) {
      Serial.println(F("[EXP] Enable drive first (CIRCLE on drive controller) — rig experiments need it."));
    } else if (amp == 0 || fabsf(amp) > (driveAxis ? 255 : 600) || dur < 100 || dur > 10000) {
      Serial.println(driveAxis ? F("[EXP] Usage: step drive <pwm -255..255> <ms 100..10000>")
                               : F("[EXP] Usage: step s2s <counts -600..600> <ms 100..10000>"));
    } else {
      startStepExperiment(driveAxis, amp, (unsigned long)dur);
    }
  } else if (cmd.startsWith("autotune drive") || cmd.startsWith("autotune s2s")) {
    bool driveAxis = cmd.startsWith("autotune drive");
    String rest = cmd.substring(driveAxis ? 14 : 12);
    rest.trim();
    float amp = rest.length() ? rest.toFloat() : (driveAxis ? 60.0f : 150.0f);
    if (!driveEnabled) {
      Serial.println(F("[EXP] Enable drive first (CIRCLE on drive controller) — rig experiments need it."));
    } else if (amp == 0 || fabsf(amp) > (driveAxis ? 150 : 500)) {
      Serial.println(driveAxis ? F("[EXP] Usage: autotune drive [amp -150..150 PWM]")
                               : F("[EXP] Usage: autotune s2s [amp -500..500 counts]"));
    } else {
      startRelayExperiment(driveAxis, amp);
    }
  } else if (cmd == "autotune apply") {
    applyPendingTune();
  } else if (cmd == "autotune abort") {
    abortExperiment("user request");
  }

  else if (cmd == "bt forget") {
    BP32.forgetBluetoothKeys();
    Serial.println(F("[BT] Bluetooth keys forgotten — re-pair controllers"));
  } else if (cmd == "debug sound") {
    debugSound = !debugSound;
    Serial.println(debugSound ? F("Sound debug ENABLED") : F("Sound debug DISABLED"));
  } else if (cmd == "debug flywheel") {
    debugFlywheel = !debugFlywheel;
    Serial.println(debugFlywheel ? F("Flywheel debug ENABLED") : F("Flywheel debug DISABLED"));
  } else if (cmd == "cfg show") {
    Serial.println(F("\n=== Current Config ==="));
    Serial.printf("Revision: %s | Date: %s\n", cfg.revision, cfg.revisionDate);
    Serial.printf("potCenter=%ld | pitchOffset=%.3f | rollOffset=%.3f\n",
                  (long)cfg.potCenter, cfg.pitchOffset, cfg.rollOffset);
    Serial.printf("mpuDeadzone=%.2f | cfgVersion=0x%08lX\n",
                  cfg.mpuDeadzone, (unsigned long)cfg.cfgVersion);
    Serial.printf("Drive PID: Kp=%.2f Ki=%.2f Kd=%.2f | S2S PID: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  cfg.driveKp, cfg.driveKi, cfg.driveKd, cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
  } else if (cmd == "cfg save") {
    Serial.println(saveConfig() ? F("[CFG] Saved to NVS.") : F("[CFG] Save failed."));
  } else if (cmd == "cfg load") {
    Serial.println(loadConfig() ? F("[CFG] Loaded from NVS.") : F("[CFG] Load failed."));
  } else if (cmd == "cfg reset") {
    resetConfigToDefaults();
    Serial.println(saveConfig() ? F("[CFG] Reset to defaults and saved.") : F("[CFG] Reset failed."));
  } else if (cmd.startsWith("cfg set revision")) {
    strlcpy(cfg.revision, cmd.substring(17).c_str(), sizeof(cfg.revision));
    Serial.println(saveConfig() ? F("[CFG] Revision saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set date")) {
    strlcpy(cfg.revisionDate, cmd.substring(13).c_str(), sizeof(cfg.revisionDate));
    Serial.println(saveConfig() ? F("[CFG] Date saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set pitchoffset")) {
    cfg.pitchOffset = cmd.substring(20).toFloat();
    Serial.println(saveConfig() ? F("[CFG] pitchOffset saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set rolloffset")) {
    cfg.rollOffset = cmd.substring(19).toFloat();
    Serial.println(saveConfig() ? F("[CFG] rollOffset saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set potcenter")) {
    cfg.potCenter = cmd.substring(18).toInt();
    Serial.println(saveConfig() ? F("[CFG] potCenter saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set mpudeadzone")) {
    cfg.mpuDeadzone = cmd.substring(20).toFloat();
    Serial.println(saveConfig() ? F("[CFG] mpuDeadzone saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("pref swing")) {
    float newSwing = cmd.substring(10).toFloat();
    if (newSwing > 0 && newSwing <= 90) {
      s2sMaxDegrees = newSwing;
      Serial.printf("[PREF] S2S max swing set to %.1f degrees\n", s2sMaxDegrees);
    } else {
      Serial.println(F("[PREF] Invalid swing value. Must be 1-90 degrees."));
    }
  } else if (cmd.startsWith("pref lean")) {
    float v = cmd.substring(9).toFloat();
    if (v >= 50 && v <= 255) {
      maxJoyDrivePwm = v;
      Serial.printf("[PREF] Max joystick drive authority set to %.0f PWM\n", maxJoyDrivePwm);
    } else {
      Serial.println(F("[PREF] Invalid value. Must be 50-255."));
    }
  } else if (cmd.startsWith("pref innerkp")) {
    float v = cmd.substring(12).toFloat();
    if (v > 0 && v <= 5) {
      s2sInnerKp = v;
      Serial.printf("[PREF] S2S inner (position) Kp set to %.2f PWM/count\n", s2sInnerKp);
    } else {
      Serial.println(F("[PREF] Invalid value. Must be 0-5."));
    }
  } else if (cmd == "cfg calibrate s2scenter") {
    beginS2SCenterCalibration();
  } else if (cmd == "debug") {
    debugAll = !debugAll;
    Serial.println(debugAll ? F("ALL debug ENABLED") : F("ALL debug DISABLED"));
  } else if (cmd == "debug mpu") {
    debugMPU = !debugMPU;
    Serial.println(debugMPU ? F("MPU debug ENABLED") : F("MPU debug DISABLED"));
  } else if (cmd == "debug 32u4") {
    debug32u4 = !debug32u4;
    Serial.println(debug32u4 ? F("32u4 debug ENABLED") : F("32u4 debug DISABLED"));
  } else if (cmd == "debug dome") {
    debugDome = !debugDome;
    Serial.println(debugDome ? F("Dome debug ENABLED") : F("Dome debug DISABLED"));
  } else if (cmd == "debug controllers") {
    debugControllersFlag = !debugControllersFlag;
    Serial.println(debugControllersFlag ? F("Controller debug ENABLED") : F("Controller debug DISABLED"));
  } else if (cmd == "debug s2s") {
    debugS2S = !debugS2S;
    Serial.println(debugS2S ? F("S2S debug ENABLED") : F("S2S debug DISABLED"));
  } else if (cmd == "debug drive") {
    debugDrive = !debugDrive;
    Serial.println(debugDrive ? F("Drive debug ENABLED") : F("Drive debug DISABLED"));
  } else if (cmd == "debug to32u4") {
    debugTo32u4 = !debugTo32u4;
    Serial.println(debugTo32u4 ? F("[DEBUG] TO 32u4 ENABLED") : F("[DEBUG] TO 32u4 DISABLED"));
  } else if (cmd == "debug from32u4") {
    debugFrom32u4 = !debugFrom32u4;
    Serial.println(debugFrom32u4 ? F("[DEBUG] FROM 32u4 ENABLED") : F("[DEBUG] FROM 32u4 DISABLED"));
  }

  // PID tuning commands
  else if (cmd.startsWith("pid set drive kp")) {
    float val = cmd.substring(17).toFloat();
    if (val >= 0 && val <= 100) {
      drivePID.setTunings(val, drivePID.getKi(), drivePID.getKd());
      cfg.driveKp = val;
      Serial.printf("[PID] Drive Kp updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Kp value (0-100 allowed)."));
  } else if (cmd.startsWith("pid set drive ki")) {
    float val = cmd.substring(17).toFloat();
    if (val >= 0 && val <= 100) {
      drivePID.setTunings(drivePID.getKp(), val, drivePID.getKd());
      cfg.driveKi = val;
      Serial.printf("[PID] Drive Ki updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Ki value (0-100 allowed)."));
  } else if (cmd.startsWith("pid set drive kd")) {
    float val = cmd.substring(17).toFloat();
    if (val >= 0 && val <= 20) {
      drivePID.setTunings(drivePID.getKp(), drivePID.getKi(), val);
      cfg.driveKd = val;
      Serial.printf("[PID] Drive Kd updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Kd value (0-20 allowed)."));
  } else if (cmd.startsWith("pid set s2s kp")) {
    float val = cmd.substring(15).toFloat();
    if (val >= 0 && val <= 200) {
      s2sPID.setTunings(val, s2sPID.getKi(), s2sPID.getKd());
      cfg.s2sKp = val;
      Serial.printf("[PID] S2S Kp updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Kp value (0-200 allowed)."));
  } else if (cmd.startsWith("pid set s2s ki")) {
    float val = cmd.substring(15).toFloat();
    if (val >= 0 && val <= 200) {
      s2sPID.setTunings(s2sPID.getKp(), val, s2sPID.getKd());
      cfg.s2sKi = val;
      Serial.printf("[PID] S2S Ki updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Ki value (0-200 allowed)."));
  } else if (cmd.startsWith("pid set s2s kd")) {
    float val = cmd.substring(15).toFloat();
    if (val >= 0 && val <= 20) {
      s2sPID.setTunings(s2sPID.getKp(), s2sPID.getKi(), val);
      cfg.s2sKd = val;
      Serial.printf("[PID] S2S Kd updated to %.2f\n", val);
    } else Serial.println(F("[PID] Invalid Kd value (0-20 allowed)."));
  } else if (cmd == "pid show") {
    Serial.printf("[PID] Drive: Kp=%.2f Ki=%.2f Kd=%.2f | S2S: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  drivePID.getKp(), drivePID.getKi(), drivePID.getKd(),
                  s2sPID.getKp(), s2sPID.getKi(), s2sPID.getKd());
  } else if (cmd == "pid save") {
    Serial.println(savePidOnly() ? F("[PID] PID settings saved to NVS.") : F("[PID] Save failed."));
  } else if (cmd == "pid reset") {
    cfg.driveKp = RC4_DRIVE_KP;
    cfg.driveKi = RC4_DRIVE_KI;
    cfg.driveKd = RC4_DRIVE_KD;
    cfg.s2sKp = RC4_S2S_KP;
    cfg.s2sKi = RC4_S2S_KI;
    cfg.s2sKd = RC4_S2S_KD;
    drivePID.setTunings(cfg.driveKp, cfg.driveKi, cfg.driveKd);
    s2sPID.setTunings(cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
    Serial.println(F("[PID] PID values reset to RC4 defaults."));
    Serial.printf("[PID] Drive: Kp=%.2f Ki=%.2f Kd=%.2f | S2S: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  cfg.driveKp, cfg.driveKi, cfg.driveKd, cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
  } else if (cmd.length() > 0) {
    Serial.println(F("[?] Unknown command. Type 'help' for the menu."));
  }
}
