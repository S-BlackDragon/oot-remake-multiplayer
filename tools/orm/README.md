# tools/orm: test tools for the two-game co-op

These tools start two hidden games on one PC (plus a relay and, optionally, fake players), play a scenario file in
each game and check the games' logs. They are a gift, taken as they are or changed: nothing here is needed to build or
run the co-op itself. File and scenario names are Spanish on purpose (`escenarios`, `caliente`, `partida`...).

| Folder | What it is |
|---|---|
| `harness/` | the pair harness, its scenarios and its checks |
| `saves/` | the synthetic save tool: a base save plus a text recipe gives a save folder at a chosen point of the game |
| `plataforma/` | `comparar.py`: compares a Hookshot run against an emulator run from their logs (Spanish report) |

Not included: a progress meter that depends on our private planning documents, and anything that needs tools of the
source tree (`tools/romfs`, `tools/kit`). Tests that need them are skipped with a reason.

## Configuration (environment variables)

Every machine path comes from `harness/orm_paths.py`. Everything lives under `ORM_ROOT` (default `~/orm-work`) unless a
more specific variable is set. PowerShell scripts read the same variables (`$env:ORM_ROOT`, same default).

| Variable | Meaning | Default |
|---|---|---|
| `ORM_ROOT` | base folder | `~/orm-work` |
| `ORM_BUILD_BIN` | emulator build folder (with `eden-cli.exe`) | `ORM_ROOT/build/bin` |
| `ORM_INST_DIR` | where test instances are made | `ORM_ROOT/inst` |
| `ORM_SERVER_EXE` | `orm_server` (or the upstream `coop_server`, with `--server-exe`) | `ORM_ROOT/server/orm_server` |
| `ORM_BOT_EXE` | `remake-bot`, for scenarios with fake players | `ORM_ROOT/bot/remake-bot` |
| `ORM_TREE` | source tree with `tools/release/kit/*.txt`, `tools/autotest/scenarios/load.txt` and `lichii/hook` | `ORM_ROOT/tree` |
| `ORM_WT` | folder of trees (only `lint_escenarios.py`) | `ORM_ROOT/wt` |
| `ORM_SAVES_ROOT` | where synthetic saves are written | `ORM_ROOT/saves` |
| `ORM_BASE_SAVES` | a real save folder the recipes start from (read only) | `ORM_SAVES_ROOT/base` |
| `ORM_DUMP`, `ORM_ROMFS` | unpacked dump, merged romfs | `ORM_ROOT/dump`, `ORM_ROOT/romfs_merged` |
| `ORM_PUPPET_LOAD` | puppet pack `load` folder | `ORM_ROOT/puppet_user/load` |
| `ORM_REF_USER` | reference emulator `user/` folder (keys, firmware) | `ORM_ROOT/ref/user` |
| `ORM_GAME_DIR` | folder with the game package (`*.nup`) | `ORM_ROOT/game` |
| `ORM_REPORTS`, `ORM_LOG_ROOT` | summaries, raw logs | `ORM_ROOT/reports`, `ORM_ROOT/logs` |
| `ORM_GAME_LOCK` | lock folder, so only one set of games runs at once | `ORM_LOG_ROOT/game.lock` |
| `ORM_STORY_LIST` | `shared_story.txt` | `ORM_TREE/tools/release/kit/shared_story.txt` |
| `ORM_NETSIM_DIR` | optional TCP link simulator (`netsim.py`, class `NetSim`), `aguante.py` only | `ORM_ROOT/netsim` |
| `ORM_UNLOAD_MODELS` | comma list of local Ollama model prefixes to unload before a run | none |
| `ORM_STACK` | `test8`, `test9` or `test10`: fills the build, server, bot and instance variables above (see `protocolo.py`) | none |
| `ORM_READY_TIMEOUT` | seconds to wait for free RAM before a run | 120 |

The games are not started unless 14 GB of RAM are free and no earlier game is running. A `STOP` file in the gift root
holds the long scripts.

## The tools in `harness/`

| File | What it does |
|---|---|
| `r03_pareja.py` | one scenario: relay, two games, both join, `load.txt`, then `<name>.A.txt` and `<name>.B.txt`; checks the logs |
| `caliente.py` | the same for a list of scenarios, keeping one booted pair of games between them ("hot instances") |
| `secuencia.ps1` | runs a list of scenarios one after the other (`-Caliente` uses `caliente.py`) |
| `rapida_t9.ps1`, `r03_todos.ps1` | a quick set of six scenarios; every scenario of a folder |
| `aguante.py` | a long stability run (memory, handles, fps slopes; optional relay restart and network cut) |
| `carga6.ps1`, `rendimiento.ps1`, `latencia.ps1` | one game with five fake players: load, fps cost by variant, latency |
| `orm_inst.py` | the instances: hard-linked copy of the build, own `user/` folder, hidden window, no audio |
| `protocolo.py` | protocol number, message types, the three stacks, the switch names |
| `resumen_registro.py` | a readable timeline from an emulator log |
| `lint_escenarios.py` | checks scenario files without a game (see the directive list below) |
| `historias.py`, `meca_adulto.py`, `recorridos_adulto.py`, `seis_adulto.py` | generators of scenario files from tables (edit the table, not the files; `--check` compares) |
| `romfs_combinado.py`, `r03_marioneta.ps1` | merge the base and update romfs of your own dump; build the puppet pack |
| `captura.py`, `escenas_banc.txt` | window screenshots; the list of scenes and start points |

