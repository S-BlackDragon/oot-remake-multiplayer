// Tests for the temples list (tools/release/kit/shared_dungeon_temples.txt, oot-remake docs/TEMPLOS.md,
// written by oot-remake/tools/romfs/temples_list.py). The rule, as in oot-64 (one world for the group): a
// Goron of the Fire Temple freed by one player is gone from his cell for everyone (once:, one way), and the
// count of Gorons still caught is a plain shared Int. These checks keep the list meaningful against the
// other lists without the game.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <optional>
#include <set>
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
const char* const kGorons[] = {"001", "002", "003", "004", "005", "007", "008", "009", "010"};

host::FlagKey Named(const std::string& name) { return host::FlagKey(host::flag_hash(name), std::uint16_t{0}); }

bool Has(const std::vector<host::FlagKey>& keys, const std::string& name) {
  return std::find(keys.begin(), keys.end(), Named(name)) != keys.end();
}

host::DungeonList Load(const std::string& file) {
  std::string error;
  auto list = host::DungeonList::load(kKit + file, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

}  // namespace

TEST_CASE("temples list: the kit's file, eighteen Goron lines once and the count") {
  const auto list = Load("shared_dungeon_temples.txt");
  CHECK(list.once().size() == 18);
  for (const char* n : kGorons) {
    INFO(n);
    CHECK(Has(list.once(), std::string("Npc_Goron_") + n + "_Delete"));
    CHECK(Has(list.once(), std::string("Npc_Goron_") + n + "_Adult_FireTemple_Delete"));
  }
  CHECK_FALSE(Has(list.once(), "Npc_Goron_006_Delete"));  // Link the Goron has no cell
  REQUIRE(list.share().size() == 1);
  CHECK(Has(list.share(), "NumberOfCoughtGoron"));
  CHECK(list.unshare().empty());
  CHECK(list.holds().empty());
  CHECK(list.pickup_keys().empty());
  CHECK(list.share_enums().size() == 0);
}

TEST_CASE("temples list: adds only what no other list shares, the cells stay daurosk's") {
  const auto temples = Load("shared_dungeon_temples.txt");
  const auto child = Load("shared_dungeon_child.txt");
  const auto adult = Load("shared_dungeon_adult.txt");
  const auto mini = Load("shared_dungeon_minigames.txt");
  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  const auto story = host::StoryList::load(kKit + "shared_story.txt");
  REQUIRE(story);
  std::set<std::uint32_t> story_flags;
  for (const auto& f : story->end_flags()) story_flags.insert(f.hash);

  std::vector<host::FlagKey> mine = temples.once();
  mine.insert(mine.end(), temples.share().begin(), temples.share().end());
  for (const auto& k : mine) {
    INFO(k.hash);
    CHECK_FALSE(dungeon.contains(k.hash));
    CHECK_FALSE(story_flags.contains(k.hash));
    CHECK(host::find_item(k.hash) == nullptr);
    for (const host::DungeonList* other : {&child, &adult, &mini}) {
      const bool once = std::find(other->once().begin(), other->once().end(), k) != other->once().end();
      const bool share = std::find(other->share().begin(), other->share().end(), k) != other->share().end();
      CHECK_FALSE(once);
      CHECK_FALSE(share);
    }
  }
  // The cell doors are in daurosk's built-in list already (TEMPLOS.md, section 2).
  for (const char* n : kGorons) {
    INFO(n);
    CHECK(dungeon.contains(host::flag_hash(std::string("Npc_Goron_") + n + "_ShutterisOpen")));
  }

  // As the hook logs it with only this file: 18 once + 1 Int added, nothing taken off.
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  const auto r = host::apply_dungeon_list(shared, temples);
  CHECK(r.added == 19);
  CHECK(r.removed == 0);
  CHECK(r.once == 18);
  CHECK(r.held == 0);
  CHECK(r.pickups == 0);
  CHECK(shared->contains(Named("NumberOfCoughtGoron")));
  CHECK(shared->contains(Named("Npc_Goron_010_Adult_FireTemple_Delete")));
}

TEST_CASE("temples list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/templo-goron.*.txt match these hashes in the logs.
  CHECK(host::flag_hash("Npc_Goron_001_Delete") == 0x963cce38u);
  CHECK(host::flag_hash("Npc_Goron_001_Adult_FireTemple_Delete") == 0x364ca911u);
  CHECK(host::flag_hash("Npc_Goron_010_Delete") == 0x20dc1f8au);
  CHECK(host::flag_hash("Npc_Goron_010_Adult_FireTemple_Delete") == 0xa4e7353fu);
  CHECK(host::flag_hash("NumberOfCoughtGoron") == 0x12295a45u);
  CHECK(host::flag_hash("Npc_Goron_001_ShutterisOpen") == 0x023e190fu);  // coop_dungeon.cpp's table
}
