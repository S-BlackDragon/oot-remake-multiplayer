// Tests for the town list (tools/release/kit/shared_dungeon_towns.txt, oot-remake docs/PUEBLOS.md,
// written by oot-remake/tools/romfs/town_list.py). The rule, as in oot-64 (one world for the
// group, D-061): what opens a way or is done for good in a town is done once for everyone
// (once:, one-time keys); a played event whose result would change the town under a Link
// standing in it waits while he is there (hold:); what one player does there stays his. These
// checks keep the list meaningful against the other lists without the game.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "coop_dungeon.h"
#include "coop_story.h"
#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"

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

// The kit's lists as coop-options.ini turns them on, in its order.
std::string KitLists() {
  std::string out;
  for (const char* f : {"shared_dungeon_child.txt", "shared_dungeon_adult.txt", "shared_dungeon_minigames.txt",
                        "shared_dungeon_montana.txt", "shared_dungeon_towns.txt"}) {
    if (!out.empty()) out += ',';
    out += kKit + f;
  }
  return out;
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

constexpr const char* kOnce[] = {
    "KakarikoVillage_Gimmick_IsReDeadDestroyed",  // the ReDead grotto
    "GraveYard_Field_Sharp_Dead",     "GraveYard_Field_Flat_Dead",  // the Poe brothers
    "RoyalFamilyTomb_Room00_Cleared",  // the royal tomb's first room
    "Get_StoneOfWater_GetRutoLetter",  // Ruto's letter found (the letter is in the finder's bottle)
    "Gimmick_MagicBeanSoil_ZoraRiver",  // the bean in Zora's River
    "HrlCastle_GateOpen",              // the castle gate
};

struct Held {
  const char* name;
  const char* place;
};
constexpr Held kHeld[] = {
    {"HavePlayedEvent.RoyalFamilyTomb_00_EnemyAnnihilation_Clear", "Graveyard"},
    {"HavePlayedEvent.Dm_LakeHrl_0020", "HyruleField"},  // the lake's water (Lake Hylia is in the field's scene)
    {"SageOfWater_KingZora_RedIce_02", "ZoraDomain"},     // King Zora's red ice (1 until melted)
    {"HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice", "ZoraDomain"},
};

}  // namespace

TEST_CASE("town list: the kit's file, its kinds and counts") {
  const auto list = Load("shared_dungeon_towns.txt");
  CHECK(list.once().size() == std::size(kOnce));
  for (const char* name : kOnce) {
    INFO(name);
    CHECK(Has(list.once(), Named(name)));
  }
  REQUIRE(list.holds().size() == std::size(kHeld));
  for (const auto& h : kHeld) {
    INFO(h.name);
    CHECK(list.held_in(host::flag_hash(h.name)) == std::optional<std::string_view>(h.place));
  }
  // The adult towns' saved breakables: one-time keys, nothing else shared plainly.
  CHECK(list.pickup_keys().size() == 13);
  CHECK(list.share().size() == 13);
  CHECK(Has(list.share(), host::FlagKey::key64_flag(0x4d1752c8637f713aull)));  // the King's ice
  CHECK(Has(list.share(), host::FlagKey::key64_flag(0x2e5e979511117605ull)));  // the same, in the _Alternative Domain
  CHECK(list.share_enums().empty());
  CHECK(list.unshare().empty());
}

TEST_CASE("town list: a held flag waits in its town, its other era and its _Alternative version") {
  const auto list = Load("shared_dungeon_towns.txt");
  const auto lake = host::flag_hash("HavePlayedEvent.Dm_LakeHrl_0020");
  CHECK(list.holds_in(lake, "HyruleField_Adult"));
  CHECK(list.holds_in(lake, "HyruleField_Child"));
  CHECK_FALSE(list.holds_in(lake, "KakarikoVillage_Adult"));  // out of the field: the lake fills
  CHECK_FALSE(list.holds_in(lake, "ZoraDomain_Adult"));
  const auto ice = host::flag_hash("SageOfWater_KingZora_RedIce_02");
  CHECK(list.holds_in(ice, "ZoraDomain_Adult"));
  CHECK(list.holds_in(ice, "ZoraDomain_Adult_Alternative"));  // the bug of PUEBLOS.md: nothing was held there
  CHECK_FALSE(list.holds_in(ice, "ZoraRiver_Adult"));
  CHECK_FALSE(list.holds_in(ice, "ZoraFountain_Adult"));
  const auto tomb = host::flag_hash("HavePlayedEvent.RoyalFamilyTomb_00_EnemyAnnihilation_Clear");
  CHECK(list.holds_in(tomb, "Graveyard_Adult"));
  CHECK(list.holds_in(tomb, "Graveyard_Child"));
  CHECK_FALSE(list.holds_in(tomb, "KakarikoVillage_Child"));

  // The child list's Kakariko events wait in adult Kakariko's second version too.
  std::string error;
  const auto all = host::DungeonList::load_many(KitLists(), &error);
  INFO(error);
  REQUIRE(all);
  const auto gate = host::flag_hash("HavePlayedEvent.KakarikoVillage_ClosedGateOpen");
  REQUIRE(all->held_in(gate));
  CHECK(all->holds_in(gate, "KakarikoVillage_Adult_Alternative"));
  CHECK(all->holds_in(gate, "KakarikoVillage_Child"));
  CHECK(all->holds_in(ice, "ZoraDomain_Adult_Alternative"));
}

