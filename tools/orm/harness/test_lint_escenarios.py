"""Unit tests of lint_escenarios.py (no game, no worktrees).

    python -m pytest tools/orm/harness/test_lint_escenarios.py -q
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lint_escenarios as L  # noqa: E402


def test_literals_join_and_placeholders():
    src = 'LOG_INFO(Core, "coop: boss: "\n  "respawn: {}", x); // "not this"\nauto s = "hearts: " + n;\n#include "a.h"\n'
    lits = L.literals(src)
    assert lits[0] == ("coop: boss: respawn: {}", False, False)
    assert lits[1] == ("hearts: ", False, True)
    assert len(lits) == 2
    segs = L.to_segments("coop: x {:.2f} y %d", False, False)
    assert L.seg_text(segs) == "coop: x {} y %d"  # fmt wins: the printf part stays literal
    assert L.seg_text(L.to_segments("revive requested by P%u", False, False)) == "revive requested by P{}"


def test_nfa_scores_literal_characters():
    fmt = L.to_segments("coop: welcomed as player {} on {}:{}", False, False)
    assert L.NFA(r"coop: welcomed as player \d").best(fmt) == len("coop: welcomed as player ")
    assert L.NFA(r"coop: welcomed by").best(fmt) < 10  # only inside the placeholders: not found
    assert L.NFA(r"coop: welcomed by").best(L.to_segments("coop: welcomed as player", False, False)) == -1
    # a placeholder takes any text: the regex can only score what it reads on the literal
    gen = L.to_segments("coop: {}", False, False)
    assert L.NFA(r"coop: hearts: max").best(gen) == len("coop: ")


def test_check_line_mirrors_parse():
    assert L.check_line("press A+LSTICK_UP 10".split()) is None
    assert L.check_line("press Q".split()).startswith("unknown key")
    assert L.check_line("goto 1 2 0.1".split()).startswith("want: goto")
    assert L.check_line("warp WaterTemple WaterTemple_FromLakeHylia adult".split()) is None
    assert L.check_line("assign Hammer".split()) is None
    assert L.check_line("assign Hamer".split()).startswith("want: assign")
    assert L.check_line("key 0x275da83c3d984241 1".split()) is None
    assert L.check_line("fly 1".split()) == "unchecked"


def test_directives_and_marks():
    t = ("#! expect A coop: x within m1..end\n#! count B coop: y >= 2\n#! expect coop: z\n#! expect C coop: w\n"
         "#! expect bots bot1: mark m1\nmark m1\nhold A 30 m2\n")
    d = L.parse_directives([t], games="B")
    assert d[0]["game"] == "A" and d[0]["within"] == ("m1", "end")
    assert d[1]["op"] == ">=" and d[1]["n"] == 2
    assert d[2]["game"] == "B" and not d[2]["error"]  # no letter: the file's own game (r03_pareja, unified)
    assert d[3]["error"].startswith("unknown game 'C'")
    assert d[4]["game"] == "bots" and not d[4]["error"]
    assert L.script_marks(t) == ["m1", "m2"]
    assert L.needs_branches("# Needs daurosk branch orm/meca-adulto\nmark a\n") == {"meca-adulto"}


SRC = '''
// coop_testinput.cpp (a mirror): the commands and the lines built in steps
if (cmd == "MARK") {}
LOG_INFO(Core, "coop: test {}", line);
log.push_back("mark " + s.mark);
LOG_INFO(Core, "horse {}", line);
log_("coop: puppet: " + text);
Say("P" + std::to_string(n) + " " + name + " spawned after " + ms);
constexpr const char* kHorsePack = "CoopPuppet_Horse";
switch (s) { case a: return "alive"; case b: return "reviving"; }
out.push_back(std::string(to_string(s)) + " -> " + to_string(n) + (why.empty() ? "" : ": " + why));
text("revived (%s, life %d)", by ? text("by P%u", by).c_str() : "our own fairy");
auto on = EnvOn("LICHII_COOP_DUNGEON_LISTS") || EnvOn("LICHII_COOP_SET_KEY64");
'''


def _corpus(tmp_path):
    hook = tmp_path / "t" / "lichii" / "hook"
    hook.mkdir(parents=True)
    (hook / "coop_testinput.cpp").write_text(SRC, encoding="utf-8")
    return L.Corpus(tmp_path)


def test_lines_built_in_steps(tmp_path):
    c = _corpus(tmp_path)
    m = L.Matcher(c)
    assert c.wrapper("horse ") and not c.wrapper("cow ")
    # "coop: test {}" + "mark " + the script's own mark name
    assert m.composed("coop: test mark walk", frozenset({"mark", "walk"}))[0] == {"t"}
    assert m.composed("coop: test mark walk", frozenset())[0] == set()
    # the downed machine: state names, " -> ", ": ", "revived (%s" and "by P%u"
    assert m.composed(r"reviving -> alive: revived \(by P\d", frozenset())[0] == {"t"}
    assert m.composed(r"-> game_over", frozenset())[0] == set()  # no such state in this mirror
    # the horse director's lines: "horse {}" + "coop: puppet: " + "P" ... " spawned after "
    assert m.composed(r"horse coop: puppet: P\d CoopPuppet_Horse\w* spawned after", frozenset())[0] == {"t"}
    assert m.composed(r"horse coop: puppet: P\d CoopPuppet_Horse spawned before", frozenset())[0] == set()


def test_switches_as_run_ps1_reads_them(tmp_path):
    c = _corpus(tmp_path)
    esc = tmp_path / "esc"
    esc.mkdir()
    f = esc / "x.A.txt"
    f.write_text("#! switches A:LICHII_COOP_SET_KEY64=1,LICHII_COOP_DUNGEON_LISTS=a.txt,b.txt\n"
                 "#! count A horse coop: puppet: P\\d CoopPuppet_Horse spawned after >= 1\nmark a\n", encoding="utf-8")
    found = L.lint_file(f, c, L.Matcher(c), {})
    causes = {(x["cause"], x["detail"]) for x in found if x["sev"] == "error"}
    assert not any("SET_KEY64" in d for _, d in causes)  # A:NAME=v is run.ps1's per-game form
    assert any(cause.startswith("interruptor con comas") and d == "b.txt" for cause, d in causes)
    assert not any(cause.startswith("count con una etiqueta") for cause, _ in causes)  # "horse {}" is real


def test_join_header_wants_a_first_or_b_first(tmp_path):
    c = _corpus(tmp_path)
    esc = tmp_path / "esc"
    esc.mkdir()
    good, bad = esc / "g.A.txt", esc / "b.A.txt"
    good.write_text("#! join B-first\nmark a\n", encoding="utf-8")
    bad.write_text("#! join both\nmark a\n", encoding="utf-8")
    causes = lambda f: {x["cause"] for x in L.lint_file(f, c, L.Matcher(c), {}) if x["sev"] == "error"}
    assert not any("join" in x or "desconocida" in x for x in causes(good))
    assert any(x.startswith("join") for x in causes(bad))
