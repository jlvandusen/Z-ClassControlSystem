# Z-Class Control System — Operations Runbook

**Applies to:** firmware RC4.2 (`firmware/`), bb8 Commander (`tools/Bb8Commander`), repo HEAD 2026-08-21
**Audience:** whoever is at the bench with the droid, a laptop, and a USB cable.

---

## 0. The five rules that prevent 90 % of bad evenings

1. **One program per COM port.** A monitor that is still open is what makes `upload` and `tune` fail ("butterfly_recv failed", "access denied", "port doesn't exist"). Close the monitor (`q` + Enter) before any upload or tune. The tool will tell you when a port is held — believe it.
2. **Opening the drive's port reboots the drive.** The CP2104 auto-reset fires on port open. Boot calibration runs again → **the droid must be sitting level and still whenever you connect to it.**
3. **What's on the chip is what matters, not what's in the repo.** Every board prints `PREFIX | revision | build N | date | git HASH` on boot, on monitor attach, and on `version`. If a feature "isn't there", read the banner first.
4. **Calibrate before you tune.** A zero-offset error looks to the PID like a permanent lean; no gain fixes it.
5. **Commit after a flashing session.** Build stamps with a `+` suffix mean the binary came from an uncommitted tree.

---

## 1. System overview

| Board | Role | MCU | Link to drive | Console |
|---|---|---|---|---|
| **drive** | master: Bluepad32 controllers, balance PID, drive/S2S/flywheel H-bridges | ESP32 HUZZAH32 | — | USB (CP2104), resets on open |
| **body** | dome tilt servos, dome spin motor + encoder, DFPlayer audio | Feather 32u4 | Serial 74880 baud (SerialTransfer) | USB-CDC, no reset on open |
| **imu** | MPU6050 + Kalman pitch/roll at 100 Hz | Trinket M0 | Serial 115200 (SerialTransfer) | USB-CDC |
| **dome** | NeoPixels, battery, deep sleep | ESP32 HUZZAH32 | ESP-NOW ch 11 | USB (CP2104) |

Control loop: 100 Hz on the drive. Pitch → drive motor PID (PWM/deg). Roll → cascaded S2S: outer roll PID produces a target pot position, inner P loop servos the pot. Joystick blends with stabilization; L2 is the throttle boost (half speed released, full pull = 100 %).

Repo layout:

```
firmware/ESP32_DRIVE_RC4/   drive   (canonical source — the Documents\Arduino copies are stale)
firmware/32U4_DRIVE_RC4/    body
firmware/TrinketM0_MPU_RC4/ imu
firmware/ESP32_DOME_RC4/    dome
firmware/*_RC3/             originals, rollback only
tools/Bb8Commander/         the bb8 CLI (C#/.NET 10)
targets.json                fleet definition: sketch, FQBN, USB VID/PID, banner match, connect commands
versions.json               per-board build counters (committed)
docs/                       this runbook, review & fixes, setup notes
```

---

## 2. First-time setup (one laptop, once)

> Bringing up a whole new droid / board set, not just a laptop? The start-to-finish
> walkthrough (flash order, MAC pairing, first calibration, sign checks) is
> [`docs/FirstTimeSetup.md`](FirstTimeSetup.md). The release installer
> (`ZClass-ControlSystem-Setup-v*.exe` from GitHub Releases) does steps 1–3 for you.

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/) and the cores: `esp32-bluepad32:esp32` (drive — it hosts the BT gamepads), `esp32:esp32` 3.x (dome — stock core since RC4.4), `adafruit:avr`, `adafruit:samd`.
2. Install the sketch libraries: SerialTransfer, DFRobotDFPlayerMini, Adafruit MPU6050 + Unified Sensor, Kalman (TKJ), Adafruit NeoPixel, Servo.
3. Install .NET SDK 10+, then in the repo: `.\install.ps1` → builds `bin\bb8.exe`.
4. Optional: add the repo's `bin\` folder to PATH so it's just `bb8 …` from anywhere.
   The checkout can live in any folder — `targets.json` paths are relative to it.
5. `bb8 list` → confirms targets and shows USB ports with board guesses.

---

## 3. Build · flash · verify

