; Encore: Basement Studio installer for Windows (Inno Setup 6)
;
;   ISCC.exe /DAppVersion=0.4.0 /DBuildDir=..\build\EncoreStudio_artefacts\Release installer\EncoreStudio.iss
;
; Makes dist\Encore-Studio-<version>-windows-setup.exe: Start menu and (optional)
; desktop shortcuts, opens .encore songs (and .basement ones from before the
; rename) on double-click, and a normal uninstaller. Songs and settings are
; never installed or removed by it: they live in the user's own folders.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\EncoreStudio_artefacts\Release"
#endif
#ifndef BridgeDir
  #define BridgeDir "..\build32\EncoreStudioBridge32_artefacts\Release"
#endif

[Setup]
AppId={{85CBF2F3-407D-4790-8345-BA701885E638}
AppName=Encore: Basement Studio
AppVersion={#AppVersion}
AppVerName=Encore: Basement Studio {#AppVersion}
AppPublisher=Encore Music
AppPublisherURL=https://encoremusic.studio
AppSupportURL=https://github.com/jimmyrlondo/encore-studio/issues
AppUpdatesURL=https://github.com/jimmyrlondo/encore-studio/releases
DefaultDirName={autopf}\Encore Studio
DefaultGroupName=Encore Studio
DisableProgramGroupPage=yes
; installs for everyone (asks for admin) or just this user, the person's choice
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
CloseApplications=yes
WizardStyle=modern
SetupIconFile=encore.ico
UninstallDisplayIcon={app}\Encore Studio.exe
UninstallDisplayName=Encore: Basement Studio
OutputDir=..\dist
OutputBaseFilename=Encore-Studio-{#AppVersion}-windows-setup
Compression=lzma2/max
SolidCompression=yes
InfoBeforeFile=welcome.txt

[Tasks]
Name: "desktopicon"; Description: "Put Encore: Basement Studio on the desktop"; GroupDescription: "Shortcuts:"
Name: "associate"; Description: "Open .encore songs with Encore: Basement Studio"; GroupDescription: "Songs:"

[Files]
Source: "{#BuildDir}\Encore Studio.exe"; DestDir: "{app}"; Flags: ignoreversion
#if FileExists(AddBackslash(BridgeDir) + "Encore Studio Bridge32.exe")
Source: "{#BridgeDir}\Encore Studio Bridge32.exe"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "..\README.md"; DestDir: "{app}"

[InstallDelete]
; shortcuts from before the rename (Encore Studio, Encore Basement)
Type: files; Name: "{autoprograms}\Encore Studio.lnk"
Type: files; Name: "{autodesktop}\Encore Studio.lnk"
Type: files; Name: "{autoprograms}\Encore Basement.lnk"
Type: files; Name: "{autodesktop}\Encore Basement.lnk"

[Icons]
Name: "{autoprograms}\Encore Basement Studio"; Filename: "{app}\Encore Studio.exe"
Name: "{autodesktop}\Encore Basement Studio"; Filename: "{app}\Encore Studio.exe"; Tasks: desktopicon

[Registry]
; .encore and .basement songs open in Encore: Basement Studio (per user or machine, whichever this install is)
Root: HKA; Subkey: "Software\Classes\.encore"; ValueType: string; ValueName: ""; ValueData: "EncoreStudio.Song"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.basement"; ValueType: string; ValueName: ""; ValueData: "EncoreStudio.Song"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\EncoreStudio.Song"; ValueType: string; ValueName: ""; ValueData: "Encore: Basement Studio song"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\EncoreStudio.Song\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Encore Studio.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\EncoreStudio.Song\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Encore Studio.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\Encore Studio.exe"; Description: "Open Encore: Basement Studio now"; Flags: nowait postinstall skipifsilent
