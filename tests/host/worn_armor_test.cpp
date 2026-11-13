// ORM (oot-remake docs/DETALLES_MARIONETA.md): the armor a player really
// wears goes in PLAYER_EQUIP (coop/host/worn_armor.hpp), on fake memory.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <utility>
#include <vector>

#include "coop/host/worn_armor.hpp"
#include "fake_memory.hpp"

using namespace coop::host;
using namespace coop::host::test;
namespace proto = coop::protocol;

namespace {

// The player's EquipmentUser with its 8 hand slots and 13 armor links, every
// one empty; pieces are actors with a name (as reader_test's put_user).
constexpr std::uint64_t kComponents = 0x92000000, kUser = 0x92100000, kHolders = 0x92200000,
                        kPieces = 0x92300000;

void put_user(FakeMemory& m, const Layout& l) {
  m.put_u64(l.actor1 + addr::kActorComponents, kComponents);
  m.put_u64(kComponents + 0x200, kUser);
  for (std::uint32_t i = 0; i < 8 + worn::kArmorLinkCount; ++i) {
    m.put_u64(kUser + 0x20 + 0x18 * i + 8, kHolders + 0x100 * i);
    m.put_u32(kUser + 0x20 + 0x18 * i + 0x10, 0xffffffffu);
  }
}

// Link `n` (0 to 7 the hand slots, 8 and on the armor links) holds a live
// piece called `name`.
void put_piece(FakeMemory& m, std::uint32_t n, const std::string& name, std::uint32_t state = 4) {
  const std::uint64_t holder = kHolders + 0x100 * n;
  const std::uint64_t piece = kPieces + 0x1000 * n;
  const std::uint64_t text = piece + 0x800;
  const std::uint32_t id = 300 + n;
  m.put_u32(kUser + 0x20 + 0x18 * n + 0x10, id);
  m.put_u64(holder + 0x40, piece);
  m.put_u32(holder + 0x48, id);
  m.put_u32(piece + 0x2c, state);
  m.put_u64(piece + 0x260, text);
  const std::string room(80, char{0});
  m.put(text, room.data(), room.size());
  m.put(text, name.c_str(), name.size() + 1);
}

constexpr std::uint32_t armor(std::uint32_t index) { return 8 + index; }

// A GameDataMgr with the String64 table (reader_test.cpp's layout), at
// addresses of its own.
constexpr std::uint64_t kGameData = 0x93000000, kStrEntries = 0x93100000, kStrSlots = 0x93200000,
                        kStrText = 0x93300000;
constexpr std::uint32_t kStrCap = 17;

void put_strings(FakeMemory& m, const std::vector<std::pair<std::uint32_t, std::string>>& flags) {
  m.put_u64(FakeMemory::kBase + 0x4e842a8, kGameData);
  m.put_u32(kGameData + 0x610, static_cast<std::uint32_t>(flags.size()));
  m.put_u64(kGameData + 0x618, kStrEntries);
  m.put_u64(kGameData + 0x620, kStrSlots);
  m.put_u32(kGameData + 0x62c, kStrCap);
  for (std::uint32_t i = 0; i < kStrCap; ++i) m.put_u64(kStrSlots + 8 * i, 0);
  for (std::uint32_t i = 0; i < flags.size(); ++i) {
    const auto& [hash, text] = flags[i];
    std::uint32_t s = hash % kStrCap;
    while (*read_u32(m, kStrSlots + 8 * s) != 0) s = (s + 1) % kStrCap;
    m.put_u32(kStrSlots + 8 * s, hash);
    m.put_u32(kStrSlots + 8 * s + 4, i);
    const std::uint64_t entry = kStrEntries + 0x108ull * i;
    const std::uint64_t buffer = kStrText + 0x40ull * i;
    m.put_u64(entry + 0x68, buffer);
    m.put_u32(entry + 0x70, 64);
    std::string bytes = text;
    bytes.resize(64, '\0');
    m.put(buffer, bytes.data(), bytes.size());
  }
}

}  // namespace

TEST_CASE("worn armor: which pieces are shown and which a puppet may take off") {
  CHECK(worn::shown_armor("Armor_Adult_HeavyBoot"));
  CHECK(worn::shown_armor("Armor_Child_Mask_Keaton"));
  CHECK(worn::shown_armor("Armor_Adult_Normal"));
  CHECK(worn::shown_armor("Armor_Child_GlassTruth"));
  CHECK_FALSE(worn::shown_armor("Armor_Child_Default_Head"));
  CHECK_FALSE(worn::shown_armor("Armor_Adult_Default_Head"));
  CHECK_FALSE(worn::shown_armor("Weapon_Sword_Kokiri"));
  CHECK_FALSE(worn::shown_armor(""));
  CHECK(worn::strayable_armor("Armor_Adult_HoverBoot"));
  CHECK(worn::strayable_armor("Armor_Adult_Gauntlet_Silver"));
  CHECK(worn::strayable_armor("Armor_Adult_Goron"));
  CHECK_FALSE(worn::strayable_armor("Armor_Adult_Normal"));
  CHECK_FALSE(worn::strayable_armor("Armor_Child_Default_Head"));

  CHECK(worn::worn_or_flag(std::string("Armor_Adult_HeavyBoot"), std::string("")) ==
        "Armor_Adult_HeavyBoot");
  CHECK(worn::worn_or_flag(std::string(""), std::string("Armor_Adult_Zora")) == "Armor_Adult_Zora");
  CHECK(worn::worn_or_flag(std::nullopt, std::string("Armor_Adult_Zora")) == "Armor_Adult_Zora");
  CHECK(worn::worn_or_flag(std::string("Armor_Child_Default_Head"), std::string("")) == "");
  CHECK_FALSE(worn::worn_or_flag(std::nullopt, std::nullopt));
}

