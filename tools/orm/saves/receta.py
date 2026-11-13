"""Recipes for synthetic saves: a text file of what a save holds at one point of the game, by flag name.

One directive a line; `#` starts a comment; names are the game's (GameDataList), hashed here with murmur3. A name
may also be a raw hash (0x1234abcd). Lines apply in order, an `include`d recipe first, so a later line wins.

  include FILE                     the lines of another recipe (relative to this one), first
  age child|adult                  Sequence_IsChildhood
  scene BANC [STARTPOS | X Y Z]    Sequence_CurrentBanc (+ the save's String32 copy) and Link's saved position: a
                                   StartPos of that Banc (read from the romfs at build time) or coordinates
  hearts N                         PlayerStatus.MaxLife and Life = 4 N (quarter hearts)
  rupees N                         PlayerStatus.CurrentRupee
  item NAME [N]                    PouchData.NAME.StockNum = N (1), IsGet.NAME = 1 when the save has it, and what the
                                   game's own get gives with it (GRANTS: ammo room and ammo, the lift grade)
  song NAME                        IsGet.Item_Melody_NAME (ZeldaLullaby, EponaSong, SariaSong, SunSong, TimeSong,
                                   StormSong, ForestMinuet, FireBolero, WaterSerenade, ShadowNocturne,
                                   SpiritRequiem, LightPrelude, ScarecrowSong)
  step QUEST VALUE                 Enum Step_QUEST = VALUE (Step_ may be written or not)
  seen NAME...                     HavePlayedEvent.NAME = 1 for each (story scenes and events already played)
  dungeon NAME [map] [compass] [bosskey] [keys=N]
                                   DungeonInfo.NAME.IsGetMap / IsGetCompass / IsGetBossKey, SmallKeyNum
  equip SLOT ACTOR                 PlayerStatus.Equip<Slot> (weapon shield armor boot headarmor handarmor ocarina
  other-era SLOT ACTOR             arrow); other-era: PlayerStatus.AnotherEraEquip<Slot>; ACTOR - = nothing
  bottle A-E CONTENT               PouchData.Bottle<X>.StockNum 1, its IsValid and BottleItem (Empty, Fish, ...)
  bool NAME [0|1]   int NAME N   uint NAME N   float NAME X   enum NAME VALUE   string NAME TEXT (String64)
  key64 KEY [0|1]                  a 64-bit key (a placement's Hash: chest, pickup) true or not

parse() returns a Recipe: its ops in order, the files read, the scene request and the age. Nothing here reads a
save or the romfs (partida.py does), so the parser is tested without game data.
"""
import os
import re
import shlex

from progress_sav import BOOL, ENUM, FLOAT, INT, STRING32, STRING64, UINT, VECTOR3, murmur3_32

SET, ATLEAST = "set", "atleast"
KEY64 = "key64"

CHILDHOOD = "Sequence_IsChildhood"
CURRENT_BANC = "Sequence_CurrentBanc"
# Unnamed flags every save seen holds next to the scene (docs/PARTIDAS_SINTETICAS.md): a String32 that
# repeats Sequence_CurrentBanc and three Vector3 that repeat Link's position. Written when the save has them.
BANC_COPY_STRING32 = 0x4A6BDB7D
POSITION_VECTORS = (0x960AD56A, 0xC884818D, 0x8773F02D)

LIFT_GRADE = "PlayerStatus.LiftGrade"
LIFT_ORDER = ("None", "Low", "Middle", "High")

# Ammo room: an unnamed Int struct (0xf80f8077) whose member flags have hashes only (daurosk docs/items.md).
CAP_RUPEE, CAP_SEED, CAP_BOMB, CAP_ARROW, CAP_STICK, CAP_NUT = (
    0xE0010A50, 0xAE8CC742, 0xE753508B, 0x418DB791, 0x457616B0, 0x735B00F7)


def _ammo(cap, room, item=None, fill=None, isget=None):
    out = [(INT, cap, room)]
    if item:
        out.append((INT, f"PouchData.{item}.StockNum", room if fill is None else fill))
    if isget:
        out.append((BOOL, f"IsGet.{isget}", 1))
    return out


