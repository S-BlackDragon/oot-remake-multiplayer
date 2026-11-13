"""Tests of the entrance saves (entradas.py) without game data: every route has its entrance recipe, each recipe
clears every flag its route's A sets and every story event it asks for, keeps the boss items, the readers of the
kit lists, the story split, and a whole entrance built on a made-up save and read back.
`python -m pytest tools/orm/saves -q`. With daurosk's tree and the romfs on this PC, also that the written
recipes are what `entradas.py write` writes."""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import entradas as en  # noqa: E402
import partida as pt  # noqa: E402
import progress_sav as ps  # noqa: E402
import receta as rc  # noqa: E402
from test_partidas import make_save  # noqa: E402

m = ps.murmur3_32


def last_ops(path):
    """{hash: last Op} of a recipe with its includes, and its text."""
    r = rc.parse(path)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return {op.hash: op for op in r.ops if op.kind != rc.KEY64}, text


# ---------------------------------------------------------------- the written recipes


def test_every_route_has_one_entrance():
    assert sorted(e.route for e in en.ENTRADAS) == en.routes_in()
    assert len({e.name for e in en.ENTRADAS}) == len(en.ENTRADAS)
    for e in en.ENTRADAS:
        assert os.path.isfile(os.path.join(en.RECIPES, e.name + ".txt")), e.name
        assert e.items in en.BLOCK and e.story in en.BLOCK
        assert en.route_files(e.route), e.route


@pytest.mark.parametrize("e", en.ENTRADAS, ids=lambda e: e.name)
def test_recipe_clears_what_the_route_sets(e):
    path = os.path.join(en.RECIPES, e.name + ".txt")
    ops, text = last_ops(path)
    lines = [x for x in text.splitlines() if x and not x.startswith("#")]
    assert lines[0] == f"include {e.items}.txt"
    flags, events = en.route_actions(e.route)
    assert flags or events or e.route in ("mercado",), "a route that sets nothing"
    for name in flags + ["HavePlayedEvent." + ev for ev in events]:
        if f"# not saved: {name}" in text:
            continue
        op = ops.get(m(name))
        assert op is not None, f"{name}: the recipe does not clear it"
        assert op.kind in (ps.BOOL, ps.INT) and op.value == 0, f"{name}: left at {op.value}"
    for x in e.extra:
        if x.startswith("item "):
            item = x.split()[1]
            op = ops[m(f"PouchData.{item}.StockNum")]
            assert op.value >= 1, f"{item}: taken away after the extra line"
    # The story recipe's own steps stand (the Spirit Temple's are the child entrance's)
    story = en.recipe_state(e.story)
    for h, op in story.items():
        if op.kind == ps.ENUM and op.name and op.name.startswith("Step_"):
            assert ops[h].value == op.value, f"{op.name}: {ops[h].value}, {e.story} says {op.value}"


def test_route_actions_read_the_a_legs():
    flags, events = en.route_actions("deku")
    assert "InsideTheDekuTree_RoomC_Cleared" in flags and "InsideTheDekuTree_RoomA_NearScaffold" in flags
    assert en.route_actions("kokiri")[1] == ["KokiriForest_Meet_DekuTree_MeetDekuTree_01",
                                             "SacredForestMeadow_Field_MelodyGet_Saria"]
    # recorrido-tiempo-* is not recorrido-adulto-tiempo-*
    assert all("adulto" not in p for p in en.route_files("tiempo"))


# ---------------------------------------------------------------- the readers


