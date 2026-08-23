Z-Drive v10 compact mainboard - rev A - JLCPCB order set (FINAL, 2026-08-23)
Ordered as JLCPCB W2026082400040087 (5 pcs, machine-only PCBA).

Upload, in this order, on jlcpcb.com:
  1. PCB:  ZDrive_v10_compact_GERBERS_r7.zip     (upload the zip AS-IS - flat gerbers + Excellon drill, KiCad page origin, no drill map)
             4-layer, 1.6 mm, 1 oz, green, HASL or ENIG. Pick Economic PCBA if offered.
  2. PCBA: ZDrive_v10_compact_BOM_r7_machine-only.csv   (15 lines - only the parts JLC solders)
  3. PCBA: ZDrive_v10_compact_CPL_r7_machine-only.csv   (44 placements, top side; rotations verified in JLC's preview
             2026-08-23 - U6/U7/Q1 +270, U5 +180, D4 +180 - nothing should need turning)
  JLC will warn that 31 footprints have no BOM/CPL entry: expected, those are hand-fitted (hand_solder_bom.csv).

hand_solder_bom.csv   parts YOU solder (LCSC numbers, qty for 5 boards +10%)
ASSEMBLY_SCOPE.md     why this split, cost model, stock warnings
Bring-up and polarity checks: hardware/ASSEMBLY.md in the repo. Full parts-to-order list: hardware/bom/v10_order_remaining.csv
Regenerate the CSVs: python tools/hw/jlc_outputs.py hardware/kicad/compact/mainboard.kicad_pcb hardware/bom/v10_mainboard_bom.csv <out> --scope smd --exclude C1,C2,C3,C4,C7,D1,D6,F3,F4,L1
