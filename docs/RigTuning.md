# Rig & Sealed-Shell Tuning — repeatable procedure

*Written 2026-08-22 from a live bench session (drive build 22, dome build 5,
body build 11). Follow this start-to-finish each time until the droid goes
back in the shell — then §5 is the only section you need.*

---

## 0. The facts this procedure is built on (measured, not guessed)

| Finding | Number | Consequence |
|---|---|---|
| Out-of-shell S2S relay autotune | **Tu = 0.41 s, Ku = 22.6** (amp 150 counts) | plant rings at ~2.4 Hz; classic Ziegler–Nichols Ki (67!) is far too hot — never `autotune apply` blindly on this axis |
| Conservative gains from Ku/Tu | **S2S Kp 10 / Ki 2 / Kd 1.0** (≈0.45·Ku, light I and D) | at-rest roll σ dropped 3.67°→**0.07°**, 2 Hz limit cycle **gone**, 0 % motor saturation |
| Old gains' failure mode | Kd 2.74 pumped the 2 Hz ring; tgt–pot lag 87 counts mean | keep S2S Kd ≤ 1 on this actuator |
| Steer-return transient | full stick at swing 70° = **27° frame tilt**, return rings ±4° @ ~2.7 Hz for ~1 s on the stiction floor (±35 PWM) | `pref swing 40` (persisted now); retest return after any gain change |
| Drive axis on rollers | wheel spins free → pitch loop can never satisfy Ki → **wind-up to full PWM** ("full bore") | **`pid set drive ki 0` on the rig, always**; restore Ki≈2 on the floor only |
| ESP-NOW drive→dome | 100 % (98/98) after dome fixes | link is not the limit anymore |
| ESP-NOW dome→drive | fails while the drive's BT is *inquiry-scanning* (no pad); works with the pad **connected** (occasional first-try fail, retries cover it) | wireless tuning wants the pad on — which autotune needs anyway |

Radio fixes that made the link work (all in firmware now):
- **Dome runs the stock `esp32:esp32` core** (3.x, guarded callbacks). The
  Bluepad32 core booted BTstack the dome never used → 89–94 % packet loss and
  `WiFi.setSleep(false)` was an `abort()`. On the stock core: BT gone,
  `WiFi.setSleep(false)` legal, RX always on.
- **Drive keeps modem sleep** (mandatory with BT) but opens the
  connectionless RX window: `esp_now_set_wake_window(65535)`.
- Dome retries un-ACKed command packets (6×, 40 ms apart).

## 1. Bench setup

- Frame on the rollers, out of shell, **level**. Body + drive on USB for wired
  work — or **only the dome on USB** for wireless (§5).
- One program per COM port. Close monitors before flashing. Opening a drive
  port **resets it** (boot cal re-runs → keep it level; the pad takes
  10–40 s to reconnect — wait for the startup chirp, track 1).
- `bb8 list` to find ports. Drive/dome share VID — `bb8 identify` tells them
  apart by banner.

## 2. Every-session preamble

1. `bb8 update` (pulls new firmware; flash anything it flags).
2. Drive console: `cfg calibrate` (level, hands off, 3 s).
3. `pid show` — expect `Drive Kp 12 / Ki 0 / Kd 0.5 | S2S Kp 10 / Ki 2 / Kd 1`
   (rig state). If drive Ki ≠ 0 on the rig: `pid set drive ki 0`, `pid save`.
4. `pref show`-equivalents: swing should say 40 (persisted since build 22).

## 3. S2S session (roll)

1. **Baseline capture**: `telemetry on`, PS to enable, CROSS for autoBalance,
   hands off 8 s, then a few single nudges, then steer-and-release with the
   stick. Log via `bb8 monitor drive --log s2s.csv`, then `bb8 analyze`.
2. Healthy numbers (out of shell): at-rest roll σ ≤ 0.1°, no sustained
   oscillation, |tgt−pot| p95 ≤ 20 counts, saturation ≈ 0 %.
3. If it rings: `autotune s2s` (hands off; needs drive enabled; self-aborts
   >15°). **Use its Ku/Tu, not its suggested gains**: set
   Kp ≈ 0.45·Ku, Ki 2, Kd ≤ 1 → `pid save`. Re-capture and compare.
4. Steer-return check: full stick, release — should come back with ≤ 1
   overshoot and settle < 0.5 s. Still ringing → lower swing (30) or Kp.

## 4. Drive session (pitch) — rig limits

- PD only (`Ki 0`). `bb8 tune drive` or `step drive 60 2000` + capture.
- Verify the **balance sign**: motors must push *under* the lean
  (`DRIVE_BALANCE_INVERT` if not). Sign has NOT yet been verified on pitch.
- Real pitch tuning (and Ki≈2) happens on the floor, in the shell.

## 5. Sealed shell — wireless console via the dome bridge

The dome (unscrewed from the droid, on your desk, USB to the PC) is a
transparent bridge to the drive:

```
bb8 monitor ball          # "ball" target = dome's USB port, bridges to the DRIVE
```

- Anything you type that isn't a dome-local command (`help`, `version`,
  `debug`, `setmac`) is sent to the drive; the drive's **entire console**
  (telemetry included) streams back. Measured: 20 Hz telemetry = 198/200
  lines over 10 s, zero corruption, drive loop unaffected (925 Hz).
- **The gamepad must be connected** (BT scanning starves the drive's radio
  RX). Autotune needs the drive enabled anyway, so this is the normal state.
- The tunnel arms on any command and auto-expires 60 s after the dome stops
  keepaliving (dome pings while it has seen USB traffic in the last 10 min) —
  so a dropped session can't leave the drive spraying packets while driving.
- Everything in §2–§4 works identically through the bridge: `cfg calibrate`,
  `pid ...`, `autotune ...`, `telemetry on`, captures with `--log`.
- 100 Hz `telemetry fast` fits the link budget but eats more airtime — use
  20 Hz unless chasing something specific, and keep sessions short while the
  droid is actively being driven.

## 6. When you're happy

`pid save` (gains), prefs already persist (`swing`, sounds). Commit
`versions.json` + BuildStamps, note the gains in the session log, and tag.

## 7. Current state ledger (update each session)

| Date | Board builds | S2S | Drive | Notes |
|---|---|---|---|---|
| 2026-08-22 | drive 22 / dome 5 / body 11 / imu 2 | **Kp 10 Ki 2 Kd 1**, swing 40 — at-rest verified, steer-return retest pending | Kp 12 **Ki 0** Kd 0.5 (rig); sign unverified | wireless bridge live; pad idle-stick read jx/jy −127 once after reconnect — watch |
