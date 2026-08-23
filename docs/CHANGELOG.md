# Changelog — Z-Class RC4 firmware + bb8 tooling

Builds = `versions.json` counters at the time; each board's banner shows `build N | date | git`.

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
