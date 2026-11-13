"""The meca-* scenario pairs of the ADULT dungeons (docs/MECANISMOS_ADULTO.md,
families F2 switches, F4 blocks, F5 keys and locks, F7 torches, F8 breakables, F9 cleared rooms).

One table (SCENARIOS) -> escenarios/meca-<kind>-<dungeon>.A.txt and .B.txt. Each scenario is a list of phases; each
phase is one mechanism seen two ways where it can be:
  flag  NAME [shared]  A queues the room's Bool (the game's own record that the switch, torch, lock or room is done):
                       shared -> B gets it live ('bool 0x.. 0 -> 1'); not shared (the adult list's unshare: lines, the
                       issue 149 reset records) -> B never logs it.
  key   KEY  ACTOR     A queues a placement's 64-bit key (red ice, cracked wall, hammer pillar, falling floor, room enemy
                       counter, torch): B's key table gets it ('key64 0x.. 0 -> 1'). Needs the `key` test command
                       (daurosk branch orm/meca-adulto).
  lock  ...            A opens a small-key door for real: a key given (LICHII_COOP_CHEATS=1, the count is the group's),
                       put in front of the lock, walk in, A. B: the lock 'broken by another player', the count 1 -> 0
                       once, never below 0. 'both' adds B opening the same door 1 s later: one key back in B.
  push  ...            A pushes a block for real (daurosk's deku-block pattern): WORLD_MOVE / WORLD_PLACE, B follows.
  step  ...            A stands on a floor switch for real: some flag of A's room goes 0 -> 1 and B gets one (exploratory:
                       which flag is the switch's is not mapped; the flag phase checks the named one).
  hit   ...            A hits a crystal switch or a hammer pillar for real with the hammer (given and put on ZR).
B lands where A does; it is put by the mechanism (`watch`) or stays at the entrance and enters later (`late`: put in
the room at the end, so the capture of mark 'tarde' shows the room as A left it). B's script carries A's marks at the
same times, so the 'within' windows read the same in both logs.

Run:  python tools/harness/meca_adulto.py            writes the files (and lists them)
      python tools/harness/meca_adulto.py --check    also checks every name and key against the romfs GameDataList
                                                     (ORM_ROMFS), the built-in list (coop_dungeon.cpp) and
                                                     shared_dungeon_adult.txt of a daurosk tree (--tree, default
                                                     ORM_TREE); needs `zstandard` (plain python, not -I)
Positions: Banc/<Dungeon>/*_Static.bcett.byml placements (romfs_read.py), metres; none were walked yet.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import orm_paths  # noqa: E402
sys.path.insert(0, str(orm_paths.TREE / "tools" / "romfs"))
import romfs_read  # noqa: E402  (murmur3 only unless --check; romfs_read.py is a tool of the source tree: ORM_TREE/tools/romfs)

OUT = HERE / "escenarios"
LIST = "shared_dungeon_adult.txt"
SWITCHES = f"LICHII_COOP_PUPPET=1,LICHII_COOP_DEBUG_FLAGS=1,LICHII_COOP_DUNGEON_LISTS={LIST}"
LOAD = [
    "wait 1200               # the title screen",
    "mark title",
    "press A",
    "wait 180",
    "press A",
    "wait 180",
    "press A",
    "wait 300",
    "press A                 # loads the save, then the warp",
    "wait 2400               # the save loads, then the warp lands (about 30 s)",
    "mark after",
    "wait 600                # the other game lands too: puppets made",
]
LOAD_TICKS = 1200 + 4 * 12 + 180 + 180 + 300 + 2400 + 600

# Dungeon -> (label in Spanish, scene, start position, age, SmallKeyNum flag or None).
DUNGEONS = {
    "bosque": ("Templo del Bosque", "ForestTemple", "ForestTemple", "adult"),
    "fuego": ("Templo del Fuego", "FireTemple", "FireTemple", "adult"),
    "hielo": ("Caverna de Hielo", "IceCavern", "IceCavern_FromZoraFountain", "adult"),
    "agua": ("Templo del Agua", "WaterTemple", "WaterTemple_FromLakeHylia", "adult"),
    "pozo": ("Pozo de Kakariko (de nino)", "BottomOfTheWell", "Well_KakarikoSide", "child"),
    "sombra": ("Templo de la Sombra", "ShadowTemple", "Entrance", "adult"),
    "gerudo": ("Campo de Entrenamiento Gerudo", "GerudoTrainingGround", "GerudoTrainingGround_FromGerudoFortress",
               "adult"),
    "espiritu": ("Templo del Espiritu", "SpiritTemple", "SpiritTemple_Entrance", "adult"),
    "ganon": ("Castillo de Ganon", "InsideGanonCastle", "Entrance", "adult"),
}
# Dungeon -> the synthetic save at the entry of its block (`#! saves`, tools/orm/saves/recetas, docs/ARNES.md).
SAVES = {"bosque": "12-bosque", "fuego": "13-fuego", "hielo": "14-hielo", "agua": "15-agua", "pozo": "16-pozo",
         "sombra": "17-sombra", "gerudo": "18-gerudo", "espiritu": "19-espiritu-adulto", "ganon": "20-ganon"}

# Bools B must NEVER get: the adult list's unshare: lines (a player's place or a fight that shuts a room) and the
# records of one player's reset try left out of the built-in list (issue 149, daurosk coop_dungeon.cpp).
NOT_SHARED_ISSUE_149 = {"FireTemple_Room08_SwitchWasReset", "FireTemple_Room10_SwitchWasReset",
                        "FireTemple_Room13_SwitchWasReset", "InsideGanonCastle_Dungeon_Room08_CandleWasReset"}


@dataclass
class Phase:
    kind: str                 # flag key lock push step hit
    mark: str
    name: str = ""            # flag: the Bool's name; key/lock/push/hit: the actor's name
    key: int = 0              # key/lock/push/hit: the placement hash
    shared: bool = True       # flag
    at: tuple = ()            # lock/push/step/hit: the object's place (x, y, z)
    n: tuple = ()             # lock/push/hit: (dx, dz), the side A stands on (from the object) or the push direction
    both: bool = False        # lock: B opens the same door 1 s after A
    note: str = ""


@dataclass
class Scenario:
    name: str                 # meca-<kind>-<dungeon>
    dungeon: str
    family: str               # F2 F4 F5 F7 F8 F9
    what: str                 # one line, English (the file's header)
    phases: list = field(default_factory=list)
    late: bool = False        # B waits at the entrance and is put in the room at the end
    watch: tuple = ()         # B is put here (x, y, z) when the phases start
    keys: int = 0             # small keys given to A first (LICHII_COOP_CHEATS=1)
    hammer: bool = False      # the hammer given to A and put on ZR first


def F(mark, name, shared=True, note=""):
    return Phase("flag", mark, name=name, shared=shared, note=note)


def K(mark, key, actor, note=""):
    return Phase("key", mark, name=actor, key=key, note=note)


S = Scenario
SCENARIOS = [
    # ---------------------------------------------------------------- Templo del Bosque
    S("meca-ojo-bosque", "bosque", "F2", "eye switches: room 08's eye switch and room 02's gold eye, both room state",
      [F("ojo", "ForestTemple_Room08_EyeSwitch_Clear"), F("ojo2", "ForestTemple_Room02_GoldEyeSwitch_On"),
       F("ojo3", "ForestTemple_Level_Room_06_PressedEyeball")]),
    S("meca-suelo-bosque", "bosque", "F2", "a floor switch stood on for real, then room 17's switch flag",
      [Phase("step", "pisa", name="Obj_StepSwitch_A_01", key=0xa60352c96c7679c3, at=(17.13, 100.00, -17.16)),
       F("abierta", "ForestTemple_Room17_Switch_On_01")],
      watch=(20.6, 100.6, -17.16)),
    S("meca-bloque-bosque", "bosque", "F4", "the shortcut block pushed for real toward its goal (+z, the hole), then "
      "its 'cleared' record", [Phase("push", "empuja", name="Obj_MovableBox_A_05", key=0xe4280f8c2cf4014b,
                                     at=(44.69, 105.25, -31.62), n=(0.0, 1.0),
                                     note="goal Obj_Gimmick_AnswerPosAnchor fcdd055246a7dfc4 at z -29.11"),
                               F("hecho", "ForestTemple_Clear_MovableBox_Shortcut")],
      watch=(48.2, 105.85, -31.62)),
    S("meca-llave-bosque", "bosque", "F5", "a small-key door opened for real with B 3.5 m back, then room 05's lock "
      "record", [Phase("lock", "abre", name="Obj_DoorChainLock_A_01", key=0xa5743ae24632f7d6,
                       at=(-25.89, 103.50, 0.00), n=(1.0, 0.0)),
                 F("registro", "ForestTemple_Room05_LockOpne")],
      watch=(-22.4, 104.1, 0.0), keys=1),
    S("meca-llave-ambos-bosque", "bosque", "F5", "the same small-key door opened by A and, 1 s later, by B standing at it "
      "(its own lock left there while its Link is within 2.5 m): two keys go, one comes back in B",
      [Phase("lock", "abre", name="Obj_DoorChainLock_A_01", key=0xa5743ae24632f7d6, at=(-25.89, 103.50, 0.00),
             n=(1.0, 0.0), both=True)],
      keys=2),
    S("meca-antorchas-bosque", "bosque", "F7", "torches lit (the played gimmick events of rooms 17, 25 and 27: the "
      "chest, the shutter, the candles)",
      [F("enciende", "HavePlayedEvent.ForestTemple_Room17_TBoxAppear_CandleLight"),
       F("enciende2", "HavePlayedEvent.ForestTemple_Room25_CandleLight_ShutterOpen"),
       F("enciende3", "HavePlayedEvent.ForestTemple_Room27_CandleLight")]),
    S("meca-limpia-bosque", "bosque", "F9", "room 06 cleared (its Bool and an enemy counter's key), B entering later",
      [F("limpia", "ForestTemple_Room06_Cleard"), K("contador", 0x08b97cf063ef1790, "Obj_EnemyCounter_A_01"),
       F("sensor", "ForestTemple_Room09_In", shared=False)], late=True),
    # ---------------------------------------------------------------- Templo del Fuego
    S("meca-ojo-fuego", "fuego", "F2", "the eye switch (FireTemple_HitEyeSwitch_01) and its played event",
      [F("ojo", "FireTemple_HitEyeSwitch_01"), F("ojo2", "HavePlayedEvent.FireTemple_06_EyeSwitch")]),
    S("meca-cristal-fuego", "fuego", "F2", "a crystal switch hit for real with the hammer, then rooms 04 and 12's "
      "switch flags", [Phase("hit", "golpea", name="Obj_SwitchHit_A_04", key=0x944a40272bfdbb98,
                             at=(87.78, 133.08, 16.14), n=(-1.0, 0.0)),
                       F("abierta", "FireTemple_Room04_SwitchHit"), F("abierta2", "FireTemple_Room12_SwitchHit")],
      watch=(83.8, 133.7, 16.14), hammer=True),
    S("meca-suelo-fuego", "fuego", "F2", "a floor switch stood on for real, then the iron-boots switch record",
      [Phase("step", "pisa", name="Obj_StepSwitch_A_01", key=0x0d6ca15dd623cd11, at=(34.00, 105.30, -42.13)),
       F("abierta", "FireTemple_BootStepSwitch_03")],
      watch=(34.0, 105.9, -38.6)),
    S("meca-bloque-fuego", "fuego", "F4", "the dropped blocks' records (FireTemple_DropMovableBox_01/02) with B in "
      "the dungeon", [F("cae", "FireTemple_DropMovableBox_01"), F("cae2", "FireTemple_DropMovableBox_02")]),
    S("meca-llave-fuego", "fuego", "F5", "a small-key door opened for real (A_04 lock: its model faces the door, so "
      "A stands on the lock's own side), then a key that drops from an event",
      [Phase("lock", "abre", name="Obj_DoorChainLock_A_04", key=0x3509563663200a0c, at=(-44.53, 147.04, 8.51),
             n=(0.0, -1.0), note="twin a475b6f70b208547 at z 8.81"),
       F("llave", "FireTemple_GetSmallKey_01")],
      watch=(-44.53, 147.64, 5.0), keys=1),
    S("meca-antorchas-fuego", "fuego", "F7", "the torch rooms solved (rooms 04 and 12), the room 13 fire",
      [F("enciende", "FireTemple_Room04_GimmickClear"), F("enciende2", "FireTemple_Room12_GimmickClear"),
       F("enciende3", "FireTemple_Room13_FireOn")]),
    S("meca-antorcha-tiempo", "fuego", "F7", "timed switches, each player's own: A's reset records of rooms 08, 10 and "
      "13 (issue 149, never shared), then room 08's switch done (shared)",
      [F("reinicia", "FireTemple_Room08_SwitchWasReset", shared=False),
       F("reinicia2", "FireTemple_Room10_SwitchWasReset", shared=False),
       F("reinicia3", "FireTemple_Room13_SwitchWasReset", shared=False),
       F("hecho", "FireTemple_Room08_SwitchPushed")]),
    S("meca-rompe-fuego", "fuego", "F8", "a hammer pillar struck for real (it falls and stays as a bridge), another "
      "pillar's key, a bomb wall's key and the pillar record; B entering later",
      [Phase("hit", "golpea", name="Obj_FireTemple_HammerSlipPillar_A_01", key=0xdf52cccb74511168,
             at=(7.00, 100.00, 5.54), n=(0.0, 1.0), note="the bottom of a stack of six at x 7, z 5.54 (y 100-103)"),
       K("pilar", 0xf4431985c472eb51, "Obj_FireTemple_HammerSlipPillar_A_01"),
       K("pared", 0x3449e93b49816fb2, "Obj_FireTemple_CrackWall_A_01"),
       F("registro", "FireTemple_Room18_HammerSlipPillarIsDead")],
      late=True, hammer=True),
    S("meca-limpia-fuego", "fuego", "F9", "rooms cleared: the Keese of room 10, an enemy counter's key, a Flare "
      "Dancer room; the arrival sensor never",
      [F("limpia", "FireTemple_Room10_KeeseDead_01"), K("contador", 0xa8906e79adeb7b16, "Obj_EnemyCounter_A_01"),
       F("limpia2", "FireTemple_Room24_FlareDancerisDead"),
       F("sensor", "FireTemple_ArrivePlayer_01", shared=False)], late=True),
    # ---------------------------------------------------------------- Caverna de Hielo
    S("meca-rompe-hielo", "hielo", "F8", "red ice melted (two placements' keys and room 01's records); B entering "
      "later", [K("hielo", 0x2c6b412d1c15fb39, "Obj_RedIce_A_01"), K("hielo2", 0x755920da9d4a6670, "Obj_RedIce_A_01"),
                F("registro", "IceCavern_Room01_RedIce_Melted_01"), F("registro2", "IceCavern_Room00_RedIce_Melted")],
      late=True),
    S("meca-bloque-hielo", "hielo", "F4", "the ice slider block pushed for real (no goal linked: -z guessed), then "
      "room 06's slider record", [Phase("push", "empuja", name="Obj_SliderBox_A_01", key=0xa6c217f99519dc47,
                                        at=(-26.00, 100.00, -27.14), n=(0.0, -1.0),
                                        note="no AnswerPosAnchor linked; an Area 4.6 m at -z"),
                                  F("hecho", "IceCavern_Room06_SliderBox_Move")],
      watch=(-29.5, 100.6, -27.14)),
    S("meca-limpia-hielo", "hielo", "F9", "room cleared (an enemy counter's key), a door unlocked by it; the room 02 "
      "way-in sensor never", [K("contador", 0x5ef49c004cac67df, "Obj_EnemyCounter_A_01"),
                              F("abierta", "IceCavern_Room04_Unlocked"),
                              F("sensor", "IceCavern_Room02_PassedThrough", shared=False)], late=True),
    # ---------------------------------------------------------------- Templo del Agua
    S("meca-ojo-agua", "agua", "F2", "eye switches: room 00's shot switch and room 21's shutter",
      [F("ojo", "WaterTemple_Room00_ShootSwitchOnce"), F("ojo2", "WaterTemple_Room21_OpenShutterBySwitch")]),
    S("meca-cristal-agua", "agua", "F2", "a crystal switch hit for real with the hammer, then room 08's shutter",
      [Phase("hit", "golpea", name="Obj_SwitchHit_A_06", key=0xb17c9f37828304c9, at=(-28.50, 111.50, 3.50),
             n=(0.0, -1.0)),
       F("abierta", "WaterTemple_Room08_OpenShutterBySwitch")],
      watch=(-28.5, 112.1, 0.0), hammer=True),
    S("meca-suelo-agua", "agua", "F2", "a floor switch stood on for real, then room 14's switch pressed (by boots, "
      "by a block)", [Phase("step", "pisa", name="Obj_StepSwitch_A_01", key=0xbd347dc936548bd8,
                            at=(-27.01, 100.00, 20.15)),
                      F("abierta", "WaterTemple_Room14_SwitchPushed"),
                      F("abierta2", "WaterTemple_Room14_SwitchPushedByBlock")],
      watch=(-23.5, 100.6, 20.15)),
    S("meca-llave-agua", "agua", "F5", "a small-key door opened for real (the water level of the room as the save "
      "has it), then room 02's key record", [Phase("lock", "abre", name="Obj_DoorChainLock_A_01",
                                                   key=0xa836443f9ff95827, at=(-9.55, 100.76, -4.50),
                                                   n=(-1.0, 0.0)),
                                             F("llave", "WaterTemple_Room02_GetSmallKey")],
      watch=(-13.0, 101.4, -4.5), keys=1),
    S("meca-rompe-agua", "agua", "F8", "a cracked wall bombed (its key); B entering later",
      [K("pared", 0x72a417d6550e0c31, "Obj_WaterTemple_CrackWall_A_01"),
       K("pared2", 0x7b542dd92b9be0fa, "Obj_WaterTemple_CrackWall_A_01")], late=True),
    S("meca-limpia-agua", "agua", "F9", "room 02 cleared (Bool and counter key); Dark Link's room shut behind the "
      "one who went in never reaches B", [F("limpia", "WaterTemple_Room02_Cleared"),
                                          K("contador", 0x59d5867377cce7ae, "Obj_EnemyCounter_A_01"),
                                          F("encierra", "WaterTemple_DarkLinkRoom_Locked", shared=False),
                                          F("sensor", "WaterTemple_Room20_EnterTheRoom", shared=False)], late=True),
    # ---------------------------------------------------------------- Pozo (child)
    # The Well's locks (meca-llave-pozo, meca-llave-ambos) are the child agent's (docs/MECANISMOS.md).
    S("meca-antorchas-pozo", "pozo", "F7", "a key torch lit (its placement key) and room 02's burned gimmick",
      [K("antorcha", 0x275da83c3d984241, "Obj_Well_CandleStick_A_03"),
       F("registro", "BottomOfTheWell_Level_Room_02_BurnedGimmick")]),
    S("meca-rompe-pozo", "pozo", "F8", "a rock broken (its key, room 09's record); the near-the-rock sensor never",
      [K("roca", 0x5a9784b0a3c5ee23, "Obj_BrokenRock_A_01"),
       F("registro", "BottomOfTheWell_Level_Room_09_RockDestoryed_01"),
       F("sensor", "BottomOfTheWell_Level_Room_09_NearRock", shared=False)], late=True),
    S("meca-limpia-pozo", "pozo", "F9", "rooms 00 and 08 cleared, an enemy counter's key; room 01's area never",
      [F("limpia", "BottomOfTheWell_Level_Room_00_A_Clear"), F("limpia2", "BottomOfTheWell_Level_Room_08_A_Clear"),
       K("contador", 0x05d15a1e1d206e4d, "Obj_EnemyCounter_A_09"),
       F("sensor", "BottomOfTheWell_Level_Room_01_InTheCenterArea", shared=False)], late=True),
    # The Well has no pushable block nor any floor switch (Banc/BottomOfTheWell: no Obj_MovableBox, Obj_SliderBox or
    # Obj_StepSwitch): its F4 and F3 boxes get the silver rupees instead (avance_mapa.toml, 16.D).
    S("meca-plata-pozo", "pozo", "F2", "the silver rupees of room 09 got (all five: the room's record), the silver "
      "token's two played events (it appears, it unlocks); B entering later",
      [F("plata", "BottomOfTheWell_Level_Room_09_A_SilverRupee_GetAll"),
       F("aparece", "HavePlayedEvent.BottomOfTheWell_SilverToken_Appear"),
       F("abre", "HavePlayedEvent.BottomOfTheWell_SilverToken_Unlock")], late=True),
    # ---------------------------------------------------------------- Templo de la Sombra
    S("meca-antorchas-sombra", "sombra", "F7", "the torches lit with Din's Fire (room 01 solved, the main gate of the "
      "adult side)", [F("enciende", "ShadowTemple_Room_01_GimmickClear"),
                      F("abierta", "ShadowTemple_Gimmick_Open_MainGate_Adult")]),
    S("meca-rompe-sombra", "sombra", "F8", "the falling floor of room 26 (its placement key and record), a cracked "
      "wall's key; B entering later", [K("suelo", 0x1b0636c687993cd9, "Obj_ShadowTemple_FallingFloor_A_01"),
                                       F("registro", "ShadowTemple_Level_Room_26_FloorFalled"),
                                       K("pared", 0x152cb120ab6d9ebe, "Obj_ShadowTemple_CrackWall_A_02")],
      late=True),
    S("meca-llave-sombra", "sombra", "F5", "a small-key door opened for real, then room 13's key record",
      [Phase("lock", "abre", name="Obj_DoorChainLock_A_01", key=0x3f3e33ac8b0bc295, at=(56.94, 52.51, 45.59),
             n=(0.0, 1.0), note="twin 627c6462337716cf at z 45.39"),
       F("llave", "ShadowTemple_Level_Room_13_GetKey")],
      watch=(56.94, 53.1, 49.1), keys=1),
    S("meca-bloque-sombra", "sombra", "F4", "a block pushed for real toward its goals (+z), then room 11's block "
      "record", [Phase("push", "empuja", name="Obj_MovableBox_A_05", key=0xf6250e775fee5ed0,
                       at=(26.99, 56.40, 84.75), n=(0.0, 1.0),
                       note="goals Obj_Gimmick_AnswerPosAnchor at z 92.24 (x 4.4 to 20.8)"),
                 F("hecho", "ShadowTemple_Level_Room_11_Block_Out")],
      watch=(30.5, 57.0, 84.75)),
    S("meca-limpia-sombra", "sombra", "F9", "rooms cleared (B1F C, its shutter, an enemy counter's key, the "
      "annihilation event of room 03)", [F("limpia", "ShadowTemple_RoomB1FC_Cleared"),
                                         F("abierta", "ShadowTemple_RoomB1FC_ShutterOpen"),
                                         K("contador", 0x07e502e4a13dec63, "Obj_EnemyCounter_A_01"),
                                         F("evento", "HavePlayedEvent.ShadowTemple_03_EnemyAnnihilation_Clear")],
      late=True),
    # ---------------------------------------------------------------- Campo de Entrenamiento Gerudo
    S("meca-ojo-gerudo", "gerudo", "F2", "eye switches of rooms 00 and 05",
      [F("ojo", "GerudoTrainingGround_Dungeon_Room00_EyeSwitch"),
       F("ojo2", "GerudoTrainingGround_Dungeon_Room05_EyeSwitch")]),
    S("meca-suelo-gerudo", "gerudo", "F2", "a floor switch stood on for real, then rooms 03 and 05's step switches",
      [Phase("step", "pisa", name="Obj_StepSwitch_A_01", key=0x511ca63c9fa3987d, at=(-1.86, 98.00, -45.08)),
       F("abierta", "GerudoTrainingGround_Dungeon_Room03_StepSwitch"),
       F("abierta2", "GerudoTrainingGround_Dungeon_Room05_StepSwitch")],
      watch=(1.6, 98.6, -45.08)),
    S("meca-llave-gerudo", "gerudo", "F5", "a small-key door opened for real; which door a player came in by never",
      [Phase("lock", "abre", name="Obj_DoorChainLock_A_01", key=0x6317e46992b8fc57, at=(3.00, 98.00, -50.40),
             n=(1.0, 0.0)),
       F("sensor", "GerudoTrainingGround_Dungeon_Room01_EnterFromRoom00", shared=False)],
      watch=(6.5, 98.6, -50.4), keys=1),
    S("meca-antorchas-gerudo", "gerudo", "F7", "room 06's fire and the turning floor",
      [F("enciende", "GerudoTrainingGround_Dungeon_Room06_Fire"),
       F("abierta", "GerudoTrainingGround_StoneGimmick_TurnFloorOn")]),
    S("meca-rompe-gerudo", "gerudo", "F8", "a hammer pillar's key and the cracked wall's key; B entering later",
      [K("pilar", 0xe3e8c0193f9f543d, "Obj_FireTemple_HammerSlipPillar_A_01"),
       K("pared", 0xdf3f38237e1df2f9, "Obj_GerudoTrainingGround_CrackWall_A_01")], late=True),
    S("meca-limpia-gerudo", "gerudo", "F9", "a room cleared (an enemy counter's key), its chest; the secret room "
      "sensor never", [K("contador", 0x067908a9d6e794cc, "Obj_EnemyCounter_A_01"),
                       F("cofre", "GerudoTrainingGround_Room04_SpawnTreasureBox"),
                       F("sensor", "GerudoTrainingGround_Dungeon_Room03_InSecretRoom", shared=False)], late=True),
    # ---------------------------------------------------------------- Templo del Espiritu
    S("meca-sol-espiritu", "espiritu", "F2", "sun switches (room 19's light switches, room 31's light hit): the "
      "result by flag, each player's mirror their own",
      [F("sol", "SpiritTemple_Room19_LightSwitch_On_01"), F("sol2", "SpiritTemple_Room31_LightHit")]),
    S("meca-cristal-espiritu", "espiritu", "F2", "a crystal switch hit for real with the hammer, then room 30's floor",
      [Phase("hit", "golpea", name="Obj_SwitchHit_A_02", key=0xe0686ea2e54887ec, at=(33.57, 111.81, -30.74),
             n=(0.0, -1.0)),
       F("abierta", "SpiritTemple_Room30_Floor_On")],
      watch=(33.57, 112.4, -34.2), hammer=True),
    S("meca-suelo-espiritu", "espiritu", "F2", "a floor switch stood on for real, then room 08's reversed switch",
      [Phase("step", "pisa", name="Obj_StepSwitch_A_01", key=0x7b85b77f0bd786b6, at=(47.15, 129.98, -24.65)),
       F("abierta", "SpiritTemple_Room08_StepSwitch_Reverse")],
      watch=(43.6, 130.6, -24.65)),
    S("meca-bloque-espiritu", "espiritu", "F4", "a block pushed for real toward its goal (+x), then the blocks fit in "
      "rooms 01 and 20", [Phase("push", "empuja", name="Obj_MovableBox_A_04", key=0x0ed7429202a97513,
                                at=(19.35, 103.25, -22.00), n=(1.0, 0.0),
                                note="goal Obj_Gimmick_AnswerPosAnchor 52639f969f08154d at x 23.87"),
                          F("hecho", "SpiritTemple_Room01_MovableBox_Fit"),
                          F("hecho2", "SpiritTemple_Room20_MovableBox_Fit_01")],
      watch=(19.35, 103.85, -25.5)),
    S("meca-llave-espiritu", "espiritu", "F5", "a small-key door opened for real, then room 04's unlocked record",
      [Phase("lock", "abre", name="Obj_SpiritTemple_DoorChainLock_A_01", key=0x8d1f8eec8842a3be,
             at=(-33.59, 105.96, -55.30), n=(0.0, 1.0)),
       F("registro", "SpiritTemple_Room04_Unlocked")],
      watch=(-33.59, 106.56, -51.8), keys=1),
    S("meca-antorchas-espiritu", "espiritu", "F7", "torches lit in rooms 04, 10 and 22",
      [F("enciende", "SpiritTemple_Room04_FireSet"), F("enciende2", "SpiritTemple_Room10_Candle_On"),
       F("enciende3", "SpiritTemple_Room22_FireSet")]),
    S("meca-rompe-espiritu", "espiritu", "F8", "a cracked wall's key and room 25's pillars broken; B entering later",
      [K("pared", 0x7d19595a56ab39af, "Obj_SpiritTemple_CrackWall_A_01"),
       F("pilar", "SpiritTemple_Room25_PillarBroken_01")], late=True),
    S("meca-limpia-espiritu", "espiritu", "F9", "room 04's enemies beaten, an enemy counter's key; the Iron Knuckle "
      "fight of room 25 and room 16's position check never",
      [F("limpia", "SpiritTemple_Room04_EnemyDestroy"), K("contador", 0x0a0733954ef23213, "Obj_EnemyCounter_A_09"),
       F("pelea", "SpiritTemple_Room25_FightBegain", shared=False),
       F("sensor", "SpiritTemple_Room16_PlayerPosCheck", shared=False)], late=True),
    # ---------------------------------------------------------------- Castillo de Ganon
    S("meca-antorchas-ganon", "ganon", "F7", "the torch rooms: room 08's candles lit and its shutter's played event, "
      "room 02's candle; a player's own reset record of room 08 never",
      [F("enciende", "InsideGanonCastle_Room08_Candle_ON"),
       F("abierta", "HavePlayedEvent.InsideGanonCastle_Dungeon_ShutterOpen_08_01"),
       F("enciende2", "InsideGanonCastle_Dungeon_Room02_CandleStick"),
       F("reinicia", "InsideGanonCastle_Dungeon_Room08_CandleWasReset", shared=False)]),
]

# Flags the 12 adulto-* pairs already use (not repeated here; CONTENIDO_ADULTO.md section 6).
ADULTO_FLAGS = {
    "Market_Child_TimeTempleDoor_Opened", "ForestTemple_MegBattleStart", "ForestTemple_Room08_ClimbIvy",
    "FireTemple_Room10_StepSwitchOnLink", "Npc_Goron_001_ShutterisOpen", "IceCavern_Room02_DungeonIn",
    "IceCavern_Room06_GimmickClear", "WaterTemple_Room05_InRoom", "BottomOfTheWell_CoffinRoomCheck",
    "BottomOfTheWell_Room07_UnLock", "ShadowTemple_Level_Room_13_PlayerPosCheck", "ShadowTemple_Level_Room_14_Clear",
    "GerudoFortress_IsInPrison", "GerudoFortress_Saved_Npc_Hylia_013_Adult", "SpiritTemple_Room12_FightBegain",
    "SpiritTemple_Room02_Unlocked", "InsideGanonCastle_Dungeon_BarrierRemove_Light",
    "KakarikoVillage_GatheringTokenGold_GetReward10",
}

LOCK_TICKS = 120 + 30 + 12 + 360
PUSH_TICKS = 240 + 150 + 120 + 600
STEP_TICKS = 360 + 120
HIT_TICKS = 240 + 20 + 20 + 360
FLAG_TICKS = 300


def h32(name: str) -> str:
    return f"0x{romfs_read.murmur3_32(name):08x}"


def key_debug(key: int) -> str:
    """The key as the flags debug line prints it ({:#x}: no leading zeros)."""
    return f"0x{key:x}"


def f2(v: float) -> str:
    return f"{v:.2f}"


def key_flag(scene: str) -> str:
    return f"DungeonInfo.{scene}.SmallKeyNum"


def phase_ticks(p: Phase) -> int:
    return {"flag": FLAG_TICKS, "key": FLAG_TICKS, "lock": LOCK_TICKS + (60 if p.both else 0),
            "push": PUSH_TICKS, "step": STEP_TICKS, "hit": HIT_TICKS}[p.kind]


def puppet(age: str) -> str:
    return "CoopPuppet_Child" if age == "child" else "CoopPuppet_Adult"


def build(s: Scenario) -> tuple[str, str]:
    label, scene, start, age = DUNGEONS[s.dungeon]
    warp = f"warp {scene} {start} {age}"
    give = s.keys > 0 or s.hammer
    switches = SWITCHES + (",LICHII_COOP_CHEATS=1" if give else "")
    expects: list[str] = [f"#! expect B coop: puppet: P\\d: create {puppet(age)}"]
    a: list[str] = ["mark start", warp] + LOAD
    b: list[str] = ["mark start", warp] + LOAD
    ticks = 0
    if give:
        a.append("mark prep")
        b.append("mark prep")
        for _ in range(s.keys):
            a.append(f"give {key_flag(scene)}")
        if s.hammer:
            a += ["give PouchData.Item_Hammer.StockNum", "assign Hammer"]
        a.append("wait 300")
        b.append("wait 300")
        ticks += 300
        kf = h32(key_flag(scene))
        if s.keys:
            expects.append(f"#! expect A coop: test give {re.escape(key_flag(scene))}: queued within prep..end")
            expects.append(f"#! expect B coop: flags debug: int {kf} \\(entry \\d+\\) 0 -> 1 within prep..end")
            expects.append(f"#! never A coop: flags debug: int {kf} \\(entry \\d+\\) 0 -> -1")
            expects.append(f"#! never B coop: flags debug: int {kf} \\(entry \\d+\\) 0 -> -1")
        if s.hammer:
            expects.append("#! expect A coop: test assign Hammer: set within prep..end")
    if s.watch:
        b.append(f"place {' '.join(f2(v) for v in s.watch)}   # B by the mechanism, out of A's way")
    for p in s.phases:
        a.append(f"mark {p.mark}")
        b.append(f"mark {p.mark}")
        t = phase_ticks(p)
        win = f"within {p.mark}..end"
        if p.kind == "flag":
            h = h32(p.name)
            a += [f"flag {p.name} 1" + ("" if p.shared else "   # a player's own: never shared"), f"wait {FLAG_TICKS}"]
            expects.append(f"#! expect A coop: test flag {re.escape(p.name)} \\({h}\\) = 1: queued {win}")
            if p.shared:
                expects.append(f"#! expect B coop: flags debug: bool {h} \\(entry \\d+\\) 0 -> 1 {win}")
            else:
                expects.append(f"#! never B coop: flags debug: bool {h} ")
        elif p.kind == "key":
            a += [f"key 0x{p.key:016x} 1   # {p.name}", f"wait {FLAG_TICKS}"]
            expects.append(f"#! expect A coop: test key 0x{p.key:016x} = 1: queued {win}")
            expects.append(f"#! expect B coop: flags debug: key64 {key_debug(p.key)} \\(record \\d+\\) 0 -> 1 {win}")
        elif p.kind == "lock":
            x, y, z = p.at
            dx, dz = p.n
            stand = (x + 1.4 * dx, y + 0.6, z + 1.4 * dz)
            into = (x - 1.0 * dx, z - 1.0 * dz)
            a += [f"place {' '.join(f2(v) for v in stand)}   # 1.4 m in front of the lock {p.key:016x}",
                  "wait 120", f"toward {f2(into[0])} {f2(into[1])} 30   # face the door", "press A", "wait 360"]
            hx = f"{p.key:016x}"
            kf = h32(key_flag(scene))
            expects.append(f"#! expect A coop: world: {hx} {p.name} broken here \\(.*\\): sent {win}")
            expects.append(f"#! expect A coop: flags debug: int {kf} \\(entry \\d+\\) {s.keys} -> {s.keys - 1} {win}")
            if p.both:
                side = (stand[0] - 0.8 * dz, stand[1], stand[2] + 0.8 * dx)
                b += [f"place {' '.join(f2(v) for v in side)}   # at the same door, beside A's place (within 2.5 m)",
                      "wait 180              # 1 s after A", f"toward {f2(into[0])} {f2(into[1])} 30", "press A",
                      "wait 360"]
                expects.append(f"#! expect B coop: world: {hx} lock opened (here after another player|by both at "
                               f"once): one key back {win}")
                expects.append(f"#! expect B coop: flags debug: int {kf} \\(entry \\d+\\) 0 -> 1 {win}")
                expects.append(f"#! never B coop: world: {hx} lock .*but no key of ours went")
            else:
                expects.append(f"#! expect B coop: world: {hx} {p.name} (broken by another player|was broken before "
                               f"we saw it) {win}")
                expects.append(f"#! expect B coop: flags debug: int {kf} \\(entry \\d+\\) {s.keys} -> {s.keys - 1} "
                               f"{win}")
                expects.append(f"#! never B coop: world: {hx} {p.name} broken here")
        elif p.kind == "push":
            x, y, z = p.at
            dx, dz = p.n
            stand = (x - 3.2 * dx, y + 0.6, z - 3.2 * dz)
            to = (x + 8.0 * dx, z + 8.0 * dz)
            aside = (stand[0] - 2.5 * dz, stand[1], stand[2] + 2.5 * dx)
            a += [f"place {' '.join(f2(v) for v in stand)}   # 3.2 m behind the block {p.key:016x}", "wait 240",
                  f"toward {f2(to[0])} {f2(to[1])} 150   # walk into it: one push", "wait 120",
                  f"place {' '.join(f2(v) for v in aside)}   # step aside", "wait 600"]
            hx = f"{p.key:016x}"
            expects.append(f"#! expect A coop: world: {hx} {p.name} moved here: sending {win}")
            expects.append(f"#! expect A coop: world: {hx} {p.name} at rest: place sent {win}")
            expects.append(f"#! expect B coop: world: {hx} {p.name} moved by another player {win}")
            expects.append(f"#! expect B coop: world: {hx} {p.name} (put|came to rest) where another player left it"
                           f" {win}")
            expects.append(f"#! never B coop: world: {hx} {p.name} moved here: sending")
        elif p.kind == "step":
            x, y, z = p.at
            a += [f"place {f2(x)} {f2(y + 0.5)} {f2(z)}   # on the floor switch {p.key:016x}", "wait 360",
                  f"place {f2(x)} {f2(y + 0.6)} {f2(z + 2.0)}   # off it", "wait 120"]
            expects.append(f"#! expect A coop: test place {f2(x)} {f2(y + 0.5)} {f2(z)}: done {win}")
            expects.append(f"#! expect A coop: flags debug: (bool 0x[0-9a-f]{{8}} \\(entry|key64 0x[0-9a-f]+ \\(record)"
                           f" \\d+\\) 0 -> 1 {win}")
            expects.append(f"#! expect B coop: flags debug: (bool 0x[0-9a-f]{{8}} \\(entry|key64 0x[0-9a-f]+ \\(record)"
                           f" \\d+\\) 0 -> 1 {win}")
        elif p.kind == "hit":
            x, y, z = p.at
            dx, dz = p.n
            stand = (x + 1.5 * dx, y + 0.6, z + 1.5 * dz)
            a += [f"place {' '.join(f2(v) for v in stand)}   # 1.5 m from {p.name} {p.key:016x}", "wait 240",
                  f"toward {f2(x)} {f2(z)} 20   # face it", "press ZR 10   # the hammer", "wait 360"]
            hx = f"{p.key:016x}"
            if "Pillar" in p.name:
                expects.append(f"#! expect A coop: world: {hx} {p.name} broken here \\(.*\\): sent {win}")
                expects.append(f"#! expect B coop: world: {hx} {p.name} (broken by another player|was broken before "
                               f"we saw it) {win}")
                expects.append(f"#! expect B coop: flags debug: key64 {key_debug(p.key)} \\(record \\d+\\) 0 -> 1 "
                               f"{win}")
                expects.append(f"#! never B coop: world: {hx} {p.name} did not fall on life 0")
            else:
                expects.append(f"#! expect A coop: flags debug: (bool 0x[0-9a-f]{{8}} \\(entry|key64 0x[0-9a-f]+ "
                               f"\\(record) \\d+\\) 0 -> 1 {win}")
                expects.append(f"#! expect B coop: flags debug: (bool 0x[0-9a-f]{{8}} \\(entry|key64 0x[0-9a-f]+ "
                               f"\\(record) \\d+\\) 0 -> 1 {win}")
        if not (p.kind == "lock" and p.both):
            b.append(f"wait {t}")
        ticks += t
    if s.late:
        # B comes into the room after A: where A acted last (or the room of the first key's object, unknown: the
        # entrance stays, and the key table says the rest).
        spot = next((p.at for p in reversed(s.phases) if p.at), None)
        a += ["mark tarde", "wait 600"]
        if spot:
            b += ["mark tarde", f"place {f2(spot[0])} {f2(spot[1] + 0.6)} {f2(spot[2] + 3.0)}   # B enters after",
                  "wait 600"]
        else:
            b += ["mark tarde", "wait 600              # B's key table already holds A's work: a reload shows it"]
        ticks += 600
    a += ["wait 300", "mark end"]
    b += ["wait 300", "mark end"]
    ticks += 300
    timeout = (LOAD_TICKS + ticks) // 60 + 120
    head = (f"# {s.name} (ORM, docs/MECANISMOS_ADULTO.md, family {s.family}, {label})"
            f"{' (de nino)' if age == 'child' and 'nino' not in label else ''}, A: both {age.upper()} at the "
            f"entrance; {s.what}.")
    notes = [f"#   {p.kind} '{p.mark}': {p.note}" for p in s.phases if p.note]
    common = [
        f"# Run with --transport ws; switches below (#! switches). Needs daurosk branch orm/meca-adulto (the `key` test",
        "# command) on orm/test8 (the `warp ... adult|child`, LICHII_COOP_DUNGEON_LISTS). Generated by",
        "# tools/harness/meca_adulto.py: change the table there, not this file. Positions from the Banc, not walked yet:",
        "# a 'test place ...: failed' or a 'goto timed out' is the scenario's gap, not the code's.",
        "# Mutant: the same run without LICHII_COOP_DUNGEON_LISTS turns the 'never' lines of the adult list red.",
    ]
    a_text = "\n".join([head] + notes + common + ["#! noload", "#! start coop: test warp done",
                                                  f"#! saves {SAVES[s.dungeon]}",
                                                  f"#! switches {switches}", f"#! timeout {timeout}"]
                       + expects + a) + "\n"
    b_head = (f"# {s.name} (ORM, docs/MECANISMOS_ADULTO.md, family {s.family}, {label}), B: lands where A does"
              f"{', is put by the mechanism' if s.watch else ''}"
              f"{', enters the room after A' if s.late else ''}"
              f"{', opens the same door 1 s after A' if any(p.both for p in s.phases) else ''}"
              f"; A's marks at the same times; the checks are in A's file.")
    b_text = "\n".join([b_head, "#! noload", "#! start coop: test warp done", f"#! saves {SAVES[s.dungeon]}",
                        f"#! switches {switches}",
                        f"#! timeout {timeout}"] + b) + "\n"
    return a_text, b_text


def write_all(out: Path = OUT) -> list[Path]:
    written = []
    for s in SCENARIOS:
        a_text, b_text = build(s)
        for suffix, text in ((".A.txt", a_text), (".B.txt", b_text)):
            p = out / f"{s.name}{suffix}"
            p.write_text(text, encoding="utf-8", newline="\n")
            written.append(p)
    return written


def check(tree: Path) -> list[str]:
    """Problems: a name not a Bool (or key not a Bool64bitKey) of GameDataList, a shared phase not in the built-in
    list or taken off by the adult list, a never phase still shared."""
    problems = []
    gd = romfs_read.GameData(romfs_read.Romfs())
    src = (tree / "lichii" / "hook" / "coop_dungeon.cpp").read_text(encoding="utf-8")
    builtin = {m.group(3): m.group(2) == "true" for m in
               re.finditer(r'\{0x([0-9a-f]{8}), (true|false), "([^"]+)"\}', src)}
    keys = {int(m.group(1), 16): (m.group(2), m.group(3)) for m in
            re.finditer(r'\{0x([0-9a-f]{16}), "([^"]+)", "([^"]+)"\}', src)}
    adult = (tree / "tools" / "release" / "kit" / LIST).read_text(encoding="utf-8")
    taken = set(re.findall(r"^(?:unshare|held):(\S+)", adult, re.M))
    for s in SCENARIOS:
        scene = DUNGEONS[s.dungeon][1]
        if s.keys and not gd.lookup(key_flag(scene), "Int"):
            problems.append(f"{s.name}: {key_flag(scene)} is no Int")
        for p in s.phases:
            if p.kind == "flag":
                if p.name in ADULTO_FLAGS:
                    problems.append(f"{s.name}: {p.name} is an adulto-* scenario's already")
                if not gd.lookup(p.name, "Bool"):
                    problems.append(f"{s.name}: {p.name} is no Bool of GameDataList")
                if p.shared and (p.name not in builtin or builtin[p.name] or p.name in taken):
                    problems.append(f"{s.name}: {p.name} is not a shared Bool (built-in list, adult list)")
                if not p.shared and p.name not in taken and p.name not in NOT_SHARED_ISSUE_149:
                    problems.append(f"{s.name}: {p.name} 'never' but the adult list does not take it off")
            elif p.kind in ("key", "lock", "push", "hit", "step"):
                if p.kind == "key" or (p.kind == "hit" and "Pillar" in p.name):
                    if p.key not in keys:
                        problems.append(f"{s.name}: key {p.key:016x} ({p.name}) is not in the built-in key list")
                    elif keys[p.key] != (scene, p.name):
                        problems.append(f"{s.name}: key {p.key:016x} is {keys[p.key]}, not {(scene, p.name)}")
                if p.kind != "key":
                    found = _placement(scene, p.key)
                    if not found or found[0] != p.name:
                        problems.append(f"{s.name}: placement {p.key:016x} is {found}, not {p.name}")
                    elif p.at and any(abs(a - b) > 0.01 for a, b in zip(found[1], p.at)):
                        problems.append(f"{s.name}: placement {p.key:016x} is at {found[1]}, not {p.at}")
    return problems


_BANC: dict = {}


def _placement(scene: str, key: int):
    if scene not in _BANC:
        r = romfs_read.Romfs()
        found = {}
        for kind in ("Dynamic", "Static"):
            rel = f"Banc/{scene}/{scene}_{kind}.bcett.byml.zs"
            if r.exists(rel):
                for a in r.byml(rel).get("Actors", []):
                    found[int(a.get("Hash", 0))] = (a.get("Gyaml", ""),
                                                    tuple(round(float(v), 2) for v in a.get("Translate", [0, 0, 0])))
        _BANC[scene] = found
    return _BANC[scene].get(key)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="check names, keys and places against the romfs and a tree")
    ap.add_argument("--tree", default=str(orm_paths.TREE), help="daurosk tree with coop_dungeon.cpp and the kit")
    args = ap.parse_args()
    for p in write_all():
        print(p.name)
    if args.check:
        problems = check(Path(args.tree))
        for line in problems:
            print("PROBLEM", line)
        print(f"check: {len(problems)} problems, {len(SCENARIOS)} scenarios")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
