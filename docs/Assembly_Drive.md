# Z-Class Drive — Mechanical Assembly Guide

*Generated from the Fusion 360 model `Z-Class Drive-v4.1` — STEP export for the
structure plus the Fusion **navigator captures** for the linked purchased parts
(the STEP flattens X-referenced components out, so future exports: navigator
screenshots fill that gap). Remaining unknowns carry a **[VERIFY]** flag.*

Companion guide for operating it: [HowToGuide.md](HowToGuide.md) · wiring: the
v9.15 PDF in the [Z-ClassDriveSystem](https://github.com/jlvandusen/Z-ClassDriveSystem) repo.

---

## 0. Drivetrain reference (from the model)

| Stage | Teeth | Ratio | Purpose |
|---|---|---|---|
| Motor Gear → Drive Gear | 26 → 47 | **1.81 : 1** | gear stage off the ROBOTZONE planetary drive motor |
| Chain: sprocket `3307-1006-0010` → plate sprocket `3310-0032-0042` | ~10 → ~42 (per goBILDA numbering) | ≈ **4.2 : 1** | chain stage to the shell drive — the "chain-driven main drive" |
| S2S Motor Spur → Gantry Spur | 22 → 69 | **3.14 : 1** | S2S worm-gear motor tilts the gantry |
| Spur Gear (16T) | — | — | third gear in the S2S stack **[VERIFY** what it meshes — idler or pot takeoff?**]** |
| GantryPot Gear → Pot Gear | 55 → 46 | 1 : 1.20 (pot overdriven) | gantry angle → B10K feedback pot |

The pot turning ~1.2° per gantry degree is why S2S positions are tuned in **pot
counts** (`potCenter` ≈ 1744–1774 on this build; swing 40° ≈ ±the counts the
firmware computes from `POT_COUNTS_PER_DEGREE`).

## 1. Sub-assembly A — Gantry (the structural core)

**Parts (STEP + navigator):** `Gantry`, `DriveGear Casing` + `(Mirror)`, `Speakers`,
**ROBOTZONE planetary gearmotor** (the drive motor), chain sprockets
`3307-1006-0010` (pinion) + `3310-0032-0042` (plate), `1InchBearing_535051`,
goBILDA mounts `1302-0032-1000` and `1400-0032-0032`

1. Fit the two **DriveGear Casings** (mirrored pair) to the gantry — they close
   around the drive gear train and locate its shafts.
   3D-printed references: `hardware/mechanical/InsideGearCasing.stl`,
   `OutsideGearCasing.stl`, `Gantry.stl`.
2. Mount the **speaker pair** in their gantry pockets before the casings block
   access. Wire to the body's amp with a **standard 2-pin JST per speaker**
   (the v10 note formalizes this).
3. **[VERIFY]** casing fastener sizes and whether the casings sandwich the
   gantry or bolt to one face.

## 2. Sub-assembly B — Drive gear train

**Parts:** `Motor Gear (26T)`, `Drive Gear (47T)`, ROBOTZONE planetary gearmotor, chain sprockets (from §1's list)

1. Press/pin the **26T motor gear** onto the **ROBOTZONE planetary gearmotor**
   shaft (`1311 Thru-Hole Sonic Hub, 6 mm D-bore` is in the parts library for
   exactly this kind of shaft). Mount the motor to the gantry via the goBILDA
   `1302-0032-1000` / `1400-0032-0032` mount assemblies.
2. Seat the **47T drive gear** on the output shaft inside the casings; mesh with
   the 26T, set backlash (a strip of paper between teeth while tightening is a
   good default), close the mirror casing.
3. **Chain stage:** the `3307` pinion sprocket rides the gear-stage output; the
   big `3310-0032-0042` plate sprocket drives the shell wheel. Fit the chain,
   tension so it deflects a few mm at mid-span — a tight chain eats the
   planetary's bearings, a loose one skips under torque. **[VERIFY]** chain
   pitch/length and tensioner arrangement.
4. Spin by hand: one full output turn must feel even — no tight spots, chain
   quiet in both directions.

## 3. Sub-assembly C — S2S (steering) gears

**Parts (STEP + navigator):** `S2S Motor Spur Gear (22T)`, `Gantry Spur Gear (69T)`,
`Spur Gear (16T)`, **DC worm-gear motor** (from SwingArms — worm drive holds the
tilt without power, which is why the droid doesn't flop when disabled)

1. 22T onto the worm-gearbox output, 69T sector on the gantry pivot: the motor
   rotates the whole gantry (and with it, the drive) side-to-side — that lean
   is the steering.
2. Place the **16T spur** per the model **[VERIFY** its role — idler between
   stages or the pot-gear takeoff**]**.
3. Mesh, backlash, then swing the gantry lock-to-lock by hand — note the worm
   stage will resist back-driving; drive it from the motor side to check.

## 4. Sub-assembly D — Swing arms + position feedback

**Parts (STEP + navigator):** `S2SArm`, `FlyWheelArm`, `Pot Gear (46T)`,
`GantryPot Gear (55T)`, **PotentiometerB10K** (10 kΩ linear), **ActoBotics
planetary gearmotor** (flywheel drive, by position — **[VERIFY]**),
`8mmIDbearing_608zz` (608ZZ) bearings, `DC worm gear` motor (used in §3)

1. Fit the **S2SArm** and **FlyWheelArm** to the gantry pivots on the 608ZZ
   bearings; the ActoBotics planetary mounts in the FlyWheelArm.
2. Install the **55T GantryPot gear** on the gantry side and the **46T pot gear**
   on the potentiometer shaft; mesh them.
3. **Center rule:** with the gantry mechanically level, the pot must be near the
   middle of its travel — the firmware's boot calibration reads ~1744–1774
   counts here. If the pot rails (0 / 4095) anywhere in the swing, re-clock the
   pot gear a tooth at a time.
4. Wire the **B10K** pot wiper to the drive ESP32 **GPIO34** (per the
   firmware), add 100 nF wiper→GND at the connector (v10 note).

## 5. Sub-assembly E — Flywheel & ballast deck

**Parts:** `FlyWheel_Assembly` → `LazySusanBase`, `Ballast`, `BatteryHarness` (→ `Cable Management`)

1. Bolt the **LazySusanBase** (flywheel bearing base) to the bottom of the frame.
2. Add the **Ballast** mass and the **BatteryHarness**; route leads through the
   **Cable Management** part *before* closing anything — it's modeled as part of
   the harness for a reason.
3. The **ActoBotics planetary** (in the FlyWheelArm, §4) drives this deck's
   rotating mass **[VERIFY** how the disc/deck couples to it — not explicit in
   v4.1**]**.
4. Battery per the power spec (v10 target: 3S pack, 20 A BMS). Keep the mass low
   and centered — this deck *is* the pendulum that makes balance work.

## 6. Sub-assembly F — Head tilt & dome spin

**Parts:** `HeadTilt_Assembly` → `Coupler` (+ **2× 1-inch bearings** `535051`),
`TiltMast`, `TiltMotorCoupler`, `CouplerPin`, `CouplerPin(Mirror)`
plus: 2 dome-tilt servos + dome-spin motor with encoder **[not modeled]**

1. Press the **two 1-inch bearings** into the **Coupler** — the mast rotates in
   these.
2. Fit the **TiltMast** through; secure with **CouplerPin + mirrored pin**.
3. **TiltMotorCoupler** joins the dome-spin motor to the mast. The motor's
   encoder (840 counts/rev in firmware) closes the dome-heading loop.
4. Attach the two tilt servos (body pins 11/12, neutral 70°/110°) so they tilt
   the mast platform. **Power servos from their own 6 V rail** — never the 5 V
   logic feed (bench-proven brownout).
5. The dome's magnet carrier rides the top of the mast; the shell slides between
   dome and carrier. **[VERIFY]** magnet stack orientation.

## 7. Integration order (whole drive)

1. **A Gantry** → **B drive gears** → close casings.
2. **C S2S gears** + motor onto the frame; **D swing arms + pot**; check the
   free swing again *with* motors meshed.
3. **E flywheel/ballast deck** under; batteries in; all wiring through Cable
   Management.
4. **F head-tilt mast** on top.
5. Electronics per the Runbook §1 diagram (drive ESP32, body 32u4, Trinket IMU
   — IMU rigidly mounted, flat, connector orientation per the v9.15 PDF).
6. **Bench verification, before the shell** (each maps to a console/bb8 step):
   - `bb8 monitor body` → `debug encoder` — spin the mast by hand, count moves.
   - Tilt servos: `tilt show`, stick tilt, `bb8 tune dome` once the drive runs.
   - `cfg calibrate` level → `telemetry on` → tip the frame: **roll** should
     drive the S2S motor *against* the lean (S2S polarity verified good on this
     build), pot must track `tgt`.
   - Sounds + PSI (proves body link and dome radio in one shot: play a track).
   - Then the rollers and [RigTuning.md](RigTuning.md) from §2.

## 8. Complete part manifest (from the STEP)

```
Z-Class Drive-v4.1
├─ Gantry
│  ├─ DriveGear Casing            ├─ DriveGear Casing (Mirror)
│  └─ Speakers
├─ Drive Gears
│  ├─ Motor Gear (26T)            └─ Drive Gear (47T)
├─ S2Sgears
│  ├─ S2S Motor Spur Gear (22T)   └─ Gantry Spur Gear (69T)
├─ SwingArms
│  ├─ S2SArm                      ├─ FlyWheelArm
│  ├─ Pot Gear (46T)              └─ GantryPot Gear (55T)
├─ FlyWheel_Assembly
│  ├─ LazySusanBase               ├─ Ballast
│  └─ BatteryHarness ─ Cable Management
└─ HeadTilt_Assembly
   ├─ Coupler ─ 2× 1-inch bearing (535051)
   ├─ TiltMast                    ├─ TiltMotorCoupler
   └─ CouplerPin + CouplerPin (Mirror)
```

From the navigator (linked parts the STEP flattened out):
- Gantry: **ROBOTZONE planetary gearmotor** (drive), sprockets `3307-1006-0010`
  + `3310-0032-0042`, `1InchBearing_535051`, mounts `1302-0032-1000`,
  `1400-0032-0032`, Speakers
- SwingArms: **DC worm-gear motor** (S2S), **ActoBotics planetary** (flywheel),
  **PotentiometerB10K**, `608ZZ` 8 mm-ID bearings
- S2Sgears: + `Spur Gear (16T)`

Still not modeled anywhere: dome-spin motor + encoder, 2 tilt servos, the
6-inch main bearing (`6inchBearing.step` in the parts library), shell +
magnets, chain itself, most fasteners, electronics boards.

*Regenerate this tree after any Fusion revision:*
`python tools/step_tree.py "Z-Class Drive-vX.step"` — prints part counts
and hierarchy from any ASCII STEP export.*
