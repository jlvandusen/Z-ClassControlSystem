<#
.SYNOPSIS
  Z-Class Control System - one-shot installer for a fresh Windows PC.

.DESCRIPTION
  Run this from the folder the release zip was extracted into. It builds the
  complete, self-contained environment in place:
    1. arduino-cli (downloaded if missing) + the exact board cores this fleet
       builds on (Bluepad32 for the drive, stock esp32 3.x for the dome,
       Adafruit AVR / SAMD for body / IMU) + every sketch library.
    2. bb8 Commander (self-contained, no .NET runtime needed) on your PATH,
       with targets.json pointed at THIS folder.
    3. Optional: turns the folder into a git checkout of the GitHub repo so
       'bb8 update' can pull new firmware forever (needs git on PATH).
  Re-running is safe (everything is idempotent / version-pinned).

.PARAMETER SkipToolchain   Only wire up bb8/targets.json (arduino-cli already set up).
.PARAMETER NoGit           Don't convert the folder into a git checkout.
#>
[CmdletBinding()]
param(
  [switch]$SkipToolchain,
  [switch]$NoGit
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path (Join-Path $Root "targets.json"))) {
  # script may live in tools\release inside a repo checkout; walk up to the root
  $probe = $Root
  while ($probe -and -not (Test-Path (Join-Path $probe "targets.json"))) { $probe = Split-Path -Parent $probe }
  if (-not $probe) { throw "targets.json not found above $Root - run this from the extracted release folder." }
  $Root = $probe
}
Set-Location $Root
Write-Host "`n=== Z-Class Control System installer ===  root: $Root`n" -ForegroundColor Cyan

# ---- constants: what the fleet builds on (pinned = reproducible) ----
$BoardUrls = @(
  "https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json",
  "https://espressif.github.io/arduino-esp32/package_esp32_index.json",
  "https://adafruit.github.io/arduino-board-index/package_adafruit_index.json"
)
# arduino:avr must precede adafruit:avr (the Adafruit platform references it —
# caught by CI: a machine without it can't compile the body)
$Cores = @("esp32-bluepad32:esp32@4.1.0", "esp32:esp32@3.3.7", "arduino:avr@1.8.7", "adafruit:avr@1.4.15", "adafruit:samd@1.7.17")
$Libs  = @("SerialTransfer", "DFRobotDFPlayerMini", "Adafruit MPU6050", "Adafruit Unified Sensor",
           "Adafruit BusIO", "Kalman Filter Library", "Adafruit NeoPixel", "Servo")
$RepoUrl = "https://github.com/jlvandusen/Z-ClassControlSystem.git"

function Add-UserPath([string]$dir) {
  $cur = [Environment]::GetEnvironmentVariable("Path", "User")
  if (($cur -split ";") -notcontains $dir) {
    [Environment]::SetEnvironmentVariable("Path", ($cur.TrimEnd(";") + ";" + $dir), "User")
    Write-Host "  PATH += $dir (user)" -ForegroundColor DarkGray
  }
  if (($env:Path -split ";") -notcontains $dir) { $env:Path += ";$dir" }
}

# ---- 1. toolchain ----
if (-not $SkipToolchain) {
  Write-Host "[1/3] arduino-cli + cores + libraries" -ForegroundColor Yellow
  $cli = Get-Command arduino-cli -ErrorAction SilentlyContinue
  if (-not $cli) {
    $tcDir = Join-Path $Root "toolchain\arduino-cli"
    New-Item -ItemType Directory -Force $tcDir | Out-Null
    $zip = Join-Path $env:TEMP "arduino-cli.zip"
    Write-Host "  downloading arduino-cli (latest, Windows x64)..."
    Invoke-WebRequest -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $tcDir -Force
    Add-UserPath $tcDir
    $cli = Get-Command arduino-cli -ErrorAction Stop
  }
  Write-Host "  arduino-cli: $($cli.Source)"
  & arduino-cli config init --overwrite --dest-dir (Join-Path $Root "toolchain") 2>$null | Out-Null
  $cfg = Join-Path $Root "toolchain\arduino-cli.yaml"
  $urls = ($BoardUrls -join ",")
  & arduino-cli --config-file $cfg config set board_manager.additional_urls $urls
  & arduino-cli --config-file $cfg core update-index
  foreach ($c in $Cores) {
    Write-Host "  core $c"
    & arduino-cli --config-file $cfg core install $c
  }
  foreach ($l in $Libs) {
    Write-Host "  lib  $l"
    & arduino-cli --config-file $cfg lib install $l
  }
  # bb8 calls plain 'arduino-cli'; make it use this config everywhere
  [Environment]::SetEnvironmentVariable("ARDUINO_CONFIG_FILE", $cfg, "User")
  $env:ARDUINO_CONFIG_FILE = $cfg
  Write-Host "  ARDUINO_CONFIG_FILE = $cfg" -ForegroundColor DarkGray
} else { Write-Host "[1/3] toolchain skipped" -ForegroundColor DarkGray }

