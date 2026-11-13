"""Entrance saves for the route scenarios (recorrido-*): recetas/entrada-<route>.txt, each a recipe that takes the
block's own recipe (NN-<block>.txt, the progress up to the block) and then CLEARS what belongs to the block and later,
so the save is "just arrived, nothing done": every flag the route's A sets (B has to see it go 0 -> 1), the block's
area flags and one-time keys of the shared lists, the story events not played yet at the entrance, what the later
recipes set, the story steps after the entrance and the rewards not earned yet. It adds the items the block's boss
needs (RECORRIDOS_ADULTO.md: the boss and portal lines are red without them).

  python entradas.py write   [--kit DIR] [--romfs DIR]   (re)write recetas/entrada-*.txt from the table below
  python entradas.py check   [--kit DIR] [--romfs DIR]   1 when a written recipe differs from what write would write
  python entradas.py all     [--base DIR] [--root DIR]   build every entrada-* save (partida.py) and verify it
  python entradas.py verify  [--root DIR]                read the built saves back: every flag the route's A sets is 0
  python entradas.py map                                 route -> save, one line each

Inputs of write: the recipes (recetas/NN-*.txt), the route files (tools/harness/escenarios/recorrido-*.A.txt), and,
from daurosk's tree on this PC (--kit, default ORM_TREE): tools/release/kit/shared_dungeon_child.txt,
shared_dungeon_adult.txt, shared_story.txt and lichii/hook/coop_dungeon.cpp (the built-in list of the 13 dungeons:
flags and 64-bit keys). The romfs (GameDataList) gives each name's type and new-game value; a name it does not save
is written as a `# not saved:` comment. The written recipes are names only (no game data) and are what the
repository keeps; the saves go to <ORM_SAVES_ROOT>/entrada-<route>, never to the repository.

Tests: test_entradas.py (no game data).
"""
import argparse
import glob
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import partida as pt  # noqa: E402
import progress_sav as ps  # noqa: E402
import receta as rc  # noqa: E402

RECIPES = pt.RECIPES
SCENARIOS = os.path.abspath(os.path.join(HERE, "..", "harness", "escenarios"))
DEFAULT_KIT = str(pt.orm_paths.TREE)
SUMMARY = "entradas_resumen.txt"
PREFIX = "entrada-"

# The block recipes in game order (each includes the one before). The number is the block of PLAN_100.md section 3
# (19.5: the adult side of the Spirit Temple), used to compare with the story list's sections.
CHAIN = [("01-kokiri", 1), ("02-deku", 2), ("03-campo", 3), ("04-mercado", 4), ("05-castillo", 5),
         ("06-kakariko", 6), ("07-montana", 7), ("08-dodongo", 8), ("09-zora", 9), ("10-jabu", 10),
         ("11-tiempo", 11), ("12-bosque", 12), ("13-fuego", 13), ("14-hielo", 14), ("15-agua", 15),
         ("16-pozo", 16), ("17-sombra", 17), ("18-gerudo", 18), ("19-espiritu-nino", 19),
         ("19-espiritu-adulto", 19.5), ("20-ganon", 20)]
BLOCK = dict(CHAIN)
ORDER = [stem for stem, _ in CHAIN]

# The first block of each section of shared_story.txt (a word of its "# ---- " title): an event the recipes say
# nothing about (no step they set, no scene they mark seen) is taken as played when its section starts before the
# block. Checked in order, the first word found wins.
STORY_SECTIONS = [("Kokiri Forest", 1), ("Hyrule Field", 3), ("Kakariko, Graveyard", 6), ("Zora's River", 9),
                  ("Temple of Time", 11), ("Sheik, the Forest", 12), ("Fire Temple", 13), ("Water Temple", 14),
                  ("Shadow Temple", 16), ("Spirit Temple", 18), ("Ganon's Castle", 20)]

