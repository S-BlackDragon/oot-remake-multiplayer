"""One place for the co-op protocol, our message type numbers, the stack the harness runs and the hook switch names.

    python -I tools/orm/harness/protocolo.py show              # protocol in use and our message types
    python -I tools/orm/harness/protocolo.py env [--stack test9]   # what ORM_STACK sets (secuencia.ps1)
    python -I tools/orm/harness/protocolo.py switches K=V,K=V ...  # -Switches as the hook of this stack reads them

- Protocol: ORM_PROTOCOL (108 or 109) if set; else the ORM_STACK's; else the one of the orm_server in ORM_SERVER_EXE
  (its build folder's CMakeCache.txt names the daurosk tree, whose messages.hpp says kProtocolVersion); else 108.
- ORM_STACK=test8|test9 fills ORM_BUILD_BIN, ORM_SERVER_EXE, ORM_BOT_EXE and ORM_INST_DIR where they are not set
  (orm_inst.py does it on import, secuencia.ps1 through `env`) and says which of those paths is missing.
- Switches: since test9 our own switches are LICHII_ORM_* (ADOPTAR_UPSTREAM.md rule 6); the old LICHII_COOP_* names
  are their aliases. `hook_switches` hands each stack's hook the names it reads (PIN_TEST9.md "Historia").

Doc: docs/ARNES.md. Sources: docs/PIN_TEST9.md (types 240-245, protocol 109), docs/PIN_TEST8.md.
"""
import argparse, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import orm_paths


def _r(rel: str) -> str:
    """A path under ORM_ROOT (orm_paths.py), as a forward-slash string."""
    return orm_paths.posix(orm_paths.ROOT / rel)


# Our message types (coop/protocol/include/coop/protocol/messages.hpp, the "ORM:" lines of MessageType). Up to 108 they
# sat at 60-65; test9 moved them out of every block upstream has used, to 240-245 (246-254 free).
PROTOCOLS: dict[int, dict[str, int]] = {
    108: {"revive": 60, "revived": 61, "group_defeat": 62, "story_event": 63, "player_look": 64, "object_pose": 65},
    109: {"revive": 240, "revived": 241, "group_defeat": 242, "story_event": 243, "player_look": 244,
          "object_pose": 245},
    # test10 took 241-255 (PvP, HUD): ours moved to their own block 0xF000-0xF0FF, same order (docs/PIN_TEST10.md)
    110: {"revive": 0xF000, "revived": 0xF001, "group_defeat": 0xF002, "story_event": 0xF003, "player_look": 0xF004,
          "object_pose": 0xF005},
}
DEFAULT_PROTOCOL = 108  # the stack of kit 0.10 (D-R16) until test9 is the one served

# The stacks (docs/PIN_TEST8.md, docs/PIN_TEST9.md). "tree" is the daurosk worktree the hook and server come from.
STACKS: dict[str, dict] = {
    "test8": {"protocol": 108, "tree": _r("wt/daurosk-test8"),
              "ORM_BUILD_BIN": _r("fritchii-src-584/build/coop/bin"),
              "ORM_SERVER_EXE": _r("orm-server-build-test8/orm_server.exe"),
              "ORM_BOT_EXE": _r("remake-bot-108.exe"),
              "ORM_INST_DIR": _r("inst-test8")},
    "test9": {"protocol": 109, "tree": _r("wt/daurosk-test9"),
              "ORM_BUILD_BIN": _r("fritchii-src-584/build/coop-t9/bin"),
              "ORM_SERVER_EXE": _r("orm-server-build-test9/orm_server.exe"),
              "ORM_BOT_EXE": _r("remake-bot-109.exe"),
              "ORM_INST_DIR": _r("inst-t9")},
    # test10 (docs/PIN_TEST10.md): orm/t10-integrado, its emulator build coop-t10, orm_server 110 (sealed traffic, a
    # join password always: r03_pareja gives one), the 110 bot, test10's puppet pack (ORM_PUPPET_LOAD). No kit step
    # table of its own yet ("kit": False).
    "test10": {"protocol": 110, "tree": _r("wt/t10-integrado"), "kit": False,
               "ORM_BUILD_BIN": _r("fritchii-src-584-t9/build/coop-t10/bin"),
               "ORM_SERVER_EXE": _r("orm-server-build-t10int/orm_server.exe"),
               "ORM_BOT_EXE": _r("remake-bot-110.exe"),
               "ORM_INST_DIR": _r("inst-t10"),
               "ORM_PUPPET_LOAD": _r("puppet_user-t10/load")},
}
STACK_VARS = ("ORM_BUILD_BIN", "ORM_SERVER_EXE", "ORM_BOT_EXE", "ORM_INST_DIR")
OPTIONAL_VARS = ("ORM_PUPPET_LOAD",)  # set only by the stacks that name it

