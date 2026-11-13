# 12-bodies: Real bodies between Links

Each mate's puppet gets a real body in your game: your Link stops at it as at a townsperson and can stand on its head. Before this, two Links simply walked through each other, and a mate who pressed Continue on the same spot as you was drawn inside your Link. The bodies are a room switch (`/choque`), on by default. When the bodies cannot run (old puppet pack, a create the game refuses, a switch set to off), the mate's puppet is only drawn aside, which is the older layer of this feature.

## What changes for players

- Walking into a mate stops your Link beside him. Walls and railings still count, because it is the game's own physics that stops you, not our code.
- You can stand on a mate's head (a thin standable disc on top of his body). From the sides nobody climbs up: you slide off, as on a townsperson.
- Nobody gets wedged. A mate's body is a "ghost" (not solid) until the two of you are 0.8 m apart, or one over the other, for 0.3 s. Two players who press Continue on one spot therefore start apart. While a body is a ghost its puppet is drawn aside (1 m at most, presentation only).
- A body is also a ghost while that mate is downed (so a reviver can kneel by him), riding, hidden or elsewhere, and after his samples jump more than 3 m (a warp).
- Anyone in the room can type `/choque on` or `/choque off` (aliases `/cuerpos`, `/bodies`; `/choque` alone says how it is; Tab completes `on`/`off`; listed in `/ayuda`). Everyone sees a notice "Choque entre jugadores activado por <name>", and a late joiner sees "En esta sala Choque entre jugadores está activado (lo activó <name>)".
- Your own Link's position is never written by this feature.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `/choque` (room switch, chat) | on | on (`kBodiesDefault = true`, `coop/host/include/coop/host/orm_pvp.hpp`) | `on`, `off` | The room's choice: real bodies for everyone, or none. Kept by the server like any room switch |
| `LICHII_ORM_BODIES` | `LICHII_ORM_BODIES=1` | on (`coop_puppet_config.cpp`, `CoopSwitch::On`) | 0/1/true/false/yes/no/on/off | The drawn-aside layer: a mate's puppet standing on your Link is drawn out to two body radii. 0: puppets are drawn on their sample even when overlapping. Also named in the `/pvp`-commands start line |
| `LICHII_ORM_BODY_COLLIDER` | not in the kit | `room` (`coop_bodies.cpp`, `CoopSwitch::Value`) | `room`, or an on/off word | `room`: the room's `/choque` decides. On: bodies whatever the room says (used by probes). Off: never bodies on this PC, mates only drawn aside. Any other value: `room` and a log line |
| `LICHII_ORM_BODY_TOP` | not in the kit | on (`coop_bodies.cpp`) | 0/1/true/false/yes/no/on/off | The head disc. 0 uses the `_BodySide` actors (sides only: you slide off a head) |

Nothing here works without `LICHII_COOP_PUPPET=1` (the bodies follow the puppets) and a puppet pack built with this kit (it must list `CoopPuppet_*_Body`).

## How it works

