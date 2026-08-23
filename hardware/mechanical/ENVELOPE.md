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

Board frame (the netlist frame used in every table below): origin = **axle centre**, **+x = assembly +Y**
(toward the tail), **+y = assembly +Z** (up). Casing STL frame: Y = x + 5.6, Z = y + 160.9. Inside the
`.kicad_pcb` files the axle centre sits at (150, 150) mm and KiCad y runs downward; the generator handles the flip.
Files (mm, importable via File → Import → Graphics):

| File | Layer to import onto | Content |
|---|---|---|
| `hardware/kicad/casing_inner_wall.dxf` | User.Comments | exact inner wall contour |
| `hardware/kicad/casing_outer_wall.dxf` | User.Comments | outer wall (for the cover check) |
| `hardware/kicad/compact/board_outline_draft.dxf` | **Edge.Cuts** | compact outline (151.8 × 109.6 mm, ordered rev A) — already on Edge.Cuts in `compact/mainboard.kicad_pcb` |
| `hardware/kicad/extended/board_outline_draft.dxf` | **Edge.Cuts** | extended outline (151.8 × 124.6 mm, casing extended 15 mm downward, unrouted) |

Board area ≈ 12 000 mm² (v9.15 mainboard: 7 400 mm²). Usable height above the board:
**≈ 14 mm** in the teardrop zone (18 − 1.6 board − ~2.5 standoff), **≈ 34 mm** inside r 42.5.

## Two board variants

| Variant | Folder | Outline (board frame) | Status |
|---|---|---|---|
| compact | `hardware/kicad/compact/` | x −51.3 … 100.5, y −54.8 … 54.8 (151.8 × 109.6 mm) — fits the original casing | hand-placed, routed, DRC 0 errors; **rev A ordered from JLCPCB 2026-08-23** (4-layer 1.6 mm FR4; fab package in `hardware/fab/compact-20260823/`) |
| extended | `hardware/kicad/extended/` | x −51.3 … 100.5, y −69.8 … 54.8 (151.8 × 124.6 mm) — needs the casing extended 15 mm downward, top edge unchanged | auto-placed, **unrouted**; not fabbed — fallback only |

Both are generated from the same `hardware/netlist/mainboard.py` and share H1–H5. Everything else in this
file (tables, vent zones, windows) describes the compact board — the one being built. Note
`hardware/bom/v10_mainboard_bom.csv` row PCB still lists both outlines (~152 × 125 extended / ~152 × 110 compact).

## Keep-outs (to confirm)
- **Axle bore:** modelled as Ø 38 (r 19) around the origin — sized from the ~25 mm bore seen in the
  side projection plus bearing-flange margin. **Confirm** the bore / boss on the empty side.
