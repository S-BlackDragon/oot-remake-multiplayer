"""Tests of seis_adulto.py (the seis-<block> scenarios of blocks 11 to 20), without a game:

    python -m pytest tools/orm/harness/test_seis_adulto.py -q

The files match the tables; each block's rooms, flag, boss, save and scenes are real names of the routes, the
recipes and the romfs; the marks every `within` names are in that log's script, in order; and the bots' and the
server's regexes match lines made with remake-bot's and orm_server's own formats (a good run), while the `never`
ones do not. With a remake-bot that has `-check` (ORM_BOT_EXE, else orm/bots' build), each bots' script passes it.
"""
import os
import re
import subprocess
from pathlib import Path

import pytest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import orm_paths
sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
pytest.importorskip("romfs_read", reason="needs tools/romfs of the source tree (put it in ORM_TREE/tools/romfs)")
import recorridos_adulto as ra
import seis_adulto as sa

ESC = sa.ESC
NAMES = [f"seis-{b.route}" for b in sa.BLOQUES] + ["seis-ganon-huida"]
BANC = {l.split("\t")[0]: l.split("\t")[1].split() for l in (Path(__file__).with_name("escenas_banc.txt")
        .read_text(encoding="utf-8").splitlines()) if l and not l.startswith("#")}
DIRECTIVE = re.compile(r"^#!\s*(expect|never|count)\s+(.*)$", re.M)
WITHIN = re.compile(r"\s+within\s+(\S*)\.\.(\S*)\s*$")
COUNT = re.compile(r"\s+(>=|<=|==)\s*(\d+)\s*$")


def directives(text: str, own: str) -> list:
    """(kind, log, regex, within, op, n) of a file's lines; a letterless one reads the file's own log."""
    out = []
    for m in DIRECTIVE.finditer(text):
        kind, rest = m.group(1), m.group(2).strip()
        g = re.match(r"(A|B|bots|server)\s+(.*)$", rest)
        log, body = (g.group(1), g.group(2)) if g else (own, rest)
        w = WITHIN.search(body)
        within = (w.group(1), w.group(2)) if w else None
        body = body[:w.start()] if w else body
        c = COUNT.search(body) if kind == "count" else None
        op, n = (c.group(1), int(c.group(2))) if c else (None, None)
        out.append((kind, log, body[:c.start()].strip() if c else body.strip(), within, op, n))
    return out


def script_marks(text: str) -> list:
    return re.findall(r"^(?:@\S+\s+)?mark\s+(\S+)", text, re.M)


def test_files_match_the_tables():
    for path, text in sa.files().items():
        assert path.exists(), f"{path.name}: run python -I tools/orm/harness/seis_adulto.py"
        assert path.read_text(encoding="utf-8") == text, f"{path.name} differs from seis_adulto.py's tables"


def test_one_scenario_per_block_11_to_20():
    blocks = sorted(int(sa.route_of(b).block) for b in sa.BLOQUES)
    assert blocks == list(range(11, 21))
    assert {b.route for b in sa.BLOQUES} == {"tiempo", "bosque", "fuego", "hielo", "agua", "pozo", "sombra", "gerudo",
                                              "espiritu", "ganon"}


@pytest.mark.parametrize("b", sa.BLOQUES, ids=lambda b: b.route)
def test_block_names_are_real(b):
    leg = sa.route_of(b).legs[b.leg]
    assert len(sa.rooms_of(b)) == len(b.rooms) >= 4
    assert all(r.at for r in sa.rooms_of(b))
    # the bot's flag is one of the walked leg's room results: the route's A sets it, the entry save has it at 0
    assert any(b.flag in r.flags for r in leg.rooms), b.flag
    assert (sa.RECETAS / f"entrada-adulto-{b.route}.txt").exists()
    assert leg.scene in BANC and leg.start in BANC[leg.scene]
    assert b.outside in BANC and b.outside != leg.scene
    boss = sa.boss_of(b)
    if boss:
        assert boss.scene == leg.scene, "one warp per start: the boss room must be in the walked scene"


def test_escape_names_are_real():
    assert sa.HUIDA_FROM in BANC and sa.HUIDA_TO in BANC and sa.HUIDA_START in BANC[sa.HUIDA_TO]
    assert (sa.RECETAS / "entrada-adulto-ganon.txt").exists()


@pytest.mark.parametrize("name", NAMES)
def test_within_marks_are_in_the_scripts(name):
    a = (ESC / f"{name}.A.txt").read_text(encoding="utf-8")
    bots = (ESC / f"{name}.bots.txt").read_text(encoding="utf-8")
    marks = {"A": script_marks(a), "bots": script_marks(bots)}
    for kind, log, regex, within, op, n in directives(a, "A") + directives(bots, "bots"):
        re.compile(regex)
        assert log in ("A", "bots", "server"), f"{name}: a seis scenario has no game B"
        assert kind != "count" or op, regex
        if within and within != ("boot", ""):  # boot..: the whole log (lines logged once as the game joins)
            m1, m2 = within
            assert m1 in marks[log], f"{name}: {m1} is not a mark of {log}'s script"
            assert m2 in marks[log] and marks[log].index(m1) < marks[log].index(m2), f"{name}: {within}"


