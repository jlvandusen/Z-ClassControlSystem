# Sub-Assembly E — Flywheel & ballast deck

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The rotating mass and ballast at the bottom of the frame. This deck *is* the pendulum that makes balance work — keep the mass low and centered.

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `FlyWheel_Assembly` | The flywheel deck assembly | Bottom of the frame; rotating mass driven by the ActoBotics planetary |
| `LazySusanBase` | Flywheel bearing base | Bolts to the bottom of the frame; carries the 6-inch bearing the deck rotates on |
| **6-inch lazy-susan bearing** | The turntable bearing the deck rotates on (identified — the model's `6inchBearing.step`) | Between `LazySusanBase` (bottom plate → frame) and the deck (top plate); ~6"×6" square steel plates + ball ring. Vendor/PN **[TODO: builder]** |
| `Ballast` | Ballast mass | Added to the deck to lower/center mass |
| `BatteryHarness` | Battery harness | Routes battery leads; modeled with Cable Management |
| `Cable Management` | Cable-routing part (modeled as part of the harness) | Leads route through it *before* closing anything |
| **ActoBotics planetary gearmotor** | Flywheel drive motor (in the FlyWheelArm, Sub-Assembly D) | Drives this deck's rotating mass — **[VERIFY** how the disc/deck couples to it — not explicit in v4.1**]** |

## Tools & fasteners

- `LazySusanBase` + 6-inch lazy-susan bearing mounting, and ballast fasteners: **not specified.** **[TODO: builder]** — record sizes and any thread-locker.
- Battery/power spec: v10 target is a **3S pack, 20 A BMS**. **[TODO: builder]** — exact pack, connector, and BMS part.

## Assembly steps

1. Bolt the **LazySusanBase** (flywheel bearing base) to the bottom of the frame, and mount the **6-inch lazy-susan turntable bearing** to it — the bearing's bottom (fixed) plate to the base/frame, its top (rotating) plate carrying the deck. This is the model's `6inchBearing.step`: a ~6"×6" square steel turntable bearing (commodity part, ~$3–8; any smooth 6" one rated for the deck weight works).
   - **[TODO: builder]** — mounting fastener spec + the exact bearing vendor/PN once chosen.
2. Add the **Ballast** mass and the **BatteryHarness**; route leads through the **Cable Management** part *before* closing anything — it's modeled as part of the harness for a reason.
   - **[TODO: builder]** — ballast mass/quantity and a routing photo of the leads through Cable Management.
3. The **ActoBotics planetary** (in the FlyWheelArm, Sub-Assembly D) drives this deck's rotating mass **[VERIFY** how the disc/deck couples to it — not explicit in v4.1**]**.
   - **[TODO: builder]** — resolve and document the deck-to-motor coupling.
4. Battery per the power spec (v10 target: 3S pack, 20 A BMS). Keep the mass low and centered — this deck *is* the pendulum that makes balance work.
   - **[TODO: builder]** — battery pack, BMS, and how the pack is retained.

## Per-part notes

### `FlyWheel_Assembly`
Role: the rotating-mass deck. Mating: rides the LazySusanBase; driven by the ActoBotics planetary. Function: the pendulum mass that makes balance work — mass must sit low and centered.

### `LazySusanBase`
Role: flywheel bearing base — it carries the 6-inch lazy-susan turntable bearing. Mating: bolts to the bottom of the frame; the deck rotates on the bearing's top plate. Fitment: **[TODO: builder]**.

### 6-inch lazy-susan bearing (the model's `6inchBearing.step`)
Role: the turntable bearing the flywheel/ballast deck rotates on. Construction: two square ~6"×6" steel plates with a ring of ball bearings between; the top plate spins freely on the bottom, a bolt-hole in each corner. Mating: bottom (fixed) plate fixes to the `LazySusanBase`/frame, top (rotating) plate carries the deck. It's a commodity hardware part (rotating-shelf / turntable bearing, ~$3–8) — pick a smooth-spinning 6" one rated for the deck's weight. **[TODO: builder]** — exact vendor / part number. *(Identified from the model's `6inchBearing.step`, which the CAD didn't assign to a sub-assembly; placed here as the deck bearing.)*

### `Ballast`
Role: added mass to lower and center the pendulum. Fitment: **[TODO: builder]** — mass/quantity.

### `BatteryHarness`
Role: carries the battery leads. Mating: routes through the Cable Management part. Note: battery per the power spec (v10: 3S, 20 A BMS).

### `Cable Management`
Role: the cable-routing part, modeled as part of the harness deliberately. Fitment: route all leads through it *before* closing anything.

### ActoBotics planetary gearmotor
Role: drives this deck's rotating mass (the motor itself lives in the FlyWheelArm, Sub-Assembly D). **[VERIFY]** how the disc/deck couples to it — not explicit in v4.1. **[TODO: builder]** — document the coupling.

## Verification

- The deck rotates freely on the LazySusanBase.
- Ballast is low and centered — the pendulum feels balanced (this is what makes balance work).
- All battery/harness leads are routed through Cable Management *before* anything is closed up.
- Deck-to-motor coupling confirmed against the physical build (**[VERIFY]** resolved).

## Photos

- **[TODO: photo]** — LazySusanBase bolted to the frame bottom.
- **[TODO: photo]** — ballast in place, showing low/centered mass.
- **[TODO: photo]** — battery leads routed through Cable Management before closing.
- **[TODO: photo]** — the ActoBotics-to-deck coupling (once resolved).
