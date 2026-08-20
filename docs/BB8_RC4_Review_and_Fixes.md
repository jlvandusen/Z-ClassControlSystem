# BB-8 Z-Class RC3 → RC4: Full Review, Fixes & Way Forward

**Date:** 2026-08-20 · **Scope:** ESP32_DRIVE, 32U4_DRIVE, TrinketM0_MPU, ESP32_DOME
**Method:** line-by-line review of all four RC3 firmwares by five specialist review
passes (control theory, servo motion, comms/timing, correctness, architecture), each
finding adversarially re-verified against the source. **112 findings, 110 confirmed.**
All RC4 firmware compiles clean; RC3 baselines also compile for rollback.

---

## 1. System architecture (as-built)

```
 PS controller ─BT Classic─┐                     ┌─ESP-NOW ch11── ESP32 DOME (HUZZAH32)
 PS controller ─BT Classic─┤                     │                 NeoPixels PSI/logic/HP/eye
                           ▼                     │                 deep sleep, battery ADC
                  ESP32 DRIVE (HUZZAH32) ────────┘
                  Bluepad32 · PID balance
                  Drive / S2S / Flywheel H-bridges (20 kHz LEDC)
                  S2S position pot on GPIO34
                     │ Serial1 115200          │ Serial2 74880 (pins 13/12)
                     ▼                         ▼
              Trinket M0 + MPU6050       Feather 32u4
              Kalman pitch/roll          2× dome-tilt servos (9/11? → pins 12/11)
              (SerialTransfer)           dome spin motor + 840 CPR encoder
                                         DFPlayer Mini (SoftwareSerial) + BUSY pin
```

Drive mechanics: one side is the geared main drive (pitch axis, fwd/back); the S2S
middle-gear tilts the internal frame left/right to shift flywheel mass and steer
(roll axis); the flywheel provides yaw/spin assist. The S2S pot measures absolute
tilt position — which is why S2S wants a *cascaded* (position) loop, not a raw
PWM loop.

---

## 2. Why the balance was untunable — the five structural defects

These are the confirmed root causes. No PID card values could have fixed them.

### 2.1 Two PID implementations fought over the same motors  *(critical)*
With autoBalance on, `handleMotorControl()` ran `drivePID/s2sPID.compute()` **every
loop pass (~1 kHz)** AND `updateIMUValues()` (every ~20 ms) called
`autoBalanceControl()` — a second, hand-rolled PID with its **own integrator state
and its own gains**. Both wrote `DRIVE_CH`/`S2S_CH`; the H-bridge direction pins
toggled at 50 Hz whenever the two integrators disagreed in sign.

Worse: `autoBalanceControl()` read the globals `driveKp/driveKi/driveKd` which
**nothing ever updates** — not the serial commands, not the gamepad tuner, not the
NVS load. Half of the control effort always ran the compile-time defaults, which is
exactly why tuning "did nothing." It also bypassed all mode gating (ran during
dome-spin/flywheel modes and even with drive disabled), and its S2S output scaling
differed ×10 from the other loop.

### 2.2 Derivative computed at ~kHz on 50 Hz data  *(critical)*
`PIDController::compute()` was called every loop with `millis()` dt. Between IMU
packets the error is constant (derivative = 0); on the pass where a packet lands,
the full 20 ms angle step gets divided by ~1 ms → **derivative spikes 10–20×**, a
full-scale PWM kick at 50 Hz. The `dt<=0 → 0.01` fallback also made the integral
timestep alternate 10:1. That's the "jerk" you felt, and it made Kd untunable.

### 2.3 3° hard-zero deadzone on the angle *before* the PID  *(critical)*
Inside ±3° the controller saw zero error; the instant tilt crossed 3° the error
jumped discontinuously to >3°, which at the effective gain (below) commanded
saturated PWM. Guaranteed limit cycle: rock across the boundary forever, get a
bang-bang burst each crossing.

### 2.4 Hidden ×10 output gain  *(major)*
`DEFAULT_DRIVE_GAIN = 10` (declared `int32_t = 10.0f`) multiplied the PID output.
Your "Kp = 15" was really **150 PWM/degree** — saturation at 1.7° of error. Combined
with 2.3 the loop was pure bang-bang. (The serial card's 0–100 Kp range was really
0–1000.)

