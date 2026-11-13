"""Unit tests (no game) for caliente.py, the hot instances (docs/ARNES.md, "Instancias calientes"): the hot form of a
script, the undo of what a scenario leaves, the plan of our scenarios, and the chain itself against fake games (a thread
per game that reads its script file and writes a log the way the hook does)."""
import re
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import caliente as cal
import r03_pareja as rp
import orm_paths

ESC = Path(__file__).parent / "escenarios"
TITLE = "wait 1200\nmark title\npress A\nwait 180\npress A\nwait 180\npress A\nwait 300\npress A   # loads the save\n"


def test_hot_warp_script_moves_the_warp_past_the_title():
    text = "# boss\n#! noload\nmark start\nwarp InsideTheDekuTree StartPos_Boss child\n" + TITLE + "wait 2400\nmark in\n"
    hot, why = cal.hot_warp_script(text)
    assert why == ""
    assert hot.splitlines() == ["# boss", "#! noload", "mark start", "mark title",
                                "warp InsideTheDekuTree StartPos_Boss child", "wait 2400", "mark in"]


def test_hot_warp_script_keeps_a_late_games_delay():
    text = "warp X Entrance\nwait 1200\nmark title\nwait 2400   # 40 s late\nmark late\n" + TITLE.split("mark title\n")[1]
    hot, _ = cal.hot_warp_script(text + "mark b-in\n")
    assert [cal.code(l) for l in hot.splitlines()] == ["mark title", "wait 2400", "mark late", "warp X Entrance", "mark b-in"]


@pytest.mark.parametrize("text, why", [
    ("warp Market_Child\n" + TITLE, "names no start point"),
    ("warp X Y adult\n".replace("Y adult", "adult") + TITLE, "names no start point"),
    ("press A\nwarp X Y\n" + TITLE, "a press before its first warp"),
    (TITLE, "presses through the title without a warp"),
    ("warp X Y\nwait 1200\npress A\n", "no 'mark title'"),
    ("warp X Y\nwait 1200\nmark title\npress A\npress A\n", "fewer than four presses"),
    ("warp X Y\nwait 1200\nmark title\npress A\nstick 0 1 30\npress A\npress A\npress A\n", "inside the title screen"),
    ("warp X Y\nwait 1200\nmark title\npress A\npress B\npress A\npress A\n", "four 'press A'"),
])
def test_hot_warp_script_refuses(text, why):
    hot, got = cal.hot_warp_script(text)
    assert hot is None and why in got


def test_every_hot_script_of_ours_keeps_its_marks_and_parses():
    """Each '#! noload' file that runs hot: the same marks in the same order, the same warps, only commands the hook
    knows, and no title presses left before its warp."""
    known = {"wait", "hold", "press", "stick", "rstick", "goto", "toward", "warp", "place", "chat", "give", "wear",
             "assign", "life", "gate", "bottle", "event", "flag", "int", "key", "enum", "age", "mark", "await", "skip",
             "talkthrough"}
    n = 0
    for f in sorted(ESC.glob("*.[AB].txt")):
        text = f.read_text(encoding="utf-8")
        if not rp.scenario_plan([text])[0]:
            continue
        hot, _ = cal.hot_warp_script(text)
        if hot is None:
            continue
        n += 1
        marks = lambda t: re.findall(r"^\s*mark\s+(\S+)", t, re.M)
        assert marks(hot) == marks(text), f.name
        cmds = [cal.words(l)[0].lower() for l in hot.splitlines() if cal.words(l)]
        assert set(cmds) <= known, f.name
        assert cmds.count("warp") == [cal.words(l)[:1] for l in text.splitlines()].count(["warp"]), f.name
        assert "press" not in cmds[:cmds.index("warp")], f.name
    assert n > 400


def test_script_seconds():
    t = "wait 60\npress A\npress B 10\nhold A 30 half\nstick 0 1 60\ngoto 1 2 0.5 120\ngoto 1 2\ntoward 1 2 60\nmark x\n"
    assert cal.script_seconds(t) == pytest.approx((60 + 12 + 20 + 30 + 60 + 120 + 600 + 60) / 60)


