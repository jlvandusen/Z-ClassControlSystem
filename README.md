# Z-Class Control System — BB-8 firmware fleet + tooling

Firmware, build system, and a full serial mission-control CLI for the
[Z-Class BB-8 drive system](https://github.com/jlvandusen/Z-ClassDriveSystem).

RC4 is a ground-up fix of the RC3 firmware after a 112-finding review
(110 confirmed against source): single 100 Hz PID control path with real-unit
gains, cascaded S2S position loop, servo easing on the dome tilt, non-blocking
ESP-NOW with BT-coexistence fixes, and a dozen safety/correctness repairs.
Full write-up: [`docs/BB8_RC4_Review_and_Fixes.md`](docs/BB8_RC4_Review_and_Fixes.md).

**RC4.5 (2026-08-23) — what it does now** (history in [`docs/CHANGELOG.md`](docs/CHANGELOG.md)):
- **Wireless console**: the dome on USB bridges `bb8` to the drive over ESP-NOW — tune and capture telemetry with the shell closed (`bb8 monitor ball`).
- **Self-updating tooling**: `bb8 update` / every `upload` pulls new firmware from GitHub first; `bb8 update --flash` reflashes only boards that are behind.
- **Audio cues**: startup on pad connect, shutdown on pad loss, state-aware enable/disable, boot-cal chirp — all `pref`-tunable and persisted; glitch-proof button debounce.
- **Dome**: white speech-pulsing PSI, blue scrolling logic bars; built on the stock esp32 core (the Bluepad32 core's BT stack was killing the ESP-NOW link).
- **Dome tilt**: leveling + stick + throttle-proportional motion lean (`tilt lean`) all blend; S2S tuned (Kp 10 / Ki 2 / Kd 1, swing 40) and the method documented in [`docs/RigTuning.md`](docs/RigTuning.md).

## The fleet

| Target | Board | Sketch | FQBN |
|---|---|---|---|
| `drive` | ESP32 HUZZAH32 Feather | `firmware/ESP32_DRIVE_RC4` | `esp32-bluepad32:esp32:featheresp32` |
| `body`  | Feather 32u4 | `firmware/32U4_DRIVE_RC4` | `adafruit:avr:feather32u4` |
| `imu`   | Trinket M0 + MPU6050 | `firmware/TrinketM0_MPU_RC4` | `adafruit:samd:adafruit_trinket_m0` |
| `dome`  | ESP32 HUZZAH32 Feather | `firmware/ESP32_DOME_RC4` | `esp32:esp32:featheresp32` (stock core, 3.x) |
| `ball`  | *(the dome's USB port)* | — | drive console via the dome's ESP-NOW bridge — `bb8 monitor ball` |

> **This repo's `firmware/` folder is the canonical source.** The sketches are
> plain Arduino-IDE-compatible folders — open them in the IDE from here if you
> like. RC3 originals are kept alongside for reference and rollback (point
> `targets.json` at the `_RC3` sketch and `bb8 upload`).
>
> The **drive** builds on the **Bluepad32** core (it hosts the BT gamepads).
> The **dome** builds on the **stock `esp32:esp32` core (3.x)** since RC4.4 — the
> Bluepad32 core boots a Bluetooth stack the dome never uses, which starved its
> radio into ~90 % ESP-NOW loss; the 3.x ESP-NOW callback signatures are
> version-guarded in the sketch.

**Prereqs:** [arduino-cli](https://arduino.github.io/arduino-cli/) on PATH with
cores `esp32-bluepad32:esp32` (drive), `esp32:esp32` ≥ 3.3 (dome), `adafruit:avr`, `adafruit:samd` installed, plus
the sketch libraries (SerialTransfer, DFRobotDFPlayerMini, Adafruit MPU6050,
Kalman, Adafruit NeoPixel, Servo). [.NET SDK 10+] to build the CLI.

## Install from a release — BASIC or MAX

Two installers per [release](https://github.com/jlvandusen/Z-ClassControlSystem/releases),
both no-admin, both fully self-contained:

| | **Setup-BASIC** | **Setup-MAX** |
|---|---|---|
| For | driving the droid | modifying the firmware |
| Flashing | `bb8 flash <board>` — **prebuilt binaries**, flashers bundled (esptool for the ESP32s; AVR109 and UF2 spoken natively by bb8). `bb8 upload` falls back to this automatically. | `bb8 upload <board>` — compiles from source (arduino-cli + cores, ~1 GB one-time toolchain setup) |
| Updates | `bb8 update` pulls the **latest GitHub release over HTTPS** — no git | `bb8 update` fast-forwards the git checkout — plus everything BASIC does |
| Needs | nothing but Windows | internet for the toolchain task; `git` for the source link |

Installing one over the other upgrades in place (same folder, same AppId). The zip
asset is the same bundle without an installer: extract anywhere, `.\Install-ZClass.ps1`
(add `-SkipToolchain -NoGit` for a BASIC-style setup). Cutting a release:
`tools\release\make-release.ps1 -Version X.YY` — it builds both installers.

The installed (or cloned) folder is **relocatable**: `targets.json` uses relative
`sketchRoot`/`buildRoot` paths that bb8 resolves against wherever `targets.json` lives
(found via `BB8_HOME`, the current directory and its parents, or next to `bb8.exe`).

**New droid or fresh board set?** Follow
[`docs/FirstTimeSetup.md`](docs/FirstTimeSetup.md) — flash order, radio-MAC pairing,
controller pairing, first calibration, and the polarity checks that must come before
the first enable.

## bb8 Commander

```powershell
.\install.ps1          # one-time: builds bin\bb8.exe (bb8.cmd wraps it)

bb8 list               # targets + detected USB serial ports
bb8 build all          # compile the whole fleet
bb8 upload drive       # compile + flash (auto-detects the port)
bb8 flash drive        # flash the PREBUILT release binary — no compiler, no cores, no git
bb8 deploy drive       # build + upload + open monitor
bb8 identify           # probe every port, read boot banners
bb8 update             # pull new firmware / tooling from GitHub
bb8 update --flash     # ...and reflash every plugged-in board that is behind its sketch
bb8 monitor ball       # drive console THROUGH the dome over ESP-NOW (shell closed)
bb8 tune s2s --port COMx   # live tuner — works through the bridge too (COMx = dome)
```

### Staying current with GitHub

Two channels, picked automatically by whether the folder is a git checkout:

- **Git checkout (MAX / dev):** every `build` / `upload` / `deploy` (and any other
  command, at most once every 4 h) does a `git fetch` first and **fast-forwards the
  checkout when that is safe** — it never touches local commits or edits (the only
  files it sets aside are the generated `versions.json` / `BuildStamp.h`, keeping the
  higher build counters). If `tools/Bb8Commander` changed, `bb8.cmd` rebuilds
  `bin\bb8.exe` and re-runs your command. `bb8 update --flash` reads each plugged-in
  board's banner (`git HASH`) and reflashes only the boards whose sketch has commits
  since that hash.
- **No git (BASIC):** `bb8 update` finds the **latest GitHub release over plain
  HTTPS** (no git, no API key), downloads it, and applies it over the install —
  including a new `bb8.exe`, which swaps itself in on the next run. `bb8 update
  --flash` then compares each plugged-in board's banner build number against the
  release's prebuilt binaries and runs `bb8 flash` on the ones that are behind.

Offline, either channel says so once and carries on with what's on disk.
Skip the check with `--no-update` or `BB8_NO_UPDATE=1`.

### The serial monitor

```powershell
bb8 monitor drive                       # one board
bb8 monitor drive body --log run1.csv   # several boards at once, CSV log
bb8 monitor COM7 --baud 115200          # raw port
```

Full-screen with a **pinned input line** — incoming traffic scrolls above it and
never clobbers what you're typing:

| Key | Action |
|---|---|
| type + `Enter` | send a serial command to the **active** board (`help`, `pid show`, `telemetry on`, `debug s2s`, …) |
| `↑` / `↓` | command history |
| `Tab` | switch active board (multi-board mode; each board's lines are color-tagged) |
| `Esc` / `Ctrl+C` | exit |

Send `telemetry on` to the drive and the 20 Hz stream renders as a **live
status bar** (pitch/roll/pot/target/PWM/loop-Hz) instead of scrolling spam —
add `--show-tlm` to scroll it too. `--log file.csv` captures *everything*
(timestamp, board, line — telemetry included) for offline plotting.
`--raw` disables the UI for piping.

Boards reset or unplugged mid-session **auto-reconnect**. Note: close the
monitor before `bb8 upload` on the same port — two programs can't hold one COM
port.

Port auto-detection: 32u4 and Trinket by USB VID/PID; the two ESP32s share the
same CP2104 bridge, so the tool resets the board and matches its boot banner.
If that fails, pass `--port COMx`.

### Typical tuning session

```
bb8 monitor drive --log tune1.csv
> telemetry on
> pid show
> pid set drive kp 14
> pid save
```

Tuning procedure + starting gains: [`docs/BB8_RC4_Review_and_Fixes.md`](docs/BB8_RC4_Review_and_Fixes.md) §5.

### Rig experiments — let the droid tune itself (roller cradle)

With the ball on the roller cradle (free to pitch/roll in place), the firmware
can measure its own physics. In the monitor, with drive enabled:

```
> telemetry fast              # 100 Hz capture for clean data
> autotune drive              # relay autotune: a small controlled rock,
                              #   measures Ku/Tu, prints suggested Kp/Ki/Kd
> autotune apply              # take the suggestion (then 'pid save')
> autotune s2s                # same for the roll/S2S loop
> step drive 80 2000          # open-loop step for system ID captures
> step s2s 200 2000
```

Safety: experiments require drive enabled, abort on |angle| > 15°, joystick
grab, mode change, or 25 s timeout. If the relay oscillation runs away instead
of settling, your plant sign is inverted — rerun with a negative amplitude
(`autotune drive -60`).

Then analyze any logged session offline:

```
bb8 analyze tune1.csv
```

Reports per-axis bias/noise/oscillation frequency, PWM saturation, S2S
tracking error, and concrete gain prescriptions. For a deeper read, hand the
CSV to Claude in this workspace.

## Layout

```
firmware/            Arduino sketches — RC4 (current) + RC3 (reference/rollback)
tools/Bb8Commander/  C#/.NET 10 source for the bb8 CLI
targets.json         fleet definition (sketch, FQBN, VID/PID, boot banner)
docs/                full review, fixes, tuning guide, way forward
install.ps1          builds bin\bb8.exe
bb8.cmd              command wrapper (also rebuilds bin\ after a pulled tool change)
```

## Why the firmware is C++ and the tooling is C#

The firmware depends on Bluepad32 (BT-Classic gamepad host), ESP-NOW, and a
100 Hz control loop — none of which exist in .NET nanoFramework, and the 32u4
(2.5 KB RAM) and SAMD21 can't host a runtime at all. Going full C# would mean
new hardware, new controller input, and worse latency for zero functional gain.
The PC side is the opposite story — everything above the serial port is C#.

## Branches

| Branch | Hardware | Purpose |
|---|---|---|
| `main` | **v9.15 / v8.2 boards** (HUZZAH32, Feather 32u4, Trinket M0) | RC4.x firmware, bb8 tooling, runbook — the droid as built today. Keeps getting fixes. |
| `v10` | **v10.0 boards** (ESP32 DevKit, RP2350-Zero, ICM-42688 IMU) | New firmware ports (`RP2350_BODY_RC5`, drive IMU module, new pin tables), `targets.json` for the new modules, KiCad sources under `hardware/`. |

bb8 Commander and the docs are shared: fix them on `main`, merge `main` into `v10` regularly (`git checkout v10 && git merge main`). Firmware directories diverge by design.