# Our switch (LICHII_ORM_*) -> its older name (LICHII_COOP_*). The first two are read under the new name by the
# protocol-109 hook (orm_story_events.cpp; the old name still works there); the rest are still read under the old name
# on both stacks and get the new one when INTEGRACION.md section 5 renames them.
SWITCH_ALIASES: dict[str, str] = {
    "LICHII_ORM_STORY_EVENTS": "LICHII_COOP_STORY",
    "LICHII_ORM_STORY_LIST": "LICHII_COOP_STORY_LIST",
    "LICHII_ORM_ITEM_RULES": "LICHII_COOP_ITEM_RULES",
    "LICHII_ORM_GROUP_EXIT": "LICHII_COOP_GROUP_EXIT",
    "LICHII_ORM_AGE": "LICHII_COOP_AGE",
    "LICHII_ORM_AGE_EVENT": "LICHII_COOP_AGE_EVENT",
    "LICHII_ORM_OCARINA_HAND": "LICHII_COOP_OCARINA_HAND",
    "LICHII_ORM_COUNTS": "LICHII_COOP_COUNTS",
}
# The LICHII_ORM_* names each protocol's hook reads itself.
NATIVE_ORM: dict[int, frozenset] = {
    108: frozenset(),
    109: frozenset({"LICHII_ORM_STORY_EVENTS", "LICHII_ORM_STORY_LIST"}),
    110: frozenset({"LICHII_ORM_STORY_EVENTS", "LICHII_ORM_STORY_LIST"}),
}
# Old names that upstream also uses from 109 on: LICHII_COOP_STORY=0 there turns off upstream's shared story flags too,
# so a LICHII_ORM_STORY_EVENTS=0 is never copied into it on 109.
UPSTREAM_TOO: dict[int, frozenset] = {108: frozenset(), 109: frozenset({"LICHII_COOP_STORY"}),
                                      110: frozenset({"LICHII_COOP_STORY"})}
SWITCH_KEY = re.compile(r"^(LICHII_COOP_|LICHII_ORM_|EDEN_NX2_)[A-Z0-9_]+$")
# Items are split at a comma, semicolon or blank only where the next item starts (KEY=), so a value may hold commas:
# LICHII_COOP_DUNGEON_LISTS=a.txt,b.txt stays one item.
ITEM_SPLIT = re.compile(r"[,;\s]+(?=[A-Za-z0-9_:]+=)")
KPROTO = re.compile(r"kProtocolVersion\s*=\s*(\d+)")
# The join password the harness's server and games share on protocol 110 and later (daurosk test10 lets nobody in
# without one), given as the 0.12 kit does: the server reads it from an environment variable (--password-env), each
# game from its coop.ini (password = ...). Test-only, on 127.0.0.1.
JOIN_PASSWORD_FROM = 110
HARNESS_PASSWORD = "orm-harness-join-pass"
HARNESS_PASSWORD_ENV = "ORM_HARNESS_JOIN_PASSWORD"


class StackError(ValueError):
    pass


def stack_name(env=None) -> str | None:
    """The ORM_STACK in env (lower case), None when unset; StackError for one not in STACKS."""
    env = os.environ if env is None else env
    name = (env.get("ORM_STACK") or "").strip().lower()
    if not name:
        return None
    if name not in STACKS:
        raise StackError(f"ORM_STACK={name}: unknown stack (known: {', '.join(STACKS)})")
    return name


