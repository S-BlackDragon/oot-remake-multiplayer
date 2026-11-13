"""Tests of the synthetic saves without game data: the recipe parser, the save reader and writer on a made-up
progress.sav (every kind of value this tool writes, the 64-bit key list), the read-back check, and a whole build
from a made-up save folder into a temp folder. `python -m pytest tools/orm/saves -q`."""
import os
import struct
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import partida as pt  # noqa: E402
import progress_sav as ps  # noqa: E402
import receta as rc  # noqa: E402

m = ps.murmur3_32


# ---------------------------------------------------------------- a made-up save


def make_save(flags, keys=(), version=7):
    """flags: [(type id, name or hash, value)]; values of String32/String64/Vector3 go to the pool. Every type
    0..0x1f is opened, as in a real save; the 64-bit keys (true ones) end the file."""
    by_type = {}
    for kind, flag, value in flags:
        h = flag if isinstance(flag, int) else m(flag)
        by_type.setdefault(kind, []).append((h, value))
    n_records = 0x21 + 1 + sum(len(v) for v in by_type.values())  # openers, the end pair, the flags
    pool = 0x20 + 8 * n_records
    pool_data = bytearray()
    table = bytearray()
    for kind in range(0x21):
        table += struct.pack("<II", 0, kind)
        for h, v in by_type.get(kind, []) if kind < 0x20 else ():
            _record(table, pool_data, pool, kind, h, v)
    table += struct.pack("<II", 0x1234, 0)  # the end pair: its offset is filled in below
    keys_at = pool + len(pool_data)
    struct.pack_into("<I", table, len(table) - 4, keys_at)
    head = struct.pack("<III", ps.MAGIC, version, pool) + b"\0" * 20
    tail = b"".join(struct.pack("<Q", k) for k in sorted(keys)) + b"\0" * 8
    return bytes(head + table + pool_data + tail)


def _record(table, pool_data, pool, kind, h, v):
    if kind in ps.STRING_SIZE:
        table += struct.pack("<II", h, pool + len(pool_data))
        raw = v.encode()
        pool_data += raw + b"\0" * (ps.STRING_SIZE[kind] - len(raw))
    elif kind == ps.VECTOR3:
        table += struct.pack("<II", h, pool + len(pool_data))
        pool_data += struct.pack("<3f", *v)
    elif kind == ps.FLOAT:
        table += struct.pack("<If", h, v)
    elif kind == ps.INT:
        table += struct.pack("<Ii", h, v)
    else:
        table += struct.pack("<II", h, v)


# Everything the test recipes below touch, at a new game's values.
BASE_FLAGS = [
    (ps.BOOL, rc.CHILDHOOD, 1),
    (ps.STRING64, rc.CURRENT_BANC, "KokiriForest_Child"),
    (ps.STRING32, rc.BANC_COPY_STRING32, "KokiriForest_Child"),
    *[(ps.VECTOR3, h, (1.0, 2.0, 3.0)) for h in rc.POSITION_VECTORS],
    (ps.INT, "PlayerStatus.MaxLife", 12),
    (ps.INT, "PlayerStatus.Life", 12),
    (ps.INT, "PlayerStatus.CurrentRupee", 0),
    (ps.INT, "PouchData.Weapon_Range_SlingShot.StockNum", 0),
    (ps.BOOL, "IsGet.Weapon_Range_SlingShot", 0),
    (ps.INT, "PouchData.Item_DekuSeed.StockNum", 0),
    (ps.BOOL, "IsGet.Item_DekuSeed", 0),
    (ps.INT, rc.CAP_SEED, 0),
    (ps.INT, "PouchData.Armor_Child_Gauntlet_Goron.StockNum", 0),
    (ps.BOOL, "IsGet.Armor_Child_Gauntlet_Goron", 0),
    (ps.ENUM, rc.LIFT_GRADE, m("None")),
    (ps.INT, "PouchData.BottleA.StockNum", 0),  # a bottle has no IsGet
    (ps.BOOL, rc.BOTTLE_INFO.format("A") + ".IsValid", 0),
    (ps.ENUM, rc.BOTTLE_INFO.format("A") + ".BottleItem", m("Empty")),
    (ps.BOOL, "IsGet.Item_Melody_ZeldaLullaby", 0),
    (ps.ENUM, "Step_Meet_DekuTree", m("NotReady")),
    (ps.BOOL, "HavePlayedEvent.Dm_Kokiri_0070", 0),
    (ps.BOOL, "HavePlayedEvent.Dm_Kokiri_0075", 0),
    (ps.BOOL, "DungeonInfo.InsideTheDekuTree.IsGetMap", 0),
    (ps.BOOL, "DungeonInfo.InsideTheDekuTree.IsGetCompass", 0),
    (ps.INT, "DungeonInfo.InsideTheDekuTree.SmallKeyNum", 0),
    (ps.STRING64, "PlayerStatus.EquipWeapon", ""),
    (ps.STRING64, "PlayerStatus.AnotherEraEquipWeapon", ""),
    (ps.UINT, "SomeOrder", 3),
    (ps.FLOAT, "PlayerStatus.DiveDepth", 3.0),
    (ps.BOOL, "Untouched", 1),
    (ps.INT, "UntouchedInt", 77),
]

