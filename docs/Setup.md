

---
> **Superseded notes below.** First-time bring-up lives in `docs/FirstTimeSetup.md`;
> current behaviour lives in `docs/Runbook.md` (controls §6, console §5,
> lights §15, wireless bridge §16) and `docs/CHANGELOG.md`. In particular: since RC4.3 **PS** is the
> only drive enable/disable (tap) + force-disable (hold 2 s); CIRCLE is a plain sound button.

## Updates (2026-08-20 evening)
- ~~Drive Enable is now CIRCLE on the drive controller~~ (reverted in RC4.3 — PS only). Silent-mode
  sound moved to **L1+CIRCLE**.
- **`version` command on every board**; the 32u4 and Trinket also print their
  banner the moment a monitor attaches, and `bb8 monitor` auto-asks each board
  for its version on connect — so you always see which firmware you're talking to.
- **32u4/Trinket upload "butterfly_recv failed"**: bootloader race. bb8 now
  retries automatically; manual fallback = double-tap reset (LED pulses), then
  `bb8 list` (bootloader may be a different COM) and `bb8 upload body --port COMx`.
