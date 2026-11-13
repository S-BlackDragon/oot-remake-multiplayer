"""The historia-* scenario pairs of every story cutscene (docs/HISTORIAS_ESCENARIOS.md).

One table (TABLE) -> escenarios/historia-<slug>.A.txt and .B.txt, and for the cutscenes that change the world (a story
step or a world Bool on their line) escenarios/historia-<slug>-rezagado.A.txt and .B.txt. The rules come from the
shared story list (shared_story.txt of the kit, docs/HISTORIA_COMPLETA.md, docs/CINEMATICAS.md, docs/FINAL.md):

  shared   both games warp to the cutscene's scene; A asks for it with the test command `event`; both press A every
           2 s; A is the owner ('owner: our game started'), B follows the STORY_EVENT start ('starting', 'event running
           ... as P1 asked') and both end with the line's flags: HavePlayedEvent 0 -> 1, the world Bools 0 -> 1 and
           the final Step_* values (B). No second start: B never starts it again after the press window, A never
           logs 'runs here again'.
  solo     (D-R20: the cutscene after a blue warp is the warper's) A logs 'is solo' and never sends a
           start; B never runs it and never gets a start, but gets its HavePlayedEvent, its Bools and its steps.
  near=M   (docs/FINAL.md, the six trial barriers in branch orm/final): both games land on the same spot, so B is
           within M m and plays it with A (final-pruebas-otra covers the "too far" side).
  rezagado (latecomer, the historia-rezagado pattern): B marks the cutscene played on its own save 10 s before A
           asks, ignores A's start ('is set here') and still ends with A's world: the same Bools, the same steps (the
           step change lines and a final `enum` that reads the target value or a later one).

The step values each cutscene may move to are read from docs/HISTORIA_COMPLETA.md section 3 (every branch of its
flow; the list's comments keep only the first); the list gives the line's flags, solo and each step's value order.

Run:  python tools/orm/harness/historias.py           writes the files (and says how many)
      python tools/orm/harness/historias.py --table   prints the Markdown table of HISTORIAS_ESCENARIOS.md
The list: env ORM_STORY_LIST, else <ORM_TREE>/tools/release/kit/shared_story.txt.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "escenarios"
DOC = HERE / "HISTORIA_COMPLETA.md"  # optional: the step table (our Spanish doc); without it the tests that need it skip
import orm_paths  # noqa: E402
LIST = orm_paths.STORY_LIST
SWITCHES = "LICHII_COOP_STORY=1"
GENERATED = "Written by tools/harness/historias.py: change its table and run it again, do not edit by hand."

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
]
PRESS_TICKS = 114  # wait 108 + press A 6: one press every ~2 s
LEAD = 600  # 'after' -> 'asked': the other game lands too (10 s); in a rezagado B marks the cutscene played here
TAIL = 600  # 'played' -> 'end': the last flags of the end come in

# Bools of a line that a branch may set but this scenario cannot expect to go 0 -> 1 on its own.
MAYBE_BOOLS = {
    "InsideGanonCastle_IsAllBarrierClear",  # only with all six barriers down
    "IsHyruleFieldBridgeClose",  # the stones' lines: it may open or stay; the escape line (Escape) expects it
}
EXPECT_BOOLS_ANYWAY = {("HyruleField_DoorOfTime_Escape", "IsHyruleFieldBridgeClose")}

# Cutscenes that already have their own scenario (historia-*, final-*): only the rezagado pair is written here, and
# only when the line changes the world. slug -> the existing scenario's name.
COVERED = {
    "KokiriForest_Meet_DekuTree_MeetDekuTree_01": "historia",  # + historia-largo, -fin, -tarde, -rezagado
    "KokiriForest_WarpFromInsideTheDekuTree": "historia-arbol",
    "HyruleField_Goto_HyruleCastle_Kaepora": "historia-kaepora",
    "PlayStormSong_fromBancLogic": "historia-tormenta",
    "Market_Child_DoorOfTime_Draw": "historia-espada",
    "KakarikoVillage_Arrive": "historia-kakariko",
    "Meet_FourCarpenter_ClearEvent": "historia-gerudo",
    "Market_Adult_Field_GetArrowLight": "historia-zelda",
    "InsideGanonCastle_Dungeon_BarrierRemove_Forest": "final-pruebas-sala",  # + final-pruebas-otra (near=)
    "CrumblingGanonCastle_Arrive": "final-huida-reloj",
    "InsideGanonCastle_Dungeon_Boss_GetMasterSword": "final-ganon-espada",
    "InsideGanonCastle_Dungeon_BossDie_2": "final-creditos",
}
HAS_REZAGADO = {"KokiriForest_Meet_DekuTree_MeetDekuTree_01": "historia-rezagado"}
# Ganondorf at the organ, his fall and Ganon: on test8's list, but branch orm/final takes them off it (the boss-room
# sync of room 8 plays them, docs/FINAL.md): a base pair for test8, no rezagado (their steps are the boss room's).
NO_REZAGADO = {"InsideGanonCastle_Dungeon_BossAppear_1", "InsideGanonCastle_Dungeon_BossDie_1",
               "InsideGanonCastle_Dungeon_BossAppear_2"}


@dataclass
class Row:
    event: str
    slug: str
    warp: str  # SCENE [START] [adult|child]
    block: str
    presses: int = 75  # 150 s of A presses; longer cutscenes more


B_KOK = "Bosque Kokiri (niño)"
B_CAM = "Campo, Ciudadela y Castillo de Hyrule (niño)"
B_KAK = "Kakariko, Cementerio, Montaña, Ciudad Goron (niño)"
B_ZOR = "Río y Dominio Zora, Jabu-Jabu (niño)"
B_TIE = "Templo del Tiempo y la huida de Zelda"
B_BOS = "Adulto: Sheik y el Bosque"
B_FUE = "Adulto: Montaña y Templo del Fuego"
B_AGU = "Adulto: Lago, Dominio Zora, Caverna de Hielo, Templo del Agua"
B_SOM = "Adulto: Kakariko en llamas y Templo de la Sombra"
B_ESP = "Valle, Fortaleza, Desierto y Templo del Espíritu"
B_FIN = "Los sabios, Zelda, Castillo de Ganon y el final"

R = Row
TABLE = [
    # ---- Bosque Kokiri (niño)
    R("KokiriForest_StopInFrontOfLinkHouse", "saria-casa", "KokiriForest_Child EntrancePos", B_KOK),
    R("KokiriForest_Meet_DekuTree_MeetDekuTree_01", "", "", B_KOK),
    R("InsideTheDekuTree_Arrive", "deku-llegada", "InsideTheDekuTree Entrance child", B_KOK),
    R("KokiriForest_WarpFromInsideTheDekuTree", "", "", B_KOK),
    R("KokiriForest_Get_StoneOfForest_Completed_Event", "mido-vuelta", "KokiriForest_Child After_DekuWither", B_KOK),
    R("KokiriForest_ArriveLostWood", "saria-puente", "KokiriForest_Child LostWoodBridge_FrontOfHyruleField", B_KOK),
    # ---- Campo, Ciudadela y Castillo (niño)
    R("HyruleField_Goto_HyruleCastle_Kaepora", "kaepora", "HyruleField_Child HyruleField_FrontOfLostWoodBridge", B_CAM),
    R("Dm_SY_BancChangeArrive_Market", "ciudadela", "Market_Child Market_Entrance", B_CAM),
    R("Dm_HrlCastle_0000", "castillo-subida", "HyruleCastle HyruleCastle_Entrance child", B_CAM),
    R("HyruleCastle_Taron_WakeUp", "talon", "HyruleCastle HyruleCastle_Entrance child", B_CAM),
    R("HyruleCastle_Goto_HyruleCastle_MeetZelda", "zelda-patio", "HyruleCastle HyruleCastle_Entrance child", B_CAM,
      125),
    R("3906666092_3949112793_2073510616_D_T", "nana-impa", "HyruleCastle HyruleCastle_Entrance child", B_CAM),
    R("Entry_LonLonRanch", "lonlon", "HyruleField_Child EntrancePos", B_CAM),
    R("Get_EponaSong", "epona-cancion", "HyruleField_Child EntrancePos", B_CAM),
    R("Dm_HrlField_0030", "campo-0030", "HyruleField_Child HyruleField_FrontOfLostWoodBridge", B_CAM),
    R("Dm_HrlField_0040", "campo-0040", "HyruleField_Child HyruleField_FrontOfLostWoodBridge", B_CAM),
    R("Dm_HrlField_0050", "campo-0050", "HyruleField_Child HyruleField_FrontOfLostWoodBridge", B_CAM),
    # ---- Kakariko, Cementerio, Montaña, Ciudad Goron (niño)
    R("Dm_Graveyard_0000", "cementerio", "Graveyard_Child GraveYard_KakarikoSide", B_KAK),
    R("PlayStormSong_fromBancLogic", "tormenta", "KakarikoVillage_Child Kakariko_WellSide", B_KAK),
    R("PlayStormSong_InWindmill", "tormenta-molino", "KakarikoVillage_Child Kakariko_WellSide", B_KAK),
    R("RoyalFamilyTomb_BlowOcarina", "tumba-real", "Graveyard_Child GraveYard_KakarikoSide", B_KAK),
    R("Obj_Graveyard_Monument_A_02", "cancion-sol", "Graveyard_Child GraveYard_KakarikoSide", B_KAK),
    R("DeathMountainTrail_Arrive", "montana-llegada", "DeathMountainTrail_Child MountainTrail_KakarikoSide", B_KAK),
    R("Dm_DeathMtTrail_0005", "montana-0005", "DeathMountainTrail_Child MountainTrail_KakarikoSide", B_KAK),
    R("GoronCity_InitDemo_Arrive", "goron-llegada", "GoronCity_Child GoronCity_MountainTrailSide", B_KAK),
    R("Get_StoneOfFire_MeetDarnia", "darunia", "GoronCity_Child GoronCity_MountainTrailSide", B_KAK),
    R("Get_StoneOfFire_ArriveFromKokiri", "saria-bosque", "LostWood_Child LostWood_Entrance", B_KAK),
    R("SacredForestMeadow_Field_MelodyGet_Saria", "saria-cancion",
      "SacredForestMeadow_Child SacredForestMeadow_FrontOfLostWood", B_KAK),
    R("Meet_Darunia_PlayOcarina", "darunia-baile", "GoronCity_Child GoronCity_MountainTrailSide", B_KAK),
    R("Get_StoneOfFire_DodongoCavernOpne", "dodongo-abre", "DeathMountainTrail_Child MountainTrail_GoronCitySide",
      B_KAK),
    R("Dm_DodongoCave_0000", "dodongo-dentro", "DodongoCavern DodongoCavern_MountainTrailSide child", B_KAK),
    R("Get_StoneOfFire_Complete", "rubi", "DeathMountainTrail_Child DodongoCavernFront", B_KAK, 100),
    # ---- Río y Dominio Zora, Jabu-Jabu (niño)
    R("Get_StoneOfWater_TalkKaepora_ZoraRiver", "kaepora-rio", "ZoraRiver_Child ZoraRiver_Entrance", B_ZOR),
    R("Dm_ZoraRiver_0005", "rio-0005", "ZoraRiver_Child ZoraRiver_Entrance", B_ZOR),
    R("Get_StoneOfWater_WaterfallSplit", "cascada", "ZoraRiver_Child ZoraRiver_FromZoraDomain", B_ZOR),
    R("Goto_ZoraDomain", "zora-llegada", "ZoraDomain_Child ZoraDomain_FromZoraRiver", B_ZOR),
    R("Get_StoneOfWater_ShowLetter", "carta-ruto", "ZoraDomain_Child ZoraDomain_FromZoraRiver", B_ZOR),
    R("Get_StoneOfWater_JabuJabuEat", "jabu-pez", "ZoraFountain_Child ZoraFountain_FromZoraDomain", B_ZOR),
    R("Get_StoneOfWater_ArriveJabuJabuBelly", "jabu-dentro",
      "InsideJabuJabuBelly InsideJabuJabuBelly_FromZoraFountain child", B_ZOR),
    R("Get_StoneOfWater_MeetRuto", "ruto", "InsideJabuJabuBelly InsideJabuJabuBelly_FromZoraFountain child", B_ZOR),
    R("Get_StoneOfWater_DiscoveryStone", "ruto-sala",
      "InsideJabuJabuBelly InsideJabuJabuBelly_FromZoraFountain child", B_ZOR),
    R("Get_StoneOfWater_RutoGetStone", "ruto-piedra",
      "InsideJabuJabuBelly InsideJabuJabuBelly_FromZoraFountain child", B_ZOR),
    R("Get_StoneOfWater_Complete", "zafiro", "ZoraFountain_Child ClearJabuJabu", B_ZOR),
    # ---- Templo del Tiempo y la huida de Zelda (el cambio de edad es D-R17)
    R("HyruleField_DoorOfTime_SendOff", "zelda-huye", "HyruleField_Child HyruleField_FrontOfMarket", B_TIE),
    R("Dm_TimeTemple_0000", "templo-tiempo", "Market_Child LightPrelude", B_TIE),
    R("DoorOfTime_DiscoverySword", "pedestal", "Market_Child LightPrelude", B_TIE),
    R("HyruleField_DoorOfTime_Escape", "ganondorf-puente", "HyruleField_Child StartPos_DoorOfTime", B_TIE),
    R("HyruleField_DoorOfTime_GetOcarinaOfTime", "ocarina-tiempo", "HyruleField_Child StartPos_DoorOfTime", B_TIE),
    R("Market_Child_DoorOfTime_OpenDoor", "puerta-tiempo", "Market_Child LightPrelude", B_TIE),
    R("Market_Child_DoorOfTime_Draw", "espada", "Market_Child LightPrelude", B_TIE),
    R("SageRoom_MeetGanondorf", "ganondorf-reino", "Market_Child LightPrelude", B_TIE),
    R("SageRoom_MeetRauru", "rauru", "Market_Child LightPrelude", B_TIE),
    R("Market_Child_Field_ChildToAdult", "a-adulto", "Market_Child LightPrelude", B_TIE),
    R("Market_Adult_Field_AdultToChild", "a-nino", "Market_Adult LightPrelude", B_TIE),
    # ---- Adulto: Sheik y el Bosque
    R("Market_Adult_AllSage_Start", "sheik", "Market_Adult LightPrelude", B_BOS),
    R("Dm_TimeTemple_0065", "templo-adulto", "Market_Adult LightPrelude", B_BOS),
    R("Dm_SY_BancChangeArrive_LostWood", "prado", "SacredForestMeadow_Adult SacredForestMeadow_FrontOfLostWood",
      B_BOS),
    R("SacredForestMeadow_Field_Moblin_Club", "moblin", "SacredForestMeadow_Adult SacredForestMeadow_FrontOfLostWood",
      B_BOS),
    R("ForestTemple_SageOfForest_MelodyGet", "minueto", "SacredForestMeadow_Adult OutsideOfForestTemple", B_BOS),
    R("ForestTemple_SageOfForest_BlockerMove", "mido-paso", "LostWood_Adult LostWood_FrontOfSacredForestMeadow",
      B_BOS),
    R("Dm_ForestTemple_0000", "bosque-entrada", "ForestTemple EntrancePos adult", B_BOS),
    R("ForestTemple_SageOfForest_SageRoom", "saria-sabia", "KokiriForest_Adult WarpFromForestTemple", B_BOS),
    R("ForestTemple_SageOfForest_EscapeFromTempleOutSide", "brote", "KokiriForest_Adult WarpFromForestTemple",
      B_BOS),
    # ---- Adulto: Montaña y Templo del Fuego
    R("Dm_DeathMtTrail_0006", "montana-0006", "DeathMountainTrail_Adult MountainTrail_KakarikoSide", B_FUE),
    R("Dm_DeathMtTrail_0007", "montana-0007", "DeathMountainTrail_Adult MountainTrail_KakarikoSide", B_FUE),
    R("GoTo_DeathMountainCrater_FromGoronCity", "crater", "DeathMountainCrater_Adult Crater_GoronCitySide", B_FUE),
    R("SageOfFire_MelodyGetFire", "bolero", "DeathMountainCrater_Adult Crater_GoronCitySide", B_FUE),
    R("Demo_FireTemple_MeetAgainDarniaEvent", "darunia-templo", "FireTemple EntrancePos adult", B_FUE),
    R("FireTemple_SageOfFire_SageRoom", "darunia-sabio", "DeathMountainCrater_Adult FireBolero", B_FUE),
    R("Demo_SageOfFire_Arrive_FireBolero", "volvagia-crater", "DeathMountainCrater_Adult FireBolero", B_FUE),
    R("FireTemple_SageOfFire_Kakariko", "volvagia-kakariko", "KakarikoVillage_Adult KakarikoVillage_Entrance", B_FUE),
    # ---- Adulto: Lago, Dominio Zora, Caverna de Hielo, Templo del Agua
    R("Goto_LakeHylia", "lago-seco", "HyruleField_Adult WaterSerenade", B_AGU),
    R("Dm_LakeHrl_0003", "lago-valle", "HyruleField_Adult LakeHylia_FromGerudoValley", B_AGU),
    R("Dm_LakeHrl_0005", "lago-dominio", "HyruleField_Adult ZoraLakeHylia_FromZoraDomain", B_AGU),
    R("FromZoraDomain_arriveZoraFountain", "fuente-helada", "ZoraFountain_Adult ZoraFountain_FromZoraDomain", B_AGU),
    R("SageOfWater_MelodyGet_Water", "serenata", "IceCavern IceCavern_FromZoraFountain adult", B_AGU),
    R("Dm_WaterTemple_0000", "agua-entrada", "HyruleField_Adult OutsideOfWaterTemple", B_AGU),
    R("Demo_WaterTemple_MeetAgainRutoEvent", "ruto-templo", "WaterTemple WaterTemple_FromLakeHylia adult", B_AGU),
    R("SageOfWater_SageRoom", "ruto-sabia", "HyruleField_Adult WaterSerenade", B_AGU),
    R("Demo_SageOfWater_Arrive_WaterSerenade", "lago-lleno", "HyruleField_Adult WaterSerenade", B_AGU),
    # ---- Adulto: Kakariko en llamas y Templo de la Sombra
    R("KakarikoVillage_Arrive", "kakariko", "KakarikoVillage_Adult Kakariko_WellSide", B_SOM),
    R("KakarikoVillage_FlashbackScenes", "impa-historia", "KakarikoVillage_Adult Kakariko_WellSide", B_SOM),
    R("KakarikoVillage_MelodyGet_Shadow", "nocturno", "KakarikoVillage_Adult Kakariko_WellSide", B_SOM),
    R("Dm_ShadowTemple_0000", "sombra-abre", "ShadowTemple Entrance adult", B_SOM),
    R("ShadowTemple_SageOfShadow_SageRoom", "impa-sabia", "Graveyard_Adult ShadowNocturne", B_SOM),
    R("ShadowTemple_Warp_Graveyard", "bongo-cementerio", "Graveyard_Adult ShadowNocturne", B_SOM),
    # ---- Valle, Fortaleza, Desierto y Templo del Espíritu (niño y adulto)
    R("Dm_GerudoValley_0000", "valle", "GerudoValley_Adult GerudoValley_Entrance", B_ESP),
    R("Dm_Gerudo_0000", "fortaleza", "GerudoFortress_Adult GerudoFortress_FromGerudoValley", B_ESP),
    R("Meet_FourCarpenter_ClearEvent", "gerudo", "GerudoFortress_Adult GerudoFortress_FromGerudoValley", B_ESP),
    R("FromWasteland_ArriveColossus", "coloso", "DesertColossus_Adult DesertColossus_FromHountedWasteland", B_ESP),
    R("Dm_MelodyGet_Spilit", "requiem", "DesertColossus_Adult DesertColossus_FromHountedWasteland", B_ESP),
    R("SageOfSpirit_MeetNabooru", "nabooru", "SpiritTemple SpiritTemple_Entrance child", B_ESP),
    R("SageOfSpirit_NaboorIsTakenAway", "nabooru-raptada",
      "DesertColossus_Child DesertColossus_FromSpiritTempleLeftHand", B_ESP),
    R("SageOfSpirit_MiddleBossStart", "nudillo", "SpiritTemple SpiritTemple_Entrance adult", B_ESP),
    R("SageOfSpirit_MiddleBossEnd", "nudillo-cae", "SpiritTemple SpiritTemple_Entrance adult", B_ESP),
    R("SageOfSpirit_WalkBackTimeTemple", "espiritu-vuelta", "Market_Child LightPrelude", B_ESP),
    R("SpiritTemple_SageOfSpirit_SageRoom", "nabooru-sabia",
      "DesertColossus_Adult DesertColossus_FromSpiritTempleEntrance", B_ESP),
    R("SpiritTemple_Level_Room_34_Escape", "twinrova-coloso",
      "DesertColossus_Adult DesertColossus_FromSpiritTempleEntrance", B_ESP),
    # ---- Los sabios, Zelda, Castillo de Ganon y el final
    R("Market_Adult_Field_GetLightPrelude", "preludio", "Market_Adult LightPrelude", B_FIN),
    R("Market_Adult_Field_GetArrowLight", "zelda", "Market_Adult LightPrelude", B_FIN, 200),
    R("GanonCastle_Adult_Field_EntryGanonCastle", "castillo-ganon", "GanonCastle GanonCastleEntrance adult", B_FIN),
    R("GanonCastle_Adult_Field_Rainbow", "arcoiris", "GanonCastle GanonCastleEntrance adult", B_FIN),
    R("GanonCastle_Dungeon_SeeTowerBarrier", "barrera-torre", "InsideGanonCastle Entrance adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Forest", "prueba-bosque", "InsideGanonCastle Entrance adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Fire", "prueba-fuego", "InsideGanonCastle Entrance adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Water", "prueba-agua", "InsideGanonCastle Entrance adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Shadow", "prueba-sombra", "InsideGanonCastle Entrance adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Spirit", "prueba-espiritu", "InsideGanonCastle Entrance adult",
      B_FIN),
    R("InsideGanonCastle_Dungeon_BarrierRemove_Light", "prueba-luz", "InsideGanonCastle Entrance adult", B_FIN),
    R("Dm_InsideGanonCastle_0007", "ganon-0007", "InsideGanonCastle Entrance adult", B_FIN),
    R("Dm_InsideGanonCastle_0035", "ganon-0035", "InsideGanonCastle Entrance adult", B_FIN),
    R("Dm_InsideGanonCastle_0040", "ganon-0040", "InsideGanonCastle Entrance adult", B_FIN),
    R("Dm_InsideGanonCastle_0060", "ganon-0060", "InsideGanonCastle Entrance adult", B_FIN),
    R("Dm_InsideGanonCastle_0100", "ganon-0100", "GanonCastle GanonCastleEntrance_FromInside adult", B_FIN),
    R("InsideGanonCastle_Dungeon_BossAppear_1", "ganondorf-organo", "InsideGanonCastle StartPos_Defeat_Ganon adult",
      B_FIN),
    R("InsideGanonCastle_Dungeon_BossDie_1", "ganondorf-cae", "InsideGanonCastle StartPos_Defeat_Ganon adult", B_FIN),
    R("CrumblingGanonCastle_Arrive", "", "", B_FIN),
    R("EscapeFromCrumblingGanonCastle_3F_Fookshot", "huida-3f",
      "CrumblingGanonCastle CrumblingGanonCastle_StartPos adult", B_FIN),
    R("EscapeFromCrumblingGanonCastle_2F_FallDown", "huida-2f",
      "CrumblingGanonCastle CrumblingGanonCastle_StartPos adult", B_FIN),
    R("EscapeFromCrumblingGanonCastle_1F_BreakRoom", "huida-1f",
      "CrumblingGanonCastle CrumblingGanonCastle_StartPos adult", B_FIN),
    R("EscapeFromCrumblingGanonCastle_EscapeComplete", "huida-fuera",
      "CrumblingGanonCastle CrumblingGanonCastle_StartPos adult", B_FIN),
    R("Dm_InsideGanonCastle_0190", "ruinas", "CrumblingGanonCastle CrumblingGanonCastle_StartPos adult", B_FIN),
    R("InsideGanonCaslte_Dungeon_Escape", "ganondorf-ruinas", "FinalBattleField FinalBattleField_Entrance adult",
      B_FIN),
    R("InsideGanonCastle_Dungeon_BossAppear_2", "ganon", "FinalBattleField StartPos_Defeat_Ganon adult", B_FIN),
    R("InsideGanonCastle_Dungeon_Boss_GetMasterSword", "", "", B_FIN),
    R("InsideGanonCastle_Dungeon_BossBind0", "sabios-atan", "FinalBattleField StartPos_Defeat_Ganon_InBattle adult",
      B_FIN),
    R("InsideGanonCastle_Dungeon_BossDie_2", "creditos", "FinalBattleField StartPos_Defeat_Ganon_InBattle adult",
      B_FIN, 300),
    R("InsideGanonCastle_Dungeon_StaffRoll_HyruleCastle", "creditos-castillo",
      "FinalBattleField StartPos_Defeat_Ganon_InBattle adult", B_FIN, 200),
]


# ---------------------------------------------------------------- the list and the doc


@dataclass
class Line:
    event: str
    solo: bool = False
    near: int | None = None
    flags: list[str] = field(default_factory=list)  # HavePlayedEvent.Dm_* and world Bools, in the line's order
    steps: dict[str, list[str]] = field(default_factory=dict)  # Step_X -> its values in GameDataList order
    comment: str = ""

    @property
    def bools(self) -> list[str]:
        return [f for f in self.flags if not f.startswith("HavePlayedEvent.")]


def read_list(path: Path = LIST) -> list[Line]:
    """The lines of a shared_story.txt, in order, with the comment line above each."""
    out, comment = [], ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#"):
            comment = raw.lstrip("# ").strip()
            continue
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        ln = Line(words[0], comment=comment)
        for w in words[1:]:
            if w == "solo":
                ln.solo = True
            elif w.startswith("near="):
                ln.near = int(w[5:])
            elif w.startswith("step:"):
                name, vals = w[5:].split("=", 1)
                ln.steps[name] = vals.split(",")
            else:
                ln.flags.append(w)
        out.append(ln)
        comment = ""
    return out


def read_doc(path: Path = DOC) -> dict[str, tuple[str, dict[str, list[str]]]]:
    """event -> (what it is, in Spanish; {Step_X: [values a branch may move it to]}) from section 3 of
    HISTORIA_COMPLETA.md."""
    text = path.read_text(encoding="utf-8")
    sec = text.split("## 3. La lista", 1)[1].split("Valores de los 17", 1)[0]
    out = {}
    for l in sec.splitlines():
        if not l.startswith("| "):
            continue
        cols = [c.strip() for c in l.strip().strip("|").split("|")]
        m = re.match(r"`([^`]+)`", cols[1]) if len(cols) >= 5 else None
        if not m:
            continue
        steps: dict[str, list[str]] = {}
        for name, val in re.findall(r"(\w+)=(\w+)", re.sub(r"`[^`]*`", "", cols[4])):
            steps.setdefault("Step_" + name, [])
            if val not in steps["Step_" + name]:
                steps["Step_" + name].append(val)
        out[m.group(1)] = (cols[0], steps)
    return out


# ---------------------------------------------------------------- the scenarios


@dataclass
class Plan:
    row: Row
    line: Line
    what: str  # Spanish, from the doc
    targets: dict[str, list[str]]  # Step_X -> the values it may end on

    @property
    def scene(self) -> str:
        return self.row.warp.split()[0]

    @property
    def bools(self) -> list[str]:
        return [b for b in self.line.bools
                if b not in MAYBE_BOOLS or (self.line.event, b) in EXPECT_BOOLS_ANYWAY]

    @property
    def important(self) -> bool:
        """A cutscene that changes the world for everyone: a story step or a world Bool on its line."""
        return bool(self.line.steps or self.line.bools)

    def at_least(self, step: str) -> list[str]:
        """The values of a step from the first one this cutscene may move it to, on (a step only moves forward)."""
        vals = self.line.steps[step]
        first = min(vals.index(v) for v in self.targets[step])
        return vals[first:]


def plans() -> list[Plan]:
    lines = {ln.event: ln for ln in read_list()}
    doc = read_doc()
    out = []
    for row in TABLE:
        ln = lines[row.event]
        what, doc_steps = doc.get(row.event, ("", {}))
        targets = {s: v for s, v in doc_steps.items() if s in ln.steps}
        out.append(Plan(row, ln, what, targets))
    return out


def rx(s: str) -> str:
    return re.escape(s)


def alt(vals: list[str]) -> str:
    return vals[0] if len(vals) == 1 else "(" + "|".join(vals) + ")"


def presses(n: int) -> list[str]:
    out = []
    for _ in range(n):
        out += ["wait 108", "press A 6"]
    return out


def ticks(lines: list[str]) -> int:
    t = 0
    for l in lines:
        w = l.split("#", 1)[0].split()
        if not w:
            continue
        if w[0] == "wait":
            t += int(w[1])
        elif w[0] == "press":
            t += int(w[2]) if len(w) >= 3 else 12
    return t


def timeout(lines: list[str]) -> int:
    secs = ticks(lines) / 60
    return max(400, int((secs * 1.5 + 120) // 50 * 50 + 50))


def run_line(name: str, t: int) -> str:
    return (f"# Run: python tools/orm/harness/r03_pareja.py --scenario {name} --transport ws --scen-dir "
            f"tools/orm/harness/escenarios --timeout {t} --env {SWITCHES}")


def headers(t: int) -> list[str]:
    return ["#! noload", "#! start coop: test warp done", f"#! switches {SWITCHES}", f"#! timeout {t}"]


def started_lines(g: str, p: Plan, win: str) -> list[str]:
    e, s = rx(p.line.event), rx(p.scene)
    if g == "A":
        return [f"#! expect A coop: story: owner: our game started '{e}' in {s}; STORY_EVENT start to the others "
                f"within {win}",
                f"#! expect A coop: story: STORY_EVENT start sent \\('{e}' in {s}, within {win}",
                f"#! expect A coop: event running '{e}' within {win}"]
    return [f"#! expect B coop: story: STORY_EVENT start received from P\\d \\('{e}' in {s}\\): starting within {win}",
            f"#! expect B coop: story: event running '{e}' as P\\d asked within {win}",
            f"#! expect B coop: event running '{e}' within {win}"]


def end_lines(g: str, p: Plan, win: str, played: bool = True) -> list[str]:
    out = []
    if played:
        out.append(f"#! expect {g} coop: story: HavePlayedEvent\\.{rx(p.line.event)} 0 -> 1 within {win}")
    for b in p.bools:
        out.append(f"#! expect {g} coop: story: {rx(b)} 0 -> 1 within {win}")
    for step, vals in p.targets.items():
        out.append(f"#! expect {g} coop: story: {step} \\S+ -> {alt(vals)} within {win}")
    return out


def enum_lines(p: Plan) -> list[str]:
    return [f"enum {s}" for s in p.targets]


def enum_expects(g: str, p: Plan, win: str) -> list[str]:
    return [f"#! expect {g} coop: test enum {s} \\(0x[0-9a-f]{{8}}\\): find_type -?\\d+, value {alt(p.at_least(s))} "
            f"\\(0x within {win}" for s in p.targets]


def describe(p: Plan) -> str:
    rule = "solo" if p.line.solo else (f"near={p.line.near}" if p.line.near else "shared")
    moves = "; ".join(f"{s} -> {'/'.join(v)}" for s, v in p.targets.items())
    bools = ", ".join(p.line.bools)
    extra = "; ".join(x for x in (moves, bools) if x)
    return f"{p.line.event} ({rule}{'; ' + extra if extra else ''})"


def build(p: Plan) -> tuple[str, str]:
    """The pair historia-<slug>: the cutscene seen by both (shared) or by A alone (solo)."""
    name = f"historia-{p.row.slug}"
    e = p.line.event
    body_a = ["mark start", f"warp {p.row.warp}", *LOAD, f"wait {LEAD}                # the other game lands too",
              "mark asked", f"event {e}", *presses(p.row.presses), "mark played", f"wait {TAIL}", "mark end"]
    body_b = ["mark start", f"warp {p.row.warp}", *LOAD, f"wait {LEAD}                # A asks for the cutscene now",
              "mark asked", *presses(p.row.presses), "mark played", f"wait {TAIL}", "mark end"]
    t = timeout(body_a)
    what = p.line.comment or e
    if p.line.solo:
        a = [f"# {name} (ORM, docs/HISTORIAS_ESCENARIOS.md; block {p.row.block}), A: {what}",
             f"# A solo line (D-R20, ZMP D-067): only who starts it sees it, its flags reach the other game. Both "
             f"games warp to {p.row.warp};",
             "# A asks for it with the test command `event` and both press A every 2 s.",
             run_line(name, t), f"# {GENERATED} Not run yet.", *headers(t),
             f"#! expect A coop: story: '{rx(e)}' is solo: only we see it, its flags are shared within asked..end",
             f"#! expect A coop: event running '{rx(e)}' within asked..end",
             f"#! never A coop: story: STORY_EVENT start sent \\('{rx(e)}'",
             *end_lines("A", p, "asked..end"), *body_a]
        b = [f"# {name}, B: lands where A does and only watches: its game never runs the solo cutscene, never gets a "
             "start for it,", "# and still ends with its HavePlayedEvent, its world Bools and its story steps.",
             f"# {GENERATED}", *headers(t),
             f"#! never B coop: event running '{rx(e)}' within after..end",
             f"#! never B coop: story: STORY_EVENT start received from P\\d \\('{rx(e)}'",
             f"#! never B coop: story: event running '{rx(e)}' as P\\d asked",
             *end_lines("B", p, "after..end"), *body_b]
        return "\n".join(a) + "\n", "\n".join(b) + "\n"
    near = (f" Its line has near={p.line.near} (docs/FINAL.md): both land on the same spot, so B is within it."
            if p.line.near else "")
    s = rx(p.scene)
    a = [f"# {name} (ORM, docs/HISTORIAS_ESCENARIOS.md; block {p.row.block}), A: {what}",
         f"# Both games warp to {p.row.warp}; A asks for {e} with the test command `event` (the hook's",
         f"# request by name, CINEMATICAS.md P1) and both press A every 2 s for {p.row.presses * 2} s to move the texts "
         f"on.{near}", run_line(name, t), f"# {GENERATED} Not run yet.", *headers(t),
         *started_lines("A", p, "asked..end"), *end_lines("A", p, "asked..end"),
         f"#! never A coop: story: '{rx(e)}' runs here again", *body_a]
    b = [f"# {name}, B: lands where A does, follows A's STORY_EVENT start at once (the simultaneous start) and ends",
         "# with the line's flags; it never starts the cutscene a second time.", f"# {GENERATED}", *headers(t),
         *started_lines("B", p, "after..end"), *end_lines("B", p, "after..end"),
         f"#! never B coop: story: STORY_EVENT start received from P\\d \\('{rx(e)}' in {s}\\): ignored",
         f"#! never B coop: story: STORY_EVENT start received from P\\d \\('{rx(e)}' in {s}\\): starting within "
         "played..end", *body_b]
    return "\n".join(a) + "\n", "\n".join(b) + "\n"


def build_rezagado(p: Plan) -> tuple[str, str]:
    """The pair historia-<slug>-rezagado: B played it before (its own HavePlayedEvent set 10 s before A asks), does
    not see it again, and still ends with A's world."""
    name = f"historia-{p.row.slug}-rezagado"
    e, s = rx(p.line.event), rx(p.scene)
    body_a = ["mark start", f"warp {p.row.warp}", *LOAD, f"wait {LEAD}                # B marks it played meanwhile",
              *enum_lines(p), "mark asked", f"event {p.line.event}", *presses(p.row.presses), "mark played",
              *enum_lines(p), f"wait {TAIL}", "mark end"]
    body_b = ["mark start", f"warp {p.row.warp}", *LOAD,
              f"flag HavePlayedEvent.{p.line.event} 1",
              "wait 30",
              f"flag HavePlayedEvent.{p.line.event} 1   # twice: a queued write may miss the frame the game applies",
              "mark flagged", *enum_lines(p), f"wait {LEAD - 30}", "mark asked",
              f"wait {p.row.presses * PRESS_TICKS}       # only watches while A plays it",
              "mark played", *enum_lines(p), f"wait {TAIL}", "mark end"]
    t = timeout(body_a)
    a = [f"# {name} (ORM, docs/HISTORIAS_ESCENARIOS.md; block {p.row.block}), A: {p.line.comment or e}",
         f"# The latecomer (historia-rezagado's pattern): B marks {p.line.event} played on its own",
         "# save 10 s before A asks for it, so it ignores A's start; A plays it to its end pressing A every 2 s and",
         "# B must still end with A's world: the same Bools and the same story steps (BANDERAS_ENUM.md).",
         run_line(name, t), f"# {GENERATED} Not run yet.", *headers(t),
         f"#! expect A coop: story: owner: our game started '{e}' in {s}; STORY_EVENT start to the others within "
         "asked..end",
         f"#! expect A coop: story: HavePlayedEvent\\.{e} 0 -> 1 within after..end",
         *end_lines("A", p, "asked..end", played=False), *body_a]
    b = [f"# {name}, B: marks the cutscene played here at once (A's start is then ignored: 'is set here'), reads",
         "# the steps, only watches while A plays it, and reads the steps again at the end (the target or later).",
         f"# {GENERATED}", *headers(t),
         f"#! expect B coop: test flag HavePlayedEvent\\.{e} \\(0x[0-9a-f]{{8}}\\) = 1 within after..flagged",
         f"#! expect B coop: story: STORY_EVENT start received from P\\d \\('{e}' in {s}\\): ignored, "
         f"HavePlayedEvent\\.{e} is set here within flagged..end",
         f"#! never B coop: story: STORY_EVENT start received from P\\d \\('{e}' in {s}\\): starting",
         f"#! never B coop: story: event running '{e}' as P\\d asked",
         *end_lines("B", p, "asked..end", played=False), *enum_expects("B", p, "played..end"), *body_b]
    return "\n".join(a) + "\n", "\n".join(b) + "\n"


