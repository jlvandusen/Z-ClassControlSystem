# Z-Drive v10 mainboard — status, files, firmware port

**Status (2026-08-23):** rev A **compact** board laid out, routed, KiCad DRC 0 errors, JLCPCB
fab package **r7** generated (`hardware/fab/ZDrive_v10_compact_JLCPCB_r7.zip`). The r6 package was
uploaded/previewed at JLCPCB first; r7 differs from r6 **only** in the U7 direction fix below
(BOM and CPL identical). *If the order went in from r6, rev A boards need the U7 bodge.*

The **v10 firmware is not written yet** — the v9.15 system stays in service. Everything the port
needs (pin maps, behaviour changes, `bb8` target changes) is in §5 so it can be done later without
re-deriving it from the netlist.

## 1. Errata — rev A

| # | What | Fix |
|---|---|---|
| 1 | **U7 74LVC245 DIR** was strapped to GND on r6. Per the TI SN74LVC245A pin table DIR **high = A→B**; the 5 V encoder sits on A1/A2 and the RP2350 on B1/B2, so with DIR low the encoder is never read and U7's A outputs fight the encoder. | r7 gerbers: pin 1 → +3V3 (jumper to pin 20), unused A3–A8 → GND. **r6 boards:** cut the GND thermal spoke into U7 pin 1, solder-bridge pin 1 to pin 20 (VCC, same end of the package), meter-check pin 1 = 3.3 V. |
| 2 | J2/J3 motor ribbons are **bare 2×5 headers, not keyed** (VCC on pins 1/2, GND on 9/10 — a reversed ribbon shorts 3V3 to GND). | Mark pin 1 on both ribbon ends; check before power. |
| 3 | J1 XT60PB-M (15.5 mm + plug) and a standard mini ATM fuse (~16 mm) exceed the ~14 mm cover budget in the teardrop zone. | Low-profile APS/ATT mini fuse, cover window for the XT60 — see `hardware/mechanical/compact-cover/cover_layout.md`. |
| 4 | No hardware `MOTOR_EN`, no test points, no reset switch on rev A. E-stop (J15 → GPIO36) is firmware-enforced; the 10 k pull-downs R12–R23 hold the drivers off during reset. | Firmware (§5). |
| 5 | MAX9744 is a plug-in **module** on J_AMP (2×7); the on-board QFN is deferred to rev B. | — |

## 2. As built (one screen)

- **Input:** single 12 V 3S4P pack → J1 XT60PB-M vertical → F1 10 A mini blade (Keystone 3568) →
  Q1 Si7461DP high-side reverse-polarity FET (D6 BZT52C12 gate clamp, R33 47 k) → `VIN`;
  D1 SMBJ15A TVS, C1 220 µF/25 V. **No charge path on the board** — the pack charges through its own
  lead out the axle. R24/R25 47k/10k + D4 BAT54S + C29 → `VBAT_SENSE` (ESP32 GPIO35).
- **Rails:** PS1 Pololu D24V50F5 → `+5V_LOGIC` (C2 470 µF) and `+5V_LED` via F3 3 A polyfuse (C3 1000 µF);
  PS2 Pololu D24V25F6 → `+6V_SERVO` 6.0 V (C4 470 µF/16 V; Hitec HS-805BB or JX PDI-HV2060MG);
  U5 AMS1117-3.3 → `+3V3`; amp `AMP_12V` from VIN via F4 3 A + L1 BLM31KN601 + C7 1000 µF/25 V.
  RP2350-Zero 5 V via D2 SS14 (`RP_5V`, blocks USB back-feed). R29/R30 → `V5_SENSE` (GP28).
- **Modules (customer-fitted):** ESP32 DevKitC **30-pin** in J_U1A/J_U1B (1×15 sockets, rows 25.4 mm;
  38-pin does not fit), Waveshare RP2350-Zero in J_U2A/J_U2B (1×9) + J_U2C (1×5), DFPlayer Mini in U3,
  PS1/PS2 soldered into 5-pin rows (EN VIN GND GND OUT; EN open = on), MAX9744 module on J_AMP.