# What the game's own get scene gives with an item (daurosk docs/items.md, "Ammo capacity", the original game's
# numbers), applied as "at least" so nothing already there is lowered. The gauntlets raise the lift grade.
GRANTS = {
    "Weapon_Range_SlingShot": _ammo(CAP_SEED, 30, "Item_DekuSeed", 30, "Item_DekuSeed"),
    "Item_BulletBag_Small": _ammo(CAP_SEED, 30, "Item_DekuSeed"),
    "Item_BulletBag_Medium": _ammo(CAP_SEED, 40, "Item_DekuSeed"),
    "Item_BulletBag_Large": _ammo(CAP_SEED, 50, "Item_DekuSeed"),
    "Weapon_Range_Bow": _ammo(CAP_ARROW, 30, "Item_Arrow_Normal", 30, "Item_Arrow_Normal"),
    "Item_Quiver_Small": _ammo(CAP_ARROW, 30, "Item_Arrow_Normal"),
    "Item_Quiver_Medium": _ammo(CAP_ARROW, 40, "Item_Arrow_Normal"),
    "Item_Quiver_Large": _ammo(CAP_ARROW, 50, "Item_Arrow_Normal"),
    "Item_BombBag_Small": _ammo(CAP_BOMB, 20, "Item_Bomb", None, "Item_Bomb"),
    "Item_BombBag_Medium": _ammo(CAP_BOMB, 30, "Item_Bomb", None, "Item_Bomb"),
    "Item_BombBag_Large": _ammo(CAP_BOMB, 40, "Item_Bomb", None, "Item_Bomb"),
    "Item_NutBag_Small": _ammo(CAP_NUT, 20, "Item_DekuNut", None, "Item_DekuNut"),
    "Item_NutBag_Medium": _ammo(CAP_NUT, 30, "Item_DekuNut", None, "Item_DekuNut"),
    "Item_NutBag_Large": _ammo(CAP_NUT, 40, "Item_DekuNut", None, "Item_DekuNut"),
    "Item_StickBag_Small": _ammo(CAP_STICK, 10, "Item_DekuStick", None, "Item_DekuStick"),
    "Item_StickBag_Medium": _ammo(CAP_STICK, 20, "Item_DekuStick", None, "Item_DekuStick"),
    "Item_StickBag_Large": _ammo(CAP_STICK, 30, "Item_DekuStick", None, "Item_DekuStick"),
    "Item_DekuStick": _ammo(CAP_STICK, 10),
    "Item_DekuNut": _ammo(CAP_NUT, 20),
    "Item_Wallet_Small": _ammo(CAP_RUPEE, 200),
    "Item_Wallet_Medium": _ammo(CAP_RUPEE, 500),
    "Armor_Child_Gauntlet_Goron": [(ENUM, LIFT_GRADE, "Low")],
    "Armor_Adult_Gauntlet_Silver": [(ENUM, LIFT_GRADE, "Middle")],
    "Armor_Adult_Gauntlet_Gold": [(ENUM, LIFT_GRADE, "High")],
}

SLOTS = {"weapon": "Weapon", "shield": "Shield", "armor": "Armor", "boot": "Boot", "headarmor": "HeadArmor",
         "handarmor": "HandArmor", "ocarina": "Ocarina", "arrow": "Arrow"}
BOTTLES = "ABCDE"
BOTTLE_INFO = "PlayerStatus.AssignableItem.BottleInfo.Bottle{}"
SCALAR = {"bool": BOOL, "int": INT, "uint": UINT, "float": FLOAT, "enum": ENUM, "string": STRING64}
HASH = re.compile(r"^0x[0-9a-fA-F]{1,8}$")
NAME = re.compile(r"^[A-Za-z0-9_.\-]+$")


class RecipeError(ValueError):
    pass


class Op:
    """One write. kind: a save type id or KEY64. name: the flag name (None for a raw hash). value: int, float, str,
    (x, y, z), or for an Enum the value's name (str; `value_hash` is its hash). mode: SET or ATLEAST (an Enum with
    ATLEAST is compared by `order`). optional: skipped when the save lacks the flag (IsGet of an item that has none,
    the unnamed scene copies); a required flag the save lacks is an error at build time."""

    __slots__ = ("kind", "hash", "name", "value", "mode", "order", "optional", "where")

    def __init__(self, kind, flag, value, where, mode=SET, order=None, optional=False):
        self.kind = kind
        if isinstance(flag, int):
            self.hash, self.name = flag, None
        else:
            self.hash, self.name = flag_hash(flag)
        self.value = value
        self.mode = mode
        self.order = order
        self.optional = optional
        self.where = where

    @property
    def value_hash(self):
        return murmur3_32(self.value) if self.kind == ENUM and isinstance(self.value, str) else self.value

    def label(self):
        return self.name or f"{self.hash:#010x}"

    def __repr__(self):
        return f"Op({self.kind}, {self.label()}, {self.value!r}, {self.mode})"


