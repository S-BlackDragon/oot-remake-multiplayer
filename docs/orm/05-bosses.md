# 05-bosses: Bosses, enemies and hazards with six

Rules for fights with up to six players on top of upstream's shared boss room ([dungeons-boss.md](../dungeons-boss.md)) and object ownership ([objects.md](../objects.md)): boss families that act on "the player" (grabs, tennis, Twinrova's mirror), downed players at the boss door, enemies that turn to the players still standing, hits on another game's enemy that feel immediate, the endgame (barrier count, Ganondorf and Ganon rooms, the escape), the Gerudo Fortress and Spirit Temple, and hazards. The rules come from our earlier lockstep co-op for the N64 game, turned into "it acts on its owner's Link, and its owner does not change while it does".

## What changes for players

| Where | What happens |
|---|---|
| A Like Like, Wallmaster, Floormaster, ReDead, Gibdo, Dead Hand, Morpha's tentacle or Bongo Bongo's hand grabs your Link | That enemy stays in your game until it lets you go: neither a hitter nor a nearer player takes it. The others see you hanging on their copy of it. A mate's hit still frees you as in the original (Din's Fire on the tentacle, a sword on the Like Like). |
| Phantom Ganon, Ganondorf (tennis) | While a ball of light is in flight, the boss does not change owner mid-volley; only the owner's game returns the ball, so never a double return. |
| Twinrova | While Kotake's ice holds your Link (or your mirror shield holds half a charge), the sisters stay with your game so the ice thaws. |
| A downed Link at the boss door | Taken into the boss room with the group, still lying down (a load would stand him up, so he is moved as he lies). |
| A revive or a game over during a boss fight | Never taken as "boss beaten" (before, any event of a downed player in the fight was). |
| Game over inside a boss room | The room starts over after the reload. After Continue's reload, if you are up in the same dungeon but outside the room and the boss is not beaten, you are moved back in (every 3 s, 3 tries). |
| A mate goes down mid-fight | Within 1 s the enemies he owned go to the nearest player still standing. |
| An owner leaves the scene, unloads the enemy or disconnects | Its enemies go to the nearest player standing, not to one who is down, paused or loading. |
| Six around one Iron Knuckle, Stalfos, Lizalfos, Dinolfos or Wolfos | Treated as a mini-boss: it fights the player beside it (a player within 10 m and 6 m nearer than the owner takes it after 3 s, not more often than every 8 s); every hit counts once, it dies once. |
| You leave a cleared room while a mate stays in it, and come back | Its enemies stay dead for you (the room "stayed loaded"). If everyone leaves, they come back as in the game. |
| You hit an enemy another game owns | Your game's own knockback, flinch, flash and sound show at once; the copy is held there until the owner's answer arrives and blends back; no second flinch, a copy you killed does not keep walking, a slow answer never heals and re-hits it. |
| Ganon's Castle trials | Two trials finished at the same moment in two games both count; the tower's barrier falls at six. |
| Ganondorf and Ganon | Their rooms are boss rooms like the eight others (moving in, late entry, intros skipped when someone already finished them). |
| The collapsing castle | A game still in the escape follows a mate (up) who already reached the fight with Ganon. |
| Gerudo Fortress | Each game has its own guards and only the Link a guard catches goes to the cell; a carpenter count lost when two players free carpenters at once in two games mends itself after 3 s. |
| Spirit Temple | A cutscene a mate starts is followed only by players of his age. |
| Lava, quicksand, deep water, whirlpools, the void | Each player's heat, air and cold are his own game's (no change). If you go down where nobody can reach you, the others see your body at the last firm ground you stood on and can revive you there. |
| Haunted Wasteland | The guide Poe runs in each game (before, your Poe followed another player's path). |

## Switches

| Variable | Kit default (coop-options.ini) | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_BOSS_FAMILIES` | not in the kit | on (`coop_objects.cpp`) | on/off | Grab and tennis rules and drawing the held player on the part. |
| `LICHII_ORM_TWINROVA_KEEP` | commented: `# LICHII_ORM_TWINROVA_KEEP=0` | on (`coop_final_game.cpp`) | on/off | Twinrova stays with the frozen or charging player's game. |
| `LICHII_COOP_BOSS_PULL_DOWNED` | commented: `# LICHII_COOP_BOSS_PULL_DOWNED=0` | same as `LICHII_COOP_DOWNED` (`coop_boss_game.cpp`) | on/off | A downed Link is moved into the boss room as he lies. |
| `LICHII_COOP_BOSS_RESPAWN` | commented: `# LICHII_COOP_BOSS_RESPAWN=0` | same as `LICHII_COOP_DOWNED` | on/off | Back into the boss room after a game over in it. |
| `LICHII_COOP_HITS` | not in the kit | on (`coop_objects.cpp`) | on/off | The hitter's side of hits on another game's enemy. |
| `LICHII_COOP_HITS_DEDUPE` | not in the kit | off | on/off | On the owner: take back a life drop that looks like the same hit twice (a puppet weapon hitting there). Off until a run shows whether puppet weapons hit at all; the pairs are logged either way. |
| `LICHII_COOP_COPY_SMOOTH` | not in the kit | on | on/off | Smoothing of enemy copies (used by the hit feedback). |
| `LICHII_ORM_COUNTS` (old name `LICHII_COOP_COUNTS` still read) | `LICHII_ORM_COUNTS=shared_counts.txt` | unset: nothing counted (`coop_final_game.cpp`) | a list file | The counted Ints (barrier count) and their Bools. |
| `LICHII_ORM_ESCAPE_CLOCK` (old `LICHII_COOP_ESCAPE_CLOCK`) | commented: `# LICHII_ORM_ESCAPE_CLOCK=1` | off | on/off | One countdown for the group in the collapsing castle. |
| `LICHII_ORM_ESCAPE_CLOCK_WORD` | commented: `# LICHII_ORM_ESCAPE_CLOCK_WORD=main+0x...:f32` | unset | `main+0x<ptr>,0x<off>:f32` (or the word kinds `CountdownWord::parse` takes) | Where our game keeps its countdown. Unset: looked for in the running game. |
| `LICHII_ORM_ESCAPE_CLOCK_SEARCH` | not in the kit | on | off, or extra roots to search | The countdown search. |
| `LICHII_ORM_ESCAPE_CLOCK_LENGTH_S` | not in the kit | 180 | 1 to 3600 | The countdown's full length. |
| `LICHII_ORM_ESCAPE_CLOCK_TRIES` | not in the kit | 3 | 1 to 720 | Searches before giving up. |
| `LICHII_ORM_GROUP_EXIT` (old `LICHII_COOP_GROUP_EXIT`) | commented: `# LICHII_ORM_GROUP_EXIT=0` | on (`coop_teleport_game.cpp`) | on/off | The escape goes with the group. |
| `LICHII_ORM_GERUDO` (old `LICHII_COOP_GERUDO`) | not as a line (the comment says `LICHII_ORM_GERUDO=0` turns it off) | on (`coop_gerudo.cpp`) | on/off | Carpenter count mend and the Spirit Temple age rule for cutscenes. The guards and the guide Poe run in each game without a switch. |
| `LICHII_COOP_HAZARDS` | not in the kit | on unless exactly `0` (`coop_hook.cpp`, plain `getenv`) | `0` = off | Hazard flags kept each player's and the downed reach point (the reach point only with `LICHII_COOP_DOWNED=1`). |
| `LICHII_COOP_DEBUG_BOSS` | not in the kit | off (upstream's switch) | | Boss room debug lines; our scenarios use it. |

Server knobs (fields of `SessionConfig` in `server/session.hpp`, compiled defaults, no command-line option): `owner_downed_us` 1 s (0: upstream's 10 s), `fight_near_m` 10, `fight_near_margin_m` 6, `fight_near_hold_us` 3 s (0: off), `fight_near_cooldown_us` 8 s, `dead_room_m` 40 (0: off), `dead_left_m` 15, `owner_keep_max_us` 30 s (the longest a game may keep an object with `owner_keeps`; 0: off).

## How it works

| File | What it does |
|---|---|
| `coop/host/include/coop/host/boss_rules.hpp`, `src/boss_rules.cpp` | No game addresses, no sockets. `boss_pull` (how a Link may be moved into the room given his downed state), `BossRespawn` (remembers a game over that starts in a boss room; after the reload, asks to move Link back in), and the families: the actor table (`family_actors`), `held_animation` (Link's held AS commands: `Grab*`, `Eaten*`, `FaceGrab`, not `GrabMovableObj` nor `Cliff_Hold*`), `family_key`, `family_holds`, `FamilyWatch` (once per objects walk: what holds us, whether a volley runs, and the check lines). |
| `lichii/hook/coop_boss_game.cpp` (upstream file, our glue added) | Reads `CoopDowned::State()`, sets `pull_while_down`, ticks `BossRespawn` and carries out its move with what upstream already has (the scene change when an event runs and the room has a start point, else the matrix move). |
| `server/enemies.cpp` | Four server rules as `Session` functions: a downed owner gives up its enemies after `owner_downed_us`; a leaving owner hands them to the nearest standing player; in a fight the enemy goes to the player clearly nearer; a kill stays dead for a game that reloads the room while another player stayed in it. Non-boss enemies also respect `owner_keeps`. |
| `lichii/hook/coop_enemies.h/.cpp` | The mini-boss list (`Enemy_Stalfos`, `Enemy_Lizalfos`, `Enemy_Dinolfos`, `Enemy_IronKnuckle_*`, `Enemy_Wolfos*`; never `_Dictionary` or `_Haribote` actors); `CoopObjects::IsBoss` asks it, so each gets upstream's fight lock and boss hand-over rules. |
| `lichii/hook/coop_hits.h/.cpp` | Hit feedback on the hitter (the copy left to its own game for 300 ms, held until the owner's answer is on screen, blended back), the dedupe window measured from the samples' own latency, and the owner's ledger (`OwnerLedger`). The owner still applies every hit and decides life. |
| `coop/host/include/coop/host/final_rules.hpp`, `src/final_rules.cpp` | Counts (`apply_counts`: an Int never below the number of its Bools set), the escape clock (`EscapeClock`: starts at the first collapse event seen in the room; a game with more time than the group is set to the group's, never more, never 0), `ending_holds_age`, `hand_over_holds` (Twinrova; reads the family table). |
| `lichii/hook/coop_final.h/.cpp`, `coop_final_game.h/.cpp` | Glue: adds the counted flags to the shared set at session start, receives `STORY_EVENT`s, every 0.5 s applies counts, writes the countdown word, sets Twinrova's keep bits through `CoopObjects::SetKeep`. |
| `coop/host/include/coop/host/countdown_search.hpp`, `src/countdown_search.cpp`; `lichii/hook/coop_escape_timer.h/.cpp` | Looks for the escape countdown in a running game: words of the GameUI module (`main+0x4e7cfa8`) and the objects it points to that read as seconds left, followed once a second; the one that drops 1 s per second is reported in `LICHII_ORM_ESCAPE_CLOCK_WORD`'s format. |
| `coop/host/include/coop/host/group_exit.hpp`, `src/group_exit.cpp`; `lichii/hook/coop_escape.h/.cpp` | A game still in the escape scene follows a mate (up) who reached the next scene, through the `/teleport` trip; never while our Link is down or an event runs; the mate must be seen there for a settle time. (The header's "no glue in this tree" note is old: the glue is `coop_escape.cpp`, fed from `coop_teleport_game.cpp`.) |
| `coop/host/include/coop/host/gerudo.hpp`, `src/gerudo.cpp`; `lichii/hook/coop_gerudo.h/.cpp` | Guards with the security-guard brain run in each game (`runs_in_each_game`; guards that upstream's own guard sync shares stay shared), `CarpenterCounts` mend, `story_start_for_age`, a log line when all four carpenters are free and nobody has the card. |
| `coop/host/include/coop/host/hazards.hpp`, `src/hazards.cpp` | `hazard_flags` (20 Bools kept each player's: Lens, guide Poe, Wasteland respawn, void-out resets...), `unshare_hazard_flags`, `is_own_hazard_actor` (`Npc_GuidePoe*`), `DownedReach` (last firm ground; while down in lava/sand/water/whirlpool/void our samples carry that place). |

### Hook points in upstream files

| File | `// ORM` hook |
|---|---|
| `lichii/hook/coop_boss.cpp` | `Local::pull_while_down` (a downed Link is moved by matrix, never by scene change), `Sync::Beaten`, Ganondorf's and Ganon's rooms added to the room table. |
| `lichii/hook/coop_objects.cpp` | Switch reads (`LICHII_COOP_HITS`, `_HITS_DEDUPE`, `_COPY_SMOOTH`, `LICHII_ORM_BOSS_FAMILIES`), the light balls handed to the family watch, hold parts passed to the ride code. |
| `lichii/hook/coop_objects_sync.cpp` | `IsMiniBoss` in `IsBoss`, `FamilyWatch` each tick, `KeepBits` (sets `owner_keeps` in our samples), the hit feedback and owner ledger. |
| `lichii/hook/coop_hook.cpp` | `CoopFinal::AddShared/OnEvent/Frame`, `CoopEscapeTimer::Frame`, `CoopGerudo::Frame`, the hazards reach point and `unshare_hazard_flags`. |
| `lichii/hook/coop_teleport_game.cpp` | The escape (`CoopEscape::Escape`) on the `/teleport` trip. |
| `server/session.cpp`, `server/session.hpp` | `owner_keeps` / `owner_keeps_near` honoured for hits and nearness (at most `owner_keep_max_us`), the enemy rules' hooks (`unfit_owner`, `drop_presence`, `run_ownership`, `dead_for`, `set_scene`, `on_object_present`), counters `owner_kept`, `downed_handed`, `fight_near_moved`, `dead_kept_inside`. |

## Messages and components

No new message types. Uses:

| What | Where |
|---|---|
| `object_state` bit 7 `owner_keeps` (no hand-over to a hitter nor a nearer player) and bit 6 `owner_keeps_near` (no hand-over to a nearer player) | `OBJECT_SAMPLE` component 18, `orm_types.hpp` |
| `ride` (component 30 of `PLAYER_SAMPLE`): a held player's samples name the grabbing part, as a lift | `orm_types.hpp` |
| `STORY_EVENT` (`0xF003`): the escape clock's start, Spirit Temple age rule | see [07-cutscenes](07-cutscenes.md) |
| Upstream's `DAMAGE`, `OBJECT_REMOVE`, `HANDOVER_*`, `PLAYER_STATUS` bit 4 | unchanged |

## Tests

| File | What it checks |
|---|---|
| `tests/lichii/boss_rules_test.cpp` | `boss_pull` table; downed pulled lying down; no pull while being revived or in game over; fake-memory chains `read_vitals` to `Downed` to `boss_pull` to `Sync`, and group defeat to Continue to back in the room; retries; late entry; the family table, held names, keys, what is kept, the watch with a Like Like, fallback, another game's copy, tennis, objects sync with a Like Like and Phantom Ganon. |
| `tests/orm/lichii_boss_test.cpp` | Our boss room cases moved out of upstream's `boss_test.cpp`: Ganondorf's room (the tower's top floor) and Ganon's, a late player pulled into Ganondorf's room; a downed player's revive or game over event is never the boss's death; after a game over and its reload the room starts over; fake-memory fights with a revive and with a group defeat. |
| `tests/server/enemies_test.cpp` | Downed owner in 1 s, everyone down, leave/disconnect/unload with a downed player nearer, fight nearness and its negatives, boss excluded, mini-boss with two and six hitters, a kill across a hand-over once, reload with someone inside, `owner_keeps` on a grabbing enemy. |
| `tests/lichii/enemies_test.cpp` | Mini-boss names, `IsBoss`, two Stalfos = two fights. |
| `tests/lichii/hits_test.cpp` | Feedback phases, latency, death, dedupe window, the owner's ledger, two simulated games at 100 ms and 350 ms one way. |
| `tests/host/final_rules_test.cpp` | The kit's count list, two trials at once on fake memory, never lower, the clock (start, never more, never 0, server clock wrap), the ending's age, Twinrova keep and mirror charge, the glue's lines. |
| `tests/host/countdown_search_test.cpp` | Candidates, a word that drops 1 s per second, restarts, the word format. |
| `tests/host/group_exit_test.cpp`, `tests/lichii/escape_test.cpp` | Follow only an up mate, never while down or in an event, settle and retry; the trip. |
| `tests/host/gerudo_test.cpp` | Guard actors, carpenter mend, age rule, the card line. |
| `tests/host/hazards_test.cpp` | Flag hashes against names, nothing shared carries a hazard flag, guide Poe not shared, death and firm-ground commands, the reach point in lava, ground, void, water, slides, zone change, game over, off. |
| `tests/server/session_test.cpp` | `owner_keeps` stops a hit hand-over and frees after it, `owner_keeps_near` only stops the nearer rule, 30 s at most. |

Harness scenarios (in `tools/orm/harness`): `jefe-<boss>-entrada/caer/derrota/tarde` for Gohma, King Dodongo, Barinade, Phantom Ganon, Volvagia, Morpha and Bongo Bongo; `jefe-morpha-agarre`, `jefe-bongo-bongo-agarre`, `jefe-dead-hand-agarre`, `jefe-phantom-ganon-tenis`, `jefe-ganondorf-tenis`; `jefe-twinrova-*`; `final-*` (trials, barrier count, Ganondorf, escape, escape clock, sword, credits); `enemigo-caido`, `enemigo-sale`, `enemigo-reaparece`, `enemigo-minijefe`; `golpes-corte`, `golpes-muerte`.

## Log lines

- Boss room: `coop: boss rooms shared, a downed Link moved in as he lies, back into the boss room after a game over in it`
- Respawn: `coop: boss: respawn: game over in the boss room of ...`, `... Continue left us in the boss room: nothing to do`, `... back in the boss room of ...`, `... not in the boss room yet: moved again (try N)`
- Grab: `coop: grab: our Link is held by Enemy_LikeLike ... (Like Like), 0.5 m from it; ours: our samples of Like Like carry owner_keeps while it holds us ...`, `coop: grab: our Link is let go by ...`; on the others `coop: ride: P2 is held by ...: drawn on our copy of it`
- Tennis: `coop: tennis: Phantom Ganon's ball is in flight in our game: the volley is ours, ...`, `... volley is over: it may be handed over again`
- Once each, whether the readings work: `coop: check: grab ok|fallback ...`, `coop: check: tennis ok|fallback ...`, `coop: check: twinrova charge ok|fallback`
- Hits: `coop: objects: share hits feedback on, owner dedupe off (logged only), copy smoothing on`; every 5 s `coop: objects: share hits: 5.0 s: N of ours on copies ...`
- Server: `enemies: told player N a kill again: another player stayed in its room (...)`
- Endgame: `coop: count: on (LICHII_ORM_COUNTS shared_counts.txt): ...`, `coop: count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)`, `coop: escape clock: on (LICHII_ORM_ESCAPE_CLOCK): one countdown for the group in ...`, `coop: escape clock: our countdown found at main+0x4e7cfa8,0x1a0:f32 (...)`, `coop: escape: on (a game still in the collapsing castle follows the mate at Ganon)`, `coop: escape: going with the group: trip to P2`
- Gerudo: `coop: gerudo: on (LICHII_ORM_GERUDO): ...`, `coop: gerudo: in GerudoFortress_Adult: each game runs its own guards ...`, `coop: gerudo: carpenters: count mended: ...`
- Hazards: `coop: hazards: on (LICHII_COOP_HAZARDS): 20 per-player flags kept out of sharing ...`, `coop: hazards: down in lava ('LavaDead', ...): the others see us lying there ...`, `coop: hazards: down on the ground ('Dead'): our real place is sent (a mate can reach us)`

## Status

| Part | Status |
|---|---|
| Boss room entry with two (upstream's room plus our rules on) | confirmed in game on our test9-based build (scenario `jefe-gohma-entrada`, 9/9: A in the room with the intro, B moved in) |
| Downed pulled in, respawn after a game over in the room | not yet confirmed in game |
| Grab, tennis, Twinrova | not yet confirmed in game |
| Enemies with six (server rules, mini-bosses) | not yet confirmed in game |
| Hits on another game's enemy | not yet confirmed in game (`golpes-corte` ran red: B did not get to hit copies, no data) |
| Barrier count, Ganondorf/Ganon rooms, escape with the group | not yet confirmed in game |
| Escape countdown | not yet confirmed in game (`final-huida-reloj` red: the game refused the collapse event on the probe save, so no countdown ran) |
| Gerudo / Spirit Temple | not yet confirmed in game |
| Hazards | not yet confirmed in game |

## Known gaps

- Upstream's test10 turned off "the boss goes to the nearest" (`boss_near_hold_us = 0`), so our `owner_keeps_near` bit no longer changes anything for bosses; it is kept for when that rule comes back.
- Only the owner's Link can be grabbed (a boss's AI knows its own game's Link): a non-owner is never grabbed while he is not the owner. Accepted difference.
- Whether the others see the tennis ball (it is not a shared object) is not known.
- Volvagia's breath and Bongo Bongo's drum still act on the owner only; each needs a two-player scenario.
- The escape countdown's word is not known; without it the group's clock cannot set our game's timer.
- Twinrova's mirror charge amount is not read.
- Whether puppet weapons hit enemies in the owner's game (the reason for the dedupe switch) is not known.
- Group defeat inside a boss room: where the game's own Continue leaves the group was not measured.
- Hazards: whether the game's fairy branch really brings a body back from the void or the bottom of the lake on revive is not measured.
