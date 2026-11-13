"""Unit tests (no game) for r03_pareja: scenario_plan (when a scenario loads the save itself, no load.txt first) and the
content directives (#! expect / never / count), these also over the logs captured in <ORM_REPORTS>/pareja.
"""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import r03_pareja as rp
import orm_paths

WALK_A = "# walk\nmark start\nstick 0 0.5 90\nmark walked\n"
BOSS_A = ("# boss\n#! noload\n#! start coop: test warp done\n#! timeout 400\nmark start\n"
          "warp WaterTemple StartPos_SageOfWater\nwait 1200\nmark title\npress A\nwait 180\n")
BOSS_B = "mark start\nwarp WaterTemple WaterTemple_FromLakeHylia\nwait 1200\nmark title\npress A\n"


def test_walk_keeps_load_txt():
    assert rp.scenario_plan([WALK_A]) == (False, "coop: test warp done")


def test_noload_header_and_directive():
    assert rp.scenario_plan([BOSS_A, BOSS_B]) == (True, "coop: test warp done")


def test_custom_start_directive():
    t = BOSS_A.replace("coop: test warp done", "coop: boss: in the boss room")
    assert rp.scenario_plan([t]) == (True, "coop: boss: in the boss room")


def test_warp_before_press_without_header():
    t = "mark start\nwarp HyruleField_Child X  # comment\nwait 1200\nmark title\npress A\n"
    assert rp.scenario_plan([t]) == (True, "coop: test warp done")


def test_warp_after_load_is_not_noload():
    assert rp.scenario_plan(["press A\nwait 180\nwarp X Y\n"])[0] is False


def test_legacy_note():
    assert rp.scenario_plan(["# Run with -NoLoad: ...\nmark start\n"])[0] is True


def test_noload_without_warp_starts_with_its_script():
    # late/epona/crawl press through the title themselves: no warp, so never a 'test warp done'
    t = "# late\n#! noload\nwait 1200\nmark title\npress A\nwait 180\n"
    assert rp.scenario_plan([t, t]) == (True, "coop: test script started")
    assert rp.scenario_plan(["# Run with -NoLoad\nmark start\nstick 0 0.6 60\n"]) == (True, "coop: test script started")


def test_real_files_if_present():
    d = rp.SCEN
    for n, exp in (("walk", False), ("boss", True), ("warp", True)):
        f = d / f"{n}.A.txt"
        if f.exists():
            assert rp.scenario_plan([f.read_text(encoding="utf-8")])[0] is exp


def test_directive_without_letter_reads_its_own_file():
    # the letterless form (charla-mido): the directive reads the log of the file it sits in
    a = "#! expect coop: talk: 0x502f6cea \\S+ 1 -> 0\n#!expect   coop: npc: talking to Npc_Kokiri_Mido  \n# no: #! expect x\n"
    b = "#! never coop: talk: .* 0 -> 1\n#! count coop: flags: talk flag >= 2\n"
    d = rp.parse_directives([a, b])
    assert [(x["kind"], x["game"], x["regex"], x["error"]) for x in d] == [
        ("expect", "A", r"coop: talk: 0x502f6cea \S+ 1 -> 0", ""), ("expect", "A", "coop: npc: talking to Npc_Kokiri_Mido", ""),
        ("never", "B", "coop: talk: .* 0 -> 1", ""), ("count", "B", "coop: flags: talk flag", "")]
    assert d[3]["op"] == ">=" and d[3]["n"] == 2
    assert rp.parse_directives([b], games="B")[0]["game"] == "B"
    log = ("coop: test script started\n[1] x: coop: npc: talking to Npc_Kokiri_Mido_Child 0x1 owner P2, claimed\n"
           "[2] x: coop: talk: 0x502f6cea KokiriForest_Npc_Kokiri_Mido_Child_Blocker 1 -> 0 in KokiriForest_Child\n")
    assert [ok for ok, _ in rp.evaluate_directives(d[:2], {"A": log})] == [True, True]
    assert [ok for ok, _ in rp.evaluate_directives(d[:2], {"A": log.splitlines()[1]})] == [False, True]