LOG = """\
coop: test flag Door_Open (0x12345678) = 1: queued (it read 0)
coop: test flag Door_Open (0x12345678) = 0: queued (it read 1)
coop: test int Rupees (0x0000abcd) = 99: queued (it read 12)
coop: test key 0x00000000deadbeef = 1: queued (it read 0)
coop: test enum Sequence_Era (0x11112222): find_type 7, value Child (0x0badf00d); the game's values 2: ...; entry 0
coop: test enum Sequence_Era = Adult (0x0f00ba11): queued
coop: test enum Other (0x33334444): find_type 7, value 0x00000005 (no listed name); the game's values none read; entry none
coop: test life 0: written (was 12 of 12)
coop: test life 8: written (was 0 of 12)
coop: test bottle B Fairy: before: ...
coop: test bottle B Fairy: written (now Fairy, IsValid 1, StockNum 1)
coop: test bottle C Fairy: written (now Fairy, IsValid 1, StockNum 1)
coop: test bottle C restore: written (item Empty)
coop: test give Bow: done
coop: downed: alive -> downed (life 0)
coop: downed: downed -> reviving: revive requested by P2
coop: downed: reviving -> alive: revived (by P2)
"""


def test_leftovers_undo_what_the_script_set():
    undo, notes, dirty = cal.leftovers(LOG)
    assert undo == ["flag Door_Open 0", "int Rupees 12", "key 0x00000000deadbeef 0", "enum Sequence_Era 0x0badf00d",
                    "life 12", "bottle B restore"]
    assert notes == ["items given stay: Bow"]
    assert dirty == []


def test_leftovers_dirty():
    _, _, dirty = cal.leftovers("coop: boss: boss beaten (Gohma)\ncoop: downed: alive -> downed (life 0)\n")
    assert dirty == ["a boss was beaten", "Link left downed"]
    assert cal.leftovers("coop: story: STORY_EVENT 3 from P1\n")[2] == ["a shared story event was played"]


def test_caliente_options():
    o = cal.caliente_options({"A": "#! caliente antes flag Door_Open 0\n#! caliente stagger 45\n",
                              "B": "#! caliente despues frío\n"})
    assert o["before"] == {"A": ["flag Door_Open 0"], "B": []}
    assert o["stagger"] == 45 and o["after_cold"] and o["cold"] is None and not o["errors"]
    assert cal.caliente_options({"A": "#! caliente no the boss dies\n"})["cold"] == "the boss dies"
    assert cal.caliente_options({"A": "#! caliente antes rm -rf\n#! caliente quizas\n"})["errors"] == [
        "#! caliente antes rm -rf", "#! caliente quizas"]


def test_homes_and_bounce():
    assert cal.recipe_home("02-deku")[0] == cal.Home("KokiriForest_Child", "FrontDekuTree")
    h = cal.recipe_home("20-ganon")[0]
    assert h.warp() == "warp GanonCastle GanonCastleEntrance adult" and h.adult
    assert cal.family_home(None, rp.BASE_SAVES)[0] == cal.DEFAULT_HOME
    assert cal.family_home(None, Path("/elsewhere"))[0] is None
    assert cal.family_home("/work/saves/mia", Path("/work/saves/mia"))[0] is None
    assert cal.parse_home("Market_Child Market_Entrance") == cal.Home("Market_Child", "Market_Entrance")
    assert cal.parse_home("GanonCastle GanonCastleEntrance adult 1 2.5 -3") == cal.Home(
        "GanonCastle", "GanonCastleEntrance", "adult", (1.0, 2.5, -3.0))
    with pytest.raises(ValueError):
        cal.parse_home("OnlyAScene")
    assert cal.bounce_for({"InsideTheDekuTree"}, False).scene == "HyruleField_Child"
    assert cal.bounce_for({"HyruleField_Child"}, False).scene == "KokiriForest_Child"
    assert cal.bounce_for({"WaterTemple"}, True).scene == "HyruleField_Adult"


