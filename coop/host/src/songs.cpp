// ORM: the songs with several players: the thirteen songs as data, their world effects and
//   the rule each follows, the flag list they bring, and the warp-song invitation (oot-64
//   D-067). Described in coop/host/songs.hpp.
// Doc: oot-remake docs/CANCIONES.md.
// Switch: LICHII_COOP_WARP_INVITE (the invitation; off by default), LICHII_COOP_DUNGEON_LISTS
//   (the kit's shared_dungeon_songs.txt).
// Test: tests/host/songs_test.cpp.

#include "coop/host/songs.hpp"

#include <algorithm>
#include <array>

namespace coop::host {
namespace {

// Read from the romfs (2026-11-11): Event/EventFlow/Dm_SY_Ocarina (MelodyType cases, the
// items they give, the OcarinaWarp sub flow's ChildBanc, AdultBanc and NextStartPos) and
// the StartPos placements of those names in Banc/<Scene>/<Scene>_<Era>_Dynamic (the same
// Translate in both eras; the Bolero's has two, 0.15 apart, the first is kept).
constexpr std::array<SongInfo, kSongCount> kSongs{{
    {Song::zelda_lullaby, "ZeldaLullaby", "IsGet.Item_Melody_ZeldaLullaby", "la Nana de Zelda",
     "Impa in the castle courtyard (the story list's 3906666092_3949112793_2073510616_D_T)", std::nullopt},
    {Song::epona, "EponaSong", "IsGet.Item_Melody_EponaSong", "la Cancion de Epona", "Get_EponaSong",
     std::nullopt},
    {Song::saria, "SariaSong", "IsGet.Item_Melody_SariaSong", "la Cancion de Saria",
     "SacredForestMeadow_Field_MelodyGet_Saria", std::nullopt},
    {Song::sun, "SunSong", "IsGet.Item_Melody_SunSong", "la Cancion del Sol", "Obj_Graveyard_Monument_A_02",
     std::nullopt},
    {Song::time, "TimeSong", "IsGet.Item_Melody_TimeSong", "la Cancion del Tiempo",
     "HyruleField_DoorOfTime_GetOcarinaOfTime", std::nullopt},
    {Song::storm, "StormSong", "IsGet.Item_Melody_StormSong", "la Cancion de la Tormenta",
     "PlayStormSong_Adult (the windmill, Dm_MelodyGet_Storm)", std::nullopt},
    {Song::minuet, "ForestMinuet", "IsGet.Item_Melody_ForestMinuet", "el Minueto del Bosque",
     "ForestTemple_SageOfForest_MelodyGet",
     WarpPad{"SacredForestMeadow_Child", "SacredForestMeadow_Adult", "ForestMinuet", {-6.95f, 114.89f, -95.99f}}},
    {Song::bolero, "FireBolero", "IsGet.Item_Melody_FireBolero", "el Bolero del Fuego", "SageOfFire_MelodyGetFire",
     WarpPad{"DeathMountainCrater_Child", "DeathMountainCrater_Adult", "FireBolero", {0.39f, 111.84f, -8.63f}}},
    {Song::serenade, "WaterSerenade", "IsGet.Item_Melody_WaterSerenade", "la Serenata del Agua",
     "SageOfWater_MelodyGet_Water",
     WarpPad{"HyruleField_Child", "HyruleField_Adult", "WaterSerenade", {-202.29f, 131.72f, 509.78f}}},
    {Song::requiem, "SpiritRequiem", "IsGet.Item_Melody_SpiritRequiem", "el Requiem del Espiritu",
     "Dm_MelodyGet_Spilit",
     WarpPad{"DesertColossus_Child", "DesertColossus_Adult", "SpiritRequiem", {-125.96f, 94.36f, 10.47f}}},
    {Song::nocturne, "ShadowNocturne", "IsGet.Item_Melody_ShadowNocturne", "el Nocturno de la Sombra",
     "KakarikoVillage_MelodyGet_Shadow",
     WarpPad{"Graveyard_Child", "Graveyard_Adult", "ShadowNocturne", {64.65f, 108.97f, 2.48f}}},
    {Song::prelude, "LightPrelude", "IsGet.Item_Melody_LightPrelude", "el Preludio de la Luz",
     "Market_Adult_Field_GetLightPrelude",
     WarpPad{"Market_Child", "Market_Adult", "LightPrelude", {84.75f, 104.23f, -34.0f}}},
    {Song::scarecrow, "ScarecrowSong", "IsGet.Item_Melody_ScarecrowSong", "la Cancion del Espantapajaros",
     "Step_SubEvent_GetScarecrow (Bonooru and Pierre; Dm_SY_Ocarina_Scarecrow)", std::nullopt},
}};

using R = SongRule;
using S = SongStatus;

// Every effect, by song. The `lines` are DungeonList lines (coop/host/dungeon_lists.hpp);
// what is in no list on purpose says why in its note. Bool names were checked against
// GameData/GameDataList (ResetTypeValue 8: saved); the Song of Time signs' hashes are the
// Obj_SongOfTimeSign_A_01 placements with isSaveGameData in the dungeons' Banc files.
constexpr std::array kEffects = std::to_array<SongEffect>({
    // ---- Zelda's Lullaby ----
    {Song::zelda_lullaby, "", "knowing the song", R::item, S::done, "", "game_items.cpp, R::any"},
    {Song::zelda_lullaby, "HyruleCastle_Child", "Impa teaches it", R::story, S::done, "",
     "shared_story.txt (the event the game names by hashes)"},
    {Song::zelda_lullaby, "Graveyard_Child", "the Royal Tomb opens", R::story, S::done, "",
     "RoyalFamilyTomb_BlowOcarina: GraveYard_RoyalFamilyTomb_Open, _Breaked"},
    {Song::zelda_lullaby, "ZoraRiver_Child", "the waterfall to the Domain parts", R::built_in, S::done, "",
     "Get_StoneOfWater_WaterfallSplit and once:Zorariver_TheWaterfallsplits (shared_dungeon_child.txt)"},
    {Song::zelda_lullaby, "ZoraRiver_Adult", "the waterfall parts (adult)", R::once, S::to_run,
     "once:Zorariver_TheWaterfallsplits_Adult", "the adult copy of the child's flag"},
    {Song::zelda_lullaby, "GoronCity_Child", "Darunia's door opens", R::built_in, S::done, "",
     "once:IsZeldaLullabyPlayed_DaruniaEntrance (shared_dungeon_child.txt)"},
    {Song::zelda_lullaby, "BottomOfTheWell", "room 01: the water stops", R::hold, S::to_run,
     "hold:HavePlayedEvent.BottomOfTheWell_Level_Room_01_Ocarina BottomOfTheWell",
     "its results (BottomOfTheWell_Room_01_StopWater, _Blow_Ocarina) are in coop_dungeon.cpp; the event "
     "is held while we are in the well, so our own trigger keeps working (issue 129)"},
    {Song::zelda_lullaby, "WaterTemple", "the triforce marks move the water", R::built_in, S::to_find, "",
     "the water's level flags are coop_dungeon.cpp's; live water is PLAN_100 F6 (the water work)"},
    {Song::zelda_lullaby, "InsideGanonCastle", "the Ocarina room (room 13) opens", R::once, S::to_run,
     "once:InsideGanonCastele_Dungeon_Room13_Ocarina\nhold:HavePlayedEvent.InsideGanonCastle_Dungeon_Ocarina "
     "InsideGanonCastle",
     "the game's own spelling, Castele; the event is in the flow Defeat_Ganon"},
    {Song::zelda_lullaby, "ShadowTemple", "the boat", R::local, S::each_game, "",
     "a one-way ride: left out on purpose (coop_dungeon.cpp's header)"},
    {Song::zelda_lullaby, "(fountains)", "a Great Fairy appears on her triforce mark", R::local, S::to_find, "",
     "Obj_TriForcePoint_A_01..06: HavePlayedEvent.Obj_TriForcePoint_A_0N not shared: if a fairy's gift is "
     "not an item (magic, defence) sharing her event would keep it from the others; to see with two games"},
    {Song::zelda_lullaby, "ZoraRiver_Child", "the frogs: its reward", R::once, S::to_run,
     "once:MiniGame_FrogTenors_ZeldaLullaby", "the frogs give each song's reward once in the world"},
    // ---- Epona's Song ----
    {Song::epona, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::epona, "LonLonRanch", "Malon teaches it", R::story, S::done, "", "Get_EponaSong"},
    {Song::epona, "(fields)", "calling Epona", R::local, S::each_game, "",
     "one Epona per player, the group owns her (oot-64 D-233, D-R19): the epona work"},
    {Song::epona, "(cows)", "a cow gives milk", R::local, S::each_game, "", "into the player's own bottle"},
    {Song::epona, "ZoraRiver_Child", "the frogs: its reward", R::once, S::to_run,
     "once:MiniGame_FrogTenors_EponaSong", "the frogs give each song's reward once in the world"},
    // ---- Saria's Song ----
    {Song::saria, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::saria, "SacredForestMeadow_Child", "Saria teaches it", R::story, S::done, "",
     "SacredForestMeadow_Field_MelodyGet_Saria"},
    {Song::saria, "GoronCity_Child", "Darunia dances: the cavern opens", R::story, S::done, "",
     "Meet_Darunia_PlayOcarina (oot-64 D-176/D-177: the scene is the player's who plays)"},
    {Song::saria, "(anywhere)", "Saria talks", R::local, S::each_game, "", "a talk with one player"},
    {Song::saria, "ZoraRiver_Child", "the frogs: its reward", R::once, S::to_run,
     "once:MiniGame_FrogTenors_SariaSong", ""},
    // ---- Sun's Song ----
    {Song::sun, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::sun, "Graveyard_Child", "learnt on the Royal Tomb's wall", R::story, S::done, "",
     "Obj_Graveyard_Monument_A_02: RoyalFamilyTomb_MelodyGet_Clear"},
    {Song::sun, "(outdoors)", "the time of day jumps for everyone", R::time, S::to_run, "",
     "TIME_OF_DAY with the jump bit (docs/time.md, coop_time_sync.h); oot-64 D-062: one clock for the "
     "room; the time-of-day work (orm/hora)"},
    {Song::sun, "(anywhere)", "ReDeads freeze, a fairy appears, townspeople react", R::local, S::each_game, "",
     "the sensor SongOfTheSunSensorActor lives in the game of who plays; enemies are their owner's"},
    {Song::sun, "ZoraRiver_Child", "the frogs: its reward", R::once, S::to_run, "once:MiniGame_FrogTenors_SunSong",
     ""},
    // ---- Song of Time ----
    {Song::time, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::time, "HyruleField_Child", "the Ocarina of Time and the song", R::story, S::done, "",
     "HyruleField_DoorOfTime_GetOcarinaOfTime"},
    {Song::time, "Market_Child", "the Door of Time opens", R::story, S::done, "",
     "Market_Child_DoorOfTime_OpenDoor: Market_Child_TimeTempleDoor_Opened"},
    {Song::time, "Market", "a Song of Time block in the Temple of Time", R::local, S::each_game, "",
     "none in the remake: no Obj_SongOfTimeBlock in Banc/Market (searched 2026-11-11)"},
    {Song::time, "ForestTemple", "room 02 and room 25: the block goes for good", R::key_once, S::to_run,
     "key64:0x0dbe35edd0ba3c92  # ForestTemple_Static Obj_SongOfTimeSign_A_01, room 2, isOnceOnly",
     "the named Bools (Foresttemple_SongOfTimeBlock_A_01_Disappearance, ForestTemple_Room02_TimeBlock, "
     "the Room25 first block) are in coop_dungeon.cpp"},
    {Song::time, "FireTemple", "room 11: the block goes for good", R::key_once, S::to_run,
     "key64:0x261d8308d842d916  # FireTemple_Static Obj_SongOfTimeSign_A_01, room 11, isOnceOnly",
     "Firetemple_SongOfTimeBlock_A_01_Disappearance is in coop_dungeon.cpp"},
    {Song::time, "GerudoTrainingGround", "room 9: the four blocks go for good", R::key_once, S::to_run,
     "key64:0xc44444be8c06624b  # GerudoTrainingGround_Static Obj_SongOfTimeSign_A_01, room 9, isOnceOnly", ""},
    {Song::time, "SacredForestMeadow", "the four blocks on the way to the temple go for good", R::key_once,
     S::to_run,
     "key64:0x48d70af7eb780c61  # SacredForestMeadow_Child/_Adult_Static Obj_SongOfTimeSign_A_01, isOnceOnly\n"
     "hold:HavePlayedEvent.SacredForest_FirstTimeBLock SacredForestMeadow",
     "the same placement hash in both eras; the event's result (ForestTemple_SageOfForest_HaveSearchedTimeBlock) "
     "is in coop_dungeon.cpp"},
    {Song::time, "SpiritTemple", "room 15: the two blocks swap (each song moves them back and forth)",
     R::key_toggle, S::to_run,
     "hold:HavePlayedEvent.SpiritTemple_Level_Room_15_Ocarina SpiritTemple\n"
     "toggle64:0x03ecedcb30afe5e4 SpiritTemple  # Obj_SongOfTimeSign_A_01, room 15, on at first\n"
     "toggle64:0x97e1668d83dba136 SpiritTemple  # Obj_SongOfTimeSign_A_01, room 15, off at first",
     "the signs' keys go both ways: toggle64 lines (shared both ways, held while our Link is in the temple: "
     "no block goes from under him, the last song wins when he leaves or loads); whether a block answers "
     "live: LiveCheck (song_block_watches); the room's chest (SpiritTemple_Room15_TBox_On) is in coop_dungeon.cpp"},
    {Song::time, "SpiritTemple", "room 21: a block that comes back when the room loads", R::local,
     S::each_game, "", "sign 0xa2b2901919669298 has no isSaveGameData: nothing to share"},
    {Song::time, "SpiritTemple", "rooms 08 and 29: the Ocarina gimmicks (song not read)", R::hold, S::to_run,
     "hold:HavePlayedEvent.SpiritTemple_Level_Room_08_Ocarina SpiritTemple\n"
     "hold:HavePlayedEvent.SpiritTemple_Level_Room_29_Ocarina SpiritTemple",
     "SageOfSpirit_FocusOnGimmick; room 29's result (SpiritTemple_Room29_Unlocked) is in coop_dungeon.cpp; "
     "which song the room asks for is not read (filed under the Song of Time until a run says)"},
    {Song::time, "FireTemple", "room 1: two blocks swap", R::key_toggle, S::to_run,
     "toggle64:0xc1ce2332223d2aec FireTemple  # Obj_SongOfTimeSign_A_01, room 1, on at first\n"
     "toggle64:0xf015b3e1a1c12718 FireTemple  # Obj_SongOfTimeSign_A_01, room 1, off at first",
     "both ways, held while our Link is in the temple (see room 15 of the Spirit Temple)"},
    {Song::time, "GerudoTrainingGround", "room 6: blocks swap", R::key_toggle, S::to_run,
     "toggle64:0x44543896c729bd8b GerudoTrainingGround  # Obj_SongOfTimeSign_A_01, room 6\n"
     "toggle64:0x2d8814df959c0731 GerudoTrainingGround  # Obj_SongOfTimeSign_A_01, room 6",
     "both off at first; both ways, held while our Link is in the training ground"},
    {Song::time, "IceCavern", "room 8: three blocks", R::key_toggle, S::to_run,
     "toggle64:0x8ad9985355a1d827 IceCavern  # Obj_SongOfTimeSign_A_01, room 8, off at first",
     "not isOnceOnly; IceCavern_Room06_SongOfTimeBlock_ON is in coop_dungeon.cpp"},
    {Song::time, "InsideGanonCastle", "room 3: a block", R::key_toggle, S::to_run,
     "toggle64:0xb1f382f3d0834d9e InsideGanonCastle  # Obj_SongOfTimeSign_A_01, room 3, off at first",
     "not isOnceOnly; both ways, held while our Link is in the castle"},
    {Song::time, "ZoraRiver_Child", "the frogs: its reward", R::once, S::to_run, "once:MiniGame_FrogTenors_TimeSong",
     ""},
    // ---- Song of Storms ----
    {Song::storm, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::storm, "KakarikoVillage_Adult", "the windmill man teaches it", R::once, S::to_run,
     "once:HavePlayedEvent.PlayStormSong_Adult",
     "the lesson is not in the story list (no world Bool, HISTORIA_COMPLETA.md): one lesson for the world, "
     "the song is the group's; its step (Step_SubEvent_GetStormSong) is the story list's"},
    {Song::storm, "KakarikoVillage_Child", "the windmill: the well is drained", R::story, S::done, "",
     "PlayStormSong_fromBancLogic, PlayStormSong_InWindmill; once:..._DeleteWaterPlane (child kit)"},
    {Song::storm, "KakarikoVillage_Adult", "the windmill spins fast", R::local, S::each_game, "",
     "KakarikoVillage_Windmill_IsHighSpeed: a passing state"},
    {Song::storm, "DesertColossus", "the hole and its chest", R::once, S::to_run,
     "once:DessertClossus_IsStormSongPlayed\nhold:HavePlayedEvent.DessertColossus_IsStormSongPlayed DesertColossus",
     "the game's own spelling, Dessert/Clossus; DesertColossus_IsStormSongPlayed_01/_02 are in "
     "shared_dungeon_adult.txt"},
    {Song::storm, "(fairy spots)", "a fairy comes out (well, Colossus, Shadow Temple, field)", R::local,
     S::each_game, "", "FairySpirit_AppearPoint_StormSong: a fairy for whoever plays"},
    {Song::storm, "(bean spots)", "rain on a planted bean", R::local, S::to_find, "",
     "Field_PlantMagicBeanStormMelody (IsStart_FairySpiritMove, passing)"},
    {Song::storm, "ZoraRiver_Child", "the frogs: its heart piece", R::once, S::to_run,
     "once:MiniGame_FrogTenors_StormSong", "the heart piece itself is shared as a key (game_items, coop_chests)"},
    // ---- Warp songs ----
    {Song::minuet, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::bolero, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::serenade, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::requiem, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::nocturne, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::prelude, "", "knowing the song", R::item, S::done, "", "game_items.cpp"},
    {Song::minuet, "SacredForestMeadow", "warp to the meadow", R::warp, S::to_run, "",
     "WarpInvite; learnt: ForestTemple_SageOfForest_MelodyGet (story)"},
    {Song::bolero, "DeathMountainCrater", "warp to the crater", R::warp, S::to_run, "",
     "WarpInvite; learnt: SageOfFire_MelodyGetFire (story; oot-64 D-183/D-196)"},
    {Song::serenade, "HyruleField", "warp to Lake Hylia", R::warp, S::to_run, "",
     "WarpInvite (the lake is in the HyruleField scene); learnt: SageOfWater_MelodyGet_Water (story)"},
    {Song::requiem, "DesertColossus", "warp to the Colossus", R::warp, S::to_run, "",
     "WarpInvite; learnt: Dm_MelodyGet_Spilit (story)"},
    {Song::nocturne, "Graveyard", "warp to the graveyard", R::warp, S::to_run, "",
     "WarpInvite; learnt: KakarikoVillage_MelodyGet_Shadow (story)"},
    {Song::prelude, "Market", "warp to the Temple of Time", R::warp, S::to_run, "",
     "WarpInvite; learnt: Market_Adult_Field_GetLightPrelude (story)"},
    // ---- The Scarecrow's Song ----
    {Song::scarecrow, "", "knowing the song", R::item, S::done, "",
     "game_items.cpp; the remake teaches it with an example like the others (Dm_SY_Ocarina, MelodyType 12), "
     "so it looks like a fixed tune, not one each player records (inferred, not seen)"},
    {Song::scarecrow, "(16 spots)", "Pierre appears where it is played (one place at a time)", R::shared, S::to_run,
     "Appear_SCARECROW_LAKE_HYLIA_01\nAppear_SCARECROW_LAKE_HYLIA_02\nAppear_SCARECROW_LAKE_HYLIA_03\n"
     "Appear_SCARECROW_ZORA_FOUNTAIN_01\nAppear_SCARECROW_SACRED_FOREST_MEADOW_01\n"
     "Appear_SCARECROW_DEATH_MOUNTAIN_CRATER_01\nAppear_SCARECROW_DEATH_MOUNTAIN_CRATER_02\n"
     "Appear_SCARECROW_GERUDO_FORTRESS_01\nAppear_SCARECROW_DODONGO_CAVERN_01\nAppear_SCARECROW_FOREST_TEMPLE_01\n"
     "Appear_SCARECROW_FIRE_TEMPLE_01\nAppear_SCARECROW_WATER_TEMPLE_01\nAppear_SCARECROW_SHADOW_TEMPLE_01\n"
     "Appear_SCARECROW_SHADOW_TEMPLE_02\nAppear_SCARECROW_GERUDO_TRAINING_GROUND_01\n"
     "Appear_SCARECROW_SPIRIT_TEMPLE_01",
     "AppearPierre_<spot> sets its own and clears the other fifteen: one Pierre in the world, as oot-64's one "
     "save; both ways, so his move reaches everyone"},
    {Song::scarecrow, "LakeHylia", "the scarecrow quest (Bonooru, Pierre)", R::local, S::to_find, "",
     "Step_SubEvent_GetScarecrow has in-between steps (IntoThePast, IntoTheFuture) about one player's era: "
     "not shared (as Epona's race, D-R20); the song is"},
});

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// "al Prado Sagrado": the place with its preposition (review: "a el" is "al").
const char* place_spanish(Song song) {
  switch (song) {
    case Song::minuet: return "al Prado Sagrado";
    case Song::bolero: return "al Crater de la Montana de la Muerte";
    case Song::serenade: return "al Lago Hylia";
    case Song::requiem: return "al Coloso del Desierto";
    case Song::nocturne: return "al Cementerio";
    case Song::prelude: return "al Templo del Tiempo";
    default: return "a otro sitio";
  }
}

float ground_distance2(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0];
  const float dz = a[2] - b[2];
  return dx * dx + dz * dz;
}

}  // namespace

