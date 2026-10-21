# Blubox Map Format

The canonical map file is UTF-8 JSON with the `.blubox` extension. The current writer emits
`format: "blubox-map"`, `formatVersion: 1`, meters, right-handed coordinates and `+Y` up.

```json
{
  "format": "blubox-map",
  "formatVersion": 1,
  "coordinateSystem": {
    "handedness": "right",
    "up": "+Y",
    "units": "meters"
  },
  "worldId": "stable-world-id",
  "name": "My World",
  "entities": [
    {
      "id": "stable-entity-id",
      "name": "Baseplate",
      "kind": "block",
      "transform": {
        "position": [0, -0.5, 0],
        "rotation": [0, 0, 0],
        "scale": [24, 1, 24]
      },
      "color": [92, 143, 116, 255],
      "visible": true,
      "scripts": {
        "luau": "body.onTick(function(dt)\n    -- entity script\nend)"
      },
      "components": [
        { "type": "Collider", "shape": "box", "static": true }
      ]
    }
  ]
}
```

`transform.position` and `transform.scale` use world meters. Rotation is Euler radians in X/Y/Z
order. `kind` is currently `block` or `sphere`. Component objects carry a required string `type`
and typed properties defined by that component. Entity/map ids remain stable between edits so the
server can use them for replication identity. `scripts.luau` is optional source text for that
entity, limited to 256 KiB. Scripts are source data, never native code or bytecode. The writer
validates finite transforms, bounded values, supported entity types, and a 100,000-entity limit
before publishing.

## Luau Studio API

The editor runs Luau in a separate sandboxed VM with a 64 MiB allocator limit, a 20 ms execution
budget for initial runs and an 8 ms budget per tick/event callback. It omits `os`, `debug`, dynamic
loading and `require`. Every entity script has access to:

- `game.broadcast(message)`, `game.getEntity(idOrName)`, `game.spawnEntity(kind, name?)`,
  `game.destroyEntity(id)`, `game.getTime()` and `game.findEntitiesInRadius(position, radius)`.
- `math.createVector3(x, y, z)`, `math.distance(a, b)`, `math.lerp(a, b, alpha)`,
  `math.clamp(value, minimum, maximum)`, `math.randomRange(minimum, maximum)` and
  `math.randomInt(minimum, maximum)`.
- `body.getName()`, `body.getPosition()`, `body.setPosition(vector)`, `body.getRotation()`,
  `body.setRotation(vector)`, `body.setColor(r, g, b, a?)`, `body.setVisible(bool)`,
  `body.applyForce(vector)`, `body.applyImpulse(vector)`, `body.onTouch(callback)`,
  `body.onUntouch(callback)` and `body.onTick(callback)`.

`onTick` runs during the Studio preview loop. `game.broadcast` currently writes to the editor's
status line; it does not send a network message. Touch handlers are registered for the runtime
host to dispatch; Studio currently has no collision simulation to generate touch events. Force
and impulse calls serialize `ScriptPhysics.pendingForce` / `pendingImpulse` component data for the
game runtime to consume. They do not move entities in Studio, which currently has no physics
preview. These editor APIs must be implemented and independently sandboxed in the production game
runtime before published scripts execute in multiplayer.

## Runtime integration still required

The existing Blubox client/server protocol currently transports player/session state and does
not load this map schema. To make a published map playable, implement a shared C++ map reader in
the game client and server. The server must validate map/component allowlists, spawn authoritative
physics entities, and include the map id/revision in the authenticated world handshake. Clients
must render the approved entity types and treat server state as authoritative. Do not let a map
file choose arbitrary native code, scripts, filesystem paths, or network endpoints.

## Website API contract

The native Studio calls only the website, never Supabase directly:

- `POST /api/studio/maps`: JSON map body, `Authorization: Bearer <user-session-token>`; server
  validates ownership and writes the versioned `.blubox` data to managed storage.
- `GET /api/studio/maps/{worldId}`: authenticated read; server verifies access and returns either
  the map JSON or `{ "map": <map-json> }`.

Both routes must enforce body-size limits, schema validation, ownership/permissions, rate limits,
and safe world ids. These endpoints are integration requirements for the website backend and must
be deployed there before Studio's cloud buttons can succeed.