# The rewards of the blocks: taken away when the entrance's recipe does not hold them (an advanced base save does).
REWARDS = ["Item_Stone_Forest", "Item_Stone_Fire", "Item_Stone_Water", "Item_Medal_Light", "Item_Medal_Forest",
           "Item_Medal_Fire", "Item_Medal_Water", "Item_Medal_Shadow", "Item_Medal_Spirit"]

# Never cleared by the "later recipes" rule: Link's things (items, equipment, life, age, scene), not progress.
NOT_PROGRESS = ("PlayerStatus.", "PouchData.", "IsGet.", "Sequence_")


class Entrada:
    """One route's entrance save. route: the recorrido-<route> name. items: the recipe it includes (Link's things).
    story: the recipe whose story state it has (the same but for the Spirit Temple: the adult recipe's items with
    the child entrance's story). sections: the shared_dungeon_child.txt sections that are the block's. prefixes:
    the name prefixes (any case) of the block's own flags and keys in coop_dungeon.cpp and shared_dungeon_adult.txt.
    dungeons: DungeonInfo.<name> taken back to nothing (map, compass, boss key, small keys). extra: recipe lines
    after the include (the boss's weapons, an age or scene of its own)."""

    def __init__(self, route, items, story=None, sections=(), prefixes=(), dungeons=(), extra=(), note=""):
        self.route = route
        self.items = items
        self.story = story or items
        self.sections = tuple(sections)
        self.prefixes = tuple(prefixes)
        self.dungeons = tuple(dungeons)
        self.extra = tuple(extra)
        self.note = note

    @property
    def name(self):
        return PREFIX + self.route


