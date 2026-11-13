"""F13 of PLAN_100 (casilla R, "recorrido con dos"): the adult route scenarios, entrance to blue warp.

    python -I tools/orm/harness/recorridos_adulto.py          # (re)writes escenarios/recorrido-adulto-*
    python -I tools/orm/harness/recorridos_adulto.py --check  # exit 1 if a file differs from the tables

Same format as the child routes (docs/RECORRIDOS.md, checked by test_recorridos.py): per block a daurosk steps file
`recorrido-adulto-<block>.steps.txt` and one `-<N>.A.txt`/`.B.txt` pair per leg; a leg is one scene, starts both
games again and warps in once (the game takes a warp only in the event after the save loads), chains the rooms with
`place`, and ends walking into the scene's exit (`salida`) or in the boss room (`jefe`, `pelea`, `portal`). The files
are made from the tables below: edit the tables, not the files (test_recorridos_adulto.py checks they match).
Doc: docs/RECORRIDOS_ADULTO.md.

Where the numbers come from: the scene's Banc (romfs `Banc/<Folder>/<Scene>_{Static,Dynamic}.bcett.byml.zs`, read
with tools/romfs/romfs_read.py). A room's spot is its `DungeonRoomCenterAnchor` (the AiGroup of the room prefab
`..._Level_Room_NN_Prefab` references it) one unit up; entrances and the boss arrival are `StartPos` names, exits
the `FieldAndDungeonEntranceEventRequestActor` that leads to the next scene, boss doors their `Obj_*SwitchShutter`,
the blue warp `Obj_WarpPoint_A_01`. Only numbers and names. The flags are names of daurosk's built-in dungeon list
(lichii/hook/coop_dungeon.cpp) and of the kit's shared_dungeon_adult.txt; their hash is MurmurHash3
(romfs_read.murmur3_32), as the hook logs them.

In a room A sets the room's result flags with `flag` (a stand-in for solving it until a save and a player do it)
and the room's one-player sensors; B, 1.2 m beside A, must get every result live, never a sensor, and never a
result going back to 0.
"""
import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "romfs"))
from romfs_read import murmur3_32  # noqa: E402

ESC = HERE / "escenarios"
SWITCHES = ("LICHII_COOP_PUPPET=1,LICHII_COOP_DEBUG_OBJECTS=1,LICHII_COOP_DEBUG_FLAGS=1,LICHII_COOP_DEBUG_BOSS=1,"
            "LICHII_COOP_STORY=1,LICHII_COOP_DUNGEON_LISTS=shared_dungeon_adult.txt")
B_DX = 1.2              # B stands this far east (x) of A, as in the child routes
UP = 1.0                # a room anchor is on its floor: place one unit above it
LOAD = """mark start
warp {scene} {start} {age}
wait 1200               # the title screen
mark title
press A
wait 180
press A
wait 180
press A
wait 300
press A                 # loads the save, then the warp
wait 2400               # the save loads, then the warp lands (about 30 s)
mark llegada
wait 3000               # B loads after A (r03_pareja) or with it (run.ps1): wait for it
"""
ROOM_SETTLE, ROOM_STAY = 300, 600
FIGHT = ["hold ZL 30"] + ["press Y", "wait 60", "hold ZR 60", "wait 120"] * 12 + [
    "wait 3600               # the boss's death and the heart container"]
PORTAL_PRESSES = 24


@dataclass
class Room:
    """One room: both stand in it (`at`: A's spot; None = stay where the last move left them), A sets `flags`
    (shared: B must get each live) and `sensors` (where one player is: B must never get them)."""
    name: str
    label: str
    at: tuple | None
    flags: list = field(default_factory=list)
    sensors: list = field(default_factory=list)


@dataclass
class Boss:
    scene: str
    actor: str
    door: tuple | None   # A's spot just outside the boss door (None: placed straight in at `arrival`)
    arrival: tuple       # the room's StartPos (or the boss rush's <Boss>Stage point when it has none)
    warp: tuple          # Obj_WarpPoint_A_01 (Ganon's Castle: the anchor that loads the collapsing castle)
    after: str           # regex of the scene(s) the warp leads to


@dataclass
class Leg:
    title: str
    scene: str
    start: str
    age: str             # adult | child (the word the warp ends with)
    rooms: list
    exit: tuple | None = None   # (next scene, x, z of the exit trigger)
    boss: Boss | None = None


@dataclass
class Route:
    name: str            # recorrido-adulto-<name>
    block: str           # PLAN_100 section 3 block
    title: str
    legs: list
    goal: str = ""


def h(name: str) -> str:
    return f"{murmur3_32(name):08x}"


def fmt(v) -> str:
    return " ".join(f"{c:.2f}" for c in v)


def esc(v) -> str:
    return " ".join(f"{c:.2f}".replace(".", r"\.").replace("-", r"\-") for c in v)


def up(x, y, z):
    return (x, y + UP, z)


# ----------------------------------------------------------------------------------------------------------------
# The routes. Room order: the critical path as the original game has it, matched to the remake's room prefabs by the
# flag names and the positions; "por confirmar" in the doc until the first run's captures show each room.