```powershell
bb8 build all            # compile everything (drive 88 %, body 93 %, imu 17 %, dome 86 % flash)
bb8 upload drive         # stamp → compile → flash → VERIFY banner
bb8 upload body
bb8 upload imu
bb8 upload dome
bb8 deploy drive         # upload + open the monitor
bb8 update [--flash]     # pull new firmware/tooling from GitHub (--flash: reflash boards that are behind)
```

What `upload` does, in order:

0. **Checks GitHub** — `git fetch`, and a fast-forward if the branch is simply behind (local commits/edits
   are never touched; generated `versions.json` / `BuildStamp.h` are set aside, higher build counters kept).
   If `tools/` changed, `bb8.cmd` rebuilds `bin\bb8.exe` and re-runs the command. Offline → one grey line, carries on.
   `--no-update` / `BB8_NO_UPDATE=1` skips it. Other commands check at most once every 4 h.
1. **Stamps** — bumps `versions.json` for that board, writes `BuildStamp.h` (`BB8_BUILD_NUM / DATE / GIT`), touches the `.ino` so the cache recompiles.
2. **Compiles** with arduino-cli into `build/<sketch>`.
3. **Flashes** — auto-detects the port (VID/PID; the two ESP32s are told apart by boot banner). Native-USB boards (body, imu) get an automatic retry for the bootloader race.
4. **Verifies** — reads the banner back and compares the running build number with the stamp:
   - `[VERIFY] OK — body is running build 7: …` ✅
   - `[VERIFY] MISMATCH — expected 7, board reports 6` → flash again
   - `[VERIFY] no banner` → board still booting or pre-stamp firmware; check with `bb8 monitor`

### 3.1 When the 32u4 / Trinket won't flash

Symptom: `butterfly_recv(pgm, &c, 1) failed` / `initialization failed`.