ENTRADAS = [
    Entrada("kokiri", "01-kokiri", sections=[1], note="block 1, Kokiri Forest, Lost Woods, Sacred Forest Meadow"),
    Entrada("deku", "02-deku", sections=[2], prefixes=["InsideTheDekuTree"], dungeons=["InsideTheDekuTree"],
            extra=["item Weapon_Range_SlingShot  # Gohma (the slingshot is the tree's: 03-campo)"],
            note="block 2, the Deku Tree and Gohma"),
    Entrada("campo", "03-campo", sections=[3], note="block 3, Hyrule Field, Lon Lon Ranch"),
    Entrada("mercado", "04-mercado", sections=[4], note="block 4, Castle Town"),
    Entrada("castillo", "05-castillo", sections=[5], note="block 5, Hyrule Castle"),
    Entrada("kakariko", "06-kakariko", sections=[6], note="block 6, Kakariko, the Graveyard"),
    Entrada("montana", "07-montana", sections=[7], note="block 7, Death Mountain, Goron City"),
    Entrada("dodongo", "08-dodongo", sections=[8], prefixes=["DodongoCavern", "Gimmick_DefeatEnemy_Open_DodongoCave"],
            dungeons=["DodongoCavern"], note="block 8, Dodongo's Cavern and King Dodongo (the bombs: 08-dodongo)"),
    Entrada("zora", "09-zora", sections=[9], note="block 9, Zora's River, Domain, Fountain"),
    Entrada("jabu", "10-jabu", sections=[10], prefixes=["InsideJabuJabuBelly", "Insidejabujabu"],
            dungeons=["InsideJabuJabuBelly"],
            extra=["item Item_Boomerang  # Barinade (the boomerang is Jabu-Jabu's: 11-tiempo)"],
            note="block 10, Jabu-Jabu and Barinade"),
    Entrada("tiempo", "11-tiempo", note="block 11, the Temple of Time (child): the door still shut"),
    Entrada("adulto-tiempo", "11-tiempo",
            extra=["age adult", "scene Market_Adult Market_Entrance", "item Weapon_Sword_Master",
                   "item Armor_Adult_Normal", "equip weapon Weapon_Sword_Master", "equip shield -", "equip armor -",
                   "other-era weapon Weapon_Sword_Kokiri", "other-era shield Weapon_Shield_Deku"],
            note="block 11, the Temple of Time (adult side): the child's progress, an adult with the Master Sword"),
    Entrada("adulto-bosque", "12-bosque", prefixes=["ForestTemple", "Foresttemple",
                                                    "Gimmick_DefeatEnemy_Open_ForestTemple"],
            dungeons=["ForestTemple"],
            extra=["item Weapon_Range_Bow  # Phantom Ganon (the bow is the temple's: 13-fuego)", "item Item_Quiver_Small"],
            note="block 12, the Forest Temple and Phantom Ganon"),
    Entrada("adulto-fuego", "13-fuego", prefixes=["FireTemple", "Firetemple", "Gimmick_DefeatEnemy_Open_FireTemple",
                                                  "Gimmick_DeleteFire_FireTemple", "Npc_Goron_0"],
            dungeons=["FireTemple"], extra=["item Item_Hammer  # Volvagia (the hammer is the temple's: 14-hielo)"],
            note="block 13, the Fire Temple and Volvagia"),
    Entrada("adulto-hielo", "14-hielo", prefixes=["IceCavern"], dungeons=["IceCavern"],
            note="block 14, the Ice Cavern"),
    Entrada("adulto-agua", "15-agua", prefixes=["WaterTemple"], dungeons=["WaterTemple"],
            extra=["item Item_Longshot  # Morpha (the Longshot is the temple's: 16-pozo)"],
            note="block 15, the Water Temple and Morpha"),
    Entrada("adulto-pozo", "16-pozo", prefixes=["BottomOfTheWell"], dungeons=["BottomOfTheWell"],
            note="block 16, the Bottom of the Well (child)"),
    Entrada("adulto-sombra", "17-sombra", prefixes=["ShadowTemple", "GraveYard_Field_ShadowTemple"],
            dungeons=["ShadowTemple"],
            extra=["item Armor_Adult_HoverBoot  # Bongo Bongo's way (the boots are the temple's: 18-gerudo)"],
            note="block 17, the Shadow Temple and Bongo Bongo"),
    Entrada("adulto-gerudo", "18-gerudo", prefixes=["GerudoTrainingGround", "GerudoFortress", "Gerudo_phantomdesert",
                                                    "HountedWasteland", "DesertColossus"],
            dungeons=["GerudoTrainingGround"], note="block 18, Gerudo Valley, the fortress, the desert"),
    Entrada("adulto-espiritu", "19-espiritu-adulto", story="19-espiritu-nino",
            prefixes=["SpiritTemple", "Enemy_MiddleBoss_IronKnuckle_Nabooru"], dungeons=["SpiritTemple"],
            extra=["age child", "scene DesertColossus_Child DesertColossus_FromSpiritTempleEntrance",
                   "equip weapon Weapon_Sword_Kokiri", "equip shield Weapon_Shield_Deku", "equip armor -",
                   "other-era weapon Weapon_Sword_Master", "other-era shield Weapon_Shield_Hylian",
                   "item Weapon_Shield_Mirror  # Twinrova and the light puzzles (the temple's: 20-ganon)"],
            note="block 19, the Spirit Temple, both sides: the adult's items (the Silver Gauntlets), the child's "
                 "entrance (19-espiritu-nino's story)"),
    Entrada("adulto-ganon", "20-ganon", prefixes=["InsideGanonCastle", "GanonCastle_"], dungeons=["InsideGanonCastle"],
            note="block 20, Ganon's Castle and Ganondorf"),
]
BY_ROUTE = {e.route: e for e in ENTRADAS}


# ---------------------------------------------------------------- reading the sources


def route_files(route, folder=SCENARIOS):
    """The A files of recorrido-<route>, by leg."""
    pat = re.compile(rf"^recorrido-{re.escape(route)}-(\d+)\.A\.txt$")
    found = [(int(m.group(1)), n) for n in os.listdir(folder) if (m := pat.match(n))]
    return [os.path.join(folder, n) for _, n in sorted(found)]


def routes_in(folder=SCENARIOS):
    """Every route that has a steps file (recorrido-<route>.steps.txt)."""
    return sorted(n[len("recorrido-"):-len(".steps.txt")] for n in os.listdir(folder)
                  if n.startswith("recorrido-") and n.endswith(".steps.txt"))