FT = "ForestTemple_"
FOREST_ROOMS = [
    Room("entrada", "first room, the small key on the tree", up(0.10, 103.44, 59.70), [FT + "Room00_GetSmallKey"]),
    Room("lobos", "the Wolfos corridor", up(-0.07, 102.60, 34.80)),
    Room("vestibulo", "the main hall (four torches, the lift down)", up(0.00, 104.50, -3.50)),
    Room("patio-oeste", "west courtyard: the ivy and the eye switch", up(-30.27, 99.00, -27.65),
         [FT + "Room08_ClimbIvy", FT + "Room08_EyeSwitch_Clear"], [FT + "Room08_ClimbIvy_Near", FT + "Room08_Back_Enter"]),
    Room("pozo", "the well room under the courtyard", up(-7.63, 88.03, -40.00), [FT + "Room09_GetSmallKey"],
         [FT + "Room09_In"]),
    Room("puerta-oeste", "the locked door west of the hall", up(-34.82, 103.51, 0.00), [FT + "Room05_LockOpne"]),
    Room("bloques", "the block puzzle (two floors)", up(-60.92, 114.13, 5.67),
         [FT + "Room11_MovableBox_Cleard1", FT + "Room11_MovableBox_Cleard2", FT + "Room11_1F_Cleard",
          FT + "Room11_2F_Cleard", FT + "Room11_LockOpne"], [FT + "InRoom11_AfterGetBow"]),
    Room("pasillo-oeste", "upper west corridor and its chest", up(-30.92, 125.53, -59.48),
         [FT + "Room12_LockOpne", FT + "Room_12_AppearTreasureChest"]),
    Room("techo", "the falling ceiling room", up(-56.43, 107.31, -59.40), [FT + "Level_Room_21_Clear"],
         [FT + "IsInFallingCeilingRoom"]),
    Room("arco", "the bow room (Stalfos)", up(-0.10, 105.50, -55.91), [FT + "Room06_Cleard", FT + "Room06_GetBow"]),
    Room("cuadro1", "the first painting", up(56.75, 103.94, 0.20), [FT + "Room16_Picture_Shot"]),
    Room("cuadro2", "the second painting and its chest", up(-65.67, 107.00, -26.77),
         [FT + "Room18_Picture_Shot", FT + "Room_18_AppearTreasureChest"]),
    Room("patio-este", "east courtyard (the well, the water)", up(24.65, 99.99, -24.55), [FT + "Room07_Cleard"]),
    Room("pasillo-este", "upper east corridor and its chest", up(28.87, 125.53, -59.49),
         [FT + "Room13_LockOpne", FT + "Room_13_AppearTreasureChest"]),
    Room("ojo-helado", "the frozen eye", up(56.54, 124.57, 3.31), [FT + "Room14_MeltedIce"]),
    Room("llave-jefe", "the boss key", up(-56.40, 124.70, -34.49), [FT + "Room23_GetBossKey"]),
    Room("tablero", "the checkerboard room", up(54.52, 104.00, -36.30), [FT + "Room15_Cleard"]),
    Room("poes", "back in the hall: the four Poes and Meg", up(0.00, 104.50, -3.50),
         [FT + "DefeatJoe", FT + "DefeatBeth", FT + "DefeatAmy", FT + "DefeatMeg"], [FT + "MegBattleStart"]),
    Room("sotano", "the basement under the lift (the boss door)", up(-0.07, 61.77, -14.66),
         [FT + "Room17_Switch_On_01", FT + "BossRoom_LockOpne"], [FT + "ShortcutLift_ComeFromBelow", FT + "Room20_In"]),
]
FOREST = Route("bosque", "12", "Templo del Bosque y Ganon Fantasma", [
    Leg("de la entrada a la puerta del jefe", "ForestTemple", "ForestTemple", "adult", FOREST_ROOMS),
    Leg("Ganon Fantasma y el portal azul", "ForestTemple", "ForestTemple", "adult",
        [Room("sotano", "the basement under the lift (the boss door)", up(-0.07, 61.77, -14.66))],
        boss=Boss("ForestTemple", "Enemy_DungeonBoss_PhantomGanon", None, (-5.99, 74.69, -66.02),
                  (0.00, 74.70, -62.50), "(SageRoom|KokiriForest_Adult)")),
])

FI = "FireTemple_"
FIRE = Route("fuego", "13", "Templo del Fuego y Volvagia", [
    Leg("de la entrada a la puerta del jefe", "FireTemple", "FireTemple", "adult", [
        Room("entrada", "the first room", up(0.00, 103.60, 12.20)),
        Room("darunia", "Darunia's room (the chain)", up(-25.50, 105.00, 0.00), [FI + "Room02_CreateChain"]),
        Room("lava", "the lava room and the first Goron cell", up(41.40, 105.00, -5.10),
             [FI + "Room01_BubbleRedSwitching00", "Npc_Goron_001_ShutterisOpen"], [FI + "ArrivePlayer_01"]),
        Room("bailarina1", "the first Flare Dancer", up(-7.00, 106.50, -33.40), [FI + "Room03_FlareDancerisDead"]),
        Room("roca", "the boulder room upstairs", up(93.00, 141.10, 8.40),
             [FI + "Room04_GimmickClear", FI + "Room04_SwitchHit"], [FI + "Room04_IsAtCorrectArea", FI + "ArrivePlayer_02"]),
        Room("persiana", "the shutter of room 06", up(42.10, 147.10, 0.30), [FI + "Room06_ShutterOpen"]),
        Room("cima", "the top floor: switch and chest", up(41.80, 187.00, 0.20),
             [FI + "Room08_SwitchPushed", FI + "Room08_TBox_isOpen"]),
        Room("laberinto", "the maze with the step switch", up(-29.50, 160.20, 0.00),
             [FI + "Room10_KeeseDead_01", FI + "Room10_SwitchWasOn"], [FI + "Room10_StepSwitchOnLink"]),
        Room("escalera", "the stair room", up(-44.50, 145.20, 0.00), [], [FI + "Room11_EnteredFromUpperStair"]),
        Room("muro-fuego", "the fire wall room", up(-68.10, 182.10, 9.30),
             [FI + "Room12_GimmickClear", FI + "Room12_DeleteAirWall"], [FI + "Room12_IsAtCorrectArea"]),
        Room("antorchas", "the torch room", up(-43.70, 189.50, -0.10), [FI + "Room13_FireOn"]),
        Room("bailarina2", "the second Flare Dancer: the hammer", up(-72.00, 151.20, 2.40),
             [FI + "Room24_FlareDancerisDead", FI + "GetHammer"]),
        Room("martillo", "the hammer's stair down", up(-44.50, 180.70, -19.60), [FI + "Room14_HammmerStair_Complete"]),
        Room("puerta16", "the door of room 16", up(24.00, 147.00, 0.00), [FI + "Room16_DoorOpen"]),
    ]),
    Leg("Volvagia y el portal azul", "FireTemple", "FireTemple", "adult",
        [Room("darunia", "Darunia's room (the boss door)", up(-25.50, 105.00, 0.00))],
        boss=Boss("FireTemple", "Enemy_DungeonBoss_Volvagia", (-36.00, 106.00, 0.00), (-64.50, 102.50, 0.00),
                  (-80.20, 102.50, 0.10), "(SageRoom|DeathMountainCrater_Adult)")),
])

