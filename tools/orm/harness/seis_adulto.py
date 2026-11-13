"""PLAN_100 box 6 ("con seis") of blocks 11 to 20: one game and five bots (docs/BOTS.md, F14) walk each block.

    python -I tools/orm/harness/seis_adulto.py          # (re)writes escenarios/seis-<block>.A.txt/.bots.txt
    python -I tools/orm/harness/seis_adulto.py --check  # exit 1 if a file differs from the tables

One scenario per block, `seis-<clave>` (the block's key in tools/avance_mapa.toml), plus `seis-ganon-huida` (block
20's escape, another scene: a game takes one warp per start). Each is one game (A) and five bots (remake-bot): A loads
the block's entry save (`#! saves entrada-adulto-<clave>`, tools/orm/saves/recetas), warps into the block's scene and
walks its key rooms with `place` (the rooms and spots of tools/harness/recorridos_adulto.py, imported, not copied); the
bots start in a scene outside and follow A in (`follow 1 3`), each at its own angle. In the first room A falls
(`life 0`) and bots 3 and 4 walk to it and lift it; then bot5 sends a room's result flag (A's game, the host, must
take it 0 -> 1) and bot1 goes down where it stands and bot2 lifts it (A's game sees both); where the block has a
boss, A goes into the boss room with the five behind it (the intro, no fight). Block 11 adds the change of era: bot4
turns child and adult again (A's game makes its puppet again each time).

The bots cannot see A's marks: they wait for A in the block's scene (`until 1 scene`), for its fall (`until 1 down`)
and for it to be up (`until 1 up`); the rest is timed from there while A walks the rooms, 20 s each. A's `#!` lines
read A's log; the .bots.txt's letterless ones the bots' log, `server` the server's (r03_pareja of orm/rama-unica; the
same layout as blocks 1-10, docs/SEIS_BLOQUES_1_10.md). Doc: docs/SEIS_BLOQUES_11_20.md. Edit the tables
below, not the files (test_seis_adulto.py checks they match).
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import recorridos_adulto as ra  # noqa: E402  (the rooms, spots, flags and bosses of the adult routes)

ESC = HERE / "escenarios"
RECETAS = HERE.parent / "partidas" / "recetas"
SWITCHES = ra.SWITCHES + ",LICHII_COOP_DOWNED=1,LICHII_COOP_DEBUG_LIFE=1"
BOTS = 5
FOLLOW_M = 3             # the bots keep this far from A, each at its own angle
ARRIVE = 1200            # after A's arrival: the five follow A into the scene (each loads 1 s)
SETTLE, STAY = 300, 900  # per room: the place, then the bots walk up (4 m/s; over 60 m they jump to A)
FALL = 2400              # A down: bots 3 and 4 come (a few metres), hold A 3.5 s, the game's revive takes 6 s
AFTER_FALL = 600
INTRO = 2400             # the boss's intro after A walks in
AFTER_BOSS = 900         # the six in the boss room after the intro
# The bots' clock after A is up again (seconds).
FLAG_AT = 10             # bot5 sends the room's flag
DOWN_AT = 20             # bot1 goes down where it stands; bot2 walks to it and holds A (3.5 s, then 6 s getting up)
UP_WAIT = 45             # bot1 lies until then (the revive is over by about 25 s), then follows A again
AGE_AT, AGE_FOR = 30, 15  # block 11: bot4 is a child for this long (the sword back in the pedestal), then adult


@dataclass
class Bloque:
    route: str           # recorridos_adulto Route.name = the block's key (avance_mapa.toml [[bloque]] clave)
    leg: int             # the route's leg walked (0-based): its scene, start and age
    rooms: list          # the key rooms of that leg, by name, in the order A walks them (A falls in the first)
    flag: str            # a result flag of one of the leg's rooms that bot5 sends (shared; 0 in the entry save)
    outside: str         # the scene the bots start in, outside the block's scene
    boss_leg: int | None = None  # the leg whose boss room ends the walk (the same scene)
    note: str = ""


BLOQUES = [
    Bloque("tiempo", 0, ["nave", "puerta", "pedestal", "despertar"], "Market_Child_TimeTempleDoor_Opened",
           "HyruleField_Adult", note="the change of era: bot4 turns child (the sword back in the pedestal), then adult"),
    Bloque("bosque", 0, ["entrada", "vestibulo", "patio-oeste", "arco", "llave-jefe", "sotano"],
           "ForestTemple_Room06_Cleard", "SacredForestMeadow_Adult", boss_leg=1),
    Bloque("fuego", 0, ["entrada", "lava", "bailarina1", "cima", "martillo", "darunia"],
           "FireTemple_Room03_FlareDancerisDead", "DeathMountainCrater_Adult", boss_leg=1),
    Bloque("hielo", 0, ["hielo-rojo", "caverna", "muro-rojo", "rupias", "mapa", "lobo-blanco"],
           "IceCavern_Room00_RedIce_Melted", "ZoraFountain_Adult"),
    Bloque("agua", 0, ["centro-alto", "oeste", "pilar", "centro-fondo", "link-oscuro", "gancho-largo", "antesala"],
           "WaterTemple_Room05_Cleared", "HyruleField_Adult", boss_leg=1),
    Bloque("pozo", 0, ["vestibulo", "puerta07", "ataudes", "telaranas", "sotano", "dead-hand"],
           "BottomOfTheWell_Room07_UnLock", "KakarikoVillage_Child",
           note="Dead Hand's room ends the walk (the hook has no boss room for it)"),
    Bloque("sombra", 0, ["entrada", "calavera", "dead-hand", "b2f-a", "barca", "antesala"],
           "ShadowTemple_Level_Room_01_Clear_A", "Graveyard_Adult", boss_leg=1),
    Bloque("gerudo", 1, ["carpintero013", "carpintero014", "carpintero015", "carpintero016", "puerta-campo"],
           "GerudoFortress_Saved_Npc_Hylia_013_Adult", "GerudoValley_Adult",
           note="the fortress and its four carpenters (the valley, the Training Ground and the desert: their own legs)"),
    Bloque("espiritu", 1, ["bloque", "adulto-entrada", "rupias", "luces", "estatua", "espejos", "nabooru"],
           "SpiritTemple_Room16_Unlocked", "DesertColossus_Adult", boss_leg=2,
           note="the adult half (the child half is its own leg: one warp, one age per start)"),
    Bloque("ganon", 1, ["vestibulo", "p1-barrera", "p2-barrera", "p3-barrera", "torre", "torre24"],
           "InsideGanonCastle_Dungeon_BarrierRemove_Forest", "GanonCastle", boss_leg=2,
           note="the trials' barriers, the tower and Ganondorf; the escape is seis-ganon-huida"),
]

# Block 20's escape: A is already out of the collapsing castle (on the field of the last fight); the five come out
# after it one by one, the last one lifted inside by a mate first; then the six go into Ganon's ring.
HUIDA_FROM, HUIDA_TO, HUIDA_START = "CrumblingGanonCastle", "FinalBattleField", "FinalBattleField_Entrance"
HUIDA_RING = (11.80, 100.10, -1.10)   # coop_boss.cpp room 9's `arrive` (GanonStage's StartPos), inside the ring
HUIDA_SWITCHES = ("LICHII_COOP_PUPPET=1,LICHII_COOP_DEBUG_BOSS=1,LICHII_COOP_STORY=1,LICHII_COOP_DOWNED=1,"
                  "LICHII_COOP_GROUP_EXIT=1")
HUIDA_OUT = {1: 5, 2: 10, 3: 15}      # bot -> seconds after A is out; bot4 after lifting bot5, bot5 last
HUIDA_WAIT_OUT, HUIDA_RING_AT = 600, 3000


def route_of(b: Bloque) -> ra.Route:
    return next(r for r in ra.ROUTES if r.name == b.route)


def rooms_of(b: Bloque) -> list:
    by = {r.name: r for r in route_of(b).legs[b.leg].rooms}
    return [by[n] for n in b.rooms]


def boss_of(b: Bloque):
    return route_of(b).legs[b.boss_leg].boss if b.boss_leg is not None else None


def puppet(age: str) -> str:
    return "CoopPuppet_Child" if age == "child" else "CoopPuppet_Adult"


def pad(cmd: str, comment: str, col: int = 44) -> str:
    return cmd + " " * max(1, col - len(cmd)) + "# " + comment


def load(scene: str, start: str, age: str) -> list:
    """recorridos_adulto's load (title, Continue, the warp in the event after the load), up to the arrival."""
    return ra.LOAD.split("mark llegada")[0].format(scene=scene, start=start, age=age).splitlines()