RECIPE = """\
# a comment line
age adult            # trailing comment
scene HyruleField_Adult 10.5 -2 3.25
hearts 5
rupees 99
item Weapon_Range_SlingShot
item Armor_Child_Gauntlet_Goron
bottle a Fish
song ZeldaLullaby
step Meet_DekuTree Complete
seen Dm_Kokiri_0070 Dm_Kokiri_0075
dungeon InsideTheDekuTree map compass keys=2
equip weapon Weapon_Sword_Master
other-era weapon Weapon_Sword_Kokiri
uint SomeOrder 9
float PlayerStatus.DiveDepth 5.5
key64 0x00000000000000ff
key64 0x10
key64 0x20 0
"""


def base_save(keys=(0x20, 0x30)):
    return make_save(BASE_FLAGS, keys)


# ---------------------------------------------------------------- hash


def test_murmur3_matches_the_games_names():
    # Hashes daurosk's docs list (found in GameDataList)
    assert m("PlayerStatus.MaxLife") == 0xFBE01DA1
    assert m("Sequence_IsChildhood") == 0xCEA220D3
    assert m("PlayerStatus.LiftGrade") == 0x59979321
    assert m("Low") == 0xD9E40095 and m("None") == 0xB6EEDE09
    assert m("IsGet.Item_Melody_ZeldaLullaby") == 0x01C20002
    assert m(rc.BOTTLE_INFO.format("A") + ".IsValid") == 0xF39CE410
    assert m(rc.BOTTLE_INFO.format("A") + ".BottleItem") == 0x7D48DF76
    assert m("") == 0


# ---------------------------------------------------------------- parser


def ops_of(text):
    return rc.parse_text(text).ops


def test_parser_directives():
    r = rc.parse_text(RECIPE)
    assert r.age == "adult"
    assert r.scene == ("HyruleField_Adult", None, (10.5, -2.0, 3.25), "<text>:3")
    got = {(op.kind, op.label()): op for op in r.ops}
    assert got[(ps.BOOL, rc.CHILDHOOD)].value == 0
    assert got[(ps.INT, "PlayerStatus.MaxLife")].value == 20
    assert got[(ps.INT, "PlayerStatus.Life")].value == 20
    assert got[(ps.INT, "PouchData.Weapon_Range_SlingShot.StockNum")].value == 1
    assert got[(ps.BOOL, "IsGet.Weapon_Range_SlingShot")].optional
    assert got[(ps.ENUM, "Step_Meet_DekuTree")].value_hash == m("Complete")
    assert got[(ps.INT, "DungeonInfo.InsideTheDekuTree.SmallKeyNum")].value == 2
    assert got[(ps.STRING64, "PlayerStatus.AnotherEraEquipWeapon")].value == "Weapon_Sword_Kokiri"
    assert got[(ps.ENUM, rc.BOTTLE_INFO.format("A") + ".BottleItem")].value == "Fish"
    assert got[(ps.FLOAT, "PlayerStatus.DiveDepth")].value == 5.5
    keys = [(op.hash, op.value) for op in r.ops if op.kind == rc.KEY64]
    assert keys == [(0xFF, 1), (0x10, 1), (0x20, 0)]