- **Connectors:** J2 DRIVE(A)+S2S(B), J3 FLYWHEEL(A)+DOME(B) bare 2×5 (odd = ch A: VCC PWM INA INB GND,
  even = ch B); J4/J5 servo 1×3 (SIG +6V GND); J6 IMU JST-XH 4 (+5V GND SCL SDA, v9.15 MPU order, GY-BMI160
  over I²C-A); J7 S2S JST-XH 5 (3V3 GND OUT SDA SCL, AS5600 or pot); J8 encoder JST-XH 4 (**B A GND 5V**,
  v9.15 order); J9 hall XH-3; J10 body NeoPixel XH-3 (5V_LED GND DATA); J11 slip ring XH-2 (5V_LED GND);
  J13/J14 speakers JST-VH; J15 E-stop XH-2; J17 Qwiic JST-SH (GND 3V3 SDA SCL, RP2350 bus I²C-B).
- **Stack-up:** 4-layer 1 oz: F.Cu signal + GND pour / In1 solid GND / In2 +5V pour + signal / B.Cu signal
  + GND pour. Netclasses Default 0.15/0.25 mm, Rail 0.6, Power 1.2, Motor 0.5, Audio 1.0.
- **Mechanical:** teardrop 152 × 110 mm, Ø38 axle cut-out at the origin; 5× M3 holes H1 (−42,24)
  H2 (−42,−24) H3 (27,37) H4 (27,−37) H5 (70,0) board frame (casing STL: Y = x + 5.6, Z = y + 160.9).

## 3. Files

| Path | What |
|---|---|
| `hardware/netlist/mainboard.py` | **Single source of truth** — parts, pins, nets, netclasses, keep-outs, silk text. |
| `tools/hw/gen_kicad.py` | netlist → `.kicad_sch/.kicad_pcb/.kicad_pro` (needs `board_outline_draft.dxf` beside the output). |
| `tools/hw/sync_parts.py` | push netlist changes into a hand-placed board without moving parts. |
| `tools/hw/prep_route.py` → Freerouting (`tools/freerouting/`) → `tools/hw/post_route.py` | autoroute recipe (`--route-5v`, `-mp 16 -mt 6`, GND stitching). |
| `tools/hw/fab_outputs.ps1 compact` / `tools/hw/jlc_outputs.py` | gerbers/drill/PDFs → `hardware/fab/compact-<date>/`; JLCPCB CPL (centroids, +90° for socket/pin headers) + BOM from `hardware/bom/lcsc_map.csv`. |
| `tools/hw/cover_layout.py` | cover DXF kit (holes, tall parts, connectors, vent zones) in board + casing frames. |
| `hardware/kicad/compact/` | **the built board** (routed, DRC-clean). `archive/` holds pre-route / pre-J1 / pre-U7-fix snapshots (git-ignored). |
| `hardware/kicad/extended/` | 152 × 125 mm variant (casing +15 mm down), synced to the netlist, unrouted. |
| `hardware/fab/ZDrive_v10_compact_JLCPCB_r7.zip` | gerbers + `cpl_jlcpcb.csv` + `bom_jlcpcb.csv` + PDFs + renders (upload set). `..._GERBERS_r7.zip` = gerbers only. |
| `hardware/bom/v10_mainboard_bom.csv`, `lcsc_map.csv` | human BOM · verified LCSC C-numbers per ref. |
| `hardware/ASSEMBLY.md` | population, module fitting, bring-up order. |
| `hardware/mechanical/ENVELOPE.md`, `compact-cover/` | casing fit, heights, hole table, cover DXFs. |
| `docs/PCB_v10_Design.md` | design spec, updated with the as-built sections. |
| `docs/pcb-v10/ZDrive_v10_Wiring_Guide.html` | per-connector wiring guide (artifact). |
| `docs/pcb-v10/Charge_Path_Audit_2026-08-22.md` | why the charge path was removed. |

JLCPCB upload rules that cost time: gerbers on the KiCad page origin, **no drill-map file**; CPL headers
`Designator,Mid X,Mid Y,Layer,Rotation` with courtyard-centre coordinates; BOM `Comment,Designator,Footprint,JLCPCB Part #`
with C-numbers; re-upload under a new filename after a failed preview; in the placement preview the purple
mark is the model's pin 1 — Q1, U5, U6, U7 and D4 needed rotating in their viewer.

## 4. Bring-up order (short form — details in `hardware/ASSEMBLY.md`)

1. Bare board: U7 pin 1 = +3V3 (r6: bodge first). No shorts VIN/+5V/+3V3/+6V to GND.
2. No modules fitted, bench supply 12 V at J1 with current limit 0.5 A: VIN ≈ 12 V after Q1, PS1 → 5.0 V,
   PS2 → 6.0 V, U5 → 3.3 V, `RP_5V` ≈ 4.7 V. Reverse the supply once: nothing conducts.