std::span<const SongInfo> songs() { return kSongs; }

const SongInfo& song_info(Song song) { return kSongs[static_cast<std::size_t>(song) % kSongCount]; }

std::optional<Song> song_from_melody_type(int type) {
  if (type < 0 || type >= static_cast<int>(kSongCount)) return std::nullopt;
  return static_cast<Song>(type);
}

std::optional<Song> song_from_id(std::string_view id) {
  for (const std::string_view prefix : {std::string_view("IsGet."), std::string_view("Item_Melody_")}) {
    if (id.starts_with(prefix)) id.remove_prefix(prefix.size());
  }
  for (const auto& s : kSongs) {
    if (s.id == id) return s.song;
  }
  return std::nullopt;
}

std::optional<Song> warp_song_to(std::string_view scene) {
  if (scene.empty()) return std::nullopt;
  for (const auto& s : kSongs) {
    if (s.warp && (s.warp->child_scene == scene || s.warp->adult_scene == scene)) return s.song;
  }
  return std::nullopt;
}

const char* to_string(SongRule rule) {
  switch (rule) {
    case SongRule::item: return "item";
    case SongRule::story: return "story";
    case SongRule::once: return "once";
    case SongRule::shared: return "shared";
    case SongRule::hold: return "hold";
    case SongRule::key_once: return "key_once";
    case SongRule::key_toggle: return "key_toggle";
    case SongRule::built_in: return "built_in";
    case SongRule::local: return "local";
    case SongRule::time: return "time";
    case SongRule::warp: return "warp";
  }
  return "?";
}

