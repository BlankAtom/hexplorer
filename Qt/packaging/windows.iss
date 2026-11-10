#ifndef AppVersion
  #error AppVersion must be supplied by package-windows.ps1
#endif
#ifndef SourceDir
  #error SourceDir must be supplied by package-windows.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by package-windows.ps1
#endif

[Setup]
AppId={{61B21CF7-348E-47DF-9D69-744A8F5DA8B7}
AppName=HExplorer
AppVersion={#AppVersion}
AppVerName=HExplorer {#AppVersion}
AppPublisher=HExplorer contributors
AppPublisherURL=https://github.com/BlankAtom/hexplorer
AppUpdatesURL=https://github.com/BlankAtom/hexplorer/releases
DefaultDirName={localappdata}\Programs\HExplorer
DefaultGroupName=HExplorer
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=HExplorer-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\HExplorer.exe
VersionInfoVersion={#AppVersion}
VersionInfoDescription=HExplorer Windows x64 setup
AppMutex=Local\HExplorer.Running.61B21CF7-348E-47DF-9D69-744A8F5DA8B7
CloseApplications=no
RestartApplications=no
SetupLogging=yes
UsePreviousAppDir=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\HExplorer"; Filename: "{app}\HExplorer.exe"
Name: "{autodesktop}\HExplorer"; Filename: "{app}\HExplorer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\HExplorer.exe"; Description: "{cm:LaunchProgram,HExplorer}"; Flags: nowait postinstall skipifsilent
