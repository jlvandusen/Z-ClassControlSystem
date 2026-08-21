@echo off
rem bb8 launcher. Exit code 75 from bb8.exe means "my own source just came down
rem from GitHub" - rebuild bin\ and re-run the same command once (--no-update
rem stops it from checking again).
setlocal
"%~dp0bin\bb8.exe" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="75" exit /b %RC%

echo [UPDATE] rebuilding bb8 from the updated source...
dotnet publish "%~dp0tools\Bb8Commander" -c Release -o "%~dp0bin" --nologo -v quiet
if errorlevel 1 (
    echo [ERROR] rebuild failed - fix the build, or run install.ps1, then retry.
    exit /b 1
)
"%~dp0bin\bb8.exe" %* --no-update
exit /b %ERRORLEVEL%
