#!/usr/bin/env python3
"""
gen_kicad.py — generate a KiCad 8+ project (schematic, PCB, project file) from a
netlist module such as hardware/netlist/mainboard.py.

  py tools/hw/gen_kicad.py hardware/netlist/mainboard.py hardware/kicad/mainboard

Schematic: one generated symbol per component (rectangle + pins), connectivity by
global labels (net names), no-connect flags on NC pins. PCB: casing outline from the
DXF, axle cut-out, every footprint placed at the netlist coordinates with nets
assigned (ratsnest ready, unrouted), GND pours on both layers. Library footprints
are embedded from the installed KiCad share dir when present, otherwise a
dimensionally-reasonable generic footprint is generated (swap in the GUI later).
"""
import sys, os, re, math, uuid, json, importlib.util, glob

NS = uuid.UUID("7f1b5a0c-2d7e-4e8a-9c3b-1a2b3c4d5e6f")
def U(*parts): return str(uuid.uuid5(NS, "|".join(map(str, parts))))
def q(s): return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"') + '"'

# ------------------------------------------------------------------ load netlist
def load_netlist(path):
    spec = importlib.util.spec_from_file_location("netlist", path)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m

def net_table(comps):
    nets = {}
    for c in comps:
        for num, name, net in c["pins"]:
            if net == "NC": continue
            nets.setdefault(net, []).append((c["ref"], num, name))
    # GND first, then rails, then alphabetical
    order = sorted(nets, key=lambda n: (n != "GND", not n.startswith("+"), n))
    return {n: i + 1 for i, n in enumerate(order)}, nets