Causes, in order of likelihood:
1. **A monitor is holding the port** (the 1200-baud reset touch can't open it). Close it. `tasklist | findstr bb8` shows stragglers.
2. **Bootloader race** — the Caterina bootloader lives ~8 s, often on a *different* COM number. bb8 retries once; manual fallback:
   - double-tap the board's reset (LED pulses = bootloader)
   - `bb8 list` → note the new COM
   - `bb8 upload body --port COMx` immediately
3. **Board dropped off USB entirely** (`bb8 list` shows no 239A device) — cable/hub. Re-seat.
   Confirm from Windows' side: `Get-PnpDevice -PresentOnly | ? InstanceId -match 'VID_239A'` — nothing
   listed (not even a faulted device) means the PC never saw the board: charge-only cable, wrong socket,
   or the 32u4 USB stack is wedged → double-tap reset (bootloader enumerates as 239A:000C for ~8 s).
   Seen 2026-08-21: body "plugged in" but absent from PnP entirely.

### 3.2 ESP32 notes

- Port "doesn't exist" during upload right after an identify/probe = the port is mid-re-enumeration; wait 5 s and retry.
- If a probe leaves the chip in download mode (`boot:0x7 DOWNLOAD_BOOT`), any normal `upload` or a DTR pulse recovers it — it's harmless.

---

## 4. Serial monitor

```powershell
bb8 monitor drive                      # one board
bb8 monitor drive body imu --log x.csv # several, color-tagged; Tab switches who gets your keyboard
bb8 monitor COM7 --baud 115200         # raw port
```

- Pinned input line at the bottom; type + Enter sends to the **active** board. ↑/↓ history. `q` + Enter, Esc, or Ctrl+C exits.
- On attach the monitor sends each board its `connectCommands` from `targets.json` (drive: `version`, `cfg show`, `pid show`), so you always see what you're talking to.
- `telemetry on/fast` lines render in the live **status bar** instead of scrolling (`--show-tlm` to scroll them too). `--log` captures *everything* with timestamps.
- Auto-reconnects after resets/replugs.

### 4.1 What you can see from the drive alone

The drive is the hub: IMU pitch/roll, body replies (`debug from32u4`), what it sends the body (`debug to32u4`), link health (`debug 32u4` → `[32u4-LINK] rx=20 pkt/s lastPkt=31ms crcErrs=0 … OK`), dome battery. Each board's *own* internals (DFPlayer logs, encoder, Kalman) need that board's USB.

---

## 5. Console command reference

### 5.1 drive (ESP32)

| Command | Does |
|---|---|
| `help` / `version` | command list / build banner |
| `telemetry on` · `telemetry fast` · `telemetry off` | 20 Hz · 100 Hz stream: `t,exp,pitch,roll,pot,tgt,drv,s2s,fly,en,bal,jx,jy,hz` |
| `cfg show` · `cfg save` · `cfg load` · `cfg reset` | config in NVS |
| `cfg calibrate` | **3 s level calibration: pitch zero + roll zero + pot center** (droid level & still) |
| `cfg calibrate drive` · `cfg calibrate s2s` | pitch zero only · roll zero + pot center only |
| `cfg set pitchoffset/rolloffset/potcenter/mpudeadzone <v>` | manual overrides |
| `pid show` · `pid set drive|s2s kp|ki|kd <v>` · `pid save` · `pid reset` | gains — real units (drive: PWM/deg; S2S: pot counts/deg). `pid save` saves *only* PID |
| `pref swing <deg>` | S2S authority limit (default 70; **40 in use** — full stick at 70 tilted the bare frame 27°). Persisted |
| `pref lean <pwm>` | max joystick drive authority |
| `pref innerkp <v>` | S2S inner position loop gain (PWM per pot count, default 0.9) |
| `pref sndon <n>` · `pref sndoff <n>` | drive-enable / disable feedback tracks (default 60 / 60) |
| `pref sndshut <n>` · `pref sndconn <n>` · `pref sndcal <n>` | controller-disconnect "shutdown" clip (100) · controller-connect "startup" clip (1) · boot-cal-done chirp (6, `0` = silent). **All `pref snd*` and `pref swing` persist in NVS** (RC4.3/4.4) |
| `bt mac` · `bt list` · `bt prefer drive|dome <MAC|slot0|slot1|none>` · `bt prefer show` · `bt forget` | pad pairing/assignment (see §14) |
| `step drive <pwm> <ms>` · `step s2s <counts> <ms>` | open-loop steps (rig) |
| `autotune drive [amp]` · `autotune s2s [amp]` · `autotune apply` · `autotune abort` | on-board relay autotune (rig) |
| `debug …` | `mpu`, `s2s`, `drive`, `32u4` (link health), `to32u4`, `from32u4`, `dome`, `controllers`, `sound`, `flywheel`, `debug` (all) |
| `bt forget` | forget controller pairings |

### 5.2 body (32u4)

| Command | Does |
|---|---|
| `help` / `version` | list / banner (also prints on monitor attach) |
| `telemetry on|off` | 50 Hz: `t,pitch,roll,tx,ty,l,r,bal,en` |
| `tilt show` · `tilt gain <f>` · `tilt alpha <f>` · `tilt slew <deg/s>` · `tilt invert x|y` · `tilt save` · `tilt reset` | dome tilt compensation (EEPROM). Current: `gain 1.0 alpha 0.35 slew 220 invX=1 invY=0` |
| `tilt lean <deg>` | **RC4.5 motion lean** — dome tilts *against* the direction of travel, proportional to commanded throttle, so the magnet-riding dome stays on top of the shell. Default **−8**; sign flips direction, `tilt save` persists |
| `audio status` | DFPlayer ready, BUSY, volume, SD file/folder counts |
| `audio scan [max]` | **muted scan of MP3/0001..00NN — prints which tracks exist** |
| `audio stop` · `vol <0-30>` · `play <n>` | direct audio control |
| `debug` · `debug encoder` · `center` · `set zero` | state dump · encoder view · servos neutral · dome forward = here |

### 5.3 imu (Trinket M0)

`help`, `version`, `debug` (prints pitch/roll/raw at 100 Hz). Banner prints on attach.

### 5.4 dome (ESP32) — also the wireless bridge

Dome-local commands: `help`, `version`, `debug` (prints every ESP-NOW packet + send status), `setmac XX:XX:XX:XX:XX:XX` (drive's WiFi MAC, saved in prefs).

**Anything else you type is tunnelled to the DRIVE over ESP-NOW and the drive's whole console streams back** (RC4.4). `bb8 monitor ball` uses exactly this — see §16. One command at a time (the dome holds one in-flight command for ACK-retry).

---

## 6. Controller mapping (RC4.1)

| Input | Drive controller | Dome controller |
|---|---|---|
| **PS** tap / **PS hold 2 s** | **Drive enable toggle** / **force DISABLE** — acknowledged with a **random quick blip (70–74)**; pin a fixed track with `pref sndon`/`sndoff` | dome-function toggle |
| Drive controller **disconnects** (detected) | force-disable + **0061 "shutdown"** (`pref sndshut`) | — |
| Controller **connects** to the drive slot | startup clip (`pref sndconn`, **1**) | — |
| **CIRCLE** | sound 28 (RC4.3 — no longer a drive toggle; PS is the only one) | sound 28 |
| **CROSS** | autoBalance toggle (sound 63) | random sound |
| Left stick | drive (Y) + steer/S2S (X). **Forward/back also leans the dome against the motion** (`tilt lean`, RC4.5) | dome tilt — **works while autoBalance is on** (RC4.5 blend: stick offset + leveling + motion lean all stack) |
| L1 + stick X | dome spin | **flywheel** |
| L2 | throttle boost; **L2 + D-pad UP/DOWN = volume ±**; **L2 + D-pad RIGHT = excited/smart-ass (40–49)**; **L2 + D-pad LEFT = button blips (70–79)** | — |
| D-pad | sounds 1-30 random / 3 / 4 / 5; L1-shifted 10-13 | sounds 21-23; L1-shifted 16-19 |
| L1 + CIRCLE | silent-mode toggle | — |
| Both D-pad UP 3 s / both DOWN 3 s | save prefs / factory reset + reboot | |

Sound cue model: **boot complete → 0060 "bootup"** (`pref sndcal`) · **PS enable/disable → random quick blip 70–74** (`pref sndon`/`sndoff`, 0 = random) · **pad drop detected → 0061 "shutdown"** (`pref sndshut`) · pad connect → 0001 (`pref sndconn`).

---

## 7. Calibration

When: after mounting changes, after any "it leans at rest", before the first tune of a session, and any time the drive rebooted while the droid wasn't level (remember rule 2).

1. Droid on the rollers (or the bench), **level, hands off**.
2. Drive console: `cfg calibrate` → 3 s → `[CAL] Done (saved). pitchOffset=… rollOffset=… potCenter=…` (the `*` marks what was updated).
3. Sanity: `telemetry on` — pitch and roll should read within ±0.5° at rest, `pot` ≈ `tgt` ≈ potCenter.

`cfg calibrate drive` / `cfg calibrate s2s` re-zero one axis without touching the other's saved values.

---

## 8. Tuning

> **Start here: [`docs/RigTuning.md`](RigTuning.md)** — the measured, repeatable
> rig/sealed-shell procedure (2026-08-22): S2S autotune numbers and the
> conservative-gain rule, the drive-Ki=0 rig rule, and the wireless console
> bridge (`bb8 monitor ball`) for tuning with the shell closed.

### 8.1 The loop: capture → analyze → correct → verify

```powershell
bb8 monitor drive --log s2s1.csv     # then: telemetry fast, do the moves, telemetry off, q
bb8 analyze s2s1.csv                 # per-axis noise/bias, oscillation Hz, saturation, prescriptions
```
Hand any CSV to Claude in the workspace ("read s2s1.csv") for cross-correlation / phase / sign analysis.

### 8.2 S2S (roll) — `bb8 tune s2s`

Droid on the rollers. The tool owns the port, streams 100 Hz telemetry, prompts you to **nudge the top ~5° sideways and let go**, measures peak / overshoots / settle / tail oscillation / saturation, averages over `--nudges N` (default 2), classifies (oscillating → Kp×0.65 Kd×1.15; ringing → Kp×0.8 Kd×1.2; sluggish → Kp×1.25; good → adds Ki=3 then confirms), and `pid save`s. Every transient is logged to `tune-s2s-HHMM.csv`.

What the first capture taught us (2026-08-20): defaults Kp=30 exceeded the S2S actuator's bandwidth — the pot lagged the target by 180 ms at 46 % saturation → a sustained 2.9 Hz limit cycle. Starting point for this mechanism: **Kp 10, Ki 2, Kd 1.0, `pref swing 40`**, then let the tuner refine.

### 8.3 Drive (pitch) — `bb8 tune drive`

Same flow, nudging **forward**, with one physics caveat: on a roller rig the shell spins freely, so pitch barely responds to drive PWM and **any integral winds into a runaway** ("it keeps rolling"). The tuner therefore runs **PD-only** and leaves drive Ki=0 on the rig. On the floor, if it drifts or won't hold a slope: `pid set drive ki 2` → `pid save`.

The tuner warns at start if the angle reads >2.5° or the motor averages >40 PWM at rest — that's a zero-offset, fix it with `cfg calibrate drive` first.

### 8.4 On-board relay autotune (alternative)

`autotune s2s` / `autotune drive [amp]` (Åström–Hägglund → Ziegler–Nichols). Needs drive enabled, a non-ringing starting point, and hands off. Runaway instead of a steady small rock = inverted plant sign → rerun with a negative amplitude. `autotune apply` → `pid save`.

### 8.5 Dome tilt compensation — `bb8 tune dome`

Connects to the **body**. With drive enabled + autoBalance on, the dome servos counter body tilt. Prompted to **rock the droid side-to-side ~1 Hz for 6 s**; the tool measures servo-output lag, amplitude ratio and roughness against the commanded tilt and adjusts `tilt alpha` (smoothing) and `tilt slew`, then `tilt save`s.

By eye, before/after: the dome should **lean opposite the body (stay level)**. If it leans *with* the body → `tilt invert x` (or `y`). How much it compensates is `tilt gain` (1.0 = level; >1 exaggerates).

### 8.6 Sign switches (compile-time, drive `.ino`)

`REVERSE_DRIVE`, `REVERSE_S2S`, `S2S_STICK_INVERT`, `S2S_BALANCE_INVERT`, `DRIVE_BALANCE_INVERT`. Use when joystick direction is right but balance pushes *into* the lean (balance invert), or joystick itself is backwards (reverse/stick invert). From the 2026-08-20 capture: S2S balance polarity is **correct** as shipped (`corr(roll, tgt) = −0.96`).

---

## 9. Audio

- **Boot calibration** plays `pref sndcal` (**track 6**, the "I'm level — you can enable" chirp) ~3 s after every power-up; it fires with no controller connected, once per boot. `pref sndcal 0` silences it; `pref sndcal <n>` picks another track. (This is the "random sound with nothing connected" — it's the cal-complete chirp, not noise.)
- **`bb8 sounds [E:] [--flash]`** — scan the DFPlayer SD from the PC: bank-coverage report (which triggers reach which files, whats missing), regenerates the PSI beep-envelope tables (needs ffmpeg), and `--flash` reflashes the dome when the set changed. Run it after every card edit.
- **Track banks** (numbering convention): **1–31** general chatter (D-pad UP rolls these) · **40–49 excited / smart-ass** (L2+D-pad RIGHT) · **50** — · **60–66** state cues (**60 bootup/enable, 61 shutdown/disable** — L3's reverse-toggle also plays 61 — 62 dome-mode, 63 autoBalance, 64/65/66 —) · **70–79 button / quick-press blips** (L2+D-pad LEFT) · **80–89 errors / alerts** (fired by safety events: IMU-stale cutoff, rig-experiment abort) · **99–106** long/specials (**1 = startup, 100 = shutdown, 6 = boot-cal chirp** live outside their banks by history).
- Files live in `MP3/0001.mp3 … 00NN.mp3` on the DFPlayer's SD (FAT32). Tracks **1–119** are addressable from the pad/prefs (the link field is an `int8_t`; 125/126/127 are the volume±/silent control codes since RC4.3). The console `play <n>` has no limit. Card as of 2026-08-22: 1–31, 50, 60, 99–103, 105–106 — **1 = startup, 100 = shutdown**.
- **`audio scan`** on the body console tells you exactly which numbers exist (muted, ~20 s for 1-100). A missing track now logs `[AUDIO] DFPlayer ERROR (TRACK FILE NOT FOUND on SD)` instead of failing silently.
- Sound commands ride the 50 Hz state stream with a sequence number, repeated 5×, so the DFPlayer's SoftwareSerial interrupt-blocking (which corrupts ~1–2 link packets per command — the `CRC_ERROR`/`PAYLOAD_ERROR` lines) can't drop them. Those error lines are expected and cosmetic.
- Missing on the card (verified by ear 2026-08-22): **61** (L3), **62** (dome-pad PS), **63** (CROSS/autoBalance) → those presses are silent until the files exist. `audio scan` is trustworthy since body build 10 (it used to credit the *next* number with the previous track's BUSY — 32/51/61 were phantoms).
- **PSI "talking" light**: the body reports `isplaying` → drive → dome over ESP-NOW; the dome runs the white speech-flicker while it's set (§15). If sounds play but the PSI doesn't talk, it's the ESP-NOW link (§11).

---

## 10. Safety behaviors (firmware)

| Event | Response |
|---|---|
| IMU stale > 500 ms | autoBalance output cut, motors braked, **alert sound (80–89)** |
| Drive controller disconnects | drive force-DISABLED + shutdown clip (100); dome pad may be promoted but drive stays disabled until PS |
| PS held 2 s | drive force-DISABLED (+ sound) |
| No ESP32 packet to body for 2 s | servos neutral, dome motor stopped |
| Rig experiment (`step`, `autotune`) | aborts on \|angle\| > 15°, joystick grab, mode change, 25 s timeout — **alert sound (80–89)** |
| Drive disabled | PID integrators reset; re-enable starts clean |

---

## 11. Troubleshooting matrix

| Symptom | Cause | Fix |
|---|---|---|
| `upload` fails, butterfly/avrdude errors | monitor holding the port / bootloader race | close monitor; retry; double-tap reset + `--port` |
| "Unknown command" or silence to `help`/`version` | board runs pre-stamp firmware | read the banner; `bb8 upload <board>` |
| `[VERIFY] MISMATCH` | stale binary in cache | upload again (the `.ino` touch now prevents this) |
| Board "plugged in" but `bb8 list` / PnP show no 239A | PC never enumerated it | charge-only cable / socket; double-tap reset; try another cable |
| `[UPDATE] could not fast-forward` | local commits or edits collide with GitHub | `git pull --rebase` (commits) or commit/stash edits; bb8 never forces |
| `[UPDATE] GitHub unreachable` | offline / proxy | harmless — local firmware is used; `bb8 update` later |
| Toggle sound plays every other press | old body firmware (`lastTrack`/busy suppressors) | flash body |
| Track commanded but silent | file missing on SD | `audio scan`, add the file or `pref sndon/off` |
| CRC/PAYLOAD lines ~30 ms after every sound | SoftwareSerial interrupt blocking | expected; protocol tolerates it; `debug 32u4` shows the rate |
| CROSS / dome-pad PS make no sound | tracks 63 / 62 not on the SD (RC4.3 scan: 1–31, 50, 60, 99) | add `MP3/0062.mp3`, `0063.mp3` (and `0061` for L3) |
| Droid oscillates left-right with balance on | outer S2S gain beyond actuator bandwidth and/or roll zero off | `cfg calibrate`, Kp 10 / Ki 2 / Kd 1, `pref swing 40`, `bb8 tune s2s` |
| Drive wheel "keeps rolling" on the rig | integral windup on a near-open-loop pitch plant + zero offset | `cfg calibrate drive`, Ki=0 on the rig (`bb8 tune drive` does this) |
| Controller pairs then drops / laggy | radio coexistence | RC4 uses `PREFER_BALANCE` + 11 dBm; escalate to `PREFER_BT` in the drive sketch if needed |
| Dome leans with the body | tilt sign | `tilt invert x` / `tilt invert y` on the body (x is inverted on this droid) |
| Dome leans the wrong way when driving | motion-lean sign | `tilt lean 8` (positive) instead of −8, `tilt save` |
| Tilt stick dead with autoBalance on | body firmware < build 13 | flash body (RC4.5 blends stick + leveling) |
| Servos weak / body resets when servos move | servos on the 5 V logic feed | own 6 V rail (BEC or bench supply), common GND, 470–1000 µF at the servos |
| PSI doesn't flicker on sounds / dome crash-loops `Should enable WiFi modem sleep…` | dome built on the Bluepad32 core (boots BTstack it never uses → 90 % ESP-NOW loss; `setSleep(false)` aborts) | dome builds on **stock `esp32:esp32`** since RC4.4 — `bb8 upload dome` |
| `bb8 monitor ball`: dome answers, drive doesn't | pad not connected — BT inquiry scan starves the drive's radio RX | power the pad on, wait for the startup chirp, retry |
| `bb8 monitor ball`: "2 candidate ports" | drive AND dome both on USB | `--port <domeCOM>` or unplug the drive's USB |
| Random sound ~3 s after power-up with no pad | boot-cal-complete chirp (track 6), by design | `pref sndcal 0` to silence, `pref sndcal <n>` to change |
| Drive boots with wrong zero | port-open reset while not level | `cfg calibrate` level |
| 32u4 build won't fit | flash at 93 % | trim debug strings; long-term: v10 body MCU |

---

## 12. Versions & release discipline

- Banner format: `BOOT | Joe Drive Rev 1.0 RC4 | build 24 | 2026-08-23 11:29 | git 6128198+`
- `build N` ↔ `versions.json` (committed after each session); `git HASH` ↔ the exact commit; `+` = dirty tree at flash time.
- `bb8 upload` verifies the running build after every flash; `bb8 update --flash` reflashes only boards whose banner hash is behind their sketch. Tag milestones: `git tag rc4.5-bench-2026-08-23 && git push --tags`. Full feature history: [`docs/CHANGELOG.md`](CHANGELOG.md).

---

## 13. Known limits / next

- 32u4 at **95 %** flash (build 13) and 1.7 KB RAM used of 2.5 KB — the next body feature needs a diet; v10 board design addresses this.
- DFPlayer on SoftwareSerial is the structural source of link corruption — v10 moves audio to a hardware UART.
- Drive Ki must be finished on the floor; the rig can't express it (Ki is 0 on the rig). Pitch balance **sign** is still unverified.
- The dome-tilt servos must have their own rail (6 V, high current). On the 5 V logic feed they're weak and brown the body out.
- Wireless bridge (§16) needs the gamepad connected; the body (no radio) is only reachable by USB — `bb8 tune dome` is a shell-open job.
- The dome's logic-bar pixel count is a guess (`LOGIC_PIXELS 4`) until counted.

---

## 14. Pairing PS3 / Navigation controllers — `bb8 pair` (self-help walkthrough)

PS3-era pads (Sixaxis, DualShock 3, **PS Move Navigation**) connect only to the one Bluetooth
**master address** stored inside them — the drive's ESP32. The drive in turn decides which pad is
**PRIMARY (drive slot)** and which is **SECONDARY (dome slot)** by the pad's own MAC. `bb8 pair`
walks through all of it; nothing needs SixaxisPairTool or editing the sketch.

### What you need
- Drive board on USB (close any monitor — one program per port)
- Each pad + a **data** micro-USB cable (charge-only cables are the #1 failure)

### The walkthrough

```powershell
bb8 pair
```

1. **Drive MAC** — the tool opens the drive (it reboots — normal), sends `bt mac`, and prints
   `drive Bluetooth MAC: C4:5B:BE:…`. (No drive handy? `bb8 pair --mac C4:5B:BE:90:6A:6A` — the MAC is
   also in the drive's boot banner `[BT] Host MAC:`.)
2. **Plug pad #1 in.** Within a second the tool shows:
   ```
   [PAIR] detected: PS Move Navigation
          pad MAC:        00:06:F5:64:60:3E     <- the pad's OWN address (feature report 0xF2)
          current master: 00:00:00:00:00:00     <- who it currently pairs to (feature report 0xF5)
   Pair this pad to the drive (C4:5B:BE:90:6A:6A)? [Y/n]
   ```
   Enter → it writes the master and reads it back: `master written and verified`.
3. **Assign it:** `[1] PRIMARY = drive pad  [2] SECONDARY = dome pad  [Enter] skip`.
   The tool sends `bt prefer drive <padMAC>` (or `dome`) to the drive, which saves it in NVS and
   confirms: `stored in drive NVS as PRIMARY (drive)`.
4. **Unplug, plug pad #2**, repeat (choose `2`). `q` + Enter finishes; the tool prints the drive's
   stored assignment (`[BT] preferred DRIVE … / preferred DOME …`).
5. **Use them:** power the drive, press **PS** on each pad. They connect and land in their assigned
   slots regardless of connection order. `bt list` on the drive console shows slot, MAC and model.

`bb8 pair --auto` does the same with no prompts (first pad = primary, second = secondary);
`bb8 pair --list` only shows the pads' MACs and masters.

### Doing it by hand (no tool) / fixing a live assignment
| On the drive console | Does |
|---|---|
| `bt mac` | the master address pads must hold |
| `bt list` | connected pads: slot, MAC, model, and the stored preference |
| `bt prefer drive slot0` / `bt prefer dome slot1` | store *the pad currently in that slot* as primary/secondary |
| `bt prefer drive 00:06:F5:64:60:3E` | store by MAC |
| `bt prefer drive none` | clear (first pad to connect takes the slot) |
| `bt forget` | forget Bluetooth link keys (pads must reconnect) |

Slot rules: the preferred DRIVE pad always claims slot 0 (any occupant moves to the dome slot);
the preferred DOME pad takes slot 1 (or slot 0 temporarily if no drive pad yet); anything else fills
the first free slot. If the drive pad disconnects, drive is force-DISABLED and the dome pad is
promoted — but drive stays disabled until you tap PS.

Protocol notes: master = HID feature report `0xF5` `[F5][00][MAC×6]`; own address = report `0xF2`
bytes 4..9 — identical to Bluepad32's `tools/sixaxispairer` and hid-sony. If Windows refuses the
write, the original SixaxisPairTool (libusb) is the fallback — write the MAC from `bt mac`.

---

## 15. Dome lights (RC4.5)

Reference look: the "talking" BB-8 — white PSI pulsing with the voice, calm blue logic bars, no KITT/cylon sweeps.

| Light | Behaviour | Where to tweak (`ESP32_DOME_RC4.ino`) |
|---|---|---|
| **PSI** | While a track plays: **white**, easing up/down (0.22–0.5 s ramps, 60–250 ms holds) with a fast flicker burst after ~60 % of peaks; dark when the sound ends. PSI runs at brightness 255 (other strips 64). Idle: dark, or the pad anims (CROSS = rainbow, CIRCLE = red, L1 = blue) painted on change | `updateAnimations()` PSI block — `rampMs`, hold windows, the `< 60` flicker chance |
| **Logic bars** (2×) | slow **blue comet** scrolling along the bar, head bright / tail fading, the two bars offset half a bar | `LOGIC_PIXELS` (set to the real count!), 160 ms step |
| **HP** | solid blue (one-liner to make it red) | `HP.Color(0, 0, 255)` |
| **Eye** | unchanged: green boot flash, random red while running | eye block |

The PSI flag is **not** generated on the dome — it's `isplaying` from the body's DFPlayer BUSY pin, relayed by the drive over ESP-NOW (change-driven + 1 Hz heartbeat). Measured link after the RC4.4 radio fixes: 98/98 packets, flicker on 16/16 sounds, ~160 ms from sound start to first flash.

---

## 16. Wireless console bridge — `bb8 monitor ball` (RC4.4)

For tuning with the shell closed: the dome, off the droid and on USB at the PC, is a transparent bridge to the drive.

```
PC (bb8) ──USB──> dome ──ESP-NOW──> drive (sealed in the ball)
```

- `bb8 monitor ball [--log x.csv]` — `ball` is the dome's USB port. Anything you type that isn't a dome-local command (`help`/`version`/`debug`/`setmac`) goes to the drive; the drive's whole console (telemetry, `pid show`, autotune progress…) streams back. Measured: 20 Hz telemetry 198/200 lines in 10 s, zero corrupted lines, drive loop unaffected.
- `bb8 tune s2s --port <domeCOM>` — the tuner works through the bridge unchanged (`--port` is needed: the dome doesn't answer the drive's banner).
- **Pad must be connected** — while Bluepad32 is inquiry-scanning for a pad, the drive's WiFi receiver is starved and commands get no ACK. Autotune needs the drive enabled anyway.
- The drive mirrors its console only while the tunnel is **armed**: any command arms it 60 s, the dome keepalives every 15 s while bb8 is attached, so a dropped session can't leave the drive spraying radio while you drive.
- One command at a time; prefer `telemetry on` (20 Hz) over `telemetry fast` wirelessly.
- Under the hood: `TunnelCmd` (185 B) / `TunnelOut` (241 B) ESP-NOW packets, sizes are the discriminator; drive `TeeSerial` wraps `Serial` so no call site changed; dome retries un-ACKed commands 6×40 ms; drive keeps modem sleep (BT needs it) but opens the connectionless RX window (`esp_now_set_wake_window`).
- Not bridged: the **body** (no radio). Dome-tilt tuning stays a USB job.

Full procedure with the measured tuning numbers: [`docs/RigTuning.md`](RigTuning.md).
