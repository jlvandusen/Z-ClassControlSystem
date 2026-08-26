# First-Time Setup — from wired boards to first drive

The start-to-finish walkthrough for a **fresh build or a fresh board set**: install the
tooling, flash all four boards, introduce them to each other, pair a controller,
calibrate, and make the first safe moves. Daily operation lives in the
[How-To Guide](HowToGuide.md); deep detail in the [Runbook](Runbook.md); tuning method in
[RigTuning](RigTuning.md).

> **The one law of bring-up:** what's on the chip is what matters, not what's in the
> repo or what the seller flashed. Every board answers `version` with
> `PREFIX | revision | build N | date | git HASH`. A board that says nothing, or the
> wrong thing, is running old/foreign firmware — flash it before believing anything else.

---

## 1. Install the PC tooling

Two roads, same result:

**A. Release installer (no dev tools needed).** Download
**`ZClass-ControlSystem-Setup-v*.exe`** from
[Releases](https://github.com/jlvandusen/Z-ClassControlSystem/releases) and run it —
no admin needed, installs to `%LOCALAPPDATA%\ZClass` by default. Tick *install
toolchain* (arduino-cli + the exact cores/libraries) and *link to GitHub* (so
`bb8 update` keeps you current).

**B. Clone the repo (dev road).** Needs [arduino-cli](https://arduino.github.io/arduino-cli/)
and the .NET SDK 10+:

```powershell
git clone https://github.com/jlvandusen/Z-ClassControlSystem
cd Z-ClassControlSystem
.\install.ps1          # builds the bb8 CLI into bin\
```

Cores: `esp32-bluepad32:esp32` (drive), `esp32:esp32` 3.x (dome), `adafruit:avr` (body),
`adafruit:samd` (imu). Libraries: SerialTransfer, DFRobotDFPlayerMini, Adafruit MPU6050
(+ Unified Sensor), Kalman (TKJ), Adafruit NeoPixel, Servo.

Either way the folder is **self-contained and relocatable**: `targets.json` uses
relative `sketchRoot`/`buildRoot` paths which bb8 resolves against the folder
`targets.json` lives in — move the folder anywhere and everything still works.

Sanity check:

```powershell
bb8 list       # the five targets + your USB serial ports with board guesses
```

## 2. Flash the four boards — one at a time

Plug in **one board at a time** the first time through (the two ESP32s share the same
USB chip and a never-flashed board can't identify itself by banner). For each:

```powershell
bb8 upload drive --port COMx    # then: imu, body, dome
```

Suggested order: **drive → imu → body → dome** (the drive is the hub — with it running
you can watch each link come alive from one console). Wait for
**`[VERIFY] OK — <board> is running build N`** each time; that read-back of the boot
banner is the only proof a flash landed. Once a board runs this firmware, plain
`bb8 upload <target>` auto-detects it — `--port` is only for first flashes and ties.

Trouble spots:

- **32u4 / Trinket won't flash** (`butterfly_recv failed`): close any open monitor,
  then double-tap the reset button (LED pulses = bootloader, often a *different* COM
  number) and `bb8 upload body --port COMx` immediately. Full list: [Runbook §3.1](Runbook.md).
- **Opening an ESP32's port resets it.** Harmless, but it means boot calibration
  re-runs — keep the drive level and still whenever you connect (§5).

## 3. Introduce the boards to each other (radio MACs)

The drive and dome talk over ESP-NOW, addressed by MAC — a new board set has new MACs:

1. `bb8 monitor drive` — the boot banner includes
   `[ESP-NOW] Drive WiFi MAC: XX:XX:XX:XX:XX:XX (dome masterMAC[] must match this)`.
   Write it down.
2. `bb8 monitor dome` — its banner shows `[MAC] Dome WiFi STA MAC: …`. Write that down
   too. Then, still on the dome console:

   ```
   setmac XX:XX:XX:XX:XX:XX      # the DRIVE's WiFi MAC from step 1
   ```

   Saved to flash — it survives reboots *and* reflashes.
3. The drive's knowledge of the dome is compiled in: set `domeMACAddress[]` near the
   top of `firmware/ESP32_DRIVE_RC4/ESP32_DRIVE_RC4.ino` to the dome's STA MAC from
   step 2, then `bb8 upload drive` once more.

Link check (pad must be connected — the dome can't reach a drive that's mid-BT-scan):
play any sound; the **PSI pulsing white in time with it** means the radio link is live.
The dome's USB port is also your wireless console into the sealed ball afterwards:
`bb8 monitor ball` ([Runbook §16](Runbook.md)).

## 4. Pair your controller(s)

```powershell
bb8 pair                    # guided PS3/Nav pairing + primary/secondary assignment
bb8 pair --install-driver   # once, if the PS3/Nav libusb driver isn't installed
```

A fresh drive board is a fresh Bluetooth host — pads paired to an old board must be
re-paired. On connect you get the startup sound; `bt prefer drive|dome <MAC|slot0|slot1|none>`
on the drive console pins which pad is which (saved).

## 5. First calibration — before anything moves

Set the droid (or the bare drive unit) **level and still**, then on the drive console:

```
cfg calibrate          # 3 s: pitch + roll zeros + S2S pot center
```

This stores the level offsets and pot center in flash. A zero-offset error looks to
the PID like a permanent lean — no gain will ever fix it, so calibrate first, always.
(`cfg calibrate drive` / `cfg calibrate s2s` redo one axis; `cfg show` displays what's
stored.)

## 6. Sign checks — the step you must not skip on new wiring

A balance loop with a reversed motor doesn't balance, it *shoves*. New build = every
motor's polarity is unproven. With the ball **on a roller cradle or stand** (never the
floor for this):

1. Enable (tap **PS**) with autoBalance on, and nudge the shell a few degrees by hand.
   Each axis should push **against** your nudge, back toward level. A correction that
   pushes *with* the tilt = that motor's leads are swapped (or swap the sign at the
   H-bridge output) — fix, recalibrate, retest.
2. The relay autotuner is also a sign detector: if `autotune drive` / `autotune s2s`
   oscillation **runs away instead of settling, the plant sign is inverted** — rerun
   with a negative amplitude (`autotune drive -60`) to confirm, then fix the wiring.
   Experiments auto-abort at |angle| > 15°, on joystick grab, or after 25 s.

## 7. First tune

Full method in [RigTuning](RigTuning.md). The short version: on the cradle,
`bb8 tune s2s` then `bb8 tune drive` (the tuner nudges, measures, and saves). Rig
rule: the drive axis is near open-loop on rollers — keep **drive Ki = 0 on the rig**,
add Ki back on the floor. Known-good starting gains: S2S **Kp 10 / Ki 2 / Kd 1** with
`pref swing 40`; drive **Kp 12 / Ki 0 / Kd 0.5**.

## 8. Sounds

The DFPlayer reads **`MP3/NNNN.mp3` only** (root files are ignored). `bb8 sounds E:`
reports which sound banks your card covers and regenerates the dome's beep-sync
envelopes (add `--flash` to reflash the dome when the card changed). Which cue plays
when — and the `pref snd*` settings that change them — are in the
[How-To Guide §5](HowToGuide.md).

## 9. From here on

You're set up. Daily flow: `bb8 update --flash` before a session picks up new firmware
for every plugged-in board that's behind; the [How-To Guide](HowToGuide.md) covers the
controls; [Runbook §11](Runbook.md) is the troubleshooting matrix. The golden rules
that prevent 90 % of bad evenings are at the top of the [Runbook](Runbook.md) — the
short form: one program per COM port, boot level, believe the banner.