def test_item_grants_and_lift_grade():
    ops = ops_of("item Weapon_Range_SlingShot\nitem Armor_Adult_Gauntlet_Gold\n")
    atleast = [(op.label(), op.value) for op in ops if op.mode == rc.ATLEAST]
    assert (f"{rc.CAP_SEED:#010x}", 30) in atleast
    assert ("PouchData.Item_DekuSeed.StockNum", 30) in atleast
    lift = [op for op in ops if op.label() == rc.LIFT_GRADE]
    assert lift[0].value == "High" and lift[0].order == rc.LIFT_ORDER
    # A count of 0 takes the item away and grants nothing
    ops = ops_of("item Weapon_Range_SlingShot 0\n")
    assert [(op.label(), op.value) for op in ops] == [("PouchData.Weapon_Range_SlingShot.StockNum", 0)]


def test_raw_hashes_and_step_prefix():
    ops = ops_of("bool 0xdeadbeef\nint 0x10 -5\nstep Step_DoorOfTime Step01\n")
    assert (ops[0].hash, ops[0].name, ops[0].value) == (0xDEADBEEF, None, 1)
    assert (ops[1].hash, ops[1].value) == (0x10, -5)
    assert ops[2].label() == "Step_DoorOfTime"


@pytest.mark.parametrize("line", [
    "fly away", "age teen", "hearts 0", "hearts 21", "hearts x", "item", "item 0x12 1", "item Foo -1",
    "equip hat Foo", "equip weapon", "bottle F Fish", "bottle A", "key64 0", "key64 0x1 2",
    "scene A B C", "scene Bad/Name X", "dungeon X sword", "bool", "bool X 2", "int X", "seen",
    "string X", "include",
])
def test_parser_refuses(line):
    with pytest.raises(rc.RecipeError):
        rc.parse_text(line + "\n")


def test_include_order_and_loops(tmp_path):
    (tmp_path / "a.txt").write_text("hearts 3\nrupees 1\n")
    (tmp_path / "b.txt").write_text("include a.txt\nhearts 4\n")
    r = rc.parse(str(tmp_path / "b.txt"))
    assert [op.value for op in r.ops if op.label() == "PlayerStatus.MaxLife"] == [12, 16]
    assert [os.path.basename(p) for p in r.files] == ["a.txt", "b.txt"]
    (tmp_path / "c.txt").write_text("include d.txt\n")
    (tmp_path / "d.txt").write_text("include c.txt\n")
    with pytest.raises(rc.RecipeError, match="loop"):
        rc.parse(str(tmp_path / "c.txt"))
    (tmp_path / "e.txt").write_text("include nothing.txt\n")
    with pytest.raises(rc.RecipeError, match="no such recipe"):
        rc.parse(str(tmp_path / "e.txt"))


def test_age_and_scene_must_agree():
    rc.check_age_scene(rc.parse_text("age adult\nscene ForestTemple 1 2 3\n"))
    rc.check_age_scene(rc.parse_text("age child\nscene KokiriForest_Child 1 2 3\n"))
    with pytest.raises(rc.RecipeError):
        rc.check_age_scene(rc.parse_text("age adult\nscene KokiriForest_Child 1 2 3\n"))


def test_the_repository_recipes_parse():
    files = pt.recipe_files([])
    assert len(files) >= 21
    for path in files:
        r = rc.parse(path)
        rc.check_age_scene(r)
        assert r.age in ("child", "adult") and r.scene, path
        assert r.scene[1], f"{path}: every block starts at a named StartPos"


# ---------------------------------------------------------------- save reader and writer


def test_save_round_trip_unchanged():
    raw = base_save()
    s = ps.Save(raw)
    assert s.to_bytes() == raw
    assert s.get(ps.STRING64, m(rc.CURRENT_BANC)) == "KokiriForest_Child"
    assert s.get(ps.VECTOR3, rc.POSITION_VECTORS[0]) == (1.0, 2.0, 3.0)
    assert s.get(ps.FLOAT, m("PlayerStatus.DiveDepth")) == 3.0
    assert s.keys == [0x20, 0x30]