# ---- 2. bb8 + targets.json ----
Write-Host "[2/3] bb8 Commander" -ForegroundColor Yellow
$bb8Dir = Join-Path $Root "bb8"
if (-not (Test-Path (Join-Path $bb8Dir "bb8.exe"))) {
  # source-only checkout: build it (needs .NET SDK)
  if (Get-Command dotnet -ErrorAction SilentlyContinue) {
    Write-Host "  no prebuilt bb8.exe - building from tools\Bb8Commander (dotnet publish)..."
    dotnet publish (Join-Path $Root "tools\Bb8Commander") -c Release -o $bb8Dir --nologo -v quiet
  } else { throw "bb8\bb8.exe missing and no .NET SDK to build it. Use the release zip, or install .NET SDK 10." }
}
# targets.json ships with relative sketchRoot/buildRoot ("firmware"/"build") which bb8
# resolves against the folder targets.json lives in - normalise any old absolute paths.
$tj = Join-Path $Root "targets.json"
$j = Get-Content $tj -Raw | ConvertFrom-Json
$j.sketchRoot = "firmware"
$j.buildRoot  = "build"
$j | ConvertTo-Json -Depth 6 | Set-Content $tj -Encoding UTF8
Write-Host "  targets.json -> sketchRoot=firmware, buildRoot=build (relative to $Root)"
# The wrapper also swaps in bb8.exe.new (a running exe can't overwrite itself;
# bb8's release-channel update leaves the new one beside it).
Set-Content (Join-Path $Root "bb8.cmd") ("@echo off`r`n" +
  "if exist `"%~dp0bb8\bb8.exe.new`" move /y `"%~dp0bb8\bb8.exe.new`" `"%~dp0bb8\bb8.exe`" >nul`r`n" +
  "`"%~dp0bb8\bb8.exe`" %*") -Encoding ascii
Add-UserPath $Root
[Environment]::SetEnvironmentVariable("BB8_HOME", $Root, "User"); $env:BB8_HOME = $Root

# ---- 3. git checkout for 'bb8 update' ----
Write-Host "[3/3] updates" -ForegroundColor Yellow
if ($NoGit) { Write-Host "  skipped (-NoGit)" -ForegroundColor DarkGray }
elseif (Test-Path (Join-Path $Root ".git")) { Write-Host "  already a git checkout - bb8 update is live" }
elseif (Get-Command git -ErrorAction SilentlyContinue) {
  Write-Host "  turning this folder into a checkout of $RepoUrl ..."
  git init -q
  git remote add origin $RepoUrl
  git fetch -q origin main
  git reset -q origin/main            # index/HEAD = release commit; working files untouched
  git branch -q -M main
  git branch -q --set-upstream-to=origin/main main
  Write-Host "  done - 'bb8 update' will pull new firmware from GitHub"
} else { Write-Host "  git not found: bb8 update disabled (install git, re-run installer)" -ForegroundColor DarkYellow }

Write-Host @"

=== Ready ===  (open a NEW terminal so PATH changes apply)
  bb8 list            boards on USB
  bb8 pair            pair PS3 / Nav pads (guided)
  bb8 upload drive    compile + flash + verify   (body, imu, dome likewise)
  bb8 monitor drive   console   |   bb8 monitor ball = drive via the dome bridge
  docs\HowToGuide.md  start here
Prebuilt binaries (no compile): tools\release\Flash-Prebuilt.ps1 -Target drive -Port COMx
"@ -ForegroundColor Green
