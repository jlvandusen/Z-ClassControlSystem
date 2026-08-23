#!/usr/bin/env python3
"""
cover_layout.py — export what the casing/cover designer needs from a routed board. KiCad python.

  python cover_layout.py <board.kicad_pcb> <outdir>

Writes, in BOTH coordinate frames (board: origin = axle centre, +x tail, +y up;
casing STL: Y = x + 5.6, Z = y + 160.9 — see hardware/mechanical/ENVELOPE.md):
  <outdir>/cover_layout_board.dxf / cover_layout_casing.dxf   layers:
     OUTLINE      board edge + axle cut-out
     HOLES_M3     the 5 mounting holes, dia 3.2
     BOSSES       dia 6 boss outline around each hole
     TALL_PARTS   rectangles of everything >= 8 mm tall, labelled with height
     CONNECTORS   rectangles + names of every connector (cable exits)
     VENT_ZONES   suggested vent rectangles over the heat sources
  <outdir>/cover_layout.md   dimension table
"""
import sys, os, math, pcbnew

# component height above the board top surface, mm (body + socket where socketed)
HEIGHTS = {
    "C1": 10.5, "C2": 10.5, "C3": 10.5, "C4": 10.5, "C7": 10.5,           # SMD electrolytics 8x10.2 / 10x10.2
    "J1": 15.5,                                                            # XT60PB vertical body (plug adds ~20)
    "F1": 7.5,                                                             # Keystone 3568 holder; a mini ATM fuse stands ~16 total
    "J_U1A": 13.6, "J_U1B": 13.6,                                          # 8.5 socket + DevKit PCB + module can
    "J_U2A": 13.3, "J_U2B": 13.3, "J_U2C": 13.3,                           # 8.5 socket + RP2350-Zero + USB-C
    "U3": 12.5,                                                            # DFPlayer in its socket
    "PS1": 9.0, "PS2": 9.0,                                                # Pololu modules (inductor/cap)
    "J2": 14.0, "J3": 14.0, "J_AMP": 14.0,                                 # pin header + IDC/ribbon or module above
    "J4": 11.0, "J5": 11.0,                                                # servo plug on a 1x3 header
    "J6": 10.0, "J7": 10.0, "J8": 10.0, "J9": 10.0, "J10": 10.0, "J11": 10.0, "J15": 10.0,   # JST-XH with housing
    "J13": 11.0, "J14": 11.0,                                              # JST-VH with housing
    "J17": 4.0, "U5": 1.8, "U6": 1.2, "U7": 1.2, "Q1": 1.1, "L1": 1.0, "F3": 1.5, "F4": 0.8,
}
CONNECTOR_NAMES = {"J1": "XT60 battery", "J2": "motor A ribbon", "J3": "motor B ribbon", "J4": "servo L", "J5": "servo R",
                   "J6": "IMU", "J7": "S2S", "J8": "encoder", "J9": "hall", "J10": "NeoPixel", "J11": "slip ring 5V",
                   "J13": "spk L", "J14": "spk R", "J15": "E-stop", "J17": "I2C", "J_AMP": "amp module", "U3": "DFPlayer / SD"}
HEAT = {"PS1": "5 V buck ~2.5 W", "PS2": "6 V buck ~1 W", "J_AMP": "MAX9744 amp module 2-4 W", "Q1": "RPP FET ~0.5-1.5 W", "U5": "3.3 V LDO ~0.5 W"}
AX, AY = 150.0, 150.0          # KiCad coords of the axle centre
OFF_Y, OFF_Z = 5.6, 160.9      # casing-frame offsets

def to_board(x, y):  return (x - AX, AY - y)           # KiCad mm -> board frame (y up)
def to_casing(bx, by): return (bx + OFF_Y, by + OFF_Z)

class DXF:
    def __init__(self): self.e = []
    def poly(self, layer, pts, closed=True):
        self.e += ["0", "LWPOLYLINE", "8", layer, "90", str(len(pts)), "70", "1" if closed else "0"]
        for x, y in pts: self.e += ["10", f"{x:.3f}", "20", f"{y:.3f}"]
    def circle(self, layer, x, y, r): self.e += ["0", "CIRCLE", "8", layer, "10", f"{x:.3f}", "20", f"{y:.3f}", "40", f"{r:.3f}"]
    def text(self, layer, x, y, h, s): self.e += ["0", "TEXT", "8", layer, "10", f"{x:.3f}", "20", f"{y:.3f}", "40", f"{h:.2f}", "1", s]
    def write(self, path):
        out = ["0", "SECTION", "2", "ENTITIES"] + self.e + ["0", "ENDSEC", "0", "EOF"]
        open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")

