# 13-resume: Back to your own last place

A player who closes the game and comes back to the same room returns to his own last scene and place, not to where the loaded save puts everyone. A place that was in a boss fight or a cutscene sends him to that scene's entrance instead, and inside a dungeon he goes back to the entrance room (the game can only load a dungeon at its entrance). This is "resume session" from our earlier lockstep co-op for the N64 game.

## What changes for players

| Last place | When you come back |
|---|---|
| Free play somewhere, same scene as the load | Link is moved back to where he last stood on his feet ("De vuelta donde lo dejaste"). |
| Free play in another scene | A trip there (the game's own scene change to the nearest start point), then Link is moved to the place. |
| In a boss room or during your boss fight, or while a cutscene or a talk ran | The entrance of that scene; a boss fight that still runs pulls you in as it does a late player. |
| Inside a dungeon, in a room other than the entrance room | Back in the dungeon, in the entrance room, on the spot you last stood on before leaving it (the way toward the rest of the dungeon): "De vuelta a la sala por donde saliste: tu sitio está en otra sala". With no such spot kept (another PC, an old file): the dungeon's entrance. |
| Kept at the other age, or a scene of the other age | You stay where the load put you. |
| Nothing kept | Nothing happens. |

The decision is taken once, after the load, when Link has stood 3 s with no event, no load and no scene change asked by anyone. Nothing is written over the kept place before that decision and its trip are done, so a crash on the way keeps the old place.

Separately, when a helper tells the game that the room's save was stored while playing, "Guardado" shows for 2 s at the bottom right of the in-image HUD (see [14-app](14-app.md) and [10-hud](10-hud.md)).

## Switches

| Variable | Kit default (coop-options.ini) | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_RESUME` | not in the kit | on (`coop_resume_game.cpp`) | on/off | Off: no place kept, no way back. |
| `LICHII_ORM_RESUME_PLACE` | not in the kit | unset: this PC's `coop-resume.txt` | one place line (taken as ours whatever its room and name), several lines split by `\|` (one per player name, for a harness pair), or `none` | The place to go back to, given from outside (a helper that keeps places for several PCs). A value with no good line of this player is ignored, with a log line. |
| `LICHII_ORM_ROOM` | not in the kit | unset: the server address from `coop.ini` (`server:port/path`) | text | The room name the place is kept under. |
| `LICHII_COOP_TELEPORT` | not in the kit | on (upstream's switch) | `0` = off | Resume travels with the `/teleport` trip; with it off, only a move within the same scene is possible. |

## How it works

### The file

`coop-resume.txt` beside `coop.ini` holds two comment lines and:

- the place line: `v1;room;name;scene;x;y;z;age;kind;unix_s` (age `child`, `adult` or `-`; kind `ok`, `boss` or `event`);
- in a dungeon, the entrance room's spot: `d1;room;name;scene;start;x;y;z;age;unix_s` (a helper that only passes `v1` lines leaves it on this PC).

Fields are cleaned (`;`, `#` and control characters become `_`, at most 64 bytes). The place is written again when the scene or kind changes (at most once a second) or after a move of more than 1 unit (at most every 5 s).

### Inside a dungeon (upstream issue 197)

A dungeon is one scene loaded one room at a time, and the game's scene change can only land at a start point; every dungeon has its usable start points in the entrance room only (the others start cutscenes: boss, sage, stone). Writing Link's place into a room that is not loaded left him stuck on a black picture, so upstream refuses a move out of the room Link is in (`CheckMove`, with the puppet pack's `coop_rooms.txt`, or 10 m without it). So while playing the game keeps the last spot of the entrance room Link stood on, and on return travels to the entrance and moves Link to that spot. The move is checked again against where the load really put him.

### Files

| File | What it does |
|---|---|
| `lichii/hook/coop_resume.h/.cpp` | The rules, no emulator code: `Place` and `Door`, `Format`/`Parse`/`FileText`/`FromFile`/`FromGiven`, `Recorder` (what to keep and when), `DoorAt` (the entrance room's spot), `Returner` (the one decision after the load: none, move, or travel, with the log line and the HUD notice). |
| `lichii/hook/coop_resume_game.h/.cpp` | The hook side: reads the switches and `coop.ini`, reads and writes `coop-resume.txt` (through a temporary file), the room table from the puppet pack, once a frame `CoopResume::Frame`. |

### Hook points in upstream files

| File | `// ORM` hook |
|---|---|
| `lichii/hook/coop_teleport_game.cpp` | Calls `CoopResume::Frame` each frame; a `move` becomes a trip action, a `travel` starts `Trip::Resume`. |
| `lichii/hook/coop_teleport.h/.cpp` | `Trip::Resume`: the scene change, then our own place or the entrance room's spot; its lines go to the log, not the chat; a lost connection does not stop it, a player's own `/teleport` does. |

## Messages and components

None. The place stays on the PC (or is handed in by `LICHII_ORM_RESUME_PLACE`).

## Tests

| File | What it checks |
|---|---|
| `tests/lichii/resume_test.cpp` | A place line and back; bad lines; cleaning; the line of this room and name; one given line or a pair; the recorder's rate; boss, fight and event kinds; same scene moves, another scene travels (to the place or the entrance); never across ages nor to a scene with no start point; the decision waits for Link to stand still, once; giving up; the trip (scene change then place, entrance only, ended by `/teleport`, giving up goes to the log). Dungeon: the spot is only in the entrance room, the `d1` line round trip, travel to the spot or the entrance, the same dungeon, the trip. |

Harness scenarios (in `tools/orm/harness`): `reanudar-mazmorra-1` (B walks to a spot of the Deku Tree's entrance room, is then placed by a test command on the slingshot ledge in another room, and keeps place and spot) followed by `reanudar-mazmorra-2` with the same instances (B comes back: travel to the Deku Tree's entrance, then the entrance room's spot, never moved to the ledge); `reanudar-sitio` (A's place in Gohma's room goes to the Deku Tree's entrance; B goes back to his place). The harness clears `coop-resume.txt` between runs unless `ORM_KEEP_RESUME=1`.

## Log lines

- `coop: resume: off (LICHII_ORM_RESUME=0): no last place kept, no way back`
- `coop: resume: on: the last place of 'B' in room <room>: InsideTheDekuTree at x y z (ok, this PC's coop-resume.txt)` (or `given by the app`), or `coop: resume: on: no last place of 'B' in room <room> (...)`
- `coop: resume: the spot of InsideTheDekuTree's entrance room last stood on: x y z (start point Entrance, this PC's coop-resume.txt)`
- `coop: resume: dungeon rooms: N placements in the puppet pack's table (...)` or `no room table in the puppet pack (moves within 10 m of the entrance only)`
- The decision: `coop: resume: going back to the last place, ... (start point ...)`, `coop: resume: back to the last place, ... (same scene)`, `coop: resume: already at the last place (...)`, `coop: resume: the last place (...) is not a place to go back to (...)`, `coop: resume: the last place (...) is in another room of this dungeon (issue 197 ...)`, `... was kept with the other age: staying here`, `coop: resume: no last place kept for this player in this room`, `coop: resume: our Link never stood still after the load: no way back to the last place`
- The trip: `coop: resume: scene change to InsideTheDekuTree at Entrance`, `coop: resume: at the spot of the entrance room of InsideTheDekuTree we last stood on before leaving it`, `coop: resume: not moved to ... from where the load put us (...): staying here`
- After: `coop: resume: keeping our last place in <file>`, `coop: resume: kept ...`
- HUD: `coop: hud: "Guardado" for 2000 ms (the app sent the room's save)`

## Status

| Part | Status |
|---|---|
| Inside a dungeon: back to the entrance room's spot | confirmed in game on test10 (scenarios `reanudar-mazmorra-1` then `reanudar-mazmorra-2`, two games, 10/10) |
| Outside dungeons: back to the exact place, another scene's trip | not yet confirmed in game (`reanudar-sitio` not run) |
| Boss room or cutscene place: the entrance | not yet confirmed in game |
| "Guardado" on the HUD | not yet confirmed in game |

## Known gaps

- A place in a dungeon room other than the entrance's is never reached exactly: the game has no request we know for its own room change (a door).
- The place is kept per room name and player name; renaming a player or changing the server address (without `LICHII_ORM_ROOM`) starts with no place.
- Not checked in a game: that the resume trip does not clash with the warp song invitation or with the boss room rules.
- Across PCs the place travels only if something hands it in through `LICHII_ORM_RESUME_PLACE`; the kit alone uses the file of this PC.
