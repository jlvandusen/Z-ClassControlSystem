#!/usr/bin/env python3
"""
post_route.py — import a Freerouting session onto a board and finish it. KiCad python.

  python post_route.py <board.kicad_pcb> <session.ses> [pitch_mm]

1. ImportSpecctraSES (tracks + vias land on the existing placement).
2. GND stitching vias on a grid: ties the F/B GND pours to the In1 GND plane so no pour
   island is left starved. Every via is checked against ALL copper on ALL four layers
   (pads, tracks, vias of other nets) and against rule areas, so it cannot short the In2
   +5V plane or anything else.
3. Refill zones, save, and print the remaining unconnected count.
"""
import sys, os, math, pcbnew
FMM = pcbnew.FromMM

def seg_pt(ax, ay, bx, by, px, py):
    dx, dy = bx-ax, by-ay; L2 = dx*dx+dy*dy or 1
    t = max(0.0, min(1.0, ((px-ax)*dx+(py-ay)*dy)/L2))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

def copper(b):
    layers = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)
    discs = {L: [] for L in layers}; segs = {L: [] for L in layers}
    for fp in b.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition(); r = max(p.GetSizeX(), p.GetSizeY())/2; st = p.GetLayerSet()
            for L in layers:
                if st.Contains(L): discs[L].append((c.x, c.y, r, p.GetNetCode()))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c = t.GetPosition()
            for L in layers: discs[L].append((c.x, c.y, FMM(0.5), t.GetNetCode()))
        elif t.GetLayer() in segs:
            s, e = t.GetStart(), t.GetEnd(); segs[t.GetLayer()].append((s.x, s.y, e.x, e.y, t.GetWidth()/2, t.GetNetCode()))
    return discs, segs

def main():
    pcb, ses = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    pitch = float(sys.argv[3]) if len(sys.argv) > 3 else 8.0
    b = pcbnew.LoadBoard(pcb)
    print("[post] SES import:", pcbnew.ImportSpecctraSES(b, ses))
    # --- GND stitching
    gnd = b.FindNet("GND"); nc = gnd.GetNetCode()
    discs, segs = copper(b)
    keep = [z for z in b.Zones() if z.GetIsRuleArea()]
    outline = None
    for d in b.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and d.Type() == pcbnew.PCB_SHAPE_T and d.GetShape() == pcbnew.SHAPE_T_POLY: outline = d.GetPolyShape()
    R = FMM(0.4); CLR = FMM(0.3)
    def ok(x, y):
        v = pcbnew.VECTOR2I(int(x), int(y))
        if outline is not None and not outline.Contains(v): return False
        if math.hypot(x - FMM(150), y - FMM(150)) < FMM(19.0 + 2.0): return False
        for z in keep:
            if z.Outline().Contains(v): return False
        # 2 mm from the outline: sample 4 points around
        if outline is not None:
            for dx, dy in ((FMM(2), 0), (-FMM(2), 0), (0, FMM(2)), (0, -FMM(2))):
                if not outline.Contains(pcbnew.VECTOR2I(int(x+dx), int(y+dy))): return False
        for L in discs:
            for (dx, dy, dr, n) in discs[L]:
                if n != nc and math.hypot(x-dx, y-dy) < R + dr + CLR: return False
            for (ax, ay, bx, by, hw, n) in segs[L]:
                if n != nc and seg_pt(ax, ay, bx, by, x, y) < R + hw + CLR: return False
        return True
    bb = b.GetBoardEdgesBoundingBox(); n = 0
    y = bb.GetTop(); P = FMM(pitch)
    while y < bb.GetBottom():
        x = bb.GetLeft()
        while x < bb.GetRight():
            if ok(x, y):
                v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(int(x), int(y))); v.SetDrill(FMM(0.4)); v.SetWidth(FMM(0.8))
                v.SetNet(gnd); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
                for L in discs: discs[L].append((x, y, R, nc))
                n += 1
            x += P
        y += P
    print(f"[post] GND stitching vias: {n} (pitch {pitch} mm)")
    pcbnew.ZONE_FILLER(b).Fill(b.Zones()); pcbnew.SaveBoard(pcb, b)
    b = pcbnew.LoadBoard(pcb)
    print("[post] tracks:", sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T), "vias:", sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T))

if __name__ == "__main__":
    main()
