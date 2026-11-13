// Tests for the collectible list (tools/release/kit/shared_dungeon_collectibles.txt, oot-remake
// docs/COLECCIONABLES.md, written by oot-remake/tools/romfs/collectible_list.py, which checks
// every line and the coverage against the romfs). The rule, as in oot-64 (one save for the
// group): a loose heart piece, a heart piece a person gives, a planted bean and a one-time scrub
// sale are taken once for everyone; the beans, their price and the ammunition stay each
// player's. These checks keep the list meaningful against the other lists without the game.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "coop_chests.h"
#include "coop_dungeon.h"
#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"

namespace host = coop::host;

namespace {

const std::string kKit = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";

host::FlagKey Named(std::uint32_t hash) { return host::FlagKey(hash, std::uint16_t{0}); }
host::FlagKey Named(const char* name) { return Named(host::flag_hash(name)); }

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

// The loose heart pieces (Obj_Heart_Piece, Obj_GerudoValley_Heart_Piece placements of the Banc).
constexpr std::uint64_t kLoosePieces[] = {
    0x8f5f3d3321aeb7cd, 0x0a564328faf93c71, 0x09ecb6a0150fee97, 0x92feab9a9810c619, 0xdfd66c63357cdea7,
    0x1df0dbf50c1732b7, 0xbaec91d6ed8dd96b, 0x88358af5b5300aa9, 0xb9d0070b468f26d2, 0xf8c7601da7dd887e,
    0xac2e26b5ec7da3a2, 0x6f736fc80f7b0aca, 0x3a23b59eaedc0510, 0x9efabd5b0516c341,
};
// The soils' saved Bools (GameData struct 0x820c6b5e: crater, trail, Colossus, valley, Graveyard,
// Lake Hylia, Kokiri twice, Lost Woods twice, Zora's River).
constexpr std::uint32_t kSoils[] = {0x562fab76, 0xf2ceba92, 0x2a1c58ce, 0x50a1775e, 0x274b7e3d, 0xb0377637,
                                    0xe7bb7f2f, 0xb55acadd, 0x610eb13c, 0x5ebf7c8b, 0x5eea40a6};
constexpr const char* kNamed[] = {
    "Npc_Hylia_045_HasDived", "KakarikoVillage_IsAfterSave_Npc_Hylia_025", "MiniGame_FrogTenors_InsectRepellent",
    "LostWood_Enemy_Scrub_Business_DekuStickPouch_01", "Lostwood_Enemy_Scrub_Business_DekuNutsPouch_01"};

}  // namespace

TEST_CASE("collectible list: the kit's file, its kinds and counts") {
  const auto list = Load("shared_dungeon_collectibles.txt");
  const auto keys = list.pickup_keys();
  CHECK(keys.size() == std::size(kLoosePieces));
  for (const auto key : kLoosePieces) {
    INFO(key);
    CHECK(Has(keys, host::FlagKey::key64_flag(key)));
  }
  CHECK(list.once().size() == std::size(kSoils) + std::size(kNamed));
  for (const auto soil : kSoils) {
    INFO(soil);
    CHECK(Has(list.once(), Named(soil)));
  }
  for (const char* name : kNamed) {
    INFO(name);
    CHECK(Has(list.once(), Named(name)));
  }
  // The key64 lines are the only plain lines (share() holds them); nothing taken off or held.
  CHECK(list.share().size() == std::size(kLoosePieces));
  CHECK(list.share_enums().empty());
  CHECK(list.unshare().empty());
  CHECK(list.holds().empty());
}

