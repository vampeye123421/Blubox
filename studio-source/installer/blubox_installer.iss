#ifndef AppVersion
  #define AppVersion "0.2.5"
#endif
#ifndef ClientVersion
  #define ClientVersion "0.2.5"
#endif
#ifndef StudioVersion
  #define StudioVersion "0.1.11"
#endif
#ifndef ProductMode
  #define ProductMode "Bundle"
#endif
#define EulaVersion "2026-11-01"

#define DistDir AddBackslash(SourcePath) + "..\dist"
#define StageDir AddBackslash(DistDir) + "release_staging"

#if ProductMode == "Client"
  #define SetupAppId "{{5F1E3447-4F5B-43CE-8E96-FA67BE6F17A1}"
  #define SetupAppName "Blubox Client"
  #define SetupDir "{localappdata}\Programs\Blubox\Client"
  #define SetupVersion ClientVersion
  #define SetupOutput "Blubox-Client-Setup-" + ClientVersion
  #define UninstallableFlag "yes"
  #define CreateAppDirFlag "yes"
  #define UninstallIcon "BluboxClient.exe"
  #define OutputDirectory AddBackslash(DistDir) + "child-installers"
  #define InstallerLicense AddBackslash(StageDir) + "BluboxClient-LICENSE.txt"
#elif ProductMode == "Studio"
  #define SetupAppId "{{855A8507-8E44-4A19-9DB7-B33A43C01D44}"
  #define SetupAppName "Blubox Studio"
  #define SetupDir "{localappdata}\Programs\Blubox\Studio"
  #define SetupVersion StudioVersion
  #define SetupOutput "Blubox-Studio-Setup-" + StudioVersion
  #define UninstallableFlag "yes"
  #define CreateAppDirFlag "yes"
  #define UninstallIcon "BluboxStudio.exe"
  #define OutputDirectory AddBackslash(DistDir) + "child-installers"
  #define InstallerLicense AddBackslash(StageDir) + "BluboxStudio-LICENSE.txt"
#else
  #define SetupAppId "{{26327D1E-B98C-483C-9E66-8B40F7DD543A}"
  #define SetupAppName "Blubox Setup"
  #define SetupDir "{localappdata}\Programs\Blubox"
  #define SetupVersion AppVersion
  #define SetupOutput "Blubox-Setup-" + AppVersion
  #define UninstallableFlag "yes"
  #define CreateAppDirFlag "yes"
  #define UninstallIcon "Client\BluboxClient.exe"
  #define OutputDirectory DistDir
  #define InstallerLicense AddBackslash(StageDir) + "Blubox-Bundle-LICENSE.txt"
#endif