def route_actions(route, folder=SCENARIOS):
    """(flags, events) of the route's A legs: the names `flag NAME 1` writes and `event NAME` asks for, in order."""
    flags, events = [], []
    for path in route_files(route, folder):
        with open(path, encoding="utf-8") as f:
            for raw in f:
                words = raw.split("#", 1)[0].split()
                if len(words) >= 3 and words[0] == "flag" and words[2] != "0" and words[1] not in flags:
                    flags.append(words[1])
                elif len(words) == 2 and words[0] == "event" and words[1] not in events:
                    events.append(words[1])
    return flags, events


LIST_PREFIXES = ("once:", "hold:", "unshare:", "held:")


def parse_list(text):
    """[(section number or None, kind, name)] of a dungeon list file: kind "flag" (any of share, once:, hold:,
    unshare:, held:) or "key" (key64:, name the hex key)."""
    out, section = [], None
    for raw in text.splitlines():
        m = re.match(r"^# --- (\d+)\. ", raw)
        if m:
            section = int(m.group(1))
            continue
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        token = line.split()[0]
        if token.startswith("key64:"):
            out.append((section, "key", token[6:]))
            continue
        for p in LIST_PREFIXES:
            if token.startswith(p):
                token = token[len(p):]
                break
        out.append((section, "flag", token))
    return out


def parse_builtin(text):
    """(flags, keys) of coop_dungeon.cpp: every name of its flag tables (kFlags, kLiveFlags, and the names it leaves
    out on purpose: kNotShared, kCorridor) and every (key, scene) of kKeys."""
    flags = [m.group(2) for m in re.finditer(r'\{0x[0-9a-fA-F]+, (true|false), "([^"]+)"\}', text)]
    for table in ("kNotShared", "kCorridor"):
        m = re.search(table + r"[^{]*\{(.*?)\};", text, re.S)
        if m:
            flags += re.findall(r'"([^"]+)"', m.group(1))
    keys = [(m.group(1), m.group(2)) for m in re.finditer(r'\{(0x[0-9a-fA-F]+), "([^"]+)", "[^"]+"\}', text)]
    seen, uniq = set(), []
    for n in flags:
        if n not in seen:
            seen.add(n)
            uniq.append(n)
    return uniq, keys


class StoryEvent:
    __slots__ = ("name", "flags", "steps", "targets", "section")

    def __init__(self, name, flags, steps, targets, section):
        self.name = name
        self.flags = flags      # HavePlayedEvent.* and Bools the event sets
        self.steps = steps      # {Step_Q: [values in order]}
        self.targets = targets  # [(Step_Q, value)] what it moves them to (from its comment)
        self.section = section  # first block of its section


def section_block(title):
    for word, block in STORY_SECTIONS:
        if word in title:
            return block
    raise ValueError(f"shared_story.txt: section {title!r} has no block in STORY_SECTIONS")


def parse_story(text):
    """[StoryEvent] of shared_story.txt. The comment line before an event says what it moves ("moves Q to V, ...")."""
    events, section, comment = [], None, ""
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("# ----"):
            section = section_block(line[6:].strip())
            comment = ""
            continue
        if line.startswith("#"):
            comment = line
            continue
        if not line or section is None:
            continue
        words = line.split()
        flags, steps = [], {}
        for w in words[1:]:
            if w == "solo":
                continue
            if w.startswith("step:"):
                q, _, vals = w[5:].partition("=")
                steps[q] = vals.split(",")
            else:
                flags.append(w)
        targets = []
        m = re.search(r"moves (.+?)\)", comment)
        if m:
            for part in m.group(1).split(","):
                t = re.match(r"\s*(\w+) to (\w+)\s*$", part)
                if t:
                    q = t.group(1) if t.group(1).startswith("Step_") else "Step_" + t.group(1)
                    if q in steps and t.group(2) in steps[q]:
                        targets.append((q, t.group(2)))
        events.append(StoryEvent(words[0], flags, steps, targets, section))
        comment = ""
    return events