def good_logs(b_scene: str, flag: str, from_scene: str, down: int = 1, lifter: int = 2, down_in: str = "",
              a_falls: bool = True) -> dict:
    """The bots' and the server's logs of a good run, written with remake-bot's (main.go, player.go, runner.go)
    and orm_server's (server/session.cpp) formats; P1 is the game, bot N is player N + 1."""
    bots = [f"remake-bot: bot{n} joined as player {n + 1}" for n in range(1, 6)]
    for n in range(1, 6):
        bots.append(f"remake-bot: bot{n}: scene {from_scene} -> {b_scene} (loading 1.0 s)")
        bots.append(f"remake-bot: bot{n}: in {b_scene} at (1.0, 100.0, 2.0)")
        bots.append(f"remake-bot: bot{n}: until P1 scene {b_scene}: after 12.3 s")
    if a_falls:
        bots += [f"remake-bot: bot{n}: until P1 down: after 30.2 s" for n in range(1, 6)]
        bots += ["remake-bot: bot3: holding A beside P1 (0.95 away)",
                 "remake-bot: bot3: A held 3.5 s beside P1: revive request, REVIVE sent to P1",
                 "remake-bot: bot4: holding A beside P1 (1.10 away)",
                 "remake-bot: bot4: revive: P1 is up before the end"]
        bots += [f"remake-bot: bot{n}: until P1 up: after 9.8 s" for n in range(1, 6)]
    if flag:
        bots.append(f"remake-bot: bot5: flags: FLAG_SET sent (0x{ra.h(flag)}[0] kind 1 = 1)")
    bots += [f"remake-bot: bot{down}: alive -> downed at (1.0, 100.0, 2.0) in {down_in or b_scene}",
             f"remake-bot: bot{lifter}: holding A beside P{down + 1} (0.95 away)",
             f"remake-bot: bot{lifter}: A held 3.5 s beside P{down + 1}: revive request, REVIVE sent to P{down + 1}",
             f"remake-bot: bot{down}: REVIVE from P{lifter + 1}",
             f"remake-bot: bot{down}: downed -> reviving: revive requested by P{lifter + 1}",
             f"remake-bot: bot{down}: reviving -> alive: revived (by P{lifter + 1})"]
    bots += [f"remake-bot: bot{n}: mark done" for n in range(1, 6)]
    bots += [f"remake-bot: bot{n}: script done" for n in range(1, 6)]
    server = ["orm_server: player 1 joined: Link"] + [f"orm_server: player {n + 1} joined: bot{n}" for n in range(1, 6)]
    return {"bots": "\n".join(bots), "server": "\n".join(server)}


def check(ds: list, logs: dict) -> list:
    bad = []
    for kind, log, regex, within, op, n in ds:
        if log not in logs:
            continue
        hits = sum(1 for l in logs[log].splitlines() if re.search(regex, l))
        ok = {"expect": hits > 0, "never": hits == 0,
              "count": op and {">=": hits >= (n or 0), "<=": hits <= (n or 0), "==": hits == n}[op]}[kind]
        if not ok:
            bad.append(f"{kind} {log} {regex} ({hits})")
    return bad


@pytest.mark.parametrize("b", sa.BLOQUES, ids=lambda b: b.route)
def test_bot_and_server_lines_match_a_good_run(b):
    leg = sa.route_of(b).legs[b.leg]
    ds = directives((ESC / f"seis-{b.route}.bots.txt").read_text(encoding="utf-8"), "bots")
    assert check(ds, good_logs(leg.scene, b.flag, b.outside)) == []
    # red: nobody lifted bot1, the flag never sent, A never fell (nobody lifted it), a bot timed out waiting
    logs = good_logs(leg.scene, b.flag, b.outside)
    logs["bots"] = "\n".join(l for l in logs["bots"].splitlines() if "bot1:" not in l or "downed" not in l)
    assert check(ds, logs)
    assert check(ds, good_logs(leg.scene, "", b.outside))
    assert check(ds, good_logs(leg.scene, b.flag, b.outside, a_falls=False))
    logs = good_logs(leg.scene, b.flag, b.outside)
    logs["bots"] += "\nremake-bot: bot2: until P1 down: timed out after 400 s"
    assert check(ds, logs)


def test_escape_bot_lines_match_a_good_run():
    ds = directives((ESC / "seis-ganon-huida.bots.txt").read_text(encoding="utf-8"), "bots")
    assert check(ds, good_logs(sa.HUIDA_TO, "", sa.HUIDA_FROM, down=5, lifter=4, down_in=sa.HUIDA_FROM,
                                a_falls=False)) == []
    # bot5 lifted on the field instead of inside the castle: red
    assert check(ds, good_logs(sa.HUIDA_TO, "", sa.HUIDA_FROM, down=5, lifter=4, a_falls=False))


@pytest.mark.parametrize("name", NAMES)
def test_bot_scripts_select_bots_1_to_5(name):
    for line in (ESC / f"{name}.bots.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^@(\S+)\s", line)
        if m:
            for part in m.group(1).split(","):
                lo, _, hi = part.partition("-")
                assert 1 <= int(lo) <= int(hi or lo) <= sa.BOTS, line


def bot_exe():
    for p in (os.environ.get("ORM_BOT_EXE"), str(orm_paths.BOT_EXE)):
        if p and Path(p).exists():
            h = subprocess.run([p, "-h"], capture_output=True, text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if "-check" in h.stdout + h.stderr:
                return p
    return None


@pytest.mark.parametrize("name", NAMES)
def test_bot_scripts_pass_remake_bot_check(name):
    exe = bot_exe()
    if not exe:
        pytest.skip("no remake-bot with -check (ORM_BOT_EXE or orm/bots' build)")
    r = subprocess.run([exe, "-check", "-count", str(sa.BOTS), "-real", "-look", "auto", "-script",
                        str(ESC / f"{name}.bots.txt")], capture_output=True, text=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    assert r.returncode == 0, r.stdout + r.stderr
