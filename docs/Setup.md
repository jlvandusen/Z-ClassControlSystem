

---
## Updates (2026-08-20 evening)
- **Drive Enable is now CIRCLE on the drive controller** (PS still works as backup;
  some controllers don't report the PS button through Bluepad32). Silent-mode
  sound moved to **L1+CIRCLE**.
- **`version` command on every board**; the 32u4 and Trinket also print their
  banner the moment a monitor attaches, and `bb8 monitor` auto-asks each board
  for its version on connect — so you always see which firmware you're talking to.
- **32u4/Trinket upload "butterfly_recv failed"**: bootloader race. bb8 now
  retries automatically; manual fallback = double-tap reset (LED pulses), then
  `bb8 list` (bootloader may be a different COM) and `bb8 upload body --port COMx`.
