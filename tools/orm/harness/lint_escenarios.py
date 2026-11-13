"""Lint of the pair scenarios without a game (in-game time is the scarce resource).

    python -I tools/orm/harness/lint_escenarios.py [--wt <folder of trees>] [--trees t9-integrado|all]
                                                          [--report <md>] [--only <prefix>]

For every escenarios/*.A.txt, *.B.txt and *.steps.txt:
  - the `#!` header lines, read with r03_pareja's directive parser (`#! expect|never|count A|B <regex>
    [>=|<=|== N] [within <mark>..<mark>]`, plus noload/start/switches/timeout); every regex compiles;
  - every script line against the hook's test script parser (lichii/hook/coop_testinput.cpp): the command exists in
    orm/integracion or orm/test9 (or only in a feature branch: the scenario needs that branch) and its arguments
    take the shape Parse() wants;
  - every `warp SCENE [START]` against escenas_banc.txt (the romfs Banc);
  - `within` marks that the game's own script says (a missing first mark is a FAIL at run time);
  - every `#! switches` name exists in some branch's code;
  - most important: each expect/count/start regex can match a log line the code really writes. The format strings
    are every C++ string literal of lichii/hook and coop/ in each tree of ORM_WT (adjacent literals joined,
    fmt `{...}` / printf `%d` placeholders and `+`/`<<` joins turned into wildcards); the regex is run as an NFA over
    each format (a wildcard: any text), and it counts as found when it matches with at least min(10, its own
    shortest width) characters of the format's literal text (less: only a generic format such as "coop: {}"); and
    every piece of message text in the regex (cut at numbers, hex, Names_Like_This) is said by some literal or a row
    of them (a placeholder writes a value, not a phrase).
    A `never` that matches no format at all can never fail (a warning, not an error).

Report: <ORM_REPORTS>/lint_escenarios.md (per cause, per author topic: the commit that added the file).
Exit status 1 when there are errors.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

from re import _constants as C  # noqa: N812 (the regex compiler's own opcodes)
from re import _parser as P  # noqa: N812

HERE = Path(__file__).resolve().parent
ESC = HERE / "escenarios"
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))
import orm_paths  # noqa: E402
REPORT = orm_paths.REPORTS / "lint_escenarios.md"
WT = orm_paths.WT
# the worktrees read for log formats and hook commands, and what every scenario may count on: orm/t9-integrado, the
# single source since everything was integrated there (2026-11-11); --trees all reads every worktree of --wt, with
# orm/integracion and orm/test9 as the base (the lint before the integration)
TREES = ("t9-integrado",)
BASE = TREES
SRC_DIRS = ("lichii/hook", "coop")
SRC_EXT = {".cpp", ".h", ".hpp", ".cc", ".inc"}



def read_scenes(path: Path) -> dict:
    """escenas_banc.txt: one line a scene, `Scene<TAB>StartPos StartPos ...` -> {scene: set of StartPos names}."""
    out: dict = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#") and "	" in line:
            name, rest = line.split("	", 1)
            out[name.strip()] = set(rest.split())
    return out


# ---------------------------------------------------------------------------------------------------------------
# Directives: r03_pareja's parser when it has it, else the same code copied here.

DIRECTIVE = re.compile(r"^#!\s*(expect|never|count)\b(.*)$", re.M)
WITHIN = re.compile(r"\s+within\s+(\S*)\.\.(\S*)\s*$")
COUNT_OP = re.compile(r"\s+(>=|<=|==)\s*(\d+)\s*$")
HEADER = re.compile(r"^#!\s*([\w-]+)(.*)$", re.M)
# saves and the run options (players ... min-fps, scenario_options, docs/BOTS.md): r03_pareja
HEADERS = {"noload", "start", "switches", "timeout", "expect", "never", "count", "saves", "players", "bots", "bot-arg",
           "transport", "env", "puppets", "min-fps", "join", "caliente"}  # caliente: caliente.py (docs/ARNES.md)


def _parse_directives_copy(texts: list[str], games: str = "AB") -> list[dict]:
    """r03_pareja.parse_directives verbatim (it has no letterless form: games is not read)."""
    out = []
    for t in texts:
        for m in DIRECTIVE.finditer(t):
            kind, rest = m.group(1), m.group(2).strip()
            d = {"kind": kind, "raw": m.group(0).strip(), "game": "", "regex": "", "within": None, "op": None,
                 "n": None, "error": ""}
            out.append(d)
            g = re.match(r"([AB])\s+(.+)$", rest)
            if not g:
                d["error"] = "expected 'A' or 'B' and a regex"
                continue
            d["game"], body = g.group(1), g.group(2)
            w = WITHIN.search(body)
            if w:
                d["within"], body = (w.group(1), w.group(2)), body[:w.start()]
            if kind == "count":
                c = COUNT_OP.search(body)
                if not c:
                    d["error"] = "count needs '>= N', '<= N' or '== N' after the regex"
                    continue
                d["op"], d["n"], body = c.group(1), int(c.group(2)), body[:c.start()]
            d["regex"] = body.strip()
            try:
                re.compile(d["regex"])
            except re.error as e:
                d["error"] = f"bad regex: {e}"
            if not d["regex"]:
                d["error"] = "empty regex"
    return out


def _harness_parser():
    """r03_pareja.parse_directives taken from its source (importing r03_pareja pulls orm_inst, which needs the game
    package): the function and the module constants it reads. None when this copy of r03_pareja has none."""
    import ast  # noqa: PLC0415
    src = (HERE / "r03_pareja.py").read_text(encoding="utf-8")
    tree = ast.parse(src)
    fn = next((n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "parse_directives"), None)
    if fn is None:
        return None
    consts = [n for n in tree.body if isinstance(n, ast.Assign) and all(isinstance(t, ast.Name) and t.id.isupper()
                                                                          for t in n.targets)
              and isinstance(n.value, ast.Call) and ast.unparse(n.value.func) == "re.compile"]
    ns: dict = {"re": re}
    exec(compile(ast.Module(body=consts + [fn], type_ignores=[]), "r03_pareja.py", "exec"), ns)  # noqa: S102
    return ns["parse_directives"]


_PARSER = None


def parse_directives(texts: list[str], games: str = "AB") -> list[dict]:
    global _PARSER  # noqa: PLW0603
    if _PARSER is None:
        try:
            _PARSER = _harness_parser() or _parse_directives_copy
        except (OSError, SyntaxError, KeyError):
            _PARSER = _parse_directives_copy
    return _PARSER(texts, games)


# ---------------------------------------------------------------------------------------------------------------
# The hook's test script parser (lichii/hook/coop_testinput.cpp Parse()), argument shapes.

KEYS = set("A B X Y L R ZL ZR PLUS MINUS LCLICK RCLICK DUP DDOWN DLEFT DRIGHT".split()) | {
    f"{s}_{d}" for s in ("LSTICK", "RSTICK") for d in ("UP", "DOWN", "LEFT", "RIGHT")}
WEAR = {"weapon", "shield", "head", "armor", "boots", "hand"}
ASSIGN = set("""Empty DekuStick DekuNut Bomb Boomerang SlingShot Hookshot Longshot Hammer GlassTruth Bombchu DinFire
FaroreWind NayruLove MagicBean BottleA BottleB BottleC BottleD FishingRod Lure KeatonMask SkullMask SpookyMask BunnyMask
TruthMask GoronMask ZoraMask GerudoMask HeavyBoot HoverBoot Bow WeirdEgg AlarmCucco ZeldaLetter PocketEgg PocketCucco
CojiroCucco OddMushroom OddPowder PoacherSaw BrokenSword Prescription EyeballFrog EyeDrop ClaimCheck SheikahWarp
FireArrow IceArrow LightArrow ScrubBusinessSeed BottleE Amiibo SinkLure NormalLure""".split())
BOTTLE = {b.upper() for b in "Empty RedPotion GreenPotion BluePotion BlueFire Fish Letter BigPoe Poe Bug Fairy MaxMilk "
          "HalfMilk restore".split()}
STEPS_CMDS = {"start", "load", "play", "close", "kill", "server", "sleep", "expect", "shot", "try"}  # run.ps1 Run-Steps


def _num(w: str, lim: float = 1e6) -> bool:
    try:
        v = float(w)
    except ValueError:
        return False
    return abs(v) <= lim and v == v


def _ticks(w: str) -> bool:
    return w.isdigit() and len(w) <= 9


def _keys(w: str) -> bool:
    return all(k.upper() in KEYS for k in w.split("+"))


def _printable(w: str) -> bool:
    return 0 < len(w) <= 160 and all(0x20 < ord(c) < 0x7f for c in w)


def check_line(w: list[str]) -> str | None:
    """None when Parse() takes the words of one script line, else its complaint (or 'unchecked' for a command this
    mirror does not know: the command table from the source decides whether it exists)."""
    cmd, n = w[0].upper(), len(w)
    if cmd == "WAIT":
        return None if n == 2 and _ticks(w[1]) else "want: wait TICKS"
    if cmd == "HOLD":
        if not 3 <= n <= 4 or not _ticks(w[2]):
            return "want: hold KEYS TICKS [MARK]"
        return None if _keys(w[1]) else f"unknown key {w[1]}"
    if cmd == "PRESS":
        if not 2 <= n <= 3 or (n == 3 and not _ticks(w[2])):
            return "want: press KEYS [TICKS]"
        return None if _keys(w[1]) else f"unknown key {w[1]}"
    if cmd in ("STICK", "RSTICK"):
        if not 4 <= n <= 5 or not (_num(w[1], 1) and _num(w[2], 1) and _ticks(w[3])):
            return f"want: {cmd.lower()} X Y TICKS [KEYS], X and Y from -1 to 1"
        return None if n == 4 or _keys(w[4]) else f"unknown key {w[4]}"
    if cmd == "GOTO":
        ok = 3 <= n <= 5 and _num(w[1]) and _num(w[2]) and (n < 4 or (_num(w[3]) and float(w[3]) >= 0.2))
        return None if ok and (n < 5 or _ticks(w[4])) else "want: goto X Z [RADIUS] [TICKS], radius at least 0.2"
    if cmd == "TOWARD":
        if not 4 <= n <= 5 or not (_num(w[1]) and _num(w[2]) and _ticks(w[3])):
            return "want: toward X Z TICKS [KEYS]"
        return None if n == 4 or _keys(w[4]) else f"unknown key {w[4]}"
    if cmd == "AWAIT":
        ok = 2 <= n <= 3 and w[1].lower() == "mate" and (n == 2 or _ticks(w[2]))
        return None if ok else "want: await mate [TICKS]"
    if cmd == "TALKTHROUGH":  # A pressed while our event runs, until it is over (coop_testinput, ORM)
        return None if n == 1 or (n == 2 and _ticks(w[1])) else "want: talkthrough [TICKS]"
    if cmd == "WARP":
        k = n - (1 if n >= 3 and w[-1].lower() in ("adult", "child") else 0)
        return None if 2 <= k <= 3 else "want: warp SCENE [START] [adult|child]"
    if cmd == "PLACE":
        return None if n == 4 and all(_num(x) for x in w[1:]) else "want: place X Y Z"
    if cmd == "CHAT":
        return None if n >= 2 else "want: chat TEXT"
    if cmd == "GIVE":
        return None if n == 2 else "want: give ITEM"
    if cmd == "WEAR":
        return None if n == 3 and w[1].lower() in WEAR else "want: wear weapon|shield|head|armor|boots|hand NAME"
    if cmd == "ASSIGN":
        return None if n == 2 and w[1] in ASSIGN else "want: assign ITEM (the item button enum's names)"
    if cmd == "LIFE":
        return None if n == 2 and w[1].isdigit() and len(w[1]) <= 3 else "want: life N, N quarter hearts from 0 to 999"
    if cmd == "GATE":
        return None if n == 2 and w[1] in ("0", "1") else "want: gate 0|1 (0 open, 1 closed)"
    if cmd == "BOTTLE":
        ok = n == 3 and len(w[1]) == 1 and "A" <= w[1].upper() <= "E" and w[2].upper() in BOTTLE
        return None if ok else "want: bottle A-E VALUE|restore"
    if cmd == "EVENT":
        return None if n == 2 and re.fullmatch(r"[A-Za-z0-9_]{1,127}", w[1]) else "want: event NAME"
    if cmd == "FLAG":
        return None if n == 3 and _printable(w[1]) and w[2] in ("0", "1") else "want: flag NAME|0xHASH 0|1"
    if cmd == "ENUM":
        return None if n in (2, 3) and all(_printable(x) for x in w[1:]) else "want: enum NAME|0xHASH [VALUE|0xHASH]"
    if cmd == "AGE":
        return None if n == 2 and w[1].upper() in ("ADULT", "CHILD") else "want: age adult|child"
    if cmd == "MARK":
        return None if n == 2 else "want: mark NAME (one word)"
    if cmd == "KEY":  # orm/meca-adulto
        ok = n == 3 and re.fullmatch(r"0[xX][0-9a-fA-F]{1,16}", w[1]) and w[2] in ("0", "1")
        return None if ok else "want: key 0xKEY 0|1 (1 to 16 hex digits)"
    if cmd == "INT":  # orm/objetos-uso
        ok = n == 3 and _printable(w[1]) and re.fullmatch(r"-?\d{1,7}", w[2]) and abs(int(w[2])) <= 1000000
        return None if ok else "want: int NAME|0xHASH VALUE (decimal, -1000000 to 1000000)"
    return "unchecked"


# ---------------------------------------------------------------------------------------------------------------
# Format strings from C++ sources.

WILD = None  # a wildcard segment in a format
SEP = chr(0)  # between literals in a tree's text
MAX_FORMAT = 600  # longer literals are data (an embedded list), not a log line
ENGLISH = re.compile(r"(?<![A-Za-z0-9_])[a-z]{3,}(?![A-Za-z0-9_])")  # a word of the message, not a value's name
PHRASE = re.compile(r"(?<![A-Za-z0-9_])[a-z]{2,}(?: [a-z]{2,})*(?![A-Za-z0-9_])")  # words of the message in a row
FMT_PH = re.compile(r"\{\{|\}\}|\{[0-9]*(?::[^{}]*)?\}")
PRINTF_PH = re.compile(r"%%|%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|j|z|t|L|I64|I32)?[diouxXeEfFgGaAcsp]")
_ESC = {"n": "\n", "t": "\t", "r": "\r", "0": SEP, "\\": "\\", '"': '"', "'": "'", "?": "?", "a": "\a", "b": "\b",
        "f": "\f", "v": "\v"}


def _unescape(s: str) -> str:
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            d = s[i + 1]
            if d == "x":
                m = re.match(r"[0-9a-fA-F]+", s[i + 2:])
                out.append(chr(int(m.group(0), 16) & 0xFF) if m else "x")
                i += 2 + (len(m.group(0)) if m else 0)
                continue
            out.append(_ESC.get(d, d))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


TOKEN = re.compile(r"""//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"([^(\s]*)\((.*?)\)\1"|(?:u8|u|U|L)?"((?:[^"\\\n]|\\.)*)"|'(?:[^'\\\n]|\\.)*'|#[^\n]*|\s+|<<|\+=|\+|[^\s"'/#]+|/""", re.S)