const char* to_string(SongStatus status) {
  switch (status) {
    case SongStatus::done: return "done";
    case SongStatus::to_run: return "to_run";
    case SongStatus::to_find: return "to_find";
    case SongStatus::each_game: return "each_game";
  }
  return "?";
}

std::span<const SongEffect> song_effects() { return kEffects; }

std::string song_flag_list_text() {
  std::string out =
      "# ORM songs (oot-remake docs/CANCIONES.md): the world flags the songs set, made from\n"
      "# coop/host songs.cpp's song_effects(). A dungeon list file (coop/host dungeon_lists.hpp).\n";
  for (const auto& e : kEffects) {
    if (e.lines.empty()) continue;
    out += "# ";
    out += song_info(e.song).id;
    out += ", ";
    out += e.place;
    out += ": ";
    out += e.what;
    out += '\n';
    std::string_view rest = e.lines;
    while (!rest.empty()) {
      const std::size_t nl = rest.find('\n');
      const std::string_view line = trim(rest.substr(0, nl));
      rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
      if (line.empty()) continue;
      out += line;
      out += '\n';
    }
  }
  return out;
}

std::string mate_label(std::uint8_t number, std::string_view name) {
  if (!name.empty()) return std::string(name);
  return "P" + std::to_string(number);
}

