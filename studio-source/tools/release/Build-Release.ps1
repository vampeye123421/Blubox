[CmdletBinding()]
param(
    [string] $ClientRoot = $(if ($env:BLUBOX_CLIENT_ROOT) { $env:BLUBOX_CLIENT_ROOT } else { 'D:\blubox-client' }),
    [string] $StudioRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $ClientBuildDir = '',
    [string] $StudioBuildDir = '',
    [string] $AppVersion = '',
    [string] $IsccPath = '',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $ClientBuildDir) { $ClientBuildDir = Join-Path $ClientRoot 'build' }
if (-not $StudioBuildDir) { $StudioBuildDir = Join-Path $StudioRoot 'build' }
$outputRoot = Join-Path $StudioRoot 'dist'
$staging = Join-Path $outputRoot 'release_staging'
$childInstallerDir = Join-Path $outputRoot 'child-installers'
$installerScript = Join-Path $StudioRoot 'installer\blubox_installer.iss'

function Invoke-Native([string] $File, [string[]] $Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File exited with code $LASTEXITCODE" }
}

function Find-BuiltExecutable([string] $BuildDir, [string] $Name) {
    foreach ($candidate in @(
        (Join-Path $BuildDir $Name),
        (Join-Path (Join-Path $BuildDir 'Release') $Name),
        (Join-Path (Join-Path $BuildDir 'RelWithDebInfo') $Name)
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "Build output not found: $Name under $BuildDir"
}

foreach ($requiredPath in @($ClientRoot, $StudioRoot, $installerScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required release path is missing: $requiredPath" }
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath (Join-Path $ClientBuildDir 'CMakeCache.txt'))) {
        Invoke-Native $cmake @('-S', $ClientRoot, '-B', $ClientBuildDir, '-DCMAKE_BUILD_TYPE=Release')
    }
    Invoke-Native $cmake @('--build', $ClientBuildDir, '--config', 'Release', '--target', 'blubox_client')
    Invoke-Native $cmake @('--build', $ClientBuildDir, '--config', 'Release', '--target', 'blubox_launcher')
    Invoke-Native $cmake @('--build', $ClientBuildDir, '--config', 'Release', '--target', 'blubox_studio_launcher')

    if (-not (Test-Path -LiteralPath (Join-Path $StudioBuildDir 'CMakeCache.txt'))) {
        Invoke-Native $cmake @('-S', $StudioRoot, '-B', $StudioBuildDir, '-DCMAKE_BUILD_TYPE=Release')
    }
    Invoke-Native $cmake @('--build', $StudioBuildDir, '--config', 'Release', '--target', 'blubox_studio')
}

$clientVersionFile = Join-Path $ClientRoot 'version.json'
if (Test-Path -LiteralPath $clientVersionFile) {
    $clientVersion = (Get-Content -LiteralPath $clientVersionFile -Raw | ConvertFrom-Json).version
} else {
    throw "Client version manifest not found: $clientVersionFile"
}
$studioProject = Get-Content -LiteralPath (Join-Path $StudioRoot 'CMakeLists.txt') -Raw
if ($studioProject -notmatch 'project\(BluboxStudio VERSION ([0-9]+(?:\.[0-9]+){1,3})') {
    throw 'Could not determine the Studio version from CMakeLists.txt.'
}
$studioVersion = $Matches[1]
if (-not $AppVersion) { $AppVersion = $clientVersion }
foreach ($version in @($clientVersion, $studioVersion, $AppVersion)) {
    if ($version -notmatch '^\d+(\.\d+){1,3}$') { throw "Invalid release version: $version" }
}

$clientExe = Find-BuiltExecutable $ClientBuildDir 'blubox_client.exe'
$clientLauncherExe = Find-BuiltExecutable $ClientBuildDir 'blubox_launcher.exe'
$studioLauncherExe = Find-BuiltExecutable $ClientBuildDir 'blubox_studio_launcher.exe'
$studioExe = Find-BuiltExecutable $StudioBuildDir 'blubox_studio.exe'
$fontDir = Join-Path $StudioRoot 'assets\editor\fonts'
$clientDependencyLicenses = @(
    @{ Source = (Join-Path $ClientRoot 'assets\fonts\OFL.txt'); Name = 'Lato-OFL.txt' },
    @{ Source = (Join-Path $ClientBuildDir '_deps\raylib-src\LICENSE'); Name = 'raylib-LICENSE.txt' },
    @{ Source = (Join-Path $ClientBuildDir '_deps\joltphysics-src\LICENSE'); Name = 'Jolt-LICENSE.txt' },
    @{ Source = (Join-Path $ClientBuildDir '_deps\enet-src\LICENSE'); Name = 'ENet-LICENSE.txt' },
    @{ Source = (Join-Path $ClientBuildDir '_deps\nlohmann_json-src\LICENSE.MIT'); Name = 'nlohmann-json-LICENSE.txt' }
)
$requiredFiles = @(
    $clientExe,
    $clientLauncherExe,
    $studioLauncherExe,
    $studioExe,
    (Join-Path $StudioRoot 'assets\editor\luau_api_catalog.json'),
    (Join-Path $fontDir 'InterVariable.ttf'),
    (Join-Path $fontDir 'JetBrainsMono-Regular.ttf'),
    (Join-Path $fontDir 'Inter-LICENSE.txt'),
    (Join-Path $fontDir 'JetBrainsMono-LICENSE.txt'),
    (Join-Path $StudioRoot 'studio\third_party\ImGuiColorTextEdit\LICENSE'),
    (Join-Path $StudioRoot 'THIRD_PARTY_LICENSES.md'),
    (Join-Path $StudioRoot 'LICENSE')
) + @($clientDependencyLicenses | ForEach-Object { $_.Source })
$missing = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count) { throw "Release inputs missing:`n$($missing -join "`n")" }

# Only remove the generated staging folder, after verifying it remains inside dist.
$fullOutput = [System.IO.Path]::GetFullPath($outputRoot).TrimEnd('\') + '\'
$fullStaging = [System.IO.Path]::GetFullPath($staging)
if (-not $fullStaging.StartsWith($fullOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a staging folder outside dist: $fullStaging"
}
$fullChildInstallers = [System.IO.Path]::GetFullPath($childInstallerDir)
if (-not $fullChildInstallers.StartsWith($fullOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean an installer folder outside dist: $fullChildInstallers"
}
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
if (Test-Path -LiteralPath $childInstallerDir) { Remove-Item -LiteralPath $childInstallerDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $staging 'assets\editor\fonts') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging 'licenses\client') -Force | Out-Null
New-Item -ItemType Directory -Path $childInstallerDir -Force | Out-Null

Copy-Item -LiteralPath $clientExe -Destination (Join-Path $staging 'BluboxClient.exe')
Copy-Item -LiteralPath $clientLauncherExe -Destination (Join-Path $staging 'BluboxLauncher.exe')
Copy-Item -LiteralPath $studioLauncherExe -Destination (Join-Path $staging 'BluboxStudioLauncher.exe')
Copy-Item -LiteralPath $studioExe -Destination (Join-Path $staging 'BluboxStudio.exe')
Copy-Item -LiteralPath (Join-Path $StudioRoot 'assets\editor\luau_api_catalog.json') -Destination (Join-Path $staging 'assets\editor\luau_api_catalog.json')
Copy-Item -LiteralPath (Join-Path $fontDir 'InterVariable.ttf') -Destination (Join-Path $staging 'assets\editor\fonts\InterVariable.ttf')
Copy-Item -LiteralPath (Join-Path $fontDir 'JetBrainsMono-Regular.ttf') -Destination (Join-Path $staging 'assets\editor\fonts\JetBrainsMonoRegular.ttf')
Copy-Item -LiteralPath (Join-Path $fontDir 'Inter-LICENSE.txt') -Destination (Join-Path $staging 'licenses\Inter-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $fontDir 'JetBrainsMono-LICENSE.txt') -Destination (Join-Path $staging 'licenses\JetBrainsMono-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $StudioRoot 'THIRD_PARTY_LICENSES.md') -Destination (Join-Path $staging 'licenses\THIRD_PARTY_LICENSES.md')
Copy-Item -LiteralPath (Join-Path $StudioRoot 'LICENSE') -Destination (Join-Path $staging 'licenses\BluboxStudio-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $StudioRoot 'studio\third_party\ImGuiColorTextEdit\LICENSE') -Destination (Join-Path $staging 'licenses\ImGuiColorTextEdit-LICENSE.txt')
foreach ($license in $clientDependencyLicenses) {
    Copy-Item -LiteralPath $license.Source -Destination (Join-Path $staging "licenses\client\$($license.Name)")
}

if (-not $IsccPath) {
    $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($iscc) { $IsccPath = $iscc.Source }
}
if (-not $IsccPath) {
    $candidates = @(
        'D:\Inno Setup 7\ISCC.exe',
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
    throw 'Inno Setup Compiler not found. Install Inno Setup 7 or pass -IsccPath "C:\Program Files (x86)\Inno Setup 7\ISCC.exe".'
}

$modes = @('Client', 'Studio', 'Bundle')
foreach ($mode in $modes) {
    Invoke-Native $IsccPath @(
        '/Qp',
        "/DAppVersion=$AppVersion",
        "/DClientVersion=$clientVersion",
        "/DStudioVersion=$studioVersion",
        "/DProductMode=$mode",
        $installerScript)
}

$installer = Join-Path $outputRoot "Blubox-Setup-$AppVersion.exe"
$clientInstaller = Join-Path $childInstallerDir "Blubox-Client-Setup-$clientVersion.exe"
$studioInstaller = Join-Path $childInstallerDir "Blubox-Studio-Setup-$studioVersion.exe"
foreach ($path in @($installer, $clientInstaller, $studioInstaller)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Inno Setup did not create expected output: $path" }
}

Write-Host "Release staging: $staging"
Write-Host "Client installer: $clientInstaller"
Write-Host "Studio installer: $studioInstaller"
Write-Host "Combined installer: $installer"
