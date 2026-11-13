// Tests for the end of the game with up to six players (coop/host/final_rules.hpp,
// oot-remake docs/FINAL.md): counted Ints on a fake game's flag tables, the escape's clock, the
// ending's age, Twinrova's owner.
#include <doctest/doctest.h>

#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/final_rules.hpp"
#include "coop/host/game_flags.hpp"
#include "coop_final.h"         // the hook glue (lichii/hook, no emulator code)
#include "coop_objects_sync.h"  // our samples of Twinrova
#include "fake_memory.hpp"

using namespace coop::host;
using coop::host::test::FakeMemory;
namespace proto = coop::protocol;

namespace {

// A game's flag manager with a Bool table (M + 0x10, entries of 0x18, the value at +0x16) and
// an Int table (M + 0xd0, entries of 0x28, the value at +0x1c), each with the game's set queue
// (docs/flags.md; the offsets written out again, as game_flags_test.cpp does).
class Writable final : public WritableGameMemory {
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
    const auto v = read_u32(mem, a);
    if (v && *v == expected) mem.put_u32(a, desired);
    return v;
  }
  FakeMemory mem;
};

constexpr std::uint64_t kMgr = 0x8000000000ull;
constexpr std::uint64_t kBoolEntries = 0x8100000000ull, kBoolSlots = 0x8110000000ull,
                        kBoolQueue = 0x8120000000ull;
constexpr std::uint64_t kIntEntries = 0x8200000000ull, kIntSlots = 0x8210000000ull,
                        kIntQueue = 0x8220000000ull;
constexpr std::uint32_t kCap = 31, kQueueCap = 8;

void zero(FakeMemory& m, std::uint64_t a, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) m.put_u8(a + i, 0);
}

void table(FakeMemory& m, std::uint64_t base, std::uint64_t entries, std::uint64_t slots, std::uint64_t stride,
           const std::vector<std::uint32_t>& hashes) {
  m.put_u32(kMgr + base, static_cast<std::uint32_t>(hashes.size()));
  m.put_u64(kMgr + base + 8, entries);
  m.put_u64(kMgr + base + 0x10, slots);
  m.put_u32(kMgr + base + 0x1c, kCap);
  zero(m, slots, 8 * kCap);
  zero(m, entries, stride * hashes.size());
  std::uint32_t index = 0;
  for (const auto h : hashes) {
    std::uint32_t s = h % kCap;
    while (*read_u32(m, slots + 8 * s) != 0) s = (s + 1) % kCap;
    m.put_u32(slots + 8 * s, h);
    m.put_u32(slots + 8 * s + 4, index++);
  }
}

void queue(FakeMemory& m, std::uint64_t base, std::uint64_t buf, std::uint64_t slot) {
  m.put_u32(kMgr + base + 0x20, kQueueCap);
  m.put_u64(kMgr + base + 0x28, buf);
  m.put_u32(kMgr + base + 0x30, 0x30000000);  // round 3, nothing taken
  zero(m, buf, slot * kQueueCap);
}

constexpr const char* kTrials[] = {"Forest", "Fire", "Water", "Shadow", "Spirit", "Light"};
const std::string kCountName = "InsideGanonCastle_Dungeon_Barrier_Count";

std::uint32_t barrier(const char* trial) {
  return proto::flag_hash(std::string("InsideGanonCastle_Dungeon_BarrierRemove_") + trial);
}

// The castle as the game holds it: the six barriers (Bools, `down` of them set) and the count.
struct Castle {
  Writable w;
  std::vector<std::uint32_t> bools;
  explicit Castle(int down, std::int32_t count) {
    auto& m = w.mem;
    m.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
    for (const auto t : kTrials) bools.push_back(barrier(t));
    table(m, 0x10, kBoolEntries, kBoolSlots, 0x18, bools);
    queue(m, 0x10, kBoolQueue, 8);
    table(m, 0xd0, kIntEntries, kIntSlots, 0x28, {proto::flag_hash(kCountName)});
    queue(m, 0xd0, kIntQueue, 12);
    for (int i = 0; i < down; ++i) set_bool(i, true);
    m.put_u32(kIntEntries + 0x1c, static_cast<std::uint32_t>(count));
  }
  void set_bool(int index, bool on) { w.mem.put_u8(kBoolEntries + 0x18 * index + 0x16, on ? 1 : 0); }
  std::optional<std::int32_t> count() { return GameFlags(w).get_int(proto::flag_hash(kCountName)); }
};

CountList kit_counts() {
  std::string error;
  auto list = CountList::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_counts.txt", &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  return *list;
}