class Sources:
    """What write reads: the dungeon lists, the built-in list, the story list."""

    def __init__(self, child_list, adult_list, builtin, story):
        self.child = parse_list(child_list)
        self.adult = parse_list(adult_list)
        self.builtin_flags, self.builtin_keys = parse_builtin(builtin)
        self.story = parse_story(story)

    @classmethod
    def from_kit(cls, kit):
        def read(*parts):
            path = os.path.join(kit, *parts)
            if not os.path.isfile(path):
                raise pt.BuildError(f"{path}: not found (--kit: daurosk's tree with the kit lists)")
            with open(path, encoding="utf-8") as f:
                return f.read()
        k = ("tools", "release", "kit")
        return cls(read(*k, "shared_dungeon_child.txt"), read(*k, "shared_dungeon_adult.txt"),
                   read("lichii", "hook", "coop_dungeon.cpp"), read(*k, "shared_story.txt"))


# ---------------------------------------------------------------- types (GameDataList)


class Types:
    """A name's save type and new-game value. lookup(name) -> (kind, default) or None (not saved in progress.sav).
    default: 0/1 for a Bool, an int for an Int, the value's name for an Enum."""

    def __init__(self, table):
        self.table = table  # {hash: [(kind, default)]}

    @classmethod
    def from_romfs(cls, info):
        table = {}
        for h, entries in info.gd.by_hash.items():
            for tname, ent in entries:
                kind = ps.GDL_TYPE.get(tname)
                if kind not in (ps.BOOL, ps.INT, ps.ENUM) or ent.get("SaveFileIndex") != 0:
                    continue
                d = ent.get("DefaultValue", 0)
                if kind == ps.BOOL:
                    d = 1 if d else 0
                elif kind == ps.ENUM:
                    values, raw = ent.get("Values") or [], ent.get("RawValues") or []
                    d = raw[values.index(d)] if d in values and len(raw) == len(values) else (raw[0] if raw else None)
                table.setdefault(h, []).append((kind, d))
        return cls(table)

    def lookup(self, name, kinds=(ps.BOOL, ps.INT)):
        for kind, d in self.table.get(ps.murmur3_32(name), []):
            if kind in kinds:
                return kind, d
        return None


# ---------------------------------------------------------------- the recipe of one entrance


def recipe_state(stem):
    """What a block recipe (and its includes) sets, by hash: {hash: last Op}."""
    r = rc.parse(os.path.join(RECIPES, stem + ".txt"))
    return {op.hash: op for op in r.ops if op.kind != rc.KEY64}


def _idx(values, v):
    return values.index(v) if v in values else -1


def story_split(events, story_state, block, forced=()):
    """(done, pending) of the story events at the entrance of `block`, whose recipe sets `story_state`. An event is
    played when the recipe has one of the steps it moves at or past its value; else when the recipe marks one of its
    scenes seen; else, when the recipe sets none of its steps, when its section starts before the block. An event
    the route asks for (`forced`) is always pending."""
    done, pending = [], []
    for ev in events:
        if ev.name in forced:
            pending.append(ev)
            continue
        known = [(q, v) for q, v in ev.targets if ps.murmur3_32(q) in story_state]
        reached = any(_idx(ev.steps[q], story_state[ps.murmur3_32(q)].value) >= _idx(ev.steps[q], v)
                      for q, v in known)
        seen = any((op := story_state.get(ps.murmur3_32(f))) is not None and op.kind == ps.BOOL and op.value == 1
                   for f in ev.flags + ["HavePlayedEvent." + ev.name] if f.startswith("HavePlayedEvent."))
        if reached or seen or (not known and ev.section < block):
            done.append(ev)
        else:
            pending.append(ev)
    return done, pending