- **ESP32 antenna keep-out:** (22.5, 19) – (51.5, 33.5) board frame, no copper on any layer (the
  DevKit's antenna end points +y, toward H3).
- Deeper-zone circle r 42.5 — as built (compact rev A) the 14 mm parts J2/J3 (motor ribbons,
  x −6.2 … 7.6, y −35.1 … −20.6) and J_AMP (x −0.8 … 18.1, y 27.2 … 33.4) all lie inside r 42.5. The
  socketed ESP32 DevKitC (J_U1A/B, 13.6 mm, x 22.5 … 51.5, y ±19.6) and RP2350-Zero (J_U2A/B/C, 13.3 mm,
  x −19.0 … −1.0, y 21.4 … 45.8) reach outside r 42.5 and clear the 14 mm teardrop budget by < 1 mm —
  check the cover print. Over budget outside r 42.5: J1 XT60 (15.5 mm body + ~20 mm mating plug) and F1
  with a standard mini ATM fuse (~16 mm) — see "Cover windows" below.

## As-built placement — compact rev A (ordered 2026-08-23)

`placement_sketch.png` is the 2026-08-21 draft and is superseded. Current references:
`compact-cover/cover_layout_board.dxf` / `cover_layout_casing.dxf` (layers OUTLINE, HOLES_M3, BOSSES,
TALL_PARTS, CONNECTORS, VENT_ZONES), the dimension table in `compact-cover/cover_layout.md` (all generated
by `tools/hw/cover_layout.py`), and the renders in `hardware/fab/compact-20260823/`.
Extents: x −51.3 … 100.5, y −54.8 … 54.8; Ø 38 axle cut-out at the origin.

| Part | Board x | Board y | Note |
|---|---|---|---|
| ESP32 DevKitC 30-pin, socketed in J_U1A/J_U1B (1×15 sockets, 13.6 mm) | 22.5 … 51.5 | −19.6 … 19.6 (pin rows) | right of the bore, partly in the deep zone; antenna end toward H3 (+y, copper keep-out to y 33.5), USB end toward −y — not toward the tail |
| RP2350-Zero, socketed in J_U2A/J_U2B/J_U2C (1×9 + 1×9 + 1×5 sockets, 13.3 mm) | −19.0 … −1.0 | 21.4 … 45.8 | left of the bore, upper lobe |
| DFPlayer Mini U3 in its socket (12.5 mm) | 59.1 … 80.9 | −33.4 … −11.6 | lower tail; SD slot faces +x (card keep-out in the silk) so the card is inserted/removed through the tail opening |
| J_AMP 2×7 header (14 mm incl. plug) for the off-board MAX9744 amp module (rev A: module on a harness; rev B integrates the QFN) | −0.8 … 18.1 | 27.2 … 33.4 | no amp IC / thermal pad on the board; C7 1000 µF/25 V (10.5 mm) at x −32.3 … −19.7, y 32.8 … 43.7. Where the amp module itself is strapped: unknown / not fixed by the board |
| Motor headers J2 (DRIVE ch A odd pins + S2S ch B) / J3 (FLYWHEEL A + DOME B), bare 2×5, rows along x, 14 mm | −6.1 … 7.6 / −6.2 … 7.6 | −35.1 … −28.9 / −26.8 … −20.6 | stacked below the bore inside r 42.5; ribbons exit downward/tail |
| PS1 Pololu D24V50F5 5 V 5 A (9 mm) | −43.8 … −22.7 | 0.7 … 19.3 | left of the bore (not the tail) |
| PS2 Pololu D24V25F6 6.0 V 2.5 A servo rail (9 mm) | −43.8 … −25.2 | −18.5 … 0.0 | left of the bore, below PS1 |
| J1 XT60PB-M vertical (15.5 mm body; single 12 V pack input, pack lead = XT60 female) | −26.6 … −10.1 | −44.4 … −35.1 | **bottom** of the lobe; needs a cover window (see below) |
| F1 Keystone 3568 mini blade holder + **10 A** ATM fuse (holder 7.5 mm, ~16 mm with a standard mini fuse) | −9.5 … 7.5 (courtyard, centre −1) | −45.5 … −37.5 (centre −41.5) | bottom of the lobe, right of the XT60 |
| Servos J4 / J5 (1×3, 11 mm) | 54.2 … 57.8 / 59.7 … 63.3 | −1.4 … 7.4 | tail, on the axle line |
| Speakers J13 / J14 (JST-VH, 11 mm) | 2.2 … 11.2 / 11.7 … 20.7 | 35.7 … 45.3 | **top of the lobe**, not the tail |
| Slip ring J11 (JST-XH 2-pin: +5V_LED / GND only, 10 mm) | 85.5 … 94.0 | −17.9 … −11.1 | tail tip, lower edge |
| Body NeoPixel J10 / E-stop J15 / Qwiic J17 (I2C-B, RP2350) | 76.0 … 87.0 / 75.8 … 84.2 / centre (57.6, 29.6) | 11.6 … 18.4 / 20.1 … 26.9 / — | upper tail edge → exit |
| IMU J6 (JST-XH 4-pin, GY-BMI160 over I2C-A, 10 mm) | 53.8 … 67.2 | 17.1 … 23.9 | upper tail |
| Hall J9 (XH 3-pin) | 54.0 … 65.0 | 8.6 … 15.4 | tail |
| Encoder J8 (XH 4-pin, B A GND 5V) | 76.0 … 89.5 | 3.1 … 9.9 | tail tip |
| S2S J7 (XH 5-pin, also carries I2C-A for the AS5600) | 76.0 … 92.0 | −5.4 … 1.4 | tail tip |

Heights (above the board top, from `cover_layout.md`): ESP32 DevKitC **socketed** (J_U1A/B 8.5 mm sockets
+ module = 13.6 mm) and RP2350-Zero socketed (13.3 mm) — both fit the 14 mm teardrop budget with < 1 mm to
spare; motor headers J2/J3 and J_AMP 14 mm (inside r 42.5); DFPlayer in socket 12.5 mm; JST-VH 11 mm,
JST-XH 10 mm; Pololu modules 9 mm; SMD electrolytics C1–C4, C7 (8×10 / 10×10.2 cans) 10.5 mm — C7 is a
1000 µF/25 V 10 mm can, not a 6.3 mm polymer. Over budget: J1 XT60 15.5 mm (+ ~20 mm plug) and F1 with a
standard mini ATM fuse (~16 mm).

## Cover windows (compact)

- **XT60 window:** J1 XT60PB-M vertical at board x −26.6 … −10.1, y −44.4 … −35.1 (casing Y −21.0 … −4.5,
  Z 116.5 … 125.8), bottom of the lobe, outside r 42.5. Body 15.5 mm + mating XT60 female plug ~20 mm →
  cut a window in the cover so the pack lead plugs through it (or leave the plug standing in a pocket).
  This is the board's only power input (12 V 3S4P pack → F1 10 A → Q1 Si7461DP RPP → VIN); there is no
  charge connector/path on the board — the pack charges through its own lead.
- **Fuse:** F1 Keystone 3568 mini blade holder centred at (−1, −41.5), courtyard x −9.5 … 7.5,
  y −45.5 … −37.5 (casing Y −3.9 … 13.1, Z 115.4 … 123.4), 9.9 mm pin pitch, **10 A ATM** fuse (not 15 A).
  Holder alone 7.5 mm; a standard mini ATM fuse stands ~16 mm → over the 14 mm teardrop budget (it is
  outside r 42.5, ~46 mm from the axle). Fit a low-profile mini (APS/ATT, ~11 mm) or extend the XT60 window
  to cover the fuse as well, so the fuse can be swapped without removing the cover.

## Vent zones (compact)

From `compact-cover/cover_layout_board.dxf`, layer VENT_ZONES (18 × 14 mm rectangles centred on each
heat source, board frame):

| Zone | centre (x, y) | rectangle x / y | Source |
|---|---|---|---|
| PS1 5 V buck ~2.5 W | (−33.3, 10.0) | −42.3 … −24.3 / 3.0 … 17.0 | Pololu D24V50F5 |
| PS2 6 V buck ~1 W | (−34.5, −9.3) | −43.5 … −25.5 / −16.3 … −2.3 | Pololu D24V25F6 |
| J_AMP amp module 2–4 W | (8.7, 30.3) | −0.3 … 17.7 / 23.3 … 37.3 | header only — the MAX9744 module itself is off-board |
| Q1 RPP FET ~0.5–1.5 W | (12.2, −45.6) | 3.2 … 21.2 / −52.6 … −38.6 | Si7461DP PowerPAK SO-8 (bottom edge of the lobe) |
| U5 3.3 V LDO ~0.5 W | (66.8, 29.3) | 57.8 … 75.8 / 22.3 … 36.3 | AMS1117-3.3 SOT-223 (upper tail) |

Slots ≥ 3 mm wide print without supports; put two vent fields on opposite sides of the cover because the
ball rotates. Casing-frame copies of the same rectangles are in `cover_layout_casing.dxf`
(Y = x + 5.6, Z = y + 160.9).

## Tail / harness exit (compact)

Connectors in the tail (x > 52), all JST-XH/SH or 0.1 in headers, 10–11 mm tall: J4/J5 servos
(x 54 … 63, y −1 … 7), J6 IMU (GY-BMI160, I2C-A, x 54 … 67, y 17 … 24), J9 hall (x 54 … 65, y 9 … 15),
J17 Qwiic I2C-B (centre 57.6, 29.6), J10 body NeoPixel (x 76 … 87, y 12 … 18), J15 E-stop
(x 76 … 84, y 20 … 27), J8 dome encoder (x 76 … 89.5, y 3 … 10), J7 S2S/AS5600 (x 76 … 92, y −5 … 1),
J11 slip ring +5V_LED/GND (x 85.5 … 94, y −18 … −11), U3 DFPlayer with the SD slot facing +x
(x 59 … 81, y −33 … −12). The two motor ribbons (J2/J3) leave from below the bore, the speakers (J13/J14)
from the top of the lobe, and the XT60 from the bottom-left — not all harness exits are at the tail.

## Mounting — proposal
The casing has no internal bosses. Add **five M3 bosses (Ø 6, 5 mm tall, heat-set inserts)** to the
*empty* casing print at the H1–H5 positions in the table below (fixed in the netlist and identical in both
board variants; 3.2 mm plated holes on the board). The board screws to the casing, components facing the
cover; the 3 mm wall keeps the outline margin.

## Confirmed (2026-08-21)
1. **Axle keep-out Ø 38 (r 19) — confirmed OK.**
2. **The cover is `OutsideGearCasing.stl` itself** — the part sliced above; the empty side gets the same
   cover, so the envelope is exact. Heat: the MAX9744 is not on the board (rev A uses an off-board amp
   module on J_AMP). Vent the cover over the VENT_ZONES in `compact-cover/cover_layout_board.dxf` (listed
   under "Vent zones" above), and keep the Pololu inductors and the 10.5 mm electrolytics ≥ 1 mm off the cover.
3. **Tail opening is free on both sides of the axle for the harness.** Depth could grow a little if
   needed but **width (X) must not** — the tilting servo bars would hit the cover. Height budget stays
   14 mm (teardrop) / 34 mm (lobe).
4. **PCB bosses in the empty casing: approved.** The netlist fixes five positions (H1–H5, table below);
   the casing model gets Ø 6 × 5 mm bosses with M3 heat-set inserts at those coordinates.

## PCB boss positions for the empty-casing model (as built — compact rev A, 2026-08-23; identical in the extended variant)

Bosses: Ø 6 mm, 5 mm tall, M3 heat-set insert, on the casing's inner face at X ≈ −62 (the frame-plate
side), i.e. the board sits at the bottom of the 18 mm teardrop zone with components facing the cover.

