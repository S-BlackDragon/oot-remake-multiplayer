"""Checks of the route scenarios (PLAN_100 F13, docs/RECORRIDOS.md) without a game.

Every escenarios/recorrido-<block>.steps.txt and the leg scripts it plays: steps lines daurosk's run.ps1 knows,
input lines the hook's test script parser takes (lichii/hook/coop_testinput.cpp), every `warp` scene in the known
scene list (escenas_banc.txt, from the romfs Banc by tools/romfs/scene_list.py) with its StartPos, both games
doing the same rooms, and the `#!` assertions well formed (expect/never/count, `within <mark>..<mark>` naming marks
of that game's script). Also: every warp of every other scenario names a known scene.

    python -m pytest tools/orm/harness/test_recorridos.py -q
"""
import os
import re
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ESC = HERE / "escenarios"
sys.path.insert(0, str(HERE))
import orm_paths  # noqa: E402
sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
pytest.importorskip("romfs_read", reason="needs tools/romfs of the source tree (put it in ORM_TREE/tools/romfs)")
pytest.importorskip("scene_list", reason="needs tools/romfs of the source tree")
import scene_list  # noqa: E402

SCENES = scene_list.read(HERE / "escenas_banc.txt")

STEPS_CMDS = {"start", "load", "play", "close", "kill", "server", "sleep", "expect", "shot"}  # run.ps1 Run-Steps
KEYS = set("A B X Y L R ZL ZR PLUS MINUS LCLICK RCLICK DUP DDOWN DLEFT DRIGHT".split()) | {
    f"{s}_{d}" for s in ("LSTICK", "RSTICK") for d in ("UP", "DOWN", "LEFT", "RIGHT")}
NUM = re.compile(r"^-?\d+(\.\d+)?$")
DIRECTIVE = re.compile(r"^#!\s*(\w+)(?:\s+(.*?))?\s*$")
WITHIN = re.compile(r"^(.*?)\s+within\s+(\S+)\.\.(\S+)$")
COUNT = re.compile(r"^(.*?)\s+(>=|<=|==|>|<)\s+(\d+)$")
HEADERS = {"noload", "start", "switches", "timeout", "expect", "never", "count", "saves",
           "caliente"}  # saves: r03_pareja; caliente: caliente.py
ADULT_ROUTE = "recorrido-adulto-"  # the adult routes (recorridos_adulto.py): any age, said in each warp


def warp_age(words):
    """'Adult' or 'Child': the age a warp asks (its last word, or the scene's era), as coop_testinput takes it."""
    if words[-1].lower() in ("adult", "child"):
        return words[-1].capitalize()
    return "Adult" if words[0].endswith("_Adult") else "Child"


def routes():
    return sorted(ESC.glob("recorrido-*.steps.txt"))


def code(line):
    return line.split("#", 1)[0].strip()


def keys_ok(word):
    return all(k in KEYS for k in word.split("+"))


def check_input_line(words):
    """None when the hook's parser (coop_testinput.cpp) takes the line, else why not."""
    cmd, n = words[0].upper(), len(words)
    num = lambda w: bool(NUM.match(w))  # noqa: E731
    if cmd == "WAIT":
        return None if n == 2 and words[1].isdigit() else "want: wait TICKS"
    if cmd in ("HOLD", "PRESS"):
        if n < 2 or not keys_ok(words[1]):
            return "unknown key"
        return None if (n == 2 and cmd == "PRESS") or (n >= 3 and words[2].isdigit() and n <= 4) else "bad ticks"
    if cmd in ("STICK", "RSTICK"):
        return None if n in (4, 5) and all(num(w) for w in words[1:4]) else "want: stick X Y TICKS [KEYS]"
    if cmd == "GOTO":
        ok = 3 <= n <= 5 and num(words[1]) and num(words[2]) and (n < 4 or (num(words[3]) and float(words[3]) >= 0.2))
        return None if ok and (n < 5 or words[4].isdigit()) else "want: goto X Z [RADIUS] [TICKS]"
    if cmd == "TOWARD":
        return None if n in (4, 5) and num(words[1]) and num(words[2]) and words[3].isdigit() else "bad toward"
    if cmd == "WARP":
        k = n - (1 if n >= 3 and words[-1].lower() in ("adult", "child") else 0)
        return None if 2 <= k <= 3 else "want: warp SCENE [START] [adult|child]"
    if cmd == "PLACE":
        return None if n == 4 and all(num(w) for w in words[1:]) else "want: place X Y Z"
    if cmd == "FLAG":
        return None if n == 3 and words[2] in ("0", "1") else "want: flag NAME|0xHASH 0|1"
    if cmd == "EVENT":
        return None if n == 2 and re.fullmatch(r"[A-Za-z0-9_]{1,127}", words[1]) else "want: event NAME"
    if cmd == "ENUM":
        return None if n in (2, 3) else "want: enum NAME [VALUE]"
    if cmd == "MARK":
        return None if n == 2 else "want: mark NAME (one word)"
    if cmd in ("CHAT", "GIVE", "WEAR", "ASSIGN", "LIFE", "GATE", "BOTTLE"):
        return None if n >= 2 else f"want: {words[0]} ..."
    return "unknown command " + words[0]


