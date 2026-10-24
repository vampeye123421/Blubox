# Local Play Mode

Local Play is implemented as a serialized handoff from the editor document into two independent runtime documents.

## Runtime boundaries

- `RuntimeSnapshot::capture` validates and serializes the editor map and classifies `Script` and `LocalScript` assets.
- `ServerRuntime` and `ClientRuntime` each reconstruct their own `MapDocument`, `SceneRuntime`, `InstanceRegistry`, Luau VM, and `RemoteManager`.
- The client runtime document is used as the Play viewport source. Runtime edits do not write into the editor document.
- Server simulation and ENet polling run on a `std::jthread`; the local client is polled and rendered on Studio's UI thread.
- ENet binds the server to `127.0.0.1:0`, reads the OS-assigned port, and authenticates the client with a one-session random credential through Hello/Welcome.

## Script policy

Each runtime has a separate Luau VM. Each script is loaded on its own `luaL_sandboxthread` environment. `io`, `os`, `debug`, `dofile`, `loadfile`, `loadstring`, and `require` are removed. The embedder caps each VM at 64 MiB and applies a 20 ms interrupt budget to bootstrap execution and each scheduler tick. `game:GetService` enforces an allowlist before returning a handle:

- Server: Workspace, Players, ReplicatedStorage, ServerScriptService, ServerStorage, RunService, Lighting.
- Client: Workspace, Players, ReplicatedStorage, StarterGui, StarterPlayer, RunService.
- Shared: Workspace, Players, ReplicatedStorage, RunService.

## Scope of this first slice

This establishes separate worlds, sandboxed script contexts, authenticated local ENet transport, and Play lifecycle controls. It does not yet implement replicated property snapshots/deltas, a physics-authority loop, or Luau `RemoteEvent`/`RemoteFunction` methods. The existing ENet manager and registries are instantiated independently and map authored RemoteEvent names, ready for that transport/API slice. Local Play rendering currently draws the independent client snapshot with Studio's existing Raylib renderer.
