# v10 mainboard 3D model — for fit-checking in Fusion 360

| File | Board | Notes |
|---|---|---|
| **`v10_mainboard_compact_envelopes.step`** | **compact rev A — USE THIS for collision checks** | board + every library 3D model + **23 grey envelope boxes** for what KiCad has no model for: ESP32 DevKitC and RP2350-Zero sitting on their sockets, DFPlayer (socketed height), both Pololus, fuse holder + a *standard-height* mini fuse, the mated XT60 plug and its wire exit, IDC/ribbon over J2/J3/J_AMP, JST-XH/VH housings + wire exit over every JST, servo plugs on J4/J5. Origin = axle centre. |
| `v10_mainboard_compact.step` | compact rev A, parts only | same export without the envelopes (the modules/plugs are missing — do not use it to judge clearance) |
| `v10_mainboard.step` | extended (152 × 125 mm), pre-routing snapshot of 2026-08-21 | fallback variant only — superseded for fit checks |
| `v10_board_outline.dxf` | extended outline (2 mm inset from the casing wall) | — |
| `../compact-cover/cover_layout_board.dxf` / `_casing.dxf` | compact outline + holes + tall parts + vent zones | board frame and casing-STL frame respectively |

Origin (0,0) = the **axle centre**, matching `ENVELOPE.md` (KiCad +x = assembly +Y toward the tail,
KiCad +y up = assembly +Z; casing STL frame Y = x + 5.6, Z = y + 160.9).

Frames: STEP z = 0 is the **bottom** of the PCB; the top surface is z = 1.6; component and envelope heights are
measured from the top surface. Envelopes are named `ENV_<L>x<W>x<H>` (mm); the DevKit is the 28.3 × 52 × 5 box, the
Zero 18 × 24.7 × 4.5, the fuse 10.9 × 3.8 × 15.3 (a standard mini ATM — swap for an 11 mm low-profile if the cover
needs it), XT60 plug 16.6 × 9.3 × 8.5 with a 10 mm wire-exit box above it. Envelopes are deliberately worst-case.

Regenerate after a layout change (envelopes: `python tools/hw/fusion_model.py hardware/kicad/compact/mainboard.kicad_pcb hardware/mechanical/board-3d/v10_mainboard_compact_envelopes.step` — it attaches the boxes to a scratch copy, exports, and verifies every placement by parsing the STEP back). Parts-only:

```
kicad-cli pcb export step --user-origin "150x150mm" --subst-models --no-dnp --force ^
  -o hardware\mechanical\board-3d\v10_mainboard_compact.step hardware\kicad\compact\mainboard.kicad_pcb
```

Library models give the real shapes of the sockets, headers, JSTs, XT60 body, capacitors and ICs; the envelope
boxes stand in for what has no model. Heights above the board top: DevKit 13.5, Zero 13, DFPlayer 12.5 (socketed),
Pololus 9, fuse 16.3 (standard mini), XT60 plug 24 + wires to 34, IDC/ribbons 14, JST-XH housings 13, VH 14,
servo plugs 12 (table in `../compact-cover/cover_layout.md`).

## Fusion 360 workflow
1. In your gear-casing assembly: **Insert → Insert Mesh/STEP** → pick `v10_mainboard_compact_envelopes.step`. The envelopes come in as separate bodies named `ENV_…` under each connector/socket, so you can hide them or run Interference against the casing/cover directly.
2. Align: the board's Ø38 axle cut-out is concentric with the casing's bearing bore; the board plane is
   perpendicular to the axle (sits at the bottom of the ~18 mm teardrop cavity, components facing the cover).
3. Check: tall parts vs the cavity depth (~14 mm budget in the teardrop zone, ~34 mm inside r 42.5); connectors
   (J-refs) line up with the tail opening; the 5 mounting bosses H1–H5 land where `ENVELOPE.md` says.
4. Add the M3 bosses (Ø6 × 5 mm, heat-set inserts) to the *empty* casing at the ENVELOPE coordinates and test the
   screw fit; cut the XT60 window and the vent slots from `cover_layout_casing.dxf`.

This is fit-checking only — routing/DRC stays in KiCad (`hardware/kicad/compact/mainboard.kicad_pro`).
