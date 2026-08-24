<#
.SYNOPSIS  Flash a prebuilt release binary to a board without compiling.
.EXAMPLE   .\tools\release\Flash-Prebuilt.ps1 -Target drive -Port COM4
.NOTES     Needs the board core installed (Install-ZClass.ps1 does that) - the
           uploader (esptool / avrdude / bossac) ships with the core.
           Binaries live in binaries\<target>\ (from the release zip).
#>
param(
  [Parameter(Mandatory)][ValidateSet("drive","dome","body","imu")][string]$Target,
  [Parameter(Mandatory)][string]$Port
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\..")).Path
$j = Get-Content (Join-Path $Root "targets.json") -Raw | ConvertFrom-Json
$t = $j.targets | Where-Object { $_.name -eq $Target }
$dir = Join-Path $Root "binaries\$Target"
if (-not (Test-Path $dir)) { throw "no prebuilt binaries at $dir" }
Write-Host "[FLASH] $Target ($($t.sketch)) -> $Port  from $dir" -ForegroundColor Cyan
& arduino-cli upload -p $Port --fqbn $t.fqbn --input-dir $dir (Join-Path $Root "firmware\$($t.sketch)")
Write-Host "[FLASH] done - confirm with: bb8 monitor $Target  (banner shows the build number)" -ForegroundColor Green