def main():
    pcb, outdir = sys.argv[1], sys.argv[2]; os.makedirs(outdir, exist_ok=True)
    b = pcbnew.LoadBoard(pcb)
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    # outline polygon + axle circle from Edge.Cuts
    outline = None; axle = None
    for d in b.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts: continue
        if d.GetShape() == pcbnew.SHAPE_T_POLY:
            o = d.GetPolyShape().Outline(0); outline = [(o.CPoint(i).x / 1e6, o.CPoint(i).y / 1e6) for i in range(o.PointCount())]
        elif d.GetShape() == pcbnew.SHAPE_T_CIRCLE:
            axle = (d.GetCenter().x / 1e6, d.GetCenter().y / 1e6, d.GetRadius() / 1e6)
    def bbox_mm(f):
        c = f.GetCourtyard(pcbnew.F_CrtYd); bb = c.BBox() if c.OutlineCount() else f.GetBoundingBox(False)
        return (bb.GetLeft() / 1e6, bb.GetTop() / 1e6, bb.GetRight() / 1e6, bb.GetBottom() / 1e6)
    rows = []
    for frame, conv in (("board", to_board), ("casing", lambda x, y: to_casing(*to_board(x, y)))):
        D = DXF()
        D.poly("OUTLINE", [conv(x, y) for x, y in outline])
        if axle: cx, cy = conv(axle[0], axle[1]); D.circle("OUTLINE", cx, cy, axle[2]); D.text("OUTLINE", cx - 6, cy - 1, 2.5, "AXLE")
        for r in sorted(fps):
            f = fps[r]
            if r.startswith("H"):
                p = f.GetPosition(); x, y = conv(p.x / 1e6, p.y / 1e6)
                D.circle("HOLES_M3", x, y, 1.6); D.circle("BOSSES", x, y, 3.0); D.text("HOLES_M3", x + 3.5, y - 1, 2.0, r)
                if frame == "casing": rows.append((r, *to_board(p.x / 1e6, p.y / 1e6), x, y))
            h = HEIGHTS.get(r, 0)
            l, t, rr, bt = bbox_mm(f); p1 = conv(l, t); p2 = conv(rr, bt)
            rect = [(min(p1[0], p2[0]), min(p1[1], p2[1])), (max(p1[0], p2[0]), min(p1[1], p2[1])), (max(p1[0], p2[0]), max(p1[1], p2[1])), (min(p1[0], p2[0]), max(p1[1], p2[1]))]
            if h >= 8.0:
                D.poly("TALL_PARTS", rect); D.text("TALL_PARTS", rect[0][0], rect[0][1] - 2.2, 1.6, f"{r} h{h:g}")
            if r in CONNECTOR_NAMES:
                D.poly("CONNECTORS", rect); D.text("CONNECTORS", rect[0][0], rect[2][1] + 0.6, 1.6, f"{r} {CONNECTOR_NAMES[r]}")
            if r in HEAT:
                cxm, cym = (rect[0][0] + rect[2][0]) / 2, (rect[0][1] + rect[2][1]) / 2
                D.poly("VENT_ZONES", [(cxm - 9, cym - 7), (cxm + 9, cym - 7), (cxm + 9, cym + 7), (cxm - 9, cym + 7)])
                D.text("VENT_ZONES", cxm - 9, cym + 7.6, 1.6, f"vent: {HEAT[r]}")
        D.write(os.path.join(outdir, f"cover_layout_{frame}.dxf"))
    # dimension table
    bx = [to_board(x, y) for x, y in outline]; xs = [p[0] for p in bx]; ys = [p[1] for p in bx]
    md = ["# Cover / casing layout — compact v10 board", "",
          f"Board frame: origin = axle centre, +x toward tail, +y up. Casing STL frame: Y = x + {OFF_Y}, Z = y + {OFF_Z} (board plane = Y–Z, see ENVELOPE.md).",
          "Mirror Y -> -Y if the empty-side casing is the mirror of OutsideGearCasing.stl.", "",
          f"Board extents (board frame): x {min(xs):.1f} … {max(xs):.1f} ({max(xs)-min(xs):.1f} mm), y {min(ys):.1f} … {max(ys):.1f} ({max(ys)-min(ys):.1f} mm). Axle cut-out Ø{2*axle[2]:.0f} at origin.", "",
          "## Mounting holes (Ø3.2 mm, M3) — put a Ø6 × 5 mm boss with a heat-set insert at each",
          "| Hole | board x | board y | casing Y | casing Z |", "|---|---|---|---|---|"]
    for r, bxv, byv, cy, cz in rows: md.append(f"| {r} | {bxv:.2f} | {byv:.2f} | {cy:.2f} | {cz:.2f} |")
    md += ["", "## Tall parts (height above board top surface)", "| Ref | what | height mm | board x-range | board y-range |", "|---|---|---|---|---|"]
    for r in sorted(fps, key=lambda r: -HEIGHTS.get(r, 0)):
        h = HEIGHTS.get(r, 0)
        if h < 8: continue
        l, t, rr, bt = bbox_mm(fps[r]); p1 = to_board(l, t); p2 = to_board(rr, bt)
        md.append(f"| {r} | {CONNECTOR_NAMES.get(r, fps[r].GetValue())} | {h:g} | {min(p1[0],p2[0]):.1f} … {max(p1[0],p2[0]):.1f} | {min(p1[1],p2[1]):.1f} … {max(p1[1],p2[1]):.1f} |")
    md += ["", "Budget (ENVELOPE.md): ~14 mm above the board in the teardrop zone, ~34 mm inside r 42.5 around the axle.",
           "Over budget in the teardrop zone: J1 XT60 (15.5 body + ~20 mating plug -> cover cut-out / plug through the cover), a mini ATM fuse in F1 (~16) -> use low-profile APS/ATT mini fuses (11) or cut a window.",
           "", "## Vents — suggested zones (VENT_ZONES layer)", "\n".join(f"- {r}: {v}" for r, v in HEAT.items()),
           "Slots >= 3 mm wide print without supports; two fields on opposite sides of the cover help since the ball rotates."]
    open(os.path.join(outdir, "cover_layout.md"), "w", encoding="utf-8").write("\n".join(md) + "\n")
    print("\n".join(md))

if __name__ == "__main__":
    main()