IC = "IceCavern_"
ICE = Route("hielo", "14", "Caverna de Hielo", [
    Leg("de la Fuente Zora a las Botas de Hierro y fuera", "IceCavern", "IceCavern_FromZoraFountain", "adult", [
        Room("hielo-rojo", "the first red ice", up(17.30, 102.30, 79.80), [IC + "Room00_RedIce_Melted"]),
        Room("caverna", "the first cavern", up(0.00, 99.80, 35.90),
             [IC + "Room01_RedIce_Melted_01", IC + "Room01_Unlocked"], [IC + "Room01Demo_Far"]),
        Room("cuchillas", "the spinning blades", up(2.10, 103.10, 12.30)),
        Room("muro-rojo", "the room with the red ice wall", up(7.30, 105.20, -13.40),
             [IC + "Room05_RedIce_Melted_01", IC + "Room05_Unlocked"]),
        Room("rupias", "the silver rupees and the push block", up(-32.10, 100.00, -20.40),
             [IC + "Room06_SilverRupeeGet", IC + "Room06_GimmickClear", IC + "Room06_Unlocked"]),
        Room("mapa", "the map room", up(18.40, 103.30, -57.70), [IC + "Room07_GetMap", IC + "Room07_RedIce_Melted_01"]),
        Room("cruce", "the hub", up(-13.70, 100.70, -17.50)),
        Room("hielo-sala03", "the red ice of room 03", up(-41.80, 105.80, 8.00), [IC + "Room03_RedIce_Melted_02"]),
        Room("lobo-blanco", "the White Wolfos (the Iron Boots)", up(-46.20, 109.60, 36.60), [IC + "Room04_Unlocked"]),
        Room("salida-trasera", "the way out under the fountain", up(-50.70, 113.10, 62.70), [],
             [IC + "Room02_DungeonIn", IC + "Room02_DungeonExit", IC + "Room02_PassedThrough"]),
    ], exit=("ZoraFountain_Adult", -46.90, 83.20)),
], goal="los dos en la Fuente Zora por la puerta de atrás, con las Botas de Hierro")

WT = "WaterTemple_"
WATER = Route("agua", "15", "Templo del Agua y Morpha", [
    Leg("de la entrada a la puerta del jefe", "WaterTemple", "WaterTemple_FromLakeHylia", "adult", [
        Room("centro-alto", "the top of the central hall", up(-4.50, 111.50, -2.00)),
        Room("oeste", "the west room", up(-36.50, 101.00, 19.90), [WT + "Room03_00"]),
        Room("llave1", "the room of the first key", up(-48.00, 113.70, -4.40), [WT + "Room05_Cleared"],
             [WT + "Room05_InRoom"]),
        Room("persiana", "the shutter opened by a switch", up(-36.00, 100.00, -19.60),
             [WT + "Room08_Cleared", WT + "Room08_OpenShutterBySwitch"]),
        Room("pilar", "the central pillar", up(0.00, 89.00, -2.00), [WT + "Room02_Cleared"]),
        Room("centro-fondo", "the bottom of the central hall", up(-4.50, 44.70, -2.00),
             [WT + "Room00_OpenDoor1F", WT + "Room00_ShootSwitchOnce"]),
        Room("tektite", "the Tektite switch", up(-26.00, 125.90, 3.40), [WT + "Room10_Cleared", WT + "Room10_SwitchTektite"]),
        Room("este", "the east room (Ruto's pond)", up(25.90, 106.90, -4.20), [WT + "Room17_Cleared"]),
        Room("llave-este", "the east key", up(40.10, 100.00, -4.40), [WT + "Room18_Cleared"]),
        Room("stingers", "the Stingers", up(36.20, 115.10, -23.90), [WT + "Room04_00"]),
        Room("diana", "the longshot target room", up(-6.50, 113.10, -43.60), [WT + "Room20_00"],
             [WT + "Room20_EnterTheRoom", WT + "Room20_EnterByLongshot"]),
        Room("agua-sube", "the rising water room", up(-81.90, 122.20, -19.40), [WT + "Room06_Cleared"]),
        Room("link-oscuro", "Dark Link", (-78.00, 125.40, -42.00),
             [WT + "DarkLink_Death", WT + "DefeatDarkLinkAndGo", WT + "Room22_Cleared"], [WT + "DarkLinkRoom_Locked"]),
        Room("gancho-largo", "the Longshot", up(-78.00, 129.30, -108.80), [WT + "Room07_Cleared"]),
        Room("persiana21", "the shutter of room 21", up(-59.00, 110.20, -70.00), [WT + "Room21_OpenShutterBySwitch"]),
        Room("rio", "the river and its switch", up(-4.50, 98.50, -114.20), [WT + "Room14_Cleared", WT + "Room14_SwitchPushed"]),
        Room("tektite15", "the Tektite of room 15", up(14.90, 103.00, -98.00),
             [WT + "Room15_Cleared", WT + "Room15_DefeatTektite"]),
        Room("antesala", "the way to the boss door", up(-4.20, 129.30, -54.30)),
    ]),
    Leg("Morpha y el portal azul", "WaterTemple", "WaterTemple_FromLakeHylia", "adult",
        [Room("antesala", "the way to the boss door", up(-4.20, 129.30, -54.30))],
        boss=Boss("WaterTemple", "Enemy_DungeonBoss_Morpha", (-4.50, 132.40, -68.20), (-4.50, 131.40, -85.00),
                  (-4.50, 124.50, -91.80), "(SageRoom|HyruleField_Adult)")),
])

BW = "BottomOfTheWell_"
WELL = Route("pozo", "16", "Pozo de Kakariko (de niño) y Dead Hand", [
    Leg("del pozo seco a la Lente y de vuelta a Kakariko", "BottomOfTheWell", "Well_KakarikoSide", "child", [
        Room("vestibulo", "the first hall", up(0.10, 106.00, 34.40),
             [BW + "Room_01_WaterLevel_Down", BW + "Room_01_StopWater"], [BW + "Level_Room_01_InTheCenterArea"]),
        Room("estanque", "the hall's pool (diving and back)", up(-0.30, 100.00, -14.10), [BW + "Level_Room_01_C_Clear"],
             [BW + "Level_Room_01_InTheDivingArea", BW + "Level_Room_01_InTheReturnArea"]),
        Room("puerta07", "the locked door of room 07", up(-26.30, 100.00, -8.80), [BW + "Room07_UnLock"]),
        Room("ataudes", "the coffin room", up(-54.50, 100.00, -18.50),
             [BW + "Level_Room_03_Coffin_Open", BW + "Level_Room_03_Get_SmallKey"], [BW + "CoffinRoomCheck"]),
        Room("telaranas", "the burnt webs", up(27.30, 100.00, -29.10), [BW + "Level_Room_02_BurnedGimmick"]),
        Room("sala04", "room 04", up(8.30, 100.00, -27.80)),
        Room("sala05", "room 05", up(-8.00, 100.00, -27.40)),
        Room("sotano", "the basement", up(1.70, 90.80, -20.10), [BW + "Level_Room_09_A_Clear"],
             [BW + "Level_Room_09_NearRock"]),
        Room("dead-hand", "Dead Hand (the Lens of Truth)", (3.60, 92.40, -97.20),
             [BW + "Level_Room_06_Boss_Appear", BW + "Level_Room_06_Boss_Defeat", BW + "Level_Room_06_A_Clear"]),
        Room("vuelta", "back at the foot of the well", up(0.00, 106.00, 42.40)),
    ], exit=("KakarikoVillage_Child", 0.00, 38.10)),
], goal="los dos de vuelta en Kakariko con la Lente de la Verdad")

