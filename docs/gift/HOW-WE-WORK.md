# How we work

**English** | [Español](es/HOW-WE-WORK.md)

This is our method in plain words. "We" is me (S-BlackDragon) and the AI agents I work with. The tools named here are
in this gift under `tools/orm/`.

| Tool | Where | What |
|---|---|---|
| Pair harness | `tools/orm/harness/r03_pareja.py` | runs one scenario with two hidden games and writes a summary |
| Batches | `tools/orm/harness/secuencia.ps1` | runs a list of scenarios one after the other, one summary line each |
| Hot instances | `tools/orm/harness/caliente.py` | one pair of games plays a chain of scenarios without restarting |
| Scenario lint | `tools/orm/harness/lint_escenarios.py` | checks every scenario without a game |
| Scenarios | `tools/orm/harness/escenarios/` | `<name>.A.txt` and `<name>.B.txt` |
| Synthetic saves | `tools/orm/saves/` | `partida.py` and the recipes in `recetas/` |
| Progress meter | `avance.py` (kept in our own repository: it reads our private notes) | counts what is verified in game, from the summaries only |
| Platform comparison | `tools/orm/plataforma/comparar.py` | same route on Hookshot and on the emulator |

Some file and directive names are Spanish (`escenarios` = scenarios, `partida` = save, `receta` = recipe,
`caliente` = hot, `avance` = progress). The code and log lines are in English.

## 1. Two hidden games per test

Every test is a **scenario**: two games (A and B) on the same PC, each with its own instance folder (its own copy of
the emulator user folder, saves and `coop.ini`), a local server on 127.0.0.1, and windows hidden (they never take the
focus and make no sound). Each game reads a small script through the hook's test input: walk, press buttons, warp,
place Link, chat, write a flag, and `mark <name>` to cut its log into parts.

The harness starts the server and both games, waits until both joined and loaded, lets the scripts run, then reads the
two game logs and the server log and writes `<name>_resumen.txt` with one line per check and a last line
`RESULT: PASS` or `RESULT: FAIL (...)`. Nobody has to watch the games: the log is the judge. Screenshots are taken at
the marks (`--capture-marks`) for the things only eyes can judge.

Only one pair runs at a time on my PC (a lock file), and nothing starts while a `STOP` file exists (I am playing).

A short example (`choque-encima`, abridged):

```
# choque-encima.A.txt
#! expect A coop: bodies: colliders on within start..apart
#! expect A bodies: our Link stands on P\d's head within drop..end
#! never A coop: check: bodies: collider fallback
mark start
wait 240
goto -82.0 30.6 0.4 600
mark apart
mark drop
place -82.0 124.93 33.65
wait 180
mark end
```

## 2. Scenario directives (`#!` lines)

The `#!` header lines say what must happen. The exact list is the one `lint_escenarios.py` knows.

