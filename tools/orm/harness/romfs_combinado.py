"""Merged romfs view (base + update on top) from an unpacked .nup, with hard links (no extra space).

    python -I tools/orm/harness/romfs_combinado.py [--dump <ORM_DUMP>] [--out <ORM_ROMFS>]

This is what the Qt frontend's "Dump RomFS" gives and what daurosk's puppet_pack expects: the update's own tables
(RSDB/*.Product.111.*, ResourceSizeTable.Product.111.*) replace the base's .100 ones. A base file whose versioned
name has an update counterpart (same name with another Product.<v>) is left out, so the builder finds exactly one.

The puppet pack needs base + update merged.
"""
import argparse, os, re, shutil
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import orm_paths  # noqa: E402

VER = re.compile(r"\.Product\.\d+\.")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=str(orm_paths.DUMP))
    ap.add_argument("--out", default=str(orm_paths.ROMFS))
    a = ap.parse_args()
    base = Path(a.dump) / "base" / "romfs"
    updates = sorted((Path(a.dump) / "updates").glob("*/romfs"), key=lambda p: int(p.parent.name))
    out = Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    layers = [base] + updates
    chosen: dict[str, Path] = {}
    for layer in layers:
        for f in layer.rglob("*"):
            if f.is_file():
                rel = f.relative_to(layer).as_posix()
                key = VER.sub(".Product.*.", rel)  # versioned tables: the newest layer wins
                chosen[key] = f
    n = 0
    for key, f in chosen.items():
        layer = next(l for l in reversed(layers) if f.is_relative_to(l))
        dst = out / f.relative_to(layer)
        dst.parent.mkdir(parents=True, exist_ok=True)
        os.link(f, dst)
        n += 1
    print(f"merged {len(layers)} layers ({', '.join(p.parent.name for p in layers)}): {n} files -> {out}")


if __name__ == "__main__":
    main()
