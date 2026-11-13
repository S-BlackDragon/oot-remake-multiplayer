"""Unit test (no game) for orm_inst.py: the instance ini and the fps read from a log.
Doc: docs/DECISIONES.md D-R01."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import orm_inst as oi
import pytest
from pathlib import Path

def test_ini_defaults():
    result = oi.ini()
    assert "backend=1" in result
    assert "resolution_setup=1" in result
    assert "oot_30fps=true" in result
    assert "nvdec_emulation=0" in result
    assert "output_engine=3" in result
    
    # Check for the default=false lines
    assert "backend" + chr(92) + "default=false" in result
    assert "resolution_setup" + chr(92) + "default=false" in result
    assert "oot_30fps" + chr(92) + "default=false" in result
    assert "nvdec_emulation" + chr(92) + "default=false" in result
    assert "output_engine" + chr(92) + "default=false" in result

def test_ini_with_args():
    result = oi.ini(fps30=False, no_video=False)
    assert "oot_30fps=false" in result
    assert "nvdec_emulation=2" in result

def test_fps_after(tmp_path):
    # Create the folder structure and log file
    inst_dir = tmp_path / "inst"
    inst_dir.mkdir()
    user_dir = inst_dir / "user"
    user_dir.mkdir(parents=True)
    log_dir = user_dir / "log"
    log_dir.mkdir()
    
    log_file = log_dir / "eden_log.txt"
    log_content = """FPS: 10.0 speed: 100% frametime: 1 ms
coop: test mark loaded
FPS: 30.0 speed: 100% frametime: 1 ms
FPS: 29.5 speed: 100% frametime: 1 ms"""
    log_file.write_text(log_content)
    
    # Test with existing marker
    result = oi.fps_after(inst_dir, "coop: test mark loaded")
    assert result == [30.0, 29.5]
    
    # Test with non-existing marker
    result = oi.fps_after(inst_dir, "no such marker")
    assert result == []

def test_wait_log(tmp_path):
    # Create a simple log file
    inst_dir = tmp_path / "inst"
    inst_dir.mkdir()
    user_dir = inst_dir / "user"
    user_dir.mkdir(parents=True)
    log_dir = user_dir / "log"
    log_dir.mkdir()
    
    log_file = log_dir / "eden_log.txt"
    log_file.write_text("test pattern found")
    
    # Test with matching pattern and single root
    result = oi.wait_log([inst_dir], r"test pattern.*", timeout=2)
    assert isinstance(result, (int, float))
    
    # Test with non-matching pattern - should return None
    result = oi.wait_log([inst_dir], r"nonexistent pattern", timeout=2)
    assert result is None

def test_min_fps():
    assert oi.MIN_FPS == 20