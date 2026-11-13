"""Hot instances one pair of games booted once plays a chain of
scenarios. Hot instances.

    python -I tools/orm/harness/caliente.py --tag fam_deku --scen-dir tools/orm/harness/escenarios \
        --only jefe-gohma-entrada,jefe-gohma-caer [--switch K=V ...] [--header-switches] [--plan]

secuencia.ps1 -Caliente runs the same thing with its own arguments. What it does:

- Groups the scenarios into families: same games, transport, hook switches and save. One family = one boot (server,
  both games, join, daurosk's load.txt one game at a time, settle), as r03_pareja.py does for one scenario.
- Each scenario in the running games: a reset script per game (the undo of the flags the last scenario set, its
  '#! caliente antes' lines, a warp to another scene so the next warp really reloads), then the scenario itself:
  a '#! noload' scenario without its title screen (its warp moves to where the save would load; the hook takes a warp
  in free play by starting the game's DefaultParentEvent, orm/t9-integrado), and a scenario that starts where the save
  stands after a warp to the save's place (its recipe's 'scene', or Kokiri Forest for the default save).
- Verdict as r03_pareja.py (game_checks, the '#! expect' lines), on that scenario's part of each log only.
- Bad state (a game exited, a script not read, a warp not taken, no new mark for the step timeout, over its time,
  frozen): the pair is closed and the scenario runs cold (r03_pareja.py, games started for it). A hot FAIL with
  healthy games runs cold too (--no-recheck: not), so a state left by an earlier scenario never passes for a bug.
- Cold from the start: bots, '#! caliente no', scripts that press through the title without a warp, a warp with no
  start point, a '#! noload' game file without a warp. They run after the hot ones, one r03_pareja.py each.
- After a scenario that beat a boss, played a shared story event, left Link down or says '#! caliente despues frio',
  the pair is closed and the next one boots again.

Summary: <ORM_REPORTS>/pareja/<tag>/resumen.txt (secuencia.ps1's lines plus the mode of each), each scenario's
<name>_resumen.txt and its part of the game and server logs beside it. --plan writes the plan and the hot scripts
(guiones/) without starting anything.
"""
import argparse, os, re, shutil, statistics, subprocess, sys, time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import orm_inst as oi
import protocolo
import orm_paths
import r03_pareja as rp

HERE = Path(__file__).resolve().parent
REPORTS = orm_paths.REPORTS / "pareja"
LOCK = orm_paths.GAME_LOCK
STOP = oi.REPO / "STOP"
TICKS_PER_S = 60  # the hook's kTicksPerSecond
PRESS_TICKS, GOTO_TICKS = 6, 600  # kPressTicks (down, then as long up), kGotoTicks
WARP_DONE = r"coop: test warp done"
WARP_GAVE_UP = r"coop: test warp to \S+ did not happen"
SCRIPT_STARTED = "coop: test script started"


@dataclass
class Timing:
    """Every wait of the runner (the unit tests shrink them)."""
    poll: float = 1.0
    read: float = 20.0         # a written script must log 'test script started' by then
    warp: float = 110.0        # a reset warp (the hook gives up at 90 s)
    settle: float = 90.0       # 3 fps lines >= MIN_FPS after a reset
    stagger: float = 20.0      # B's scenario after A's start (cold: 60 s plus B's title screen)
    tail: float = 22.0         # after the last marks: the freeze verdict reads the last 20 s
    boot: float = 240.0        # both welcomed
    load_dip: float = 20.0     # after load.txt's 'loaded' (r03_pareja)
    margin: float = 120.0      # a scenario's time: its scripts plus this, plus the stagger


# ---------------------------------------------------------------------------------------- the scripts, made hot

def code(line: str) -> str:
    return line.split("#", 1)[0].strip()


def words(line: str) -> list[str]:
    return code(line).split()


def script_seconds(text: str) -> float:
    """How long a script plays (real time, as the hook counts it): its waits, holds, presses, sticks, gotos (their
    tick limit) and towards."""
    ticks = 0
    for line in text.splitlines():
        w = words(line)
        if not w:
            continue
        cmd, num = w[0].lower(), [x for x in w[1:] if re.fullmatch(r"\d+", x)]
        if cmd == "wait" and num:
            ticks += int(num[0])
        elif cmd == "hold" and len(w) >= 3 and w[2].isdigit():
            ticks += int(w[2])
        elif cmd == "press":
            ticks += 2 * (int(w[2]) if len(w) >= 3 and w[2].isdigit() else PRESS_TICKS)
        elif cmd in ("stick", "rstick") and len(w) >= 4 and w[3].isdigit():
            ticks += int(w[3])
        elif cmd == "goto":
            ticks += int(w[4]) if len(w) >= 5 and w[4].isdigit() else GOTO_TICKS
        elif cmd == "toward" and len(w) >= 4 and w[3].isdigit():
            ticks += int(w[3])
        elif cmd == "await":  # await mate [TICKS]: at most its ticks (9000 by default)
            ticks += int(w[2]) if len(w) >= 3 and w[2].isdigit() else 9000
        elif cmd == "talkthrough":  # talkthrough [TICKS]: at most its ticks (1800 by default)
            ticks += int(w[1]) if len(w) >= 2 and w[1].isdigit() else 1800
    return ticks / TICKS_PER_S


def warp_target(line: str) -> tuple[str, str | None, str | None]:
    """(scene, start or None, age word or None) of a 'warp SCENE [START] [adult|child]' line."""
    w = words(line)[1:]
    age = w.pop().lower() if len(w) >= 2 and w[-1].lower() in ("adult", "child") else None
    return w[0], (w[1] if len(w) > 1 else None), age