### 2.5 The serial tuning commands were mangling your numbers  *(critical)*
`"pid set drive kp".substring(18)` — off by one. **Typing `pid set drive kp 15`
set Kp = 5.** Same for every drive-PID field. So the values you thought you were
testing were never the values running. (S2S offsets were correct.)

**Also confirmed in the chain:** no anti-windup and integrators never reset on
enable; joystick fully *replaced* the PID output (stabilization off while driving,
integral still winding → lurch on release); S2S ignored its position pot in balance
mode and had **opposite motor polarity** between the manual path and the PID/auto-center
paths; boot calibration overwrote your saved level/center in NVS **every boot** with
whatever pose the droid happened to boot in; no IMU-staleness failsafe; the Trinket's
bias calibration could never complete (unreachable finalize) so the bias was never
applied; and the Kalman filter was fed **swapped gyro axes** (pitch used gyro.x,
roll used gyro.y — backwards for the accelerometer angle definitions used), making
the fused angles lag/overshoot during any actual rotation.

---

## 3. Why the dome tilt was jerky

| Defect | Effect |
|---|---|
| ESP32 sent tilt packets every 50 ms (20 Hz) | base staircase rate |
| 32u4 moved servos **only when a packet arrived** | no motion between packets |
| `SERVO_HYSTERESIS 1.8°` gate + integer `Servo.write()` | every move is a ≥2° jump |
| `map()` called with float pitch/roll | truncation to whole degrees |
| `#define FILTERMPU false` tested with `#ifdef` | the "disabled" EMA filter + snap-to-center block was **always compiled in** (adds lag + a snap discontinuity) |
| Stick deadzone (20 counts) not rescaled | tilt jumps ~4.9° when the stick crosses the threshold |
| 3° deadzone on pitch/roll sent from ESP32 | tilt target itself is discontinuous |
| 32u4 `sendToESP32()` **every loop pass** | ~535 pkt/s flooded the 74880 link both ways; ESP32 drained 1 pkt/loop → RX overflow, CRC storms, latency |
| DFPlayer ACK traffic on SoftwareSerial | bit-banged RX disables interrupts ≥1 ms/byte → Servo pulse jitter (physical twitch) |
| Dome-spin extras | boot zero omitted `FORWARD_ANGLE` (servo mode commanded a half-revolution at startup); torn 4-byte encoder reads on 8-bit AVR (random twitches); bang-bang return-to-center; no PWM slew |

And the whole robot stuttered whenever the dome ESP32 was off/asleep:
`handleDomeAndBodyLights()` retried ESP-NOW sends with `delay(10)`×3 **inside the
control loop**, and `sendDomeDataUntilSuccess()` re-sent every 20 ms forever.

## 3.5 Radio collisions (Bluepad32 BT vs ESP-NOW)

The ESP32 has **one 2.4 GHz radio** shared by BT Classic (controllers) and WiFi
(ESP-NOW). RC3 made the contention as bad as possible:

- `ESP_COEX_PREFER_WIFI` — dome **lights** were prioritized over your **controllers**
- `WIFI_POWER_19_5dBm` — max TX power for a peer that is inches away
- the ESP-NOW retry storm above — continuous WiFi airtime grabs

RC4: `ESP_COEX_PREFER_BALANCE` (escalate to `ESP_COEX_PREFER_BT` if needed),
11 dBm TX, and ESP-NOW reduced to change-driven sends + 1 Hz heartbeat,
non-blocking with a 30 ms minimum gap. Channel stays fixed at 11 on both ends.

---

## 4. What RC4 changes (per board)

New sketch folders (RC3 untouched, roll back anytime):
`ESP32_DRIVE_RC4`, `32U4_DRIVE_RC4`, `TrinketM0_MPU_RC4`, `ESP32_DOME_RC4`.
Every change is tagged `// RC4:` in source.

### ESP32_DRIVE_RC4
1. **One control path.** `autoBalanceControl()` + duplicate globals deleted; a single
   `runControl(dt)` owns all three motors, on a fixed **100 Hz tick with measured dt**.
2. **PIDController rewritten**: derivative-on-measurement + low-pass (τ = 40/60 ms),
   integral anti-windup clamp, output limits, caller-supplied dt, `reset()` on
   enable/disable.
