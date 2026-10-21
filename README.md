# Blubox Client, Studio, and update feed

`version.json` is consumed by the Windows bootstrap launcher from:

`https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json`

The manifest points to versioned GitHub Release assets and includes SHA-256 digests for the client, both launchers, Studio, and Studio fonts. The client launcher installs or repairs Studio on first run and checks it for updates before launching the game. Studio has a separate updater launcher used by its Start Menu shortcut.

The client launcher creates a **Blubox Studio** Start Menu shortcut, registers `blubox://` and `blubox-studio://`, and installs versioned launchers under `%LOCALAPPDATA%\Blubox\launchers` so protocol and Start Menu entries can be repaired as launcher releases change.

Each release also includes `Blubox-Setup.exe`, which installs the Client and Studio using two independent per-user installers. Each app has its own Start Menu and Desktop shortcut and its own Windows uninstall entry. The setup package is the website's recommended download; `blubox_launcher.exe` and `blubox_studio_launcher.exe` remain versioned updater payloads used by the app launch flow.