TEST_CASE("worn armor: the reader sends the piece worn, else the flag") {
  FakeMemory m;
  const Layout l = make_game(m);
  PlayerReader reader(m);
  put_strings(m, {{addr::kEquipArmor, "Armor_Adult_Normal"},
                  {addr::kEquipBoot, ""},
                  {addr::kEquipHeadArmor, ""},
                  {addr::kEquipHandArmor, ""}});

  SUBCASE("no EquipmentUser: the flags, as before") {
    const auto e = reader.read_equipment();
    REQUIRE(e);
    CHECK(e->armor == "Armor_Adult_Normal");
    CHECK(e->boots == "");
    CHECK(e->head == "");
  }
  put_user(m, l);
  SUBCASE("empty links: the flags") {
    const auto e = reader.read_equipment();
    REQUIRE(e);
    CHECK(e->armor == "Armor_Adult_Normal");
    CHECK(e->boots == "");
    CHECK(e->head == "");
    CHECK(e->hand == "");
  }
  SUBCASE("boots put on with ZR (no flag) and gauntlets: the pieces") {
    put_piece(m, armor(worn::kArmorLower), "Armor_Adult_HeavyBoot");
    put_piece(m, armor(worn::kArmorHand), "Armor_Adult_Gauntlet_Silver");
    put_piece(m, armor(worn::kArmorUpper), "Armor_Adult_Zora");  // the menu just changed it
    const auto e = reader.read_equipment();
    REQUIRE(e);
    CHECK(e->boots == "Armor_Adult_HeavyBoot");
    CHECK(e->hand == "Armor_Adult_Gauntlet_Silver");
    CHECK(e->armor == "Armor_Adult_Zora");
    SUBCASE("taken off: the flag again") {
      m.put_u32(kUser + 0x20 + 0x18 * armor(worn::kArmorLower) + 0x10, 0xffffffffu);
      CHECK(reader.read_equipment()->boots == "");
    }
    SUBCASE("a piece that is gone is not worn") {
      m.put_u32(kPieces + 0x1000 * armor(worn::kArmorLower) + 0x2c, 9);
      CHECK(reader.read_equipment()->boots == "");
    }
    SUBCASE("a stale holder is not worn") {
      m.put_u32(kHolders + 0x100 * armor(worn::kArmorLower) + 0x48, 1);
      CHECK(reader.read_equipment()->boots == "");
    }
    SUBCASE("a name that is not text: the flag") {
      m.put_u8(kPieces + 0x1000 * armor(worn::kArmorLower) + 0x800 + 2, 0x07);
      CHECK(reader.read_equipment()->boots == "");
    }
  }
  SUBCASE("the head: the default head is no mask, a mask in link 0 or in the Helmet slot is") {
    put_piece(m, armor(worn::kArmorHead), "Armor_Child_Default_Head");
    CHECK(reader.read_equipment()->head == "");
    put_piece(m, worn::kHelmetSlot, "Armor_Child_Mask_Skull");
    CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Skull");
    put_piece(m, armor(worn::kArmorHead), "Armor_Child_Mask_Keaton");
    CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Keaton");
  }
  SUBCASE("something that is not armor in an armor link is not sent") {
    put_piece(m, armor(worn::kArmorLower), "Weapon_Sword_Kokiri");
    CHECK(reader.read_equipment()->boots == "");
  }
  SUBCASE("the player gone: the flags") {
    put_piece(m, armor(worn::kArmorLower), "Armor_Adult_HoverBoot");
    m.put_u32(l.actor1 + addr::kActorState, 7);
    CHECK(reader.read_equipment()->boots == "");
  }
}

TEST_CASE("worn armor: link names are checked as the hand slots are") {
  FakeMemory m;
  const Layout l = make_game(m);
  put_user(m, l);
  const std::uint64_t link = kUser + worn::kArmorLinks + worn::kLinkStride * worn::kArmorLower;
  CHECK(worn::link_name(m, link) == "");
  put_piece(m, armor(worn::kArmorLower), "Armor_Adult_HoverBoot");
  CHECK(worn::link_name(m, link) == "Armor_Adult_HoverBoot");
  CHECK(worn::armor_name(m, kUser, worn::kArmorLower) == "Armor_Adult_HoverBoot");
  CHECK_FALSE(worn::armor_name(m, kUser, worn::kArmorLinkCount));
  const std::string full(80, 'a');
  m.put(kPieces + 0x1000 * armor(worn::kArmorLower) + 0x800, full.data(), full.size());
  CHECK_FALSE(worn::link_name(m, link));  // no end within a name's length
  m.erase(link + 0x10, 4);
  CHECK_FALSE(worn::link_name(m, link));  // unreadable
  CHECK(worn::equipment_user(m, l.actor1) == kUser);
  CHECK_FALSE(worn::equipment_user(m, 0x12345000));
}
