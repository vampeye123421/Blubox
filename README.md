# Blubox Client, Studio, and update feed

`version.json` is consumed by the Windows bootstrap launcher from:

`https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json`

The manifest points to versioned GitHub Release assets and includes SHA-256 digests for the client, both launchers, Studio, and Studio fonts. The client launcher installs or repairs Studio on first run and checks it for updates before launching the game. Studio has a separate updater launcher used by its Start Menu shortcut.

The client launcher creates a **Blubox Studio** Start Menu shortcut, registers `blubox://` and `blubox-studio://`, and installs versioned launchers under `%LOCALAPPDATA%\Blubox\launchers`. Each app launch repairs its own per-user protocol registration; each runtime release also publishes both launchers and advances their manifest versions and SHA-256 hashes, so the registry and shortcuts move to the verified launcher after updates.

Each release also includes `Blubox-Setup.exe`, which installs the Client and Studio using two independent per-user installers. Each app has its own Start Menu and Desktop shortcut and its own Windows uninstall entry. The setup package is the website's recommended download; it installs bootstrap launchers, while the lowercase `blubox_launcher.exe` and `blubox_studio_launcher.exe` assets are the versioned, hash-verified updater payloads.
