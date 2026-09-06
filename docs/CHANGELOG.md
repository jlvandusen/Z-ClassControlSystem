# Changelog — Z-Class RC4 firmware + bb8 tooling

Builds = `versions.json` counters at the time; each board's banner shows `build N | date | git`.

## v1.03 — 2026-09-06 · the sealed-ball release (bench-verified)
Everything since v1.02, focused on bringing up, tuning, and fixing the droid **without opening the sealed shell** — plus resilience for the things you can't reach once it is. Bench-verified this session (drive + body flashed, self-test all-PASS).

- **`cfg autocenter`** — drives the S2S to both stops and saves the true midpoint as the steering center (persists across reboot *and* reflash; boot cal no longer clobbers it). The fix for a fresh build that slams to one side.
- **Always-on dome leveling** + live tilt tuning (`tilt gain/lean/slew/alpha/invert`) — the dome stays perched whether or not autoBalance is on; slew/alpha capped so a servo move can't buck it off.
- **IMU power-glitch resilience** — the Trinket retries/re-inits the MPU instead of dying in a blink loop, and the drive waits for the IMU instead of running at pitch/roll = 0. (Root-caused from a real short this session.)
- **Runtime motor/direction sign fixes** — `pref revdrive/revs2s/revs2spot` (polarity) and `invdrivebal/invs2sbal/invs2sstick` (direction), all NVS-persisted and coherent with auto-balance. Correct a backwards-wired motor in a sealed ball with one command — no re-wire, no reflash.
- **Sealed-ball service over the dome bridge** — `cfg autocenter`, the PID autotuner, and every sign fix run over `bb8 monitor ball`; autocenter streams a live "busy" heartbeat while it sweeps.
- **Safety guards** — fall/tip-over auto-disable past 45°, and an S2S stall cutout (jam / dead motor / disconnected pot). Default on, persist, overridable.
- **On-board `selftest [full]`** — a POST that reports PASS/WARN/FAIL for IMU/links/pot/config from *inside* the ball, plus **`setup`**, a guided bring-up wizard.
- **Body NeoPixel servo-twitch fix** — the strip is static-per-state so `show()` stops corrupting the 32u4 servo pulses.

Flash order after installing: `bb8 upload drive` · `body` · `imu` · `dome` (or `bb8 flash <target>` on a BASIC install). Full detail below and in the printable **Build Guide** (`docs/Z-Class_Build_Guide.html`).