def wants_rezagado(p: Plan) -> bool:
    e = p.line.event
    return p.important and not p.line.solo and e not in HAS_REZAGADO and e not in NO_REZAGADO


def outputs() -> dict[str, str]:
    """file name -> text, every file this script writes."""
    out = {}
    for p in plans():
        e = p.line.event
        if e not in COVERED:
            a, b = build(p)
            out[f"historia-{p.row.slug}.A.txt"], out[f"historia-{p.row.slug}.B.txt"] = a, b
        if wants_rezagado(p):
            a, b = build_rezagado(p)
            out[f"historia-{p.row.slug}-rezagado.A.txt"], out[f"historia-{p.row.slug}-rezagado.B.txt"] = a, b
    return out


def table() -> str:
    """The Markdown table of docs/HISTORIAS_ESCENARIOS.md, by block."""
    rows, block = [], None
    head = "| Cinemática | Evento | Regla | Escena (warp) | Escenario | Rezagado | B termina con |"
    for p in plans():
        if p.row.block != block:
            block = p.row.block
            rows += ["", f"### {block}", "", head, "|---|---|---|---|---|---|---|"]
        e = p.line.event
        rule = "**solo**" if p.line.solo else (f"near={p.line.near}" if p.line.near else "compartida")
        scen = f"`{COVERED[e]}` (ya estaba)" if e in COVERED else f"`historia-{p.row.slug}`"
        if e in HAS_REZAGADO:
            rez = f"`{HAS_REZAGADO[e]}` (ya estaba)"
        elif wants_rezagado(p):
            rez = f"`historia-{p.row.slug}-rezagado`"
        else:
            rez = "-"
        warp = f"`{p.row.warp}`" if p.row.warp else "(la suya)"
        end = [f"{s[5:]}={'/'.join(v)}" for s, v in p.targets.items()] + [f"`{b}`" for b in p.bools]
        rows.append(f"| {p.what} | `{e}` | {rule} | {warp} | {scen} | {rez} | {', '.join(end) or '-'} |")
    return "\n".join(rows).lstrip("\n") + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--table", action="store_true", help="print the Markdown table instead of writing the files")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args()
    if not LIST.is_file():
        print(f"no story list at {LIST} (set ORM_STORY_LIST)", file=sys.stderr)
        return 2
    if a.table:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(table())
        return 0
    files = outputs()
    out = Path(a.out)
    for name, text in files.items():
        (out / name).write_text(text, encoding="utf-8", newline="\n")
    pairs = len(files) // 2
    rez = sum(1 for n in files if n.endswith("-rezagado.A.txt"))
    print(f"{pairs} pairs ({pairs - rez} historia-*, {rez} historia-*-rezagado) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
