; Inno Setup script for the CrowdMike Windows installer (built by .github/workflows/build.yml).
; ISCC /DAppVersion=0.2.0 /DStageDir=<folder containing CrowdMike.exe, CrowdMike.vst3, CrowdMike.clap> installer\CrowdMike.iss
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{6B0E8E52-3F1B-4D55-A8C6-0C3C4D2B1C77}
AppName=CrowdMike
AppVersion={#AppVersion}
AppPublisher=Circuit Drift Labs
DefaultDirName={autopf}\CrowdMike
DefaultGroupName=CrowdMike
OutputBaseFilename=CrowdMike-{#AppVersion}-Windows-x64-Setup
OutputDir=.
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=CrowdMike

[Types]
Name: "full"; Description: "Standalone application + VST3 + CLAP"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom

[Files]
Source: "{#StageDir}\CrowdMike.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#StageDir}\CrowdMike.vst3\*"; DestDir: "{commoncf64}\VST3\CrowdMike.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\CrowdMike.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\CrowdMike"; Filename: "{app}\CrowdMike.exe"; Components: standalone
Name: "{autodesktop}\CrowdMike"; Filename: "{app}\CrowdMike.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\CrowdMike.exe"; Description: "Launch CrowdMike"; Flags: nowait postinstall skipifsilent; Components: standalone
