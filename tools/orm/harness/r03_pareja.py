"""Pair harness: two-game co-op on one PC, checked from the logs (hidden windows, no focus, no sound).

    python -I tools/orm/harness/r03_pareja.py [--scenario walk] [--saves <dir>]

Server (daurosk coop_server) + two hidden eden-cli instances with the co-op hook, both join, play daurosk's load.txt
(title -> Continue), then <scenario>.A.txt / .B.txt. Saves: a copy of your base save folder (ORM_BASE_SAVES), only read. A scenario file's `#! saves <block>` gives that game the synthetic save
of the block (tools/orm/saves, built on demand); --saves gives every game one folder. Summary:
<ORM_REPORTS>/pareja/<scenario>_resumen.txt

Paths: see orm_paths.py (README.md of tools/orm).
"""
import argparse, math, os, re, shutil, statistics, subprocess, sys, threading, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import orm_inst as oi
import orm_paths

# The source tree (ORM_TREE): holds tools/autotest/scenarios (load.txt) and tools/release/kit/*.txt.
DAUROSK = orm_paths.TREE
# The server (orm_server, or the upstream coop_server with --server-exe) and the fake-player bot (--bots):
# ORM_SERVER_EXE / ORM_BOT_EXE (orm_paths.py).
SERVER = orm_paths.SERVER_EXE
BOT = orm_paths.BOT_EXE
SCEN = DAUROSK / "tools/autotest/scenarios"  # load.txt comes from here
ESC = Path(__file__).resolve().parent / "escenarios"  # our scenario files
BASE_SAVES = orm_paths.BASE_SAVES  # a real save folder the recipes start from (read only)
SWITCHES = {"LICHII_COOP_PUPPET": "1", "LICHII_COOP_OBJECTS": "1", "LICHII_COOP_DEBUG_OBJECTS": "1"}
NOLOAD_STAGGER_S = 60  # a -NoLoad scenario reaches the world ~50 s after its start (title, Continue, load)
FROZEN_FPS = 10  # a frozen game reads 0-10; a busy scene with two games on one PC reads 12-20 (SLOW, a warning)
KEEP = re.compile(r"coop: (welcomed|game build|player |server |puppet|test mark|test script|connection|stopped|"
                  r"objects: copy|ENet|on|off)|CRITICAL|Critical")


MARK = re.compile(r"coop: test mark (\S+)")
# A mark in any log a directive reads: the games' 'coop: test mark X', a bot's 'botN: mark X' (remake-bot -script)
ANY_MARK = re.compile(r"(?:coop: test mark|: mark) (\S+)")
LOGS = ("A", "B", "bots", "server")  # what a directive may name: a game, the bots' log, the server's log


def new_marks(text: str, done: int) -> list[str]:
    """The `coop: test mark <name>` names in the log text, in order, after the first `done` ones."""
    return MARK.findall(text)[done:]


def capture_thread(pid: int, root: Path, rep: Path, scenario: str, marks: bool, every: int, stop, shots: list) -> None:
    """Polls A's log: a PNG of A's window per new test mark (<scenario>_A_<mark>.png) and, with every > 0, one every
    `every` seconds (<scenario>_A_t<seconds>.png, seconds since this thread started)."""
    cap = Path(__file__).with_name("captura.py")
    done, t0, last = 0, time.time(), time.time()

    def shot(name: str) -> None:
        png = rep / f"{scenario}_A_{name}.png"
        cp = subprocess.run([sys.executable, "-I", str(cap), str(pid), str(png)], capture_output=True, text=True)
        shots.append(f"{png.name}: {cp.stdout.strip() or cp.stderr.strip()[-100:]}")

    while not stop.is_set():
        if marks:
            for m in new_marks(oi.log_text(root), done):
                done += 1
                shot(m)
        if every and time.time() - last >= every:
            last = time.time()
            shot(f"t{int(last - t0)}")
        stop.wait(0.5)


def last_mark(script: Path) -> str | None:
    marks = re.findall(r"^\s*mark\s+(\S+)", script.read_text(encoding="utf-8"), re.M)
    return marks[-1] if marks else None


def scenario_plan(texts: list[str]) -> tuple[bool, str]:
    """(noload, start regex) from the scenario files' text (A, and B when it exists).

    Same rule as daurosk's run.ps1: a `#! noload` header line means the script loads the save itself; kept from before:
    "-NoLoad" anywhere in the text (the older header note), and a `warp` command before the first `press` (the warp is
    asked before the save loads, the game takes it only in the event after the load). `#! start REGEX` says what the log
    must match once the scenario has started; a script that loads itself defaults to the test warp being done, and one
    with no warp at all (late, epona, crawl: they press through the title themselves) to its script having started (a
    warp that is never asked never logs 'test warp done')."""
    noload, start, warps = False, None, False
    for t in texts:
        if re.search(r"^#!\s*noload\s*$", t, re.M) or "-NoLoad" in t:
            noload = True
        m = re.search(r"^#!\s*start\s+(.+?)\s*$", t, re.M)
        if m and start is None:
            start = m.group(1)
        body = [l.split("#", 1)[0].strip() for l in t.splitlines()]
        cmds = [l.split()[0] for l in body if l and not l.startswith("mark")]
        warps = warps or "warp" in cmds
        if "warp" in cmds and ("press" not in cmds or cmds.index("warp") < cmds.index("press")):
            noload = True
    if start is None:
        start = "coop: test script started" if noload and not warps else "coop: test warp done"
    return noload, start


def tree_path(value: str) -> str:
    """A switch value that names a file of the source tree as `tools/release/kit/<file>` (how scenario files write it)
    becomes the absolute path in ORM_TREE; anything else is returned as it is."""
    if re.match(r"^tools/(release/kit|autotest)/", value):
        return str((orm_paths.TREE / value).as_posix())
    return value


OPTION = re.compile(r"^#!\s*(players|bots|bot-arg|transport|env|switches|puppets|min-fps|timeout|join)\b(.*)$", re.M)
SEIS = {"players": 1, "bots": 5, "puppets": True, "min_fps": 25.0}  # seis-<block>: 1 game + 5 bots (PLAN_100 F14)