| Boss | Board (x,y) mm, origin = axle | Casing STL frame Y, Z (mm) | Note |
|---|---|---|---|
| H1 | (−42, 24) | Y −36.40, Z 184.90 | lobe, upper left |
| H2 | (−42, −24) | Y −36.40, Z 136.90 | lobe, lower left |
| H3 | (27, 37) | Y 32.60, Z 197.90 | lobe, upper right |
| H4 | (27, −37) | Y 32.60, Z 123.90 | lobe, lower right |
| H5 | (70, 0) | Y 75.60, Z 160.90 | mid-tail, between the servo headers (J4/J5) and J7/J8 (supports the tail) |

Holes H1–H5 are 5× M3 plated (3.2 mm); the full cover/boss kit (`cover_layout_casing.dxf`,
`cover_layout_board.dxf`, `cover_layout.md`) lives in `hardware/mechanical/compact-cover/`.

Mirror Y → −Y if the empty casing is the mirror image of `OutsideGearCasing.stl` about the axle.

Note: `hardware/mechanical/board-3d/README.md` points Fusion users at the H1–H5 coordinates in this file,
so this table is the one to trust (H5 was at (95, 0) in the 2026-08-21 draft). Whether `v10_mainboard.step`
was exported from the compact or the extended layout is not recorded — unknown, verify before fit-checking
(the compact outline is 109.6 mm tall, the extended 124.6 mm).
