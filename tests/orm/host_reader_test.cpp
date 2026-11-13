// ORM cases moved out of upstream's tests/host/reader_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "coop/host/orm_as_slots.hpp"
#include "coop/protocol/codec.hpp"
#include "../host/fake_memory.hpp"

using namespace coop::host;
using namespace coop::host::test;
namespace proto = coop::protocol;

namespace {

// A GameDataMgr with only the String64 table (docs/flags.md layout, type 16
// at M + 0x610: count, entries, hash slots, slot count; entries of 0x108
// with char* +0x68 and s32 size +0x70), offsets written out again here.
constexpr std::uint64_t kGameData = 0x90000000, kStrEntries = 0x90100000,
                        kStrSlots = 0x90200000, kStrText = 0x90300000;
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

// The player's EquipmentUser (component table +0x200): 8 slots of 0x18 at
// +0x20 (holder +8, id +0x10), a state byte per slot at +0x13bc.
constexpr std::uint64_t kComponents = 0x91000000, kUser = 0x91100000,
                        kEquipHolders = 0x91200000, kEquipActors = 0x91300000;

void put_user(FakeMemory& m, const Layout& l) {
  m.put_u64(l.actor1 + addr::kActorComponents, kComponents);
  m.put_u64(kComponents + 0x200, kUser);
  for (std::uint32_t i = 0; i < 8; ++i) {
    m.put_u64(kUser + 0x20 + 0x18 * i + 8, kEquipHolders + 0x100 * i);
    m.put_u32(kUser + 0x20 + 0x18 * i + 0x10, 0xffffffffu);
    m.put_u8(kUser + 0x13bc + i, 0);
  }
}

}  // namespace

TEST_CASE("host reader: the Lens of Truth, while it is on, is the head (ORM objetos-uso)") {
  FakeMemory m;
  const Layout l = make_game(m);
  PlayerReader reader(m);

  put_strings(m, {{addr::kEquipWeapon, "Weapon_Sword_Kokiri"},
                  {addr::kEquipHeadArmor, "Armor_Child_Mask_Keaton"}});
  put_user(m, l);
  // The armor links after the hand slots: all empty, then one holds the lens.
  for (std::uint32_t i = 0; i < addr::kArmorLinkCount; ++i) {
    m.put_u64(kUser + 0xe0 + 0x18 * i + 8, kEquipHolders + 0x100 * (8 + i));
    m.put_u32(kUser + 0xe0 + 0x18 * i + 0x10, 0xffffffffu);
  }
  CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Keaton");  // the flag
  const auto wear = [&](std::uint32_t link, std::uint32_t id, const std::string& name) {
    const std::uint64_t holder = kEquipHolders + 0x100 * (8 + link);
    const std::uint64_t piece = kEquipActors + 0x1000 * (8 + link);
    const std::uint64_t text = piece + 0x800;
    m.put_u32(kUser + 0xe0 + 0x18 * link + 0x10, id);
    m.put_u64(holder + 0x40, piece);
    m.put_u32(holder + 0x48, id);
    m.put_u32(piece + 0x2c, 4);
    m.put_u64(piece + 0x260, text);
    const std::string room(80, char{0});
    m.put(text, room.data(), room.size());
    m.put(text, name.c_str(), name.size() + 1);
    return piece;
  };
  wear(0, 70, "Armor_Child_Mask_Keaton");  // a mask in the head link is not the lens
  CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Keaton");
  const std::uint64_t lens = wear(5, 71, "Armor_Child_GlassTruth");
  CHECK(reader.read_equipment()->head == "Armor_Child_GlassTruth");
  CHECK(reader.read_equipment()->weapon == "Weapon_Sword_Kokiri");
  SUBCASE("put away: gone, or a stale holder, and the flag is the head again") {
    m.put_u32(lens + 0x2c, 9);
    CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Keaton");
    m.put_u32(lens + 0x2c, 4);
    m.put_u32(kEquipHolders + 0x100 * (8 + 5) + 0x48, 99);
    CHECK(reader.read_equipment()->head == "Armor_Child_Mask_Keaton");
  }
  SUBCASE("an unreadable link is skipped") {
    m.put_u32(kUser + 0xe0 + 0x18 * 2 + 0x10, 72);  // its holder points at nothing
    CHECK(reader.read_equipment()->head == "Armor_Child_GlassTruth");
  }
}

namespace {

// An event session at `session` playing `name` (event objects at 0x80f00000
// and on, the current one the second), as the game lays it out
// (addresses.hpp): the name buffer holds `capacity` bytes.
std::uint64_t put_event(FakeMemory& m, const Layout& l, const std::string& name, std::int32_t capacity = 128) {
  constexpr std::uint64_t kSession = 0x80e00000, kList = 0x80e10000, kEvent = 0x80f00100,
                          kText = 0x80f10000;
  m.put_u64(l.events + addr::kEventRunning, kSession);
  m.put_u64(kSession + addr::kSessionEvents, kList);
  m.put_u32(kSession + addr::kSessionCount, 2);
  m.put_u32(kSession + addr::kSessionCurrent, 1);
  m.put_u64(kList, 0x80f00000);
  m.put_u64(kList + 8, kEvent);
  m.put_u64(kEvent + addr::kEventNamePtr, kText);
  m.put_u32(kEvent + addr::kEventNameCapacity, static_cast<std::uint32_t>(capacity));
  std::string buffer = name;
  buffer.resize(static_cast<std::size_t>(std::max(capacity, 0)), '\0');
  m.put(kText, buffer.data(), buffer.size());
  return kEvent;
}

}  // namespace