def test_save_writes_in_place():
    raw = base_save()
    s = ps.Save(raw)
    s.set(ps.STRING64, m(rc.CURRENT_BANC), "ForestTemple")
    s.set(ps.INT, m("PlayerStatus.CurrentRupee"), -3)
    s.set(ps.ENUM, m("Step_Meet_DekuTree"), m("Complete"))
    s.set(ps.VECTOR3, rc.POSITION_VECTORS[1], (4, 5, 6))
    s.set(ps.BOOL, m("Untouched"), 0)
    again = ps.Save(s.to_bytes())
    assert len(again.data) == len(raw)
    assert again.get(ps.STRING64, m(rc.CURRENT_BANC)) == "ForestTemple"
    assert again.get(ps.INT, m("PlayerStatus.CurrentRupee")) == -3
    assert again.get(ps.ENUM, m("Step_Meet_DekuTree")) == m("Complete")
    assert again.get(ps.VECTOR3, rc.POSITION_VECTORS[1]) == (4.0, 5.0, 6.0)
    assert again.get(ps.BOOL, m("Untouched")) == 0
    with pytest.raises(ps.SaveError):
        s.set(ps.STRING64, m(rc.CURRENT_BANC), "x" * 64)
    with pytest.raises(ps.SaveError):
        s.set(ps.STRING32, rc.BANC_COPY_STRING32, "y" * 32)
    with pytest.raises(ps.SaveError):
        s.set(ps.INT, m("Nowhere"), 1)


def test_key_list_grows_and_shrinks_at_the_end():
    raw = base_save(keys=(0x20,))
    s = ps.Save(raw)
    s.set_key(0x10, 1)
    s.set_key(0xFFFF_FFFF_FFFF, 1)
    s.set_key(0x20, 0)
    s.set_key(0x99, 0)  # not there: nothing
    out = s.to_bytes()
    again = ps.Save(out)
    assert again.keys == [0x10, 0xFFFF_FFFF_FFFF]
    assert len(out) == len(raw) + 8
    assert out[:again.keys_at] == raw[:again.keys_at]


def test_broken_saves_are_refused():
    raw = bytearray(base_save())
    with pytest.raises(ps.SaveError):
        ps.Save(b"\0" * 64)
    bad = bytearray(raw)
    bad += b"junk"  # bytes after the key list
    with pytest.raises(ps.SaveError):
        ps.Save(bytes(bad))
    bad = bytearray(raw)
    struct.pack_into("<II", bad, 0x20 + 8, 0, 5)  # type 5 where 1 is due
    with pytest.raises(ps.SaveError):
        ps.Save(bytes(bad))


# ---------------------------------------------------------------- apply and read back


def test_apply_and_verify_round_trip():
    raw = base_save()
    recipe = rc.parse_text(RECIPE)
    ops = pt.all_ops(recipe, None)
    s = ps.Save(raw)
    changes, skipped = pt.apply(s, ops)
    out = s.to_bytes()
    assert skipped == []
    assert pt.verify(raw, out, ops) == []
    r = ps.Save(out)
    assert r.get(ps.BOOL, m(rc.CHILDHOOD)) == 0
    assert r.get(ps.STRING64, m(rc.CURRENT_BANC)) == "HyruleField_Adult"
    assert r.get(ps.STRING32, rc.BANC_COPY_STRING32) == "HyruleField_Adult"
    assert all(r.get(ps.VECTOR3, h) == (10.5, -2.0, 3.25) for h in rc.POSITION_VECTORS)
    assert r.get(ps.INT, m("PlayerStatus.MaxLife")) == 20
    assert r.get(ps.INT, rc.CAP_SEED) == 30
    assert r.get(ps.INT, m("PouchData.Item_DekuSeed.StockNum")) == 30
    assert r.get(ps.ENUM, m(rc.LIFT_GRADE)) == m("Low")
    assert r.get(ps.ENUM, m(rc.BOTTLE_INFO.format("A") + ".BottleItem")) == m("Fish")
    assert r.get(ps.UINT, m("SomeOrder")) == 9
    assert r.get(ps.FLOAT, m("PlayerStatus.DiveDepth")) == 5.5
    assert r.get(ps.BOOL, m("Untouched")) == 1 and r.get(ps.INT, m("UntouchedInt")) == 77
    assert r.keys == [0x10, 0x30, 0xFF]
    # Applying again to the result changes nothing (a recipe is a state, not a delta)
    s2 = ps.Save(out)
    pt.apply(s2, ops)
    assert s2.to_bytes() == out


