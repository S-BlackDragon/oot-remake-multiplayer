"""Synthetic saves for the scenarios of later blocks: a base save + a recipe (recetas/NN-<block>.txt) -> a save
folder in <ORM_SAVES_ROOT>/<block>, outside the repository, read back and checked.

  python partida.py check [RECIPE ...]       parse every recipe (all of recetas/ by default) and, with the romfs,
                                             check each name, type, enum value and StartPos against GameDataList
  python partida.py build RECIPE [--base DIR] [--slot NN|auto] [--out DIR] [--romfs DIR | --no-romfs]
  python partida.py all [--base DIR] [...]   build every recipe; summary in <ORM_SAVES_ROOT>/partidas_resumen.txt
  python partida.py verify RECIPE --out DIR [--base DIR] [--slot NN|auto]
  python partida.py show SAVE NAME ...       a progress.sav (or a save folder: its newest slot) read by flag name

The base is a save folder as the game keeps it (account.sav, option.sav, slot_NN/progress.sav and caption.sav);
by default ORM_BASE_SAVES (a copy of your own saves; default <ORM_SAVES_ROOT>/base). It is only read. The output folder has
the same files; every slot_NN gets the built progress.sav (and the base slot's caption.sav), so Continue loads the
recipe whatever slot it picks. Beside it, <out>.partida.txt says what was built from what, every change (old ->
new) and the verdict of the read-back. An existing output folder is replaced only when that file is there (a
folder this tool made). Nothing is ever written under the repository, %APPDATA% or %LOCALAPPDATA%.

Scenarios use the folder as any other: `orm_inst.prepare(k, saves=Path("<ORM_SAVES_ROOT>/08-dodongo"))`, or
`r03_pareja.py --saves <ORM_SAVES_ROOT>/08-dodongo` (or `#! saves 08-dodongo` in a scenario file).

The romfs (ORM_ROMFS: your own dump, merged) is needed for a StartPos and for checking enum values; run with
plain `python` (zstandard is in the user site). Without it a name the save lacks is still an error. Tests: test_partidas.py (no game data).
"""
import argparse
import hashlib
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import progress_sav as ps  # noqa: E402
import receta as rc  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
RECIPES = os.path.join(HERE, "recetas")
sys.path.insert(0, os.path.join(HERE, "..", "harness"))
import orm_paths  # noqa: E402  (ORM_ROOT and one variable per path)
SAVES_ROOT = str(orm_paths.SAVES_ROOT)
DEFAULT_BASE = str(orm_paths.BASE_SAVES)
DEFAULT_ROMFS = str(orm_paths.ROMFS)
MANIFEST = ".partida.txt"
SUMMARY = "partidas_resumen.txt"


class BuildError(ValueError):
    pass


# ---------------------------------------------------------------- paths


def _norm(p):
    return os.path.normcase(os.path.realpath(p))


def _inside(path, root):
    p, r = _norm(path), _norm(root)
    return p == r or p.startswith(r.rstrip("\\/") + os.sep)


def check_out_path(out, base):
    for name in ("APPDATA", "LOCALAPPDATA"):
        root = os.environ.get(name)
        if root and _inside(out, root):
            raise BuildError(f"{out}: under %{name}%, never written")
    if _inside(out, REPO):
        raise BuildError(f"{out}: inside the repository (saves never go there)")
    if _inside(out, base) or _inside(base, out):
        raise BuildError(f"{out}: overlaps the base save {base}")
    if os.path.exists(out) and not os.path.isfile(out + MANIFEST):
        raise BuildError(f"{out}: exists and is not a folder this tool made (no {os.path.basename(out)}{MANIFEST})")


def slots_of(folder):
    """{slot dir name: progress.sav path} of a save folder."""
    out = {}
    if os.path.isdir(folder):
        for n in sorted(os.listdir(folder)):
            p = os.path.join(folder, n, "progress.sav")
            if n.lower().startswith("slot_") and os.path.isfile(p):
                out[n] = p
    return out


