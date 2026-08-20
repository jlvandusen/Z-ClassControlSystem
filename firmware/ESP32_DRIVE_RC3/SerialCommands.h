
#pragma once
#include <Arduino.h>

// External helpers from main code
extern bool saveConfig();
extern bool loadConfig();
extern void resetConfigToDefaults();
extern void beginS2SCenterCalibration();
extern void printControllersSummary();
extern void printControllerDetails();

// Debug flags
extern bool debugAll, debugMPU, debug32u4, debugDome, debugControllersFlag, debugS2S, debugDrive;
extern bool debugSound;

// Swing limit
extern float s2sMaxDegrees;

// IMU data
extern struct struct_messagempu mpudata;

// ---------- Help menu ----------
inline void printHelpMenu() {
  Serial.println(F("\n=== Available Commands ==="));
  Serial.println(F("help or commands      - Show this help menu"));
  Serial.println(F("debug                 - Toggle ALL debug"));
  Serial.println(F("debug mpu             - Toggle MPU debug"));
  Serial.println(F("debug 32u4            - Toggle 32u4 debug"));
  Serial.println(F("debug dome            - Toggle Dome ESPNOW debug"));
  Serial.println(F("debug controllers     - Toggle controller input debug"));
  Serial.println(F("debug s2s             - Toggle S2S debug"));
  Serial.println(F("debug drive           - Toggle Drive debug"));
  Serial.println(F("debug sound on/off    - Enable or disable sound debug"));
  Serial.println(F("debug flywheel         - Toggle Flywheel debug"));
  Serial.println(F("cfg show              - Show current config"));
  Serial.println(F("cfg save              - Save config to NVS"));
  Serial.println(F("cfg load              - Load config from NVS"));
  Serial.println(F("cfg reset             - Reset config to defaults and save"));
  Serial.println(F("cfg set revision <text>"));
  Serial.println(F("cfg set date <YYYY-MM-DD>"));
  Serial.println(F("cfg set pitchoffset <float>"));
  Serial.println(F("cfg set rolloffset <float>"));
  Serial.println(F("cfg set potcenter <int>"));
  Serial.println(F("cfg set mpudeadzone <float>"));
  Serial.println(F("pref swing <float>    - Set S2S swing limit (default 70)"));
  Serial.println(F("cfg calibrate s2scenter - Start 3s calibration"));

  Serial.println(F("pid set drive kp <val> - Set Drive Kp (0-100)"));
  Serial.println(F("pid set drive ki <val> - Set Drive Ki (0-10)"));
  Serial.println(F("pid set drive kd <val> - Set Drive Kd (0-10)"));
  Serial.println(F("pid set s2s kp <val>   - Set S2S Kp (0-100)"));
  Serial.println(F("pid set s2s ki <val>   - Set S2S Ki (0-10)"));
  Serial.println(F("pid set s2s kd <val>   - Set S2S Kd (0-10)"));
  Serial.println(F("pid show               - Show current PID values"));
  Serial.println(F("pid save               - Save PID values to NVS"));
  Serial.println(F("pid reset              - Reset PID values to defaults"));
}

