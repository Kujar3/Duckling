; Duckling installer (Inno Setup 6)
; Build everything with:  scripts\build-installer.bat   -> dist\Duckling-Setup-<version>.exe
; (compiles a static-runtime Release build in build-release, copies it to installer\payload, then runs ISCC on this file)
; Payload: installer\payload\{VST3\Duckling.vst3, CLAP\Duckling.clap}, built with the static MSVC runtime
;          (-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded), so no VC++ redistributable is needed.
; Testing: ISCC /DTestRoot=<folder> installer\Duckling.iss builds a per-user variant that installs everything below
;          <folder> instead of Program Files (no admin rights, never touches a real installation).

#define AppName "Duckling"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppPublisher "Kujar3"

#ifdef TestRoot
  #define PluginRoot TestRoot + "\CommonFiles"
  #define AppRoot TestRoot + "\App"
#else
  #define PluginRoot "{commoncf64}"
  #define AppRoot "{autopf}\" + AppName
#endif

[Setup]
#ifdef TestRoot
; Own AppId, never the previous install's folder: a test install can never take over or move the real one.
AppId={{8A41C6E2-5D07-4B3F-9C2E-71F0B5A9D3E4}
UsePreviousAppDir=no
#else
AppId={{3F9B2D61-0C4A-4E8F-B7D5-6A1E9C2F8B07}
#endif
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={#AppRoot}
DisableDirPage=yes
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
#ifdef TestRoot
PrivilegesRequired=lowest
OutputBaseFilename=Duckling-Setup-{#AppVersion}-TEST
#else
PrivilegesRequired=admin
OutputBaseFilename=Duckling-Setup-{#AppVersion}
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
SetupIconFile=duckling.ico
UninstallDisplayIcon={app}\duckling.ico
UninstallDisplayName={#AppName} {#AppVersion}
InfoAfterFile=QuickStart.txt
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
; The default filter (*.exe,*.dll,*.chm) would not notice a DAW that has the plugin binaries loaded.
CloseApplicationsFilter=*.exe,*.dll,*.chm,*.vst3,*.clap

[Messages]
WelcomeLabel2=This will install [name/ver] - a sidechain / ducking plugin for FL Studio and other DAWs: the classic "pumping" effect in one click, in time with your project, from a sidechain or from MIDI notes.%n%nPlease close FL Studio before continuing.

[Types]
Name: "full"; Description: "Recommended (VST3 + CLAP)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (use this one in FL Studio)"; Types: full custom; Flags: fixed
Name: "clap"; Description: "CLAP plugin"; Types: full

[Files]
Source: "payload\VST3\Duckling.vst3\*"; DestDir: "{#PluginRoot}\VST3\Duckling.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\CLAP\Duckling.clap"; DestDir: "{#PluginRoot}\CLAP"; Components: clap; Flags: ignoreversion
Source: "QuickStart.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "duckling.ico"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; A clean bundle every time (an older build may have had other files inside it).
Type: filesandordirs; Name: "{#PluginRoot}\VST3\Duckling.vst3"

[UninstallDelete]
Type: dirifempty; Name: "{#PluginRoot}\VST3\Duckling.vst3"

[Icons]
Name: "{group}\Duckling Quick Start"; Filename: "{app}\QuickStart.txt"
Name: "{group}\Uninstall Duckling"; Filename: "{uninstallexe}"