def scenario_options(name: str, texts: list[str]) -> dict:
    """The run options a scenario's files ask for (A, B and <scenario>.bots.txt), so `--scenario seis-kokiri` alone
    runs it as written (docs/BOTS.md):

        #! players N          games (1 or 2)
        #! bots N             fake players (remake-bot) after the games' load
        #! bot-arg ARGS...    more remake-bot arguments (split at spaces; repeatable)
        #! transport ws|udp   how the games reach the server
        #! env K=V            a hook switch (repeatable)
        #! switches K=V,K=V   the same, the form daurosk's run.ps1 reads
        #! puppets            FAIL (not a warning) when a game shows fewer puppets than the other players
        #! min-fps N          FAIL when a game's fps median from its first mark is under N
        #! timeout N          seconds each step may wait (as daurosk's run.ps1 reads it)
        #! join A-first|B-first   the second game starts only after the first has joined the room (default: both
                              start at once and the order is a race)
    A name starting with 'seis-' defaults to 1 game, 5 bots, puppets and min-fps 25 (F14). The command line wins
    over all of it. An unknown value is kept with 'errors' set (a FAIL, not ignored)."""
    o = {"players": None, "bots": None, "bot_args": [], "transport": None, "env": [], "puppets": False,
         "min_fps": None, "timeout": None, "join": None, "errors": []}
    if name.startswith("seis-"):
        o.update(SEIS)
    for t in texts:
        for m in OPTION.finditer(t):
            key, val = m.group(1), m.group(2).strip()
            try:
                if key in ("players", "bots"):
                    n = int(val)
                    if key == "players" and n not in (1, 2) or key == "bots" and not 0 <= n <= 5:
                        raise ValueError
                    o[key] = n
                elif key == "bot-arg":
                    o["bot_args"] += val.split()
                elif key == "transport":
                    if val not in ("ws", "udp"):
                        raise ValueError
                    o["transport"] = val
                elif key in ("env", "switches"):
                    for kv in (val.split(",") if key == "switches" else [val]):
                        if "=" not in kv or " " in kv.strip() or not kv.split("=", 1)[0].strip():
                            raise ValueError
                        o["env"].append(kv.strip())
                elif key == "join":
                    if val not in ("A-first", "B-first"):
                        raise ValueError
                    o["join"] = val[0]
                elif key == "puppets":
                    if val:
                        raise ValueError
                    o["puppets"] = True
                elif key == "timeout":
                    o["timeout"] = int(val)
                    if o["timeout"] < 1:
                        raise ValueError
                else:
                    o["min_fps"] = float(val)
            except ValueError:
                o["errors"].append(m.group(0).strip())
    return o


DIRECTIVE = re.compile(r"^#!\s*(expect|never|count)\b(.*)$", re.M)
WITHIN = re.compile(r"\s+within\s+(\S*)\.\.(\S*)\s*$")
COUNT_OP = re.compile(r"\s+(>=|<=|==)\s*(\d+)\s*$")
SCRIPT_START = "coop: test script started"


def parse_directives(texts: list[str], games: str = "AB") -> list[dict]:
    """The content assertions of a scenario, from its files' text (A, and B when it exists), in order:

        #! expect [A|B] <regex> [within <mark1>..<mark2>]   the regex matches a line of that game's log
        #! never  [A|B] <regex> [within <mark1>..<mark2>]   no line matches
        #! count  [A|B] <regex> >=|<=|== N [within ...]      the number of matching lines
    A directive may sit in either file; the letter names the game whose log it reads, and without it the directive
    reads the log of the file's own game (texts[i] is game games[i]; the form of charla-mido). A lone capital other
    than A or B before the regex is a wrong letter (an error), not part of the regex. The lines are taken from the
    scenario's start (that game's last 'coop: test script started' line: load.txt runs before it), and `within`
    restricts them to after the first 'coop: test mark <mark1>' and before the next 'coop: test mark <mark2>' (an
    empty side: from the start / to the end); `within boot..` reads the whole log, from the game's start (what a
    session logs once when it joins, before the script). A line that does not parse is kept with 'error' set (a FAIL, not ignored).
    """
    out = []
    for own, t in zip(games, texts):
        for m in DIRECTIVE.finditer(t):
            kind, rest = m.group(1), m.group(2).strip()
            d = {"kind": kind, "raw": m.group(0).strip(), "game": "", "regex": "", "within": None, "op": None,
                 "n": None, "error": ""}
            out.append(d)
            other = re.match(r"(bots|server)(?:\s+(.*))?$", rest)
            if other:
                d["game"], body = other.group(1), other.group(2) or ""
                rest = None
            g = re.match(r"([A-Z])(?:\s+(.*))?$", rest) if rest is not None else None
            if g and g.group(1) not in "AB":
                d["error"] = f"unknown game '{g.group(1)}' (A or B, or none for this file's game)"
                continue
            if rest is not None:
                d["game"], body = (g.group(1), g.group(2) or "") if g else (own, rest)
            w = WITHIN.search(body)
            if w:
                d["within"], body = (w.group(1), w.group(2)), body[:w.start()]
            if kind == "count":
                c = COUNT_OP.search(body)
                if not c:
                    d["error"] = "count needs '>= N', '<= N' or '== N' after the regex"
                    continue
                d["op"], d["n"], body = c.group(1), int(c.group(2)), body[:c.start()]
            d["regex"] = body.strip()
            try:
                re.compile(d["regex"])
            except re.error as e:
                d["error"] = f"bad regex: {e}"
            if not d["regex"]:
                d["error"] = "empty regex"
    return out


def scenario_lines(text: str) -> list[str]:
    """The log lines from the scenario's start: from the last 'coop: test script started' line (the whole log when
    there is none)."""
    lines = text.splitlines()
    starts = [i for i, l in enumerate(lines) if SCRIPT_START in l]
    return lines[starts[-1]:] if starts else lines


def _window(lines: list[str], within) -> tuple[list[str] | None, str]:
    """(lines, where) for a `within (m1, m2)`; None when mark1 never came."""
    if not within:
        return lines, "after start"
    m1, m2 = within
    marks = [(i, mm.group(1)) for i, l in enumerate(lines) for mm in [ANY_MARK.search(l)] if mm]
    lo = 0
    if m1:
        lo = next((i for i, n in marks if n == m1), None)
        if lo is None:
            return None, f"mark '{m1}' never reached"
    hi = next((i for i, n in marks if i > lo and n == m2), None) if m2 else None
    where = f"within {m1 or 'start'}..{m2 or 'end'}" + (f" (mark '{m2}' never reached: to the end)" if m2 and hi is None else "")
    return lines[lo:hi], where