## Run a pair scenario

```powershell
$env:ORM_ROOT = "$HOME\orm-work"      # optional (this is the default); or set the individual variables
$env:ORM_STACK = "test10"              # optional: names the build, server, bot and instance folders
python -I tools/orm/harness/r03_pareja.py --scenario cartel --transport ws --timeout 240
```

`--scenario NAME` reads `harness/escenarios/NAME.A.txt` and `NAME.B.txt` (`--scen-dir` for another folder),
`--players 1` runs one game, `--bots N` adds fake players, `--env K=V` adds a hook switch, `--capture-marks` takes a
screenshot at every `mark`. The summary is `ORM_REPORTS/pareja/NAME_resumen.txt` (`RESULT: PASS` or `FAIL (...)`),
with each game's log and the server log beside it. Several scenarios:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness/secuencia.ps1 -Tag mine -Only cartel,charla -Transport ws -HeaderSwitches
```

Nothing is touched outside `ORM_INST_DIR`, `ORM_REPORTS`, `ORM_LOG_ROOT` and `ORM_SAVES_ROOT`; only processes started
here are stopped.

## Scenario files

A scenario is `NAME.A.txt` (game A), optionally `NAME.B.txt` (game B) and `NAME.bots.txt` (the fake players' script).
Lines starting with `#!` are directives; other `#` lines are comments. The rest is the hook's test script, one command a
line: `wait N` (ticks, 60 a second), `press BUTTON`, `stick X Y N`, `mark NAME`, `warp SCENE [START]`, `goto X Y Z N`,
`life N`, `give ...`, `enum ...`, `event ...`, `key ...`. `lint_escenarios.py` checks every command against the hook's
parser.

| Directive | Meaning |
|---|---|
| `#! expect A\|B REGEX [within MARK..MARK]` | a log line of that game matches |
| `#! never A\|B REGEX [within ...]` | no line matches |
| `#! count A\|B REGEX >=\|<=\|== N [within ...]` | the number of matching lines |
| `#! noload` | the script loads the save itself (title screen included) |
| `#! start REGEX` | the log line that says the scenario has started |
| `#! switches K=V,K=V` / `#! env K=V` | hook switches for both games; `A:K=V` or `B:K=V` for one game |
| `#! saves BLOCK\|FOLDER\|default` | the save this game starts from: a recipe name of `saves/recetas` (built when missing) |
| `#! players N`, `#! bots N`, `#! bot-arg ARGS` | games (1 or 2), fake players, extra bot arguments |
| `#! transport ws\|udp`, `#! join A-first\|B-first`, `#! timeout N` | connection and timing options |
| `#! puppets`, `#! min-fps N` | fail when a game shows fewer puppets than players / runs under N fps |
| `#! caliente no [why]`, `#! caliente despues frio`, `#! caliente antes LINE`, `#! caliente stagger N` | hot-instance behaviour (`caliente.py`) |

A path in a switch that starts with `tools/release/kit/` or `tools/autotest/` is resolved against `ORM_TREE`
(for example `LICHII_COOP_TALK=tools/release/kit/shared_talk.txt`).

## Synthetic saves (`saves/`)

A recipe (`saves/recetas/NN-name.txt`) names what a save holds, by the game's own flag names: `scene`, `age`, `hearts`,
`rupees`, `item`, `song`, `step`, `seen`, `dungeon`, `equip`, `bottle`, `bool`, `int`, `enum`, `key64`, `include`
(the full list is in `receta.py`). `partida.py` applies it to a copy of a base save and reads the result back. No game
data is in the repository: the base save and the romfs come from your own copy.

```powershell
python saves/partida.py check                       # parse every recipe (names are checked with the romfs when found)
python saves/partida.py build saves/recetas/08-dodongo.txt --base <base save folder> --out <folder>
python saves/partida.py all                         # every recipe into ORM_SAVES_ROOT
python saves/partida.py show <save folder> Sequence_IsChildhood
python saves/entradas.py write|check|all|verify|map # the "just arrived" saves of the route scenarios
```

`#! saves 08-dodongo` in a scenario file builds and uses that save. Looking up a start point or enum values needs the
romfs (`ORM_ROMFS`) and `romfs_read.py` from the source tree's `tools/romfs`, with `zstandard` installed.

## Tests

From any folder (they need no game, server or dump):

```powershell
python -I -m pytest -q <path to the gift>/tools/orm
```

Tests that need your own data or tools of the source tree are skipped with a reason: games' captured logs, a tree under
`ORM_TREE`, `tools/romfs`, the merged romfs, a bot executable, `kit_steps.ps1`. `pytest.ini` in this folder keeps the
run from looking above it.