def test_charla_mido_directives_parse_if_present():
    esc = Path(__file__).parent / "escenarios"
    files = [esc / f"charla-mido.{side}.txt" for side in "AB"]
    if not all(f.exists() for f in files):
        pytest.skip("no charla-mido scenario")
    d = rp.parse_directives([f.read_text(encoding="utf-8") for f in files])
    assert {x["game"] for x in d} == {"A", "B"} and not [x for x in d if x["error"]]


def test_all_scenario_directives_parse():
    esc = Path(__file__).parent / "escenarios"
    bad = []
    for fa in sorted(esc.glob("*.A.txt")):
        fb = fa.with_name(fa.name[:-6] + ".B.txt")
        files = [fa] + ([fb] if fb.exists() else [])
        bad += [f"{fa.name[:-6]}: {x['raw']} ({x['error']})"
                for x in rp.parse_directives([f.read_text(encoding="utf-8") for f in files]) if x["error"]]
    assert not bad, "\n".join(bad)


def test_new_marks_in_order():
    log = ("x coop: test script started\n" "A coop: test mark start\n" "noise\n" "B coop: test mark at-npc\n"
           "coop: test goto timed out at line 12\n" "coop: test mark closed\n")
    assert rp.new_marks(log, 0) == ["start", "at-npc", "closed"]
    assert rp.new_marks(log, 2) == ["closed"]
    assert rp.new_marks(log, 3) == []
    assert rp.new_marks("", 0) == []


SCN = ("# x\n#! expect A coop: event running .*Sign within try..reading\n#! never B show -> hold\n"
       "#! count A coop: puppet: P2 \\S+ spawned >= 1\n#! count B foo == 0 within ..end\n#! expect C nope\n"
       "#! count A foo\n#! expect A (\nmark start\n")
LOG_A = ("coop: test script started: 15 steps\ncoop: test mark loaded\ncoop: event running 'Sign' (before start)\n"
         "coop: test script started: 9 steps\ncoop: test mark start\ncoop: puppet: P2 CoopPuppet_Child spawned\n"
         "coop: test mark try\ncoop: event running 'Obj_Sign_03' (we play 'EvtTurn')\ncoop: test mark reading\n")


def test_parse_directives():
    d = rp.parse_directives([SCN])
    assert [(x["kind"], x["game"], x["regex"], x["within"], x["op"], x["n"]) for x in d[:4]] == [
        ("expect", "A", "coop: event running .*Sign", ("try", "reading"), None, None),
        ("never", "B", "show -> hold", None, None, None),
        ("count", "A", "coop: puppet: P2 \\S+ spawned", None, ">=", 1),
        ("count", "B", "foo", ("", "end"), "==", 0)]
    assert [x["error"] for x in d[:4]] == ["", "", "", ""]
    assert all(x["error"] for x in d[4:]) and len(d) == 7  # bad game letter, count without N, bad regex
    assert rp.parse_directives(["# walk\n#! noload\n#! start coop: x\nmark start\n"]) == []


def test_scenario_plan_ignores_directives():
    assert rp.scenario_plan([SCN]) == (False, "coop: test warp done")


def test_evaluate_directives():
    d = rp.parse_directives([SCN])[:4]
    res = rp.evaluate_directives(d, {"A": LOG_A, "B": "coop: test script started\ncoop: puppet: P1 show -> hold\n"})
    assert [ok for ok, _ in res] == [True, False, True, True]
    assert res[0][1] == "expect ok: expect A coop: event running .*Sign (A within try..reading)"
    assert res[1][1] == "expect FAIL: never B show -> hold (B after start, 1 lines)"
    # load.txt's part of the log (before the last 'test script started') does not count
    early = rp.parse_directives(["#! never A before start\n#! expect A test mark loaded\n"])
    assert [ok for ok, _ in rp.evaluate_directives(early, {"A": LOG_A})] == [True, False]