def args(**kw):
    base = dict(players=None, transport=None, timeout=None, header_switches=False, saves=None, bots=0,
                scen_dir=str(ESC), recheck=True, max_log_mb=0, restart_every=0, wait_games=0, port=27560, res=1)
    base.update(kw)
    return SimpleNamespace(**base)


def test_plan_of_our_scenarios():
    sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
    pytest.importorskip("romfs_read", reason="building the 02-deku save needs tools/romfs of the source tree and your romfs")
    if not orm_paths.ROMFS.exists():
        pytest.skip("needs your merged romfs (ORM_ROMFS)")
    a = args(transport="ws", players=2)
    boss = cal.make_plan("jefe-gohma-caer", ESC, a, [])
    assert (boss.kind, boss.cold, boss.error) == ("warp", "", "")
    assert boss.targets == {"InsideTheDekuTree"} and not boss.adult
    assert boss.saves_value == "02-deku" and boss.saves["A"] == boss.saves["B"]
    assert "LICHII_COOP_DOWNED=1" in boss.env
    cartel = cal.make_plan("cartel", ESC, a, [])
    assert (cartel.kind, cartel.cold) == ("home", "") and cartel.run["A"] == (ESC / "cartel.A.txt").read_text(encoding="utf-8")
    assert "bots" in cal.make_plan("ancho-seis", ESC, a, []).cold
    assert "start point" in cal.make_plan("botella-mercado", ESC, a, []).cold
    fams, cold = cal.families([boss, cal.make_plan("jefe-gohma-derrota", ESC, a, []), cartel])
    assert [[p.name for p in f] for f in fams] == [["jefe-gohma-caer", "jefe-gohma-derrota"], ["cartel"]] and not cold


def test_scenario_names_only():
    assert cal.scenario_names(ESC, ["cartel,charla", "edad"]) == ["cartel", "charla", "edad"]


# ------------------------------------------------------------------------------------------ fake games

class FakeGame(threading.Thread):
    """The hook, enough for the runner: rereads the script file when it changes, plays it (ticks / SPEED seconds), logs
    the lines the runner reads, an fps line every 20 ms; dies at a mark named in `die_at`."""
    SPEED = 400.0

    def __init__(self, script: Path, log: Path, die_at: set):
        super().__init__(daemon=True)
        self.script, self.log, self.die_at = script, log, die_at
        self.stop = threading.Event()
        self.flags = {}
        self.f = open(log, "a", encoding="utf-8")

    def say(self, line: str) -> None:
        self.f.write(f"[ 0.0] Core <Info> {line}\n")
        self.f.flush()

    def run(self) -> None:
        seen, steps, due, next_fps = None, [], 0.0, 0.0
        try:
            while not self.stop.is_set():
                now = time.time()
                if now >= next_fps:
                    self.say("FPS: 30.0 speed: 100%")
                    next_fps = now + 0.02
                st = self.script.stat()
                if (st.st_mtime_ns, st.st_size) != seen:
                    seen = (st.st_mtime_ns, st.st_size)
                    text = self.script.read_text(encoding="utf-8")
                    steps = [cal.words(l) for l in text.splitlines() if cal.words(l)]
                    if steps:
                        self.say(f"coop: test script started: {len(steps)} steps")
                    due = now
                while steps and time.time() >= due:
                    w = steps.pop(0)
                    cmd = w[0].lower()
                    if cmd in ("wait", "hold", "stick", "press", "toward"):
                        n = [int(x) for x in w[1:] if x.isdigit()]
                        due = time.time() + (n[-1] if n else 6) / 60 / self.SPEED
                    elif cmd == "mark":
                        if w[1] in self.die_at:
                            return
                        self.say(f"coop: test mark {w[1]}")
                    elif cmd == "warp":
                        self.say(f"coop: test warp to {w[1]} (start {w[2] if len(w) > 2 else '-'}): scene set")
                        self.say("coop: test warp done after 0.1 s, Link at 1.00 2.00 (taken)")
                    elif cmd == "flag":
                        was = self.flags.get(w[1], 0)
                        self.flags[w[1]] = int(w[2])
                        self.say(f"coop: test flag {w[1]} (0x00000001) = {w[2]}: queued (it read {was})")
                    elif cmd == "place":
                        self.say(f"coop: test place {w[1]} {w[2]} {w[3]}: done")
                    elif cmd == "event":
                        self.say(f"coop: test event {w[1]}: Link 0x1 at 0 0 0, scene X, event none")
                time.sleep(0.003)
        finally:
            self.f.close()


