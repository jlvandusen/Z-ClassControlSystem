# JLCPCB assembly scope — what to let them solder vs do yourself (5 boards)

Fee rules from jlcpcb.com/help/article/pcb-assembly-price (updated 2026-08-20), Economic PCBA:
setup $8.18 + stencil $1.53 per order · SMT $0.0016/joint · through-hole $3.58/order + $0.0164/joint ·
**$3.07 per unique "extended" part** · basic parts $0. 4-layer 1.6 mm green is allowed in Economic; THT parts
do not force Standard. Part categories checked per C-number on jlcpcb.com/partdetail (2026-08-23).

**The bill is loading fees, not soldering.** 28 of the 38 BOM lines are extended → $86 of the assembly charge;
all 760 SMT joints on 5 boards cost $1.22. So the cheap scope is: JLC places every *basic* part (free) plus the
five extended parts that genuinely need a machine; you buy and solder the rest.

| Scope | JLC lines (extended) | JLC fees | JLC parts | **JLC assembly total** | you buy (LCSC prices) | you solder / board |
|---|---|---|---|---|---|---|
| A. full PCBA (as quoted) | 38 (28) | $110.6 | $54.7 | **$165** | — | nothing |
| B. SMD only | 25 (15) | $57.0 | $35.3 | **$92** | $19.5 | 21 THT parts (124 joints) |
| **C. machine-only (recommended)** | 15 (5) | $26.1 | $20.0 | **$46** | $34.8 | 31 parts: 21 THT + 5 electrolytic cans + D1, D6, F3, F4, L1 (144 joints) |
| D. bare minimum | 12 (5) | $26.0 | $16.1 | **$42** | $38.6 | 35 parts (adds U5, D2, C5/C6 — all basic, i.e. free at JLC: not worth it) |

Net: C cuts the JLC assembly+parts line by ≈ **$119** and adds ≈ $35 of parts + one LCSC/Digi-Key shipment
(≈ $10–20), so ≈ **$85–95 saved of the $300** — the 4-layer PCB, shipping and the setup/stencil floor ($9.71) don't
move. The quote total should land around $180–190.

## Upload set for scope C — `../ZDrive_v10_compact_JLCPCB_r7_machine-only.zip`

- gerbers (r7, unchanged) · `machine-only/bom_jlcpcb.csv` (15 lines) · `machine-only/cpl_jlcpcb.csv` (44 placements, top side)
- JLC places: Q1 SI7461DP (PowerPAK, bottom pad), U6 74AHCT125 + U7 74LVC245 (0.65 mm TSSOP), D4 BAT54S (SOT-23),
  J17 JST-SH (1 mm), U5 AMS1117, D2 SS14, C5/C6 1206 and all 0603 R/C (45 per board).
- In the order flow make sure it says **Economic** PCBA (the 4-layer/1.6 mm/green/HASL combo qualifies); if it
  shows Standard, setup+stencil jump from $9.71 to $33.77 and every line is charged $1.53.
- JLC will show the unassembled pads as empty — that is expected; it will also ask to confirm "parts not assembled".

## Your shopping list — `machine-only/hand_solder_bom.csv` (LCSC numbers, qty for 5 boards +10 %)

All 2.5 mm-pitch or bigger: XT60, fuse holder, 2×5 / 2×7 / 1×3 headers, 1×15 / 1×9 / 1×5 sockets, JST-XH/VH,
plus the five SMD electrolytic cans (3.5–4 mm pads), SMB/SMA/SOD-123 diodes, 1812 polyfuse, 1206 fuse and bead.
Order the same C-numbers from LCSC (one basket, ≈ $35 + shipping) or substitute freely — none of these are critical
parts except: F1 must be the Keystone 3568 (9.9 mm pitch), J1 the XT60PB-M *vertical*, C7 a 25 V low-ESR 10 mm can.

## Stock warnings (JLC assembly stock, 2026-08-23) — check again before you pay

- **Q1 SI7461DP C553968: 81 pcs** — critical. If it's gone, pick a JLC-stocked −60 V P-FET in PowerPAK/DFN 5×6 with
  ≥ 20 mΩ and pad order S S S G / D tab (e.g. AO4407A is SOIC-8 — different footprint; check the footprint first).
- U6 SN74AHCT125PWR C36365: 274 pcs. Alt: Nexperia 74AHCT125PW (same pinout).
- J_U2A/J_U2B socket C124417: 432 pcs (on your list now, not JLC's — cut a 1×15 C124408 at position 10 if out).
- C3 1000 µF/10 V C278397: 335 pcs · C2 470 µF/10 V C178530: 883 pcs (on your list — any 10 mm / 8 mm SMD can works).
- Minimum purchase quantities bill the minimum even if you use fewer: C25804 (10 k) min 20 — fine, 85 used.
