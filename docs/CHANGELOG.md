# Changelog — Z-Class RC4 firmware + bb8 tooling

Builds = `versions.json` counters at the time; each board's banner shows `build N | date | git`.

## RC4.7 — 2026-09-01 · the capabilities drop (in source — lands on each board's next flash)
- **Wireless drive updates (OTA)**: `bb8 upload drive --ota` (or `bb8 flash drive --ota` for the prebuilt) streams the app image through the dome's ESP-NOW bridge — the sealed ball updates in ~2–3 min with no USB. Needs: drive powered + DISABLED, pad connected. The stock featheresp32 partition table already has dual OTA slots, so no partition change; a failed/aborted transfer leaves the running firmware untouched, and USB stays the rescue path.
- **`bb8 backup [file]` / `bb8 restore <file>`**: the drive's whole tuned state (PID gains, level offsets, pot center, board + pad MACs, sound/idle/battery prefs, macros) captured via the new `cfg dump` as a REPLAYABLE command file. Board swap = flash + restore. Works over USB or the dome tunnel.
- **`bb8 doctor`**: one-command health check — toolchain/update channel/prebuilt binaries, then every plugged-in board's banner and staleness.
- **Black box**: 25 Hz × 30 s ring of pitch/roll/pot/target/PWM that FREEZES on safety events (pad lost, IMU stale, experiment abort); `blackbox dump` prints CSV, `blackbox arm` resumes. "It fell over" is now data.
- **Idle personality**: `pref idle <sec>` — random chatter (bank 1–31) after the sticks go quiet, pad-connected-guarded like every sound. **Dome-battery alert**: `pref batlow <V>` chirps the alert bank + logs when the dome cell sags. **Macros**: `macro set 1 <cmd;wait ms;cmd>` / `macro run 1` — 4 NVS slots, one step per control-loop pass.
- **`dome mac XX:..`** (drive console): the dome board's ESP-NOW MAC is now runtime-configurable + NVS-persisted — a spare dome board no longer needs a drive source edit. `ver` = tunnel-safe version alias.
- **Dome phone dashboard**: `web on` on the dome console (persisted) raises AP **ZClass-Dome** (pass `zclassbb8`) → http://192.168.4.1 shows battery/link/track, the drive's mirrored console, and a command box that injects through the tunnel. Suppresses the inactivity sleep while on.
- **`bb8 monitor drive --web`**: live browser telemetry charts (pitch/roll, pot vs target, PWM) at http://127.0.0.1:8787 — the live version of `bb8 analyze`.
- Releases now publish a **SHA256SUMS** asset and the release-channel update verifies its download against it. `pref lean` / `pref innerkp` now persist. CI compiles the whole fleet + bb8 on every push.

## bb8 Commander — 2026-08-27 · BASIC/MAX installers, toolchain-free flashing, git-free updates
- **`bb8 flash <target>`**: flashes the release's prebuilt binaries with **no arduino-cli, no cores, no git** — bundled `tools\flash\esptool.exe` for the ESP32s (bootloader/partitions/boot_app0/app from `binaries\<target>\flash.json`), the 32u4's Caterina **AVR109 bootloader protocol spoken natively by bb8** (1200-baud touch → block writes → banner verify), and the Trinket M0 flashed by **UF2 file copy** to its `TRINKETBOOT` drive. `bb8 upload` falls back to `flash` automatically when arduino-cli is missing; the banner stays the only judge of success.
- **Release-channel `bb8 update`**: with no `.git`, the latest GitHub **release** is discovered from the `/releases/latest` redirect (no git, no API quota), downloaded, and applied over the install; a new `bb8.exe` lands as `bb8.exe.new` and the `bb8.cmd` wrapper swaps it in on the next run. `bb8 update --flash` compares banner build numbers against the release's `flash.json` builds and reflashes only stale boards. Verified end-to-end against the real v1.02 release.
- **Two installers per release**: `Setup-BASIC` (prebuilt flashing + HTTPS updates — no toolchain/git tasks at all) and `Setup-MAX` (source + toolchain + git link, as before). Same AppId — installing one over the other upgrades in place. `make-release.ps1` builds both, stages esptool, and writes the per-board `flash.json` manifests.
- Port detection no longer needs arduino-cli: USB VID/PID comes from the registry when the CLI is absent.

## bb8 Commander + docs — 2026-08-26 (drive 27 flashed)
- **Portable install**: `targets.json` now ships relative `sketchRoot`/`buildRoot` (`"firmware"`/`"build"`), resolved against the folder `targets.json` lives in — the checkout/install works from any location, nothing is hard-coded. `Install-ZClass.ps1` writes relative paths too; old absolute paths still work.
- **`docs/FirstTimeSetup.md`**: start-to-finish bring-up walkthrough for a new build / fresh board set (flash order, ESP-NOW MAC pairing via dome `setmac` + drive `domeMACAddress[]`, pad pairing, first calibration, polarity sign checks before first enable).