| File | What it does |
|---|---|
| `tools/puppet_pack/build.py` (`BODY_KINDS`) | Builds four extra actors from each player's own romfs: `CoopPuppet_Child_Body`, `CoopPuppet_Adult_Body` (sides plus head disc) and `CoopPuppet_Child_BodySide`, `CoopPuppet_Adult_BodySide` (sides only). Each is two kinematic rigid bodies that track the actor and hit only the `Player` layer: the sides are the player's own `Player_cct` prism (radius 0.23 / head 1.17 m child, 0.35 / 1.68 m adult) on the `Character`/`NPC` layer with the unridable character material; the disc is 0.12 m thick on the head, on the `GroundObject` layer with a standable material. No model, animation, life or AI: the same shape as the game's invisible `FldObj_AirWall_Npc` walls. Lists them under `"bodies"` in `coop_puppet.json` |
| `coop/host/include/coop/host/orm_bodies.hpp`, `coop/host/src/orm_bodies.cpp` | Portable logic, no emulator. `PuppetAside`: the drawn-aside step (way from our Link to the puppet; on top of each other a fixed way from the mate's number; eased at 2 m/s). `ReadBack`: logs when the game moved a puppet we drew. `BodyGate`: solid or ghost, plan by plan (`kClearM` 0.8 m for `kClearForMs` 300 ms to go solid; a ghost again closer than `kDeepM` 0.25 m, downed, riding, not shown, or a jump over `kJumpM` 3 m), plus the self-check lines for the probes |
| `lichii/hook/coop_bodies.h`, `lichii/hook/coop_bodies.cpp` | The game glue. A fourth puppet director (svc caller order 26: after the puppets, spawn, fairies and horses; before the object mover 30) creates and places the body actors. Each frame the body goes exactly where the puppets' director drew that mate this frame (`CoopPuppet::LastSmoothed`), upright, then the game's own matrix setter runs (`SetMatrixCall`) so its physics takes the place. Bodies are created parked under the floor (`SetCreateParked`): never on anyone. `Choose` picks on/off/fallback; `CreateTally` counts create lines and falls back after two failed creates of an actor with none made |
| `coop/host/src/orm_commands.cpp` | The `/choque` command entry (`Id::bodies`, aliases, `Who::anyone`) |
| `coop/host/src/orm_pvp.cpp` | Room switch names and defaults (`default_on`: `bodies` is the one key on by default), and the notices |
| `lichii/hook/orm_commands_game.cpp` | Keeps the room's current `/choque` state (`BodiesOn`) and the fallback flag (`SetBodiesFallback`); when our game cannot make bodies, the `/choque` reply adds "(en tu juego los compañeros solo se apartan: mira el registro, 'bodies: collider')" |

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_puppet.cpp` | The planner runs `PuppetAside` (when `settings_.bodies`) and the `BodyGate` (when colliders are on): a solid body's puppet is drawn on its sample, a ghost's aside. Gate and aside lines join the puppet log lines |
| `lichii/hook/coop_puppet_config.cpp` | `OrmSettings` reads `LICHII_ORM_BODIES`; `ConfigurePuppets` calls `CoopBodies::Use()` after the puppets are configured |
| `tools/puppet_pack/build.py` | The body actors are built with the other puppet actors (`BODY_KINDS`) |

## Messages and components

- `ROOM_SWITCH` (0xF008), key `bodies` = 3 (`RoomSwitchKey` in `coop/protocol/include/coop/protocol/messages.hpp`). Keys 1 and 2 are PvP and ally lock-on (09-pvp). The server stores and relays any key; it has no code of its own for bodies.
- No other message: the bodies are placed from the puppets' existing samples.

## Tests

| File | What it checks |
|---|---|
| `tests/orm/bodies_test.cpp` | Drawn aside: the Continue-spot case (the puppet drawn out at once, our Link never moved), the eased step, the fixed way by number through jitter, downed / another floor / not shown / no Link drawn on the sample, the read back once per contact, the planner with `LICHII_ORM_BODIES` on and off. Bodies: ghost until clear then solid, a new mate starts a ghost even far away, warp / downed / riding / hidden / no Link make a ghost, one over the other counts as clear (stand on a head), a solid body drawn on its sample, the planner's gate (`SetColliders`), the probe self-check lines |
| `tests/lichii/bodies_hook_test.cpp` | `LICHII_ORM_BODY_COLLIDER` parsing (room by default, on/off words, anything else room with a line), actor names by age and kind and which the pack lists, `Choose` (room switch, forced modes, pack, broken create), the bodies' plan from the puppets' plan (place less the aside, solid shown, ghost parked), the same-frame pose only, the create tally |
| `tools/puppet_pack/tests/test_puppet_pack.py` (`BodyTest`, full-build test) | The body actors' Phive files: two rigid bodies `CoopBody` and `CoopTop`, sizes from the player's prism |
| `tests/orm/commands_test.cpp` | `/choque`, `/cuerpos` parse to the room's bodies switch |

Harness scenarios (they live in `tools/orm/harness`): `choque-bloqueo` (B types `/choque on` and stands still; A walks at him and must stop, then walk off without being wedged), `choque-encima` (A drops onto B's head and must stand there; with `LICHII_ORM_BODY_TOP=0` he must slide off), `choque-continuar` (both appear on one Continue spot, A joining second; no walk times out, nobody's Link is moved). `cartel` and `charla` cover the older drawn-aside case on the Continue spot.

## Log lines

- At start: `coop: bodies: colliders by the room's /choque; the head disc on; the puppet pack has CoopPuppet_*_Body, CoopPuppet_*_BodySide` (the mode and pack parts change with the switches and the pack).
- On each change: `coop: bodies: colliders on (the room's /choque, CoopPuppet_*_Body, with the head disc): each mate's body waits until clear of our Link`, or `coop: bodies: colliders off: <why>` (for example `the room's /choque is off: the mates are drawn aside`).
- Once per path: `coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`, or `coop: check: bodies: collider fallback <why>` (no body in the pack: run make-puppet-pack again; a body could not be made 2 times; `LICHII_ORM_BODY_COLLIDER=0`).
- The gate: `bodies: P2's body a ghost until clear: on our Link (0.17 m)`, `bodies: P2's body solid (1.00 m from our Link)`, `bodies: P2's body a ghost: <why>` (downed, riding, not shown, our Link not read, its samples jumped).
- Probe self-checks: `bodies: our Link against P2's body (0.47 m apart, the bodies 0.46)` after 0.5 s touching, `bodies: our Link stands on P2's head (1.17 m over his feet)` after 1 s on top.
- Drawn aside: `bodies: P1's puppet on our Link (...): drawn ... m aside..., our Link untouched`, `bodies: P1's puppet drawn on its sample again`, `bodies: P1's puppet read back where we drew it aside ...`.
- The body director's own lines carry a `body ` prefix, for example `body coop: puppet: P2: create CoopPuppet_Child_Body requested ...`.

## Status

Confirmed in game on test10, two players, WebSocket, sealed protocol 110, A joining second: scenario `choque-bloqueo` (14/14: `collider ok CoopPuppet_Child_Body`, A stops 0.44 m from B with bodies 0.46 apart, then walks off), `choque-encima` (10/10: `our Link stands on P1's head`), `choque-continuar` (9/9: the puppet drawn 1 m aside on the shared Continue spot, the body solid there, no walk timed out, no Link moved). `cartel` and `charla` (4/4 each) passed in the same run, so the earlier Continue-spot stall did not return.

## Known gaps

- Not yet seen with a high ping. Each game stops its own Link at the other's body; with a lot of delay the game may give a small push. If that looks bad, raise `kDeepM` or lower the radius.
- Not checked: what B sees while A stands on his head (A's body touches B's head from above and may push him aside), whether the disc carries a Link along when the mate below walks, and whether your own sword, arrows or the camera notice the bodies (they should not: the bodies hit only the `Player` layer).
- The bodies are parked 500 m under the floor while they are ghosts; if the game ever deletes a parked body, the director makes it again (it would show in the log).
- The app's "Cooperativo" screen has no checkbox for `/choque`; it is chat only.
- Not tried on a Hookshot build.