def hot_warp_script(text: str) -> tuple[str | None, str]:
    """A '#! noload' game file without its title screen: (script, '') or (None, why it cannot run hot).

    The form of every one of ours: lines, `warp SCENE START`, the title screen's wait, `mark title`, maybe a deliberate
    delay (`wait N`, `mark late`), four `press A` with waits (Continue: the save loads and the warp is taken), the
    scenario. Hot: the lines before the warp, the marks and the delay, then the warp where the save would load (the hook
    takes it in free play with the game's DefaultParentEvent), then the rest as written."""
    lines = text.splitlines()
    cmds = [(words(l) or [""])[0].lower() for l in lines]
    if "warp" not in cmds:
        return None, "it presses through the title without a warp"
    w = cmds.index("warp")
    if "press" in cmds[:w]:
        return None, "a press before its first warp"
    scene, start, _ = warp_target(lines[w])
    if start is None:
        return None, f"'warp {scene}' names no start point (a warp in the world would keep the old scene's start: black)"
    t = next((i for i in range(w + 1, len(lines)) if cmds[i] == "mark" and words(lines[i])[1:2] == ["title"]), None)
    if t is None:
        return None, "no 'mark title' after its warp"
    presses = [i for i in range(t + 1, len(lines)) if cmds[i] == "press"]
    if len(presses) < 4:
        return None, "fewer than four presses after 'mark title'"
    p = presses[3]
    for i in range(w + 1, p + 1):
        if cmds[i] not in ("", "wait", "press", "mark"):
            return None, f"line {i + 1} '{code(lines[i])}' inside the title screen's lines"
        if cmds[i] == "press" and words(lines[i])[1:2] != ["A"]:
            return None, f"line {i + 1} '{code(lines[i])}': the title screen is four 'press A'"
    kept = [lines[i] for i in range(w + 1, p + 1)
            if cmds[i] == "mark" or (cmds[i] == "wait" and t < i < presses[0]) or (cmds[i] == "" and lines[i].strip())]
    return "\n".join(lines[:w] + kept + [lines[w]] + lines[p + 1:]) + "\n", ""


# --------------------------------------------------------------------------- where a family's games stand

@dataclass(frozen=True)
class Home:
    scene: str
    start: str
    age: str | None = None              # 'adult'/'child' for a scene without the age in its name
    at: tuple | None = None             # then 'place X Y Z' (the default save stands off any start point)

    def warp(self) -> str:
        return f"warp {self.scene} {self.start}" + (f" {self.age}" if self.age else "")

    @property
    def adult(self) -> bool:
        return self.scene.endswith("_Adult") or self.age == "adult"


# The default save (your base save): Kokiri Forest, Link at -82.0 122.3 33.7 after Continue (the 'you at' of
# every cartel run). The start point is only to name one; 'place' puts Link where Continue does.
DEFAULT_HOME = Home("KokiriForest_Child", "KokiriForest_FrontOfLostWood", None, (-82.0, 122.3, 33.7))
# Another scene to warp to first, so the scenario's own warp reloads (a warp to the scene Link is in is done at once,
# with nothing reset). Entrance start points of the hook's table (coop_start_points.inc).
BOUNCE = {False: [Home("HyruleField_Child", "HyruleField_FrontOfMarket"),
                  Home("KokiriForest_Child", "KokiriForest_FrontOfLostWood")],
          True: [Home("HyruleField_Adult", "HyruleField_FrontOfMarket"),
                 Home("KokiriForest_Adult", "KokiriForest_FrontOfLostWood")]}


def recipe_home(block: str, recipes: Path = rp.RECIPES) -> tuple[Home | None, str]:
    """(home, note) of a saves block: its recipe's 'scene BANC STARTPOS' and age."""
    sys.path.insert(0, str(rp.PARTIDAS))
    import receta  # noqa: PLC0415 (tools/orm/saves, pure Python)
    try:
        r = receta.parse(str(recipes / f"{block}.txt"))
    except (receta.RecipeError, OSError) as e:
        return None, f"recipe {block}: {e}"
    if not r.scene or not r.scene[1]:
        return None, f"recipe {block} names no start point ('scene BANC STARTPOS')"
    banc, start = r.scene[0], r.scene[1]
    age = r.age if r.age and not banc.endswith(("_Adult", "_Child")) else None
    return Home(banc, start, age), f"recipe {block}: {banc} {start}"


def family_home(saves_value: str | None, saves_path: Path, override: Home | None = None) -> tuple[Home | None, str]:
    if override:
        return override, "--home"
    if saves_value is None or saves_value in ("default", "base"):
        return (DEFAULT_HOME, "the default save") if Path(saves_path) == rp.BASE_SAVES else (None, "a save folder")
    if re.search(r"[\\/:]", saves_value):
        return None, f"save folder {saves_value}: its place is not known (--home)"
    return recipe_home(saves_value)


def bounce_for(targets: set[str], adult: bool) -> Home:
    """The scene the reset warps to: an entrance of the hook's table, of the scenario's age, that it does not go to (a
    recipe's start may be a save point only, so never the home)."""
    for h in BOUNCE[adult]:
        if h.scene not in targets:
            return h
    return BOUNCE[not adult][0]


def parse_home(text: str) -> Home:
    """--home 'SCENE START [adult|child] [X Y Z]'."""
    w = text.split()
    at = tuple(float(x) for x in w[-3:]) if len(w) >= 5 and all(re.fullmatch(r"-?[0-9.]+", x) for x in w[-3:]) else None
    w = w[:-3] if at else w
    age = w.pop().lower() if len(w) == 3 and w[-1].lower() in ("adult", "child") else None
    if len(w) != 2:
        raise ValueError(f"--home '{text}': want SCENE START [adult|child] [X Y Z]")
    return Home(w[0], w[1], age, at)


# ------------------------------------------------------------------- '#! caliente' and what a scenario leaves

CAL = re.compile(r"^#!\s*caliente\b[ \t]*(.*?)\s*$", re.M)
BEFORE_CMDS = {"flag", "int", "key", "enum", "life", "bottle", "give", "wear", "assign", "age", "wait", "mark"}


def caliente_options(texts: dict) -> dict:
    """The '#! caliente' lines of a scenario's game files:

        #! caliente no [why]          always cold (games started for it)
        #! caliente despues frio      the pair is closed after it (it leaves a state no reset undoes)
        #! caliente antes LINE        a script line for this file's game in the reset, before the warp
        #! caliente stagger N         seconds between A's start and B's scenario (default --stagger)"""
    o = {"cold": None, "after_cold": False, "before": {g: [] for g in texts}, "stagger": None, "errors": []}
    for g, t in texts.items():
        for m in CAL.finditer(t or ""):
            v = m.group(1)
            w = v.split()
            if w and w[0] == "no":
                o["cold"] = " ".join(w[1:]) or "'#! caliente no'"
            elif [x.lower().replace("é", "e").replace("í", "i") for x in w] == ["despues", "frio"]:
                o["after_cold"] = True
            elif len(w) >= 2 and w[0] == "antes" and w[1].lower() in BEFORE_CMDS:
                o["before"][g].append(" ".join(w[1:]))
            elif len(w) == 2 and w[0] == "stagger" and re.fullmatch(r"\d+(\.\d+)?", w[1]):
                o["stagger"] = float(w[1])
            else:
                o["errors"].append(m.group(0).strip())
    return o


