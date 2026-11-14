; MDU installer (Inno Setup).
;
; Per-user install: no administrator rights, installs to
; %LOCALAPPDATA%\Programs\MDU, settings stay in %LOCALAPPDATA%\MDU.
;
; Build (after `cmake --install build/windows --config Release --prefix staging`):
;   iscc /DSourceDir=<staging> /DAppVersion=<x.y.z> installer\mdu.iss

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or newer is required
#endif

#ifndef AppVersion
  #error Define AppVersion, e.g. /DAppVersion=0.1.0
#endif
#ifndef SourceDir
  #error Define SourceDir, the folder produced by cmake --install
#endif

[Setup]
AppId={{7A1C6E52-3B0D-4C55-9E2A-6D9F0B8C4E11}
AppName=MDU
AppVersion={#AppVersion}
AppVerName=MDU {#AppVersion}
AppPublisher=MDU contributors
AppPublisherURL=https://github.com/im4theone/mdu
AppSupportURL=https://github.com/im4theone/mdu/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\MDU
DefaultGroupName=MDU
DisableProgramGroupPage=yes
DisableDirPage=auto
; Per-user only: never elevates.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 11
MinVersion=10.0.22000
SetupIconFile=..\resources\icons\mdu.ico
UninstallDisplayIcon={app}\mdu.exe
UninstallDisplayName=MDU
OutputBaseFilename=MDU-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; MDU is stopped with `mdu.exe --exit` instead (see [Code]).
CloseApplications=no
RestartApplications=no

[Tasks]
Name: startup; Description: "Start MDU when I sign in"
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\MDU"; Filename: "{app}\mdu.exe"
Name: "{autodesktop}\MDU"; Filename: "{app}\mdu.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\mdu.exe"; Parameters: "--set-startup on"; Tasks: startup; Flags: runhidden waituntilterminated
Filename: "{app}\mdu.exe"; Parameters: "--set-startup off"; Tasks: not startup; Flags: runhidden waituntilterminated
Filename: "{app}\mdu.exe"; Description: "Start MDU now"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; Stops MDU, restores the wallpaper it replaced, removes the startup entry,
; deletes caches, logs and WebView2 data. Runs before files are removed.
Filename: "{app}\mdu.exe"; Parameters: "--uninstall-cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "MDUCleanup"

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // Upgrades: stop the running copy so its files can be replaced.
  if FileExists(ExpandConstant('{app}\mdu.exe')) then
    Exec(ExpandConstant('{app}\mdu.exe'), '--exit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  DataDir := ExpandConstant('{localappdata}\MDU');
  // Settings are kept unless the user asks otherwise; silent uninstalls keep them.
  if not UninstallSilent and DirExists(DataDir) then
    if MsgBox('Also delete your MDU settings?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(DataDir, True, True, True);
  // Remove the folder if nothing is left in it.
  RemoveDir(DataDir);
end;