def test_verify_finds_a_stray_or_a_missing_change():
    raw = base_save()
    ops = pt.all_ops(rc.parse_text(RECIPE), None)
    s = ps.Save(raw)
    pt.apply(s, ops)
    good = s.to_bytes()
    stray = ps.Save(good)
    stray.set(ps.INT, m("UntouchedInt"), 78)
    problems = pt.verify(raw, stray.to_bytes(), ops)
    assert any(f"{m('UntouchedInt'):#010x} changed" in p for p in problems)
    wrong = ps.Save(good)
    wrong.set(ps.INT, m("PlayerStatus.MaxLife"), 24)
    assert any("PlayerStatus.MaxLife" in p for p in pt.verify(raw, wrong.to_bytes(), ops))
    keys = ps.Save(good)
    keys.set_key(0x10, 0)
    assert any("64-bit keys" in p for p in pt.verify(raw, keys.to_bytes(), ops))
    assert pt.verify(raw, b"nonsense", ops)


def test_at_least_never_lowers():
    flags = [f for f in BASE_FLAGS if f[1] not in (rc.CAP_SEED, rc.LIFT_GRADE)]
    raw = make_save(flags + [(ps.INT, rc.CAP_SEED, 50), (ps.ENUM, rc.LIFT_GRADE, m("High"))])
    ops = pt.all_ops(rc.parse_text("item Weapon_Range_SlingShot\nitem Armor_Child_Gauntlet_Goron\n"), None)
    s = ps.Save(raw)
    pt.apply(s, ops)
    assert s.get(ps.INT, rc.CAP_SEED) == 50
    assert s.get(ps.ENUM, m(rc.LIFT_GRADE)) == m("High")
    assert pt.verify(raw, s.to_bytes(), ops) == []


def test_missing_flags_are_errors_unless_optional():
    raw = base_save()
    s = ps.Save(raw)
    with pytest.raises(pt.BuildError, match="IsGet.Item_Nothing|PouchData.Item_Nothing"):
        pt.apply(s, rc.parse_text("item Item_Nothing\n").ops)
    assert s.to_bytes() == raw  # nothing written when a flag is missing
    # the bottle's IsGet does not exist: skipped, not an error
    changes, skipped = pt.apply(ps.Save(raw), rc.parse_text("item BottleA\n").ops)
    assert [op.label() for op in skipped] == ["IsGet.BottleA"]


def test_scene_without_position_or_romfs_is_refused():
    with pytest.raises(pt.BuildError, match="StartPos or X Y Z"):
        pt.all_ops(rc.parse_text("scene ForestTemple\n"), None)
    with pytest.raises(pt.BuildError, match="needs the romfs"):
        pt.all_ops(rc.parse_text("scene ForestTemple Entrance\n"), None)


# ---------------------------------------------------------------- a whole build into a temp folder


@pytest.fixture
def folders(tmp_path, monkeypatch):
    # tmp_path lives under %LOCALAPPDATA%; the refusal of that root is tested on its own below
    monkeypatch.delenv("APPDATA", raising=False)
    monkeypatch.delenv("LOCALAPPDATA", raising=False)
    base = tmp_path / "base"
    for slot in ("slot_00", "slot_01"):
        (base / slot).mkdir(parents=True)
        (base / slot / "progress.sav").write_bytes(base_save())
        (base / slot / "caption.sav").write_bytes(b"caption " + slot.encode())
    (base / "account.sav").write_bytes(b"account")
    (base / "option.sav").write_bytes(b"option")
    recipe = tmp_path / "recetas" / "99-test.txt"
    recipe.parent.mkdir()
    recipe.write_text(RECIPE)
    return base, recipe, tmp_path / "out" / "99-test"