3. Fit the RP2350-Zero, then the DevKit, DFPlayer, amp module. USB each module alone first.
4. Motor ribbons last, pin 1 checked; dome by hand → GP11/GP12 toggle (proves U7).

## 5. Firmware port (deferred — v9.15 in service)

Targets after the port: **drive** = ESP32 DevKitC (`ESP32_DRIVE_RC5`), **body** = RP2350-Zero
(`RP2350_BODY_RC5`, port of `32U4_DRIVE_RC4`); the Trinket IMU target retires; the dome target is unchanged
until a v10 dome board exists.

### 5.1 ESP32 DevKitC pin map (drive)

| GPIO | Socket pin | Net | Function | v9.15 |
|---|---|---|---|---|
| 36 (VP) | J_U1A-2 | `ESTOP_SENSE` | E-stop loop (J15), R28 10 k to 3V3; input-only, `INPUT`; HIGH = loop open = **stop** | none |
| 39 (VN) | J_U1A-3 | NC | spare, input-only (no charge sense) | none |
| 34 | J_U1A-4 | `S2S_POS` | S2S position, ADC1_CH6: J7 OUT → R2 1 k, C28 100 n (pot wiper or AS5600 OUT) | `S2S_POT_PIN` 34 |
| 35 | J_U1A-5 | `VBAT_SENSE` | pack volts, ADC1_CH7, ÷5.7 (47k/10k), `ADC_11db`, 12-bit | none (came from the dome) |
| 32 | J_U1A-6 | `FLY_PWM` | flywheel PWM → J3-3 | `FLYWHEEL_PIN_1` (was dir) |
| 33 | J_U1A-7 | `FLY_INA` | → J3-5 | `S2S_PWM` |
| 25 | J_U1A-8 | `S2S_PWM` | → J2-4 | `S2S_PIN_2` |
| 26 | J_U1A-9 | `S2S_INA` | → J2-6 | `S2S_PIN_1` |
| 27 | J_U1A-10 | `S2S_INB` | → J2-8 | `DRIVE_PIN_2` |
| 14 | J_U1A-11 | `FLY_INB` | → J3-7 | `FLYWHEEL_PIN_2` (same) |
| 12 | J_U1A-12 | NC | strap — leave unused | Serial2 TX to 32u4 |
| 13 | J_U1A-13 | `I2C_A_SDA` | IMU (J6-4) + AS5600 (J7-4), R5 4.7 k | Serial2 RX |
| VIN | J_U1A-15 | `+5V_LOGIC` | from PS1 (module diode-ORs with USB) | 5 V |
| 23 / 19 / 18 / 5 | J_U1B-1/6/7/8 | NC | spare (a full VSPI set) | none |
| 22 | J_U1B-2 | `LINK_RP_TX` | UART2 **RX** ← RP2350 GP0 | none |
| 21 | J_U1B-5 | `LINK_ESP_TX` | UART2 **TX** → RP2350 GP1 | `DRIVE_PWM` |
| 17 (TX2) | J_U1B-9 | `DRIVE_INB` | → J2-7 — plain GPIO, **do not start UART2 on default pins** | Serial1 TX (Trinket) |
| 16 (RX2) | J_U1B-10 | `DRIVE_INA` | → J2-5 — plain GPIO | Serial1 RX (Trinket) |
| 4 | J_U1B-11 | `DRIVE_PWM` | → J2-3 | `DRIVE_PIN_1` (was dir) |
| 2 | J_U1B-12 | NC | module LED only (`HEARTBEAT_LED` still 2) | same |
| 15 | J_U1B-13 | `I2C_A_SCL` | IMU (J6-3) + AS5600 (J7-5), R6 4.7 k (keeps the strap high) | `FLYWHEEL_PWM` |
| 1 / 3 | J_U1B-3/4 | NC | USB console via the module's CP2102 | same |

All 12 driver inputs have 10 k pull-downs (R12–R23). Only GPIO14 and 34 keep their v9.15 roles.

### 5.2 RP2350-Zero pin map (body)