WarpInvite::Seen& WarpInvite::seen(std::uint8_t number) {
  for (auto& s : seen_) {
    if (s.number == number) return s;
  }
  seen_.push_back(Seen{number});
  return seen_.back();
}

InviteOutput WarpInvite::tick(const InviteInput& in) {
  InviteOutput out;
  const std::uint64_t now = in.now_us;
  const std::string ours = in.scene.value_or(std::string());

  // A mate who left the room is forgotten at once (and his invitation with him). One who is
  // only missing from the list (no pose shown for a moment: his warp's load, a dropped
  // sample) is kept for kAbsentUs, so his landing is still judged when he shows again.
  const auto listed = [&](std::uint8_t number) {
    return std::any_of(in.mates.begin(), in.mates.end(),
                       [&](const InviteMate& m) { return m.number == number; });
  };
  const auto in_room = [&](std::uint8_t number) {
    return !in.room || std::find(in.room->begin(), in.room->end(), number) != in.room->end();
  };
  std::erase_if(seen_, [&](const Seen& s) {
    if (listed(s.number)) return false;
    if (!in_room(s.number)) return true;
    return now >= s.listed_us && now - s.listed_us > kAbsentUs;
  });
  for (auto& s : seen_) {
    if (listed(s.number)) s.listed_us = now;  // also while his samples carry no scene
  }
  if (invite_.number != 0 && !listed(invite_.number)) {
    const bool left = !in_room(invite_.number);
    const bool kept =
        std::any_of(seen_.begin(), seen_.end(), [&](const Seen& s) { return s.number == invite_.number; });
    if (left || !kept) {
      out.log.push_back("invitation from P" + std::to_string(invite_.number) + " closed: " +
                        (left ? std::string("the player left")
                              : "not seen for " + std::to_string(kAbsentUs / 1'000'000) + " s"));
      invite_ = {};
      held_since_us_.reset();
    }
  }

  for (const auto& m : in.mates) {
    if (m.number == 0 || !m.scene || m.scene->empty()) continue;
    Seen& s = seen(m.number);
    s.listed_us = now;
    if (m.anim) {
      s.anim_known = true;
      // Free play ("Ocarina...") or the song's own scene ("DemoOcarina..."), in our scene.
      const std::string_view a = *m.anim;
      if ((a.starts_with("Ocarina") || a.starts_with("DemoOcarina")) && !ours.empty() && *m.scene == ours) {
        s.ocarina_us = now;
      }
    }
    if (s.scene.empty()) {  // first sight: no landing to judge
      s.scene = *m.scene;
      s.changed_us = now;
      s.looked = true;
      continue;
    }
    if (*m.scene != s.scene) {
      // Only a mate who leaves our own scene can be followed from it.
      s.left = (!ours.empty() && ours == s.scene) ? s.scene : std::string();
      s.scene = *m.scene;
      s.changed_us = now;
      s.looked = s.left.empty() || !warp_song_to(s.scene);
      // With his animations known, a warp song needs his Ocarina out before he left.
      const bool played = !s.anim_known || (s.ocarina_us && now - *s.ocarina_us <= kSongUs);
      if (!s.looked && !played) {
        s.looked = true;
        out.log.push_back("P" + std::to_string(m.number) + " went from " + s.left + " to " + s.scene +
                          " with no Ocarina out before: no invitation");
      }
      s.ocarina_us.reset();
    }
    if (s.looked) continue;
    const std::uint64_t age = now >= s.changed_us ? now - s.changed_us : 0;
    if (age > kLandWindowUs) {
      s.looked = true;
      out.log.push_back("P" + std::to_string(m.number) + " went from " + s.left + " to " + s.scene +
                        ": not seen on a warp pad, no invitation");
      continue;
    }
    if (age < kLandSettleUs || !m.position) continue;
    const Song song = *warp_song_to(s.scene);
    const WarpPad& pad = *song_info(song).warp;
    if (ground_distance2(*m.position, pad.position) > kPadRadius * kPadRadius) continue;
    s.looked = true;
    if (ours.empty() || ours != s.left || ours == s.scene) {
      out.log.push_back("P" + std::to_string(m.number) + " warped to " + s.scene + " with the " +
                        std::string(song_info(song).id) + ", but we are no longer in " + s.left);
      continue;
    }
    // A newer landing replaces an older invitation (oot-64: one invitation at a time).
    invite_ = Invite{m.number, mate_label(m.number, m.name), song, s.scene, ours, now + kInviteUs};
    held_since_us_.reset();
    released_ = false;
    out.log.push_back("P" + std::to_string(m.number) + " warped from " + ours + " to " + s.scene + " (" +
                      std::string(pad.start_pos) + ") with the " + std::string(song_info(song).id) +
                      ": invitation open for 10 s");
    out.notices.push_back(invite_.name + " se ha ido con " + std::string(song_info(song).spanish) + " " +
                          place_spanish(song) + ". Manten " + button_ + " para ir tambien (10 s)");
  }

  if (invite_.number == 0) return out;
  if (!in.held) released_ = true;
  if (now >= invite_.until_us) {
    out.log.push_back("invitation from P" + std::to_string(invite_.number) + " over (10 s)");
    invite_ = {};
    held_since_us_.reset();
    return out;
  }
  if (ours != invite_.from) {
    out.log.push_back("invitation from P" + std::to_string(invite_.number) + " closed: we left " + invite_.from);
    invite_ = {};
    held_since_us_.reset();
    return out;
  }
  // The button with our Link up and no event of ours (the trip does not start in one), pressed
  // after the invitation opened (a hold from before it never follows by itself).
  if (!in.held || !in.standing || !released_) {
    held_since_us_.reset();
    return out;
  }
  if (!held_since_us_) {
    held_since_us_ = now;
    return out;
  }
  if (now < *held_since_us_ || now - *held_since_us_ < kHoldUs) return out;
  out.follow = invite_.number;
  out.follow_name = invite_.name;
  out.log.push_back("following P" + std::to_string(invite_.number) + " to " + invite_.to + " (held " +
                    button_ + ")");
  out.notices.push_back("Vas con " + invite_.name + " " + place_spanish(invite_.song) +
                        ": saca la ocarina o habla con alguien para viajar");
  invite_ = {};
  held_since_us_.reset();
  return out;
}

