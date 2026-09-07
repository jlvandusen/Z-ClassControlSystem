# Z-Class Drive — Per-Area Assembly Guides

*Generated from Assembly_Drive.md — expand the [TODO: builder] items with your build's specifics.*

These are **per-area builder guides**, one Markdown file per drive sub-assembly, meant to be handed to a builder and expanded into full per-part guides in **Word (.docx)**. They are split by area so each can grow independently as the builder adds fastener specs, torque values, wire gauges, and photos.

**Single source of truth:** [`../Assembly_Drive.md`](../Assembly_Drive.md) — the Z-Class Drive mechanical assembly guide generated from the Fusion 360 model `Z-Class Drive-v4.1`. Everything in these files comes from that document. Where the model lacks a detail (exact fastener, torque, wire gauge, or a photo), the guides carry a `**[TODO: builder]**` placeholder, and the source's own `[VERIFY]` flags are preserved verbatim.

## The six areas

| # | Guide | Area | Key ratios (source §0) |
|---|---|---|---|
| A | [A_Gantry.md](A_Gantry.md) | Gantry — the structural core (casings, speakers, drive motor mount) | — |
| B | [B_DriveGearTrain.md](B_DriveGearTrain.md) | Drive gear train (26T→47T + chain to shell) | 26→47 = **1.81 : 1**; chain ≈ **4.2 : 1** |
| C | [C_S2S_Steering.md](C_S2S_Steering.md) | S2S (steering) gears — leans the gantry to steer | 22→69 = **3.14 : 1** |
| D | [D_SwingArms_Feedback.md](D_SwingArms_Feedback.md) | Swing arms + position feedback (B10K pot on GPIO34) | GantryPot→Pot = **1 : 1.20** |
| E | [E_Flywheel_Ballast.md](E_Flywheel_Ballast.md) | Flywheel & ballast deck — the balance pendulum | — |
| F | [F_HeadTilt_DomeSpin.md](F_HeadTilt_DomeSpin.md) | Head tilt & dome spin (mast, coupler, servos) | — |

## How to use these

1. Read the relevant area file end-to-end before starting that sub-assembly.
2. Build in the integration order from the source (§7): **A → B → C → D → E → F**, then electronics and bench verification.
3. Resolve every `[VERIFY]` and `**[TODO: builder]**` against the physical parts as you build, and capture the `**[TODO: photo]**` shots — those become the per-part detail in the .docx expansion.
4. For operating/tuning the finished drive, see the source's companion guides: `../HowToGuide.md` and `../RigTuning.md`.

## Coverage note

Each area file lists **every part the manifest (§8 of the source) assigns to that area**, including the navigator-linked purchased parts. A few source-listed items are not owned by any single drive sub-assembly and are flagged in the area files or left to integration: the **6-inch main bearing** (`6inchBearing.step`), the **shell + magnets**, and the **electronics boards** (wired per the Runbook at integration). The **535051 1-inch bearing** is used **4× total** — 2 in the Gantry (A, one each side) and 2 in the Head-Tilt Coupler (F, one each side).
