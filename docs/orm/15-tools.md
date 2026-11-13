# 15-tools: Test tools, perf meter and kit scripts

What we added to make the other features testable and shippable: a meter of the hook's own time, more test-script commands for `coop_testinput`, a flag probe, PvP scenarios for upstream's autotest, the kit's options file with our switches, Spanish hosting checks, and puppet pack additions. Our own two-game harness and the synthetic save tool are not part of the code tree: they come as `tools/orm/harness` and `tools/orm/saves` (see the end).

## What changes for players

Nothing in the game. In the kit:

- `coop-options.ini` lists every ORM switch with its shipped value and a plain explanation (sections "On by default", "Off by default", debug logging).
- The host gets Spanish checks before the server starts (ports free, held by this kit's server, held by another program, or reserved by Windows for Hyper-V, WSL or Docker), the networks it sees (Hamachi, Radmin VPN, Tailscale, ZeroTier), what Windows' firewall will do (the way without commands first, then narrow `netsh` lines for an administrator prompt: only `coop_server.exe`, only its ports, only from the network in use), and the server's important log lines explained. `comprobar-alojamiento.cmd` is the same check on its own, beside upstream's English `check-hosting.cmd`. All of it only reads: no port is opened, no rule or router setting changed.
- The colour the launcher writes into `coop.ini` (one per player) is kept and passed as `LICHII_COOP_COLOR`, so your own tunic is the colour the others see.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_PERF` | not in the kit | off (`coop_hook.cpp`, upstream's `EnvFlag` reading) | unset, empty or `0`: off; **any other value is on** (also `off` or `false`) | a log line every 5 s of where the hook's own time goes |
| `LICHII_ORM_FLAG_PROBE` | not in the kit | off (`coop_hook.cpp`, `coop_switch.h`) | 0/1, true/false, yes/no, on/off | log each change of the probed flags with where it came from, and name a flag that bounces between games |
| `LICHII_ORM_FLAG_PROBE_NAMES` | not in the kit | none | comma-separated flag names | more flags for the probe |
| `LICHII_COOP_TEST_SCRIPT` | not in the kit | none | a script file | upstream's: play a test script (our commands below are added to it) |
| `LICHII_COOP_TEST_COMMANDS` | not in the kit | none | a file | upstream's, Hookshot only: test-only steps written by its autotest driver |
| `LICHII_COOP_DEBUG_LIFE` | commented in the kit | off | `1` | our Link's life, max, Death component word, event and AS command logged on each change (`coop: life:` lines, at most ten a second); also starts the test-input reader |
| `LICHII_ORM_FRAME_LOG` | not in the kit | | | the frame-time log: feature 01-platform |
| `server_websocket` (kit key, host) | commented (`# server_websocket = no`) | on | `yes`/`no` (also on/off, true/false, 1/0) | the WebSocket listener on TCP, on the UDP port + 1; `no` leaves UDP only (feature 02-network) |

## How it works

### Perf meter

| File | What it does |
|---|---|
| `lichii/hook/coop_perf.h`, `.cpp` | `CoopPerf::Meter`: per-part nanosecond totals over 5 s windows. "calc" is what runs inside the game's calc thread (the svc cave: the game waits for it), split into puppet frame, pose copy, heads, belt and calls, plus each svc caller by its order; "present" is what runs on the emulator's compositor thread each presented frame, split into objects, input, read, flags, session, publish, share, features and place; guest reads and writes counted in bytes. Off costs one relaxed load per scope |
| `lichii/hook/coop_hook.cpp`, `coop_callers.cpp`, `coop_puppet.cpp` | `// ORM` scopes around those parts; the line is written from `OnFrame` |

What the game itself spends drawing and animating the puppets is not in these numbers: it is the frame time left over.

### Test-script commands added to `coop_testinput`

All are test only, written in a scenario's `.A.txt` / `.B.txt` script (`lichii/hook/coop_testinput.h` documents each):

| Command | What it does |
|---|---|
| `warp <scene> <start> adult\|child` | a last word asks that age for a scene whose name has no era (a dungeon). A warp asked in free play starts the `/teleport` scene event after 2 s (again every 6 s, at most 3 times), since the game takes a scene change only when an event ends |
| `life N` | writes N quarter hearts into Link's current life (0 kills him); refused with a log line when the path cannot be read |
| `gate 0\|1` | opens or closes the downed gate's word (feature 04-downed; patched in only with `LICHII_COOP_DOWNED=1`) |
| `bottle A <value>` / `bottle A restore` | puts a BottleItem value (Empty, Fairy, RedPotion...) straight into bottle A to E, owning it if needed; `restore` puts back what the first write found |
| `event <name>` | asks the game to start that event, as the game over asks its own (from the svc cave; needs `LICHII_COOP_PUPPET=1`) |
| `skip` | force skips the event running now with the game's own force skip; the story layer counts it as the owner's skip (07-cutscenes) |
| `flag <name\|0xhash> 0\|1` | queues a Bool flag through the game's own queue, so the save keeps it |
| `int <name\|0xhash> <value>` | queues an Int flag (-1000000 to 1000000) |
| `key 0x<key> 0\|1` | queues a 64-bit-key flag (a placement's saved state: chests, red ice, cracked walls, room counters) |
| `enum <name\|0xhash> [value]` | logs an Enum flag as the game holds it (type, hash, name, the game's value list, the entry's bytes); with a value, queues it |
| `age adult\|child` | this game reloads its scene at that age, the way a game following a mate's age change does (08-age) |
| `await mate [ticks]` | waits until a mate stands in our scene (default 9000 ticks, 150 s) |
| `talkthrough [ticks]` | presses A while our own event runs (a talk's text) until it is over (default 1800 ticks, 30 s) |

### Flag probe

`coop/host/include/coop/host/flag_probe.hpp`, `coop/host/src/flag_probe.cpp`: flags whose meaning only a run shows. Defaults: the Water Temple's room 14 switch flags (`WaterTemple_Room14_SwitchPushed`, `..._SwitchPushedByBlock`, `..._SwitchPushedHeavyBoots`) and Jabu-Jabu's room 17 stone (`InsidejaubjabuBelly_gimic_switchOn_Stone`). A flag that goes back and forth between games is named once as a candidate for a "held" line in the dungeon lists (06-world). Hooked in `coop_hook.cpp`.

### Upstream autotest scenarios

Our PvP scenarios in `tools/autotest/scenarios/` (`orm-pvp.steps.txt`, `pvp-espada`, `pvp-escudo`, `pvp-fijar`, `pvp-choque`, `pvp-bomba`, `pvp-cornisa`) run with upstream's `tools/autotest/run.ps1`; each is described in 09-pvp.

### Kit options and hosting scripts

| File | What it does |
|---|---|
| `tools/release/kit/coop-options.ini` | Every ORM switch with its shipped value and explanation; the host keys `server_websocket` added |
| `tools/release/kit/coop.ini.template` | A `color:` line (a colour name or `#RRGGBB`, optional) |
| `tools/release/kit/shared_*.txt` | The content lists (06-world, 07-cutscenes, 08-age); `make_kit.ps1` copies every `shared_*.txt` by pattern, so a new list needs no line there |
| `tools/release/kit/scripts/kit-paths.ps1` | Reads `LICHII_ORM_*` lines too (and `EDEN_NX2_*`), skips the host keys; a value that names kit files, a comma list included, becomes full paths (from `kit\emulator`, `..\name`, when the kit folder's path has a comma) |
| `tools/release/kit/scripts/play.ps1` | Keeps the colour in `coop.ini` and passes it as `LICHII_COOP_COLOR`; checks the server's ports before its window opens |
| `tools/release/kit/scripts/start-server.ps1` | The Spanish banner and join addresses (WebSocket included), port and firewall checks before start, `--ws-port` from `Get-OrmWsArgs`, the server's lines explained in the window |
| `tools/release/kit/scripts/coop-ini.ps1` | Reads and writes the `color` key |
| `tools/release/kit/scripts/orm-hosting.ps1` | The Spanish checks above, as functions that take their facts as arguments (so they test without a network). UTF-8 with a BOM: Windows PowerShell 5.1 reads a file without one as ANSI |
| `tools/release/kit/scripts/orm-check-hosting.ps1`, `tools/release/kit/comprobar-alojamiento.cmd` | The host's self-check in Spanish: before Host.bat, are the ports free; with the server open, is it listening (UDP, and TCP for WebSocket), which addresses to give, does a firewall rule let it in, who may join |
| `tools/release/make_kit.ps1` | Copies the `shared_*.txt` lists by pattern and the two `orm-*.ps1` scripts |

### Other tool changes

| File | Change |
|---|---|
| `tools/puppet_pack/build.py` | New actors in the pack, each when its source is in the player's own romfs: shot stand-ins (`CoopShot_*`: fishing line, Din's Fire, the hammer's shock wave, fire / ice / light arrows), status stand-ins on puppets (`CoopFx_Burn`, `_Freeze`, `_Shock`, `_Nayru`), chest finds held over the finder (`CoopGet_*`), riders' horses (`CoopPuppet_Horse`, `_Ranch`, `_Ingo`), lock-on puppets (`CoopPuppet_*_Lock`, 09-pvp) and puppet bodies (`CoopPuppet_*_Body`, `_BodySide`, 12-bodies). `--no-effects` leaves the effects out |
| `tools/puppet_pack/dumpcheck.py` | Checks the dump holds the sources of those actors (Dark Link's attention params for the lock-on puppets among them) |
| `tools/puppet_pack/tests/test_puppet_pack.py` | Tests for each of them |
| `tools/hud_font/make_font.py` | Generates `lichii/hook/coop_hud_font.h` (10-hud) |
| `tools/game_scan/make_patterns.py` | `ORM_UNSCANNED`: our build-only offsets (carried object's stillness, the downed gate and its caves) get no pattern |
| `tools/coop_test/probe.cpp` | Knows the `player_look` event |

## Messages and components

None.

## Tests

| Test | What it checks |
|---|---|
| `tests/lichii/perf_test.cpp` | The meter: windows, parts, callers, guest bytes, the line's text, off costs nothing |
| `tests/lichii/testinput_test.cpp`, `tests/orm/lichii_testinput_test.cpp` | Parsing of every added command and its errors, the warp event rule, `talkthrough` and `await mate` timing |
| `tests/host/flag_probe_test.cpp` | Probed changes, their source, the bouncing rule |
| `tools/release/tests/orm_hosting_test.ps1` | The WebSocket switch, network kinds (ZeroTier), the port check, firewall lines, the server's lines in Spanish, the self-check; every input given as data. Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\tests\orm_hosting_test.ps1` |
| `tools/release/tests/launch_test.ps1` | `LICHII_ORM_*` lines reach the emulator; kit files in a switch value become full paths, comma lists and a comma in the kit path included |
| `tools/puppet_pack/tests/test_puppet_pack.py` | The new actors |

## Log lines

| Line | Says |
|---|---|
| `coop: perf: on (LICHII_ORM_PERF), a line every 5 s` | the meter is on |
| `coop: perf: 5.0 s, 300 presents (16.61 ms each), 300 calc frames, 5 puppets; calc 0.212 ms/frame (puppet frame ..., pose ..., heads ..., belt ..., calls ...; callers 10: ...); present 0.305 ms/frame (objects ..., input ..., read ..., flags ..., session ..., publish ..., share ..., features ..., place ...); guest 412 reads 96 writes 181 KB per calc frame` | one window |
| `coop: flag probe: <name> 0 -> 1 in our game` / `... from P2` / `... bounces between games (3 changes in 10.0 s, ours and the others'): a held: candidate` | the flag probe |
| `coop: test ...` (for example `coop: test int PlayerStatus.CurrentRupee (0x...) = 87: queued`, `coop: test skip`, `coop: test key 0x...: queued`) | each test command and what the game did with it |
| `coop: life: ...` | `LICHII_COOP_DEBUG_LIFE` |
| `coop-options.ini: skipped '<line>' (only LICHII_COOP_NAME=value, LICHII_ORM_NAME=value or EDEN_NX2_NAME=value lines)` | `kit-paths.ps1` ignored a line |

## Status

| Part | Status |
|---|---|
| Test commands | used in passing two-game runs on our test9-based build: `life` (`caer-pareja`), `int` and `await mate` (`cartera-cada-uno`, `charla-mido`), `talkthrough` (`charla-dos`, `efectos-cofre`), `skip` (`meca-sostenido-espiritu`) |
| Perf meter | not yet confirmed in game (unit-tested only) |
| Flag probe | not yet confirmed in game |
| Hosting checks | unit-tested (`orm_hosting_test.ps1`); not yet run on a real host PC from this tree's kit |
| Puppet pack additions | built and tested against a real romfs dump; the actors' use in game is reported per feature (09, 11, 12) |

## Our harness and synthetic saves

The harness runs two hidden games per scenario (A and B, joined to a local server on 127.0.0.1), plays a script in each through the test-input commands above, and checks the logs with scenario directives (`#! expect`, `#! never`, `#! count`, `within <mark>..<mark>`, `#! switches`, `#! saves`, ...); a progress meter counts the pass and fail lines over the scenario summaries. Scenario names used throughout these docs (`caer-pareja`, `charla-dos`, `choque-bloqueo`...) are its scenarios. The synthetic save tool builds the saves the scenarios start from, on each PC from its own game files. Both come in `tools/orm/harness` and `tools/orm/saves`, with their own READMEs.

## Known gaps

- `LICHII_ORM_PERF` uses upstream's `EnvFlag` reading, not `coop_switch.h`: `LICHII_ORM_PERF=off` turns it **on**.
- `start-server.ps1` always passes `--ws-port` (a port, or `off`). The server built from this tree (`server/options.cpp`) does not know that option and stops with `unknown argument: --ws-port`; the WebSocket listener is not part of this tree (feature 02-network). Either the server accepts `--ws-port` or `start-server.ps1` leaves it out when the server has no WebSocket.
- `make_kit.ps1` does not copy `comprobar-alojamiento.cmd` into the kit, and does not fill `@ORM_MAX_PLAYERS@` in `orm-hosting.ps1` (the script then says 6).
- `pvp-cornisa` needs a ledge position before it can run (09-pvp).