class Lines:
    """The clearing lines of a recipe, each name once (the first group that names it), with what was not saved."""

    def __init__(self, types):
        self.types = types
        self.groups = []
        self.names = set()
        self.not_saved = []

    def group(self, title):
        self.groups.append((title, []))

    def clear(self, name, zero=False):
        """A Bool or Int flag back to its new-game value (to 0 with zero: a flag the route's A sets)."""
        if name in self.names:
            return
        self.names.add(name)
        t = self.types.lookup(name)
        if t is None:
            self.not_saved.append(name)
            self.groups[-1][1].append(f"# not saved: {name}")
            return
        kind, default = t
        value = 0 if zero else default
        self.groups[-1][1].append(f"{'bool' if kind == ps.BOOL else 'int'} {name} {value}")

    def line(self, text, key=None):
        if key is not None:
            if key in self.names:
                return
            self.names.add(key)
        self.groups[-1][1].append(text)

    def text(self):
        out = []
        for title, lines in self.groups:
            if lines:
                out.append(f"# --- {title} ({len(lines)})")
                out += lines
        return out


def _matches(name, prefixes):
    low = name.lower()
    return any(low.startswith(p.lower()) for p in prefixes)


def entrance_lines(e, src, types, route_flags, route_events, states):
    """The recipe text (list of lines) of entrance e. states: {stem: recipe_state(stem)}."""
    items_state, story_state = states[e.items], states[e.story]
    block = BLOCK[e.story]
    protected = set(story_state)  # what the entrance's story recipe sets itself stays as it says
    out = [f"# {e.name}: the save at the entrance of {e.note}. Just arrived, nothing of the block done.",
           "# Written by entradas.py (python tools/orm/saves/entradas.py write): change its table, not this",
           f"# file. Route: recorrido-{e.route} (tools/harness/escenarios). Items: {e.items}; story: {e.story}.",
           f"include {e.items}.txt"]
    out += [x for x in e.extra]
    L = Lines(types)

    L.group(f"every flag recorrido-{e.route}'s A sets: 0, so B sees it go 0 -> 1")
    for name in route_flags:
        L.clear(name, zero=True)

    by_name = {ev.name: ev for ev in src.story}
    L.group(f"the story events recorrido-{e.route} asks for, not played yet")
    for name in route_events:
        L.clear("HavePlayedEvent." + name, zero=True)
        for f in by_name[name].flags if name in by_name else ():
            L.clear(f, zero=True)

    L.group("the block's flags in shared_dungeon_child.txt (sections "
            + (", ".join(map(str, e.sections)) or "none") + "), coop_dungeon.cpp and shared_dungeon_adult.txt")
    for section, kind, name in src.child:
        if kind == "flag" and section in e.sections and ps.murmur3_32(name) not in protected:
            L.clear(name)
    for name in src.builtin_flags + [n for s, k, n in src.adult if k == "flag"]:
        if _matches(name, e.prefixes) and ps.murmur3_32(name) not in protected:
            L.clear(name)
    for d in e.dungeons:
        for member in ("IsGetMap", "IsGetCompass", "IsGetBossKey", "SmallKeyNum"):
            name = f"DungeonInfo.{d}.{member}"
            if ps.murmur3_32(name) not in protected:
                L.clear(name)

    L.group("the block's one-time keys (chests, walls, rocks, enemy counters): not taken")
    keys = [k for s, kind, k in src.child if kind == "key" and s in e.sections]
    keys += [k for k, scene in src.builtin_keys if _matches(scene, e.prefixes)]
    for k in keys:
        L.line(f"key64 {k} 0", key=int(k, 16))

    done, pending = story_split(src.story, story_state, block, forced=route_events)
    done_flags = {f for ev in done for f in ev.flags + ["HavePlayedEvent." + ev.name]}
    L.group("the story events not played at the entrance (shared_story.txt)")
    for ev in pending:
        for f in ["HavePlayedEvent." + ev.name] + ev.flags:
            if f not in done_flags and ps.murmur3_32(f) not in protected:
                L.clear(f)

    later = ORDER[ORDER.index(e.story) + 1:]
    L.group("what the later recipes set (" + (f"{later[0]} to {later[-1]}" if later else "none") + ")")
    for stem in later:
        for h, op in states[stem].items():
            if (op.name and op.kind in (ps.BOOL, ps.INT) and not op.name.startswith(NOT_PROGRESS)
                    and h not in protected):
                L.clear(op.name)

    # Steps: the entrance's own where it sets them; else the furthest a played event moved them; else new-game.
    L.group("the story steps at the entrance")
    quests = {}
    for ev in src.story:
        for q, values in ev.steps.items():
            quests.setdefault(q, values)
    for stem in ORDER:
        for op in states[stem].values():
            if op.kind == ps.ENUM and op.name and op.name.startswith("Step_"):
                quests.setdefault(op.name, None)
    implied = {}
    for ev in done:
        for q, v in ev.targets:
            if ps.murmur3_32(q) not in story_state:
                vals = ev.steps[q]
                if q not in implied or _idx(vals, v) > _idx(vals, implied[q]):
                    implied[q] = v
    for q in sorted(quests):
        h = ps.murmur3_32(q)
        if h in story_state:
            want = story_state[h].value
        elif q in implied:
            want = implied[q]
        else:
            t = types.lookup(q, kinds=(ps.ENUM,))
            if t is None:
                L.line(f"# not saved: {q}", key=q)
                continue
            want = t[1]
        have = items_state.get(h)
        if have is not None and have.value == want and h in story_state:
            continue  # the included recipe already leaves it there
        L.line(f"step {q} {want}", key=q)

    L.group("rewards not earned yet")
    extra_items = {x.split()[1] for x in e.extra if x.startswith("item ")}
    for item in REWARDS:
        op = story_state.get(ps.murmur3_32(f"PouchData.{item}.StockNum"))
        if (op is None or op.value == 0) and item not in extra_items:
            if types.lookup(f"PouchData.{item}.StockNum") is None:
                L.line(f"# not saved: PouchData.{item}.StockNum", key="item " + item)
                continue
            L.line(f"item {item} 0", key="item " + item)
            L.clear("IsGet." + item)
    return out + L.text(), L.not_saved


