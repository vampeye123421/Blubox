# Blubox update feed

`version.json` is consumed by the Windows bootstrap launcher from:

`https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json`

The manifest points to versioned GitHub Release assets and includes SHA-256 digests for the client, Studio executable, and Studio fonts. Run `blubox_launcher.exe --studio` to install or update Studio and launch it. The release workflow publishes binaries first, then updates the matching `version.json`.

The first `--studio` launch also creates a **Blubox Studio** shortcut in the Windows Start Menu and registers `blubox-studio://` for browser sign-in handoff.

The release should also include `blubox_launcher.exe`. The launcher is the registered `blubox://` protocol handler; it downloads and verifies the versioned client. Launcher self-updates are not implemented yet and require a separate installer/bootstrap update mechanism.