ST = "ShadowTemple_"
SHADOW = Route("sombra", "17", "Templo de la Sombra y Bongo Bongo", [
    Leg("de la entrada a la sala sobre el jefe", "ShadowTemple", "Entrance", "adult", [
        Room("entrada", "the first hall (the Graveyard door Din's Fire opened)", up(13.90, 90.40, 45.50),
             ["GraveYard_Field_ShadowTemple_ShutterOpen", ST + "RoomB1FEntrance_Cleared"]),
        Room("calavera", "the skull's room", up(42.90, 90.00, 1.00), [ST + "Level_Room_01_Clear_A"]),
        Room("b1f-b", "room B1F B", up(-3.50, 90.40, -4.70), [ST + "Level_Room_02_Clear"]),
        Room("b1f-c", "room B1F C and its shutter", up(2.70, 90.40, -23.40),
             [ST + "RoomB1FC_Cleared", ST + "RoomB1FC_ShutterOpen", ST + "Level_Room_03_Clear"]),
        Room("dead-hand", "Dead Hand (the Hover Boots)", up(-72.70, 90.90, -10.20),
             [ST + "Level_Room_B1F_D_Defeat_DeadHand", ST + "RoomB1FD_Cleared", ST + "Level_Room_04_Clear"]),
        Room("b2f-a", "the first room of B2F", up(77.40, 87.20, 1.50), [ST + "Level_Room_05_Clear"]),
        Room("b2f-rupias", "the silver rupees of B2F", up(83.50, 87.20, -23.60),
             [ST + "RoomB2FC_Cleared", ST + "Level_Room_06_SilverToken_Appear"]),
        Room("b2f-b", "the shutter of B2F B", up(84.20, 87.20, 19.40), [ST + "RoomB2FB_Cleared", ST + "RoomB2FB_ShutterOpen"]),
        Room("cuchillas", "the blades", up(80.00, 65.50, 71.40), [ST + "RoomB3FB4FB_Cleared", ST + "Level_Room_09_Clear_A"]),
        Room("pinchos", "the falling spikes", up(19.20, 56.40, 92.10), [ST + "RoomB3FB4FC_Cleared"]),
        Room("suelo-invisible", "the invisible floor room", up(56.90, 52.50, 33.90),
             [ST + "RoomB3FB4FE_Cleared_All", ST + "Level_Room_13_Clear"], [ST + "Level_Room_13_PlayerPosCheck"]),
        Room("keith", "the Keith fight", up(31.00, 59.30, 32.90), [ST + "RoomB3FB4FF_DefeatKeith"]),
        Room("viento", "the wind room", up(124.50, 52.00, 6.70), [ST + "RoomB3FB4FH_Cleared"]),
        Room("redeads", "the ReDeads and the falling chest", up(109.70, 52.00, -18.80),
             [ST + "RoomB3FB4FI_Cleared", ST + "RoomB3FB4FI_DefeatRedead"]),
        Room("barca", "the boat (each game its own)", (131.20, 46.00, -47.70)),
        Room("torre", "past the river: the statue and the tower", up(7.80, 42.00, -58.40),
             [ST + "RoomB3FB4FJ_IsTowerBreak", ST + "Level_Room_17_StatueBroken"]),
        Room("floormaster", "the Floormaster", up(-181.70, 41.80, -17.50),
             [ST + "RoomB3FB4FL_Cleared", ST + "RoomB3FB4FL_DefeatFloormaster_Truth"]),
        Room("sala-m", "room M", up(-181.80, 41.30, -67.00), [ST + "RoomB3FB4FM_Cleared"]),
        Room("sala-n", "room N", up(-218.00, 41.30, -43.60), [ST + "RoomB3FB4FN_Cleared"]),
        Room("antesala", "the room over the boss (the hole)", (-138.50, 42.00, 21.20), [ST + "RoomB3FB4FP_Cleared"]),
    ]),
    Leg("Bongo Bongo y el portal azul", "ShadowTemple", "Entrance", "adult",
        [Room("antesala", "the room over the boss (the hole)", (-138.50, 42.00, 21.20))],
        boss=Boss("ShadowTemple", "Enemy_DungeonBoss_BongoBongo", None, (-138.30, -11.50, 22.00),
                  (-138.30, -11.50, 30.10), "(SageRoom|Graveyard_Adult)")),
])

GF = "GerudoFortress_"
GT = "GerudoTrainingGround_"


def carpenter(n: str, at: tuple, extra=()) -> Room:
    hy = f"Npc_Hylia_{n}_Adult"
    return Room(f"carpintero{n}", f"the carpenter {n} in his cell", up(*at),
                [GF + "DestroyGerudoThief_" + hy, GF + "Saved_" + hy, GF + "IsAfterHelpCarpenter_" + hy, *extra],
                [GF + "IsInPrison"] if n == "013" else [])