def test_build_writes_every_slot_and_a_manifest(folders):
    base, recipe, out = folders
    before = {p: p.read_bytes() for p in base.rglob("*") if p.is_file()}
    text = pt.build(str(recipe), str(base), "01", str(out), None)
    assert {p: p.read_bytes() for p in base.rglob("*") if p.is_file()} == before  # the base is only read
    assert (out / "account.sav").read_bytes() == b"account"
    built = (out / "slot_00" / "progress.sav").read_bytes()
    assert built == (out / "slot_01" / "progress.sav").read_bytes()
    assert (out / "slot_00" / "caption.sav").read_bytes() == b"caption slot_01"
    assert pt.verify_folder(str(recipe), str(out), str(base), "01") == []
    manifest = (out.parent / ("99-test" + pt.MANIFEST)).read_text(encoding="utf-8")
    assert manifest == text and "read back: OK" in text and "Sequence_CurrentBanc" in text
    # Without the romfs an old Enum value the recipe does not name shows as its hash
    assert f"Step_Meet_DekuTree: {m('NotReady'):#010x} -> Complete" in text
    # Built again over its own folder: fine, same bytes
    pt.build(str(recipe), str(base), "01", str(out), None)
    assert (out / "slot_00" / "progress.sav").read_bytes() == built


def test_build_refuses_foreign_and_forbidden_folders(folders, tmp_path, monkeypatch):
    base, recipe, out = folders
    foreign = tmp_path / "someone"
    foreign.mkdir()
    with pytest.raises(pt.BuildError, match="not a folder this tool made"):
        pt.build(str(recipe), str(base), "00", str(foreign), None)
    with pytest.raises(pt.BuildError, match="repository"):
        pt.check_out_path(os.path.join(pt.REPO, "tools", "saves-here"), str(base))
    with pytest.raises(pt.BuildError, match="overlaps"):
        pt.check_out_path(str(base / "inner"), str(base))
    monkeypatch.setenv("APPDATA", str(tmp_path / "roaming"))
    with pytest.raises(pt.BuildError, match="APPDATA"):
        pt.check_out_path(str(tmp_path / "roaming" / "eden" / "x"), str(base))


def test_a_failed_build_leaves_the_old_folder(folders):
    base, recipe, out = folders
    pt.build(str(recipe), str(base), "00", str(out), None)
    old = (out / "slot_00" / "progress.sav").read_bytes()
    recipe.write_text(RECIPE + "int NotInTheSave 1\n")
    with pytest.raises(pt.BuildError, match="NotInTheSave"):
        pt.build(str(recipe), str(base), "00", str(out), None)
    assert (out / "slot_00" / "progress.sav").read_bytes() == old
    assert not [p for p in out.parent.iterdir() if ".tmp-" in p.name]


def test_pick_slot(folders):
    base, _, _ = folders
    assert pt.pick_slot(str(base), "1") == "slot_01"
    assert pt.pick_slot(str(base), "slot_00") == "slot_00"
    with pytest.raises(pt.BuildError):
        pt.pick_slot(str(base), "07")
    os.utime(base / "slot_00" / "progress.sav", (2_000_000_000, 2_000_000_000))
    assert pt.pick_slot(str(base), "auto") == "slot_00"


# ---------------------------------------------------------------- the probe saves (docs/SONDAS.md, "Partidas nuevas")


def test_banc_area_by_name_or_by_its_files():
    areas = ["HyruleField", "GanonCastle", "InsideGanonCastle", "Hyrule"]
    files = {("GanonCastle", "CrumblingGanonCastle_Static.bcett.byml.zs")}
    has = lambda a, n: (a, n) in files  # noqa: E731
    assert pt.banc_area("HyruleField_Adult", areas, has) == "HyruleField"  # the longest name first
    assert pt.banc_area("GanonCastle", areas, has) == "GanonCastle"
    assert pt.banc_area("InsideGanonCastle", areas, has) == "InsideGanonCastle"
    assert pt.banc_area("CrumblingGanonCastle", areas, has) == "GanonCastle"  # found by its placements
    assert pt.banc_area("Nowhere", areas, has) is None
    files.add(("HyruleField", "CrumblingGanonCastle_Dynamic.bcett.byml.zs"))
    assert pt.banc_area("CrumblingGanonCastle", areas, has) is None  # two folders: not guessed