def evaluate_directives(directives: list[dict], logs: dict[str, str | None]) -> list[tuple[bool, str]]:
    """(ok, 'expect ok|FAIL: <what> (<where>)') per directive; logs maps 'A'/'B' to that game's whole log text (None:
    no log)."""
    res = []
    for d in directives:
        what = (f"{d['kind']} {d['game']} {d['regex']}" if d["kind"] != "count"
                else f"count {d['game']} {d['regex']} {d['op']} {d['n']}") if not d["error"] else d["raw"]
        if d["error"]:
            res.append((False, f"expect FAIL: {what} ({d['error']})"))
            continue
        text = logs.get(d["game"])
        if text is None:
            res.append((False, f"expect FAIL: {what} (no log for {d['game']})"))
            continue
        # `within boot..`: the whole log, from the game's start (lines a session logs once as it joins, before
        # the script starts: "coop: talk: sharing N talk flags")
        if d["within"] and d["within"][0] == "boot":
            lines, where = (text.splitlines(), "whole log") if not d["within"][1] else (None, "boot..<mark> is not a window")
        else:
            lines, where = _window(scenario_lines(text), d["within"])
        if lines is None:
            res.append((False, f"expect FAIL: {what} ({d['game']}: {where})"))
            continue
        rx = re.compile(d["regex"])
        hits = sum(1 for l in lines if rx.search(l))
        if d["kind"] == "expect":
            ok = hits > 0
        elif d["kind"] == "never":
            ok = hits == 0
        else:
            ok = {">=": hits >= d["n"], "<=": hits <= d["n"], "==": hits == d["n"]}[d["op"]]
        got = f", {hits} lines" if d["kind"] != "expect" or not ok else ""
        res.append((ok, f"expect {'ok' if ok else 'FAIL'}: {what} ({d['game']} {where}{got})"))
    return res


def first_mark_marker(a_text: str) -> str:
    """The log line A's checks count from: A's first mark, else daurosk's load.txt 'loaded'."""
    first = re.findall(r"^\s*mark\s+(\S+)", a_text, re.M)
    return f"coop: test mark {first[0]}" if first else "coop: test mark loaded"


# ---- who joined first, the save each game really has, and the game that never starts (docs/TRIAGE_ROJOS.md, family b/a)

WELCOMED = re.compile(r"coop: welcomed as player (\d+)")
LIFE = re.compile(r"our Link's life (\d+) of (\d+), MaxLife flag (\d+)")
DEFAULT_MAXLIFE = 12  # the base save (BASE_SAVES) and the base every synthetic block starts from: 3 hearts
MAXLIFE_CHANGE = re.compile(r"PlayerStatus\.MaxLife: \d+ -> (\d+)")


def join_order(texts: dict) -> dict:
    """{game: player number} from each game's 'coop: welcomed as player N' (the first one; a game not welcomed is absent)."""
    out = {}
    for g, t in texts.items():
        m = WELCOMED.search(t or "")
        if m:
            out[g] = int(m.group(1))
    return out


def join_line(texts: dict) -> str:
    """The summary line that says which game joined first (player 1) and which second, read from the logs."""
    order = join_order(texts)
    if not order:
        return "join order: unknown (no 'welcomed as player' in the logs)"
    by_player = sorted(order.items(), key=lambda kv: kv[1])
    names = ["first", "second", "third"]
    parts = [f"{g} player {n} ({names[i] if i < len(names) else str(i + 1) + 'th'})" for i, (g, n) in enumerate(by_player)]
    missing = [g for g in texts if g not in order]
    return "join order: " + ", ".join(parts) + (f"; not welcomed: {', '.join(missing)}" if missing else "")


def save_maxlife(folder: Path, default: Path = BASE_SAVES, root: Path | None = None) -> int | None:
    """The MaxLife (quarter hearts) a save folder holds, read from what tools/orm/saves knows: the default save and a
    block's base are DEFAULT_MAXLIFE; a built block's <block>.partida.txt lists its last 'PlayerStatus.MaxLife: a -> b'.
    None when it cannot be known (a real save or a folder with no manifest)."""
    folder = Path(folder)
    if folder == Path(default):
        return DEFAULT_MAXLIFE
    manifest = folder.parent / (folder.name + MANIFEST) if root is None else Path(root) / (folder.name + MANIFEST)
    if not manifest.exists():
        return None
    changes = MAXLIFE_CHANGE.findall(manifest.read_text(encoding="utf-8", errors="replace"))
    return int(changes[-1]) if changes else DEFAULT_MAXLIFE


def life_check(name: str, text: str, expected: int | None) -> tuple[bool, str]:
    """(ok, summary line) comparing the game's first 'our Link's life N of M, MaxLife flag K' with the MaxLife of the save
    the scenario gave it. A mismatch is the wrong-save bug (orm/t9-integrado 3614aa9): the guest that joined second kept
    the title's table instead of its own save."""
    m = LIFE.search(text or "")
    if expected is None:
        return True, f"-- {name}: life check skipped (the save's hearts are not known)"
    if not m:
        return True, f"-- {name}: life check skipped (no 'our Link's life' line in the log)"
    n, mx, flag = (int(x) for x in m.groups())
    if mx != expected or flag != expected:
        return False, (f"{name}: save mismatch: {name} has {n} of {mx}, its save has {expected}"
                       + (f" (MaxLife flag {flag})" if flag != mx else ""))
    return True, f"-- {name}: first life {n} of {mx} = its save's {expected} (ok)"


class StartWatch:
    """The NEVER STARTED watchdog. A game is armed when a script is written to its script.txt; it must log one more
    'coop: test script started' within `timeout` seconds, else it is still on the title screen (or hung) and the run
    ends at once. poll() makes it usable as a process in oi.wait_log's `procs` (not None = something died)."""

    def __init__(self, timeout: float, read_log, clock=time.time, every: float = 5.0):
        self.timeout, self.read_log, self.clock, self.every = timeout, read_log, clock, every
        self.armed: dict = {}      # game -> (time written, starts logged by then)
        self.late: list = []       # (game, seconds waited)
        self._last = 0.0

    def arm(self, game: str) -> None:
        self.armed[game] = (self.clock(), (self.read_log(game) or "").count(SCRIPT_START))

    def check(self) -> list:
        now = self.clock()
        for g, (t, n) in list(self.armed.items()):
            if (self.read_log(g) or "").count(SCRIPT_START) > n:
                del self.armed[g]
            elif now - t >= self.timeout and all(g != x for x, _ in self.late):
                self.late.append((g, now - t))
        return self.late

    def poll(self):
        if self.late:
            return 1
        now = self.clock()
        if now - self._last >= self.every:
            self._last = now
            self.check()
        return 1 if self.late else None

    def lines(self) -> list:
        return [f"{g}: NEVER STARTED (script.txt written {s:.0f} s ago, no '{SCRIPT_START}': still on the title screen "
                f"or hung; run ended at once)" for g, s in self.late]


def fps_in(text: str, marker: str) -> list[float]:
    """The fps of the 'FPS:' lines after the first `marker` in a log text (orm_inst.fps_after on a text)."""
    i = text.find(marker)
    return [float(a) for a, _ in oi.FPS.findall(text[i:] if i >= 0 else "")]