class FakePair(cal.Pair):
    """A Pair whose games are FakeGame threads (no server: its log stays empty)."""
    die_at: set = set()
    boots: list = []

    def boot(self, lines):
        FakePair.boots.append(self.number)
        self.threads = {}
        for g in self.games:
            root = self.out / f"fake{self.number}{g}"
            root.mkdir()
            self.roots[g] = root
            self.scripts[g] = root / "script.txt"
            self.scripts[g].write_text("", encoding="utf-8")
            (root / "user/log").mkdir(parents=True)
            self.log(g).write_text("", encoding="utf-8")
            self.threads[g] = FakeGame(self.scripts[g], self.log(g), FakePair.die_at)
            self.threads[g].start()
        self.server_log.write_text("", encoding="utf-8")
        lines.append(f"boot {self.number}: fake")
        return True

    def alive(self):
        return all(t.is_alive() for t in self.threads.values())

    def shutdown(self):
        for t in self.threads.values():
            t.stop.set()
            t.join(timeout=5)


FAST = cal.Timing(poll=0.01, read=3, warp=3, settle=3, stagger=0.05, tail=0.3, boot=3, load_dip=0, margin=5)


def chain(tmp_path, monkeypatch, scen: dict, names: list[str], **kw):
    sd = tmp_path / "scen"
    sd.mkdir()
    for fname, text in scen.items():
        (sd / fname).write_text(text, encoding="utf-8")
    out = tmp_path / "out"
    out.mkdir()
    monkeypatch.setattr(cal, "STOP", tmp_path / "STOP")
    a = args(**dict(dict(scen_dir=str(sd), timeout=5), **kw))
    plans = [cal.make_plan(n, sd, a, []) for n in names]
    fams, _ = cal.families(plans)
    homes = {f[0].family(): cal.family_home(f[0].saves_value, f[0].saves.get("A")) for f in fams}
    colds = []

    def cold(p, a, out):
        colds.append(p.name)
        return cal.Outcome("RESULT: PASS", True, seconds=1)

    FakePair.boots = []
    s = cal.run_all(plans, a, out, out / "resumen.txt", FAST, make_pair=FakePair, cold=cold, homes=homes)
    return s, (out / "resumen.txt").read_text(encoding="utf-8"), out, colds


SETS = "mark start\nwait 60\nflag Door_Open 1\nwait 30\nmark end\n#! expect A coop: test flag Door_Open\n"
WALKS = "mark start\nwait 30\nmark bend\n"
BOSS = ("#! noload\n#! expect A coop: test warp done\nmark start\nwarp Dungeon Entrance\n" + TITLE
        + "wait 120\nmark a-in\n")
BOSS_B = "#! noload\nmark start\nwarp Dungeon Entrance\n" + TITLE + "wait 60\nmark b-in\n"


