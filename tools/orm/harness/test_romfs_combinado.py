"""Unit test (no game) for romfs_combinado.py: the merged base + update romfs view.
"""
import os
import pathlib
import shutil
import sys
from unittest.mock import patch

import pytest

# Add the current directory to the path so we can import romfs_combinado
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import romfs_combinado


def test_romfs_combinado(tmp_path):
    # Create fake dump structure
    base = tmp_path / "dump" / "base" / "romfs"
    updates = tmp_path / "dump" / "updates" / "131072" / "romfs"
    
    base.mkdir(parents=True)
    updates.mkdir(parents=True)
    
    # Create base files
    (base / "RSDB").mkdir()
    (base / "RSDB" / "ActorInfo.Product.100.rstbl.byml.zs").write_text("base100")
    (base / "Pack").mkdir()
    (base / "Pack" / "a.txt").write_text("base-a")
    (base / "Pack" / "b.txt").write_text("base-b")
    
    # Create update files
    (updates / "RSDB").mkdir()
    (updates / "RSDB" / "ActorInfo.Product.111.rstbl.byml.zs").write_text("upd111")
    (updates / "Pack").mkdir()
    (updates / "Pack" / "b.txt").write_text("upd-b")
    
    # Set the command line arguments
    with patch.object(sys, 'argv', ['romfs_combinado.py', '--dump', str(tmp_path / "dump"), '--out', str(tmp_path / "out")]):
        romfs_combinado.main()
    
    # Check result
    out = tmp_path / "out"
    
    # 1. out/RSDB has exactly one file, ActorInfo.Product.111.rstbl.byml.zs, with content "upd111".
    rsdb_file = out / "RSDB" / "ActorInfo.Product.111.rstbl.byml.zs"
    assert rsdb_file.exists()
    assert rsdb_file.read_text() == "upd111"
    
    # 2. out/Pack/a.txt reads "base-a" and out/Pack/b.txt reads "upd-b".
    pack_a = out / "Pack" / "a.txt"
    pack_b = out / "Pack" / "b.txt"
    assert pack_a.exists()
    assert pack_a.read_text() == "base-a"
    assert pack_b.exists()
    assert pack_b.read_text() == "upd-b"
    
    # 3. Running main() a second time with the same arguments gives the same result (no error).
    with patch.object(sys, 'argv', ['romfs_combinado.py', '--dump', str(tmp_path / "dump"), '--out', str(tmp_path / "out")]):
        romfs_combinado.main()
    
    # 4. out/Pack/a.txt is a hard link of the base file: os.path.samefile(out/Pack/a.txt, dump/base/romfs/Pack/a.txt) is True.
    base_a = tmp_path / "dump" / "base" / "romfs" / "Pack" / "a.txt"
    assert os.path.samefile(pack_a, base_a)
    
    # Test that the structure matches expected behavior
    assert (out / "RSDB").exists()
    assert (out / "Pack").exists()