TEST_CASE("town list: loads with every kit list, adds only what no other list shares") {
  // coop-options.ini's line: one bad pair of kinds and no list is used at all ("the lists are
  // not used"); the King's ice was once: in the mountain list and hold: here.
  std::string error;
  const auto all = host::DungeonList::load_many(KitLists(), &error);
  INFO(error);
  REQUIRE(all);

  const auto towns = Load("shared_dungeon_towns.txt");
  std::vector<host::FlagKey> others_named;
  for (const char* f : {"shared_dungeon_child.txt", "shared_dungeon_adult.txt", "shared_dungeon_minigames.txt",
                        "shared_dungeon_montana.txt"}) {
    const auto other = Load(f);
    others_named.insert(others_named.end(), other.once().begin(), other.once().end());
    others_named.insert(others_named.end(), other.share().begin(), other.share().end());
    for (const auto& h : other.holds()) others_named.push_back(host::FlagKey(h.hash, std::uint16_t{0}));
  }
  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  const auto talk = TalkFlags();

  std::vector<host::FlagKey> mine = towns.once();
  for (const auto& h : towns.holds()) mine.push_back(host::FlagKey(h.hash, std::uint16_t{0}));
  for (const auto& k : mine) {
    INFO(k.hash);
    CHECK_FALSE(Has(others_named, k));
    CHECK_FALSE(dungeon.contains(k.hash));
    CHECK_FALSE(talk.contains(k.hash));
    CHECK(host::find_item(k.hash) == nullptr);
  }
  // Two held events are the built-in story list's already (shared live there): the list holds them.
  CHECK(CoopStory::Find(host::flag_hash("HavePlayedEvent.Dm_LakeHrl_0020")) != nullptr);
  CHECK(CoopStory::Find(host::flag_hash("HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice")) != nullptr);
  for (const char* name : kOnce) {
    INFO(name);
    CHECK(CoopStory::Find(host::flag_hash(name)) == nullptr);
  }

  // As the hook logs it with only this file, over the built-in dungeon list: 7 once + 4 held +
  // 13 keys added; over the dungeon and story lists (the hook's order), the two story events are
  // there already: 22.
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  auto r = host::apply_dungeon_list(shared, towns);
  CHECK(r.added == 24);
  CHECK(r.removed == 0);
  CHECK(r.once == 7);
  CHECK(r.held == 4);
  CHECK(r.pickups == 13);
  std::optional<host::SharedFlags> with_story;
  CoopDungeon::Merge(with_story, true);
  CoopStory::Merge(with_story, true);
  r = host::apply_dungeon_list(with_story, towns);
  CHECK(r.added == 22);
}

TEST_CASE("town list: with every kit list on, the towns' work is shared and each player's is not") {
  std::string error;
  const auto all = host::DungeonList::load_many(KitLists(), &error);
  INFO(error);
  REQUIRE(all);
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  host::apply_dungeon_list(shared, *all);
  for (const char* name : kOnce) {
    INFO(name);
    CHECK(shared->contains(Named(name)));
    CHECK(Has(all->once(), Named(name)));  // once: in the merged list: a 0 never undoes it
  }
  for (const auto& h : kHeld) {
    INFO(h.name);
    CHECK(shared->contains(Named(h.name)));
    CHECK(all->held_in(host::flag_hash(h.name)) == std::optional<std::string_view>(h.place));
  }
  // King Zora's ice is held (both ways), not once: (a 1 that never yields would freeze the King
  // again in a game where he was free).
  CHECK_FALSE(Has(all->once(), Named("SageOfWater_KingZora_RedIce_02")));
  // What a player does in a town stays his (oot-64): his talks, his sensors, his run, his
  // bottle and his cow. No name of this list says one of those.
  for (const char* name : kOnce) {
    const std::string_view n = name;
    INFO(name);
    for (const std::string_view own : {"Talk", "Tutorial", "Navi", "Near", "PosCheck", "InFront", "FoundBySecurity",
                                       "Playing", "Fishing", "Cow", "Bottle", "Record", "Best", "Positioning"}) {
      CHECK(n.find(own) == std::string_view::npos);
    }
  }
}

TEST_CASE("town list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/pueblo-*.txt match these hashes in the logs.
  CHECK(host::flag_hash("HavePlayedEvent.Dm_LakeHrl_0020") == 0x81f4c6b5u);
  CHECK(host::flag_hash("HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice") == 0xbca9167bu);
  CHECK(host::flag_hash("SageOfWater_KingZora_RedIce_02") == 0x41c0bd32u);
  CHECK(host::flag_hash("Get_StoneOfWater_GetRutoLetter") == 0x77dc0488u);
  CHECK(host::flag_hash("GraveYard_Field_Sharp_Dead") == 0x43b1c877u);
  CHECK(host::flag_hash("KakarikoVillage_Gimmick_IsReDeadDestroyed") == 0xbdbf9ba5u);
  CHECK(host::flag_hash("HavePlayedEvent.KakarikoVillage_TreasureBoxAppear") == 0x398e5400u);
}