def test_evaluate_missing_mark_log_and_errors():
    d = rp.parse_directives(["#! expect A event within nomark..end\n#! expect B x\n#! expect A (\n"
                             "#! expect A Obj_Sign within try..nomark\n"])
    res = rp.evaluate_directives(d, {"A": LOG_A})
    assert [ok for ok, _ in res] == [False, False, False, True]
    assert "mark 'nomark' never reached" in res[0][1] and "no log for B" in res[1][1] and "bad regex" in res[2][1]
    assert "to the end" in res[3][1]


# The proof without games: every directive of our scenarios holds on the logs captured in <ORM_REPORTS>/pareja. cartel's A side
# reads cartel-name (the run after the hook logged event names; the two-game cartel run is from before).
CAPTURED = {"cartel": {"A": "cartel-name", "B": "cartel"}}
OURS = ("cartel", "charla", "caer-pareja", "caer-solo", "caer-derrota", "historia-largo", "historia-rezagado",
        "caer-camara")
# Directives for lines the hook logs only since a build newer than the captured run (left out of this proof until the
# scenario is captured again): caer-derrota's "coop: continue: the game over event is ..." (orm/riesgos-mundo, in
# orm/t9-integrado; docs/RIESGOS_RESUELTOS.md "Mundo e historia").
NEWER_THAN_CAPTURE = {"caer-derrota": ("coop: continue: the game over event is",)}


@pytest.mark.parametrize("scenario", OURS)
def test_directives_hold_on_captured_logs(scenario):
    esc, logs_dir = Path(__file__).parent / "escenarios", orm_paths.REPORTS / "pareja"
    d = rp.parse_directives([f.read_text(encoding="utf-8") for f in (esc / f"{scenario}.A.txt", esc / f"{scenario}.B.txt")
                             if f.exists()])
    assert d and not [x for x in d if x["error"]], f"{scenario}: no directives or a bad one"
    newer = NEWER_THAN_CAPTURE.get(scenario, ())
    d = [x for x in d if not x["regex"].startswith(newer)] if newer else d
    logs = {}
    for g in sorted({x["game"] for x in d}):
        p = logs_dir / f"{CAPTURED.get(scenario, {}).get(g, scenario)}_{g}_eden_log.txt"
        if not p.exists():
            pytest.skip(f"no captured log {p.name}")
        logs[g] = p.read_text(encoding="utf-8", errors="replace")
    bad = [line for ok, line in rp.evaluate_directives(d, logs) if not ok]
    assert not bad, "\n".join(bad)


# --- six players: one game and bots (docs/BOTS.md, PLAN_100 F14) ---------------------------------------------------
ESC = Path(__file__).parent / "escenarios"
SEIS = sorted(f.name[:-6] for f in ESC.glob("seis-*.A.txt"))


def test_scenario_options_headers_and_seis_defaults():
    o = rp.scenario_options("x", ["#! players 1\n#! bots 3\n#! bot-arg -real -look auto\n#! bot-arg -anim HorseMove\n"
                                  "#! transport ws\n#! env LICHII_COOP_DOWNED=1\n#! switches A=1,B=2\n#! puppets\n"
                                  "#! min-fps 25\n#! expect A x\n"])
    assert (o["players"], o["bots"], o["transport"], o["puppets"], o["min_fps"], o["errors"]) == (1, 3, "ws", True, 25.0, [])
    assert o["bot_args"] == ["-real", "-look", "auto", "-anim", "HorseMove"]
    assert o["env"] == ["LICHII_COOP_DOWNED=1", "A=1", "B=2"]
    s = rp.scenario_options("seis-kokiri", [""])
    assert (s["players"], s["bots"], s["puppets"], s["min_fps"]) == (1, 5, True, 25.0)
    assert rp.scenario_options("walk", [""])["bots"] is None and not rp.scenario_options("walk", [""])["puppets"]
    assert rp.scenario_options("seis-x", ["#! bots 2\n"])["bots"] == 2  # a header wins over the seis defaults
    assert rp.scenario_options("jefe-x", ["#! timeout 400\n"])["timeout"] == 400