| Directive | Meaning |
|---|---|
| `#! expect A\|B <regex> [within <mark>..<mark>]` | this game's log must have a line that matches, inside that part of the log |
| `#! never A\|B <regex> [within ...]` | this game's log must never have such a line |
| `#! count A\|B <regex> >=\|<=\|== N [within ...]` | the number of matching lines must be as said |
| `#! noload` | the script loads the save itself (no `load.txt`) |
| `#! start <regex>` | the scenario counts as started when the log matches (e.g. `coop: test warp done`) |
| `#! switches K=V,K=V` | hook switches for this scenario (the batch's own switches win) |
| `#! env K=V` | one more hook switch (repeatable) |
| `#! timeout N` | seconds before the run is cut |
| `#! saves <block>\|<folder>\|default` | which save this game starts from (a synthetic block such as `12-bosque`) |
| `#! players 1\|2` | how many games |
| `#! bots N` | fake players (0 to 5) after the games load |
| `#! bot-arg ARGS` | more bot arguments (repeatable) |
| `#! transport ws\|udp` | how the games reach the server |
| `#! join A-first\|B-first` | the second game starts only after the first joined |
| `#! puppets` | FAIL when a game shows fewer puppets than the other players |
| `#! min-fps N` | FAIL when a game's median fps from its first mark is under N |
| `#! caliente no [reason]`, `despues frio`, `antes <script line>`, `stagger N` | hot-instance options (section 4) |

`within boot..` means "from the game's start-up", for lines the hook writes before any script runs.

**The lint.** `lint_escenarios.py` runs without a game (game time is the scarce resource). It parses every `#!` line,
compiles every regex, checks every script command against the hook's script parser, every `warp` against the romfs
scene list, every `within` mark against the marks the script writes, and every switch name against the code. Most
important: it checks that **each expect regex can match a log line the code really writes** (it reads every string
literal of the hook as a format and runs the regex over it). A typo in an expected line is found before a 10-minute run.

**Mutants.** A good scenario must also fail when the feature is off. So most scenarios name a mutant: the same run with
the feature's switch off (or a key line deleted) must go red.

## 3. Synthetic saves

To test the second half of the game you need a save there. A **recipe** (`tools/orm/saves/recetas/<block>.txt`) lists
names of flags and values: items, songs, story steps, events already seen, age, hearts, dungeons done, equipment, and
the start point. Each recipe includes the one before, so `20-ganon` carries everything. `partida.py` writes the recipe
over a real save **of your own**, re-reads every slot from disk and checks that each value is there and that nothing
else changed, byte by byte. Only the recipes are in the gift; the saves are built on each PC, never shared.

```
python tools/orm/saves/partida.py check     # every recipe against the romfs (flag exists, type, enum value, start point)
python tools/orm/saves/partida.py all       # builds every block in a few seconds
```

A scenario says `#! saves 13-fuego` and the harness builds that save (or rebuilds it if the recipe changed) before
starting the games. A real save always wins over a synthetic one. Limits: some moments cannot be reached this way yet
(Epona won, some boss states, the final escape needed its own recipe `21-huida-arriba`); those show up only in real
play.

## 4. Hot instances (`caliente.py`)

Starting a pair of games costs minutes. `caliente.py` groups scenarios into **families** (same games, transport,
switches and save), starts the pair once, and plays the family's scenarios one after another. Before each scenario it
undoes what the last one wrote (flags, ints, life, bottles) and warps to another scene, so the scenario's own warp
really reloads. Each scenario still gets its own summary from its own part of the logs.

A broken state (a game closed, a warp not taken, no new mark in time) closes the pair and the scenario runs **cold**
(fresh games). A hot FAIL with healthy games is also run again cold, and the cold result is the one that counts: an
inherited state never makes a pass. The pair restarts after a boss kill, a shared story event, a downed Link, or when
a log grows too big.

## 5. Switches are off until a game confirms them

Every new feature has a switch (`LICHII_ORM_*`; one reader accepts `0/1/true/false/yes/no/on/off`). It is **off in the
kit options** until a scenario with two real games shows it working; the probes turn it on for themselves. A feature
that only passed unit tests is "not yet confirmed in game" and never reaches friends switched on.

## 6. Every risky path has a fallback and a log line

When the code depends on something we could not read from the game in advance, it gets three things: a robust path
(measure the real value in the running game), a fallback path, and **one self-check line** that says which path ran,
once per thing and path:

```
coop: check: <thing> ok <what was measured or read>
coop: check: <thing> fallback <why, and what is used instead>
```

The scenarios check these lines: `ok` is the good result; `fallback` turns the scenario red and says why. So a risk is
never a note in a document: it is code, a fallback, and a line that tells which way it went. When a fix depends on a
value from the game, we write a short **probe** (two games, a few minutes) that logs the value, put the value in the
code, and run the same scenario again to confirm.

## 7. Working by families of failures

A batch gives many red scenarios. We do not fix them one by one. First we read the logs only (no game), and group the
reds by cause into families, for example: the second game loaded the wrong save; the second game started late and a
fixed wait was too short; a check looked in the wrong part of the log; the script's movement timed out; the PC ran out
of RAM; a moment the synthetic saves cannot reach. Then each family is fixed once, for every scenario it touches, and
the batch runs again. A red is never accepted because it passed on a second try.

## 8. The progress meter (`avance.py`)

Our `avance.py` (not in this gift: it reads our private notes and branches) reads only files (the scenario summaries, the scenarios, git) and writes a progress
page. Each of the 20 content blocks of the game has up to 8 boxes:

| Box | What it asks |
|---|---|
| M | puppets and enemies: both players see each other; enemies have one owner and the same life and death |
| D | world and dungeon: switches, doors, keys, water, chests arrive live in the other game |
| J | boss: entering together, downed and revived inside, group defeat, late entry, the kill seen by both |
| C | cutscenes: story cutscenes at the same time, and the same world for whoever did not see them |
| P | talks: the one who talks claims the townsperson, nobody hides |
| R | a full walk-through of the zone or dungeon with two games |
| 6 | the same with six (one game plus five bots), and once with six people |
| A | seen by me: screenshots or a real game with friends |

A box counts only when its scenarios **pass with their checks** in one run with two real games. The meter gives
separate figures: verified in game, code ready (integrated and documented, not played), waiting for a value from a
game, and scenarios written. The current figures are in [LIMITS.md](LIMITS.md).

## Contributing, or rather: where our work goes

Our public fork, **github.com/S-BlackDragon/oot-remake-multiplayer**, is where we keep adding our work over time: one
commit per feature (the feature IDs of this gift), with the same sanitizing (no paths of my PC, no private hosts, no
game data). Look at it whenever you like, take what fits, change it, or leave it. Nothing is expected from you: no
review, no merge, no answer.