class Recipe:
    def __init__(self):
        self.ops = []
        self.files = []
        self.scene = None  # (banc, startpos or None, (x, y, z) or None, where)
        self.age = None


def flag_hash(token):
    """(hash, name) of a flag token: a raw 0x... hash (name None) or a name."""
    if HASH.match(token):
        return int(token, 16), None
    if not NAME.match(token):
        raise RecipeError(f"{token!r}: not a flag name")
    return murmur3_32(token), token


def _int(text, where):
    try:
        return int(text, 0)
    except ValueError:
        raise RecipeError(f"{where}: {text!r} is not a number") from None


def _float(text, where):
    try:
        return float(text)
    except ValueError:
        raise RecipeError(f"{where}: {text!r} is not a number") from None


def _flag01(args, where):
    if len(args) == 1:
        return 1
    if len(args) == 2 and args[1] in ("0", "1"):
        return int(args[1])
    raise RecipeError(f"{where}: expected NAME [0|1]")


def _need(args, n, usage, where):
    if len(args) != n:
        raise RecipeError(f"{where}: {usage}")


def parse_line(words, where, recipe):
    """Appends the ops of one directive (already split) to recipe."""
    cmd, args = words[0].lower(), words[1:]
    ops = recipe.ops
    if cmd == "age":
        _need(args, 1, "age child|adult", where)
        if args[0] not in ("child", "adult"):
            raise RecipeError(f"{where}: age child|adult")
        recipe.age = args[0]
        ops.append(Op(BOOL, CHILDHOOD, 1 if args[0] == "child" else 0, where))
    elif cmd == "scene":
        if len(args) == 2:
            recipe.scene = (args[0], args[1], None, where)
        elif len(args) == 4:
            recipe.scene = (args[0], None, tuple(_float(a, where) for a in args[1:]), where)
        elif len(args) == 1:
            recipe.scene = (args[0], None, None, where)
        else:
            raise RecipeError(f"{where}: scene BANC [STARTPOS | X Y Z]")
        if not NAME.match(args[0]) or len(args[0]) > 31:
            raise RecipeError(f"{where}: {args[0]!r}: not a Banc name (31 characters at most)")
    elif cmd == "hearts":
        _need(args, 1, "hearts N", where)
        n = _int(args[0], where)
        if not 1 <= n <= 20:
            raise RecipeError(f"{where}: hearts 1 to 20")
        ops.append(Op(INT, "PlayerStatus.MaxLife", 4 * n, where))
        ops.append(Op(INT, "PlayerStatus.Life", 4 * n, where))
    elif cmd == "rupees":
        _need(args, 1, "rupees N", where)
        ops.append(Op(INT, "PlayerStatus.CurrentRupee", _int(args[0], where), where))
    elif cmd == "item":
        if len(args) not in (1, 2):
            raise RecipeError(f"{where}: item NAME [N]")
        item = args[0]
        if not NAME.match(item) or HASH.match(item):
            raise RecipeError(f"{where}: item takes the item's actor name")
        n = _int(args[1], where) if len(args) == 2 else 1
        if n < 0:
            raise RecipeError(f"{where}: item count below 0")
        ops.append(Op(INT, f"PouchData.{item}.StockNum", n, where))
        if n > 0:
            ops.append(Op(BOOL, f"IsGet.{item}", 1, where, optional=True))
            for kind, flag, value in GRANTS.get(item, ()):
                order = LIFT_ORDER if kind == ENUM else None
                ops.append(Op(kind, flag, value, where, mode=ATLEAST, order=order))
    elif cmd == "song":
        _need(args, 1, "song NAME", where)
        ops.append(Op(BOOL, "IsGet.Item_Melody_" + args[0], 1, where))
    elif cmd == "step":
        _need(args, 2, "step QUEST VALUE", where)
        quest = args[0] if args[0].startswith("Step_") else "Step_" + args[0]
        ops.append(Op(ENUM, quest, args[1], where))
    elif cmd == "seen":
        if not args:
            raise RecipeError(f"{where}: seen NAME...")
        for name in args:
            ops.append(Op(BOOL, "HavePlayedEvent." + name, 1, where))
    elif cmd == "dungeon":
        if not args:
            raise RecipeError(f"{where}: dungeon NAME [map] [compass] [bosskey] [keys=N]")
        base = "DungeonInfo." + args[0]
        for a in args[1:]:
            if a == "map":
                ops.append(Op(BOOL, base + ".IsGetMap", 1, where))
            elif a == "compass":
                ops.append(Op(BOOL, base + ".IsGetCompass", 1, where))
            elif a == "bosskey":
                ops.append(Op(BOOL, base + ".IsGetBossKey", 1, where))
            elif a.startswith("keys="):
                ops.append(Op(INT, base + ".SmallKeyNum", _int(a[5:], where), where))
            else:
                raise RecipeError(f"{where}: {a!r}: map, compass, bosskey or keys=N")
    elif cmd in ("equip", "other-era"):
        _need(args, 2, f"{cmd} SLOT ACTOR", where)
        slot = SLOTS.get(args[0].lower())
        if slot is None:
            raise RecipeError(f"{where}: slot {args[0]!r}: one of {', '.join(SLOTS)}")
        actor = "" if args[1] == "-" else args[1]
        if actor and not NAME.match(actor):
            raise RecipeError(f"{where}: {actor!r}: not an actor name")
        prefix = "PlayerStatus.Equip" if cmd == "equip" else "PlayerStatus.AnotherEraEquip"
        ops.append(Op(STRING64, prefix + slot, actor, where))
    elif cmd == "bottle":
        _need(args, 2, "bottle A-E CONTENT", where)
        x = args[0].upper()
        if len(x) != 1 or x not in BOTTLES:
            raise RecipeError(f"{where}: bottle A to E")
        ops.append(Op(INT, f"PouchData.Bottle{x}.StockNum", 1, where))
        ops.append(Op(BOOL, BOTTLE_INFO.format(x) + ".IsValid", 1, where))
        ops.append(Op(ENUM, BOTTLE_INFO.format(x) + ".BottleItem", args[1], where))
    elif cmd in SCALAR:
        kind = SCALAR[cmd]
        if kind == BOOL:
            if not args:
                raise RecipeError(f"{where}: bool NAME [0|1]")
            ops.append(Op(BOOL, args[0], _flag01(args, where), where))
        else:
            _need(args, 2, f"{cmd} NAME VALUE", where)
            if kind in (INT, UINT):
                value = _int(args[1], where)
            elif kind == FLOAT:
                value = _float(args[1], where)
            else:
                value = args[1]
            ops.append(Op(kind, args[0], value, where))
    elif cmd == "key64":
        if not args or len(args) > 2:
            raise RecipeError(f"{where}: key64 KEY [0|1]")
        key = _int(args[0], where)
        if not 0 < key < 2**64:
            raise RecipeError(f"{where}: 64-bit key out of range")
        ops.append(Op(KEY64, key, _flag01(args, where), where))
    else:
        raise RecipeError(f"{where}: unknown directive {words[0]!r}")


