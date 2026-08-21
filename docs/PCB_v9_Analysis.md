# Z-Class / Joe's Drive v2 PCB Analysis (for v10.0 redesign)

Source: GitHub `jlvandusen/Z-ClassDriveSystem`, folder `PCB/` (three zips) plus root-level
`JoeDriveV2_v9.15.pdf`, `ESP32 Proposed Solution v76.pdf`, `Z-Drive BOM.xlsx`, `README.md`.
Working copy: `C:\Users\james\AppData\Local\Temp\claude\c--Users-james-BB8\fcc8dd6d-b06f-4f7e-8666-be29fc51427c\scratchpad\pcb\`

Method note: the zips contain **only fabrication outputs** (Gerber RS-274X, Excellon drill, a
Fusion "partlist" text and a pick-and-place text). There is **no schematic, no netlist, no
.sch/.brd/.kicad_* source**. Every net below was reconstructed by parsing the copper Gerbers
(flashes + strokes, union-find on geometric overlap, through-hole pads joined across layers),
then naming pads from the PnP centroids, drill table, silkscreen labels and the known Adafruit
Feather / DFPlayer / BOB-12009 footprints. Scripts: `nets.py`, `nets2.py`, `map_main.py`,
`map_dome.py`, `map_imu.py` in the working dir; renders `render_*_{top,bottom}.png`.
The reconstruction is self-consistent (every silkscreen label, e.g. "POT GND 3V", "B A GND VCC",
lands on the expected Feather pin), so I have high confidence in it, but it is derived, not
authoritative.

---

## 0. Repository inventory (relevant to electronics)

| Path | Size | What |
|---|---|---|
| `PCB/BB8 Mainboard v9.15_2025-12-30.zip` | 298,636 B | Mainboard CAM outputs (Fusion Electronics 9.7.0, Eagle engine) |
| `PCB/bb8 DOME v8.2_2024-05-05.zip` | 116,740 B | Dome board CAM outputs |
| `PCB/bb8 IMU v8.2_2024-05-05.zip` | 58,317 B | IMU board CAM outputs |
| `JoeDriveV2_v9.15.pdf` | 53 pp, Word | Operator/build guide; p.22 has a render of mainboard **v9.1**; p.17-18 give DFPlayer/NeoPixel wiring text |
| `ESP32 Proposed Solution v76.pdf` | 17 pp, Visio, Jan 2022 | v7.x wiring/pin diagrams (6 options); historical |
| `Z-Drive BOM.xlsx` | 1 sheet | Full mechanical + electrical BOM (see section 5) |
| `Z-Class System v2.png` | | Mechanical CAD render only, no electrical info |
| `README.md` | | Architecture blurb; claims "buck converters built right in" (not true of v9.15, see below) |
| `SourceFiles/*` | | Firmware (ESP32_Primary_v9_15, 32u4_Secondary_v9_15, ESP32_Dome_v9.15, TrinketM0_IMU_v9.15) |

### Zip contents (identical structure for all three)

```
.../CAMOutputs/Assembly/<board>.txt           Fusion partlist (ref, value, device, package)
.../CAMOutputs/Assembly/PnP_<board>_front.txt PnP (mainboard only; dome/IMU PnP files are 0 bytes)
.../CAMOutputs/DrillFiles/drill_1_16.xln      Excellon, metric
.../CAMOutputs/GerberFiles/copper_top.gbr / copper_bottom.gbr
.../CAMOutputs/GerberFiles/soldermask_top/bottom.gbr, solderpaste_top/bottom.gbr (paste files are empty, all THT)
.../CAMOutputs/GerberFiles/silkscreen_top/bottom.gbr
.../CAMOutputs/GerberFiles/profile.gbr        board outline
.../CAMOutputs/GerberFiles/gerber_job.gbrjob  layer count, size, thickness
```

| Board | gbrjob ProjectId | Layers | Size (mm) | Thickness | Created |
|---|---|---|---|---|---|
| Mainboard | "BB8 Mainboard v9.1 v21" (partlist says v9.1 v24) | 2 | 116.51 x 63.49 | 1.66 | 2025-12-30 |
| Dome | "bb8 v2 dome PCB v20" (partlist: v8.2 dome PCB v24) | 2 | 29.19 x 87.3 | 1.57 | 2024-05-05 |
| IMU | "bb8 v2 MPU PCB v61" (partlist: v8.2 IMU PCB v64) | 2 | 58.73 x 16.73 | 1.57 | 2024-05-05 |

**Not recoverable from the files:** schematic symbols/net names, any design-rule data, component
tolerances/voltage ratings beyond what the partlist says, the DFR0601 motor driver's internal
schematic, what is wired *off-board* (Pololu regulators, battery, motor drivers, slip ring).
**Recoverable:** full connectivity, footprints, trace widths, drill sizes, outline, silkscreen.

---

## 1. Mainboard v9.15 ("BB8 Mainboard v9.15", bottom silk: "Joe's Drive v2 main board / last updated 12.9.2025 / Design by James VanDusen / Special thanks to Mimir Reynisson, Greg Bellows and Joe Latiola")

### 1.1 Partlist (from `BB8 Mainboard v9.1 v24.txt`, 12/30/2025)

| Qty | Ref(s) | Value / Device | Package | Notes |
|---|---|---|---|---|
| 2 | `32U4`, `ESP32` | ADAFRUIT_FEATHER | ADAFRUIT_FEATHER (12+16 THT, 0.1") | Socketed Feathers (BOM: Feather 32u4 RFM69 *or* Feather Proto M0*, and HUZZAH32) |
| 1 | `U$3` | DFPLAYER | DFPLAYER 2x8 THT | DFPlayer Mini |
| 1 | `U1` | BOB-12009 | CONV_BOB-12009 2x6 THT | SparkFun BSS138 4-ch level shifter |
| 1 | `R1` | 1k Ohm | axial 7.2 mm | series into DFPlayer RX |
| 1 | `R2` | 330 Ohm | axial 7.2 mm | series into NeoPixel data |
| 2 | `R3`, `R4` | 20k Ohm | axial 7.2 mm | BUSY divider |
| 1 | `1000_UF_CAP` | 1mF "1000UF-RADIAL-5MM-25V-20%" | CPOL radial 5 mm pitch, 10 mm dia | bulk on +5V (partlist description wrongly says "ceramic") |
| 3 | `5V_IN`, `6V_IN`, `AUDIO_OUT` | 2-pole 5 mm screw terminal | TERMINAL_BLOCK_2P_5 | |
| 1 | `VCCSENSOR` | JST-XH-2P | | |
| 4 | `AUDIO_AUX`, `HALL`, `NEOPIX`, `S2SPOT` | JST-XH-3P | | |
| 3 | `DOME_ENC`, `ESP32I2C`, `MPU` | JST-XH-4P | | |
| 1 | `SERVOS(L/R)` | PINHD-2X3 | 2x3 0.1" | |
| 2 | `MOTORDRIVER1`, `MOTORDRIVER2` | PINHD-2X5 | 2x5 0.1" | ribbon to DFR0601 |

\*Silkscreen reads "FEATHER 32u4 M0 / FEATHER 32u4 RF". Note v9.1 (PDF p.22 render) had R1-R8
including four 10k resistors; v9.15 dropped the 10k parts and renumbered to R1-R4. The PDF's
"Building the Main Board" list (p.21: 10k x4, 1x5 headers x4 etc.) is therefore **stale vs v9.15**.

Board: 116.51 x 63.49 mm, 2-layer, **no copper pours**, 4x 3.0 mm mounting holes at
(0.24, 3.10), (0.24, 60.25), (110.73, 3.10), (110.73, 60.25) mm (outline origin at x=-2.54).
Drills: 4x 3.0, 6x 1.4 (terminal slots), 94x 1.016, 26x 1.0, 2x 0.9, 16x 0.8, 8x 0.66, **4x 0.35 (vias; only 4 vias on the whole board)**.

### 1.2 Block diagram (words)

```
 5V_IN (from external Pololu D36V28F5, 5 V 3.2 A) ──┬── ESP32 Feather "USB" pin ── on-board 3.3 V LDO ─┐
                                                     ├── 32u4 Feather "USB" pin ── on-board 3.3 V LDO ─┤
                                                     ├── DFPlayer VCC                                   │
                                                     ├── NEOPIX 5V (body LEDs)                          │
                                                     ├── DOME_ENC VCC (NeveRest encoder)                │
                                                     ├── MPU/IMU VCC (to Trinket BAT pin)               │
                                                     ├── BOB-12009 HV                                   │
                                                     └── 1000 uF bulk                                   │
 6V_IN (from external Pololu D36V28F6, 6 V 2.7 A) ──── SERVOS(L/R) centre pins only                     │
                                                                                                         │
 3V3_ESP32 (ESP32 LDO) ── S2SPOT 3V, ESP32I2C VCC, MOTORDRIVER1 row-S2S VCC, MOTORDRIVER2 both VCC ◄────┤
 3V3_32U4  (32u4 LDO)  ── HALL 3V, BOB-12009 LV, MOTORDRIVER1 row-DOME VCC ◄────────────────────────────┘

 ESP32 ── IO33/26/25 ─► MOTORDRIVER1 row A (S2S)          ESP32 ── IO16/17 ◄─► MPU JST (Trinket IMU, 115200)
 ESP32 ── IO21/4/27  ─► MOTORDRIVER2 row A (DRIVE)         ESP32 ── IO13/12 ◄─► 32u4 D1/D0 (74880)
 ESP32 ── IO15/32/14 ─► MOTORDRIVER2 row B (FLYWHEEL/"GYRO") ESP32 ── IO22/23 ─► ESP32I2C JST (no pullups)
 ESP32 ── IO34 ◄── S2SPOT wiper ; IO39 ◄── VCCSENSOR SIG (no divider on board)
 32u4  ── D3/D5/D6 ─► MOTORDRIVER1 row B (DOME)            32u4 ── D12/D11 ─► SERVOS L/R
 32u4  ── D2 ◄─ BOB ch2 ◄─ ENC A ; A0 ◄─ BOB ch3 ◄─ ENC B  32u4 ── A2(D20) ◄── HALL SIG
 32u4  ── D13 ─► BOB ch1 ─► 330R ─► NEOPIX SIG             32u4 ── A4(D22) ─► 1k ─► DFPlayer RX
 32u4  ── D9  ◄─ BOB ch4 ◄─ DFPlayer TX                    32u4 ── D10 ◄─ 20k ◄─ DFPlayer BUSY, 20k ─► GND
 DFPlayer SPK1/SPK2 ─► AUDIO_OUT terminal ; DAC_L/DAC_R ─► AUDIO_AUX JST (L, GND, R)
```

### 1.3 Complete reconstructed netlist

Feather pin naming: 16-pin row (x = 53.62 mm for 32u4, 78.74 mm for ESP32) runs from the USB
end: RST, 3V, AREF/NC, GND, A0..A5, SCK, MOSI, MISO, RX, TX, free/IO21. 12-pin row (x = 33.30 /
58.42): BAT, EN, USB, 13, 12, 11/27, 10/33, 9/15, 6/32, 5/14, SCL, SDA.

| Net (my name) | Pins |
|---|---|
| **GND** (23) | 32U4.GND, ESP32.GND, DFPLAYER.7, BOB.GND(HV), BOB.GND(LV), ESP32I2C.GND, NEOPIX.GND, DOME_ENC.GND, MPU.GND, HALL.GND, S2SPOT.GND, AUDIO_AUX.GND, VCCSENSOR.GND, 5V_IN.2, 6V_IN.2, MOTORDRIVER1 pins 5 (both rows), MOTORDRIVER2 pins 5 (both rows), SERVOS R.GND, SERVOS L.GND, R3.b, C1.- |
| **+5V** (9) | 5V_IN.1, 32U4.USB, ESP32.USB, DFPLAYER.1 VCC, BOB.HV, NEOPIX.5V, DOME_ENC.VCC, MPU.VCC, C1.+ |
| **+6V** (3) | 6V_IN.1, SERVOS R.6V, SERVOS L.6V |
| **3V3_ESP32** (6) | ESP32.3V, ESP32I2C.VCC, S2SPOT.3V, MOTORDRIVER1.rowA.VCC, MOTORDRIVER2.rowA.VCC, MOTORDRIVER2.rowB.VCC |
| **3V3_32U4** (4) | 32U4.3V, BOB.LV, HALL.3V, MOTORDRIVER1.rowB.VCC |
| S2S_PWM | ESP32.IO33 — MOTORDRIVER1.rowA.PWM |
| S2S_IN1 | ESP32.IO26(A0) — MOTORDRIVER1.rowA.INA |
| S2S_IN2 | ESP32.IO25(A1) — MOTORDRIVER1.rowA.INB |
| DOME_PWM | 32U4.D3(SCL) — MOTORDRIVER1.rowB.PWM |
| DOME_INA | 32U4.D5 — MOTORDRIVER1.rowB.INA |
| DOME_INB | 32U4.D6 — MOTORDRIVER1.rowB.INB |
| DRIVE_PWM | ESP32.IO21 — MOTORDRIVER2.rowA.PWM |
| DRIVE_IN1 | ESP32.IO4(A5) — MOTORDRIVER2.rowA.INA |
| DRIVE_IN2 | ESP32.IO27 — MOTORDRIVER2.rowA.INB |
| FLY_PWM | ESP32.IO15 — MOTORDRIVER2.rowB.PWM |
| FLY_IN1 | ESP32.IO32 — MOTORDRIVER2.rowB.INA |
| FLY_IN2 | ESP32.IO14 — MOTORDRIVER2.rowB.INB |
| S2S_POT | ESP32.IO34(A2) — S2SPOT.POT |
| VCC_SENSE | ESP32.IO39(A3) — VCCSENSOR.SIG |
| ESP_RX1 | ESP32.IO16(RX) — MPU.TX |
| ESP_TX1 | ESP32.IO17(TX) — MPU.RX |
| ESP_RX2 | ESP32.IO13 — 32U4.TX(D1) |
| ESP_TX2 | ESP32.IO12 — 32U4.RX(D0) |
| ESP_SCL | ESP32.IO22 — ESP32I2C.SCL |
| ESP_SDA | ESP32.IO23 — ESP32I2C.SDA |
| SERVO_L | 32U4.D12 — SERVOS.LEFT.SIG |
| SERVO_R | 32U4.D11 — SERVOS.RIGHT.SIG |
| HALL_SIG | 32U4.A2(D20) — HALL.SIG |
| NEO_LV | 32U4.D13 — BOB.LV1 |
| NEO_HV | BOB.HV1 — R2.a |
| NEO_OUT | R2.b — NEOPIX.SIG (+1 via) |
| ENC_A_LV | 32U4.D2(SDA) — BOB.LV2 (+1 via) |
| ENC_A_HV | BOB.HV2 — DOME_ENC.A (+1 via) |
| ENC_B_LV | 32U4.A0 — BOB.LV3 |
| ENC_B_HV | BOB.HV3 — DOME_ENC.B (+1 via) |
| DF_TX_LV | 32U4.D9 — BOB.LV4 |
| DF_TX_HV | DFPLAYER.3 TX — BOB.HV4 |
| DF_RX_MCU | 32U4.A4(D22) — R1.a |
| DF_RX | R1.b — DFPLAYER.2 RX |
| DF_BUSY | DFPLAYER.16 BUSY — R4.a |
| BUSY_DIV | R4.b — R3.a — 32U4.D10 |
| SPK1 / SPK2 | DFPLAYER.8 SPK_1 — AUDIO_OUT.left pad ; DFPLAYER.6 SPK_2 — AUDIO_OUT.right pad (silk "1") |
| AUX_L / AUX_R | DFPLAYER.5 DAC_L — AUDIO_AUX.L ; DFPLAYER.4 DAC_R — AUDIO_AUX.R |
| **Unconnected** | 32U4: RST, AREF, A1, A3, A5(D23), SCK, MOSI, MISO, free, BAT, EN. ESP32: RST, NC, IO36(A4), IO5, IO18, IO19, BAT, EN. DFPlayer: 9 IO_1, 10 GND(!), 11 IO_2, 12/13 ADKEY, 14/15 USB+/-. |

### 1.4 Connector pinouts (silkscreen order, left to right as viewed on top render)

| Connector | Type | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Net voltage |
|---|---|---|---|---|---|---|
| `5V_IN` | 5 mm screw, 2P | **+5V** (silk "1", top) | GND | | | 5 V in from external buck |
| `6V_IN` | 5 mm screw, 2P | **+6V** (silk "1", top) | GND | | | 6 V in from external buck |
| `AUDIO_OUT` | 5 mm screw, 2P | SPK_1 | SPK_2 (silk "1") | | | DFPlayer bridged speaker out (~3 W) |
| `AUDIO_AUX` | JST-XH 3P | L (DAC_L) | GND | R (DAC_R) | | line level |
| `ESP32I2C` | JST-XH 4P | GND | VCC = 3V3_ESP32 | SDA = IO23 | SCL = IO22 | 3.3 V, **no pull-ups** |
| `NEOPIX` | JST-XH 3P | 5V | GND | SIG (5 V via BOB ch1 + 330R) | | 5 V data |
| `DOME_ENC` | JST-XH 4P | B (→BOB HV3) | A (→BOB HV2) | GND | VCC = +5V | 5 V encoder, shifted to 3.3 V |
| `IMU \| MPU` | JST-XH 4P | VCC = +5V | GND | TX (IMU→ESP32 IO16) | RX (ESP32 IO17→IMU) | 3.3 V UART |
| `HALL` | JST-XH 3P | 3V = 3V3_32U4 | GND | SIG → 32u4 A2/D20 | | 3.3 V |
| `S2S POT` | JST-XH 3P | POT → ESP32 IO34 | GND | 3V = 3V3_ESP32 | | 3.3 V analog |
| `VCC Sensor` | JST-XH 2P | GND | SIG → ESP32 IO39 | | | raw to ADC, **no divider/clamp** |
| `SERVOS(L/R)` | 2x3 0.1" | col1 SIG | col2 6V | col3 GND | | RIGHT row (top, y=10.16) = D11, LEFT row (y=7.62) = D12; standard S-V-G servo order |
| `MOTORDRIVER1` | 2x5 0.1" | col1 VCC | col2 PWM | col3 INA | col4 INB | col5 GND. **Row A (top, y=25.4) = S2S: 3V3_ESP32, IO33, IO26, IO25, GND. Row B (y=22.86) = DOME: 3V3_32U4, D3, D5, D6, GND** |
| `MOTORDRIVER2` | 2x5 0.1" | same order | | | | **Row A (y=17.78) = DRIVE: 3V3_ESP32, IO21, IO4, IO27, GND. Row B (y=15.24) = FLYWHEEL ("GYRO"): 3V3_ESP32, IO15, IO32, IO14, GND** |

The 2x5 header order (VCC, PWM, INA, INB, GND per channel) matches the documented DFR0601
header (VCC, PWM1, INA1, INB1, GND / VCC, PWM2, INA2, INB2, GND), so one 10-way ribbon per
DFR0601 carries both channels. Physical pin order on the DFR0601 should be verified on the
bench; I could only confirm the documented list.

### 1.5 Power architecture

* **No regulators on the PCB.** Despite the README ("buck converters built right in ... 48V in
  and supports 5v, 6v and 9v outputs") and the v7 Visio diagrams, v9.15 has only two input
  terminal blocks. The BOM lists off-board **Pololu D36V28F5 (5 V, 3.2 A), D36V28F6 (6 V, 2.7 A),
  D36V28F9 (9 V, 2.6 A)** and a 24 V 28 Ah e-bike Li-ion pack (12 V options also listed). The 9 V
  module has no PCB connection (presumably the external audio amp).
* **+5V rail** powers: both Feathers via their *USB* pins (i.e. direct VBUS injection), DFPlayer,
  body NeoPixels (NEOPIX 5V), NeveRest encoder, IMU board (into the Trinket's BAT pin), and the
  level shifter HV side. Bulk: one 1000 uF/25 V radial. No other decoupling on the board.
* **+6V rail** only goes to the two servo connectors.
* **Two separate 3.3 V rails** exist: 3V3_ESP32 (HUZZAH32 LDO, ~500 mA budget, most consumed by
  the ESP32 radio) and 3V3_32U4 (Feather 32u4 LDO). They are *routed* separately but the
  MOTORDRIVER1 header feeds 3V3_ESP32 to the S2S channel VCC and 3V3_32U4 to the DOME channel VCC
  of the **same DFR0601 board**; if (as is likely) the DFR0601 ties both VCC pins together, the two
  Feather LDO outputs are paralleled through the ribbon cable.
* **Motor power (battery, 6.5-37 V per DFR0601 spec) never touches this PCB** — it goes straight
  to the DFR0601 P+/P- terminals. Good for noise; the only motor-side coupling is via the
  3.3 V logic VCC and the ribbon.
* **All traces, including GND, +5V and +6V, are 0.1524 mm (6 mil)** — see weaknesses.

### 1.6 Motor driver channels

BOM: "Motor Driver 12A" x2, dfrobot.com/product-1861 = **DFRobot DFR0601 Dual-Channel DC Motor
Driver 12A**: 6.5-37 V motor supply, 12 A continuous / 70 A (100 ms) peak per channel, 3-5 V
logic, PWM + INA + INB per channel, 50 x 50 mm. Four channels used:

| Channel | Driver board / row | MCU pins | Motor (BOM) |
|---|---|---|---|
| S2S | MOTORDRIVER1 A | ESP32 IO33 PWM, IO26 INA, IO25 INB | worm-gear motor 130 rpm |
| Dome rotate | MOTORDRIVER1 B | 32u4 D3 PWM, D5 INA, D6 INB | NeveRest Classic 60 w/ encoder |
| Main drive | MOTORDRIVER2 A | ESP32 IO21 PWM, IO4 INA, IO27 INB | ServoCity 118 rpm planetary |
| Flywheel ("GYRO") | MOTORDRIVER2 B | ESP32 IO15 PWM, IO32 INA, IO14 INB | ServoCity 1621 rpm planetary |

Firmware uses 20 kHz / 8-bit LEDC PWM on the ESP32 (`PWM_FREQ = 20000`).

### 1.7 Sensors

* S2S potentiometer on IO34 (ADC1_CH6, input-only, **no internal pull-up/down, no RC filter on the
  board**). BOM specifies a **200 k** pot — far too high a source impedance for the ESP32 SAR ADC
  (recommend <= 10 k); expect noisy, sample-rate-dependent readings.
* Hall sensor on 32u4 A2/D20 with 3V3_32U4 supply; no pull-up on board (open-collector sensors need
  INPUT_PULLUP in firmware or an external resistor).
* NeveRest quadrature encoder at 5 V, A/B through BOB-12009 channels 2/3 (BSS138 + 10 k pull-ups
  both sides) to D2 (INT) and A0. Fine at dome speeds; ~100 kHz-class bandwidth.
* "VCC Sensor" 2-pin JST: GND + SIG straight into IO39 (ADC1_CH3). BOM's "HiLetgo Voltage Sensor"
  is a 30k/7.5k (5:1) divider → a 24 V pack gives **4.8 V into IO39 (abs max 3.6 V)**. Not read by
  the RC4 drive firmware, but hazardous if anyone plugs it in.
* IMU is off-board (Trinket M0 + MPU6050) via UART.

### 1.8 Serial links and logic levels

| Link | Pins | Baud (firmware) | Levels |
|---|---|---|---|
| ESP32 Serial2 ↔ 32u4 Serial1 | ESP32 IO13 (RX) ← 32u4 D1 TX; ESP32 IO12 (TX) → 32u4 D0 RX | 74880 | 3.3 V both sides (Feather 32u4 is 3.3 V/8 MHz) — OK |
| ESP32 Serial1 ↔ Trinket M0 Serial1 | ESP32 IO16 RX ← IMU TX; IO17 TX → IMU RX (straight 1:1 JST cable to IMU board) | 115200 | 3.3 V both — OK. HUZZAH32 variant defines RX1=16/TX1=17 so `Serial1.begin(115200)` matches the PCB |
| 32u4 SoftwareSerial ↔ DFPlayer | 32u4 A4/D22 TX → 1 k → DF RX; DF TX → BOB HV4/LV4 → 32u4 D9 RX | 9600 | DFPlayer I/O is 3.3 V logic; the level shifter on TX is harmless but unnecessary |
| ESP32 I2C JST | IO22/IO23 | — | 3.3 V, no pull-ups |

### 1.9 Cross-check against firmware pin assignments

| Firmware | PCB | Result |
|---|---|---|
| ESP32 S2S_PWM=33, S2S_PIN_1=26, S2S_PIN_2=25 | MOTORDRIVER1 row A PWM/INA/INB | **match** |
| DRIVE_PWM=21, DRIVE_PIN_1=4, DRIVE_PIN_2=27 | MOTORDRIVER2 row A | **match** |
| FLYWHEEL_PWM=15, FLYWHEEL_PIN_1=32, FLYWHEEL_PIN_2=14 | MOTORDRIVER2 row B | **match** |
| S2S_POT=34 | S2SPOT.POT | **match** |
| Serial2 RX=13 TX=12 (74880) | IO13←32u4 TX, IO12→32u4 RX | **match** |
| Serial1 to Trinket (default pins) | IO16/IO17 to MPU JST | **match** (variant default RX1/TX1 = 16/17) |
| 32u4 servos 12 / 11 | D12 = LEFT SIG, D11 = RIGHT SIG | **match** |
| 32u4 dome motor PWM 3, dirA 5, dirB 6 | MOTORDRIVER1 row B | **match** |
| 32u4 encoder A = 2 (INT), B = A0 | BOB ch2 → D2, ch3 → A0 | **match** |
| 32u4 hall = 20 | A2 = D20 on the 32u4 Feather | **match** |
| 32u4 DFPlayer RX 9, TX 22, BUSY 10 | D9 ← DF TX (via shifter); **D22 = A4** → 1 k → DF RX; D10 ← BUSY divider | **match** — pin 22 is not odd: on the Feather 32u4, A4 is digital 22 (A0=18 ... A5=23). The PCB really does route A4. |
| Trinket M0 MPU6050 on I2C, Serial1 to ESP32 | see IMU board | **match** |
| Dome NeoPixels 25, 27, 33, 15, 32 | PSI=25, SLOGIC=27, LLOGIC=33, HP=15, EYE=32 | **match** |
| Dome battery ADC A13/GPIO35, wake GPIO35 | not on any PCB net (HUZZAH32 on-board VBAT/2 divider) | n/a — but GPIO35 is permanently at VBAT/2 ≈ 1.9-2.1 V, so an ext0 wake on GPIO35 HIGH fires immediately; wake source is a firmware concern, not a PCB one |

**No discrepancies found between PCB routing and firmware pin numbers.** Discrepancies that do
exist are documentation-level: README claims on-board bucks; PDF p.17 says "D9 RX via 20k/10k
divider from DF TX" (v9.15 actually uses the BOB-12009 ch4 for DF TX and a 20k/20k divider on
BUSY); PDF p.21 parts list is v9.1.

### 1.10 Observed design weaknesses (mainboard)

1. **6-mil (0.1524 mm) traces everywhere, no pours, only 4 vias.** GND (~330 mm of 6-mil trace),
   +5V (~200 mm) and +6V (~112 mm, bottom layer) are all 6 mil. IPC-2221 external 1 oz 6 mil ≈
   0.5 A for 10 °C rise. The +6V trace to two JX PDI-HV2060MG 62 kg-cm servos (stall currents of
   several amps each) is ~0.35 Ω: at 2 A that is 0.7 V drop and 1.4 W in the trace — it will
   brown out the servos or lift the trace. The +5V trace carries two Feathers (ESP32 with BT
   ~250-500 mA peaks), DFPlayer + speaker (~1 A peaks), body NeoPixels, encoder and IMU board.
   GND return for all of that is another 6-mil trace. This is the single most important fix for
   v10: ground pour both sides, >= 1.5 mm (60 mil) power traces or pours, stitching vias.
2. **5 V injected on the Feathers' USB pins.** Those pins are VBUS; plugging a PC into either
   Feather while 5V_IN is live parallels the PC's 5 V with the Pololu output (no diode on the
   PCB). Relevant because the tuning tool works over the USB ports. Need Schottky/ideal-diode OR-ing
   or feed the Feathers' BAT/VIN path instead.
3. **Two 3.3 V LDO outputs (ESP32 Feather and 32u4 Feather) are almost certainly shorted together
   through the DFR0601 VCC pins** on MOTORDRIVER1 (row A VCC = 3V3_ESP32, row B VCC = 3V3_32U4).
   Use one 3.3 V domain (or a proper on-board 3.3 V regulator with headroom) in v10.
4. **DFPlayer BUSY divider is marginal.** BUSY → 20 k → D10 → 20 k → GND, and firmware sets
   `INPUT_PULLUP` on D10. DFPlayer BUSY is 3.3 V logic, so the divider yields ~1.65 V (≈2.0 V
   with the ~35 k internal pull-up) against a VIH of 0.6 x 3.3 = 1.98 V. HIGH detection is on
   the threshold; the divider is only needed for 5 V MCUs and should be removed.
5. **BSS138 shifter for NeoPixel data.** Adafruit explicitly warns that 10 k-pull-up auto-direction
   shifters (BOB-12009) are too slow for 800 kHz WS2812 data; use a 74AHCT125/74HCT245 or drive
   the first pixel from 3.3 V with a 5 V-tolerant scheme. The DF TX channel (ch4) is unnecessary
   (DF TX is already 3.3 V), and enc A/B channels are fine.
6. **SoftwareSerial on the 32u4 for the DFPlayer** (D9 RX, D22 TX) while the same MCU bit-bangs
   NeoPixels (interrupts off during `show()`), services an encoder ISR on D2 and runs two servos
   on Timer1 — a known recipe for dropped DF commands. v10: use an MCU with a spare hardware UART
   (the ESP32 has UART0 free in deployment, or pick an MCU with 3+ UARTs), or use a UART-free
   sound board.
7. **ESP32 strapping pins used for I/O.** IO12 (MTDI, flash-voltage strap; must be LOW at boot) is
   the Serial2 TX to the 32u4's RX pin. It works only because the 32u4 RX is high-Z at ESP32 boot;
   any pull-up on that line (e.g. a library enabling the AVR RX pull-up) would brick boot. IO15
   (MTDO) drives the flywheel PWM. IO13 also drives the HUZZAH32's red LED, so that LED will flicker
   with 32u4 TX traffic. Avoid 0/2/12/15 for external nets in v10.
8. **S2S pot on IO34 with no RC filter and a 200 k pot.** IO34 has no internal pull-up (fine for a
   pot, but a broken wiper floats the ADC). Add 10 k pot + 1-10 k series + 100 nF; or use a
   magnetic absolute encoder.
9. **VCC Sensor input to IO39 has no divider, clamp or series resistor**; the BOM sensor module
   can present 4.8 V. Either put a proper divider + clamp on the PCB or delete the connector.
10. **ESP32I2C JST has no pull-ups**; the dome I2C header (section 2) has none either, so a slip-ring
    I2C link would rely on ESP32 internal ~45 k pull-ups only.
11. **No input protection**: no reverse-polarity, fuse, TVS or bulk on 6V_IN; one 1000 uF on 5 V and
    **zero 100 nF decoupling** anywhere. No ESD on any external JST.
12. **No hardware enable / E-stop.** Motor enable relies entirely on the MCUs. DFR0601 inputs float
    if a Feather is unpowered or in reset; add pull-downs on all PWM/INA/INB lines.
13. **Encoder and hall pull-ups are firmware-only** (`INPUT_PULLUP`); the PDF tells builders to add
    4.7-10 k externally. Put them on the board.
14. **DFPlayer pin 10 (second GND) left floating**, USB± and ADKEY unconnected (harmless but ADKEY1
    floating can false-trigger on some clones — tie to VCC via 10 k or leave; documented behaviour
    varies).
15. **Connector mix**: 5 mm screw terminals for power (OK), JST-XH for signals (no latch, 3 A rated),
    bare 0.1" male headers for servos and the 10-way motor ribbons (unkeyed, reversible — a reversed
    ribbon puts 3.3 V on GND). Use keyed/shrouded headers or JST-XH/PH with polarisation in v10.
16. **Mechanical/silk**: the partlist calls the 1000 uF part "ceramic"; the board-name in gbrjob is
    "v9.1 v21" while silk says v9.15 — revision hygiene. DFPlayer and BOB-12009 are mounted as
    modules-on-modules, taking ~30 % of the board area.
17. **Power budget concerns** (not PCB-routing per se): all 5 V loads share one 3.2 A buck and a
    single electrolytic; ESP32 BT bursts + DFPlayer amp transients + NeoPixel rail on the same
    6-mil trace will produce resets. Separate the LED/audio rail from the logic rail.

---

## 2. Dome board v8.2 ("Dome Controller v8.2")

### 2.1 Partlist (`bb8 v8.2 dome PCB v24.txt`, 5/5/2024)

| Qty | Ref(s) | Device | Package |
|---|---|---|---|
| 1 | `ESP32` | ADAFRUIT_FEATHER | 12+16 THT |
| 1 | `I2C` | JST-XH-4P | |
| 4 | `3V_I2C`, `3V_SENSOR`, `5V_I2C`, `5V_SENSOR` | PINHD-1X2 (jumper) | 1x2 |
| 5 | `EYE`, `HP`, `LLOGIC`, `PSI`, `SLOGIC` | PINHD-1X3 | 1x3, silk "+  --  SIG" |

Board 29.19 x 87.3 mm, 2 layer, 1.57 mm, chamfered corners, **no mounting holes**, no vias,
no passive components at all. PnP files are empty; positions taken from drill/silk.

### 2.2 Netlist

| Net | Pins |
|---|---|
| GND | ESP32.GND, HP.-, EYE.-, PSI.-, LLOGIC.-, SLOGIC.-, I2C.p1 |
| LED_V (selectable) | HP.+, EYE.+, PSI.+, LLOGIC.+, SLOGIC.+, 3V_SENSOR.1, 5V_SENSOR.1 |
| 3V3 | ESP32.3V, 3V_SENSOR.2, 3V_I2C.2 |
| VBAT | ESP32.BAT, 5V_SENSOR.2, 5V_I2C.2 |
| I2C_V (selectable) | I2C.p2, 5V_I2C.1, 3V_I2C.1 |
| PSI_SIG | ESP32.IO25 — PSI.SIG |
| SLOGIC_SIG | ESP32.IO27 — SLOGIC.SIG |
| LLOGIC_SIG | ESP32.IO33 — LLOGIC.SIG |
| HP_SIG | ESP32.IO15 — HP.SIG |
| EYE_SIG | ESP32.IO32 — EYE.SIG |
| SCL / SDA | ESP32.IO22 — I2C.p4 ; ESP32.IO23 — I2C.p3 |
| Unconnected | ESP32 RST, EN, **USB**, IO26, IO34, IO39, IO36, IO4, IO5, IO18, IO19, IO16, IO17, IO21, IO13, IO12, IO14 |

### 2.3 Connectors

| Connector | Pin order | Notes |
|---|---|---|
| `HP`, `EYE`, `PSI`, `LLOGIC`, `SLOGIC` | + , -- , SIG (1x3, 0.1") | "+" is a common rail selected by the *SENSOR* jumpers (confusing name): `3V_SENSOR` → 3.3 V LDO, `5V_SENSOR` → **Feather BAT pin (3.7-4.2 V LiPo, never 5 V)**. SIG is a bare 3.3 V GPIO, no series R, no cap. |
| `I2C` JST-XH 4P | GND, V(sel), SDA(IO23), SCL(IO22) | Same order as mainboard `ESP32I2C` (GND, VCC, SDA, SCL) so a straight cable works. V selected by `3V_I2C`/`5V_I2C` jumpers (again 3.3 V or VBAT). No pull-ups. |

### 2.4 Power

The dome Feather's USB pin is **not** routed; the only power nets are 3V3 and BAT. So the dome
is powered through the Feather's own micro-USB or LiPo JST (from the slip ring — not determinable
from the files), and the "5V" jumper positions actually deliver VBAT. The firmware's `battPin A13`
reads the Feather's on-board VBAT/2 divider, consistent with LiPo/BAT-pin operation.

### 2.5 Weaknesses

1. "5V" labels are wrong (BAT ≈ 3.7-4.2 V); installing both jumpers of a pair shorts 3V3 to VBAT.
   Use a proper 5 V source if 5 V NeoPixels are intended, or label honestly.
2. No bulk capacitor, no 300-500 Ω series resistor on any NeoPixel data line, no level shifting
   (acceptable only while the LED rail is <= ~4.2 V, where 3.3 V data meets 0.7 x VDD).
3. IO15 (strapping) used for HP LEDs; a NeoPixel DIN load at boot is usually fine but avoid.
4. No mounting holes at all; no test points; no I2C pull-ups; unkeyed 0.1" headers for LEDs.
5. Nothing for future sensors despite the README ("motion sensor and distance sensors"): 16 GPIOs
   unrouted.

---

## 3. IMU board v8.2 ("bb8 SHADOW IMU v8.2")

### 3.1 Partlist (`bb8 v8.2 IMU PCB v64.txt`)

| Ref | Device | Package |
|---|---|---|
| `U$1` | ADAFRUIT_MPU6050_NTH | 2x6 THT (only the bottom row is used) |
| `U$2` | ADAFRUIT_TRINKET_M0_NTH | 2x5 THT |
| `X2` | JST-XH-04 round pad | 1x4 |

Board 58.73 x 16.73 mm, 2 layer, 1.57 mm, **no mounting holes**, 1 via, traces 6 and 8 mil.

### 3.2 Netlist

| Net | Pins |
|---|---|
| GND | MPU.GND, MPU.top-row pin 6, TRINKET.GND, X2.p2 |
| 3V3 | TRINKET.3.3V — MPU pin silk-labelled "USB" (= MPU VIN) (+1 via) |
| SCL | TRINKET pin 2 ("SCL") — MPU.SCL |
| SDA | TRINKET pin 0 ("SDA") — MPU.SDA |
| INT | TRINKET pin 1 ("INT") — MPU.INT |
| IMU_TX | TRINKET pin 4 ("TX") — X2.p3 |
| IMU_RX | TRINKET pin 3 ("RX") — X2.p4 |
| VIN | TRINKET.BAT — X2.p1 |
| Unconnected | TRINKET USB, RST; MPU "3V" (3Vo), MPU top-row pins 1-5 |

`X2` pin order (p1..p4) = VIN, GND, TX, RX, which mates 1:1 with the mainboard `IMU | MPU` JST
(VCC, GND, TX, RX): mainboard "TX" = ESP32 RX (IO16) receives the Trinket's TX. Correct.

### 3.3 Power / levels

+5 V from the mainboard enters the **Trinket BAT pin** (its regulator input; 3.5-6 V allowed),
the Trinket's 3.3 V output feeds the MPU6050 breakout VIN (its own LDO then runs at ~3.0-3.1 V
on a 3.3 V input — in spec for the MPU-6050 (2.375-3.46 V) but with no headroom; the breakout's
3Vo pin is left floating). All logic 3.3 V. Trinket USB pin is unconnected, so plugging USB into
the Trinket while the droid is powered is safe (Trinket has its own diode OR-ing).

### 3.4 Weaknesses

1. Silkscreen "USB" on the MPU pin that is actually VIN at 3.3 V — misleading.
2. No mounting holes; a 58 mm board with two modules hanging off 0.1" headers is a vibration
   problem for an IMU (mechanical coupling directly affects balance PID).
3. Separate MCU + UART just to read an I2C IMU adds latency and a second firmware; v10 should put
   the IMU on the main MCU (SPI/I2C, ideally an ICM-42688/BMI270/ISM330 class part soldered to the
   main board, or a BNO085 for on-chip fusion) or keep it off-board over a differential/robust link.
4. No decoupling, no ESD on the JST, 6-mil traces.

---

## 4. System-level observations for v10.0

* **Every firmware pin assignment matches the PCB**; the redesign is free to re-pin, but the existing
  mapping is a known-good baseline. The "odd" 32u4 pin 22 is simply A4.
* Architecture today: ESP32 (BT/PS3 + balance) + 32u4 (dome, sound, LEDs, servos) + Trinket M0
  (IMU) + ESP32 (dome) = four MCUs, three UART links, one SoftwareSerial. The 32u4 exists purely
  to provide pins/UARTs and is the weakest part (8 MHz AVR, SoftwareSerial + NeoPixel + servos +
  ISR). A single ESP32-S3 (3 HW UARTs, RMT for NeoPixels, MCPWM/LEDC for 4 H-bridges, 2 x I2C,
  plenty of ADC1 channels) could absorb ESP32 + 32u4 + Trinket duties; keep classic ESP32 only if
  PS3 Bluetooth-Classic is a hard requirement (ESP32-S3 has no BT Classic).
* Motor drivers stay off-board (DFR0601 12 A) which is reasonable; keep the ribbon interface but
  add pull-downs, make it keyed, and use one logic domain.
* Power: bring the Pololu bucks onto the board or at least give the board a real 5 V/3 A input
  with reverse/fuse/TVS, pours, separate LED/audio rail, 100 nF on every module, and an ideal-diode
  between USB-programming ports and system 5 V.
* Sound: replace DFPlayer + SoftwareSerial with a hardware-UART DFPlayer/DY-SV17F or an I2S DAC +
  amp driven by the ESP32 (also removes the DAC_L/R + SPK terminal duplication).
* Add the items the docs tell builders to bolt on externally: encoder/hall pull-ups, pot RC filter,
  NeoPixel 74AHCT125 + 1000 uF + series R, voltage-sense divider, I2C pull-ups.

---

## 5. BOM (`Z-Drive BOM.xlsx`, electrical lines only; total sheet cost US$1,248.04)

| Item | Qty | Unit $ | Source |
|---|---|---|---|
| Adafruit Feather 32u4 RFM69HCW (or Feather Proto M0 alt.) | 1 | 24.95 / 19.95 | adafruit 5000 / 2771 |
| Adafruit HUZZAH32 ESP32 Feather, stacking headers | 2 | 21.95 | adafruit |
| Hall effect sensor | 1 | 2.00 | adafruit |
| MPU (MPU-6050 breakout) | 1 | 6.95 | adafruit |
| Trinket M0 | 1 | 8.95 | adafruit |
| Feather 12+16 female header set | 2 | 0.95 | adafruit 2886 |
| NeoPixel 5 mm THT LED 5-pack | 2 | 4.95 | adafruit 1938 |
| NeoPixel Jewel 7 | 1 | 5.95 | adafruit 2226 |
| NeoPixel Stick 8 RGBW WW | 2 | 7.95 | adafruit 2867 |
| 24 V 28 Ah Li-ion e-bike pack (12 V options listed, not purchased) | 1 | 100 | ebay |
| Amp | 1 | 16.99 | amazon |
| 12 V 6-way fuse block / power distribution | 1 | 13.99-15.99 | amazon B07HDZVW3P |
| Pot (200 k) | 1 | 9.40 | amazon |
| Power remote on/off controller | 1 | 23.00 | amazon |
| Servos JX PDI-HV2060MG 62 kg | 2 | 42.99 | amazon |
| Slip ring | 2 | 17.71 | amazon |
| Small buck | 1 | 11.99 | amazon B07CVBG8CT |
| Voltage sensor (HiLetgo 0-25 V divider) | 1 | 5.39 | amazon |
| Worm gear motor 130 rpm (S2S) | 1 | 26.76 | amazon |
| Transducers | 2 | 24.17 | amazon |
| 3.5 mm ground-loop isolator, 1" right-angle 3.5 mm cable | 1 ea | 11.99 / 5.99 | amazon |
| JST-XH 2P/3P/4P pre-wired sets, female pin headers, Dupont | — | ~5-9 | amazon |
| NeveRest Classic 60 gearmotor + encoder cable | 1 | 31.50 + 5 | andymark |
| **DFRobot Motor Driver 12A (DFR0601)** | 2 | 29.00 | dfrobot product-1861 |
| ServoCity 118 rpm (drive) and 1621 rpm (flywheel) planetary motors | 1 ea | 39.99 | servocity |
| SparkFun Qwiic MP3 Trigger + Qwiic-Grove cable (legacy option, not on v9.15 PCB) | 1 | 20 + 1.5 | sparkfun |
| Pololu 5 mm 2-pin screw terminals | 3 | 1.50 | pololu 2440 |
| **Pololu D36V28F5 5 V 3.2 A**, **D36V28F6 6 V 2.7 A**, **D36V28F9 9 V 2.6 A** step-downs | 1 ea | 11.95 | pololu 3782/3783/3785 |
| Pololu 2x5 and 1x5 female headers | 1 + 4 | 0.69 / 0.44 | pololu |

PCB-mounted passives per `JoeDriveV2_v9.15.pdf` p.21 (v9.1 list): 1k x1, 330 x1, 20k x2, 10k x4,
1000 uF x1 — v9.15 CAM uses only 1k, 330, 20k x2 and the 1000 uF.