3. **Real units, no hidden gain**: Drive Kp/Ki/Kd in PWM per °, °·s, °/s.
   New defaults: Drive 12 / 6 / 0.5 · S2S 30 / 10 / 1.0.
4. **No deadzone in the control path** (kept only for the dome-display tilt values).
5. **S2S cascaded loop**: roll PID (and/or stick) → target pot position, clamped to
   ±`pref swing`; inner P loop with deadband + stiction floor servos the pot.
   One polarity convention everywhere (`REVERSE_S2S`, `S2S_BALANCE_INVERT`,
   `S2S_STICK_INVERT` to adapt to wiring).
6. **Joystick blends with stabilization** (expo 0.3 + slew 1500 PWM/s + L2 scale —
   L2 now correctly 0–1023). Stabilization stays active while driving.
7. **Non-blocking ESP-NOW** (change-latch + heartbeat), coexistence BALANCE, 11 dBm.
8. Serial links **drained** every loop; 32u4 TX rate up to 50 Hz.
9. **Safety**: IMU-staleness cutoff (500 ms) disables autoBalance; controller
   disconnect now neutralizes sticks/buttons (RC3 latched last values — droid kept
   driving); PID state resets on toggle.
10. Serial commands: **substring offsets fixed** (`pid set drive kp 15` now sets 15!),
    `pid save` saves *only* PIDs (RC3 also overwrote your level offsets with the
    current pose), non-blocking command reader, `telemetry on/off` (20 Hz
    Serial-Plotter-compatible stream), `bt forget`, `pref lean/innerkp`.
11. BT keys no longer wiped every boot; 5 s blocking window removed → controllers
    reconnect in seconds.
12. Boot calibration applies to **RAM only** (explicit save persists); config
    auto-migrates: your stored level/center offsets are kept, PID fields reset to
    RC4 defaults once (cfgVersion bump).
13. Gamepad PID tuner kept (L1+UP / L1+LEFT hold 3 s) but it now only adjusts gains
    while normal control keeps running — you feel each change live; CROSS no longer
    triple-booked (balance toggle and light anims suppressed while tuning).
14. D-pad decoded with bitmasks (diagonals work); `brakeDrive/brakeFlywheel` pin bug
    fixed; debug prints rate-limited to 10 Hz and printed once (was twice/loop).

### 32U4_DRIVE_RC4
1. `#if FILTERMPU` semantics fixed (default off).
2. **Servo easing engine**: packets set *targets*; a 15 ms tick low-passes the target
   (α = 0.35) and slews the output at 220°/s, writing `writeMicroseconds()`
   (~0.1° resolution). No hysteresis staircase. Tune feel with
   `SERVO_MAX_DEG_PER_S` / `SERVO_SMOOTH_ALPHA`.
3. Stick deadband rescaled (continuous from zero), float-safe math.
4. RX drained; motion continues between packets; TX rate-limited to 20 Hz.
5. Dome spin: PWM slew (no clunks), boot zero = forward (no more half-revolution
   surprise), atomic encoder reads.
6. DFPlayer ACK off (SoftwareSerial RX was jittering the servo timer).
7. Watchdog actually 2 s; non-blocking serial command reader; debug at 10 Hz.

### TrinketM0_MPU_RC4
1. **Bias calibration state machine actually completes** and applies.
2. **Gyro axes un-swapped** (pitch↔gyro.y, roll↔gyro.x, matching the accel angle
   definitions). If an angle runs away during rotation, flip its `GYRO_*_SIGN`.
3. 100 Hz output (was 50), MPU DLPF 44 Hz (was 21 — too laggy).
4. Inbound commands no longer received into the live IMU struct.

### ESP32_DOME_RC4
1. `battPin` GPIO35: `INPUT` (input-only pin — `INPUT_PULLUP` was a no-op/misleading).
2. Battery telemetry actually sends every 5 min (RC3 declared it, never called it);
   the drive's receive callback is enabled again, so the drive now sees dome voltage.
3. Compile note: build with the Bluepad32 core (esp32 3.x broke the 2.x ESP-NOW API).

---

## 5. Tuning guide (RC4, real units)

Start here — the defaults are deliberately soft:

