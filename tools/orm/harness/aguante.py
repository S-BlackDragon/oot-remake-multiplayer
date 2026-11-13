"""A long stability run of the remake co-op, two hidden games for N minutes (aguante).

    python -I tools/orm/harness/aguante.py [--minutes 120] [--list aguante_lista.txt] [--tag NAME]
    python -I tools/orm/harness/aguante.py --plan            (no games: the list and the schedule)
    python -I tools/orm/harness/aguante.py --reanalizar <ORM_REPORTS>/aguante/<tag>   (summary again)

The r03_pareja.py building blocks (orm_inst instances, orm_server by WebSocket, daurosk's load.txt, settle) bring two
games into the world once; then the scenarios of the list are chained in the SAME session (the hook plays a script file
again each time it changes) until N minutes have passed. Every --sample-every minutes, per game: fps from the log (the
median and lowest of the 2 s lines since the last sample), working set, private bytes and handle count of the process,
the log's size; of the relay: its RSS, its established WebSocket connections and the players its stats line counts. At
--restart-at the relay is killed and started again (both games must be welcomed again); at --cut-at game B's network
is cut for --cut-seconds through a TCP link simulator (netsim, optional, ORM_NETSIM_DIR; B reaches the relay through it), and B must come back. At the end:
resumen.txt with the slope per hour of every series after the warm-up (Theil-Sen), the events and a verdict.

Game lock <ORM_LOG_ROOT>/game.lock (a folder made with mkdir, as carga6.ps1) taken before anything starts and given back
in a finally; a STOP file at the repository root holds the start and ends the run early. ORM_READY_TIMEOUT is honoured
through orm_inst.ready_for_games. Summary in <ORM_REPORTS>/aguante/<tag>/, raw logs in <ORM_LOG_ROOT>/aguante/<tag>/.

Test: test_aguante.py (parsing and verdict math with fake data).
"""
import argparse, csv, ctypes, json, os, re, shutil, statistics, struct, subprocess, sys, time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import orm_inst as oi
import r03_pareja as rp
import orm_paths

HERE = Path(__file__).resolve().parent
REPO = oi.REPO
LOCK = orm_paths.GAME_LOCK
STOP = REPO / "STOP"
NETSIM_DIR = orm_paths.NETSIM_DIR  # optional: a folder with netsim.py (class NetSim)
DEFAULT_LIST = HERE / "aguante_lista.txt"
RAW_ROOT = orm_paths.LOG_ROOT / "aguante"
GAMES = ("A", "B")
WELCOME = "coop: welcomed as player"
LOST = "coop: connection lost"
STATS_RX = re.compile(r"orm_server: stats; players (\d+), relayed (\d+), dropped (\d+)")
MARK_LINE = re.compile(r"^\s*mark\s+(\S+)", re.M)
IDLE_B = "# B has no script of its own in this scenario: stands still\nmark aguante-idle\nwait 60\n"
FROZEN_FPS = rp.FROZEN_FPS
MIN_POINTS = 4  # fewer samples after the warm-up than this: no slope (n/a)

# Slope per hour after the warm-up above which a series is a WARN / a FAIL (None: that level does not exist).
# Private bytes are what a leak shows in (the 10 GB of guest memory are committed once, at boot); the working set
# moves with what Windows trims, so it only warns. A relay keeps nothing per frame: a few MB an hour is already odd.
LIMITS = {
    "priv_mb": (100.0, 300.0),
    "ws_mb": (200.0, None),
    "handles": (200.0, 1000.0),
    "log_mb": (300.0, None),  # 300 MB/h: 600 MB in 2 h, close to the 1 GiB cap of extended logging
    "srv_rss_mb": (5.0, 20.0),
}
FPS_DROP = (25.0, 50.0)  # % lower in the last third of the samples than in the first: WARN / FAIL

GAME_KEYS = ("alive", "fps_med", "fps_min", "fps_n", "ws_mb", "priv_mb", "handles", "log_mb")
COLUMNS = (["t_min"] + [f"{g}_{k}" for g in GAMES for k in GAME_KEYS]
           + ["srv_alive", "srv_rss_mb", "srv_priv_mb", "srv_conns", "srv_players", "free_ram_gb"])


# ---------------------------------------------------------------------------------------------- reading the logs