class Script:
    """One game's input script: its lines, marks, warps, places and `#!` assertions."""

    def __init__(self, path):
        self.path = path
        self.text = path.read_text(encoding="utf-8")
        self.cmds, self.marks, self.warps, self.places, self.asserts, self.errors = [], [], [], [], [], []
        for i, raw in enumerate(self.text.splitlines(), 1):
            m = DIRECTIVE.match(raw)
            if m:
                if m.group(1) in ("expect", "never", "count"):
                    self.asserts.append((i, m.group(1), m.group(2) or ""))
                elif m.group(1) not in HEADERS:
                    self.errors.append(f"{path.name}:{i}: unknown header #! {m.group(1)}")
                continue
            line = code(raw)
            if not line:
                continue
            words = line.split()
            why = check_input_line(words)
            if why:
                self.errors.append(f"{path.name}:{i}: {why}: {line}")
            cmd = words[0].lower()
            self.cmds.append(cmd)
            if cmd == "mark" and len(words) == 2:
                self.marks.append(words[1])
            elif cmd == "hold" and len(words) == 4:
                self.marks.append(words[3])  # hold KEYS TICKS MARK: a mark halfway
            elif cmd == "warp":
                self.warps.append((i, words[1:]))
            elif cmd == "place":
                self.places.append(i)

    def parsed_asserts(self):
        """[(line, kind, game, regex, (from, to) or None, count or None)]"""
        out = []
        for i, kind, rest in self.asserts:
            game, _, body = rest.partition(" ")
            if game not in ("A", "B", "S"):
                game, body = None, rest  # old form: the regex alone, about this script's game
            span = count = None
            if kind == "count":
                m = COUNT.match(body)
                count = (m.group(2), int(m.group(3))) if m else ("?", -1)
                body = m.group(1) if m else body
            m = WITHIN.match(body)
            if m:
                body, span = m.group(1), (m.group(2), m.group(3))
            out.append((i, kind, game, body, span, count))
        return out


def steps_plan(path):
    """[(line, words)] of a steps file's command lines."""
    out = []
    for i, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if raw.startswith("#!"):
            continue
        line = code(raw)
        if line:
            out.append((i, line.split()))
    return out


def legs(path):
    """[(A script path, B script path)] played by a steps file."""
    out = []
    for _i, w in steps_plan(path):
        if w[0] == "play":
            pairs = dict(zip(w[1::2], w[2::2]))
            out.append((ESC / pairs.get("A", "?"), ESC / pairs.get("B", "?")))
    return out


# ---------------------------------------------------------------- the scene list itself


def test_scene_list_round_trip(tmp_path):
    table = {"KokiriForest_Child": {"FrontDekuTree", "EntrancePos"}, "InsideTheDekuTree": {"Entrance"}}
    p = tmp_path / "s.txt"
    p.write_text(scene_list.render(table), encoding="utf-8")
    assert scene_list.read(p) == table
    assert scene_list.scene_of("Banc/KokiriForest/KokiriForest_Child_Static.bcett.byml.zs") == "KokiriForest_Child"
    assert scene_list.scene_of("Banc/DodongoCavern/DodongoCavern_Dynamic.bcett.byml.zs") == "DodongoCavern"
    assert scene_list.scene_of("Banc/GameBancParam/Foo.bgyml") is None


def test_scene_list_has_the_child_blocks():
    for s in ("KokiriForest_Child", "LostWood_Child", "SacredForestMeadow_Child", "InsideTheDekuTree",
              "HyruleField_Child", "Market_Child", "HyruleCastle", "KakarikoVillage_Child", "Graveyard_Child",
              "DeathMountainTrail_Child", "GoronCity_Child", "DodongoCavern", "ZoraRiver_Child", "ZoraDomain_Child",
              "ZoraFountain_Child", "InsideJabuJabuBelly"):
        assert s in SCENES, s
    assert "StartPos_Get_StoneOfForest" in SCENES["InsideTheDekuTree"]


# ---------------------------------------------------------------- the routes


def test_there_is_a_route_per_child_block():
    names = {p.name[len("recorrido-"):-len(".steps.txt")] for p in routes()}
    for block in ("kokiri", "deku", "campo", "mercado", "castillo", "kakariko", "montana", "dodongo", "zora",
                  "jabu", "tiempo"):
        assert block in names, block


