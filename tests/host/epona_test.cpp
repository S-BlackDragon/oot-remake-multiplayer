// Tests for ORM D-R19 (coop/host/epona.hpp): the group's Epona as a step
// shared forward with only its last value, the race hold's test, the actors
// each game keeps, and the rider's ride and gait.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/epona.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/scene.hpp"
#include "fake_memory.hpp"

namespace {

namespace epona = coop::host::epona;
using coop::host::flag_hash;
using coop::host::test::FakeMemory;
namespace protocol = coop::protocol;

// A game with only the Enum table, holding Step_Get_Epona (docs/flags.md;
// the layout of game_flags_test.cpp's game_with_enums, written out again).
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
  FakeMemory mem;
};

constexpr std::uint64_t kMgr = 0x8000000000ull;
constexpr std::uint64_t kEntries = 0x8600000000ull, kSlots = 0x8610000000ull,
                        kQueue = 0x8620000000ull, kValues = 0x8630000000ull;
constexpr std::uint32_t kCap = 7, kQueueCap = 4;

void zero(FakeMemory& m, std::uint64_t a, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) m.put_u8(a + i, 0);
}

Writable game_at(std::uint32_t value) {
  Writable w;
  auto& m = w.mem;
  m.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
  // The Enum table at manager + 0x250: count, entries, slots, capacity.
  m.put_u32(kMgr + 0x250, 1);
  m.put_u64(kMgr + 0x250 + 8, kEntries);
  m.put_u64(kMgr + 0x250 + 0x10, kSlots);
  m.put_u32(kMgr + 0x250 + 0x1c, kCap);
  zero(m, kSlots, 8 * kCap);
  zero(m, kEntries, 0x38);
  m.put_u32(kSlots + 8 * (epona::kStep % kCap), epona::kStep);
  m.put_u32(kSlots + 8 * (epona::kStep % kCap) + 4, 0);
  // Its queue: 8-byte slots {value, hash}.
  m.put_u32(kMgr + 0x250 + 0x20, kQueueCap);
  m.put_u64(kMgr + 0x250 + 0x28, kQueue);
  m.put_u32(kMgr + 0x250 + 0x30, 0x30000000);
  zero(m, kQueue, 8 * kQueueCap);
  m.put_u32(kEntries + 0x18, value);
  m.put_u32(kEntries + 0x1c, value);
  m.put_u32(kEntries + 0x20, value);
  // The game's own value list in the entry (count at +0x28, array at +0x30).
  const auto& values = epona::step_values();
  m.put_u32(kEntries + 0x28, static_cast<std::uint32_t>(values.size()));
  m.put_u64(kEntries + 0x30, kValues);
  for (std::size_t i = 0; i < values.size(); ++i) m.put_u32(kValues + 4 * i, values[i].second);
  return w;
}

std::uint32_t value_of(const char* name) {
  for (const auto& [n, h] : epona::step_values()) {
    if (n == name) return h;
  }
  return 0;
}

}  // namespace

TEST_CASE("epona: the step's hashes are the game's names") {
  CHECK(epona::kStep == flag_hash("Step_Get_Epona"));
  CHECK(epona::kStep == 2453871701u);  // GameDataList's Hash
  CHECK(epona::kComplete == flag_hash("Complete"));
  CHECK(epona::kGambleRacePlaying == flag_hash("GambleRacePlaying"));
  CHECK(epona::kAfterWonGambleRace == flag_hash("AfterWonGambleRace"));
  CHECK(epona::kReadyForGambleRace == flag_hash("ReadyForGambleRace"));
  const auto& values = epona::step_values();
  REQUIRE(values.size() == 11);
  for (const auto& [name, hash] : values) {
    INFO(std::string(name));
    CHECK(hash == flag_hash(name));
    CHECK(epona::value_name(hash) == name);
  }
  CHECK(values.front().first == "NotReady");
  CHECK(values.back().first == "Complete");
  CHECK_FALSE(epona::value_name(0x1234));
}

TEST_CASE("epona: only Complete is shared, as a step") {
  const auto e = epona::shared_step();
  CHECK(e.size() == 1);
  CHECK(e.steps(epona::kStep));
  CHECK(e.allows(epona::kStep, epona::kComplete));
  for (const auto& [name, hash] : epona::step_values()) {
    if (name == "Complete") continue;
    INFO(std::string(name));
    CHECK_FALSE(e.allows(epona::kStep, hash));  // never sent: the race, the song, the escape
  }
  CHECK(epona::is_step(protocol::FlagKey(epona::kStep, std::uint16_t{0})));
  CHECK_FALSE(epona::is_step(protocol::FlagKey(epona::kStep, std::uint16_t{1})));
  CHECK_FALSE(epona::is_step(protocol::FlagKey(epona::kComplete, std::uint16_t{0})));
}

