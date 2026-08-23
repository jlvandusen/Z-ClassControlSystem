#!/usr/bin/env python3
"""jlc_outputs.py — JLCPCB-format CPL + BOM from a board. KiCad python.
   python jlc_outputs.py <board.kicad_pcb> <bom.csv> <outdir>
CPL : Designator, Mid X, Mid Y, Layer, Rotation   (mm, Y up, Top/Bottom)
BOM : Comment, Designator, Footprint, LCSC Part #  (one row per value+footprint, designators comma-joined)
Socketed modules and the mounting holes are excluded (customer fits those)."""
import sys, os, csv, re, pcbnew
SKIP = {"U3","PS1","PS2","H1","H2","H3","H4","H5"}   # socketed modules + mounting holes
def expand(refs):
    out=[]
    for part in re.split(r"\s*\+\s*|\s*,\s*", refs.strip()):
        m=re.match(r"([A-Z_]+)(\d+)-([A-Z_]+)?(\d+)$", part)
        if m: out += [f"{m.group(1)}{i}" for i in range(int(m.group(2)), int(m.group(4))+1)]
        elif part: out.append(part)
    return out
def main():
    pcb, bom, outdir = sys.argv[1], sys.argv[2], sys.argv[3]; os.makedirs(outdir, exist_ok=True)
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
    # CPL
    with open(os.path.join(outdir,"cpl_jlcpcb.csv"),"w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["Designator","Mid X","Mid Y","Layer","Rotation"]); n=0
        for fp in fps:
            r=fp.GetReference()
            if r in SKIP: continue
            # JLC wants the part CENTROID, not KiCad's footprint origin (which is pin 1 on library headers/sockets)
            c = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
            bb = c.BBox() if c.OutlineCount() else None
            if bb is None:   # no courtyard: centre of the pads
                xs=[q.GetPosition().x for q in fp.Pads()]; ys=[q.GetPosition().y for q in fp.Pads()]
                cx, cy = (min(xs)+max(xs))//2, (min(ys)+max(ys))//2
            else: cx, cy = bb.GetCenter().x, bb.GetCenter().y
            rot = fp.GetOrientation().AsDegrees()
            fpn = str(fp.GetFPID().GetLibItemName())
            if fpn.startswith(("PinSocket_", "PinHeader_")):   # JLC's header/socket models lie along X at 0 deg; KiCad's run along Y
                rot = (rot + 90.0) % 360.0
            w.writerow([r, f"{(cx-ao.x)/1e6:.3f}mm", f"{(ao.y-cy)/1e6:.3f}mm", "Bottom" if fp.IsFlipped() else "Top", f"{rot:.1f}"]); n+=1
    # BOM grouped by (comment, footprint)
    groups={}
    for fp in fps:
        r=fp.GetReference()
        if r in SKIP: continue
        val, part, pkg = mpn.get(r, (fp.GetValue(), "", ""))
        comment = f"{val} {part}".strip() if part else val
        cnum = ""
        if r in lcsc: comment, cnum = lcsc[r]
        key=(comment, str(fp.GetFPID().GetLibItemName()), cnum); groups.setdefault(key, []).append(r)
    with open(os.path.join(outdir,"bom_jlcpcb.csv"),"w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["Comment","Designator","Footprint","JLCPCB Part #"])
        for (comment, fpn, cnum), refs in groups.items(): w.writerow([comment, ",".join(refs), fpn, cnum])
    print(f"CPL: {n} placements; BOM: {len(groups)} lines  -> {outdir}")
if __name__ == "__main__": main()
