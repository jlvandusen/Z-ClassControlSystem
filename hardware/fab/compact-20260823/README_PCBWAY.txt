Z-DRIVE v10.0 MAINBOARD (compact)  -  PCBWay fabrication package
==================================================================
Designer : James VanDusen   (github.com/jlvandusen/Z-ClassControlSystem)
Board    : BB-8 "Z-Class" droid controller mainboard, rev A, compact outline
Date     : 2026-08-22

UPLOAD gerbers.zip to the PCBWay order form. Everything else here is for reference / assembly.

BOARD SPEC
  Layers         : 4, 1 oz copper
  Stackup        : F.Cu   signal + GND pour
                   In1.Cu solid GND plane
                   In2.Cu +5V pour + some signal routing
                   B.Cu   signal + GND pour
  Material       : FR4 1.6 mm, Tg150 fine
  Outline        : teardrop ~152 x 110 mm, with a 38 mm dia centre cut-out (the droid axle passes through)
  Finish         : HASL lead-free or ENIG - either
  Mask / silk    : green / white (or your choice)
  Min trace/space: 0.25 / 0.15 mm      Min via: 0.6 mm pad / 0.3 mm drill
  Smallest drill : 0.30 mm (vias); largest 3.2 mm (M3 mounting holes)
  Copper to edge : 0.30 mm
  Drill files    : Excellon mm, plated; map in mainboard-drl_map.gbr
  Gerber job     : mainboard-job.gbrjob (layer order + stackup)

DESIGN STATUS
  Fully routed. KiCad 10 DRC: 0 errors. 15 warnings, all cosmetic (single-spoke thermal reliefs on a few
  pads; two silk lines clipped by pads). Two small isolated GND pour fragments are intentional / harmless.

ASSEMBLY (if quoting PCBA)
  cpl.csv  = pick-and-place, both sides (all SMD on top)
  bom.csv  = bill of materials with manufacturer part numbers (authoritative)
  DO NOT populate the socketed modules: U1 (ESP32 DevKit), U2 (RP2350-Zero), U3 (DFPlayer Mini),
  PS1/PS2 (Pololu regulators) - the customer fits these. Through-hole connectors (JST-XH/VH, pin
  headers, XT60) may be assembled or left for the customer.
  Notes: Q1 is a PowerPAK SO-8 (pads 1-3 source, 4 gate, tab drain). D2/D6 and D1 are polarised
  (cathode = pad 1). Electrolytics C1-C4, C7 polarised (+ marked on silk).

CONTACT
  Questions about the design: jlvandusen@gmail.com