// ---------- Command handler ----------
inline void handleSerialCommand(const String &cmd) {
  if (cmd == "help" || cmd == "commands") {
    printHelpMenu();
  } else if (cmd == "show controllers") {
    printControllersSummary();
  } else if (cmd == "debug sound") {
    debugSound = !debugSound;
    Serial.println(F("[DEBUG] Sound debug"));
  }

  else if (cmd == "debug flywheel") {
    debugFlywheel = !debugFlywheel;
    Serial.println(debugFlywheel ? F("Flywheel debug ENABLED") : F("Flywheel debug DISABLED"));
  }

  else if (cmd == "cfg show") {
    Serial.println(F("\n=== Current Config ==="));
    Serial.printf("Revision: %s | Date: %s\n", cfg.revision, cfg.revisionDate);
    Serial.printf("potCenter=%ld | pitchOffset=%.3f | rollOffset=%.3f\n",
                  (long)cfg.potCenter, cfg.pitchOffset, cfg.rollOffset);
    Serial.printf("mpuDeadzone=%.2f | cfgVersion=0x%08lX\n",
                  cfg.mpuDeadzone, (unsigned long)cfg.cfgVersion);
  } else if (cmd == "cfg save") {
    Serial.println(saveConfig() ? F("[CFG] Saved to NVS.") : F("[CFG] Save failed."));
  } else if (cmd == "cfg load") {
    Serial.println(loadConfig() ? F("[CFG] Loaded from NVS.") : F("[CFG] Load failed."));
  } else if (cmd == "cfg reset") {
    resetConfigToDefaults();
    Serial.println(saveConfig() ? F("[CFG] Reset to defaults and saved.") : F("[CFG] Reset failed."));
  } else if (cmd.startsWith("cfg set revision")) {
    cfg.revision[0] = '\0';
    strlcpy(cfg.revision, cmd.substring(17).c_str(), sizeof(cfg.revision));
    Serial.println(saveConfig() ? F("[CFG] Revision saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set date")) {
    cfg.revisionDate[0] = '\0';
    strlcpy(cfg.revisionDate, cmd.substring(14).c_str(), sizeof(cfg.revisionDate));
    Serial.println(saveConfig() ? F("[CFG] Date saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set pitchoffset")) {
    cfg.pitchOffset = cmd.substring(20).toFloat();
    Serial.println(saveConfig() ? F("[CFG] pitchOffset saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set rolloffset")) {
    cfg.rollOffset = cmd.substring(19).toFloat();
    Serial.println(saveConfig() ? F("[CFG] rollOffset saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set potcenter")) {
    cfg.potCenter = cmd.substring(19).toInt();
    Serial.println(saveConfig() ? F("[CFG] potCenter saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("cfg set mpudeadzone")) {
    cfg.mpuDeadzone = cmd.substring(21).toFloat();
    Serial.println(saveConfig() ? F("[CFG] mpuDeadzone saved.") : F("[CFG] Save failed."));
  } else if (cmd.startsWith("pref swing")) {
    float newSwing = cmd.substring(10).toFloat();
    if (newSwing > 0 && newSwing <= 90) {
      s2sMaxDegrees = newSwing;
      Serial.printf("[PREF] S2S max swing set to %.1f degrees\n", s2sMaxDegrees);
    } else {
      Serial.println("[PREF] Invalid swing value. Must be 1–90 degrees.");
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
  }

  else if (cmd == "debug to32u4") {
    debugTo32u4 = !debugTo32u4;
    Serial.println(debugTo32u4 ? F("[DEBUG] TO 32u4 ENABLED") : F("[DEBUG] TO 32u4 DISABLED"));
  } else if (cmd == "debug from32u4") {
    debugFrom32u4 = !debugFrom32u4;
    Serial.println(debugFrom32u4 ? F("[DEBUG] FROM 32u4 ENABLED") : F("[DEBUG] FROM 32u4 DISABLED"));
  }

  // PID tuning commands
  else if (cmd.startsWith("pid set drive kp")) {
    float val = cmd.substring(18).toFloat();
    if (val >= 0 && val <= 100) {
      drivePID.setTunings(val, drivePID.getKi(), drivePID.getKd());
      cfg.driveKp = val;
      Serial.printf("[PID] Drive Kp updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Kp value (0–100 allowed).");
    }
  } else if (cmd.startsWith("pid set drive ki")) {
    float val = cmd.substring(18).toFloat();
    if (val >= 0 && val <= 10) {
      drivePID.setTunings(drivePID.getKp(), val, drivePID.getKd());
      cfg.driveKi = val;
      Serial.printf("[PID] Drive Ki updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Ki value (0–10 allowed).");
    }
  } else if (cmd.startsWith("pid set drive kd")) {
    float val = cmd.substring(18).toFloat();
    if (val >= 0 && val <= 10) {
      drivePID.setTunings(drivePID.getKp(), drivePID.getKi(), val);
      cfg.driveKd = val;
      Serial.printf("[PID] Drive Kd updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Kd value (0–10 allowed).");
    }
  } else if (cmd.startsWith("pid set s2s kp")) {
    float val = cmd.substring(16).toFloat();
    if (val >= 0 && val <= 100) {
      s2sPID.setTunings(val, s2sPID.getKi(), s2sPID.getKd());
      cfg.s2sKp = val;
      Serial.printf("[PID] S2S Kp updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Kp value (0–100 allowed).");
    }
  } else if (cmd.startsWith("pid set s2s ki")) {
    float val = cmd.substring(16).toFloat();
    if (val >= 0 && val <= 10) {
      s2sPID.setTunings(s2sPID.getKp(), val, s2sPID.getKd());
      cfg.s2sKi = val;
      Serial.printf("[PID] S2S Ki updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Ki value (0–10 allowed).");
    }
  } else if (cmd.startsWith("pid set s2s kd")) {
    float val = cmd.substring(16).toFloat();
    if (val >= 0 && val <= 10) {
      s2sPID.setTunings(s2sPID.getKp(), s2sPID.getKi(), val);
      cfg.s2sKd = val;
      Serial.printf("[PID] S2S Kd updated to %.2f\n", val);
    } else {
      Serial.println("[PID] Invalid Kd value (0–10 allowed).");
    }
  } else if (cmd == "pid show") {
    Serial.printf("[PID] Drive: Kp=%.2f Ki=%.2f Kd=%.2f | S2S: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  drivePID.getKp(), drivePID.getKi(), drivePID.getKd(),
                  s2sPID.getKp(), s2sPID.getKi(), s2sPID.getKd());
  } else if (cmd == "pid save") {
    savePrefs();
    Serial.println("[PID] PID settings saved to NVS.");
  }

  else if (cmd == "pid reset") {
    // Reset PID values to defaults
    cfg.driveKp = DEFAULT_DRIVE_PK;
    cfg.driveKi = DEFAULT_DRIVE_KI;
    cfg.driveKd = DEFAULT_DRIVE_KD;

    cfg.s2sKp = DEFAULT_S2S_PK;
    cfg.s2sKi = DEFAULT_S2S_KI;
    cfg.s2sKd = DEFAULT_S2S_KD;

    // Apply to PID controllers immediately
    drivePID.setTunings(cfg.driveKp, cfg.driveKi, cfg.driveKd);
    s2sPID.setTunings(cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);

    Serial.println("[PID] PID values reset to defaults.");
    Serial.printf("[PID] Drive: Kp=%.2f Ki=%.2f Kd=%.2f | S2S: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                  cfg.driveKp, cfg.driveKi, cfg.driveKd,
                  cfg.s2sKp, cfg.s2sKi, cfg.s2sKd);
  }
}