def pick_slot(base, slot):
    slots = slots_of(base)
    if not slots:
        raise BuildError(f"{base}: no slot_NN/progress.sav")
    if slot in (None, "auto"):
        return max(slots, key=lambda n: (os.path.getmtime(slots[n]), n))
    name = slot if slot.lower().startswith("slot_") else f"slot_{int(slot):02d}"
    if name not in slots:
        raise BuildError(f"{base}: no {name} (has {', '.join(slots)})")
    return name


def sha1(data):
    return hashlib.sha1(data).hexdigest()


# ---------------------------------------------------------------- romfs


def banc_area(banc, areas, has_file):
    """The Banc folder holding scene `banc`'s placements: the longest folder name the scene is or starts with
    (HyruleField for HyruleField_Adult), else the one folder with a `<banc>_Static/_Dynamic.bcett.byml.zs`
    (CrumblingGanonCastle and FinalBattleField live in GanonCastle/). None when no folder or several have it.
    `has_file(area, name)` says whether Banc/<area>/<name> exists."""
    by_name = sorted((a for a in areas if banc == a or banc.startswith(a + "_")), key=len, reverse=True)
    if by_name:
        return by_name[0]
    holders = [a for a in sorted(areas)
               if any(has_file(a, f"{banc}_{part}.bcett.byml.zs") for part in ("Static", "Dynamic"))]
    return holders[0] if len(holders) == 1 else None


class RomfsInfo:
    """GameDataList and the Banc StartPos placements, read-only (romfs_read.py of the source tree, tools/romfs)."""

    def __init__(self, root):
        sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))  # romfs_read.py of the source tree
        import romfs_read as rr  # needs zstandard (user site: plain python)
        self.rr = rr
        self.romfs = rr.Romfs(root)
        self.gd = rr.GameData(self.romfs)
        self._startpos = {}

    def entry(self, kind, h):
        for tname, ent in self.gd.by_hash.get(h, []):
            if ps.GDL_TYPE.get(tname) == kind:
                return tname, ent
        return None

    def startpos(self, banc, name):
        """(x, y, z) of the StartPos placement `name` in Banc `banc`."""
        if banc not in self._startpos:
            found = {}
            area = banc_area(banc, os.listdir(self.romfs.path("Banc")),
                             lambda a, n: self.romfs.exists(os.path.join("Banc", a, n)))
            if area is None:
                raise BuildError(f"scene {banc}: no Banc folder for it in the romfs")
            for part in ("Static", "Dynamic"):
                rel = os.path.join("Banc", area, f"{banc}_{part}.bcett.byml.zs")
                if not self.romfs.exists(rel):
                    continue
                for a in self.romfs.byml(rel).get("Actors", []):
                    if "StartPos" in str(a.get("Gyaml", "")) and a.get("Name"):
                        t = tuple(round(float(v), 4) for v in a.get("Translate", (0, 0, 0)))
                        found.setdefault(str(a["Name"]), set()).add(t)
            if not found:
                raise BuildError(f"scene {banc}: no StartPos in {area}/{banc}_*.bcett.byml.zs")
            self._startpos[banc] = found
        found = self._startpos[banc]
        if name not in found:
            raise BuildError(f"scene {banc}: no StartPos {name!r} (has {', '.join(sorted(found))})")
        if len(found[name]) != 1:
            raise BuildError(f"scene {banc}: StartPos {name!r} is at {len(found[name])} places")
        return next(iter(found[name]))


def load_romfs(root, required=False):
    if root and os.path.isdir(root):
        try:
            return RomfsInfo(root)
        except (ImportError, SystemExit, OSError) as e:
            if required:
                raise BuildError(f"romfs {root}: {e}") from None
            print(f"partida: romfs not read ({e}); names are checked against the save only", file=sys.stderr)
    elif required:
        raise BuildError(f"romfs {root}: not a folder")
    return None