TEST_CASE("host reader: the current event's name") {
  FakeMemory m;
  const Layout l = make_game(m);
  PlayerReader reader(m);
  SUBCASE("no event: nothing") {
    CHECK_FALSE(reader.read_current_event());
    CHECK_FALSE(reader.read_event_name());
  }
  SUBCASE("the current one of the session's list") {
    const auto event = put_event(m, l, "KokiriForest_Meet_DekuTree_MeetDekuTree_01");
    CHECK(reader.read_current_event() == event);
    CHECK(reader.read_event_name() == std::string("KokiriForest_Meet_DekuTree_MeetDekuTree_01"));
    CHECK(reader.read_event_running() == true);
  }
  SUBCASE("the index past the count: nothing") {
    put_event(m, l, "DmF_SY_GameOver");
    m.put_u32(0x80e00000 + addr::kSessionCurrent, 2);
    CHECK_FALSE(reader.read_current_event());
    CHECK_FALSE(reader.read_event_name());
  }
  SUBCASE("a name filling the buffer without its NUL: refused") {
    put_event(m, l, "Delayed_GameOver", 16);
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "Delayed_GameOver", 17);
    CHECK(reader.read_event_name() == std::string("Delayed_GameOver"));
  }
  SUBCASE("empty, unprintable, unreadable or a bad capacity: refused") {
    put_event(m, l, "");
    CHECK(reader.read_current_event());
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "Bad\x01Name");
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "Has space");
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "NPCTalk", 1);
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "NPCTalk", -5);
    CHECK_FALSE(reader.read_event_name());
    put_event(m, l, "NPCTalk");
    m.erase(0x80f10000 + 100, 1);  // a byte of the buffer unreadable
    CHECK_FALSE(reader.read_event_name());
  }
  SUBCASE("a long buffer is read no further than the longest name") {
    put_event(m, l, std::string(kMaxEventNameChars, 'a'), 4096);
    CHECK(reader.read_event_name() == std::string(kMaxEventNameChars, 'a'));
    put_event(m, l, std::string(kMaxEventNameChars + 1, 'a'), 4096);
    CHECK_FALSE(reader.read_event_name());
  }
}

TEST_CASE("host reader: every AS slot's command of our Link (ORM PvP shield)") {
  // The AS controller of actor1 with four slots: the legs' lock-on, an empty
  // slot, the upper body's guard pose, and a name that is no command.
  FakeMemory m;
  const Layout l = make_game(m);
  constexpr std::uint64_t components = 0x81100000, as = 0x81110000, inner = 0x81120000, controller = 0x81130000,
                          slots = 0x81140000, slot0 = 0x81150000, names = 0x81160000;
  m.put_u64(l.actor1 + addr::kActorComponents, components);
  m.put_u64(components + addr::kComponentsAs, as);
  m.put_u64(as + addr::kAsComponentInner, inner);
  m.put_u64(inner + addr::kAsInnerController, controller);
  m.put_u32(controller + addr::kAsSlotCount, 4);
  m.put_u64(controller + addr::kAsSlotArray, slots);
  const std::vector<std::string> text = {"LockOnWait", "", "Pose_Sword_ShieldGuard_U", std::string("Bad\x01Name")};
  for (std::size_t i = 0; i < text.size(); ++i) {
    const std::uint64_t slot = slot0 + 0x1000 * i;
    const std::uint64_t name = names + 0x100 * i;
    m.put_u64(slots + i * addr::kAsSlotStride, slot);
    m.put_u64(slot + addr::kAsSlotCommandName, name);
    std::string bytes = text[i];
    bytes.resize((bytes.size() + 16) / 16 * 16, '\0');
    m.put(name, bytes.data(), bytes.size());
  }
  auto got = orm::read_as_commands(m, l.actor1);
  REQUIRE(got.size() == 4);
  CHECK(got[0] == "LockOnWait");
  CHECK(got[1].empty());
  CHECK(got[2] == "Pose_Sword_ShieldGuard_U");
  CHECK(got[3].empty());  // not printable: no command
  // At most max_slots, and the count bounds it.
  CHECK(orm::read_as_commands(m, l.actor1, 2).size() == 2);
  m.put_u32(controller + addr::kAsSlotCount, 3);
  CHECK(orm::read_as_commands(m, l.actor1).size() == 3);
  // A slot whose pointer is null reads as empty, the others still read.
  m.put_u64(slots + 1 * addr::kAsSlotStride, 0);
  got = orm::read_as_commands(m, l.actor1);
  REQUIRE(got.size() == 3);
  CHECK(got[2] == "Pose_Sword_ShieldGuard_U");
  // No controller, a count of 0 or a negative one: nothing.
  m.put_u32(controller + addr::kAsSlotCount, 0);
  CHECK(orm::read_as_commands(m, l.actor1).empty());
  m.put_u32(controller + addr::kAsSlotCount, 0xffffffffu);
  CHECK(orm::read_as_commands(m, l.actor1).empty());
  CHECK(orm::read_as_commands(m, 0).empty());
  m.put_u64(l.actor1 + addr::kActorComponents, 0);
  CHECK(orm::read_as_commands(m, l.actor1).empty());
}