GERUDO = Route("gerudo", "18", "Valle y Fortaleza Gerudo, Campo de Entrenamiento, Desierto y Coloso", [
    Leg("el valle hasta la fortaleza", "GerudoValley_Adult", "GerudoValley_Entrance", "adult", [
        Room("carrera", "the valley by the fortress (the riding race start)", up(-37.00, 101.20, -18.00)),
        Room("puerta-fortaleza", "beside the way into the fortress", up(-62.40, 102.60, -6.50)),
    ], exit=("GerudoFortress_Adult", -66.60, -7.20)),
    Leg("los cuatro carpinteros", "GerudoFortress_Adult", "GerudoFortress_FromGerudoValley", "adult", [
        carpenter("013", (31.90, 102.60, -66.60)),
        carpenter("014", (72.60, 120.60, -111.40)),
        carpenter("015", (39.70, 119.00, -22.90)),
        carpenter("016", (10.70, 110.00, -78.30), [GF + "IsAfterClearEvent_Npc_Hylia_012_Adult"]),
        Room("puerta-campo", "the Training Ground's door", up(7.80, 110.30, 0.10)),
    ], exit=("GerudoTrainingGround", -1.20, 10.80)),
    Leg("el Campo de Entrenamiento", "GerudoTrainingGround", "GerudoTrainingGround_FromGerudoFortress", "adult", [
        Room("vestibulo", "the hall", up(-1.80, 99.20, -6.10), [GT + "Dungeon_Room00_EyeSwitch"],
             [GT + "EnterThroughEntrance"]),
        Room("stalfos", "the Stalfos room", up(-47.10, 97.80, -5.10), [GT + "Dungeon_Room01_DefeatEnemy"],
             [GT + "Dungeon_Room01_EnterFromRoom00"]),
        Room("rocas", "the boulders and silver rupees", up(-46.80, 98.00, -43.00), [GT + "Dungeon_Room02_SilverRupee"],
             [GT + "Dungeon_Room01_EnterFromRoom02"]),
        Room("wolfos", "the Wolfos and the step switch", up(-47.10, 104.00, -85.30),
             [GT + "Dungeon_Room03_DefeatEnemy", GT + "Dungeon_Room03_StepSwitch"], [GT + "Dungeon_Room03_InSecretRoom"]),
        Room("dinolfos", "the Dinolfos", up(-47.50, 104.00, -126.20), [GT + "Dungeon_Room10_DefeatEnemy"]),
        Room("estatua", "the statue room", up(-2.20, 108.20, -82.20), [GT + "Room04_OpenDoor"]),
        Room("ojo", "the eye switch and the hammer pillars", up(43.30, 98.40, -82.40),
             [GT + "Dungeon_Room05_DefeatEnemy", GT + "Dungeon_Room05_EyeSwitch"],
             [GT + "Dungeon_Room05_EnterFromRoom04", GT + "Dungeon_Room05_EnterFromRoom06"]),
        Room("lava", "the lava and silver rupees", up(43.20, 94.00, -48.40), [GT + "Dungeon_Room06_SilverRupee"]),
        Room("beamos", "the Beamos room", up(44.40, 98.50, -5.40), [GT + "Dungeon_Room07_DefeatEnemy"],
             [GT + "Dungeon_Room07_EnterFromRoom06", GT + "Dungeon_Room07_EnterFromRoom00"]),
        Room("agua", "the water and silver rupees", up(68.90, 94.80, -43.90), [GT + "Dungeon_Room09_SilverRupee"]),
        Room("flechas-hielo", "the centre: the Ice Arrows", up(-1.40, 98.00, -48.00), [GT + "Dungeon_Room08_TBox"],
             [GT + "Dungeon_Room08_InSecretRoom"]),
        Room("puerta", "back at the door", up(-1.80, 103.00, 11.90), [], [GT + "OutThroughEntrance"]),
    ], exit=("GerudoFortress_Adult", -1.80, 6.90)),
    Leg("la verja del desierto", "GerudoFortress_Adult", "GerudoFortress_FromHountedWasteland", "adult", [
        Room("verja", "the gate to the desert", up(-31.30, 104.90, -105.10), ["Gerudo_phantomdesert_gate"]),
    ], exit=("HountedWasteland_Adult", -49.20, -117.50)),
    Leg("el Desierto Encantado", "HountedWasteland_Adult", "HountedWasteland_FromGerudoFortress", "adult", [
        Room("cofre", "the chest that appears", up(115.80, 95.40, 53.60), ["HountedWasteland_TreasureBoxAppear"]),
        Room("lado-coloso", "the far side, at the Colossus", up(-103.20, 99.20, -55.80)),
    ], exit=("DesertColossus_Adult", -117.40, -55.80)),
    Leg("el Coloso hasta el Templo del Espíritu", "DesertColossus_Adult", "DesertColossus_FromHountedWasteland", "adult", [
        Room("cofres", "the Colossus' chests", up(-3.00, 100.10, 0.00),
             ["DesertColossus_IsSecretHole_TreasureBoxAppear", "DesertColossus_Tbox_On"]),
        Room("puerta-templo", "at the Spirit Temple's door", up(-159.30, 96.20, 10.30)),
    ], exit=("SpiritTemple", -173.70, 10.00)),
], goal="los dos en el Templo del Espíritu, con los cuatro carpinteros libres y la verja abierta")

