#ifndef AppVersion
  #define AppVersion "0.2.5"
#endif
#ifndef ClientVersion
  #define ClientVersion "0.2.5"
#endif
#ifndef StudioVersion
  #define StudioVersion "0.1.5"
#endif
#ifndef ProductMode
  #define ProductMode "Bundle"
#endif

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
#else
  #define SetupAppId "{{26327D1E-B98C-483C-9E66-8B40F7DD543A}"
  #define SetupAppName "Blubox Setup"
  #define SetupDir "{tmp}\BluboxSetup"
  #define SetupVersion AppVersion
  #define SetupOutput "Blubox-Setup-" + AppVersion
  #define UninstallableFlag "no"
  #define CreateAppDirFlag "no"
  #define UninstallIcon "BluboxClient.exe"
  #define OutputDirectory DistDir
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

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

#if ProductMode == "Client"
[Files]
Source: "{#StageDir}\BluboxClient.exe"; DestDir: "{app}"; Flags: ignoreversion
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

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Blubox\Client\versions"
Type: files; Name: "{localappdata}\Blubox\Client\installed_version.txt"
#endif

#if ProductMode == "Studio"
[Files]
Source: "{#StageDir}\BluboxStudio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\InterVariable.ttf"; DestDir: "{app}\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\assets\editor\fonts\JetBrainsMonoRegular.ttf"; DestDir: "{app}\assets\editor\fonts"; Flags: ignoreversion
Source: "{#StageDir}\licenses\BluboxStudio-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
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

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Blubox\Studio\versions"
Type: files; Name: "{localappdata}\BluboxStudio\session.json"
#endif

#if ProductMode == "Bundle"
[Files]
Source: "{#DistDir}\child-installers\Blubox-Client-Setup-{#ClientVersion}.exe"; DestDir: "{tmp}"; Flags: ignoreversion
Source: "{#DistDir}\child-installers\Blubox-Studio-Setup-{#StudioVersion}.exe"; DestDir: "{tmp}"; Flags: ignoreversion

[Code]
procedure InstallChild(const FileName, ProductName: string);
var
  ResultCode: Integer;
  InstallerPath: string;
begin
  InstallerPath := ExpandConstant('{tmp}') + '\' + FileName;
  if not Exec(InstallerPath,
    '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) then
    RaiseException('Could not start the ' + ProductName + ' installer.');
  if ResultCode <> 0 then
    RaiseException(ProductName + ' installation failed (exit code ' + IntToStr(ResultCode) + ').');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    InstallChild('Blubox-Client-Setup-{#ClientVersion}.exe', 'Blubox Client');
    InstallChild('Blubox-Studio-Setup-{#StudioVersion}.exe', 'Blubox Studio');
  end;
end;
#endif
