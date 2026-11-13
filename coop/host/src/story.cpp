// ORM: story cutscenes for everyone in the scene, solo ones after a blue warp, story steps forward
//   only. The rules are described in coop/host/story.hpp.
// Doc: oot-remake docs/CINEMATICAS.md, HISTORIA_COMPLETA.md (D-R15, D-R20).
// Switch: LICHII_ORM_STORY_EVENTS (on in the kit; LICHII_COOP_STORY before test9),
// LICHII_ORM_STORY_LIST.
// Test: tests/host/story_test.cpp (+ story_list_data.inc), tests/lichii/story_test.cpp.

#include "coop/host/story.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "coop/host/game_flags.hpp"
#include "coop/protocol/codec.hpp"

namespace coop::host {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && is_space(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

// A flag token: 0x and 1 to 8 hex digits, or a printable ASCII name.
std::optional<std::uint32_t> flag_of(std::string_view token) {
  if (token.size() >= 3 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    const std::string_view digits = token.substr(2);
    if (digits.size() > 8) return std::nullopt;
    std::uint32_t hash = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (ec != std::errc() || end != digits.data() + digits.size() || hash == 0) return std::nullopt;
    return hash;
  }
  for (const char c : token) {
    if (c < 0x21 || c > 0x7e) return std::nullopt;
  }
  return protocol::flag_hash(token);
}

std::string ms(std::uint64_t us) { return std::to_string(us / 1000) + " ms"; }

constexpr std::string_view kStepPrefix = "step:";
constexpr std::string_view kSolo = "solo";
constexpr std::string_view kNearPrefix = "near=";

// near=M: metres, 1 to 1000 (oot-remake docs/FINAL.md).
std::optional<float> near_of(std::string_view token) {
  token.remove_prefix(kNearPrefix.size());
  if (token.empty() || token.size() > 8) return std::nullopt;
  for (const char ch : token) {
    if (!((ch >= '0' && ch <= '9') || ch == '.')) return std::nullopt;
  }
  try {
    const float metres = std::stof(std::string(token));
    if (!(metres >= 1.0f && metres <= 1000.0f)) return std::nullopt;
    return metres;
  } catch (...) {
    return std::nullopt;
  }
}

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// step:Name=V1,V2,...: a story step and its values, in the game's order.
std::optional<StoryStep> step_of(std::string_view token, std::string& why) {
  token.remove_prefix(kStepPrefix.size());
  const std::size_t eq = token.find('=');
  if (eq == std::string_view::npos || eq == 0) {
    why = "a step is step:Name=Value1,Value2,...";
    return std::nullopt;
  }
  StoryStep step;
  step.name = std::string(token.substr(0, eq));
  const auto hash = flag_of(step.name);
  if (!hash) {
    why = "a step is a printable name or 0x and 1 to 8 hex digits";
    return std::nullopt;
  }
  step.hash = *hash;
  std::string_view rest = token.substr(eq + 1);
  while (true) {
    const std::size_t comma = rest.find(',');
    const std::string_view value = rest.substr(0, comma);
    const auto value_hash = value.empty() ? std::nullopt : flag_of(value);
    if (!value_hash) {
      why = "a step's value is a printable name or 0x and 1 to 8 hex digits";
      return std::nullopt;
    }
    if (std::find(step.values.begin(), step.values.end(), *value_hash) != step.values.end()) {
      why = "a step lists a value once";
      return std::nullopt;
    }
    step.values.push_back(*value_hash);
    step.value_names.emplace_back(value);
    if (step.values.size() > EnumFlags::kMaxValues) {
      why = "too many step values";
      return std::nullopt;
    }
    if (comma == std::string_view::npos) break;
    rest = rest.substr(comma + 1);
  }
  return step;
}

}  // namespace

// The built-in list: the story cutscenes of the whole game, child and adult
// (Kokiri Forest to the ending), with the HavePlayedEvent flags of the
// timelines each plays, the world-state Bools it sets and the story steps it
// moves. Read from the romfs with oot-remake/tools/romfs (story_flows.py: the
// game's EventSetting table gives each event's flow and entry point, the flow
// graph what its branches play, set and move; GameDataList every hash and each
// step's values in order: tests/host/story_test.cpp checks them). The Deku
// Tree meeting is seen end to end in a game; the rest is read, not played
// (oot-remake docs/HISTORIA_COMPLETA.md).
const char* const kDefaultStoryList =
    "# ORM story cutscenes for everyone (LICHII_ORM_STORY_EVENTS=1): the story events that play on\n"
    "# every game in the same scene when one player's game starts them. One event a line, by\n"
    "# the name the game gives it; HavePlayedEvent.<event> is always shared, and each flag after\n"
    "# the name is shared with it too: the HavePlayedEvent.Dm_* of the timelines it plays, the\n"
    "# Bools it sets at its end, and as step:Name=Value,... the story steps (Enum flags, every\n"
    "# value in GameDataList order) it moves; a step only moves forward. The word solo after "
    "the name:\n"
    "# only who starts it sees it (ZMP D-067: the cutscene after a blue warp is the warper's),\n"
    "# its flags are shared all the same. near=M after the name: only the games whose Link is\n"
    "# within M m of where its starter stood see it (ZMP D-188: a cutscene of one room of a\n"
    "# scene, the trials of Ganon's Castle, is that room's); its flags are shared all the same.\n"
    "#\n"
    "# The whole game, child and adult, read from the romfs (oot-remake/tools/romfs: the\n"
    "# EventSetting table names each event's flow and entry point, the flows say what it plays\n"
    "# and moves, GameDataList has every name). Each comment says the flow and what its\n"
    "# branches may play and move. Not listed: the boss rooms' intros and deaths, Ganondorf's and\n"
    "# Ganon's too (the boss-room sync plays them; oot-remake docs/FINAL.md), dungeon mechanism cutscenes (Poe sisters, water levels,\n"
    "# rooms that spin), minigames, the horse (each player's), Gerudo guards taking one Link to\n"
    "# the cell (only that Link goes), chests and item gets, the boss rush.\n"
    "# ---- Kokiri Forest (child)\n"
    "# Saria greets Link at his house (Npc_Kokiri_Saria_Child; plays Dm_Kokiri_0060).\n"
    "KokiriForest_StopInFrontOfLinkHouse HavePlayedEvent.Dm_Kokiri_0060\n"
    "# Meeting the Great Deku Tree; the end opens its mouth (Meet_DekuTree; plays "
    "Dm_Kokiri_0070, Dm_Kokiri_0075; moves Meet_DekuTree to Complete, Get_StoneOfForest to "
    "GoInside).\n"
    "KokiriForest_Meet_DekuTree_MeetDekuTree_01 HavePlayedEvent.Dm_Kokiri_0070 "
    "HavePlayedEvent.Dm_Kokiri_0075 KokiriForest_Meet_DekuTree_OpenDekuTreeMouth "
    "step:Step_Meet_DekuTree=NotReady,Ready,Step01,Step02,Step03,Complete "
    "step:Step_Get_StoneOfForest=NotReady,Ready,GoInside,MeetBoss,GoToBossRoom,DefeatBoss,GoToWa"
    "rpPoint,Complete\n"
    "# Arriving inside the Deku Tree the first time (Dm_SY_BancChangeArrive; plays "
    "Dm_Deku_0000; moves Get_StoneOfForest to MeetBoss).\n"
    "InsideTheDekuTree_Arrive HavePlayedEvent.Dm_Deku_0000 "
    "step:Step_Get_StoneOfForest=NotReady,Ready,GoInside,MeetBoss,GoToBossRoom,DefeatBoss,GoToWa"
    "rpPoint,Complete\n"
    "# After Gohma's blue warp: the Deku Tree dies, Kokiri Emerald [solo: only who starts it "
    "sees it] (Dm_SY_BancChange_EscapeDungeon; plays Dm_Kokiri_0080, Dm_Kokiri_0095, "
    "Dm_Kokiri_0090, Dm_Kokiri_0130, Dm_Kokiri_0140; moves Get_StoneOfForest to Complete, "
    "Goto_HyruleCastle to Step01).\n"
    "KokiriForest_WarpFromInsideTheDekuTree solo HavePlayedEvent.Dm_Kokiri_0080 "
    "HavePlayedEvent.Dm_Kokiri_0095 HavePlayedEvent.Dm_Kokiri_0090 "
    "HavePlayedEvent.Dm_Kokiri_0130 HavePlayedEvent.Dm_Kokiri_0140 "
    "step:Step_Get_StoneOfForest=NotReady,Ready,GoInside,MeetBoss,GoToBossRoom,DefeatBoss,GoToWa"
    "rpPoint,Complete "
    "step:Step_Goto_HyruleCastle=NotReady,Ready,Step01,Step02,Step02_01,Step03,Step03_01,Step03_"
    "02,Step04,Complete\n"
    "# Back from the Tree: Mido and the Kokiri (Npc_Kokiri_Mido_Child; plays Dm_Kokiri_0145).\n"
    "KokiriForest_Get_StoneOfForest_Completed_Event HavePlayedEvent.Dm_Kokiri_0145\n"
    "# Saria on the bridge gives the Fairy Ocarina (Goto_HyruleCastle; plays Dm_Kokiri_0150, "
    "Dm_Kokiri_0155).\n"
    "KokiriForest_ArriveLostWood HavePlayedEvent.Dm_Kokiri_0150 HavePlayedEvent.Dm_Kokiri_0155\n"
    "\n"
    "# ---- Hyrule Field, Castle Town, Hyrule Castle (child)\n"
    "# Kaepora Gaebora outside the forest (Goto_HyruleCastle; plays Dm_HrlField_0000, "
    "Dm_HrlField_0005, Dm_HrlField_0010; moves Goto_HyruleCastle to Step02).\n"
    "HyruleField_Goto_HyruleCastle_Kaepora HavePlayedEvent.Dm_HrlField_0000 "
    "HavePlayedEvent.Dm_HrlField_0005 HavePlayedEvent.Dm_HrlField_0010 "
    "step:Step_Goto_HyruleCastle=NotReady,Ready,Step01,Step02,Step02_01,Step03,Step03_01,Step03_"
    "02,Step04,Complete\n"
    "# Castle Town, the first time across the drawbridge (Dm_SY_BancChangeArrive; plays "
    "Dm_HrlCastle_0005; moves Goto_HyruleCastle to Step02_01).\n"
    "Dm_SY_BancChangeArrive_Market HavePlayedEvent.Dm_HrlCastle_0005 "
    "step:Step_Goto_HyruleCastle=NotReady,Ready,Step01,Step02,Step02_01,Step03,Step03_01,Step03_"
    "02,Step04,Complete\n"
    "# The road up to the castle, the first time: Kaepora again (Dm_HrlCastle_0000).\n"
    "Dm_HrlCastle_0000\n"
    "# Talon wakes up (the cucco) (Goto_HyruleCastle; plays Dm_HrlCastle_0020; moves "
    "Goto_HyruleCastle to Step04).\n"
    "HyruleCastle_Taron_WakeUp HavePlayedEvent.Dm_HrlCastle_0020 "
    "step:Step_Goto_HyruleCastle=NotReady,Ready,Step01,Step02,Step02_01,Step03,Step03_01,Step03_"
    "02,Step04,Complete\n"
    "# Zelda in the courtyard: the letter, Impa (Goto_HyruleCastle; plays Dm_HrlCastle_0030, "
    "Dm_HrlCastle_0035, Dm_HrlCastle_0045, Dm_HrlCastle_0050, Dm_HrlCastle_0060; moves "
    "Goto_HyruleCastle to Complete, DoorOfTime to Step01).\n"
    "HyruleCastle_Goto_HyruleCastle_MeetZelda HavePlayedEvent.Dm_HrlCastle_0030 "
    "HavePlayedEvent.Dm_HrlCastle_0035 HavePlayedEvent.Dm_HrlCastle_0045 "
    "HavePlayedEvent.Dm_HrlCastle_0050 HavePlayedEvent.Dm_HrlCastle_0060 "
    "step:Step_Goto_HyruleCastle=NotReady,Ready,Step01,Step02,Step02_01,Step03,Step03_01,Step03_"
    "02,Step04,Complete "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "# Impa teaches Zelda's Lullaby (the game names it by hashes) (DoorOfTime; plays "
    "Dm_MelodyGet_Zelda).\n"
    "3906666092_3949112793_2073510616_D_T HavePlayedEvent.Dm_MelodyGet_Zelda\n"
    "# Lon Lon Ranch, the first time (Entry_LonLonRanch; plays Dm_LonLon_0010).\n"
    "Entry_LonLonRanch HavePlayedEvent.Dm_LonLon_0010\n"
    "# Malon teaches Epona's Song (SubEvent_GetEponaSong; plays Dm_MelodyGet_Epona).\n"
    "Get_EponaSong HavePlayedEvent.Dm_MelodyGet_Epona\n"
    "# Hyrule Field cutscene (asked for by a field placement) (Dm_HrlField_0030).\n"
    "Dm_HrlField_0030\n"
    "# Hyrule Field cutscene (asked for by a field placement) (Dm_HrlField_0040).\n"
    "Dm_HrlField_0040\n"
    "# Kaepora at the way to Lake Hylia (Dm_HrlField_0050).\n"
    "Dm_HrlField_0050\n"
    "\n"
    "# ---- Kakariko, Graveyard, Death Mountain, Goron City (child)\n"
    "# The Graveyard, the first time (Dm_Graveyard_0000).\n"
    "Dm_Graveyard_0000\n"
    "# Song of Storms in the windmill: the well is drained (Npc_Hylia_009_Child; plays "
    "Dm_Kakariko_0050; moves SubEvent_GetStormSong to step03).\n"
    "PlayStormSong_fromBancLogic HavePlayedEvent.Dm_Kakariko_0050 "
    "KakarikoVillage_Npc_Hylia_009_Child_DeleteWaterPlane KakarikoVillage_DeleteDummyLadder "
    "step:Step_SubEvent_GetStormSong=NotReady,Ready,ReadyForGetStormSong,step02,step03,BeforeBos"
    "sRoom,step04,step05,Complete\n"
    "# The same, played inside the windmill (Npc_Hylia_009_Child; plays Dm_Kakariko_0050; "
    "moves SubEvent_GetStormSong to step03).\n"
    "PlayStormSong_InWindmill HavePlayedEvent.Dm_Kakariko_0050 "
    "KakarikoVillage_Npc_Hylia_009_Child_DeleteWaterPlane KakarikoVillage_DeleteDummyLadder "
    "step:Step_SubEvent_GetStormSong=NotReady,Ready,ReadyForGetStormSong,step02,step03,BeforeBos"
    "sRoom,step04,step05,Complete\n"
    "# Zelda's Lullaby on the Royal Tomb: the tomb opens (GimmickFlow_RoyalFamilyTomb; plays "
    "Dm_Graveyard_0010; moves SubEvent_GetMelodySun to GetMelodySun).\n"
    "RoyalFamilyTomb_BlowOcarina HavePlayedEvent.Dm_Graveyard_0010 "
    "GraveYard_RoyalFamilyTomb_Open GraveYard_RoyalFamilyTomb_Breaked "
    "step:Step_SubEvent_GetMelodySun=NotReady,Ready,GetMelodySun,Complete\n"
    "# The Sun's Song on the tomb's wall (GimmickFlow_RoyalFamilyTomb; plays Dm_MelodyGet_Sun, "
    "Dm_MelodyGet_Sun_03; moves SubEvent_GetMelodySun to Complete).\n"
    "Obj_Graveyard_Monument_A_02 HavePlayedEvent.Dm_MelodyGet_Sun "
    "HavePlayedEvent.Dm_MelodyGet_Sun_03 RoyalFamilyTomb_MelodyGet_Clear "
    "step:Step_SubEvent_GetMelodySun=NotReady,Ready,GetMelodySun,Complete\n"
    "# Death Mountain Trail, the first time (Dm_SY_BancChangeArrive; plays "
    "Dm_DeathMtTrail_0000; moves Get_StoneOfFire to DeathMtTrailNpc).\n"
    "DeathMountainTrail_Arrive HavePlayedEvent.Dm_DeathMtTrail_0000 "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Death Mountain Trail cutscene (child) (Dm_DeathMtTrail_0005).\n"
    "Dm_DeathMtTrail_0005\n"
    "# Goron City, the first time (Dm_SY_BancChangeArrive; plays Dm_Goron_0000; moves "
    "Get_StoneOfFire to GoronCityDarniaroomNpcTalk01).\n"
    "GoronCity_InitDemo_Arrive HavePlayedEvent.Dm_Goron_0000 "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Meeting Darunia (Get_StoneOfFire; plays Dm_Goron_0020; moves Get_StoneOfFire to "
    "DarniaRoomKokiriTalk).\n"
    "Get_StoneOfFire_MeetDarnia HavePlayedEvent.Dm_Goron_0020 "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Saria in the Lost Woods (Saria's Song) (Dm_SY_BancChangeArrive; plays Dm_LostWood_0000; "
    "moves Get_StoneOfFire to SariaMelodyGet).\n"
    "Get_StoneOfFire_ArriveFromKokiri HavePlayedEvent.Dm_LostWood_0000 "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Saria teaches Saria's Song in the meadow (SacredForestMeadow; plays Dm_MelodyGet_Saria; "
    "moves Get_StoneOfFire to SariaMelodyPlay).\n"
    "SacredForestMeadow_Field_MelodyGet_Saria HavePlayedEvent.Dm_MelodyGet_Saria "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Saria's Song for Darunia: he dances, the cavern opens (Get_StoneOfFire; plays "
    "Dm_Goron_0028, Dm_Goron_0025, Dm_Goron_0030; moves Get_StoneOfFire to DodongoCavernOpne).\n"
    "Meet_Darunia_PlayOcarina HavePlayedEvent.Dm_Goron_0028 HavePlayedEvent.Dm_Goron_0025 "
    "HavePlayedEvent.Dm_Goron_0030 "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# Dodongo's Cavern opens (Get_StoneOfFire; plays Dm_DeathMtTrail_0010).\n"
    "Get_StoneOfFire_DodongoCavernOpne HavePlayedEvent.Dm_DeathMtTrail_0010\n"
    "# Inside Dodongo's Cavern, the first time (Dm_DodongoCave_0000).\n"
    "Dm_DodongoCave_0000\n"
    "# After King Dodongo's blue warp: Darunia gives the Goron Ruby [solo: only who starts it "
    "sees it] (Dm_SY_BancChange_EscapeDungeon; plays Dm_DeathMtTrail_0020, "
    "Dm_DeathMtTrail_0025, Dm_DeathMtTrail_0027; moves Get_StoneOfFire to Complete, DoorOfTime "
    "to Step2_2, Meet_GreatFairy to GoToTopOfMountain).\n"
    "Get_StoneOfFire_Complete solo HavePlayedEvent.Dm_DeathMtTrail_0020 "
    "HavePlayedEvent.Dm_DeathMtTrail_0025 HavePlayedEvent.Dm_DeathMtTrail_0027 "
    "Get_StoneOfFire_TektiteDelete IsHyruleFieldBridgeClose "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete "
    "step:Step_Meet_GreatFairy=NotReady,Ready,GoToTopOfMountain,GoToFountain,GetFairyBreath,GoTo"
    "HyruleCastle,Complete\n"
    "\n"
    "# ---- Zora's River, Zora's Domain, Jabu-Jabu (child)\n"
    "# Kaepora at Zora's River (Get_StoneOfWater; plays Dm_ZoraRiver_0000; moves "
    "Get_StoneOfWater to Step02).\n"
    "Get_StoneOfWater_TalkKaepora_ZoraRiver HavePlayedEvent.Dm_ZoraRiver_0000 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# Zora's River cutscene (Dm_ZoraRiver_0005).\n"
    "Dm_ZoraRiver_0005\n"
    "# Zelda's Lullaby at the waterfall: it opens (Get_StoneOfWater; plays Dm_ZoraRiver_0010).\n"
    "Get_StoneOfWater_WaterfallSplit HavePlayedEvent.Dm_ZoraRiver_0010 "
    "Zorariver_TheWaterfallsplits\n"
    "# Zora's Domain, the first time (Dm_SY_BancChangeArrive; plays Dm_Zora_0000; moves "
    "Get_StoneOfWater to Step03).\n"
    "Goto_ZoraDomain HavePlayedEvent.Dm_Zora_0000 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# Ruto's letter for King Zora: he moves (Get_StoneOfWater; plays Dm_Zora_0010; moves "
    "Get_StoneOfWater to Step07).\n"
    "Get_StoneOfWater_ShowLetter HavePlayedEvent.Dm_Zora_0010 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# The fish for Jabu-Jabu: he swallows Link (Get_StoneOfWater; plays Dm_ZoraFountain_0010).\n"
    "Get_StoneOfWater_JabuJabuEat HavePlayedEvent.Dm_ZoraFountain_0010 GaveFishToJabJab\n"
    "# Inside Jabu-Jabu (Get_StoneOfWater; plays Dm_InsideJabu_Jabu_0000; moves "
    "Get_StoneOfWater to Step09).\n"
    "Get_StoneOfWater_ArriveJabuJabuBelly HavePlayedEvent.Dm_InsideJabu_Jabu_0000 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# Meeting Ruto (Get_StoneOfWater; plays Dm_InsideJabu_Jabu_0010; moves Get_StoneOfWater "
    "to Step10).\n"
    "Get_StoneOfWater_MeetRuto HavePlayedEvent.Dm_InsideJabu_Jabu_0010 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# Ruto finds the stone's room (Get_StoneOfWater; plays Dm_InsideJabu_Jabu_0020).\n"
    "Get_StoneOfWater_DiscoveryStone HavePlayedEvent.Dm_InsideJabu_Jabu_0020\n"
    "# Ruto takes the stone (Get_StoneOfWater; plays Dm_InsideJabu_Jabu_0030; moves "
    "Get_StoneOfWater to Step12).\n"
    "Get_StoneOfWater_RutoGetStone HavePlayedEvent.Dm_InsideJabu_Jabu_0030 "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete\n"
    "# After Barinade's blue warp: Ruto gives the Zora Sapphire [solo: only who starts it sees "
    "it] (Dm_SY_BancChange_EscapeDungeon; plays Dm_ZoraFountain_0020, Dm_ZoraFountain_0030; "
    "moves Get_StoneOfWater to Complete, DoorOfTime to Step2_2).\n"
    "Get_StoneOfWater_Complete solo HavePlayedEvent.Dm_ZoraFountain_0020 "
    "HavePlayedEvent.Dm_ZoraFountain_0030 IsHyruleFieldBridgeClose "
    "step:Step_Get_StoneOfWater=NotReady,Ready,Step01,Step02,Step03,Step03_Impa,Step04,Step05,St"
    "ep05_01,Step06,Step07,Step08,Step09,Step10,Step11,Step12,Step13,Step13_01,Step14,Complete "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "\n"
    "# ---- Temple of Time and the escape (child; the age change itself is D-R17)\n"
    "# Zelda and Impa flee from the castle (DoorOfTime; plays Dm_HrlField_0020; moves "
    "DoorOfTime to Step02, Get_StoneOfFire to KakarikoVillageNpc).\n"
    "HyruleField_DoorOfTime_SendOff HavePlayedEvent.Dm_HrlField_0020 "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete "
    "step:Step_Get_StoneOfFire=NotReady,Ready,KakarikoVillageNpc,KakarikoVillageLetter,DeathMtTr"
    "ailIn,DeathMtTrailNpc,GoronCityIn,GoronCityDarniaroomNpcTalk01,GoronCityDarniaroomOpen,Goro"
    "nCityDarniaTalk,DarniaRoomKokiriTalk,KokiriForestNPCTalk,KokiriForestFire,KokiriForestIn,Sa"
    "riaMelodyGet,SariaMelodyPlay,DodongoCavernOpne,DungeonStart,BossBattle,EscapeDungeon,Comple"
    "te\n"
    "# The Temple of Time, the first time (Dm_TimeTemple_0000).\n"
    "Dm_TimeTemple_0000\n"
    "# The Master Sword's pedestal (DoorOfTime; plays Dm_TimeTemple_0020).\n"
    "DoorOfTime_DiscoverySword HavePlayedEvent.Dm_TimeTemple_0020\n"
    "# Ganondorf chases Zelda across the drawbridge (DoorOfTime; plays Dm_HrlField_0060; moves "
    "DoorOfTime to Step04).\n"
    "HyruleField_DoorOfTime_Escape HavePlayedEvent.Dm_HrlField_0060 IsHyruleFieldBridgeClose "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "# The Ocarina of Time and the Song of Time (DoorOfTime; plays Dm_MelodyGet_Time; moves "
    "DoorOfTime to Step05).\n"
    "HyruleField_DoorOfTime_GetOcarinaOfTime HavePlayedEvent.Dm_MelodyGet_Time "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "# The Door of Time opens (DoorOfTime; plays Dm_TimeTemple_0010; moves DoorOfTime to "
    "Step06).\n"
    "Market_Child_DoorOfTime_OpenDoor HavePlayedEvent.Dm_TimeTemple_0010 "
    "Market_Child_TimeTempleDoor_Opened "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "# Link draws the Master Sword (the age change, D-R17) (DoorOfTime; plays "
    "Dm_TimeTemple_0030, Dm_TimeTemple_0080).\n"
    "Market_Child_DoorOfTime_Draw HavePlayedEvent.Dm_TimeTemple_0030 "
    "HavePlayedEvent.Dm_TimeTemple_0080\n"
    "# Ganondorf enters the Sacred Realm (DoorOfTime; plays Dm_TimeTemple_0040).\n"
    "SageRoom_MeetGanondorf HavePlayedEvent.Dm_TimeTemple_0040\n"
    "# Rauru in the Chamber of Sages (DoorOfTime; plays Dm_SageRoom_0000, Dm_SageRoom_0005; "
    "moves DoorOfTime to Complete).\n"
    "SageRoom_MeetRauru HavePlayedEvent.Dm_SageRoom_0000 HavePlayedEvent.Dm_SageRoom_0005 "
    "step:Step_DoorOfTime=NotReady,Ready,Step01,Step02,Step2_1,Srep2_1_1,Step2_2,Step03,Step04,S"
    "tep05,Step06,Complete\n"
    "# The sword back in: child to adult (D-R17) (Market_Child_Field_ChildToAdult; plays "
    "Dm_TimeTemple_0080).\n"
    "Market_Child_Field_ChildToAdult HavePlayedEvent.Dm_TimeTemple_0080\n"
    "# The sword out: adult to child (D-R17) (Market_Adult_Field_AdultToChild; plays "
    "Dm_TimeTemple_0070).\n"
    "Market_Adult_Field_AdultToChild HavePlayedEvent.Dm_TimeTemple_0070\n"
    "\n"
    "# ---- Adult: Sheik, the Forest\n"
    "# Sheik meets adult Link in the Temple of Time (AllSage; plays Dm_TimeTemple_0050, "
    "Dm_TimeTemple_0055; moves SageOfLight to Complete, AllSage to Step01, SageOfForest to "
    "Step01).\n"
    "Market_Adult_AllSage_Start HavePlayedEvent.Dm_TimeTemple_0050 "
    "HavePlayedEvent.Dm_TimeTemple_0055 step:Step_SageOfLight=NotReady,Ready,Complete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete "
    "step:Step_SageOfForest=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step06_1_0,"
    "Step06_1_1,Step06_1_2,Step06_2,Step07,Step08,Step09,Complete\n"
    "# Temple of Time cutscene (adult) (Dm_TimeTemple_0065).\n"
    "Dm_TimeTemple_0065\n"
    "# The Sacred Forest Meadow, the first time (Dm_SY_BancChangeArrive; plays "
    "Dm_LostWood_0010).\n"
    "Dm_SY_BancChangeArrive_LostWood HavePlayedEvent.Dm_LostWood_0010\n"
    "# The Moblin with the club (SacredForestMeadow; plays Dm_SacredForest_0000).\n"
    "SacredForestMeadow_Field_Moblin_Club HavePlayedEvent.Dm_SacredForest_0000\n"
    "# Sheik teaches the Minuet of Forest (SageOfForest; plays Dm_MelodyGet_Forest).\n"
    "ForestTemple_SageOfForest_MelodyGet HavePlayedEvent.Dm_MelodyGet_Forest\n"
    "# Mido lets Link through (SageOfForest; plays Dm_LostWood_0015, Dm_LostWood_0021, "
    "Dm_LostWood_0025).\n"
    "ForestTemple_SageOfForest_BlockerMove HavePlayedEvent.Dm_LostWood_0015 "
    "HavePlayedEvent.Dm_LostWood_0021 HavePlayedEvent.Dm_LostWood_0025\n"
    "# Forest Temple, the entrance (Dm_ForestTemple_0000; moves SageOfForest to Step05).\n"
    "Dm_ForestTemple_0000 "
    "step:Step_SageOfForest=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step06_1_0,"
    "Step06_1_1,Step06_1_2,Step06_2,Step07,Step08,Step09,Complete\n"
    "# Saria awakens as a sage [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Awake_Forest, Dm_Awake_Forest_Get).\n"
    "ForestTemple_SageOfForest_SageRoom solo HavePlayedEvent.Dm_Awake_Forest "
    "HavePlayedEvent.Dm_Awake_Forest_Get\n"
    "# The Deku Tree Sprout [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Kokiri_0158, Dm_Kokiri_0160, Dm_Kokiri_0162; "
    "moves SageOfForest to Complete, AllSage to Step01_00).\n"
    "ForestTemple_SageOfForest_EscapeFromTempleOutSide solo HavePlayedEvent.Dm_Kokiri_0158 "
    "HavePlayedEvent.Dm_Kokiri_0160 HavePlayedEvent.Dm_Kokiri_0162 "
    "step:Step_SageOfForest=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step06_1_0,"
    "Step06_1_1,Step06_1_2,Step06_2,Step07,Step08,Step09,Complete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "\n"
    "# ---- Adult: Death Mountain and the Fire Temple\n"
    "# Death Mountain Trail cutscene (adult) (Dm_DeathMtTrail_0006).\n"
    "Dm_DeathMtTrail_0006\n"
    "# Death Mountain Trail cutscene (adult) (Dm_DeathMtTrail_0007).\n"
    "Dm_DeathMtTrail_0007\n"
    "# Death Mountain Crater, the first time (Dm_SY_BancChangeArrive; plays "
    "Dm_DeathMtCrater_0000).\n"
    "GoTo_DeathMountainCrater_FromGoronCity HavePlayedEvent.Dm_DeathMtCrater_0000\n"
    "# Sheik teaches the Bolero of Fire (SageOfFire; plays Dm_MelodyGet_Fire).\n"
    "SageOfFire_MelodyGetFire HavePlayedEvent.Dm_MelodyGet_Fire\n"
    "# Darunia in the Fire Temple (SageOfFire; plays Dm_FireTemple_0000, Dm_FireTemple_0010; "
    "moves SageOfFire to GetBossKey).\n"
    "Demo_FireTemple_MeetAgainDarniaEvent HavePlayedEvent.Dm_FireTemple_0000 "
    "HavePlayedEvent.Dm_FireTemple_0010 FireTemple_Room02_CreateChain "
    "step:Step_SageOfFire=NotReady,Ready,StopDaruniaJr,TalkDaruniaJr,MoveStatue,EnterCrater,Meet"
    "Sheik,EnterTemple,MeetDarunia,GetBossKey,MeetBoss,DefeatBoss,GoToWarpPoint,Complete\n"
    "# Darunia awakens as a sage [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Awake_Fire, Dm_Awake_Fire_Get).\n"
    "FireTemple_SageOfFire_SageRoom solo HavePlayedEvent.Dm_Awake_Fire "
    "HavePlayedEvent.Dm_Awake_Fire_Get\n"
    "# After Volvagia: back in the crater [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_DeathMtCrater_0010; moves SageOfFire to "
    "Complete, AllSage to Step01_00_01).\n"
    "Demo_SageOfFire_Arrive_FireBolero solo HavePlayedEvent.Dm_DeathMtCrater_0010 "
    "step:Step_SageOfFire=NotReady,Ready,StopDaruniaJr,TalkDaruniaJr,MoveStatue,EnterCrater,Meet"
    "Sheik,EnterTemple,MeetDarunia,GetBossKey,MeetBoss,DefeatBoss,GoToWarpPoint,Complete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "# After Volvagia: Kakariko [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Kakariko_0025).\n"
    "FireTemple_SageOfFire_Kakariko solo HavePlayedEvent.Dm_Kakariko_0025\n"
    "\n"
    "# ---- Adult: Lake Hylia, Zora's Domain, the Ice Cavern, the Water Temple\n"
    "# Lake Hylia drained (Dm_SY_BancChangeArrive; plays Dm_LakeHrl_0000).\n"
    "Goto_LakeHylia HavePlayedEvent.Dm_LakeHrl_0000\n"
    "# Kaepora at Lake Hylia, from the valley (Dm_LakeHrl_0003).\n"
    "Dm_LakeHrl_0003\n"
    "# Kaepora at Lake Hylia, from Zora's Domain (Dm_LakeHrl_0005).\n"
    "Dm_LakeHrl_0005\n"
    "# Zora's Fountain frozen (Dm_SY_BancChangeArrive; plays Dm_ZoraFountain_0000).\n"
    "FromZoraDomain_arriveZoraFountain HavePlayedEvent.Dm_ZoraFountain_0000\n"
    "# Sheik teaches the Serenade of Water (SageOfWater; plays Dm_MelodyGet_Water; moves "
    "SageOfWater to Step04).\n"
    "SageOfWater_MelodyGet_Water HavePlayedEvent.Dm_MelodyGet_Water "
    "step:Step_SageOfWater=NotReady,Ready,Step00,Step01,Step03,Step04,Step05,Step06,Step07,Step0"
    "7_00,Step07_1,Step07_1_Safety,Step07_2,Step08,Complete\n"
    "# The Water Temple's entrance (Dm_WaterTemple_0000).\n"
    "Dm_WaterTemple_0000 WaterTemple_EmitGatePillar\n"
    "# Ruto in the Water Temple (SageOfWater; plays Dm_WaterTemple_0010, Dm_WaterTemple_0015; "
    "moves SageOfWater to Step07).\n"
    "Demo_WaterTemple_MeetAgainRutoEvent HavePlayedEvent.Dm_WaterTemple_0010 "
    "HavePlayedEvent.Dm_WaterTemple_0015 "
    "step:Step_SageOfWater=NotReady,Ready,Step00,Step01,Step03,Step04,Step05,Step06,Step07,Step0"
    "7_00,Step07_1,Step07_1_Safety,Step07_2,Step08,Complete\n"
    "# Ruto awakens as a sage [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Awake_Water, Dm_Awake_Water_Get).\n"
    "SageOfWater_SageRoom solo HavePlayedEvent.Dm_Awake_Water HavePlayedEvent.Dm_Awake_Water_Get\n"
    "# After Morpha: the lake fills again [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_LakeHrl_0020; moves SageOfWater to Complete, "
    "AllSage to Step01_00_01).\n"
    "Demo_SageOfWater_Arrive_WaterSerenade solo HavePlayedEvent.Dm_LakeHrl_0020 "
    "SageOfWater_Icedelete "
    "step:Step_SageOfWater=NotReady,Ready,Step00,Step01,Step03,Step04,Step05,Step06,Step07,Step0"
    "7_00,Step07_1,Step07_1_Safety,Step07_2,Step08,Complete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "\n"
    "# ---- Adult: Kakariko burning, the Shadow Temple\n"
    "# Kakariko burns; Sheik and the shadow (Dm_SY_BancChangeArrive; plays Dm_Kakariko_0030).\n"
    "KakarikoVillage_Arrive HavePlayedEvent.Dm_Kakariko_0030 KakarikoVillage_Burning_Start\n"
    "# Sheik tells Impa's story (Dm_SY_BancChangeArrive; plays Dm_Kakariko_0040).\n"
    "KakarikoVillage_FlashbackScenes HavePlayedEvent.Dm_Kakariko_0040\n"
    "# Sheik teaches the Nocturne of Shadow (Dm_SY_BancChangeArrive; plays "
    "Dm_MelodyGet_Shadow; moves SageOfShadow to GoToDungeon).\n"
    "KakarikoVillage_MelodyGet_Shadow HavePlayedEvent.Dm_MelodyGet_Shadow "
    "step:Step_SageOfShadow=NotReady,Ready,GoToDungeon,GotGlassTruth,GetGlassTruth,MeetBoss,Batt"
    "leOnShip,BeforeBossRoom,DefeatBoss,GoToWarpPoint,Complete\n"
    "# The Shadow Temple opens (Din's Fire) (Dm_ShadowTemple_0000).\n"
    "Dm_ShadowTemple_0000 GraveYard_Field_ShadowTemple_ShutterOpen\n"
    "# Impa awakens as a sage [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Awake_Shadow, Dm_Awake_Shadow_Get, "
    "Dm_SageRoom_0010; moves AllSage to Step02).\n"
    "ShadowTemple_SageOfShadow_SageRoom solo HavePlayedEvent.Dm_Awake_Shadow "
    "HavePlayedEvent.Dm_Awake_Shadow_Get HavePlayedEvent.Dm_SageRoom_0010 "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "# After Bongo Bongo: the Graveyard [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Graveyard_0030; moves SageOfShadow to Complete, "
    "AllSage to Step01_00_03).\n"
    "ShadowTemple_Warp_Graveyard solo HavePlayedEvent.Dm_Graveyard_0030 "
    "step:Step_SageOfShadow=NotReady,Ready,GoToDungeon,GotGlassTruth,GetGlassTruth,MeetBoss,Batt"
    "leOnShip,BeforeBossRoom,DefeatBoss,GoToWarpPoint,Complete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "\n"
    "# ---- Gerudo Valley, the fortress, the desert, the Spirit Temple (child and adult)\n"
    "# Gerudo Valley, the first time (Dm_GerudoValley_0000).\n"
    "Dm_GerudoValley_0000\n"
    "# Gerudo's Fortress, the first time (Dm_Gerudo_0000).\n"
    "Dm_Gerudo_0000\n"
    "# The fourth carpenter free: the Gerudo membership card (SageOfSpirit; plays "
    "Dm_Gerudo_0025; moves SageOfSpirit to Step02).\n"
    "Meet_FourCarpenter_ClearEvent HavePlayedEvent.Dm_Gerudo_0025 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# The Desert Colossus, the first time (Dm_SY_BancChangeArrive; plays "
    "Dm_DesertColossus_0000; moves SageOfSpirit to Step04).\n"
    "FromWasteland_ArriveColossus HavePlayedEvent.Dm_DesertColossus_0000 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# Sheik teaches the Requiem of Spirit (Dm_MelodyGet_Spilit).\n"
    "Dm_MelodyGet_Spilit\n"
    "# Child Link meets Nabooru (SageOfSpirit; plays Dm_DesertColossus_0020, "
    "Dm_DesertColossus_0025; moves SageOfSpirit to Step06).\n"
    "SageOfSpirit_MeetNabooru HavePlayedEvent.Dm_DesertColossus_0020 "
    "HavePlayedEvent.Dm_DesertColossus_0025 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# Twinrova take Nabooru away (SageOfSpirit; plays Dm_DesertColossus_0030; moves "
    "SageOfSpirit to Step07).\n"
    "SageOfSpirit_NaboorIsTakenAway HavePlayedEvent.Dm_DesertColossus_0030 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# The Iron Knuckle (Nabooru) appears (not one of the 8 boss rooms) (SageOfSpirit; plays "
    "Dm_SpiritTemple_0010; moves SageOfSpirit to Step08).\n"
    "SageOfSpirit_MiddleBossStart HavePlayedEvent.Dm_SpiritTemple_0010 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# The Iron Knuckle falls: it is Nabooru (SageOfSpirit; plays Dm_SpiritTemple_0020; moves "
    "SageOfSpirit to Step08_01).\n"
    "SageOfSpirit_MiddleBossEnd HavePlayedEvent.Dm_SpiritTemple_0020 "
    "SpiritTemple_Room33_ChairBroken "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete\n"
    "# To the Temple of Time for the adult side (SageOfSpirit; plays Dm_TimeTemple_0086).\n"
    "SageOfSpirit_WalkBackTimeTemple HavePlayedEvent.Dm_TimeTemple_0086\n"
    "# Nabooru awakens as a sage [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_Awake_Spirit, Dm_Awake_Spirit_Get, "
    "Dm_SageRoom_0010; moves SageOfSpirit to Complete, AllSage to Step02).\n"
    "SpiritTemple_SageOfSpirit_SageRoom solo HavePlayedEvent.Dm_Awake_Spirit "
    "HavePlayedEvent.Dm_Awake_Spirit_Get HavePlayedEvent.Dm_SageRoom_0010 "
    "step:Step_SageOfSpirit=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step05_01,Step05_0"
    "2,Step05_03,Step06,Step07,Step07_pre00,Step07_00,Step08,Step08_01,Step09,Step09_01,Step10,C"
    "omplete "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete\n"
    "# After Twinrova: the Colossus [solo: only who starts it sees it] "
    "(Dm_SY_BancChange_EscapeDungeon; plays Dm_DesertColossus_0040).\n"
    "SpiritTemple_Level_Room_34_Escape solo HavePlayedEvent.Dm_DesertColossus_0040\n"
    "\n"
    "# ---- The sages, Zelda, Ganon's Castle, the end\n"
    "# Sheik teaches the Prelude of Light (AllSage; plays Dm_MelodyGet_Shine).\n"
    "Market_Adult_Field_GetLightPrelude HavePlayedEvent.Dm_MelodyGet_Shine\n"
    "# Sheik is Zelda: the Light Arrows; Ganondorf takes her (AllSage; plays "
    "Dm_TimeTemple_0090, Dm_TimeTemple_0095, Dm_TimeTemple_0110, Dm_TimeTemple_0115, "
    "Dm_TimeTemple_0125, Dm_TimeTemple_0130, Dm_TimeTemple_0135; moves AllSage to Complete, "
    "Defeat_Ganon to Step01).\n"
    "Market_Adult_Field_GetArrowLight HavePlayedEvent.Dm_TimeTemple_0090 "
    "HavePlayedEvent.Dm_TimeTemple_0095 HavePlayedEvent.Dm_TimeTemple_0110 "
    "HavePlayedEvent.Dm_TimeTemple_0115 HavePlayedEvent.Dm_TimeTemple_0125 "
    "HavePlayedEvent.Dm_TimeTemple_0130 HavePlayedEvent.Dm_TimeTemple_0135 "
    "step:Step_AllSage=NotReady,Ready,Step01,Step01_00,Step01_00_01,Step01_00_02,Step01_00_03,St"
    "ep01_01,Step01_02,Step02,Complete "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# Ganon's Castle, the first time (Dm_SY_BancChangeArrive; plays Dm_GanonCastle_0000).\n"
    "GanonCastle_Adult_Field_EntryGanonCastle HavePlayedEvent.Dm_GanonCastle_0000\n"
    "# The sages' rainbow bridge (Defeat_Ganon; plays Dm_InsideGanonCastle_0000; moves "
    "Defeat_Ganon to Step02).\n"
    "GanonCastle_Adult_Field_Rainbow HavePlayedEvent.Dm_InsideGanonCastle_0000 "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The tower's barrier (Defeat_Ganon; plays Dm_InsideGanonCastle_0003; moves Defeat_Ganon "
    "to Step03).\n"
    "GanonCastle_Dungeon_SeeTowerBarrier HavePlayedEvent.Dm_InsideGanonCastle_0003 "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Forest trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0020, "
    "Dm_InsideGanonCastle_0025, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Forest near=30 HavePlayedEvent.Dm_InsideGanonCastle_0020 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0025 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Forest InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Fire trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0050, "
    "Dm_InsideGanonCastle_0055, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Fire near=30 HavePlayedEvent.Dm_InsideGanonCastle_0050 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0055 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Fire InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Water trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0030, "
    "Dm_InsideGanonCastle_0032, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Water near=30 HavePlayedEvent.Dm_InsideGanonCastle_0030 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0032 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Water InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Shadow trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0010, "
    "Dm_InsideGanonCastle_0015, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Shadow near=30 HavePlayedEvent.Dm_InsideGanonCastle_0010 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0015 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Shadow InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Spirit trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0080, "
    "Dm_InsideGanonCastle_0085, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Spirit near=30 HavePlayedEvent.Dm_InsideGanonCastle_0080 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0085 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Spirit InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Light trial: its barrier falls [near: only who is within 30 m of who starts it, the "
    "trial's room (ZMP D-188)] (Defeat_Ganon; plays Dm_InsideGanonCastle_0070, "
    "Dm_InsideGanonCastle_0075, Dm_InsideGanonCastle_0090; moves Defeat_Ganon to Step04).\n"
    "InsideGanonCastle_Dungeon_BarrierRemove_Light near=30 HavePlayedEvent.Dm_InsideGanonCastle_0070 "
    "HavePlayedEvent.Dm_InsideGanonCastle_0075 HavePlayedEvent.Dm_InsideGanonCastle_0090 "
    "InsideGanonCastle_Dungeon_BarrierRemove_Light InsideGanonCastle_IsAllBarrierClear "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# Inside Ganon's Castle (asked for by a placement) (Dm_InsideGanonCastle_0007).\n"
    "Dm_InsideGanonCastle_0007\n"
    "# Inside Ganon's Castle: the tower (asked for by a placement) (Dm_InsideGanonCastle_0035).\n"
    "Dm_InsideGanonCastle_0035\n"
    "# Inside Ganon's Castle (asked for by a placement) (Dm_InsideGanonCastle_0040).\n"
    "Dm_InsideGanonCastle_0040\n"
    "# Inside Ganon's Castle (asked for by a placement) (Dm_InsideGanonCastle_0060).\n"
    "Dm_InsideGanonCastle_0060\n"
    "# Ganon's Castle outside (asked for by a placement) (Dm_InsideGanonCastle_0100).\n"
    "Dm_InsideGanonCastle_0100\n"
    "# The castle starts to fall (Defeat_Ganon; plays Dm_InsideGanonCastle_0110).\n"
    "CrumblingGanonCastle_Arrive HavePlayedEvent.Dm_InsideGanonCastle_0110\n"
    "# The escape: third floor (EscapeFromCrumblingGanonCastle; plays "
    "Dm_InsideGanonCastle_0150).\n"
    "EscapeFromCrumblingGanonCastle_3F_Fookshot HavePlayedEvent.Dm_InsideGanonCastle_0150\n"
    "# The escape: second floor (EscapeFromCrumblingGanonCastle; plays "
    "Dm_InsideGanonCastle_0160).\n"
    "EscapeFromCrumblingGanonCastle_2F_FallDown HavePlayedEvent.Dm_InsideGanonCastle_0160\n"
    "# The escape: first floor (EscapeFromCrumblingGanonCastle; plays "
    "Dm_InsideGanonCastle_0180).\n"
    "EscapeFromCrumblingGanonCastle_1F_BreakRoom HavePlayedEvent.Dm_InsideGanonCastle_0180\n"
    "# Out of the castle (EscapeFromCrumblingGanonCastle; plays Dm_InsideGanonCastle_0170, "
    "Dm_FinalBattle_0002).\n"
    "EscapeFromCrumblingGanonCastle_EscapeComplete HavePlayedEvent.Dm_InsideGanonCastle_0170 "
    "HavePlayedEvent.Dm_FinalBattle_0002\n"
    "# The ruins (asked for by a placement) (Dm_InsideGanonCastle_0190).\n"
    "Dm_InsideGanonCastle_0190\n"
    "# Ganondorf rises from the ruins (Defeat_Ganon; plays Dm_FinalBattle_0005; moves "
    "Defeat_Ganon to Step07).\n"
    "InsideGanonCaslte_Dungeon_Escape HavePlayedEvent.Dm_FinalBattle_0005 "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The Master Sword back in the fight (Defeat_Ganon; plays Dm_FinalBattle_0010).\n"
    "InsideGanonCastle_Dungeon_Boss_GetMasterSword HavePlayedEvent.Dm_FinalBattle_0010\n"
    "# The sages bind Ganon (Defeat_Ganon; plays Dm_FinalBattle_0030).\n"
    "InsideGanonCastle_Dungeon_BossBind0 HavePlayedEvent.Dm_FinalBattle_0030\n"
    "# Ganon is sealed: the sages, the ending, the credits (Defeat_Ganon; plays "
    "Dm_FinalBattle_BossOut, Dm_SageRoom_0020, Dm_SageRoom_0025, Dm_SageRoom_0027, "
    "Dm_Ending_0000, Dm_StaffCredit_0000; moves Defeat_Ganon to Complete).\n"
    "InsideGanonCastle_Dungeon_BossDie_2 HavePlayedEvent.Dm_FinalBattle_BossOut "
    "HavePlayedEvent.Dm_SageRoom_0020 HavePlayedEvent.Dm_SageRoom_0025 "
    "HavePlayedEvent.Dm_SageRoom_0027 HavePlayedEvent.Dm_Ending_0000 "
    "HavePlayedEvent.Dm_StaffCredit_0000 "
    "step:Step_Defeat_Ganon=NotReady,Ready,Step01,Step02,Step03,Step04,Step05,Step06,Step07,Step"
    "08,Step09,Complete\n"
    "# The credits at Hyrule Castle (Defeat_Ganon; plays Dm_StaffCredit_0005, "
    "Dm_StaffCredit_0007, Dm_StaffCredit_0010).\n"
    "InsideGanonCastle_Dungeon_StaffRoll_HyruleCastle HavePlayedEvent.Dm_StaffCredit_0005 "
    "HavePlayedEvent.Dm_StaffCredit_0007 HavePlayedEvent.Dm_StaffCredit_0010\n";

bool valid_event_name(std::string_view name) {
  if (name.empty() || name.size() > protocol::kMaxStoryNameChars) return false;
  for (const char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_';
    if (!ok) return false;
  }
  return true;
}

std::string played_flag_name(std::string_view event) {
  return "HavePlayedEvent." + std::string(event);
}

StoryList StoryList::defaults() {
  auto list = parse(kDefaultStoryList);
  return list ? std::move(*list) : StoryList{};
}

std::optional<StoryList> StoryList::parse(std::string_view text, std::string* error) {
  std::vector<StoryEntry> entries;
  std::size_t line_number = 0;
  auto fail = [&](const std::string& why) -> std::optional<StoryList> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + why;
    return std::nullopt;
  };
  while (!text.empty()) {
    ++line_number;
    const std::size_t newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    const auto parts = tokens(line);
    if (parts.empty()) continue;
    if (!valid_event_name(parts[0])) {
      return fail("an event name is 1 to 127 of A-Z a-z 0-9 _");
    }
    for (const auto& e : entries) {
      if (e.name == parts[0]) return fail(std::string(parts[0]) + " is listed twice");
    }
    if (entries.size() >= kMaxEntries) return fail("too many events");
    StoryEntry entry;
    entry.name = std::string(parts[0]);
    entry.played_hash = protocol::flag_hash(played_flag_name(entry.name));
    for (std::size_t i = 1; i < parts.size(); ++i) {
      if (parts[i] == kSolo) {
        if (entry.solo) return fail("solo is written twice");
        entry.solo = true;
        continue;
      }
      if (parts[i].starts_with(kNearPrefix)) {
        if (entry.near_m > 0.0f) return fail("near= is written twice");
        const auto metres = near_of(parts[i]);
        if (!metres) return fail("near=M is metres, 1 to 1000");
        entry.near_m = *metres;
        continue;
      }
      if (parts[i].starts_with(kStepPrefix)) {
        std::string why;
        auto step = step_of(parts[i], why);
        if (!step) return fail(why);
        for (const auto& e : entries) {
          for (const auto& other : e.steps) {
            if (other.hash == step->hash && other.values != step->values) {
              return fail(step->name + " is listed with other values on " + e.name + "'s line");
            }
          }
        }
        for (const auto& other : entry.steps) {
          if (other.hash == step->hash) return fail(step->name + " is listed twice");
        }
        entry.steps.push_back(std::move(*step));
        continue;
      }
      const auto hash = flag_of(parts[i]);
      if (!hash) return fail("a flag is a printable name or 0x and 1 to 8 hex digits");
      entry.flags.push_back(FlagKey{*hash, std::uint16_t{0}});
      entry.flag_names.emplace_back(parts[i]);
    }
    entries.push_back(std::move(entry));
  }
  return StoryList(std::move(entries));
}

std::optional<StoryList> StoryList::load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error);
}