SP = "SpiritTemple_"
SPIRIT = Route("espiritu", "19", "Templo del Espíritu (las dos mitades) y Twinrova", [
    Leg("la mitad de niño hasta la mano derecha", "SpiritTemple", "SpiritTemple_Entrance", "child", [
        Room("nino-entrada", "the child's side, first room", up(-33.60, 104.80, -30.40), [SP + "Room02_Unlocked"],
             [SP + "Level_Room_02_PlayerPosChange1", SP + "LevelRoom_02_PlayerPosChange2"]),
        Room("puente", "the bridge and the net", up(-52.10, 104.70, -52.90), [SP + "Room03_BoardNet_On"]),
        Room("antorchas", "the torches of room 04", up(-33.40, 104.70, -78.70), [SP + "Room04_FireSet", SP + "Room04_Unlocked"]),
        Room("cofre05", "the chest of room 05", up(-15.30, 104.70, -52.30), [SP + "Room05_TBox_On"]),
        Room("puerta06", "the door of room 06", up(-33.60, 104.80, -49.50), [SP + "Room06_Unlocked"]),
        Room("subida", "the climb and the enemies", up(-32.20, 119.50, -47.50),
             [SP + "Level_Room_07_DefeatEnemy", SP + "Room07_Unlocked"]),
        Room("estatua", "the big statue hall", up(0.00, 120.70, -59.20), [SP + "Room08_Unlocked_00"],
             [SP + "Room08_StepSwitchOnLink"]),
        Room("vela", "the Beamos and the candle", up(-44.20, 130.80, -21.80), [SP + "Room10_Candle_On", SP + "Room10_Unlocked"]),
        Room("puerta11", "the door of room 11", up(-55.80, 132.80, 2.50), [SP + "Room11_Unlocked"]),
        Room("nudillo", "the Iron Knuckle (Nabooru taken)", up(-55.90, 132.10, 21.80),
             [SP + "Room12_ChairBroken", SP + "Room12_Unlocked"], [SP + "Room12_FightBegain"]),
        Room("mano-derecha", "the right hand (the Silver Gauntlets)", up(-55.80, 132.10, 39.30)),
    ], exit=("DesertColossus_Child", -55.80, 43.70)),
    Leg("la mitad de adulto hasta la mano izquierda y arriba", "SpiritTemple", "SpiritTemple_Entrance", "adult", [
        Room("bloque", "the lobby's block", up(0.00, 100.00, -10.00), [SP + "Room01_MovableBox_Fit"]),
        Room("adulto-entrada", "the adult's side, first room", up(33.60, 104.70, -30.70),
             [SP + "Room14_Unlocked_01", SP + "Room14_Unlocked_02"]),
        Room("enemigos15", "the enemies of room 15", up(18.10, 105.00, -48.40),
             [SP + "Level_Room_15_DefeatEnemy", SP + "Level_Room_15_DefeatEnemy_02"]),
        Room("rupias", "the silver rupees and the Time block", up(48.80, 100.20, -54.80), [SP + "Room16_Unlocked"],
             [SP + "Room16_PlayerPosCheck"]),
        Room("cofre17", "the chest of room 17", up(48.90, 100.20, -77.80), [SP + "Room17_TBox_Open"]),
        Room("luces", "the four light switches", up(35.40, 119.20, -47.50),
             [SP + "Room19_LightSwitch_On_01", SP + "Room19_LightSwitch_On_02", SP + "Room19_LightSwitch_On_03",
              SP + "Room19_LightSwitch_On_04"]),
        Room("estatua", "the big statue hall (from the adult's side)", up(0.00, 120.70, -59.20), [SP + "Room08_Unlocked_01"]),
        Room("beamos", "the Beamos and the mark block", up(38.60, 130.00, -23.00),
             [SP + "Room22_BeamosDead", SP + "Room22_Unlocked_01"]),
        Room("amos", "the Amos on the switch", up(55.80, 130.00, -23.20), [SP + "Room23_Unlocked_01"],
             [SP + "Room23_StepSwitchOnLink", SP + "Room23_StepSwitchOnAmos"]),
        Room("nudillo", "the Iron Knuckle (the Mirror Shield's hand)", up(55.60, 132.20, 21.80),
             [SP + "Room25_ChairBroken", SP + "Room25_Unlocked"], [SP + "Room25_FightBegain"]),
        Room("arriba", "the upper floor's enemies", up(17.10, 139.00, -22.70), [SP + "Room28_EnemyDefeat"]),
        Room("puerta29", "the door of room 29", up(19.80, 154.00, -48.00), [SP + "Room29_Unlocked"]),
        Room("puerta-trampa", "the tricky door", up(19.80, 152.90, -65.10), [SP + "Room30_TrickyDoor_Defeat"]),
        Room("espejos", "the mirrors and the sun on the face", up(-0.10, 153.10, -50.00),
             [SP + "Room31_LightHit_After", SP + "Room31_Unlocked"],
             [SP + "Level_Room_31_AreaCheck1", SP + "Level_Room_31_AreaCheck2"]),
        Room("nabooru", "the last Iron Knuckle (Nabooru)", up(0.10, 137.50, -88.90), [SP + "Room33_ChairBroken"]),
    ]),
    Leg("Twinrova y el portal azul", "SpiritTemple", "SpiritTemple_Entrance", "adult",
        [Room("nabooru", "the last Iron Knuckle's room, before the boss", up(0.10, 137.50, -88.90))],
        boss=Boss("SpiritTemple", "Enemy_DungeonBoss_Twinrova", None, (-0.70, 142.20, -158.60),
                  (0.00, 142.20, -157.90), "(SageRoom|DesertColossus_Adult)")),
])

IG = "InsideGanonCastle_"
IGD = IG + "Dungeon_"
GANON = Route("ganon", "20", "Castillo de Ganon: las seis pruebas, la torre y Ganondorf", [
    Leg("el puente hasta la puerta", "GanonCastle", "GanonCastleEntrance", "adult", [
        Room("puerta", "the main gate", up(42.30, 98.70, 228.60), ["GanonCastle_IsOpenMainGate"]),
    ], exit=("InsideGanonCastle", 40.90, 221.30)),
    Leg("las seis pruebas y la torre", "InsideGanonCastle", "Entrance", "adult", [
        Room("vestibulo", "the entrance hall", up(-0.10, 106.00, 39.90), [IGD + "SeeBarrierGuide"]),
        Room("p1-wolfos", "trial of rooms 12-15: the Wolfos", up(-45.60, 99.80, -21.00), [IGD + "Room12_DefeatEnemy"]),
        Room("p1-rupias", "trial of rooms 12-15: the silver rupees in the wind", up(-67.50, 106.40, -21.00),
             [IGD + "Room13_Cleared", IGD + "Room14_SilverRupee"]),
        Room("p1-barrera", "trial of rooms 12-15: its barrier", up(-83.50, 102.70, -21.00), [IGD + "BarrierRemove_Forest"]),
        Room("p2-hielo", "trial of rooms 05-07: the ice blocks and the hammer", up(78.40, 97.70, -20.90),
             [IGD + "Room06_IceBlock", IGD + "Room06_HmmerSwitch"]),
        Room("p2-barrera", "trial of rooms 05-07: its barrier", up(101.80, 99.70, -21.00), [IGD + "BarrierRemove_Water"]),
        Room("p3-antorcha", "trial of rooms 08-09: the hammer, the step switch, the torch", up(38.60, 103.90, -88.90),
             [IGD + "Room08_HummerSwitch", IGD + "Room08_StepSwitch", IG + "Room08_Candle_ON"]),
        Room("p3-barrera", "trial of rooms 08-09: its barrier", up(65.80, 105.40, -135.90), [IGD + "BarrierRemove_Shadow"]),
        Room("p4-enemigos", "trial of rooms 02-04: the torch and the enemies", up(23.00, 103.20, 19.10),
             [IGD + "Room02_CandleStick", IGD + "Room02_DefeatEnemy"]),
        Room("p4-rupias", "trial of rooms 02-04: the silver rupees", up(36.50, 102.80, 42.20),
             [IGD + "Room03_SilverRupee", IGD + "Room03_StepSwitch"]),
        Room("p4-barrera", "trial of rooms 02-04: its barrier", up(47.50, 102.90, 62.10), [IGD + "BarrierRemove_Fire"]),
        Room("p5-roca", "trial of rooms 10-11: the boulder and the silver rupees", up(-30.50, 103.70, -73.80),
             [IGD + "Room10_LiftRock", IGD + "Room10_SilverRupee"]),
        Room("p5-barrera", "trial of rooms 10-11: its barrier", up(-52.60, 104.00, -103.90), [IGD + "BarrierRemove_Light"]),
        Room("p6-rupias", "trial of rooms 16-18: the silver rupees", up(-22.30, 103.90, 18.60), [IGD + "Room16_SilverRupee"]),
        Room("p6-sol", "trial of rooms 16-18: the Bombchu switch and the sun", up(-30.10, 107.10, 37.40),
             [IGD + "Room17_HitSwitch_Bombchu", IGD + "Room17_SolarSwitch"]),
        Room("p6-barrera", "trial of rooms 16-18: its barrier", up(-38.10, 105.00, 51.40), [IGD + "BarrierRemove_Spirit"]),
        Room("torre", "the central tower, all barriers gone", up(-0.20, 109.40, -20.50),
             [IG + "IsAllBarrierClear", IG + "ExtendingPlatform_ON"]),
        Room("torre20", "the tower, room 20", up(0.40, 136.20, -24.40), [IGD + "Room20_Cleared"]),
        Room("torre22", "the tower, room 22", up(0.20, 143.00, -25.00), [IGD + "Room22_Cleared"]),
        Room("torre24", "the tower, room 24 (the two chairs)", up(0.10, 151.20, -24.70),
             [IGD + "Room24_Cleared", IGD + "Room24_Unlocked", IGD + "Room24_BlackChairBroken",
              IGD + "Room24_WhiteChairBroken"]),
    ]),
    Leg("Ganondorf (y la salida al castillo que se derrumba)", "InsideGanonCastle", "Entrance", "adult",
        [Room("torre24", "the tower's last room before the top", up(0.10, 151.20, -24.70))],
        boss=Boss("InsideGanonCastle", "Enemy_DungeonBoss_Ganondorf", None, (0.00, 171.10, -11.30),
                  (0.00, 171.10, -24.80), "CrumblingGanonCastle")),
], goal="los dos en la pelea con Ganondorf; la huida y Ganon siguen en los escenarios huida y final-*")

