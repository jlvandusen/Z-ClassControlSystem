# Fabrication outputs for the v10 mainboard (run after routing).
#   .	ools\hwab_outputs.ps1 extended      (or: compact)
# Produces hardware/fab/<board>-<yyyymmdd>/: gerbers.zip, drill files, pick-and-place CSV, BOM CSV, PDFs.
param([ValidateSet('extended','compact')][string]$Board = 'extended')
$ErrorActionPreference = 'Stop'
$cli = Get-ChildItem "$env:LOCALAPPDATA\Programs\KiCad\*\bin\kicad-cli.exe", "C:\Program Files\KiCad\*\bin\kicad-cli.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $cli) { throw "kicad-cli not found - install KiCad (winget install KiCad.KiCad)" }
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pcb  = Join-Path $root "hardware\kicad\$Board\mainboard.kicad_pcb"
$sch  = Join-Path $root "hardware\kicad\$Board\mainboard.kicad_sch"
$out  = Join-Path $root ("hardware\fab\$Board-" + (Get-Date -Format 'yyyyMMdd'))
New-Item -ItemType Directory -Force $out | Out-Null
$g = Join-Path $out 'gerbers'; New-Item -ItemType Directory -Force $g | Out-Null

& $cli.FullName pcb export gerbers --output "$g\" --layers "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts" --subtract-soldermask --use-drill-file-origin $pcb
& $cli.FullName pcb export drill   --output "$g\" --format excellon --excellon-units mm $pcb
Compress-Archive -Path "$g\*" -DestinationPath (Join-Path $out 'gerbers.zip') -Force
& $cli.FullName pcb export pos     --output (Join-Path $out 'cpl.csv') --format csv --units mm --side both --use-drill-file-origin $pcb
& $cli.FullName pcb export pdf     --output (Join-Path $out 'assembly_top.pdf') --layers "F.Cu,F.SilkS,F.Fab,Edge.Cuts" $pcb
& $cli.FullName sch export pdf     --output (Join-Path $out 'schematic.pdf') $sch
& $cli.FullName sch export bom     --output (Join-Path $out 'bom_from_schematic.csv') --fields "Reference,Value,Footprint,Qty" --group-by "Value,Footprint" $sch
Copy-Item (Join-Path $root 'hardware\bom\v10_mainboard_bom.csv') (Join-Path $out 'bom.csv')

Write-Host "`nFab outputs in $out" -ForegroundColor Green
Get-ChildItem $out | Select-Object Name, Length | Format-Table -AutoSize
Write-Host "Reminder: the board must be ROUTED and DRC-clean before sending these out." -ForegroundColor Yellow