def game_checks(name: str, text: str, own_text: str, a_marker: str, min_fps: float | None,
                log_label) -> tuple[bool, list[str], bool]:
    """(ok, summary lines, slow) for one game's log text after a scenario: started, not frozen, fps, the warps taken,
    the kept lines, the pose probe and 'you at' (r03_pareja's verdict; caliente.py runs it on one scenario's part of
    a log that holds many)."""
    ok, lines, slow = True, [], False
    # each game from the first mark of its own script (B's need not have A's marks; -NoLoad has no 'loaded')
    own = re.findall(r"^\s*mark\s+(\S+)", own_text, re.M)
    cands = ([f"coop: test mark {own[0]}"] if own else []) + [a_marker, "coop: test mark loaded"]
    marker = next((c for c in cands if c in text), "coop: test script started")
    fps = fps_in(text, marker)
    tail = fps[-10:]
    med = statistics.median(tail) if tail else 0
    if not fps:
        # no fps line after the marker: the game never got that far (refused, crashed, never joined), not frozen
        ok = False
        reason = next((l.strip() for l in text.splitlines() if "coop: server refused" in l or "coop: refused" in l), "")
        lines.append(f"{name}: NEVER STARTED (no fps after '{marker}'){': ' + reason[-120:] if reason else ''}")
    elif len(tail) < 5 or med < FROZEN_FPS:
        ok = False
        lines.append(f"{name}: FROZEN at the end (fps median of last 20 s {med:.1f} < {FROZEN_FPS})")
    elif min_fps is not None and statistics.median(fps) < min_fps:
        ok = False
        lines.append(f"{name}: FPS UNDER {min_fps:g} (median {statistics.median(fps):.1f} after '{marker}')")
    elif med < oi.MIN_FPS:
        slow = True
        lines.append(f"{name}: SLOW at the end (fps median of last 20 s {med:.1f} < {oi.MIN_FPS}; warning, not a fail)")
    asked = text.count("coop: test warp to ")
    if asked and "coop: test warp done" not in text:  # the script goes on after a warp that never happened
        ok = False
        last_scene = (re.findall(r"scene (\S+)$", text, re.M) or ["?"])[-1]  # needs ORM_POSE_PROBE, else "?"
        lines.append(f"{name}: WARP NOT TAKEN (asked {asked} times, no 'test warp done'; scene {last_scene})")
    seen, keep = set(), []
    for l in text.splitlines():
        m = KEEP.search(l)
        if m:
            key = re.sub(r"[0-9.\-]+", "#", l.split("> ", 1)[-1])[:90]
            if key not in seen:
                seen.add(key)
                keep.append(l.split("> ", 1)[-1][:200])
    free = [float(x) for x in re.findall(r"([0-9.]+) GiB RAM free", text)]
    lines.append(f"-- {name}: fps after '{marker}' median {statistics.median(fps) if fps else 0:.0f} (n={len(fps)}), "
                 f"min RAM free {min(free) if free else -1:.1f} GB, "
                 f"critical {text.count('<Critical>')}, log {log_label}")
    lines += ["   " + k for k in keep[:30]]
    probe = re.findall(r"orm: pose probe frame \d+ sent ([-0-9.]+) ([-0-9.]+) ([-0-9.]+) "
                       r"actor ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)", text.split(marker, 1)[-1])
    if probe:  # ORM_POSE_PROBE (patches/daurosk/0001): the GameUI copy the host sends vs the actor's own position
        d = sorted(math.dist(tuple(map(float, p[:3])), tuple(map(float, p[3:]))) for p in probe)
        moved = math.dist(tuple(map(float, probe[0][3:])), tuple(map(float, probe[-1][3:])))
        med = d[len(d) // 2]
        if med > 0.1:
            ok = False
        lines.append(f"   pose probe: {len(d)} samples, sent-vs-actor distance median {med:.2f} max {d[-1]:.2f}, "
                     f"actor moved {moved:.1f} -> " + ("FAIL (the sent position does not follow the actor)"
                                                       if med > 0.1 else "ok"))
        # kAsMainSlot: a Link who moved went through more than one whole-body state
        anims = {m for m in re.findall(r"orm: pose probe .* anim (\S+) [-0-9.]+ [-0-9.]+ scene", text)}
        if moved > 2 and len(anims) < 2:
            ok = False
        # kBancPath*: a warp that was done shows up as a second scene name
        scenes = list(dict.fromkeys(s for s in re.findall(r"orm: pose probe .* scene (\S+)$", text, re.M)
                                    if s != "none"))
        warped = "coop: test warp done" in text
        if warped and len(scenes) < 2:
            ok = False
        lines.append(f"   pose probe: animations {sorted(anims)[:8]}{' (FAIL: Link moved, one state)' if moved > 2 and len(anims) < 2 else ''}; "
                     f"scenes {scenes}{' (FAIL: warp done, scene unchanged)' if warped and len(scenes) < 2 else ''}")
    you = re.findall(r"you at \(([-0-9.]+), ?([-0-9.]+), ?([-0-9.]+)\)", text.split(marker, 1)[-1])
    if you:  # LICHII_COOP_DEBUG_CAMERA: the GameUI position the host reads; it must follow the player
        pts = [tuple(float(v) for v in t) for t in you]
        span = max(max(p[i] for p in pts) - min(p[i] for p in pts) for i in range(3))
        lines.append(f"   you at: {len(pts)} samples, {len(set(pts))} distinct, largest axis span {span:.1f}; "
                     f"first {pts[0]}, last {pts[-1]}")
    return ok, lines, slow


BOT_ERROR = re.compile(r"(?m)^remake-bot: (?:bot\d+: script line \d+|bot\d+: (?:.*: )?(?:read|write|connect)[: ]|-)")


def bot_log_summary(text: str) -> str:
    """One line on the bots' log (remake-bot): joins (a reconnection is a second join), errors (a script line that
    failed, a broken connection), how many scripts ran to their end."""
    return (f"{text.count(' joined as player')} joins, {len(BOT_ERROR.findall(text))} errors, "
            f"script done by {text.count(': script done')}")


def check_bots(count: int, args: list[str], exe: Path | None = None) -> str:
    """'' when remake-bot takes these arguments and its -script for every one of the bots (its -check: nothing
    connects), else why not."""
    exe = exe or BOT
    if not exe.exists():
        return f"no bot program {exe} (cd server && go build -o bin/remake-bot.exe ./cmd/remake-bot)"
    cp = subprocess.run([str(exe), "-check", "-count", str(count)] + args, capture_output=True, text=True,
                        creationflags=oi.NO_WINDOW, timeout=60)
    if cp.returncode != 0:
        return (cp.stderr.strip() or cp.stdout.strip() or f"exit {cp.returncode}")[-300:]
    return ""


# `#! saves <block>|<folder>|default` (docs/ARNES.md, "Partidas por escenario"): the save folder one game starts from.
# A block is a recipe of tools/orm/saves/recetas (docs/PARTIDAS_SINTETICAS.md); its folder <ORM_SAVES_ROOT>/<block> is built
# with partida.py when it is missing or older than its recipe. ORM_SAVES_ROOT: another folder for the built saves.
SAVES_HEADER = re.compile(r"^#!\s*saves\b[ \t]*(.*?)\s*$", re.M)
PARTIDAS = Path(__file__).resolve().parents[1] / "saves"
RECIPES = PARTIDAS / "recetas"
SAVES_ROOT = orm_paths.SAVES_ROOT
MANIFEST = ".partida.txt"  # partida.py writes <block>.partida.txt beside each folder it built


class SavesError(ValueError):
    pass


def saves_header(text: str) -> str | None:
    """The value of the file's `#! saves` line: None without one, '' for a line with no value."""
    found = SAVES_HEADER.findall(text)
    if len(found) > 1:
        raise SavesError(f"more than one '#! saves' line ({', '.join(found)})")
    return found[0] if found else None


def known_blocks(recipes: Path = RECIPES) -> list[str]:
    return sorted(p.stem for p in recipes.glob("*.txt"))


def recipe_sha1(recipe: Path) -> str:
    """The sha1 partida.py writes as 'recipe sha1:' in its manifest: the recipe and every file it includes."""
    import hashlib
    sys.path.insert(0, str(PARTIDAS))
    import receta  # noqa: PLC0415 (tools/orm/saves, pure Python)
    try:
        files = receta.parse(str(recipe)).files
    except receta.RecipeError as e:
        raise SavesError(f"recipe {recipe.name}: {e}") from e
    return hashlib.sha1(b"".join(Path(p).read_bytes() for p in files)).hexdigest()


def built_sha1(manifest: Path) -> str | None:
    m = re.search(r"^recipe sha1: ([0-9a-f]{40})", manifest.read_text(encoding="utf-8"), re.M) if manifest.exists() else None
    return m.group(1) if m else None


def build_block(recipe: Path, out: Path) -> None:
    """partida.py build, with plain python (no -I: the romfs reader needs zstandard from the user site)."""
    cp = subprocess.run([sys.executable, str(PARTIDAS / "partida.py"), "build", str(recipe), "--out", str(out)],
                        capture_output=True, text=True, creationflags=oi.NO_WINDOW, timeout=600)
    if cp.returncode:
        tail = " / ".join((cp.stderr or cp.stdout).strip().splitlines()[-3:])
        raise SavesError(f"partida.py build {recipe.name} failed (exit {cp.returncode}): {tail[-300:]}")


def resolve_saves(value: str, default: Path, root: Path = SAVES_ROOT, recipes: Path = RECIPES,
                  build=build_block) -> tuple[Path, str]:
    """(save folder, a note for the summary) for one `#! saves` value; SavesError says why not."""
    if value in ("default", "base"):
        return default, "the default save"
    if not value:
        raise SavesError("'#! saves' needs a block (e.g. 12-bosque), a save folder or 'default'")
    if re.search(r"[\\/:]", value):
        folder = Path(value)
        if not folder.is_dir():
            raise SavesError(f"'#! saves {value}': no such save folder")
        return folder, "a save folder"
    recipe = recipes / f"{value}.txt"
    if not recipe.is_file():
        import difflib
        blocks = known_blocks(recipes)
        near = difflib.get_close_matches(value, blocks, n=3, cutoff=0.5)
        raise SavesError(f"unknown saves block '{value}'" + (f" (did you mean {', '.join(near)}?)" if near else "")
                         + f"; the blocks are the recipes of {recipes}: {', '.join(blocks)}")
    out, manifest = root / value, root / (value + MANIFEST)
    if out.is_dir() and not manifest.exists():
        return out, "a real save (no .partida.txt beside it): used as it is"  # a real save of the block wins
    if out.is_dir() and built_sha1(manifest) == recipe_sha1(recipe):
        return out, "built before from this recipe"
    why = "the recipe changed" if out.is_dir() else "missing"
    t0 = time.time()
    build(recipe, out)
    if not out.is_dir():
        raise SavesError(f"partida.py build {recipe.name} left no folder {out}")
    return out, f"built now with partida.py, {why} ({time.time() - t0:.0f} s)"


def plan_saves(texts: dict, default: Path, override: Path | None = None, **kw) -> tuple[dict, list[str]]:
    """{game: save folder} and summary lines, from each game's scenario text (None: no file for that game).
    A game whose file has no `#! saves` takes the other game's (both in the same point of the story); with none at all,
    `default`. An explicit --saves (override) wins over the headers."""
    vals = {g: (saves_header(t) if t is not None else None) for g, t in texts.items()}
    if override is not None:
        named = sorted({v for v in vals.values() if v is not None})
        return {g: override for g in texts}, [f"saves: --saves {override} for every game"
                                             + (f" (the scenario's '#! saves {', '.join(named)}' not used)" if named else "")]
    out, lines, done = {}, [], {}
    for g in texts:
        v = vals[g] if vals[g] is not None else next((vals[o] for o in texts if vals[o] is not None), None)
        if v is None:
            out[g] = default
            lines.append(f"saves {g}: {default} (no '#! saves')")
            continue
        if v not in done:
            done[v] = resolve_saves(v, default, **kw)
        out[g] = done[v][0]
        lines.append(f"saves {g}: {v} -> {done[v][0]} ({done[v][1]}" + (")" if vals[g] is not None else
                                                                        ", from the other game's file)"))
    return out, lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="walk")
    ap.add_argument("--saves", default=None, help=f"save folder for every game, over the scenario's '#! saves' "
                    f"(default: the scenario's '#! saves', else {BASE_SAVES})")
    ap.add_argument("--port", type=int, default=27560)
    ap.add_argument("--server-exe", default="", help=f"relay server to launch (default {SERVER})")
    ap.add_argument("--server-arg", action="append", default=[], help="extra orm_server argument (repeatable), "
                    "e.g. --server-arg=--log-stats-every-ms --server-arg=30000 (the relay's counters in its log)")
    ap.add_argument("--bots", type=int, default=None, help="fake players joining by WebSocket (server/bin/remake-bot.exe); "
                    "default the scenario's '#! bots N', else 0")
    ap.add_argument("--bot-arg", action="append", default=[], help="extra remake-bot argument (repeatable), e.g. "
                    "--bot-arg=-radius --bot-arg=0 --bot-arg=-center --bot-arg=-76,122,38 (a bot standing still)")
    ap.add_argument("--capture", action="store_true", help="show A's window (unfocused) and screenshot it once the bots' puppets are in")
    ap.add_argument("--capture-marks", action="store_true", help="show A's window (unfocused) and screenshot it at every "
                    "'coop: test mark <name>' in A's log: <scenario>_A_<name>.png")
    ap.add_argument("--capture-every", type=int, default=0, metavar="N", help="also a screenshot of A every N seconds "
                    "(<scenario>_A_t<seconds>.png); shows A's window like --capture-marks")
    ap.add_argument("--transport", choices=("udp", "ws"), default=None,
                    help="how the games reach the server: udp (ENet, daurosk's) or ws (WebSocket on port+1)")
    ap.add_argument("--timeout", type=int, default=None, help="seconds each step may wait (default the scenario's "
                    "'#! timeout N', else 240)")
    ap.add_argument("--res", type=int, default=1)
    ap.add_argument("--players", type=int, default=None, choices=(1, 2), help="games (default the scenario's "
                    "'#! players N', else 2)")
    ap.add_argument("--scen-dir", default=str(ESC), help="folder with <scenario>.A.txt (default: the escenarios "
                    "folder next to this file; the tree's tools/autotest/scenarios has daurosk's)")
    ap.add_argument("--env", action="append", default=[], help="extra hook switch K=V (repeatable), e.g. "
                    "LICHII_COOP_DEBUG_CAMERA=1 (logs 'you at' every 120 frames: the GameUI position read)")
    ap.add_argument("--join", choices=("A-first", "B-first"), default=None, help="the join order, over the scenario's "
                    "'#! join' (B-first: A joins second, the guest that loads before the host on the title screen)")
    a = ap.parse_args()
    scen = Path(a.scen_dir)
    files = [scen / f"{a.scenario}.{x}.txt" for x in ("A", "B", "bots")]
    sopt = scenario_options(a.scenario, [f.read_text(encoding="utf-8") for f in files if f.exists()])
    sopt["join"] = a.join[0] if a.join else sopt["join"]  # the command line wins over '#! join'
    a.players = a.players or sopt["players"] or 2
    a.bots = sopt["bots"] or 0 if a.bots is None else a.bots
    a.transport = a.transport or sopt["transport"] or "udp"
    a.timeout = a.timeout or sopt["timeout"] or 240
    a.env = sopt["env"] + a.env  # the command line's last: a repeated key takes its value
    bot_script = files[2] if files[2].exists() and a.bots else None
    a.bot_arg = sopt["bot_args"] + (["-script", str(bot_script)] if bot_script else []) + a.bot_arg
    rep = orm_paths.REPORTS / "pareja"
    rep.mkdir(parents=True, exist_ok=True)
    out = rep / f"{a.scenario}_resumen.txt"
    lines = [f"pair {a.scenario} {time.strftime('%Y-%m-%dT%H:%M:%S')}"]
    # each game's save from its file's `#! saves` (built now if missing), before anything is launched
    game_files = {g: scen / f"{a.scenario}.{g}.txt" for g in "AB"[:a.players]}
    try:
        saves, saves_lines = plan_saves({g: f.read_text(encoding="utf-8") if f.exists() else None
                                         for g, f in game_files.items()}, BASE_SAVES,
                                        Path(a.saves) if a.saves else None)
    except SavesError as e:
        lines.append(f"RESULT: FAIL (saves: {e})")
        out.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1
    lines += saves_lines
    ready, why = oi.ready_for_games()  # no leftover game, our models unloaded, enough free RAM
    lines.append(("ready: " if ready else "RESULT: FAIL (not ready: ") + why + ("" if ready else ")"))
    if not ready:
        out.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1
    lines[0] += f" res={a.res} players={a.players}" + (f" bots={a.bots}" if a.bots else "")
    if sopt["errors"]:
        lines.append("RESULT: FAIL (scenario options not understood: " + "; ".join(sopt["errors"]) + ")")
        out.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1
    if a.bots:
        # the bot reads its options and script before any game starts: a typo fails in a second, not after the load
        cp = check_bots(a.bots, a.bot_arg)
        lines.append(f"bots: {a.bots}, arguments {' '.join(a.bot_arg) or '(none)'}: " + (cp or "checked"))
        if cp:
            lines.append("RESULT: FAIL (the bots' options or script)")
            out.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return 1
    # --transport ws: the games reach orm_server by WebSocket (patch 0003/0004 in the hook) instead of ENet/UDP
    ws = a.transport == "ws"
    ini = (f"server = ws://127.0.0.1:{a.port + 1}/\r\n" if ws else f"server = 127.0.0.1\r\nport = {a.port}\r\n")
    # Protocol 110 (daurosk test10) lets nobody in without a join password: as the 0.12 kit, the server reads it from
    # an environment variable (--password-env) and each game from its coop.ini (password = ...)
    proto = oi.protocolo.protocol()
    password = oi.protocolo.HARNESS_PASSWORD if proto >= oi.protocolo.JOIN_PASSWORD_FROM else ""
    if password:
        ini += f"password = {password}\r\n"
        lines.append(f"join password: on (protocol {proto}: server --password-env "
                     f"{oi.protocolo.HARNESS_PASSWORD_ENV}, coop.ini password)")
    roots =[oi.prepare(k, saves=saves[n], coop_ini=ini + f"name = {n}\r\n", res=a.res)
             for k, n in ((1, "A"), (2, "B"))[:a.players]]
    scripts = [r / "script.txt" for r in roots]
    for s in scripts:
        s.write_text("", encoding="utf-8")
    srv_log = open(rep / f"{a.scenario}_server.log", "w", encoding="utf-8")
    server_exe = Path(a.server_exe) if a.server_exe else SERVER
    port_flag = "--udp-port" if "orm_server" in server_exe.name else "--port"
    if ws and port_flag != "--udp-port":
        lines.append("RESULT: FAIL (--transport ws needs orm_server)")
        rep.joinpath(f"{a.scenario}_resumen.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1
    ws_on = ws or a.bots > 0  # the bots always come in by WebSocket
    # --bind 127.0.0.1: listening on every interface makes Windows ask to allow the program through the firewall
    # --max-half-open: daurosk's server lets one address hold 4 connections waiting for HELLO; five bots from
    # 127.0.0.1 at once hit it (the fifth is closed at accept, and the bot program gives up as a whole)
    ws_args = (["--bind", "127.0.0.1", "--ws-port", str(a.port + 1) if ws_on else "off", "--max-half-open", "16"]
               if port_flag == "--udp-port" else [])
    if a.bots and port_flag != "--udp-port":
        lines.append("RESULT: FAIL (--bots needs orm_server)")
        rep.joinpath(f"{a.scenario}_resumen.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1
    bots = None
    pw_args = ["--password-env", oi.protocolo.HARNESS_PASSWORD_ENV] if password and port_flag == "--udp-port" else []
    server = subprocess.Popen([str(server_exe), port_flag, str(a.port)] + ws_args + pw_args + a.server_arg,
                              stdout=srv_log, stderr=subprocess.STDOUT, creationflags=oi.NO_WINDOW,
                              env={**os.environ, oi.protocolo.HARNESS_PASSWORD_ENV: password} if pw_args else None)
    extra = dict(e.split("=", 1) for e in a.env)
    extra = {k: tree_path(v) for k, v in extra.items()}  # a tree-relative path in a switch (see tree_path)
    def launch(i: int):
        return oi.launch(roots[i], dict(SWITCHES, **extra, LICHII_COOP_TEST_SCRIPT=str(scripts[i]),
                                        **({"EDEN_NX2_HIDE_WINDOW": "0"} if (a.capture or a.capture_marks or a.capture_every)
                                           and i == 0 else {})))

    # '#! join A-first|B-first': the second game starts only after the first has joined the room (else both at once)
    order = [0, 1][:a.players] if sopt["join"] != "B" or a.players < 2 else [1, 0]
    launched, join_failed = {}, False
    if sopt["join"] and a.players < 2:
        lines.append(f"join: '#! join {sopt['join']}-first' ignored (1 game)")
    if sopt["join"] and a.players == 2:
        launched[order[0]] = launch(order[0])
        t = oi.wait_log([roots[order[0]]], r"coop: welcomed as player", a.timeout, [launched[order[0]]])
        lines.append(f"join: {'AB'[order[0]]} first: joined " + (f"in {t:.0f} s, then {'AB'[order[1]]} started"
                                                                  if t is not None else "FAIL (timeout or it exited)"))
        join_failed = t is None
        if not join_failed:
            launched[order[1]] = launch(order[1])
    else:
        launched = {i: launch(i) for i in order}
    procs = [launched[i] for i in sorted(launched)]
    watch = StartWatch(a.timeout, lambda g: oi.log_text(roots["AB".index(g)]) if oi.log_path(roots["AB".index(g)]).exists() else "")
    wprocs = procs + [watch]  # the watchdog counts as a dead process for every wait below
    ok = not join_failed
    stop, shots = threading.Event(), []
    cthread = None
    if a.capture_marks or a.capture_every:
        cthread = threading.Thread(target=capture_thread, daemon=True, args=(
            procs[0].pid, roots[0], rep, a.scenario, a.capture_marks, a.capture_every, stop, shots))
        cthread.start()
    try:
        def step(name: str, pattern: str, timeout: int) -> bool:
            t = oi.wait_log(roots, pattern, timeout, wprocs)
            lines.append(f"{name}: " + (f"ok in {t:.0f} s" if t is not None else "FAIL (timeout or a game exited)"))
            out.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return t is not None

        ok = ok and step("both joined", r"coop: welcomed as player", a.timeout)
        # One world load at a time: each game commits ~10 GB of guest memory plus 3-8 GB of its own at the load peak;
        # two peaks together fill this PC's 31 GB and one game pages to disk (frozen 40 s or more, R0.3).
        sa, sb = scen / f"{a.scenario}.A.txt", scen / f"{a.scenario}.B.txt"
        # daurosk's "Run with -NoLoad" scenarios load the save themselves (a warp is taken only during the event
        # after a load): no load.txt first, as his run.ps1 -NoLoad
        noload, start = scenario_plan([f.read_text(encoding="utf-8") for f in (sa, sb) if f.exists()])
        if noload:
            lines.append(f"no load.txt (the scenario loads the save itself; started when the log matches '{start}')")
        load = (scen / "load.txt" if (scen / "load.txt").exists() else SCEN / "load.txt").read_text(encoding="utf-8")
        for name, r, s in zip("AB", roots, scripts):
            if not ok or noload:
                break
            watch.arm(name)
            s.write_text(load, encoding="utf-8")
            t = oi.wait_log([r], r"coop: test mark loaded", a.timeout, wprocs)
            lines.append(f"{name} save loaded (mark loaded): " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
            ok = t is not None
            if ok:
                # the world load dips to ~10 fps for ~15 s (measured): settle = 3 samples >= MIN_FPS in a row
                time.sleep(20)  # the dip starts a few seconds AFTER the mark
                t0 = time.time()
                while time.time() - t0 < 90:
                    f = oi.fps_after(r, "coop: test mark loaded")
                    if len(f) >= 3 and min(f[-3:]) >= oi.MIN_FPS:
                        break
                    time.sleep(2)
                else:
                    ok = False
                lines.append(f"{name} world settled (3 samples >= MIN_FPS): "
                             + (f"ok in {time.time() - t0:.0f} s" if ok else "FAIL in 90 s (frozen)"))
            out.write_text("\n".join(lines) + "\n", encoding="utf-8")
        if ok and a.bots:
            # fake players (server/cmd/remake-bot): HELLO, PONG, moving samples in the forest; the games must list
            # them as joined and show their puppets (checked in the verdict)
            bot_log = open(rep / f"{a.scenario}_bots.log", "w", encoding="utf-8")
            bots = subprocess.Popen([str(BOT), "-url", f"ws://127.0.0.1:{a.port + 1}/", "-name", "bot", "-count",
                                     str(a.bots), "-seconds", "0"] + a.bot_arg, stdout=bot_log, stderr=subprocess.STDOUT,
                                    creationflags=oi.NO_WINDOW)
            t0 = time.time()
            while time.time() - t0 < 60:
                if len(set(re.findall(r"coop: player (\d+) 'bot\d+' joined", oi.log_text(roots[0])))) >= a.bots:
                    break
                time.sleep(1)
            else:
                ok = False
            lines.append(f"{a.bots} bots joined (seen by A): " + (f"ok in {time.time() - t0:.0f} s" if ok else "FAIL in 60 s"))
            out.write_text("\n".join(lines) + "\n", encoding="utf-8")
            time.sleep(10)  # let their puppets spawn before the scenario moves
            if a.capture:
                # A's window is shown (not focused) only for this: PrintWindow copies what DWM composes
                shot = rep / f"{a.scenario}_bots{a.bots}_A.png"
                cp = subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("captura.py")),
                                     str(procs[0].pid), str(shot)], capture_output=True, text=True)
                lines.append(f"capture: {cp.stdout.strip() or cp.stderr.strip()[-120:]}")
        if ok:
            watch.arm("A")
            scripts[0].write_text(sa.read_text(encoding="utf-8"), encoding="utf-8")
            if len(scripts) > 1:
                if noload:
                    # these scenarios load the world: one load at a time (D-R01). A's start line says A is in
                    # (the stagger daurosk's run.ps1 gets from playing A first); then the fixed stagger as before.
                    t = oi.wait_log([roots[0]], start, a.timeout, wprocs)
                    lines.append(f"A started ('{start}'): " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
                    ok = t is not None
                    time.sleep(NOLOAD_STAGGER_S if ok else 0)
                if ok:
                    watch.arm("B")
                    scripts[1].write_text(sb.read_text(encoding="utf-8") if sb.exists() else "wait 1200\n", encoding="utf-8")
                    if noload and sb.exists():
                        t = oi.wait_log([roots[1]], start, a.timeout, wprocs)
                        lines.append(f"B started ('{start}'): " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
                        ok = t is not None
            for name, r, f in zip("AB", roots, (sa, sb)):
                mark = last_mark(f) if f.exists() else None
                if mark and ok:
                    t = oi.wait_log([r], rf"coop: test mark {re.escape(mark)}", a.timeout, wprocs)
                    lines.append(f"scenario {name} last mark '{mark}': " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
                    ok = t is not None
            if a.capture and ok:
                shot = rep / f"{a.scenario}_bots{a.bots}_A_end.png"  # after the moves: the others are in view
                cp = subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("captura.py")),
                                     str(procs[0].pid), str(shot)], capture_output=True, text=True)
                lines.append(f"capture at the end: {cp.stdout.strip() or cp.stderr.strip()[-120:]}")
            time.sleep(30)  # the freeze verdict uses the last 20 s: a frozen game does not come back
    finally:
        stop.set()
        if cthread:
            cthread.join(timeout=60)
            lines.append(f"captures: {len(shots)}")
            lines += ["   " + x for x in shots]
        oi.kill(procs)
        if bots is not None:
            bots.kill()
            bots.wait(timeout=10)
            bot_log.close()
        server.kill()
        server.wait(timeout=30)  # the next test binds the same port
        srv_log.close()
    # The test script counts real time, not frames: a frozen game still "plays" it. Require live frames in the scenario.
    slow = []
    if a.bots:
        for name, r in zip("AB", roots):
            text = oi.log_text(r)
            joined = sorted(set(re.findall(r"coop: player (\d+) 'bot\d+' joined", text)), key=int)
            puppets = sorted(set(re.findall(r"coop: puppet: P(\d+) CoopPuppet_\w+ spawned", text)), key=int)
            lines.append(f"-- {name}: bots joined as players {joined}; puppets spawned for players {puppets}")
            if len(puppets) < a.players + a.bots - 1:  # every other player's puppet (real or bot)
                if sopt["puppets"]:  # '#! puppets' (seis-*): the bots are in the game's scene, so it is a fail
                    ok = False
                    lines.append(f"{name}: PUPPETS MISSING ({len(puppets)} of {a.players + a.bots - 1})")
                else:
                    slow.append(f"{name}:puppets")  # a warning until the bots' scene matches the games' (R1.5)
        bot_path = rep / f"{a.scenario}_bots.log"
        bot_text = bot_path.read_text(encoding="utf-8", errors="replace") if bot_path.exists() else ""
        lines.append(f"-- bots: {bot_log_summary(bot_text)}, log {bot_path}")
    game_logs = {n: oi.log_text(r) for n, r in zip("AB", roots)}
    lines.append(join_line(game_logs))
    if watch.late:
        ok = False
        lines += watch.lines()
    mismatch = []
    for name in game_logs:
        good, line = life_check(name, game_logs[name], save_maxlife(saves[name]))
        lines.append(line)
        if not good:
            ok = False
            mismatch.append(line)
    a_file = scen / f"{a.scenario}.A.txt"
    a_marker = first_mark_marker(a_file.read_text(encoding="utf-8"))
    for name, r, f in zip("AB", roots, (a_file, scen / f"{a.scenario}.B.txt")):
        good, more, is_slow = game_checks(name, oi.log_text(r), f.read_text(encoding="utf-8") if f.exists() else "",
                                          a_marker, sopt["min_fps"], oi.log_path(r))
        ok = ok and good
        lines += more
        if is_slow:
            slow.append(name)
    # content assertions (#! expect / never / count in the scenario files): on the logs copied next to the summary
    directives = parse_directives([f.read_text(encoding="utf-8") for f in files[:2] if f.exists()])
    if files[2].exists() and a.bots:  # in the bots' script a letterless directive reads the bots' log
        directives += parse_directives([files[2].read_text(encoding="utf-8")], games=["bots"])
    games = "AB"[:a.players]
    skipped = [d for d in directives if d["game"] and d["game"] not in games and d["game"] not in ("bots", "server")]
    if skipped:  # a two-game scenario run with --players 1: B's directives have no game to read
        lines.append(f"expect: {len(skipped)} directives for a game not run here skipped ({a.players} player)")
        directives = [d for d in directives if d not in skipped]
    failed = []
    if directives:
        logs = {}
        for name, r in zip("AB", roots):
            if oi.log_path(r).exists():
                copy = rep / f"{a.scenario}_{name}_eden_log.txt"
                shutil.copyfile(oi.log_path(r), copy)
                logs[name] = copy.read_text(encoding="utf-8", errors="replace")
        srv_log_path = rep / f"{a.scenario}_server.log"
        logs["server"] = srv_log_path.read_text(encoding="utf-8", errors="replace") if srv_log_path.exists() else None
        bot_log_path = rep / f"{a.scenario}_bots.log"
        logs["bots"] = bot_log_path.read_text(encoding="utf-8", errors="replace") if bot_log_path.exists() else None
        res = evaluate_directives(directives, logs)
        lines += [l for _, l in res]
        failed = [l.split(": ", 1)[1] for good, l in res if not good]
        lines.append(f"expect: {len(res) - len(failed)}/{len(res)} ok")
        ok = ok and not failed
    if not ok:  # the next run overwrites the instance logs: keep them with the summary
        for name, r in zip("AB", roots):
            if oi.log_path(r).exists():
                shutil.copyfile(oi.log_path(r), rep / f"{a.scenario}_{name}_eden_log.txt")
    lines.append("RESULT: " + ("PASS" if ok else "FAIL") + (" (SLOW: " + ",".join(slow) + ")" if ok and slow else "")
                 + (" (NEVER STARTED: " + ",".join(g for g, _ in watch.late) + ")" if watch.late else "")
                 + (" (" + "; ".join(mismatch)[:200] + ")" if mismatch else "")
                 + (f" (expect: {len(failed)} failed: " + "; ".join(failed)[:300] + ")" if failed else ""))
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