CHILD_LIST = """\
# --- 1. Kokiri Forest ---
KokiriForest_A                     # shared
once:KokiriForest_B
hold:HavePlayedEvent.KokiriForest_C KokiriForest
key64:0x05687269aff9adfa  # a rock
# --- 2. Deku Tree ---
unshare:InsideTheDekuTree_Sensor
"""
ADULT_LIST = """\
unshare:ForestTemple_Sensor
held:FireTemple_Switch FireTemple
Market_Child_TimeTempleDoor_Opened
"""
BUILTIN = """\
constexpr std::array<std::string_view, 2> kNotShared{
    "InsideTheDekuTree_BossArea_ShutIn", "DodongoCavern_BossGroundChange"};
constexpr std::array kFlags = std::to_array<Flag>({
    {0x1215a1d0, false, "InsideTheDekuTree_RoomA_Cleared"},
    {0xbb057cea, true, "InsideTheDekuTree_Count"},
    {0x01, false, "Gimmick_DefeatEnemy_Open_DodongoCave_01"},
});
constexpr std::array kKeys = std::to_array<Key>({
    {0x35fb354fac262d36, "InsideTheDekuTree", "Enemy_Skulltula_Gold"},
    {0x6789388e9fb9c92c, "DodongoCavern", "Obj_Chest"},
});
"""
STORY = """\
# header
# ---- Kokiri Forest (child)
# Saria at the house (plays Dm_K0060).
Ev_Saria HavePlayedEvent.Dm_K0060
# Meeting the Tree (Meet; plays Dm_K0070; moves Meet_DekuTree to Complete, Get_StoneOfForest to GoInside).
Ev_Meet HavePlayedEvent.Dm_K0070 Tree_Mouth step:Step_Meet_DekuTree=NotReady,Step01,Complete step:Step_Get_StoneOfForest=NotReady,Ready,GoInside,Complete
# After Gohma (plays Dm_K0080; moves Get_StoneOfForest to Complete).
Ev_Warp solo HavePlayedEvent.Dm_K0080 step:Step_Get_StoneOfForest=NotReady,Ready,GoInside,Complete
# ---- Hyrule Field, Castle Town, Hyrule Castle (child)
# A field scene, nothing moved.
Ev_Field
"""


def test_parse_list_sections_and_prefixes():
    got = en.parse_list(CHILD_LIST)
    assert got == [(1, "flag", "KokiriForest_A"), (1, "flag", "KokiriForest_B"),
                   (1, "flag", "HavePlayedEvent.KokiriForest_C"), (1, "key", "0x05687269aff9adfa"),
                   (2, "flag", "InsideTheDekuTree_Sensor")]
    assert [n for _, _, n in en.parse_list(ADULT_LIST)] == ["ForestTemple_Sensor", "FireTemple_Switch",
                                                           "Market_Child_TimeTempleDoor_Opened"]


def test_parse_builtin():
    flags, keys = en.parse_builtin(BUILTIN)
    assert flags == ["InsideTheDekuTree_RoomA_Cleared", "InsideTheDekuTree_Count",
                     "Gimmick_DefeatEnemy_Open_DodongoCave_01", "InsideTheDekuTree_BossArea_ShutIn",
                     "DodongoCavern_BossGroundChange"]
    assert keys == [("0x35fb354fac262d36", "InsideTheDekuTree"), ("0x6789388e9fb9c92c", "DodongoCavern")]


def test_parse_story_targets_and_sections():
    evs = {e.name: e for e in en.parse_story(STORY)}
    assert evs["Ev_Meet"].targets == [("Step_Meet_DekuTree", "Complete"), ("Step_Get_StoneOfForest", "GoInside")]
    assert evs["Ev_Meet"].flags == ["HavePlayedEvent.Dm_K0070", "Tree_Mouth"]
    assert evs["Ev_Warp"].flags == ["HavePlayedEvent.Dm_K0080"]  # "solo" is not a flag
    assert evs["Ev_Saria"].targets == [] and evs["Ev_Saria"].section == 1
    assert evs["Ev_Field"].section == 3
    with pytest.raises(ValueError):
        en.parse_story("# ---- Somewhere new\nEv_X\n")


def state(text):
    return {op.hash: op for op in rc.parse_text(text).ops}


def test_story_split():
    evs = en.parse_story(STORY)

    def split(text, block, forced=()):
        done, pending = en.story_split(evs, state(text), block, forced)
        return sorted(e.name for e in done), sorted(e.name for e in pending)

    # Block 2: the Tree met (a step at its value), Gohma not beaten; Saria's scene by its section; the field after
    assert split("step Meet_DekuTree Complete\nstep Get_StoneOfForest GoInside\n", 2) == (
        ["Ev_Meet", "Ev_Saria"], ["Ev_Field", "Ev_Warp"])
    # Block 1: nothing of the Tree; Saria's scene seen in the recipe
    assert split("step Meet_DekuTree Step01\nseen Dm_K0060\n", 1) == (["Ev_Saria"], ["Ev_Field", "Ev_Meet", "Ev_Warp"])
    # A route's event is pending whatever the recipe says
    assert split("step Meet_DekuTree Complete\n", 2, forced=["Ev_Meet"])[1] == ["Ev_Field", "Ev_Meet"]
    # (Ev_Warp there: the recipe sets none of its steps and its section starts before block 2: played)
    # Block 4: the field's section started before it
    assert "Ev_Field" in split("step Get_StoneOfForest Complete\n", 4)[0]


