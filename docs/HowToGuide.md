# Z-Class BB-8 — Drive System How-To Guide

*The friendly guide. Bringing up a **new build / fresh boards** for the first time?
Start with [FirstTimeSetup](FirstTimeSetup.md). Deep detail lives in the
[Runbook](Runbook.md); tuning numbers in [RigTuning](RigTuning.md); version history in
the [CHANGELOG](CHANGELOG.md).*

---

## 1. What you have

A ball-bot in three layers:

| Layer | What's in it |
|---|---|
| **Drive (inside the ball)** | The inner frame: main drive motor (chain to the shell), side-to-side (S2S) tilt gear for steering, flywheel, IMU, battery. Brain = **ESP32 "drive"** — hosts your PS3/Nav pads over Bluetooth, runs the 100 Hz balance loops, talks to everything else. |
| **Body electronics** | **Feather 32u4 "body"** — dome-tilt servos, dome spin motor + encoder, DFPlayer audio (SD card of sounds). Wired to the drive over a serial link. |
| **Dome** | **ESP32 "dome"** — PSI / logic / HP / eye NeoPixels, linked to the drive by radio (ESP-NOW). Rides the shell on magnets, tilted by the body's servos. Doubles as your **wireless console** when the shell is closed. |

One more "board": **`ball`** isn't hardware — it's the dome's USB port acting as a
radio bridge into the drive (§6).

## 2. One-time PC setup

```powershell
git clone https://github.com/jlvandusen/Z-ClassControlSystem
cd Z-ClassControlSystem
.\install.ps1          # builds the bb8 CLI  (needs .NET SDK 10+, arduino-cli on PATH)
```

Cores: `esp32-bluepad32:esp32` (drive), `esp32:esp32` 3.x (dome), `adafruit:avr` (body),
`adafruit:samd` (imu). Pair pads once with `bb8 pair` (guided).

From then on **the tooling keeps itself current**: every `bb8 upload` checks GitHub
first and pulls new firmware; `bb8 update --flash` refreshes any plugged-in board
that's behind.

## 3. Powering up — what you'll hear and see

1. Power the droid. ~3 s later: **the boot chirp (track 6)** — that's the drive saying
   "I'm level-calibrated, ready." (**Power up with the droid level** — it zeroes its
   angles at boot. `pref sndcal 0` silences the chirp.)
2. Turn your pad on → it reconnects → **startup sound (track 1)**. Dome eye red, logic
   bars scrolling blue.
3. You're live.

## 4. Driving — the controls

**Drive pad** (primary):

| Do this | Get this |
|---|---|
| **Tap PS** | drive enable / disable (sound each way) |
| **Hold PS 2 s** | force-disable — do this before switching the pad off |
| *pad powers off / drops* | droid force-disables itself + plays the **shutdown clip** |
| **CROSS** | autoBalance on/off |
| **Left stick ↑↓** | drive forward/back — the dome automatically leans *against* the motion to stay perched (`tilt lean`) |
| **Left stick ←→** | steer (S2S tilt) |
| **L2** | throttle boost · **L2 + D-pad ↑/↓ = volume ±** |
| **L1 + stick X** | dome spin |
| **CIRCLE** | sound 28 · **L1+CIRCLE** silent mode |
| **D-pad** | sounds (↑ = random 1-30; →↓← = 3/4/5; L1-shifted 10-13) |
| **Both pads D-pad ↑ 3 s** | save prefs · **both ↓ 3 s** = factory reset |

**Dome pad** (secondary): stick = dome tilt (works *while* autoBalance levels it),
L1+stick X = flywheel, CIRCLE/D-pad = sounds, PS = dome-function toggle.

While a sound plays, the **PSI pulses white** like speech. That's your link-health
indicator too — sounds without PSI action means the radio link is down (§7).

## 5. Sounds

SD card in the DFPlayer: `MP3/0001.mp3 … `. Every cue is a preference, changed live
on the drive console and **saved across reboots**:

| Event | Pref | Default |
|---|---|---|
| pad connects | `pref sndconn` | 1 (startup) |
| enable / disable | `pref sndon` / `sndoff` | 60 |
| pad lost / powered off | `pref sndshut` | 100 (shutdown) |
| boot calibration done | `pref sndcal` | 6 (`0` = silent) |

`audio scan` on the body console lists exactly which tracks the card really has.

## 6. Tuning & the sealed shell

Short version (full method: [RigTuning](RigTuning.md)):

- **On the rollers**: `cfg calibrate` level → `bb8 tune s2s` / `autotune s2s` (use the
  measured Ku/Tu, not the raw suggestion) → drive stays **Ki 0** on the rig.
- **Shell closed**: plug **the dome** into the PC — `bb8 monitor ball` is the drive's
  full console over the air (commands in, telemetry out). Pad must be connected.
  `bb8 tune s2s --port <domeCOM>` works the same way.
- Current known-good: S2S **Kp 10 / Ki 2 / Kd 1**, `pref swing 40`, drive Kp 12 / Ki 0 / Kd 0.5 (rig).

## 7. If something's off — fast checks

| Symptom | First move |
|---|---|
| Won't respond to the pad | Is the drive enabled (tap PS)? Pad paired (`bb8 pair --list`)? |
| Leans/oscillates with balance on | `cfg calibrate` (level!), then [RigTuning](RigTuning.md) §3 |
| Sounds play, PSI dark | radio link — dome powered? within range? (Runbook §11) |
| Servos weak / body resets | tilt servos need their **own 6 V supply**, not the 5 V feed |
| Wireless console silent | pad must be **connected** before the dome can reach the drive |
| Anything else | Runbook §11 troubleshooting matrix |

## 8. Golden rules

1. **One program per COM port** — close monitors before flashing.
2. **Boot level** — the drive zeroes its angles at power-on (and on USB port-open).
3. **Believe the banner** — after any flash, `version` must show the new build number.
4. `bb8 update` before a session; commit `versions.json` + tag after.

---

*Assembly instructions for the mechanical drive: `docs/Assembly_Drive.md` (generated
from the Fusion 360 model).*
