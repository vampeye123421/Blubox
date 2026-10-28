# Blubox Client and Launcher

C++20 foundation for Blubox. `blubox_launcher` is the lightweight Raylib UI bootstrapper; `blubox_client` contains the game built with Raylib and Jolt Physics; `blubox_server` is the headless dedicated server based on ENet.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler (MSVC, Clang, or GCC)
- Git and Internet access for the first configure: CMake downloads Raylib 5.5, Jolt Physics 5.3.0, ENet, and nlohmann/json
- OpenSSL development package on non-Windows systems (Windows uses the system-provided CNG/BCrypt)

## Build

In PowerShell, from the project directory:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With Visual Studio, the executables are created at `build/Release/blubox_launcher.exe` and `build/Release/blubox_client.exe`. With a single-configuration generator, they are placed directly in `build/`. CMake places the launcher, client payload, and `version.json` in the same output directory.

## Launch and updates

```powershell
./build/Release/blubox_launcher.exe --mode=guest
./build/Release/blubox_launcher.exe --mode=guest --fullscreen
./build/Release/blubox_launcher.exe "blubox://place/123"
```

The client launcher checks the HTTPS feed in `version.json`, automatically installs or repairs Blubox Studio and its fonts, then updates and launches the client. Studio is managed by a separate launcher (`blubox_studio_launcher.exe`) and opened from the Blubox Studio shortcut in the Start menu. Both launchers verify published SHA-256 hashes before installing files.

## Register `blubox://` on Windows

Run the launcher once:

```powershell
.\build\Release\blubox_launcher.exe --register-protocol
```

With MinGW or a single-configuration generator, use `./build/blubox_launcher.exe --register-protocol`. The registration is written to `HKEY_CURRENT_USER\Software\Classes\blubox`, requires no administrator privileges, and points to the launcher. To remove it:

```powershell
.\build\Release\blubox_launcher.exe --unregister-protocol
```

Once registered, Windows starts the launcher with the URL; the launcher forwards the received arguments to the client, including `blubox://join?token=...&worldId=123`. The client extracts the token and world ID into `ClientSession`, which remains in memory only. The token is removed from the copied URI/query after extraction and overwritten when the session is destroyed; it is never written to a file or included in logs. The client keeps non-sensitive routing data in `ProtocolLaunchData`. Percent-encoded values are decoded; duplicate parameters or invalid encoding are rejected. A `join` request without a token or world ID produces a warning.

For a valid `join` URI, the client opens an asynchronous ENet UDP connection to `blubox-node1.duckdns.org:7777` and sends a version 2 handshake with the token and world ID over a reliable channel. The loop calls `enet_host_service(..., 0)` without blocking rendering. After authentication, the client sends the player's position at 20 Hz; the server broadcasts world snapshots at 60 Hz. ENet does not encrypt traffic.

## Controls

- `W`, `A`, `S`, `D` (or arrow keys): move the player
- `SPACE`: jump when the character is on the ground
- Mouse: rotate the third-person view
- `TAB`: capture or release the mouse (`left click` to capture it again)
- `F11`: toggle fullscreen
- `ESC`: exit

Physics uses a fixed 120 Hz timestep accumulator, with a limit on catch-up after pauses or dragged windows. The graphics frame rate targets 180 FPS, and the window can be resized.
Remote players have kinematic capsule colliders in the local physics world, so the local player cannot walk through their avatars. Their collider positions follow the same smoothed network snapshots as their rendered avatars.

The visible R6 character is embedded from `assets/avatars/BluboxR6_Studio.glb`. Because this model has no skeleton or skin weights, the client animates its arm and leg meshes procedurally for walking and jumping rather than applying skeletal clips to an incompatible mesh. The `BluboxR6_Walk.glb` and `BluboxR6_Jump.glb` files are not used by this static model path; skeletal playback requires a skinned export of the visible character.

## Dedicated server

The dedicated server and its updater are maintained separately in the private
`vampeye123421/blubox-server` repository. The client uses ENet protocol version
2 and connects to the node configured by the site's join flow.