TEST_CASE("epona: the step joins the shared flags once, and another list's rule stays") {
  std::optional<coop::host::SharedFlags> none;
  CHECK(epona::add_shared_step(none) == epona::Added::no_sharing);
  CHECK_FALSE(none);

  std::optional<coop::host::SharedFlags> shared =
      coop::host::SharedFlags({protocol::FlagKey(0x11u, std::uint16_t{0})});
  REQUIRE(epona::add_shared_step(shared) == epona::Added::added);
  REQUIRE(shared);
  CHECK(shared->contains(protocol::FlagKey(epona::kStep, std::uint16_t{0})));
  CHECK(shared->contains(protocol::FlagKey(0x11u, std::uint16_t{0})));
  CHECK(shared->size() == 2);
  CHECK(shared->enums().steps(epona::kStep));
  CHECK(shared->enums().values(epona::kStep)->size() == 1);
  CHECK(epona::add_shared_step(shared) == epona::Added::already);
  CHECK(shared->size() == 2);

  // A list (a story line with the whole step) that opts it in first keeps its rule.
  coop::host::EnumFlags whole;
  std::vector<std::uint32_t> all;
  for (const auto& v : epona::step_values()) all.push_back(v.second);
  REQUIRE(whole.add(epona::kStep, all, true));
  std::optional<coop::host::SharedFlags> story = coop::host::SharedFlags({}, whole);
  CHECK(epona::add_shared_step(story) == epona::Added::already);
  CHECK(story->enums().values(epona::kStep)->size() == 11);
}

TEST_CASE("epona: Complete from elsewhere is written over any value, and nothing else is read") {
  const auto enums = epona::shared_step();
  const protocol::FlagKey key(epona::kStep, std::uint16_t{0});
  const protocol::FlagEntry complete{key, protocol::FlagKind::uint32, epona::kComplete};
  for (const char* before : {"NotReady", "AfterGetEponaSong", "ReadyForGambleRace", "AfterTalkMalon"}) {
    INFO(before);
    auto w = game_at(value_of(before));
    coop::host::GameFlagStore store(w, &enums);
    // Our own step is not one to share: unknown, so the watcher sends nothing.
    CHECK_FALSE(store.read(key));
    REQUIRE(store.write(complete));
    CHECK(epona::read_step(w) == epona::kComplete);
    CHECK(store.read(key) == complete);
  }
  // Already Complete: taken as done, nothing queued.
  auto w = game_at(epona::kComplete);
  coop::host::GameFlagStore store(w, &enums);
  CHECK(store.write(complete));
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30000000u);
  // A middle value from elsewhere is refused (never sent anyway).
  auto w2 = game_at(value_of("NotReady"));
  coop::host::GameFlagStore store2(w2, &enums);
  CHECK_FALSE(store2.write({key, protocol::FlagKind::uint32, epona::kGambleRacePlaying}));
  CHECK(epona::read_step(w2) == value_of("NotReady"));
}

TEST_CASE("epona: the race hold runs while the race or the escape does") {
  CHECK_FALSE(epona::race_running(std::nullopt));
  CHECK(epona::race_running(epona::kGambleRacePlaying));
  CHECK(epona::race_running(epona::kAfterWonGambleRace));
  CHECK_FALSE(epona::race_running(epona::kReadyForGambleRace));  // lost, or not started
  CHECK_FALSE(epona::race_running(epona::kComplete));
  CHECK_FALSE(epona::race_running(value_of("AfterTalkMalon")));
  auto w = game_at(epona::kGambleRacePlaying);
  CHECK(epona::race_running(epona::read_step(w)));
}

TEST_CASE("epona: horses, Ingo and the horseback archery are each game's own") {
  for (const char* own :
       {"Animal_Epona_Adult", "Animal_Epona_Child", "Animal_Horse", "Animal_Horse_For_Ingo",
        "Animal_Horse_Archery", "Animal_Horse_For_Event_Base", "Animal_Horse_For_Zelda",
        "Animal_Horse_Wagon", "Npc_Hylia_Ingo_001_Adult", "Npc_Hylia_Ingo_002_Adult",
        "Npc_Hylia_Ingo_003_Adult", "Npc_Gerudo_008", "Obj_Gerudo_HorsebackArcheryPot_A_01",
        "Obj_Gerudo_HorsebackArcheryTarget_A_02"}) {
    INFO(own);
    CHECK(epona::own_per_game(own));
  }
  for (const char* shared :
       {"Animal_Cucco", "Animal_Cow", "Animal_Fish_Bass", "Npc_Hylia_Ingo_001_Child", "Npc_Hylia_Malon_Adult",
        "Npc_Gerudo_007", "Npc_Gerudo_FortressGuard", "Obj_Pot_A_01", "Enemy_PhantomGanonHorse",
        "Land_Gerudo_HorsebackArcheryFence_A_01", "CoopPuppet_Adult", "Dm_Npc_Navi"}) {
    INFO(shared);
    CHECK_FALSE(epona::own_per_game(shared));
  }
}