## RC4.6 — 2026-08-25 (dome 16 · imu 2)
- **Beep-synced PSI**: per-track 25 Hz brightness envelopes generated from the SD card's MP3s (`PsiEnvelopes.h`); the drive relays the playing track number so the dome pulses in time with the actual clip (unknown track → generic cadence).
- **Sound banks**: chatter 1–31 (D-pad ↑ roll), excited 40s (L2+→), blips 70s (L2+←; PS toggle uses 70–74), alerts 80s (IMU-stale + experiment aborts), cues 60–63.
- **`bb8 sounds [E:] [--flash]`**: one command scans the card, reports bank coverage, regenerates the PSI envelopes (ffmpeg), and reflashes the dome only when the card changed.

## RC4.5 — 2026-08-23 (drive 24 · body 13 · dome 12 · imu 2)
- **Dome motion lean** (`tilt lean <deg>`, default −8): the drive sends its slewed commanded throttle; the body tilts the dome *against* the direction of travel so the magnet-riding dome stays on top of the shell. Signed, persisted.
- **Tilt blend**: the dome stick is live while autoBalance is on (stick offset + leveling + lean all stack; was if/else).
- **Dome lights** per the reference look: PSI **white** speech-pulse (0.22–0.5 s ramps, flicker bursts) while a track plays, at full brightness; **blue scrolling** logic bars (`LOGIC_PIXELS`); solid blue HP; eye unchanged. Pad anims painted on change — the old per-loop PSI clear fought the flicker.
- Body `invX=1` saved (roll leveling direction on this droid).
- Runbook §15/§16, README capabilities, this changelog, firmware headers.

## RC4.4 — 2026-08-22 (drive 22 · body 11 · dome 5)
- **Wireless console bridge**: dome on USB ↔ ESP-NOW ↔ drive; `bb8 monitor ball`, `bb8 tune … --port <dome>`. Drive `TeeSerial` mirrors the whole console; injected commands ride the normal parser. Measured 198/200 telemetry lines per 10 s.
- **Dome moved to the stock esp32 core** + `WiFi.setSleep(false)`: ESP-NOW delivery 6 % → 100 %; PSI flicker 1/16 → 16/16. Root cause of the "hit or miss" talking light.
- Drive: `esp_now_set_wake_window(65535)`; dome: NACK retry 6×40 ms, keepalive. Pad must be connected for dome→drive.
- `pref swing` persists (set 40). Drive Ki = 0 on the rig. S2S tuned **Kp 10 / Ki 2 / Kd 1** from relay autotune Tu 0.41 s / Ku 22.6 (raw ZN rejected). `docs/RigTuning.md`.

## RC4.3 — 2026-08-22 (drive 19 · body 11)
- PS is the only enable/disable; CIRCLE = sound 28. Pad connect → track 1, pad disconnect → track 100 (shutdown), boot-cal done → track 6 (`pref sndcal`, 0 = silent). `pref snd*` persist in NVS.
- Link control codes moved to 125/126/127 (100 collided with the shutdown clip); tracks 1..119.
- Integrating button debounce (single-sample RF spikes no longer register); sounds only from a connected + armed pad; `randomSeed(esp_random())`.
- Body `audio scan` phantom-track fix. Card truth: 1–31, 50, 60, 99–103, 105–106.

## bb8 Commander — 2026-08-21
- `bb8 update [--flash]`: fetch + safe fast-forward before build/upload/deploy, self-rebuild when `tools/` changed, `--flash` reflashes only boards whose banner hash is behind; `--no-update` / `BB8_NO_UPDATE=1`.
- Board identification by the `version` reply's revision field (`revMatch`); 45 colour lines that printed literal `[36m` fixed.

## RC4.2 — 2026-08-21 (body 9)
- Tilt params runtime-tunable + EEPROM; `bb8 tune dome`; audio diagnostics; upload verification by banner with retries; `bb8 pair` (PS3/Nav over libusb).

## RC4.1 / RC4 — 2026-08-20
- RC3 → RC4 rewrite after the 112-finding review (`docs/BB8_RC4_Review_and_Fixes.md`): single 100 Hz PID path, real-unit gains, cascaded S2S, servo easing, non-blocking ESP-NOW with BT coexistence, state-aware sounds, rig experiments (`step`, `autotune`), telemetry stream, `bb8 analyze` / `bb8 tune`.