FLAG_SET = re.compile(r"coop: test flag (\S+) \(0x[0-9a-fA-F]+\) = [01]: queued \(it read ([01])\)")
INT_SET = re.compile(r"coop: test int (\S+) \(0x[0-9a-fA-F]+\) = -?\d+: queued \(it read (-?\d+)\)")
KEY_SET = re.compile(r"coop: test key (0x[0-9a-fA-F]+) = [01]: queued \(it read ([01])\)")
ENUM_READ = re.compile(r"coop: test enum (\S+) \(0x[0-9a-fA-F]+\): find_type -?\d+, value (?:[^;(]*\()?(0x[0-9a-fA-F]{8})")
ENUM_SET = re.compile(r"coop: test enum (\S+) = .*: queued")
LIFE_SET = re.compile(r"coop: test life \d+: written \(was (\d+) of \d+\)")
BOTTLE = re.compile(r"coop: test bottle ([A-E]) (\S+): (written|restore|NOT written|before)")
DOWNED = re.compile(r"coop: downed: \S+ -> (\w+)")
GIVE = re.compile(r"coop: test give ([^:\s]+)")
DIRTY = [(re.compile(r"coop: boss: boss beaten"), "a boss was beaten"),
         (re.compile(r"coop: story: STORY_EVENT"), "a shared story event was played"),
         (re.compile(r"coop: test event \S+:"), "a story event was asked ('event')")]


def leftovers(text: str) -> tuple[list[str], list[str], list[str]]:
    """(undo lines for the next reset, notes, dirty reasons) from one game's part of the log for one scenario: each flag,
    Int, key and enum the script set goes back to what it read first; the first life written goes back to what it was;
    a bottle written is restored. A boss beaten, a shared story event or Link left down: dirty (boot again)."""
    undo, notes, dirty, seen = [], [], [], set()
    for rx, cmd in ((FLAG_SET, "flag"), (INT_SET, "int"), (KEY_SET, "key")):
        for name, was in rx.findall(text):
            if (cmd, name) not in seen:
                seen.add((cmd, name))
                undo.append(f"{cmd} {name} {was}")
    reads = {}
    for name, was in ENUM_READ.findall(text):
        reads.setdefault(name, was)
    for name in dict.fromkeys(ENUM_SET.findall(text)):
        if name in reads:
            undo.append(f"enum {name} {reads[name]}")
        else:
            notes.append(f"enum {name} was set without a value read first: not undone")
    life = LIFE_SET.search(text)
    if life and int(life.group(1)) > 0:
        undo.append(f"life {life.group(1)}")
    bottles = {}
    for slot, value, what in BOTTLE.findall(text):
        if what == "written" and value != "restore":
            bottles[slot] = True
        elif value == "restore":
            bottles[slot] = False
    undo += [f"bottle {s} restore" for s, written in sorted(bottles.items()) if written]
    given = sorted(set(GIVE.findall(text)))
    if given:
        notes.append("items given stay: " + ", ".join(given))
    for rx, why in DIRTY:
        if rx.search(text):
            dirty.append(why)
    downs = DOWNED.findall(text)
    if downs and downs[-1] != "alive":
        dirty.append(f"Link left {downs[-1]}")
    return undo, notes, dirty


# ------------------------------------------------------------------------------------------ the plan

@dataclass
class Plan:
    name: str
    texts: dict                  # game -> file text (None: no file for that game)
    players: int = 2
    transport: str = "ws"
    timeout: int = 240
    env: list = field(default_factory=list)       # K=V for both games (r03_pareja --env, sopt's first)
    cli_env: list = field(default_factory=list)   # what a cold run passes as --env
    saves: dict = field(default_factory=dict)
    saves_lines: list = field(default_factory=list)
    saves_value: str | None = None
    sopt: dict = field(default_factory=dict)
    cal: dict = field(default_factory=dict)
    noload: bool = False
    start: str = WARP_DONE
    kind: str = ""               # 'warp' (its own warp, hot) or 'home' (it starts where the save stands)
    run: dict = field(default_factory=dict)       # game -> the script played hot
    targets: set = field(default_factory=set)
    adult: bool = False          # its warps ask for an adult (a scene X_Adult or the word adult)
    cold: str = ""               # why it runs cold ('' = hot)
    error: str = ""              # why it cannot run at all (a FAIL line, as r03_pareja's)
    seconds: float = 0.0

    def family(self) -> tuple:
        return (self.players, self.transport, tuple(sorted(dict(e.split("=", 1) for e in self.env).items())),
                tuple(str(self.saves.get(g)) for g in "AB"[:self.players]))


