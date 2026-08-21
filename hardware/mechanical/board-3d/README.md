# v10 mainboard 3D model — for fit-checking in Fusion 360

- `v10_mainboard.step` — the board + components as a 3D solid (KiCad STEP export).
  Board origin (0,0) = the **axle centre**, matching `ENVELOPE.md` (KiCad +x = assembly +Y toward the tail, +y = assembly +Z up).
- `v10_board_outline.dxf` — just the 2D outline (2 mm inset from the casing wall).

## Fusion 360 workflow
1. In your gear-casing assembly: **Insert → Insert Mesh/STEP** → pick `v10_mainboard.step`.
2. Align: the board's cylindrical axle cut-out is concentric with the casing's bearing bore; the board plane is perpendicular to the axle (sits at the bottom of the ~18 mm teardrop cavity, components facing the cover).
3. Check: tallest parts (Pololu bucks ~10 mm, electrolytics ~10 mm) vs the cavity depth; connectors (J-refs) line up with the tail opening; the 5 mounting bosses (H1–H5) land where §ENVELOPE says.
4. Add the M3 bosses to the *empty* casing at the ENVELOPE coordinates and test the screw fit.

This is fit-checking only — routing/DRC finishing stays in KiCad (`mainboard.kicad_pro`).
