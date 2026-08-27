"""Publish docs/ to the GitHub wiki (Z-ClassControlSystem.wiki.git).

Usage: python tools/release/publish-wiki.py [--dry-run]
- Clones (or inits) the wiki repo into dist/wiki, regenerates every page from docs/
  with links rewritten to wiki page names, writes Home + Installation + _Sidebar,
  commits and pushes. Re-run after any doc change.
"""
import os, re, subprocess, sys, shutil, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, "docs")
WIKI = os.path.join(ROOT, "dist", "wiki")
URL = "https://github.com/jlvandusen/Z-ClassControlSystem.wiki.git"
REPO = "https://github.com/jlvandusen/Z-ClassControlSystem"
DRY = "--dry-run" in sys.argv

# source doc -> wiki page name (title derived from the H1 in the file)
PAGES = {
    "FirstTimeSetup.md":          "First-Time-Setup",
    "HowToGuide.md":              "How-To-Guide",
    "Runbook.md":                 "Runbook",
    "RigTuning.md":               "Rig-Tuning",
    "Assembly_Drive.md":          "Assembly-Drive",
    "CHANGELOG.md":               "Changelog",
    "BB8_RC4_Review_and_Fixes.md":"RC4-Review-and-Fixes",
    "PCB_v10_Design.md":          "v10-Board-Design",
    "PCB_v9_Analysis.md":         "v9-PCB-Analysis",
}

def rewrite_links(md: str) -> str:
    # docs-relative links -> wiki pages
    for src, page in PAGES.items():
        md = re.sub(r"\((?:docs/)?" + re.escape(src) + r"(#[^)]*)?\)", lambda m: f"({page}{m.group(1) or ''})", md)
        md = md.replace(f"`docs/{src}`", f"[{page}]({page})").replace(f"`{src}`", f"[{page}]({page})")
    # remaining repo-relative links (tools/, firmware/, hardware/, README) -> GitHub blob URLs
    md = re.sub(r"\]\(((?:tools|firmware|hardware|docs)/[^)\s]+)\)", lambda m: f"]({REPO}/blob/main/{m.group(1)})", md)
    md = md.replace("](README.md)", f"]({REPO}#readme)")
    return md

def run(*args, cwd=WIKI):
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()

# ---- get the wiki repo ----
if os.path.isdir(os.path.join(WIKI, ".git")):
    rc, out = run("git", "pull", "-q", "--rebase")
else:
    shutil.rmtree(WIKI, ignore_errors=True)
    rc, out = run("git", "clone", "-q", URL, WIKI, cwd=ROOT)
    if rc != 0:
        print("wiki repo not on GitHub yet - initializing locally (first push creates it if allowed)")
        os.makedirs(WIKI, exist_ok=True)
        run("git", "init", "-q", "-b", "master")
        run("git", "remote", "add", "origin", URL)

# ---- wipe generated pages, regenerate ----
for f in os.listdir(WIKI):
    if f.endswith(".md"):
        os.remove(os.path.join(WIKI, f))

for src, page in PAGES.items():
    p = os.path.join(DOCS, src)
    if not os.path.exists(p):
        continue
    md = open(p, encoding="utf-8").read()
    md = rewrite_links(md)
    md += f"\n\n---\n*Source: [`docs/{src}`]({REPO}/blob/main/docs/{src}) — regenerated {datetime.date.today()} by `tools/release/publish-wiki.py`. Edit the repo copy, not the wiki.*\n"
    open(os.path.join(WIKI, page + ".md"), "w", encoding="utf-8", newline="\n").write(md)
    print("page:", page)