std::optional<std::size_t> StoryList::find(std::string_view name) const {
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].name == name) return i;
  }
  return std::nullopt;
}

std::vector<FlagKey> StoryList::shared_flags() const {
  std::vector<FlagKey> keys;
  for (const auto& e : entries_) {
    keys.push_back(FlagKey{e.played_hash, std::uint16_t{0}});
    keys.insert(keys.end(), e.flags.begin(), e.flags.end());
    for (const auto& step : e.steps) keys.push_back(FlagKey{step.hash, std::uint16_t{0}});
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

EnumFlags StoryList::step_flags() const {
  EnumFlags out;
  for (const auto& e : entries_) {
    for (const auto& step : e.steps) out.add(step.hash, step.values, true);  // once: parse checked
  }
  return out;
}

std::vector<StoryFlag> StoryList::end_flags() const {
  std::vector<StoryFlag> out;
  std::set<std::uint32_t> seen;
  for (const auto& e : entries_) {
    for (std::size_t i = 0; i < e.flags.size(); ++i) {
      if (seen.insert(e.flags[i].hash).second) {
        out.push_back(StoryFlag{e.flag_names[i], e.flags[i].hash, std::nullopt});
      }
    }
  }
  for (const auto& e : entries_) {
    for (const auto& step : e.steps) {
      if (seen.insert(step.hash).second) out.push_back(StoryFlag{step.name, step.hash, step});
    }
  }
  return out;
}

std::optional<std::string> StoryList::value_name(std::uint32_t hash, std::uint32_t value) const {
  for (const auto& e : entries_) {
    for (const auto& step : e.steps) {
      if (step.hash != hash) continue;
      for (std::size_t i = 0; i < step.values.size(); ++i) {
        if (step.values[i] == value) return step.value_names[i];
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::vector<std::optional<bool>> read_story_played(GameMemory& memory, const StoryList& list) {
  GameFlags flags(memory);
  std::vector<std::optional<bool>> out;
  out.reserve(list.size());
  for (const auto& e : list.entries()) out.push_back(flags.get_bool(e.played_hash));
  return out;
}

std::vector<std::optional<std::uint32_t>> read_story_flags(GameMemory& memory,
                                                           const StoryList& list) {
  GameFlags flags(memory);
  std::vector<std::optional<std::uint32_t>> out;
  for (const auto& f : list.end_flags()) {
    if (f.step) {
      out.push_back(flags.get_enum(f.hash));
    } else {
      const auto b = flags.get_bool(f.hash);
      out.push_back(b ? std::optional<std::uint32_t>(*b ? 1u : 0u) : std::nullopt);
    }
  }
  return out;
}

// ---- Story -----------------------------------------------------------------

Story::Story(StoryList list, StorySettings settings)
    : list_(std::move(list)),
      settings_(settings),
      played_(list_.size()),
      end_flags_(list_.end_flags()),
      flag_seen_(end_flags_.size()) {}

void Story::receive(const protocol::StoryEvent& message) {
  if (inbox_.size() < 256) inbox_.push_back(message);
}

void Story::host_reset() {
  ran_.clear();
  room_played_.clear();
  room_skipped_.clear();
  skip_.reset();
  press_.reset();
  noted_skip_.reset();
  reset_ = true;
}

void Story::note_skip(std::string how) { noted_skip_ = std::move(how); }

std::optional<std::string> Story::asking() const {
  if (!pending_) return std::nullopt;
  return pending_->name;
}

std::optional<std::string> Story::owned() const {
  if (!running_ || running_->from != 0) return std::nullopt;
  return running_->name;
}

std::optional<std::string> Story::skipping() const {
  if (!skip_) return std::nullopt;
  return skip_->name;
}

void Story::update_played(const StoryInput& in, StoryOutput& out) {
  for (std::size_t i = 0; i < played_.size() && i < in.played.size(); ++i) {
    if (!in.played[i]) continue;  // unreadable this frame: as it was
    Played& p = played_[i];
    const bool on = *in.played[i];
    const std::string flag = played_flag_name(list_.entries()[i].name);
    if (!p.known) {  // the first reading: set before we came, or not
      p = Played{true, on, on, in.now_us};
      if (on) out.log.push_back(flag + " is 1 (set before we came)");
    } else if (on != p.on) {
      p = Played{true, on, false, in.now_us};
      out.log.push_back(flag + (on ? " 0 -> 1" : " 1 -> 0"));
    }
  }
}

std::string Story::flag_value(const StoryFlag& flag, std::uint32_t value) const {
  if (!flag.step) return std::to_string(value);
  for (std::size_t i = 0; i < flag.step->values.size(); ++i) {
    if (flag.step->values[i] == value) return flag.step->value_names[i];
  }
  char hex[16];
  std::snprintf(hex, sizeof hex, "0x%08x", value);
  return std::string(hex) + " (not a listed value)";
}

void Story::update_flags(const StoryInput& in, StoryOutput& out) {
  for (std::size_t i = 0; i < end_flags_.size() && i < in.flags.size(); ++i) {
    if (!in.flags[i]) continue;  // unreadable this frame: as it was
    auto& seen = flag_seen_[i];
    const StoryFlag& flag = end_flags_[i];
    if (!seen) {
      out.log.push_back(flag.name + " is " + flag_value(flag, *in.flags[i]));
    } else if (*seen != *in.flags[i]) {
      out.log.push_back(flag.name + " " + flag_value(flag, *seen) + " -> " +
                        flag_value(flag, *in.flags[i]));
    }
    seen = *in.flags[i];
  }
}

bool Story::played_here(std::size_t index, std::uint64_t now_us) const {
  if (index >= played_.size() || !played_[index].on) return false;
  const Played& p = played_[index];
  return p.old || now_us - p.since >= settings_.played_settle_us;
}

namespace {

std::string metres_text(float d) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f m", d);
  return text;
}

// near=M (ZMP D-188, docs/RIESGOS_RESUELTOS.md "Mundo e historia"): a start is for the room of
// whoever started it. Two measures, either is enough: our Link within M of where the starter's
// Link stood when it started (a start place of 0 0 0 is one the starter did not know), or within M
// of the starter as presence shows him now in our scene (he walked on, or his start place was not
// known). True: taken, `how` says which measure. False: `why` says what was measured.
bool near_ok(const protocol::StoryEvent& m, const StoryInput& in, float near_m, std::string& how,
             std::string& why) {
  const std::string room = "it is for the room of P" + std::to_string(m.player_number);
  char limit[32];
  std::snprintf(limit, sizeof limit, "near=%g", near_m);
  if (!in.position) {
    why = room + " and our place is unknown";
    return false;
  }
  const bool start_known = !(m.position[0] == 0.0f && m.position[1] == 0.0f && m.position[2] == 0.0f);
  std::optional<float> from_start;
  if (start_known) from_start = distance(*in.position, m.position);
  std::optional<float> from_starter;
  for (const auto& mate : in.mates) {
    if (mate.number == m.player_number && mate.scene && *mate.scene == m.scene) {
      from_starter = distance(*in.position, mate.position);
      break;
    }
  }
  if (from_start && *from_start <= near_m) {
    how = "near: " + metres_text(*from_start) + " from where it started, " + limit;
    return true;
  }
  if (from_starter && *from_starter <= near_m) {
    how = "near: " + metres_text(*from_starter) + " from P" + std::to_string(m.player_number) + " now (" +
          (from_start ? metres_text(*from_start) + " from where it started" : std::string("its start place not known")) +
          "), " + limit;
    return true;
  }
  const std::string who = "P" + std::to_string(m.player_number);
  if (from_start && from_starter) {
    why = room + ": we are " + metres_text(*from_start) + " from where it started and " +
          metres_text(*from_starter) + " from " + who + " now (" + limit + ")";
  } else if (from_start) {
    why = room + ": we are " + metres_text(*from_start) + " from where it started (" + limit + ")";
  } else if (from_starter) {
    why = room + ": we are " + metres_text(*from_starter) + " from " + who + " now, its start place not known (" +
          limit + ")";
  } else {
    why = room + ": its start place is not known and " + who + " is not seen in our scene (" + limit + ")";
  }
  return false;
}

}  // namespace

void Story::on_start(const protocol::StoryEvent& m, const StoryInput& in, StoryOutput& out) {
  const std::string who = "STORY_EVENT start received from P" + std::to_string(m.player_number) +
                          " ('" + m.name + "' in " + m.scene + ")";
  const auto index = list_.find(m.name);
  std::string why;
  std::string near_how;  // near=: which measure took it
  if (!index) {
    why = "not in our list";
  } else if (list_.entries()[*index].solo) {
    why = "it is solo here (only who starts it sees it)";
  } else if (!in.scene) {
    why = "we are loading";
  } else if (*in.scene != m.scene) {
    why = "we are in " + *in.scene;
  } else if (in.downed) {
    why = "we are down";
  } else if (const float near_m = list_.entries()[*index].near_m;
             near_m > 0.0f && !near_ok(m, in, near_m, near_how, why)) {
    // why says it (near_ok)
  } else if (ran_.count(m.name) != 0) {
    why = "it already ran here";
  } else if (played_here(*index, in.now_us)) {
    why = played_flag_name(m.name) + " is set here";
  } else if (in.event_running.value_or(false)) {
    why = "our own event runs" + (current_name_.empty() ? std::string() : " ('" + current_name_ + "')");
  } else if (pending_) {
    why = "we are asking for '" + pending_->name + "' already";
  }
  if (!why.empty()) {
    out.log.push_back(who + ": ignored, " + why);
    return;
  }
  pending_ = Pending{m.scene, m.name, m.player_number, in.now_us, in.now_us, 0};
  out.log.push_back(who + ": starting" + (near_how.empty() ? std::string() : " (" + near_how + ")"));
}

void Story::confirm(const StoryInput& in, StoryOutput& out) {
  out.log.push_back("event running '" + pending_->name + "' as P" + std::to_string(pending_->from) +
                    " asked, " + ms(in.now_us - pending_->since) + " after their start came (" +
                    std::to_string(pending_->asks) + (pending_->asks == 1 ? " ask)" : " asks)"));
  ran_.insert(pending_->name);
  running_ = Running{pending_->scene, pending_->name, pending_->from};
  pending_.reset();
}

void Story::on_first_name(const std::string& name, const StoryInput& in, StoryOutput& out) {
  if (pending_ && pending_->name == name) {
    confirm(in, out);
    return;
  }
  const auto index = list_.find(name);
  if (!index) return;
  if (list_.entries()[*index].solo) {
    if (ran_.insert(name).second) {
      out.log.push_back("'" + name + "' is solo: only we see it, its flags are shared");
    }
    return;
  }
  if (ran_.count(name) != 0) {
    out.log.push_back("'" + name + "' runs here again; not shared again");
    return;
  }
  if (!in.scene) {
    out.log.push_back("'" + name + "' started while the scene is unknown; not shared");
    return;
  }
  ran_.insert(name);
  const std::pair<std::string, std::string> key{*in.scene, name};
  if (const auto it = room_played_.find(key); it != room_played_.end()) {
    // Someone's (a latecomer's game, or one that came back to it): not shared
    // again, and it does not outlast theirs (D-048).
    const std::uint8_t owner = it->second;
    const std::string whose = owner != 0 ? " (P" + std::to_string(owner) + "'s)" : std::string();
    if (owner != 0) running_ = Running{*in.scene, name, owner};
    if (room_skipped_.count(key) != 0) {
      skip_ = Skip{name, owner, in.now_us, in.now_us, 0};
      out.log.push_back("'" + name + "' was played in this room already" + whose +
                        " and skipped there: not shared again, ours is skipped");
    } else {
      out.log.push_back("'" + name + "' was played in this room already" + whose + "; not shared again" +
                        (owner != 0 ? ", ours is skipped only if its owner skips theirs" : ""));
    }
    return;
  }
  room_played_.emplace(key, 0);
  running_ = Running{*in.scene, name, 0};
  press_.reset();
  protocol::StoryEvent m;
  m.kind = protocol::StoryKind::start;
  m.scene = *in.scene;
  m.name = name;
  m.position = in.position.value_or(protocol::Vec3{});
  m.server_time_ms = static_cast<std::uint32_t>(in.server_now_us / 1000);
  out.send.push_back(std::move(m));
  out.log.push_back("owner: our game started '" + name + "' in " + *in.scene +
                    "; STORY_EVENT start to the others");
}

// The owner's `skipped` (relayed live, or to a joiner after `played`): he
// skipped it. A start we still ask for is dropped; our copy of it is skipped
// at once (D-048). A plain `end` is only logged: it never cuts anyone.
void Story::on_skipped(const protocol::StoryEvent& m, const StoryInput& in, StoryOutput& out) {
  const std::string what = "P" + std::to_string(m.player_number) + " skipped '" + m.name + "' in " + m.scene;
  room_skipped_.insert({m.scene, m.name});
  if (pending_ && pending_->scene == m.scene && pending_->name == m.name) {
    out.log.push_back(what + ": not asked any more, skipped before it started here (" +
                      std::to_string(pending_->asks) + (pending_->asks == 1 ? " ask)" : " asks)"));
    pending_.reset();
    return;
  }
  if (running_ && running_->from != 0 && running_->from == m.player_number && running_->scene == m.scene &&
      running_->name == m.name && !skip_) {
    skip_ = Skip{m.name, m.player_number, in.now_us, in.now_us, 0};
    out.log.push_back(what + "; ours is skipped too");
    return;
  }
  out.log.push_back(what);
}

// The owner's side: a skip press while our owned event runs (a skip button's
// rising edge, or note_skip), the last one kept for its end.
void Story::watch_skip(const StoryInput& in, bool running, StoryOutput& out) {
  const std::uint64_t held = in.pad.value_or(0) & settings_.skip_buttons;
  const std::uint64_t pressed = in.pad ? (held & ~pad_was_) : 0;
  if (in.pad) pad_was_ = held;
  const std::optional<std::string> noted = std::exchange(noted_skip_, std::nullopt);
  if (!running_ || running_->from != 0 || !running) return;
  std::string how;
  if (noted) {
    how = *noted;
  } else if (pressed != 0) {
    for (int bit = 0; bit < 64; ++bit) {
      if (((pressed >> bit) & 1) == 0) continue;
      if (!how.empty()) how += "+";
      how += bit == 10 ? std::string("PLUS") : bit == 11 ? std::string("MINUS") : "button " + std::to_string(bit);
    }
  } else {
    return;
  }
  if (!press_) {
    out.log.push_back("skip pressed (" + how + ") during our '" + running_->name +
                      "': its end is `skipped` if it is over within " + ms(settings_.skip_press_window_us));
  }
  press_ = SkipPress{in.now_us, how};
}

// Our copy of an event its owner skipped: the game's force skip until no
// event runs (each skip cuts one shot), never on a downed Link (his revive is
// an event too), given up after skip_for_us.
void Story::step_skip(const StoryInput& in, bool running, StoryOutput& out) {
  if (!skip_) return;
  Skip& s = *skip_;
  const std::string whose = s.from != 0 ? "P" + std::to_string(s.from) + "'s" : std::string("the room's");
  if (!running) {
    if (s.skips == 0) {
      out.log.push_back("'" + s.name + "' ended here by itself after " + whose + " skip (not force skipped)");
    } else {
      out.log.push_back("'" + s.name + "' skipped here after " + whose + ": over after " +
                        std::to_string(s.skips) + (s.skips == 1 ? " force skip" : " force skips"));
    }
    skip_.reset();
    return;
  }
  if (in.downed) {
    out.log.push_back("'" + s.name + "' not skipped: we are down");
    skip_.reset();
    return;
  }
  if (in.now_us - s.since >= settings_.skip_for_us) {
    out.log.push_back("'" + s.name + "' still runs " + ms(in.now_us - s.since) + " after its skips began (" +
                      std::to_string(s.skips) + (s.skips == 1 ? " force skip" : " force skips") +
                      "); given up, it plays on");
    skip_.reset();
    return;
  }
  if (in.now_us < s.next) return;
  ++s.skips;
  s.next = in.now_us + settings_.skip_every_us;
  out.skip = true;
  out.log.push_back("force skip of our '" + (current_name_.empty() ? s.name : current_name_) + "' (" + whose +
                    " '" + s.name + "' was skipped; skip " + std::to_string(s.skips) + ")");
}

StoryOutput Story::tick(const StoryInput& in) {
  StoryOutput out;
  update_played(in, out);
  update_flags(in, out);
  if (std::exchange(reset_, false)) {
    out.log.push_back(
        "the host loaded a save: what ran here and what the room played are forgotten; the next "
        "start of each event is shared again");
  }

  // What the server said.
  for (const auto& m : std::exchange(inbox_, {})) {
    const std::string by = "P" + std::to_string(m.player_number);
    switch (m.kind) {
      case protocol::StoryKind::played:
        room_played_.emplace(std::make_pair(m.scene, m.name), m.player_number);
        out.log.push_back(by + " played '" + m.name + "' in " + m.scene +
                          " before we came: not shared again from here");
        break;
      case protocol::StoryKind::end:
        out.log.push_back(by + "'s '" + m.name + "' in " + m.scene + " is over there (not skipped)");
        break;
      case protocol::StoryKind::skipped:
        on_skipped(m, in, out);
        break;
      case protocol::StoryKind::start:
        room_played_.emplace(std::make_pair(m.scene, m.name), m.player_number);
        on_start(m, in, out);
        break;
    }
  }

  // Our own events: the rising edge, and the first name that can be read.
  const bool running = in.event_running.value_or(was_running_);
  if (running && !was_running_) {
    edge_ = true;
    edge_at_ = in.now_us;
    current_name_.clear();
  }
  if (!running && was_running_) quiet_since_ = in.now_us;
  if (running) quiet_since_ = 0;
  was_running_ = running;
  if (running && in.event_name && *in.event_name != current_name_) {
    current_name_ = *in.event_name;
    if (edge_) {
      edge_ = false;
      on_first_name(current_name_, in, out);
    } else if (pending_ && pending_->name == current_name_) {
      confirm(in, out);  // ours came after another name
    }
  }
  if (edge_ && in.now_us - edge_at_ > settings_.name_wait_us) {
    edge_ = false;
    out.log.push_back("an event started and no name could be read for it");
  }
  if (!running) current_name_.clear();

  // The owner's side of a skip: what was pressed while ours ran.
  watch_skip(in, running, out);

  // A listed event here is over.
  if (running_ && !running && quiet_since_ != 0 &&
      in.now_us - quiet_since_ >= settings_.end_quiet_us) {
    if (running_->from == 0) {
      // Skipped: a skip press at most skip_press_window_us before it was over
      // (quiet_since_); else a plain end, which cuts nobody (D-048).
      const bool skipped = press_ && quiet_since_ >= press_->at &&
                           quiet_since_ - press_->at <= settings_.skip_press_window_us;
      protocol::StoryEvent m;
      m.kind = skipped ? protocol::StoryKind::skipped : protocol::StoryKind::end;
      m.scene = running_->scene;
      m.name = running_->name;
      m.position = in.position.value_or(protocol::Vec3{});
      m.server_time_ms = static_cast<std::uint32_t>(in.server_now_us / 1000);
      out.send.push_back(std::move(m));
      if (skipped) {
        out.log.push_back("'" + running_->name + "' is over here, skipped by us (" + press_->how + ", " +
                          ms(quiet_since_ - press_->at) + " before its end); STORY_EVENT skipped to the others");
      } else {
        out.log.push_back("'" + running_->name + "' is over here, not skipped (" +
                          (press_ ? "the last skip press " + ms(quiet_since_ - press_->at) + " before its end"
                                  : std::string("no skip press")) +
                          "); STORY_EVENT end to the others");
      }
      press_.reset();
    } else {
      out.log.push_back("'" + running_->name + "' (P" + std::to_string(running_->from) +
                        "'s) is over here");
    }
    running_.reset();
  }

  // Someone's start we follow: ask our game until it runs, or give up.
  if (pending_) {
    std::string why;
    if (in.now_us - pending_->since >= settings_.give_up_us) {
      why = "the game did not start it in " + ms(settings_.give_up_us);
    } else if (!in.scene || *in.scene != pending_->scene) {
      why = "we left " + pending_->scene;
    } else if (in.downed) {
      why = "we went down";
    }
    if (!why.empty()) {
      out.log.push_back("'" + pending_->name + "' asked by P" + std::to_string(pending_->from) +
                        ": rejected, " + why + " (" + std::to_string(pending_->asks) +
                        (pending_->asks == 1 ? " ask)" : " asks)"));
      pending_.reset();
    } else if (!running && in.now_us >= pending_->next_ask) {
      ++pending_->asks;
      pending_->next_ask = in.now_us + settings_.ask_every_us;
      out.ask = pending_->name;
      out.log.push_back("asking our game for '" + pending_->name + "' (try " +
                        std::to_string(pending_->asks) + ")");
    }
  }

  // Our copy of an event whose owner's copy is over (D-048).
  step_skip(in, running, out);
  return out;
}

}  // namespace coop::host
