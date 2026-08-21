#!/usr/bin/env python3
"""
route.py — autoroute the mainboard: KiCad .kicad_pcb -> Specctra .dsn ->
Freerouting -> .ses -> back into the .kicad_pcb, then stamp GND stitching vias.
Run with KiCad's bundled python (pcbnew):

  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools\\hw\\route.py hardware\\kicad\\mainboard.kicad_pcb
"""
import sys, os, subprocess, math, glob

def find(*globs):
    for g in globs:
        for p in glob.glob(g):
            if os.path.exists(p): return p
    return None

def main():
    pcb = os.path.abspath(sys.argv[1])
    base = os.path.splitext(pcb)[0]
    out = os.path.join(os.path.dirname(pcb), "out"); os.makedirs(out, exist_ok=True)
    dsn = os.path.join(out, "mainboard.dsn")
    ses = os.path.join(out, "mainboard.ses")

    import pcbnew
    board = pcbnew.LoadBoard(pcb)

    # 1) export Specctra DSN
    ok = pcbnew.ExportSpecctraDSN(board, dsn)
    print(f"[route] DSN export: {'ok' if ok else 'FAILED'} -> {dsn}")
    if not ok: sys.exit(1)

    # 2) Freerouting (headless)
    jdks = sorted(glob.glob(r"C:\Program Files\Microsoft\jdk-*\bin\java.exe")
                  + glob.glob(r"C:\Program Files\Java\jdk-*\bin\java.exe")
                  + glob.glob(r"C:\Program Files\Eclipse Adoptium\jdk-*\bin\java.exe"), reverse=True)
    jars = sorted(glob.glob(os.path.join(os.path.dirname(__file__), "..", "freerouting", "freerouting-*.jar")), reverse=True)
    cmd = None
    for jar in jars:
        for java in jdks:
            probe = subprocess.run([java, "-jar", jar, "--help"], capture_output=True, text=True)
            if "UnsupportedClassVersionError" in (probe.stdout + probe.stderr):
                continue
            cmd = [java, "-jar", jar, "-de", dsn, "-do", ses, "-mp", "100"]
            print(f"[route] using {os.path.basename(jar)} on {java}")
            break
        if cmd:
            break
    if not cmd:
        print(f"[route] no working Freerouting/Java combo; jars={[os.path.basename(j) for j in jars]} jdks={jdks}")
        sys.exit(2)
    print("[route] running Freerouting (headless, up to a few minutes)...")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        print("[route] Freerouting timed out"); sys.exit(3)
    tail = "\n".join((r.stdout + r.stderr).splitlines()[-6:])
    print(tail)
    if not os.path.exists(ses):
        print("[route] no .ses produced — Freerouting failed"); sys.exit(3)

    # 3) import the session
    board = pcbnew.LoadBoard(pcb)
    ok = pcbnew.ImportSpecctraSES(board, ses)
    print(f"[route] SES import: {'ok' if ok else 'FAILED'}")

    # 4) GND stitching vias on a grid over the filled pours, clear of tracks/pads/vias
    stitch(board, pitch=8.0, drill=0.4, dia=0.8)

    # 5) refill zones + save
    filler = pcbnew.ZONE_FILLER(board); filler.Fill(board.Zones())
    pcbnew.SaveBoard(pcb, board)
    print(f"[route] saved {pcb}")

def stitch(board, pitch, drill, dia):
    import pcbnew
    gnd = board.FindNet("GND")
    if not gnd: print("[stitch] no GND net"); return
    box = board.GetBoardEdgesBoundingBox()
    x0, y0, x1, y1 = box.GetLeft(), box.GetTop(), box.GetRight(), box.GetBottom()
    # collision set: all pads + existing vias, with clearance
    obstacles = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            c = pad.GetPosition(); r = max(pad.GetSizeX(), pad.GetSizeY()) / 2 + pcbnew.FromMM(0.8)
            obstacles.append((c.x, c.y, r))
    for tr in board.Tracks():
        if tr.Type() == pcbnew.PCB_VIA_T:
            c = tr.GetPosition(); obstacles.append((c.x, c.y, tr.GetWidth() / 2 + pcbnew.FromMM(0.8)))
    P = pcbnew.FromMM(pitch)
    n = 0
    yy = y0
    while yy < y1:
        xx = x0
        while xx < x1:
            p = pcbnew.VECTOR2I(int(xx), int(yy))
            if on_board(board, p) and not any((xx - ox) ** 2 + (yy - oy) ** 2 < r * r for ox, oy, r in obstacles):
                v = pcbnew.PCB_VIA(board); v.SetPosition(p); v.SetDrill(pcbnew.FromMM(drill))
                v.SetWidth(pcbnew.FromMM(dia)); v.SetNet(gnd)
                v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                board.Add(v); obstacles.append((xx, yy, pcbnew.FromMM(dia))); n += 1
            xx += P
        yy += P
    print(f"[stitch] {n} GND vias @ {pitch} mm")

def on_board(board, p):
    # inside Edge.Cuts outline and not in the axle hole: use the board's filled GND zone as the mask
    import pcbnew
    for z in board.Zones():
        if z.GetNetname() == "GND" and z.HitTestFilledArea(pcbnew.F_Cu, p, 0):
            return True
    return False

if __name__ == "__main__":
    main()
