# Sub-Assembly B — Drive gear train

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The gear + chain stage that turns motor torque into shell drive — the "chain-driven main drive." Two ratios matter here (source §0): the **1.81 : 1** motor→drive gear stage and the ≈ **4.2 : 1** chain stage to the shell.

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `Motor Gear (26T)` | 26-tooth pinion on the motor shaft | Presses/pins onto the ROBOTZONE gearmotor shaft; meshes the 47T |
| `Drive Gear (47T)` | 47-tooth driven gear | Seats on the output shaft inside the casings; meshes the 26T |
| **ROBOTZONE planetary gearmotor** | Main drive motor (assigned to Gantry, Sub-Assembly A) | Its shaft carries the 26T; mounted to the gantry via goBILDA mounts |
| Sprocket `3307-1006-0010` (pinion) | Chain pinion (assigned to Gantry, Sub-Assembly A) | Rides the gear-stage output; drives the chain |
| Sprocket `3310-0032-0042` (plate) | Large plate sprocket (assigned to Gantry, Sub-Assembly A) | Driven by the chain; turns the shell wheel |
| `1311 Thru-Hole Sonic Hub, 6 mm D-bore` | Shaft hub in the parts library (candidate for the 6 mm D-shaft) | Couples the 26T motor gear to the ROBOTZONE 6 mm D-bore shaft. **[TODO: builder]** — confirm this hub is used |
| Chain | The roller chain for the chain stage (**not modeled** in v4.1) | Wraps the `3307` pinion and `3310-0032-0042` plate sprockets. **[TODO: builder]** — pitch/length |
| goBILDA mounts `1302-0032-1000` / `1400-0032-0032` | Motor mounts (assigned to Gantry, Sub-Assembly A) | Mount the ROBOTZONE motor to the gantry |

## Tools & fasteners

- Backlash technique (from source): a strip of paper between the teeth while tightening is a good default for setting mesh.
- Motor-gear retention (press vs. pin): the `1311 Thru-Hole Sonic Hub, 6 mm D-bore` is in the parts library for exactly this 6 mm D-shaft. **[TODO: builder]** — confirm press-fit vs. hub-and-setscrew and record setscrew size/thread-locker.
- Chain, sprocket, and casing fasteners: **not specified** — per source §3 fastener sizes carry a `[VERIFY]` flag. **[TODO: builder]**.

## Assembly steps

1. Press/pin the **26T motor gear** onto the **ROBOTZONE planetary gearmotor** shaft (`1311 Thru-Hole Sonic Hub, 6 mm D-bore` is in the parts library for exactly this kind of shaft). Mount the motor to the gantry via the goBILDA `1302-0032-1000` / `1400-0032-0032` mount assemblies.
   - **[TODO: builder]** — exact motor-gear retention method and setscrew/thread-locker spec.
2. Seat the **47T drive gear** on the output shaft inside the casings; mesh with the 26T, set backlash (a strip of paper between teeth while tightening is a good default), close the mirror casing. *(This 26T→47T stage is the **1.81 : 1** ratio from §0.)*
   - **[TODO: builder]** — target backlash figure / paper-shim thickness.
3. **Chain stage:** the `3307` pinion sprocket rides the gear-stage output; the big `3310-0032-0042` plate sprocket drives the shell wheel. Fit the chain, tension so it deflects a few mm at mid-span — a tight chain eats the planetary's bearings, a loose one skips under torque. **[VERIFY]** chain pitch/length and tensioner arrangement. *(This chain stage is the ≈ **4.2 : 1** ratio from §0.)*
   - **[TODO: builder]** — resolved chain pitch/length, tensioner design, and the target mid-span deflection number.
4. Spin by hand: one full output turn must feel even — no tight spots, chain quiet in both directions.

## Per-part notes

### `Motor Gear (26T)`
Role: the 26-tooth pinion on the motor shaft. Mating: press/pin to the ROBOTZONE 6 mm D-bore shaft (candidate hub `1311 Thru-Hole Sonic Hub, 6 mm D-bore`); meshes the 47T. Ratio: 26 → 47 = **1.81 : 1** (§0).

### `Drive Gear (47T)`
Role: the 47-tooth driven gear on the output shaft. Mating: meshes the 26T inside the casings and carries the chain pinion output. Fitment: set backlash with a paper shim before closing the mirror casing.

### ROBOTZONE planetary gearmotor
Role: main drive motor (manifest-assigned to Gantry, Sub-Assembly A). Mating: shaft carries the 26T; body bolts to the gantry through the goBILDA mounts. Referenced here because the gear-train build hangs off its shaft.

### Sprockets `3307-1006-0010` (pinion) / `3310-0032-0042` (plate)
Role: the chain stage. The pinion rides the gear-stage output; the plate sprocket drives the shell wheel. Fitment: tension the chain to a few mm mid-span deflection — too tight loads the planetary bearings, too loose skips under torque. **[VERIFY]** chain pitch/length and tensioner arrangement.

### `1311 Thru-Hole Sonic Hub, 6 mm D-bore`
Role: parts-library hub suited to the ROBOTZONE 6 mm D-shaft; likely couples the 26T to the motor shaft. **[TODO: builder]** — confirm it is used and record the setscrew spec.

### Chain
Role: the roller chain linking pinion to plate sprocket. Not modeled in v4.1. **[TODO: builder]** — pitch, length, and master-link type.

## Verification

- One full output turn by hand feels even — no tight spots.
- Chain is quiet in both directions and deflects a few mm at mid-span (not tight, not skipping).
- 26T↔47T mesh holds the set backlash after the mirror casing is closed.

## Photos

- **[TODO: photo]** — 26T pressed/pinned on the motor shaft (shows retention method).
- **[TODO: photo]** — 47T meshed with 26T, paper shim in place for backlash.
- **[TODO: photo]** — chain routed over both sprockets with the tensioner.
- **[TODO: photo]** — closed casing, hand-spin check.