def parse_text(text, source="<text>", recipe=None, resolve=None, _stack=()):
    """Parses recipe text into `recipe` (a new one by default). `resolve(name)` returns (path, text) for an
    include; parse() gives one that reads files next to `source`."""
    recipe = recipe or Recipe()
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = f"{os.path.basename(source)}:{n}"
        try:
            words = shlex.split(line, posix=True)
        except ValueError as e:
            raise RecipeError(f"{where}: {e}") from None
        if words[0].lower() == "include":
            _need(words[1:], 1, "include FILE", where)
            if resolve is None:
                raise RecipeError(f"{where}: include needs a recipe file")
            path, sub = resolve(words[1])
            key = os.path.normcase(os.path.abspath(path))
            if key in _stack:
                raise RecipeError(f"{where}: include loop through {words[1]}")
            parse_text(sub, path, recipe, _resolver(path), _stack + (key,))
            continue
        parse_line(words, where, recipe)
    if source not in recipe.files:
        recipe.files.append(source)
    return recipe


def _resolver(path):
    folder = os.path.dirname(os.path.abspath(path))

    def resolve(name):
        p = os.path.join(folder, name)
        if not os.path.isfile(p):
            raise RecipeError(f"{name}: no such recipe next to {os.path.basename(path)}")
        with open(p, encoding="utf-8") as f:
            return p, f.read()

    return resolve


def parse(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    key = os.path.normcase(os.path.abspath(path))
    return parse_text(text, path, None, _resolver(path), (key,))


def check_age_scene(recipe):
    """A scene with an age in its name must match the recipe's age."""
    if recipe.scene and recipe.age:
        banc = recipe.scene[0]
        if (banc.endswith("_Child") and recipe.age == "adult") or (banc.endswith("_Adult") and recipe.age == "child"):
            raise RecipeError(f"{recipe.scene[3]}: scene {banc} with age {recipe.age}")