def check_romfs(recipe, info):
    """Errors (list of str) of the recipe against GameDataList: every named flag of its type and saved in
    progress.sav (SaveFileIndex 0), every enum value one of the flag's, the StartPos there."""
    errors = []
    for op in recipe.ops:
        if op.kind == rc.KEY64:
            if not any(t == "Bool64bitKey" for t, _ in info.gd.by_hash.get(op.hash, [])):
                errors.append(f"{op.where}: key64 {op.hash:#x} not in GameDataList")
            continue
        got = info.entry(op.kind, op.hash)
        if got is None:
            if not op.optional:
                errors.append(f"{op.where}: {ps.TYPE_NAMES[op.kind]} {op.label()} not in GameDataList")
            continue
        tname, ent = got
        if ent.get("SaveFileIndex") != 0:
            errors.append(f"{op.where}: {op.label()} is not saved in progress.sav (SaveFileIndex "
                          f"{ent.get('SaveFileIndex')})")
        if op.kind == ps.ENUM:
            raw = ent.get("RawValues")
            if isinstance(raw, list) and op.value not in raw:
                errors.append(f"{op.where}: {op.label()} has no value {op.value!r} ({', '.join(map(str, raw))})")
            if op.order and isinstance(raw, list) and [v for v in raw if v in op.order] != list(op.order):
                errors.append(f"{op.where}: {op.label()}: values not in the order {op.order}")
    if recipe.scene and recipe.scene[1]:
        try:
            info.startpos(recipe.scene[0], recipe.scene[1])
        except BuildError as e:
            errors.append(f"{recipe.scene[3]}: {e}")
    return errors


# ---------------------------------------------------------------- applying


def scene_ops(recipe, info):
    """The ops of the recipe's `scene` line: Sequence_CurrentBanc, its String32 copy, Link's position."""
    if not recipe.scene:
        return []
    banc, startpos, xyz, where = recipe.scene
    if startpos:
        if info is None:
            raise BuildError(f"{where}: scene {banc} {startpos}: a StartPos needs the romfs (or give X Y Z)")
        xyz = info.startpos(banc, startpos)
    ops = [rc.Op(ps.STRING64, rc.CURRENT_BANC, banc, where),
           rc.Op(ps.STRING32, rc.BANC_COPY_STRING32, banc, where, optional=True)]
    if xyz is None:
        raise BuildError(f"{where}: scene {banc}: give a StartPos or X Y Z (Link's saved position)")
    ops += [rc.Op(ps.VECTOR3, h, tuple(xyz), where, optional=True) for h in rc.POSITION_VECTORS]
    return ops


def all_ops(recipe, info):
    rc.check_age_scene(recipe)
    return recipe.ops + scene_ops(recipe, info)


def _value_for(save, op):
    """The value op leaves, given what `save` holds now."""
    if op.kind == rc.KEY64:
        return op.value
    if op.kind == ps.ENUM:
        new = op.value_hash
        if op.mode == rc.ATLEAST:
            order = [ps.murmur3_32(v) for v in op.order]
            old = save.get(op.kind, op.hash)
            if old in order and order.index(old) >= order.index(new):
                return old  # never lowered; an unknown value is raised
        return new
    if op.mode == rc.ATLEAST:
        return max(save.get(op.kind, op.hash), op.value)
    return op.value


def apply(save, ops):
    """Applies ops to save in order. Returns (changes, skipped): changes [(op, old, new)], skipped [op] (optional
    flags the save lacks). A required flag the save lacks is a BuildError."""
    changes, skipped, missing = [], [], []
    for op in ops:
        if op.kind != rc.KEY64 and not save.has(op.kind, op.hash):
            (skipped if op.optional else missing).append(op)
            continue
        if missing:
            continue
        old = save.get_key(op.hash) if op.kind == rc.KEY64 else save.get(op.kind, op.hash)
        new = _value_for(save, op)
        if op.kind == rc.KEY64:
            save.set_key(op.hash, new)
        else:
            save.set(op.kind, op.hash, new)
        changes.append((op, old, save.get_key(op.hash) if op.kind == rc.KEY64 else save.get(op.kind, op.hash)))
    if missing:
        raise BuildError("not in this save: " + "; ".join(
            f"{op.where} {ps.TYPE_NAMES[op.kind]} {op.label()}" for op in missing))
    return changes, skipped