# ------------------------------------------------------------------ schematic
PIN_PITCH = 2.54
def symbol_geometry(c):
    pins = c["pins"]
    n = len(pins)
    left = pins[: (n + 1) // 2]; right = pins[(n + 1) // 2:]
    rows = max(len(left), len(right))
    h = (rows + 1) * PIN_PITCH
    w = 2.54 * max(8, 2 + max(len(p[1]) for p in pins) // 2 * 2)
    w = min(w, 40.64)
    return left, right, w, h

def lib_symbol(c):
    left, right, w, h = symbol_geometry(c)
    name = f"ZCLASS:{c['ref']}"
    s = [f'    (symbol {q(name)} (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)',
         f'      (property "Reference" {q(c["ref"])} (at 0 {h/2 + 1.27:.2f} 0) (effects (font (size 1.27 1.27))))',
         f'      (property "Value" {q(c["value"])} (at 0 {-(h/2 + 1.27):.2f} 0) (effects (font (size 1.27 1.27))))',
         f'      (property "Footprint" {q(c["fp"])} (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
         f'      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))',
         f'      (symbol {q(c["ref"] + "_0_1")}',
         f'        (rectangle (start {-w/2:.2f} {h/2:.2f}) (end {w/2:.2f} {-h/2:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))',
         f'      )',
         f'      (symbol {q(c["ref"] + "_1_1")}']
    def pin(num, nm, x, y, ang):
        s.append(f'        (pin passive line (at {x:.2f} {y:.2f} {ang}) (length 2.54)'
                 f' (name {q(nm)} (effects (font (size 1.27 1.27)))) (number {q(num)} (effects (font (size 1.27 1.27)))))')
    for i, (num, nm, net) in enumerate(left):
        pin(num, nm, -w/2 - 2.54, h/2 - (i + 1) * PIN_PITCH, 0)
    for i, (num, nm, net) in enumerate(right):
        pin(num, nm, w/2 + 2.54, h/2 - (i + 1) * PIN_PITCH, 180)
    s += ['      )', '    )']
    return "\n".join(s)

def sch_instance(c, x, y, root_uuid, project):
    left, right, w, h = symbol_geometry(c)
    iu = U("sym", c["ref"])
    out = [f'  (symbol (lib_id {q("ZCLASS:" + c["ref"])}) (at {x:.2f} {y:.2f} 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid {q(iu)})',
           f'    (property "Reference" {q(c["ref"])} (at {x:.2f} {y - h/2 - 1.27:.2f} 0) (effects (font (size 1.27 1.27))))',
           f'    (property "Value" {q(c["value"])} (at {x:.2f} {y + h/2 + 1.27:.2f} 0) (effects (font (size 1.27 1.27))))',
           f'    (property "Footprint" {q(c["fp"])} (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))',
           f'    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes)))']
    for num, nm, net in c["pins"]:
        out.append(f'    (pin {q(num)} (uuid {q(U("pin", c["ref"], num))}))')
    out.append(f'    (instances (project {q(project)} (path {q("/" + root_uuid)} (reference {q(c["ref"])}) (unit 1))))')
    out.append('  )')
    labels = []
    for i, (num, nm, net) in enumerate(left):
        px, py = x - w/2 - 2.54, y - (h/2 - (i + 1) * PIN_PITCH)
        labels.append(label_or_nc(net, px, py, 180, c["ref"], num))
    for i, (num, nm, net) in enumerate(right):
        px, py = x + w/2 + 2.54, y - (h/2 - (i + 1) * PIN_PITCH)
        labels.append(label_or_nc(net, px, py, 0, c["ref"], num))
    return "\n".join(out) + "\n" + "\n".join(labels)

def label_or_nc(net, px, py, ang, ref, num):
    if net == "NC":
        return f'  (no_connect (at {px:.2f} {py:.2f}) (uuid {q(U("nc", ref, num))}))'
    just = "right" if ang == 180 else "left"
    return (f'  (global_label {q(net)} (shape bidirectional) (at {px:.2f} {py:.2f} {ang}) (fields_autoplaced yes)'
            f' (effects (font (size 1.27 1.27)) (justify {just})) (uuid {q(U("lbl", ref, num))})'
            f' (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {px:.2f} {py:.2f} 0) (effects (font (size 1.27 1.27)) (hide yes))))')

def write_schematic(m, outbase, project):
    comps = m.COMPONENTS
    root = U("sheet", project)
    lines = [f'(kicad_sch (version 20231120) (generator "zclass_gen") (generator_version "1.0")',
             f'  (uuid {q(root)})', '  (paper "A2")',
             '  (title_block (title ' + q(getattr(m, "SCH_TITLE", "Z-Class v10 mainboard")) + ') (company ' + q(getattr(m, "SCH_COMPANY", "")) + ') (rev "A")'
             + "".join(f' (comment {i+1} {q(c)})' for i, c in enumerate(getattr(m, "SCH_COMMENTS", []))) + ')',
             '  (lib_symbols']
    for c in comps: lines.append(lib_symbol(c))
    lines.append('  )')
    # place: flow in columns, grouped by kind order
    order = {"module": 0, "ic_generic": 1, "ic3": 1, "connector": 2, "fuse": 3, "diode": 3, "cap": 4, "res": 5, "hole": 6}
    comps_sorted = sorted(comps, key=lambda c: (order.get(c["kind"], 9), c["ref"]))
    x, y, col_w, row_max = 40.0, 40.0, 0.0, 0.0
    page_h = 560.0
    for c in comps_sorted:
        left, right, w, h = symbol_geometry(c)
        block_w = w + 2 * 2.54 + 2 * 20.0      # room for labels
        block_h = h + 10.16
        if y + block_h > page_h:
            y = 40.0; x += col_w + 5.08; col_w = 0.0
        cx, cy = x + block_w / 2, y + block_h / 2
        cx = round(cx / 1.27) * 1.27; cy = round(cy / 1.27) * 1.27
        lines.append(sch_instance(c, cx, cy, root, project))
        y += block_h; col_w = max(col_w, block_w)
    lines.append(f'  (sheet_instances (path "/" (page "1")))')
    lines.append(')')
    with open(outbase + ".kicad_sch", "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

# ------------------------------------------------------------------ footprints
KICAD_FP_DIRS = (glob.glob(r"C:\Program Files\KiCad\*\share\kicad\footprints") +
                 glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "KiCad", "*", "share", "kicad", "footprints")))

class SExpr:
    """Minimal S-expression parser returning nested lists of str."""
    def __init__(self, text): self.t = re.findall(r'"(?:\\.|[^"\\])*"|\(|\)|[^\s()]+', text); self.i = 0
    def parse(self):
        tok = self.t[self.i]; self.i += 1
        if tok == '(':
            lst = []
            while self.t[self.i] != ')': lst.append(self.parse())
            self.i += 1; return lst
        return tok
def sx_dump(node):
    if isinstance(node, list): return "(" + " ".join(sx_dump(n) for n in node) + ")"
    return node

def find_lib_footprint(fpid):
    if ":" not in fpid or fpid.startswith("ZCLASS:"): return None
    lib, name = fpid.split(":", 1)
    for d in KICAD_FP_DIRS:
        p = os.path.join(d, lib + ".pretty", name + ".kicad_mod")
        if os.path.exists(p): return p
    return None

def embed_lib_footprint(path, c, netnum, x, y, rot, side):
    with open(path, encoding="utf-8") as f: tree = SExpr(f.read()).parse()
    # tree = ['footprint', '"name"', ...]
    out = ["footprint", tree[1]]
    body = tree[2:]
    # strip existing layer/at/tedit/uuid, keep the rest
    body = [n for n in body if not (isinstance(n, list) and n and n[0] in ("layer", "at", "tedit", "uuid", "tstamp", "path", "sheetfile", "sheetname"))]
    layer = '"F.Cu"' if side == "F" else '"B.Cu"'
    out += [["layer", layer], ["uuid", q(U("fp", c["ref"]))], ["at", f"{x:.3f}", f"{y:.3f}", f"{rot:g}"]]
    pinnets = {str(num): net for num, nm, net in c["pins"]}
    for n in body:
        if isinstance(n, list) and n and n[0] == "property" and len(n) > 2 and n[1] == '"Reference"':
            n[2] = q(c["ref"])
        if isinstance(n, list) and n and n[0] == "property" and len(n) > 2 and n[1] == '"Value"':
            n[2] = q(c["value"])
        if isinstance(n, list) and n and n[0] == "fp_text" and len(n) > 2 and n[1] == "reference":
            n[2] = q(c["ref"])
        if isinstance(n, list) and n and n[0] == "fp_text" and len(n) > 2 and n[1] == "value":
            n[2] = q(c["value"])
        if isinstance(n, list) and n and n[0] == "pad":
            num = n[1].strip('"')
            n[:] = [k for k in n if not (isinstance(k, list) and k and k[0] == "net")]
            net = pinnets.get(num)
            if net and net != "NC" and net in netnum:
                n.append(["net", str(netnum[net]), q(net)])
            n.append(["uuid", q(U("pad", c["ref"], num, id(n)))])
        out.append(n)
    if side == "B":   # mirror is handled by KiCad on layer change when loading; good enough for placement
        pass
    return sx_dump(out)

def pad(num, x, y, w, h, net, netnum, shape="roundrect", drill=None, layers=None):
    layers = layers or ('"F.Cu" "F.Paste" "F.Mask"' if drill is None else '"*.Cu" "*.Mask"')
    typ = "smd" if drill is None else "thru_hole"
    s = f'    (pad {q(num)} {typ} {shape} (at {x:.3f} {y:.3f}) (size {w:.2f} {h:.2f})'
    if drill: s += f' (drill {drill:.2f})'
    s += f' (layers {layers})'
    if shape == "roundrect": s += ' (roundrect_rratio 0.25)'
    if net and net != "NC" and net in netnum: s += f' (net {netnum[net]} {q(net)})'
    s += f' (uuid {q(U("gpad", num, x, y))}))'
    return s

def generic_footprint(c, netnum, x, y, rot, side):
    """Dimensionally reasonable footprints for modules + fallbacks for library parts."""
    fp = c["fp"]; pins = c["pins"]; pinnets = {str(n): net for n, nm, net in pins}
    P = []
    lines = []
    def text_ref():
        lines.append(f'    (property "Reference" {q(c["ref"])} (at 0 -1 0) (layer "F.SilkS") (uuid {q(U("r", c["ref"]))}) (effects (font (size 1 1) (thickness 0.15))))')
        lines.append(f'    (property "Value" {q(c["value"])} (at 0 1 0) (layer "F.Fab") (uuid {q(U("v", c["ref"]))}) (effects (font (size 1 1) (thickness 0.15))))')
    def rect(w, h, layer="F.SilkS"):
        lines.append(f'    (fp_rect (start {-w/2:.2f} {-h/2:.2f}) (end {w/2:.2f} {h/2:.2f}) (stroke (width 0.12) (type default)) (fill none) (layer {q(layer)}) (uuid {q(U("rect", c["ref"], layer))}))')
    def courtyard(w, h): rect(w + 0.5, h + 0.5, "F.CrtYd")
    text_ref()
    if fp == "ZCLASS:DevKit_2x15_25.4mm":
        rect(28.0, 51.5); courtyard(28.0, 51.5)
        for i in range(15):
            yy = (i - 7) * 2.54
            P.append(pad(str(i + 1), -12.7, yy, 1.7, 1.7, pinnets.get(str(i + 1)), netnum, "circle", 1.0))
            P.append(pad(str(i + 16), 12.7, yy, 1.7, 1.7, pinnets.get(str(i + 16)), netnum, "circle", 1.0))
    elif fp == "ZCLASS:RP2350_Zero":
        rect(18.0, 23.5); courtyard(18.0, 23.5)
        for i in range(9):
            yy = (i - 4) * 2.54 - 1.27
            P.append(pad(str(i + 1), -7.62, yy, 1.7, 1.7, pinnets.get(str(i + 1)), netnum, "circle", 1.0))
            P.append(pad(str(i + 10), 7.62, yy, 1.7, 1.7, pinnets.get(str(i + 10)), netnum, "circle", 1.0))
        for i in range(5):
            P.append(pad(str(i + 19), (i - 2) * 2.54, 10.16, 1.7, 1.7, pinnets.get(str(i + 19)), netnum, "circle", 1.0))
    elif fp == "ZCLASS:DFPlayer_2x8":
        rect(21.0, 21.0); courtyard(21.0, 21.0)
        for i in range(8):
            yy = (i - 3.5) * 2.54
            P.append(pad(str(i + 1), -7.62, yy, 1.7, 1.7, pinnets.get(str(i + 1)), netnum, "circle", 1.0))
            P.append(pad(str(16 - i), 7.62, yy, 1.7, 1.7, pinnets.get(str(16 - i)), netnum, "circle", 1.0))
    elif fp.startswith("ZCLASS:Pololu"):
        w, h = (25.4, 27.9) if "D24V50" in fp else (15.2, 17.8)
        rect(w, h); courtyard(w, h)
        for i in range(4):
            P.append(pad(str(i + 1), (i - 1.5) * 2.54, h/2 - 1.5, 1.7, 1.7, pinnets.get(str(i + 1)), netnum, "circle", 1.0))
    elif fp in ("ZCLASS:Hdr_2x6", "ZCLASS:Hdr_2x5_shrouded"):
        cols = 6 if "2x6" in fp else 5
        w = cols * 2.54 + (6.0 if "shrouded" in fp else 0.5); h = 2 * 2.54 + (4.0 if "shrouded" in fp else 0.5)
        rect(w, h); courtyard(w, h)
        for i in range(cols):
            xx = (i - (cols - 1) / 2) * 2.54
            P.append(pad(str(2 * i + 1), xx, -1.27, 1.7, 1.7, pinnets.get(str(2 * i + 1)), netnum, "rect" if i == 0 else "circle", 1.0))
            P.append(pad(str(2 * i + 2), xx, 1.27, 1.7, 1.7, pinnets.get(str(2 * i + 2)), netnum, "circle", 1.0))
    elif fp in ("ZCLASS:XT60PW", "ZCLASS:XT30PW"):
        pitch, d, w, h = (7.2, 2.5, 16.0, 11.0) if "60" in fp else (5.0, 2.0, 10.0, 7.5)
        rect(w, h); courtyard(w, h)
        P.append(pad("1", -pitch/2, 0, d + 1.5, d + 1.5, pinnets.get("1"), netnum, "rect", d))
        P.append(pad("2", pitch/2, 0, d + 1.5, d + 1.5, pinnets.get("2"), netnum, "circle", d))
    elif fp == "ZCLASS:Fuse_Mini_Blade":
        rect(16.0, 8.0); courtyard(16.0, 8.0)
        P.append(pad("1", -5.4, 0, 2.6, 2.6, pinnets.get("1"), netnum, "circle", 1.6))
        P.append(pad("2", 5.4, 0, 2.6, 2.6, pinnets.get("2"), netnum, "circle", 1.6))
    # ---- generic fallbacks for library footprints that aren't installed
    elif "0603" in fp:
        rect(1.6, 0.8, "F.Fab"); courtyard(2.4, 1.4)
        P.append(pad("1", -0.8, 0, 0.8, 0.95, pinnets.get("1"), netnum)); P.append(pad("2", 0.8, 0, 0.8, 0.95, pinnets.get("2"), netnum))
    elif "1206" in fp or "1812" in fp:
        L = 3.2 if "1206" in fp else 4.5; W = 1.6 if "1206" in fp else 3.2
        rect(L, W, "F.Fab"); courtyard(L + 1, W + 0.6)
        P.append(pad("1", -L/2 + 0.2, 0, 1.2, W + 0.2, pinnets.get("1"), netnum)); P.append(pad("2", L/2 - 0.2, 0, 1.2, W + 0.2, pinnets.get("2"), netnum))
    elif "CP_Elec" in fp:
        d = 10.0 if "10x10" in fp else 8.0
        rect(d, d); courtyard(d + 1, d + 1)
        P.append(pad("1", -d/2 + 0.5, 0, 2.5, 1.8, pinnets.get("1"), netnum)); P.append(pad("2", d/2 - 0.5, 0, 2.5, 1.8, pinnets.get("2"), netnum))
    elif "D_SMA" in fp or "D_SMB" in fp:
        L = 4.3 if "SMA" in fp else 5.4; W = 2.6 if "SMA" in fp else 3.6
        rect(L, W, "F.Fab"); courtyard(L + 2, W + 0.6)
        P.append(pad("1", -L/2 - 0.3, 0, 2.0, W - 0.6, pinnets.get("1"), netnum)); P.append(pad("2", L/2 + 0.3, 0, 2.0, W - 0.6, pinnets.get("2"), netnum))
    elif "SOT-223" in fp:
        rect(6.5, 3.5, "F.Fab"); courtyard(8, 7.5)
        for i in range(3): P.append(pad(str(i + 1), (i - 1) * 2.3, 3.1, 1.2, 2.0, pinnets.get(str(i + 1)), netnum))
        P.append(pad("2", 0, -3.1, 3.6, 2.0, pinnets.get("2"), netnum))
    elif "PowerPAK" in fp:
        rect(6.2, 5.2, "F.Fab"); courtyard(7, 6)
        for i in range(4): P.append(pad(str(i + 1), (i - 1.5) * 1.27, 2.7, 0.8, 1.4, pinnets.get(str(i + 1)), netnum))
        P.append(pad("2", 0, -1.0, 4.0, 3.0, pinnets.get("2"), netnum))
    elif "TSSOP" in fp:
        n = 20 if "20" in fp else 14; L = 6.5 if n == 20 else 5.0
        rect(4.4, L, "F.Fab"); courtyard(7.5, L + 0.5)
        for i in range(n // 2):
            yy = (i - (n / 2 - 1) / 2) * 0.65
            P.append(pad(str(i + 1), -2.9, yy, 1.5, 0.4, pinnets.get(str(i + 1)), netnum))
            P.append(pad(str(n - i), 2.9, yy, 1.5, 0.4, pinnets.get(str(n - i)), netnum))
    elif "JST_XH" in fp or "JST_VH" in fp or "JST_SH" in fp:
        mm = re.search(r"1x(\d+)", fp); n = int(mm.group(1)) if mm else 2
        pitch = 2.5 if "XH" in fp else (3.96 if "VH" in fp else 1.0)
        w = (n - 1) * pitch + (4.9 if "XH" in fp else (6.0 if "VH" in fp else 3.0)); h = 7.0 if "XH" in fp else (7.5 if "VH" in fp else 4.5)
        rect(w, h); courtyard(w + 0.5, h + 0.5)
        for i in range(n):
            xx = (i - (n - 1) / 2) * pitch
            if "SH" in fp: P.append(pad(str(i + 1), xx, -1.5, 0.6, 1.55, pinnets.get(str(i + 1)), netnum))
            else: P.append(pad(str(i + 1), xx, 0, 1.9, 1.9, pinnets.get(str(i + 1)), netnum, "rect" if i == 0 else "circle", 1.0 if "XH" in fp else 1.6))
    elif "MountingHole" in fp:
        P.append(pad("1", 0, 0, 6.0, 6.0, pinnets.get("1"), netnum, "circle", 3.2))
        lines.append(f'    (fp_circle (center 0 0) (end 3.5 0) (stroke (width 0.12) (type default)) (fill none) (layer "F.CrtYd") (uuid {q(U("c", c["ref"]))}))')
    else:
        # unknown: two pads 2.54 apart so it is at least visible and connected
        rect(3.0, 2.0)
        for i, (num, nm, net) in enumerate(pins): P.append(pad(str(num), (i - (len(pins) - 1) / 2) * 2.54, 0, 1.7, 1.7, net, netnum, "circle", 1.0))
    layer = "F.Cu" if side == "F" else "B.Cu"
    head = f'  (footprint {q(fp)} (layer {q(layer)}) (uuid {q(U("fp", c["ref"]))}) (at {x:.3f} {y:.3f} {rot:g})'
    return "\n".join([head] + lines + P + ["  )"])


# ------------------------------------------------------------------ collision-aware placement
GEN_SIZES = {  # footprint -> (w, h) in mm at rot 0 (courtyard incl.)
    "ZCLASS:DevKit_2x15_25.4mm": (28.5, 52.0), "ZCLASS:RP2350_Zero": (18.5, 24.0), "ZCLASS:DFPlayer_2x8": (21.5, 21.5),
    "ZCLASS:Pololu_D24V50F5": (25.9, 28.4), "ZCLASS:Pololu_D24V22F6": (15.7, 18.3),
    "ZCLASS:Hdr_2x6": (16.0, 6.0), "ZCLASS:Hdr_2x5_shrouded": (19.2, 9.6),
    "ZCLASS:XT60PW": (16.5, 11.5), "ZCLASS:XT30PW": (10.5, 8.0), "ZCLASS:Fuse_Mini_Blade": (16.5, 8.5),
}
def courtyard_size(fpid):
    if fpid in GEN_SIZES: return GEN_SIZES[fpid]
    path = find_lib_footprint(fpid)
    if path:
        try:
            with open(path, encoding="utf-8") as f: tree = SExpr(f.read()).parse()
            xs, ys = [], []
            def walk(n, on_crtyd):
                if not isinstance(n, list): return
                if n and n[0] in ("fp_line", "fp_rect", "fp_poly", "fp_circle", "fp_arc"):
                    layer = next((k[1].strip('"') for k in n if isinstance(k, list) and k and k[0] == "layer"), "")
                    if layer.endswith("CrtYd"):
                        if n[0] == "fp_circle":
                            cen = next(k for k in n if isinstance(k, list) and k[0] == "center")
                            end = next(k for k in n if isinstance(k, list) and k[0] == "end")
                            r = math.hypot(float(end[1]) - float(cen[1]), float(end[2]) - float(cen[2]))
                            xs += [float(cen[1]) - r, float(cen[1]) + r]; ys += [float(cen[2]) - r, float(cen[2]) + r]
                        for k in n:
                            if isinstance(k, list) and k and k[0] in ("start", "end", "xy", "mid"):
                                xs.append(float(k[1])); ys.append(float(k[2]))
                            if isinstance(k, list) and k and k[0] == "pts":
                                for p in k[1:]:
                                    if isinstance(p, list) and p[0] == "xy": xs.append(float(p[1])); ys.append(float(p[2]))
                for k in n: walk(k, on_crtyd)
            walk(tree, False)
            if xs and ys: return (max(xs) - min(xs), max(ys) - min(ys))
        except Exception: pass
    # fallbacks
    f = fpid
    if "0603" in f: return (2.6, 1.5)
    if "1206" in f: return (4.4, 2.4)
    if "1812" in f: return (5.6, 4.0)
    if "CP_Elec_10" in f: return (11.0, 11.0)
    if "CP_Elec" in f: return (9.0, 9.0)
    if "D_SMB" in f: return (7.6, 4.4)
    if "D_SMA" in f: return (6.5, 3.4)
    if "SOT-223" in f: return (8.2, 7.8)
    if "PowerPAK" in f: return (7.2, 6.4)
    if "TSSOP-20" in f: return (7.8, 7.2)
    if "TSSOP" in f: return (7.8, 5.8)
    if "MountingHole" in f: return (7.0, 7.0)
    return (6.0, 6.0)

def place_all(m, outline_pts):
    """Modules keep their coordinates; everything else spirals to the nearest free spot."""
    def inside(x, y):
        c = False; n = len(outline_pts)
        for i in range(n):
            x0, y0 = outline_pts[i]; x1, y1 = outline_pts[(i + 1) % n]
            if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0: c = not c
        return c
    EDGE = 1.0     # extra margin from the board edge
    def fits(x, y, w, h):
        hw, hh = w / 2 + EDGE, h / 2 + EDGE
        for cx, cy in ((x - hw, y - hh), (x + hw, y - hh), (x + hw, y + hh), (x - hw, y + hh), (x, y - hh), (x, y + hh), (x - hw, y), (x + hw, y)):
            if not inside(cx, cy): return False
            if math.hypot(cx, cy) < m.AXLE_KEEPOUT_R + 1.0: return False
        return True
    placed = []   # (x, y, w, h)
    for (tx, ty, sz, text) in getattr(m, "BOARD_TEXT_FRONT", []):   # silkscreen text reserves space
        placed.append((tx, ty, len(text) * sz * 0.8 + 1, sz * 1.4))
    def overlaps(x, y, w, h, gap=0.6):
        for (px, py, pw, ph) in placed:
            if abs(x - px) < (w + pw) / 2 + gap and abs(y - py) < (h + ph) / 2 + gap: return True
        return False
    order = {"module": 0, "hole": 0, "connector": 1, "ic_generic": 2, "ic3": 2, "fuse": 3, "diode": 3, "cap": 4, "res": 5}
    moved = []
    def area(c):
        w, h = courtyard_size(c["fp"]); return -(w * h)
    for c in sorted(m.COMPONENTS, key=lambda c: (0 if c["kind"] == "hole" else 1, area(c))):
        w, h = courtyard_size(c["fp"])
        if c.get("rot", 0) in (90, 270): w, h = h, w
        x0, y0 = c["at"]
        if c["kind"] == "hole":
            placed.append((x0, y0, w, h)); continue
        best = None
        # spiral search: radius 0..45 mm, 1 mm rings, 16..64 angles
        for r in [0] + [i * 1.0 for i in range(1, 46)]:
            steps = 1 if r == 0 else max(16, int(2 * math.pi * r / 1.0))
            for k in range(steps):
                a = 2 * math.pi * k / steps
                x, y = x0 + r * math.cos(a), y0 + r * math.sin(a)
                if fits(x, y, w, h) and not overlaps(x, y, w, h):
                    best = (x, y); break
            if best: break
        if best is None:
            sys.stderr.write(f"[warn] no free spot for {c['ref']} — left at preferred position\n"); best = (x0, y0)
        if best != (x0, y0): moved.append((c["ref"], x0, y0, round(best[0], 1), round(best[1], 1)))
        c["at"] = (round(best[0], 2), round(best[1], 2))
        placed.append((c["at"][0], c["at"][1], w, h))
    return moved

# ------------------------------------------------------------------ PCB
def read_dxf_polyline(path):
    pts = []; toks = open(path).read().split("\n")
    i = 0
    while i < len(toks) - 1:
        if toks[i].strip() == "10":
            x = float(toks[i + 1]); y = float(toks[i + 3]); pts.append((x, y)); i += 4
        else: i += 1
    return pts

def write_pcb(m, outbase, outline_dxf, origin=(150.0, 150.0)):
    comps = m.COMPONENTS
    outline_pts = read_dxf_polyline(outline_dxf)
    moved = place_all(m, outline_pts)
    if moved:
        sys.stderr.write(f"[place] {len(moved)} parts moved to free spots (largest shift "
                         f"{max(math.hypot(a-x, b-y) for _, x, y, a, b in moved):.1f} mm)\n")
    netnum, nets = net_table(comps)
    ox, oy = origin
    def P2(x, y): return ox + x, oy - y           # KiCad Y is down
    L = [f'(kicad_pcb (version 20240108) (generator "zclass_gen") (generator_version "1.0")',
         '  (general (thickness 1.6) (legacy_teardrops no))', '  (paper "A3")',
         '  (layers (0 "F.Cu" signal) (31 "B.Cu" signal) (32 "B.Adhes" user "B.Adhesive") (33 "F.Adhes" user "F.Adhesive")',
         '    (34 "B.Paste" user) (35 "F.Paste" user) (36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen")',
         '    (38 "B.Mask" user) (39 "F.Mask" user) (40 "Dwgs.User" user "User.Drawings") (41 "Cmts.User" user "User.Comments")',
         '    (42 "Eco1.User" user "User.Eco1") (43 "Eco2.User" user "User.Eco2") (44 "Edge.Cuts" user) (45 "Margin" user)',
         '    (46 "B.CrtYd" user "B.Courtyard") (47 "F.CrtYd" user "F.Courtyard") (48 "B.Fab" user) (49 "F.Fab" user))',
         '  (setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints no)',
         '    (pcbplotparams (layerselection 0x00010fc_ffffffff) (plot_on_all_layers_selection 0x0000000_00000000) (disableapertmacros no) (usegerberextensions no) (usegerberattributes yes) (usegerberadvancedattributes yes) (creategerberjobfile yes) (dashed_line_dash_ratio 12.000000) (dashed_line_gap_ratio 3.000000) (svgprecision 4) (plotframeref no) (viasonmask no) (mode 1) (useauxorigin no) (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15.000000) (pdf_front_fp_property_popups yes) (pdf_back_fp_property_popups yes) (dxfpolygonmode yes) (dxfimperialunits yes) (dxfusepcbnewfont yes) (psnegative no) (psa4output no) (plotreference yes) (plotvalue yes) (plotfptext yes) (plotinvisibletext no) (sketchpadsonfab no) (subtractmaskfromsilk no) (outputformat 1) (mirror no) (drillshape 1) (scaleselection 1) (outputdirectory "")))',
         '  (net 0 "")']
    for n, i in sorted(netnum.items(), key=lambda kv: kv[1]):
        L.append(f'  (net {i} {q(n)})')
    # footprints
    for c in comps:
        x, y = P2(*c["at"]); rot = c.get("rot", 0); side = c.get("side", "F")
        lib = find_lib_footprint(c["fp"])
        if lib:
            try:
                L.append("  " + embed_lib_footprint(lib, c, netnum, x, y, rot, side)); continue
            except Exception as ex:
                sys.stderr.write(f"[warn] {c['ref']}: library footprint failed ({ex}); using generic\n")
        L.append(generic_footprint(c, netnum, x, y, rot, side))
    # outline
    pts = read_dxf_polyline(outline_dxf)
    poly = " ".join(f"(xy {P2(x, y)[0]:.3f} {P2(x, y)[1]:.3f})" for x, y in pts)
    L.append(f'  (gr_poly (pts {poly}) (stroke (width 0.1) (type default)) (fill none) (layer "Edge.Cuts") (uuid {q(U("edge"))}))')
    cx, cy = P2(0, 0)
    L.append(f'  (gr_circle (center {cx:.3f} {cy:.3f}) (end {cx + m.AXLE_KEEPOUT_R:.3f} {cy:.3f}) (stroke (width 0.1) (type default)) (fill none) (layer "Edge.Cuts") (uuid {q(U("axle"))}))')
    L.append(f'  (gr_circle (center {cx:.3f} {cy:.3f}) (end {cx + m.DEEP_ZONE_R:.3f} {cy:.3f}) (stroke (width 0.15) (type dash)) (fill none) (layer "Cmts.User") (uuid {q(U("deep"))}))')
    L.append(f'  (gr_text "deep zone r42.5: +20 mm height" (at {cx:.3f} {cy - m.DEEP_ZONE_R - 2:.3f} 0) (layer "Cmts.User") (uuid {q(U("deeptxt"))}) (effects (font (size 1.5 1.5) (thickness 0.2))))')
    L.append(f'  (gr_text "AXLE" (at {cx:.3f} {cy:.3f} 0) (layer "Cmts.User") (uuid {q(U("axletxt"))}) (effects (font (size 3 3) (thickness 0.4))))')
    import subprocess, datetime
    try: git = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip() or "nogit"
    except Exception: git = "nogit"
    today = datetime.date.today().isoformat()
    for (tx, ty, sz, text) in getattr(m, "BOARD_TEXT_FRONT", []):
        px, py = P2(tx, ty); text = text.replace("${GIT}", git).replace("${DATE}", today)
        L.append(f'  (gr_text {q(text)} (at {px:.3f} {py:.3f} 0) (layer "F.SilkS") (uuid {q(U("ftxt", text))}) (effects (font (size {sz} {sz}) (thickness {sz*0.15:.2f})) (justify left)))'.replace("(justify left)", ""))
    for (tx, ty, sz, text) in getattr(m, "BOARD_TEXT_BACK", []):
        px, py = P2(tx, ty); text = text.replace("${GIT}", git).replace("${DATE}", today)
        L.append(f'  (gr_text {q(text)} (at {px:.3f} {py:.3f} 0) (layer "B.SilkS") (uuid {q(U("btxt", text))}) (effects (font (size {sz} {sz}) (thickness {sz*0.15:.2f})) (justify mirror)))')
    # GND pours both sides
    for layer in ("F.Cu", "B.Cu"):
        L.append(f'  (zone (net {netnum["GND"]}) (net_name "GND") (layer {q(layer)}) (uuid {q(U("zone", layer))}) (hatch edge 0.5)'
                 f' (connect_pads (clearance 0.3)) (min_thickness 0.25) (filled_areas_thickness no)'
                 f' (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5)) (polygon (pts {poly})))')
    L.append(')')
    with open(outbase + ".kicad_pcb", "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    return netnum

def write_project(outbase):
    pro = {"board": {"design_settings": {"rules": {"min_clearance": 0.15, "min_track_width": 0.2, "min_via_diameter": 0.6, "min_via_drill": 0.3}},
                     "layer_presets": [], "viewports": []},
           "boards": [], "cvpcb": {"equivalence_files": []}, "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
           "meta": {"filename": os.path.basename(outbase) + ".kicad_pro", "version": 1},
           "net_settings": {"classes": [{"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.8, "via_drill": 0.4},
                                        {"name": "Power", "clearance": 0.3, "track_width": 1.5, "via_diameter": 1.2, "via_drill": 0.6}],
                            "netclass_assignments": {}, "netclass_patterns": [
                                {"netclass": "Power", "pattern": "+*"}, {"netclass": "Power", "pattern": "VIN"}, {"netclass": "Power", "pattern": "BAT*"},
                                {"netclass": "Power", "pattern": "AMP_12V*"}, {"netclass": "Power", "pattern": "SPK_*"}, {"netclass": "Power", "pattern": "CHG*"}]},
           "pcbnew": {"page_layout_descr_file": ""}, "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
           "sheets": [], "text_variables": {}}
    with open(outbase + ".kicad_pro", "w", encoding="utf-8") as f:
        json.dump(pro, f, indent=2)

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    m = load_netlist(sys.argv[1]); outbase = sys.argv[2]
    project = os.path.basename(outbase)
    os.makedirs(os.path.dirname(outbase) or ".", exist_ok=True)
    outline = os.path.join(os.path.dirname(outbase), "board_outline_draft.dxf")
    write_schematic(m, outbase, project)
    netnum = write_pcb(m, outbase, outline)
    write_project(outbase)
    libs = sum(1 for c in m.COMPONENTS if find_lib_footprint(c["fp"]))
    print(f"wrote {outbase}.kicad_sch / .kicad_pcb / .kicad_pro — {len(m.COMPONENTS)} parts, {len(netnum)} nets, "
          f"{libs} library footprints embedded, {len(m.COMPONENTS) - libs} generated")

if __name__ == "__main__":
    main()