def marks_of(b: Bloque) -> list:
    rooms = [f"r{i:02d}-{r.name}" for i, r in enumerate(rooms_of(b), 1)]
    return ["start", "title", "llegada", rooms[0], "caida", "levantado"] + rooms[1:] + (
        ["jefe"] if boss_of(b) else []) + ["fin"]


def script_a(b: Bloque) -> list:
    leg = route_of(b).legs[b.leg]
    lines = load(leg.scene, leg.start, leg.age)
    lines += ["mark llegada", pad(f"wait {ARRIVE}", f"the five bots follow A into {leg.scene}", 24)]
    for i, r in enumerate(rooms_of(b), 1):
        m = f"r{i:02d}-{r.name}"
        lines += [pad(f"mark {m}", r.label, 24), f"place {ra.fmt(r.at)}", f"wait {SETTLE}"]
        if i == 1:
            lines += ["mark caida              # A falls here: bots 3 and 4 come and hold A beside it",
                      "life 0",
                      pad(f"wait {FALL}", "their 3.5 s and the game's 6 s revive", 24),
                      "mark levantado          # then bot5 sends the flag and bot1 goes down, bot2 lifts it",
                      f"wait {AFTER_FALL}"]
        else:
            lines.append(f"wait {STAY}")
    boss = boss_of(b)
    if boss:
        lines.append("mark jefe               # into the boss room, the five behind A")
        if boss.door:
            lines += [f"place {ra.fmt(boss.door)}", "wait 120",
                      f"goto {boss.arrival[0]:.2f} {boss.arrival[2]:.2f} 1.5 900"]
        else:
            lines.append(f"place {ra.fmt(boss.arrival)}")
        lines.append(pad(f"wait {INTRO}", "the intro (our cutscene: the puppets out of sight meanwhile)", 24))
        lines.append(pad(f"wait {AFTER_BOSS}", "the six in the boss room; no fight here (jefe-* and recorrido-*)", 24))
    lines.append("mark fin")
    return lines


