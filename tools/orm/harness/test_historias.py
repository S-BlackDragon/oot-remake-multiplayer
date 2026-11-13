"""Checks of the story scenarios (PLAN_100 F11, docs/HISTORIAS_ESCENARIOS.md) without a game.

With the shared story list (env ORM_STORY_LIST, else ORM_TREE/tools/release/kit; the tests that need it are
skipped without it): every cutscene of the list that is not `solo` has a pair whose A asks for it (`event <name>`,
historia-* or final-*), every solo one too (with B never running it), every one that changes the world (a story step
or a world Bool on its line) a latecomer pair (`-rezagado`), and the files and the doc table are what historias.py
writes today. Always: every pair historias.py wrote parses as the hook's test script (test_recorridos' parser), warps
both games to the same known scene and StartPos, names only marks of the game it reads, keeps A's and B's marks on
the same ticks, and in a rezagado B marks the cutscene played at least 5 s before A asks for it.

    python -m pytest tools/orm/harness/test_historias.py -q
"""
import re
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import historias as H  # noqa: E402
from test_recorridos import ESC, SCENES, Script, code  # noqa: E402  (the hook's line parser, the scene list)

HAVE_LIST = H.LIST.is_file()
needs_list = pytest.mark.skipif(not HAVE_LIST, reason=f"no story list at {H.LIST} (set ORM_STORY_LIST)")
DOC = HERE.parent.parent / "docs" / "HISTORIAS_ESCENARIOS.md"


def asked_events(path: Path) -> list[str]:
    return [w[1] for w in (code(l).split() for l in path.read_text(encoding="utf-8").splitlines())
            if len(w) == 2 and w[0].lower() == "event"]


def scenarios_asking() -> dict[str, list[str]]:
    """event -> the A scripts (historia-*, final-*) that ask for it."""
    out: dict[str, list[str]] = {}
    for p in sorted(ESC.glob("historia*.A.txt")) + sorted(ESC.glob("final-*.A.txt")):
        for e in asked_events(p):
            out.setdefault(e, []).append(p.name[:-len(".A.txt")])
    return out


def generated_pairs() -> list[str]:
    return sorted(p.name[:-len(".A.txt")] for p in ESC.glob("historia-*.A.txt")
                  if H.GENERATED in p.read_text(encoding="utf-8"))


def mark_ticks(path: Path) -> dict[str, int]:
    """mark -> the tick the script reaches it (waits and presses summed as historias.ticks does)."""
    out, lines = {}, []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#"):
            continue
        w = code(raw).split()
        if w and w[0] == "mark":
            out[w[1]] = H.ticks(lines)
        lines.append(raw)
    return out


def tick_of(path: Path, pred) -> int | None:
    lines = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#"):
            continue
        if pred(code(raw).split()):
            return H.ticks(lines)
        lines.append(raw)
    return None


# ---------------------------------------------------------------- against the list


@needs_list
def test_every_non_solo_cutscene_has_a_scenario():
    asking = scenarios_asking()
    missing = [ln.event for ln in H.read_list() if not ln.solo and ln.event not in asking]
    assert not missing, f"{len(missing)} shared cutscenes with no scenario asking for them: {missing}"


@needs_list
def test_every_solo_cutscene_has_a_scenario_where_b_does_not_start_it():
    asking = scenarios_asking()
    bad = []
    for ln in H.read_list():
        if not ln.solo:
            continue
        names = asking.get(ln.event, [])
        ok = any(re.search(rf"^#!\s*never B coop: (story: )?event running '{re.escape(ln.event)}'",
                           (ESC / f"{n}.B.txt").read_text(encoding="utf-8") + (ESC / f"{n}.A.txt").read_text(
                               encoding="utf-8"), re.M) for n in names if (ESC / f"{n}.B.txt").exists())
        if not ok:
            bad.append(ln.event)
    assert not bad, f"solo cutscenes with no pair saying B never runs them: {bad}"


@needs_list
def test_every_world_changing_cutscene_has_a_latecomer_pair():
    asking = scenarios_asking()
    missing = []
    for ln in H.read_list():
        if ln.solo or not (ln.steps or ln.bools) or ln.event in H.NO_REZAGADO:
            continue
        if not any(n.endswith("rezagado") for n in asking.get(ln.event, [])):
            missing.append(ln.event)
    assert not missing, f"cutscenes that change the world with no -rezagado pair: {missing}"


@needs_list
def test_the_table_is_the_list():
    listed = [ln.event for ln in H.read_list()]
    table = [r.event for r in H.TABLE]
    assert sorted(table) == sorted(listed), (
        f"not in the table: {sorted(set(listed) - set(table))}; not on the list: {sorted(set(table) - set(listed))}")
    slugs = [r.slug for r in H.TABLE if r.slug]
    assert len(slugs) == len(set(slugs)), "a slug twice"
    for r in H.TABLE:
        if r.event not in H.COVERED or H.wants_rezagado(next(p for p in H.plans() if p.row is r)):
            assert r.slug and r.warp, f"{r.event}: needs a slug and a warp"