def literals(text: str) -> list[tuple[str, bool, bool]]:
    """(string, joined_before, joined_after) for each run of adjacent C++ string literals in `text`; joined_* when a
    `+`, `+=` or `<<` sits next to it (the message goes on with something the literal does not say)."""
    out: list[list] = []
    prev_sig, cur = "", None
    for m in TOKEN.finditer(text):
        tok = m.group(0)
        if tok.startswith("//") or tok.startswith("/*") or tok.isspace() or (tok.startswith("#") and cur is None):
            continue
        if m.group(3) is not None or m.group(2) is not None:  # a string literal
            s = m.group(2) if m.group(2) is not None else _unescape(m.group(3))
            if cur is None:
                cur = [s, prev_sig.endswith(("+", "+=", "<<")), False]
            else:
                cur[0] += s
            continue
        if cur is not None:
            cur[2] = tok.startswith(("+", "<<")) and not tok.startswith("++")
            out.append(cur)
            cur = None
        prev_sig = tok
    if cur is not None:
        out.append(cur)
    return [tuple(x) for x in out]


def to_segments(s: str, before: bool, after: bool) -> tuple:
    """A literal as a tuple of characters and WILD (placeholders and joins)."""
    segs: list = [WILD] if before else []
    ph = FMT_PH if FMT_PH.search(s.replace("{{", "").replace("}}", "")) else (PRINTF_PH if PRINTF_PH.search(s) else None)
    pos = 0
    if ph is not None:
        for m in ph.finditer(s):
            segs.extend(s[pos:m.start()])
            t = m.group(0)
            if t in ("{{", "}}", "%%"):
                segs.append(t[0])
            elif not segs or segs[-1] is not WILD:
                segs.append(WILD)
            pos = m.end()
    segs.extend(s[pos:])
    if after and (not segs or segs[-1] is not WILD):
        segs.append(WILD)
    return tuple(segs)