def _same(kind, a, b):
    if kind == ps.FLOAT:
        return abs(a - b) <= 1e-4 * max(1.0, abs(b))
    if kind == ps.VECTOR3:
        return all(abs(x - y) <= 1e-3 for x, y in zip(a, b))
    if kind == ps.ENUM and isinstance(b, str):
        return a == ps.murmur3_32(b)
    return a == b


def fmt(kind, v):
    if kind == ps.ENUM and isinstance(v, int):
        return f"{v:#010x}"
    if kind == ps.VECTOR3:
        return "(" + ", ".join(f"{x:.2f}" for x in v) + ")"
    return repr(v) if isinstance(v, str) else str(v)


def verify(base_bytes, out_bytes, ops):
    """Problems (list of str) of a built save against its base and ops: it parses, the last write of every flag
    reads back, and nothing else differs (every other record, string and vector byte for byte; the key list only
    by the recipe's keys)."""
    problems = []
    try:
        out = ps.Save(out_bytes)
    except ps.SaveError as e:
        return [f"the result is not a save: {e}"]
    base = ps.Save(base_bytes)
    expect = ps.Save(base_bytes)
    try:
        changes, _ = apply(expect, ops)
    except BuildError as e:
        return [str(e)]
    final = {}
    for op, _old, new in changes:
        final[(op.kind, op.hash)] = (op, new)
    for (kind, h), (op, new) in final.items():
        got = out.get_key(h) if kind == rc.KEY64 else out.get(kind, h)
        if not _same(kind, got, new):
            problems.append(f"{op.where} {op.label()}: reads {fmt(kind, got)}, expected {fmt(kind, new)}")
        if op.mode == rc.SET and kind != rc.KEY64 and not _same(kind, got, op.value):
            problems.append(f"{op.where} {op.label()}: reads {fmt(kind, got)}, the recipe says {fmt(kind, op.value)}")
    if set(out.slots) != set(base.slots):
        problems.append("the table's records differ from the base's")
    if out.pool != base.pool or out.keys_at != base.keys_at or out.data[:out.keys_at] != expect.data[:expect.keys_at]:
        # Find what differs outside the recipe's flags, for the message.
        touched = set(final)
        for key in base.slots:
            if key in touched:
                continue
            try:
                if not _same(key[0], out.get(*key), base.get(*key)):
                    problems.append(f"{ps.TYPE_NAMES.get(key[0], key[0])} {key[1]:#010x} changed, not in the recipe")
            except ps.SaveError:
                pass
        if not any("changed, not in the recipe" in p for p in problems):
            problems.append("bytes differ from the base outside the recipe's values")
    want_keys = set(base.keys)
    for (kind, h), (_op, new) in final.items():
        if kind == rc.KEY64:
            want_keys.add(h) if new else want_keys.discard(h)
    if set(out.keys) != want_keys:
        problems.append(f"64-bit keys: {len(out.keys)} in the result, {len(want_keys)} expected")
    return problems


# ---------------------------------------------------------------- build


def build(recipe_path, base=DEFAULT_BASE, slot="auto", out=None, info=None):
    """Builds the save folder. Returns the manifest text (also written beside the folder)."""
    recipe = rc.parse(recipe_path)
    stem = os.path.splitext(os.path.basename(recipe_path))[0]
    out = out or os.path.join(SAVES_ROOT, stem)
    base = os.path.abspath(base)
    out = os.path.abspath(out)
    check_out_path(out, base)
    if info is not None:
        errors = check_romfs(recipe, info)
        if errors:
            raise BuildError("against GameDataList:\n  " + "\n  ".join(errors))
    ops = all_ops(recipe, info)
    name = pick_slot(base, slot)
    with open(slots_of(base)[name], "rb") as f:
        base_bytes = f.read()
    save = ps.Save(base_bytes)
    changes, skipped = apply(save, ops)
    new_bytes = save.to_bytes()
    problems = verify(base_bytes, new_bytes, ops)
    if problems:
        raise BuildError("the built save does not read back:\n  " + "\n  ".join(problems))

    tmp = f"{out}.tmp-{os.getpid()}"
    if os.path.exists(tmp):
        shutil.rmtree(tmp)
    os.makedirs(tmp)
    try:
        for n in os.listdir(base):
            p = os.path.join(base, n)
            if os.path.isfile(p):
                shutil.copyfile(p, os.path.join(tmp, n))
        caption = os.path.join(base, name, "caption.sav")
        for s in slots_of(base):
            os.makedirs(os.path.join(tmp, s))
            with open(os.path.join(tmp, s, "progress.sav"), "wb") as f:
                f.write(new_bytes)
            if os.path.isfile(caption):
                shutil.copyfile(caption, os.path.join(tmp, s, "caption.sav"))
        # Read back from disk, every slot, before it replaces anything.
        for s, p in slots_of(tmp).items():
            with open(p, "rb") as f:
                problems = verify(base_bytes, f.read(), ops)
            if problems:
                raise BuildError(f"{s} on disk does not read back:\n  " + "\n  ".join(problems))
        if os.path.exists(out):
            shutil.rmtree(out)
        os.replace(tmp, out)
    finally:
        if os.path.exists(tmp):
            shutil.rmtree(tmp)

    text = manifest(recipe, recipe_path, base, name, base_bytes, new_bytes, changes, skipped, info, out)
    with open(out + MANIFEST, "w", encoding="utf-8") as f:
        f.write(text)
    return text