@needs_list
def test_every_step_on_a_line_has_its_target_values():
    for p in H.plans():
        assert sorted(p.targets) == sorted(p.line.steps), f"{p.line.event}: HISTORIA_COMPLETA.md section 3 misses a step"
        for s, vals in p.targets.items():
            assert all(v in p.line.steps[s] for v in vals), f"{p.line.event}: {s} has no value {vals}"


@needs_list
def test_the_files_are_what_historias_writes():
    stale = [n for n, text in H.outputs().items()
             if not (ESC / n).exists() or (ESC / n).read_text(encoding="utf-8") != text]
    assert not stale, f"run python tools/orm/harness/historias.py: {stale[:10]}"


@needs_list
def test_the_doc_table_is_current():
    assert H.table().strip() in DOC.read_text(encoding="utf-8"), \
        "paste python tools/orm/harness/historias.py --table into HISTORIAS_ESCENARIOS.md"


# ---------------------------------------------------------------- the files themselves


def test_there_are_generated_pairs():
    assert len(generated_pairs()) >= 150


@pytest.mark.parametrize("name", generated_pairs())
def test_pair(name):
    pa, pb = ESC / f"{name}.A.txt", ESC / f"{name}.B.txt"
    assert pb.exists(), f"no {pb.name}"
    a, b = Script(pa), Script(pb)
    by_game = {"A": a, "B": b}
    for s in (a, b):
        assert not s.errors, "\n".join(s.errors)
        assert re.search(r"^#!\s*noload\s*$", s.text, re.M) and re.search(r"^#!\s*start\s+\S", s.text, re.M)
        assert len(s.warps) == 1, f"{s.path.name}: one warp, at the start"
        _i, words = s.warps[0]
        scene, rest = words[0], [w for w in words[1:] if w.lower() not in ("adult", "child")]
        assert scene in SCENES, f"{s.path.name}: no scene {scene} in escenas_banc.txt"
        if rest:
            assert rest[0] in SCENES[scene], f"{s.path.name}: {scene} has no StartPos {rest[0]}"
        if not re.search(r"_(Adult|Child)$", scene) and rest:
            assert words[-1].lower() in ("adult", "child"), f"{s.path.name}: a scene with no era needs its age"
        assert len(set(s.marks)) == len(s.marks), f"{s.path.name}: a mark twice"
        for i, kind, game, rx, span, count in s.parsed_asserts():
            assert game in ("A", "B"), f"{s.path.name}:{i}: say which game's log"
            assert game == ("A" if s is a else "B"), f"{s.path.name}:{i}: each file asserts its own game's log"
            re.compile(rx)
            if span:
                marks = by_game[game].marks
                for m in span:
                    assert m in marks, f"{s.path.name}:{i}: no mark {m} in {by_game[game].path.name}"
                assert marks.index(span[0]) < marks.index(span[1]), f"{s.path.name}:{i}: marks out of order"
    assert a.warps[0][1] == b.warps[0][1], "both games warp to the same scene and start"
    events_a, events_b = asked_events(pa), asked_events(pb)
    assert len(events_a) == 1 and not events_b, "A asks for the cutscene once, B never"
    event = events_a[0]
    ta, tb = mark_ticks(pa), mark_ticks(pb)
    for m in ("after", "asked", "played", "end"):
        assert ta.get(m) == tb.get(m), f"mark {m}: A at {ta.get(m)}, B at {tb.get(m)} (the windows must match)"
    asked = tick_of(pa, lambda w: len(w) == 2 and w[0] == "event")
    kinds = {(k, g, rx) for _i, k, g, rx, _s, _c in a.parsed_asserts() + b.parsed_asserts()}
    e = re.escape(event)
    if name.endswith("-rezagado"):
        flagged = tick_of(pb, lambda w: w[:1] == ["flag"] and w[1:] == [f"HavePlayedEvent.{event}", "1"])
        assert flagged is not None and flagged <= asked - 300, "B marks it played at least 5 s before A asks"
        assert any(k == "expect" and g == "B" and "is set here" in rx for k, g, rx in kinds)
        assert any(k == "never" and g == "B" and rx.endswith("starting") for k, g, rx in kinds)
        assert any(k == "expect" and g == "B" and ("0 -> 1" in rx or "-> " in rx) for k, g, rx in kinds), \
            "the latecomer ends with the world's flags"
    elif any("is solo" in rx for _k, _g, rx in kinds):
        assert ("never", "B", f"coop: event running '{e}'") in kinds, "B never runs a solo cutscene"
        assert ("never", "A", f"coop: story: STORY_EVENT start sent \\('{e}'") in kinds, "A never offers it"
        assert any(k == "expect" and g == "B" and f"HavePlayedEvent\\.{e} 0 -> 1" in rx for k, g, rx in kinds)
    else:
        assert any(k == "expect" and g == "A" and rx.startswith(f"coop: story: owner: our game started '{e}'")
                   for k, g, rx in kinds)
        assert any(k == "expect" and g == "B" and ": starting" in rx and f"('{e}'" in rx.replace("\\", "")
                   for k, g, rx in kinds)
        assert any(k == "never" and g == "B" and rx.endswith("starting") for k, g, rx in kinds), \
            "no second start in B"
        assert any(k == "never" and g == "A" and "runs here again" in rx for k, g, rx in kinds), \
            "no second start in A"
    assert ta["end"] <= tb["end"], "B lasts as long as A"
