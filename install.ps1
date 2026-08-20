# Builds the bb8 Commander CLI into .\bin (bb8.cmd points there).
# Requires: .NET SDK 10+, arduino-cli on PATH.
dotnet publish "$PSScriptRoot\tools\Bb8Commander" -c Release -o "$PSScriptRoot\bin"
Write-Host "`nDone. Run '.\bb8.cmd list' (or add $PSScriptRoot\bin to PATH)." -ForegroundColor Green
