# Blubox update feed

`version.json` is consumed by the Windows bootstrap launcher from:

`https://raw.githubusercontent.com/vampeye123421/Blubox/main/version.json`

The manifest points to a versioned GitHub Release asset and includes its SHA-256 digest. Publish the release asset `blubox_client.exe` first, then publish the matching `version.json`; otherwise clients may see a manifest that references an unavailable download.

The release should also include `blubox_launcher.exe`. The launcher is the registered `blubox://` protocol handler; it downloads and verifies the versioned client. Launcher self-updates are not implemented yet and require a separate installer/bootstrap update mechanism.
