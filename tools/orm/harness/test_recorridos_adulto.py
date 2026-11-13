"""Checks of the adult route scenarios (PLAN_100 F13, docs/RECORRIDOS_ADULTO.md) without a game; the format checks
common to every route are test_recorridos.py's.

- the files on disk are what recorridos_adulto.py's tables make (nobody edited one by hand);
- one route per adult block of PLAN_100 section 3;
- A and B take the same time in every leg (they move together);
- every flag A sets has its line in A's header with the right hash; B expects each shared one live and never a
  sensor;
- with daurosk's tree on this PC: every shared flag is shared by the built-in dungeon list or the kit's adult list
  (and not taken off by it), every sensor is not shared.

    python -m pytest tools/orm/harness/test_recorridos_adulto.py -q
"""
import re
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import orm_paths  # noqa: E402
sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
pytest.importorskip("romfs_read", reason="needs tools/romfs of the source tree (put it in ORM_TREE/tools/romfs)")
import recorridos_adulto as ra  # noqa: E402

DAUROSK = orm_paths.TREE
DUNGEON_CPP = DAUROSK / "lichii/hook/coop_dungeon.cpp"
ADULT_LIST = DAUROSK / "tools/release/kit/shared_dungeon_adult.txt"
LEGS = [(r, n, leg) for r in ra.ROUTES for n, leg in enumerate(r.legs, 1)]


def test_files_match_the_tables():
    made = ra.files()
    differs = [p.name for p, text in made.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
    assert not differs, f"run python -I {ra.__file__}: {differs}"
    stale = [p.name for p in ra.ESC.glob("recorrido-adulto-*") if p not in made]
    assert not stale, f"files no table makes: {stale}"


def test_a_route_per_adult_block():
    assert {r.name for r in ra.ROUTES} == {"tiempo", "bosque", "fuego", "hielo", "agua", "pozo", "sombra", "gerudo",
                                           "espiritu", "ganon"}
    assert {r.block for r in ra.ROUTES} == {str(b) for b in range(11, 21)}
    for r in ra.ROUTES:
        last = r.legs[-1]
        assert last.boss or last.exit or r.goal, f"{r.name}: ends at a boss, an exit or a stated goal"


@pytest.mark.parametrize("item", LEGS, ids=lambda x: f"{x[0].name}-{x[1]}")
def test_both_games_take_the_same_time(item):
    _r, _n, leg = item
    a, b = ra.leg_script(leg, "A"), ra.leg_script(leg, "B")
    assert ra.ticks(a) == ra.ticks(b)
    assert [l for l in a if l.startswith("mark")] == [l for l in b if l.startswith("mark")]


@pytest.mark.parametrize("item", LEGS, ids=lambda x: f"{x[0].name}-{x[1]}")
def test_flag_lines_and_hashes(item):
    r, n, leg = item
    a = ra.render_leg(r, n, leg, "A")
    b = ra.render_leg(r, n, leg, "B")
    set_in_a = re.findall(r"^flag (\S+) 1", a, re.M)
    assert set_in_a == [f for room in leg.rooms for f in room.sensors + room.flags]
    for f in set_in_a:
        hx = f"{ra.murmur3_32(f):08x}"
        assert f"#! expect A coop: test flag {f} \\(0x{hx}\\) = 1 within " in a, f
    for room in leg.rooms:
        for f in room.flags:
            hx = ra.h(f)
            assert re.search(rf"^#! expect B coop: flags debug: \\w\+ 0x{hx} .* 0 -> 1 within ", b, re.M), f
        for f in room.sensors:
            assert f"#! never B coop: flags debug: \\w+ 0x{ra.h(f)} \n" in b, f
            assert f not in room.flags


def test_no_flag_is_both_shared_and_a_sensor():
    shared = {f for _r, _n, leg in LEGS for room in leg.rooms for f in room.flags}
    sensors = {f for _r, _n, leg in LEGS for room in leg.rooms for f in room.sensors}
    assert not shared & sensors


def daurosk_lists():
    text = DUNGEON_CPP.read_text(encoding="utf-8")
    body = text.split("constexpr std::array kFlags", 1)[1].split("});", 1)[0]
    flags = {m.group(3): (int(m.group(1), 16), m.group(2) == "true")
             for m in re.finditer(r'\{0x([0-9a-f]+), (true|false), "([^"]+)"\}', body)}
    not_shared = set(re.findall(r'"([^"]+)"', text.split("kNotShared{", 1)[1].split("};", 1)[0]))
    unshare, shared_lines = set(), set()
    for raw in ADULT_LIST.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith(("unshare:", "held:")):
            unshare.add(line.split(":", 1)[1].split()[0])
        else:
            shared_lines.add(line)
    return flags, not_shared, unshare, shared_lines


@pytest.mark.skipif(not (DUNGEON_CPP.exists() and ADULT_LIST.exists()), reason="no daurosk test8 tree on this PC")
def test_flags_are_in_the_lists():
    flags, not_shared, unshare, shared_lines = daurosk_lists()
    for name, (hx, _int) in flags.items():
        assert ra.murmur3_32(name) == hx, name  # the hash the hook logs is the list's
    bad = []
    for r, n, leg in LEGS:
        for room in leg.rooms:
            for f in room.flags:
                built_in = f in flags and not flags[f][1] and f not in not_shared and f not in unshare
                if not (built_in or f in shared_lines):
                    bad.append(f"{r.name}-{n} {room.name}: {f} is not a shared Bool")
            for f in room.sensors:
                if not (f in unshare or (f not in flags and f not in shared_lines)):
                    bad.append(f"{r.name}-{n} {room.name}: sensor {f} is still shared")
    assert not bad, "\n".join(bad)
