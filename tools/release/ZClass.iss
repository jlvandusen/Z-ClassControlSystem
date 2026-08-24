; Z-Class Control System - Inno Setup script
; Compiled by tools/release/make-release.ps1:
;   ISCC.exe /DVersion=1.01 /DStage="dist\stage\ZClass-ControlSystem-v1.01" /DOut="dist" ZClass.iss
; Installs the self-contained bundle to a user-writable folder (no admin), adds
; bb8 to PATH, Start-menu shortcuts, an uninstaller, and optionally runs the
; toolchain setup (Install-ZClass.ps1) on the first launch.

#ifndef Version
  #define Version "0.0"
#endif
#ifndef Stage
  #error Pass /DStage=<staging folder>
#endif
#ifndef Out
  #define Out "."
#endif

[Setup]
AppId={{7B1E2B40-ZCLS-4C1E-9D3A-BB8DRIVE0101}
AppName=Z-Class Control System
AppVersion={#Version}
AppVerName=Z-Class Control System v{#Version}
AppPublisher=James VanDusen
AppPublisherURL=https://github.com/jlvandusen/Z-ClassControlSystem
AppSupportURL=https://github.com/jlvandusen/Z-ClassControlSystem/issues
AppUpdatesURL=https://github.com/jlvandusen/Z-ClassControlSystem/releases
DefaultDirName={localappdata}\ZClass
DefaultGroupName=Z-Class Control System
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#Out}
OutputBaseFilename=ZClass-ControlSystem-Setup-v{#Version}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=Z-Class Control System v{#Version}
ChangesEnvironment=yes
LicenseFile=
InfoBeforeFile={#Stage}\RELEASE_NOTES.md

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "toolchain"; Description: "Install arduino-cli + board cores + libraries now (internet, ~10 min) - needed to compile/flash"; GroupDescription: "First-run setup:"
Name: "gitlink";   Description: "Link the install folder to GitHub so 'bb8 update' pulls new firmware (needs git)"; GroupDescription: "First-run setup:"
Name: "desktopicon"; Description: "Desktop shortcut to the Z-Class console"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Z-Class Console";        Filename: "{cmd}"; Parameters: "/k cd /d ""{app}"" && echo Z-Class Control System v{#Version} - try: bb8 list"; WorkingDir: "{app}"
Name: "{group}\How-To Guide";           Filename: "{app}\docs\docx\HowToGuide.docx"
Name: "{group}\Assembly Guide";         Filename: "{app}\docs\docx\Assembly_Drive.docx"
Name: "{group}\Runbook";                Filename: "{app}\docs\docx\Runbook.docx"
Name: "{group}\Re-run toolchain setup"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoExit -File ""{app}\Install-ZClass.ps1"""; WorkingDir: "{app}"
Name: "{group}\GitHub releases";        Filename: "https://github.com/jlvandusen/Z-ClassControlSystem/releases"
Name: "{group}\Uninstall";              Filename: "{uninstallexe}"
Name: "{autodesktop}\Z-Class Console";  Filename: "{cmd}"; Parameters: "/k cd /d ""{app}"""; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; bb8 on the user PATH (+ BB8_HOME) - removed on uninstall
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "BB8_HOME"; ValueData: "{app}"; Flags: uninsdeletevalue

[Run]
; The PowerShell engine does the heavy lifting; tasks decide how much of it runs.
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoExit -File ""{app}\Install-ZClass.ps1"""; WorkingDir: "{app}"; Description: "Run toolchain setup (arduino-cli, cores, libraries, GitHub link)"; Flags: postinstall nowait skipifsilent; Tasks: toolchain and gitlink
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoExit -File ""{app}\Install-ZClass.ps1"" -NoGit"; WorkingDir: "{app}"; Description: "Run toolchain setup (arduino-cli, cores, libraries)"; Flags: postinstall nowait skipifsilent; Tasks: toolchain and not gitlink
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoExit -File ""{app}\Install-ZClass.ps1"" -SkipToolchain"; WorkingDir: "{app}"; Description: "Link to GitHub for bb8 update"; Flags: postinstall nowait skipifsilent; Tasks: gitlink and not toolchain
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Install-ZClass.ps1"" -SkipToolchain -NoGit"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; Tasks: not toolchain and not gitlink

[UninstallDelete]
Type: filesandordirs; Name: "{app}\build"
Type: filesandordirs; Name: "{app}\toolchain"
Type: filesandordirs; Name: "{app}\.git"
Type: files; Name: "{app}\bb8.cmd"

[Code]
function NeedsAddPath(Param: string): boolean;
var OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
  begin Result := True; exit; end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var P, App: string; I: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    App := ExpandConstant('{app}');
    if RegQueryStringValue(HKCU, 'Environment', 'Path', P) then
    begin
      I := Pos(';' + Uppercase(App), Uppercase(P));
      if I > 0 then
      begin
        Delete(P, I, Length(App) + 1);
        RegWriteExpandStringValue(HKCU, 'Environment', 'Path', P);
      end;
    end;
  end;
end;