std::vector<LiveWatch> song_block_watches() {
  // The named Bools the built-in list (coop_dungeon.cpp) shares for the blocks of a place.
  struct Named {
    std::string_view place;
    std::string_view name;
  };
  static constexpr Named kNamed[] = {
      {"ForestTemple", "Foresttemple_SongOfTimeBlock_A_01_Disappearance"},
      {"ForestTemple", "ForestTemple_Room02_TimeBlock"},
      {"ForestTemple", "ForestTemple_HidingHole23_TimeBlockOFF"},
      {"FireTemple", "Firetemple_SongOfTimeBlock_A_01_Disappearance"},
      {"IceCavern", "IceCavern_Room06_SongOfTimeBlock_ON"},
  };
  std::vector<LiveWatch> out;
  const auto watch_of = [&](std::string_view place) -> LiveWatch& {
    for (auto& w : out) {
      if (w.place == place) return w;
    }
    LiveWatch w;
    w.id = "song of time " + std::string(place);
    w.place = std::string(place);
    w.actor = "SongOfTimeBlock";
    w.cause = "Ocarina";
    out.push_back(std::move(w));
    return out.back();
  };
  for (const auto& e : kEffects) {
    if (e.song != Song::time || (e.rule != SongRule::key_once && e.rule != SongRule::key_toggle)) continue;
    LiveWatch& w = watch_of(e.place);
    std::string_view rest = e.lines;
    while (!rest.empty()) {
      const std::size_t nl = rest.find('\n');
      std::string_view line = trim(rest.substr(0, nl));
      rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
      for (const std::string_view prefix : {std::string_view("key64:0x"), std::string_view("toggle64:0x")}) {
        if (!line.starts_with(prefix)) continue;
        line.remove_prefix(prefix.size());
        std::uint64_t key = 0;
        std::size_t used = 0;
        for (const char c : line) {
          int d = -1;
          if (c >= '0' && c <= '9') d = c - '0';
          else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
          if (d < 0 || used == 16) break;
          key = key << 4 | static_cast<std::uint64_t>(d);
          ++used;
        }
        if (used != 0 && key != 0) w.keys.push_back(FlagKey::key64_flag(key));
      }
    }
  }
  for (const auto& n : kNamed) {
    watch_of(n.place).keys.emplace_back(flag_hash(n.name), std::uint16_t{0});
  }
  return out;
}

