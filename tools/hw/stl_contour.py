#!/usr/bin/env python3
"""
stl_contour.py — chain the segments of one STL cross-section into polylines,
report each polyline's extents, and export the chosen one as DXF (KiCad:
File > Import > Graphics, onto Edge.Cuts or User.Comments).

  py tools/hw/stl_contour.py part.stl --axis x --at -72 --pick inner --dxf outline.dxf [--inset 1.0]

--pick inner|outer|N   inner = polyline with the smallest area, outer = largest, N = index
--inset d              shrink the polyline toward its centroid by ~d mm (approximate offset)
"""
import sys, os, math, argparse
sys.path.insert(0, os.path.dirname(__file__))
from stl_slice import read_stl, slice_tris

def chain(segs, tol=0.05):
    segs = [list(s) for s in segs]
    used = [False] * len(segs)
    polys = []
    def near(a, b): return abs(a[0] - b[0]) < tol and abs(a[1] - b[1]) < tol
    for i in range(len(segs)):
        if used[i]: continue
        used[i] = True
        poly = [segs[i][0], segs[i][1]]
        grown = True
        while grown:
            grown = False
            for j in range(len(segs)):
                if used[j]: continue
                a, b = segs[j]
                if near(poly[-1], a): poly.append(b); used[j] = True; grown = True
                elif near(poly[-1], b): poly.append(a); used[j] = True; grown = True
                elif near(poly[0], b): poly.insert(0, a); used[j] = True; grown = True
                elif near(poly[0], a): poly.insert(0, b); used[j] = True; grown = True
        polys.append(poly)
    return polys

def area(poly):
    s = 0
    for i in range(len(poly)):
        x0, y0 = poly[i]; x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return abs(s) / 2

def centroid(poly):
    return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))

def write_dxf(path, polys, layer="0"):
    out = ["0", "SECTION", "2", "ENTITIES"]
    for poly in polys:
        out += ["0", "LWPOLYLINE", "8", layer, "90", str(len(poly)), "70", "1" if poly[0] == poly[-1] else "0"]
        for (x, y) in poly:
            out += ["10", f"{x:.3f}", "20", f"{y:.3f}"]
    out += ["0", "ENDSEC", "0", "EOF"]
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stl"); ap.add_argument("--axis", default="x"); ap.add_argument("--at", type=float, required=True)
    ap.add_argument("--pick", default=None); ap.add_argument("--dxf"); ap.add_argument("--inset", type=float, default=0.0)
    ap.add_argument("--closed-only", action="store_true")
    a = ap.parse_args()
    segs = slice_tris(read_stl(a.stl), a.axis, a.at)
    polys = chain(segs)
    polys.sort(key=lambda p: -len(p))
    print(f"{len(segs)} segments -> {len(polys)} polylines at {a.axis}={a.at}")
    for i, p in enumerate(polys):
        xs = [q[0] for q in p]; ys = [q[1] for q in p]
        closed = abs(p[0][0] - p[-1][0]) < 0.1 and abs(p[0][1] - p[-1][1]) < 0.1
        print(f"  [{i}] {len(p):4d} pts  {'closed' if closed else 'open  '}  u {min(xs):8.2f}..{max(xs):8.2f} ({max(xs)-min(xs):7.2f})  v {min(ys):8.2f}..{max(ys):8.2f} ({max(ys)-min(ys):7.2f})  area {area(p):9.1f}")
    if a.pick is None or not a.dxf: return
    cands = [p for p in polys if len(p) > 4]
    if a.pick == "inner": pick = min(cands, key=area)
    elif a.pick == "outer": pick = max(cands, key=area)
    else: pick = polys[int(a.pick)]
    if a.inset:
        cx, cy = centroid(pick)
        # approximate offset: move each vertex toward the centroid by `inset` along the radial direction
        new = []
        for (x, y) in pick:
            dx, dy = x - cx, y - cy; d = math.hypot(dx, dy) or 1
            new.append((x - dx / d * a.inset, y - dy / d * a.inset))
        pick = new
    write_dxf(a.dxf, [pick])
    xs = [q[0] for q in pick]; ys = [q[1] for q in pick]
    print(f"wrote {a.dxf}: {len(pick)} pts, u {min(xs):.2f}..{max(xs):.2f}, v {min(ys):.2f}..{max(ys):.2f}")

if __name__ == "__main__":
    main()