class FakeRomfs:
    """What a build asks of the romfs, without game data: every flag known and saved in progress.sav, every
    StartPos at one place."""
    AT = (-1.25, 150.5, 7.75)

    def __init__(self):
        self.gd = type("GD", (), {"by_hash": {}})()

    def entry(self, kind, h):
        return "Fake", {"SaveFileIndex": 0}

    def startpos(self, banc, name):
        return self.AT


NEW_GAME = {ps.BOOL: 0, ps.INT: 0, ps.UINT: 0, ps.FLOAT: 0.0, ps.STRING64: "", ps.STRING32: "",
            ps.VECTOR3: (0.0, 0.0, 0.0)}


def new_game_save(ops):
    """A made-up save holding every flag `ops` writes, at a new game's values (Enum steps NotReady, the lift grade
    None, a bottle Empty), and one flag no recipe touches."""
    flags, seen = [], set()
    for op in ops:
        if (op.kind, op.hash) in seen:
            continue
        seen.add((op.kind, op.hash))
        if op.kind == ps.ENUM:
            value = m("None") if op.order else m("Empty") if "Bottle" in op.label() else m("NotReady")
        else:
            value = NEW_GAME[op.kind]
        flags.append((op.kind, op.hash, value))
    return make_save(flags + [(ps.INT, "UntouchedInt", 77)], keys=(0x20,))


def build_probe_save(name, tmp_path, monkeypatch):
    """Builds the repository's recipe `name` over a new game into tmp_path (every slot), checks the read-back and
    returns the save read from the disk."""
    monkeypatch.delenv("APPDATA", raising=False)
    monkeypatch.delenv("LOCALAPPDATA", raising=False)
    recipe = os.path.join(pt.RECIPES, name + ".txt")
    info = FakeRomfs()
    raw = new_game_save(pt.all_ops(rc.parse(recipe), info))
    base = tmp_path / "base"
    for slot in ("slot_00", "slot_03"):
        (base / slot).mkdir(parents=True)
        (base / slot / "progress.sav").write_bytes(raw)
    out = tmp_path / "saves" / name
    text = pt.build(recipe, str(base), "auto", str(out), info)
    assert "read back: OK" in text and "checked against GameDataList: yes" in text
    assert pt.verify_folder(recipe, str(out), str(base), "auto", info) == []
    built = (out / "slot_00" / "progress.sav").read_bytes()
    assert built == (out / "slot_03" / "progress.sav").read_bytes()
    save = ps.Save(built)
    assert save.get(ps.INT, m("UntouchedInt")) == 77
    return rc.parse(recipe), save


def flag(save, kind, name):
    return save.get(kind, m(name))


def assert_adult_at(recipe, save, banc, startpos):
    assert recipe.scene[:2] == (banc, startpos)
    assert flag(save, ps.BOOL, rc.CHILDHOOD) == 0
    assert flag(save, ps.STRING64, rc.CURRENT_BANC) == banc
    assert save.get(ps.STRING32, rc.BANC_COPY_STRING32) == banc
    assert all(save.get(ps.VECTOR3, h) == FakeRomfs.AT for h in rc.POSITION_VECTORS)


def test_epona_save(tmp_path, monkeypatch):
    recipe, s = build_probe_save("12-epona", tmp_path, monkeypatch)
    assert_adult_at(recipe, s, "HyruleField_Adult", "StartPos_Get_Epona_Race")
    assert flag(s, ps.ENUM, "Step_Get_Epona") == m("Complete")
    assert flag(s, ps.BOOL, "IsGet.Item_Melody_EponaSong") == 1
    assert flag(s, ps.BOOL, "IsGet.Item_Ocarina_Time") == 1
    assert flag(s, ps.STRING64, "PlayerStatus.EquipOcarina") == "Item_Ocarina_Time"
    for event in ("Dm_MelodyGet_Epona", "Dm_LonLon_0020", "Dm_LonLon_0030", "Dm_LonLon_0040", "Dm_HrlField_0070"):
        assert flag(s, ps.BOOL, "HavePlayedEvent." + event) == 1, event
    assert flag(s, ps.BOOL, "Skullchild_JustAfterGetEpona") == 1
    assert flag(s, ps.BOOL, "MiniGame_Unlock_GambleRace") == 1
    assert flag(s, ps.STRING64, "PlayerStatus.EquipWeapon") == "Weapon_Sword_Master"