def _rel(path):
    try:
        return os.path.relpath(path, REPO)
    except ValueError:  # another drive
        return path


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def manifest(recipe, recipe_path, base, slot, base_bytes, new_bytes, changes, skipped, info, out):
    lines = [
        f"Synthetic save {os.path.basename(out)} (tools/orm/saves/partida.py, {time.strftime('%Y-%m-%d %H:%M')})",
        f"recipe: {_rel(recipe_path)} (includes: {', '.join(os.path.basename(p) for p in recipe.files[:-1]) or 'none'})",
        f"recipe sha1: {sha1(b''.join(_read(p) for p in recipe.files))}",
        f"base: {base} {slot} sha1 {sha1(base_bytes)}",
        f"result: every slot_NN/progress.sav, sha1 {sha1(new_bytes)}, {len(new_bytes)} bytes",
        f"checked against GameDataList: {'yes' if info else 'no (names checked against the save only)'}",
        "read back: OK (every value of the recipe, nothing else changed)",
        f"changes: {sum(1 for _, o, n in changes if o != n)} of {len(changes)} writes; skipped (not in this save): "
        + (", ".join(op.label() for op in skipped) or "none"),
    ]
    for op, old, new in changes:
        if old != new:
            k = op.kind if op.kind != rc.KEY64 else None
            kname = ps.TYPE_NAMES.get(op.kind, "key64")
            if op.kind == ps.ENUM:
                old_s, new_s = enum_name(op, old, info), enum_name(op, new, info)
            else:
                old_s, new_s = fmt(k, old), fmt(k, new)
            lines.append(f"  {op.where:<22} {kname:<8} {op.label()}: {old_s} -> {new_s}")
    return "\n".join(lines) + "\n"


def enum_name(op, value, info):
    """An Enum value by name when the recipe or GameDataList knows it, else its hash."""
    names = [op.value] + list(op.order or ())
    if info is not None:
        got = info.entry(ps.ENUM, op.hash)
        if got and isinstance(got[1].get("RawValues"), list):
            names += [str(v) for v in got[1]["RawValues"]]
    for n in names:
        if isinstance(n, str) and ps.murmur3_32(n) == value:
            return n
    return f"{value:#010x}"


def verify_folder(recipe_path, out, base=DEFAULT_BASE, slot="auto", info=None):
    recipe = rc.parse(recipe_path)
    ops = all_ops(recipe, info)
    with open(slots_of(base)[pick_slot(base, slot)], "rb") as f:
        base_bytes = f.read()
    problems = []
    slots = slots_of(out)
    if not slots:
        return [f"{out}: no slot_NN/progress.sav"]
    for s, p in slots.items():
        with open(p, "rb") as f:
            problems += [f"{s}: {x}" for x in verify(base_bytes, f.read(), ops)]
    return problems


# ---------------------------------------------------------------- show


