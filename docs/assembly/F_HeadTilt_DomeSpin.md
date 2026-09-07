# Sub-Assembly F — Head tilt & dome spin

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

**Purpose:** The mast at the top of the drive that spins the dome and tilts the head platform. The dome-spin motor's encoder closes the heading loop; two servos tilt the mast platform to keep the dome level.

## Parts in this area

| Part | What it is | Where it goes / mates to |
|---|---|---|
| `HeadTilt_Assembly` | The head-tilt / dome-spin assembly | Top of the drive |
| `Coupler` | The mast coupler that holds the mast bearings | Takes 2× 1-inch bearings; the mast rotates in these |
| 2× 1-inch bearing (`535051`) | 1-inch bearings | Press into the Coupler; the mast rotates in them. **Note:** same 535051 bearing also listed for the Gantry (Sub-Assembly A) — confirm total count. **[TODO: builder]** |
| `TiltMast` | The mast | Fits through the Coupler bearings; secured by the coupler pins |
| `TiltMotorCoupler` | Coupler joining the dome-spin motor to the mast | Joins dome-spin motor to the `TiltMast` |
| `CouplerPin` | Mast retaining pin | Secures the `TiltMast` in the Coupler |
| `CouplerPin (Mirror)` | Mirrored retaining pin | Secures the `TiltMast` opposite `CouplerPin` |
| Dome-spin motor + encoder | Dome heading drive (**not modeled**) | Coupled to the mast via `TiltMotorCoupler`; encoder = 840 counts/rev in firmware |
| 2× tilt servos | Head-tilt servos (**not modeled**) | Tilt the mast platform; body pins 11/12, neutral 70°/110° |

## Tools & fasteners

- Bearing press: two 1-inch bearings (`535051`) press into the Coupler. **[TODO: builder]** — press method / retention.
- Servo power: **power the servos from their own 6 V rail** — never the 5 V logic feed (bench-proven brownout).
- Pins, motor-coupler, and servo-mount fasteners: **not specified**; per source §3 fastener sizes carry a `[VERIFY]` flag. **[TODO: builder]**.

## Assembly steps

1. Press the **two 1-inch bearings** into the **Coupler** — the mast rotates in these.
   - **[TODO: builder]** — bearing press/retention detail; confirm 535051 count vs. the Gantry use.
2. Fit the **TiltMast** through; secure with **CouplerPin + mirrored pin**.
   - **[TODO: builder]** — pin size/retention.
3. **TiltMotorCoupler** joins the dome-spin motor to the mast. The motor's encoder (840 counts/rev in firmware) closes the dome-heading loop.
   - **[TODO: builder]** — dome-spin motor part and how the encoder mounts.
4. Attach the two tilt servos (body pins 11/12, neutral 70°/110°) so they tilt the mast platform. **Power servos from their own 6 V rail** — never the 5 V logic feed (bench-proven brownout).
   - **[TODO: builder]** — servo part, horn/linkage, and the 6 V rail wiring.
5. The dome's magnet carrier rides the top of the mast; the shell slides between dome and carrier. **[VERIFY]** magnet stack orientation.
   - **[TODO: builder]** — magnet stack (count/orientation) once verified.

## Per-part notes

### `HeadTilt_Assembly`
Role: the top-of-drive assembly that spins the dome and tilts the head. Contains the Coupler, mast, motor coupler, and pins.

### `Coupler`
Role: holds the two mast bearings. Mating: presses 2× 1-inch bearings; the TiltMast rotates in them. Fitment: **[TODO: builder]**.

### 2× 1-inch bearing (`535051`)
Role: let the mast rotate. Mating: pressed into the Coupler. **Cross-area note:** the same 535051 1-inch bearing is listed for the Gantry (Sub-Assembly A) in the navigator manifest — confirm the total count across both areas. **[TODO: builder]**.

### `TiltMast`
Role: the mast that carries the dome and the magnet carrier. Mating: passes through the Coupler bearings; retained by CouplerPin + mirror; driven by the dome-spin motor via TiltMotorCoupler.

### `TiltMotorCoupler`
Role: joins the dome-spin motor to the mast. Mating: dome-spin motor ↔ TiltMast.

### `CouplerPin` / `CouplerPin (Mirror)`
Role: retain the TiltMast in the Coupler. Fitment/size: **[TODO: builder]**.

### Dome-spin motor + encoder (not modeled)
Role: drives dome heading; the encoder (840 counts/rev in firmware) closes the heading loop. Mating: coupled to the mast via TiltMotorCoupler. **[TODO: builder]** — motor and encoder parts.

### 2× tilt servos (not modeled)
Role: tilt the mast platform to keep the dome level. Wiring: body pins 11/12, neutral 70°/110°; **power from their own 6 V rail** (never 5 V logic — bench-proven brownout). Behavior: dome-tilt counter-rotation is always-on. Wrong direction → `tilt invert x|y` then `tilt save`; too fast/jerky → lower `tilt slew`. **[TODO: builder]** — servo part and linkage.

### Dome magnet carrier / shell interface
Role: the dome's magnet carrier rides the top of the mast; the shell slides between dome and carrier. **[VERIFY]** magnet stack orientation. **[TODO: builder]** — magnet stack count/orientation once verified.

## Verification

- Mast rotates smoothly in the Coupler bearings and is retained by both pins.
- `bb8 monitor body` → `debug encoder`: spin the mast by hand and confirm the counts move (source §7).
- Tilt servos: `tilt show`, stick tilt; with the drive enabled, tip the frame — the dome servos should counter-rotate to stay level. Wrong way → `tilt invert x|y` then `tilt save`; too fast/jerky → lower `tilt slew` (source §7).
- `bb8 tune dome` once the drive runs.
- Magnet stack orientation confirmed (**[VERIFY]** resolved).

## Photos

- **[TODO: photo]** — 1-inch bearings pressed into the Coupler.
- **[TODO: photo]** — TiltMast through the Coupler with both CouplerPins fitted.
- **[TODO: photo]** — TiltMotorCoupler joining the dome-spin motor to the mast (encoder visible).
- **[TODO: photo]** — tilt servos mounted with the 6 V rail wiring.
- **[TODO: photo]** — dome magnet carrier on the mast top (shows stack orientation).