def tree_protocol(tree) -> int | None:
    """kProtocolVersion of a daurosk tree's messages.hpp, None when there is none."""
    p = Path(tree) / "coop/protocol/include/coop/protocol/messages.hpp"
    try:
        m = KPROTO.search(p.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return None
    return int(m.group(1)) if m else None


def tree_types(tree) -> dict[str, int]:
    """Our ORM message types as a daurosk tree's messages.hpp numbers them ({name: number}; 'revived' is a comment)."""
    p = Path(tree) / "coop/protocol/include/coop/protocol/messages.hpp"
    try:
        text = p.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    out = {m[1]: int(m[2]) for m in re.finditer(r"^\s*(\w+)\s*=\s*(\d+),\s*//\s*ORM:", text, re.M)}
    named = re.findall(r"^\s*(\w+)\s*=\s*orm::type::(\w+),\s*//\s*ORM:", text, re.M)
    if named:  # protocol 110: the numbers live in orm_types.hpp (revive = 0xF000, ...)
        try:
            src = (p.parent / "orm_types.hpp").read_text(encoding="utf-8", errors="replace")
        except OSError:
            src = ""
        nums = {m[1]: int(m[2], 0) for m in re.finditer(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+),", src, re.M)}
        out.update({k: nums[v] for k, v in named if v in nums and k in PROTOCOLS[110]})
    return out


def server_protocol(exe) -> int | None:
    """The protocol an orm_server.exe was built with: orm-server/build.ps1 leaves CMakeCache.txt beside it, whose
    DAUROSK_DIR is the tree it was compiled from. None when that cannot be told."""
    cache = Path(exe).parent / "CMakeCache.txt"
    try:
        m = re.search(r"^DAUROSK_DIR:PATH=(.+?)\s*$", cache.read_text(encoding="utf-8", errors="replace"), re.M)
    except OSError:
        return None
    return tree_protocol(m.group(1)) if m else None


def protocol(env=None) -> int:
    """The co-op protocol the harness talks: ORM_PROTOCOL, the stack's, the server's, or DEFAULT_PROTOCOL."""
    env = os.environ if env is None else env
    raw = (env.get("ORM_PROTOCOL") or "").strip()
    if raw:
        if not raw.isdigit() or int(raw) not in PROTOCOLS:
            raise StackError(f"ORM_PROTOCOL={raw}: unknown protocol (known: {', '.join(map(str, PROTOCOLS))})")
        return int(raw)
    name = stack_name(env)
    if name:
        return STACKS[name]["protocol"]
    exe = env.get("ORM_SERVER_EXE")
    p = server_protocol(exe) if exe else None
    return p if p in PROTOCOLS else DEFAULT_PROTOCOL


def msg_type(name: str, proto: int | None = None) -> int:
    """Number of one of our message types ('revive', 'story_event', ... or REVIVE, STORY_EVENT) in a protocol."""
    table = PROTOCOLS[protocol() if proto is None else proto]
    return table[name.lower()]


def msg_name(number: int, proto: int | None = None) -> str | None:
    """Name of our message type `number` in a protocol (None when it is not one of ours)."""
    table = PROTOCOLS[protocol() if proto is None else proto]
    return next((k for k, v in table.items() if v == number), None)


def stack_problems(name: str, values: dict) -> list[str]:
    """What is missing for a stack, one clear line each ([] when all is there). values: the four ORM_* paths."""
    out = []
    bin_dir = Path(values["ORM_BUILD_BIN"])
    if not (bin_dir / "eden-cli.exe").is_file():
        out.append(f"stack {name}: no emulator at {bin_dir / 'eden-cli.exe'} (ORM_BUILD_BIN; build it first: "
                   f"docs/PIN_TEST9.md 'Construcciones nuevas')")
    for var, what in (("ORM_SERVER_EXE", "orm_server"), ("ORM_BOT_EXE", "bot (remake-bot)")):
        if not Path(values[var]).is_file():
            out.append(f"stack {name}: no {what} at {values[var]} ({var})")
    if "ORM_PUPPET_LOAD" in values and not Path(values["ORM_PUPPET_LOAD"]).is_dir():
        out.append(f"stack {name}: no puppet pack at {values['ORM_PUPPET_LOAD']} (ORM_PUPPET_LOAD; build it with the "
                   f"stack tree's tools/puppet_pack/build.py --user-dir <its parent>)")
    inst = Path(values["ORM_INST_DIR"])
    if not inst.parent.is_dir():
        out.append(f"stack {name}: the instance folder's parent {inst.parent} does not exist (ORM_INST_DIR)")
    return out


def stack_values(name: str, env=None) -> tuple[dict, dict]:
    """(values, set_here): the four ORM_* paths for a stack, an explicit environment value winning, and the ones the
    stack supplies (not set in env)."""
    env = os.environ if env is None else env
    values, set_here = {}, {}
    for var in STACK_VARS + OPTIONAL_VARS:
        if var not in STACKS[name] and var in OPTIONAL_VARS:
            continue
        if env.get(var):
            values[var] = env[var]
        else:
            values[var] = set_here[var] = str(Path(STACKS[name][var]))
    return values, set_here


def apply_stack(env=None) -> list[str]:
    """ORM_STACK: sets the four ORM_* paths not set in env (os.environ by default). Returns the problems (missing
    paths, unknown stack) as lines; [] when ORM_STACK is unset or all is there."""
    env = os.environ if env is None else env
    try:
        name = stack_name(env)
    except StackError as e:
        return [str(e)]
    if not name:
        return []
    values, set_here = stack_values(name, env)
    env.update(set_here)
    return stack_problems(name, values)


def split_items(items) -> list[str]:
    """K=V items from -Switches strings (each may hold several, split by ',', ';' or blanks before the next KEY=)."""
    out = []
    for raw in items:
        out += [p.strip() for p in ITEM_SPLIT.split(raw.strip()) if p.strip()]
    return out


def hook_switches(items, proto: int | None = None) -> list[str]:
    """The K=V switches for both games, as the hook of `proto` reads them (order kept, a repeated key: last wins).

    - LICHII_ORM_X=V given alone: also LICHII_COOP_X=V when the hook does not read the new name itself (all of them
      on 108; on 109 the renames not done yet), except a 0 into a name upstream also uses (LICHII_COOP_STORY on 109).
    - LICHII_COOP_X=V given alone, where the hook reads LICHII_ORM_X: also LICHII_ORM_X=V (the log then names it).
    - Both given: both as given. ValueError for an item that is not KEY=VALUE or not a hook switch, and for A:/B:
      (one game only: secuencia's switches go to both games)."""
    proto = protocol() if proto is None else proto
    native, upstream = NATIVE_ORM[proto], UPSTREAM_TOO[proto]
    pairs: dict[str, str] = {}
    for item in split_items(items):
        key, eq, value = item.partition("=")
        key = key.strip()
        if re.match(r"^[AB]:", key):
            raise ValueError(f"{item}: a switch for one game only (A:/B:) cannot go through -Switches (both games)")
        if not eq or not key:
            raise ValueError(f"{item}: not KEY=VALUE")
        if not SWITCH_KEY.match(key):
            raise ValueError(f"{item}: not a hook switch (LICHII_COOP_*, LICHII_ORM_* or EDEN_NX2_*)")
        pairs.pop(key, None)
        pairs[key] = value.strip()
    out = dict(pairs)
    for new, old in SWITCH_ALIASES.items():
        if new in pairs and old not in pairs:
            v = pairs[new]
            if new not in native and not (old in upstream and v == "0"):
                out[old] = v
        elif old in pairs and new not in pairs and new in native:
            out[new] = pairs[old]
    return [f"{k}={v}" for k, v in out.items()]


def header_switches(scenario) -> list[str]:
    """The items of a scenario file's `#! switches` lines (daurosk's run.ps1 reads them; r03_pareja does not), in order."""
    text = Path(scenario).read_text(encoding="utf-8", errors="replace")
    return split_items(m.group(1) for m in re.finditer(r"^#!\s*switches\s+(.+?)\s*$", text, re.M))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="co-op protocol, stack and switch names (docs/ARNES.md)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("show", help="the protocol in use and our message types")
    e = sub.add_parser("env", help="'set K=V' lines for what ORM_STACK fills, 'problem: ...' lines; exit 2 on a problem")
    e.add_argument("--stack", default="", help="this stack instead of ORM_STACK")
    s = sub.add_parser("switches", help="one K=V per line, as this stack's hook reads them; exit 2 on a bad item")
    s.add_argument("--stack", default="", help="this stack instead of ORM_STACK")
    s.add_argument("--scenario", default="", help="also this scenario file's '#! switches' (first; the items win)")
    s.add_argument("items", nargs="*")
    a = ap.parse_args(argv)
    env = dict(os.environ)
    if getattr(a, "stack", ""):
        env["ORM_STACK"] = a.stack
    try:
        proto = protocol(env)
    except StackError as err:
        print(f"problem: {err}")
        return 2
    if a.cmd == "show":
        print(f"protocol {proto} (stack {stack_name(env) or '-'})")
        for k, v in PROTOCOLS[proto].items():
            print(f"  {k.upper():<13} {v}")
        return 0
    if a.cmd == "env":
        name = stack_name(env)
        if not name:
            print(f"note: no ORM_STACK; protocol {proto}")
            return 0
        values, set_here = stack_values(name, env)
        for k, v in set_here.items():
            print(f"set {k}={v}")
        for k in STACK_VARS:
            if k not in set_here:
                print(f"note: {k}={values[k]} (set explicitly, kept)")
        if not Path(values["ORM_INST_DIR"]).is_dir():
            print(f"note: {values['ORM_INST_DIR']} does not exist yet (made on the first run)")
        problems = stack_problems(name, values)
        for p in problems:
            print(f"problem: {p}")
        print(f"note: stack {name}, protocol {proto}")
        return 2 if problems else 0
    try:
        items = (header_switches(a.scenario) if a.scenario else []) + list(a.items)
        for line in hook_switches(items, proto):
            print(line)
    except (ValueError, OSError) as err:
        print(f"problem: {err}")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
