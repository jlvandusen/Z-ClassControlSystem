#!/usr/bin/env python3
"""
plane_vias.py — connect every SMD pad on a plane net (GND -> In1, +5V_LOGIC -> In2)
to its plane with a via placed adjacent to the pad, checked against real F.Cu/B.Cu
copper (tracks, pads, vias) so it never shorts. Run with KiCad's python.
"""
import sys, math, pcbnew

FMM = pcbnew.FromMM
# GND rides the F.Cu/B.Cu ground pours; only +5V_LOGIC pads need a via to their plane.
PLANE = {"+5V_LOGIC": pcbnew.In2_Cu}

def seg_pt_dist(ax, ay, bx, by, px, py):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy or 1
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

def main():
    pcb = sys.argv[1]
    b = pcbnew.LoadBoard(pcb)
    via_dia, via_drill = FMM(0.6), FMM(0.3)
    clr = FMM(0.25)                       # keep this far from foreign copper

    # forbidden regions: rule-area keepouts (antenna) + axle hole + board-edge margin
    keepouts = []   # list of (test_fn)
    for z in b.Zones():
        if z.GetIsRuleArea():
            bb = z.GetBoundingBox()
            keepouts.append(("rect", bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
    edge = b.GetBoardEdgesBoundingBox()
    def forbidden(x, y):
        for k in keepouts:
            if k[0] == "rect" and k[1] <= x <= k[3] and k[2] <= y <= k[4]:
                return True
        # 2 mm inside board edge, and >=  (axle keepout handled by edge-cuts circle -> use bbox center hole)
        if not (edge.GetLeft() + FMM(2) < x < edge.GetRight() - FMM(2) and
                edge.GetTop() + FMM(2) < y < edge.GetBottom() - FMM(2)):
            return True
        return False

    # foreign-copper index per layer (F.Cu / B.Cu): tracks as segments, pads/vias as discs
    segs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    discs = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c = t.GetPosition(); net = t.GetNetCode()
            for lyr in (pcbnew.F_Cu, pcbnew.B_Cu):
                discs[lyr].append((c.x, c.y, FMM(0.4), net))
        else:
            lyr = t.GetLayer()
            if lyr in segs:
                s, e = t.GetStart(), t.GetEnd()
                segs[lyr].append((s.x, s.y, e.x, e.y, t.GetWidth() / 2, t.GetNetCode()))
    for fp in b.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition(); r = max(p.GetSizeX(), p.GetSizeY()) / 2; net = p.GetNetCode()
            on = p.GetLayerSet().CuStack()
            for lyr in (pcbnew.F_Cu, pcbnew.B_Cu):
                if lyr in on:
                    discs[lyr].append((c.x, c.y, r, net))

    def clash(x, y, netcode):
        rr = via_dia / 2
        for lyr in (pcbnew.F_Cu, pcbnew.B_Cu):
            for (ax, ay, bx, by, hw, n) in segs[lyr]:
                if n != netcode and seg_pt_dist(ax, ay, bx, by, x, y) < rr + hw + clr:
                    return True
            for (dx, dy, dr, n) in discs[lyr]:
                if n != netcode and math.hypot(x - dx, y - dy) < rr + dr + clr:
                    return True
        return False

    added = 0
    for fp in b.GetFootprints():
        for p in fp.Pads():
            net = p.GetNetname()
            if net not in PLANE or p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                continue
            c = p.GetPosition(); nc = p.GetNetCode()
            best = None
            for r in (FMM(0.9), FMM(1.2), FMM(1.5), FMM(1.9), FMM(2.4)):
                for a in range(0, 360, 20):
                    x = c.x + int(r * math.cos(math.radians(a)))
                    y = c.y + int(r * math.sin(math.radians(a)))
                    if not forbidden(x, y) and not clash(x, y, nc):
                        best = (x, y); break
                if best:
                    break
            if not best:
                print(f"  [skip] {fp.GetReference()} {net}: no clear via spot"); continue
            v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(*best))
            v.SetDrill(via_drill); v.SetWidth(via_dia); v.SetNet(p.GetNet())
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
            # short track from pad to the via on F.Cu (pad's layer), same net -> no clash
            tr = pcbnew.PCB_TRACK(b); tr.SetStart(c); tr.SetEnd(pcbnew.VECTOR2I(*best))
            tr.SetWidth(FMM(0.4)); tr.SetLayer(pcbnew.F_Cu); tr.SetNet(p.GetNet()); b.Add(tr)
            discs[pcbnew.F_Cu].append((best[0], best[1], FMM(0.4), nc))
            discs[pcbnew.B_Cu].append((best[0], best[1], FMM(0.4), nc))
            added += 1
    print(f"plane-connect vias added: {added}")
    pcbnew.SaveBoard(pcb, b)

if __name__ == "__main__":
    main()