| Loop | Kp | Ki | Kd | Units |
|---|---|---|---|---|
| Drive (pitch) | 12 | 6 | 0.5 | PWM/°, PWM/°·s, PWM per °/s |
| S2S outer (roll) | 30 | 10 | 1.0 | pot-counts/°, … |
| S2S inner (position) | 0.9 (`pref innerkp`) | — | — | PWM/count |

Procedure (droid on a stand first, then floor):

1. `bb8 monitor drive --log tune.csv` → `telemetry on`. Watch `pitch`, `drv`, `pot`,
   `tgt`, `s2s`.
2. **S2S inner first** (autoBalance OFF): stick left/right should glide the pot to
   target and hold without buzzing. Buzz → lower `pref innerkp` or raise deadband;
   sluggish/undershoot → raise innerkp by 0.1.
3. **Drive Kp** (autoBalance ON, Ki=0 Kd=0 via `pid set`): raise Kp until the body
   corrects briskly and just starts a slow rock, then back off ~30%.
4. **Kd**: raise in 0.1 steps until the rock damps. Too much → gritty/noisy motor.
5. **Ki**: raise until it holds level against a constant push (drift removed) —
   anti-windup is built in, but keep Ki the smallest value that kills steady lean.
6. **S2S outer**: same order. Kp until a roll disturbance is countered confidently,
   Kd to damp, Ki for level-hold.
7. `pid save`. Sanity-check the sign conventions before first floor run: push the
   top of the droid right — the S2S target (`tgt`) must move to counter it. If it
   runs away, flip `S2S_BALANCE_INVERT`.

If a gain change feels like it "did nothing," check with `pid show` — but unlike
RC3, what you type is now what runs.

---

## 6. "Should we rewrite this in C#?"

**Firmware: no.** The 100 Hz balance loop, Bluepad32 (BT Classic host stack),
ESP-NOW, and LEDC live in the ESP-IDF/Arduino C++ ecosystem. .NET nanoFramework
has no Bluepad32/ESP-NOW support and GC pauses are the last thing a balance loop
wants; the 32u4 (2.5 KB RAM) and Trinket M0 can't host it at all.

**Tooling: yes — and it's built.** `bb8` (C#/.NET 10, `tools\Bb8Commander`) now
does what the Arduino IDE did for you — compile, upload, serial monitor — plus
port auto-identification, timestamped color logs, CSV telemetry capture, and it
scripts (`bb8 build all`). The firmware's `telemetry on` stream is
machine-readable (`name:value,…`), so the natural next step is a live plotting
window in the same C# app (see §7).

---

## 7. Way forward (recommended order)

1. **Flash RC4 + field-test** the tuning guide above. Expect: controllers reconnect
   fast, no stutter with the dome off, smooth dome tilt, tunable balance.
2. **Verify S2S polarity + gyro signs on the bench** (both have invert switches in
   config — one test each).
3. **BB8 Commander v2**: live telemetry plot + PID sliders over the existing
   serial protocol (WinForms/WPF or Spectre.Console chart; the CSV log format is
   already there).
4. **OTA for the two ESP32s** (ArduinoOTA in a maintenance mode; flash is at 88% —
   would need the `min_spiffs` partition scheme to fit two OTA slots) — ends
   panel-off USB flashing.
5. **Protocol hardening** (optional): one shared header for the wire structs
   (they're hand-mirrored in three places today), sequence numbers, and actually
   checking the app-level checksums on receive (SerialTransfer's CRC currently
   carries the integrity load — fine in practice).
6. **Known small quirks left as-is** (documented, low risk): drive sends dome anim
   codes 1–7 but the dome implements 1–3; dome wake button needs two presses after
   an inactivity sleep; DFPlayer init is skipped if BUSY reads low at boot;
   `Serial1.begin(115200)` on the drive uses default UART1 pins — matches v9.15
   and your working PCB, but worth pinning explicitly (`Serial1.begin(115200,
   SERIAL_8N1, RX, TX)`) next time the PCB doc is out; 74880 baud carries ~1.1%
   clock error on the 16 MHz AVR — fine at RC4's packet rates.

## 8. Rollback

RC3 folders are untouched and still compile. To go back:
edit `targets.json` sketches to `*_RC3` → `bb8 upload <target>`.
(RC4's config migration bumped cfgVersion; RC3 will see a size-matched config and
use it as-is — your offsets survive both directions.)