HOME = f"""# Z-Class Control System

Firmware, tooling and documentation for the **Z-Class BB-8** drive system — a ball-bot
with an ESP32 drive brain (balance PIDs, Bluetooth gamepads), a Feather 32u4 body node
(dome tilt servos, dome spin, audio), a Trinket M0 IMU node and an ESP32 dome (lights +
the wireless console bridge). The `bb8` CLI builds, flashes, monitors, tunes and keeps
itself updated from this repo.

**Get it:** [Releases]({REPO}/releases) → run `ZClass-ControlSystem-Setup-BASIC-v*.exe` (or `-MAX-`) → see [Installation](Installation).

**The hardware** — mechanics/STLs, PCB fabrication files, BOM, wiring docs — lives in
[Z-ClassDriveSystem](https://github.com/jlvandusen/Z-ClassDriveSystem); this repo and wiki are
primary for everything control and software.

## Start here
| Page | What it's for |
|---|---|
| [Installation](Installation) | Setup.exe walkthrough, first run, verifying the install |
| [First-Time Setup](First-Time-Setup) | new build / fresh boards: flash order, radio-MAC + pad pairing, first calibration, sign checks before first enable |
| [How-To Guide](How-To-Guide) | operate the droid: power-on, controls, sounds, tuning quick path, golden rules |
| [Runbook](Runbook) | the deep operations reference: flashing, console commands for every board, controller mapping, calibration, tuning, audio, safety, troubleshooting, dome lights, wireless bridge |
| [Rig Tuning](Rig-Tuning) | the measured, repeatable tuning procedure (rollers and sealed shell) |
| [Assembly (Drive)](Assembly-Drive) | mechanical build from the Fusion 360 model |
| [Changelog](Changelog) | what changed in each release |
| [RC4 Review and Fixes](RC4-Review-and-Fixes) | why RC4 exists — the 112-finding review of RC3 |
| [v10 Board Design](v10-Board-Design) · [v9 PCB Analysis](v9-PCB-Analysis) | the next-generation mainboard and the analysis of the current one |

## The fleet at a glance
| Target | Board | Sketch |
|---|---|---|
| `drive` | ESP32 HUZZAH32 (Bluepad32 core) | `firmware/ESP32_DRIVE_RC4` |
| `body` | Feather 32u4 | `firmware/32U4_DRIVE_RC4` |
| `imu` | Trinket M0 + MPU6050 | `firmware/TrinketM0_MPU_RC4` |
| `dome` | ESP32 HUZZAH32 (stock esp32 core) | `firmware/ESP32_DOME_RC4` |
| `ball` | *(the dome's USB port)* | drive console over ESP-NOW — `bb8 monitor ball` |

## bb8 in one screen
```
bb8 list                 boards on USB           bb8 update [--flash]     pull from GitHub (+ reflash stale boards)
bb8 upload <board>       compile + flash + verify bb8 monitor <board|ball> console (--log x.csv)
bb8 flash <board>        prebuilt binary, no toolchain (BASIC)             bb8 analyze x.csv        offline tuning analysis
bb8 tune s2s|drive|dome  live closed-loop tuner  bb8 pair                 PS3 / Nav pad pairing
```
"""

