#!/usr/bin/env python3
"""
finish.py — post-route finishing for the mainboard, run with KiCad's python:
  import routed SES -> +5V plane vias (keepout+copper aware) -> GND stitching
  vias (F/B pours <-> In1 plane) -> refill -> report remaining opens.

  & "<kicad>/bin/python.exe" tools/hw/finish.py hardware/kicad/extended/mainboard.kicad_pcb hardware/kicad/extended/out/mainboard.ses
"""
import sys, math, pcbnew
FMM = pcbnew.FromMM

def seg_pt(ax, ay, bx, by, px, py):
    dx, dy = bx - ax, by - ay; L2 = dx*dx + dy*dy or 1
    t = max(0.0, min(1.0, ((px-ax)*dx + (py-ay)*dy)/L2))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

def build_copper(b):
    segs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}; discs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c = t.GetPosition()
            for lyr in (pcbnew.F_Cu, pcbnew.B_Cu): discs[lyr].append((c.x, c.y, FMM(0.4), t.GetNetCode()))
        elif t.GetLayer() in segs:
            s, e = t.GetStart(), t.GetEnd()
            segs[t.GetLayer()].append((s.x, s.y, e.x, e.y, t.GetWidth()/2, t.GetNetCode()))
    for fp in b.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition(); r = max(p.GetSizeX(), p.GetSizeY())/2; st = p.GetLayerSet().CuStack()
            for lyr in (pcbnew.F_Cu, pcbnew.B_Cu):
                if lyr in st: discs[lyr].append((c.x, c.y, r, p.GetNetCode()))
    return segs, discs

def forbidden_fn(b):
    keep = []
    for z in b.Zones():
        if z.GetIsRuleArea():
            bb = z.GetBoundingBox(); keep.append((bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
    edge = b.GetBoardEdgesBoundingBox()
    def f(x, y):
        for (l, t, r, bt) in keep:
            if l <= x <= r and t <= y <= bt: return True
        return not (edge.GetLeft()+FMM(2) < x < edge.GetRight()-FMM(2) and edge.GetTop()+FMM(2) < y < edge.GetBottom()-FMM(2))
    return f

def clash_fn(segs, discs, dia, clr):
    rr = dia/2
    def c(x, y, nc):
        for lyr in (pcbnew.F_Cu, pcbnew.B_Cu):
            for (ax, ay, bx, by, hw, n) in segs[lyr]:
                if n != nc and seg_pt(ax, ay, bx, by, x, y) < rr + hw + clr: return True
            for (dx, dy, dr, n) in discs[lyr]:
                if n != nc and math.hypot(x-dx, y-dy) < rr + dr + clr: return True
        return False
    return c

def add_via(b, x, y, net, dia=FMM(0.6), drill=FMM(0.3)):
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
    v.SetDrill(drill); v.SetWidth(dia); v.SetNet(net); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
    return v

def main():
    pcb, ses = sys.argv[1], sys.argv[2]
    b = pcbnew.LoadBoard(pcb)
    print("import SES:", pcbnew.ImportSpecctraSES(b, ses))
    forb = forbidden_fn(b)

    # --- +5V_LOGIC SMD pads -> In2 plane via (GND rides the F/B pours) ---
    segs, discs = build_copper(b); clash = clash_fn(segs, discs, FMM(0.6), FMM(0.25))
    v5 = b.FindNet("+5V_LOGIC"); added5 = 0
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() != "+5V_LOGIC" or p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH: continue
            c = p.GetPosition(); nc = p.GetNetCode(); best = None
            for r in (FMM(0.9), FMM(1.2), FMM(1.5), FMM(1.9), FMM(2.4)):
                for a in range(0, 360, 20):
                    x = c.x + int(r*math.cos(math.radians(a))); y = c.y + int(r*math.sin(math.radians(a)))
                    if not forb(x, y) and not clash(x, y, nc): best = (x, y); break
                if best: break
            if not best: print(f"  [skip5] {fp.GetReference()}"); continue
            add_via(b, *best, p.GetNet())
            tr = pcbnew.PCB_TRACK(b); tr.SetStart(c); tr.SetEnd(pcbnew.VECTOR2I(*best)); tr.SetWidth(FMM(0.4))
            tr.SetLayer(pcbnew.F_Cu); tr.SetNet(p.GetNet()); b.Add(tr)
            discs[pcbnew.F_Cu].append((best[0], best[1], FMM(0.4), nc)); discs[pcbnew.B_Cu].append((best[0], best[1], FMM(0.4), nc)); added5 += 1
    print("+5V plane vias:", added5)

    # --- GND stitching: tie F/B pours to In1 on a grid, clear of copper/keepout ---
    segs, discs = build_copper(b); clash = clash_fn(segs, discs, FMM(0.8), FMM(0.3))
    gnd = b.FindNet("GND"); nc = gnd.GetNetCode(); edge = b.GetBoardEdgesBoundingBox()
    P = FMM(7.0); n = 0; yy = edge.GetTop()
    while yy < edge.GetBottom():
        xx = edge.GetLeft()
        while xx < edge.GetRight():
            if not forb(xx, yy) and not clash(xx, yy, nc):
                add_via(b, xx, yy, gnd, FMM(0.8), FMM(0.4))
                discs[pcbnew.F_Cu].append((xx, yy, FMM(0.4), nc)); discs[pcbnew.B_Cu].append((xx, yy, FMM(0.4), nc)); n += 1
            xx += P
        yy += P
    print("GND stitching vias:", n)

    pcbnew.SaveBoard(pcb, b)
    # refill + connectivity report
    b2 = pcbnew.LoadBoard(pcb); pcbnew.ZONE_FILLER(b2).Fill(b2.Zones()); pcbnew.SaveBoard(pcb, b2)
    b2 = pcbnew.LoadBoard(pcb); b2.BuildConnectivity(); conn = b2.GetConnectivity()
    import collections; opens = collections.Counter()
    for i in range(1, b2.GetNetCount()):
        rn = conn.GetRatsnestForNet(i)
        if rn:
            cnt = len(list(rn))
            if cnt: opens[b2.GetNetInfo().GetNetItem(i).GetNetname()] += cnt
    print("REMAINING OPEN NETS:", dict(opens) if opens else "NONE - fully connected")

if __name__ == "__main__":
    main()
