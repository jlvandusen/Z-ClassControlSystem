#!/usr/bin/env python3
"""fusion_model.py — STEP of the compact board for the Fusion 360 fit-check, WITH collision envelopes
for everything KiCad has no 3D model for: the plug-in modules on their sockets, the Pololus, the DFPlayer,
the fuse holder + a standard mini fuse, the XT60 plug + wire exit, and the mating housings/ribbons on
every header. KiCad python.
  python fusion_model.py <board.kicad_pcb> <out.step>
Envelope boxes are written to hardware/mechanical/board-3d/envelopes/ (ENV_<L>x<W>x<H>.step) and attached
to a scratch COPY of the board; the source board is never written. Origin of the STEP = axle centre.
After export the STEP is parsed back and every envelope's placement is checked against the intended position."""
import sys, os, re, math, shutil, subprocess, tempfile, pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from step_box import box as step_box

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENVDIR = os.path.join(ROOT, "hardware", "mechanical", "board-3d", "envelopes")
KICAD = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "KiCad", "10.0", "bin", "kicad-cli.exe")
AX, AY = 150.0, 150.0

def mm(v): return v / 1e6
def pads_bbox(f):
    xs = [mm(p.GetPosition().x) for p in f.Pads()]; ys = [mm(p.GetPosition().y) for p in f.Pads()]
    return min(xs), min(ys), max(xs), max(ys)
def crt_bbox(f):
    c = f.GetCourtyard(pcbnew.F_CrtYd)
    if not c.OutlineCount(): return pads_bbox(f)
    bb = c.BBox(); return mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())
def grow(b, dx, dy): return b[0] - dx, b[1] - dy, b[2] + dx, b[3] + dy
def centred(cx, cy, L, W): return cx - L / 2, cy - W / 2, cx + L / 2, cy + W / 2

def envelopes(b):
    """-> list of (anchor_ref, name, (x0,y0,x1,y1) world KiCad mm y-down, z0, z1)"""
    F = {f.GetReference(): f for f in b.GetFootprints()}
    E = []
    # ESP32 DevKitC 30-pin on J_U1A/J_U1B: 28.3 wide, 52 long, antenna end 13.5 mm beyond pin 1
    a, bb = F["J_U1A"], F["J_U1B"]; pa = {int(p.GetNumber()): p.GetPosition() for p in a.Pads()}
    xa, xb = mm(pa[1].x), mm(list(bb.Pads())[0].GetPosition().x); x0, x1 = min(xa, xb), max(xa, xb)
    y1, y15 = mm(pa[1].y), mm(pa[15].y); dirn = 1 if y15 > y1 else -1
    ya, yb = y1 - dirn * 13.5, y15 + dirn * 2.9
    E.append(("J_U1A", "ESP32 DevKitC", (x0 - 1.45, min(ya, yb), x1 + 1.45, max(ya, yb)), 8.5, 13.5))
    # RP2350-Zero on J_U2A/J_U2B/J_U2C: 18 x 23.5, USB-C at the end away from J_U2C
    ba, bbx, bc = pads_bbox(F["J_U2A"]), pads_bbox(F["J_U2B"]), pads_bbox(F["J_U2C"])
    x0, x1 = min(ba[0], bbx[0]) - 1.4, max(ba[2], bbx[2]) + 1.4
    cy = (bc[1] + bc[3]) / 2
    if abs(ba[1] - cy) > abs(ba[3] - cy): usb, end = ba[1] - 1.6, cy + 1.5
    else: usb, end = ba[3] + 1.6, cy - 1.5
    E.append(("J_U2A", "RP2350-Zero", (x0, min(usb, end), x1, max(usb, end)), 8.5, 13.0))
    # DFPlayer on U3 (socketed worst case): rows 15.24 apart -> module 20 across rows, 21.5 along
    pb = pads_bbox(F["U3"]); across_x = (pb[2] - pb[0]) < (pb[3] - pb[1])
    E.append(("U3", "DFPlayer Mini", grow(pb, 2.38 if across_x else 1.86, 1.86 if across_x else 2.38), 8.5, 12.5))
    for r in ("PS1", "PS2"): E.append((r, "Pololu " + F[r].GetValue(), crt_bbox(F[r]), 0.0, 9.0))
    # F1: Keystone 3568 holder 16.0 x 6.7 x 7.5 along the pad axis; standard mini fuse 10.9 x 3.8 x 16.3
    pb = pads_bbox(F["F1"]); cx, cy = (pb[0] + pb[2]) / 2, (pb[1] + pb[3]) / 2; horiz = (pb[2] - pb[0]) > (pb[3] - pb[1])
    E.append(("F1", "fuse holder", centred(cx, cy, *((16.0, 6.7) if horiz else (6.7, 16.0))), 0.0, 7.5))
    E.append(("F1", "mini fuse (std height)", centred(cx, cy, *((10.9, 3.8) if horiz else (3.8, 10.9))), 1.0, 16.3))
    # J1: mated XT60 female + wire exit above the 15.5 mm male body
    cb = crt_bbox(F["J1"]); E.append(("J1", "XT60 plug", cb, 15.5, 24.0)); E.append(("J1", "XT60 wire exit", cb, 24.0, 34.0))
    for r in ("J2", "J3", "J_AMP"): E.append((r, "IDC + ribbon", grow(crt_bbox(F[r]), 1.0, 1.0), 8.5, 14.0))
    for r in ("J6", "J7", "J8", "J9", "J10", "J11", "J15"): E.append((r, "XH housing + wires", crt_bbox(F[r]), 7.0, 13.0))
    for r in ("J13", "J14"): E.append((r, "VH housing + wires", crt_bbox(F[r]), 8.0, 14.0))
    for r in ("J4", "J5"):
        pb = pads_bbox(F[r]); cx, cy = (pb[0] + pb[2]) / 2, (pb[1] + pb[3]) / 2; horiz = (pb[2] - pb[0]) > (pb[3] - pb[1])
        E.append((r, "servo plug", centred(cx, cy, *((8.5, 3.5) if horiz else (3.5, 8.5))), 2.5, 12.0))
    return E