def make_plan(name: str, scen: Path, a, common: list[str]) -> Plan:
    """One scenario's plan (no game started; a saves block is built here if missing, as r03_pareja does)."""
    files = {g: scen / f"{name}.{g}.txt" for g in ("A", "B", "bots")}
    texts = {g: (f.read_text(encoding="utf-8") if f.exists() else None) for g, f in files.items()}
    sopt = rp.scenario_options(name, [t for t in texts.values() if t is not None])
    p = Plan(name, {g: texts[g] for g in "AB"}, sopt=sopt)
    p.players = a.players or sopt["players"] or 2
    p.transport = a.transport or sopt["transport"] or "udp"
    p.timeout = a.timeout or sopt["timeout"] or 240
    if a.header_switches and sopt["timeout"] and sopt["timeout"] > p.timeout:
        p.timeout = sopt["timeout"]  # secuencia.ps1 -HeaderSwitches: '#! timeout' when it is longer
    try:
        p.cli_env = protocolo.hook_switches((protocolo.header_switches(files["A"]) if a.header_switches and texts["A"]
                                             else []) + list(common))
    except (ValueError, OSError) as e:
        p.error = f"RESULT: NO SUMMARY ({e})"
        return p
    p.env = sopt["env"] + p.cli_env
    p.cal = caliente_options({g: texts[g] for g in "AB"})
    if texts["A"] is None:
        p.error = f"RESULT: FAIL (no {files['A'].name})"
        return p
    if sopt["errors"] or p.cal["errors"]:
        p.error = "RESULT: FAIL (scenario options not understood: " + "; ".join(sopt["errors"] + p.cal["errors"]) + ")"
        return p
    games = "AB"[:p.players]
    try:
        p.saves, p.saves_lines = rp.plan_saves({g: texts[g] for g in games}, rp.BASE_SAVES,
                                               Path(a.saves) if a.saves else None)
    except rp.SavesError as e:
        p.error = f"RESULT: FAIL (saves: {e})"
        return p
    vals = [rp.saves_header(texts[g]) for g in games if texts[g] is not None]
    p.saves_value = None if a.saves else next((v for v in vals if v is not None), None)
    p.noload, p.start = rp.scenario_plan([texts[g] for g in "AB" if texts[g] is not None])
    if a.bots or sopt["bots"]:
        p.cold = "bots (they join for each scenario)"
    elif sopt.get("join"):
        p.cold = f"#! join {sopt['join']}-first (the hot pair joined long ago: the order is forced at the start)"
    elif p.cal["cold"]:
        p.cold = p.cal["cold"]
    elif p.noload:
        p.kind = "warp"
        for g in games:
            if texts[g] is None:
                p.cold = f"no {name}.{g}.txt: cold, {g} stays at the title screen"
                break
            hot, why = hot_warp_script(texts[g])
            if hot is None:
                p.cold = f"{g}: {why}"
                break
            p.run[g] = hot
            scene, _, age = warp_target(next(l for l in texts[g].splitlines() if words(l)[:1] == ["warp"]))
            p.targets.add(scene)
            p.adult = p.adult or scene.endswith("_Adult") or age == "adult"
    else:
        p.kind = "home"
        for g in games:
            p.run[g] = texts[g] if texts[g] is not None else "wait 1200\n"
    p.seconds = max((script_seconds(t) for t in p.run.values()), default=0.0)
    return p


def scenario_names(scen: Path, only: list[str]) -> list[str]:
    """secuencia.ps1's list: every <name>.A.txt, sorted, kept to --only when given."""
    names = sorted(f.name[:-len(".A.txt")] for f in scen.glob("*.A.txt"))
    only = [x for o in only for x in re.split(r"[,; ]+", o) if x]
    return [n for n in names if n in only] if only else names


def families(plans: list[Plan]) -> tuple[list[list[Plan]], list[Plan]]:
    """(hot families in order of first appearance, the cold ones)."""
    fams: dict[tuple, list[Plan]] = {}
    cold = []
    for p in plans:
        if p.error:
            continue
        if p.cold:
            cold.append(p)
        else:
            fams.setdefault(p.family(), []).append(p)
    return list(fams.values()), cold


# ------------------------------------------------------------------------------------------ reading logs

class Cursor:
    """A log from an offset on: one scenario's part of a log that holds many (read as it grows)."""

    def __init__(self, path: Path, start: int | None = None):
        self.path = Path(path)
        self.start = self.size() if start is None else start
        self.pos = self.start
        self.data = bytearray()

    def size(self) -> int:
        try:
            return self.path.stat().st_size
        except OSError:
            return 0

    def text(self) -> str:
        size = self.size()
        if size < self.pos:  # the file was made again: from its start
            self.start = self.pos = 0
            self.data = bytearray()
        if size > self.pos:
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                chunk = f.read(size - self.pos)
            self.pos += len(chunk)
            self.data += chunk
        return self.data.decode("utf-8", errors="replace")


# ------------------------------------------------------------------------------------------ the pair

class Pair:
    """The server and the family's games, booted once (r03_pareja.py's start: prepare, server, launch, join, load.txt
    one game at a time, settle)."""

    def __init__(self, plan: Plan, out: Path, number: int, a, timing: Timing):
        self.plan, self.out, self.number, self.a, self.t = plan, out, number, a, timing
        self.games = "AB"[:plan.players]
        self.roots, self.scripts, self.procs = {}, {}, {}
        self.server = self.srv_f = None
        self.server_log = out / f"caliente_server_{number}.log"
        self.undo = {g: [] for g in self.games}
        self.played = 0
        self.boot_text = {}

    def log(self, g: str) -> Path:
        return oi.log_path(self.roots[g])

    def alive(self) -> bool:
        return bool(self.procs) and all(p.poll() is None for p in self.procs.values()) and \
            (self.server is None or self.server.poll() is None)

    def write(self, g: str, text: str) -> None:
        self.scripts[g].write_text(text, encoding="utf-8")

    def boot(self, lines: list[str]) -> bool:
        p, a = self.plan, self.a
        ws = p.transport == "ws"
        ini = f"server = ws://127.0.0.1:{a.port + 1}/\r\n" if ws else f"server = 127.0.0.1\r\nport = {a.port}\r\n"
        for k, g in ((1, "A"), (2, "B"))[:p.players]:
            self.roots[g] = oi.prepare(k, saves=p.saves[g], coop_ini=ini + f"name = {g}\r\n", res=a.res)
            with open(self.roots[g] / "orm.ini", "a", encoding="utf-8") as f:  # 1 GiB log cap instead of 100 MiB
                f.write("[Debugging]\nextended_logging" + oi.BS + "default=false\nextended_logging=true\n")
            self.scripts[g] = self.roots[g] / "script.txt"
            self.write(g, "")
        self.srv_f = open(self.server_log, "w", encoding="utf-8")
        self.server = subprocess.Popen([str(rp.SERVER), "--udp-port", str(a.port), "--bind", "127.0.0.1", "--ws-port",
                                        str(a.port + 1) if ws else "off", "--max-half-open", "16"],
                                       stdout=self.srv_f, stderr=subprocess.STDOUT, creationflags=oi.NO_WINDOW)
        extra = dict(e.split("=", 1) for e in p.env)
        for g in self.games:
            self.procs[g] = oi.launch(self.roots[g], dict(rp.SWITCHES, **extra,
                                                          LICHII_COOP_TEST_SCRIPT=str(self.scripts[g])))
        procs = list(self.procs.values())
        t = oi.wait_log(list(self.roots.values()), r"coop: welcomed as player", self.t.boot, procs)
        lines.append(f"boot {self.number}: joined " + (f"in {t:.0f} s" if t is not None else "FAIL (timeout or a game exited)"))
        if t is None:
            return False
        scen = Path(a.scen_dir)
        load = (scen / "load.txt" if (scen / "load.txt").exists() else rp.SCEN / "load.txt").read_text(encoding="utf-8")
        for g in self.games:  # one world load at a time (D-R01)
            self.write(g, load)
            t = oi.wait_log([self.roots[g]], r"coop: test mark loaded", self.t.boot, procs)
            if t is None:
                lines.append(f"boot {self.number}: {g} save loaded: FAIL")
                return False
            time.sleep(self.t.load_dip)
            t0 = time.time()
            while time.time() - t0 < self.t.settle:
                f = oi.fps_after(self.roots[g], "coop: test mark loaded")
                if len(f) >= 3 and min(f[-3:]) >= oi.MIN_FPS:
                    break
                time.sleep(self.t.poll)
            else:
                lines.append(f"boot {self.number}: {g} world settled: FAIL in {self.t.settle:.0f} s (frozen)")
                return False
            lines.append(f"boot {self.number}: {g} loaded and settled")
        # what each game logged once at its start (a directive 'within boot..' reads it, with the scenario's part)
        self.boot_text = {g: oi.log_text(self.roots[g]) for g in self.games}
        lines.append(f"boot {self.number}: " + rp.join_line(self.boot_text))
        for g in self.games:  # the wrong-save bug (3614aa9): a game whose first life is not its save's
            good, line = rp.life_check(g, self.boot_text[g], rp.save_maxlife(p.saves[g]))
            lines.append(f"boot {self.number}: {line}")
            if not good:
                return False
        return True

    def shutdown(self) -> None:
        oi.kill(list(self.procs.values()))
        self.procs = {}
        if self.server is not None:
            self.server.kill()
            self.server.wait(timeout=30)  # the next boot binds the same port
            self.server = None
        if self.srv_f:
            self.srv_f.close()
            self.srv_f = None

    def log_mb(self) -> float:
        return max((self.log(g).stat().st_size / 2**20 for g in self.games if self.log(g).exists()), default=0.0)