TEST_CASE("epona: a rider's command says whether he rides, gets on or walks") {
  using epona::Ride;
  CHECK(epona::ride_of(std::nullopt) == Ride::none);
  CHECK(epona::ride_of(std::string_view("Wait")) == Ride::none);
  CHECK(epona::ride_of(std::string_view("HorseAccess")) == Ride::none);
  CHECK(epona::ride_of(std::string_view("DismountHorse")) == Ride::none);
  CHECK(epona::ride_of(std::string_view("DismounHorse")) == Ride::none);
  CHECK(epona::ride_of(std::string_view("HorseRideOn")) == Ride::mounting);
  CHECK(epona::ride_of(std::string_view("HorseRideOnL_F_Upslope")) == Ride::mounting);
  CHECK(epona::ride_of(std::string_view("HorseMove")) == Ride::riding);
  CHECK(epona::ride_of(std::string_view("HorseRide")) == Ride::riding);
  CHECK(epona::ride_of(std::string_view("HorseJumpGear4Big")) == Ride::riding);
  CHECK(epona::ride_of(std::string_view("Evt_Horse_Talk_N")) == Ride::riding);
  CHECK(epona::ride_of(std::string_view("AimHorseLowerF")) == Ride::riding);
  // Every command scene.hpp parks for is riding or getting on here, and the other way.
  for (const char* a : {"HorseMove", "HorseRideOn", "Evt_Horse_ShowItem_St", "DismountHorse", "HorseAccess",
                        "Walk", "OcarinaReadyStart"}) {
    INFO(a);
    const bool parked = coop::host::activity_of(std::string_view(a)) == coop::host::Activity::riding;
    CHECK(parked == (epona::ride_of(std::string_view(a)) != Ride::none));
  }
}

TEST_CASE("epona: the gait follows the speed, with a margin at each line") {
  using epona::Gait;
  const epona::GaitSpeeds s;
  CHECK(epona::gait_command(Gait::wait) == "Wait");
  CHECK(epona::gait_command(Gait::walk) == "Move_Gear_1");
  CHECK(epona::gait_command(Gait::trot) == "Move_Gear_2");
  CHECK(epona::gait_command(Gait::canter) == "Move_Gear_3");
  CHECK(epona::gait_command(Gait::gallop) == "Move_Gear_Top");
  CHECK(epona::gait_for(0.0f, Gait::wait, s) == Gait::wait);
  CHECK(epona::gait_for(2.0f, Gait::wait, s) == Gait::walk);
  CHECK(epona::gait_for(5.0f, Gait::wait, s) == Gait::trot);
  CHECK(epona::gait_for(20.0f, Gait::wait, s) == Gait::gallop);
  CHECK(epona::gait_for(20.0f, Gait::trot, s) == Gait::gallop);
  CHECK(epona::gait_for(0.0f, Gait::gallop, s) == Gait::wait);
  // On a line: no flip either way.
  CHECK(epona::gait_for(s.trot + 0.1f, Gait::walk, s) == Gait::walk);
  CHECK(epona::gait_for(s.trot - 0.1f, Gait::trot, s) == Gait::trot);
  CHECK(epona::gait_for(s.trot + 0.5f, Gait::walk, s) == Gait::trot);
  CHECK(epona::gait_for(s.trot - 0.5f, Gait::trot, s) == Gait::walk);
  CHECK(epona::gait_for(-1.0f, Gait::canter, s) == Gait::wait);
}

TEST_CASE("epona: Epona's look only for Epona (request), each horse its own") {
  CHECK(epona::horse_look(std::string_view("Animal_Epona_Adult")) == "CoopPuppet_Horse");
  CHECK(epona::horse_look(std::string_view("Animal_Horse_For_Ingo")) == "CoopPuppet_Horse_Ingo");
  CHECK(epona::horse_look(std::string_view("Animal_Horse")) == "CoopPuppet_Horse_Ranch");
  CHECK(epona::horse_look(std::string_view("Animal_Horse_Archery")) == "CoopPuppet_Horse_Ranch");
  for (const auto m : {std::optional<std::string_view>{}, std::optional<std::string_view>{""},
                       std::optional<std::string_view>{"Animal_Cow"}}) {
    CHECK(epona::horse_look(m) == "Animal_Horse_For_Event_Base");  // never Epona's
  }
  CHECK(epona::is_horse("Animal_Epona_Adult"));
  CHECK(epona::is_horse("Animal_Horse_For_Ingo"));
  CHECK_FALSE(epona::is_horse("CoopPuppet_Horse"));
  CHECK_FALSE(epona::is_horse("Enemy_PhantomGanonHorse"));
}