## RC4.7 — 2026-09-06 · safety guards, on-board self-test, guided setup wizard
- **Fall / tip-over guard** (`pref fallguard on|off`, default on, NVS-persisted): sustained tilt past 45° for >1.2 s that the balance loop can't recover from = it's on its side → the drive force-disables, brakes all motors, plays an alert, and freezes the black box so the motors don't thrash. **Tap PS to re-arm** once upright.
- **S2S stall guard** (`pref stallguard on|off`, default on, NVS-persisted): the S2S pushing hard (|pwm| ≥ 130) with no pot movement for 700 ms = jammed gear / dead motor / disconnected pot → the S2S is latched OFF (the drive's pitch balance keeps running) and it alerts. The latch clears on re-enable (tap PS) or `cfg autocenter`; while latched `cfg show` reads "[S2S STALLED …]".
- Both guards **fail safe, persist in NVS, appear in `cfg show`** (a new "Guards:" line), and are captured by **`bb8 backup`** (via `cfg dump`) — overridable if they ever false-trip.
- **`selftest` / `selftest full`** (drive console): an on-board POST that diagnoses a sealed ball from the **inside** (complements the PC-side `bb8 doctor`). Reports PASS/WARN/FAIL per line + a summary for: IMU streaming + accel magnitude (~9.8), 32u4 body link (packet age + CRC), dome ESP-NOW link, S2S pot in range, and config/calibration present. `selftest full` adds a gentle two-way S2S nudge to prove motor + pot work together (disables the drive first — re-center with `cfg autocenter` after).
- **`setup`** (drive console): a guided first-bring-up **wizard** — autocenter → level → sign check (nudge test, prints the exact `pref rev*`/`inv*` fixes if it's backwards) → save, one step at a time. While active it owns the console (`go`/`skip`/`y`/`n`/`next`/`quit`).
- Both `selftest` and `setup` **run over the dome bridge** (`bb8 monitor ball`), so a sealed ball is tested and brought up without opening it.

## RC4.7 — 2026-09-06 · runtime sign fixes + sealed-ball service over the bridge
- **Six sign flags are now runtime + NVS-persisted**, replacing the compile-time `#define`s (which stay as the defaults): polarity at the I/O layer — `pref revdrive` (DRIVE motor), `pref revs2s` (S2S motor), `pref revs2spot` (S2S pot); direction at the control-mix layer — `pref invdrivebal` (drive balance), `pref invs2sbal` (S2S roll-hold), `pref invs2sstick` (S2S steering). A backwards-wired motor in a **sealed ball** is now a one-command fix — no re-wire, no reflash.
- **Coherent with auto-balance**: the polarity reversals live at the I/O layer (motor output, pot read), so reversing a motor flips the **whole axis** — balance correction and joystick together — keeping the feedback loop stable instead of turning it into a runaway. The `inv*` flags then set each direction independently.
- **Fix-it order** (get it wrong and it runs away): **stability first** — toggle exactly one of `revs2s`/`revs2spot` until the frame holds center, then re-run `cfg autocenter`; **direction second** — `invs2sbal` / `invs2sstick` / `invdrivebal`.
- All six appear in `cfg show` and are captured by `bb8 backup` (via `cfg dump`), so they survive reboot **and** reflash.
- **Sealed-ball service over the dome bridge**: `cfg autocenter`, the PID autotuner (`autotune drive|s2s` → `apply` → `pid save`), and these six sign fixes all run over `bb8 monitor ball` — injected into the same parser the USB console uses — so a sealed ball never needs opening. `cfg autocenter` now **pumps the tunnel from inside its blocking sweep** (`flushConsoleTunnel()`) and streams a ~1 Hz "busy" heartbeat with the live pot, so the console shows it working instead of going dead for ~15–20 s; the compact `low/high/center` result lands as one line. The relay autotune is non-blocking and streams normally.

## RC4.7 — 2026-09-05 · S2S auto-center, always-on dome level, IMU resilience (evening bench, new engine)
- **`cfg autocenter`** (drive): drive-to-endstops S2S centering for first-time builders. Drives the S2S to both mechanical stops (FIND_PWM 95, stall = <4 counts for 400 ms), takes the midpoint as `potCenter`, saves it to NVS, and parks the frame there. Survives reboot; re-invocable any time; fails safe if the pot barely moves (|hi−lo|<100). Bench-verified on the new engine: low 336 / high 1322 → **center 829** (matched the manual re-level of 824). The gearbox holds the frame wherever it's left, so its resting pose is NOT the center — this finds the true one instead of the flopped one.
- **Boot cal no longer overwrites `potCenter`**: RC4 re-captured the pot center at every boot from the (flopped) boot pose, so the saved center was clobbered each reset — the root cause of the S2S slamming to one side. Boot cal now re-zeros pitch/roll only; the center persists from `cfg autocenter` / `cfg set potcenter`.
- **Dome leveling is now always-on** (body): the IMU-based dome level (servos counter body roll/pitch to keep the dome perched) used to be gated behind autoBalance. On the 32u4 that flag only ever gated this cosmetic level — the real balance loop is on the drive ESP32 — so it's decoupled: the dome stays level whenever the drive is enabled, and autoBalance now controls drive stabilization only.
- **Servo motion slowed to keep the dome seated**: a fast servo lurch can throw the magnet-riding dome off. `tilt slew` default effectively lowered (this build runs 90 °/s, was 220) and `tilt alpha` 0.22 (was 0.35); both live-tunable + persisted. `tilt invert x|y` set for the new drive's mirrored geometry (this build: invX 0, invY 1).
- **IMU power-glitch resilience** (drive + imu — *staged in source, lands on the next flash*): a hardware short this session browned out the board and (on old firmware) left the Trinket dead in its `mpu.begin()` blink loop → "No IMU samples collected" → pitch/roll ran at 0. Now the Trinket **retries `mpu.begin()`** until the MPU answers (self-heals once power settles) and **re-inits mid-run** if the accelerometer flatlines (I2C dropout); the drive's boot calibration **waits up to ~15 s** for the IMU to come online instead of giving up after 3 s and defaulting the offsets to 0.
- Docs: new **`docs/Z-Class_Build_Guide.html`** (printable assemble → flash → calibrate → first-moves → save walkthrough); FirstTimeSetup/HowToGuide/RigTuning/Runbook/Assembly_Drive updated for autocenter + dome level + tilt tuning.

## RC4.7 — 2026-09-05 additions (bench)
- **Boot pad-reset** (`pref btreset on|off`, default on, persisted): after a drive reboot a paired pad reconnects still holding stale link state and sits lit until it times out. The drive now bounces each pad ONCE as it reconnects within 25 s of boot (`gp->disconnect()` → LEDs off → clean reconnect); normal mid-session reconnects and deliberate power-ons are untouched.
- **Body boot sound reliable**: the startup track fired 1 s after `mp3.begin(doReset=true)`, but that reset makes the DFPlayer remount the SD (~2-3 s) so the play was dropped ("scratch then nothing"). `playBootSoundConfirmed()` settles ~3 s then plays with BUSY-pin confirmation + up to 3 retries.

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