def test_scenario_options_errors():
    for bad in ("#! players 3", "#! bots 6", "#! transport tcp", "#! env NOEQUALS", "#! switches A=1,B",
                "#! puppets yes", "#! min-fps fast", "#! timeout 0", "#! timeout soon"):
        assert rp.scenario_options("x", [bad + "\n"])["errors"] == [bad], bad


def test_directives_on_the_bots_and_server_logs():
    d = rp.parse_directives(["#! expect bots bot3: mark back\n#! count server story: player \\d+ starts >= 1\n"
                             "#! never bots script line \\d+ within m1..\n"])
    assert [(x["game"], x["regex"], x["error"]) for x in d] == [
        ("bots", "bot3: mark back", ""), ("server", r"story: player \d+ starts", ""), ("bots", r"script line \d+", "")]
    # in the bots' own file a letterless directive reads the bots' log
    own = rp.parse_directives(["#! expect joined as player\n"], games=["bots"])
    assert own[0]["game"] == "bots"
    bots = ("remake-bot: bot1 joined as player 2\nremake-bot: bot3: mark m1\nremake-bot: bot3: drops the connection\n"
            "remake-bot: bot3: mark back\n")
    server = "orm_server: story: player 3 starts 'BotSeisPrueba' in KokiriForest_Child\n"
    res = rp.evaluate_directives(d + own, {"bots": bots, "server": server})
    assert [ok for ok, _ in res] == [True, True, True, True], res
    res = rp.evaluate_directives(d[:1], {"bots": None})
    assert not res[0][0] and "no log for bots" in res[0][1]


def test_bot_marks_open_windows():
    lines = ["remake-bot: bot1: mark a", "x", "remake-bot: bot1: mark b", "y"]
    assert rp._window(lines, ("a", "b"))[0] == lines[0:2]
    assert rp._window(["coop: test mark a", "z"], ("a", ""))[0] == ["coop: test mark a", "z"]


def test_bot_log_summary():
    text = ("remake-bot: bot1 joined as player 2\nremake-bot: bot1: read: failed to get reader: EOF\n"
            "remake-bot: bot2: script line 4: revive: x\nremake-bot: bot1 joined as player 2\n"
            "remake-bot: bot1: script done\nremake-bot: bot2: P1 is down (PLAYER_STATUS dead)\n")
    assert rp.bot_log_summary(text) == "2 joins, 2 errors, script done by 1"


def test_check_bots():
    if not rp.BOT.exists():
        pytest.skip(f"no bot program {rp.BOT}")
    ok = ESC / "seis-kokiri.bots.txt"
    assert rp.check_bots(5, ["-script", str(ok), "-real", "-look", "auto"]) == ""
    bad = Path(__file__).parent / "__bots_bad_script.txt"
    try:
        bad.write_text("wait 1\n@5 wiat 1\n", encoding="utf-8")
        assert "line 2" in rp.check_bots(5, ["-script", str(bad)])
        assert rp.check_bots(4, ["-script", str(bad)]) != ""  # another bot's typo fails too
    finally:
        bad.unlink(missing_ok=True)
    assert "no bot program" in rp.check_bots(1, [], exe=Path("/nowhere/remake-bot.exe"))


