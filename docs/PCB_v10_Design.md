# Z-Class v10.0 — Board Redesign

**Scope:** Mainboard ("shadow body" v9.15 → v10.0 rev A, built). IMU: no custom board — a GY-BMI160 breakout on J6 (I²C). Dome board: not part of the mainboard netlist (status unknown).
**Inputs:** full netlist reconstruction of the v9.15 / v8.2 Gerbers (`PCB_ANALYSIS.md`), the RC4 firmware work, and two bench sessions
**Status:** mainboard rev A laid out, DRC 0 errors, JLCPCB fab package r7 (2026-08-23). Compact variant, teardrop 152 × 110 mm, 4-layer 1 oz. Sources: `hardware/netlist/mainboard.py` (single source of truth) → `hardware/kicad/compact/` (fabbed) and `hardware/kicad/extended/` (152 × 125 mm, unrouted); fab package `hardware/fab/ZDrive_v10_compact_JLCPCB_r7.zip` (gerbers + JLC CPL/BOM), working folder `hardware/fab/compact-20260823/`. **Confirm which revision was ordered; r6 boards need the U7 bodge (§4.4 J8).** Sections below marked 'as built' override the original spec text.

> **As built — rev A (compact, 2026-08-23)**
> Single 12 V input J1 XT60PB-M → F1 10 A mini blade → Q1 Si7461DP high-side P-FET RPP → `VIN`. PS1 D24V50F5 → `+5V_LOGIC` (and `+5V_LED` via F3 3 A polyfuse); PS2 D24V25F6 → `+6V_SERVO` (6.0 V); U5 AMS1117-3.3 → `+3V3`. No charge path on the board. J11 slip ring is 2-pin (5V_LED / GND). IMU = GY-BMI160 breakout on J6 over I²C-A (ESP32 D13/D15). J16 removed; J17 Qwiic on I²C-B (RP2350 GP26/27). MAX9744 as a plug-in module on J_AMP (2×7). All modules — ESP32 DevKitC 30-pin, RP2350-Zero, DFPlayer, PS1/PS2 — socketed and customer-fitted. 4-layer 1 oz; netclasses Default 0.15 / 0.25 mm, Rail 0.6 mm, Power 1.2 mm. No test points, no reset switch, no `MOTOR_EN` net.

---

## 1. Why v10 — what the current boards actually do wrong

The v9.15 zips contain only Gerbers (no schematic), so every net below was reconstructed from copper. **Every firmware pin matches the PCB** — nothing in v9.15 is mis-wired. The problems are structural:

