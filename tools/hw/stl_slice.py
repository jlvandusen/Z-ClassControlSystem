#!/usr/bin/env python3
"""
stl_slice.py — measure an STL and cut cross-sections into PNG images.
Pure standard library (no numpy / PIL), so it runs anywhere `py` does.

  py tools/hw/stl_slice.py part.stl                     -> bounding box + dims
  py tools/hw/stl_slice.py part.stl --axis z --at 5,10  -> slices at z=5 and z=10 mm
  py tools/hw/stl_slice.py part.stl --axis z --steps 8  -> 8 evenly spaced slices
  py tools/hw/stl_slice.py part.stl --proj              -> top/front/side silhouettes

Outputs PNGs next to the STL (part_z5.0.png ...) with a 10 mm grid so the
space can be read by eye, plus a .txt with the exact extents of each slice.
"""
import struct, sys, os, zlib, math, argparse

# ---------------- STL reading ----------------
def read_stl(path):
    with open(path, 'rb') as f:
        data = f.read()
    tris = []
    if len(data) >= 84:
        n = struct.unpack('<I', data[80:84])[0]
        if 84 + n * 50 == len(data):
            off = 84
            for _ in range(n):
                v = struct.unpack('<12f', data[off + 0:off + 48])
                tris.append(((v[3], v[4], v[5]), (v[6], v[7], v[8]), (v[9], v[10], v[11])))
                off += 50
            return tris
    # ASCII
    verts = []
    for line in data.decode('ascii', 'ignore').splitlines():
        t = line.strip().split()
        if len(t) == 4 and t[0] == 'vertex':
            verts.append((float(t[1]), float(t[2]), float(t[3])))
            if len(verts) == 3:
                tris.append(tuple(verts)); verts = []
    return tris

def bbox(tris):
    xs = [p[0] for t in tris for p in t]; ys = [p[1] for t in tris for p in t]; zs = [p[2] for t in tris for p in t]
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

# ---------------- slicing ----------------
def slice_tris(tris, axis, at):
    """Intersect every triangle with the plane axis=at; return 2D segments (u,v)."""
    a = {'x': 0, 'y': 1, 'z': 2}[axis]
    u, v = [i for i in range(3) if i != a]
    segs = []
    for t in tris:
        pts = []
        for i in range(3):
            p, q = t[i], t[(i + 1) % 3]
            da, db = p[a] - at, q[a] - at
            if (da < 0 <= db) or (db < 0 <= da):
                f = da / (da - db)
                pts.append((p[u] + f * (q[u] - p[u]), p[v] + f * (q[v] - p[v])))
        if len(pts) == 2:
            segs.append((pts[0], pts[1]))
    return segs

