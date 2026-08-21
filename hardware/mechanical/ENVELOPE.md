# Mainboard v10 envelope — inside the empty (mirror) gear casing

Derived from `OutsideGearCasing.stl`, `InsideGearCasing.stl` and `Gantry.stl`
(Z-ClassDriveSystem repo, assembly coordinates). The empty casing on the other side of the
gantry is the mirror of the gear casing, so its interior is this, mirrored in X.

## Geometry (assembly coordinates)

| Item | Value |
|---|---|
| Board plane | **Y–Z** (the casing's thin axis is X; the board stands on edge, perpendicular to the axle) |
| Casing wall | 3.0 mm |
| **Inner teardrop** | Y −47.7 … 108.0 (**155.8 mm**), Z 104.1 … 217.7 (**113.6 mm**) |
| Lobe (round end) | centre ≈ (Y 5.6, Z 160.9) = **the axle centre**, inner radius ≈ 53 mm |
| Tail | tapers to an opening at +Y (tip ≈ 102 mm from the axle centre); opening ≈ 45 mm tall — **harness exit** |
| Depth, full teardrop | X −62 … −80 = **18 mm** (between the frame-plate face and the internal shoulder) |
| Depth, lobe only | X −80 … −100: a further **20 mm** inside a **Ø 85 mm** (r 42.5) cylinder around the axle — where the big gear lives on the gear side; free height for us |
| Openings | the teardrop is a shroud: open toward the frame plate (X −62) and at the −Y flat end near that face (slot ≈ Z 123–200); tail open at +Y |

The yellow part in the render (cover with the round bore) is not in the three files I have; the
axle bore / bearing boss on the **empty** side is therefore unmeasured — see "to confirm".

## KiCad coordinate convention

Origin = **axle centre**. KiCad **+X = assembly +Y** (toward the tail), KiCad **+Y = assembly +Z** (up).
Files (mm, importable via File → Import → Graphics):

| File | Layer to import onto | Content |
|---|---|---|
| `hardware/kicad/casing_inner_wall.dxf` | User.Comments | exact inner wall contour |
| `hardware/kicad/casing_outer_wall.dxf` | User.Comments | outer wall (for the cover check) |
| `hardware/kicad/board_outline_draft.dxf` | **Edge.Cuts** | inner wall inset 1.5 mm (approximate radial offset — true-offset it in KiCad before fab) |

Board area ≈ 12 000 mm² (v9.15 mainboard: 7 400 mm²). Usable height above the board:
**≈ 14 mm** in the teardrop zone (18 − 1.6 board − ~2.5 standoff), **≈ 34 mm** inside r 42.5.

## Keep-outs (to confirm)
- **Axle bore:** modelled as Ø 38 (r 19) around the origin — sized from the ~25 mm bore seen in the
  side projection plus bearing-flange margin. **Confirm** the bore / boss on the empty side.
- Deeper-zone circle r 42.5 — only matters for parts taller than 14 mm (none planned, but the
  DevKit can be socketed there if wanted).

## First placement (all rectangles verified inside the outline, clear of the bore, no overlaps)

`placement_sketch.png` — red = casing inner wall, black = board outline, blue = r 19 keep-out and r 42.5 deep zone.

| Part | KiCad x | KiCad y | Note |
|---|---|---|---|
| ESP-WROOM-32 DevKit (28 × 52, vertical) | 22 … 50 | −26 … 26 | right of the bore, partly in the deep zone; USB faces the tail opening |
| RP2350-Zero (18 × 24) | 2 … 20 | 22 … 46 | |
| DFPlayer Mini (21 × 21) | −40 … −19 | 8 … 29 | |
| MAX9744 + caps (28 × 20) | −48 … −20 | −12 … 8 | thermal pad to the pour; cover acts as heatsink |
| Motor headers 2×5 A / B | −22 … −2 / 0 … 20 | −46 … −34 | ribbons exit downward |
| Pololu 5 V (25 × 28) | 54 … 79 | −14 … 14 | tail |
| Pololu 6 V (15 × 18) | 82 … 97 | −9 … 9 | tail tip |
| XT60 + 15 A fuse (24 × 14) | −24 … 0 | 30 … 44 | top of lobe |
| Slip ring / servos / speakers JSTs | 52 … 80 | 16 … 28 | upper tail edge → exit |
| IMU SH / S2S / encoder / hall JSTs | 52 … 80 | −30 … −18 | lower tail edge → exit |

Heights: DevKit **soldered on low-profile pins (≈ 7 mm)**, not socketed (13 mm) — unless placed
fully inside r 42.5. Pololu modules 10 mm ✓. MAX9744 electrolytic: SMD polymer (6.3 mm) ✓.

## Mounting — proposal
The casing has no internal bosses. Since you own the Fusion model: add **four M3 bosses (Ø 6,
5 mm tall, heat-set inserts)** to the *empty* casing print at the positions KiCad will fix
(draft: (−40, 36), (−40, −36), (60, 24), (60, −24) — outside every footprint above). The board
then screws to the casing, components facing the cover; the 3 mm wall keeps the outline margin.

## Confirmed (2026-08-21)
1. **Axle keep-out Ø 38 (r 19) — confirmed OK.**
2. **The cover is `OutsideGearCasing.stl` itself** — the part sliced above; the empty side gets the same
   cover, so the envelope is exact. Heat: add vent slots to the cover over the MAX9744 / bucks, or keep
   the buck inductors and 1000 µF caps ≥ 1 mm off the cover.
3. **Tail opening is free on both sides of the axle for the harness.** Depth could grow a little if
   needed but **width (X) must not** — the tilting servo bars would hit the cover. Height budget stays
   14 mm (teardrop) / 34 mm (lobe).
4. **PCB bosses in the empty casing: approved.** KiCad fixes the four positions; the casing model gets
   Ø 6 × 5 mm bosses with M3 heat-set inserts at those coordinates.