# ---------------------------------------------------------------- a whole entrance on a made-up save


class FakeTypes(en.Types):
    def __init__(self, bools=(), ints=(), enums=()):
        table = {}
        for n in bools:
            table.setdefault(m(n), []).append((ps.BOOL, 0))
        for n in ints:
            table.setdefault(m(n), []).append((ps.INT, 0))
        for n, d in enums:
            table.setdefault(m(n), []).append((ps.ENUM, d))
        super().__init__(table)


BOOLS = ["KokiriForest_A", "KokiriForest_B", "HavePlayedEvent.KokiriForest_C", "InsideTheDekuTree_Sensor",
         "InsideTheDekuTree_RoomA_Cleared", "InsideTheDekuTree_BossArea_ShutIn", "DodongoCavern_BossGroundChange",
         "Gimmick_DefeatEnemy_Open_DodongoCave_01", "ForestTemple_Sensor", "FireTemple_Switch",
         "Market_Child_TimeTempleDoor_Opened", "HavePlayedEvent.Ev_Saria", "HavePlayedEvent.Dm_K0060",
         "HavePlayedEvent.Ev_Meet", "HavePlayedEvent.Dm_K0070", "Tree_Mouth", "HavePlayedEvent.Ev_Warp",
         "HavePlayedEvent.Dm_K0080", "HavePlayedEvent.Ev_Field", "Gohma_DefeatBoss",
         "DungeonInfo.InsideTheDekuTree.IsGetMap", "IsGet.Item_Stone_Forest", "IsGet.Weapon_Range_SlingShot",
         "IsGet.Item_DekuSeed"]
INTS = ["InsideTheDekuTree_Count", "DungeonInfo.InsideTheDekuTree.SmallKeyNum", "PouchData.Item_Stone_Forest.StockNum",
        "PouchData.Weapon_Range_SlingShot.StockNum", "PouchData.Item_DekuSeed.StockNum"]
STEPS = [("Step_Meet_DekuTree", "NotReady"), ("Step_Get_StoneOfForest", "NotReady")]
ROUTE_A = """\
#! noload
warp InsideTheDekuTree Entrance child
flag InsideTheDekuTree_RoomA_Cleared 1
flag InsideTheDekuTree_Sensor 1   # a sensor: B never gets it
flag Not_A_Saved_Flag 1
event Ev_Warp
"""


@pytest.fixture
def world(tmp_path, monkeypatch):
    """A made-up block recipe chain (02-deku, 03-campo), a route, and a base save where everything is done."""
    monkeypatch.delenv("APPDATA", raising=False)
    monkeypatch.delenv("LOCALAPPDATA", raising=False)
    recetas = tmp_path / "recetas"
    recetas.mkdir()
    (recetas / "02-deku.txt").write_text("step Meet_DekuTree Complete\nstep Get_StoneOfForest GoInside\n"
                                         "seen Dm_K0070\n")
    (recetas / "03-campo.txt").write_text("include 02-deku.txt\nitem Item_Stone_Forest\nbool Gohma_DefeatBoss 1\n"
                                          "step Get_StoneOfForest Complete\nseen Dm_K0080\n")
    scen = tmp_path / "escenarios"
    scen.mkdir()
    (scen / "recorrido-deku-1.A.txt").write_text(ROUTE_A)
    (scen / "recorrido-deku.steps.txt").write_text("start A B\n")
    flags = [(ps.BOOL, n, 1) for n in BOOLS] + [(ps.INT, n, 3) for n in INTS]
    flags += [(ps.ENUM, n, m("Complete")) for n, _ in STEPS] + [(ps.INT, rc.CAP_SEED, 0)]
    base = tmp_path / "base"
    (base / "slot_00").mkdir(parents=True)
    (base / "slot_00" / "progress.sav").write_bytes(make_save(flags, keys=(0x05687269AFF9ADFA, 0x35FB354FAC262D36,
                                                                           0x6789388E9FB9C92C)))
    monkeypatch.setattr(en, "RECIPES", str(recetas))
    monkeypatch.setattr(en, "ORDER", ["02-deku", "03-campo"])
    monkeypatch.setattr(en, "BLOCK", {"02-deku": 2, "03-campo": 3})
    src = en.Sources(CHILD_LIST, ADULT_LIST, BUILTIN, STORY)
    e = en.Entrada("deku", "02-deku", sections=[2], prefixes=["InsideTheDekuTree"], dungeons=["InsideTheDekuTree"],
                   extra=["item Weapon_Range_SlingShot  # Gohma"], note="the test's Deku Tree")
    return tmp_path, recetas, scen, base, src, e