TIME = Route("tiempo", "11", "Templo del Tiempo de adulto", [
    Leg("la Ciudadela en ruinas hasta el pedestal", "Market_Adult", "Market_Entrance", "adult", [
        Room("nave", "the Temple of Time's nave (where the Prelude of Light lands)", up(84.80, 103.20, -34.00)),
        Room("puerta", "the Door of Time and the three stones", up(106.00, 103.90, -34.00),
             ["Market_Child_TimeTempleDoor_Opened"]),
        Room("pedestal", "the Master Sword's pedestal", up(133.50, 103.90, -34.00)),
        Room("despertar", "where the adult Link wakes", up(120.50, 104.70, -26.70)),
    ]),
], goal="los dos ante el pedestal con la Puerta del Tiempo abierta (la época del grupo: D-R17, F15)")

ROUTES = [TIME, FOREST, FIRE, ICE, WATER, WELL, SHADOW, GERUDO, SPIRIT, GANON]


# ----------------------------------------------------------------------------------------------------------------


def leg_marks(leg: Leg) -> list:
    out = ["start", "title", "llegada"] + [f"r{i:02d}-{r.name}" for i, r in enumerate(leg.rooms, 1)]
    if leg.exit:
        out.append("salida")
    if leg.boss:
        out += ["jefe", "pelea", "portal"]
    return out + ["fin"]


def nxt(marks: list, m: str) -> str:
    return marks[marks.index(m) + 1]


def leg_script(leg: Leg, who: str) -> list:
    dx = B_DX if who == "B" else 0.0
    lines = LOAD.format(scene=leg.scene, start=leg.start, age=leg.age).splitlines()
    for i, r in enumerate(leg.rooms, 1):
        m = f"r{i:02d}-{r.name}"
        lines.append(f"mark {m}" + " " * max(1, 22 - len(m)) + f"# {r.label}")
        if r.at:
            lines.append(f"place {fmt((r.at[0] + dx, r.at[1], r.at[2]))}")
        lines.append(f"wait {ROOM_SETTLE}")
        if who == "A":
            lines += [f"flag {f} 1          # where one player is: never in B" for f in r.sensors]
            lines += [f"flag {f} 1" for f in r.flags]
        lines.append(f"wait {ROOM_STAY}" + ("               # the other one sees us in this room" if i == 1 else ""))
    if leg.exit:
        nscene, x, z = leg.exit
        lines += [f"mark salida             # into {nscene}", f"goto {x + dx:.2f} {z:.2f} 0.8 900",
                  "wait 1800               # the scene change, then the other one lands too"]
    b = leg.boss
    if b:
        lines.append("mark jefe               # into the boss room together")
        if b.door:
            lines += [f"place {fmt((b.door[0] + dx, b.door[1], b.door[2]))}", "wait 120",
                      f"goto {b.arrival[0] + dx:.2f} {b.arrival[2]:.2f} 1.5 900"]
        else:
            lines.append(f"place {fmt((b.arrival[0] + dx, b.arrival[1], b.arrival[2]))}")
        lines.append("wait 2400               # the intro")
        lines.append("mark pelea              # the fight: with the entrance save's weapons (PLAN_100 section 9)")
        lines += FIGHT
        lines.append("mark portal             # both into the blue warp")
        lines.append(f"goto {b.warp[0] + dx:.2f} {b.warp[2]:.2f} 0.8 900")
        lines += ["wait 120", "press A"] * PORTAL_PRESSES
        lines.append("wait 1800               # the blue warp's scene and the next scene loading")
    lines.append("mark fin")
    return lines


def ticks(lines: list) -> int:
    n = 0
    for l in lines:
        w = l.split("#", 1)[0].split()
        if not w:
            continue
        if w[0] == "wait":
            n += int(w[1])
        elif w[0] == "press":
            n += 12
        elif w[0] == "hold":
            n += int(w[2])
        elif w[0] == "goto":
            n += int(w[4])
    return n


