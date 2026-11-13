// Tests for the mountain list (tools/release/kit/shared_dungeon_montana.txt, oot-remake
// docs/MONTANA_SOMBRA.md, written by oot-remake/tools/romfs/montana_list.py): Death Mountain,
// Goron City, the Ice Cavern, the Shadow Temple and the Bottom of the Well. The rule, as in
// oot-64 (one save for the group, D-061): what the group did there is done for everyone (a rock
// blown, a Goron stopped, a door or a statue moved, the pot spinning); where one Link stands, what
// he carries and the boat he takes are his. These checks keep the list meaningful against the
// other lists without the game.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "coop_dungeon.h"
#include "coop_story.h"
#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/story.hpp"

namespace host = coop::host;

namespace {

const std::string kKit = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";

host::FlagKey Named(const char* name) { return host::FlagKey(host::flag_hash(name), std::uint16_t{0}); }

bool Has(const std::vector<host::FlagKey>& keys, host::FlagKey key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

host::DungeonList Load(const std::string& file) {
  std::string error;
  auto list = host::DungeonList::load(kKit + file, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

constexpr const char* kOnce[] = {
    "DeathMountainCrater_OpenTresureBox",  // the Crater grotto's chest
    "DarniaDoor_Opened",                   // Darunia's door
    "GoronLargePot_isMoving",              // the big pot spins
    "GoronCity_BombHitsGoron006",          // the rolling Goron stopped by a bomb
    "GoronCity_StopGoron006",
    "GoronCity_GetBombBag_fromGoron006",  // his bomb bag, once for the group
    "SageOfFire_GenerateDaruniaJr",       // Link the Goron
    "SageOfFire_Step01_DaruniaJr_BombHit",
    "SageOfFire_Step02_DaruniaJr_FirstTalked",
    "SageOfFire_Step02_TalkedSchedule",
    "SageOfFire_Step02_DoorOpen",    // Darunia's statue moved
    "SageOfWater_GetWaterSerenade",  // the Bool King Zora's talk reads
};

struct Held {
  const char* name;
  const char* place;
};
constexpr Held kHeld[] = {
    {"HavePlayedEvent.DeathMountainCrater_SecretHole_AppearTreasureBox", "DeathMountainCrater"},
    {"HavePlayedEvent.GoronCity_SecretHole_AppearTreasureBox", "GoronCity"},
    {"HavePlayedEvent.GoronCity_SecretHole_AppearTreasureBox2", "GoronCity"},
};

// What stays each player's (MONTANA_SOMBRA.md): the Lens on his face, his boat, the way he came
// into Goron City, where his Link is in room 09.
constexpr const char* kOwn[] = {
    "PlayerStatus.IsActiveGlassTruth",
    "HavePlayedEvent.ShadowTemple_Gimmick_Departure",
    "SageOfFire_FromCrater",
    "SageOfFire_FromDeathMt",
    "ShadowTemple_Level_Room_09_Away",
};

}  // namespace

TEST_CASE("mountain list: the kit's file, its kinds and counts") {
  const auto list = Load("shared_dungeon_montana.txt");
  CHECK(list.once().size() == std::size(kOnce));
  for (const char* name : kOnce) {
    INFO(name);
    CHECK(Has(list.once(), Named(name)));
  }
  REQUIRE(list.holds().size() == std::size(kHeld));
  for (const auto& h : kHeld) {
    INFO(h.name);
    CHECK(list.held_in(host::flag_hash(h.name)) == std::optional<std::string_view>(h.place));
    CHECK(list.holds_in(host::flag_hash(h.name), std::string(h.place) + "_Adult"));
    CHECK(list.holds_in(host::flag_hash(h.name), std::string(h.place) + "_Child"));
  }
  // 6 Trail rocks, the city's big rock and 18 lifted rocks: one-time keys, nothing else plain.
  CHECK(list.pickup_keys().size() == 25);
  CHECK(list.share().size() == 25);
  CHECK(Has(list.share(), host::FlagKey::key64_flag(0x8324a99325078f72ull)));  // a Trail rock
  CHECK(Has(list.share(), host::FlagKey::key64_flag(0x784dacc398d8b905ull)));  // the big rock
  CHECK(list.share_enums().empty());
  // Taken off: room 09's sensor, a flag of the built-in dungeon list.
  REQUIRE(list.unshare().size() == 1);
  CHECK(list.unshare()[0] == Named("ShadowTemple_Level_Room_09_Away"));
  const std::uint32_t sensor = host::flag_hash("ShadowTemple_Level_Room_09_Away");
  bool builtin = false;
  for (const auto& f : CoopDungeon::Flags()) builtin = builtin || f.hash == sensor;
  for (const auto& f : CoopDungeon::LiveFlags()) builtin = builtin || f.hash == sensor;
  CHECK(builtin);
}

TEST_CASE("mountain list: King Zora's ice is the town list's, held in the Domain") {
  // It was once: here and hold: in shared_dungeon_towns.txt: the two lists together failed to
  // load. The ice is 1 until melted (PUEBLOS.md), so it is shared both ways, held.
  const auto list = Load("shared_dungeon_montana.txt");
  CHECK_FALSE(Has(list.once(), Named("SageOfWater_KingZora_RedIce_02")));
  CHECK_FALSE(list.held_in(host::flag_hash("SageOfWater_KingZora_RedIce_02")));
  CHECK_FALSE(Has(list.share(), host::FlagKey::key64_flag(0x4d1752c8637f713aull)));
  CHECK_FALSE(Has(list.share(), host::FlagKey::key64_flag(0x2e5e979511117605ull)));
  const auto towns = Load("shared_dungeon_towns.txt");
  CHECK(towns.held_in(host::flag_hash("SageOfWater_KingZora_RedIce_02")) ==
        std::optional<std::string_view>("ZoraDomain"));
  CHECK(Has(towns.share(), host::FlagKey::key64_flag(0x4d1752c8637f713aull)));
}

TEST_CASE("mountain list: loads with the other lists, adds only what no list shares") {
  const auto mine = Load("shared_dungeon_montana.txt");
  std::vector<host::FlagKey> others;
  for (const char* f : {"shared_dungeon_child.txt", "shared_dungeon_adult.txt", "shared_dungeon_minigames.txt",
                        "shared_dungeon_towns.txt"}) {
    const auto other = Load(f);
    others.insert(others.end(), other.once().begin(), other.once().end());
    others.insert(others.end(), other.share().begin(), other.share().end());
    others.insert(others.end(), other.unshare().begin(), other.unshare().end());
    for (const auto& h : other.holds()) others.push_back(host::FlagKey(h.hash, std::uint16_t{0}));
  }
  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);

  std::vector<host::FlagKey> keys = mine.once();
  keys.insert(keys.end(), mine.share().begin(), mine.share().end());
  for (const auto& h : mine.holds()) keys.push_back(host::FlagKey(h.hash, std::uint16_t{0}));
  for (const auto& k : keys) {
    INFO(k.hash);
    INFO(k.key);
    CHECK_FALSE(Has(others, k));
    if (k.kind == host::FlagKeyKind::named) {
      CHECK_FALSE(dungeon.contains(k.hash));
      CHECK(CoopStory::Find(k.hash) == nullptr);
      CHECK(host::find_item(k.hash) == nullptr);  // the Goron Tunic and the bomb bag are the items list's
    }
  }

  // As the hook logs it with only this file, over the built-in dungeon list: 12 once + 3 held +
  // 25 keys added, room 09's sensor taken off.
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  REQUIRE(shared->contains(Named("ShadowTemple_Level_Room_09_Away")));
  const auto r = host::apply_dungeon_list(shared, mine);
  CHECK(r.added == 40);
  CHECK(r.removed == 1);
  CHECK(r.once == 12);
  CHECK(r.held == 3);
  CHECK(r.pickups == 25);
  CHECK_FALSE(shared->contains(Named("ShadowTemple_Level_Room_09_Away")));
}

TEST_CASE("mountain list: with every kit list on, each player's own never travels, the story steps stay steps") {
  std::string error;
  std::string paths;
  for (const char* f : {"shared_dungeon_child.txt", "shared_dungeon_adult.txt", "shared_dungeon_minigames.txt",
                        "shared_dungeon_montana.txt", "shared_dungeon_towns.txt"}) {
    if (!paths.empty()) paths += ',';
    paths += kKit + f;
  }
  const auto all = host::DungeonList::load_many(paths, &error);
  INFO(error);
  REQUIRE(all);
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  CoopStory::Merge(shared, true);
  REQUIRE(shared);
  host::apply_dungeon_list(shared, *all);
  for (const char* name : kOnce) {
    INFO(name);
    CHECK(shared->contains(Named(name)));
  }
  for (const char* own : kOwn) {
    INFO(own);
    CHECK_FALSE(shared->contains(Named(own)));
  }
  // Step_SageOfFire (Link the Goron's chain) and Step_SageOfShadow are the story list's steps,
  // forward only: this list names neither.
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  CHECK(story->step_flags().contains(host::flag_hash("Step_SageOfFire")));
  CHECK(story->step_flags().contains(host::flag_hash("Step_SageOfShadow")));
  CHECK_FALSE(all->share_enums().contains(host::flag_hash("Step_SageOfFire")));
  CHECK_FALSE(all->share_enums().contains(host::flag_hash("Step_SageOfShadow")));
}

TEST_CASE("mountain list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/montana-*.txt and sombra-*.txt match these in the logs.
  CHECK(host::flag_hash("GoronLargePot_isMoving") == 0xf495670cu);
  CHECK(host::flag_hash("GoronCity_StopGoron006") == 0xa0c92810u);
  CHECK(host::flag_hash("GoronCity_GetBombBag_fromGoron006") == 0x1fb5294du);
  CHECK(host::flag_hash("SageOfFire_Step02_DoorOpen") == 0x56f40096u);
  CHECK(host::flag_hash("SageOfFire_FromCrater") == 0x737d2078u);
  CHECK(host::flag_hash("ShadowTemple_Level_Room_09_Away") == 0x645bf023u);
}