TEST_CASE("collectible list: loads with the other kit lists, adds only what no list shares") {
  std::string error;
  const std::string lists = kKit + "shared_dungeon_child.txt," + kKit + "shared_dungeon_adult.txt," + kKit +
                            "shared_dungeon_minigames.txt," + kKit + "shared_dungeon_collectibles.txt";
  const auto all = host::DungeonList::load_many(lists, &error);
  INFO(error);
  REQUIRE(all);  // no flag with two kinds across the four lists (coop-options.ini's order)

  const auto mine = Load("shared_dungeon_collectibles.txt");
  const auto child = Load("shared_dungeon_child.txt");
  const auto adult = Load("shared_dungeon_adult.txt");
  const auto mini = Load("shared_dungeon_minigames.txt");
  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  host::reset_item_rules();
  for (const auto& k : mine.once()) {
    INFO(k.hash);
    CHECK_FALSE(dungeon.contains(k.hash));
    CHECK(host::find_item(k.hash) == nullptr);
    for (const auto* other : {&child, &adult, &mini}) {
      CHECK_FALSE(Has(other->once(), k));
      CHECK_FALSE(Has(other->share(), k));
    }
  }
  std::set<std::uint64_t> chests;
  for (const auto& c : CoopChests::Keys()) chests.insert(c.key);
  const auto gold = host::gold_skulltula_keys();
  for (const auto& k : mine.pickup_keys()) {
    INFO(k.key);
    CHECK_FALSE(chests.contains(k.key));
    CHECK(std::find(gold.begin(), gold.end(), k.key) == gold.end());
    for (const auto* other : {&child, &adult, &mini}) CHECK_FALSE(Has(other->share(), k));
  }

  // As the hook logs it with only this file on (the scenarios coleccion-pieza and coleccion-judia):
  // "30 flags and keys added, 0 taken off, 16 once, 0 held in their area, 14 one-time keys".
  std::optional<host::SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  const auto r = host::apply_dungeon_list(shared, mine);
  CHECK(r.added == 30);
  CHECK(r.removed == 0);
  CHECK(r.once == 16);
  CHECK(r.held == 0);
  CHECK(r.pickups == 14);
  for (const auto key : kLoosePieces) CHECK(shared->contains(host::FlagKey::key64_flag(key)));
  for (const auto soil : kSoils) CHECK(shared->contains(Named(soil)));
}

TEST_CASE("collectible list: the 100 gold Skulltulas and the per-player beans") {
  // The coverage collectible_list.py checks in the romfs: the built-in token list has the 100.
  const auto gold = host::gold_skulltula_keys();
  CHECK(gold.size() == 100);
  CHECK(std::set<std::uint64_t>(gold.begin(), gold.end()).size() == 100);
  host::reset_item_rules();
  // Four pieces, a heart for everyone: the piece count is a shared count.
  const auto* pieces = host::find_item(host::flag_hash("PouchData.Obj_Heart_Piece.StockNum"));
  REQUIRE(pieces);
  CHECK(pieces->rule == host::ItemRule::count);
  // The beans themselves stay each player's (COLECCIONABLES.md, as ammunition).
  const auto* beans = host::find_item(host::flag_hash("PouchData.Item_MagicBean.StockNum"));
  REQUIRE(beans);
  CHECK(beans->rule == host::ItemRule::per_player);
}

TEST_CASE("collectible list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/coleccion-*.txt match these hashes in the logs.
  CHECK(host::flag_hash("Npc_Hylia_045_HasDived") == 0xad7a21ceu);
  CHECK(host::flag_hash("KakarikoVillage_IsAfterSave_Npc_Hylia_025") == 0x85d8113au);
  CHECK(host::flag_hash("MiniGame_FrogTenors_InsectRepellent") == 0x8ffec767u);
  CHECK(host::flag_hash("LostWood_Enemy_Scrub_Business_DekuStickPouch_01") == 0x8c98cf5au);
  CHECK(host::flag_hash("Lostwood_Enemy_Scrub_Business_DekuNutsPouch_01") == 0x5ef24799u);
  CHECK(host::flag_hash("PouchData.Obj_Heart_Piece.StockNum") == 0x51ec97b3u);
  // The soil's Bool is the struct member's value for the soil's placement, Murmur3 of its key in decimal
  // (Kokiri Forest's soil with bugs, placement 0x548d80c51011dd32): the member, as the game looks it up.
  CHECK(host::flag_hash(std::to_string(0x548d80c51011dd32ull)) == 0xef729864u);
}