INSTALL = f"""# Installation

## Which installer?

| | **Setup-BASIC** | **Setup-MAX** |
|---|---|---|
| For | driving the droid | modifying the firmware source |
| Flashing | prebuilt binaries, flashers bundled — `bb8 flash` / `bb8 upload` | compiles from source — `bb8 upload` |
| Updates | latest GitHub **release** over HTTPS — no git | git fast-forward + everything BASIC does |
| Extra setup | none | toolchain task (~1 GB, one time) + git link |

Not sure? **BASIC.** Installing MAX over it later upgrades in place.

## A. The installer (recommended)
1. Download **`ZClass-ControlSystem-Setup-BASIC-v*.exe`** (or `-MAX-`) from [Releases]({REPO}/releases).
2. Run it. It's unsigned, so Windows SmartScreen may show *"Windows protected your PC"* → **More info → Run anyway**. No administrator rights are needed.
3. Wizard:
   - **Install folder** — default `%LOCALAPPDATA%\\ZClass`. Any user-writable folder works, and the folder is relocatable.
   - **First-run setup** tasks (MAX only):
     - ☑ *Install arduino-cli + board cores + libraries now* — needs internet, ~10 min. Required before you can *compile*. (You can run it later from the Start menu: *Re-run toolchain setup*.)
     - ☑ *Link the install folder to GitHub* — needs `git` on the PC. Enables source-level `bb8 update`.
   - Optional desktop shortcut.
4. MAX: after the files copy, a **PowerShell window** opens and runs the setup you ticked — watch it finish (`=== Ready ===`). It downloads arduino-cli if you don't have it, installs the exact cores (`esp32-bluepad32` 4.1.0, `esp32` 3.3.7, Adafruit AVR 1.4.15 / SAMD 1.7.17) and the sketch libraries into a private config. BASIC: there is nothing to wait for.
5. **Open a new terminal** (PATH changes need one) and verify:
   ```
   bb8 list
   ```
   You should see the five targets and any boards on USB.

### First session
Bringing up a **new droid or fresh board set**? Follow [First-Time Setup](First-Time-Setup) end to end. The short form:
| Step | Command |
|---|---|
| pair your PS3 / Nav pads | `bb8 pair` (guided) |
| flash a board (compiles from source, verifies the banner) | `bb8 upload drive` — then `body`, `imu`, `dome` |
| or flash the bundled prebuilt binaries, no compile | `tools\\release\\Flash-Prebuilt.ps1 -Target drive -Port COM4` |
| watch a board | `bb8 monitor drive` |
| then read | [How-To Guide](How-To-Guide) |

## B. The zip (no installer)
Download `ZClass-ControlSystem-v*.zip`, extract anywhere, PowerShell in that folder:
`.\\Install-ZClass.ps1` — same setup engine (`-SkipToolchain`, `-NoGit` switches available).

## C. From source (developers)
```
git clone {REPO}
cd Z-ClassControlSystem
.\\install.ps1            # builds bb8 (needs .NET SDK 10+)
.\\tools\\release\\Install-ZClass.ps1 -SkipToolchain   # wires targets.json / PATH
```

## Uninstall
Start menu → *Z-Class Control System → Uninstall* (or Settings → Apps). Removes the install, its `build/`, `toolchain/`, `.git`, and the PATH / `BB8_HOME` entries.

## Troubleshooting the install
| Symptom | Fix |
|---|---|
| `bb8` not recognised | open a **new** terminal; or run `bb8.cmd` from the install folder |
| `arduino-cli: command not found` during upload | re-run *Re-run toolchain setup* from the Start menu |
| Core install fails / slow | corporate proxy or offline — retry on a normal connection; cores are ~1 GB |
| `[UPDATE] not a git checkout` | `git` wasn't installed when you ran setup — install git, re-run setup with the GitHub-link task |
| Board plugged in but not in `bb8 list` | driver: CP210x for the ESP32s; Adafruit boards need a data USB cable — see [Runbook](Runbook#3-build--flash--verify) |
"""

SIDEBAR = """**Z-Class Control System**
- [Home](Home)
- [Installation](Installation)
- [First-Time Setup](First-Time-Setup)
- [How-To Guide](How-To-Guide)
- [Runbook](Runbook)
- [Rig Tuning](Rig-Tuning)
- [Assembly (Drive)](Assembly-Drive)
- [Changelog](Changelog)
- [RC4 Review & Fixes](RC4-Review-and-Fixes)
- [v10 Board Design](v10-Board-Design)
- [v9 PCB Analysis](v9-PCB-Analysis)

[Releases](https://github.com/jlvandusen/Z-ClassControlSystem/releases) · [Repo](https://github.com/jlvandusen/Z-ClassControlSystem)
"""

for name, body in (("Home", HOME), ("Installation", INSTALL), ("_Sidebar", SIDEBAR)):
    open(os.path.join(WIKI, name + ".md"), "w", encoding="utf-8", newline="\n").write(body)
    print("page:", name)

if DRY:
    print("dry run - not pushing")
    sys.exit(0)

run("git", "add", "-A")
rc, out = run("git", "commit", "-q", "-m", f"wiki: regenerate from docs/ ({datetime.date.today()})")
print(out or "committed")
rc, out = run("git", "push", "-q", "-u", "origin", "HEAD:master")
if rc != 0:
    print("PUSH FAILED:\n" + out)
    print("\nIf GitHub says 'Repository not found': open the repo's Wiki tab, click "
          "'Create the first page', save it, then re-run this script.")
    sys.exit(1)
print("wiki pushed:", REPO + "/wiki")
