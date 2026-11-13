"""Remake harness core: isolated Fritchii instances (hidden window, Vulkan, no audio), never touching %APPDATA%.

Each instance is <ORM_INST_DIR>/nK/ with hard links of the build's files (the portable user/ folder is looked up beside the
EXE on Windows), its own user/ (keys copied, firmware .nca hard-linked, the rest copied), its own shader cache copy and
an ini. Only processes started here are stopped, by PID.

Paths: see orm_paths.py (ORM_ROOT and one environment variable per path).
"""
import ctypes, json, os, re, shutil, subprocess, sys, time, urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import protocolo
import orm_paths

# ORM_STACK=test8|test9|test10 (docs/ARNES.md, protocolo.STACKS): fills ORM_BUILD_BIN, ORM_SERVER_EXE, ORM_BOT_EXE and
# ORM_INST_DIR in os.environ where they are not set, before anything here or in r03_pareja.py reads them. A missing
# path makes ready_for_games() refuse with the line that names it.
STACK_PROBLEMS = protocolo.apply_stack()
# ORM_BUILD_BIN / ORM_INST_DIR: the emulator build and the instance root (defaults under ORM_ROOT, orm_paths.py).
BUILD_BIN = orm_paths.BUILD_BIN
REF_USER = orm_paths.REF_USER
INST = orm_paths.INST_DIR
# ORM_PUPPET_LOAD: the puppet pack "load" folder (built with r03_marioneta.ps1 or the tree's puppet_pack tool).
PUPPET_LOAD = orm_paths.PUPPET_LOAD
# ORM_KIT_LISTS: a kit folder (<tree>/tools/release/kit) whose shared_*.txt and coop_map.txt are copied beside the exe,
# where a scenario's relative LICHII_COOP_DUNGEON_LISTS=shared_dungeon_adult.txt or LICHII_COOP_MAP_FILE=coop_map.txt
# finds them (the games run with the instance as their folder, as a kit does). Unset: nothing copied (docs/SONDAS.md).
KIT_LISTS = Path(os.environ["ORM_KIT_LISTS"]) if os.environ.get("ORM_KIT_LISTS") else None
# ORM_MAP_TABLE: your coop_map.txt (tools/romfs/map_table.py of the tree, kept outside the repository), copied beside the exe too.
MAP_TABLE = Path(os.environ["ORM_MAP_TABLE"]) if os.environ.get("ORM_MAP_TABLE") else None
REPO = Path(__file__).resolve().parents[3]
# None in a checkout without the game: the unit tests still import this module.
# ORM_GAME_DIR (default ORM_ROOT/game), then <gift root>/juego.
GAME_DIRS = [orm_paths.GAME_DIR, REPO / "juego"]
NUP = next((n for d in GAME_DIRS if d.is_dir() for n in d.glob("*.nup")), None)
# The emulator profile folder differs on every install: ORM_PROFILE_ID names yours (the folder under
# nand/user/save/0000000000000000 of your emulator user folder); unset, the first one found beside ORM_PUPPET_LOAD's user folder is used.
def _profile_id():
    pid = os.environ.get("ORM_PROFILE_ID", "").strip()
    if pid:
        return pid
    base = Path(str(orm_paths.PUPPET_LOAD)).parent / "nand" / "user" / "save" / "0000000000000000"
    if base.is_dir():
        for d in sorted(base.iterdir()):
            if d.is_dir():
                return d.name
    return "YOUR_PROFILE_ID"


SAVE_REL = Path("nand/user/save/0000000000000000") / _profile_id() / "0400486024DEC000"
BS = chr(92)
FPS = re.compile(r"FPS: ([0-9.]+) speed: ([0-9.]+)%")
NO_WINDOW = 0x08000000


# oot_30fps: the game's native 30 fps (its timing follows real time, so full speed); half the cost. ORM_TEST_FPS30=0
# turns it off (a 60 fps measurement with ORM_TEST_HIGH_REFRESH=1).
TEST_FPS30 = os.environ.get("ORM_TEST_FPS30", "1") != "0"
TEST_NO_VIDEO = True  # nvdec_emulation Off: skips the intro videos, where a second instance stalled at boot (R0.3)
# "alive" threshold for the verdict: 30 fps tests run at 24-30 in heavy moments (R0.3 overnight run); a frozen game is
# at 0-10. The fps medians are always written to the summary.
# ORM_TEST_MIN_FPS overrides it (a capture run with A's window shown leaves B at 10-15 fps while it loads; the fps
# are not the point there).
MIN_FPS = int(os.environ.get("ORM_TEST_MIN_FPS", "0")) or (20 if TEST_FPS30 else 45)