@pytest.mark.parametrize("scenario", SEIS)
def test_seis_scenarios_are_whole(scenario):
    fa, fbots = ESC / f"{scenario}.A.txt", ESC / f"{scenario}.bots.txt"
    texts = [fa.read_text(encoding="utf-8")] + ([fbots.read_text(encoding="utf-8")] if fbots.exists() else [])
    o = rp.scenario_options(scenario, texts)
    assert not o["errors"] and o["players"] == 1 and o["bots"] == 5 and o["puppets"]
    d = rp.parse_directives(texts[:1]) + rp.parse_directives(texts[1:], games=["bots"])
    assert d and not [x for x in d if x["error"]]
    assert all(x["game"] in ("A", "bots", "server") for x in d), "a seis scenario has no game B"
    assert fbots.exists(), "a seis scenario needs its bots' script"
    if rp.BOT.exists():
        assert rp.check_bots(o["bots"], o["bot_args"] + ["-script", str(fbots)]) == ""


# `#! saves` (docs/ARNES.md, "Partidas por escenario"): no game, no partida.py (a fake builder), a fake recipes folder.

def _recipes(tmp_path):
    rec = tmp_path / "recetas"
    rec.mkdir()
    (rec / "01-uno.txt").write_text("# one\nage child\n", encoding="utf-8")
    (rec / "12-bosque.txt").write_text("include 01-uno.txt\nage adult\n", encoding="utf-8")
    return rec


def _builder(calls):
    def build(recipe, out):
        calls.append(recipe.stem)
        (out / "slot_00").mkdir(parents=True, exist_ok=True)
        out.with_name(out.name + rp.MANIFEST).write_text(f"x\nrecipe sha1: {rp.recipe_sha1(recipe)}\n", encoding="utf-8")
    return build


def test_saves_header():
    assert rp.saves_header("#! noload\n#! saves 12-bosque\nmark start\n") == "12-bosque"
    assert rp.saves_header("#!saves   /work/saves/x  \n") == "/work/saves/x"
    assert rp.saves_header("#! saves\n") == ""
    assert rp.saves_header("# saves 12-bosque\n#! savesx 1\n") is None
    with pytest.raises(rp.SavesError):
        rp.saves_header("#! saves 01-uno\n#! saves 12-bosque\n")


def test_saves_with_noload_and_start():
    t = "#! noload\n#! saves 12-bosque\n#! start coop: boss: in\nmark start\nwarp ForestTemple X\nwait 1200\npress A\n"
    assert rp.scenario_plan([t]) == (True, "coop: boss: in")
    assert rp.saves_header(t) == "12-bosque"


def test_block_built_once_then_reused(tmp_path):
    rec, root, calls = _recipes(tmp_path), tmp_path / "saves", []
    kw = dict(root=root, recipes=rec, build=_builder(calls))
    p, note = rp.resolve_saves("12-bosque", tmp_path / "def", **kw)
    assert p == root / "12-bosque" and "built now" in note and "missing" in note and calls == ["12-bosque"]
    p, note = rp.resolve_saves("12-bosque", tmp_path / "def", **kw)
    assert "built before" in note and calls == ["12-bosque"]
    (rec / "01-uno.txt").write_text("# one\nage child\nhearts 4\n", encoding="utf-8")  # an included recipe changed
    p, note = rp.resolve_saves("12-bosque", tmp_path / "def", **kw)
    assert "recipe changed" in note and calls == ["12-bosque", "12-bosque"]


def test_real_save_without_manifest_wins(tmp_path):
    rec, root, calls = _recipes(tmp_path), tmp_path / "saves", []
    (root / "01-uno" / "slot_00").mkdir(parents=True)
    p, note = rp.resolve_saves("01-uno", tmp_path / "def", root=root, recipes=rec, build=_builder(calls))
    assert p == root / "01-uno" and "real save" in note and calls == []


