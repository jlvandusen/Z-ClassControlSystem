# v10 mainboard 3D model — for fit-checking in Fusion 360

| File | Board | Notes |
|---|---|---|
| `v10_mainboard_compact.step` | **compact (152 × 110 mm) — the built rev A board** | KiCad 10 STEP export, 2026-08-23, origin = axle centre, library 3D models, no copper |
| `v10_mainboard.step` | extended (152 × 125 mm), pre-routing snapshot of 2026-08-21 | fallback variant only — superseded for fit checks |
| `v10_board_outline.dxf` | extended outline (2 mm inset from the casing wall) | — |
| `../compact-cover/cover_layout_board.dxf` / `_casing.dxf` | compact outline + holes + tall parts + vent zones | board frame and casing-STL frame respectively |

Origin (0,0) = the **axle centre**, matching `ENVELOPE.md` (KiCad +x = assembly +Y toward the tail,
KiCad +y up = assembly +Z; casing STL frame Y = x + 5.6, Z = y + 160.9).

Regenerate after a layout change:

```
kicad-cli pcb export step --user-origin "150x150mm" --subst-models --no-dnp --force ^
  -o hardware\mechanical\board-3d\v10_mainboard_compact.step hardware\kicad\compact\mainboard.kicad_pcb
```

Heights in the STEP come from the library models: socketed modules (DevKit, RP2350-Zero, DFPlayer) and the
Pololu bucks are **not** in the model — add ~13.6 / 13.3 / 12.5 / 9 mm bodies over J_U1x / J_U2x / U3 / PS1–PS2
(table in `../compact-cover/cover_layout.md`). J1's mating XT60 plug adds ~20 mm above the 15.5 mm body.

## Fusion 360 workflow
1. In your gear-casing assembly: **Insert → Insert Mesh/STEP** → pick `v10_mainboard_compact.step`.
2. Align: the board's Ø38 axle cut-out is concentric with the casing's bearing bore; the board plane is
   perpendicular to the axle (sits at the bottom of the ~18 mm teardrop cavity, components facing the cover).
3. Check: tall parts vs the cavity depth (~14 mm budget in the teardrop zone, ~34 mm inside r 42.5); connectors
   (J-refs) line up with the tail opening; the 5 mounting bosses H1–H5 land where `ENVELOPE.md` says.
4. Add the M3 bosses (Ø6 × 5 mm, heat-set inserts) to the *empty* casing at the ENVELOPE coordinates and test the
   screw fit; cut the XT60 window and the vent slots from `cover_layout_casing.dxf`.

This is fit-checking only — routing/DRC stays in KiCad (`hardware/kicad/compact/mainboard.kicad_pro`).
