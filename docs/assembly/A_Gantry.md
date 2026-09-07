# Sub-Assembly A — Gantry (the structural core)

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The gantry is the structural core of the drive. Everything else hangs off it — the drive gear train and its casings, the speakers, the S2S pivot, the swing arms, and the pot feedback. Build this first and true.

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `Gantry` | The main structural frame of the drive | Structural core; carries the drive motor, casings, S2S pivot, swing arms, pot |
| `DriveGear Casing` | One half of the mirrored casing pair that closes around the drive gear train | Bolts to the gantry; locates the drive-train shafts |
| `DriveGear Casing (Mirror)` | The mirror-image second casing half | Closes over the drive gear train opposite `DriveGear Casing` |
| `Speakers` | Speaker pair for droid sound | Mount in the gantry pockets, wired to the body amp |
| **ROBOTZONE planetary gearmotor** | The main drive motor | Mounts to the gantry via the goBILDA mounts; carries the 26T motor gear (see Sub-Assembly B) |
| Sprocket `3307-1006-0010` | Chain pinion sprocket (goBILDA) | Rides the gear-stage output; drives the chain to the shell (steps in Sub-Assembly B) |
| Sprocket `3310-0032-0042` | Large plate sprocket (goBILDA) | Driven by the chain; turns the shell wheel (steps in Sub-Assembly B) |
| `1InchBearing_535051` | 1-inch bearing | Gantry bearing per the navigator manifest. **Note:** the same 535051 1-inch bearing is used 2× in the Head-Tilt Coupler (Sub-Assembly F) — confirm count/location for this build. **[TODO: builder]** |
| goBILDA mount `1302-0032-1000` | Motor mount assembly | Mounts the ROBOTZONE gearmotor to the gantry |
| goBILDA mount `1400-0032-0032` | Motor mount assembly | Mounts the ROBOTZONE gearmotor to the gantry |

## Tools & fasteners

- Casing and mount fasteners: **not specified in the source.** Per §3 of the source, fastener sizes carry a `[VERIFY]` flag project-wide. **[TODO: builder]** — record exact screw sizes, lengths, and any thread-locker used.
- 3D-printed references for fit: `hardware/mechanical/InsideGearCasing.stl`, `OutsideGearCasing.stl`, `Gantry.stl`.

## Assembly steps

1. Fit the two **DriveGear Casings** (mirrored pair) to the gantry — they close around the drive gear train and locate its shafts. 3D-printed references: `hardware/mechanical/InsideGearCasing.stl`, `OutsideGearCasing.stl`, `Gantry.stl`.
2. Mount the **speaker pair** in their gantry pockets *before the casings block access*. Wire each speaker to the body's amp with a **standard 2-pin JST per speaker** (the v10 note formalizes this).
3. **[VERIFY]** casing fastener sizes and whether the casings sandwich the gantry or bolt to one face.
   - **[TODO: builder]** — record the resolved fastener size/length and the sandwich-vs-single-face decision once confirmed against the physical parts.
4. Mount the **ROBOTZONE planetary gearmotor** to the gantry using the goBILDA `1302-0032-1000` / `1400-0032-0032` mount assemblies. (The motor's 26T gear and the drive/chain stages are built in Sub-Assembly B.)
   - **[TODO: builder]** — motor-mount fastener spec and any alignment shims.

## Per-part notes

### `Gantry`
Role: structural core of the whole drive. Everything mounts to it — build square and true because every downstream mesh and the free-swing depend on it.

### `DriveGear Casing` / `DriveGear Casing (Mirror)`
Role: a mirrored pair that closes around the drive gear train and locates its shafts. Fitment: close the mirror casing only after the 26T/47T mesh and backlash are set (Sub-Assembly B). **[VERIFY]** whether the pair sandwiches the gantry or bolts to one face; fastener size unknown — **[TODO: builder]**.

### `Speakers`
Role: droid audio. Mating: seat in the gantry pockets and wire to the body amp with a standard 2-pin JST per speaker. Fitment: install *before* the casings, which otherwise block access.

### ROBOTZONE planetary gearmotor
Role: the main drive motor. Mating: bolts to the gantry through the goBILDA `1302-0032-1000` / `1400-0032-0032` mounts; drives the 26T motor gear. See §0 — the motor gear stage is **1.81 : 1** into the 47T drive gear, then the chain stage is ≈ **4.2 : 1** to the shell.

### Sprocket `3307-1006-0010` (pinion) and `3310-0032-0042` (plate)
Role: the chain stage from the gear-train output to the shell drive. The `3307` pinion rides the gear-stage output; the `3310-0032-0042` plate sprocket drives the shell wheel. Chain-fitting and tensioning steps live in Sub-Assembly B. Ratio ≈ **4.2 : 1** (per goBILDA numbering, ~10 → ~42 teeth).

### `1InchBearing_535051`
Role: 1-inch bearing assigned to the Gantry in the navigator manifest. **Cross-area note:** the same 535051 1-inch bearing appears 2× in the Head-Tilt Coupler (Sub-Assembly F). Confirm how many this build uses here vs. there. **[TODO: builder]** — bearing count and seat location for the gantry.

### goBILDA mounts `1302-0032-1000` and `1400-0032-0032`
Role: the motor-mount assemblies that fix the ROBOTZONE gearmotor to the gantry. Fitment/fasteners: **[TODO: builder]**.

## Verification

- Casings seat flush and the drive-train shafts are located with no bind (final backlash check is in Sub-Assembly B).
- Speakers are captive in their pockets and their JST leads reach the body amp before the casings are closed.
- Gantry is square/true — recheck free swing later *with motors meshed* (integration order, source §7).

## Photos

- **[TODO: photo]** — bare gantry, both faces, before any parts.
- **[TODO: photo]** — speaker seated in its gantry pocket with JST lead routed.
- **[TODO: photo]** — casing pair fitted to the gantry (shows sandwich-vs-single-face).
- **[TODO: photo]** — ROBOTZONE gearmotor on its goBILDA mounts.