def test_unknown_block_and_bad_values(tmp_path):
    rec = _recipes(tmp_path)
    kw = dict(root=tmp_path / "saves", recipes=rec, build=_builder([]))
    with pytest.raises(rp.SavesError, match=r"unknown saves block '12-bosqe' \(did you mean 12-bosque\?\).*01-uno, 12-bosque"):
        rp.resolve_saves("12-bosqe", tmp_path, **kw)
    with pytest.raises(rp.SavesError, match="needs a block"):
        rp.resolve_saves("", tmp_path, **kw)
    with pytest.raises(rp.SavesError, match="no such save folder"):
        rp.resolve_saves(str(tmp_path / "nope"), tmp_path, **kw)
    assert rp.resolve_saves(str(tmp_path), tmp_path / "d", **kw) == (tmp_path, "a save folder")
    assert rp.resolve_saves("default", tmp_path / "d", **kw)[0] == tmp_path / "d"


def test_plan_saves_per_game(tmp_path):
    rec, calls = _recipes(tmp_path), []
    kw = dict(root=tmp_path / "saves", recipes=rec, build=_builder(calls))
    d = tmp_path / "def"
    got, lines = rp.plan_saves({"A": "#! saves 12-bosque\n", "B": "#! saves 01-uno\n"}, d, **kw)
    assert got == {"A": tmp_path / "saves/12-bosque", "B": tmp_path / "saves/01-uno"} and len(lines) == 2
    got, lines = rp.plan_saves({"A": "#! saves 12-bosque\n", "B": None}, d, **kw)  # B takes A's
    assert got["B"] == tmp_path / "saves/12-bosque" and "other game's" in lines[1]
    got, _ = rp.plan_saves({"A": "#! saves 12-bosque\n", "B": "#! saves default\n"}, d, **kw)
    assert got["B"] == d
    got, lines = rp.plan_saves({"A": "mark start\n", "B": None}, d, **kw)
    assert got == {"A": d, "B": d}
    got, lines = rp.plan_saves({"A": "#! saves 12-bosque\n"}, d, tmp_path / "mine", **kw)  # --saves wins
    assert got == {"A": tmp_path / "mine"} and "not used" in lines[0]
    with pytest.raises(rp.SavesError, match="unknown saves block 'zz'"):
        rp.plan_saves({"A": "#! saves zz\n", "B": None}, d, **kw)
    assert calls.count("12-bosque") == 1


def test_real_saves_headers_name_known_blocks():
    blocks = set(rp.known_blocks())
    bad = []
    for f in sorted((Path(__file__).parent / "escenarios").glob("*.txt")):
        v = rp.saves_header(f.read_text(encoding="utf-8"))
        if v is not None and v not in blocks and v not in ("default", "base") and not re.search(r"[\\/:]", v):
            bad.append(f"{f.name}: {v}")
    assert not bad, bad


def test_within_boot_reads_the_whole_log():
    log = "coop: talk: sharing 20 talk flags (10 new)\ncoop: test script started: 9 steps\ncoop: test mark start\n"
    d = rp.parse_directives(["#! expect A coop: talk: sharing \\d+ talk flags within boot..\n"
                             "#! expect A coop: talk: sharing \\d+ talk flags\n"], games="A")
    assert [ok for ok, _ in rp.evaluate_directives(d, {"A": log})] == [True, False]


# ---- join order, save-vs-life check, '#! join', NEVER STARTED watchdog (no game: fake log text)

LIFE_OK = "coop: objects: our Link's life 24 of 24, MaxLife flag 24 (quarter hearts)"
LIFE_BAD = "coop: objects: our Link's life 12 of 12, MaxLife flag 12 (quarter hearts)"


def test_join_line_names_player_1_and_2():
    logs = {"A": "x\ncoop: welcomed as player 2 on 127.0.0.1:27560\n", "B": "coop: welcomed as player 1 on h:1\n"}
    assert rp.join_line(logs) == "join order: B player 1 (first), A player 2 (second)"
    assert rp.join_order(logs) == {"A": 2, "B": 1}
    assert "unknown" in rp.join_line({"A": "", "B": None})
    assert "not welcomed: B" in rp.join_line({"A": "coop: welcomed as player 1 on h", "B": ""})