bool logged(const std::vector<std::string>& log, const std::string& part) {
  for (const auto& line : log) {
    if (line.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

// ---- Counted Ints --------------------------------------------------------------------------

TEST_CASE("final: the kit's shared_counts.txt counts the six barriers into the castle's count") {
  const CountList list = kit_counts();
  REQUIRE(list.entries().size() == 1);
  const auto& e = list.entries()[0];
  CHECK(e.name == kCountName);
  CHECK(e.hash == 0xa0df2650u);  // the built-in dungeon list's hash of it (lichii/hook/coop_dungeon.cpp)
  REQUIRE(e.of.size() == 6);
  // The same six hashes the built-in list shares (coop_dungeon.cpp).
  CHECK(e.of == std::vector<std::uint32_t>{0x6c7286eeu, 0x5be5ac4du, 0x0942ae9cu, 0xba2e06e0u, 0xbe1ba489u,
                                           0x444d4c74u});
  for (const auto t : kTrials) CHECK(std::find(e.of.begin(), e.of.end(), barrier(t)) != e.of.end());
  const auto keys = list.keys();
  CHECK(keys.size() == 7);
  CHECK(std::is_sorted(keys.begin(), keys.end()));
}

TEST_CASE("final: count lines parse, bad ones are refused with their number") {
  std::string error;
  const auto list = CountList::parse("# c\n\ncount:Some_Count A_Done B_Done 0x1234  # tail\r\ncount:Other X\n", &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  REQUIRE(list->entries().size() == 2);
  CHECK(list->entries()[0].of == std::vector<std::uint32_t>{proto::flag_hash("A_Done"), proto::flag_hash("B_Done"),
                                                            0x1234u});
  CHECK(list->entries()[0].of_names[2] == "0x1234");
  CHECK(list->entries()[1].hash == proto::flag_hash("Other"));

  CHECK_FALSE(CountList::parse("Some_Count A\n", &error));
  CHECK(error == "line 1: a line is count:IntFlag BoolFlag ...");
  CHECK_FALSE(CountList::parse("count:N\n", &error));
  CHECK(error == "line 1: N counts no Bool");
  CHECK_FALSE(CountList::parse("count:N A A\n", &error));
  CHECK(error == "line 1: A is listed twice on N's line");
  CHECK_FALSE(CountList::parse("count:N A\ncount:N B\n", &error));
  CHECK(error == "line 2: N is counted twice");
  CHECK_FALSE(CountList::parse("count:N A\ncount:M N\n", &error));
  CHECK(error == "line 2: N is counted and counts");
  CHECK_FALSE(CountList::parse("count:N A\ncount:A B\n", &error));
  CHECK(error == "line 2: A is counted and counts");
  CHECK_FALSE(CountList::parse("count:N N\n", &error));
  CHECK_FALSE(CountList::parse("count:N 0xzz\n", &error));
  std::string many = "count:N";
  for (int i = 0; i < 33; ++i) many += " B" + std::to_string(i);
  CHECK_FALSE(CountList::parse(many + "\n", &error));
  CHECK(error == "line 1: N counts more than 32 Bools");
  CHECK(CountList::parse("", &error)->empty());
  CHECK_FALSE(CountList::load("no/such/shared_counts.txt", &error));
}

TEST_CASE("final: two trials done at once in two games: the count is raised to the barriers down") {
  // Three barriers were down (count 3). A finished the Forest trial and B the Water one at the
  // same moment: each game counted 3 -> 4 and shared its 4 ("last write wins": one is lost).
  // Both barriers came as shared Bools: five set, the count at 4. Raised to 5, through the
  // game's queue.
  Castle game(5, 4);
  const CountList list = kit_counts();
  auto fixes = count_fixes(game.w, list);
  REQUIRE(fixes.size() == 1);
  CHECK(fixes[0].from == 4);
  CHECK(fixes[0].to == 5);
  const auto log = apply_counts(game.w, list);
  CHECK(logged(log, "count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)"));
  CHECK(read_u32(game.w.mem, kIntQueue) == 5u);  // queued as the game's setter does
  CHECK(game.count() == 5);
  // In step: nothing more.
  CHECK(count_fixes(game.w, list).empty());
  CHECK(apply_counts(game.w, list).empty());
  // The sixth: raised to 6, the tower's barrier can fall.
  game.set_bool(5, true);
  CHECK(logged(apply_counts(game.w, list), "count: InsideGanonCastle_Dungeon_Barrier_Count 5 -> 6 (6 of 6 set)"));
  CHECK(game.count() == 6);
}

TEST_CASE("final: a count is never lowered, nor touched while anything is unreadable") {
  const CountList list = kit_counts();
  Castle ahead(2, 3);  // the game counted more than the Bools say (its own way): left alone
  CHECK(count_fixes(ahead.w, list).empty());
  Castle loading(6, 0);
  loading.w.mem.erase(kBoolEntries + 0x18 * 3 + 0x16, 1);  // one barrier unreadable
  CHECK(count_fixes(loading.w, list).empty());
  Castle down(6, 0);
  down.w.mem.put_u64(FakeMemory::kBase + 0x4e842a8, 0);  // the manager not up
  CHECK(count_fixes(down.w, list).empty());
  CHECK(apply_counts(down.w, list).empty());
}

// ---- The escape's clock ----------------------------------------------------------------------

TEST_CASE("final: a game that saw the collapse later is set to the group's countdown (ZMP D-164)") {
  EscapeClock clock;
  EscapeClockInput in;
  in.now_us = 1'000'000;
  in.scene = std::string("CrumblingGanonCastle");
  in.server_now_ms = 100'000;
  in.seconds = 180;
  CHECK_FALSE(clock.update(in).set_seconds);  // no start known: nothing
  clock.started("CrumblingGanonCastle_Arrive", 70'000);  // the room's first: 30 s ago
  clock.started("CrumblingGanonCastle_Arrive", 95'000);  // a later one (ours, a relay): not the group's
  clock.started("EscapeFromCrumblingGanonCastle_2F_FallDown", 10'000);
  CHECK(clock.group_seconds(100'000) == 150u);
  auto a = clock.update(in);
  REQUIRE(a.set_seconds);
  CHECK(*a.set_seconds == 150u);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0] == "escape clock: ours shows 180 s, the group's 150 s (it started 30 s ago): set to 150 (ZMP D-164)");
  // Not seen yet: not again within 1 s; then again.
  in.now_us += 500'000;
  in.server_now_ms += 500;
  CHECK_FALSE(clock.update(in).set_seconds);
  in.now_us += 600'000;
  in.server_now_ms += 600;
  CHECK(clock.update(in).set_seconds == 149u);
  // Taken: in step, nothing.
  in.seconds = 149;
  in.now_us += 100'000;
  in.server_now_ms += 100;
  CHECK_FALSE(clock.update(in).set_seconds);
  // Within the slack (2 s), or behind the group: never more time.
  in.seconds = 151;
  CHECK_FALSE(clock.update(in).set_seconds);
  in.seconds = 120;
  CHECK_FALSE(clock.update(in).set_seconds);
}

// ORM (oot-remake docs/SONDAS.md, 2026-11-12): the search took a word that was not the countdown.
TEST_CASE("final: a word that never takes what is set is given up after three sets in a row") {
  EscapeClock clock;
  clock.started("CrumblingGanonCastle_Arrive", 70'000);
  EscapeClockInput in;
  in.now_us = 1'000'000;
  in.scene = std::string("CrumblingGanonCastle");
  in.server_now_ms = 100'000;
  in.seconds = 175;
  REQUIRE(clock.update(in).set_seconds);  // 1
  for (int i = 0; i < 2; ++i) {
    in.now_us += 1'100'000;
    in.server_now_ms += 1'100;
    REQUIRE(clock.update(in).set_seconds);  // 2, 3: not taken yet
  }
  in.now_us += 1'100'000;
  in.server_now_ms += 1'100;
  auto a = clock.update(in);
  CHECK_FALSE(a.set_seconds);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0].find("that word is not the countdown; nothing more is set this escape") != std::string::npos);
  in.now_us += 5'000'000;
  in.server_now_ms += 5'000;
  a = clock.update(in);
  CHECK_FALSE(a.set_seconds);
  CHECK(a.log.empty());
  // A new escape (after the group's ran out) tries again.
  clock.started("CrumblingGanonCastle_Arrive", in.server_now_ms + 200'000);
  in.server_now_ms += 210'000;
  in.now_us += 210'000'000;
  CHECK(clock.update(in).set_seconds);
}

// ORM (oot-remake docs/SONDAS.md round 7, sondas21r7): a display copy takes our write for a frame, then the game
// writes its own value again: each set read back once, so 'in step' reset the count and it was set 56 times.
TEST_CASE("final: a word that takes our set for a moment and goes back is given up after three bounces") {
  EscapeClock clock;
  clock.started("CrumblingGanonCastle_Arrive", 70'000);
  EscapeClockInput in;
  in.now_us = 1'000'000;
  in.scene = std::string("CrumblingGanonCastle");
  in.server_now_ms = 228'000;  // the group's 22 s
  std::vector<std::string> log;
  int sets = 0;
  for (int i = 0; i < 12; ++i) {
    in.seconds = 25;  // the game's own value
    auto a = clock.update(in);
    sets += a.set_seconds ? 1 : 0;
    log.insert(log.end(), a.log.begin(), a.log.end());
    in.now_us += 100'000;
    in.seconds = a.set_seconds ? *a.set_seconds : 25;  // our write, read back once
    clock.update(in);
    in.now_us += 150'000;
    in.server_now_ms += 250;
  }
  CHECK(sets == 3);  // the first set and two bounced ones; the third bounce gives up
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("that word is not the countdown") != std::string::npos);

  // A real countdown takes the set and stays in step: no give-up, the count starts afresh.
  EscapeClock real;
  real.started("CrumblingGanonCastle_Arrive", 70'000);
  in.now_us = 1'000'000;
  in.server_now_ms = 228'000;
  in.seconds = 25;
  auto first = real.update(in);
  REQUIRE(first.set_seconds);
  for (int i = 0; i < 20; ++i) {
    in.now_us += 250'000;
    in.server_now_ms += 250;
    in.seconds = *real.group_seconds(in.server_now_ms);
    auto a = real.update(in);
    CHECK_FALSE(a.set_seconds);
    CHECK(a.log.empty());
  }
  in.seconds = *real.group_seconds(in.server_now_ms) + 10;  // pushed off later (a pause): set again
  CHECK(real.update(in).set_seconds);
}

TEST_CASE("final: the escape's clock only in the escape, with a countdown read, and the server's clock") {
  EscapeClock clock;
  clock.started("CrumblingGanonCastle_Arrive", 4'294'960'000u);  // just before the server's clock wraps
  CHECK(clock.group_seconds(20'000) == 180u - 27u);             // 27.296 s later, across the wrap
  CHECK(clock.group_seconds(4'294'960'000u + 200'000u) == 0u);   // past the end: 0, the game's own
  EscapeClockInput in;
  in.now_us = 1;
  in.server_now_ms = 20'000;
  in.seconds = 179;
  in.scene = std::string("FinalBattleField");
  CHECK_FALSE(clock.update(in).set_seconds);
  in.scene.reset();
  CHECK_FALSE(clock.update(in).set_seconds);
  in.scene = std::string("CrumblingGanonCastle");
  in.seconds.reset();
  CHECK_FALSE(clock.update(in).set_seconds);
  in.seconds = 179;
  in.server_now_ms = 0;
  CHECK_FALSE(clock.update(in).set_seconds);
  in.server_now_ms = 20'000;
  CHECK(clock.update(in).set_seconds == 153u);
  clock.reset();
  CHECK_FALSE(clock.group_seconds(20'000));
}

TEST_CASE("final: the escape's clock takes the room's first start, a new one after it ran out, and never cuts a longer countdown") {
  EscapeClock clock;
  clock.started("CrumblingGanonCastle_Arrive", 95'000);  // ours first
  clock.started("CrumblingGanonCastle_Arrive", 70'000);  // A's, relayed after ours: earlier, the room's
  CHECK(clock.group_seconds(100'000) == 150u);
  // Ran out: 0, and nothing is set from here (the game's own end, RestartPos).
  EscapeClockInput in;
  in.now_us = 1;
  in.scene = std::string("CrumblingGanonCastle");
  in.seconds = 100;
  in.server_now_ms = 70'000 + 181'000;
  CHECK(clock.group_seconds(in.server_now_ms) == 0u);
  CHECK_FALSE(clock.update(in).set_seconds);
  // The escape again (after a game over): that start is the group's new one.
  clock.started("CrumblingGanonCastle_Arrive", 260'000);
  CHECK(clock.group_seconds(270'000) == 170u);
  // Our countdown longer than the whole length: the length is wrong, nothing is cut; said once.
  in.server_now_ms = 270'000;
  in.seconds = 240;
  in.now_us = 2;
  const auto a = clock.update(in);
  CHECK_FALSE(a.set_seconds);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0] == "escape clock: ours shows 240 s, more than the length 180 s: the length is not the game's; not set");
  CHECK(clock.update(in).log.empty());
  // With the game's length, it is set.
  EscapeClockSettings settings;
  settings.length_s = 240;
  EscapeClock longer(settings);
  longer.started("CrumblingGanonCastle_Arrive", 260'000);
  CHECK(longer.update(in).set_seconds == 230u);
}

TEST_CASE("final: the countdown word parses, bad ones are refused") {
  std::string error;
  const auto w = CountdownWord::parse("main+0x1000,0x10:f32", &error);
  REQUIRE_MESSAGE(w.has_value(), error);
  CHECK(w->base == 0x1000u);
  CHECK(w->offsets == std::vector<std::uint64_t>{0x10u});
  CHECK(w->unit == CountdownWord::Unit::f32_seconds);
  const auto u = CountdownWord::parse(" main+0x2A:u32 \n", &error);
  REQUIRE(u.has_value());
  CHECK(u->base == 0x2au);
  CHECK(u->offsets.empty());
  CHECK(u->unit == CountdownWord::Unit::u32_seconds);
  CHECK(CountdownWord::parse("main+0x2a,0x8,0x0:ms", &error)->offsets == std::vector<std::uint64_t>{8u, 0u});

  CHECK_FALSE(CountdownWord::parse("0x1000:f32", &error));
  CHECK(error == "a countdown word starts with main+0x<hex>");
  CHECK_FALSE(CountdownWord::parse("main+0x1000", &error));
  CHECK(error == "a countdown word is main+0x<hex>[,0x<hex>...]:f32|u32|ms|mmss");
  CHECK_FALSE(CountdownWord::parse("main+0x1000:s", &error));
  CHECK(error == "the unit is f32 (seconds), u32 (seconds), ms (milliseconds) or mmss (minutes and seconds)");
  CHECK_FALSE(CountdownWord::parse("main+1000:f32", &error));
  CHECK(error == "an offset is 0x and 1 to 16 hex digits");
  CHECK_FALSE(CountdownWord::parse("main+0x10,,0x8:f32", &error));
  CHECK_FALSE(CountdownWord::parse("main+0x:f32", &error));
  CHECK_FALSE(CountdownWord::parse("main+0x10,0xg:f32", &error));
  CHECK_FALSE(CountdownWord::parse("main+0x10,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1:f32", &error));
  CHECK(error == "at most 8 pointer steps");
  CHECK(CountdownWord::parse("main+0x10,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1:f32", &error).has_value());
}

TEST_CASE("final: the countdown word is read and written through its pointers (fake memory)") {
  Writable w;
  constexpr std::uint64_t kTimer = 0x9000000000ull;
  w.mem.put_u64(FakeMemory::kBase + 0x1000, kTimer);
  w.mem.put_f32(kTimer + 0x10, 180.75f);
  const auto word = *CountdownWord::parse("main+0x1000,0x10:f32");
  CHECK(word.address(w) == kTimer + 0x10);
  CHECK(word.read_seconds(w) == 180u);  // rounded down
  CHECK(word.write_seconds(w, 150));
  CHECK(*read_f32(w.mem, kTimer + 0x10) == 150.0f);
  CHECK(word.read_seconds(w) == 150u);
  CHECK_FALSE(word.write_seconds(w, CountdownWord::kMaxSeconds + 1));
  // Not a countdown: below 0, not finite, too long.
  w.mem.put_f32(kTimer + 0x10, -1.0f);
  CHECK_FALSE(word.read_seconds(w));
  w.mem.put_f32(kTimer + 0x10, std::numeric_limits<float>::quiet_NaN());
  CHECK_FALSE(word.read_seconds(w));
  w.mem.put_f32(kTimer + 0x10, 4000.0f);
  CHECK_FALSE(word.read_seconds(w));
  // A null pointer on the way (the timer not made yet): nothing, and nothing written.
  w.mem.put_u64(FakeMemory::kBase + 0x1000, 0);
  CHECK_FALSE(word.address(w));
  CHECK_FALSE(word.read_seconds(w));
  CHECK_FALSE(word.write_seconds(w, 10));
  // Milliseconds, and whole seconds, straight in main.
  w.mem.put_u32(FakeMemory::kBase + 0x2000, 149'500);
  const auto ms = *CountdownWord::parse("main+0x2000:ms");
  CHECK(ms.read_seconds(w) == 149u);
  CHECK(ms.write_seconds(w, 140));
  CHECK(read_u32(w.mem, FakeMemory::kBase + 0x2000) == 140'000u);
  w.mem.put_u32(FakeMemory::kBase + 0x3000, 90);
  const auto whole = *CountdownWord::parse("main+0x3000:u32");
  CHECK(whole.read_seconds(w) == 90u);
  CHECK(whole.write_seconds(w, 80));
  CHECK(read_u32(w.mem, FakeMemory::kBase + 0x3000) == 80u);
}

// ---- The ending holds the age ----------------------------------------------------------------

TEST_CASE("final: the ending's age change is not the room's (ZMP INVENTARIO G7)") {
  CHECK(ending_holds_age(std::string("InsideGanonCastle_Dungeon_BossDie_2"), std::nullopt));
  CHECK(ending_holds_age(std::string("InsideGanonCastle_Dungeon_StaffRoll_HyruleCastle"), std::nullopt));
  CHECK(ending_holds_age(std::string("Dm_StaffCredit_0005"), std::nullopt));
  CHECK(ending_holds_age(std::string("Dm_Ending_0000"), std::nullopt));
  CHECK(*ending_holds_age(std::nullopt, proto::flag_hash("Complete")) ==
        "Ganon is sealed (Step_Defeat_Ganon Complete)");
  // The Master Sword at the Temple of Time (D-R17's own change), the fight, the escape: not held.
  CHECK_FALSE(ending_holds_age(std::string("Market_Child_DoorOfTime_Draw"), proto::flag_hash("Step08")));
  CHECK_FALSE(ending_holds_age(std::string("InsideGanonCastle_Dungeon_Boss_GetMasterSword"), std::nullopt));
  CHECK_FALSE(ending_holds_age(std::nullopt, std::nullopt));
}

// ---- Twinrova's owner -------------------------------------------------------------------------

TEST_CASE("final: Twinrova's owner is kept while frozen, and for 'nearest' while it holds a part charge") {
  HandOverInput in;
  in.body = "Enemy_DungeonBoss_Twinrova";
  CHECK_FALSE(hand_over_holds(in));
  in.owner_frozen = true;  // Kotake's ice: its game must thaw it (D-231)
  CHECK(hand_over_holds(in));
  in.reason = HandOver::hit;
  CHECK(hand_over_holds(in));
  in.owner_up = false;  // a downed owner: handed over as before
  CHECK_FALSE(hand_over_holds(in));
  in.owner_up = true;
  in.owner_frozen = false;
  for (unsigned charge : {1u, 2u}) {
    in.owner_charge = charge;
    in.reason = HandOver::nearest;
    INFO(charge);
    CHECK(*hand_over_holds(in) == "Twinrova's owner holds a charge of " + std::to_string(charge) +
                                      " in the mirror shield: it is theirs to fire");
    in.reason = HandOver::hit;
    CHECK_FALSE(hand_over_holds(in));  // a hit on the boss still hands it over (issue 150)
  }
  in.reason = HandOver::nearest;
  in.owner_charge = 0;
  CHECK_FALSE(hand_over_holds(in));
  in.owner_charge = 3;  // full: fired at once
  CHECK_FALSE(hand_over_holds(in));
  // Koume and Kotake (the first half) are Twinrova; no other boss is held.
  in.owner_frozen = true;
  in.body = "Enemy_DungeonBoss_Kotake";
  CHECK(hand_over_holds(in));
  in.body = "Enemy_DungeonBoss_Ganondorf";
  CHECK_FALSE(hand_over_holds(in));
  in.body = "Enemy_KotakeKoumeBattleManager";  // the group's root while it is loaded
  CHECK(hand_over_holds(in));
}

TEST_CASE("final: owner_keep says both reasons at once, frozen_animation reads Link's ice") {
  HandOverInput in;
  in.body = "Enemy_DungeonBoss_Twinrova";
  CHECK(owner_keep(in) == OwnerKeep{});
  in.owner_frozen = true;
  in.reason = HandOver::nearest;  // not read
  const OwnerKeep frozen = owner_keep(in);
  CHECK(frozen.hit);
  CHECK(frozen.nearest);
  CHECK(frozen.why == "Twinrova's owner is frozen by Kotake's ice: its game thaws it (ZMP D-231)");
  in.owner_frozen = false;
  in.owner_charge = 2;
  in.reason = HandOver::hit;  // not read either
  const OwnerKeep charge = owner_keep(in);
  CHECK_FALSE(charge.hit);
  CHECK(charge.nearest);
  CHECK(charge.why == "Twinrova's owner holds a charge of 2 in the mirror shield: it is theirs to fire");
  in.owner_up = false;
  CHECK(owner_keep(in) == OwnerKeep{});

  CHECK(frozen_animation("Freeze"));
  CHECK(frozen_animation("Freeze_03"));
  CHECK(frozen_animation("FreezeBreak"));
  CHECK_FALSE(frozen_animation("Wait"));
  CHECK_FALSE(frozen_animation("Guard_Freeze"));
  CHECK_FALSE(frozen_animation(""));
}

TEST_CASE("final: charging the mirror shield keeps Twinrova's owner for a hit and for 'nearest' (owner_keeps)") {
  HandOverInput in;
  in.body = "Enemy_DungeonBoss_Koume";
  in.owner_charging = true;  // our Link holds a witch's blast on the shield
  for (HandOver reason : {HandOver::hit, HandOver::nearest}) {
    in.reason = reason;
    REQUIRE(hand_over_holds(in));
    CHECK(*hand_over_holds(in) == "Twinrova's owner is charging the mirror shield with a witch's blast: the "
                                  "charge is in its game, theirs to fire");
  }
  const OwnerKeep keep = owner_keep(in);
  CHECK(keep.hit);
  CHECK(keep.nearest);
  in.owner_frozen = true;  // the ice says it first
  CHECK(owner_keep(in).why == "Twinrova's owner is frozen by Kotake's ice: its game thaws it (ZMP D-231)");
  in.owner_frozen = false;
  in.owner_up = false;  // a downed owner holds nothing
  CHECK(owner_keep(in) == OwnerKeep{});
  in.owner_up = true;
  in.body = "Enemy_DungeonBoss_Ganondorf";  // no other boss
  CHECK(owner_keep(in) == OwnerKeep{});

  // The beam guards of Player_Adult/Child.root.asb: the AS commands and their animations.
  for (const char* name : {"GuardBeam", "GuardBeamHit", "Guard_Beam", "Guard_Beam_St", "Guard_Beam_St_UL",
                           "Guard_Beam_Hit", "Guard_Beam_Hit_DR"}) {
    INFO(name);
    CHECK(absorbing_animation(name));
  }
  for (const char* name : {"Guard", "GuardHit", "GuardBreak", "GuardNayru", "CrouchGuard",
                           "LockOn_Wait_ShieldGuard", "Freeze_03", "Wait", ""}) {
    INFO(name);
    CHECK_FALSE(absorbing_animation(name));
  }
}

TEST_CASE("final: our samples of Twinrova carry the keep bits, no other boss's") {
  // The numbers of the one table (orm_types.hpp, TIPOS_MENSAJE.md): ours count down from bit 7.
  CHECK(proto::object_state::owner_keeps == 0x80u);
  CHECK(proto::object_state::owner_keeps_near == 0x40u);
  CoopObjects::Sync sync;
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Twinrova") == 0);
  sync.SetKeep({true, true, std::nullopt});
  CHECK(sync.KeepBits("Enemy_KotakeKoumeBattleManager") == proto::object_state::owner_keeps);
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Kotake") == proto::object_state::owner_keeps);
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Ganon") == 0);
  sync.SetKeep({true, false, 1u});
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Koume") == proto::object_state::owner_keeps_near);
  sync.SetKeep({false, true, std::nullopt});  // downed: nothing kept
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Twinrova") == 0);
  // Charging the mirror shield: owner_keeps (bit 7), neither the hitter nor the nearer one.
  CoopObjects::Sync::Keep charging;
  charging.charging = true;
  sync.SetKeep(charging);
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Twinrova") == proto::object_state::owner_keeps);
  CHECK(sync.KeepBits("Enemy_KotakeKoumeBattleManager") == proto::object_state::owner_keeps);
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Bongo") == 0);
  charging.up = false;
  sync.SetKeep(charging);
  CHECK(sync.KeepBits("Enemy_DungeonBoss_Twinrova") == 0);
}

// ---- The hook glue (lichii/hook/coop_final.cpp) ----------------------------------------------

TEST_CASE("final glue: counted Ints join the shared flags and are raised every 0.5 s with Link in the game") {
  CoopFinal::Settings settings;
  settings.counts = COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_counts.txt";
  CoopFinal::Final glue(settings);
  std::optional<SharedFlags> none;
  CHECK(logged(glue.Begin(none), "coop: count: flags are not shared: no Int is counted"));
  CHECK_FALSE(glue.CountsOn());
  std::optional<SharedFlags> shared =
      SharedFlags(std::vector<FlagKey>{FlagKey{barrier("Forest"), std::uint16_t{0}}});
  const auto begun = glue.Begin(shared);
  CHECK(logged(begun, "coop: count: on (LICHII_ORM_COUNTS "));
  CHECK(logged(begun, "): 1 counted Ints, 6 flags added to the shared ones"));
  REQUIRE(glue.CountsOn());
  CHECK(shared->size() == 7);
  CHECK(shared->contains(FlagKey{proto::flag_hash(kCountName), std::uint16_t{0}}));

  Castle game(5, 4);
  CoopFinal::FrameIn in;
  in.now_us = 1'000'000;
  CHECK(glue.Frame(&game.w, in).log.empty());  // no Link (the title, a load): nothing
  in.link = true;
  CHECK(glue.Frame(nullptr, in).log.empty());  // no game
  CHECK(logged(glue.Frame(&game.w, in).log,
               "coop: count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)"));
  game.set_bool(5, true);
  in.now_us += 100'000;
  CHECK(glue.Frame(&game.w, in).log.empty());  // within 0.5 s
  in.now_us += 400'000;
  CHECK(logged(glue.Frame(&game.w, in).log,
               "coop: count: InsideGanonCastle_Dungeon_Barrier_Count 5 -> 6 (6 of 6 set)"));
  CHECK(game.count() == 6);

  CoopFinal::Settings missing;
  missing.counts = "no/such/shared_counts.txt";
  CoopFinal::Final off(missing);
  CHECK(logged(off.Begin(shared), "coop: count: LICHII_ORM_COUNTS cannot open "));
  CHECK_FALSE(off.CountsOn());
}

TEST_CASE("final glue: the escape's clock from a relayed start, written into the countdown word") {
  CoopFinal::Settings settings;
  settings.escape_clock = true;
  settings.clock_word = "main+0x1000,0x10:f32";
  CoopFinal::Final glue(settings);
  std::optional<SharedFlags> none;
  CHECK(logged(glue.Begin(none), "coop: escape clock: on (LICHII_ORM_ESCAPE_CLOCK): one countdown for the group in "
                                 "CrumblingGanonCastle, 180 s from the first CrumblingGanonCastle_Arrive, ours at "
                                 "main+0x1000,0x10:f32"));
  REQUIRE(glue.Word().has_value());
  Writable w;
  constexpr std::uint64_t kTimer = 0x9000000000ull;
  w.mem.put_u64(FakeMemory::kBase + 0x1000, kTimer);
  w.mem.put_f32(kTimer + 0x10, 180.0f);

  proto::StoryEvent other;  // another event: not the escape's
  other.player_number = 3;
  other.scene = "FinalBattleField";
  other.name = "InsideGanonCastle_Dungeon_BossDie_2";
  other.server_time_ms = 10'000;
  glue.Story(other);
  proto::StoryEvent m;
  m.player_number = 2;
  m.kind = proto::StoryKind::start;
  m.scene = "CrumblingGanonCastle";
  m.name = "CrumblingGanonCastle_Arrive";
  m.server_time_ms = 70'000;
  glue.Story(m);
  CoopFinal::FrameIn in;
  in.now_us = 5'000'000;
  in.link = true;
  in.scene = std::string("CrumblingGanonCastle");
  in.server_now_us = 100'000'000;
  auto out = glue.Frame(&w, in);
  CHECK(logged(out.log, "coop: escape clock: the group's countdown started at server time 70000 ms (P2's start)"));
  CHECK(logged(out.log, "coop: escape clock: ours shows 180 s, the group's 150 s (it started 30 s ago): set to 150 "
                        "(ZMP D-164)"));
  CHECK(*read_f32(w.mem, kTimer + 0x10) == 150.0f);
  // Our own start 2 s later is not the group's; our countdown is in step: nothing.
  in.event_name = std::string("CrumblingGanonCastle_Arrive");
  in.now_us += 2'000'000;
  in.server_now_us += 2'000'000;
  out = glue.Frame(&w, in);
  CHECK_FALSE(logged(out.log, "started at"));
  CHECK_FALSE(logged(out.log, "set to"));
  // Later our countdown runs slow (a pause in our game): set again.
  w.mem.put_f32(kTimer + 0x10, 179.0f);
  in.now_us += 2'000'000;
  in.server_now_us += 60'000'000;  // 92 s since the start: the group's 88
  out = glue.Frame(&w, in);
  CHECK(logged(out.log, "set to 88 (ZMP D-164)"));
  CHECK(*read_f32(w.mem, kTimer + 0x10) == 88.0f);
  // The pointer gone (the timer freed): nothing is read, so nothing is set.
  w.mem.put_u64(FakeMemory::kBase + 0x1000, 0);
  in.now_us += 2'000'000;
  CHECK(glue.Frame(&w, in).log.empty());  // nothing read, nothing to set
}

TEST_CASE("final glue: our own start is the group's when none came, without a word nothing is set") {
  CoopFinal::Settings settings;
  settings.escape_clock = true;
  settings.clock_length = "0";
  settings.clock_word = "nope";
  CoopFinal::Final bad(settings);
  std::optional<SharedFlags> none;
  const auto lines = bad.Begin(none);
  CHECK(logged(lines, "coop: escape clock: LICHII_ORM_ESCAPE_CLOCK_LENGTH_S 0 is not 1 to 3600: 180 s"));
  CHECK(logged(lines, "coop: escape clock: LICHII_ORM_ESCAPE_CLOCK_WORD nope: a countdown word is "
                      "main+0x<hex>[,0x<hex>...]:f32|u32|ms|mmss; nothing is set"));
  CHECK(logged(lines, "ours not read (no LICHII_ORM_ESCAPE_CLOCK_WORD)"));
  CHECK_FALSE(bad.Word());

  CoopFinal::FrameIn in;
  in.now_us = 1;
  in.link = true;
  in.scene = std::string("CrumblingGanonCastle");
  in.event_name = std::string("CrumblingGanonCastle_Arrive");
  in.server_now_us = 50'000'000;
  auto out = bad.Frame(nullptr, in);
  CHECK(logged(out.log, "coop: escape clock: the group's countdown started at server time 50000 ms (our game's start)"));
  CHECK(logged(out.log, "coop: escape clock: our countdown is not read (no LICHII_ORM_ESCAPE_CLOCK_WORD"));
  out = bad.Frame(nullptr, in);
  CHECK(out.log.empty());  // each said once

  // Not connected: no start of ours is taken.
  CoopFinal::Settings on;
  on.escape_clock = true;
  CoopFinal::Final fresh(on);
  fresh.Begin(none);
  in.server_now_us = 0;
  CHECK_FALSE(logged(fresh.Frame(nullptr, in).log, "started at"));
  // Off: nothing at all, a relayed start ignored.
  CoopFinal::Final off;
  off.Begin(none);
  CHECK_FALSE(off.ClockOn());
  proto::StoryEvent m;
  m.name = "CrumblingGanonCastle_Arrive";
  m.scene = "CrumblingGanonCastle";
  off.Story(m);
  in.server_now_us = 50'000'000;
  CHECK(off.Frame(nullptr, in).log.empty());
}

TEST_CASE("final glue: with no word given, the word our game's search found is taken") {
  CoopFinal::Settings settings;
  settings.escape_clock = true;
  CoopFinal::Final glue(settings);
  std::optional<SharedFlags> none;
  glue.Begin(none);
  Writable w;
  constexpr std::uint64_t kTimer = 0x9000000000ull;
  w.mem.put_u64(FakeMemory::kBase + 0x1000, kTimer);
  w.mem.put_f32(kTimer + 0x10, 180.0f);
  CoopFinal::FrameIn in;
  in.now_us = 1'000'000;
  in.link = true;
  in.scene = std::string("CrumblingGanonCastle");
  in.event_name = std::string("CrumblingGanonCastle_Arrive");
  in.server_now_us = 50'000'000;
  auto out = glue.Frame(&w, in);  // not found yet: the fallback says so
  CHECK(logged(out.log, "none found in our game yet"));
  CHECK_FALSE(glue.Word());
  // The search found it: taken, and the group's countdown is written into it.
  in.event_name.reset();
  in.found_word = std::string("main+0x1000,0x10:f32");
  in.now_us += 2'000'000;
  in.server_now_us += 40'000'000;  // 40 s since the start: the group's 140
  out = glue.Frame(&w, in);
  CHECK(logged(out.log, "coop: escape clock: ours at main+0x1000,0x10:f32 (found in our game"));
  REQUIRE(glue.Word().has_value());
  CHECK(logged(out.log, "set to 140"));
  CHECK(*read_f32(w.mem, kTimer + 0x10) == 140.0f);
  // Said once.
  in.now_us += 2'000'000;
  CHECK_FALSE(logged(glue.Frame(&w, in).log, "found in our game"));
  // A found word that does not parse is refused, once.
  CoopFinal::Final other(settings);
  other.Begin(none);
  in.found_word = std::string("nope");
  CHECK(logged(other.Frame(&w, in).log, "is no countdown word"));
  CHECK_FALSE(other.Word());
  CHECK_FALSE(logged(other.Frame(&w, in).log, "is no countdown word"));
  // A word given wins over a found one.
  CoopFinal::Settings given = settings;
  given.clock_word = "main+0x2000,0x10:f32";
  CoopFinal::Final fixed(given);
  fixed.Begin(none);
  in.found_word = std::string("main+0x1000,0x10:f32");
  CHECK_FALSE(logged(fixed.Frame(&w, in).log, "found in our game"));
}

TEST_CASE("final glue: Twinrova's owner keeps the boss while Kotake's ice holds our Link") {
  CoopFinal::Final glue;
  std::optional<SharedFlags> none;
  glue.Begin(none);
  CoopFinal::FrameIn in;
  in.link = true;
  in.scene = std::string("SpiritTemple");
  in.animation = std::string("Freeze_03");
  auto out = glue.Frame(nullptr, in);
  CHECK(out.keep == CoopFinal::KeepOut{true, true, std::nullopt});
  CHECK(logged(out.log, "coop: twinrova: while we own it, the boss stays with us (owner_keeps): Twinrova's owner "
                        "is frozen by Kotake's ice: its game thaws it (ZMP D-231)"));
  CHECK(glue.Frame(nullptr, in).log.empty());  // said once
  in.downed = DownedState::downed;  // a downed owner holds nothing
  out = glue.Frame(nullptr, in);
  CHECK_FALSE(out.keep.up);
  CHECK(logged(out.log, "coop: twinrova: our Link lets the boss go again"));
  in.downed = DownedState::alive;
  in.animation = std::string("Wait");
  out = glue.Frame(nullptr, in);
  CHECK(out.keep == CoopFinal::KeepOut{});
  CHECK(out.log.empty());
  // Ice elsewhere (the Ice Cavern) is no Twinrova's.
  in.scene = std::string("IceCavern");
  in.animation = std::string("Freeze");
  CHECK_FALSE(glue.Frame(nullptr, in).keep.frozen);
  // Off.
  CoopFinal::Settings settings;
  settings.twinrova = false;
  CoopFinal::Final off(settings);
  off.Begin(none);
  in.scene = std::string("SpiritTemple");
  out = off.Frame(nullptr, in);
  CHECK_FALSE(out.keep.frozen);
  CHECK(out.log.empty());
}

TEST_CASE("final glue: charging the mirror shield keeps Twinrova with us, with its check line") {
  CoopFinal::Final glue;
  std::optional<SharedFlags> none;
  glue.Begin(none);
  CoopFinal::FrameIn in;
  in.now_us = 1'000'000;
  in.link = true;
  in.scene = std::string("SpiritTemple");
  in.animation = std::string("Wait");
  CHECK(glue.Frame(nullptr, in).keep == CoopFinal::KeepOut{});
  // A witch's blast on the shield.
  in.now_us += 16'000;
  in.animation = std::string("GuardBeamHit");
  auto out = glue.Frame(nullptr, in);
  CHECK(out.keep.charging);
  CHECK_FALSE(out.keep.frozen);
  CHECK(out.keep.up);
  CHECK(logged(out.log, "coop: check: twinrova charge ok our Link's AS command GuardBeamHit in SpiritTemple "
                        "(absorbing a blast with the mirror shield): our samples of Twinrova carry owner_keeps"));
  CHECK(logged(out.log, "coop: twinrova: while we own it, the boss stays with us (owner_keeps): Twinrova's owner "
                        "is charging the mirror shield"));
  in.now_us += 16'000;
  in.animation = std::string("Guard_Beam_St");
  out = glue.Frame(nullptr, in);
  CHECK(out.keep.charging);
  CHECK(out.log.empty());  // the check line once, the reason unchanged
  // The blast fired back (or let go): the boss may go again.
  in.now_us += 16'000;
  in.animation = std::string("Wait");
  out = glue.Frame(nullptr, in);
  CHECK_FALSE(out.keep.charging);
  CHECK(logged(out.log, "coop: twinrova: our Link lets the boss go again"));
  in.now_us += 16'000;
  in.animation = std::string("GuardBeam");
  out = glue.Frame(nullptr, in);
  CHECK(out.keep.charging);
  CHECK_FALSE(logged(out.log, "coop: check:"));  // ok said once for the run
  // Downed mid-charge: nothing kept.
  in.downed = DownedState::downed;
  CHECK_FALSE(glue.Frame(nullptr, in).keep.up);
  in.downed = DownedState::alive;
  // A beam guard elsewhere is no Twinrova's.
  in.scene = std::string("ShadowTemple");
  CHECK_FALSE(glue.Frame(nullptr, in).keep.charging);

  // Our Link's AS command not read in the Spirit Temple for 2 s: the fallback, once.
  CoopFinal::Final blind;
  blind.Begin(none);
  in.scene = std::string("SpiritTemple");
  in.animation.reset();
  in.now_us = 5'000'000;
  CHECK_FALSE(logged(blind.Frame(nullptr, in).log, "coop: check:"));
  in.now_us = 6'900'000;
  CHECK_FALSE(logged(blind.Frame(nullptr, in).log, "coop: check:"));
  in.now_us = 7'000'000;
  out = blind.Frame(nullptr, in);
  CHECK(logged(out.log, "coop: check: twinrova charge fallback our Link's AS command is not read in SpiritTemple: "
                        "a charge of the mirror shield is not seen, the boss is kept only while Kotake's ice"));
  CHECK_FALSE(out.keep.charging);
  in.now_us = 20'000'000;
  CHECK_FALSE(logged(blind.Frame(nullptr, in).log, "coop: check:"));
  // A frame with the command read starts the 2 s over; no Link read (loading) is no fallback.
  CoopFinal::Final loading;
  loading.Begin(none);
  in.now_us = 1'000'000;
  loading.Frame(nullptr, in);
  in.animation = std::string("Wait");
  in.now_us = 2'000'000;
  loading.Frame(nullptr, in);
  in.animation.reset();
  in.now_us = 3'500'000;
  CHECK_FALSE(logged(loading.Frame(nullptr, in).log, "coop: check:"));
  in.link = false;
  in.now_us = 9'000'000;
  CHECK_FALSE(logged(loading.Frame(nullptr, in).log, "coop: check:"));
  // Off: no charge, no line.
  CoopFinal::Settings settings;
  settings.twinrova = false;
  CoopFinal::Final off(settings);
  off.Begin(none);
  in.link = true;
  in.animation = std::string("GuardBeam");
  out = off.Frame(nullptr, in);
  CHECK_FALSE(out.keep.charging);
  CHECK(out.log.empty());
}

// The minigame timer as minutes and seconds (two u32 words; docs/SONDAS.md row 1).
TEST_CASE("final: a minutes-and-seconds countdown word is read and written as one") {
  Writable w;
  constexpr std::uint64_t kTimer = 0x9000000000ull;
  w.mem.put_u64(FakeMemory::kBase + 0x4e7cfa8, kTimer);
  w.mem.put_u32(kTimer + 0x230, 2);
  w.mem.put_u32(kTimer + 0x234, 1);
  const auto word = CountdownWord::parse("main+0x4e7cfa8,0x230:mmss");
  REQUIRE(word.has_value());
  CHECK(word->unit == CountdownWord::Unit::u32_mmss);
  CHECK(word->read_seconds(w) == 121u);
  CHECK(word->write_seconds(w, 95));
  CHECK(*read_u32(w.mem, kTimer + 0x230) == 1u);
  CHECK(*read_u32(w.mem, kTimer + 0x234) == 35u);
  CHECK(word->read_seconds(w) == 95u);
  w.mem.put_u32(kTimer + 0x234, 60);  // not a seconds field
  CHECK_FALSE(word->read_seconds(w));
}
