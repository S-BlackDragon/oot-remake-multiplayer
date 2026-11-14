**English** | [Español](07-cutscenes.es.md)

# 07-cutscenes: Story cutscenes for everyone

A listed story cutscene that one player's game starts by itself (meeting the Great Deku Tree, Saria on the bridge, Kaepora Gaebora...) plays on every game in the same scene at the same moment, and a game that has played it does not play it again. The cutscene's end state (the flags it sets and the story steps it moves) is shared too, so a player who was elsewhere ends with the same world. Only the cutscene's owner can skip it for everyone. This goes on top of upstream's boss-room sync, which keeps the boss intros and deaths.

## What changes for players

- Walk into a story trigger and your mates in the same scene see the same cutscene start on their screens within about a second (each with his own Link). A mate in another scene, loading, down, or already in an event of his own does not get it pulled in; he gets the end state as flags.
- A player who joins later, or comes back to the scene, finds the world as the cutscene left it (the Deku Tree's mouth open, the step moved on) and the game's own trigger does not fire again.
- After a blue warp the cutscene is only for who took the warp (`solo` in the list, as in our earlier N64 co-op); the others get its result.
- A cutscene of one room of a scene (the trials of Ganon's Castle) is only for the players near where it started (`near=M`).
- Skipping: only the owner (the first game that started it) skips for everyone. When the owner presses the skip button (PLUS or MINUS, the remake's skip prompt and its confirm) while his cutscene runs, at most 5 s before it ends, its end goes out as `skipped`, and every game still playing its copy force-skips the rest (one force skip every 0.5 s, for up to 8 s). A mate who skips his own copy skips only his. A plain end never cuts anyone: a slow copy plays to its end.
- In the Temple of Time the rules of [08-age](08-age.md) decide who takes a temple cutscene.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_STORY_EVENTS` | `LICHII_ORM_STORY_EVENTS=1` | off (`orm_story_events_game.cpp`: unset is off) | 0/1/true/false/yes/no/on/off; an unknown value logs a line and counts as unset | Story cutscenes for everyone |
| `LICHII_COOP_STORY` (old name) | not set by our lines | — | `1`/on turns our cutscenes on when `LICHII_ORM_STORY_EVENTS` is unset | Kits before test9 used it. Note: upstream reads the same name for its story flag sharing (on unless 0), so only an explicit on value turns ours on |
| `LICHII_ORM_STORY_LIST` (old `LICHII_COOP_STORY_LIST`) | `# LICHII_ORM_STORY_LIST=shared_story.txt` (commented out) | the built-in list | a file path | The list from a file; a file that does not load turns the feature off with an error line. The kit's `shared_story.txt` is the built-in list (a test checks it) |

## How it works

| File | What it does |
|---|---|
| `coop/host/include/coop/host/story.hpp`, `coop/host/src/story.cpp` | The portable side: the list (`StoryList`) and the rule (`Story::tick`). Owner: when our game's event starts by itself and its first readable name is listed, we own it and send `STORY_EVENT start`; at its end, `end` (or `skipped`). The others: a start for our scene, with no event of our own, not down, not loading and not played here, makes us ask our game for the same event by name every 0.5 s for up to 3 s. A start for another scene is ignored. Skip: the press window, the decision `end`/`skipped`, and the force skip of a copy |
| `lichii/hook/orm_story_events.h`, `orm_story_events.cpp` | The memory side: the request by name (a malloc once, then `main+0x4aa890(Link's event component, &box)`), and `Skipper`, the game's force skip (`main+0x3b3af30` on the current event, the call upstream's boss rooms use) through the svc cave |
| `lichii/hook/orm_story_events_game.cpp` | The glue: reads the switches and the list, adds each listed event's `HavePlayedEvent.<event>` and end flags to the shared flags, reads the running event, the pad buttons (skip press), feeds `Story::tick`, sends and receives `STORY_EVENT` |
| `server/session.cpp`, `server/session.hpp` | The relay keeps the first start of each (scene, name) with its owner, relays it, takes `end`/`skipped` only from the owner and only once, and sends a joiner every (scene, name) the room has started as `played`, then the owner's last word. A host's save load clears them (the save's cutscenes play again for everyone) |
| `tools/release/kit/shared_story.txt` | The list: one event a line by the game's name, then its end flags: the `HavePlayedEvent.Dm_*` of the timelines it plays, the Bools it sets, `step:Step_Name=V1,V2,...` for the story steps (Enum flags, values in the game's order; a step only moves forward), `solo`, `near=M`. Read from the romfs for the whole game, child and adult |

Not in the list on purpose: boss intros and deaths (upstream's boss-room sync plays them), dungeon mechanism cutscenes, minigames, the horse, Gerudo guards taking one Link to the cell, chests and item gets.

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_hook.cpp` | `OrmStoryEvents::AddShared` at session start; `OrmStoryEvents::OnEvent` on the network thread; `OrmStoryEvents::Frame` each frame; `OrmStoryEvents::HostReset` when the host loads a save |
| `lichii/hook/coop_testinput.cpp` | The test commands `event <name>` (names checked with `OrmStoryEvents::ValidName`) and `skip` (`OrmStoryEvents::NoteSkip("test skip")`, as a press would be) |
| `server/session.cpp` | `on_story_event`, `send_story` (above) |
| `coop/protocol/include/coop/protocol/messages.hpp`, `coop/protocol/src/codec.cpp` | The `StoryEvent` body and its codec |

## Messages and components

`STORY_EVENT`, type `0xF003` (`orm_types.hpp`; it was 63 in protocol 108 and 243 in 109), channel 0. Body: player number (0 from a client), kind, scene (1 to 64 characters), event name (1 to 127 of A-Z a-z 0-9 _), the owner's Link position, server time in ms.

| Kind | Value | Meaning |
|---|---|---|
| `start` | 1 | The owner's game started it |
| `end` | 2 | The owner's copy is over, not skipped |
| `played` | 3 | Server to a joiner only: the room has started it |
| `skipped` | 4 | An end whose owner skipped it |

The end state travels as ordinary shared flags (`FLAG_SET` / snapshot), not in this message.

## Tests

| File | What it checks |
|---|---|
| `tests/host/story_test.cpp` (43 cases) | The built-in list (the whole game, with flags; no per-player flag ever shared; blue warps are `solo`; every flag hash and step value is the game's; the kit file is the built-in list; `solo` and `near=M` parsing; the trials' barriers are `near`); owner and followers; latecomers; skip: a press just before the end sends `skipped`, a natural end or an old press sends `end`, the `skip` command, a `skipped` cuts the followed copy, an `end` cuts nobody even a minute behind, a pending request is dropped, a latecomer with `skipped`, down, the 8 s limit, a save load |
| `tests/orm/story_events_test.cpp` (7 cases) | The request by name in fake memory, no call without Link or for a bad name, the switch with the old name, the force skip call |
| `tests/lichii/story_test.cpp` | Upstream's story test, updated for the hook changes |
| `tests/host/time_temple_test.cpp` | A temple `skipped` of the sword's chain becomes a plain end |
| `tests/server/session_test.cpp`, `tests/protocol/codec_test.cpp`, `tests/lichii/testinput_test.cpp` | Relay rules, codec round trip of every kind, the `skip` test command |

Harness scenarios (they live in `tools/orm/harness`): `historia` (A asks its game for the Deku Tree meeting by name; B in the same scene must start the same event within about a second, both Links at the cutscene's spot), many `historia-*` pairs, one per listed cutscene, most with a `-rezagado` variant for the latecomer, `historia-saltar` (A skips with the `skip` command; B, stopped in a text, must skip his) and `historia-largo` (a natural end cuts nobody).

## Log lines

All start with `coop: story:`.

| Line | Says |
|---|---|
| `coop: story: on (LICHII_ORM_STORY_EVENTS): N shared story events (built in): ...` | On, and which list |
| `coop: story: LICHII_ORM_STORY_LIST <file>: <why>; story events are not shared` | The file did not load: off |
| `owner: our game started '<event>' in <scene> ...` | We own it |
| `coop: story: STORY_EVENT start from P1 ('<event>' in <scene>, ...)` and `... : starting` | A mate's start, and we ask ours |
| `'<event>' is solo: only we see it, its flags are shared` | A `solo` line |
| `'<event>' was played in this room already ...; not shared again` | A repeat |
| `skip pressed (PLUS) during our '<event>': ...` | The owner's press |
| `'<event>' is over here, skipped by us (PLUS, 800 ms before its end); STORY_EVENT skipped to the others` / `is over here, not skipped (...)` | Which end the owner sent |
| `P1 skipped '<event>' in <scene>; ours is skipped too`, `force skip of our '<shot>' (...)`, `'<event>' skipped here after P1's: over after N force skips` | A follower's skip |
| `P1's '<event>' in <scene> is over there (not skipped)` | A plain end |
| `'<event>' not skipped: we are down` | No skip while down |
| `", ignored: LICHII_ORM_STORY_EVENTS is off"` at the end of a received line | Off here |

The relay logs `story: player N also started '<event>' ...` for a second starter.

## Status

- Shared cutscene start and end: **confirmed in game on our test9-based build (scenario `historia`)**: the Deku Tree meeting played on both games, and it ended well on one and on two games.
- The other `historia-*` pairs and the latecomer variants: not counted here as confirmed.
- Owner's skip: **not yet confirmed in game** (`historia-saltar`, `historia-largo` not run).

## Known gaps

- Which button the remake's skip really is: PLUS and MINUS are a guess (`StorySettings::skip_buttons` is one constant).
- Whether one force skip cuts one shot or the whole timeline, whether it cuts a copy stopped in a text box, and that the end flags come out the same on both games after a skip.
- Text inside a cutscene: each player advances his own, so the copies drift apart by seconds.
- The game hides the puppets during its own story cutscene (its `TimelineEvent` setting pauses and hides every actor that does not take part); that is the game's rule, not changed here.