class Bad(Exception):
    """The pair is in a bad state: close it, the scenario runs cold."""


def wait_text(pair: Pair, curs: dict, rx: str, timeout: float, fail_rx: str | None = None) -> float:
    """Seconds until every cursor's text matches rx; Bad on a game exit, fail_rx or the timeout."""
    r, f, t0 = re.compile(rx), re.compile(fail_rx) if fail_rx else None, time.time()
    while True:
        if not pair.alive():
            raise Bad("a game or the server exited")
        texts = {g: c.text() for g, c in curs.items()}
        if f and any(f.search(t) for t in texts.values()):
            g = next(g for g, t in texts.items() if f.search(t))
            raise Bad(f"{g}: " + f.search(texts[g]).group(0))
        if all(r.search(t) for t in texts.values()):
            return time.time() - t0
        if time.time() - t0 > timeout:
            raise Bad(f"{'/'.join(curs)}: no '{rx}' in {timeout:.0f} s")
        time.sleep(pair.t.poll)


def play(pair: Pair, g: str, text: str, until: str, timeout: float, fail_rx: str | None = None) -> tuple[Cursor, float]:
    """Writes one game's script and waits for its log to say `until` (Bad when the hook never reads it)."""
    cur = Cursor(pair.log(g))
    pair.write(g, text)
    try:
        wait_text(pair, {g: cur}, re.escape(SCRIPT_STARTED), pair.t.read)
    except Bad as e:
        raise Bad(f"{g}: the hook did not read its script ({e})") from None
    return cur, wait_text(pair, {g: cur}, until, timeout, fail_rx)


def reset(pair: Pair, p: Plan, home: Home | None, lines: list[str]) -> None:
    """Each game: the undo of the last scenario and the '#! caliente antes' lines, a warp to another scene; for a scenario
    that starts where the save stands, a warp home (and place). Then 3 fps lines >= MIN_FPS."""
    adult = p.adult if p.kind == "warp" else (home.adult if home else False)
    bounce = bounce_for(p.targets | ({home.scene} if p.kind == "home" and home else set()), adult)
    curs = {}
    for g in pair.games:
        pre = pair.undo[g] + p.cal["before"].get(g, [])
        body = [f"# caliente {pair.number}.{pair.played + 1}: reset of {g} before {p.name}", "mark caliente-reset"] + pre
        curs[g], t = play(pair, g, "\n".join(body + [bounce.warp(), "wait 60", "mark caliente-away"]) + "\n",
                          WARP_DONE, pair.t.warp, WARP_GAVE_UP)
        note = f"; undo {', '.join(pair.undo[g])}" if pair.undo[g] else ""
        lines.append(f"reset {g}: warp {bounce.scene} done in {t:.0f} s{note}")
        pair.undo[g] = []
        if p.kind == "home":
            curs[g], t = play(pair, g, f"# caliente: {g} home before {p.name}\n{home.warp()}\nwait 60\nmark caliente-home\n",
                              WARP_DONE, pair.t.warp, WARP_GAVE_UP)
            lines.append(f"reset {g}: home {home.scene} {home.start} in {t:.0f} s")
            if home.at:
                c, t = play(pair, g, "place {:.2f} {:.2f} {:.2f}\nwait 30\nmark caliente-placed\n".format(*home.at),
                            r"coop: test place \S+ \S+ \S+: (done|failed)", 30)
                if re.search(r"coop: test place \S+ \S+ \S+: failed", c.text()):
                    raise Bad(f"{g}: place {home.at} failed")
                lines.append(f"reset {g}: placed at {home.at}")
    t0 = time.time()
    while True:  # the reloads' fps dip
        if all(len(f := rp.fps_in(curs[g].text().rsplit(WARP_DONE, 1)[-1], "FPS:")) >= 3
               and min(f[-3:]) >= oi.MIN_FPS for g in pair.games):
            lines.append(f"reset: settled in {time.time() - t0:.0f} s")
            return
        if not pair.alive():
            raise Bad("a game or the server exited after the reset")
        if time.time() - t0 > pair.t.settle:
            raise Bad(f"frozen after the reset (no 3 fps lines >= {oi.MIN_FPS} in {pair.t.settle:.0f} s)")
        time.sleep(pair.t.poll)


