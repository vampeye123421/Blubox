# Blubox Windows release build

The Client and Studio each ship as one executable. Their bootstrap/update flow runs
inside `BluboxClient.exe` and `BluboxStudio.exe`; there are no separate launcher
executables in the release.

The release script builds both CMake targets, stages only the executables, Studio
fonts, proprietary app licenses, and third-party license notices, then compiles three Inno Setup packages from the single
existing script `installer/blubox_installer.iss`:

- `dist/child-installers/Blubox-Client-Setup-<client-version>.exe` installs Client only.
- `dist/child-installers/Blubox-Studio-Setup-<studio-version>.exe` installs Studio only.
- `dist/Blubox-Setup-<client-version>.exe` directly installs both apps and creates
  one Windows uninstall entry for the combined installation.

The child installers use separate Inno Setup AppIds and per-user install
directories under `%LOCALAPPDATA%\Programs\Blubox`. The combined installer writes
the same app files to those directories directly, creates separate Start Menu and
Desktop links and protocol registrations, and removes both apps' updater caches
when uninstalled. It also removes the separate uninstall entries and uninstaller
files created by older combined setups. Each app keeps its proprietary
`LICENSE.txt` in its application root and third-party license notices in the
separate `licenses` directory.

Each child installer requires acceptance of its product's proprietary license
before installation. The combined setup presents both agreements before copying
either app. Accepted license versions and local timestamps are recorded under
`HKCU\Software\Blubox\Legal\Client` and
`HKCU\Software\Blubox\Legal\Studio`; the combined uninstaller removes both
acceptance records.

## Build on Windows

Requirements: CMake 3.24+, Git, a C++20 toolchain configured for the existing build
folders, and Inno Setup 7. Install Inno Setup 7 from the official JRSoftware site or
install its compiler (`ISCC.exe`) on `PATH`. The script also recognizes the default
Inno Setup 6 install locations for compatibility.

From PowerShell:

```powershell
& 'D:\blubox-studio\tools\release\Build-Release.ps1'
```

If the client source is elsewhere, set `BLUBOX_CLIENT_ROOT` or pass `-ClientRoot`.
The Client setup version follows the release's `-AppVersion` value; Studio uses
its version from `CMakeLists.txt`. If `-AppVersion` is omitted, the Client source
version is used. To override the combined setup's displayed version, pass
`-AppVersion 0.1.7`. To use a specific
Inno compiler, pass `-IsccPath 'C:\Program Files (x86)\Inno Setup 7\ISCC.exe'`.
`-SkipBuild` is only for when both integrated app executables are already built.

The script gathers the combined setup here:

```text
D:\blubox-studio\dist\Blubox-Setup-<version>.exe
```

The intermediate app staging is at `dist/release_staging/`; the standalone child
setups are at `dist/child-installers/`. All are generated release outputs and
should not be copied into the source-controlled source tree.