def ini(backend: int = 1, res: int = 1, fps30: bool | None = None, no_video: bool | None = None) -> str:
    # backend 1 = Vulkan (Null stalls the remake, R0.2); res = ResolutionSetup index (1 = 1/2x); audio Null
    fps30 = TEST_FPS30 if fps30 is None else fps30
    no_video = TEST_NO_VIDEO if no_video is None else no_video

    def kv(k: str, v) -> str:
        return k + BS + "default=false\n" + k + "=" + str(v) + "\n"

    # ORM_TEST_HIGH_REFRESH=1: Fritchii's built-in 60 fps mode. Until Fritchii v5.6 its code cave shared
    # main+0x3c46280 with the puppet cave (the puppet patch was skipped whenever the mode was on); since v5.7 it lives
    # at main+0x3C46E00..0x3C46F10 and ours are below 0x3c46e00 again (docs/PIN_TEST8.md, patch 0008 retired)
    high = os.environ.get("ORM_TEST_HIGH_REFRESH", "0") == "1"
    return ("[Renderer]\n" + kv("backend", backend) + kv("resolution_setup", res)
            + kv("oot_30fps", "true" if fps30 else "false") + kv("nvdec_emulation", 0 if no_video else 2)
            + kv("oot_high_refresh", "true" if high else "false")
            + "[Audio]\n" + kv("output_engine", 3))


def prepare(k: int, exe_name: str = "eden-cli.exe", saves: Path | None = None, coop_ini: str | None = None,
            backend: int = 1, res: int = 1) -> Path:
    root = INST / f"n{k}"
    user = root / "user"
    # Fritchii 5.8+ runs portable only with user/ beside the exe; without it eden-cli exits 1 on this PC's old
    # %APPDATA% data (EMULADOR_584.md). Made first, so no launch ever sees the exe without it.
    user.mkdir(parents=True, exist_ok=True)
    for src in BUILD_BIN.rglob("*"):
        if src.is_file():
            dst = root / src.relative_to(BUILD_BIN)
            if dst.exists() and not os.path.samefile(src, dst):
                dst.unlink()  # a rebuilt binary: link the new one
            if not dst.exists():
                dst.parent.mkdir(parents=True, exist_ok=True)
                os.link(src, dst)
    if not (user / "keys" / "prod.keys").exists():
        shutil.copytree(REF_USER / "keys", user / "keys", dirs_exist_ok=True)
        for src in (REF_USER / "nand").rglob("*"):
            dst = user / "nand" / src.relative_to(REF_USER / "nand")
            if src.is_dir():
                dst.mkdir(parents=True, exist_ok=True)
            elif not dst.exists():
                dst.parent.mkdir(parents=True, exist_ok=True)
                os.link(src, dst) if src.suffix == ".nca" else shutil.copy2(src, dst)
    if backend != 2 and not (user / "cache" / "shader").exists():
        shutil.copytree(REF_USER / "cache" / "shader", user / "cache" / "shader")
    # The puppet Link mod, built from your own dump. An instance whose copy differs from ORM_PUPPET_LOAD (its
    # coop_puppet.json, the build's manifest) gets the new one: a pack rebuilt with more stand-ins (CoopGet_*,
    # CoopShot_*) never reached the instances made before it (2026-11-12, docs/SONDAS.md).
    pack = user / "load" / "0400486024DEC000" / "coop-puppet"
    src_manifest = PUPPET_LOAD / "0400486024DEC000" / "coop-puppet" / "coop_puppet.json"
    if PUPPET_LOAD.exists():
        old = pack / "coop_puppet.json"
        if pack.exists() and src_manifest.exists() and (
                not old.exists() or old.read_bytes() != src_manifest.read_bytes()):
            shutil.rmtree(pack)
        if not pack.exists():
            shutil.copytree(PUPPET_LOAD, user / "load", dirs_exist_ok=True)
    if saves is not None:
        dst = user / SAVE_REL
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(saves, dst)
    # The resume feature (LICHII_ORM_RESUME, on by default since orm/t9-integrado be86ec9) keeps each player's last
    # place in a room in user/config/coop-resume.txt and puts Link back there on join: an instance reused by the
    # next scenario started where the last one ended, not where its save stands (cartel, charla, 2026-11-12). Every
    # test starts from its save; a scenario about resume writes its own file or sets LICHII_ORM_RESUME_PLACE.
    # ORM_KEEP_RESUME=1 keeps it: the second session of a resume scenario on the instances of its first
    # (reanudar-mazmorra-2 after reanudar-mazmorra-1 reads the file the first one wrote).
    if os.environ.get("ORM_KEEP_RESUME") != "1":
        (user / "config" / "coop-resume.txt").unlink(missing_ok=True)
    if coop_ini is not None:
        (user / "config").mkdir(parents=True, exist_ok=True)
        (user / "config" / "coop.ini").write_text(coop_ini, encoding="ascii")
    if KIT_LISTS is not None and KIT_LISTS.is_dir():
        for src in [*KIT_LISTS.glob("shared_*.txt"), *KIT_LISTS.glob("coop_map.txt")]:
            shutil.copy2(src, root / src.name)
    if MAP_TABLE is not None and MAP_TABLE.is_file():
        shutil.copy2(MAP_TABLE, root / "coop_map.txt")
    (root / "orm.ini").write_text(ini(backend, res), encoding="utf-8")
    log = log_path(root)
    if log.exists():
        log.unlink()
    if not (root / exe_name).exists() or not user.is_dir():
        raise SystemExit(f"{root}: no portable user folder beside the exe; refusing to touch %APPDATA%")
    return root


