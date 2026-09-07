# Sub-Assembly D — Swing arms + position feedback

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The swing arms carry the flywheel drive and the S2S position feedback. The gantry angle is geared into a B10K pot so the firmware can steer in **pot counts**. Ratio (source §0): GantryPot → Pot gear = **1 : 1.20** (the pot is overdriven, ~1.2° pot per gantry degree).

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `S2SArm` | Side-to-side swing arm | Fits a gantry pivot on a 608ZZ bearing |
| `FlyWheelArm` | Swing arm that carries the flywheel drive | Fits a gantry pivot on a 608ZZ bearing; the ActoBotics planetary mounts in it |
| `Pot Gear (46T)` | 46-tooth gear on the potentiometer shaft | On the B10K pot shaft; meshes the 55T |
| `GantryPot Gear (55T)` | 55-tooth gear on the gantry side | On the gantry side; drives the 46T pot gear |
| **PotentiometerB10K** | 10 kΩ linear feedback pot | Wiper → drive ESP32 **GPIO34**; carries the 46T pot gear |
| **ActoBotics planetary gearmotor** | Flywheel drive motor (by position — **[VERIFY]**) | Mounts in the FlyWheelArm; drives the flywheel/ballast deck (Sub-Assembly E) |
| `8mmIDbearing_608zz` (608ZZ) | 8 mm-ID bearings | Carry the swing arms on the gantry pivots |
| `DC worm gear` motor | S2S drive motor (also used in Sub-Assembly C) | Drives the S2S gantry-tilt gear stack |

## Tools & fasteners

- Bearing press: the swing arms ride on **608ZZ** 8 mm-ID bearings at the gantry pivots. **[TODO: builder]** — press method / retention.
- Pot wiper filtering: add **100 nF wiper→GND** at the connector (v10 note).
- Fastener sizes: **not specified**; per source §3 they carry a `[VERIFY]` flag. **[TODO: builder]**.

## Assembly steps

1. Fit the **S2SArm** and **FlyWheelArm** to the gantry pivots on the 608ZZ bearings; the ActoBotics planetary mounts in the FlyWheelArm.
   - **[TODO: builder]** — bearing press/retention and arm-pivot fastener spec.
2. Install the **55T GantryPot gear** on the gantry side and the **46T pot gear** on the potentiometer shaft; mesh them. *(55T → 46T = **1 : 1.20**, pot overdriven, §0.)*
3. **Center rule:** with the gantry mechanically level, the pot must sit near the middle of its travel and **must not rail (0 / 4095) anywhere in the swing** — if it does, re-clock the pot gear a tooth at a time. The exact electrical center is found later in firmware with **`cfg autocenter`** (drives to both stops, saves the midpoint); the mechanism just has to allow a centered, never-railed pot.
4. Wire the **B10K** pot wiper to the drive ESP32 **GPIO34** (per the firmware), add 100 nF wiper→GND at the connector (v10 note).
   - **[TODO: builder]** — wire gauge, connector type, and lead routing for the pot harness.

## Per-part notes

### `S2SArm`
Role: the side-to-side swing arm. Mating: rides a gantry pivot on a 608ZZ bearing. Fitment: **[TODO: builder]**.

### `FlyWheelArm`
Role: swing arm that houses the flywheel drive. Mating: rides a gantry pivot on a 608ZZ bearing; the ActoBotics planetary mounts in it. Its motor drives the flywheel/ballast deck (Sub-Assembly E).

### `Pot Gear (46T)` and `GantryPot Gear (55T)`
Role: gear the gantry angle into the feedback pot. Mating: 55T on the gantry side meshes the 46T on the pot shaft. Ratio: **1 : 1.20** — the pot turns ~1.2° per gantry degree, which is why S2S positions are tuned in pot counts (§0). Fitment: re-clock the pot gear a tooth at a time if the pot rails anywhere in the swing.

### PotentiometerB10K
Role: 10 kΩ linear position feedback. Mating: carries the 46T pot gear; wiper → drive ESP32 **GPIO34** with a 100 nF wiper→GND at the connector. Fitment: must sit near mid-travel at gantry-level and never rail (0 / 4095). Note: `potCenter` is per-build — don't hard-code it; use `cfg autocenter` (§0, §7). Firmware derives swing from `POT_COUNTS_PER_DEGREE`.

### ActoBotics planetary gearmotor
Role: the flywheel drive motor, driven by position — **[VERIFY]**. Mating: mounts in the FlyWheelArm; couples to the flywheel/ballast deck (**[VERIFY]** how the disc/deck couples — see Sub-Assembly E).

### `8mmIDbearing_608zz` (608ZZ)
Role: 8 mm-ID bearings that carry the swing arms on the gantry pivots. Fitment/press: **[TODO: builder]**.

### DC worm-gear motor
Role: the S2S drive motor (see Sub-Assembly C for the gear-stack steps). Listed here because the manifest assigns it to SwingArms. Worm stage holds tilt without power.

## Verification

- Both swing arms move freely on their 608ZZ bearings with no bind (re-check free swing at integration, *with* motors meshed — source §7).
- With the gantry mechanically level, the pot sits near mid-travel and **does not rail (0 / 4095)** anywhere across the full swing.
- Pot wiper reads on **GPIO34**; the 100 nF wiper→GND cap is fitted at the connector.
- Later (firmware): `cfg autocenter` drives to both stops and saves the true midpoint; tip the frame and confirm the pot tracks `tgt` (source §7).

## Photos

- **[TODO: photo]** — S2SArm and FlyWheelArm on their 608ZZ pivots.
- **[TODO: photo]** — ActoBotics planetary mounted in the FlyWheelArm.
- **[TODO: photo]** — 55T/46T pot-gear mesh with the gantry level (shows pot clocking).
- **[TODO: photo]** — pot harness with the 100 nF cap at the connector, routed to GPIO34.