@dataclass
class Outcome:
    line: str                 # the summary's 'RESULT: ...'
    ok: bool
    bad: str = ""             # a bad state (the pair is closed, the scenario runs cold)
    dirty: list = field(default_factory=list)
    seconds: float = 0.0


def run_hot(pair: Pair, p: Plan, home: Home | None, out: Path) -> Outcome:
    """One scenario in the running pair; writes <name>_resumen.txt and its part of each log into out."""
    t0 = time.time()
    lines = [f"R0.3 {p.name} {time.strftime('%Y-%m-%dT%H:%M:%S')} caliente: boot {pair.number}, scenario "
             f"{pair.played + 1} in it, kind {p.kind}"] + p.saves_lines
    summary = out / f"{p.name}_resumen.txt"
    curs, srv = {}, None

    def save(result: str, ok: bool, bad: str = "", dirty=()) -> Outcome:
        for g, c in curs.items():
            (out / f"{p.name}_{g}_eden_log.txt").write_text(c.text(), encoding="utf-8")
        if srv is not None:
            (out / f"{p.name}_server.log").write_text(srv.text(), encoding="utf-8")
        lines.append(result)
        summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return Outcome(result, ok, bad, list(dirty), time.time() - t0)

    try:
        reset(pair, p, home, lines)
        pair.played += 1
        stagger = p.cal["stagger"] if p.cal["stagger"] is not None else pair.t.stagger
        deadline = time.time() + p.seconds + stagger + pair.t.margin
        srv = Cursor(pair.server_log)
        curs = {g: Cursor(pair.log(g)) for g in pair.games}
        head = f"# caliente {pair.number}.{pair.played}: {p.name}\n"
        pair.write("A", head + p.run["A"])
        if "B" in pair.games:
            if p.kind == "warp":
                t = wait_text(pair, {"A": curs["A"]}, p.start, p.timeout, WARP_GAVE_UP)
                lines.append(f"A started ('{p.start}'): ok in {t:.0f} s")
                time.sleep(stagger)
            pair.write("B", head + p.run["B"])
            if p.kind == "warp":
                t = wait_text(pair, {"B": curs["B"]}, p.start, p.timeout, WARP_GAVE_UP)
                lines.append(f"B started ('{p.start}'): ok in {t:.0f} s")
        marks = {g: _last_mark(p.run[g]) for g in pair.games}
        progress, seen = time.time(), -1
        while True:
            if not pair.alive():
                raise Bad("a game or the server exited during the scenario")
            texts = {g: c.text() for g, c in curs.items()}
            n = sum(len(rp.MARK.findall(t)) for t in texts.values())
            if n != seen:
                seen, progress = n, time.time()
            left = [g for g, m in marks.items() if m and not re.search(rf"coop: test mark {re.escape(m)}\b", texts[g])]
            if not left:
                break
            if time.time() - progress > p.timeout:
                raise Bad(f"no new mark in {p.timeout} s (waiting for {', '.join(f'{g} {marks[g]}' for g in left)})")
            if time.time() > deadline:
                raise Bad(f"over its time ({p.seconds + stagger + pair.t.margin:.0f} s)")
            time.sleep(pair.t.poll)
        lines.append("last marks: " + ", ".join(f"{g} '{m}'" for g, m in marks.items() if m)
                     + f" in {time.time() - t0:.0f} s")
        end = time.time() + pair.t.tail  # the freeze verdict reads the last 20 s
        while time.time() < end:
            if not pair.alive():
                raise Bad("a game or the server exited after the scenario")
            time.sleep(pair.t.poll)
    except Bad as e:
        return save(f"RESULT: FAIL (bad state: {e})", False, bad=str(e))
    texts = {g: c.text() for g, c in curs.items()}
    ok, slow = True, []
    a_marker = rp.first_mark_marker(p.texts["A"])
    for g in pair.games:
        good, more, is_slow = rp.game_checks(g, texts[g], p.texts[g] or "", a_marker, p.sopt["min_fps"],
                                             f"{p.name}_{g}_eden_log.txt (part of {pair.log(g)})")
        ok = ok and good
        lines += more
        slow += [g] if is_slow else []
    bad = "; ".join(l.split(": ", 1)[1].split(" (", 1)[0] + f" ({l[0]})" for l in lines
                    if re.match(r"^[AB]: (NEVER STARTED|FROZEN|WARP NOT TAKEN)", l))
    directives = rp.parse_directives([p.texts[g] for g in "AB" if p.texts[g] is not None])
    skipped = [d for d in directives if d["game"] and d["game"] not in pair.games and d["game"] not in ("bots", "server")]
    if skipped:
        lines.append(f"expect: {len(skipped)} directives for a game not run here skipped ({p.players} player)")
        directives = [d for d in directives if d not in skipped]
    failed = []
    if directives:
        # the boot's lines, then this scenario's: its window starts at its own 'test script started' (the last one)
        boot = getattr(pair, "boot_text", {})
        res = rp.evaluate_directives(directives, dict({g: boot.get(g, "") + t for g, t in texts.items()},
                                                      server=srv.text(), bots=None))
        lines += [l for _, l in res]
        failed = [l.split(": ", 1)[1] for good, l in res if not good]
        lines.append(f"expect: {len(res) - len(failed)}/{len(res)} ok")
        ok = ok and not failed
    dirty = []
    for g in pair.games:
        undo, notes, d = leftovers(texts[g])
        pair.undo[g] = undo
        lines += [f"left by {g}: {n}" for n in notes] + ([f"undo next for {g}: {', '.join(undo)}"] if undo else [])
        dirty += [f"{g}: {x}" for x in d]
    if p.cal["after_cold"]:
        dirty.append("'#! caliente despues frio'")
    if dirty:
        lines.append("dirty (the pair is closed after it): " + "; ".join(dirty))
    result = ("RESULT: " + ("PASS" if ok else "FAIL") + (" (SLOW: " + ",".join(slow) + ")" if ok and slow else "")
              + (f" (expect: {len(failed)} failed: " + "; ".join(failed)[:300] + ")" if failed else ""))
    return save(result, ok, bad=bad, dirty=dirty)


def _last_mark(text: str) -> str | None:
    marks = re.findall(r"^\s*mark\s+(\S+)", text, re.M)
    return marks[-1] if marks else None


# ------------------------------------------------------------------------------------------ cold runs