[Setup]
AppId={#SetupAppId}
AppName={#SetupAppName}
AppVersion={#SetupVersion}
AppPublisher=Blubox
DefaultDirName={#SetupDir}
DefaultGroupName={#SetupAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable={#UninstallableFlag}
CreateAppDir={#CreateAppDirFlag}
UninstallDisplayIcon={app}\{#UninstallIcon}
OutputDir={#OutputDirectory}
OutputBaseFilename={#SetupOutput}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
LicenseFile={#InstallerLicense}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

#if ProductMode == "Client"
[Files]
Source: "{#StageDir}\BluboxClient.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\BluboxClient-LICENSE.txt"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autodesktop}\Blubox"; Filename: "{app}\BluboxClient.exe"
Name: "{group}\Blubox"; Filename: "{app}\BluboxClient.exe"
Name: "{group}\Uninstall Blubox Client"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Classes\blubox"; ValueType: string; ValueName: ""; ValueData: "URL:Blubox Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\blubox"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\blubox\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\BluboxClient.exe"",0"
Root: HKCU; Subkey: "Software\Classes\blubox\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BluboxClient.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Blubox\Legal\Client"; ValueType: string; ValueName: "AcceptedEulaVersion"; ValueData: "{#EulaVersion}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Blubox\Legal\Client"; ValueType: string; ValueName: "AcceptedAtLocal"; ValueData: "{code:GetAcceptanceTimestamp}"; Flags: uninsdeletevalue

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Blubox\Client\versions"
Type: files; Name: "{localappdata}\Blubox\Client\installed_version.txt"
#endif

#if ProductMode == "Studio"
[Files]
Source: "{#StageDir}\BluboxStudio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\BluboxStudio-LICENSE.txt"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\luau_api_catalog.json"; DestDir: "{app}\assets\editor"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\InterVariable.ttf"; DestDir: "{app}\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\JetBrainsMonoRegular.ttf"; DestDir: "{app}\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\licenses\Inter-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\JetBrainsMono-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\ImGuiColorTextEdit-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\THIRD_PARTY_LICENSES.md"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\Blubox Studio"; Filename: "{app}\BluboxStudio.exe"
Name: "{group}\Blubox Studio"; Filename: "{app}\BluboxStudio.exe"
Name: "{group}\Uninstall Blubox Studio"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Classes\blubox-studio"; ValueType: string; ValueName: ""; ValueData: "URL:Blubox Studio Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\blubox-studio"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\blubox-studio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BluboxStudio.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Blubox\Legal\Studio"; ValueType: string; ValueName: "AcceptedEulaVersion"; ValueData: "{#EulaVersion}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Blubox\Legal\Studio"; ValueType: string; ValueName: "AcceptedAtLocal"; ValueData: "{code:GetAcceptanceTimestamp}"; Flags: uninsdeletevalue

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Blubox\Studio\versions"
Type: files; Name: "{localappdata}\BluboxStudio\session.json"
#endif

#if ProductMode == "Bundle"
[Files]
Source: "{#StageDir}\BluboxClient.exe"; DestDir: "{app}\Client"; Flags: ignoreversion
Source: "{#StageDir}\BluboxClient-LICENSE.txt"; DestDir: "{app}\Client"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\Client\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\BluboxStudio.exe"; DestDir: "{app}\Studio"; Flags: ignoreversion
Source: "{#StageDir}\BluboxStudio-LICENSE.txt"; DestDir: "{app}\Studio"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\luau_api_catalog.json"; DestDir: "{app}\Studio\assets\editor"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\InterVariable.ttf"; DestDir: "{app}\Studio\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\JetBrainsMonoRegular.ttf"; DestDir: "{app}\Studio\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\licenses\Inter-LICENSE.txt"; DestDir: "{app}\Studio\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\JetBrainsMono-LICENSE.txt"; DestDir: "{app}\Studio\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\ImGuiColorTextEdit-LICENSE.txt"; DestDir: "{app}\Studio\licenses"; Flags: ignoreversion
Source: "{#StageDir}\licenses\THIRD_PARTY_LICENSES.md"; DestDir: "{app}\Studio\licenses"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\Blubox"; Filename: "{app}\Client\BluboxClient.exe"
Name: "{group}\Blubox Client"; Filename: "{app}\Client\BluboxClient.exe"
Name: "{autodesktop}\Blubox Studio"; Filename: "{app}\Studio\BluboxStudio.exe"
Name: "{group}\Blubox Studio"; Filename: "{app}\Studio\BluboxStudio.exe"
Name: "{group}\Uninstall Blubox"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Classes\blubox"; ValueType: string; ValueName: ""; ValueData: "URL:Blubox Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\blubox"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\blubox\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\Client\BluboxClient.exe"",0"
Root: HKCU; Subkey: "Software\Classes\blubox\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Client\BluboxClient.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Blubox\Legal\Client"; ValueType: string; ValueName: "AcceptedEulaVersion"; ValueData: "{#EulaVersion}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Blubox\Legal\Client"; ValueType: string; ValueName: "AcceptedAtLocal"; ValueData: "{code:GetAcceptanceTimestamp}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\blubox-studio"; ValueType: string; ValueName: ""; ValueData: "URL:Blubox Studio Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\blubox-studio"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\blubox-studio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Studio\BluboxStudio.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Blubox\Legal\Studio"; ValueType: string; ValueName: "AcceptedEulaVersion"; ValueData: "{#EulaVersion}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Blubox\Legal\Studio"; ValueType: string; ValueName: "AcceptedAtLocal"; ValueData: "{code:GetAcceptanceTimestamp}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{{5F1E3447-4F5B-43CE-8E96-FA67BE6F17A1}_is1"; ValueType: none; Flags: deletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{{855A8507-8E44-4A19-9DB7-B33A43C01D44}_is1"; ValueType: none; Flags: deletekey

[InstallDelete]
Type: files; Name: "{app}\Client\unins000.exe"
Type: files; Name: "{app}\Client\unins000.dat"
Type: files; Name: "{app}\Studio\unins000.exe"
Type: files; Name: "{app}\Studio\unins000.dat"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Client"
Type: filesandordirs; Name: "{app}\Studio"
Type: filesandordirs; Name: "{localappdata}\Blubox\versions"
Type: files; Name: "{localappdata}\Blubox\installed_version.txt"
Type: filesandordirs; Name: "{localappdata}\Blubox\studio\versions"
Type: files; Name: "{localappdata}\Blubox\installed_studio_version.txt"
Type: files; Name: "{localappdata}\BluboxStudio\session.json"

[Code]
function GetAcceptanceTimestamp(Param: string): string;
begin
  Result := GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':');
end;
#else
[Code]
function GetAcceptanceTimestamp(Param: string): string;
begin
  Result := GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':');
end;
#endif
