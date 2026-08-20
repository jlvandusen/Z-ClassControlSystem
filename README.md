# Z-Class Control System — BB-8 firmware fleet + tooling

Firmware, build system, and a full serial mission-control CLI for the
[Z-Class BB-8 drive system](https://github.com/jlvandusen/Z-ClassDriveSystem).

RC4 is a ground-up fix of the RC3 firmware after a 112-finding review
(110 confirmed against source): single 100 Hz PID control path with real-unit
gains, cascaded S2S position loop, servo easing on the dome tilt, non-blocking
ESP-NOW with BT-coexistence fixes, and a dozen safety/correctness repairs.
Full write-up: [`docs/BB8_RC4_Review_and_Fixes.md`](docs/BB8_RC4_Review_and_Fixes.md).

## The fleet

| Target | Board | Sketch | FQBN |
|---|---|---|---|
| `drive` | ESP32 HUZZAH32 Feather | `firmware/ESP32_DRIVE_RC4` | `esp32-bluepad32:esp32:featheresp32` |
| `body`  | Feather 32u4 | `firmware/32U4_DRIVE_RC4` | `adafruit:avr:feather32u4` |
| `imu`   | Trinket M0 + MPU6050 | `firmware/TrinketM0_MPU_RC4` | `adafruit:samd:adafruit_trinket_m0` |
| `dome`  | ESP32 HUZZAH32 Feather | `firmware/ESP32_DOME_RC4` | `esp32-bluepad32:esp32:featheresp32` |

> **This repo's `firmware/` folder is the canonical source.** The sketches are
> plain Arduino-IDE-compatible folders — open them in the IDE from here if you
> like. RC3 originals are kept alongside for reference and rollback (point
> `targets.json` at the `_RC3` sketch and `bb8 upload`).
>
> The two ESP32 sketches must build on the **Bluepad32** core (2.x-based);
> esp32 core 3.x changed the ESP-NOW callback API.

**Prereqs:** [arduino-cli](https://arduino.github.io/arduino-cli/) on PATH with
cores `esp32-bluepad32:esp32`, `adafruit:avr`, `adafruit:samd` installed, plus
the sketch libraries (SerialTransfer, DFRobotDFPlayerMini, Adafruit MPU6050,
Kalman, Adafruit NeoPixel, Servo). [.NET SDK 10+] to build the CLI.

## bb8 Commander

```powershell
.\install.ps1          # one-time: builds bin\bb8.exe (bb8.cmd wraps it)

bb8 list               # targets + detected USB serial ports
bb8 build all          # compile the whole fleet
bb8 upload drive       # compile + flash (auto-detects the port)
bb8 deploy drive       # build + upload + open monitor
bb8 identify           # probe every port, read boot banners
```

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

## Layout

```
firmware/            Arduino sketches — RC4 (current) + RC3 (reference/rollback)
tools/Bb8Commander/  C#/.NET 10 source for the bb8 CLI
targets.json         fleet definition (sketch, FQBN, VID/PID, boot banner)
docs/                full review, fixes, tuning guide, way forward
install.ps1          builds bin\bb8.exe
bb8.cmd              command wrapper
```

## Why the firmware is C++ and the tooling is C#

The firmware depends on Bluepad32 (BT-Classic gamepad host), ESP-NOW, and a
100 Hz control loop — none of which exist in .NET nanoFramework, and the 32u4
(2.5 KB RAM) and SAMD21 can't host a runtime at all. Going full C# would mean
new hardware, new controller input, and worse latency for zero functional gain.
The PC side is the opposite story — everything above the serial port is C#.