def seg_text(segs: tuple) -> str:
    return "".join("{}" if x is WILD else x for x in segs)


def branch_of(tree: Path) -> str:
    try:
        r = subprocess.run(["git", "-C", str(tree), "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True,
                           text=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return r.stdout.strip() or tree.name
    except (OSError, subprocess.SubprocessError):
        return tree.name


class Corpus:
    """Formats (segments -> set of tree names), command sets, identifiers and the literal text of each worktree."""

    def __init__(self, wt: Path, trees: list[str] | None = None):
        self.formats: dict[tuple, set[str]] = defaultdict(set)
        self.commands: dict[str, set[str]] = {}
        self.words: dict[str, set[str]] = defaultdict(set)  # switch names and other identifiers in literals
        self.text: dict[str, str] = {}  # tree -> its literals, NUL between them and at placeholders
        self.branches: dict[str, str] = {}
        cache: dict[str, tuple] = {}
        names = trees or sorted(p.name for p in wt.iterdir() if (p / "lichii/hook").is_dir())
        for name in names:
            tree = wt / name
            if not (tree / "lichii/hook").is_dir():
                continue
            self.branches[name] = branch_of(tree)
            keys = []
            for d in SRC_DIRS:
                for f in sorted((tree / d).rglob("*")):
                    if f.suffix not in SRC_EXT or not f.is_file():
                        continue
                    raw = f.read_bytes()
                    key = hashlib.sha1(raw).hexdigest()
                    if key not in cache:
                        text = raw.decode("utf-8", "replace")
                        segs = [to_segments(*l) for l in literals(text)]
                        joined = SEP.join("".join(SEP if x is WILD else x for x in sg) for sg in segs)
                        cmds = set(re.findall(r'cmd == "([A-Z]+)"', text)) if f.name == "coop_testinput.cpp" else None
                        cache[key] = (segs, set(re.findall(r"[A-Z][A-Z0-9_]{3,}", joined)), joined, cmds)
                    keys.append(key)
            parts = []
            for key in dict.fromkeys(keys):
                segs, words, joined, cmds = cache[key]
                for sg in segs:
                    self.formats[sg].add(name)
                for wd in words:
                    self.words[wd].add(name)
                parts.append(joined)
                if cmds is not None:
                    self.commands[name] = cmds
            self.text[name] = SEP.join(parts)
        # the literal pieces (text between placeholders, joins and literal ends): a line built in steps
        # ("coop: test {}" with "mark " + name, "horse {}" with "coop: puppet: " + ...) is a run of them
        self.pieces: dict[str, set[str]] = defaultdict(set)
        self.ptext: dict[str, str] = {}
        for name, t in self.text.items():
            self.ptext[name] = SEP + t + SEP
            for p in t.split(SEP):
                if p:
                    self.pieces[p].add(name)
        self.all_pieces = SEP + SEP.join(self.pieces) + SEP
        # the formats a log line can come from: three letters or more; one that does not start with "coop:" may have
        # been given a prefix by a helper ("coop: {}"), and any may be followed by more (a line built in steps)
        self.flist = []
        for segs, trees_ in self.formats.items():
            lit = [x for x in segs if x is not WILD]
            if sum(1 for x in lit if x.isalpha()) < 3 or len(segs) > MAX_FORMAT:
                continue
            body = list(segs)
            virtual = body[0] is not WILD and not seg_text(segs).startswith("coop:")
            if virtual:
                body.insert(0, WILD)
            if body[-1] is not WILD:
                body.append(WILD)
            self.flist.append((tuple(body), frozenset(trees_), len(lit), seg_text(segs), virtual))
        self.grams: dict[str, set[int]] = defaultdict(set)
        self.ngrams: list[int] = []
        for i, (_, _, _, t, _) in enumerate(self.flist):
            t = t.lower()
            g = {t[j:j + 4] for j in range(len(t) - 3)}
            for x in g:
                self.grams[x].add(i)
            self.ngrams.append(max(1, len(g)))
        # whole short literals ("start", "sent "): a value one placeholder takes, beside another one's
        self.values: dict[str, set[str]] = defaultdict(set)
        for segs, trees_ in self.formats.items():
            if WILD not in segs and len(segs) <= 40:
                for t in trees_:
                    self.values[t].add("".join(segs).strip())

    def wrapper(self, prefix: str) -> bool:
        """Some literal is `prefix` and then a placeholder or a join ("horse {}"): a log line may start with it."""
        return (SEP + prefix + SEP) in self.all_pieces


# ---------------------------------------------------------------------------------------------------------------
# The regex as an NFA, run over a format with wildcards.

def _cat(code, c: str) -> bool:
    if code is C.CATEGORY_DIGIT:
        return c.isdigit()
    if code is C.CATEGORY_NOT_DIGIT:
        return not c.isdigit()
    if code is C.CATEGORY_SPACE:
        return c in " \t\n\r\f\v"
    if code is C.CATEGORY_NOT_SPACE:
        return c not in " \t\n\r\f\v"
    if code is C.CATEGORY_WORD:
        return c.isalnum() or c == "_"
    if code is C.CATEGORY_NOT_WORD:
        return not (c.isalnum() or c == "_")
    return True


def _in(items, c: str) -> bool:
    neg, hit = False, False
    for op, av in items:
        if op is C.NEGATE:
            neg = True
        elif op is C.LITERAL:
            hit = hit or ord(c) == av
        elif op is C.RANGE:
            hit = hit or av[0] <= ord(c) <= av[1]
        elif op is C.CATEGORY:
            hit = hit or _cat(av, c)
        elif op is C.RANGE_UNI_IGNORE or op is C.LITERAL_UNI_IGNORE:
            hit = True
    return hit != neg


class NFA:
    REPEAT_CAP = 24

    def __init__(self, pattern: str):
        self.eps: list[list[int]] = []
        self.tr: list[list[tuple]] = []
        p = P.parse(pattern)
        self.icase = bool(p.state.flags & re.IGNORECASE)
        self.start = self._new()
        self.accept = self._build(list(p), self.start)
        self.minwidth = _litwidth(list(p))  # the fewest literal characters a match needs

    def _new(self) -> int:
        self.eps.append([])
        self.tr.append([])
        return len(self.eps) - 1

    def _char(self, s: int, pred, weight: int = 0) -> int:
        """weight 1: a literal character of the regex; a class or a dot takes a format's literal text without
        saying it (`[^']*` through a long unrelated message is no evidence)."""
        t = self._new()
        self.tr[s].append((pred, t, weight))
        return t

    def _any_string(self, s: int) -> int:
        self.tr[s].append((lambda c: True, s, 0))
        return s

    def _build(self, items, s: int) -> int:
        for op, av in items:
            s = self._item(op, av, s)
        return s

    def _item(self, op, av, s: int) -> int:
        if op is C.LITERAL:
            return self._char(s, lambda c, v=av: ord(c) == v, 1)
        if op is C.NOT_LITERAL:
            return self._char(s, lambda c, v=av: ord(c) != v)
        if op is C.ANY:
            return self._char(s, lambda c: c != "\n")
        if op is C.IN:
            return self._char(s, lambda c, it=av: _in(it, c))
        if op is C.SUBPATTERN:
            return self._build(list(av[3]), s)
        if op is C.ATOMIC_GROUP:
            return self._build(list(av), s)
        if op is C.BRANCH:
            end = self._new()
            for alt in av[1]:
                e = self._build(list(alt), s)
                self.eps[e].append(end)
            return end
        if op in (C.MAX_REPEAT, C.MIN_REPEAT, C.POSSESSIVE_REPEAT):
            lo, hi, sub = av
            lo = min(lo, self.REPEAT_CAP)
            for _ in range(lo):
                s = self._build(list(sub), s)
            if hi is C.MAXREPEAT or hi - lo > self.REPEAT_CAP:
                loop = self._new()
                self.eps[s].append(loop)
                e = self._build(list(sub), loop)
                self.eps[e].append(loop)
                return loop
            end = self._new()
            self.eps[s].append(end)
            for _ in range(hi - lo):
                s = self._build(list(sub), s)
                self.eps[s].append(end)
            return end
        if op is C.GROUPREF_EXISTS:
            end = self._new()
            self.eps[self._build(list(av[1]), s)].append(end)
            self.eps[self._build(list(av[2]) if av[2] else [], s)].append(end)
            return end
        if op is C.GROUPREF:
            return self._any_string(s)
        # AT (anchors), ASSERT, ASSERT_NOT and the rest: no character (an over-approximation)
        return s

    def _closures(self) -> None:
        """Per state: the states its epsilon moves reach, and the states any text reaches (both with itself)."""
        def reach(s: int, wild: bool) -> tuple:
            seen, stack = {s}, [s]
            while stack:
                x = stack.pop()
                for t in self.eps[x] + ([t for _, t, _ in self.tr[x]] if wild else []):
                    if t not in seen:
                        seen.add(t)
                        stack.append(t)
            return tuple(seen)
        self.ceps = [reach(s, False) for s in range(len(self.eps))]
        self.cany = [reach(s, True) for s in range(len(self.eps))]

    @staticmethod
    def _close(cur: dict[int, int], table: list) -> dict[int, int]:
        out: dict[int, int] = {}
        for st, cnt in sorted(cur.items(), key=lambda x: -x[1]):
            for t in table[st]:
                if t not in out:
                    out[t] = cnt
        return out

    def best(self, segs: tuple) -> int:
        """The most literal characters of the format a search of the regex can match through; -1 when no line
        the format can write matches."""
        if not hasattr(self, "ceps"):
            self._closures()
            self.idle = self._close({self.start: 0}, self.ceps)
            self.memo: dict[str, dict[int, int]] = {}
        best = 0 if self.accept in self.idle else -1
        cur = self.idle
        for seg in segs:
            if seg is WILD:
                cur = self._close(cur, self.cany)
            elif cur is self.idle and seg in self.memo:
                cur = self.memo[seg]
            else:
                c = seg.lower() if self.icase else seg
                nxt: dict[int, int] = {}
                for st, cnt in cur.items():
                    for pred, t, w in self.tr[st]:
                        if pred(c) or (self.icase and pred(c.upper())):
                            if nxt.get(t, -1) < cnt + w:
                                nxt[t] = cnt + w
                if nxt:
                    if nxt.get(self.start, -1) < 0:
                        nxt[self.start] = 0
                    res = self._close(nxt, self.ceps)
                else:
                    res = self.idle
                if cur is self.idle:
                    self.memo[seg] = res
                cur = res
            if seg is WILD and cur.get(self.start, -1) < 0:
                cur[self.start] = 0
                cur = self._close(cur, self.ceps)
            if self.accept in cur:
                best = max(best, cur[self.accept])
        return best


def _litwidth(items) -> int:
    """The fewest LITERAL characters a match of these parsed items takes."""
    n = 0
    for op, av in items:
        if op is C.LITERAL:
            n += 1
        elif op is C.SUBPATTERN:
            n += _litwidth(list(av[3]))
        elif op is C.ATOMIC_GROUP:
            n += _litwidth(list(av))
        elif op is C.BRANCH:
            n += min(_litwidth(list(alt)) for alt in av[1])
        elif op in (C.MAX_REPEAT, C.MIN_REPEAT, C.POSSESSIVE_REPEAT):
            n += av[0] * _litwidth(list(av[2])) if av[0] else 0
    return n


# a value a placeholder writes, inside a regex's literal text: 0x hex, a hash, a number, a Name_Like_This or
# CamelCase name, a quoted name
DATA_TOKEN = re.compile(r"0x[0-9a-fA-F]+|[0-9a-f]{8,}|\d+(?:\.\d+)?|[A-Za-z0-9]*(?:[A-Z][a-z0-9]*[A-Z_]|_)[A-Za-z0-9_]*"
                        r"|'[^']*'")


def _all_runs(items, out: list[str]) -> list[str]:
    """The runs of LITERAL items at every level of the parsed regex (each alternative of a branch on its own)."""
    cur: list[str] = []
    for op, av in items:
        if op is C.LITERAL:
            cur.append(chr(av))
            continue
        if cur:
            out.append("".join(cur))
            cur = []
        if op is C.SUBPATTERN:
            _all_runs(list(av[3]), out)
        elif op is C.BRANCH:
            for alt in av[1]:
                _all_runs(list(alt), out)
        elif op in (C.MAX_REPEAT, C.MIN_REPEAT, C.POSSESSIVE_REPEAT):
            _all_runs(list(av[2]), out)
    if cur:
        out.append("".join(cur))
    return out


def literal_runs(pattern: str) -> list[str]:
    """The literal text the regex needs at its top level (runs of LITERAL items)."""
    runs, cur = [], []
    try:
        items = list(P.parse(pattern))
    except re.error:
        return []
    for op, av in items:
        if op is C.LITERAL:
            cur.append(chr(av))
        else:
            if cur:
                runs.append("".join(cur))
            cur = []
    if cur:
        runs.append("".join(cur))
    return runs


class Matcher:
    TOP = 120

    def __init__(self, corpus: Corpus):
        self.c = corpus
        self.n = len(corpus.flist)
        self.cache: dict[str, dict] = {}

    def candidates(self, pattern: str) -> list[int]:
        """The formats sharing the most 4-grams of the regex's literal text with it (the top TOP), all of them when
        the regex has no literal text of four characters."""
        grams = {r.lower()[j:j + 4] for r in literal_runs(pattern) for j in range(len(r) - 3)}
        hits: dict[int, int] = defaultdict(int)
        for g in grams:
            for i in self.c.grams.get(g, ()):
                hits[i] += 1
        if not hits:
            return list(range(self.n))
        # the most grams in common, and the most for their size (a short generic "coop: story: {}" shares few)
        top = sorted(hits, key=lambda i: -hits[i])[:self.TOP]
        dense = sorted(hits, key=lambda i: -hits[i] / self.c.ngrams[i])[:self.TOP]
        return list(dict.fromkeys(top + dense))

    def residue(self, pattern: str, fmt: str) -> list[str]:
        """The phrases of the regex's literal text (lowercase words, one space between) that the format's own
        literal pieces do not say: a placeholder took them, so some other literal of the code must say them."""
        pieces = sorted((x for x in fmt.split("{}") if len(x) >= 2), key=len, reverse=True)
        out = []
        for run in literal_runs(pattern):
            for piece in pieces:
                run = run.replace(piece, SEP)
            for frag in run.split(SEP):
                out += [ph for ph in PHRASE.findall(frag) if re.search(r"[a-z]{3}", ph)]
        return out

    def _said(self, phrase: str, tree: str) -> bool:
        """The phrase is in a literal of the tree, or each of its words is a whole literal there (placeholders side
        by side: "STORY_EVENT {} {}(" with "start" and "sent ")."""
        return phrase in self.c.text[tree] or all(w in self.c.values[tree] for w in phrase.split())

    def match(self, pattern: str) -> dict:
        """{'strong': {tree: (score, format)}, 'best': (score, format, missing phrases) or None}: the trees whose
        code writes a line the regex finds. A format counts when the regex goes through min(10, its shortest width)
        of the format's literal characters or more (or through all of them, for a short format that says more than
        'coop'), and the regex's phrases its placeholders took are in some literal of that tree."""
        if pattern in self.cache:
            return self.cache[pattern]
        nfa = NFA(pattern)
        need = max(1, min(10, nfa.minwidth))
        strong: dict[str, tuple] = {}
        best = None
        for i in self.candidates(pattern):
            segs, trees, nlit, text, virtual = self.c.flist[i]
            score = nfa.best(segs)
            if score < 0:
                continue
            short_full = not virtual and score >= nlit >= 5 and set(ENGLISH.findall(text.lower())) - {"coop"}
            if score < need and not short_full:
                if best is None or score > best[0]:
                    best = (score, text, [])
                continue
            res = self.residue(pattern, text)
            ok = [t for t in trees if all(self._said(ph, t) for ph in res)]
            for t in ok:
                if t not in strong or strong[t][0] < score:
                    strong[t] = (score, text)
            if not ok:
                miss = [ph for ph in res if not any(self._said(ph, t) for t in trees)]
                if best is None or score > best[0] or (score == best[0] and len(miss) < len(best[2])):
                    best = (score, text, miss)
        out = {"strong": strong, "best": best}
        self.cache[pattern] = out
        return out

    MAX_PIECES = 8

    def _segment(self, run: str, words: frozenset[str]) -> list[tuple[str, str]] | None:
        """The fewest literal pieces of the code that say `run` one after another: the first one may be the end of a
        piece (one that a placeholder or a join follows), the last one the start of a piece, the ones between whole
        pieces; a word of the scenario's own script (a mark name, a flag) may stand for any of them (the code echoes
        it through a placeholder). None when there is no such run (or it needs more than MAX_PIECES)."""
        g, whole, n = self.c.all_pieces, self.c.pieces, len(run)
        if SEP not in run and run in g:  # inside one piece (a run has no SEP, so it cannot span two)
            return [(run, "sub")]
        suf = [j for j in range(1, n) if (run[:j] + SEP) in g or run[:j] in words]
        pre = {i for i in range(1, n) if (SEP + run[i:]) in g or run[i:] in words}
        best: dict[int, list] = {j: [(run[:j], "word" if run[:j] in words else "suf")] for j in suf}
        for i in range(1, n):
            if i not in best or len(best[i]) >= self.MAX_PIECES:
                continue
            path = best[i]
            if i in pre and (n not in best or len(best[n]) > len(path) + 1):
                best[n] = path + [(run[i:], "word" if run[i:] in words else "pre")]
            for j in range(i + 1, n):
                frag = run[i:j]
                if (frag in whole or frag in words) and (j not in best or len(best[j]) > len(path) + 1):
                    best[j] = path + [(frag, "word" if frag in words else "whole")]
        return best.get(n)

    def _seg_trees(self, seg: list[tuple[str, str]]) -> set[str]:
        out = set(self.c.text)
        for frag, kind in seg:
            if kind == "word":
                continue
            if kind == "whole":
                out &= self.c.pieces.get(frag, set())
            else:
                probe = {"sub": frag, "suf": frag + SEP, "pre": SEP + frag}[kind]
                out = {t for t in out if probe in self.c.ptext[t]}
            if not out:
                break
        return out

    def composed(self, pattern: str, words: frozenset[str]) -> tuple[set[str], list[str]]:
        """The trees whose literals say every literal run of the regex as a row of pieces (a line built in steps:
        "coop: test {}" + "mark " + the script's mark, the downed "{} -> {}: {}" + "revived (%s" + "by P%u"), and
        the pieces taken. The order of the pieces across functions is not checked: a weaker proof than match()."""
        key = (pattern, words)
        if key in self.cache:
            return self.cache[key]
        trees: set[str] | None = None
        used: list[str] = []
        for run in literal_runs(pattern):
            if not run.strip():
                continue
            seg = self._segment(run, words)
            if seg is None:
                trees = set()
                break
            t = self._seg_trees(seg)
            trees = t if trees is None else trees & t
            used += [f for f, _ in seg]
            if not trees:
                break
        out = (trees or set(), used)
        self.cache[key] = out
        return out

    def unsaid(self, pattern: str, words: frozenset[str]) -> list[str]:
        """The message text of the regex that no literal of the code says: its literal runs (each alternative of a
        group too), cut at the values a placeholder writes (numbers, hex, Names_Like_This, 'quoted'), and each piece
        with three lowercase letters in a row that is not one piece of the code, nor a row of them (_segment). A
        placeholder writes a value, not a phrase: the NFA lets a wildcard take any text (the regex "received [(][0-9]+ stale[)], "
        matched "received ({} stale, {} incomplete, {} bad), " through the {} after "stale"), this does not."""
        key = ("unsaid", pattern, words)
        if key in self.cache:
            return self.cache[key]
        out = []
        try:
            runs = _all_runs(list(P.parse(pattern)), [])
        except re.error:
            runs = []
        for run in runs:
            for sub in DATA_TOKEN.split(run):
                if not re.search(r"[a-z]{3}", sub) or sub in words:
                    continue
                if self._segment(sub, words) is None and (not sub.strip() or self._segment(sub.strip(), words) is None):
                    out.append(sub)
        self.cache[key] = out
        return out


# ---------------------------------------------------------------------------------------------------------------
# Scenarios.

def code_words(line: str) -> list[str]:
    return line.split("#", 1)[0].split()


def script_marks(text: str) -> list[str]:
    out = []
    for line in text.splitlines():
        w = code_words(line)
        if len(w) == 2 and w[0].upper() == "MARK":
            out.append(w[1])
        elif len(w) == 4 and w[0].upper() == "HOLD":
            out.append(w[3])
        elif len(w) == 2 and w[0].upper() == "AGE":
            out.append(w[1].lower())
    return out


def needs_branches(text: str) -> set[str]:
    """Branch names the scenario's comments say it needs ('Needs daurosk branch orm/meca-adulto')."""
    return set(re.findall(r"orm/([\w-]+)", "\n".join(l for l in text.splitlines() if l.startswith("#"))))


def tree_names(branches: set[str], corpus: Corpus) -> set[str]:
    out = set()
    for tree, br in corpus.branches.items():
        if br.removeprefix("orm/") in branches or tree in branches:
            out.add(tree)
    return out


def lint_file(f: Path, corpus: Corpus, matcher: Matcher, scenes: dict) -> list[dict]:
    """Findings of one scenario file: {'sev': error|warn|info, 'cause', 'detail', 'line'}."""
    text = f.read_text(encoding="utf-8")
    found: list[dict] = []

    def add(sev, cause, detail, line=0):
        found.append({"sev": sev, "cause": cause, "detail": detail, "line": line, "file": f.name})

    base = {t for t in BASE if t in corpus.branches}
    allowed = base | tree_names(needs_branches(text), corpus)
    steps = f.name.endswith(".steps.txt")
    lines = text.splitlines()
    pair = f.with_name(f.name.replace(".A.txt", ".B.txt") if ".A." in f.name else f.name.replace(".B.txt", ".A.txt"))
    pair_text = pair.read_text(encoding="utf-8") if not steps and pair.exists() else ""
    # what the scripts say (mark names, flags, events): the hook echoes them through a placeholder
    words = frozenset(w for l in (text + "\n" + pair_text).splitlines() for w in code_words(l))
    # header lines
    for i, l in enumerate(lines, 1):
        m = re.match(r"^#!\s*([\w-]+)(.*)$", l)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind not in HEADERS:
            add("error", "directiva desconocida", l.strip(), i)
        elif kind == "timeout" and not rest.isdigit():
            add("error", "timeout sin número", l.strip(), i)
        elif kind == "join" and rest not in ("A-first", "B-first"):
            add("error", "join: debe ser A-first o B-first", l.strip(), i)
        elif kind == "switches":
            # run.ps1: split at every comma, each piece NAME=value or A:NAME=value (that game only)
            for kv in filter(None, (x.strip() for x in rest.split(","))):
                name = re.sub(r"^[AB]:", "", kv.split("=", 1)[0])
                if "=" not in kv and re.search(r"[./\\]", kv):
                    add("error", "interruptor con comas en el valor (run.ps1 parte por comas: una carpeta, '.')", kv, i)
                elif not re.fullmatch(r"[A-Z][A-Z0-9_]+", name) or "=" not in kv:
                    add("error", "interruptor mal escrito", kv, i)
                elif name not in corpus.words:
                    add("error", "interruptor que no existe en ningún árbol", name, i)
                elif not corpus.words[name] & allowed:
                    add("info", "interruptor solo en una rama de tema", f"{name}: {_trees(corpus.words[name], corpus)}", i)
        elif kind == "start":
            _check_regex(rest, "start", i, matcher, corpus, allowed, add, positive=True, words=words)
    # expect / never / count, with r03_pareja's parser
    if not steps:
        own = script_marks(text)
        other = script_marks(pair_text)
        mine = "A" if f.name.endswith(".A.txt") else "B"
        for d in parse_directives([text], games=mine):
            ln = next((i for i, l in enumerate(lines, 1) if l.strip() == d["raw"]), 0)
            if d["error"]:
                cause = ("directiva sin letra de juego (A|B)" if d["error"].startswith("expected 'A'") else
                         "directiva con una letra de juego que no es A ni B" if d["error"].startswith("unknown game") else
                         "regex que no compila" if d["error"].startswith("bad regex") else "directiva mal formada")
                add("error", cause, f"{d['raw']} ({d['error']})", ln)
                continue
            if d["game"] in ("bots", "server"):
                continue  # the bots' or the server's log (docs/BOTS.md): not the hook's lines, nor this script's marks
            label = re.match(r"^([a-z]+) coop:", d["regex"])
            if d["kind"] == "count" and label and not corpus.wrapper(label.group(1) + " "):
                # a real prefix is fine: coop_epona.cpp logs the horse director's lines as "horse {}"
                add("error", "count con una etiqueta delante de la regex", d["raw"], ln)
            # within boot..: the whole log (r03_pareja: lines logged once as the game joins), no marks to check
            if d["within"] and d["within"] != ("boot", ""):
                marks = own if d["game"] == mine else other
                m1, m2 = d["within"]
                if m1 and m1 not in marks:
                    add("error", "within con una marca que el guion de ese juego no dice", f"{m1}: {d['raw']}", ln)
                if m2 and m2 not in marks and m2 not in ("end", "fin"):
                    add("warn", "within: la segunda marca no está en el guion (lee hasta el final)", f"{m2}: {d['raw']}", ln)
                if m1 and m2 and m1 in marks and m2 in marks and marks.index(m2) < marks.index(m1):
                    add("error", "within con las marcas al revés", d["raw"], ln)
            _check_regex(d["regex"], d["kind"], ln, matcher, corpus, allowed, add, positive=d["kind"] != "never",
                         words=words)
    # script lines
    marks = []
    for i, l in enumerate(lines, 1):
        w = code_words(l)
        if not w:
            continue
        if steps:
            if w[0].lower() not in STEPS_CMDS:
                add("error", "orden de steps desconocida (run.ps1)", l.strip(), i)
            elif w[0].lower() == "play":
                for name in w[2::2]:
                    if not (f.parent / name).exists():
                        add("error", "steps: play de un guion que no existe", name, i)
            continue
        cmd = w[0].upper()
        have = {t for t, cmds in corpus.commands.items() if cmd in cmds}
        if not have:
            add("error", "orden que el gancho no conoce", l.strip(), i)
            continue
        if not have & allowed:
            add("info", "orden solo en una rama de tema", f"{w[0]}: {_trees(have, corpus)}", i)
        why = check_line(w)
        if why and why != "unchecked":
            add("error", "argumentos que Parse() rechaza", f"{l.strip()} ({why})", i)
        if cmd == "WARP" and not why:
            k = len(w) - (1 if len(w) >= 3 and w[-1].lower() in ("adult", "child") else 0)
            scene = w[1]
            if scene not in scenes:
                add("error", "escena que no está en el Banc", l.strip(), i)
            elif k == 3 and w[2] not in scenes[scene]:
                add("error", "StartPos que la escena no tiene", l.strip(), i)
        if cmd == "MARK" and len(w) == 2:
            if w[1] in marks:
                add("warn", "marca repetida (within toma la primera)", w[1], i)
            marks.append(w[1])
    if not steps and not any(code_words(l) for l in lines):
        add("error", "guion sin órdenes (el gancho dice 'no steps')", f.name)
    return found


def _trees(trees: set[str], corpus: Corpus) -> str:
    return ", ".join(sorted({corpus.branches.get(t, t) for t in trees}))


def _check_regex(rx: str, kind: str, ln: int, matcher: Matcher, corpus: Corpus, allowed: set[str], add,
                 positive: bool, words: frozenset[str] = frozenset()) -> None:
    try:
        re.compile(rx)
    except re.error as e:
        add("error", "regex que no compila", f"{kind}: {rx} ({e})", ln)
        return
    m = matcher.match(rx)
    strong, best = m["strong"], m["best"]
    built: set[str] = set()
    if not strong:  # a line built in steps (a helper's prefix, a state name, the script's own mark)
        built, pieces = matcher.composed(rx, words)
        how = " + ".join(repr(p) for p in pieces[:8])
    unsaid = matcher.unsaid(rx, words) if strong or built else []
    if not positive:
        if not strong and not built:
            add("warn", "never que ninguna línea del código escribe (no puede fallar)", rx, ln)
        elif unsaid:
            add("warn", "never con texto que ningún literal dice (no puede fallar)",
                f"{rx}: {', '.join(repr(x) for x in unsaid[:4])}", ln)
        return
    if unsaid:
        add("error", "regex con texto que ningún literal del código dice (un marcador escribe un valor, no una frase)",
            f"{kind}: {rx}: {', '.join(repr(x) for x in unsaid[:4])}", ln)
        return
    if strong.keys() & allowed:
        return
    if strong:
        add("info", "línea que solo escribe una rama de tema", f"{kind}: {rx} -> {_trees(set(strong), corpus)}", ln)
        return
    if built:
        where = "" if built & allowed else f" -> solo {_trees(built, corpus)}"
        add("info", "línea armada en trozos (literales seguidos, sin ver el orden en el código)",
            f"{kind}: {rx}: {how}{where}", ln)
        return
    near = ""
    if best:
        score, fmt, miss = best
        near = f" (lo más cercano: '{fmt[:90]}', {score} letras" + (f"; sin literal: {', '.join(miss[:4])}" if miss else "") + ")"
    add("error", "regex sin ninguna línea de registro en el código", f"{kind}: {rx}{near}", ln)


# ---------------------------------------------------------------------------------------------------------------
# Report.

def topics(files: list[Path]) -> dict[str, str]:
    """file name -> the subject of the commit that added it ('(sin commit)' for one not committed yet)."""
    out = {}
    try:
        r = subprocess.run(["git", "-C", str(REPO), "log", "--diff-filter=A", "--name-only", "--format=@@%h %s", "--",
                            str(ESC.relative_to(REPO)).replace("\\", "/")], capture_output=True, text=True,
                           encoding="utf-8", timeout=120, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        subj = ""
        for line in r.stdout.splitlines():
            if line.startswith("@@"):
                subj = line[2:]
            elif line.strip():
                out.setdefault(Path(line.strip()).name, subj)
    except (OSError, subprocess.SubprocessError):
        pass
    return {f.name: out.get(f.name, "(sin commit)") for f in files}


def render(findings: list[dict], files: list[Path], topic: dict[str, str], corpus: Corpus) -> str:
    sev_n = {s: sum(1 for x in findings if x["sev"] == s) for s in ("error", "warn", "info")}
    bad = sorted({x["file"] for x in findings if x["sev"] == "error"})
    out = ["# Lint de escenarios (sin juego)", "",
           f"`python -I tools/orm/harness/lint_escenarios.py`: {len(files)} archivos, "
           f"{sev_n['error']} errores en {len(bad)} archivos, {sev_n['warn']} avisos, {sev_n['info']} notas.", "",
           "Árboles leídos (formatos de registro y órdenes del gancho): "
           + ", ".join(f"`{t}` ({b})" for t, b in sorted(corpus.branches.items())) + ".",
           f"Base: {', '.join(BASE)}; una línea o una orden que solo está en otra rama es una nota "
           "(el escenario necesita esa rama), salvo que el escenario diga `orm/<rama>` en sus comentarios.", "",
           "Errores: el escenario fallará por el texto del escenario, no por el juego. Avisos: revisar a mano.", "",
           "## Por causa", ""]
    by_cause: dict[tuple, list] = defaultdict(list)
    for x in findings:
        by_cause[(x["sev"], x["cause"])].append(x)
    order = {"error": 0, "warn": 1, "info": 2}
    for (sev, cause), xs in sorted(by_cause.items(), key=lambda kv: (order[kv[0][0]], -len(kv[1]))):
        out.append(f"### [{sev}] {cause} ({len(xs)})")
        out.append("")
        for x in xs[:400]:
            out.append(f"- `{x['file']}`:{x['line']} {x['detail'].replace('|', chr(92) + '|')[:300]}")
        if len(xs) > 400:
            out.append(f"- ... y {len(xs) - 400} más")
        out.append("")
    out += ["## Por tema (commit que añadió el archivo)", "", "| Tema | Archivos | Con errores | Errores | Avisos |",
            "|---|---|---|---|---|"]
    per: dict[str, list] = defaultdict(list)
    for f in files:
        per[topic.get(f.name, "(sin commit)")].append(f.name)
    for t, names in sorted(per.items(), key=lambda kv: -sum(1 for x in findings if x["file"] in kv[1] and x["sev"] == "error")):
        e = [x for x in findings if x["file"] in names and x["sev"] == "error"]
        w = [x for x in findings if x["file"] in names and x["sev"] == "warn"]
        out.append(f"| {t.replace('|', '/')[:110]} | {len(names)} | {len({x['file'] for x in e})} | {len(e)} | {len(w)} |")
    out.append("")
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--wt", default=str(WT), help="folder of the worktrees (default ORM_WT, else ORM_ROOT/wt)")
    ap.add_argument("--report", default=str(REPORT))
    ap.add_argument("--only", default="", help="only scenario files whose name starts with this")
    ap.add_argument("--trees", default=",".join(TREES),
                    help="worktrees of --wt to read, comma separated (default t9-integrado, the base too), or 'all'")
    a = ap.parse_args(argv)
    global BASE  # noqa: PLW0603
    trees = None if a.trees == "all" else [t for t in a.trees.split(",") if t]
    BASE = ("integracion", "daurosk-test9") if trees is None else tuple(trees)
    files = sorted(f for f in ESC.glob(f"{a.only}*.txt") if re.search(r"\.(A|B|steps)\.txt$", f.name))
    corpus = Corpus(Path(a.wt), trees)
    matcher = Matcher(corpus)
    scenes = read_scenes(HERE / "escenas_banc.txt")
    findings = []
    for f in files:
        findings += lint_file(f, corpus, matcher, scenes)
    Path(a.report).parent.mkdir(parents=True, exist_ok=True)
    Path(a.report).write_text(render(findings, files, topics(files), corpus), encoding="utf-8")
    n = {s: sum(1 for x in findings if x["sev"] == s) for s in ("error", "warn", "info")}
    print(f"{len(files)} files, {n['error']} errors in {len({x['file'] for x in findings if x['sev'] == 'error'})} "
          f"files, {n['warn']} warnings, {n['info']} notes -> {a.report}")
    return 1 if n["error"] else 0


if __name__ == "__main__":
    sys.exit(main())
