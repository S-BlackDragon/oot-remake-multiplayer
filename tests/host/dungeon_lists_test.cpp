// Tests for the ORM per-block flag lists (coop/host/dungeon_lists.hpp) and the
// kit's shared_dungeon_child.txt (oot-remake docs/CONTENIDO_NINO.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "coop/host/dungeon_lists.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/towns.hpp"
#include "coop_chests.h"
#include "coop_dungeon.h"
#include "fake_memory.hpp"

namespace {

using coop::host::DungeonList;
using coop::host::FlagKey;
using coop::host::SharedFlags;
using coop::host::flag_hash;
namespace protocol = coop::protocol;

FlagKey named(std::string_view name) { return FlagKey(flag_hash(name), std::uint16_t{0}); }

DungeonList parsed(std::string_view text) {
  std::string error;
  auto list = DungeonList::parse(text, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

std::string error_of(std::string_view text) {
  std::string error;
  CHECK_FALSE(DungeonList::parse(text, &error));
  return error;
}

// A scratch folder of its own for each test that writes files.
struct Folder {
  explicit Folder(const std::string& name)
      : path(std::filesystem::temp_directory_path() / ("coop_tests_lists_" + name)) {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~Folder() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::string write(const std::string& name, const std::string& text) const {
    const auto p = path / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
  }
  std::filesystem::path path;
};

}  // namespace

TEST_CASE("dungeon lists: each kind of line") {
  const auto list = parsed(
      "# the child blocks\n"
      "\n"
      "KokiriForest_Npc_Kokiri_Mido_Child_Blocker   # shared live\n"
      "key64:0x05687269aff9adfa\n"
      "once:GoronCity_Candle01_ON\n"
      "hold:HavePlayedEvent.SacredForestMeadow_Gimmick_OpenShutter SacredForestMeadow\n"
      "unshare:DodongoCavern_Room10_PlayerOutOfRoom\n"
      "enum:Step_Meet_DekuTree NotReady Ready Complete\n");
  CHECK(list.share().size() == 2);
  CHECK(std::binary_search(list.share().begin(), list.share().end(),
                           named("KokiriForest_Npc_Kokiri_Mido_Child_Blocker")));
  REQUIRE(list.pickup_keys().size() == 1);
  CHECK(list.pickup_keys()[0] == FlagKey::key64_flag(0x05687269aff9adfaull));
  REQUIRE(list.once().size() == 1);
  CHECK(list.once()[0] == named("GoronCity_Candle01_ON"));
  REQUIRE(list.holds().size() == 1);
  CHECK(list.holds()[0].hash == flag_hash("HavePlayedEvent.SacredForestMeadow_Gimmick_OpenShutter"));
  CHECK(list.holds()[0].place == "SacredForestMeadow");
  REQUIRE(list.unshare().size() == 1);
  CHECK(list.unshare()[0] == named("DodongoCavern_Room10_PlayerOutOfRoom"));
  CHECK(list.share_enums().contains(flag_hash("Step_Meet_DekuTree")));
  CHECK_FALSE(list.empty());
  CHECK(DungeonList().empty());
  // A hash works as a name does.
  const auto by_hash = parsed("unshare:0x" + [] {
    char text[9];
    std::snprintf(text, sizeof text, "%08x", static_cast<unsigned>(flag_hash("DodongoCavern_Room10_PlayerOutOfRoom")));
    return std::string(text);
  }());
  CHECK(by_hash.unshare() == list.unshare());
}

TEST_CASE("dungeon lists: a flag has one kind, and bad lines are refused with their line") {
  CHECK(error_of("A\nunshare:A\n").find("line 2") != std::string::npos);
  CHECK(error_of("once:A\nhold:A KokiriForest\n").find("once:") != std::string::npos);
  CHECK(error_of("hold:A KokiriForest\nhold:A LostWood\n").find("two places") != std::string::npos);
  CHECK(error_of("hold:A\n").find("place") != std::string::npos);
  CHECK(error_of("hold:A B C\n").find("place") != std::string::npos);
  CHECK(error_of("once:\n").find("once:") != std::string::npos);
  CHECK(error_of("unshare:0x0\n").find("unshare:") != std::string::npos);
  CHECK(error_of("unshare:A B\n").find("one flag") != std::string::npos);
  CHECK(error_of("enum:Step_X A B\nunshare:Step_X\n").find("Enum") != std::string::npos);
  CHECK(error_of("unshare:Step_X\nenum:Step_X A B\n").find("unshare:") != std::string::npos);
  CHECK(error_of("key64:0xzz\n").find("line 1") != std::string::npos);
  // The same line twice is fine.
  const auto twice = parsed("once:A\nonce:A\nhold:B KokiriForest\nhold:B KokiriForest\nunshare:C\nunshare:C\n");
  CHECK(twice.once().size() == 1);
  CHECK(twice.holds().size() == 1);
  CHECK(twice.unshare().size() == 1);
}

TEST_CASE("dungeon lists: a hold: flag holds in its place, in either era, and nowhere else") {
  const auto list = parsed("hold:Gate SacredForestMeadow\nhold:Other KakarikoVillage_Child\n");
  const auto gate = flag_hash("Gate");
  CHECK(list.held_in(gate) == std::optional<std::string_view>("SacredForestMeadow"));
  CHECK(list.holds_in(gate, "SacredForestMeadow"));
  CHECK(list.holds_in(gate, "SacredForestMeadow_Child"));
  CHECK(list.holds_in(gate, "SacredForestMeadow_Adult"));
  CHECK_FALSE(list.holds_in(gate, "KokiriForest_Child"));
  CHECK_FALSE(list.holds_in(gate, ""));
  CHECK_FALSE(list.holds_in(flag_hash("Nothing"), "SacredForestMeadow"));
  // A place given with its era holds in that place's other era too (place_of).
  CHECK(list.holds_in(flag_hash("Other"), "KakarikoVillage_Adult"));
  CHECK_FALSE(list.held_in(flag_hash("Nothing")));
}

TEST_CASE("dungeon lists: a town's _Alternative scene holds what the town holds (PUEBLOS.md)") {
  // The romfs's second versions of adult Kakariko and Zora's Domain (escenas_banc.txt): before,
  // nothing was held there, not even the child list's Kakariko lines.
  const auto list = parsed(
      "hold:Gate KakarikoVillage\nhold:Ice ZoraDomain\nhold:Alt ZoraDomain_Adult_Alternative\n"
      "hold:Lake HyruleField\n");
  const auto gate = flag_hash("Gate");
  const auto ice = flag_hash("Ice");
  CHECK(list.holds_in(gate, "KakarikoVillage_Adult_Alternative"));
  CHECK(list.holds_in(gate, "KakarikoVillage_Adult"));
  CHECK(list.holds_in(gate, "KakarikoVillage_Child"));
  CHECK(list.holds_in(ice, "ZoraDomain_Adult_Alternative"));
  CHECK(list.holds_in(ice, "ZoraDomain_Child"));
  CHECK_FALSE(list.holds_in(ice, "KakarikoVillage_Adult_Alternative"));
  CHECK_FALSE(list.holds_in(ice, "ZoraRiver_Adult"));
  CHECK_FALSE(list.holds_in(flag_hash("Lake"), "ZoraDomain_Adult_Alternative"));
  // A place written as the alternative scene holds in the town's every version.
  const auto alt = flag_hash("Alt");
  CHECK(list.holds_in(alt, "ZoraDomain_Adult_Alternative"));
  CHECK(list.holds_in(alt, "ZoraDomain_Adult"));
  CHECK(list.holds_in(alt, "ZoraDomain_Child"));
  // The name rule itself: only a trailing _Alternative, then the era.
  CHECK(coop::host::town_place_of("KakarikoVillage_Adult_Alternative") == "KakarikoVillage");
  CHECK(coop::host::town_place_of("ZoraDomain_Adult_Alternative") == "ZoraDomain");
  CHECK(coop::host::town_place_of("ZoraDomain_Adult") == "ZoraDomain");
  CHECK(coop::host::town_place_of("ZoraDomain") == "ZoraDomain");
  CHECK(coop::host::town_place_of("_Alternative") == "_Alternative");
  CHECK(coop::host::town_place_of("") == "");
  CHECK(coop::host::town_place_of("Alternative_Adult") == "Alternative");
  // Drawing the players keeps the era-only rule (scene.hpp): this is the lists' rule.
  CHECK(coop::host::place_of("KakarikoVillage_Adult_Alternative") == "KakarikoVillage_Adult_Alternative");
}

TEST_CASE("dungeon lists: files and folders, merged in order, a disagreement refused") {
  Folder folder("load");
  const auto a = folder.write("shared_dungeon_a.txt", "once:A\nunshare:B\n");
  const auto b = folder.write("shared_dungeon_b.txt", "hold:C GoronCity\nunshare:B\n");
  folder.write("notes.txt", "this is no list\n");
  std::string error;

  const auto two = DungeonList::load_many(" " + a + " , ," + b + " ", &error);
  INFO(error);
  REQUIRE(two);
  CHECK(two->once().size() == 1);
  CHECK(two->holds().size() == 1);
  CHECK(two->unshare().size() == 1);  // both take B off: once

  // A folder: every shared_dungeon_*.txt in it, the other files left alone.
  const auto all = DungeonList::load_many(folder.path.string(), &error);
  REQUIRE(all);
  CHECK(all->once() == two->once());
  CHECK(all->holds() == two->holds());

  const auto bad = folder.write("shared_dungeon_c.txt", "B\n");  // shares what a.txt takes off
  CHECK_FALSE(DungeonList::load_many(a + "," + bad, &error));
  CHECK(error.find("shared_dungeon_c.txt") != std::string::npos);
  CHECK_FALSE(DungeonList::load_many(folder.path.string(), &error));
  CHECK_FALSE(DungeonList::load_many((folder.path / "missing.txt").string(), &error));
  CHECK(error.find("cannot open") != std::string::npos);
  const auto broken = folder.write("broken.txt", "hold:X\n");
  CHECK_FALSE(DungeonList::load_many(broken, &error));
  CHECK(error.find("line 1") != std::string::npos);
  const auto none = DungeonList::load_many(" , ", &error);
  REQUIRE(none);
  CHECK(none->empty());
}

TEST_CASE("dungeon lists: applied, they add and take off, and never turn sharing on") {
  const auto list = parsed("New\nonce:Done\nhold:Event KokiriForest\nunshare:Sensor\nkey64:0x77\nOld\n");
  std::optional<SharedFlags> shared = SharedFlags({named("Old"), named("Sensor"), named("Kept")});
  const auto r = coop::host::apply_dungeon_list(shared, list);
  REQUIRE(shared);
  CHECK(r.added == 4);  // New, Done, Event, the key
  CHECK(r.removed == 1);
  CHECK(r.once == 1);
  CHECK(r.held == 1);
  CHECK(r.pickups == 1);
  CHECK(shared->contains(named("New")));
  CHECK(shared->contains(named("Done")));
  CHECK(shared->contains(named("Event")));
  CHECK(shared->contains(FlagKey::key64_flag(0x77)));
  CHECK(shared->contains(named("Old")));
  CHECK(shared->contains(named("Kept")));
  CHECK_FALSE(shared->contains(named("Sensor")));

  std::optional<SharedFlags> off;
  const auto none = coop::host::apply_dungeon_list(off, list);
  CHECK_FALSE(off);
  CHECK(none.added == 0);
  CHECK(none.held == 1);  // still counted for the log
}

TEST_CASE("dungeon lists: an Enum flag the built-in lists hold keeps their rule, and the list's others still go (REVISION_HISTORIA H2)") {
  // The story list holds 0x10 as a step (only forward); a block list names it
  // again as a plain enum: line, and 0x20 (after it in hash order) too. The
  // first conflict stopped EnumFlags::merge: 0x20 was neither opted in nor
  // shared, and still counted as added.
  coop::host::EnumFlags steps;
  REQUIRE(steps.add(0x10, {1, 2}, true));
  std::optional<SharedFlags> shared = SharedFlags({named("Kept")}, steps);
  const auto list = parsed("enum:0x10 0x1 0x2\nenum:0x20 0x3 0x4\n");
  const auto r = coop::host::apply_dungeon_list(shared, list);
  REQUIRE(shared);
  CHECK(shared->enums().steps(0x10));  // the built-in rule wins
  CHECK(shared->enums().contains(0x20));
  CHECK(shared->contains(FlagKey(0x20, std::uint16_t{0})));
  CHECK(shared->contains(named("Kept")));
  CHECK(r.added == 1);  // 0x20 only
  CHECK(r.enum_conflicts == 1);

  // unshare: cannot take off an Enum flag a built-in list opts in: it stays,
  // and is not counted as taken off.
  const auto off = parsed("unshare:0x10\nunshare:Kept\n");
  const auto r2 = coop::host::apply_dungeon_list(shared, off);
  REQUIRE(shared);
  CHECK(shared->contains(FlagKey(0x10, std::uint16_t{0})));
  CHECK_FALSE(shared->contains(named("Kept")));
  CHECK(r2.removed == 1);
  CHECK(r2.enum_kept == 1);
}

namespace {

// A GameDataMgr with two Bools in fake memory, laid out as docs/flags.md
// says (the same layout tests/host/game_flags_test.cpp checks in full).
class Writable final : public coop::host::WritableGameMemory {
 public:
  std::uint64_t main_base() const override { return mem.main_base(); }
  bool read(std::uint64_t a, std::span<std::uint8_t> out) override { return mem.read(a, out); }
  bool write(std::uint64_t a, std::span<const std::uint8_t> data) override {
    std::vector<std::uint8_t> probe(data.size());
    if (!mem.read(a, probe)) return false;
    mem.put(a, data.data(), data.size());
    return true;
  }
  std::optional<std::uint32_t> compare_exchange_u32(std::uint64_t a, std::uint32_t expected,
                                                    std::uint32_t desired) override {
    const auto v = coop::host::read_u32(mem, a);
    if (v && *v == expected) mem.put_u32(a, desired);
    return v;
  }
  coop::host::test::FakeMemory mem;
};

constexpr std::uint64_t kMgr = 0x8000000000ull;
constexpr std::uint64_t kEntries = 0x8100000000ull, kSlots = 0x8110000000ull, kQueue = 0x8120000000ull;
constexpr std::uint32_t kCap = 7, kQueueCap = 4;

Writable game(std::uint32_t a, std::uint32_t b) {
  Writable w;
  auto& m = w.mem;
  auto zero = [&](std::uint64_t at, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) m.put_u8(at + i, 0);
  };
  m.put_u64(coop::host::test::FakeMemory::kBase + 0x4e842a8, kMgr);
  m.put_u32(kMgr + 0x10, 2);
  m.put_u64(kMgr + 0x18, kEntries);
  m.put_u64(kMgr + 0x20, kSlots);
  m.put_u32(kMgr + 0x2c, kCap);
  zero(kSlots, 8 * kCap);
  zero(kEntries, 0x18 * 2);
  std::uint32_t index = 0;
  for (const auto h : {a, b}) {
    std::uint32_t s = h % kCap;
    while (*coop::host::read_u32(m, kSlots + 8 * s) != 0) s = (s + 1) % kCap;
    m.put_u32(kSlots + 8 * s, h);
    m.put_u32(kSlots + 8 * s + 4, index++);
  }
  m.put_u32(kMgr + 0x30, kQueueCap);
  m.put_u64(kMgr + 0x38, kQueue);
  m.put_u32(kMgr + 0x40, 0x30000000);
  zero(kQueue, 8 * kQueueCap);
  return w;
}

}  // namespace

TEST_CASE("dungeon lists: a hold: flag waits in the game while our Link is in its area (fake memory)") {
  const auto list = parsed(
      "hold:HavePlayedEvent.SacredForestMeadow_Gimmick_OpenShutter SacredForestMeadow\n"
      "once:SacredForestMeadow_Field_WolfosDefeat_01\n");
  const auto gate = flag_hash("HavePlayedEvent.SacredForestMeadow_Gimmick_OpenShutter");
  const auto wolfos = flag_hash("SacredForestMeadow_Field_WolfosDefeat_01");
  auto w = game(gate, wolfos);
  coop::host::GameFlagStore store(w);
  std::string scene = "SacredForestMeadow_Child";
  // As coop_hook.cpp builds it: the list's hold first, then the dungeons'.
  coop::host::HoldFlagStore hold(store, [&](FlagKey key) {
    return key.kind == protocol::FlagKeyKind::named && key.index == 0 && list.holds_in(key.hash, scene);
  });
  const protocol::FlagEntry played{FlagKey(gate, 0), protocol::FlagKind::boolean, 1};
  const protocol::FlagEntry beaten{FlagKey(wolfos, 0), protocol::FlagKind::boolean, 1};

  // In the Meadow: the Wolfos come live (the gate's own graph plays the event
  // here); the played event from another game waits.
  CHECK(hold.write(beaten));
  CHECK(store.read(FlagKey(wolfos, 0))->value == 1);
  CHECK_FALSE(hold.write(played));
  CHECK(hold.held() == 1);
  CHECK(store.read(FlagKey(gate, 0))->value == 0);
  // The host's save load still writes a 0 through, never a held 1.
  CHECK_FALSE(hold.write_exact(played));
  CHECK(hold.write_exact({FlagKey(gate, 0), protocol::FlagKind::boolean, 0}));

  // Out of the Meadow (the field, or its other era's twin is still held):
  scene = "SacredForestMeadow_Adult";
  CHECK_FALSE(hold.write(played));
  scene = "LostWood_Child";
  CHECK(hold.write(played));
  CHECK(store.read(FlagKey(gate, 0))->value == 1);
}

TEST_CASE("dungeon lists: the kit's shared_dungeon_child.txt") {
  std::string error;
  const auto kit = DungeonList::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_dungeon_child.txt", &error);
  INFO(error);
  REQUIRE(kit);

  std::set<std::uint32_t> dungeon;
  for (const auto& f : CoopDungeon::Flags()) dungeon.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) dungeon.insert(f.hash);
  std::set<std::uint64_t> keys;
  for (const auto& k : CoopDungeon::Keys()) keys.insert(k.key);
  for (const auto& k : CoopChests::Keys()) keys.insert(k.key);

  // What it takes off is on the built-in list (else the line does nothing),
  // and is a sensor of one player or one game's Ruto, never a solve.
  CHECK(kit->unshare().size() == 13);
  for (const auto& k : kit->unshare()) CHECK(dungeon.contains(k.hash));
  CHECK_FALSE(std::binary_search(kit->unshare().begin(), kit->unshare().end(),
                                 named("InsideJabuJabuBelly_Room07_StepSwitch_Clear_OnlyRuto")));
  // What it adds is new: not on the built-in dungeon or chest lists.
  for (const auto& k : kit->share()) {
    if (k.kind == protocol::FlagKeyKind::key64) {
      CHECK_FALSE(keys.contains(k.key));
    } else {
      CHECK_FALSE(dungeon.contains(k.hash));
    }
  }
  for (const auto& k : kit->once()) CHECK_FALSE(dungeon.contains(k.hash));
  // The held events are of the overworld (the dungeons hold their own), and
  // each is a played event.
  CHECK(kit->holds().size() == 4);
  for (const auto& h : kit->holds()) {
    CHECK_FALSE(CoopDungeon::IsDungeonScene(h.place));
    CHECK(h.name.starts_with("HavePlayedEvent."));
    CHECK_FALSE(CoopDungeon::HeldIn(h.hash));
  }
  CHECK(kit->pickup_keys().size() >= 100);  // the child overworld's rocks, walls, covers, webs
  CHECK(kit->once().size() == 19);
  CHECK(kit->share_enums().empty());  // story steps are the story list's

  // On the built-in lists: adds every line, takes the 13 sensors off.
  std::optional<SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  const std::size_t before = shared->size();
  const auto r = coop::host::apply_dungeon_list(shared, *kit);
  CHECK(r.removed == 13);
  CHECK(r.added == kit->share().size() + kit->once().size() + kit->holds().size());
  CHECK(shared->size() == before + r.added - r.removed);
  CHECK_FALSE(shared->contains(named("DodongoCavern_Room10_PlayerOutOfRoom")));
  CHECK(shared->contains(named("DodongoCavern_BossGroundChange")) == false);  // issue 148, still out
  CHECK(shared->contains(named("InsideJabuJabuBelly_Room07_StepSwitch_Clear_OnlyRuto")));
  CHECK(shared->contains(named("KokiriForest_Npc_Kokiri_Mido_Child_Blocker")));
  CHECK(shared->contains(FlagKey::key64_flag(0x8a4e791b05223269ull)));  // the Death Mountain Trail boulder
}

TEST_CASE("dungeon lists: held: lines are taken off, and need their dungeon") {
  const auto list = parsed("held:FireTemple_Room10_StepSwitchOnLink FireTemple\n");
  REQUIRE(list.unshare().size() == 1);
  CHECK(list.unshare()[0] == named("FireTemple_Room10_StepSwitchOnLink"));
  CHECK(list.holds().empty());
  std::string error;
  CHECK_FALSE(DungeonList::parse("held:FireTemple_Room10_StepSwitchOnLink\n", &error));
  CHECK(error.find("line 1") != std::string::npos);
  // One kind per flag: held: and a shared line for the same flag disagree.
  CHECK_FALSE(DungeonList::parse("held:A Fire\nA\n", &error));
}

TEST_CASE("dungeon lists: the kit's shared_dungeon_adult.txt, with the child list as coop-options loads them") {
  const std::string dir = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";
  std::string error;
  const auto adult = DungeonList::load(dir + "shared_dungeon_adult.txt", &error);
  INFO(error);
  REQUIRE(adult);
  CHECK(adult->unshare().size() == 47 + 4);  // the sensors and the held switches
  CHECK(adult->share().size() == 31);        // the adult overworld (and the thieves' count)
  CHECK(adult->once().size() == 5);          // the guard's posts, Nabooru's Iron Knuckle (GERUDO_ESPIRITU.md)
  CHECK(adult->holds().empty());

  const auto child = DungeonList::load(dir + "shared_dungeon_child.txt", &error);
  REQUIRE(child);
  // coop-options.ini: LICHII_COOP_DUNGEON_LISTS=shared_dungeon_child.txt,shared_dungeon_adult.txt
  const auto both =
      DungeonList::load_many(dir + "shared_dungeon_child.txt, " + dir + "shared_dungeon_adult.txt", &error);
  REQUIRE(both);
  CHECK(both->unshare().size() == child->unshare().size() + adult->unshare().size());
  CHECK(both->holds().size() == child->holds().size());

  std::optional<SharedFlags> shared;
  CoopDungeon::Merge(shared, true);
  REQUIRE(shared);
  coop::host::apply_dungeon_list(shared, *both);
  CHECK_FALSE(shared->contains(named("FireTemple_Room10_StepSwitchOnLink")));
  CHECK_FALSE(shared->contains(named("ForestTemple_MegBattleStart")));
  CHECK_FALSE(shared->contains(named("DodongoCavern_Room10_PlayerOutOfRoom")));
  CHECK(shared->contains(named("Market_Child_TimeTempleDoor_Opened")));
  CHECK(shared->contains(named("KokiriForest_Npc_Kokiri_Mido_Child_Blocker")));
  CHECK(shared->contains(named("GerudoFortress_WarpPoint01_ClearEvent")));
  CHECK(shared->contains(named("Enemy_MiddleBoss_IronKnuckle_Nabooru_BossDefeat")));
  CHECK(shared->contains(named("Enemy_MiddleBoss_GerudoThief_DefeatNum")));
}

TEST_CASE("dungeon lists: toggle64: a two-way key, shared, never one-time, held in its place") {
  const auto list = parsed(
      "toggle64:0x03ecedcb30afe5e4 SpiritTemple  # room 15\n"
      "toggle64:0x03ECEDCB30AFE5E4 SpiritTemple\n"  // the same line twice is fine
      "key64:0x0dbe35edd0ba3c92\n");
  const FlagKey swap = FlagKey::key64_flag(0x03ecedcb30afe5e4ull);
  const FlagKey once = FlagKey::key64_flag(0x0dbe35edd0ba3c92ull);
  REQUIRE(list.toggles().size() == 1);
  CHECK(list.toggles()[0].place == "SpiritTemple");
  CHECK(std::binary_search(list.share().begin(), list.share().end(), swap));  // shared like a plain line
  REQUIRE(list.pickup_keys().size() == 1);  // only the key64 line is one-time
  CHECK(list.pickup_keys()[0] == once);
  CHECK(list.toggle_holds_in(swap.key, "SpiritTemple"));
  CHECK_FALSE(list.toggle_holds_in(swap.key, "DesertColossus_Adult"));
  CHECK_FALSE(list.toggle_holds_in(swap.key, ""));
  CHECK_FALSE(list.toggle_holds_in(once.key, "SpiritTemple"));  // a one-time key is never held so

  // Applied: the key is shared (both ways), and counted as a toggle.
  std::optional<SharedFlags> shared = SharedFlags{};
  const auto r = coop::host::apply_dungeon_list(shared, list);
  CHECK(r.toggles == 1);
  CHECK(r.pickups == 1);
  CHECK(shared->contains(swap));

  // A key is one-time or two-way, never both; one place; a key, not a name.
  CHECK(error_of("key64:0x03ecedcb30afe5e4\ntoggle64:0x03ecedcb30afe5e4 SpiritTemple\n").find("listed as") !=
        std::string::npos);
  CHECK(error_of("toggle64:0x03ecedcb30afe5e4 SpiritTemple\nkey64:0x03ecedcb30afe5e4\n").find("listed as") !=
        std::string::npos);
  CHECK(error_of("toggle64:0x1 SpiritTemple\ntoggle64:0x1 FireTemple\n").find("two places") != std::string::npos);
  CHECK(error_of("toggle64:SomeName SpiritTemple\n").find("toggle64:") != std::string::npos);
  CHECK(error_of("toggle64:0x0 SpiritTemple\n").find("toggle64:") != std::string::npos);
  CHECK(error_of("toggle64:0x12345678901234567 SpiritTemple\n").find("toggle64:") != std::string::npos);
  CHECK(error_of("toggle64:0x1\n").find("toggle64:") != std::string::npos);

  // Merged: a toggle in one list and the same key one-time in another is refused.
  DungeonList all = parsed("toggle64:0x1 FireTemple\n");
  std::string why;
  CHECK(all.merge(parsed("toggle64:0x1 FireTemple\n"), &why));
  CHECK_FALSE(all.merge(parsed("key64:0x1\n"), &why));
  CHECK(why.find("listed as") != std::string::npos);
  DungeonList other = parsed("key64:0x2\n");
  CHECK_FALSE(other.merge(parsed("toggle64:0x2 FireTemple\n"), &why));
  CHECK(all.toggles().size() == 1);
  CHECK(all.pickup_keys().empty());
}

TEST_CASE("dungeon lists: a toggle64 key from another game waits while our Link is in its place (fake memory)") {
  const auto list = parsed("toggle64:0x8ad9985355a1d827 IceCavern\n");
  const std::uint64_t k = 0x8ad9985355a1d827ull;
  std::string scene = "IceCavern";
  bool link = true;
  // As coop_hook.cpp's hold rule: a toggle64 key, with our Link read, in its place.
  const auto holds = [&](FlagKey key) {
    return link && key.kind == protocol::FlagKeyKind::key64 && key.table == 0 && list.toggle_holds_in(key.key, scene);
  };
  CHECK(holds(FlagKey::key64_flag(k)));
  CHECK_FALSE(holds(FlagKey::key64_flag(k, 1)));  // another table
  CHECK_FALSE(holds(FlagKey::key64_flag(k + 1)));
  scene = "ZorasFountain_Adult";
  CHECK_FALSE(holds(FlagKey::key64_flag(k)));  // out of the cavern: written at once (the room loads with it)
  scene = "IceCavern";
  link = false;
  CHECK_FALSE(holds(FlagKey::key64_flag(k)));  // loading: written, the room loads with it
}