def test_entrance_built_on_a_done_save_reads_back_new(world):
    tmp_path, recetas, scen, base, src, e = world
    types = FakeTypes(BOOLS, INTS, STEPS)
    states = {s: en.recipe_state(s) for s in en.ORDER}
    flags, events = en.route_actions("deku", str(scen))
    assert flags == ["InsideTheDekuTree_RoomA_Cleared", "InsideTheDekuTree_Sensor", "Not_A_Saved_Flag"]
    lines, not_saved = en.entrance_lines(e, src, types, flags, events, states)
    assert "Not_A_Saved_Flag" in not_saved  # (and the DungeonInfo members the fake types lack)
    text = "\n".join(lines) + "\n"
    assert "include 02-deku.txt" in lines and "# not saved: Not_A_Saved_Flag" in lines
    assert "key64 0x35fb354fac262d36 0" in lines          # the tree's key, by scene
    assert "key64 0x6789388e9fb9c92c 0" not in lines      # Dodongo's is not the block's
    assert "bool HavePlayedEvent.Dm_K0070 0" not in lines  # seen in 02-deku: it stays
    assert "bool HavePlayedEvent.Dm_K0060 0" not in lines  # Saria's, a section before: played
    assert "bool Tree_Mouth 0" not in lines                # Ev_Meet played (its step): its flags stay
    assert "bool HavePlayedEvent.Ev_Warp 0" in lines       # the route asks for it
    assert "bool HavePlayedEvent.Dm_K0080 0" in lines      # after Gohma: not yet
    assert "bool Gohma_DefeatBoss 0" in lines              # a later recipe's
    assert "item Item_Stone_Forest 0" in lines             # a reward not earned
    assert "bool KokiriForest_A 0" not in lines            # section 1 is not the block's
    path = recetas / (e.name + ".txt")
    path.write_text(text)
    out = tmp_path / "out" / e.name
    pt.build(str(path), str(base), "auto", str(out), None)
    save = ps.read(str(out / "slot_00" / "progress.sav"))
    problems, checked, unsaved = en.check_save(save, e, str(scen))
    assert problems == [] and checked == 3 and unsaved == ["Not_A_Saved_Flag"]
    assert save.get(ps.ENUM, m("Step_Get_StoneOfForest")) == m("GoInside")
    assert save.get(ps.ENUM, m("Step_Meet_DekuTree")) == m("Complete")
    assert save.get(ps.INT, m("InsideTheDekuTree_Count")) == 0
    assert save.get(ps.INT, m("PouchData.Weapon_Range_SlingShot.StockNum")) == 1
    assert 0x35FB354FAC262D36 not in save.keys and 0x6789388E9FB9C92C in save.keys
    rows = en.verify_built(str(tmp_path / "out"), str(scen), [e])
    assert rows == [(e.name, [], 3, ["Not_A_Saved_Flag"])]


def test_check_save_reports_a_flag_left_at_1(world):
    tmp_path, recetas, scen, base, src, e = world
    save = ps.read(str(base / "slot_00" / "progress.sav"))  # everything done
    problems, _, _ = en.check_save(save, e, str(scen))
    assert any("InsideTheDekuTree_RoomA_Cleared reads 1" in p for p in problems)
    assert any("HavePlayedEvent.Ev_Warp reads 1" in p for p in problems)


# ---------------------------------------------------------------- up to date (only with daurosk's tree and the romfs)


def test_written_recipes_are_up_to_date():
    kit = os.path.join(en.DEFAULT_KIT, "tools", "release", "kit", "shared_story.txt")
    if not (os.path.isfile(kit) and os.path.isdir(pt.DEFAULT_ROMFS)):
        pytest.skip("no daurosk tree or romfs on this PC")
    try:
        info = pt.load_romfs(pt.DEFAULT_ROMFS, required=True)
    except pt.BuildError as e:
        pytest.skip(str(e))
    report = en.write_all(en.Sources.from_kit(en.DEFAULT_KIT), en.Types.from_romfs(info), only_check=True)
    assert [n for n, status, _ in report if status != "same"] == []