| GP | Net | Function | v9.15 (32u4) |
|---|---|---|---|
| 0 | `LINK_RP_TX` | UART0 TX → ESP32 GPIO22 (`Serial1`, default pins) | D1 → ESP32 13 |
| 1 | `LINK_ESP_TX` | UART0 RX ← ESP32 GPIO21 | D0 ← ESP32 12 |
| 2 | `AMP_SHDN` | MAX9744 SHDN (J_AMP-8): LOW at boot (mute), HIGH to enable | none |
| 3 | NC | spare | — |
| 4 | `DF_TX_RP` | UART1 TX → R1 1 k → DFPlayer RX (`Serial2.setTX(4)`) | A4 SoftwareSerial |
| 5 | `DF_RX_RP` | UART1 RX ← DFPlayer TX (`Serial2.setRX(5)`) | 9 SoftwareSerial |
| 6 | `SERVO_L` | left dome-tilt servo (J4-1) | 12 |
| 7 | `SERVO_R` | right dome-tilt servo (J5-1) | 11 |
| 8 | `DOME_PWM` | dome spin PWM → J3-4 | 3 |
| 9 | `DOME_INA` | → J3-6 | 5 |
| 10 | `DOME_INB` | → J3-8 | 6 |
| 11 | `ENC_A_3V3` | encoder A (J8-2 via U7) — interrupt or PIO quadrature, no pull-up | 2 (INT) |
| 12 | `ENC_B_3V3` | encoder B (J8-1 via U7) | A0 |
| 13 | `HALL` | hall (J9-3), R9 10 k pull-up — `INPUT` (never rely on internal pull-downs, erratum E9) | 20 |
| 14 | `NEO_3V3` | body NeoPixel data → U6 74AHCT125 → R3 330 Ω → J10-3 | none |
| 15 | `DF_BUSY` | DFPlayer BUSY (LOW = playing), `INPUT` | 10 |
| 26 | `I2C_B_SDA` | `Wire1`: MAX9744 0x4B (J_AMP-6) + J17 Qwiic, R7 4.7 k | none |
| 27 | `I2C_B_SCL` | `Wire1` (J_AMP-7, J17), R8 4.7 k | none |
| 28 | `V5_SENSE` | +5V_LOGIC ÷2 (R29/R30), ADC2 | none |
| 29 | NC | spare (ADC3) | — |
| 5V | `RP_5V` | +5V_LOGIC via D2 SS14, C12 100 n | USB/5V |

### 5.3 Changes, drive (ESP32_DRIVE_RC4 → RC5)

1. **Motor pins:** `DRIVE_PWM 21→4`, `DRIVE_PIN_1 (INA) 4→16`, `DRIVE_PIN_2 (INB) 27→17`; `S2S_PWM 33→25`,
   `S2S_PIN_1 26→26`, `S2S_PIN_2 25→27`; `FLYWHEEL_PWM 15→32`, `FLYWHEEL_PIN_1 32→33`, `FLYWHEEL_PIN_2 14→14`.
   Re-verify INA/INB polarity on the bench (`REVERSE_*` flags exist).
2. **Body link:** `Serial2.begin(74880, SERIAL_8N1, 13, 12)` → `Serial2.begin(<baud>, SERIAL_8N1, 22 /*RX*/, 21 /*TX*/)`.
   Both ends are hardware UARTs now — drop the 74880 SoftwareSerial compromise (921600 proposed).
3. **Remove the Trinket link:** delete `Serial1.begin(115200)`, `ComsTrinket`, `receiveFromTrinket()` and the
   CRC_ERROR path. Never call `Serial1.begin()` on the DevKit — its default pins are the flash pins and 16/17 are now motor pins.
4. **IMU module:** `Wire.begin(13, 15, 400000)`; BMI160 at 0x68 or 0x69 (probe both — J6 leaves SA0 open).
   Read gyro+accel on the 100 Hz tick (or a timer), gyro-dominant complementary/Mahony filter, fill the existing
   `struct_messagempu mpudata` (rawX/Y/Z, pitch, roll) so `CalibrationModule.h`, `runControl()`,
   `updateSendTiltValues()` and the printers stay untouched; keep `imuHasSample`/`lastIMUUpdate` feeding the
   `IMU_STALE_MS` guard. Port `GYRO_PITCH_SIGN`/`GYRO_ROLL_SIGN` + axis pairing into a mounting-orientation setting.
5. **AS5600 on I²C-A (0x36):** keep `analogRead(34)` for control; add magnet-status health to `cfg show` and a
   cutoff on magnet loss; one-time ZPOS/MPOS programming so the 92° swing spans 0–3.3 V; rescale
   `POT_FULL_SWING_COUNTS`/`s2sInnerKp` if counts go ~1000 → ~4000; `analogSetPinAttenuation(34, ADC_11db)`.