def run_cold(p: Plan, a, out: Path) -> Outcome:
    """One scenario as secuencia.ps1 runs it: r03_pareja.py with games started for it; its summary and logs copied."""
    t0 = time.time()
    oi.wait_no_instances(a.wait_games)
    cmd = [sys.executable, "-I", str(HERE / "r03_pareja.py"), "--scenario", p.name, "--scen-dir", str(a.scen_dir),
           "--players", str(p.players), "--transport", p.transport, "--timeout", str(p.timeout)]
    for e in p.cli_env:
        cmd += ["--env", e]
    with open(out / "r03_frio.log", "a", encoding="utf-8") as log:
        code = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, creationflags=oi.NO_WINDOW).returncode
    res = REPORTS / f"{p.name}_resumen.txt"
    text = res.read_text(encoding="utf-8", errors="replace") if res.exists() else ""
    found = re.findall(r"^RESULT.*$", text, re.M)
    line = found[-1] if found else f"RESULT: NO SUMMARY (exit {code})"
    for f in (f"{p.name}_resumen.txt", f"{p.name}_server.log"):
        if (REPORTS / f).exists():
            shutil.copyfile(REPORTS / f, out / f)
    for k, g in ((1, "A"), (2, "B"))[:p.players]:
        lg = oi.INST / f"n{k}" / "user/log/eden_log.txt"
        if lg.exists():
            shutil.copyfile(lg, out / f"{p.name}_{g}_eden_log.txt")
    return Outcome(line, "RESULT: PASS" in line, seconds=time.time() - t0)


def keep_hot_files(name: str, out: Path) -> None:
    """Before a cold run of a scenario that ran hot: its hot files become <name>_caliente_*."""
    for f in out.glob(f"{name}_*"):
        if not f.name.startswith(f"{name}_caliente_"):
            f.replace(out / f.name.replace(f"{name}_", f"{name}_caliente_", 1))


# ------------------------------------------------------------------------------------------ the run

def expect_tag(line: str, summary: Path) -> str:
    """secuencia.ps1's 'RESULT: PASS (expect 3/3)' from the scenario summary's 'expect: n/m ok'."""
    text = summary.read_text(encoding="utf-8", errors="replace") if summary.exists() else ""
    m = re.findall(r"^expect: (\d+/\d+) ok", text, re.M)
    return re.sub(r"^(RESULT: \w+)", rf"\1 (expect {m[-1]})", line) if m else line


def run_all(plans: list[Plan], a, out: Path, sum_path: Path, timing: Timing, make_pair=Pair, cold=run_cold,
            hot=run_hot, homes=None) -> dict:
    """Every scenario: the hot families, then the cold ones. Appends one line per scenario to sum_path."""
    stats = {"pass": 0, "fail": 0, "boots": 0, "boot_s": 0.0, "hot": 0, "cold": 0}
    homes = homes or {}

    def add(p: Plan, o: Outcome, mode: str) -> None:
        line = expect_tag(o.line, out / f"{p.name}_resumen.txt")
        stats["pass" if "RESULT: PASS" in line else "fail"] += 1
        with open(sum_path, "a", encoding="utf-8") as f:
            f.write(f"{p.name:<16} {line:<28} {o.seconds:5.0f} s  [{mode}]\n")

    def note(text: str) -> None:
        with open(sum_path, "a", encoding="utf-8") as f:
            f.write(text + "\n")

    for p in plans:
        if p.error:
            add(p, Outcome(p.error, False), "not run")
    fams, colds = families(plans)
    stopped = False
    for fam in fams:
        pair = None
        home, home_note = homes.get(fam[0].family(), (None, ""))
        for p in fam:
            if STOP.exists():
                note(f"STOP file: stopped before {p.name}")
                stopped = True
                break
            if p.kind == "home" and home is None:
                stats["cold"] += 1
                add(p, cold(p, a, out), f"frio: no home for this save ({home_note})")
                continue
            if pair is None:
                stats["boots"] += 1
                t0 = time.time()
                if make_pair is Pair:
                    ready, why = oi.ready_for_games() if oi.wait_no_instances(a.wait_games) else \
                        (False, "a game from another test is still running")
                else:
                    ready, why = True, ""
                pair = make_pair(p, out, stats["boots"], a, timing)
                blines = [] if ready else [f"boot {stats['boots']}: not ready: {why}"]
                good = ready and pair.boot(blines)
                stats["boot_s"] += time.time() - t0
                for l in blines:
                    note("   " + l)
                if not good:
                    pair.shutdown()
                    pair = None
                    stats["cold"] += 1
                    add(p, cold(p, a, out), "frio: the boot failed")
                    continue
            o = hot(pair, p, home, out)
            if o.bad or (not o.ok and a.recheck):
                pair.shutdown()
                pair = None
                keep_hot_files(p.name, out)
                c = cold(p, a, out)
                stats["cold"] += 1
                why = f"bad state: {o.bad}" if o.bad else f"hot {o.line.removeprefix('RESULT: ')[:120]}"
                add(p, Outcome(c.line, c.ok, seconds=o.seconds + c.seconds), f"frio tras caliente: {why}")
                continue
            stats["hot"] += 1
            add(p, o, "caliente" + (f"; boot again: {'; '.join(o.dirty)}" if o.dirty else ""))
            if o.dirty or (a.max_log_mb and pair.log_mb() > a.max_log_mb) or \
                    (a.restart_every and pair.played >= a.restart_every):
                pair.shutdown()
                pair = None
        if pair is not None:
            pair.shutdown()
        if stopped:
            break
    for p in colds:
        if stopped or STOP.exists():
            if not stopped:
                note(f"STOP file: stopped before {p.name}")
            break
        stats["cold"] += 1
        add(p, cold(p, a, out), f"frio: {p.cold}")
    return stats


