"""Unit test (no game) for resumen_registro.py: real logs captured by the pair harness in reports/R0.3 and a few
hand-made lines. Doc: docs/REGISTROS.md."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import resumen_registro as rr
import orm_paths

R03 = orm_paths.REPORTS / "pareja"  # captured logs of earlier runs; the tests that need them skip without


def texts(r):
    return [s for _, s, _ in r.timeline]


def load(name):
    path = R03 / name
    if not path.is_file():
        pytest.skip(f"no {path}")
    with path.open(encoding="utf-8", errors="replace") as f:
        return rr.parse(f, name)


def test_story_owner_log():
    r = load("historia-largo_A_eden_log.txt")
    assert r.header["build"] == "v0.1.0-test5-34-g005f5bc-dirty, protocol 105"
    assert r.header["game"] == "e0f3ccfaf50065ae"
    assert r.header["player"] == "P1"
    assert "name 'A'" in r.header["server"]
    assert r.story_events == 4
    assert {"marionetas", "cinemáticas", "objetos"} <= set(r.switches)
    t = texts(r)
    assert "+ entra P2 'B'" in t
    assert "escena KokiriForest_Child" in t
    assert "marioneta de P2 creada (CoopPuppet_Child, 472 ms)" in t
    owner = t.index("historia: 'KokiriForest_Meet_DekuTree_MeetDekuTree_01' empieza aquí (dueño) en KokiriForest_Child")
    seen = t.index("historia: vista KokiriForest_Meet_DekuTree_MeetDekuTree_01")
    over = t.index("historia: 'KokiriForest_Meet_DekuTree_MeetDekuTree_01' acaba aquí")
    assert owner < seen < over
    assert "fin de evento 'Dm_Kokiri_0075'" in t
    # the noise is counted, not printed, and there is no co-op problem in this run
    assert r.counted["objects"] > 1000
    assert not [s for s in t if s.startswith(("ERROR", "FALLO", "AVISO"))]
    assert len(r.rtt) == 38
    times = [x[0] for x in r.timeline]
    assert times == sorted(times)


def test_downed_and_revived_log():
    r = load("caer-pareja_A_eden_log.txt")
    t = texts(r)
    down = t.index("caído: alive -> downed (vida 0/12)")
    asked = t.index("revivir: REVIVE de P2")
    up = next(i for i, s in enumerate(t) if s.startswith("caído: reviving -> alive: revived (by P2"))
    assert down < asked < up
    assert "caído/revivir" in r.switches


def test_render_is_compact():
    r = load("historia-largo_A_eden_log.txt")
    out = rr.render(r, max_lines=5)
    assert out.startswith("== historia-largo_A_eden_log.txt: ")
    assert "ping: mediana 8 ms" in out
    assert "líneas más (--max)" in out
    assert len(out.splitlines()) < 25


LINES = r"""[  10.625232] Core <Info> X:\src\lichii\hook\coop_hook.cpp:787:Log: coop: welcomed as player 1 on 192.0.2.7:27561
[  11.000000] Core <Info> X:\src\lichii\hook\coop_hook.cpp:799:Log: coop: player 2 'Ana' joined
[  12.000000] Core <Info> X:\src\lichii\hook\coop_svc.cpp:70:operator (): coop: puppet: P2: create CoopPuppet_Child requested (returned 0), handle slot 0x1
[  13.000000] Core <Info> X:\src\lichii\hook\coop_objects.cpp:1293:Share: coop: npc: talking to Npc_Kokiri_003_Child 0x214d869fd0 (owner P2, claimed)
[  14.000000] Core <Info> X:\src\lichii\hook\coop_objects.cpp:1300:Share: coop: npc: talk over, claim let go
[  15.000000] Core <Error> X:\src\lichii\hook\coop_story_game.cpp:98:Glue: coop: story: LICHII_COOP_STORY_LIST x.txt: no such file; story events are not shared
[  16.000000] Core <Error> core\file_sys\content_archive.cpp:10:Load: not a co-op line
[  17.000000] Core <Info> X:\src\lichii\hook\coop_hook.cpp:1128:Log: coop: player 2 'Ana' left
[  18.000000] Core <Info> X:\src\lichii\hook\coop_hook.cpp:1118:Run: coop: connection lost; reconnecting (nothing from the server for 5000 ms, x)
[  18.500000] Core <Info> X:\src\lichii\hook\coop_hook.cpp:1118:Run: coop: connection lost; reconnecting (nothing from the server for 5000 ms, x)
""".splitlines()


def test_hand_made_lines():
    r = rr.parse(LINES, "hecho")
    t = texts(r)
    assert t[0] == "conectado como P1 (192.0.2.7:27561)"
    assert "+ entra P2 'Ana'" in t and "- sale P2 'Ana'" in t
    assert "FALLO: no se pudo crear CoopPuppet_Child de P2 (returned 0)" in t
    assert "PNJ: hablando con Npc_Kokiri_003_Child (owner P2, claimed)" in t
    assert "PNJ: fin de la charla, PNJ soltado" in t
    assert any(s.startswith("FALLO: historia: LICHII_COOP_STORY_LIST") for s in t)
    assert r.emulator_errors["content_archive.cpp"] == 1
    # the same line twice in a row is one line with a count
    assert r.timeline[-1][1] == "AVISO: conexión perdida; reconectando" and r.timeline[-1][2] == 2


# Lines of the test8 tree (orm/test8: story list, talk flags, world lists, NPC pose, puppet reveal, screen line, chat
# notices) and of the branches on their way (hearts and escape in orm/huida, age in orm/d17), in the hook's own words.
H = r"[  {t}] Core <Info> X:\src\lichii\hook\coop_hook.cpp:100:Log: "
NEW = [H.format(t=f"{20 + i}.000000") + s for i, s in enumerate([
    "coop: story: on (LICHII_COOP_STORY): 128 shared story events (built in): KokiriForest_Meet_DekuTree, ...",
    "coop: story: on (LICHII_ORM_STORY_EVENTS): 128 shared story events (built in): KokiriForest_Meet_DekuTree, ...",
    r"coop: lists: X:\kit\shared_dungeon_child.txt: 145 flags and keys added, 13 taken off, 19 once, 4 held in their area, 105 one-time keys",
    "coop: lists: LICHII_COOP_DUNGEON_LISTS x.txt: no such file; the lists are not used",
    "coop: talk: sharing 16 talk flags (16 new)",
    "coop: npc pose: on (townspeople copies held in ai hold mode 1, partial slots on)",
    "coop: age: on (LICHII_COOP_AGE): a mate's age change is followed (the reload asks the event)",
    "coop: camera found (1920x1080, clip y -1), markers on",
    "coop: puppet: P2 CoopPuppet_Child spawned after 310 ms: request 0x1, actor 0x2, state 4, at (1, 2, 3); parked until it has a pose and a fresh sample",
    "coop: puppet: P2 shown after 180 ms (new): its pose on it, sample 40 ms old, at (1, 2, 3)",
    "coop: puppet: P3 shown after 3000 ms (new): no pose yet, sample age not known, at (1, 2, 3); waited the most",
    "coop: puppet: P2 back after 6100 ms out of the plan: parked until it has a pose and a fresh sample",
    "coop: puppet: P2 show -> hide (riding)",
    "coop: puppet: P2 stands on our Link after an event: drawn 0.60 to its side (0.6, 0, 0), its sample kept",
    "coop: puppet: P2 drawn on its sample again",
    "coop: pose: P2 no pose for 2500 ms: last pose held, blending into its own animation over 500 ms",
    "coop: pose: P2 poses back after 2900 ms: blended in over 200 ms",
    "coop: story: 'KokiriForest_WarpFromInsideTheDekuTree' is solo: only we see it, its flags are shared",
    "coop: story: STORY_EVENT start received from P2 ('Get_StoneOfFire_Complete' in DeathMountainCrater): ignored, it is solo here (only who starts it sees it)",
    "coop: story: 'KokiriForest_Meet_DekuTree' runs here again; not shared again",
    "coop: talk: 0x502f6cea KokiriForest_Npc_Kokiri_Mido_Child_Blocker 1 -> 0 in KokiriForest_Child",
    "coop: flags: talk flag 0x502f6cea KokiriForest_Npc_Kokiri_Mido_Child_Blocker = 0 waits: in view here",
    "coop: flags: talk flag 0x502f6cea KokiriForest_Npc_Kokiri_Mido_Child_Blocker = 0 written: out of the scene",
    "coop: npc pose: sending Npc_Kokiri_Mido_Child 0x1 (id 0x2, epoch 3)",
    "coop: npc pose: copies following their owner's pose: Npc_Kokiri_003_Child",
    "coop: notice: B ha caído: ve a su lado y mantén A",
    "coop: downed: screen: 'Mantén A para revivir a B 40 %'",
    "coop: screen: connection lost, shown on the screen line after 1 s",
    "coop: screen: connection back after 7400 ms",
    "coop: hearts: max life 12 -> 16 (P2's change, ...)",
    "coop: escape: group exit: P2 is in FinalBattleField, we are still in CrumblingGanonCastle: following",
    "coop: age: Sequence_IsChildhood <- 0 (queued)",
    "coop: boss: respawn: back in the boss room of InsideTheDekuTree (try 1)",
    "coop: downed: alive -> fairy: our own fairy (life 0 of 12, death word 0x43af)",
])] + [r"[  60.000000] Core <Error> X:\src\lichii\hook\coop_hook.cpp:1615:Start: coop: X:\kit\emulator\user\config\coop.ini:3: color must be #RRGGBB, as #2EB83C"]


def test_test8_lines():
    r = rr.parse(NEW, "test8")
    t = texts(r)
    assert r.story_events == 128
    assert {"cinemáticas", "listas del mundo", "charlas", "pose de PNJ", "edad", "etiquetas"} <= set(r.switches)
    assert "listas: shared_dungeon_child.txt: 145 añadidas, 13 quitadas" in t
    assert any(s.startswith("FALLO: listas: LICHII_COOP_DUNGEON_LISTS x.txt") for s in t)
    assert "charlas: 16 banderas compartidas (16 nuevas)" in t
    assert not [s for s in t if "camera found" in s or "cámara encontrada" in s]  # once per load: counted only
    # puppet reveal, hide, side step, pose gaps
    assert "marioneta de P2 creada (CoopPuppet_Child, 310 ms)" in t
    assert "marioneta de P2 visible tras 180 ms (new)" in t
    assert "AVISO: marioneta de P3 visible tras 3000 ms (new), sin postura o posición reciente" in t
    assert "marioneta de P2 vuelve tras 6100 ms: escondida hasta tener postura" in t
    assert "marioneta de P2: show -> hide (riding)" in t
    assert "marioneta de P2 dibujada 0.60 m a un lado (encima de tu Link tras un evento)" in t
    assert "marioneta de P2 vuelve a su sitio" in t
    assert "postura de P2: sin posturas 2500 ms, se mantiene la última" in t
    assert "postura de P2: vuelven las posturas tras 2900 ms" in t
    # story: solo lines are not failures
    assert "historia: 'KokiriForest_WarpFromInsideTheDekuTree' es solo (solo la ve quien la empieza; sus banderas viajan)" in t
    assert any(s.startswith("historia: P2 empezó 'Get_StoneOfFire_Complete': ignored, it is solo here") for s in t)
    assert "historia: 'KokiriForest_Meet_DekuTree' otra vez aquí (no se repite fuera)" in t
    # talk flags and their wait
    assert "charla: KokiriForest_Npc_Kokiri_Mido_Child_Blocker 1 -> 0 (KokiriForest_Child)" in t
    assert "charla: KokiriForest_Npc_Kokiri_Mido_Child_Blocker = 0 espera (in view here)" in t
    assert "charla: KokiriForest_Npc_Kokiri_Mido_Child_Blocker = 0 escrita (out of the scene)" in t
    assert "pose de PNJ: se envía la de Npc_Kokiri_Mido_Child" in t
    assert "pose de PNJ: copias que siguen la del dueño: Npc_Kokiri_003_Child" in t
    # what the player reads, the connection line
    assert "chat: B ha caído: ve a su lado y mantén A" in t
    assert "pantalla: 'Mantén A para revivir a B 40 %'" in t
    assert "AVISO: pantalla: sin conexión, reconectando" in t
    assert "conexión de vuelta tras 7400 ms" in t
    # hearts, escape, age, boss respawn, own fairy
    assert "corazones: max life 12 -> 16 (P2's change, ...)" in t
    assert any(s.startswith("huida: group exit: P2 is in FinalBattleField") for s in t)
    assert "edad: Sequence_IsChildhood <- 0 (queued)" in t
    assert "jefe: vuelta a la sala: back in the boss room of InsideTheDekuTree (try 1)" in t
    assert any(s.startswith("caído: alive -> fairy") and s.endswith("(vida 0/12)") for s in t)
    assert "FALLO: color mal escrito en coop.ini (#RRGGBB, as #2EB83C)" in t
    # the only problems are the ones meant: the list error, the late puppet, the lost connection, the colour
    problems = [s for s in t if s.startswith(("ERROR", "FALLO", "AVISO"))]
    assert len(problems) == 4, problems


def test_main_writes_out(tmp_path, capsys):
    log = tmp_path / "eden_log.txt"
    log.write_text("\n".join(LINES), encoding="utf-8")
    out = tmp_path / "resumen.txt"
    assert rr.main([str(log), str(tmp_path / "missing.txt"), "--out", str(out)]) == 0
    text = out.read_text(encoding="utf-8")
    assert "== eden_log.txt: 10 líneas" in text
    assert "missing.txt: no existe" in text
    assert "conectado como P1" in capsys.readouterr().out
