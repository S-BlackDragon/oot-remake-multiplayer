// Tests for the minigame list (tools/release/kit/shared_dungeon_minigames.txt, oot-remake
// docs/MINIJUEGOS.md, written by oot-remake/tools/romfs/minigame_list.py). The rule, as in
// oot-64 (one save for the group): each player plays a minigame in his own game, with his own
// score, record and run; a prize handed out once is handed out once for the group (once:), so
// a second winner gets the after-prize (rupees). These checks keep the list meaningful against
// the other lists without the game.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "coop_dungeon.h"
#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/story.hpp"

namespace host = coop::host;

namespace {

const std::string kKit = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";

host::FlagKey Named(const char* name) { return host::FlagKey(host::flag_hash(name), std::uint16_t{0}); }

bool Has(const std::vector<host::FlagKey>& keys, const char* name) {
  return std::find(keys.begin(), keys.end(), Named(name)) != keys.end();
}

host::DungeonList Load(const std::string& file) {
  std::string error;
  auto list = host::DungeonList::load(kKit + file, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

// The names the kit's talk list shares (first word of each line that is not a comment).
std::set<std::uint32_t> TalkFlags() {
  std::ifstream file(kKit + "shared_talk.txt");
  REQUIRE(file.good());
  std::set<std::uint32_t> out;
  for (std::string line; std::getline(file, line);) {
    if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    std::istringstream words(line);
    std::string first;
    if (words >> first) out.insert(host::flag_hash(first));
  }
  return out;
}

// What a game shares with the kit's lists on (coop-options.ini order: child, adult, minigames)
// over the built-in dungeon list.
host::SharedFlags KitShared() {
  std::string error;
  const auto all = host::DungeonList::load_many(kKit + "shared_dungeon_child.txt," + kKit +
                                                    "shared_dungeon_adult.txt," + kKit +
                                                    "shared_dungeon_minigames.txt",
                                                &error);
  INFO(error);
  REQUIRE(all);
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  host::apply_dungeon_list(shared, *all);
  return *shared;
}

}  // namespace

TEST_CASE("minigame list: the kit's file, its kinds and counts") {
  const auto list = Load("shared_dungeon_minigames.txt");
  CHECK(list.once().size() == 28);
  CHECK(list.share().size() == 1);  // the frogs' bug game count (an Int)
  CHECK(Has(list.share(), "MiniGame_FrogTenors_InsectRepellent_Clearnumber"));
  CHECK(list.unshare().empty());  // no list shares a minigame's own state today
  CHECK(list.holds().empty());
  CHECK(list.pickup_keys().empty());
  for (const char* prize :
       {"MiniGame_HitTarget_HrlCity_Get_BulletBag", "MiniGame_HitTarget_Kakariko_Get_Quiver",
        "LostWood_HitTargetOfDekuScrub_FirstClear", "MiniGame_BombchuBowling_ClearFirstTime",
        "MiniGame_BombchuBowling_ClearSecondTime", "MiniGame_BombchuBowling_ClearThirdTime",
        "MiniGame_ChoiceTreasure_IsClear", "UnderTheGrave_DampeRace_HeartGeted",
        "GraveYard_DiggingHoleTour_Drop_HeartPiece", "MiniGame_HorsebackArchery_GotaHeart",
        "MiniGame_HorseBackArchery_Get_Quiver", "MiniGame_FrogTenors_StormSong", "MiniGame_FrogTenors_SongClear",
        "MiniGame_Diving_Cleared", "MiniGame_CatchCucco_IsCompleted", "IsClear_Minigame_FindOutCucco",
        "SubEvent_FindRichard_GetHeartPiece", "Npc_SkullKid_Child_IsGetHeartPiece",
        "Minigame_SkullKidSession_IsGetHeartPiece",
        "DeathMountain_Enemy_Scrub_Business_HeartPiece_01"}) {
    INFO(prize);
    CHECK(Has(list.once(), prize));
  }
  // The Great Fairies' lines are written commented out (MINIJUEGOS.md: waiting on the items list).
  CHECK_FALSE(Has(list.once(), "Meet_GreatFairy_02"));
  CHECK_FALSE(Has(list.share(), "Meet_GreatFairy_RemainNumber"));
}

TEST_CASE("minigame list: Biggoron's trade step is shared both ways, with the game's values in order") {
  const auto list = Load("shared_dungeon_minigames.txt");
  const auto& enums = list.share_enums();
  REQUIRE(enums.size() == 1);
  const std::uint32_t step = host::flag_hash("Step_SubEvent_GetSwordBigGoron");
  CHECK(step == 0x26a2e01cu);
  REQUIRE(enums.contains(step));
  // Both ways: a spoiled item or a trip back in time sets it back, as the mirror trade item.
  CHECK_FALSE(enums.steps(step));
  const auto* values = enums.values(step);
  REQUIRE(values);
  const char* names[] = {"NotReady", "Ready",    "StartSubEvent", "WaitEggHatch", "EggHatched", "Step01",
                         "Step01_01", "Step02",  "Step03",        "Step03_01",    "Step03_02",  "Step03_03",
                         "Step04",   "Step05",   "Step05_01",     "Step06",       "Step06_01",  "Step06_02",
                         "Step06_03", "Step07",  "Step08",        "Step08_01",    "Step09",     "Step09_01",
                         "Step10",   "Step11",   "Complete"};
  REQUIRE(values->size() == std::size(names));
  for (std::size_t i = 0; i < values->size(); ++i) {
    INFO(names[i]);
    CHECK((*values)[i] == host::flag_hash(names[i]));
  }
  // Not the story list's (it keeps the chain out on purpose, as a step that only moves forward).
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  CHECK_FALSE(story->step_flags().contains(step));
}

TEST_CASE("minigame list: loads with the child and adult lists, adds only what no list shares") {
  const auto mini = Load("shared_dungeon_minigames.txt");
  const auto child = Load("shared_dungeon_child.txt");
  const auto adult = Load("shared_dungeon_adult.txt");
  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  const auto talk = TalkFlags();
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  std::set<std::uint32_t> story_flags;
  for (const auto& f : story->end_flags()) story_flags.insert(f.hash);

  std::vector<host::FlagKey> mine = mini.once();
  mine.insert(mine.end(), mini.share().begin(), mini.share().end());
  for (const auto& k : mine) {
    INFO(k.hash);
    CHECK_FALSE(dungeon.contains(k.hash));
    CHECK_FALSE(talk.contains(k.hash));
    CHECK_FALSE(story_flags.contains(k.hash));
    CHECK(host::find_item(k.hash) == nullptr);  // the items list's own (the fishing prizes) are not here
    CHECK_FALSE(std::binary_search(child.once().begin(), child.once().end(), k));
    CHECK_FALSE(std::binary_search(child.share().begin(), child.share().end(), k));
    CHECK_FALSE(std::binary_search(adult.share().begin(), adult.share().end(), k));
  }

  // As the hook logs it with only this file: 28 once + 1 Int + 1 Enum added, nothing taken off.
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  const auto r = host::apply_dungeon_list(shared, mini);
  CHECK(r.added == 30);
  CHECK(r.removed == 0);
  CHECK(r.once == 28);
  CHECK(r.held == 0);
  CHECK(r.pickups == 0);
}

TEST_CASE("minigame list: with every kit list on, the prizes are shared and each player's run is not") {
  const auto shared = KitShared();
  for (const char* prize : {"MiniGame_BombchuBowling_ClearSecondTime", "UnderTheGrave_DampeRace_HeartGeted",
                            "MiniGame_FrogTenors_SongClear", "MiniGame_FrogTenors_InsectRepellent_Clearnumber",
                            "GoronLargePot_IsGetHeartPiece",  // the child list's once:
                            "Step_SubEvent_GetSwordBigGoron",
                            "KakarikoVillage_GatheringTokenGold_GetReward50"}) {  // the adult list's
    INFO(prize);
    CHECK(shared.contains(Named(prize)));
  }
  CHECK(shared.enums().contains(host::flag_hash("Step_SubEvent_GetSwordBigGoron")));
  // oot-64: records, the run, rented items and the race are the player's (D-061 left them out of
  // the shared game; D-222/D-234: Ingo's race is each rider's).
  for (const char* own :
       {"MiniGame_HitTarget_BestScore", "MiniGame_HitTarget_Kakariko_BestScore",
        "MiniGame_HitTarget_HrlCity_FirstTimeClear", "MiniGame_HorsebackArchery_BestScore",
        "MiniGame_HorsebackArchery_NewScore", "MiniGame_DampeRace_BestTime_Second", "MiniGame_DampeRace_ClearCount",
        "Minigame_DampeRace_TreasureBoxState", "MiniGame_BombchuBowling_Progress",
        "MiniGame_BombchuBowling_Choise_Hardmode", "Market_MiniGame_ChoiceTreasure_HowtoPlay",
        "Market_MiniGame_ChoiceTreasure_NumOfHit", "MiniGame_SmallKeyNum", "MiniGame_Diving_Playing",
        "MiniGame_FrogTenors_Playing", "MiniGame_Fishing_RecordFishSize_Child", "MiniGame_Fishing_OnceCatchFish",
        "LonLonRanch_PlayingGambleRace", "LonLonRanch_GambleRaceGoaledPlayer", "LonLonRanch_GambleRaceWinCount",
        "MiniGame_HorseRace_BestTime_Second", "Step_MiniGame_HitTarget", "Step_MiniGame_DampeRace",
        "Step_MiniGame_BombchuBowling", "GatheringTokenGold_ContinuousRewardTalk", "Meet_GreatFairy_02"}) {
    INFO(own);
    CHECK_FALSE(shared.contains(Named(own)));
    CHECK_FALSE(shared.enums().contains(host::flag_hash(own)));
  }
}

TEST_CASE("minigame list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/mini-*.txt match these hashes in the logs.
  CHECK(host::flag_hash("MiniGame_HitTarget_HrlCity_Get_BulletBag") == 0x893fa7cbu);
  CHECK(host::flag_hash("MiniGame_HitTarget_HrlCity_FirstTimeClear") == 0xa113c16cu);
  CHECK(host::flag_hash("MiniGame_BombchuBowling_ClearFirstTime") == 0x19e5a36bu);
  CHECK(host::flag_hash("MiniGame_BombchuBowling_Choise_Hardmode") == 0x7e630d00u);
  CHECK(host::flag_hash("MiniGame_ChoiceTreasure_IsClear") == 0xb599aeedu);
  CHECK(host::flag_hash("Market_MiniGame_ChoiceTreasure_HowtoPlay") == 0xffa59212u);
  CHECK(host::flag_hash("UnderTheGrave_DampeRace_HeartGeted") == 0x5a66513cu);
  CHECK(host::flag_hash("GraveYard_DiggingHoleTour_Drop_HeartPiece") == 0xb5127e75u);
  CHECK(host::flag_hash("MiniGame_DampeRace_Failed_Navi") == 0x5a136452u);
  CHECK(host::flag_hash("MiniGame_HorsebackArchery_GotaHeart") == 0x3f43245bu);
  CHECK(host::flag_hash("MiniGame_HorsebackArchery_NewScore") == 0xefd34ff6u);
  CHECK(host::flag_hash("LonLonRanch_PlayingGambleRace") == 0xa253721cu);
  CHECK(host::flag_hash("LonLonRanch_GambleRaceGoaledPlayer") == 0x03c8d7f1u);
  CHECK(host::flag_hash("MiniGame_FrogTenors_StormSong") == 0xd578df95u);
  CHECK(host::flag_hash("MiniGame_FrogTenors_SongClear") == 0xc69d8bbfu);
  CHECK(host::flag_hash("MiniGame_FrogTenors_Playing") == 0x16451868u);
  CHECK(host::flag_hash("MiniGame_Diving_Cleared") == 0x697507aau);
  CHECK(host::flag_hash("MiniGame_CatchCucco_IsCompleted") == 0x7a7961c3u);
  CHECK(host::flag_hash("SubEvent_FindRichard_GetHeartPiece") == 0xf2fab510u);
  CHECK(host::flag_hash("Npc_SkullKid_Child_IsGetHeartPiece") == 0x5ff7aa30u);
  CHECK(host::flag_hash("KakarikoVillage_GatheringTokenGold_GetReward50") == 0xe3c6e4a3u);
  CHECK(host::flag_hash("GatheringTokenGold_ContinuousRewardTalk") == 0x281e53e4u);
  CHECK(host::flag_hash("Meet_GreatFairy_02") == 0xf8106300u);
  CHECK(host::flag_hash("Step04") == 0x27f660feu);
  CHECK(host::flag_hash("Step03_03") == 0x057ef5f8u);
}
