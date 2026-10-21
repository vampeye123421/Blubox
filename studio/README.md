# Blubox Studio

Blubox Studio is a Windows-first, C++20 3D map editor built on Raylib 5.5, Dear ImGui docking,
rlImGui and ImGuizmo. Its editor target is independent of RedGem's game runtime: Ruby, mruby,
Ruby bindings, `.rb` execution, Docker and RedGem's unrestricted project/file panels are not part
of the Studio build.

## Build on this PC

The current toolchain is already installed at `D:\CMake` and `D:\MinGW`. Configure and build
from PowerShell:

```powershell
Set-Location D:\blubox-studio
$env:Path = "D:\MinGW\bin;D:\CMake\bin;$env:Path"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target blubox_studio -j2
.\build\blubox_studio.exe
```

CMake FetchContent downloads pinned Raylib 5.5, Dear ImGui docking, rlImGui, ImGuizmo,
nlohmann/json 3.12.0 and Luau 0.743 on first configure. The editor links only Luau's VM and
compiler; its CLI and test targets are disabled. No npm, Ruby, Docker, Jolt or Ninja install is
needed for this Studio target. The installed MinGW Makefiles generator is used here.

If setting up another Windows PC, install CMake and MSYS2, then install the compiler with:

```powershell
winget install --id Kitware.CMake --exact
winget install --id MSYS2.MSYS2 --exact
& 'C:\msys64\usr\bin\bash.exe' -lc 'pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make'
```

Use that MSYS2 UCRT64 compiler and its `MinGW Makefiles` generator for the build. Alternatively,
Ninja can be installed with `py -m pip install ninja` and selected with `-G Ninja`.

## Project boundary

Maps are held in memory while editing. Studio does not expose local map save/load, an open-folder
button, a system file picker, a shell-open action or a drag/drop import path. Creating a world
starts an in-memory edit session; loading and persistence happen only through the Blubox website
API. This keeps the editor within the assigned map/project boundary rather than exposing the
user's general filesystem.

Cloud map requests go to the Blubox website over HTTPS. The native app has no Supabase URL or
service-role key. A short-lived website session token is held in memory for API requests and is
never written into the map. The Studio adapter currently expects these website routes:

- `POST /api/studio/maps` with a `blubox-map` JSON document and a Bearer user session token.
- `GET /api/studio/maps/{worldId}` returning that document (directly or in a `map` property).

The Blubox Render companion service implements these routes. It validates every bearer token
against Supabase Auth and limits map reads and writes to the authenticated owner's maps. Run
`D:\blubox-website\supabase\studio-maps.sql` in the Supabase SQL editor before enabling cloud
map persistence.

The production API base defaults to the website's private server-side companion service at
`https://blubox-mailer.onrender.com`; local builds can override it with
`BLUBOX_STUDIO_API_BASE_URL`. This avoids sending native API requests to the static-site host,
which cannot execute API routes.

## Studio sign-in

Studio opens `https://blubox.runs-on.dev/auth/studio` for sign-in. On success, the website
redirects to `blubox-studio://auth?token=<URL-encoded-session-token>`. Studio validates that
token with `GET /api/studio/session` using `Authorization: Bearer <token>`; the endpoint should
return an authenticated `user` object with an `id` and optionally `displayName`, `display_name`,
`name`, or `email`. Sign-in is not considered complete until that request succeeds.

The saved token is DPAPI-protected for the current Windows user and stored as ciphertext in
`%LOCALAPPDATA%\BluboxStudio\session.json`. It is decrypted only in memory for API requests.
For local testing, register the custom protocol from PowerShell after building:

```powershell
.\tools\register-protocol.ps1 Install
```

If the executable is elsewhere, pass its path as the second argument. Remove the association with
`.\tools\register-protocol.ps1 Uninstall`. The registration is scoped to the current Windows user.

### Test sign-in against localhost

The website frontend and API can run locally without deploying the site. In the website project,
put the Supabase project URL and public anon key in its uncommitted `.env.local`, then run:

```powershell
Set-Location D:\blubox-website
npm start
```

In Supabase Auth URL Configuration, allow `http://localhost:3000/**` as a redirect URL. Then start
Studio in a second window with the local website and API on the same origin:

```powershell
Set-Location D:\blubox-studio
.\tools\register-protocol.ps1 Install
.\run-local.ps1
```

To print sign-in and API diagnostics in the same PowerShell window, close any open Studio window
and run `.un-local-debug.ps1`. It prints the API endpoint and validation errors while redacting
session tokens. Keep that terminal open while signing in from `http://localhost:3000/auth/studio`.

The client permits unencrypted API traffic only for loopback hosts; production still requires
HTTPS. Apply `supabase/studio-maps.sql` once before testing cloud map saves.

## Map format

The versioned `.blubox` schema and its client/server integration contract are described in
[`docs/blubox-map-format.md`](docs/blubox-map-format.md). Version 1 records stable entity ids,
names, primitive types, transforms, colors and typed components. It uses meters, right-handed
coordinates and +Y up. Future client/server readers should reject unknown major versions and
ignore component types they do not implement.

## Current editor scope

The initial Studio shell provides a dockable workspace, primitive block/sphere creation,
Explorer, transform inspector, ImGuizmo translation/rotation/scaling, in-memory world editing,
per-entity Luau editing/preview, and HTTPS production API calls. Each entity's Luau source is part of
the `.blubox` JSON document. The website and game runtime still need the documented map routes and a
separate server-side/runtime implementation before these maps and scripts can be published and
played end to end.
