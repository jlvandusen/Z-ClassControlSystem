#!/usr/bin/env python3
"""
sync_parts.py — bring a hand-placed board up to date with a freshly generated one
WITHOUT touching placement. Run with KiCad's bundled python (pcbnew).

  python sync_parts.py <user_board.kicad_pcb> <generated_board.kicad_pcb> [--dry-run]

  user board      = source of truth for PLACEMENT (positions, rotations, side, text,
                    graphics). Edited in the KiCad GUI; close it in pcbnew before running.
  generated board = source of truth for PARTS and NETS (which refs exist, which
                    footprint each uses, which net each pad carries).

For every reference in the generated board:
  - exists in user board, same footprint  -> pad nets updated in place
  - exists in user board, different fp    -> footprint replaced at the same position/rotation/side
  - missing from user board               -> added, staged in a row BELOW the board outline
References in the user board but not in the generated board are removed.
Zones are refilled at the end. Writes <user_board>.bak first.
"""
import sys, os, shutil, pcbnew

def net_for(board, name, cache):
    if not name:
        return None
    if name in cache:
        return cache[name]
    n = board.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
    cache[name] = n
    return n

def fpid(fp):
    return str(fp.GetFPID().GetLibItemName())   # name only: library nickname may differ (GUI-placed vs embedded)

def geom(fp):
    """Pad geometry signature relative to the footprint origin, rotation-normalised, so a
    regenerated footprint with the same NAME but different pads is treated as a swap."""
    o = fp.GetPosition(); a = fp.GetOrientation()
    sig = []
    for p in fp.Pads():
        d = p.GetPosition() - o
        # undo footprint rotation so boards placed at different angles compare equal
        v = pcbnew.VECTOR2I(d.x, d.y); v = pcbnew.VECTOR2I(*_rot(v.x, v.y, a.AsDegrees()))   # KiCad y-down: +angle here undoes the placement rotation
        sig.append((str(p.GetPadName()), round(v.x / 1e4), round(v.y / 1e4), round(p.GetSizeX() / 1e4), round(p.GetSizeY() / 1e4)))
    return tuple(sorted(sig))

def _rot(x, y, deg):
    import math
    r = math.radians(deg); c, s = math.cos(r), math.sin(r)
    return (int(round(x * c - y * s)), int(round(x * s + y * c)))

def clone_into(board, src_fp, cache, pos=None, rot=None, flip_to_back=None):
    fp = src_fp.Duplicate(False).Cast()   # KiCad 10: Duplicate(addToParentGroup)
    # remap every pad net by NAME into the target board's net table
    for p in fp.Pads():
        nm = p.GetNetname()
        n = net_for(board, nm, cache)
        if n is not None:
            p.SetNet(n)
        else:
            p.SetNetCode(0)
    board.Add(fp)
    if pos is not None:
        fp.SetPosition(pos)
    if rot is not None:
        fp.SetOrientation(rot)
    if flip_to_back is not None and fp.IsFlipped() != flip_to_back:
        fp.Flip(fp.GetPosition(), False)
    return fp

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    user_path, gen_path = os.path.abspath(args[0]), os.path.abspath(args[1])
    U = pcbnew.LoadBoard(user_path)
    G = pcbnew.LoadBoard(gen_path)
    cache = {}
    ufp = {f.GetReference(): f for f in U.GetFootprints()}
    gfp = {f.GetReference(): f for f in G.GetFootprints()}

    updated, replaced, added, removed = [], [], [], []

    # staging row for new parts: below the board outline, left to right
    bb = U.GetBoardEdgesBoundingBox()
    sx, sy = bb.GetLeft() + pcbnew.FromMM(10), bb.GetBottom() + pcbnew.FromMM(12)

    for ref, g in gfp.items():
        u = ufp.get(ref)
        if u is None:
            if not dry:
                clone_into(U, g, cache, pos=pcbnew.VECTOR2I(int(sx), int(sy)), rot=g.GetOrientation(), flip_to_back=g.IsFlipped())
            added.append(ref); sx += pcbnew.FromMM(8)
        elif fpid(u) != fpid(g) or geom(u) != geom(g):
            if not dry:
                pos, rot, flipped = u.GetPosition(), u.GetOrientation(), u.IsFlipped()
                U.Remove(u)
                clone_into(U, g, cache, pos=pos, rot=rot, flip_to_back=flipped)
            replaced.append(f"{ref} {fpid(u)} -> {fpid(g)}")
        else:
            changed = False
            gp = {p.GetPadName(): p.GetNetname() for p in g.Pads()}
            for p in u.Pads():
                want = gp.get(p.GetPadName(), "")
                if p.GetNetname() != want:
                    changed = True
                    if not dry:
                        n = net_for(U, want, cache)
                        if n is not None: p.SetNet(n)
                        else: p.SetNetCode(0)
            if changed: updated.append(ref)
            # keep value text in sync (e.g. "5A fuse", "Charger ..." labels)
            if u.GetValue() != g.GetValue() and not dry:
                u.SetValue(g.GetValue())

    for ref, u in ufp.items():
        if ref not in gfp:
            if not dry: U.Remove(u)
            removed.append(ref)

    print(f"pad-nets updated : {len(updated)}  {updated}")
    print(f"footprint swapped: {len(replaced)}  {replaced}")
    print(f"added (staged)   : {len(added)}  {added}")
    print(f"removed          : {len(removed)}  {removed}")
    if dry:
        print("dry run - nothing written"); return
    # drop nets that no longer have any pad (e.g. CHG*), then refill pours
    U.BuildConnectivity()
    pcbnew.ZONE_FILLER(U).Fill(U.Zones())
    shutil.copy2(user_path, user_path + ".bak")
    pcbnew.SaveBoard(user_path, U)
    print(f"saved {user_path}  (backup: {user_path}.bak)")

if __name__ == "__main__":
    main()
