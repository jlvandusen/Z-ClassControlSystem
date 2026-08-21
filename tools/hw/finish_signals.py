#!/usr/bin/env python3
"""
finish_signals.py — close the remaining 2-pad SIGNAL nets with clash-checked
tracks (straight, or an L-path on either layer). Signal-only: never touches
plane nets, so no plane-crossing shorts. Run with KiCad's python.
  python finish_signals.py mainboard.kicad_pcb NET1 NET2 ...
If no nets given, auto-detects every net with exactly one open ratsnest.
"""
import sys, math, pcbnew
FMM = pcbnew.FromMM
PLANE = {"GND", "+5V_LOGIC"}

def seg_pt(ax, ay, bx, by, px, py):
    dx, dy = bx-ax, by-ay; L2 = dx*dx+dy*dy or 1
    t = max(0.0, min(1.0, ((px-ax)*dx+(py-ay)*dy)/L2))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

def seg_seg(a, b, c, d):   # min distance between two segments (endpoints sampled)
    return min(seg_pt(*a, *b, *c), seg_pt(*a, *b, *d), seg_pt(*c, *d, *a), seg_pt(*c, *d, *b))

def main():
    pcb = sys.argv[1]; want = set(sys.argv[2:])
    b = pcbnew.LoadBoard(pcb)
    # gather copper per layer: segments (with net) + pad discs (with net)
    segs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}; discs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c = t.GetPosition()
            for L in (pcbnew.F_Cu, pcbnew.B_Cu): discs[L].append((c.x, c.y, FMM(0.4), t.GetNetCode()))
        elif t.GetLayer() in segs:
            s, e = t.GetStart(), t.GetEnd(); segs[t.GetLayer()].append((s.x, s.y, e.x, e.y, t.GetWidth()/2, t.GetNetCode()))
    for fp in b.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition(); r = max(p.GetSizeX(), p.GetSizeY())/2; st = p.GetLayerSet().CuStack()
            for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                if L in st: discs[L].append((c.x, c.y, r, p.GetNetCode()))

    W = FMM(0.4); CLR = FMM(0.2)
    def track_ok(x0, y0, x1, y1, layer, nc):
        for (ax, ay, bx, by, hw, n) in segs[layer]:
            if n != nc and seg_seg((x0, y0), (x1, y1), (ax, ay), (bx, by)) < W/2 + hw + CLR: return False
        for (dx, dy, dr, n) in discs[layer]:
            if n != nc and seg_pt(x0, y0, x1, y1, dx, dy) < W/2 + dr + CLR: return False
        return True

    # net -> list of pad positions
    nets = {}
    for fp in b.GetFootprints():
        for p in fp.Pads():
            nn = p.GetNetname()
            if nn in PLANE or nn == "": continue
            nets.setdefault(nn, []).append((p.GetPosition(), p.GetNet(), p.GetLayerSet().CuStack()))
    # which nets to try
    b.BuildConnectivity()
    targets = want if want else set()
    if not targets:
        for nn, pads in nets.items():
            if len(pads) == 2: targets.add(nn)   # simple 2-pad nets

    def add_track(x0, y0, x1, y1, layer, net):
        t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(int(x0), int(y0))); t.SetEnd(pcbnew.VECTOR2I(int(x1), int(y1)))
        t.SetWidth(W); t.SetLayer(layer); t.SetNet(net); b.Add(t)
        segs[layer].append((x0, y0, x1, y1, W/2, net.GetNetCode()))

    def add_via(x, y, net):
        v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(int(x), int(y))); v.SetDrill(FMM(0.3)); v.SetWidth(FMM(0.6))
        v.SetNet(net); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
        for L in (pcbnew.F_Cu, pcbnew.B_Cu): discs[L].append((x, y, FMM(0.4), net.GetNetCode()))

    done = 0
    for nn in sorted(targets):
        pads = nets.get(nn, [])
        # find the two pads with an open ratsnest (use first two if 2-pad)
        if len(pads) < 2: continue
        (pa, net, _), (pb, _, _) = pads[0], (pads[1] if len(pads) > 1 else pads[0]),
        ax, ay = pa.x, pa.y; bx, by = pb.x, pb.y; nc = net.GetNetCode()
        placed = False
        # 1) straight on F then B
        for L in (pcbnew.F_Cu, pcbnew.B_Cu):
            if track_ok(ax, ay, bx, by, L, nc):
                add_track(ax, ay, bx, by, L, net); placed = True; break
        # 2) L-paths on F, then B (two elbow orientations)
        if not placed:
            for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                for ex, ey in ((bx, ay), (ax, by)):
                    if track_ok(ax, ay, ex, ey, L, nc) and track_ok(ex, ey, bx, by, L, nc):
                        add_track(ax, ay, ex, ey, L, net); add_track(ex, ey, bx, by, L, net); placed = True; break
                if placed: break
        # 3) two-layer L with a via at the elbow
        if not placed:
            for ex, ey in ((bx, ay), (ax, by)):
                if track_ok(ax, ay, ex, ey, pcbnew.F_Cu, nc) and track_ok(ex, ey, bx, by, pcbnew.B_Cu, nc):
                    add_track(ax, ay, ex, ey, pcbnew.F_Cu, net); add_via(ex, ey, net); add_track(ex, ey, bx, by, pcbnew.B_Cu, net); placed = True; break
        if placed: done += 1; print(f"  routed {nn}")
        else: print(f"  [could not route] {nn}")
    print(f"closed {done}/{len(targets)} signal nets")
    pcbnew.SaveBoard(pcb, b)

if __name__ == "__main__":
    main()
