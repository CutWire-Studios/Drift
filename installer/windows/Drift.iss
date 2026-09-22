#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

#ifndef MyAppSource
  #define MyAppSource "dist\\bin"
#endif

;    /DNightly  builds the nightly channel installer: its own AppId, name and install
;               directory, so it sits beside a stable install instead of upgrading it.
;
; Never change either AppId: it is what lets an installer upgrade an existing install
; in place instead of leaving two copies behind.
#ifdef Nightly
  #define MyAppName "Drift Nightly"
  #define MyAppId "1699D9B5-080B-4892-ACEE-EC56B595E89B"
  #define MyOutputBase "Drift-Setup-Nightly-x64"
#else
  #define MyAppName "Drift"
  #define MyAppId "1FC80696-7700-464A-8E35-CCBB3239EDFB"
  #define MyOutputBase "Drift-Setup-x64"
#endif
#define MyAppPublisher "CutWire Studios"
#define MyAppExeName "drift.exe"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL=https://github.com/CutWire-Studios/Drift/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma
SolidCompression=yes
WizardStyle=modern
; Path is relative to this script. Without these two, setup runs under the stock
; Inno icon and the Apps & Features entry falls back to a generic one.
SetupIconFile=..\..\resources\windows\drift.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
#ifndef Nightly
ChangesAssociations=yes
#endif
OutputDir=output
OutputBaseFilename={#MyOutputBase}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Recursive: alongside the exe and its Qt runtime this carries the bundled
; effects\, transitions\, effect-templates\ and audio-effects\ package
; directories, which the app resolves relative to the executable.
Source: "{#MyAppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

#ifndef Nightly
; Deliberately stable-only. A nightly registering the same ProgID would have its uninstaller
; delete the association out from under the stable install (uninsdeletekey), and two channels
; fighting over which opens a .drift file helps nobody.
[Registry]
Root: HKCR; Subkey: ".drift"; ValueType: string; ValueName: ""; ValueData: "CutWire.Drift.Project"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "CutWire.Drift.Project"; ValueType: string; ValueName: ""; ValueData: "Drift Project"; Flags: uninsdeletekey
Root: HKCR; Subkey: "CutWire.Drift.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCR; Subkey: "CutWire.Drift.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
#endif

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
