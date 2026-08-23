#!/usr/bin/env python3
"""jlc_outputs.py — JLCPCB-format CPL + BOM from a board. KiCad python.
   python jlc_outputs.py <board.kicad_pcb> <bom.csv> <outdir> [--scope full|smd|fine] [--exclude R1,C3,...] [--boards N]
CPL : Designator, Mid X, Mid Y, Layer, Rotation   (mm, Y up, Top/Bottom)
BOM : Comment, Designator, Footprint, LCSC Part #  (one row per value+footprint, designators comma-joined)
Socketed modules and the mounting holes are always excluded (customer fits those).

--scope full  (default) everything JLC can place
--scope smd   only surface-mount parts — every through-hole part is left for the customer
--scope fine  only the parts that genuinely need a machine: fine-pitch ICs, the PowerPAK FET, SOT-23,
              the 1 mm JST-SH, and the 0603 passives (cheap "basic" parts, tedious by hand)
--exclude     extra refs to leave out of the JLC files (comma-separated)
Whatever is left out is written to hand_solder_bom.csv (with LCSC numbers and the quantity for --boards boards)."""
import sys, os, csv, re, pcbnew
SKIP = {"U3","PS1","PS2","H1","H2","H3","H4","H5"}   # socketed modules + mounting holes
# KiCad -> JLCPCB rotation offsets (degrees, CCW-positive like the CPL). KiCad library footprints and JLC's
# part models do not agree on where 0 deg is; JLC's placement preview shows pin 1 as a purple dot.
# Base values from JLCKicadTools cpl_rotations_db.csv (matthewlai); "verified" = checked in JLC's preview by James.
ROT_FIX = [
    (r"^PinSocket_|^PinHeader_", 90),     # verified 2026-08-23 (JLC's strip models lie along X)
    (r"^SOT-223", 180),                   # U5 AMS1117 — verified 2026-08-23 ("counter 2x")
    (r"^TSSOP-", 270),                    # U6, U7 — table value; James turned them clockwise in the viewer (amount to confirm)
    (r"^PowerPAK_SO-8_Single", 270),      # Q1 — table value; turned clockwise in the viewer (amount to confirm)
    (r"^SOT-23", -90),                    # generic table value for SOT-23 — overridden per part below (D4)
    (r"^CP_Elec_8x10|^CP_Elec_10x10", 180),  # electrolytics (only in full scope)
]
# Per-part overrides keyed by LCSC number: JLC's model orientation belongs to the PART, not the footprint.
# Verified in JLC's placement preview by James, 2026-08-23 (r7 machine-only upload, every part at 0 in the CPL):
#   U6 C36365 TSSOP-14 +270 (1 click CW) . U7 C6082 TSSOP-20 +270 . Q1 C553968 PowerPAK +270 . U5 C6186 SOT-223 +180 (2 clicks)
#   D2 C2480 SS14 0 . J17 C160404 JST-SH 0 . D4 below.
ROT_FIX_PART = {
    "C47546": 180,   # D4 BAT54S: JLC's model stands vertical with pins 1-2 on the RIGHT -> 180 (2 clicks), NOT the table's -90
}
# footprints a hobby iron handles easily: leave these out of the "fine" scope
EASY_SMD = ("CP_Elec_", "D_SMB", "D_SMA", "D_SOD-123", "Fuse_1812", "Fuse_1206", "L_1206", "C_1206", "SOT-223")

def expand(refs):
    out=[]
    for part in re.split(r"\s*\+\s*|\s*,\s*", refs.strip()):
        m=re.match(r"([A-Z_]+)(\d+)-([A-Z_]+)?(\d+)$", part)
        if m: out += [f"{m.group(1)}{i}" for i in range(int(m.group(2)), int(m.group(4))+1)]
        elif part: out.append(part)
    return out

def is_tht(fp):
    return any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads() if p.GetNumber() != "")