def header_common(switches: str, timeout: int, save: str) -> list:
    """A's header lines every scenario of here has. One game, five bots, `#! puppets` and min-fps 25 come from the
    name (r03_pareja's seis-* defaults); transport, bot-arg and the server's lines sit in the .bots.txt (as blocks
    1-10's: the pair lint reads only the .A/.B files)."""
    return ["# 1 game, 5 bots, puppets and min-fps 25 come from the name (seis-*); transport and bot-arg are in the .bots.txt.",
            "#! noload", "#! start coop: test warp done", f"#! saves {save}", f"#! switches {switches}",
            f"#! timeout {timeout}",
            "#! expect A coop: test warp done",
            "#! never A coop: test goto timed out",
            "#! never A coop: test place .*: failed",
            "#! never A coop: test warp to .* did not happen",
            "#! never A coop: server refused",
            rf"#! count A coop: player \d+ 'bot\d' joined >= {BOTS} within boot..",
            r"#! never A coop: puppet: P\d+ CoopPuppet_\w+ not spawned after",
            r"#! expect A coop: downed: P\d is down \(PLAYER_STATUS dead\)",
            r"#! expect A coop: downed: P\d is up again",
            "#! never A group defeat: GROUP_DEFEAT",
            "#! never A -> game_over"]