def test_save_maxlife_from_the_manifest(tmp_path):
    (tmp_path / "12-bosque").mkdir()
    (tmp_path / "12-bosque.partida.txt").write_text(
        "recipe: x\n  03-campo.txt:5   int  PlayerStatus.MaxLife: 12 -> 16\n  11-tiempo.txt:5  int  PlayerStatus.MaxLife: 20 -> 24\n",
        encoding="utf-8")
    (tmp_path / "01-kokiri").mkdir()
    (tmp_path / "01-kokiri.partida.txt").write_text("recipe: x\n  01-kokiri.txt:7 int PlayerStatus.CurrentRupee: 7 -> 20\n",
                                                    encoding="utf-8")
    (tmp_path / "real").mkdir()
    assert rp.save_maxlife(tmp_path / "12-bosque") == 24
    assert rp.save_maxlife(tmp_path / "01-kokiri") == 12  # no change line: the base's 3 hearts
    assert rp.save_maxlife(tmp_path / "real") is None  # a real save: not known
    if rp.BASE_SAVES.exists():  # your base save folder (ORM_BASE_SAVES): only when it is there
        assert rp.save_maxlife(rp.BASE_SAVES) == 12


def test_life_check_flags_the_wrong_save():
    ok, line = rp.life_check("A", "boot\n" + LIFE_OK, 24)
    assert ok and "ok" in line
    ok, line = rp.life_check("A", LIFE_BAD, 24)
    assert not ok and line == "A: save mismatch: A has 12 of 12, its save has 24"
    assert rp.life_check("B", LIFE_BAD, 12)[0]  # 3 hearts in a 3-heart save is right
    assert rp.life_check("B", "nothing", 24)[0] and rp.life_check("B", LIFE_BAD, None)[0]  # skipped, not failed
    # only the FIRST reading counts (the later one may have grown with a heart container)
    assert not rp.life_check("A", LIFE_BAD + "\n" + LIFE_OK, 24)[0]


def test_join_option():
    assert rp.scenario_options("x", ["#! join A-first\n"])["join"] == "A"
    assert rp.scenario_options("x", ["mark a\n#! join B-first\n"])["join"] == "B"
    assert rp.scenario_options("x", ["mark a\n"])["join"] is None
    o = rp.scenario_options("x", ["#! join both\n"])
    assert o["join"] is None and o["errors"] == ["#! join both"]


def test_never_started_watchdog():
    now = [1000.0]
    logs = {"A": "", "B": ""}
    w = rp.StartWatch(240, lambda g: logs[g], clock=lambda: now[0], every=0)
    logs["A"] = rp.SCRIPT_START + "\n"  # an earlier script of A already started
    w.arm("A")
    w.arm("B")
    now[0] += 100
    assert w.poll() is None
    logs["A"] += rp.SCRIPT_START + "\n"  # A reads its new script
    now[0] += 150  # 250 s since B was armed, A started
    assert w.poll() == 1
    assert [g for g, _ in w.late] == ["B"]
    assert w.lines()[0].startswith("B: NEVER STARTED")
    assert w.poll() == 1 and len(w.late) == 1  # reported once, and stays tripped


def test_never_started_not_before_the_timeout():
    now = [0.0]
    w = rp.StartWatch(60, lambda g: "", clock=lambda: now[0], every=0)
    w.arm("A")
    now[0] = 59
    assert w.poll() is None
    now[0] = 60
    assert w.poll() == 1


def test_watch_ends_wait_log_at_once(monkeypatch):
    """oi.wait_log stops at the first 'dead' process: the watchdog counts as one."""
    import orm_inst as oi
    now = [0.0]
    w = rp.StartWatch(5, lambda g: "", clock=lambda: now[0], every=0)
    w.arm("B")
    monkeypatch.setattr(oi, "log_text", lambda r: "")
    monkeypatch.setattr(oi.time, "sleep", lambda s: now.__setitem__(0, now[0] + 1))
    assert oi.wait_log([Path("x")], "never", 240, [w]) is None
    assert now[0] <= 7  # not 240
