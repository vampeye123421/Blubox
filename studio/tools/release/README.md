# Blubox Windows release build

The Client and Studio each ship as one executable. Their bootstrap/update flow runs
inside `BluboxClient.exe` and `BluboxStudio.exe`; there are no separate launcher
executables in the release.

The release script builds both CMake targets, stages only the executables, Studio
fonts, and license notices, then compiles three Inno Setup packages from the single
existing script `installer/blubox_installer.iss`:

- `dist/child-installers/Blubox-Client-Setup-<client-version>.exe` installs Client only.
- `dist/child-installers/Blubox-Studio-Setup-<studio-version>.exe` installs Studio only.
- `dist/Blubox-Setup-<client-version>.exe` installs both child packages and does not
  create its own Windows uninstall entry.

The two child installers use separate Inno Setup AppIds and per-user install
directories under `%LOCALAPPDATA%\Programs\Blubox`. Their Start Menu/Desktop links,
protocol registrations, and updater caches are removed by that product's own
uninstaller. Removing Client leaves Studio installed and vice versa.

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
The Client and Studio child installers use each app's current source version. To
override the combined setup's displayed version, pass `-AppVersion 0.1.6`. To use a specific
Inno compiler, pass `-IsccPath 'C:\Program Files (x86)\Inno Setup 7\ISCC.exe'`.
`-SkipBuild` is only for when both integrated app executables are already built.

The script gathers the combined setup here:

```text
D:\blubox-studio\dist\Blubox-Setup-<version>.exe
```

The intermediate app staging is at `dist/release_staging/`; the independent child
setups are at `dist/child-installers/`. All are generated release outputs and should
not be copied into the source-controlled source tree.