def main():
    args = sys.argv[1:]
    scope = "full"; extra = set(); boards = 5
    if "--scope" in args: scope = args[args.index("--scope")+1]
    if "--exclude" in args: extra = set(expand(args[args.index("--exclude")+1]))
    if "--boards" in args: boards = int(args[args.index("--boards")+1])
    pos = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i-1].startswith("--"))]
    pcb, bom, outdir = pos[0], pos[1], pos[2]; os.makedirs(outdir, exist_ok=True)
    b = pcbnew.LoadBoard(pcb)
    ao = b.GetDesignSettings().GetAuxOrigin()   # same origin as the Gerber/drill export
    mpn = {}
    lcsc = {}
    lm = os.path.join(os.path.dirname(bom), "lcsc_map.csv")
    if os.path.exists(lm):
        for row in csv.DictReader(open(lm, encoding="utf-8")): lcsc[row["Ref"]] = (row["Comment"], row["LCSC"])
    for row in csv.DictReader(open(bom, encoding="utf-8")):
        for r in expand(row["Ref"]): mpn[r] = (row["Value / Part"], row["MPN"], row["Package"])
    fps = sorted(b.GetFootprints(), key=lambda f: (re.sub(r"\d+","",f.GetReference()), int(re.sub(r"\D","",f.GetReference()) or 0)))

    def customer(fp):
        """True when this part is NOT sent to JLC in the chosen scope."""
        r = fp.GetReference(); fpn = str(fp.GetFPID().GetLibItemName())
        if r in SKIP or r in extra: return True
        if scope == "full": return False
        if is_tht(fp): return True
        if scope == "fine" and fpn.startswith(EASY_SMD): return True
        return False

    # CPL
    with open(os.path.join(outdir,"cpl_jlcpcb.csv"),"w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["Designator","Mid X","Mid Y","Layer","Rotation"]); n=0
        for fp in fps:
            r=fp.GetReference()
            if customer(fp): continue
            # JLC wants the part CENTROID, not KiCad's footprint origin (which is pin 1 on library headers/sockets)
            c = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
            bb = c.BBox() if c.OutlineCount() else None
            if bb is None:   # no courtyard: centre of the pads
                xs=[q.GetPosition().x for q in fp.Pads()]; ys=[q.GetPosition().y for q in fp.Pads()]
                cx, cy = (min(xs)+max(xs))//2, (min(ys)+max(ys))//2
            else: cx, cy = bb.GetCenter().x, bb.GetCenter().y
            rot = fp.GetOrientation().AsDegrees()
            fpn = str(fp.GetFPID().GetLibItemName())
            off = ROT_FIX_PART.get(lcsc.get(r, ("", ""))[1])
            if off is None:
                off = next((o for pat, o in ROT_FIX if re.search(pat, fpn)), 0)
            rot = (rot + (-off if fp.IsFlipped() else off)) % 360.0
            w.writerow([r, f"{(cx-ao.x)/1e6:.3f}mm", f"{(ao.y-cy)/1e6:.3f}mm", "Bottom" if fp.IsFlipped() else "Top", f"{rot:.1f}"]); n+=1
    # BOM grouped by (comment, footprint) — JLC side and customer side
    groups={}; hand={}
    for fp in fps:
        r=fp.GetReference()
        if r in SKIP: continue
        val, part, pkg = mpn.get(r, (fp.GetValue(), "", ""))
        comment = f"{val} {part}".strip() if part else val
        cnum = ""
        if r in lcsc: comment, cnum = lcsc[r]
        key=(comment, str(fp.GetFPID().GetLibItemName()), cnum, "THT" if is_tht(fp) else "SMT")
        (hand if customer(fp) else groups).setdefault(key, []).append(r)
    with open(os.path.join(outdir,"bom_jlcpcb.csv"),"w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["Comment","Designator","Footprint","JLCPCB Part #"])
        for (comment, fpn, cnum, _), refs in groups.items(): w.writerow([comment, ",".join(refs), fpn, cnum])
    if hand:
        with open(os.path.join(outdir,"hand_solder_bom.csv"),"w",newline="",encoding="utf-8") as f:
            w=csv.writer(f); w.writerow(["Comment","Designator","Footprint","Mount","LCSC Part #","Qty per board",f"Qty for {boards} boards (+10%)"])
            for (comment, fpn, cnum, mount), refs in hand.items():
                q = len(refs); w.writerow([comment, ",".join(refs), fpn, mount, cnum, q, -(-q*boards*11//10)])
    jl = sum(len(v) for v in groups.values()); hl = sum(len(v) for v in hand.values())
    print(f"scope={scope}: JLC places {n} parts in {len(groups)} BOM lines; customer solders {hl} parts in {len(hand)} lines  -> {outdir}")
if __name__ == "__main__": main()