def show(path, names):
    if os.path.isdir(path):
        path = slots_of(path)[pick_slot(path, "auto")]
    save = ps.read(path)
    print(f"{path}: {len(save.slots)} flags, {len(save.keys)} 64-bit keys")
    for token in names:
        if token.lower().startswith("key64:"):
            key = int(token[6:], 0)
            print(f"  key64 {key:#x} = {save.get_key(key)}")
            continue
        h, _ = rc.flag_hash(token)
        found = False
        for kind in sorted(ps.TYPE_NAMES):
            if save.has(kind, h):
                found = True
                print(f"  {ps.TYPE_NAMES[kind]:<8} {token} = {fmt(kind, save.get(kind, h))}")
        if not found:
            print(f"  {token}: not in this save")


# ---------------------------------------------------------------- CLI


def recipe_files(args):
    if args:
        return [os.path.abspath(a) for a in args]
    return [os.path.join(RECIPES, n) for n in sorted(os.listdir(RECIPES)) if n.endswith(".txt")]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp):
        sp.add_argument("--base", default=DEFAULT_BASE)
        sp.add_argument("--slot", default="auto")
        sp.add_argument("--romfs", default=DEFAULT_ROMFS)
        sp.add_argument("--no-romfs", action="store_true")

    c = sub.add_parser("check")
    c.add_argument("recipes", nargs="*")
    c.add_argument("--romfs", default=DEFAULT_ROMFS)
    c.add_argument("--no-romfs", action="store_true")
    b = sub.add_parser("build")
    b.add_argument("recipe")
    b.add_argument("--out")
    common(b)
    al = sub.add_parser("all")
    al.add_argument("--root", default=SAVES_ROOT)
    common(al)
    v = sub.add_parser("verify")
    v.add_argument("recipe")
    v.add_argument("--out", required=True)
    common(v)
    s = sub.add_parser("show")
    s.add_argument("save")
    s.add_argument("names", nargs="+")
    a = p.parse_args(argv)
    try:
        if a.cmd == "show":
            show(a.save, a.names)
            return 0
        info = None if a.no_romfs else load_romfs(a.romfs)
        if a.cmd == "check":
            bad = 0
            for path in recipe_files(a.recipes):
                try:
                    recipe = rc.parse(path)
                    rc.check_age_scene(recipe)
                    errors = check_romfs(recipe, info) if info else []
                except rc.RecipeError as e:
                    errors = [str(e)]
                bad += bool(errors)
                print(f"{os.path.basename(path)}: " + ("OK" if not errors else f"{len(errors)} errors"))
                for e in errors:
                    print("  " + e)
            print(f"{'GameDataList checked' if info else 'GameDataList NOT checked (no romfs)'}; {bad} with errors")
            return 1 if bad else 0
        if a.cmd == "build":
            print(build(a.recipe, a.base, a.slot, a.out, info), end="")
            return 0
        if a.cmd == "verify":
            problems = verify_folder(os.path.abspath(a.recipe), a.out, a.base, a.slot, info)
            print("OK" if not problems else "\n".join(problems))
            return 1 if problems else 0
        if a.cmd == "all":
            lines, bad, t0 = [], 0, time.time()
            for path in recipe_files([]):
                stem = os.path.splitext(os.path.basename(path))[0]
                try:
                    text = build(path, a.base, a.slot, os.path.join(a.root, stem), info)
                    lines.append(f"OK    {stem}: {text.splitlines()[7]}")
                except (BuildError, rc.RecipeError, ps.SaveError, OSError) as e:
                    bad += 1
                    lines.append(f"ERROR {stem}: {e}")
            head = (f"partidas {time.strftime('%Y-%m-%d %H:%M')}: {len(lines) - bad} OK, {bad} errors, "
                    f"{time.time() - t0:.0f} s; base {a.base}; GameDataList {'checked' if info else 'NOT checked'}")
            text = head + "\n" + "\n".join(lines) + "\n"
            with open(os.path.join(a.root, SUMMARY), "w", encoding="utf-8") as f:
                f.write(text)
            print(text, end="")
            return 1 if bad else 0
    except (BuildError, rc.RecipeError, ps.SaveError, OSError) as e:
        print(f"partida: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
