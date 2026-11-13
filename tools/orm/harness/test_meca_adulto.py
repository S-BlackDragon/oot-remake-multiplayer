"""Unit test (no game, no romfs) for meca_adulto.py: the meca-* files of the adult dungeons are what the table makes,
every hash in them is the Murmur3 of its name, the windows name marks of both scripts, and no flag repeats an
adulto-* scenario's. Doc: docs/MECANISMOS_ADULTO.md. The romfs and daurosk-tree checks are
`python tools/harness/meca_adulto.py --check` (needs E:)."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import orm_paths  # noqa: E402
sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
import pytest  # noqa: E402
pytest.importorskip("romfs_read", reason="needs tools/romfs of the source tree (put it in ORM_TREE/tools/romfs)")
import meca_adulto as ma  # noqa: E402

COMMANDS = {"wait", "hold", "press", "stick", "rstick", "goto", "toward", "warp", "chat", "place", "give", "wear",
            "assign", "life", "gate", "bottle", "event", "flag", "enum", "key", "mark"}


def test_files_match_the_table():
    for s in ma.SCENARIOS:
        a, b = ma.build(s)
        assert (ma.OUT / f"{s.name}.A.txt").read_text(encoding="utf-8") == a, s.name
        assert (ma.OUT / f"{s.name}.B.txt").read_text(encoding="utf-8") == b, s.name


def test_names_unique_and_families():
    names = [s.name for s in ma.SCENARIOS]
    assert len(names) == len(set(names))
    assert all(n.startswith("meca-") for n in names)
    assert {s.family for s in ma.SCENARIOS} == {"F2", "F4", "F5", "F7", "F8", "F9"}
    assert set(ma.DUNGEONS) == {s.dungeon for s in ma.SCENARIOS}  # every adult dungeon has some


def test_hashes_are_murmur3_of_the_names():
    for s in ma.SCENARIOS:
        a, _ = ma.build(s)
        for name, h in re.findall(r"test flag (\S+) \\\((0x[0-9a-f]{8})\\\)", a):
            assert ma.h32(name.replace("\\", "")) == h, (s.name, name)
        for p in s.phases:
            if p.kind == "flag":
                assert ma.h32(p.name) in a


def test_never_only_for_flags_not_shared_and_no_adulto_repeat():
    for s in ma.SCENARIOS:
        for p in s.phases:
            if p.kind == "flag":
                assert p.name not in ma.ADULTO_FLAGS, (s.name, p.name)
        a, _ = ma.build(s)
        nevers = re.findall(r"^#! never B coop: flags debug: bool (0x[0-9a-f]{8}) ", a, re.M)
        assert sorted(nevers) == sorted(ma.h32(p.name) for p in s.phases if p.kind == "flag" and not p.shared)


def test_windows_use_marks_of_both_scripts():
    for s in ma.SCENARIOS:
        a, b = ma.build(s)
        marks_a = set(re.findall(r"^mark (\S+)", a, re.M))
        marks_b = set(re.findall(r"^mark (\S+)", b, re.M))
        assert marks_a == marks_b, s.name
        for first, last in re.findall(r" within (\S+)\.\.(\S+)$", a, re.M):
            assert first in marks_a and last in marks_a, (s.name, first)


def test_every_step_is_a_known_command_and_regexes_compile():
    for s in ma.SCENARIOS:
        for text in ma.build(s):
            for line in text.splitlines():
                if line.startswith("#! expect") or line.startswith("#! never"):
                    rx = re.sub(r" within \S+\.\.\S+$", "", line.split(" ", 3)[3])
                    re.compile(rx)
                    continue
                code = line.split("#", 1)[0].strip()
                if code:
                    assert code.split()[0] in COMMANDS, (s.name, line)


def test_key_lines_have_sixteen_digits_and_debug_form():
    for s in ma.SCENARIOS:
        a, _ = ma.build(s)
        for k in re.findall(r"^key (0x\S+) 1", a, re.M):
            assert re.fullmatch(r"0x[0-9a-f]{16}", k)
            assert f"key64 {ma.key_debug(int(k, 16))} " in a
    assert ma.key_debug(0x0151BD6B8700DDDB) == "0x151bd6b8700dddb"


def test_lock_both_b_opens_later_within_reach():
    s = next(x for x in ma.SCENARIOS if x.name == "meca-llave-ambos-bosque")
    a, b = ma.build(s)
    place = re.search(r"^place (\S+) (\S+) (\S+)\s+# at the same door", b, re.M)
    p = s.phases[0]
    dist = ((float(place.group(1)) - p.at[0]) ** 2 + (float(place.group(3)) - p.at[2]) ** 2) ** 0.5
    assert dist < 2.5  # the hook leaves our lock alone within 2.5 m of our Link
    assert "wait 180" in b and "one key back" in a
    assert s.keys == 2  # B still has a key after A's door