def write_all(src, types, folder=RECIPES, scenarios=SCENARIOS, only_check=False):
    """Writes (or with only_check compares) every entrance recipe. Returns [(name, status, not_saved)]."""
    states = {stem: recipe_state(stem) for stem in ORDER}
    report = []
    for e in ENTRADAS:
        flags, events = route_actions(e.route, scenarios)
        lines, not_saved = entrance_lines(e, src, types, flags, events, states)
        text = "\n".join(lines) + "\n"
        path = os.path.join(folder, e.name + ".txt")
        old = open(path, encoding="utf-8").read() if os.path.isfile(path) else None
        if only_check:
            report.append((e.name, "same" if old == text else "DIFFERS", not_saved))
        else:
            if old != text:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
            report.append((e.name, "written" if old != text else "same", not_saved))
    return report


# ---------------------------------------------------------------- reading a built save back


def check_save(save, e, scenarios=SCENARIOS):
    """(problems, checked, unsaved) of a built entrance save. problems: a flag the route's A sets that does not read
    0, an event the route asks for already played, a boss item of the entrance missing. checked: how many route
    names read 0. unsaved: the route names the save does not hold (GameDataList SaveFileIndex -1: the game starts
    them at their new-game value on every load, so they cannot be 1 at the entrance)."""
    problems, checked, unsaved = [], 0, []
    flags, events = route_actions(e.route, scenarios)
    for name in flags + ["HavePlayedEvent." + ev for ev in events]:
        h = ps.murmur3_32(name)
        kinds = [k for k in (ps.BOOL, ps.INT) if save.has(k, h)]
        if not kinds:
            unsaved.append(name)
        for k in kinds:
            if save.get(k, h) != 0:
                problems.append(f"{name} reads {save.get(k, h)}, the route's A needs 0")
            else:
                checked += 1
    for x in e.extra:
        if x.startswith("item "):
            item = x.split()[1]
            h = ps.murmur3_32(f"PouchData.{item}.StockNum")
            if not save.has(ps.INT, h) or save.get(ps.INT, h) < 1:
                problems.append(f"{item}: not in the pouch")
    return problems, checked, unsaved


