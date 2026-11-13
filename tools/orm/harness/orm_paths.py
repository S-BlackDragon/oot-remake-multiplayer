"""Machine paths of the test tools, as configuration.

Everything lives under ORM_ROOT (default: ~/orm-work). Every single path can be overridden with its own environment
variable. Nothing here touches the disk; a missing folder is reported by the tool that needs it.

    ORM_ROOT        base folder for everything below                         (default ~/orm-work)
    ORM_BUILD_BIN   emulator build folder holding the emulator exe            (ROOT/build/bin)
    ORM_INST_DIR    where the test instances are made                         (ROOT/inst)
    ORM_SERVER_EXE  orm_server executable                                     (ROOT/server/orm_server[.exe])
    ORM_BOT_EXE     remake-bot executable (six-player scenarios)             (ROOT/bot/remake-bot[.exe])
    ORM_TREE        source tree with tools/release/kit/*.txt and lichii/      (ROOT/tree)
    ORM_WT          folder of several trees (only lint_escenarios.py)         (ROOT/wt)
    ORM_SAVES_ROOT  where synthetic saves are written                         (ROOT/saves)
    ORM_BASE_SAVES  a real save folder the recipes start from (read only)     (ROOT/saves/base)
    ORM_DUMP        unpacked game dump                                        (ROOT/dump)
    ORM_ROMFS       merged romfs                                              (ROOT/romfs_merged)
    ORM_PUPPET_LOAD puppet pack "load" folder                                 (ROOT/puppet_user/load)
    ORM_REF_USER    reference emulator user/ folder (keys, firmware)          (ROOT/ref/user)
    ORM_GAME_DIR    folder with the game package (*.nup)                      (ROOT/game)
    ORM_GAME_LOCK   lock folder so only one set of games runs at once         (ROOT/logs/game.lock)
    ORM_REPORTS     summaries written by the tools                            (ROOT/reports)
    ORM_NETSIM_DIR  optional TCP link simulator folder (aguante.py only)       (ROOT/netsim)
    ORM_LOG_ROOT    raw logs                                                  (ROOT/logs)
    ORM_STORY_LIST  shared_story.txt                                          (ORM_TREE/tools/release/kit/shared_story.txt)
"""
import os
import sys
from pathlib import Path

EXE = ".exe" if sys.platform == "win32" else ""


def _env(name: str, default) -> Path:
    v = os.environ.get(name)
    return Path(v) if v else Path(default)


ROOT = Path(os.environ.get("ORM_ROOT") or os.path.expanduser("~/orm-work"))
BUILD_BIN = _env("ORM_BUILD_BIN", ROOT / "build" / "bin")
INST_DIR = _env("ORM_INST_DIR", ROOT / "inst")
SERVER_EXE = _env("ORM_SERVER_EXE", ROOT / "server" / ("orm_server" + EXE))
BOT_EXE = _env("ORM_BOT_EXE", ROOT / "bot" / ("remake-bot" + EXE))
TREE = _env("ORM_TREE", ROOT / "tree")
WT = _env("ORM_WT", ROOT / "wt")
SAVES_ROOT = _env("ORM_SAVES_ROOT", ROOT / "saves")
BASE_SAVES = _env("ORM_BASE_SAVES", SAVES_ROOT / "base")
DUMP = _env("ORM_DUMP", ROOT / "dump")
ROMFS = _env("ORM_ROMFS", ROOT / "romfs_merged")
PUPPET_LOAD = _env("ORM_PUPPET_LOAD", ROOT / "puppet_user" / "load")
REF_USER = _env("ORM_REF_USER", ROOT / "ref" / "user")
GAME_DIR = _env("ORM_GAME_DIR", ROOT / "game")
REPORTS = _env("ORM_REPORTS", ROOT / "reports")
NETSIM_DIR = _env("ORM_NETSIM_DIR", ROOT / "netsim")
LOG_ROOT = _env("ORM_LOG_ROOT", ROOT / "logs")
GAME_LOCK = _env("ORM_GAME_LOCK", LOG_ROOT / "game.lock")
STORY_LIST = _env("ORM_STORY_LIST", TREE / "tools" / "release" / "kit" / "shared_story.txt")


def posix(p) -> str:
    return str(p).replace("\\", "/")