@pytest.mark.parametrize("steps", routes(), ids=lambda p: p.name)
def test_steps_file(steps):
    plan = steps_plan(steps)
    text = steps.read_text(encoding="utf-8")
    assert re.search(r"^#!\s*noload\s*$", text, re.M), "a route warps: #! noload"
    started = False
    for i, w in plan:
        assert w[0] in STEPS_CMDS, f"{steps.name}:{i}: run.ps1 has no step '{w[0]}'"
        if w[0] == "start":
            started = True
            assert w[1:] and all(x in ("A", "B") for x in w[1:]), f"{steps.name}:{i}"
        if w[0] == "play":
            assert started, f"{steps.name}:{i}: play before start"
            assert len(w) == 5 and w[1] == "A" and w[3] == "B", f"{steps.name}:{i}: both games play each leg"
            for f in (w[2], w[4]):
                assert (ESC / f).exists(), f"{steps.name}:{i}: no {f}"
        if w[0] == "expect":
            assert len(w) >= 3 and all(re.fullmatch(r"[AB]=\d+", kv) for kv in w[2:]), f"{steps.name}:{i}"
    route = steps.name[: -len(".steps.txt")]
    played = {p.name for pair in legs(steps) for p in pair}
    on_disk = {p.name for p in ESC.glob(route + "-*.txt") if not p.name.endswith(".steps.txt")}
    assert played == on_disk, f"leg files not played: {sorted(on_disk - played)}; missing: {sorted(played - on_disk)}"


def leg_scripts():
    out = []
    for steps in routes():
        for a, b in legs(steps):
            out.append((a, b))
    return out


@pytest.mark.parametrize("pair", leg_scripts(), ids=lambda p: p[0].stem[:-2])
def test_leg(pair):
    a, b = Script(pair[0]), Script(pair[1])
    by_game = {"A": a, "B": b}
    for s, who in ((a, "A"), (b, "B")):
        assert not s.errors, "\n".join(s.errors)
        assert re.search(r"^#!\s*noload\s*$", s.text, re.M) and re.search(r"^#!\s*start\s+\S", s.text, re.M)
        # warps: known scenes and start names, child age only (these are the child routes)
        assert len(s.warps) == 1, f"{s.path.name}: one warp, at the start (taken only after the save loads)"
        for i, words in s.warps:
            scene = words[0]
            assert scene in SCENES, f"{s.path.name}:{i}: no scene {scene} in escenas_banc.txt"
            rest = [w for w in words[1:] if w.lower() not in ("adult", "child")]
            if rest:
                assert rest[0] in SCENES[scene], f"{s.path.name}:{i}: {scene} has no StartPos {rest[0]}"
            if s.path.name.startswith(ADULT_ROUTE):  # adult routes (docs/RECORRIDOS_ADULTO.md): the age said
                assert scene.endswith(("_Adult", "_Child")) or words[-1].lower() in ("adult", "child"), \
                    f"{s.path.name}:{i}: an adult route says the age of every warp"
            else:
                assert not scene.endswith("_Adult") and "adult" not in [w.lower() for w in words], \
                    f"{s.path.name}:{i}: a child route warps as a child"
        assert s.cmds.index("warp") < s.cmds.index("place") if "place" in s.cmds else True
        puppet = "CoopPuppet_" + warp_age(s.warps[0][1])
        # assertions
        rooms = [m for m in s.marks if re.match(r"r\d\d-", m)]
        assert rooms, f"{s.path.name}: no rooms (mark rNN-name)"
        assert len(set(s.marks)) == len(s.marks), f"{s.path.name}: a mark twice"
        covered = set()
        kinds = set()
        for i, kind, game, rx, span, count in s.parsed_asserts():
            assert game == who, f"{s.path.name}:{i}: the {who} script asserts {who}'s log only"
            try:
                re.compile(rx)
            except re.error as e:
                pytest.fail(f"{s.path.name}:{i}: bad regex: {e}")
            if count:
                assert count[1] >= 0, f"{s.path.name}:{i}: want '#! count G REGEX >= N'"
            if span:
                marks = by_game[game].marks
                for m in span:
                    assert m in marks, f"{s.path.name}:{i}: no mark {m} in {by_game[game].path.name}"
                assert marks.index(span[0]) < marks.index(span[1]), f"{s.path.name}:{i}: marks out of order"
                if kind == "expect":
                    covered.add(span[0])
            kinds.add((kind, rx))
        missing = [r for r in rooms if r not in covered]
        assert not missing, f"{s.path.name}: rooms with no expected line: {missing}"
        assert ("never", "coop: test goto timed out") in kinds, f"{s.path.name}: the critical path never times out"
        assert any(k == "expect" and puppet in rx for k, rx in kinds), \
            f"{s.path.name}: the other's puppet must be seen"
        if "salida" in s.marks or "portal" in s.marks:
            assert any(k == "expect" and "asked for the drops of" in rx for k, rx in kinds), \
                f"{s.path.name}: a leg that leaves its scene checks the next one"
    # both games move together: same rooms in the same order, same places, same warp
    assert a.marks == b.marks, "A and B must mark the same rooms in the same order"
    assert len(a.places) == len(b.places), "A and B must be placed the same number of times"
    assert a.warps[0][1] == b.warps[0][1], "both games warp to the same scene and start"


# ---------------------------------------------------------------- every scenario's warps


def all_warps():
    out = []
    for p in sorted(ESC.glob("*.txt")):
        for i, raw in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            w = code(raw).split()
            if w and w[0].lower() == "warp" and len(w) >= 2:
                out.append((p.name, i, w[1]))
    return out


def test_every_scenario_warps_to_a_known_scene():
    bad = [f"{n}:{i}: {s}" for n, i, s in all_warps() if s not in SCENES]
    assert not bad, "warp to a scene not in escenas_banc.txt:\n" + "\n".join(bad)
