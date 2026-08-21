# Mechanical references for the v10 boards

Drop STL / STEP files here (or tell Claude the folder path). What matters, in order:

1. **Axle-area cover / casing** — the interior is the keep-in volume for the mainboard.
2. **Flywheel deck** where the v9.15 electronics mount today. The v9.15 mainboard is
   116.51 × 63.49 mm with 4 × ⌀3.0 holes at (0.24, 3.10), (0.24, 60.25), (110.73, 3.10),
   (110.73, 60.25) mm from its outline origin (reconstructed from the Gerbers). Reusing that
   pattern means the deck needs no change.
3. A sentence on which way is up / outboard and where cables enter.
4. Max component height under the cover (DevKit socketed = 13 mm; Pololu bucks ≈ 10 mm;
   MAX9744 + caps ≈ 6 mm; RP2350-Zero socketed = 9 mm).
5. Photos with a ruler.

Measure and slice any STL with:

```powershell
py tools\hw\stl_slice.py hardware\mechanical\cover.stl                    # bounding box
py tools\hw\stl_slice.py hardware\mechanical\cover.stl --axis z --steps 6 # 6 cross-sections as PNG
py tools\hw\stl_slice.py hardware\mechanical\cover.stl --proj             # top/front/side silhouettes
```

PNGs have a 10 mm grid; the `_slices.txt` lists the exact extents of each cut.
