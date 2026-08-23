#!/usr/bin/env python3
"""strip_courtyard.py <board.kicad_pcb> REF [REF ...]
Remove the courtyard graphics of the named footprints, mark them allow_missing_courtyard and hide
their reference text. Text-level edit (KiCad's python invalidates footprint handles mid-edit).
Use for socket rows of one module that legitimately abut each other."""
import sys

def block_end(s, i):
    depth = 0; j = i; instr = False
    while j < len(s):
        c = s[j]
        if c == '"' and s[j - 1] != '\\':
            instr = not instr
        elif not instr:
            if c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0: return j + 1
        j += 1
    raise ValueError("unbalanced")

def main():
    p = sys.argv[1]; refs = set(sys.argv[2:])
    s = open(p, encoding="utf-8").read()
    out = []; i = 0; edited = 0
    while True:
        k = s.find("\n\t(footprint ", i)
        if k < 0: out.append(s[i:]); break
        k += 1; e = block_end(s, k); blk = s[k:e]; out.append(s[i:k])
        if any(f'(property "Reference" "{r}"' in blk for r in refs):
            nb = []; j = 0
            while True:
                m = blk.find("\n\t\t(fp_", j)
                if m < 0: nb.append(blk[j:]); break
                m += 1; me = block_end(blk, m); child = blk[m:me]
                nb.append(blk[j:m])
                if '(layer "F.CrtYd")' not in child and '(layer "B.CrtYd")' not in child: nb.append(child)
                j = me
            blk = "".join(nb)
            if "allow_missing_courtyard" not in blk:
                blk = blk.replace("(attr through_hole)", "(attr through_hole allow_missing_courtyard)", 1)
            ri = blk.find('(property "Reference"'); re_ = block_end(blk, ri); ref = blk[ri:re_]
            if "(hide yes)" not in ref:
                ref = ref.replace('(layer "F.SilkS")', '(layer "F.SilkS")\n\t\t\t(hide yes)', 1)
            blk = blk[:ri] + ref + blk[re_:]; edited += 1
        out.append(blk); i = e
    open(p, "w", encoding="utf-8").write("".join(out))
    print("footprints edited:", edited)

if __name__ == "__main__":
    main()