class LogTail:
    """Reads a log as it grows (only the new bytes each poll): fps lines since the last take_fps(), counts of every
    test mark, of welcomes and of lost connections, and the lines since begin_window() (a scenario's own lines)."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self.offset = 0
        self.rest = b""
        self.fps: list[float] = []
        self.marks: dict[str, int] = {}
        self.welcomed = 0
        self.lost = 0
        self.critical = 0
        self.window: list[str] | None = None

    def poll(self) -> int:
        """Reads what was added since the last poll; returns the number of whole lines read."""
        try:
            size = self.path.stat().st_size
        except OSError:
            return 0
        if size < self.offset:  # a new log (the file was recreated): start again
            self.offset, self.rest = 0, b""
        if size == self.offset:
            return 0
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            data = f.read(size - self.offset)
        self.offset += len(data)
        parts = (self.rest + data).split(b"\n")
        self.rest = parts.pop()  # an unfinished last line waits for the next poll
        for raw in parts:
            self.feed(raw.decode("utf-8", errors="replace").rstrip("\r"))
        return len(parts)

    def feed(self, line: str) -> None:
        m = oi.FPS.search(line)
        if m:
            self.fps.append(float(m.group(1)))
        m = rp.MARK.search(line)
        if m:
            self.marks[m.group(1)] = self.marks.get(m.group(1), 0) + 1
        if WELCOME in line:
            self.welcomed += 1
        if LOST in line:
            self.lost += 1
        if "<Critical>" in line:
            self.critical += 1
        if self.window is not None:
            self.window.append(line)

    def take_fps(self) -> list[float]:
        out, self.fps = self.fps, []
        return out

    def begin_window(self) -> None:
        self.window = []

    def end_window(self) -> str:
        text = "\n".join(self.window or [])
        self.window = None
        return text


def last_server_stats(text: str) -> dict | None:
    """The last `orm_server: stats; players N, relayed N, dropped N, ...` line (--log-stats-every-ms)."""
    found = STATS_RX.findall(text)
    if not found:
        return None
    p, r, d = found[-1]
    return {"players": int(p), "relayed": int(r), "dropped": int(d)}


def last_mark_of(text: str) -> str | None:
    marks = MARK_LINE.findall(text)
    return marks[-1] if marks else None


def load_list(text: str) -> list[str]:
    """Scenario names, one a line; `#` starts a comment; blank lines skipped; order kept, repeats allowed."""
    return [n for n in (l.split("#", 1)[0].strip() for l in text.splitlines()) if n]


def classify(names: list[str], scen: Path) -> tuple[list[str], list[str]]:
    """(playable, skipped with why). A scenario that loads the save itself (r03_pareja.scenario_plan noload: a warp
    asked before the title presses) cannot be chained into a world already loaded; one without a mark in A's script
    gives no end to wait for."""
    ok, skipped = [], []
    for n in names:
        a, b = scen / f"{n}.A.txt", scen / f"{n}.B.txt"
        if not a.exists():
            skipped.append(f"{n} (no {a.name})")
            continue
        texts = [a.read_text(encoding="utf-8")] + ([b.read_text(encoding="utf-8")] if b.exists() else [])
        if rp.scenario_plan(texts)[0]:
            skipped.append(f"{n} (loads the save itself)")
        elif last_mark_of(texts[0]) is None:
            skipped.append(f"{n} (no mark in A)")
        else:
            ok.append(n)
    return ok, skipped


# ---------------------------------------------------------------------------------------------- process and TCP

class _PMC(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [(n, ctypes.c_size_t) for n in (
        "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
        "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


_k32 = _iph = None
if os.name == "nt":
    # own library objects: the argtypes set here do not change ctypes.windll for orm_inst
    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _k32.OpenProcess.restype = wintypes.HANDLE
    _k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    _k32.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    _iph = ctypes.WinDLL("iphlpapi")
    _iph.GetExtendedTcpTable.restype = wintypes.DWORD
    _iph.GetExtendedTcpTable.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL,
                                         wintypes.ULONG, ctypes.c_int, wintypes.ULONG]


def proc_metrics(pid: int) -> dict | None:
    """Working set, private bytes (MB) and handle count of a process, or None when it cannot be opened."""
    if _k32 is None:
        return None
    h = _k32.OpenProcess(0x1000 | 0x0010, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    if not h:
        return None
    try:
        pmc = _PMC()
        pmc.cb = ctypes.sizeof(_PMC)
        if not _k32.K32GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb):
            return None
        n = wintypes.DWORD()
        handles = n.value if _k32.GetProcessHandleCount(h, ctypes.byref(n)) else None
        return {"ws_mb": pmc.WorkingSetSize / 2**20, "priv_mb": pmc.PrivateUsage / 2**20, "handles": handles}
    finally:
        _k32.CloseHandle(h)


TCP_ESTABLISHED = 5
_ROW = struct.Struct("<6I")  # MIB_TCPROW_OWNER_PID: state, local addr, local port, remote addr, remote port, pid


def tcp_rows(buf: bytes) -> list[tuple[int, int, int, int]]:
    """(state, local port, remote port, pid) of each row of a MIB_TCPTABLE_OWNER_PID (GetExtendedTcpTable, IPv4).
    The ports are in network byte order in the low 16 bits of their DWORD. Not netstat: its state words are
    translated on a Spanish Windows."""
    if len(buf) < 4:
        return []
    (count,) = struct.unpack_from("<I", buf, 0)
    rows = []
    for i in range(count):
        off = 4 + i * _ROW.size
        if off + _ROW.size > len(buf):
            break
        state, _la, lport, _ra, rport, pid = _ROW.unpack_from(buf, off)
        swap = lambda p: ((p & 0xFF) << 8) | ((p >> 8) & 0xFF)
        rows.append((state, swap(lport), swap(rport), pid))
    return rows


def tcp_table() -> bytes:
    if _iph is None:
        return b""
    size = wintypes.DWORD(0)
    _iph.GetExtendedTcpTable(None, ctypes.byref(size), False, 2, 5, 0)  # AF_INET, TCP_TABLE_OWNER_PID_ALL
    for _ in range(4):  # the table can grow between the two calls
        buf = ctypes.create_string_buffer(size.value + 4096)
        size = wintypes.DWORD(len(buf))
        if _iph.GetExtendedTcpTable(buf, ctypes.byref(size), False, 2, 5, 0) == 0:
            return buf.raw[:size.value]
    return b""


def count_established(rows: list[tuple[int, int, int, int]], port: int, pid: int | None = None) -> int:
    """Established connections whose LOCAL port is `port` (the relay's accepted sockets), owned by `pid` if given."""
    return sum(1 for s, lp, _rp, p in rows if s == TCP_ESTABLISHED and lp == port and (pid is None or p == pid))


# ---------------------------------------------------------------------------------------------- the verdict

def theil_sen(pts: list[tuple[float, float]]) -> float | None:
    """Median of the slopes of every pair of points (robust to one load spike); None with fewer than two times."""
    slopes = [(y2 - y1) / (t2 - t1) for i, (t1, y1) in enumerate(pts) for t2, y2 in pts[i + 1:] if t2 != t1]
    return statistics.median(slopes) if slopes else None


def series(rows: list[dict], key: str, t_from: float) -> list[tuple[float, float]]:
    return [(r["t_min"], r[key]) for r in rows if r.get(key) is not None and r["t_min"] >= t_from]


def fps_drop(vals: list[float]) -> float | None:
    """How much lower (%) the median of the last third of the values is than the first third's; None below 3."""
    if len(vals) < 3:
        return None
    k = max(1, len(vals) // 3)
    first, last = statistics.median(vals[:k]), statistics.median(vals[-k:])
    return (first - last) / first * 100 if first > 0 else None


def _f(v, d: int = 1) -> str:
    return "-" if v is None else f"{v:.{d}f}"


def analyze(rows: list[dict], events: list[dict], meta: dict, limits: dict = LIMITS) -> tuple[str, list[str]]:
    """(verdict word, summary lines) from the samples, the events and the run's meta (json-able, see run())."""
    fails, warns, lines = [], [], []
    warm = float(meta.get("warmup", 10))
    games = list(meta.get("games", GAMES))
    played = rows[-1]["t_min"] if rows else 0.0
    restart_t = next((e["t_min"] for e in events if e.get("kind") == "relay_restart"), None)
    lines.append(f"samples: {len(rows)} (every {meta.get('sample_every', 5)} min), played {played:.1f} of "
                 f"{meta.get('minutes', '?')} min, warm-up {warm:g} min (left out of the trends)")

    lines.append("== trends per hour after the warm-up (Theil-Sen slope; first -> last value; limits warn/fail)")

    def trend(label: str, key: str, t_from: float, lim: tuple) -> None:
        pts = series(rows, key, t_from)
        if len(pts) < MIN_POINTS:
            lines.append(f"{label:<20} n/a ({len(pts)} samples from minute {t_from:g})")
            return
        s = theil_sen(pts) * 60
        warn, fail = lim
        state = "ok"
        if fail is not None and s > fail:
            state = "FAIL"
            fails.append(f"{label} grows {s:+.1f}/h (fail above {fail:g})")
        elif warn is not None and s > warn:
            state = "WARN"
            warns.append(f"{label} grows {s:+.1f}/h (warn above {warn:g})")
        lines.append(f"{label:<20} {s:+9.1f}/h   {pts[0][1]:9.1f} -> {pts[-1][1]:9.1f}   "
                     f"({_f(warn, 0)}/{_f(fail, 0)})  {state}")

    for g in games:
        for key, label in (("priv_mb", "private MB"), ("ws_mb", "working set MB"), ("handles", "handles"),
                           ("log_mb", "log MB")):
            trend(f"{g} {label}", f"{g}_{key}", warm, limits[key])
    # the relay started again at restart_t: its RSS before and after are two processes
    srv_from = max(warm, restart_t + 5) if restart_t is not None else warm
    trend("relay RSS MB", "srv_rss_mb", srv_from, limits["srv_rss_mb"])

    lines.append("== fps (median of each sample's 2 s lines)")
    for g in games:
        pts = series(rows, f"{g}_fps_med", warm)
        vals = [v for _, v in pts]
        frozen = [t for t, v in pts if v < FROZEN_FPS]
        if frozen:
            fails.append(f"{g} frozen (fps median below {FROZEN_FPS}) in the sample at minute {frozen[0]:g}")
        silent = [r["t_min"] for r in rows if r["t_min"] >= warm and r.get(f"{g}_alive") and not r.get(f"{g}_fps_n")]
        if silent:
            warns.append(f"{g}: no fps line in the sample at minute {silent[0]:g} (the log's size cap, or the "
                         "emulator hung)")
        drop = fps_drop(vals)
        state = "ok"
        if drop is not None and drop > FPS_DROP[1]:
            state = "FAIL"
            fails.append(f"{g} fps {drop:.0f} % lower at the end than at the start")
        elif drop is not None and drop > FPS_DROP[0]:
            state = "WARN"
            warns.append(f"{g} fps {drop:.0f} % lower at the end than at the start")
        lows = [r.get(f"{g}_fps_min") for r in rows if r["t_min"] >= warm and r.get(f"{g}_fps_min") is not None]
        lines.append(f"{g}: median {_f(statistics.median(vals) if vals else None)}, lowest sample median "
                     f"{_f(min(vals) if vals else None)}, lowest 2 s line {_f(min(lows) if lows else None)}, "
                     f"first->last third {_f(-drop if drop is not None else None, 0)} %  {state}")

    for g in games:
        gone = [r["t_min"] for r in rows if r.get(f"{g}_alive") == 0]
        if gone:
            fails.append(f"game {g} not running in the sample at minute {gone[0]:g}")
    gone = [r["t_min"] for r in rows if r.get("srv_alive") == 0]
    if gone:
        fails.append(f"relay not running in the sample at minute {gone[0]:g}")

    lines.append("== events")
    if not events:
        lines.append("(none)")
    for e in events:
        ok = e.get("ok")
        lines.append(f"{e.get('kind')} at {e.get('t_min', 0):.1f} min: "
                     + ("ok" if ok else "skipped" if ok is None else "FAIL") + f" ({e.get('detail', '')})")
        if ok is False:
            fails.append(f"{e.get('kind')} at minute {e.get('t_min', 0):.1f}: {e.get('detail', '')}")
    reconnect_min = float(meta.get("reconnect_timeout", 120)) / 60
    for kind, key in (("relay_restart", "restart_at"), ("net_cut", "cut_at")):
        at = float(meta.get(key) or 0)
        if at > 0 and played >= at + reconnect_min + 1 and not any(e.get("kind") == kind for e in events):
            warns.append(f"{kind} planned at minute {at:g} not done")

    lines.append("== relay")
    last = rows[-1] if rows else {}
    alive_games = sum(1 for g in games if last.get(f"{g}_alive"))
    for key, label in (("srv_conns", "WebSocket connections"), ("srv_players", "players (its stats line)")):
        if last.get(key) is not None:
            lines.append(f"{label} at the end: {last[key]:g} (games running {alive_games})")
            if last[key] != alive_games:
                warns.append(f"relay {label} at the end {last[key]:g}, games running {alive_games}")
    settle = max([e.get("t_min", 0) for e in events] + [0]) + 5
    extra = [r["t_min"] for r in rows if r["t_min"] >= settle and r.get("srv_conns") is not None
             and r["srv_conns"] > sum(1 for g in games if r.get(f"{g}_alive"))]
    if extra:
        warns.append(f"relay held more connections than games in the sample at minute {extra[0]:g} (one left behind)")

    sc = meta.get("scenarios") or {}
    lines.append("== scenarios")
    lines.append(f"rounds {sc.get('rounds', 0)}, last mark missed {sc.get('missed', 0)}, expectations "
                 f"{sc.get('expects', 0) - sc.get('expects_missing', 0)}/{sc.get('expects', 0)} met (not in the "
                 "verdict: the world drifts between rounds)")
    if sc.get("per_name"):
        lines.append("   " + ", ".join(f"{n} {c}" for n, c in sc["per_name"].items()))
    if sc.get("missed_names"):
        lines.append("   last mark missed: " + ", ".join(sc["missed_names"][:20]))
    if sc.get("skipped"):
        lines.append("   skipped: " + "; ".join(sc["skipped"]))
    if sc.get("missed"):
        warns.append(f"{sc['missed']} scenario round(s) never reached their last mark")

    stopped = meta.get("stopped")
    if not stopped and meta.get("minutes") and played < float(meta["minutes"]) - 1 and not fails:
        warns.append(f"ended at minute {played:.1f} of {meta['minutes']} ({meta.get('ended', 'no reason kept')})")
    if meta.get("minutes") and float(meta["minutes"]) < 60:
        lines.append("note: a run under 60 min says little about leaks")
    word = "FAIL" if fails else "STOPPED" if stopped else "WARN" if warns else "PASS"
    lines.append(f"VERDICT: {word}" + (f" ({stopped})" if stopped else ""))
    lines += [f"   FAIL: {x}" for x in fails] + [f"   WARN: {x}" for x in warns]
    return word, lines


# ---------------------------------------------------------------------------------------------- files

def write_csv(path: Path, rows: list[dict]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: ("" if r.get(k) is None else (round(r[k], 2) if isinstance(r[k], float) else r[k]))
                        for k in COLUMNS})


def read_csv(path: Path) -> list[dict]:
    with open(path, newline="", encoding="utf-8") as f:
        return [{k: (float(v) if v not in ("", None) else None) for k, v in r.items()} for r in csv.DictReader(f)]


def take_lock(lock: Path, stop: Path, wait_s: float, poll_s: float = 60, note=print) -> str:
    """'taken' once mkdir(lock) succeeds with no STOP file; 'timeout' after wait_s. The lock is a folder, as
    carga6.ps1's: whoever made it owns it and removes it."""
    t0, told = time.time(), False
    while True:
        if not stop.exists():
            try:
                os.mkdir(lock)
                (lock / "owner.txt").write_text(f"aguante.py pid {os.getpid()} {time.strftime('%Y-%m-%dT%H:%M:%S')}\n",
                                                encoding="utf-8")
                return "taken"
            except FileExistsError:
                pass
        if time.time() - t0 >= wait_s:
            return "timeout"
        if not told:
            note(f"waiting for the game lock {lock}" + (" and for the STOP file to go" if stop.exists() else ""))
            told = True
        time.sleep(poll_s)


def give_lock(lock: Path) -> None:
    shutil.rmtree(lock, ignore_errors=True)


# ---------------------------------------------------------------------------------------------- the run

class Chain:
    """Plays the list's scenarios one after another in the running games: writes A's (and B's, or a short idle) script
    file, waits until every game with marks logged its script's last mark once more, then --gap seconds, the next."""

    def __init__(self, names: list[str], scen: Path, scripts: dict, tails: dict, timeout: float, gap: float):
        self.names, self.scen, self.scripts, self.tails = names, scen, scripts, tails
        self.timeout, self.gap = timeout, gap
        self.i, self.cur, self.next_at = 0, None, 0.0
        self.results: list[dict] = []

    def poll(self, now: float) -> None:
        if self.cur is None:
            if now >= self.next_at:
                self.start(now)
            return
        c = self.cur
        done = all(self.tails[g].marks.get(m, 0) > before for g, (m, before) in c["wait"].items())
        if done or now - c["t"] > self.timeout:
            logs = {g: self.tails[g].end_window() for g in GAMES}
            res = rp.evaluate_directives(c["expects"], logs)
            self.results.append({"name": c["name"], "ok": done, "seconds": round(now - c["t"], 1),
                                 "expects": len(res), "missing": sum(1 for ok, _ in res if not ok)})
            self.cur, self.next_at = None, now + self.gap

    def start(self, now: float) -> None:
        name = self.names[self.i % len(self.names)]
        self.i += 1
        a = (self.scen / f"{name}.A.txt").read_text(encoding="utf-8")
        bp = self.scen / f"{name}.B.txt"
        b = bp.read_text(encoding="utf-8") if bp.exists() else None
        wait = {}
        for g, text in (("A", a), ("B", b)):
            m = last_mark_of(text) if text else None
            if m:
                wait[g] = (m, self.tails[g].marks.get(m, 0))
        # The scenario's #! expect / never / count lines (r03_pareja.parse_directives, `within` too), read on this
        # round's window of each game's log; those for the bots or the server have no window here and are left out.
        expects = [d for d in rp.parse_directives([a] + ([b] if b else []), "AB") if d["game"] in GAMES or d["error"]]
        head = f"# aguante round {self.i}: {name}\n"  # a new text each round, so the hook always reads it again
        for g, text in (("A", a), ("B", b if b is not None else IDLE_B)):
            self.tails[g].begin_window()
            self.scripts[g].write_text(head + text, encoding="utf-8")
        self.cur = {"name": name, "t": now, "wait": wait, "expects": expects}

    def summary(self, skipped: list[str]) -> dict:
        per = {}
        for r in self.results:
            per[r["name"]] = per.get(r["name"], 0) + 1
        return {"rounds": len(self.results), "missed": sum(1 for r in self.results if not r["ok"]),
                "missed_names": [r["name"] for r in self.results if not r["ok"]],
                "expects": sum(r["expects"] for r in self.results),
                "expects_missing": sum(r["missing"] for r in self.results), "per_name": per, "skipped": skipped}


def start_server(exe: Path, port: int, log: Path):
    f = open(log, "w", encoding="utf-8")
    # 127.0.0.1 only (no firewall prompt); --max-half-open as r03_pareja; a stats line a minute for the players count
    p = subprocess.Popen([str(exe), "--udp-port", str(port), "--bind", "127.0.0.1", "--ws-port", str(port + 1),
                          "--max-half-open", "16", "--log-stats-every-ms", "60000"],
                         stdout=f, stderr=subprocess.STDOUT, creationflags=oi.NO_WINDOW)
    return p, f


def open_netsim(port: int, target_port: int, profile: str, wait_s: float = 0):
    """The netsim (a TCP proxy) in this process, or (None, why). Retries for wait_s (a port just closed)."""
    try:
        sys.path.insert(0, str(NETSIM_DIR))
        from netsim import NetSim  # noqa: E402
    except Exception as e:  # no netsim in this checkout
        return None, f"no netsim ({type(e).__name__}: {e})"
    t0 = time.time()
    while True:
        try:
            return NetSim(port, "127.0.0.1", target_port, profile).start(), ""
        except Exception as e:
            if time.time() - t0 >= wait_s:
                return None, f"netsim on {port} did not start ({type(e).__name__}: {e})"
            time.sleep(2)


def settle(root: Path, procs: list, timeout: float, lines: list, name: str) -> bool:
    """r03_pareja's load step for one game: daurosk's load.txt was written; mark loaded, then 3 fps lines >= MIN_FPS."""
    t = oi.wait_log([root], r"coop: test mark loaded", timeout, procs)
    lines.append(f"{name} save loaded: " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
    if t is None:
        return False
    time.sleep(20)  # the world load's fps dip starts a few seconds after the mark
    t0 = time.time()
    while time.time() - t0 < 90:
        f = oi.fps_after(root, "coop: test mark loaded")
        if len(f) >= 3 and min(f[-3:]) >= oi.MIN_FPS:
            lines.append(f"{name} world settled in {time.time() - t0:.0f} s")
            return True
        time.sleep(2)
    lines.append(f"{name} world settled: FAIL in 90 s (frozen)")
    return False


def take_sample(t_min: float, procs: list, tails: dict, server, srv_log: Path, ws_port: int) -> dict:
    row = {"t_min": round(t_min, 2)}
    for g, p in zip(GAMES, procs):
        fps = tails[g].take_fps()
        alive = p.poll() is None
        m = proc_metrics(p.pid) if alive else None
        try:
            log_mb = tails[g].path.stat().st_size / 2**20
        except OSError:
            log_mb = None
        row.update({f"{g}_alive": int(alive), f"{g}_fps_med": statistics.median(fps) if fps else None,
                    f"{g}_fps_min": min(fps) if fps else None, f"{g}_fps_n": len(fps),
                    f"{g}_ws_mb": m and m["ws_mb"], f"{g}_priv_mb": m and m["priv_mb"],
                    f"{g}_handles": m and m["handles"], f"{g}_log_mb": log_mb})
    alive = server.poll() is None
    m = proc_metrics(server.pid) if alive else None
    try:
        stats = last_server_stats(srv_log.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        stats = None
    row.update({"srv_alive": int(alive), "srv_rss_mb": m and m["ws_mb"], "srv_priv_mb": m and m["priv_mb"],
                "srv_conns": count_established(tcp_rows(tcp_table()), ws_port, server.pid) if alive else None,
                "srv_players": stats["players"] if stats else None, "free_ram_gb": oi.free_ram_gb()})
    return row


def run(a, rep: Path, raw: Path, head: list, names: list[str], skipped: list[str]) -> tuple[list, list, dict]:
    """Boots the two games, chains the scenarios for a.minutes and samples; (rows, events, meta)."""
    meta = {"tag": rep.name, "minutes": a.minutes, "sample_every": a.sample_every, "warmup": a.warmup,
            "restart_at": a.restart_at, "cut_at": a.cut_at, "cut_seconds": a.cut_seconds,
            "reconnect_timeout": a.reconnect_timeout, "games": list(GAMES), "list": names}
    rows, events, progress = [], [], []

    def write(state: str) -> None:
        (rep / "resumen.txt").write_text("\n".join(head + progress + [f"STATE: {state}"]) + "\n", encoding="utf-8")
        write_csv(rep / "muestras.csv", rows)
        (rep / "eventos.json").write_text(json.dumps(events, indent=1), encoding="utf-8")
        (rep / "meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")

    ws_port = a.port + 1
    server_exe = Path(a.server_exe) if a.server_exe else rp.SERVER
    netsim, why = (None, "--no-netsim") if a.no_netsim else open_netsim(a.netsim_port, ws_port, a.netsim_profile)
    meta["netsim"] = f"{a.netsim_profile} on 127.0.0.1:{a.netsim_port} (game B)" if netsim else why
    progress.append("netsim: " + meta["netsim"])
    urls = {"A": f"ws://127.0.0.1:{ws_port}/", "B": f"ws://127.0.0.1:{a.netsim_port if netsim else ws_port}/"}
    roots = {g: oi.prepare(k, saves=Path(a.saves), coop_ini=f"server = {urls[g]}\r\nname = {g}\r\n", res=a.res)
             for k, g in ((1, "A"), (2, "B"))}
    if not a.short_log:
        for r in roots.values():  # the 100 MiB log cap would stop the fps lines after ~1 h; extended logging: 1 GiB
            with open(r / "orm.ini", "a", encoding="utf-8") as f:
                f.write("[Debugging]\nextended_logging" + oi.BS + "default=false\nextended_logging=true\n")
    scripts = {g: r / "script.txt" for g, r in roots.items()}
    for s in scripts.values():
        s.write_text("", encoding="utf-8")
    srv_n = 1
    srv_log = raw / f"server_{srv_n}.log"
    server, srv_f = start_server(server_exe, a.port, srv_log)
    extra = dict(e.split("=", 1) for e in a.env)
    procs = [oi.launch(roots[g], dict(rp.SWITCHES, **extra, LICHII_COOP_TEST_SCRIPT=str(scripts[g]))) for g in GAMES]
    chain = None
    try:
        t = oi.wait_log(list(roots.values()), WELCOME, a.boot_timeout, procs)
        progress.append("both joined: " + (f"ok in {t:.0f} s" if t is not None else "FAIL"))
        ok = t is not None
        load = (Path(a.scen_dir) / "load.txt")
        load = (load if load.exists() else rp.SCEN / "load.txt").read_text(encoding="utf-8")
        for g in GAMES:  # one world load at a time (r03_pareja, D-R01)
            if not ok:
                break
            scripts[g].write_text(load, encoding="utf-8")
            ok = settle(roots[g], procs, a.boot_timeout, progress, g)
            write("booting")
        if not ok:
            meta["ended"] = "boot failed"
            events.append({"kind": "boot", "t_min": 0.0, "ok": False, "detail": progress[-1]})
            return rows, events, meta
        tails = {g: LogTail(oi.log_path(roots[g])) for g in GAMES}
        for tl in tails.values():
            tl.poll()
            tl.take_fps()  # the boot's fps are not the run's
        chain = Chain(names, Path(a.scen_dir), scripts, tails, a.scenario_timeout, a.gap)
        t0 = time.time()
        next_sample, restart, cut = 0.0, None, None
        restarted = cut_done = False
        progress.append(f"chain started {time.strftime('%H:%M:%S')}")
        write("running")
        while True:
            now = time.time()
            t_min = (now - t0) / 60
            for tl in tails.values():
                tl.poll()
            died = [g for g, p in zip(GAMES, procs) if p.poll() is not None]
            if died:
                events.append({"kind": "exit", "t_min": t_min, "ok": False,
                               "detail": "game " + ", ".join(died) + " exited (code "
                               + ", ".join(str(procs[GAMES.index(g)].returncode) for g in died) + ")"})
                meta["ended"] = "a game exited"
                rows.append(take_sample(t_min, procs, tails, server, srv_log, ws_port))
                break
            if server.poll() is not None:
                events.append({"kind": "relay_exit", "t_min": t_min, "ok": False,
                               "detail": f"the relay exited by itself (code {server.returncode})"})
                meta["ended"] = "the relay exited"
                rows.append(take_sample(t_min, procs, tails, server, srv_log, ws_port))
                break
            if STOP.exists():
                meta["stopped"] = f"STOP file at minute {t_min:.1f}"
                rows.append(take_sample(t_min, procs, tails, server, srv_log, ws_port))
                break
            if t_min >= next_sample:
                rows.append(take_sample(t_min, procs, tails, server, srv_log, ws_port))
                next_sample += a.sample_every
                r = rows[-1]
                progress.append(f"t={t_min:5.1f} min  A fps {_f(r['A_fps_med'])} priv {_f(r['A_priv_mb'], 0)} MB  "
                                f"B fps {_f(r['B_fps_med'])} priv {_f(r['B_priv_mb'], 0)} MB  relay "
                                f"{_f(r['srv_rss_mb'])} MB conns {r['srv_conns']}  rounds {len(chain.results)}")
                write("running")
            if t_min >= a.minutes:
                break
            # relay restart: kill, a short gap, the same ports again; every game must be welcomed once more
            if a.restart_at > 0 and not restarted and t_min >= a.restart_at:
                restarted = True
                before = {g: tails[g].welcomed for g in GAMES}
                server.kill()
                server.wait(timeout=30)
                srv_f.close()
                time.sleep(3)
                for attempt in range(10):  # a port the old relay held may refuse the bind for a moment
                    srv_n += 1
                    srv_log = raw / f"server_{srv_n}.log"
                    server, srv_f = start_server(server_exe, a.port, srv_log)
                    time.sleep(2)
                    if server.poll() is None:
                        break
                    srv_f.close()
                    time.sleep(3)
                restart = {"t_min": t_min, "t0": time.time(), "before": before, "back": {}, "tries": attempt + 1}
            if restart:
                for g in GAMES:
                    if g not in restart["back"] and tails[g].welcomed > restart["before"][g]:
                        restart["back"][g] = time.time() - restart["t0"]
                waited = time.time() - restart["t0"]
                if len(restart["back"]) == len(GAMES) or waited > a.reconnect_timeout:
                    ok = len(restart["back"]) == len(GAMES)
                    detail = ", ".join(f"{g} back in {s:.0f} s" for g, s in restart["back"].items())
                    if restart["tries"] > 1:
                        detail += f" (relay started at try {restart['tries']})"
                    if not ok:
                        detail += ("; " if detail else "") + "not back in " + f"{a.reconnect_timeout:.0f} s: " + \
                            ", ".join(g for g in GAMES if g not in restart["back"])
                    events.append({"kind": "relay_restart", "t_min": restart["t_min"], "ok": ok, "detail": detail})
                    progress.append(f"relay restart at {restart['t_min']:.1f} min: {'ok' if ok else 'FAIL'} ({detail})")
                    restart = None
                    write("running")
            # client network cut: B's proxy closed for cut_seconds (every connection through it drops), then opened
            if a.cut_at > 0 and not cut_done and t_min >= a.cut_at and restart is None:
                cut_done = True
                if netsim is None:
                    events.append({"kind": "net_cut", "t_min": t_min, "ok": None, "detail": meta["netsim"]})
                else:
                    cut = {"t_min": t_min, "t0": time.time(), "lost_b": tails["B"].lost, "lost_a": tails["A"].lost,
                           "before": tails["B"].welcomed, "open_t": None}
                    netsim.stop()
                    netsim = None
            if cut:
                if cut["open_t"] is None and time.time() - cut["t0"] >= a.cut_seconds:
                    netsim, why = open_netsim(a.netsim_port, ws_port, a.netsim_profile, wait_s=60)
                    cut["open_t"] = time.time()
                    if netsim is None:
                        events.append({"kind": "net_cut", "t_min": cut["t_min"], "ok": False,
                                       "detail": f"the proxy did not open again: {why}"})
                        cut = None
                if cut and cut["open_t"] is not None:
                    back = tails["B"].welcomed > cut["before"]
                    waited = time.time() - cut["open_t"]
                    if back or waited > a.reconnect_timeout:
                        saw = tails["B"].lost > cut["lost_b"]
                        a_kept = tails["A"].lost == cut["lost_a"]
                        detail = (f"{a.cut_seconds:g} s cut; B " + (f"back in {waited:.0f} s" if back else
                                  f"not back in {a.reconnect_timeout:.0f} s") + ("" if saw else "; B never logged the loss")
                                  + ("; A kept its connection" if a_kept else "; A lost its connection too"))
                        events.append({"kind": "net_cut", "t_min": cut["t_min"], "ok": back and a_kept, "detail": detail})
                        progress.append(f"net cut at {cut['t_min']:.1f} min: {'ok' if back and a_kept else 'FAIL'} ({detail})")
                        cut = None
                        write("running")
            chain.poll(now)
            time.sleep(1)
        if not meta.get("ended") and not meta.get("stopped"):
            meta["ended"] = "minutes reached"
        return rows, events, meta
    finally:
        if chain is not None:
            meta["scenarios"] = chain.summary(skipped)
        else:
            meta["scenarios"] = {"skipped": skipped}
        oi.kill(procs)
        server.kill()
        server.wait(timeout=30)
        srv_f.close()
        if netsim is not None:
            netsim.stop()
        for g, r in roots.items():  # the next run overwrites the instance logs; raw copies stay out of the repo
            if oi.log_path(r).exists():
                shutil.copyfile(oi.log_path(r), raw / f"{g}_eden_log.txt")
        write("done")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--minutes", type=float, default=120)
    ap.add_argument("--list", default=str(DEFAULT_LIST), help="scenario names, one a line")
    ap.add_argument("--scen-dir", default=str(rp.ESC), help="folder with <name>.A.txt (load.txt falls back to the tree's)")
    ap.add_argument("--saves", default=str(rp.BASE_SAVES))
    ap.add_argument("--server-exe", default="", help=f"orm_server to launch (default {rp.SERVER})")
    ap.add_argument("--port", type=int, default=27580, help="relay UDP port; its WebSocket port is this + 1")
    ap.add_argument("--res", type=int, default=1)
    ap.add_argument("--sample-every", type=float, default=5, help="minutes between samples")
    ap.add_argument("--warmup", type=float, default=10, help="minutes left out of the trends")
    ap.add_argument("--restart-at", type=float, default=30, help="minute of the relay restart (0: none)")
    ap.add_argument("--cut-at", type=float, default=60, help="minute of B's network cut (0: none)")
    ap.add_argument("--cut-seconds", type=float, default=20)
    ap.add_argument("--netsim-port", type=int, default=47209, help="B's proxy (127.0.0.1, 47201-47209: D-079)")
    ap.add_argument("--netsim-profile", default="lan", help="netsim profile: lan, region, far, bad")
    ap.add_argument("--no-netsim", action="store_true", help="B straight to the relay; no network cut")
    ap.add_argument("--reconnect-timeout", type=float, default=120, help="seconds to be welcomed again")
    ap.add_argument("--scenario-timeout", type=float, default=420)
    ap.add_argument("--gap", type=float, default=5, help="seconds between two scenarios")
    ap.add_argument("--boot-timeout", type=float, default=300)
    ap.add_argument("--lock-wait", type=float, default=720, help="minutes to wait for the game lock and STOP")
    ap.add_argument("--short-log", action="store_true", help="keep the emulator's 100 MiB log cap")
    ap.add_argument("--env", action="append", default=[], help="extra hook switch K=V (repeatable)")
    ap.add_argument("--tag", default="")
    ap.add_argument("--plan", action="store_true", help="no games: the list as it will be played and the schedule")
    ap.add_argument("--reanalizar", default="", metavar="DIR", help="no games: resumen.txt again from DIR's files")
    a = ap.parse_args(argv)

    if a.reanalizar:
        d = Path(a.reanalizar)
        meta = json.loads((d / "meta.json").read_text(encoding="utf-8"))
        word, lines = analyze(read_csv(d / "muestras.csv"), json.loads((d / "eventos.json").read_text(encoding="utf-8")),
                              meta)
        old = (d / "resumen.txt").read_text(encoding="utf-8").split("== analysis")[0].rstrip() \
            if (d / "resumen.txt").exists() else ""
        (d / "resumen.txt").write_text(old + "\n== analysis (again)\n" + "\n".join(lines) + "\n", encoding="utf-8")
        print(lines[-1] if word == "PASS" else "\n".join(l for l in lines if l.startswith(("VERDICT", "   "))))
        return {"PASS": 0, "WARN": 0, "FAIL": 1}.get(word, 2)

    scen = Path(a.scen_dir)
    names, skipped = classify(load_list(Path(a.list).read_text(encoding="utf-8")), scen)
    sched = (f"{a.minutes:g} min, a sample every {a.sample_every:g} min (warm-up {a.warmup:g}), relay restart at "
             + (f"{a.restart_at:g}" if a.restart_at else "never") + ", B's network cut at "
             + (f"{a.cut_at:g} for {a.cut_seconds:g} s" if a.cut_at and not a.no_netsim else "never"))
    if a.plan:
        print(f"scenarios ({len(names)}) from {scen}: {' '.join(names)}")
        print("skipped: " + ("; ".join(skipped) or "none"))
        print("schedule: " + sched)
        print("netsim: " + ("--no-netsim" if a.no_netsim else f"{NETSIM_DIR / 'netsim.py'} "
                            + ("found" if (NETSIM_DIR / "netsim.py").exists() else "MISSING (no network cut)")))
        print(f"game lock {LOCK}: " + ("held by someone now" if LOCK.exists() else "free")
              + ("; STOP file present" if STOP.exists() else ""))
        return 0 if names else 1

    tag = a.tag or time.strftime("aguante_%Y%m%d_%H%M")
    rep = orm_paths.REPORTS / "aguante" / tag
    raw = RAW_ROOT / tag
    rep.mkdir(parents=True, exist_ok=True)
    raw.mkdir(parents=True, exist_ok=True)
    head = [f"aguante {tag} {time.strftime('%Y-%m-%dT%H:%M:%S')}: two games, {sched}",
            f"emulator {oi.BUILD_BIN}; instances {oi.INST}; relay {a.server_exe or rp.SERVER}; scenarios {scen}",
            f"list ({len(names)}): {' '.join(names)}" + (f"; skipped: {'; '.join(skipped)}" if skipped else ""),
            f"raw logs: {raw}"]
    out = rep / "resumen.txt"

    def note(line: str) -> None:
        head.append(line)
        out.write_text("\n".join(head) + "\n", encoding="utf-8")

    if not names:
        note("VERDICT: FAIL (no playable scenario in the list)")
        return 1
    got = take_lock(LOCK, STOP, a.lock_wait * 60, note=note)
    if got != "taken":
        note(f"VERDICT: STOPPED (no game lock after {a.lock_wait:g} min{'; STOP file' if STOP.exists() else ''})")
        return 2
    note(f"game lock {LOCK} taken {time.strftime('%H:%M:%S')}")
    try:
        ready, why = oi.ready_for_games()  # no leftover game, our models unloaded, free RAM (ORM_READY_TIMEOUT)
        note("ready: " + why)
        if not ready:
            note("VERDICT: STOPPED (not ready)")
            return 2
        rows, events, meta = run(a, rep, raw, head, names, skipped)
    finally:
        give_lock(LOCK)
    word, lines = analyze(rows, events, meta)
    text = out.read_text(encoding="utf-8").rstrip()
    out.write_text(text + f"\ngame lock given back {time.strftime('%H:%M:%S')}\n== analysis\n" + "\n".join(lines) + "\n",
                   encoding="utf-8")
    return {"PASS": 0, "WARN": 0, "FAIL": 1}.get(word, 2)


if __name__ == "__main__":
    sys.exit(main())