| # | v9.15 / v8.2 defect | Consequence seen on the bench |
|---|---|---|
| 1 | **All traces 6 mil, no ground pour, 4 vias on the whole board** — including GND, +5 V and the +6 V servo feed (~0.35 Ω) | 0.7 V drop at 2 A on the servo rail; brown-outs under servo + audio + BT bursts; one 1000 µF and zero 100 nF decoupling |
| 2 | **5 V injected into both Feathers' USB/VBUS pins** with no diode | A laptop on either USB port parallels its 5 V with the Pololu buck — exactly the condition during every tuning session |
| 3 | **DFPlayer on SoftwareSerial** (32u4 D9/A4) | Interrupts blocked ~1 ms/byte → the CRC/PAYLOAD errors on the 74880 link correlated with every sound command |
| 4 | **32u4 at 93 % flash / 1.7 KB of 2.5 KB RAM** (8 MHz AVR) | No room left for body features; torn 32-bit encoder reads; servo jitter from ISR load |
| 5 | **Separate MCU + UART just to read an I²C IMU** (Trinket M0) | 50→100 Hz serialised angle, extra firmware, extra board; 21 Hz DLPF was the original "laggy" feel |
| 6 | **Two 3.3 V LDOs (ESP32 Feather, 32u4 Feather) shorted through the DFR0601 VCC pins** | Two regulators fighting; ESP32 radio peaks starve the 32u4 |
| 7 | **ESP32 strapping pin IO12 used as UART TX to the 32u4; IO15 drives flywheel PWM; IO13 shares the red LED** | Boot depends on the 32u4 RX being high-Z; LED flickers with traffic |
| 8 | **200 kΩ S2S pot on IO34, no RC filter**, no pull-up (input-only pin) | Noisy position signal into the inner loop; a broken wiper floats the ADC |
| 9 | **"VCC sensor" JST straight into IO39** — the BOM module outputs 4.8 V at 24 V | Abs-max on the ESP32 ADC is 3.6 V |
| 10 | **BSS138 auto-direction shifter on NeoPixel data** | Too slow for 800 kHz WS2812 (Adafruit's own warning); flicker/garbage risk |
| 11 | **DFPlayer BUSY through a 20k/20k divider + INPUT_PULLUP** | ≈2.0 V against a 1.98 V VIH — on the threshold |
| 12 | **No input protection** — no fuse, reverse-polarity, TVS; unkeyed 0.1" motor ribbons (a reversed ribbon puts 3.3 V on GND) | |
| 13 | **Dome: "5V" jumpers actually select VBAT (3.7–4.2 V)**; no mounting holes; no series R / bulk cap on LED data; no I²C pull-ups | |
| 14 | **IMU: no mounting holes; two modules on 0.1" headers** | Mechanical resonance couples straight into the balance loop |
| 15 | **One 2.4 GHz radio shared by BT Classic (pads) and ESP-NOW (dome)** | Coexistence tuning was needed in RC4; unavoidable while both live on one ESP32 — but the dome link can be made far lighter |

Anything in this list that a bodge wire can fix on the *current* boards is in §9 (do those now).

---

## 2. Hard requirements that shape v10

1. **PS3 Navigation controllers → Bluetooth Classic → ESP32 (classic)**. ESP32-S3/C3 have no BT Classic. Bluepad32 stays. This single fact fixes the main MCU.
2. **Three physical boards stay** — the mechanics dictate it: the dome is magnetically mounted and unwired (battery + ESP-NOW), and the IMU sits on top of the frame at the pitch/roll axes, not wherever the mainboard fits.
3. **Motor drivers stay off-board** (DFR0601 12 A, 12 V pack). The mainboard is logic-only; motor current never touches it. Keep the 2×5 ribbon interface. As built: J2/J3 are bare, unshrouded 2×5 0.1" pin headers (PH2-10-UA / A2541WV-2x5P) — NOT keyed; mark pin 1 on the ribbons.
4. **bb8 workflow unchanged**: USB flash + serial console per board, build stamps, banner on attach, `bb8 tune/pair` — v10 must keep a console UART and an auto-reset-on-open that *doesn't* wander into download mode.
5. **12 V 3S4P pack in** (§14), 5 V / 6 V / 3.3 V made on-board with real copper; 3.3 V never shared between modules.

---

## 3. System architecture v10

```mermaid
flowchart LR
  subgraph PADS["2× PS3 Nav (BT Classic)"]
  end
  subgraph MAIN["MAINBOARD v10  (shadow body)"]
    ESP["ESP-WROOM-32 DevKit (socketed)\nBluepad32 · balance PID\ndrive / S2S / flywheel\nIMU fusion"]
    RP["RP2350-Zero (socketed) body co-processor\nservos · dome spin + encoder (PIO)\nDFPlayer (HW UART1) · body LEDs (PIO)"]
    PWR["Power: 12V→5V 5A buck (PS1 D24V50F5) · 6V 2.5A buck (PS2 D24V25F6)\n3.3V 1A LDO (U5 AMS1117-3.3: sensors + driver VCC) · F1 10A / Q1 Si7461DP RPP / SMBJ15A TVS"]
    ESP -- "UART 921600\n(SerialTransfer)" --- RP
  end
  subgraph IMU["IMU v10"]
    ICM["GY-BMI160 breakout\nI²C-A (addr 0x68/0x69) · upgrade: ICM-42688"]
  end
  subgraph DOME["DOME v10"]
    DESP["ESP-WROOM-32 DevKit (socketed)\nESP-NOW · 5× NeoPixel from VBAT\nreed/motion wake · fuel gauge · sensors"]
  end
  PADS -. BT .-> ESP
  ICM -- "I²C-A: D13 SDA / D15 SCL, JST-XH 4-pin (J6: +5V GND SCL SDA)" --> ESP
  ESP -. "ESP-NOW ch 11" .-> DESP
  MAIN -- "slip ring J11 (2-pin): +5V_LED / GND out to shell lights" --> SHELL["Shell: body NeoPixels"]
  PACK["12 V pack: charges via its OWN lead out through the axle\n(no charge path on the board)"] -- "J1 XT60" --> MAIN
  LIPO["1S LiPo + USB-C charger\n(dome is NOT wired — magnets)"] --> DESP
  ESP -- "J2 (DRIVE ch A + S2S ch B) · J3 ch A (FLYWHEEL)\n2× bare 2x5 ribbons" --> DRV["2× DFR0601 12A\n(off-board, 12V pack)"]
  RP -- "J3 ch B (DOME)" --> DRV
```

**What changed and why**

| v9.15 | v10 | Reason |
|---|---|---|
| HUZZAH32 Feather (socketed) | **ELEGOO ESP-WROOM-32 DevKitC 30-pin** (plugs into J_U1A/J_U1B 1×15 female headers, rows 25.4 mm apart; the 38-pin variant does NOT fit; CP2102, standard auto-reset) | All 26 usable GPIO on the headers incl. 34/35/36/39 (Feather exposes 21); VIN/USB diode-OR'd on the module; same Bluepad32 core. |
| Feather 32u4 (8 MHz AVR, 28 KB) | **Waveshare RP2350-Zero** (socketed; 150 MHz, 2 MB flash, 520 KB RAM, 2 HW UARTs, 3 PIO blocks, 24 PWM, native USB) | Ends the flash ceiling; PIO does quadrature decode and WS2812 with zero CPU/ISR load; DFPlayer on a real UART kills the SoftwareSerial corruption. 23.5 × 18 mm. Arduino core: `earlephilhower/arduino-pico`. |
| Trinket M0 + MPU-6050 over UART | **GY-BMI160 breakout** (upgrade: ICM-42688-P) over **I²C-A** (D13 SDA / D15 SCL, 4.7 k pull-ups R5/R6, bus shared with the AS5600) | Raw gyro/accel straight into the ESP32; fusion (Mahony/complementary) on the control loop's own clock → no 50/100 Hz serialisation, no second firmware. (Sample rate: firmware-defined.) |
| Two Feather LDOs shorted via the driver ribbon | Each module regulates its own 3.3 V; **one board 3.3 V LDO** feeds sensors, shifters and **the DFR0601 VCC pins** | Ends the LDO fight; no module's 3V3 ever leaves the module |
| Off-board Pololu modules | **Buck modules on-board footprints** (PS1 Pololu D24V50F5 5 V 5 A, PS2 Pololu D24V25F6 6 V 2.5 A; customer-fitted, 5-pin row EN VIN GND GND OUT) + protection | Real copper to the loads, one harness less |

**Why not one MCU?** The DevKit has 26 usable GPIO; the body functions need ~14 more than the balance side uses, and the encoder/WS2812/servo timing on the same core as a 100 Hz control loop + BT stack is asking for the jitter we just removed. The RP2350-Zero is $5 and makes the split clean. **Why not ESP32-S3 for the body?** No need for radio there; RP2350 PIO is the better peripheral set.

---

## 4. Mainboard v10.0 ("shadow body")

### 4.1 Power tree

```mermaid
flowchart LR
  BAT["J1 XT60PB-M vertical: 12 V 3S4P pack\n(9–12.6 V, 20 A BMS) — single board input"] --> F["F1 10 A mini (ATM) blade fuse\nKeystone 3568 holder"] --> RPP["Q1 Si7461DP high-side P-FET RPP\n(D6 BZT52C12 gate clamp + R33 47k)\n+ D1 SMBJ15A TVS · C1 220 µF/25 V"] --> VIN
  VIN --> B5["5 V / 5 A buck\n(PS1 Pololu D24V50F5)"]
  VIN --> B6["6 V / 2.5 A buck\n(PS2 Pololu D24V25F6)"]
  VIN --> BS["Battery sense\n47k/10k + 3.3 V clamp → GPIO35"]
  B5 --> R5["+5V_LOGIC\nDevKit VIN (module diode-OR'd) · RP2350-Zero 5V via D2 SS14\nDFPlayer · encoder (J8) · IMU breakout VIN (J6 pin 1) · U6 74AHCT125 · U5 LDO"]
  B5 --> R5L["+5V_LED  (separate pour, own 1000 µF, 3 A polyfuse)\nshell NeoPixels via slip ring"]
  R5L --> SR["J11 slip ring (JST-XH 2-pin)\n5V_LED · GND"]
  R5L --> NP["J10 body NeoPixel (5V_LED · GND · DATA)"]
  R5 --> LDO["U5 3.3 V 1 A LDO (AMS1117-3.3, SOT-223)"] --> R33["+3V3 (board rail)\nS2S sensor (J7) · hall (J9) · U7 74LVC245 · I²C pull-ups · ESTOP pull-up\nJ17 Qwiic · amp Vi2c · DFR0601 VCC pins (J2/J3)"]
  B6 --> R6["+6V_SERVO\n2× jumbo servos @ 6.0 V (Hitec HS-805BB / JX PDI-HV2060MG) on J4/J5\nPower netclass 1.2 mm tracks · C4 470 µF/16 V"]
  USBE["DevKit USB (laptop)"] -. "module's own diode-OR\n→ can't back-feed +5V_LOGIC" .- R5
  USBR["RP2350-Zero USB-C (laptop)"] -. "SS14 blocks back-feed" .- R5
```

Both modules make their own 3.3 V on-board (AMS1117 on the DevKit, ME6217 on the Zero) and those outputs are **left unconnected** — the v9.15 LDO-fight bug cannot recur.

- **Copper (as built):** 4-layer, 1 oz, 1.6 mm FR4: F.Cu signal + GND pour / In1.Cu solid GND plane / In2.Cu +5V pour + some signal / B.Cu signal + GND pour; GND stitching vias stamped post-route. Netclasses: Default 0.15 mm clearance / 0.25 mm track; Rail (+5V_LOGIC, +3V3) 0.6 mm; Power (VIN, BAT*, +5V_LED, +6V_SERVO, AMP_12V*, RP_5V) 1.2 mm; Audio (SPK_*) 1.0 mm; Motor (*_PWM/INA/INB, SERVO_*) 0.5 mm. Min via 0.6 mm pad / 0.3 mm drill.
- **Decoupling (as built):** C1 220 µF/25 V on VIN; C2 470 µF/10 V on +5V_LOGIC; C3 1000 µF/10 V on +5V_LED; C4 470 µF/16 V on +6V_SERVO; C7 1000 µF/25 V on AMP_12V; 10 µF C5 (+5V_LOGIC) and C6 (+3V3); 100 nF C8 (U7), C9 (U6), C10/C11 (+5V_LOGIC at modules/DFPlayer), C12 (RP_5V), C29 (VBAT_SENSE reservoir).
- **USB (as built):** each module's USB can power its own MCU for bench work. The Zero's 5 V pin is fed through D2 SS14, so its USB-C never back-feeds the board. The DevKit's VIN is tied straight to `+5V_LOGIC` (no board diode) — its own on-module diode only blocks the buck from reaching the laptop; a laptop on the DevKit's USB **does** back-feed the 5 V rail while the pack is off, so program it out of the socket or with the pack connected.
- **E-stop (as built):** J15 `ESTOP` JST-XH 2-pin (LOOP / GND, normally-closed loop) with R28 10 k pull-up to +3V3, read by ESP32 GPIO36 (VP) as `ESTOP_SENSE`. There is NO hardware `MOTOR_EN` gate on the board; R12–R23 are static 10 k pull-downs on all 12 driver inputs (hold the drivers off during reset), and the E-stop itself is enforced in firmware only.
- **Input protection — Q1 RPP detail:** Si7461DP (−60 V, 14.5 mΩ, −14 A) PowerPAK SO-8, high-side: pads 1–3 SOURCE = `VIN`, pad 4 GATE, pad 5/tab DRAIN = `BAT_F` (fused pack +). On plug-in the body diode pre-charges `VIN`, then VGS = −VIN enhances the channel. D6 BZT52C12 (K = `VIN`, A = gate) clamps VGS to −12 V because the SMBJ15A can let `VIN` reach ~24 V on a transient; R33 47 k gate→GND.

### 4.2 ESP-WROOM-32 DevKit pin map — validated against the module's header

The ELEGOO board is the classic "ESP32 DevKit V1" layout (CP2102, EN + BOOT buttons, blue LED on GPIO2, AMS1117). The board is built for the **30-pin (2 × 15)** variant only: sockets J_U1A/J_U1B are 1×15 female headers (B-2200S15P-A120) with rows 25.4 mm apart, antenna end toward H3 over a copper keep-out. **A 38-pin DevKit does not fit.** (Amazon B0D8T53CQ5.) GPIO 6–11 (flash) are not on either header — correct, we don't use them.

Header order, 30-pin (USB at the bottom): left `EN VP(36) VN(39) 34 35 32 33 25 26 27 14 12 13 GND VIN` · right `3V3 GND 15 2 4 RX2(16) TX2(17) 5 18 19 21 RX0(3) TX0(1) 22 23`.

Strapping rules respected: BOOT button handles GPIO0 (not on the 30-pin header — fine); GPIO2 carries the module's blue LED (status, output only); **GPIO12 unused**; GPIO15 only as I²C-A SCL (pulled high by R6 = correct strap state); GPIO5 is unconnected (spare) — the SPI set 5/18/19/23 is free since the IMU moved to I²C. 34/35/36/39 are input-only (no internal pull-ups → external resistors).

| GPIO | Header label | Function | Notes |
|---|---|---|---|
| 1 / 3 | TX0 / RX0 | UART0 → CP2102 on the module | console + flashing; module's own DTR/RTS auto-reset, standard esptool polarity ✓ |
| 2 | D2 | status LED (module's blue LED) | |
| 4, 16, 17 | D4, RX2, TX2 | DRIVE: PWM, INA, INB | 20 kHz LEDC; 10 k pull-downs at the header. (UART2's default pins are repurposed as GPIO — UART2 is re-mapped to 21/22 via the GPIO matrix.) |
| 25, 26, 27 | D25, D26, D27 | S2S: PWM, INA, INB | |
| 32, 33, 14 | D32, D33, D14 | FLYWHEEL: PWM, INA, INB | |
| 5, 18, 19, 23 | D5, D18, D19, D23 | **spare** (NC on the socket) | freed when the IMU moved to I²C-A; a full VSPI set if ever needed |
| 21 / 22 | D21 / D22 | UART2 TX/RX ↔ RP2350-Zero GP1/GP0 | 921600 baud, 3.3 V, short on-board trace; `Serial2.begin(921600, SERIAL_8N1, 22, 21)`. Nets: D21 = `LINK_ESP_TX` → Zero GP1; Zero GP0 = `LINK_RP_TX` → D22 |
| 13 / 15 | D13 / D15 | I²C-A SDA / SCL (R5/R6 4.7 k to +3V3) | IMU GY-BMI160 on J6 (0x68/0x69) + AS5600 on J7 pins 4/5 (0x36); no separate expansion JST on this bus (J16 removed); SCL pull-up keeps GPIO15 high at boot ✓ |
| 34 | D34 | S2S pot wiper | 10 k pot, 1 k series + 100 nF to GND |
| 35 | D35 | battery sense (VBAT_SENSE) | R24 47k / R25 10k from VIN (12.6 V → 2.21 V), D4 BAT54S clamps to GND and +3V3, C29 100 nF |
| 36 | VP | ESTOP loop sense (`ESTOP_SENSE`) | R28 10 k pull-up to +3V3 |
| 39 | VN | **not connected** (spare, input-only) | no charger sense — the pack charges via its own lead, not through the board |
| VIN | VIN | +5V_LOGIC in | module diode-OR's VIN with its USB ✓ |
| 3V3 | 3V3 | **not connected** | module's AMS1117 powers the module only |

Count (as built): 16 GPIO used (4, 13, 14, 15, 16, 17, 21, 22, 25, 26, 27, 32, 33, 34, 35, 36). Spare on the socket: 5, 18, 19, 23 (a full SPI set) and 39 (input-only); 2 is the module LED, 12 is a strap, 0 is BOOT (not on the 30-pin header). I²C-A expansion is via J7 pins 4/5.

### 4.3 RP2350-Zero pin map — validated against the module's edge pins

The Waveshare RP2350-Zero exposes **20 GPIO on its edge**: GP0–GP15 down the two long sides and GP26–GP29 + 5V / GND / 3V3 on the short end (23 pads). **GP16–GP25 exist only as back-side reflow pads** — not used. GP16 also drives the module's on-board WS2812 status LED. Reset and BOOT buttons on the module.

| GP | Function | Peripheral check | Notes |
|---|---|---|---|
| 0 / 1 | UART0 TX / RX ↔ DevKit GPIO22 / 21 | UART0 default pins ✓ | 921600; `Serial1` in arduino-pico. Nets: GP0 = `LINK_RP_TX` → ESP32 D22; ESP32 D21 = `LINK_ESP_TX` → GP1 |
| 4 / 5 | UART1 TX / RX → DFPlayer RX / TX | UART1 default pins ✓ | hardware UART (`Serial2`). Nets: GP4 = `DF_TX_RP` → R1 1 k → DFPlayer RX (pin 2); DFPlayer TX (pin 3) = `DF_RX_RP` → GP5 |
| 6, 7 | dome-tilt servos L / R | PWM slice 3A/3B ✓ | 50 Hz, µs resolution |
| 8, 9, 10 | dome spin PWM / INA / INB | PWM slice 4A + GPIO | to DFR0601 ch B on J3 (even pins 4/6/8, bare 2×5 header shared with the ESP32's flywheel channel) |
| 11, 12 | dome encoder A / B | **PIO quadrature** (any GPIO) ✓ | 5 V encoder → U7 74LVC245 (A1/A2 → B1/B2, DIR = +3V3) |
| 13 | hall | GPIO in | 10 k external pull-up (E9: never rely on internal pulls) |
| 14 | body NeoPixel data | **PIO WS2812** ✓ | → 74AHCT125 → 330 Ω |
| 15 | DFPlayer BUSY | GPIO in | 3.3 V logic, direct from DFPlayer pin 16 |
| 26 / 27 | I²C-B SDA / SCL (R7/R8 4.7 k) | I²C1 default pins ✓ | J17 Qwiic (JST-SH, GND 3V3 SDA SCL) + MAX9744 module on J_AMP pins 6/7 (0x4B) |
| 28 | +5V_LOGIC sense | ADC2 ✓ | 10k/10k divider |
| 2 | MAX9744 SHDN (amp mute) | GPIO out | low at boot = silent, no pop |
| 3, 29 | spare | 29 = ADC3 | |
| GP16 (internal) | on-board RGB status LED | PIO WS2812 | already wired on the module |
| 5V | +5V_LOGIC in via SS14 | | the Zero has no USB back-feed diode — the SS14 is it |
| 3V3 | **not connected** | | module's ME6217 powers the module only |
| USB-C | bb8 `upload body` | | UF2 / picotool via arduino-cli, VID `2E8A` |

Count: 18 GPIO used of 20 on the edge (GP0–2, 4–15, 26–28); spare GP3 and GP29 (ADC3). Not 5 V tolerant — every 5 V signal (encoder, shell-side anything) is shifted. **RP2350 erratum E9:** internal pull-downs leak; every input has an external resistor (hall, encoder, BUSY are all actively driven or externally pulled).

Socket geometry (board frame: origin = axle centre, +x toward the tail, +y up): left row J_U2A (5V GND 3V3 GP29 GP28 GP27 GP26 GP15 GP14) at x = −17.62; right row J_U2B (GP0 … GP8) at x = −2.38; end row J_U2C (GP9 … GP13) at y = 22.84, pin 19 at x = −15.08. Long rows start at y = 44.43 and run toward −y; 1×9 + 1×9 + 1×5 female sockets, USB-C at the +y end.

### 4.4 Connectors — as built (refs per `hardware/netlist/mainboard.py`; JST-XH unless noted; J2/J3/J4/J5/J_AMP are bare 0.1" pin headers, not keyed)

| Ref | Type | Pinout |
|---|---|---|
| `J1` | XT60PB-M **vertical** PCB male (Amass; LCSC C19268037) | 1 BAT+ / 2 GND — single board input; pack lead = XT60 female; 15.5 mm tall + mating plug → cover cut-out |
| `J2`, `J3` | 2×5 0.1" **bare pin header (no shroud, unkeyed)** | odd pins 1/3/5/7/9 = channel A: VCC(3V3) PWM1 INA1 INB1 GND; even pins 2/4/6/8/10 = channel B: VCC PWM2 INA2 INB2 GND. J2 = DRIVE (A) + S2S (B); J3 = FLYWHEEL (A) + DOME (B). 10 k pull-downs R12–R23 on every PWM/INA/INB |
| `J4`, `J5` (servo L / R) | 1×3 0.1" pin header (standard servo plug) | SIG / +6V_SERVO / GND — Hitec HS-805BB or JX PDI-HV2060MG |
| `J6` (IMU) | JST-XH 4-pin | 1 +5V (→ GY-BMI160 VIN, its own LDO) · 2 GND · 3 SCL · 4 SDA — I²C-A; same order as the v9.15 MPU connector so the old cable fits. CS/SA0 left open → I²C |
| `J7` (S2S sensor) | JST-XH 5-pin | 3V3 · GND · OUT · SDA · SCL — AS5600 (I²C-A, 0x36) or pot on OUT; OUT → R2 1 k → C28 100 nF → GPIO34 |
| `J8` (dome encoder) | JST-XH 4-pin | **B · A · GND · 5V** — identical to the v9.15 DOME_ENC order so the NeveRest cable plugs straight in; A/B → U7 74LVC245A → GP11/GP12. U7: pin 1 DIR = +3V3 (jumpered to pin 20 around the package end; DIR high = A→B per the TI SN74LVC245A pin table — 5 V encoder on A1/A2, RP2350 on B1/B2), unused A3–A8 tied to GND. Fixed in the netlist and the r7 gerbers. **Boards made from the r6 gerbers need a bodge: cut the GND thermal spoke into U7 pin 1 and bridge pin 1 to pin 20 (VCC).** |
| `J9` (hall) | JST-XH 3-pin | 3V3 / GND / SIG (R9 10 k pull-up) |
| `J10` (body NeoPixel) | JST-XH 3-pin | 5V_LED / GND / DATA (GP14 → U6 74AHCT125 → R3 330 Ω) |
| `J11` (slip ring, body lights) | JST-XH 2-pin | 5V_LED / GND — 5 V out to the shell NeoPixels only (F3 3 A polyfuse, C3 1000 µF). The pack charges through its own lead routed out through the axle; no charge circuit crosses the board |
| `J13`, `J14` (SPK L / R) | 2-pin JST-VH (B2P-VH) each | + / − bridge-tied from the MAX9744 module (never grounded) — see §17 |
| `J15` (ESTOP) | JST-XH 2-pin | LOOP (ESTOP_SENSE → GPIO36, R28 10 k to 3V3) / GND — normally-closed loop |
| `J17` (I²C-B expansion, RP2350 bus) | JST-SH 4-pin Qwiic (SM04B-SRSS-TB, horizontal) | **GND · 3V3 · SDA · SCL** (Qwiic order). There is no I²C-A expansion connector — J16 was removed; the ESP32 bus is reachable on J7 pins 4/5 |
| `J_AMP` (MAX9744 module) | 2×7 0.1" pin header | pinout in §17 |
| `J_U1A`, `J_U1B` (ESP32 DevKitC 30-pin) | 1×15 female sockets (B-2200S15P-A120), rows 25.4 mm apart | DevKit pin order per §4.2; 38-pin DevKit does not fit |
| `J_U2A`, `J_U2B`, `J_U2C` (RP2350-Zero) | 1×9 + 1×9 + 1×5 female sockets | geometry in §4.3; GP16–25 (back pads) unused |
| `U3` (DFPlayer Mini) | 2×8 0.1" socket | SD slot faces the tail (card in/out through the tail opening) |
| `PS1`, `PS2` (Pololu bucks) | 1×5 0.1" row each | EN · VIN · GND · GND · OUT — EN left open = enabled; D24V50F5 (PS1, +5V) / D24V25F6 (PS2, +6V_SERVO) |
| USB | on the plug-in modules only (DevKit's own USB / RP2350-Zero USB-C) | the mainboard has no USB connectors |

**Unkeyed ribbons — §1 defect 12 not fixed in rev A.** J2/J3 are bare 2×5 headers with VCC(3V3) on pins 1/2 and GND on pins 9/10, so a ribbon plugged in rotated 180° still shorts 3V3 to GND. Mark pin 1 on both ends of both ribbons (§9 item 8 applies to v10 as well). Shrouded headers are a rev B item.

**Mounting holes** (5× M3, Ø3.2 mm; Ø6 × 5 mm boss with heat-set insert at each). Board frame: origin = axle centre, +x toward the tail, +y up; casing STL frame Y = x + 5.6, Z = y + 160.9.

| Hole | board x | board y | casing Y | casing Z |
|---|---|---|---|---|
| H1 | −42.00 | 24.00 | −36.40 | 184.90 |
| H2 | −42.00 | −24.00 | −36.40 | 136.90 |
| H3 | 27.00 | 37.00 | 32.60 | 197.90 |
| H4 | 27.00 | −37.00 | 32.60 | 123.90 |
| H5 | 70.00 | 0.00 | 75.60 | 160.90 |

Height budget (`hardware/mechanical/compact-cover/cover_layout.md`): ~14 mm above the board in the teardrop zone, ~34 mm inside r 42.5 around the axle. Tallest parts: J1 XT60 15.5 mm (+ ~20 mm mating plug), J2/J3/J_AMP 14, DevKit sockets 13.6, Zero sockets 13.3, DFPlayer 12.5, servo/speaker headers 11, electrolytics 10.5, JST-XH 10, Pololu bucks 9. Over budget in the teardrop zone: J1 (cover cut-out / plug through the cover) and a standard mini ATM fuse in F1 (~16 mm) — use low-profile APS/ATT mini fuses (11 mm) or cut a window.

### 4.5 Bench / test features

Rev A has no test points, no reset switch and no `MOTOR_EN` net. Silkscreen as built: 'Z-DRIVE v10.0' front band; back: title, 'Design by James VanDusen', credits, repo + rev A git/date stamp. No per-connector pinout legends — use the §4.4 table.

---

## 5. IMU board v10.0

**Goal:** the smallest, stiffest thing that can be bolted at the frame pivot.

- **Sensor: BMI160 (GY-BMI160 breakout) — adequate and chosen.** 0.008 °/s/√Hz gyro, 180 µg/√Hz accel, SPI + I²C, 3.3 V with an on-board LDO; at a 100 Hz gyro-dominant fusion its noise integrates to ~0.001° per step — not the limiting factor (the MPU-6050 problems were the 21 Hz DLPF and the serial hop, not noise). Wired **I²C** (as built): breakout VIN ← J6 pin 1 (+5V, on-board LDO makes 3.3 V), GND ← pin 2, SCL ← pin 3 (ESP32 D15), SDA ← pin 4 (ESP32 D13); CS and SA0 left open → I²C mode, address 0x68/0x69; bus shared with the AS5600 (0x36). Mount by its **holes** (M2 standoffs), never by the header pins. Upgrade path on the same 4-pin I²C connector: **ICM-42688-P** or **BMI270** breakouts.
- **No MCU.** I²C-A straight to the ESP32 (fusion on the control loop). If the cable must be long (> 20 cm) or routed near motor leads, the fallback is a **BNO085 in UART-RVC mode** (fused pitch/roll at 100 Hz over 3 wires, immune to I²C cable issues) — leave that footprint as a DNP option on the same board.
- No custom IMU PCB was built for rev A: the GY-BMI160 breakout is used directly (its own holes / LDO / pull-ups), on a 4-wire JST-XH cable from J6. A 20 × 20 mm carrier with M2.5 holes remains an option (not in the repo netlist).
- Cable: ≤ ~30 cm at 400 kHz with the on-board 4.7 k pull-ups (R5/R6) plus the breakout's own; keep it away from motor leads. The 'shielded 6-wire, 33 Ω series' advice applied only to the abandoned SPI plan.

Firmware: `TrinketM0_MPU_RC4` retires; the ESP32 gets an `Imu` module (BMI160 I²C driver on D13/D15 + Mahony/complementary filter; fusion rate firmware-defined, angles sampled by the 100 Hz control tick). The existing `GYRO_*_SIGN` logic becomes a mounting-orientation matrix.

---

## 6. Dome board v10.0

*Not captured in the mainboard netlist (`hardware/netlist/mainboard.py`) — the section below is still the spec, not as-built.*

- **ELEGOO ESP-WROOM-32 DevKit, socketed** (same module as the drive — one BOM line, one footprint; keeps the ESP-NOW firmware). Its own CP2102 + USB for bb8.
- **Power — the dome is not wired to anything** (it rides the shell on magnets), so it is a battery product:
  - **1S LiPo 2500–3000 mAh** on a JST-PH; **USB-C charging at 1 A** (BQ24075 or MCP73831 at 500 mA) with charge/done LEDs; **MAX17048 fuel gauge** on I²C so the drive can show dome battery % in telemetry (today's `A13` divider read becomes a real gauge).
  - **NeoPixels run straight from VBAT** (3.7–4.2 V): 3.3 V data ≥ 0.7 × VBAT, so no level shifter and no boost are needed — v8.2's "5V = VBAT" jumpers were accidentally right. 330 Ω series per data line, 1000 µF on the LED rail, 3.3 V LDO (AP2112K, low IQ) for the ESP32. Budget: ESP32 + 5 pixels ≈ 150–250 mA → 10+ h; deep sleep < 1 mA.
  - **Wake/sleep without a button:** a reed switch (GPIO39, ext0) closed by a magnet on the shell's dome-contact ring wakes it when placed; or a LIS3DH motion interrupt. Power switch optional.
- **LEDs:** 5 channels (PSI, sLogic, lLogic, HP, eye) through a **74AHCT125** (5 V data), 330 Ω series each, keyed 3-pin JSTs (5V / GND / DATA). HP moves off strapping GPIO15 → GPIO14; firmware pin table updated.
- **I²C** with 4.7 k pull-ups on GPIO21/22 to a Qwiic-style connector (ToF / PIR / gesture sensors the README promised); one spare GPIO (26) broken out for a PIR.
- **Wake button** on GPIO39 with external pull-up (ext0 wake, RTC-capable), replacing the current always-high GPIO35 wake.
- 4× M3 mounting holes, test points, silk pinouts.

---

## 7. Firmware impact (what changes in the repo)

| Board | Change |
|---|---|
| drive (ESP32) | pin table (§4.2 as built); IMU module (I²C-A on D13/D15 + fusion) replaces `receiveFromTrinket`; link to body → UART2 on 21/22 at 921600; ESTOP on GPIO36 handled in firmware (no hardware MOTOR_EN); battery telemetry from GPIO35 |
| body (RP2350-Zero, new sketch `RP2350_BODY_RC5`) | port of `32U4_DRIVE_RC4`: Servo → `Servo`/PWM; encoder → PIO quadrature; NeoPixel → PIO; DFPlayer → `Serial2` (UART1 on GP4/GP5); ESP32 link → `Serial1` (UART0 on GP0/GP1); EEPROM → LittleFS/`EEPROM` emulation; same serial command set + banner so bb8 and the runbook stay valid |
| imu | retired |
| dome | pin table (HP → 14), wake GPIO, sensor hooks |
| bb8 | `targets.json`: body → `rp2040:rp2040:waveshare_rp2350_zero`, VID `2E8A`; drive/dome → `esp32-bluepad32:esp32:esp32` (DevKit), VID `10C4`; upload via arduino-cli (picotool/UF2 for the Zero) |

The protocol between drive and body stays SerialTransfer with the same structs, so the transition can happen one board at a time.

---

## 8. BOM (major items, per set)

| Item | Part | ≈ $ |
|---|---|---|
| Main MCU | ELEGOO ESP-WROOM-32 DevKit ×2 (drive + dome, socketed) | 16 |
| Body MCU | Waveshare RP2350-Zero (socketed) | 5 |
| USB-UART | on the DevKits | — |
| IMU | GY-BMI160 breakout (upgrade: ICM-42688-P) | 6 |
| Bucks | Pololu D24V50F5 #2851 (5 V 5 A, PS1), Pololu D24V25F6 #2852 (6 V 2.5 A, PS2) — customer-fitted | 20 + 12 |
| LDO | AMS1117-3.3 ×1 (U5, SOT-223, LCSC C6186; alt AP7361C-33ER-13 SOT-223R — NOT AP7361C-33E-13, wrong pinout) | 1 |
| Level shift | 74AHCT125 ×1 (U6, NeoPixel), 74LVC245A ×1 (U7, encoder) on the mainboard | 2 |
| Protection | Keystone 3568 mini-blade holder + 10 A ATM fuse (F1); Si7461DP P-FET (Q1) + BZT52C12 gate zener (D6) + 47 k (R33); SMBJ15A TVS (D1); SS14 (D2, Zero 5V); BAT54S (D4, ADC clamp); 3 A polyfuse (F3, +5V_LED); 3 A 1206 fuse (F4) + ferrite BLM31KN601 (L1) for the amp. No LM66100 ideal diodes. | 5 |
| Audio | DFPlayer Mini DFR0299 (socketed, U3) + MAX9744 amp **module** on J_AMP 2×7 header (QFN on-board deferred to rev B) + C26/C27 1 µF, C7 1000 µF/25 V, F4, L1 | 8 |
| Connectors | XT60PB-M vertical; bare 2×5 ×2 (J2/J3), 2×7 (J_AMP), 1×3 ×2 (J4/J5); JST-XH 2p ×2 (J11, J15), 3p ×2 (J9, J10), 4p ×2 (J6, J8), 5p ×1 (J7); JST-VH 2p ×2 (J13/J14); JST-SH ×1 (J17); female sockets 1×15 ×2, 1×9 ×2, 1×5 ×1; no USB-C on the mainboard | 10 |
| Passives / caps | | 8 |
| PCB (mainboard, 4-layer 1 oz, teardrop 152 × 110 mm compact) | JLCPCB (`hardware/fab/ZDrive_v10_compact_JLCPCB_r7.zip`) | 25 |

Roughly **$100 per droid set**, versus ~$85 of Feathers + Trinket today — and it deletes a whole board and two firmwares.

---

## 9. Do these NOW on v9.15 / v8.2 (bodges, 1 hour)

1. **Jumper the +6 V servo trace** with 18 AWG from `6V_IN` to both servo connectors; same for the GND return.
2. **Schottky diode (SS34) in series with each Feather's 5 V feed** (cut the trace to the USB pin, bridge with the diode) — stops laptop/buck back-feeding during tuning.
3. **100 nF** across DFPlayer VCC/GND, at each Feather 3V3/GND, at the BOB-12009.
4. **S2S pot → 10 kΩ**; add 100 nF from wiper to GND at the JST.
5. **Unplug / never use the `VCC Sensor` JST** until there's a divider (or fit 10 k/3.3 k + 3.3 V Zener inline).
6. **Encoder / hall 4.7 k pull-ups** at the connectors.
7. **Replace the NeoPixel BSS138 channel** with a 74AHCT125 breakout inline (or drive the first pixel from 3.3 V with a 5 V-tolerant strip).
8. Mark the motor ribbons pin-1 with paint; a reversed ribbon shorts 3.3 V to GND.

None of these change firmware.

---

## 10. Open questions before schematic capture

1. ~~Slip ring wiring~~ **Answered:** the slip ring carries **5 V for the shell (ball) lights**, and will carry the **charge line from a charging port at one of the shell's axis points**. **Superseded as built:** the slip ring connector J11 is 2-circuit (5V_LED, GND; F3 3 A polyfuse). The pack charges through its **own charge lead routed out through the axle** — no charge pass-through, no `CHG_SENSE`, no hardware drive lock-out on the board (GPIO39 is spare). The dome is magnetically mounted and **unwired → battery + USB-C charging** (§6). Slip-ring rating only has to cover the shell LEDs (F3 3 A).
2. ~~IMU mounting spot~~ **Answered:** the IMU sits on **top of the frame at the front or back**, aligned to the pitch/roll axes; the frame itself is tilted by swinging the lower flywheel mass for S2S. Consequences: cable run from the mainboard ≈ 15–30 cm → **I²C-A at 400 kHz over the 4-wire JST-XH cable** (+5V GND SCL SDA; R5/R6 4.7 k on the board plus the breakout's own pull-ups) — keep the run short and away from motor leads; BNO085-RVC stays the DNP fallback only if the run exceeds ~40 cm. Because the sensor is **off the pitch axis**, frame rotation adds centripetal/tangential acceleration to the accel channels — fusion must be **gyro-dominant** (Mahony/complementary with a low accel weight, τ ≈ 1–2 s), and the mounting position must be entered as a lever-arm so the firmware can subtract it. Mount as close to the pitch axis as the frame allows; a 20 × 20 mm board with 4× M2.5 makes that easy.
3. ~~Pack voltage~~ **Answered: 12 V 3S4P** (§14) — D24V-series bucks, SMBJ15A TVS, 25 V electrolytics, Si7461DP RPP.
4. ~~S2S position sensing~~ **Proposed: AS5600 on the tilt pivot** (§16) with the pot kept as a wiring-compatible fallback. Confirm the pivot end has clearance for a 6 mm magnet cap + the breakout.
5. ~~Audio~~ **Decided: MAX9744 I²C class-D amp fed by the DFPlayer** (§17) — rev A carries it as a plug-in module on J_AMP (2×7); the QFN goes on-board in rev B. External amp, isolator and 9 V feed deleted.
6. ~~Board outline~~ **Answered:** teardrop ~152 × 110 mm (compact, fabbed; extended 152 × 125 mm variant kept unrouted) with a Ø38 mm axle cut-out at the origin; 5× M3 holes H1–H5 at board (x,y) = (−42,24), (−42,−24), (27,37), (27,−37), (70,0) — see §4.4 and `hardware/mechanical/compact-cover/cover_layout.md`.

---

## 11. Delivery plan

| Phase | Deliverable |
|---|---|
| 0 | §9 bodges on the current boards (unblocks field testing now) |
| 1 | ✔ Mainboard schematic/PCB generated from `hardware/netlist/mainboard.py` (`tools/hw/gen_kicad.py`); IMU board dropped in favour of the GY-BMI160 breakout; dome board: not yet captured |
| 2 | ✔ Mainboard layout (4-layer 1 oz, compact), DRC 0 errors, JLCPCB fab package r7 2026-08-23 (`hardware/fab/ZDrive_v10_compact_JLCPCB_r7.zip`, working folder `hardware/fab/compact-20260823/`) — confirm which revision was ordered; r6 boards need the U7 bodge |
| 3 | `RP2350_BODY_RC5` firmware port + ESP32 IMU module, bench-tested on dev boards before PCBs arrive |
| 4 | Bring-up with bb8 (stamps, banners, tune, pair unchanged), then one-board-at-a-time swap into the droid |

---

## 12. Module options (if you'd rather socket boards than solder bare modules)

Filter #1: the pads need Bluetooth Classic → the main MCU must be a **classic ESP32** (S3/C3/C6 have none).
Filter #2: the body side wants hardware UARTs + timing peripherals, not a faster AVR.

| Role | Module | Pins / serial | Verdict |
|---|---|---|---|
| **Main (drive)** | **DFRobot FireBeetle 2 ESP32-E** (DFR0654) | WROOM-32E; exposes essentially all 24 usable GPIO incl. 35/36/39; USB-C; LiPo charger; castellated *and* through-hole | **Best socketed choice.** Same chip as the bare-module design in §4, so the pin map there applies 1:1. Drop-in for the HUZZAH32's job with 3–4 more GPIO. |
| Main (drive) | Adafruit ESP32 Feather V2 | ESP32-PICO-MINI; Feather pinout (~21 GPIO); STEMMA QT | Feather-compatible but no pin gain over today — only worth it to keep the Feather socket. |
| Main (drive) | bare ESP32-WROOM-32E | all GPIO | Cheapest, smallest; needs SMD assembly (JLC does it for ~$8/board). |
| **Body (co-processor)** | **Raspberry Pi Pico 2 (RP2350)** | 26 GPIO, **2 HW UART**, 3 PIO blocks (12 state machines), 24 PWM, native USB, 520 KB RAM, 4 MB flash, $5 | **Best socketed choice.** Quadrature, WS2812 and servo timing in PIO; DFPlayer on a real UART. Through-hole/castellated, Arduino core mature (`arduino-pico`). Pico (RP2040) is fine too; Pico 2 is the same price with headroom. |
| Body | Adafruit Feather RP2040 | RP2040 in the **Feather footprint** | Physically replaces the Feather 32u4 in today's socket — a possible "v9.16" stopgap. But the 32u4 PCB routes the DFPlayer to A4/D9; check that those land on RP2040 pins that can host UART1 (or accept a PIO UART). Not a free lunch. |
| Body | Teensy 4.0 | 40 pins, **7 hardware UARTs**, 600 MHz, $24 | The raw "serial and pins" champion if UART count ever becomes the limiter (e.g. several serial sound/LED boards). Overkill for today's body; 3.3 V only. |
| **Dome** | FireBeetle 2 ESP32-E | as above | Same module as the main board = one BOM line, same auto-reset circuit, code unchanged. |
| Dome | FireBeetle ESP32-C3 / XIAO ESP32-C3 | small, cheap, ESP-NOW works | Only if size matters; ESP-NOW code ports but the sketch's GPIO table changes. |
| IMU | ICM-42688-P breakout (SparkFun/Adafruit) | SPI/I²C | Mount on the 20×20 carrier from §5 until the custom board exists. |

**Superseded by §13 (chosen):** ELEGOO ESP-WROOM-32 DevKitC 30-pin (drive) + Waveshare RP2350-Zero (body) + GY-BMI160 breakout over I²C. (Table above kept as the option survey.) The original recommendation — FireBeetle 2 ESP32-E (drive) + Pico 2 (body) + FireBeetle 2 ESP32-E (dome) + ICM-42688 breakout — preserves "boards in sockets" maintainability, gains the pins and UARTs, and the §4 pin maps carry over unchanged except the RP2040 pins become Pico GP numbers.

What *not* to chase: more ESP32 UARTs. The classic ESP32 has exactly 3 (UART0 console, UART1, UART2, all pin-remappable) — v10 only needs two (console + body link) — the IMU is on I²C-A.

---

## 13. Chosen modules & 12 V power (decisions 2026-08-21)

**Pack: 12 V / 12 A.** Socketed modules: **Waveshare RP2350-Zero** (body; J_U2A/J_U2B 1×9 + J_U2C 1×5 sockets) and **ELEGOO ESP-WROOM-32 DevKitC 30-pin** (drive; J_U1A/J_U1B 1×15 sockets — the 38-pin does not fit). DFPlayer Mini (U3) and both Pololu bucks (PS1/PS2) are also customer-fitted plug-ins. Board kept as small as possible; copper and ground done properly.

### 13.1 RP2350-Zero body pin map

See §4.3 (validated against the module edge pins).

### 13.2 ESP-WROOM-32 DevKit for drive and dome (pin map in §4.2)

- All GPIO of the §4 / §6 pin maps are on the headers (incl. 34/35/36/39).
- Standard esptool auto-reset polarity (EN ← RTS, IO0 ← DTR) — bb8's port-open reset behaves.
- **VIN and USB are diode-OR'd on the module** → feed VIN with +5V_LOGIC; a laptop on the USB can't back-feed the rail. The module's AMS1117 powers only the module.
- Footprint: 2 × 15-pin 0.1" rows, 25.4 mm apart (J_U1A at x = 24.3, J_U1B at x = 49.7, board frame). Sockets: B-2200S15P-A120 female headers. Antenna end toward H3 over the copper/pour keep-out (22.5,19)–(51.5,33.5) on all layers.

### 13.3 Power at 12 V

| Stage | Part | Notes |
|---|---|---|
| Input | J1 XT60PB-M vertical; F1 **10 A mini (ATM) blade fuse** in a Keystone 3568 holder; Q1 **Si7461DP** high-side P-FET RPP (PowerPAK SO-8: pads 1-3 S = VIN, 4 G, tab D = BAT_F; D6 BZT52C12 K→VIN / A→gate, R33 47 k gate→GND); D1 **SMBJ15A** TVS; C1 220 µF/25 V | board fuse carries bucks + amp only — motors are fed from the pack directly |
| 5 V | **Pololu D24V50F5** (5 A) | +5V_LOGIC (modules, DFPlayer, shifters) and +5V_LED (shell lights via slip ring, own polyfuse + 1000 µF) |
| 6 V | **Pololu D24V25F6** (6.0 V 2.5 A, PS2) | `+6V_SERVO`: 6.0 V is the one voltage both the Hitec HS-805BB (4.8–6 V) and the JX PDI-HV2060MG (6–8.4 V) accept; Power netclass 1.2 mm tracks + C4 470 µF/16 V |
| 3.3 V | U5 AMS1117-3.3 (1 A, SOT-223; alt AP7361C-33ER-13) | S2S sensor/pot (J7), hall, U7 LV side, I²C pull-ups, ESTOP pull-up, J17, amp Vi2c, **motor-driver VCC pins** (one rail, never a module's 3V3). The IMU breakout takes +5V on J6 pin 1 and regulates itself |
| Sense | 47k / 10k divider + 3.3 V clamp → GPIO35 | 12.6 V → 2.2 V |
| Charge path | **none on the board** — the pack charges via its own lead through the axle; GPIO39 is spare | |

As built: teardrop ~152 × 110 mm (compact; casing-dictated outline with Ø38 axle cut-out), 4-layer 1 oz; extended 152 × 125 mm variant kept in `hardware/kicad/extended/` (unrouted). Biggest single space saver if needed later: bare WROOM-32E instead of the DevKit.

---

## 14. Battery — DCZ12-12A, 12 V 12 Ah Li-ion 3S4P (eBay 800431024412)

| Spec (listing) | Value | Design consequence |
|---|---|---|
| Chemistry / config | Li-ion **3S4P**, 9–12.6 V, 144 Wh, > 1000 cycles | Charger = 12.6 V CC/CV 3S. Sense divider 47k/10k → 2.21 V at 12.6 V. |
| **BMS** | **20 A max** (label: 15 A output) | **The constraint** — see 14.2. |
| Charger | 12.6 V **1 A** (9 h) | Charges through the pack's own lead (routed out through the axle) — not via the slip ring or the mainboard; a 12.6 V 2–3 A charger halves the time on the same BMS. |
| Size / weight | 80 × 66 × 60 mm, 540 g | Fits the flywheel carriage flat and centred. |
| Output | bare wires | Crimp **XT60 female** (mates J1 XT60PB-M); **20 A ATO fuse holder** on the + lead at the pack (board fuse F1 10 A mini ATM stays below it — it carries only bucks + amp; motor drivers are fed from the pack directly). |
| Runtime (est.) | 6–8 h idle balancing · **2–3 h active driving** (4–6 A avg) | |

**Firmware:** warn at 10.2 V (3.4 V/cell), **force-disable drive at 9.6 V** (3.2 V/cell) — before the BMS cuts power abruptly mid-balance. `pref lowbat 9.6`.

### 14.1 If the pack ever changes — what the chemistry decides

| If it is… | Full / empty | Consequence for v10 |
|---|---|---|
| **Li-ion 3S** (this pack) | 12.6 / 9.0 V | Charger 12.6 V 3S; cutoff 9.6 V. |
| **LiFePO4 4S** (12.8 V nom.) | 14.6 / 10.0 V | Charger 14.6 V LiFePO4 type; sense 2.56 V at full; cutoff ≈ 11.0 V. |
| **SLA / AGM** | 13.8 / 10.5 V | 13.8 V float charger; no BMS. |

### 14.2 The 20 A BMS is the one number to respect

At 12 V the motors stall at ≈ 9 A (drive, flywheel — ServoCity 5202/5203 class), ≈ 11.5 A (NeveRest dome), ≈ 3–5 A (S2S worm), plus ≈ 3 A of servo + logic draw reflected to 12 V. A **flywheel spin-up during a drive reversal can exceed 20 A → the BMS opens instantly → the balance loop loses power → the droid falls.** That is the failure mode of this pack in this robot, so it gets designed out twice:

1. **Firmware power governor** (valid for the current droid too — same motors): estimated draw = Σ (PWM fraction × stall current) per channel; above ≈ 14 A scale the **flywheel first, then dome spin**, never the balance channels; flywheel spin-up gets its own slew limit. Telemetry field `amps` (estimated) and a `[POWER] governed` event.
2. **Second identical pack in parallel** when space allows: 40 A of BMS headroom, 24 Ah, 1.08 kg on the pendulum (more steering authority too). Join only at equal voltage (both full).

Other checks: the charger connects straight to the pack through its own lead (no slip-ring or board circuit); common-port BMS — no series diode in the charge lead (or the 12.6 V CV phase ends 0.4 V short); reverse protection by the keyed charge plug.

### 14.3 Placement in the flywheel carriage — it changes the physics, mostly for the better

The flywheel area is the pendulum mass the S2S swings left/right to steer.
- **Lower CG, more mass displaced per degree** → more steering authority and inherently steadier pitch. Good.
- **More inertia on the S2S axis** → the worm-gear actuator's bandwidth drops further. The 2026-08-20 capture already showed the S2S lagging its target by 180 ms at Kp = 30; expect final outer gains around Kp 6–10 with `pref swing` ≤ 40. Re-run `bb8 tune s2s` after the install — the tuner will find it.
- **Centre it left-right** on the carriage; an off-centre pack bakes a roll bias into the zero that calibration then hides (and costs S2S authority on one side).
- Secure against the swing (it's accelerated every steering move): strap + blocks, cable with strain relief, fuse **at the pack** (ATO holder on the + lead) in addition to the board fuse.

---

## 15. Repository branches

`main` = v9.15 / v8.2 hardware (RC4.x firmware, kept supported). **`v10`** = this design: new firmware ports (`RP2350_BODY_RC5`, drive IMU module, pin tables), `targets.json` for the DevKit / RP2350-Zero, KiCad sources under `hardware/`. bb8 and the docs are shared — fix on `main`, merge into `v10`.

---

## 16. S2S position sensor — AS5600 on the tilt pivot (replaces the pot)

**Where:** on the **S2S tilt pivot** — the axle about which the frame tips left/right relative to the drive carriage — i.e. the pot's current location. That is the only place the angle is measured 1:1, absolute, with no gearing or backlash between sensor and frame. Either end of the pivot; pick the one with clearance nearest the mainboard.

```
        frame (tilts L/R)                     carriage (fixed)
    ════════════╗                         ╔══════════════
                ║   pivot bolt / axle     ║
                ║ ───────────────────────►║   ┌──────────────┐
                ║                  [N|S]  ║   │ AS5600 board │  bracket to the
                ║          magnet in a    ║   │   chip  ●    │  carriage, coaxial
                ║          plastic cap on ║   └──────────────┘
                ║          the axle end   ║     ◄─ 1–2 mm ─►  air gap
    ════════════╝                         ╚══════════════
```

**Mechanical rules**
- Diametrically magnetised **6 × 2.5 mm** magnet, **centred on the rotation axis ±0.25 mm**, air gap **0.5–3 mm**, chip centred over it.
- Magnet carried in a **3D-printed or brass cap** pressed onto the axle end so steel bolt heads / bearings stay ≥ 3–5 mm away (steel *behind* the magnet is tolerated; steel *beside* it is not).
- Sensor board on the **non-rotating** side, 2 × M2 on a printed bracket — the cable never flexes with the tilt.

**Electrical — drop-in for the pot**
- Wire **both** outputs. `OUT` (ratiometric analog 0–VDD) → the existing pot input **GPIO34** through the same 1 k / 100 nF filter: the 100 Hz loop keeps reading an "ADC pot", no I²C in the control path. Programmed once (via I²C, `ZPOS`/`MPOS`) so the 92° swing spans 0–3.3 V → ≈ **4000 counts over the swing** (pot today ≈ 1000), zero wear, zero wiper noise.
- **I²C-A** (GPIO13/15, address 0x36, via J7 pins 4/5; bus shared with the BMI160 at 0x68/0x69) only for that programming and for health: magnet `DETECTED / TOO WEAK / TOO STRONG` status → `[S2S] sensor OK` in `cfg show`, and a safety cutoff if the magnet is lost (a pot cannot report that).
- `DIR` pin strapped so counts increase toward right tilt (else `REVERSE_S2S`). 3.3 V supply. Connector: JST-XH 5-pin (3V3 · GND · OUT · SDA · SCL) replacing `S2S_POT`; the pot stays usable on the same OUT pin as a fallback.

**Firmware:** no change in the control loop; `cfg calibrate s2s` still finds centre; `pref innerkp` rescales for the 4× counts (≈ 0.9 → 0.25 PWM/count) — or the firmware normalises counts to degrees and the gain stays in PWM/deg.

---

## 17. On-board audio — DFPlayer source + MAX9744 I²C class-D amp

Decision: bring the amplifier onto the mainboard — rev A as a **MAX9744 module plugged into J_AMP (2×7 0.1" header)**, rev B integrates the QFN; drop the external amp, its 12 V feed, the 3.5 mm cable and the ground-loop isolator from the BOM.

```
DFPlayer Mini (SD card, MP3)  ──DAC_L/R──►  MAX9744  ──BTL──►  SPK_L / SPK_R (transducers, 4–8 Ω)
   UART1 from RP2350-Zero                   ▲  I²C1 (0x4B) volume 0–63, SHDN on GP2
                                            └── 12 V from VIN (own 3 A fuse, ferrite, 1000 µF)
```

| Item | Choice | Why |
|---|---|---|
| Amp | **MAX9744** module (rev A, on J_AMP: 1 VDD12 · 2 GND · 3 INL · 4 INR · 5 AGND · 6 SDA · 7 SCL · 8 SHDN · 9–12 OUTL+/OUTL−/OUTR+/OUTR− · 13 Vi2c = +3V3 (required) · 14 GND); QFN-44 on-board in rev B | 2 × 20 W into 4–8 Ω from 4.5–14 V → runs on the 12 V pack directly, **volume over I²C** (63 steps), filterless class-D, shutdown pin. ≈ $3 + a dozen passives. |
| Source | DFPlayer Mini, unchanged | SD-card MP3s, `audio scan` diagnostics, seq-numbered sound protocol — nothing to rewrite |
| Why not I²S + MAX98357A | 3 W mono; decoding MP3 on the RP2350 is a firmware project | Not worth it for a droid that wants to be heard |
| Why not PAM8403 | 3 W, no volume control | Too small for transducers |

**Wiring**
- DFPlayer `DAC_L` / `DAC_R` → C26 / C27 1 µF → J_AMP `INL` / `INR` (pins 3/4); module `AGND` (pin 5) to board GND (single-ended inputs on the module).
- Outputs are **bridge-tied**: `SPK_L±`, `SPK_R±` on two 2-pin JST-VH, never grounded. Keep the BTL traces ≥ 1 mm, short, away from the IMU cable and the pot/AS5600 input.
- `SHDN` ← RP2350 **GP2**: held low at boot (no pop), released 200 ms after the DFPlayer initialises; firmware mutes on `silent` mode and on fault.
- `VDD12` from **VIN** (not the 5 V rail): F4 3 A 1206 fuse → L1 ferrite BLM31KN601 (2.9 A) → C7 1000 µF/25 V → J_AMP pin 1 (`AMP_12V`, Power netclass). The module carries its own local decoupling; no QFN/thermal pad on rev A.
- I²C-B (GP26/27) shared with J17 Qwiic; address 0x4B fixed; the module's `Vi2c` level-shifter reference (J_AMP pin 13) is tied to +3V3 — required.

**Firmware** (`RP2350_BODY_RC5`): keep the DFPlayer at a fixed high level (≈ 25/30) for best SNR and map `vol <0-30>` / the pad's volume buttons onto the MAX9744 (0–63) over I²C; `audio status` reports amp present/ muted. `silentMode` → SHDN.

**Connector changes (§4.4):** `AUDIO_OUT` and `AUDIO_AUX` are replaced by J13 `SPK L` and J14 `SPK R` (2-pin JST-VH each) plus the J_AMP 2×7 module header. No line-level test points on rev A — scope at J_AMP pins 3/4.