std::vector<std::string> InviteButton::frame(const Input& in) {
  std::vector<std::string> log;
  const std::uint64_t buttons = in.buttons.value_or(0);
  const bool down = (buttons & bit_) != 0;
  held_ = down && (!taken_ || (buttons & partner_bit_) != 0);

  const std::string anim = in.animation.value_or(std::string());
  if (anim != last_anim_) {
    last_anim_ = anim;
    anim_since_us_ = in.now_us;
  }
  if (!decided_) {
    const bool watching = in.standing && !in.invitation_open && in.animation.has_value();
    if (!watching) {
      probe_.reset();  // an event, a load, an invitation: this press says nothing
    } else if (down && !was_down_ && in.now_us - anim_since_us_ >= kReactUs) {
      probe_ = Probe{in.now_us, anim, ""};  // a press from a steady pose
    }
    if (probe_) {
      if (probe_->moved_to.empty() && anim != probe_->before) probe_->moved_to = anim;
      if (in.now_us - probe_->since_us >= kReactUs) {
        ++presses_;
        if (!probe_->moved_to.empty()) {
          ++moved_;
          if (example_.empty()) example_ = "'" + probe_->before + "' -> '" + probe_->moved_to + "'";
        }
        probe_.reset();
        if (presses_ >= kPresses) {
          decided_ = true;
          taken_ = moved_ >= kTakenAfter;
          if (taken_) {
            log.push_back("invitation button " + name_ + ": the game uses it (our Link went " + example_ + " on " +
                          std::to_string(moved_) + " of " + std::to_string(presses_) +
                          " presses): the invitation takes " + label() + " held together");
          } else {
            log.push_back("invitation button " + name_ + ": free (" + std::to_string(presses_ - moved_) + " of " +
                          std::to_string(presses_) + " presses moved nothing of our Link): " + name_ + " alone");
          }
          held_ = down && (!taken_ || (buttons & partner_bit_) != 0);
        }
      }
    }
  }
  was_down_ = down;
  return log;
}

}  // namespace coop::host
