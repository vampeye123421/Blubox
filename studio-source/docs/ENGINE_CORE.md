# Blubox Studio engine core

The first engine slice lives in `engine/` and is independent from the editor's
existing `MapDocument` JSON format. `SceneRuntime` adapts the current document
to stable runtime handles without changing saved maps.

## Runtime ownership and update order

Create one `InstanceRegistry` (also exposed as `DataModel`) for a world. It
creates `Workspace`, `ServerScriptService`, `ServerStorage`, `ReplicatedStorage`,
`StarterPlayer`, `StarterGui`, and `Lighting` services. Children are linked by
slot indices; public references are `InstanceHandle { id, generation }`. Destroy
increments the generation, so stale Lua, physics, and renderer references fail
their liveness check.

`EngineLoop::advance(frameDelta, renderDelta)` provides a 60 Hz fixed simulation
accumulator and fires `PreSimulation`, `Stepped`, physics, transform propagation,
`Heartbeat`, then `RenderStepped`. Call it on the main thread. Worker threads
must enqueue commands for the main thread instead of mutating the registry.
`WorldTransform` is cached per instance and recalculated only for dirty roots
and their descendants.

```cpp
blubox::engine::DataModel dataModel;
auto workspace = dataModel.service("Workspace");
auto block = dataModel.create("Part", blubox::engine::ClassId::Part, workspace);
dataModel.get(block)->local.position = {0, 2, 0};
dataModel.markTransformDirty(block);
dataModel.flushTransforms();
```

`SceneRuntime` builds that hierarchy from an open editor map and can synchronize
local transforms, names, and colors back into the existing `MapDocument` before
the current save path runs. The existing `.blubox` document format remains
compatible in this slice.

## Luau

`ScriptEngine` installs `task.spawn`, `task.defer`, `task.delay`, `task.wait`,
`task.cancel`, and a `RunService.Connect(eventName, callback)` table for
`Stepped`, `Heartbeat`, `RenderStepped`, and `PreSimulation`. Scheduled work is
resumed on the editor thread, and the existing Luau instruction/time interrupt
is applied to scheduled tasks and signal callbacks. Errors are forwarded to the
Studio output callback. `LuaInstanceBinding` registers one shared metatable and
tagged userdata containing handles, with safe name, children, and position
accessors; it never gives scripts an entity address.

## Rendering and UI

`RaylibRenderAdapter` draws cached part transforms while Raylib's 3D mode is
active. `Part`, `Sphere`, `WedgePart`, and `CornerWedgePart` have render
components; wedge classes are currently rendered as box placeholders. The UI
layout engine has a bottom-up measure pass and top-down arrange pass for frames,
buttons, list/grid layouts, and padding. Its output can be submitted by a
Raylib/ImGui renderer; it does not replace the Studio's current ImGui widgets yet.

## Physics and networking boundaries

`PhysicsWorld` uses Jolt 5.3 with separate static/dynamic collision layers,
anchored and dynamic box bodies, force/impulse operations, a fixed-step update,
and Jolt `CharacterVirtual` capsule controllers. The current Humanoid input
adapter maps horizontal axes and jump to controller velocity; a production
controller still needs slope tuning, animation state integration, custom gravity
zones, and parent-space synchronization for nested moving models.

`RemoteManager` in `engine/Remote.*` remains the validated legacy route layer.
`engine/network/RemoteManager` now provides ENet host/client transport, packet
framing, a credential-validation handshake callback, server-assigned player
IDs, per-session nonce checks, strict packet sequence/timestamp checks,
per-player/per-type token buckets, RemoteEvent delivery and asynchronous
RemoteFunction request/response callbacks with deadlines. `bindLegacy()` wires
the existing `engine::RemoteManager` sender and validated server event receiver
to this transport. The bound legacy object must outlive the binding.

The ENet handshake deliberately does not decide what a credential means.
The game server must provide `Authenticate`, validate the website-issued join
credential (signature, issuer, audience, expiry, and intended game/server), and
return a server-selected `PlayerId`. The transport then assigns a random
per-connection session nonce. That nonce plus sequence/timestamp checks help
reject stale or cross-session packets; this layer does not implement packet
encryption or a message-authentication code, and ENet's connection security is
not a substitute for validating the website token. Remote IDs use a compact
16-bit FNV-1a name hash; collisions fail registration and must be resolved in
the content schema. ENet channels 0, 1, and 3 use
reliable delivery; ENet orders reliable packets within each channel. Channel 2
uses ENet's unreliable sequenced delivery.

`engine/ui/` adds an independent slot-map-backed UI scene, two-pass
`UiLayoutResolver`, immutable `UiDrawList` snapshot, renderer interface, and
Raylib backend. The first layout prototype remains under
`blubox::engine::ui::legacy`; new runtime code uses `blubox::engine::ui`.
Layout headers do not include Raylib. The existing Studio window system is
still Dear ImGui; this retained-mode runtime UI is a separate path for game UI
instances, not a wholesale rewrite of ImGui editor panels. The draw-list
snapshot owns its text and stays immutable after subsequent resolver calls.

## Current integration limits

- Studio's current editor rendering and inspector still operate on
  `MapDocument`; `SceneRuntime` is an adapter layer and is not yet the editor's
  authoritative document store.
- Runtime-created `Model`, `Folder`, UI, and remote instances are supported by
  the registry/class IDs, but the existing editor's create-object menu and
  property inspector have not been migrated to those runtime classes.
- Raylib handles the primitive preview; wedge meshes and full material,
  lighting, UI input/hit testing, and script-visible runtime UI construction
  remain future work. `RaylibUiRenderer` handles rectangle, texture, text and
  scissor commands from the new immutable draw list.
- The ENet module is compiled into Studio as engine infrastructure; production
  game-server startup, web-token verification, shared remote schema rollout,
  encryption/MAC, and deployment configuration belong to the server project.
- This slice keeps map serialization backward-compatible; the chunked binary
  format from the research note is not introduced here.
