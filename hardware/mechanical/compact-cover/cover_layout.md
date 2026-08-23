# Cover / casing layout — compact v10 board

Board frame: origin = axle centre, +x toward tail, +y up. Casing STL frame: Y = x + 5.6, Z = y + 160.9 (board plane = Y–Z, see ENVELOPE.md).
Mirror Y -> -Y if the empty-side casing is the mirror of OutsideGearCasing.stl.

Board extents (board frame): x -51.3 … 100.5 (151.8 mm), y -54.8 … 54.8 (109.6 mm). Axle cut-out Ø38 at origin.

## Mounting holes (Ø3.2 mm, M3) — put a Ø6 × 5 mm boss with a heat-set insert at each
| Hole | board x | board y | casing Y | casing Z |
|---|---|---|---|---|
| H1 | -42.00 | 24.00 | -36.40 | 184.90 |
| H2 | -42.00 | -24.00 | -36.40 | 136.90 |
| H3 | 27.00 | 37.00 | 32.60 | 197.90 |
| H4 | 27.00 | -37.00 | 32.60 | 123.90 |
| H5 | 70.00 | 0.00 | 75.60 | 160.90 |

## Tall parts (height above board top surface)
| Ref | what | height mm | board x-range | board y-range |
|---|---|---|---|---|
| J1 | XT60 battery | 15.5 | -26.6 … -10.1 | -44.4 … -35.1 |
| J2 | motor A ribbon | 14 | -6.1 … 7.6 | -35.1 … -28.9 |
| J3 | motor B ribbon | 14 | -6.2 … 7.6 | -26.8 … -20.6 |
| J_AMP | amp module | 14 | -0.8 … 18.1 | 27.2 … 33.4 |
| J_U1B | ESP32 DevKitC socket R (1x15) | 13.6 | 47.9 … 51.5 | -19.6 … 19.6 |
| J_U1A | ESP32 DevKitC socket L (1x15) | 13.6 | 22.5 … 26.1 | -19.6 … 19.6 |
| J_U2B | RP2350-Zero socket R (1x9) | 13.3 | -3.8 … -1.0 | 22.7 … 45.8 |
| J_U2C | RP2350-Zero socket end (1x5) | 13.3 | -16.5 … -3.5 | 21.4 … 24.2 |
| J_U2A | RP2350-Zero socket L (1x9) | 13.3 | -19.0 … -16.2 | 22.7 … 45.8 |
| U3 | DFPlayer / SD | 12.5 | 59.1 … 80.9 | -33.4 … -11.6 |
| J5 | servo R | 11 | 59.7 … 63.3 | -1.4 … 7.4 |
| J4 | servo L | 11 | 54.2 … 57.8 | -1.4 … 7.4 |
| J14 | spk R | 11 | 11.7 … 20.7 | 35.7 … 45.3 |
| J13 | spk L | 11 | 2.2 … 11.2 | 35.7 … 45.3 |
| C2 | 470u/10V | 10.5 | -37.3 … -26.7 | -29.4 … -20.6 |
| C3 | 1000u/10V | 10.5 | -32.3 … -19.7 | 20.3 … 31.2 |
| C1 | 220u/25V | 10.5 | 34.7 … 45.3 | -37.9 … -29.1 |
| C7 | 1000u/25V | 10.5 | -32.3 … -19.7 | 32.8 … 43.7 |
| C4 | 470u/16V | 10.5 | 47.2 … 57.8 | -37.9 … -29.1 |
| J10 | NeoPixel | 10 | 76.0 … 87.0 | 11.6 … 18.4 |
| J8 | encoder | 10 | 76.0 … 89.5 | 3.1 … 9.9 |
| J7 | S2S | 10 | 76.0 … 92.0 | -5.4 … 1.4 |
| J6 | IMU | 10 | 53.8 … 67.2 | 17.1 … 23.9 |
| J15 | E-stop | 10 | 75.8 … 84.2 | 20.1 … 26.9 |
| J9 | hall | 10 | 54.0 … 65.0 | 8.6 … 15.4 |
| J11 | slip ring 5V | 10 | 85.5 … 94.0 | -17.9 … -11.1 |
| PS2 | Pololu D24V25F6 6V 2.5A | 9 | -43.8 … -25.2 | -18.5 … 0.0 |
| PS1 | Pololu D24V50F5 5V 5A | 9 | -43.8 … -22.7 | 0.7 … 19.3 |

Budget (ENVELOPE.md): ~14 mm above the board in the teardrop zone, ~34 mm inside r 42.5 around the axle.
Over budget in the teardrop zone: J1 XT60 (15.5 body + ~20 mating plug -> cover cut-out / plug through the cover), a mini ATM fuse in F1 (~16) -> use low-profile APS/ATT mini fuses (11) or cut a window.

## Vents — suggested zones (VENT_ZONES layer)
- PS1: 5 V buck ~2.5 W
- PS2: 6 V buck ~1 W
- J_AMP: MAX9744 amp module 2-4 W
- Q1: RPP FET ~0.5-1.5 W
- U5: 3.3 V LDO ~0.5 W
Slots >= 3 mm wide print without supports; two fields on opposite sides of the cover help since the ball rotates.