def to_local(f, wx, wy):
    """world KiCad mm (y-down) -> footprint-local mm (y-down); verified against GetFPRelativePosition"""
    a = math.radians(f.GetOrientationDegrees()); dx, dy = wx - mm(f.GetPosition().x), wy - mm(f.GetPosition().y)
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)

def parse_placements(path):
    """-> [(occurrence name, product name, [x,y,z])] for every ENV_* product in the exported STEP"""
    txt = open(path, encoding="utf-8", errors="ignore").read()
    ents = dict(re.findall(r"^#(\d+) = (.*?);\s*$", txt, re.M | re.S))
    refs = {}
    for k, v in ents.items():
        for t in re.findall(r"#(\d+)", v): refs.setdefault(t, []).append(k)
    def find(kind, from_id):
        return next((k for k in refs.get(from_id, []) if ents[k].startswith(kind)), None)
    out = []
    for pid, v in ents.items():
        if not v.startswith("PRODUCT('ENV_"): continue
        pdf = find("PRODUCT_DEFINITION_FORMATION", pid); pd = find("PRODUCT_DEFINITION(", pdf)
        for nauo in [k for k in refs.get(pd, []) if ents[k].startswith("NEXT_ASSEMBLY_USAGE_OCCURRENCE")]:
            name = re.match(r"NEXT_ASSEMBLY_USAGE_OCCURRENCE\('[^']*','([^']*)'", ents[nauo]).group(1)
            pds = find("PRODUCT_DEFINITION_SHAPE", nauo); cdsr = find("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION", pds)
            if not cdsr: continue
            rr = re.findall(r"#(\d+)", ents[cdsr])[0]; idt = re.findall(r"#(\d+)", ents[rr])[-1]
            a2 = re.findall(r"#(\d+)", ents[idt])[-1]; cp = re.findall(r"#(\d+)", ents[a2])[0]
            xyz = [float(s) for s in re.findall(r"[-+]?\d*\.?\d+(?:E[-+]?\d+)?", ents[cp].split("(", 1)[1])]
            out.append((name, v.split("'")[1], xyz))
    return out

def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(ENVDIR, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="fusion_"); work = os.path.join(tmp, "mainboard.kicad_pcb"); shutil.copy(src, work)
    b = pcbnew.LoadBoard(work); F = {f.GetReference(): f for f in b.GetFootprints()}
    intended = []
    for ref, name, (x0, y0, x1, y1), z0, z1 in envelopes(b):
        L, W, H = round(x1 - x0, 2), round(y1 - y0, 2), round(z1 - z0, 2)
        f = F[ref]; a = f.GetOrientationDegrees()
        Lf, Wf = (W, L) if abs(a) % 180 == 90 else (L, W)   # box is world-aligned; footprint frame is rotated
        pname = f"ENV_{Lf:g}x{Wf:g}x{H:g}"; fn = os.path.join(ENVDIR, pname + ".step")
        if not os.path.exists(fn): open(fn, "w").write(step_box(pname, -Lf / 2, -Wf / 2, 0, Lf / 2, Wf / 2, H))
        lx, ly = to_local(f, (x0 + x1) / 2, (y0 + y1) / 2)
        m = pcbnew.FP_3DMODEL(); m.m_Filename = fn; m.m_Offset = pcbnew.VECTOR3D(lx, -ly, z0); m.m_Show = True
        f.Models().push_back(m)
        intended.append((ref, name, pname, (x0 + x1) / 2 - AX, AY - (y0 + y1) / 2, z0))
    thick = mm(b.GetDesignSettings().GetBoardThickness())   # STEP frame: board bottom at z=0, top surface at z=thick
    b.Save(work)
    r = subprocess.run([KICAD, "pcb", "export", "step", "--user-origin", f"{AX}x{AY}mm", "--subst-models", "--no-dnp", "--force", "-o", out, work], capture_output=True, text=True)
    print((r.stdout.strip().splitlines() or [r.stderr[-300:]])[-1])
    found = parse_placements(out); used = set(); bad = 0
    print(f"\n{'ref':7s} {'envelope':24s} {'intended x,y,z':>24s}   {'in STEP x,y,z':>24s}")
    for ref, name, pname, x, y, z in intended:
        best = None
        for i, (occ, prod, xyz) in enumerate(found):
            if i in used or occ != ref or prod != pname: continue
            d = math.hypot(xyz[0] - x, xyz[1] - y)
            if best is None or d < best[0]: best = (d, i, xyz)
        if best is None: print(f"{ref:7s} {name:24s} {x:7.2f},{y:7.2f},{z:5.1f}   NOT FOUND"); bad += 1; continue
        d, i, xyz = best; used.add(i); ok = d < 0.05 and abs(xyz[2] - (z + thick)) < 0.05; bad += 0 if ok else 1
        print(f"{ref:7s} {name:24s} {x:7.2f},{y:7.2f},{z:5.1f}   {xyz[0]:7.2f},{xyz[1]:7.2f},{xyz[2]:5.1f}  {'ok' if ok else 'MISMATCH'}")
    print(f"\n{len(intended)} envelopes, {bad} problems -> {out}")
    shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__": main()
