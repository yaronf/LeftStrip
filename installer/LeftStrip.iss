; Inno Setup script for LeftStrip.
; Build: install Inno Setup 6, then run package.cmd from an x64 Native Tools prompt
; (or compile this .iss after LeftStrip.exe exists in the repo root).

#define MyAppName "LeftStrip"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "LeftStrip contributors"
#define MyAppURL "https://github.com/yaronf/LeftStrip"
#define MyAppExeName "LeftStrip.exe"

[Setup]
AppId={{A7C3E9B1-4F2D-4A8E-9C15-6D8E2B4F1A03}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
; Per-user install: no elevation; HKCU startup task is correct for this mode.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE.txt
OutputDir=..\dist
OutputBaseFilename=LeftStrip-Setup-{#MyAppVersion}
SetupIconFile=..\LeftStrip.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\{#MyAppExeName}
; Close a running instance before replacing files.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Start LeftStrip at sign-in"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "..\LeftStrip.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LeftStrip.ini.default"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\VALIDATION.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Optional Run key; matches the in-app "Start at sign-in" toggle (value name LeftStrip).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LeftStrip"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Ask the running instance to exit cleanly before files are removed.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--quit"; RunOnceId: "StopLeftStrip"; Flags: waituntilterminated skipifdoesntexist