def verify_built(root, scenarios=SCENARIOS, entradas=None):
    """[(entrance, problems, names read 0 per slot, names not saved)] of every built entrance save under root."""
    out = []
    for e in entradas or ENTRADAS:
        folder = os.path.join(root, e.name)
        slots = pt.slots_of(folder)
        if not slots:
            out.append((e.name, [f"{folder}: no slot_NN/progress.sav (not built)"], 0, []))
            continue
        problems, checked, unsaved = [], 0, []
        for s, p in slots.items():
            got, checked, unsaved = check_save(ps.read(p), e, scenarios)
            problems += [f"{s}: {x}" for x in got]
        out.append((e.name, problems, checked, unsaved))
    return out


def _verdict(name, problems, checked, unsaved):
    if problems:
        return f"FAIL  {name}: " + "; ".join(problems[:20])
    return (f"OK    {name}: {checked} route flags and events read 0 in every slot"
            + (f"; not saved (new-game value on load): {', '.join(unsaved)}" if unsaved else ""))


# ---------------------------------------------------------------- CLI


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("write", "check"):
        s = sub.add_parser(name)
        s.add_argument("--kit", default=DEFAULT_KIT)
        s.add_argument("--romfs", default=pt.DEFAULT_ROMFS)
    a_ = sub.add_parser("all")
    a_.add_argument("--base", default=pt.DEFAULT_BASE)
    a_.add_argument("--slot", default="auto")
    a_.add_argument("--root", default=pt.SAVES_ROOT)
    a_.add_argument("--romfs", default=pt.DEFAULT_ROMFS)
    v = sub.add_parser("verify")
    v.add_argument("--root", default=pt.SAVES_ROOT)
    sub.add_parser("map")
    a = p.parse_args(argv)
    try:
        if a.cmd == "map":
            for e in ENTRADAS:
                print(f"recorrido-{e.route:<18} #! saves {e.name}   ({e.items}; {e.note})")
            return 0
        if a.cmd in ("write", "check"):
            info = pt.load_romfs(a.romfs, required=True)
            report = write_all(Sources.from_kit(a.kit), Types.from_romfs(info), only_check=a.cmd == "check")
            bad = 0
            for name, status, not_saved in report:
                bad += status == "DIFFERS"
                print(f"{name}: {status}" + (f"; not saved: {', '.join(not_saved)}" if not_saved else ""))
            return 1 if bad else 0
        if a.cmd == "all":
            info = pt.load_romfs(a.romfs)
            lines, bad, t0 = [], 0, time.time()
            for e in ENTRADAS:
                try:
                    text = pt.build(os.path.join(RECIPES, e.name + ".txt"), a.base, a.slot,
                                    os.path.join(a.root, e.name), info)
                    lines.append(f"built {e.name}: {text.splitlines()[7]}")
                except (pt.BuildError, rc.RecipeError, ps.SaveError, OSError) as ex:
                    bad += 1
                    lines.append(f"ERROR {e.name}: {ex}")
            for row in verify_built(a.root):
                bad += bool(row[1])
                lines.append(_verdict(*row))
            head = (f"entradas {time.strftime('%Y-%m-%d %H:%M')}: {len(ENTRADAS)} saves, {bad} problems, "
                    f"{time.time() - t0:.0f} s; base {a.base}; GameDataList {'checked' if info else 'NOT checked'}")
            text = head + "\n" + "\n".join(lines) + "\n"
            with open(os.path.join(a.root, SUMMARY), "w", encoding="utf-8") as f:
                f.write(text)
            print(text, end="")
            return 1 if bad else 0
        if a.cmd == "verify":
            rows = verify_built(a.root)
            for row in rows:
                print(_verdict(*row))
            return 1 if any(row[1] for row in rows) else 0
    except (pt.BuildError, rc.RecipeError, ps.SaveError, OSError) as ex:
        print(f"entradas: {ex}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