6. **E-stop GPIO36:** `pinMode(36, INPUT)`; HIGH → brake drive/S2S/flywheel, `driveEnabled=false`, reset PIDs,
   send `driveEnabled=false`/`DomeSpin=0` to the body; report `[SAFETY] ESTOP open` + telemetry. Poll in the tick.
7. **Battery GPIO35:** `Vbat = adc × (3.3/4095) × 5.7` (× cfg calibration); telemetry + `cfg show`; warn 10.2 V,
   force-disable 9.6 V (3S policy — firmware choice).
8. **Spare/strap:** never drive GPIO12; GPIO2 = LED only; free: 5, 18, 19, 23, 39.

### 5.4 Changes, body (32U4_DRIVE_RC4 → RP2350_BODY_RC5, arduino-pico)

1. Keep the `SerialTransfer` structs `RecFromESP32`/`SendToESP32` bit-identical (packed), the 50 Hz sound-sequence
   latch, the command set (`tilt …`, `audio …`, `telemetry on|off`, `version`) and the banner format
   (`Joe Drive Rev 1.0 RC5 DRIVE`) so `bb8` and the Runbook stay valid. Link = `Serial1` (GP0/GP1).
   `EEPROM.begin(256)` … `EEPROM.commit()` after `put`.
2. **Dome:** `domeMotor_pwm 3→8`, `pin_A 5→9`, `pin_B 6→10` (`analogWriteFreq(20000)`); encoder `2→11`, `A0→12`
   (interrupts on CHANGE or PIO quadrature, no pull-ups); hall `20→13`, `INPUT`.
3. **Servos:** `12→6`, `11→7` with the arduino-pico `Servo` (PIO) + `writeMicroseconds()`; keep `degToUs()`, easing,
   70/110 centres; rail is 6.0 V — re-check end stops (40–100 / 80–140) on the jumbo servos.
4. **DFPlayer:** `SoftwareSerial(9, 22)` → `Serial2.setTX(4); Serial2.setRX(5); Serial2.begin(9600)`;
   `mp3.begin(Serial2, …)` with ACK re-enabled; `DFPLAYER_BUSY_PIN 10→15`. The RC4.1 5× repeat hack can stay.
5. **Amp (new):** `Wire1.setSDA(26); Wire1.setSCL(27); Wire1.begin()`; MAX9744 at 0x4B, volume 0–63; GP2 LOW at
   boot, HIGH ~200 ms after the DFPlayer is up; map `vol <0-30>`, pad codes 125/126 and `silentMode` 127 onto the amp
   (DFPlayer fixed high); `audio status` reports amp present/muted.
6. **NeoPixel + rail sense (new):** GP14 WS2812 channel (count = setting); GP28 ×2 → body telemetry.
7. Spare: GP3, GP29; J17 Qwiic shares `Wire1` with the amp; GP16 = module status LED.

### 5.5 `bb8` Commander / `targets.json`

- **drive:** fqbn `esp32-bluepad32:esp32:featheresp32` → `esp32-bluepad32:esp32:esp32`; description
  "ESP32 DevKitC drive master"; VID 10C4 (CP2102) unchanged.
- **body:** sketch `32U4_DRIVE_RC4` → `RP2350_BODY_RC5`; fqbn `adafruit:avr:feather32u4` →
  `rp2040:rp2040:waveshare_rp2350_zero`; usbVid `239A` → `2E8A` (PID once the module is plugged in);
  banner/revMatch → the RC5 string.
- **imu:** remove the target; drop the `imu` key from `versions.json` / BuildStamp generation.
- `Program.cs` `IsNativeUsb()` tests only `:avr:` / `:samd:` — add `:rp2040:` so the 1200-baud touch / banner-verify
  path is used for the Zero.
- **dome:** unchanged (HUZZAH32).

## 6. Runbook

`docs/Runbook.md` is the **v9.15** operating document and is unchanged. v10 operating notes live here until the
firmware port lands; the command set is kept identical by design (§5.4 item 1), so the Runbook's flashing and
console procedures carry over with only the target/board names changing.

## 7. Later

- Rev B: on-board MAX9744 QFN; shrouded/keyed J2/J3; reset button + test points; hardware `MOTOR_EN` gate if wanted.
- Route the extended variant if the compact proves too tight in the casing.
- Casing: vents over PS1/PS2/amp/Q1, XT60 window, low-profile fuse (`hardware/mechanical/compact-cover/`).