def log_path(root: Path) -> Path:
    return root / "user" / "log" / "eden_log.txt"


def log_text(root: Path) -> str:
    p = log_path(root)
    return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""


def launch(root: Path, extra_env: dict | None = None, exe_name: str = "eden-cli.exe") -> subprocess.Popen:
    env = {**os.environ, "EDEN_NX2_HIDE_WINDOW": "1", "EDEN_NX2_FPS_LOG": "1", **(extra_env or {})}  # extra wins
    if NUP is None:
        raise FileNotFoundError(f"no game package (*.nup) in {', '.join(str(d) for d in GAME_DIRS)}")
    return subprocess.Popen([str(root / exe_name), "--config", str(root / "orm.ini"), "-g", str(NUP)], cwd=root,
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=NO_WINDOW)


def wait_log(roots: list[Path], pattern: str, timeout: float, procs: list | None = None) -> float | None:
    """Seconds until every log matches pattern, or None on timeout or if a process died."""
    rx, t0 = re.compile(pattern), time.time()
    while time.time() - t0 < timeout:
        if procs and any(p.poll() is not None for p in procs):
            return None
        if all(rx.search(log_text(r)) for r in roots):
            return time.time() - t0
        time.sleep(1)
    return None


def fps_after(root: Path, marker: str) -> list[float]:
    text = log_text(root)
    i = text.find(marker)
    return [float(a) for a, _ in FPS.findall(text[i:] if i >= 0 else "")]


def kill(procs: list) -> None:
    """Kill and WAIT: a game that is still closing keeps its files locked and the next prepare() fails."""
    for p in procs:
        if p.poll() is None:
            p.kill()
    for p in procs:
        try:
            p.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pass


def wait_no_instances(timeout: float = 90) -> bool:
    """True once no eden-cli/lichii from the instance folder is running (left over from an earlier test)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        out = subprocess.run(["tasklist", "/NH"], capture_output=True, text=True, creationflags=NO_WINDOW).stdout.lower()
        if "eden-cli.exe" not in out and "lichii.exe" not in out:
            return True
        time.sleep(2)
    return False


OLLAMA = "http://127.0.0.1:11434"
# ORM_UNLOAD_MODELS: comma-separated name prefixes of local Ollama models to unload before a run (the GPU/RAM serve
# one thing at a time). Unset: nothing is unloaded; a model that matches no prefix is never touched.
OUR_MODELS = tuple(m.strip() for m in os.environ.get("ORM_UNLOAD_MODELS", "").split(",") if m.strip())
MIN_FREE_GB = 14  # two games peak at ~7 GB each; less than this and Windows pages (game frozen 30-40 s, R0.3)


def free_ram_gb() -> float:
    class MS(ctypes.Structure):
        _fields_ = [("len", ctypes.c_ulong), ("load", ctypes.c_ulong)] + [(n, ctypes.c_ulonglong) for n in
                    ("total", "avail", "ptotal", "pavail", "vtotal", "vavail", "xavail")]
    m = MS(); m.len = ctypes.sizeof(MS)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    return m.avail / 2**30


def loaded_models() -> list[str]:
    try:
        with urllib.request.urlopen(OLLAMA + "/api/ps", timeout=5) as r:
            return [m["name"] for m in json.load(r).get("models", [])]
    except OSError:
        return []  # Ollama not running: nothing loaded


def unload_our_models() -> list[str]:
    """Unload the local models named in ORM_UNLOAD_MODELS; returns the names unloaded."""
    gone = []
    for name in loaded_models():
        if not OUR_MODELS or not name.startswith(OUR_MODELS):
            continue
        body = json.dumps({"model": name, "keep_alive": 0}).encode()
        try:
            urllib.request.urlopen(urllib.request.Request(OLLAMA + "/api/generate", body,
                                                          {"Content-Type": "application/json"}), timeout=60).read()
            gone.append(name)
        except OSError:
            pass
    return gone


def ready_for_games(min_free_gb: float = MIN_FREE_GB, timeout: float = 120) -> tuple[bool, str]:
    """No leftover game, our models unloaded, enough free RAM. (ok, one line for the summary).
    ORM_READY_TIMEOUT (seconds) waits longer for the RAM: with several agents compiling at once the free RAM drops
    below 14 GB for many minutes, and a run that starts anyway pages and freezes (2026-11-11, todos_test8b)."""
    timeout = float(os.environ.get("ORM_READY_TIMEOUT", "0")) or timeout
    if STACK_PROBLEMS:
        return False, "; ".join(STACK_PROBLEMS)
    if not wait_no_instances():
        return False, "a game from an earlier test is still running"
    gone = unload_our_models()
    t0 = time.time()
    while time.time() - t0 < timeout:
        free = free_ram_gb()
        if free >= min_free_gb:
            return True, f"free RAM {free:.1f} GB" + (f" (unloaded {', '.join(gone)})" if gone else "")
        time.sleep(3)
    left = loaded_models()
    return False, (f"only {free_ram_gb():.1f} GB RAM free (< {min_free_gb})"
                   + (f"; models still loaded: {', '.join(left)}" if left else ""))
