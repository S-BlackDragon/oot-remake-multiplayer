// Tests for the bottle and trade list (tools/release/kit/shared_dungeon_trades.txt, oot-remake
// docs/BOTELLAS_TRUEQUES.md, written by oot-remake/tools/romfs/trade_list.py). The rule, as in
// oot-64 (one save for the group): a bottle is the group's and what is in it each player's; a
// bottle handed out once is handed out once for the group; a trade chain is one chain for the
// group, but the three trade items with a timer (Odd Mushroom, Eyeball Frog, Eye Drops) are
// only the receiver's, so a partner's game (with the shared step and without the item) never
// counts their timer nor spoils them. These checks keep the list meaningful against the other
// lists and the item table without the game.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
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
#include "coop/host/item_rules.hpp"
#include "coop/host/story.hpp"

namespace host = coop::host;

namespace {

const std::string kKit = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";

host::FlagKey Named(const char* name) { return host::FlagKey(host::flag_hash(name), std::uint16_t{0}); }

bool Has(const std::vector<host::FlagKey>& keys, const char* name) {
  return std::find(keys.begin(), keys.end(), Named(name)) != keys.end();
}

bool Sorted(const std::vector<host::FlagKey>& keys, host::FlagKey key) {
  return std::binary_search(keys.begin(), keys.end(), key);
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

// The kit's lists in coop-options.ini order.
const std::string kKitLists = kKit + "shared_dungeon_child.txt," + kKit + "shared_dungeon_adult.txt," + kKit +
                              "shared_dungeon_minigames.txt," + kKit + "shared_dungeon_trades.txt";

// The item rules of the kit (shared_items.txt) while it lives, the built-in ones after: the
// table is global for the build (game_items.hpp).
struct KitItemRules {
  KitItemRules() {
    host::reset_item_rules();
    std::string error;
    const auto rules = host::ItemRules::load(kKit + "shared_items.txt", &error);
    INFO(error);
    REQUIRE(rules);
    host::apply_item_rules(*rules);
  }
  ~KitItemRules() { host::reset_item_rules(); }
  KitItemRules(const KitItemRules&) = delete;
  KitItemRules& operator=(const KitItemRules&) = delete;
};

// What a game shares as the hook builds it (LoadShared: the items; then the built-in dungeon
// list; then LICHII_COOP_DUNGEON_LISTS), with `lists` as the switch.
host::SharedFlags Shared(const std::string& lists) {
  std::optional<host::SharedFlags> shared = host::item_shared_flags();
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  std::string error;
  const auto all = host::DungeonList::load_many(lists, &error);
  INFO(error);
  REQUIRE(all);
  host::apply_dungeon_list(shared, *all);
  return *shared;
}

const char* const kTimed[] = {"PouchData.Item_OddMushroom.StockNum", "PouchData.Item_EyeballFrog.StockNum",
                              "PouchData.Item_EyeDrop.StockNum"};

}  // namespace

TEST_CASE("trade list: the kit's file, its kinds and counts") {
  const auto list = Load("shared_dungeon_trades.txt");
  CHECK(list.once().size() == 5);
  CHECK(list.share().size() == 20);  // the Poe points (an Int), ten Big Poes, nine Biggoron lines
  CHECK(list.share_enums().size() == 3);
  CHECK(list.unshare().size() == 3);
  CHECK(list.holds().empty());
  CHECK(list.pickup_keys().empty());
  for (const char* once : {"Npc_Hylia_020_Shop_Item_Bottle_Present", "Market_Item_Bottle_TutorialEvent_Cleared",
                           "IsGet.Item_Bottle_Letter", "Get_StoneOfWater_GetRutoLetter", "IsAfterSell_Mask_Bunny"}) {
    INFO(once);
    CHECK(Has(list.once(), once));
  }
  for (const char* plain : {"GhostShop_CouponNum", "HyruleField_Defeat_BigPoe_01", "HyruleField_Defeat_BigPoe_10",
                            "GetSwordBigGoron_Hylia_017_ChangeScheduleArea",
                            "GetSwordBigGoron_RejectDeliveryAfterSpoiled_KingZora",
                            "GetSwordBigGoron_Step04_GiveUp", "GetSwordBigGoron_Step10_GiveUp"}) {
    INFO(plain);
    CHECK(Has(list.share(), plain));
  }
  for (const char* timed : kTimed) {
    INFO(timed);
    CHECK(Has(list.unshare(), timed));
  }
  // Each player's: the bottle tutorial's run, the clocks, the Poe talk's first time.
  for (const char* own : {"Step_Market_Item_Bottle_TutorialEvent", "Market_Item_Bottle_TutorialEvent_Started",
                          "GetSwordBigGoron_ElapsedTime", "HyruleCastle_StopWeirdEggHatchTimer",
                          "IsGet.Item_Bottle_BigPoe", "GhostShop_CouponNum_ForMessage"}) {
    INFO(own);
    CHECK_FALSE(Has(list.share(), own));
    CHECK_FALSE(Has(list.once(), own));
    CHECK_FALSE(list.share_enums().contains(host::flag_hash(own)));
  }
}

TEST_CASE("trade list: the chains' steps go both ways, with the game's values in order") {
  const auto list = Load("shared_dungeon_trades.txt");
  const auto& enums = list.share_enums();

  const std::uint32_t ghost = host::flag_hash("Step_SubEvent_GhostShop");
  CHECK(ghost == 0x6e9b2539u);
  REQUIRE(enums.contains(ghost));
  CHECK_FALSE(enums.steps(ghost));
  const auto* ghost_values = enums.values(ghost);
  REQUIRE(ghost_values);
  const char* ghost_names[] = {"NotReady", "Ready", "Playing", "Complete"};
  REQUIRE(ghost_values->size() == std::size(ghost_names));
  for (std::size_t i = 0; i < ghost_values->size(); ++i) {
    INFO(ghost_names[i]);
    CHECK((*ghost_values)[i] == host::flag_hash(ghost_names[i]));
  }

  const std::uint32_t mask = host::flag_hash("Step_SubEvent_MaskSalesman");
  CHECK(mask == 0xea010310u);
  REQUIRE(enums.contains(mask));
  CHECK_FALSE(enums.steps(mask));
  const auto* mask_values = enums.values(mask);
  REQUIRE(mask_values);
  const char* mask_names[] = {"NotReady",     "Ready",       "UntilRentMaskOfKeaton", "Step01_Hint",
                              "Step01",       "Step02",      "UntilRentMaskOfSkull",  "Step03_Hint",
                              "Step03",       "Step04",      "UntilRentMaskOfSpooky", "Step05_Hint",
                              "Step05",       "Step06",      "UntilRentMaskOfBunny",  "Step07_Hint",
                              "Step07",       "Step08",      "Complete"};
  REQUIRE(mask_values->size() == std::size(mask_names));
  for (std::size_t i = 0; i < mask_values->size(); ++i) {
    INFO(mask_names[i]);
    CHECK((*mask_values)[i] == host::flag_hash(mask_names[i]));
  }

  // Biggoron's step: the minigame list's line, value for value (both lists merge).
  const std::uint32_t biggoron = host::flag_hash("Step_SubEvent_GetSwordBigGoron");
  const auto mini = Load("shared_dungeon_minigames.txt");
  REQUIRE(enums.contains(biggoron));
  REQUIRE(mini.share_enums().contains(biggoron));
  CHECK(*enums.values(biggoron) == *mini.share_enums().values(biggoron));
  CHECK(enums.steps(biggoron) == mini.share_enums().steps(biggoron));
  CHECK(enums.values(biggoron)->size() == 27);

  // Not the story list's (its steps only move forward; these chains go back when an item spoils).
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  for (const std::uint32_t step : {ghost, mask, biggoron}) {
    INFO(step);
    CHECK_FALSE(story->step_flags().contains(step));
  }
}

TEST_CASE("trade list: loads with every kit list, adds only what no other list shares") {
  const auto trades = Load("shared_dungeon_trades.txt");
  const auto child = Load("shared_dungeon_child.txt");
  const auto adult = Load("shared_dungeon_adult.txt");
  const auto mini = Load("shared_dungeon_minigames.txt");
  std::string error;
  CHECK_MESSAGE(host::DungeonList::load_many(kKitLists, &error), error);
  // The towns list (orm/pueblos) has Ruto's letter as the same once: line: the two merge.
  if (std::filesystem::exists(kKit + "shared_dungeon_towns.txt")) {
    CHECK_MESSAGE(host::DungeonList::load_many(kKitLists + "," + kKit + "shared_dungeon_towns.txt", &error), error);
  }

  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  const auto talk = TalkFlags();
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  std::set<std::uint32_t> story_flags;
  for (const auto& f : story->end_flags()) story_flags.insert(f.hash);

  std::vector<host::FlagKey> mine = trades.once();
  mine.insert(mine.end(), trades.share().begin(), trades.share().end());
  mine.insert(mine.end(), trades.unshare().begin(), trades.unshare().end());
  for (const auto& k : mine) {
    INFO(k.hash);
    CHECK_FALSE(dungeon.contains(k.hash));
    CHECK_FALSE(talk.contains(k.hash));
    CHECK_FALSE(story_flags.contains(k.hash));
    for (const auto* other : {&child, &adult, &mini}) {
      CHECK_FALSE(Sorted(other->once(), k));
      CHECK_FALSE(Sorted(other->share(), k));
      CHECK_FALSE(Sorted(other->unshare(), k));
    }
  }
  // Only the unshare: lines name item flags: the rest is world state the item table does not have.
  for (const auto& k : trades.once()) CHECK(host::find_item(k.hash) == nullptr);
  for (const auto& k : trades.share()) CHECK(host::find_item(k.hash) == nullptr);

  // As the hook logs it with only this file: 5 once + 20 plain + 3 Enum added, the 3 items taken off.
  std::optional<host::SharedFlags> shared = host::item_shared_flags();
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  const auto r = host::apply_dungeon_list(shared, trades);
  CHECK(r.added == 28);
  CHECK(r.removed == 3);
  CHECK(r.once == 5);
  CHECK(r.held == 0);
  CHECK(r.pickups == 0);
  CHECK(r.enum_conflicts == 0);
  CHECK(r.enum_kept == 0);
}

TEST_CASE("trade list: the timed items are each receiver's, the rest of the trade slot stays one for the group") {
  // The three are trade items the built-in table mirrors (docs/items.md): taken off, they are
  // neither sent nor taken (FlagWatcher::takes: not shared).
  for (const char* timed : kTimed) {
    INFO(timed);
    const auto* item = host::find_item(host::flag_hash(timed));
    REQUIRE(item != nullptr);
    CHECK(item->kind == coop::protocol::FlagKind::int32);
    CHECK(host::built_in_item_rule(item->hash) == host::ItemRule::mirror);
  }
  const KitItemRules rules;
  const auto with = Shared(kKitLists);
  for (const char* timed : kTimed) {
    INFO(timed);
    CHECK_FALSE(with.contains(Named(timed)));
  }
  // Without this list (the mutant of the trueque-* scenarios) the three are shared as before.
  const auto without = Shared(kKit + "shared_dungeon_child.txt," + kKit + "shared_dungeon_adult.txt," + kKit +
                              "shared_dungeon_minigames.txt");
  for (const char* timed : kTimed) {
    INFO(timed);
    CHECK(without.contains(Named(timed)));
  }
  // The other trade items have no timer: one slot for the group, handing one over takes it.
  for (const char* item :
       {"PouchData.Item_PocketEgg.StockNum", "PouchData.Item_PocketCucco.StockNum",
        "PouchData.Item_CojiroCucco.StockNum", "PouchData.Item_OddPowder.StockNum",
        "PouchData.Item_PoacherSaw.StockNum", "PouchData.Item_BrokenSword.StockNum",
        "PouchData.Item_Prescription.StockNum", "PouchData.Item_ClaimCheck.StockNum",
        "PouchData.Item_ZeldaLetter.StockNum"}) {
    INFO(item);
    CHECK(with.contains(Named(item)));
    REQUIRE(host::find_item(host::flag_hash(item)) != nullptr);
    CHECK(host::find_item(host::flag_hash(item))->rule == host::ItemRule::mirror);
  }
  // The chains' steps are shared, with their values.
  for (const char* step : {"Step_SubEvent_GetSwordBigGoron", "Step_SubEvent_MaskSalesman", "Step_SubEvent_GhostShop"}) {
    INFO(step);
    CHECK(with.contains(Named(step)));
    CHECK(with.enums().contains(host::flag_hash(step)));
  }
  // The masks are the group's (max): selling one takes none away.
  for (const char* mask : {"PouchData.Armor_Child_Mask_Keaton.StockNum", "PouchData.Armor_Child_Mask_Bunny.StockNum",
                           "PouchData.Armor_Child_Mask_Truth.StockNum"}) {
    INFO(mask);
    CHECK(with.contains(Named(mask)));
  }
}

TEST_CASE("trade list: with every kit list on, a bottle is the group's and what is in it never is") {
  const KitItemRules rules;
  const auto shared = Shared(kKitLists);
  for (const char slot : {'A', 'B', 'C', 'D', 'E'}) {
    const std::string bottle = std::string("PlayerStatus.AssignableItem.BottleInfo.Bottle") + slot;
    const std::string item = bottle + ".BottleItem";
    const std::string valid = bottle + ".IsValid";
    const std::string stock = std::string("PouchData.Bottle") + slot + ".StockNum";
    INFO(item);
    CHECK_FALSE(shared.contains(Named(item.c_str())));
    CHECK_FALSE(shared.enums().contains(host::flag_hash(item)));
    CHECK(shared.contains(Named(valid.c_str())));
    CHECK(shared.contains(Named(stock.c_str())));
  }
  for (const char* once : {"Npc_Hylia_020_Shop_Item_Bottle_Present", "IsGet.Item_Bottle_Letter",
                           "Get_StoneOfWater_GetRutoLetter", "HyruleField_Defeat_BigPoe_05", "GhostShop_CouponNum"}) {
    INFO(once);
    CHECK(shared.contains(Named(once)));
  }
}

TEST_CASE("trade list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/botella-*.txt and trueque-*.txt match these in the logs.
  CHECK(host::flag_hash("PouchData.Item_OddMushroom.StockNum") == 0x07de3e6fu);
  CHECK(host::flag_hash("PouchData.Item_EyeballFrog.StockNum") == 0x919ea324u);
  CHECK(host::flag_hash("PouchData.Item_EyeDrop.StockNum") == 0x5c41a6b1u);
  CHECK(host::flag_hash("PouchData.Item_OddPowder.StockNum") == 0x58c9bc78u);
  CHECK(host::flag_hash("Step_SubEvent_GetSwordBigGoron") == 0x26a2e01cu);
  CHECK(host::flag_hash("Step04") == 0x27f660feu);
  CHECK(host::flag_hash("Step05") == 0x5cde171fu);
  CHECK(host::flag_hash("Step09") == 0x5daeca58u);
  CHECK(host::flag_hash("Step10") == 0x0be69809u);
  CHECK(host::flag_hash("UntilRentMaskOfKeaton") == 0x60c72303u);
  CHECK(host::flag_hash("Step02") == 0x0e5afad8u);
  CHECK(host::flag_hash("Complete") == 0x4c0a63f4u);
  CHECK(host::flag_hash("Playing") == 0xffa8da63u);
  CHECK(host::flag_hash("IsAfterSell_Mask_Bunny") == 0x7d3acd77u);
  CHECK(host::flag_hash("GhostShop_CouponNum") == 0xe3407ceeu);
  CHECK(host::flag_hash("HyruleField_Defeat_BigPoe_01") == 0xacdfefefu);
  CHECK(host::flag_hash("HyruleField_Defeat_BigPoe_02") == 0xb00def69u);
  CHECK(host::flag_hash("Npc_Hylia_020_Shop_Item_Bottle_Present") == 0x6067ac7du);
  CHECK(host::flag_hash("Market_Item_Bottle_TutorialEvent_Cleared") == 0x4d62de75u);
  CHECK(host::flag_hash("Market_Item_Bottle_TutorialEvent_Started") == 0x39d7877du);
  CHECK(host::flag_hash("IsGet.Item_Bottle_Letter") == 0x170b5df2u);
  CHECK(host::flag_hash("Get_StoneOfWater_GetRutoLetter") == 0x77dc0488u);
  CHECK(host::flag_hash("PlayerStatus.AssignableItem.BottleInfo.BottleA.BottleItem") == 0x7d48df76u);
  CHECK(host::flag_hash("Empty") == 0xbe69de49u);
  CHECK(host::flag_hash("Fairy") == 0x6f49e357u);
}