def leg_saves(route: Route, leg: Leg) -> str:
    """The synthetic save a leg starts from (`#! saves`, tools/orm/saves/recetas, docs/ARNES.md): its block's entry,
    the Spirit Temple's by the leg's age; the adult Temple of Time (block 11's end) takes block 12's, the first adult."""
    if route.name == "espiritu":
        return "19-espiritu-nino" if leg.age == "child" else "19-espiritu-adulto"
    if route.name == "tiempo":
        return "12-bosque"
    return f"{route.block}-{route.name}"


def leg_header(route: Route, n: int, leg: Leg, who: str) -> list:
    marks = leg_marks(leg)
    puppet = "CoopPuppet_Child" if leg.age == "child" else "CoopPuppet_Adult"
    first = marks[3]
    last = "salida" if leg.exit else ("jefe" if leg.boss else "fin")
    head = [f"# recorrido-adulto-{route.name}-{n} (ORM, docs/RECORRIDOS_ADULTO.md, bloque {route.block}. {route.title}), "
            f"{who}: tramo {n}, {leg.title}.",
            f"# {leg.scene}: warp to {leg.start} ({leg.age}), then room by room with place"
            + (" (B 1.2 m beside A)" if who == "B" else "") + f"; see recorrido-adulto-{route.name}.steps.txt.",
            "# Generated by tools/harness/recorridos_adulto.py: edit its tables, not this file. Run alone:",
            f"# r03_pareja.py --scenario recorrido-adulto-{route.name}-{n} --transport ws (switches from the header).",
            "#! noload", "#! start coop: test warp done", f"#! saves {leg_saves(route, leg)}",
            f"#! switches {SWITCHES}", f"#! timeout {ticks(leg_script(leg, 'A')) // 60 + 240}",
            f"#! expect {who} coop: test warp done",
            f"#! never {who} coop: test goto timed out",
            f"#! never {who} coop: test place .*: failed",
            f"#! never {who} coop: test warp to .* did not happen",
            f"#! never {who} coop: boss: Link's actor (is gone|not found)",
            rf"#! expect {who} coop: puppet: P\d:? (create )?{puppet} within llegada..{first}",
            rf"#! never {who} coop: puppet: P\d show -> hide within {first}..{last}"]
    if who == "A":
        head.append(r"#! expect A coop: lists: .*shared_dungeon_adult\.txt: \d+ flags and keys added")
    dx = B_DX if who == "B" else 0.0
    for i, r in enumerate(leg.rooms, 1):
        m = f"r{i:02d}-{r.name}"
        span = f"within {m}..{nxt(marks, m)}"
        if r.at:
            head.append(f"#! expect {who} coop: test place {esc((r.at[0] + dx, r.at[1], r.at[2]))}: done {span}")
        if who == "A":
            head += [rf"#! expect A coop: test flag {f} \(0x{h(f)}\) = 1 {span}" for f in r.sensors + r.flags]
        else:
            for f in r.flags:
                head.append(rf"#! expect B coop: flags debug: \w+ 0x{h(f)} \(entry \d+\) 0 -> 1 within {m}..fin")
                head.append(rf"#! never B coop: flags debug: \w+ 0x{h(f)} \(entry \d+\) 1 -> 0")
            head += [rf"#! never B coop: flags debug: \w+ 0x{h(f)} " for f in r.sensors]
    if leg.exit:
        head.append(f"#! expect {who} coop: drops: asked for the drops of {leg.exit[0]} within salida..fin")
        head.append(rf"#! expect {who} coop: puppet: P\d:? (create )?{puppet} within salida..fin")
    b = leg.boss
    if b:
        head.append(f"#! expect {who} coop: boss: in the boss room of {b.scene}: {b.actor} within jefe..pelea")
        if who == "A":
            head.append(r"#! expect A coop: boss: (intro started \(|no intro: the fight has begun here) within jefe..pelea")
        else:
            head.append("#! expect B coop: boss: (intro ended|another player's intro is over: skipping ours|"
                        "no intro: the fight has begun here) within jefe..pelea")
        head.append(rf"#! expect {who} coop: boss: boss beaten( \(|in another game) within pelea..portal")
        head.append(f"#! expect {who} coop: drops: asked for the drops of {b.after} within portal..fin")
    return head


def render_leg(route: Route, n: int, leg: Leg, who: str) -> str:
    return "\n".join(leg_header(route, n, leg, who) + leg_script(leg, who)) + "\n"


def render_steps(route: Route) -> str:
    total = sum(ticks(leg_script(l, "A")) // 60 + 120 for l in route.legs) + 300
    out = [f"# recorrido-adulto-{route.name} (ORM, docs/RECORRIDOS_ADULTO.md, PLAN_100 F13): bloque {route.block}. "
           f"{route.title}.",
           "# Both games play the critical path together, leg by leg; each leg starts both games again and warps",
           "# them in (a warp is taken only in the event after a save loads), then chains the rooms with place.",
           "# Each leg's #! lines (in its .A/.B files) are what must be in the logs. Runs with daurosk's",
           f"# tools/autotest/run.ps1 -Scenario recorrido-adulto-{route.name} (copy the recorrido-adulto-{route.name}*",
           "# files into its scenarios folder); each leg also runs alone with r03_pareja.py --scenario",
           f"# recorrido-adulto-{route.name}-N. Generated by tools/harness/recorridos_adulto.py; not run yet.",
           "#! noload", f"#! switches {SWITCHES}", f"#! timeout {total}"]
    for n, leg in enumerate(route.legs, 1):
        if n > 1:
            out += ["close A", "close B"]
        out.append(f"start A B" + " " * 22 + f"# leg {n}: {leg.title} ({leg.scene})")
        out.append(f"play A recorrido-adulto-{route.name}-{n}.A.txt B recorrido-adulto-{route.name}-{n}.B.txt")
        out.append(f"expect {route.name}-{n} A=1 B=1")
    return "\n".join(out) + "\n"


def files() -> dict:
    out = {}
    for r in ROUTES:
        out[ESC / f"recorrido-adulto-{r.name}.steps.txt"] = render_steps(r)
        for n, leg in enumerate(r.legs, 1):
            for who in "AB":
                out[ESC / f"recorrido-adulto-{r.name}-{n}.{who}.txt"] = render_leg(r, n, leg, who)
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
    stale = sorted(p.name for p in ESC.glob("recorrido-adulto-*") if p not in files())
    for name in stale:
        print(f"not made by the tables: {name}")
    return 1 if (a.check and bad) or stale else 0


if __name__ == "__main__":
    sys.exit(main())