def test_chain_two_scenarios_in_one_boot_with_the_undo(tmp_path, monkeypatch):
    s, summary, out, colds = chain(tmp_path, monkeypatch, {
        "s1.A.txt": SETS, "s1.B.txt": WALKS, "s2.A.txt": BOSS, "s2.B.txt": BOSS_B}, ["s1", "s2"])
    assert (s["pass"], s["fail"], s["hot"], s["cold"], s["boots"]) == (2, 0, 2, 0, 1), summary
    assert re.search(r"^s1 +RESULT: PASS \(expect 1/1\) .*\[caliente\]$", summary, re.M), summary
    assert re.search(r"^s2 +RESULT: PASS \(expect 1/1\) .*\[caliente\]$", summary, re.M), summary
    s1 = (out / "s1_resumen.txt").read_text(encoding="utf-8")
    assert "reset A: home KokiriForest_Child" in s1 and "placed at (-82.0, 122.3, 33.7)" in s1
    assert "undo next for A: flag Door_Open 0" in s1
    s2 = (out / "s2_resumen.txt").read_text(encoding="utf-8")
    assert "reset A: warp HyruleField_Child done" in s2 and "undo flag Door_Open 0" in s2
    assert "A started ('coop: test warp done')" in s2 and "B started" in s2
    # each scenario's part of the log only: s2's has no line of s1
    a2 = (out / "s2_A_eden_log.txt").read_text(encoding="utf-8")
    assert "coop: test mark a-in" in a2 and "coop: test mark end" not in a2 and "Door_Open = 1" not in a2
    assert not colds


def test_a_game_that_dies_runs_the_scenario_cold_and_boots_again(tmp_path, monkeypatch):
    FakePair.die_at = {"boom"}
    try:
        s, summary, out, colds = chain(tmp_path, monkeypatch, {
            "s1.A.txt": "mark start\nwait 30\nmark boom\nmark end\n", "s2.A.txt": WALKS}, ["s1", "s2"])
    finally:
        FakePair.die_at = set()
    assert colds == ["s1"] and FakePair.boots == [1, 2], summary
    assert re.search(r"^s1 .*\[frio tras caliente: bad state: a game or the server exited", summary, re.M), summary
    assert re.search(r"^s2 +RESULT: PASS .*\[caliente\]$", summary, re.M), summary
    assert (out / "s1_caliente_resumen.txt").exists()  # the hot try kept beside the cold one


@pytest.mark.parametrize("recheck", [True, False])
def test_a_hot_fail_is_checked_cold(tmp_path, monkeypatch, recheck):
    s, summary, _, colds = chain(tmp_path, monkeypatch, {
        "s1.A.txt": "#! expect A never logged\nmark start\nwait 30\nmark end\n"}, ["s1"], players=1, recheck=recheck)
    if recheck:
        assert colds == ["s1"] and re.search(r"\[frio tras caliente: hot FAIL \(expect: 1 failed", summary), summary
    else:
        assert not colds and re.search(r"^s1 +RESULT: FAIL \(expect 0/1\) .*\[caliente\]$", summary, re.M), summary


def test_a_dirty_scenario_closes_the_pair(tmp_path, monkeypatch):
    s, summary, _, colds = chain(tmp_path, monkeypatch, {
        "s1.A.txt": "mark start\nevent Dm_Something\nwait 30\nmark end\n", "s2.A.txt": WALKS,
        "s3.A.txt": "#! caliente no it needs a fresh save\nmark start\n"}, ["s1", "s2", "s3"], players=1)
    assert FakePair.boots == [1, 2] and colds == ["s3"], summary
    assert re.search(r"^s1 .*\[caliente; boot again: A: a story event was asked", summary, re.M), summary
    assert re.search(r"^s3 .*\[frio: it needs a fresh save\]$", summary, re.M), summary


def test_no_progress_is_a_bad_state(tmp_path, monkeypatch):
    s, summary, _, colds = chain(tmp_path, monkeypatch, {
        "s1.A.txt": "mark start\nwait 100000\nmark end\n"}, ["s1"], players=1, timeout=1)
    assert colds == ["s1"] and "bad state: no new mark in 1 s" in summary, summary


def test_secuencia_hands_the_list_to_caliente():
    ps = (Path(__file__).parent / "secuencia.ps1").read_text(encoding="utf-8")
    assert "[switch]$Caliente" in ps and "caliente.py" in ps and "--no-lock" in ps and "--header-switches" in ps
