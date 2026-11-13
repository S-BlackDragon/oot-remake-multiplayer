# 09-pvp: PvP rules, ally lock-on and chat commands

Since test10 the session's player-against-player switch and hit messages are upstream's (`PVP_MODE`, `PVP_HIT`, `PVP_HURT`, `lichii/hook/coop_pvp*`). This feature keeps our rules as a thin layer on top of them, and adds three things that are not PvP but were built with it: "Fijar aliados" (Z-targeting a mate's puppet), a registry of chat commands with Tab completion, and the calls, renames and on-screen statistics those commands drive.

## What changes for players

- **PvP stays off by default** and only the host turns it on or off (`pvp =` in the host's `coop-options.ini`, or `/pvp on|off` in the chat). That is upstream's rule; `/pvp` now goes to upstream's command.
- With PvP on, a hit does the **real weapon's damage**: half a heart for a Kokiri Sword swing, one heart for the long sword, spin and jump attacks, two for the Megaton Hammer, and our table for arrows, seeds, hookshot, boomerang and Din's Fire. Upstream's single amount per cause is used when the attacker's sword command is unknown.
- The victim's game then judges the hit:
  - a **shield raised toward the attacker** (70 degrees each way) blocks it; from beside or behind it does not;
  - **Nayru's Love** blocks from every side;
  - 0.8 s of **invulnerability** after a hit taken, and while our own hit reaction plays;
  - a downed Link takes nothing;
  - the **last one up** is never left at 0: one quarter heart is kept, so a friend's hit never ends the room's game (the server also counts a player who falls within 3 s of a PvP hit as up for the group defeat for 15 s);
  - a **shove**: the hit pushes the victim (the hookshot pulls), spread over 0.22 s; the game's own gravity can drop him off a ledge.
- **Fijar aliados** (`/fijar on`): Z locks on a mate's puppet as on an enemy. Room-wide; anyone may change it; everyone sees who did.
- **Chat commands**: a chat line that starts with `/` is a command, never sent as chat. Typing `/` shows the list above the chat box with a one-line help each; it narrows as you type; after a command it offers the arguments (on/off, player names, HUD places). Tab completes (again: the next one), arrow keys pick, Enter runs.
- **Calls**: `/traer <name>` asks one mate to come (he answers `/aceptar` within a minute); the host's `/reunir` calls everyone. Accepting goes there by upstream's `/teleport` rules (not in a cutscene, not in a boss fight).
- **Rename while playing**: `/nombre <new>` (1 to 16 letters, digits, `.`, `_`, `-`); the server refuses a name already in use; everyone sees the new name in tags, chat, lists and the HUD; it is saved in `coop.ini`.
- **Statistics**: `/fps on`, `/ping on`, `/red on`, `/stats on` keep a small line in the HUD's top right corner; remembered between sessions in `orm_stats.txt` beside `coop.ini`. `/stats` and `/ping` alone answer once in the chat.

The commands, their aliases and arguments (from `coop/host/src/orm_commands.cpp`; command words are Spanish, as players type them):

| Command | Aliases | Who | Scope | What it does |
|---|---|---|---|---|
| `/ayuda [command]` | `/help`, `/comandos`, `/?` | anyone | local | this list, or one command with its aliases |
| `/pvp [on\|off]` | | anyone asks; upstream lets only the host change it | room | upstream's PvP switch; alone: how it is |
| `/fijar [on\|off]` | `/fijaraliados`, `/lock` | anyone | room (notice) | Z-target mates as enemies; alone: how it is |
| `/choque [on\|off]` | `/cuerpos`, `/bodies` | anyone | room (notice) | real bodies between Links (feature 12-bodies) |
| `/jugadores` | `/players`, `/quien` | anyone | local | name, place, up or downed, ping |
| `/donde <name>` | `/where` | anyone | local | that player's place, and how far when it is yours |
| `/ir [name]` | `/teleport`, `/tp`, `/go` | anyone | local | upstream's `/teleport` with its rules; alone: the list |
| `/traer <name>` | `/bring`, `/llamar` | anyone | to that player | asks him to come |
| `/reunir` | `/gather` | host | to everyone | calls everyone to the host |
| `/aceptar` | `/accept` | anyone | local | go to whoever called you |
| `/anfitrion [name\|auto]` | `/host` | anyone | room | who hosts and why; a name or `auto` (feature 02-network) |
| `/ping [on\|off]` | | anyone | local | latency to the server and each mate; on/off: always on screen |
| `/fps [on\|off]` | | anyone | local | the game's fps on screen |
| `/red [on\|off]` | `/net` | anyone | local | up/down kbps and loss on screen |
| `/stats [on\|off]` | | anyone | local | all of the above plus puppets and the hook's ms; alone: a report |
| `/hud [on\|off\|nombres\|rupias\|panel\|tamano\|chat\|reset]` | | anyone | local | the panel (as F9), or one HUD setting (feature 10-hud) |
| `/rupias [on\|off\|place]` | `/rupees` | anyone | local | everyone's rupees line, and where (10-hud) |
| `/nombres [on\|off]` | `/names`, `/etiquetas` | anyone | local | names over the mates (10-hud) |
| `/mapa [on\|off]` | `/map` | anyone | local | mates on the maps (10-hud) |
| `/color <#RRGGBB\|name>` | `/colour` | anyone | the others see it | your colour now; saved in `coop.ini` |
| `/espectar <name>` | `/mirar`, `/spectate` | downed only | local | the spectator camera watches that mate (04-downed) |
| `/registros` | `/logs` | anyone | local | asks the app's helper to send your logs (14-app; without the app: says so) |
| `/version` | | anyone | local | app, kit and protocol versions |
| `/hora` | `/time` | anyone | local | the shared time of day (the last one a game sent) |
| `/reintentar` | `/retry` | anyone | local | retry the connection now, and the app's downloads |
| `/silencio [on\|off]` | `/mute` | anyone | local | hide the mates' chat on your screen |
| `/nombre <new>` | `/name`, `/nick` | anyone | room (notice) | rename now |

`on|off` also takes `true/false`, `sí/si/no`, `1/0`, `activar/desactivar`, `encender/apagar`. A player is named by his whole name (any case), the start of it when only one matches, or his number (`2`, `P2`). An unknown command answers `Comando desconocido, escribe /ayuda`.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `pvp` (kit key, host only) | `pvp = off` | upstream's | `on`, `off` | upstream's: the session's PvP at start (the server's `--pvp`) |
| `LICHII_ORM_PVP` | not in the kit | on (`orm_commands_game.cpp`) | 0/1, true/false, yes/no, on/off | our rules on upstream's hits (weapon damage, shield, Nayru, invulnerability, last one up, shove). Off: upstream's hits as they are |
| `LICHII_ORM_PVP_PUSH` | not in the kit | on (`orm_commands_game.cpp`) | as above | the shove after a hit |
| `LICHII_ORM_PVP_GUARD` | not in the kit | `ZL` (`orm_commands_game.cpp`) | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | the guard button read for a raised shield. Another value: ZL and a warning line |
| `LICHII_ORM_BLAST_FALLBACK` | not in the kit | on (`orm_commands_game.cpp`) | as above | a mate's bomb that went off with no copy in our game hurts our Link here (1 heart, push) when in 4 m; only while upstream's PvP is off (with it on, upstream's attacker already sends that hit) |
| `LICHII_ORM_COMMANDS` | not in the kit | on (`orm_commands_game.cpp`) | as above | the command registry. Off: the chat box passes `/` lines to the older handlers (`/teleport`, `/hud`...) as before |
| `LICHII_ORM_BODIES` | `LICHII_ORM_BODIES=1` | on | as above | read here too for `/choque`; described in 12-bodies |
| `LICHII_ORM_NAYRU_S` | not in the kit | the puppet pack's value | seconds | how long the Nayru crystal shows on a mate's puppet: feature 11-puppets, not a PvP rule |
| `LICHII_COOP_PVP_REPLAY` | not in the kit | upstream's | upstream's | upstream's "the game's own hit" switch; not ours (see Known gaps) |

Every `LICHII_ORM_*` on/off switch here is read by `lichii/hook/coop_switch.h` (feature 01-platform): a value it does not take keeps the default and logs `coop: switch NAME=VALUE is not ...`.

Server settings (`server/session.hpp`, not switches): `pvp_down_window_us` 3 s, `pvp_down_hold_us` 15 s, `hits_per_second` 8, `call_gap_us` 1 s (one call or rename a second per player).

## How it works

| File | What it does |
|---|---|
| `coop/host/include/coop/host/orm_pvp.hpp`, `coop/host/src/orm_pvp.cpp` | Portable rules, no emulator. `attack_amount` (the real weapon's damage from our Link's AS command or the shot table), `knock_of`, `UpstreamVictim` (shield, Nayru, invulnerability, repeat, downed, last one up), `guard_of` (a raised shield read three ways: a guard command in any AS slot, the guard button with a shield equipped in a pose that guards, or no shield), `Shove`, `blast_hurt`, `RoomSwitches` (the room's switches and their Spanish notices), `AllyTarget` (the lock-on puppet check and its fallback), `spare_last`. The older `Attacker`/`Victim` pair for our own `PLAYER_HIT` is still there, unused while upstream's PvP is the session's |
| `coop/host/include/coop/host/orm_commands.hpp`, `coop/host/src/orm_commands.cpp` | The command registry: names, aliases, argument kinds, who may run them, help lines, parsing, player names, colours, `coop.ini` edits, and the suggestions with the Tab / arrows / Enter cycle |
| `coop/host/include/coop/host/orm_stats.hpp`, `coop/host/src/orm_stats.cpp` | `/fps`, `/ping`, `/red`, `/stats`: the corner lines, the chat reports, the `orm_stats.txt` preferences (also `/hud on|off`, `/mapa`, `/silencio`) |
| `coop/protocol/src/orm_messages.cpp` | Byte layouts of `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME` |
| `server/orm_session.cpp` | Server rules: any player sets a room switch, the server stamps who and when, relays it to all and gives it to late joiners; hits only with PvP on, only to the victim, at most 8 a second per attacker; `/traer` to one player, `/reunir` from the host only; renames checked for clashes |
| `lichii/hook/orm_commands_game.h`, `.cpp` | The glue: runs commands from the chat box, reads our Link (AS slots, equipped shield, pad), feeds the shield reading to the victim judge, applies the shove with `MoveLink`, makes the puppets lock-on variants for "Fijar aliados", publishes the switches to the app (14-app), keeps the corner lines |
| `lichii/hook/orm_chat_complete.h`, `.cpp` | The Qt suggestion list over the chat box |
| `lichii/hook/orm_pvp_shared.h` | Header only: our shots in flight and the mates' bombs that went off with no copy here, handed from the spawn sync to the PvP frame |
| `tools/puppet_pack/build.py` | Builds `CoopPuppet_Child_Lock` and `CoopPuppet_Adult_Lock`: the puppet plus a lock-on `AttentionSenderParam` (the lock-on client only, so arrows are not auto-aimed at mates), taken from Dark Link's pack (`dumpcheck.py` now checks it is in the dump) |

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_pvp_game.cpp` | Three `// ORM` lines: after upstream's attacker makes a `PVP_HIT` for a sword swing and for a shot, `OrmCommands::PvpAttackAmount` replaces the amount with the real weapon's; after upstream's victim accepts a hit, `OrmCommands::PvpVictimTake` applies our rules and rewrites the `PVP_HURT` (amount, life left, `guarded` or `died` flag) so the attacker hears what was really taken |
| `lichii/hook/coop_hook.cpp`, `coop_window.cpp` | The chat box sends `/` lines to the registry; the PvP frame runs each frame |
| `lichii/hook/coop_spawn.cpp`, `coop_spawn.h` | Our shots and unseen bomb blasts onto the shared board |
| `lichii/hook/coop_camera_probe.cpp`, `coop_spectator.cpp` | `/espectar` |
| `coop/net/client.*` | The new name after a rename, byte counters for `/red` |
| `server/session.cpp`, `session.hpp` | The 15 s "fell to PvP counts as up" grace, armed by upstream's `PVP_HIT` too; the ORM handlers |
| `coop/protocol/codec.cpp`, `messages.hpp` | The four message types |

## Messages and components

| Message | Number | Channel | Used for |
|---|---|---|---|
| `PVP_MODE`, `PVP_HIT`, `PVP_HURT` (upstream's) | 241, 242, 243 | upstream's | the session's PvP; our rules change the amount in `PVP_HIT` and the result in `PVP_HURT` |
| `ROOM_SWITCH` | 0xF008 | 0 | room switches: key 2 "Fijar aliados", key 3 bodies (12-bodies). Key 1 (our old PvP) is never set since test10, so nobody is hit twice |
| `PLAYER_HIT` | 0xF00A | 0 | our older hit message; not sent while upstream's PvP is the session's |
| `PLAYER_CALL` | 0xF00B | 0 | `/traer` (kind 1) and `/reunir` (kind 2) |
| `PLAYER_RENAME` | 0xF00C | 0 | `/nombre`; result 0 ok, 1 in use, 2 not valid |

## Tests

| Test | What it checks |
|---|---|
| `tests/orm/pvp_test.cpp` | Switch notices (late joiner too), weapon amounts, the victim's verdicts (invulnerable, repeat, shield in front and behind, Nayru in another slot, too far, push only, last one up), the three shield paths with the real AS command names, the shove, bodies, the bomb with no copy |
| `tests/orm/commands_test.cpp` | Registry names and aliases unique, chat vs command, every on/off spelling, argument kinds, permissions (host, downed, offline), `/ayuda`, player names, colours, renames, `coop.ini` edits, suggestions and the Tab / arrows / Enter cycle |
| `tests/orm/stats_test.cpp` | The preferences file, corner lines, reports, fps and kbps |
| `tests/orm/pvp_codec_test.cpp` | Bytes, round trips and refusals of the four messages; nobody hits himself |
| `tests/orm/pvp_server_test.cpp` | Anyone sets a switch and all hear it, late joiners get it; hits only with PvP on and only to the victim; rate; `/traer` and `/reunir` rules; names in use; PvP falls do not end the room's game at once |
| `tests/orm/host_reader_test.cpp` | Every AS slot read from memory (the shield's command path) |

In-tree autotest scenarios (`tools/autotest/scenarios/`, run with upstream's `run.ps1`):

| Scenario | What it checks |
|---|---|
| `orm-pvp.steps.txt` | Plays `pvp-espada`, `pvp-fijar` and `pvp-choque` in a row (renamed from `pvp.steps.txt` at test10: upstream's own `pvp` scenario took the name) |
| `pvp-espada.A/B.txt` | A spins next to B with PvP off (nothing), turns PvP on and spins (B hurt and pushed), then B raises his shield (blocked) |
| `pvp-escudo.A/B/steps.txt` | B puts the Deku Shield on; A spins three times: B facing A with ZL (blocked), back turned (taken), shield off (taken); the log names the shield path |
| `pvp-fijar.A/B.txt` | `/fijar on`, ZL, `/fijar off`, ZL: only the notices and A's ZL; the lock itself is not checked |
| `pvp-choque.A/B.txt` | A walks into B and back; neither goes through the other (12-bodies) |
| `pvp-bomba.A/B/steps.txt` | A's bomb hurts both Links once each, in their own games; B logs no "no copy here" line |
| `pvp-cornisa.A/B/steps.txt` | A spin pushes B off a ledge. **Not runnable yet**: B's ledge position is still to fill in |

## Log lines

| Line | Says |
|---|---|
| `coop: commands: on (/ayuda); pvp on (push on, bodies on (the puppets drawn aside), bombs with no copy on); stats in orm_stats.txt` | the switches as read, once |
| `coop: commands: /<command> '<argument>'` | a command ran |
| `coop: pvp: our hit (<command>, cause N) takes N quarter hearts, not upstream's N` | the attacker's real weapon amount replaced upstream's |
| `coop: pvp: our rules on P1's hit (2 quarter hearts by upstream's): blocked, takes 0; shield up (command 'ShieldGuard' in slot 0), facing the hit (cos 0.97)` | the victim's verdict and the shield path (`command '...' in slot N`, `the guard button ...`, `no shield equipped`) |
| `... (no fresh shield reading: main command only)` | no shield reading this frame: the older main-slot rule |
| `coop: pvp: shield read from N AS slots of our Link, the equipped shield (<actor>) and the guard button 0x... (LICHII_ORM_PVP_GUARD)` | once a game |
| `coop: pvp: a mate's bomb went off N m from us with no copy here: N quarters` | the blast fallback |
| `coop: pvp: life N -> N` | life written |
| `coop: pvp: lock-on puppets in the puppet pack: yes (CoopPuppet_Child_Lock, CoopPuppet_Adult_Lock)` / `no (an older pack)` | the pack check |
| `coop: check: ally target ok (...)` / `coop: check: ally target fallback (...)` | "Fijar aliados" made a lock-on puppet, or fell back to the plain one for the session |
| `coop: commands: /nombre <name>: ...`, `coop: commands: coop.ini <key> = <value>` | rename and `/color` saved |
| `coop: commands: /registros: ...`, `coop: commands: /reintentar: connection ..., downloads ...` | the app-channel commands |
| Server: `player N set switch K on`, `player N fell to PvP: up for the group defeat for 15 s`, `player N renamed: A -> B` | server side |

## Status

| Part | Status |
|---|---|
| Damage, invulnerability and shove on upstream's PvP | confirmed in game on test10 (scenario `pvp-espada`, part of `orm-pvp`) |
| Shield | not yet confirmed in game: the `pvp-espada` run took a hit B meant to block (B wore no shield and our rule did not know the real guard command names). Fixed with the three-path shield reading; the `pvp-escudo` runs that followed did not pass |
| Chat commands | confirmed in game on test10 for the commands the scenarios type: `/pvp` and `/ir` (`pvp-espada`), `/choque on` (`choque-bloqueo`, `choque-encima`, `choque-continuar`); the other commands and the Tab list not checked in a run |
| Fijar aliados | not yet confirmed in game (Z on a lock-on puppet never seen) |
| Calls, rename, statistics | not yet confirmed in game |
| Bomb fallback, ledge shove | not yet confirmed in game |

## Known gaps

- Upstream's 0.8 s pause after a hit also runs after a hit our shield blocked.
- With upstream's `LICHII_COOP_PVP_REPLAY` (the game's own hit reaction) on, the game's knockback and our shove may add up; not looked at in a game.
- Reaches and damages are estimates, to tune by playing.
- Writing Link's life to 0 is expected to down him (04-downed); not seen with PvP.
- `pvp-cornisa` needs a ledge position before it can run.
- `/registros` and `/reintentar` need the app in the same process (14-app); without it they say so in the chat.
- `/hora` shows the last time a game sent, not our game's own clock.