def take_lock(wait_min: float, note) -> bool:
    t0, told = time.time(), False
    while True:
        if not STOP.exists():
            try:
                os.mkdir(LOCK)
                (LOCK / "owner.txt").write_text(f"caliente.py pid {os.getpid()} {time.strftime('%Y-%m-%dT%H:%M:%S')}\n",
                                                encoding="utf-8")
                return True
            except FileExistsError:
                pass
        if time.time() - t0 >= wait_min * 60:
            return False
        if not told:
            note(f"waiting for the game lock {LOCK}" + (" and for the STOP file to go" if STOP.exists() else ""))
            told = True
        time.sleep(30)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="hot instances: one pair of games plays a chain of scenarios")
    ap.add_argument("--tag", default="caliente_" + time.strftime("%Y%m%d"))
    ap.add_argument("--only", action="append", default=[], help="scenario names (commas or repeated); default all")
    ap.add_argument("--scen-dir", default=str(rp.ESC))
    ap.add_argument("--players", type=int, choices=(1, 2), default=None)
    ap.add_argument("--transport", choices=("udp", "ws"), default=None)
    ap.add_argument("--timeout", type=int, default=None, help="seconds each step may wait (a scenario's '#! timeout' "
                    "when not given; with --header-switches the longer of the two)")
    ap.add_argument("--switch", action="append", default=[], help="hook switch K=V for every scenario (secuencia's "
                    "-Switches, translated by protocolo.hook_switches)")
    ap.add_argument("--header-switches", action="store_true", help="also each scenario's '#! switches' (secuencia "
                    "-HeaderSwitches)")
    ap.add_argument("--saves", default=None, help="save folder for every game, over the scenarios' '#! saves'")
    ap.add_argument("--home", default="", help="where the scenarios without a warp start: 'SCENE START [adult|child] "
                    "[X Y Z]' (default the save's recipe, or Kokiri Forest for the default save)")
    ap.add_argument("--bots", type=int, default=0, help=argparse.SUPPRESS)
    ap.add_argument("--port", type=int, default=27560)
    ap.add_argument("--res", type=int, default=1)
    ap.add_argument("--stagger", type=float, default=Timing.stagger, help="seconds between A's start and B's scenario")
    ap.add_argument("--no-recheck", dest="recheck", action="store_false", help="a hot FAIL with healthy games is the "
                    "result (default: it runs cold too and the cold result counts)")
    ap.add_argument("--restart-every", type=int, default=0, help="boot again after N scenarios (0: never)")
    ap.add_argument("--max-log-mb", type=float, default=800, help="boot again once a game log passes this")
    ap.add_argument("--wait-games", type=float, default=1800, help="seconds to wait for another test's games to end")
    ap.add_argument("--no-lock", action="store_true", help="the game lock is already held (rapida_t9.ps1)")
    ap.add_argument("--lock-wait", type=float, default=60, help="minutes to wait for the game lock")
    ap.add_argument("--plan", action="store_true", help="write the plan and the hot scripts; start nothing")
    a = ap.parse_args(argv)
    a.scen_dir = str(Path(a.scen_dir).resolve())
    out = REPORTS / a.tag
    out.mkdir(parents=True, exist_ok=True)
    sum_path = out / "resumen.txt"
    timing = Timing(stagger=a.stagger)
    head = (f"secuencia {a.tag} started {time.strftime('%Y-%m-%dT%H:%M:%S')} caliente (hot instances) transport "
            f"{a.transport or '-'} players {a.players or '-'} scenarios {a.scen_dir} stack {os.environ.get('ORM_STACK') or '-'} "
            f"build {oi.BUILD_BIN} server {rp.SERVER} instances {oi.INST} switches {','.join(a.switch)}")
    if oi.STACK_PROBLEMS and not a.plan:
        sum_path.write_text("\n".join([head] + [f"problem: {x}" for x in oi.STACK_PROBLEMS] + ["RESULT: NOT RUN"]) + "\n",
                            encoding="utf-8")
        return 1
    try:
        override = parse_home(a.home) if a.home else None
    except ValueError as e:
        sum_path.write_text(f"{head}\nproblem: {e}\nRESULT: NOT RUN\n", encoding="utf-8")
        return 1
    names = scenario_names(Path(a.scen_dir), a.only)
    plans = [make_plan(n, Path(a.scen_dir), a, a.switch) for n in names]
    fams, colds = families(plans)
    lines = [head, f"{len(names)} scenarios: {' '.join(names)}"]
    homes = {}
    for i, fam in enumerate(fams, 1):
        p = fam[0]
        homes[p.family()] = family_home(p.saves_value, p.saves.get("A"), override)
        h, hn = homes[p.family()]
        est = sum(x.seconds for x in fam) / 60
        lines.append(f"family {i}: {len(fam)} hot ({p.players} games, {p.transport}, saves {p.saves_value or 'default'}, "
                     f"home {h.warp() if h else '-'} [{hn}], switches {','.join(dict.fromkeys(p.env)) or '-'}; "
                     f"scripts {est:.0f} min): "
                     + " ".join(x.name for x in fam))
    if colds:
        lines.append(f"cold: {len(colds)}: " + "; ".join(f"{p.name} ({p.cold})" for p in colds))
    sum_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "r03_frio.log").write_text("", encoding="utf-8")
    if a.plan:
        g_dir = out / "guiones"
        g_dir.mkdir(exist_ok=True)
        for p in plans:
            for g, t in p.run.items():
                if not p.cold and not p.error:
                    (g_dir / f"{p.name}.{g}.txt").write_text(t, encoding="utf-8")
        with open(sum_path, "a", encoding="utf-8") as f:
            f.write(f"plan only: hot scripts in {g_dir}\nRESULT: NOT RUN (--plan)\n")
        return 0
    if not a.no_lock and not take_lock(a.lock_wait, lambda x: open(sum_path, "a", encoding="utf-8").write(x + "\n")):
        with open(sum_path, "a", encoding="utf-8") as f:
            f.write(f"no game lock after {a.lock_wait:.0f} min\nRESULT: NOT RUN\n")
        return 1
    t0 = time.time()
    try:
        s = run_all(plans, a, out, sum_path, timing, homes=homes)
    finally:
        if not a.no_lock:
            shutil.rmtree(LOCK, ignore_errors=True)
    with open(sum_path, "a", encoding="utf-8") as f:
        f.write(f"total: {s['pass']} PASS, {s['fail']} FAIL, {(time.time() - t0) / 60:.0f} min\n"
                f"caliente: {s['hot']} hot, {s['cold']} cold, {s['boots']} boots ({s['boot_s'] / 60:.0f} min booting)\n"
                + ("" if a.no_lock else "game lock given back\n") + f"DONE {time.strftime('%Y-%m-%dT%H:%M:%S')}\n")
    return 0 if s["fail"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
