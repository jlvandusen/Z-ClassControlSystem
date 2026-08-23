#!/usr/bin/env python3
"""
prep_route.py — prepare a placed (unrouted) board for Freerouting so the last few percent
don't need GUI work. Run with KiCad's bundled python.

  python prep_route.py <board.kicad_pcb>

1. Axle keep-out: a rule area disc (r = AXLE_R + RING) on all copper layers, no tracks/vias,
   so the router never hugs the centre cut-out (edge-clearance violations).
2. +5V_LOGIC plane fan-out: every SMD pad on +5V_LOGIC gets a via to the In2 plane placed
   clash-free next to it with a short F.Cu stub. THT pads reach the plane by themselves.
3. Export the Specctra DSN next to the board (out/mainboard.dsn).
"""
import sys, os, math, pcbnew
FMM = pcbnew.FromMM
AXLE = (150.0, 150.0); AXLE_R = 19.0; RING = 0.8

def seg_pt(ax, ay, bx, by, px, py):
    dx, dy = bx-ax, by-ay; L2 = dx*dx+dy*dy or 1
    t = max(0.0, min(1.0, ((px-ax)*dx+(py-ay)*dy)/L2))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

def copper(b):
    """every copper item per layer: (x, y, r, net) discs + (x0,y0,x1,y1,hw,net) segs"""
    discs = {L: [] for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)}
    segs = {L: [] for L in discs}
    for fp in b.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition(); r = max(p.GetSizeX(), p.GetSizeY())/2
            st = p.GetLayerSet()
            for L in discs:
                if st.Contains(L): discs[L].append((c.x, c.y, r, p.GetNetCode()))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            c = t.GetPosition()
            for L in discs: discs[L].append((c.x, c.y, FMM(0.45), t.GetNetCode()))
        elif t.GetLayer() in segs:
            s, e = t.GetStart(), t.GetEnd(); segs[t.GetLayer()].append((s.x, s.y, e.x, e.y, t.GetWidth()/2, t.GetNetCode()))
    return discs, segs

def main():
    pcb = os.path.abspath(sys.argv[1]); b = pcbnew.LoadBoard(pcb)
    route_5v = "--route-5v" in sys.argv   # route +5V_LOGIC as a net: no fan-out vias, and hide the In2 plane from the DSN
    # ---- 1. axle keep-out disc
    have = any(z.GetIsRuleArea() and z.GetZoneName() == "axle keep-out" for z in b.Zones())
    if not have:
        z = pcbnew.ZONE(b); z.SetIsRuleArea(True); z.SetZoneName("axle keep-out")
        z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowZoneFills(False)
        z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET();
        for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu): ls.AddLayer(L)
        z.SetLayerSet(ls)
        pts = pcbnew.VECTOR_VECTOR2I()
        R = AXLE_R + RING
        for i in range(48):
            a = 2*math.pi*i/48; pts.append(pcbnew.VECTOR2I(FMM(AXLE[0] + R*math.cos(a)), FMM(AXLE[1] + R*math.sin(a))))
        z.AddPolygon(pts); b.Add(z); print(f"[prep] axle keep-out disc r={R} mm added")
    # ---- 2. +5V plane fan-out vias
    net5 = b.FindNet("+5V_LOGIC"); nc5 = net5.GetNetCode()
    discs, segs = copper(b)
    keep = [z for z in b.Zones() if z.GetIsRuleArea()]
    def forbidden(x, y):
        for z in keep:
            if z.Outline().Contains(pcbnew.VECTOR2I(int(x), int(y))): return True
        return False
    def clash(x, y, nc, rr=FMM(0.3), clr=FMM(0.25)):
        for L in discs:
            for (dx, dy, dr, n) in discs[L]:
                if n != nc and math.hypot(x-dx, y-dy) < rr + dr + clr: return True
            for (ax, ay, bx, by, hw, n) in segs[L]:
                if n != nc and seg_pt(ax, ay, bx, by, x, y) < rr + hw + clr: return True
        return False
    added = 0; skipped = []
    for fp in ([] if route_5v else b.GetFootprints()):
        for p in fp.Pads():
            if p.GetNetCode() != nc5 or p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH: continue
            c = p.GetPosition(); best = None
            for r in (FMM(1.0), FMM(1.3), FMM(1.6), FMM(2.0), FMM(2.5), FMM(3.0)):
                for a in range(0, 360, 15):
                    x = c.x + int(r*math.cos(math.radians(a))); y = c.y + int(r*math.sin(math.radians(a)))
                    if not forbidden(x, y) and not clash(x, y, nc5):
                        # the stub must also be clear on F.Cu
                        ok = True
                        for (dx, dy, dr, n) in discs[pcbnew.F_Cu]:
                            if n != nc5 and seg_pt(c.x, c.y, x, y, dx, dy) < FMM(0.2) + dr + FMM(0.2): ok = False; break
                        if ok: best = (x, y); break
                if best: break
            if not best: skipped.append(f"{fp.GetReference()}.{p.GetPadName()}"); continue
            v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(*best)); v.SetDrill(FMM(0.3)); v.SetWidth(FMM(0.6))
            v.SetNet(net5); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
            t = pcbnew.PCB_TRACK(b); t.SetStart(c); t.SetEnd(pcbnew.VECTOR2I(*best)); t.SetWidth(FMM(0.4)); t.SetLayer(pcbnew.F_Cu); t.SetNet(net5); b.Add(t)
            for L in discs: discs[L].append((best[0], best[1], FMM(0.45), nc5))
            segs[pcbnew.F_Cu].append((c.x, c.y, best[0], best[1], FMM(0.2), nc5)); added += 1
    print(f"[prep] +5V plane vias added: {added}; skipped: {skipped or 'none'}")
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(pcb, b)
    # ---- 3. DSN
    out = os.path.join(os.path.dirname(pcb), "out"); os.makedirs(out, exist_ok=True)
    dsn = os.path.join(out, "mainboard.dsn")
    bx = pcbnew.LoadBoard(pcb)
    if route_5v:
        for z in list(bx.Zones()):
            if not z.GetIsRuleArea() and z.GetLayer() == pcbnew.In2_Cu: bx.Remove(z); print("[prep] In2 +5V plane hidden from the DSN (router must route +5V_LOGIC)")
    print("[prep] DSN:", pcbnew.ExportSpecctraDSN(bx, dsn), dsn)

if __name__ == "__main__":
    main()