def test_epona_in_the_corral_save(tmp_path, monkeypatch):
    recipe, s = build_probe_save("12-epona-rancho", tmp_path, monkeypatch)
    assert_adult_at(recipe, s, "HyruleField_Adult", "StartPos_Get_Epona_Race")
    # Before Complete: the ranch's own Epona is placed (her Presence is 'not Get_Epona_IsCompleted_Exp')
    assert flag(s, ps.ENUM, "Step_Get_Epona") == m("AfterWonGambleRace")
    assert flag(s, ps.BOOL, "HavePlayedEvent.Dm_LonLon_0040") == 1
    assert not s.has(ps.BOOL, m("HavePlayedEvent.Dm_HrlField_0070"))  # the escape: not in this recipe
    assert flag(s, ps.BOOL, "IsGet.Item_Melody_EponaSong") == 1


def test_iron_boots_in_the_water_temple_save(tmp_path, monkeypatch):
    recipe, s = build_probe_save("15-agua-templo", tmp_path, monkeypatch)
    assert_adult_at(recipe, s, "WaterTemple", "WaterTemple_FromLakeHylia")
    assert flag(s, ps.INT, "PouchData.Armor_Adult_HeavyBoot.StockNum") == 1
    assert flag(s, ps.BOOL, "IsGet.Armor_Adult_HeavyBoot") == 1
    assert flag(s, ps.STRING64, "PlayerStatus.EquipBoot") == "Armor_Adult_HeavyBoot"
    assert flag(s, ps.STRING64, "PlayerStatus.EquipArmor") == "Armor_Adult_Zora"
    assert flag(s, ps.BOOL, "IsGet.Item_Melody_WaterSerenade") == 1
    assert flag(s, ps.INT, "WaterTemple_Room00_WaterLevel") == 2
    assert [flag(s, ps.BOOL, f"WaterTemple_WaterLevelChange0{n}") for n in (1, 2, 3)] == [0, 0, 1]
    for f in ("SwitchPushed", "SwitchPushedByBlock", "SwitchPushedHeavyBoots"):
        assert flag(s, ps.BOOL, "WaterTemple_Room14_" + f) == 0


def test_escape_from_the_top_save(tmp_path, monkeypatch):
    recipe, s = build_probe_save("21-huida-arriba", tmp_path, monkeypatch)
    assert_adult_at(recipe, s, "CrumblingGanonCastle", "CrumblingGanonCastle_StartPos")
    assert flag(s, ps.ENUM, "Step_Defeat_Ganon") == m("Step06")
    assert flag(s, ps.ENUM, "Step_EscapeFromCrumblingGanonCastle") == m("Ready")
    assert flag(s, ps.BOOL, "InsideGanonCastle_IsAllBarrierClear") == 1
    for n in ("03", "05", "08", "10"):
        assert flag(s, ps.BOOL, f"EscapeFromCrumblingGanonCastle_AfterStep{n}") == 0
    assert flag(s, ps.BOOL, "HavePlayedEvent.Dm_InsideGanonCastle_BossOut") == 1
    assert not s.has(ps.BOOL, m("HavePlayedEvent.Dm_InsideGanonCastle_0110"))  # the arrival is left to play


def test_spells_and_double_magic_save(tmp_path, monkeypatch):
    recipe, s = build_probe_save("20-magia", tmp_path, monkeypatch)
    assert_adult_at(recipe, s, "InsideGanonCastle", "Entrance")
    for item in ("Item_DinFire", "Item_NayruLove", "Item_FaroreWind", "Item_FairyBless_MP",
                 "Item_FairyBless_MPDouble"):
        assert flag(s, ps.INT, f"PouchData.{item}.StockNum") == 1, item
        assert flag(s, ps.BOOL, f"IsGet.{item}") == 1, item
    assert flag(s, ps.INT, "PlayerStatus.MPMax") == 48  # the double meter (MPDouble's ExpansionAbilityActor)
    assert flag(s, ps.INT, "PlayerStatus.MP") == 48
    assert flag(s, ps.BOOL, "GreatFairy_HasNoMPOnDinDemo") == 0
    assert flag(s, ps.ENUM, "Step_Meet_GreatFairy") == m("Complete")