def header_a(b: Bloque) -> list:
    route, leg = route_of(b), route_of(b).legs[b.leg]
    marks, boss = marks_of(b), boss_of(b)
    pup = puppet(leg.age)
    head = [f"# seis-{b.route} (ORM, docs/SEIS_BLOQUES_11_20.md, PLAN_100 F14, box 6 of block {route.block}. "
            f"{route.title}):",
            f"# one game and five bots. A warps into {leg.scene} ({leg.start}, {leg.age}) and walks its key rooms with",
            f"# place (recorridos_adulto.py's); the bots (seis-{b.route}.bots.txt) come in from {b.outside} after it",
            "# and follow it 3 m off. In the first room A falls and bots 3 and 4 lift it; then bot5 sends a room's flag",
            "# (A's game takes it 0 -> 1) and bot1 goes down, bot2 lifts it"
            + (f"; last, the boss room with six ({boss.actor})." if boss else "."),
            *([f"# {b.note[0].upper() + b.note[1:]}."] if b.note else []),
            "# Generated by tools/harness/seis_adulto.py: edit its tables, not this file. Run (orm/rama-unica's harness):",
            f"#   python -I tools/orm/harness/r03_pareja.py --scenario seis-{b.route} "
            "--scen-dir tools/orm/harness/escenarios"]
    head += header_common(SWITCHES, ra.ticks(script_a(b)) // 60 + 240, f"entrada-adulto-{b.route}")
    head += [r"#! never A coop: boss: Link's actor (is gone|not found)",
             r"#! expect A coop: lists: .*shared_dungeon_adult\.txt: \d+ flags and keys added within boot..",
             rf"#! count A coop: puppet: P\d+ {pup} spawned >= {BOTS}",
             "#! expect A coop: downed: alive -> downed within caida..levantado",
             r"#! expect A downed -> reviving: revive requested by P\d within caida..levantado",
             r"#! expect A reviving -> alive: revived \(by P\d within caida..levantado"]
    for i, r in enumerate(rooms_of(b), 1):
        m = f"r{i:02d}-{r.name}"
        head.append(f"#! expect A coop: test place {ra.esc(r.at)}: done within {m}..{ra.nxt(marks, m)}")
    head += [rf"#! expect A coop: flags debug: \w+ 0x{ra.h(b.flag)} \(entry \d+\) 0 -> 1 within caida..fin",
             rf"#! never A coop: flags debug: \w+ 0x{ra.h(b.flag)} \(entry \d+\) 1 -> 0"]
    if b.route == "tiempo":
        head.append(rf"#! expect A coop: puppet: P\d+ {puppet('child')} spawned within caida..fin")
    if boss:
        head += [f"#! expect A coop: boss: in the boss room of {boss.scene}: {boss.actor} within jefe..fin",
                 r"#! expect A coop: boss: (intro started \(|no intro: the fight has begun here) within jefe..fin",
                 "#! never A coop: boss: boss beaten"]
    return head


def render_a(b: Bloque) -> str:
    return "\n".join(header_a(b) + script_a(b)) + "\n"


def bots_header(scene: str, down: int = 1, lifter: int = 2) -> list:
    """The bots' file's directives: the run options, the joins, the follow into A's scene, bot `down` lifted by bot
    `lifter`, everyone's script done, the server's joins."""
    return ["#! transport ws",
            "#! bot-arg -real -look auto",
            rf"#! count joined as player \d+ >= {BOTS}",
            r"#! never script line \d+",
            r"#! never until P1 .*timed out",
            "#! never revive: nobody down",
            rf"#! count bot\d: scene \S+ -> {scene} \(loading >= {BOTS}",
            f"#! expect bot{down}: alive -> downed",
            rf"#! expect bot{down}: downed -> reviving: revive requested by P[2-6]",
            rf"#! expect bot{down}: reviving -> alive: revived \(by P[2-6]\)",
            rf"#! expect bot{lifter}: A held .* REVIVE sent to P[2-6]",
            rf"#! count bot\d: script done >= {BOTS}",
            rf"#! count server player \d+ joined: >= {BOTS + 1}",
            "#! never server group defeat: all",
            "#! never server refused a connection"]


def render_bots(b: Bloque) -> str:
    leg = route_of(b).legs[b.leg]
    out = [f"# seis-{b.route}, the five bots (remake-bot -script, docs/BOTS.md): from {b.outside} they follow A into "
           f"{leg.scene};",
           "# bots 3 and 4 lift A when it falls; bot5 sends the flag; bot1 goes down and bot2 lifts it."
           + (" bot4 turns child and adult again." if b.route == "tiempo" else ""),
           "# Letterless directives read the bots' log, 'server' the server's.",
           "# Generated by tools/harness/seis_adulto.py: edit its tables, not this file."]
    out += bots_header(leg.scene)
    out += [r"#! expect bot[34]: A held .* REVIVE sent to P1",
            rf"#! expect bot5: flags: FLAG_SET sent \(0x{ra.h(b.flag)}\[0\] kind \d+ = 1\)"]
    out += [f"scene {b.outside}", f"age {leg.age}", f"follow 1 {FOLLOW_M}",
            f"until 1 scene {leg.scene} 400",
            pad("until 1 down 400", "A's life 0 in its first room (mark caida)"),
            pad("@3-4 revive 1", "walk to it, hold A 3.5 s"),
            pad("until 1 up 120", "the game's revive takes 6 s"),
            f"@3-4 follow 1 {FOLLOW_M}",
            f"wait {FLAG_AT}",
            pad(f"@5 flag {b.flag} bool 1", "a room's result: the host's game takes it", 60),
            "@5 mark flag",
            f"wait {DOWN_AT - FLAG_AT}",
            pad("@1 down", "where it stands"),
            "@1 mark down",
            pad("@2 revive any", "walks to it, holds A 3.5 s"),
            pad(f"@1 wait {UP_WAIT}", "lifted 6 s after the request (a revived bot stands)"),
            pad(f"@1-2 follow 1 {FOLLOW_M}", "after A again")]
    if b.route == "tiempo":
        out += [f"@4 wait {AGE_AT - DOWN_AT}",
                pad("@4 age child", "the sword back in the pedestal: a child's puppet in A"),
                "@4 mark child",
                f"@4 wait {AGE_FOR}",
                pad("@4 age adult", "and drawn again"),
                "@4 mark adult"]
    out += ["wait 10", "mark done"]
    return "\n".join(out) + "\n"


# ---- block 20's escape

def script_huida() -> list:
    return load(HUIDA_TO, HUIDA_START, "adult") + [
        "mark llegada            # A is out of the collapsing castle, first",
        f"wait {HUIDA_WAIT_OUT}",
        "mark salen              # the five come out after A one by one (the last one lifted inside first)",
        f"wait {HUIDA_RING_AT}",
        "mark ruedo              # into Ganon's ring, the five behind A",
        f"place {ra.fmt(HUIDA_RING)}",
        f"wait {SETTLE}",
        pad(f"wait {INTRO}", "the intro (our cutscene: the puppets out of sight meanwhile)", 24),
        pad(f"wait {AFTER_BOSS}", "the six in the ring; no fight here (final-ganon-*)", 24),
        "mark fin"]


def render_huida_a() -> str:
    head = ["# seis-ganon-huida (ORM, docs/SEIS_BLOQUES_11_20.md, PLAN_100 F14, box 6 of block 20, the escape):",
            f"# A lands in {HUIDA_TO} (out of the collapsing castle; a game takes one warp per start, so the castle's",
            f"# walk is seis-ganon); the five bots (seis-ganon-huida.bots.txt) are still in {HUIDA_FROM} and come",
            "# out after A one by one; bot5 goes down inside and bot4 lifts it before they leave. A must see the five",
            "# come out, never go back in after them (it is out), then go into Ganon's ring with the five behind it",
            "# (the intro, no fight).",
            "# Generated by tools/harness/seis_adulto.py: edit its tables, not this file. Run (orm/rama-unica's harness):",
            "#   python -I tools/orm/harness/r03_pareja.py --scenario seis-ganon-huida "
            "--scen-dir tools/orm/harness/escenarios"]
    head += header_common(HUIDA_SWITCHES, ra.ticks(script_huida()) // 60 + 240, "entrada-adulto-ganon")
    head += [rf"#! count A coop: puppet: P\d+ CoopPuppet_Adult spawned >= {BOTS} within start..ruedo",
             "#! never A (group exit|escape): .* following",
             f"#! expect A coop: test place {ra.esc(HUIDA_RING)}: done within ruedo..fin",
             f"#! expect A coop: boss: in the boss room of {HUIDA_TO}: Enemy_DungeonBoss_Ganon within ruedo..fin",
             r"#! expect A coop: boss: (intro started \(|no intro: the fight has begun here) within ruedo..fin",
             "#! never A coop: boss: boss beaten"]
    return "\n".join(head + script_huida()) + "\n"


def render_huida_bots() -> str:
    out = [f"# seis-ganon-huida, the five bots: in {HUIDA_FROM} until A is out in {HUIDA_TO}; bot5 goes down there",
           "# and bot4 lifts it; bots 1-3 come out 5, 10 and 15 s after A, bot4 after lifting bot5, bot5 last.",
           "# Letterless directives read the bots' log, 'server' the server's.",
           "# Generated by tools/harness/seis_adulto.py: edit its tables, not this file."]
    out += bots_header(HUIDA_TO, down=5, lifter=4)
    out += [f"#! expect bot5: alive -> downed at .* in {HUIDA_FROM}",
            rf"#! never bot\d: scene {HUIDA_TO} -> {HUIDA_FROM}"]
    out += [f"scene {HUIDA_FROM}", "age adult", "at 0,100,0 2", f"until 1 scene {HUIDA_TO} 400",
            pad("@5 down", "inside the collapsing castle"),
            "@5 mark down",
            pad("@4 revive any", "lifts it before it goes"),
            "@4 wait 5"]
    out += [f"@{n} wait {s}" for n, s in HUIDA_OUT.items()]
    out += [pad(f"@5 wait {UP_WAIT}", "lifted 6 s after the request"),
            pad(f"follow 1 {FOLLOW_M}", "out: into A's scene"),
            "mark out",
            "wait 10",
            "mark done"]
    return "\n".join(out) + "\n"


def files() -> dict:
    out = {}
    for b in BLOQUES:
        out[ESC / f"seis-{b.route}.A.txt"] = render_a(b)
        out[ESC / f"seis-{b.route}.bots.txt"] = render_bots(b)
    out[ESC / "seis-ganon-huida.A.txt"] = render_huida_a()
    out[ESC / "seis-ganon-huida.bots.txt"] = render_huida_bots()
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="only compare; exit 1 if a file is missing or differs")
    a = ap.parse_args()
    bad = 0
    for path, text in files().items():
        old = path.read_text(encoding="utf-8") if path.exists() else None
        if old == text:
            continue
        bad += 1
        if a.check:
            print(f"differs: {path.name}")
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.name}")
    return 1 if a.check and bad else 0


if __name__ == "__main__":
    sys.exit(main())
