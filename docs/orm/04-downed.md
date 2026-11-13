# 04-downed: Downed, revive and group defeat

With another player in the room, a Link whose hearts run out lies on the ground instead of getting the game over, and a mate who holds A beside him for 3 s gets him up. When everyone is down at once, the game over runs for all (group defeat). Around that core: a fairy in your own bottle works as in the original, a spectator camera while you lie down, the group keeps its state through Continue, and an optional sound when a mate goes down. The rules follow our earlier lockstep co-op for the N64 game ("downed, not dead"; nobody dies and simply comes back).

## What changes for players

| Situation | What happens |
|---|---|
| Alone in the room (or no co-op session) | The game's own death and game over, unchanged. |
| A mate in the room, hearts run out | Link lies down where he fell. Top of the screen: "Has caído: un compañero puede revivirte (A 3 s a tu lado)". The mates get a chat notice "<name> ha caído: ve a su lado y mantén A". |
| A mate stands within 1.5 units and holds A | His screen counts "Mantén A para revivir a <name> n %", yours "<name> te está reviviendo n %". After 3 s you get up where you fell (the game's own fairy revive animation, no reload). |
| Life after a mate's revive | At least 3 hearts or 25 % of your max, whichever is more, rounded up to a whole heart. |
| You have a real Fairy in a bottle of your own | It gets you up at once, as in the original; you are never "down" and nobody needs to come. |
| The last mate leaves while you lie there | 10 s for one to come back ("Has caído y no queda nadie: game over en n s"), then your own game over. |
| Your own connection drops while down | You stay down up to 60 s; after the reconnect you are still down and can be revived. |
| Everyone down at once for 2 s | Group defeat: "Todos habéis caído: game over", the game over runs in every game, each player presses Continue. |
| After a group game over (Continue) | By default the group goes on with what it had when it fell: doors, chests, switches, keys, heart capacity, story, and each player's wallet and ammo are written back over what the reload undid. Life is full and you are at the area's entrance, as in the original. |
| While you are down (spectator camera) | Your view turns smoothly to the nearest mate still standing and follows him; d-pad right/left picks the next/previous mate; the screen says "Mirando a <name>". When you are up again the camera is yours. |
| A downed Link at a boss door | The boss room takes him in with the group, still lying down; a game over inside a boss room brings you back into it (details in [05-bosses](05-bosses.md)). |
| A mate goes down (optional) | A low ocarina note (0.7 s) at his puppet when he is within 20 m in your scene, else on the interface. Off in the kit. |

Player-facing texts are in Spanish (that is what our group plays in); they are in `coop/host/src/downed.cpp` (`banner_text`, `mate_down_notice`) and `lichii/hook/coop_spectator.cpp`.

## Switches

| Variable | Kit default (coop-options.ini) | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_COOP_DOWNED` | `LICHII_COOP_DOWNED=1` | off (`coop_downed_game.cpp`, `coop_svc.cpp`) | on/off (`0/1/true/false/yes/no/on/off`) | The whole feature. Also patches the game over gate into `main` at load. |
| `LICHII_ORM_HOOKSHOT_GATE` | not in the kit | off (`coop_downed_game.cpp`, Hookshot builds only) | on/off | A Hookshot build has no code patch, so no gate: there `LICHII_COOP_DOWNED=1` turns itself off unless the build carries the gate's hook and says so with this switch. |
| `LICHII_COOP_SPECTATOR` | `LICHII_COOP_SPECTATOR=1` | off (`coop_camera_probe.cpp`) | on/off | Spectator camera (needs `LICHII_COOP_DOWNED=1`). |
| `LICHII_COOP_SPECTATOR_WAKE` | commented: `# LICHII_COOP_SPECTATOR_WAKE=none` | `stick` | `stick`, `bits`, `both`, `none` | How the game's camera is woken after a load: `stick` gives one short right-left nudge on the emulated pad while Link stands, if the player has not touched the right stick for 3 s (the view turns about 5 degrees, once per load). Any other value keeps the default and logs a line. |
| `LICHII_COOP_CAMERA_YAW_RULE` | commented: `# LICHII_COOP_CAMERA_YAW_RULE=1,180` | `1,180` | `<1 or -1>,<offset>` | How the camera's yaw word maps to the view heading (debug). |
| `LICHII_COOP_DEBUG_CAMERA2` | commented | off | on/off | Camera probe lines (debug). |
| `LICHII_ORM_CONTINUE` | commented: `# LICHII_ORM_CONTINUE=save` | `keep` (`coop_continue.cpp`) | `keep`, `save`, off (`0/false/no/off`); `1/true/yes/on` = `keep` | After a group game over: `keep` writes the group's values back over the reload; `save` follows the host's reload instead (sent as `FLAG_RESET`); off: upstream's rules. Does nothing without `LICHII_COOP_DOWNED=1`. |
| `LICHII_ORM_MATE_DOWN_SOUND` | not in the kit | off (`orm_ocarina_sound.cpp`) | on/off | The mate-down note. |
| `LICHII_ORM_MATE_DOWN_NOTE` | not in the kit | `LowB` | a game ocarina note name | Which note it plays. |
| `LICHII_COOP_BOSS_PULL_DOWNED`, `LICHII_COOP_BOSS_RESPAWN` | commented (`=0` shown) | same as `LICHII_COOP_DOWNED` (`coop_boss_game.cpp`) | on/off | Boss room rules for downed players; see [05-bosses](05-bosses.md). |
| `LICHII_COOP_DEBUG_LIFE` | commented | off | on/off | Your Link's life and status as the co-op reads them (debug). |

Server knobs (fields of `SessionConfig` in `server/session.hpp`, compiled defaults, no command-line option): `revive_hold_us` = 1 s (a REVIVE progress keeps a downed player counted as standing that long; 0 turns that off).

## How it works

### The gate

The game starts its game over event from Link's update with `bl 0x4aa890` at `main+0x4ae6bc`. With `LICHII_COOP_DOWNED=1` that call is patched at load to jump into a small code cave at `main+0x3c46800` (`lichii/hook/coop_downed.s`, bytes in `coop_downed.h`). The cave reads a word at `main+0x3c46880`: 0 means open (call the real function), nonzero means shut (return 0, no game over). While the word is shut, a dead Link lies in `DeadRagdoll` and his Death component says dead.

To get him up, a Fairy is put into a bottle (a real one if there is one, else an owned empty bottle, else a bottle faked whole) and the gate is opened: the game over event's own fairy branch revives him and uses the fairy up. The bottle is then put back as it was. A bottle with anything else in it is never used.

### Files

| File | What it does |
|---|---|
| `coop/host/include/coop/host/downed.hpp`, `src/downed.cpp` | The portable side, no emulator: reads life, max life and the Death word (`read_vitals`), reads and writes the five bottles, picks the bottle (`pick_bottle`), the local state machine `Downed` (alive, downed, reviving, game_over, fairy), the mate's side `ReviveWatch` (A held 3 s within 1.5 units of a downed mate), `revive_life`, `write_life`, the screen texts. |
| `lichii/hook/coop_downed.h`, `coop_downed.s` | The cave and its addresses. |
| `lichii/hook/coop_downed_game.h/.cpp` | The emulator glue, once a frame: reads Link and the bottles, ticks `Downed`, writes the gate word, bottles and life, sets `PLAYER_STATUS` bit 4, keeps the flag watcher quiet on the bottle flags while it writes them, runs the revive watch from player 1's pad (scripted test input included), queues `REVIVE` and `GROUP_DEFEAT` from the network thread, the screen line and chat notices, the mate-down sound. |
| `coop/host/include/coop/host/continue_load.hpp`, `src/continue_load.cpp` | `ContinueLoad` (idle, game_over, reloading, idle) and `parse_continue_mode`. From the game over until Continue's reload has settled, that reload counts as a save load; in `keep` every shared flag it moves is written back to the group's value, and the wallet and ammo (taken once, on the first frame of the game over) are written back with `FlagStore::write_own`. Ends only when a load window opened after the game over closes with Link up and the event over (or after 10 s with no window, e.g. a fairy). |
| `lichii/hook/coop_continue.h/.cpp` | Glue, called from `CoopDowned::Frame`. |
| `lichii/hook/coop_spectator.h/.cpp` | Spectator rules without emulator code: whom to follow (nearest standing mate of our room, d-pad ring, follows him into another room of the scene; another scene: the view holds and the line says where he is for 10 s), smooth turn (yaw 180 deg/s, pitch 90 deg/s), the wake nudge, release when no longer downed. |
| `lichii/hook/coop_camera_probe.h/.cpp` | Finds the player camera object (yaw `+0x1a4`, pitch `+0x18c`) and applies the spectator's output. |
| `lichii/hook/coop_camera_aim.h/.cpp` | Camera checks and yaw/heading maths used by the probe. |
| `coop/host/include/coop/host/mate_down_sound.hpp`, `src/mate_down_sound.cpp` | Once per fall, only for a mate seen standing before; several falls within 2 s give one sound (the nearest); none during our own game over. |
| `lichii/hook/orm_ocarina_sound.cpp` | Plays the cue note on the game's `Ocarina` sound user, on a handle no player uses, released after 0.7 s. |

### Hook points in upstream files

| File | `// ORM` hook |
|---|---|
| `lichii/hook/coop_svc.cpp` | `PatchDownedGate`: the gate patch at load, next to the puppet patch, only with `LICHII_COOP_DOWNED=1`. |
| `lichii/hook/coop_hook.cpp` | `CoopDowned::OnEvent` with the messages, `CoopDowned::Frame` each frame (with the event name for Continue), the chat notices, `DownedBanner()` for the screen line, the hazards reach point fed with the downed state. |
| `lichii/hook/coop_window.cpp` | The `DownedBanner` widget (the Qt window's line when our in-image HUD is off). |
| `coop/host/src/flag_watcher.cpp` | `set_continue_load`, `set_continue_keep_extra`: the Continue window counts as a save load. |
| `coop/host/src/sampler.cpp`, `coop/host/include/coop/host/session.hpp` | `PLAYER_STATUS` bit 4 from our downed state, kept through `Sampler::reset` (reconnect while down); `Session::set_continue_load`, `set_continue_keep_extra`. |
| `server/session.cpp` | `on_revive`: routes `REVIVE` only to a player who is down and marks him held; `check_group_defeat` in `tick`: all players of the session (two or more) with bit 4 for 2 s running, none held, gives one `GROUP_DEFEAT` to everyone. A player who fell to PvP counts as up for 15 s (`pvp_down_hold_us`). |
| `lichii/hook/coop_boss_game.cpp` | Reads `CoopDowned::State()` for the boss room rules. |

## Messages and components

| Message | Number | Channel / sender | Body |
|---|---|---|---|
| `REVIVE` | `0xF000` | client, relayed by the server to the downed player only | `u8` reviver (0 from a client), `u8` downed player, `u8` progress: 0 to 100 while A is held (every 250 ms), 255 = the request. 101 to 254 refused. |
| (reserved) | `0xF001` | never sent | kept for "revived" |
| `GROUP_DEFEAT` | `0xF002` | server only, channel 0 | `u8` players down (1 to 6), `u32` server time in ms. |

`PLAYER_STATUS` bit 4 (`status::dead`, upstream's bit) is what the server counts. It is set while down or while the game over runs, and cleared from the moment a revive is taken (so the group never splits while someone gets up).

## Tests

| File | What it checks |
|---|---|
| `tests/host/downed_test.cpp` | The state machine on fake memory: death word, vitals, bottle choice and restore, gate tries, solo rule, the 10 s and 60 s graces, reconnect while down, progress, group defeat (also during a revive), own fairy (3 cases), life after a revive (queue then direct, at most 3 writes), held down through an age reload and put down again. |
| `tests/host/continue_load_test.cpp` | `keep` and `save` for host and guest, reloads backward and forward, wallet and ammo through `ItemFlagStore`, the window order (life back before or after the load window, an old window does not count, no window in 10 s), count changes from others during the host's reload. |
| `tests/host/mate_down_sound_test.cpp` | Once per fall, near or interface, one sound for several, none in our game over. |
| `tests/orm/orm_ocarina_sound_test.cpp` | The cue note at the puppet, on the interface, released after 0.7 s, off. |
| `tests/lichii/spectator_test.cpp`, `tests/lichii/camera_aim_test.cpp` | Follow rules with a fake camera: nearest, d-pad ring with six, rooms and floors, another scene, wake nudge, release. |
| `tests/server/session_test.cpp`, `tests/orm/server_session_test.cpp` | Group defeat after 2 s, a held player counts as up, the hold's end restarts the 2 s, `revive_hold_us = 0`. |
| `tests/orm/protocol_codec_test.cpp` | `REVIVE` and `GROUP_DEFEAT` round trips and refusals. |

Harness scenarios (in `tools/orm/harness`): `caer-pareja` (A falls, B holds A beside him, A is revived), `caer-solo` (one game: the original game over), `caer-derrota` (both fall: `GROUP_DEFEAT`, Continue in both), `caer-hada-propia` (own fairy), `caer-camara` (spectator camera with two games), `caer-gate`, `caer-fairy`.

## Log lines

- `coop: downed: on (LICHII_COOP_DOWNED): a dead Link stays down until a mate holds A beside him`
- On Hookshot without the gate: `coop: check: downed fallback off on Hookshot: no game over gate in this build (needs the ENTRY_HOOK at main+0x4aa890 and LICHII_ORM_HOOKSHOT_GATE=1): the normal game over`
- Room: `coop: downed: solo, gate open`, `coop: downed: gate shut` / `gate open`
- Transitions, one line each: `coop: downed: alive -> downed: ...`, `downed -> reviving: revive requested by P2`, `reviving -> alive: revived (by P2, life 12 of 12, ...)`, `alive -> fairy: a fairy in our own bottle ...`, `downed -> game_over: group defeat (2 down)`, `game_over -> alive: back after the game over`
- Mate's side: `coop: downed: P1 is down (PLAYER_STATUS dead)`, `holding A beside P1 (0.80 away)`, `coop: downed: REVIVE progress 41 % sent to P1`, `coop: downed: REVIVE sent to P1`
- Downed side: `coop: downed: REVIVE from P2: holding A beside us, 41 %`, `coop: downed: REVIVE from P2 (the request)`, `coop: downed: group defeat: GROUP_DEFEAT from the server (2 down, server time ... ms)`
- Server: `group defeat: all 2 players down`
- Screen copy: `coop: downed: screen: '...'`
- Continue: `coop: continue: keep: after a game over the group goes on with what it had when it fell ...` (or `save ...`, `off ...`), `coop: continue: the game over event is '...' (its name has GameOver)`, `coop: continue: our Continue's reload is done: N write(s) put the group's values from before it back (nothing sent)`
- Spectator: `coop: camera2: ... spectator on (LICHII_COOP_SPECTATOR): a downed Link's view turns to a mate, wake stick`, `coop: spectator: following P2 (nearest)`, `coop: spectator: the camera woke (the right-stick nudge) ...`, `coop: spectator: screen: '...'`
- Sound: `coop: ocarina sound: a mate's fall plays the note LowB for 700 ms ...`, `coop: downed: sound: P2 is down: the sound at his puppet (5.0 away)`

## Status

| Part | Status |
|---|---|
| Downed and revive by a mate | confirmed in game on our test9-based build (scenario `caer-pareja`, 8/8, also with A joining second) |
| Alone: original game over | confirmed in game, one game (scenario `caer-solo`) |
| Group defeat | confirmed in game on an earlier (test8-based) build (scenario `caer-derrota`); not re-run on test10 |
| Own bottled fairy | confirmed in game with one game and a bot as the mate (scenario `caer-hada-propia`) |
| Spectator camera, basic follow and wake | confirmed in game on an earlier (test8-based) build (scenario `caer-camara`); the d-pad with 3+ players, rooms/floors and another scene: not yet confirmed in game |
| Continue `keep` / `save` | not yet confirmed in game (`caer-derrota` now also expects the Continue lines; not re-run) |
| Life after a revive above 3 hearts, held down through an age reload | not yet confirmed in game |
| Mate-down sound | not yet confirmed in game (off in the kit) |
| "Being revived counts as standing" (server hold) | not yet confirmed in game |

## Known gaps

- Hookshot: the gate is a code patch, which Hookshot does not apply; there the feature is off (normal game over) unless a Hookshot build adds the hook and sets `LICHII_ORM_HOOKSHOT_GATE=1`. The spectator's stick nudge does nothing on Hookshot (it says so once).
- The group defeat is per session, not per scene: groups in different places still fall together.
- Bottles and magic are not kept through a `keep` Continue (they are each player's; only wallet and ammo are).
- Whether the remake's Continue reloads the autosave and how the game's "save on game over" step interacts with it is not measured.
- The revive reach (1.5 units) is not measured in metres.
- The mate-down sound uses the ocarina's sound user because it is the only one the hook knows how to play by name; a better sound needs another sound user's call found.
- The spectator camera turns from the downed body; it cannot move to another scene with the mate (the body has to stay where it can be revived).
