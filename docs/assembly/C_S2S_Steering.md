# Sub-Assembly C — S2S (steering) gears

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The side-to-side (S2S) gear stack that leans the whole gantry — and with it the drive — to steer. The worm drive holds the tilt without power (which is why the droid doesn't flop when disabled). Ratio (source §0): S2S motor spur → gantry spur = **3.14 : 1**.

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `S2S Motor Spur Gear (22T)` | 22-tooth spur on the worm-gearbox output | Onto the DC worm-gearbox output shaft; meshes the 69T |
| `Gantry Spur Gear (69T)` | 69-tooth sector/spur on the gantry pivot | On the gantry pivot; driven by the 22T to rotate the whole gantry |
| `Spur Gear (16T)` | 16-tooth spur, third gear in the S2S stack | **[VERIFY** what it meshes — idler or pot takeoff?**]** Place per the model **[VERIFY** its role — idler between stages or the pot-gear takeoff**]** |
| **DC worm-gear motor** | S2S drive motor (from SwingArms, Sub-Assembly D) | Its worm output carries the 22T; worm stage holds tilt without power |

## Tools & fasteners

- Fastener sizes for this stack are explicitly **`[VERIFY]`** in the source (§3). **[TODO: builder]** — record spur retention (setscrew/press), sizes, and thread-locker.
- Backlash: set mesh backlash on each pair as with the drive gears. **[TODO: builder]** — target backlash figure.

## Assembly steps

1. Fit the **22T onto the worm-gearbox output** and the **69T sector on the gantry pivot**: the motor rotates the whole gantry (and with it, the drive) side-to-side — that lean is the steering. *(22T → 69T = **3.14 : 1**, §0.)*
2. Place the **16T spur** per the model **[VERIFY** its role — idler between stages or the pot-gear takeoff**]**.
   - **[TODO: builder]** — confirm the 16T's mate and function against the physical stack, then document it.
3. Mesh, set backlash, then swing the gantry lock-to-lock by hand — note the worm stage will resist back-driving; drive it from the motor side to check.
   - **[TODO: builder]** — target backlash figure and the checked lock-to-lock swing angle.

## Per-part notes

### `S2S Motor Spur Gear (22T)`
Role: input spur on the worm-gearbox output. Mating: meshes the 69T gantry spur. Ratio: 22 → 69 = **3.14 : 1** (§0). Fitment/retention: **[TODO: builder]**.

### `Gantry Spur Gear (69T)`
Role: the sector/spur that turns the gantry pivot; leaning it steers the droid. Mating: driven by the 22T. Fitment: set backlash, then confirm a clean lock-to-lock swing.

### `Spur Gear (16T)`
Role: the third gear in the S2S stack — **[VERIFY]** whether it is an idler between stages or the pot-gear takeoff. This ambiguity is carried in both §0 and §3 of the source. **[TODO: builder]** — resolve and record what it meshes.

### DC worm-gear motor
Role: the S2S drive motor (manifest-assigned to SwingArms, Sub-Assembly D). Mating: worm output carries the 22T. Key property: the worm stage resists back-driving, so it holds the gantry tilt with power off — drive it from the motor side when checking the swing. Referenced here because it drives this gear stack.

## Verification

- Gantry swings lock-to-lock by hand with the meshed stack; motion is even with no tight spots.
- The worm stage clearly resists back-driving (hold-without-power confirmed) — verify motion by driving from the motor side.
- 16T role confirmed against the physical stack (**[VERIFY]** resolved).

## Photos

- **[TODO: photo]** — 22T on the worm-gearbox output.
- **[TODO: photo]** — 69T on the gantry pivot, meshed with the 22T.
- **[TODO: photo]** — the 16T spur in place (annotate what it meshes once resolved).
- **[TODO: photo]** — gantry at each end of its lock-to-lock swing.
