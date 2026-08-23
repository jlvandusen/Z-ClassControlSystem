# Z-Class Drive — Mechanical Assembly Guide

*Generated from the Fusion 360 model `Z-Class Drive-v4.1.step` (28 components,
6 sub-assemblies — full tree in §8). The CAD gives the structure, gear meshes and
ratios; motors, fasteners and the shell interface aren't modeled, so those steps
carry a **[VERIFY]** flag — correct them on the first build and re-commit.*

Companion guide for operating it: [HowToGuide.md](HowToGuide.md) · wiring: the
v9.15 PDF in the [Z-ClassDriveSystem](https://github.com/jlvandusen/Z-ClassDriveSystem) repo.

---

## 0. Gear reference (from the model)

| Mesh | Teeth | Ratio | Purpose |
|---|---|---|---|
| Motor Gear → Drive Gear | 26 → 47 | **1.81 : 1** reduction | main drive motor → shell |
| S2S Motor Spur → Gantry Spur | 22 → 69 | **3.14 : 1** reduction | S2S motor tilts the gantry |
| GantryPot Gear → Pot Gear | 55 → 46 | 1 : 1.20 (pot overdriven) | gantry angle → S2S feedback pot |

The pot turning ~1.2° per gantry degree is why S2S positions are tuned in **pot
counts** (`potCenter` ≈ 1744–1774 on this build; swing 40° ≈ ±the counts the
firmware computes from `POT_COUNTS_PER_DEGREE`).

## 1. Sub-assembly A — Gantry (the structural core)

**Parts:** `Gantry`, `DriveGear Casing`, `DriveGear Casing(Mirror)`, `Speakers`

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

**Parts:** `Motor Gear (26T)`, `Drive Gear (47T)` + drive motor **[not modeled]**

1. Press/pin the **26T motor gear** onto the drive motor shaft
   (goBILDA-style hub — `1311 Thru-Hole Sonic Hub (6 mm D-bore)` is in your
   parts library if the motor is a 6 mm D-shaft gearmotor). **[VERIFY]** motor
   model — the library also holds `Motor DC - 90°`.
2. Seat the **47T drive gear** on the output shaft inside the casings; mesh with
   the 26T, set backlash (a strip of paper between teeth while tightening is a
   good default), close the mirror casing.
3. Spin by hand: one full output turn must feel even — no tight spots.

## 3. Sub-assembly C — S2S (steering) gears

**Parts:** `S2S Motor Spur Gear (22T)`, `Gantry Spur Gear (69T)` + S2S motor **[not modeled]**

1. 22T onto the S2S motor shaft, 69T onto the gantry pivot: the S2S motor
   rotates the whole gantry (and with it, the drive) side-to-side inside the
   frame — that lean is the steering.
2. Mesh, backlash, then confirm the gantry swings freely lock-to-lock by hand.
3. **[VERIFY]** S2S motor model + mount fasteners.

## 4. Sub-assembly D — Swing arms + position feedback

**Parts:** `S2SArm`, `FlyWheelArm`, `Pot Gear (46T)`, `GantryPot Gear (55T)` + 10 kΩ pot **[not modeled]**

1. Fit the **S2SArm** and **FlyWheelArm** to the gantry pivots.
2. Install the **55T GantryPot gear** on the gantry side and the **46T pot gear**
   on the potentiometer shaft; mesh them.
3. **Center rule:** with the gantry mechanically level, the pot must be near the
   middle of its travel — the firmware's boot calibration reads ~1744–1774
   counts here. If the pot rails (0 / 4095) anywhere in the swing, re-clock the
   pot gear a tooth at a time.
4. Wire the pot wiper to the drive ESP32 **GPIO34** (per the firmware), add
   100 nF wiper→GND at the connector (v10 note).

## 5. Sub-assembly E — Flywheel & ballast deck

**Parts:** `FlyWheel_Assembly` → `LazySusanBase`, `Ballast`, `BatteryHarness` (→ `Cable Management`)

1. Bolt the **LazySusanBase** (flywheel bearing base) to the bottom of the frame.
2. Add the **Ballast** mass and the **BatteryHarness**; route leads through the
   **Cable Management** part *before* closing anything — it's modeled as part of
   the harness for a reason.
3. The flywheel motor drives this deck's rotating mass **[VERIFY** motor + how
   the flywheel disc itself attaches — the disc isn't a separate part in v4.1**]**.
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

Not in the model (source them from the physical BOM): drive / S2S / flywheel /
dome-spin motors, 2 tilt servos, the 6-inch main bearing (`6inchBearing.step`
in the parts library), shell + magnets, all fasteners, electronics boards.

*Regenerate this tree after any Fusion revision:*
`python tools/step_tree.py "path	o\Z-Class Drive-vX.step"` — prints part counts
and hierarchy from any ASCII STEP export.*
