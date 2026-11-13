"""Unit test (no game) for protocolo.py: the 108/109 tables, the stack switch, the protocol choice and the switch
aliases; and that the other tools agree with it (the daurosk trees, kit_steps.ps1, no hard-coded type numbers).
Doc: docs/ARNES.md."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import protocolo as pr

HERE = Path(__file__).parent
TOOLS = HERE.parent


def test_tables():
    assert pr.PROTOCOLS[108] == {"revive": 60, "revived": 61, "group_defeat": 62, "story_event": 63,
                                 "player_look": 64, "object_pose": 65}
    assert {k: v - 180 for k, v in pr.PROTOCOLS[109].items()} == pr.PROTOCOLS[108]
    # PIN_TEST9.md: outside every block upstream has used (1-38, 50, 80-84, 135-139, 150-154, 191-195, 206-215)
    used = set(range(1, 39)) | {50} | set(range(80, 85)) | set(range(135, 140)) | set(range(150, 155)) \
        | set(range(191, 196)) | set(range(206, 216))
    assert not used & set(pr.PROTOCOLS[109].values())
    assert pr.msg_type("STORY_EVENT", 109) == 243 and pr.msg_type("revive", 108) == 60
    assert pr.msg_name(245, 109) == "object_pose" and pr.msg_name(245, 108) is None
    # PIN_TEST10.md: new = 0xF000 + (old - 240), far above upstream's last type (401)
    assert {k: v - 0xF000 + 240 for k, v in pr.PROTOCOLS[110].items()} == pr.PROTOCOLS[109]
    assert min(pr.PROTOCOLS[110].values()) > 404
    assert pr.msg_type("story_event", 110) == 0xF003 and pr.msg_name(0xF005, 110) == "object_pose"


@pytest.mark.parametrize("stack", sorted(pr.STACKS))
def test_tables_match_the_trees(stack):
    tree = Path(pr.STACKS[stack]["tree"])
    if not (tree / "coop/protocol/include/coop/protocol/messages.hpp").exists():
        pytest.skip(f"no {tree} on this PC")
    proto = pr.STACKS[stack]["protocol"]
    assert pr.tree_protocol(tree) == proto
    want = {k: v for k, v in pr.PROTOCOLS[proto].items() if k != "revived"}  # 'revived' is only reserved
    assert pr.tree_types(tree) == want


def test_kit_steps_agrees():
    kit = TOOLS / "kit/kit_steps.ps1"
    if not kit.exists():
        pytest.skip("kit_steps.ps1 (the kit step table of the source tree) is not part of this folder")
    text = kit.read_text(encoding="utf-8")
    for stack, d in pr.STACKS.items():
        if d.get("kit", True) is False:  # a stack with no kit step table yet (test10)
            continue
        block = re.search(rf"'{stack}'\s*\{{(.*?)\n        \}}", text, re.S).group(1)
        assert f"DauroskDir = '{d['tree']}'" in block
        assert f"ServerExe = '{d['ORM_SERVER_EXE']}'" in block
        assert re.search(rf"Protocol = {d['protocol']}\b", block)


def test_no_hard_coded_type_numbers():
    """No Python/PowerShell tool names our message types by number (only protocolo.py and this test)."""
    rx = re.compile(r"(?i)(revive|group_defeat|story_event|player_look|object_pose|msg_?type|message.?type)"
                    r"\W{0,6}(=|:|\()\W{0,3}(6[0-5]|24[0-5])\b")
    bad = []
    for p in list(TOOLS.rglob("*.py")) + list(TOOLS.rglob("*.ps1")):
        if p.name in ("protocolo.py", "test_protocolo.py") or "__pycache__" in p.parts:
            continue
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if rx.search(line):
                bad.append(f"{p.relative_to(TOOLS)}:{i}: {line.strip()}")
    assert not bad, bad


def test_protocol_choice(tmp_path):
    assert pr.protocol({}) == pr.DEFAULT_PROTOCOL
    assert pr.protocol({"ORM_STACK": "test9"}) == 109
    assert pr.protocol({"ORM_STACK": "TEST8"}) == 108
    assert pr.protocol({"ORM_STACK": "test9", "ORM_PROTOCOL": "108"}) == 108  # ORM_PROTOCOL wins
    with pytest.raises(pr.StackError):
        pr.protocol({"ORM_PROTOCOL": "107"})
    with pytest.raises(pr.StackError):
        pr.protocol({"ORM_STACK": "test7"})
    # by the server exe: its CMakeCache.txt names the tree, whose messages.hpp says the protocol
    tree = tmp_path / "tree"
    hpp = tree / "coop/protocol/include/coop/protocol/messages.hpp"
    hpp.parent.mkdir(parents=True)
    hpp.write_text("inline constexpr std::uint16_t kProtocolVersion = 109;\n"
                   "  revive = 240,        // ORM: a mate gets a downed player up\n", encoding="utf-8")
    build = tmp_path / "build"
    build.mkdir()
    (build / "CMakeCache.txt").write_text(f"//daurosk's co-op C++ tree\nDAUROSK_DIR:PATH={tree.as_posix()}\n",
                                          encoding="utf-8")
    exe = build / "orm_server.exe"
    assert pr.server_protocol(exe) == 109
    assert pr.protocol({"ORM_SERVER_EXE": str(exe)}) == 109
    assert pr.tree_types(tree) == {"revive": 240}
    assert pr.server_protocol(tmp_path / "nowhere/orm_server.exe") is None
    assert pr.protocol({"ORM_SERVER_EXE": str(tmp_path / "nowhere/orm_server.exe")}) == pr.DEFAULT_PROTOCOL


def _fake_stack(tmp_path, monkeypatch, with_emulator=True):
    (tmp_path / "bin").mkdir()
    if with_emulator:
        (tmp_path / "bin/eden-cli.exe").write_bytes(b"")
    (tmp_path / "orm_server.exe").write_bytes(b"")
    (tmp_path / "bot.exe").write_bytes(b"")
    monkeypatch.setitem(pr.STACKS, "fake", {"protocol": 109, "tree": str(tmp_path / "tree"),
                                            "ORM_BUILD_BIN": str(tmp_path / "bin"),
                                            "ORM_SERVER_EXE": str(tmp_path / "orm_server.exe"),
                                            "ORM_BOT_EXE": str(tmp_path / "bot.exe"),
                                            "ORM_INST_DIR": str(tmp_path / "inst")})


def test_apply_stack_fills_only_what_is_unset(tmp_path, monkeypatch):
    _fake_stack(tmp_path, monkeypatch)
    env = {"ORM_STACK": "fake", "ORM_INST_DIR": str(tmp_path / "mine"), "ORM_BOT_EXE": ""}
    assert pr.apply_stack(env) == []
    assert env["ORM_BUILD_BIN"] == str(tmp_path / "bin")
    assert env["ORM_SERVER_EXE"] == str(tmp_path / "orm_server.exe")
    assert env["ORM_BOT_EXE"] == str(tmp_path / "bot.exe")  # empty counts as unset (orm_inst's `or`)
    assert env["ORM_INST_DIR"] == str(tmp_path / "mine")  # explicit wins
    env = {}
    assert pr.apply_stack(env) == [] and env == {}  # no ORM_STACK: nothing


def test_apply_stack_says_what_is_missing(tmp_path, monkeypatch):
    _fake_stack(tmp_path, monkeypatch, with_emulator=False)
    (tmp_path / "bot.exe").unlink()
    problems = pr.apply_stack({"ORM_STACK": "fake"})
    assert len(problems) == 2
    assert "no emulator at" in problems[0] and "eden-cli.exe" in problems[0] and "ORM_BUILD_BIN" in problems[0]
    assert "no bot (remake-bot) at" in problems[1] and "ORM_BOT_EXE" in problems[1]
    assert pr.apply_stack({"ORM_STACK": "nope"}) == ["ORM_STACK=nope: unknown stack (known: test8, test9, test10, fake)"]


def test_cli_env(tmp_path, monkeypatch, capsys):
    _fake_stack(tmp_path, monkeypatch)
    for var in pr.STACK_VARS + ("ORM_PROTOCOL",):
        monkeypatch.delenv(var, raising=False)
    monkeypatch.setenv("ORM_INST_DIR", str(tmp_path / "mine"))
    assert pr.main(["env", "--stack", "fake"]) == 0
    out = capsys.readouterr().out.splitlines()
    assert f"set ORM_BUILD_BIN={tmp_path / 'bin'}" in out
    assert not any(l.startswith("set ORM_INST_DIR") for l in out)
    assert any(l.startswith("note: ORM_INST_DIR=") and "kept" in l for l in out)
    (tmp_path / "orm_server.exe").unlink()
    assert pr.main(["env", "--stack", "fake"]) == 2
    assert any(l.startswith("problem: stack fake: no orm_server at") for l in capsys.readouterr().out.splitlines())


def test_split_items_keeps_commas_in_values():
    assert pr.split_items(["LICHII_COOP_DUNGEON_LISTS=a.txt,b.txt,LICHII_COOP_NPC=1; LICHII_COOP_AGE=1",
                           "LICHII_COOP_DUNGEON_LISTS=/work/x.txt"]) == [
        "LICHII_COOP_DUNGEON_LISTS=a.txt,b.txt", "LICHII_COOP_NPC=1", "LICHII_COOP_AGE=1",
        "LICHII_COOP_DUNGEON_LISTS=/work/x.txt"]


def test_switch_aliases_109():
    # the 109 hook reads our story names itself; the old name also gets the new one, so the log names it
    assert pr.hook_switches(["LICHII_ORM_STORY_EVENTS=1"], 109) == ["LICHII_ORM_STORY_EVENTS=1"]
    assert pr.hook_switches(["LICHII_COOP_STORY=1"], 109) == ["LICHII_COOP_STORY=1", "LICHII_ORM_STORY_EVENTS=1"]
    # a 0 never goes into upstream's LICHII_COOP_STORY (it would turn off its shared story flags too)
    assert pr.hook_switches(["LICHII_ORM_STORY_EVENTS=0"], 109) == ["LICHII_ORM_STORY_EVENTS=0"]
    # not renamed in the hook yet: both names
    assert pr.hook_switches(["LICHII_ORM_AGE=1"], 109) == ["LICHII_ORM_AGE=1", "LICHII_COOP_AGE=1"]
    assert pr.hook_switches(["LICHII_COOP_AGE=1"], 109) == ["LICHII_COOP_AGE=1"]
    # both given: as given
    assert pr.hook_switches(["LICHII_ORM_STORY_EVENTS=1", "LICHII_COOP_STORY=0"], 109) == [
        "LICHII_ORM_STORY_EVENTS=1", "LICHII_COOP_STORY=0"]


def test_switch_aliases_108():
    assert pr.hook_switches(["LICHII_ORM_STORY_EVENTS=1,LICHII_ORM_STORY_LIST=x.txt"], 108) == [
        "LICHII_ORM_STORY_EVENTS=1", "LICHII_ORM_STORY_LIST=x.txt", "LICHII_COOP_STORY=1",
        "LICHII_COOP_STORY_LIST=x.txt"]
    assert pr.hook_switches(["LICHII_ORM_STORY_EVENTS=0"], 108) == ["LICHII_ORM_STORY_EVENTS=0",
                                                                    "LICHII_COOP_STORY=0"]
    assert pr.hook_switches(["LICHII_COOP_STORY=1"], 108) == ["LICHII_COOP_STORY=1"]


def test_switches_last_wins_and_bad_items():
    assert pr.hook_switches(["LICHII_COOP_NPC=1,LICHII_COOP_DOWNED=1,LICHII_COOP_NPC=0"], 108) == [
        "LICHII_COOP_DOWNED=1", "LICHII_COOP_NPC=0"]
    assert pr.hook_switches(["EDEN_NX2_FPS_LOG=1"], 109) == ["EDEN_NX2_FPS_LOG=1"]
    for bad in ("LICHII_COOP_NPC", "FOO=1", "A:LICHII_COOP_SET_AFTER=100", "lichii_coop_npc=1"):
        with pytest.raises(ValueError):
            pr.hook_switches([bad], 109)


def test_header_switches_and_cli(tmp_path, capsys):
    a = tmp_path / "x.A.txt"
    a.write_text("# x\n#! noload\n#! switches LICHII_COOP_PUPPET=1,LICHII_COOP_STORY=1\nmark start\n", encoding="utf-8")
    assert pr.header_switches(a) == ["LICHII_COOP_PUPPET=1", "LICHII_COOP_STORY=1"]
    assert pr.main(["switches", "--stack", "test9", "--scenario", str(a), "LICHII_COOP_PUPPET=0"]) == 0
    assert capsys.readouterr().out.split() == ["LICHII_COOP_STORY=1", "LICHII_COOP_PUPPET=0",
                                               "LICHII_ORM_STORY_EVENTS=1"]
    a.write_text("#! switches A:LICHII_COOP_SET_AFTER=100\n", encoding="utf-8")
    assert pr.main(["switches", "--stack", "test9", "--scenario", str(a)]) == 2
    assert "one game only" in capsys.readouterr().out


def test_our_six_scenarios_exist():
    text = (HERE / "rapida_t9.ps1").read_text(encoding="utf-8")
    names = re.search(r"-eq 0\) \{ \$Scenarios = @\(([^)]*)\)", text).group(1)
    six = re.findall(r'"([^"]+)"', names)
    assert six == ["cartel", "charla", "caer-pareja", "historia", "objetos-uso", "edad"]
    for n in six:
        assert (HERE / "escenarios" / f"{n}.A.txt").exists() and (HERE / "escenarios" / f"{n}.B.txt").exists()
        pr.hook_switches(pr.header_switches(HERE / "escenarios" / f"{n}.A.txt"), 109)  # none is per-game
    sw = re.search(r"\$Switches = @\((.*?)\)\s*$", text, re.S | re.M).group(1)
    pr.hook_switches(re.findall(r'"([^"]+)"', sw), 109)


def test_stack_test10_puppet_pack(monkeypatch):
    """test10 also names its own puppet pack (ORM_PUPPET_LOAD, an optional stack variable); the others do not."""
    values, set_here = pr.stack_values("test10", {})
    assert set_here["ORM_PUPPET_LOAD"] == str(Path(pr.STACKS["test10"]["ORM_PUPPET_LOAD"]))
    assert pr.STACKS["test10"]["protocol"] == 110
    values, set_here = pr.stack_values("test9", {})
    assert "ORM_PUPPET_LOAD" not in values
    values, set_here = pr.stack_values("test10", {"ORM_PUPPET_LOAD": "X:/mine"})
    assert values["ORM_PUPPET_LOAD"] == "X:/mine" and "ORM_PUPPET_LOAD" not in set_here


def test_join_password_from_110():
    assert pr.JOIN_PASSWORD_FROM == 110 and pr.HARNESS_PASSWORD and " " not in pr.HARNESS_PASSWORD
    assert re.fullmatch(r"[A-Z_]+", pr.HARNESS_PASSWORD_ENV)