# ---------------- tiny PNG writer ----------------
def write_png(path, w, h, pix):
    raw = b''.join(b'\x00' + bytes(pix[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(tag, payload):
        c = struct.pack('>I', len(payload)) + tag + payload
        return c + struct.pack('>I', zlib.crc32(tag + payload) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)

class Canvas:
    def __init__(self, w, h, bg=(255, 255, 255)):
        self.w, self.h = w, h
        self.pix = bytearray(bg * (w * h))
    def dot(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.pix[i:i + 3] = bytes(c)
    def line(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.dot(x0, y0, c)
            if x0 == x1 and y0 == y1: break
            e2 = 2 * err
            if e2 >= dy: err += dy; x0 += sx
            if e2 <= dx: err += dx; y0 += sy

def render(segs, umin, umax, vmin, vmax, path, title, scale=8):
    pad = 12
    w = int((umax - umin) * scale) + 2 * pad + 1
    h = int((vmax - vmin) * scale) + 2 * pad + 1
    cv = Canvas(max(w, 64), max(h, 64))
    # 10 mm grid (light) and 1 mm ticks on the border
    gu = math.floor(umin / 10) * 10
    while gu <= umax:
        x = pad + (gu - umin) * scale
        cv.line(x, 0, x, cv.h - 1, (225, 225, 225)); gu += 10
    gv = math.floor(vmin / 10) * 10
    while gv <= vmax:
        y = cv.h - 1 - (pad + (gv - vmin) * scale)
        cv.line(0, y, cv.w - 1, y, (225, 225, 225)); gv += 10
    for (p, q) in segs:
        cv.line(pad + (p[0] - umin) * scale, cv.h - 1 - (pad + (p[1] - vmin) * scale),
                pad + (q[0] - umin) * scale, cv.h - 1 - (pad + (q[1] - vmin) * scale), (200, 30, 30))
    write_png(path, cv.w, cv.h, cv.pix)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stl')
    ap.add_argument('--axis', default='z', choices=['x', 'y', 'z'])
    ap.add_argument('--at', help='comma list of coordinates (mm) to slice at')
    ap.add_argument('--steps', type=int, help='number of evenly spaced slices')
    ap.add_argument('--proj', action='store_true', help='top/front/side silhouettes (all edges)')
    ap.add_argument('--scale', type=float, default=8, help='pixels per mm')
    a = ap.parse_args()

    tris = read_stl(a.stl)
    (x0, y0, z0), (x1, y1, z1) = bbox(tris)
    base = os.path.splitext(a.stl)[0]
    print(f"{os.path.basename(a.stl)}: {len(tris)} triangles")
    print(f"  X {x0:8.2f} .. {x1:8.2f}  ({x1 - x0:7.2f} mm)")
    print(f"  Y {y0:8.2f} .. {y1:8.2f}  ({y1 - y0:7.2f} mm)")
    print(f"  Z {z0:8.2f} .. {z1:8.2f}  ({z1 - z0:7.2f} mm)")

    lo = {'x': x0, 'y': y0, 'z': z0}[a.axis]; hi = {'x': x1, 'y': y1, 'z': z1}[a.axis]
    ax = {'x': 0, 'y': 1, 'z': 2}[a.axis]
    others = [i for i in range(3) if i != ax]
    mins = [(x0, y0, z0)[i] for i in others]; maxs = [(x1, y1, z1)[i] for i in others]
    names = ['x', 'y', 'z']

    cuts = []
    if a.at: cuts = [float(s) for s in a.at.split(',')]
    elif a.steps: cuts = [lo + (hi - lo) * (i + 0.5) / a.steps for i in range(a.steps)]

    report = []
    for c in cuts:
        segs = slice_tris(tris, a.axis, c)
        if not segs:
            print(f"  slice {a.axis}={c:.2f}: empty"); continue
        su = [p[0] for s in segs for p in s]; sv = [p[1] for s in segs for p in s]
        out = f"{base}_{a.axis}{c:.1f}.png"
        render(segs, mins[0], maxs[0], mins[1], maxs[1], out, f"{a.axis}={c}", a.scale)
        line = (f"  slice {a.axis}={c:7.2f}: {len(segs):6d} segs  "
                f"{names[others[0]]} {min(su):7.2f}..{max(su):7.2f} ({max(su) - min(su):6.2f})  "
                f"{names[others[1]]} {min(sv):7.2f}..{max(sv):7.2f} ({max(sv) - min(sv):6.2f})  -> {os.path.basename(out)}")
        print(line); report.append(line)

    if a.proj:
        for axis_name, (u, v) in {'top_xy': (0, 1), 'front_xz': (0, 2), 'side_yz': (1, 2)}.items():
            segs = []
            for t in tris:
                for i in range(3):
                    p, q = t[i], t[(i + 1) % 3]
                    segs.append(((p[u], p[v]), (q[u], q[v])))
            lo_u, hi_u = (x0, y0, z0)[u], (x1, y1, z1)[u]
            lo_v, hi_v = (x0, y0, z0)[v], (x1, y1, z1)[v]
            out = f"{base}_{axis_name}.png"
            render(segs, lo_u, hi_u, lo_v, hi_v, out, axis_name, a.scale)
            print(f"  projection {axis_name} -> {os.path.basename(out)}")

    if report:
        with open(base + "_slices.txt", 'w') as f:
            f.write("\n".join(report) + "\n")

if __name__ == '__main__':
    main()
