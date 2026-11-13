// ORM "downed, not dead" (coop/host/downed.hpp): the readings over fake
// memory, the bottle pick, the local state machine and the mate's watch.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <limits>
#include <string>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/protocol/codec.hpp"
#include "fake_memory.hpp"

using namespace coop::host;
using coop::host::test::FakeMemory;
namespace proto = coop::protocol;

namespace {

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
    const auto v = read_u32(mem, a);  // the game's write queues (write_life through the queue)
    if (v && *v == expected) mem.put_u32(a, desired);
    return v;
  }
  FakeMemory mem;
};

// Link's components, written out again here (docs/objects.md).
constexpr std::uint64_t kActor = 0x90000000, kTable = 0x90010000, kLife = 0x90020000,
                        kLifeWord = 0x90030000, kMaxWord = 0x90040000, kDeath = 0x90050000;

void put_link(FakeMemory& m, std::uint32_t life, std::uint32_t max, std::uint32_t death) {
  m.put_u64(kActor + 0x270, kTable);
  m.put_u64(kTable + 0x2b8, kLife);
  m.put_u64(kTable + 0x1c0, kDeath);
  m.put_u64(kLife + 0x1c10, kLifeWord);
  m.put_u64(kLife + 0x1c18, kMaxWord);
  m.put_u32(kLifeWord + 8, life);
  m.put_u32(kMaxWord + 8, max);
  m.put_u32(kDeath + 0x20, death);
}

// The flag manager's Bool (M + 0x10), Int (M + 0xd0) and Enum (M + 0x250)
// tables with the five bottles' flags, offsets written out again.
constexpr std::uint64_t kMgr = 0x8000000000ull;
constexpr std::uint32_t kCap = 31;

struct TableAt {
  std::uint64_t base, entries, slots, stride, value;
};
constexpr TableAt kBoolT{0x10, 0x8100000000ull, 0x8110000000ull, 0x18, 0x16};
constexpr TableAt kIntT{0xd0, 0x8200000000ull, 0x8210000000ull, 0x28, 0x1c};
constexpr TableAt kEnumT{0x250, 0x8300000000ull, 0x8310000000ull, 0x38, 0x1c};

void table(FakeMemory& m, const TableAt& t, const std::vector<std::uint32_t>& hashes) {
  m.put_u32(kMgr + t.base, static_cast<std::uint32_t>(hashes.size()));
  m.put_u64(kMgr + t.base + 8, t.entries);
  m.put_u64(kMgr + t.base + 0x10, t.slots);
  m.put_u32(kMgr + t.base + 0x1c, kCap);
  for (std::uint32_t i = 0; i < kCap; ++i) m.put_u64(t.slots + 8 * i, 0);
  for (std::size_t i = 0; i < t.stride * hashes.size(); ++i) m.put_u8(t.entries + i, 0);
  std::uint32_t index = 0;
  for (const auto h : hashes) {
    std::uint32_t s = h % kCap;
    while (*read_u32(m, t.slots + 8 * s) != 0) s = (s + 1) % kCap;
    m.put_u32(t.slots + 8 * s, h);
    m.put_u32(t.slots + 8 * s + 4, index++);
  }
}

// Every bottle not owned and Empty.
Writable game() {
  Writable w;
  w.mem.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
  std::vector<std::uint32_t> items, valids, stocks;
  for (int s = 0; s < kBottleCount; ++s) {
    items.push_back(bottle_keys(s).item);
    valids.push_back(bottle_keys(s).valid);
    stocks.push_back(bottle_keys(s).stock);
  }
  table(w.mem, kBoolT, valids);
  table(w.mem, kIntT, stocks);
  table(w.mem, kEnumT, items);
  for (int s = 0; s < kBottleCount; ++s) {
    w.mem.put_u32(kEnumT.entries + kEnumT.stride * s + kEnumT.value, bottle_value("Empty"));
  }
  return w;
}

const std::uint32_t kFairy = bottle_value("Fairy");
const std::uint32_t kEmpty = bottle_value("Empty");

Bottles none_owned() {
  Bottles b;
  for (auto& x : b) x = Bottle{kEmpty, false, 0};
  return b;
}

Vitals alive(std::int32_t life = 12) { return {life, 12, 0x43a0}; }
Vitals dead() { return {0, 12, 0x43af}; }

DownedInput input(std::uint64_t now_ms, Vitals v, std::optional<bool> event = false,
                  Bottles bottles = none_owned()) {
  DownedInput in;
  in.now_us = now_ms * 1000;
  in.vitals = v;
  in.event_running = event;
  in.bottles = bottles;
  return in;
}

bool logged(const DownedActions& a, const std::string& part) {
  for (const auto& l : a.log) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("downed: the death word reads as the game's QueryDeathIsDead") {
  CHECK(death_word_dead(0x43af));
  CHECK_FALSE(death_word_dead(0x43a0));  // seen alive after the fairy
  CHECK_FALSE(death_word_dead(0));
  CHECK_FALSE(death_word_dead(0x2002));  // (w & 0x6000) == 0x2000
  CHECK(death_word_dead(0x4002));
}

TEST_CASE("downed: Link's vitals are read through his components") {
  FakeMemory m;
  put_link(m, 0, 12, 0x43af);
  Vitals v = read_vitals(m, kActor);
  CHECK(v.life == 0);
  CHECK(v.max_life == 12);
  CHECK(v.death_word == 0x43afu);
  CHECK(v.dead());
  CHECK_FALSE(v.up());

  put_link(m, 12, 12, 0x43a0);
  v = read_vitals(m, kActor);
  CHECK(v.up());
  CHECK_FALSE(v.dead());

  put_link(m, 13, 12, 0x43a0);  // above the max: not a life word
  v = read_vitals(m, kActor);
  CHECK_FALSE(v.life);
  CHECK(v.max_life == 12);
  CHECK_FALSE(v.up());

  m.erase(kTable + 0x1c0, 8);  // no Death component: neither dead nor up
  put_link(m, 4, 12, 0);
  m.erase(kTable + 0x1c0, 8);
  v = read_vitals(m, kActor);
  CHECK_FALSE(v.death_word);
  CHECK_FALSE(v.dead());
  CHECK_FALSE(v.up());
  CHECK_FALSE(read_vitals(m, 0x1234).life);
}

TEST_CASE("downed: bottles are read and written in their live values") {
  CHECK(bottle_keys(0).item == 0x7d48df76u);  // docs/items.md
  CHECK(bottle_keys(0).valid == 0xf39ce410u);
  CHECK(bottle_keys(4).item == 0x365e1953u);
  CHECK(bottle_keys(0).stock == 0xd53ce3a7u);
  CHECK(kEmpty == 0xbe69de49u);  // checked in a run

  Writable w = game();
  Bottles b = read_bottles(w);
  for (const auto& x : b) {
    REQUIRE(x);
    CHECK(*x == Bottle{kEmpty, false, 0});
  }
  REQUIRE(write_bottle(w, 2, Bottle{kFairy, true, 1}));
  b = read_bottles(w);
  CHECK(*b[2] == Bottle{kFairy, true, 1});
  CHECK(*b[1] == Bottle{kEmpty, false, 0});
  // The Enum entry: 0x38 bytes, the value at +0x1c.
  CHECK(read_u32(w.mem, kEnumT.entries + 2 * 0x38 + 0x1c) == kFairy);
  CHECK(read_u8(w.mem, kBoolT.entries + 2 * 0x18 + 0x16) == 1);
  CHECK(read_u32(w.mem, kIntT.entries + 2 * 0x28 + 0x1c) == 1u);
  CHECK_FALSE(write_bottle(w, 5, Bottle{}));

  Writable bare;  // no flag manager
  CHECK_FALSE(read_bottles(bare)[0]);
  CHECK_FALSE(write_bottle(bare, 0, Bottle{}));
}

TEST_CASE("downed: the revive's bottle: own fairy, then an empty one, then one not owned") {
  Bottles b = none_owned();
  auto p = pick_bottle(b);
  REQUIRE(p);
  CHECK(p->slot == 0);
  CHECK(p->synthetic);
  CHECK_FALSE(p->own_fairy);

  b[0] = Bottle{bottle_value("RedPotion"), true, 1};  // full: never used
  b[3] = Bottle{kEmpty, true, 1};
  p = pick_bottle(b);
  REQUIRE(p);
  CHECK(p->slot == 3);
  CHECK_FALSE(p->synthetic);

  b[4] = Bottle{kFairy, true, 1};
  p = pick_bottle(b);
  REQUIRE(p);
  CHECK(p->slot == 4);
  CHECK(p->own_fairy);

  for (auto& x : b) x = Bottle{bottle_value("Bug"), true, 1};
  CHECK_FALSE(pick_bottle(b));
  b[1].reset();  // unreadable is never picked
  CHECK_FALSE(pick_bottle(b));
}

TEST_CASE("downed: dead with the gate shut, revived through a faked bottle, put back") {
  Downed d;
  auto a = d.tick(input(0, alive()));
  CHECK(d.state() == DownedState::alive);
  CHECK(a.gate_closed);  // shut before any death
  CHECK(a.write_gate);
  CHECK_FALSE(d.dead_status());

  d.request_revive(2);  // not down: dropped
  a = d.tick(input(16, dead()));
  CHECK(d.state() == DownedState::downed);
  CHECK(logged(a, "alive -> downed"));
  CHECK(a.gate_closed);
  CHECK(d.dead_status());
  CHECK(d.wants_bottles());
  a = d.tick(input(1000, dead()));
  CHECK(d.state() == DownedState::downed);  // nobody asked yet
  CHECK(a.bottles.empty());

  d.request_revive(2);
  a = d.tick(input(2000, dead()));
  CHECK(d.state() == DownedState::reviving);
  CHECK(logged(a, "downed -> reviving: revive requested by P2"));
  CHECK(logged(a, "fairy in bottle A (a bottle not owned"));
  REQUIRE(a.bottles.size() == 1);
  CHECK(a.bottles[0] == BottleWrite{0, Bottle{kFairy, true, 1}});
  REQUIRE(a.quiet);  // IsValid and StockNum are shared: quiet while faked
  CHECK(*a.quiet == std::set<FlagKey>{FlagKey{bottle_keys(0).valid, 0},
                                      FlagKey{bottle_keys(0).stock, 0}});
  CHECK(a.gate_closed);  // opened once the fairy is seen there
  CHECK(d.dead_status());

  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  a = d.tick(input(2016, dead(), false, with));
  CHECK_FALSE(a.gate_closed);
  CHECK(logged(a, "gate open"));
  a = d.tick(input(2100, dead(), true, with));
  CHECK(logged(a, "game over event running"));
  CHECK_FALSE(a.gate_closed);
  with[0] = Bottle{kEmpty, true, 1};  // the event uses the fairy up
  a = d.tick(input(6700, alive(), true, with));  // up, the event still on
  CHECK(d.state() == DownedState::reviving);
  CHECK_FALSE(a.gate_closed);
  a = d.tick(input(8500, alive(), false, with));
  CHECK(d.state() == DownedState::alive);
  CHECK(logged(a, "reviving -> alive: revived (by P2, life 12 of 12"));
  CHECK(a.gate_closed);
  REQUIRE(a.bottles.size() == 1);
  CHECK(a.bottles[0] == BottleWrite{0, Bottle{kEmpty, false, 0}});  // all put back
  REQUIRE(a.quiet);
  CHECK(a.quiet->empty());
  CHECK_FALSE(d.dead_status());
}

TEST_CASE("downed: an empty bottle of our own is used without faking ownership") {
  Downed d;
  Bottles b = none_owned();
  b[1] = Bottle{kEmpty, true, 1};
  d.tick(input(0, dead(), false, b));
  d.request_revive(3);
  auto a = d.tick(input(100, dead(), false, b));
  REQUIRE(a.bottles.size() == 1);
  CHECK(a.bottles[0] == BottleWrite{1, Bottle{kFairy, true, 1}});
  CHECK_FALSE(a.quiet);
}

TEST_CASE("downed: our own fairy gets us up without anyone, and nothing is written") {
  Downed d;
  Bottles b = none_owned();
  b[2] = Bottle{kFairy, true, 1};
  d.tick(input(0, dead(), false, Bottles{}));  // the bottles unreadable as he died: down
  REQUIRE(d.state() == DownedState::downed);
  auto a = d.tick(input(16, dead(), false, b));
  CHECK(d.state() == DownedState::reviving);
  CHECK(logged(a, "a fairy in our own bottle"));
  CHECK(a.bottles.empty());
  a = d.tick(input(32, dead(), false, b));
  CHECK_FALSE(a.gate_closed);
  d.tick(input(100, dead(), true, b));
  b[2] = Bottle{kEmpty, true, 1};
  a = d.tick(input(7000, alive(), false, b));
  CHECK(d.state() == DownedState::alive);
  CHECK(logged(a, "our own fairy"));
  CHECK(a.bottles.empty());  // a real fairy used up stays used up
}

// ---- D-R17: our own bottled fairy (oot-64 PLAN 2.8 point 1) ------------------

TEST_CASE("downed: dying with a fairy of our own opens the gate at once and never goes down") {
  Downed d;
  Bottles b = none_owned();
  b[2] = Bottle{kFairy, true, 1};
  CHECK_FALSE(d.wants_bottles(alive()));  // standing: no bottle reads
  CHECK(d.wants_bottles(dead()));         // the frame he dies: read them
  auto a = d.tick(input(0, alive(), false, b));
  CHECK(a.gate_closed);
  a = d.tick(input(16, dead(), false, b));
  CHECK(d.state() == DownedState::fairy);
  CHECK(logged(a, "alive -> fairy: a fairy in our own bottle C"));
  CHECK_FALSE(a.gate_closed);  // the game over event and its fairy branch may run
  CHECK(a.write_gate);
  CHECK(a.bottles.empty());  // the real fairy is the game's to use
  CHECK_FALSE(a.quiet);
  CHECK_FALSE(d.dead_status());  // not down: no PLAYER_STATUS bit 4
  CHECK_FALSE(d.wants_bottles());
  d.request_revive(2);                   // not down: dropped
  CHECK_FALSE(d.group_defeat(2));        // not down: not ours
  a = d.tick(input(100, dead(), true, b));
  CHECK(logged(a, "game over event running 0.08 s after the death"));
  CHECK_FALSE(a.gate_closed);
  a = d.tick(input(4700, alive(), true, b));  // up, the event still on
  CHECK(d.state() == DownedState::fairy);
  CHECK_FALSE(a.gate_closed);
  a = d.tick(input(6500, alive(), false, b));
  CHECK(d.state() == DownedState::alive);
  CHECK(logged(a, "fairy -> alive: up by our own fairy (life 12 of 12, 6.48 s after the death)"));
  CHECK(a.gate_closed);  // a mate in the room: shut again
  CHECK(a.bottles.empty());
  CHECK_FALSE(d.dead_status());
  // The next death with no fairy left is a plain downed one.
  b[2] = Bottle{kEmpty, true, 1};
  a = d.tick(input(9000, dead(), false, b));
  CHECK(d.state() == DownedState::downed);
  CHECK(a.gate_closed);
}

TEST_CASE("downed: a fairy not ours, or alone in the room, takes the usual ways") {
  Bottles b = none_owned();
  b[0] = Bottle{kFairy, false, 0};  // not owned (a test write, a faked one): not ours
  Downed d;
  d.tick(input(0, alive(), false, b));
  auto a = d.tick(input(16, dead(), false, b));
  CHECK(d.state() == DownedState::downed);
  CHECK(a.gate_closed);

  b[0] = Bottle{kFairy, true, 1};
  Downed solo;
  DownedInput in = input(0, alive(), false, b);
  in.mates = 0;
  solo.tick(in);
  in = input(16, dead(), false, b);
  in.mates = 0;
  a = solo.tick(in);  // alone the gate is open anyway: the game's own death, fairy and all
  CHECK(solo.state() == DownedState::game_over);
  CHECK_FALSE(a.gate_closed);
}

TEST_CASE("downed: our own fairy that never gets us up leaves the game over running") {
  Downed d;
  Bottles b = none_owned();
  b[4] = Bottle{kFairy, true, 1};
  d.tick(input(0, dead(), false, b));
  REQUIRE(d.state() == DownedState::fairy);
  d.tick(input(100, dead(), true, b));
  auto a = d.tick(input(19000, dead(), true, b));
  CHECK(d.state() == DownedState::fairy);
  a = d.tick(input(20200, dead(), true, b));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "our own fairy did not get us up"));
  CHECK_FALSE(a.gate_closed);
  CHECK(d.dead_status());
  a = d.tick(input(40000, alive(), false));
  CHECK(d.state() == DownedState::alive);
}

TEST_CASE("downed: no event after the gate opens: tried again, then put back") {
  Downed d;
  d.tick(input(0, dead()));
  d.request_revive(2);
  d.tick(input(16, dead()));
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  auto a = d.tick(input(32, dead(), false, with));
  CHECK_FALSE(a.gate_closed);
  a = d.tick(input(1100, dead(), false, with));
  CHECK(logged(a, "try 2"));
  CHECK(a.gate_closed);  // shut one frame
  a = d.tick(input(1116, dead(), false, with));
  CHECK_FALSE(a.gate_closed);
  d.tick(input(2200, dead(), false, with));
  d.tick(input(2216, dead(), false, with));
  a = d.tick(input(3300, dead(), false, with));
  CHECK(d.state() == DownedState::downed);
  CHECK(logged(a, "did not start"));
  CHECK(a.gate_closed);
  REQUIRE(a.bottles.size() == 1);
  CHECK(a.bottles[0].value == Bottle{kEmpty, false, 0});
  REQUIRE(a.quiet);
  CHECK(a.quiet->empty());
}

TEST_CASE("downed: no fairy branch: the game over runs and Link comes back alive") {
  Downed d;
  d.tick(input(0, dead()));
  d.request_revive(2);
  d.tick(input(16, dead()));
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  d.tick(input(32, dead(), false, with));
  d.tick(input(100, dead(), true, with));
  auto a = d.tick(input(25000, dead(), true, with));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "no fairy branch"));
  CHECK_FALSE(a.gate_closed);
  CHECK(d.dead_status());
  a = d.tick(input(40000, alive(), false));
  CHECK(d.state() == DownedState::alive);
  CHECK(a.gate_closed);
}

// An event already running when the revive starts (a cutscene, a boss intro
// in a room he was moved into as he lay) is not the game over event: the gate
// opens only once it is over, or the revive would wait for the wrong event
// and, past 20 s, give up into the game over with mates in the room.
TEST_CASE("downed: a revive asked while another event runs opens the gate after it") {
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  {
    Downed d;
    d.tick(input(0, dead()));
    d.request_revive(2);
    auto a = d.tick(input(16, dead(), true));  // the fairy is written
    REQUIRE(d.state() == DownedState::reviving);
    a = d.tick(input(32, dead(), true, with));
    CHECK(a.gate_closed);  // not opened under the other event
    CHECK_FALSE(logged(a, "game over event running"));
    CHECK(logged(a, "another event runs"));
    for (std::uint64_t t = 1000; t <= 18000; t += 1000) {
      a = d.tick(input(t, dead(), true, with));
      CHECK(d.state() == DownedState::reviving);
      CHECK(a.gate_closed);
      CHECK(a.log.empty());  // one line for the wait, not one a frame
    }
    // It ends: the gate opens, then the game over event's fairy branch.
    a = d.tick(input(18016, dead(), false, with));
    CHECK_FALSE(a.gate_closed);
    CHECK(logged(a, "gate open"));
    a = d.tick(input(18100, dead(), true, with));
    CHECK(logged(a, "game over event running"));
    with[0] = Bottle{kEmpty, true, 1};
    a = d.tick(input(24000, alive(), false, with));
    CHECK(d.state() == DownedState::alive);
    CHECK(logged(a, "revived (by P2"));
  }
  {
    // An event that never ends: given up after revive_wait_us, the bottle
    // put back, still down (never the game over).
    with[0] = Bottle{kFairy, true, 1};
    Downed d;
    d.tick(input(0, dead()));
    d.request_revive(2);
    d.tick(input(16, dead(), true));
    auto a = d.tick(input(20000, dead(), true, with));
    CHECK(d.state() == DownedState::reviving);
    CHECK(a.gate_closed);
    a = d.tick(input(20017, dead(), true, with));
    CHECK(d.state() == DownedState::downed);
    CHECK(logged(a, "stays down"));
    CHECK(a.gate_closed);
    REQUIRE(a.bottles.size() == 1);
    CHECK(a.bottles[0].value == Bottle{kEmpty, false, 0});
    REQUIRE(a.quiet);
    CHECK(a.quiet->empty());
  }
}

TEST_CASE("downed: no bottle free: stays down") {
  Downed d;
  Bottles full;
  for (auto& x : full) x = Bottle{bottle_value("Poe"), true, 1};
  d.tick(input(0, dead(), false, full));
  d.request_revive(2);
  auto a = d.tick(input(16, dead(), false, full));
  CHECK(d.state() == DownedState::downed);
  CHECK(logged(a, "no bottle free"));
  CHECK(a.bottles.empty());
  CHECK(a.gate_closed);
}

TEST_CASE("downed: with the gate in a test's hands it is never written") {
  Downed d;
  auto in = input(0, dead());
  in.manual = true;
  auto a = d.tick(in);
  CHECK_FALSE(a.write_gate);
  CHECK(d.state() == DownedState::downed);
  d.request_revive(2);
  in = input(16, dead());
  in.manual = true;
  a = d.tick(in);
  CHECK(a.bottles.empty());
  CHECK(d.state() == DownedState::downed);
  in = input(5000, alive());
  in.manual = true;
  a = d.tick(in);
  CHECK(d.state() == DownedState::alive);
  CHECK_FALSE(a.write_gate);
}

TEST_CASE("revive watch: A held 3 s within reach of a downed mate asks once") {
  ReviveWatch w;
  ReviveInput in;
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.scene = "KokiriForest_Child";
  in.a_held = true;
  Mate m{1, std::string("KokiriForest_Child"), proto::Vec3{1.4f, 0.0f, 0.0f}, 0};
  in.mates = {m};
  auto o = w.tick(in);  // standing: nothing
  CHECK_FALSE(o.request);
  CHECK(o.log.empty());

  in.mates[0].status = proto::status::dead;
  in.now_us = 1'000'000;
  o = w.tick(in);
  CHECK(o.log.size() == 2);  // "P1 is down", "holding A beside P1"
  CHECK_FALSE(o.request);
  in.now_us = 3'900'000;
  CHECK_FALSE(w.tick(in).request);
  in.now_us = 4'000'000;
  o = w.tick(in);
  REQUIRE(o.request);
  CHECK(*o.request == 1);
  in.now_us = 5'000'000;
  CHECK_FALSE(w.tick(in).request);  // once
  in.now_us = 9'000'000;
  o = w.tick(in);  // still held, still down: again
  REQUIRE(o.request);

  in.mates[0].status = 0;  // up
  in.now_us = 9'100'000;
  o = w.tick(in);
  CHECK_FALSE(o.request);
  CHECK(o.log.size() == 1);
}

TEST_CASE("revive watch: out of reach, another scene, A let go or ourselves down: no request") {
  ReviveWatch w;
  ReviveInput in;
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.scene = "KokiriForest_Child";
  in.a_held = true;
  in.mates = {Mate{2, std::string("KokiriForest_Child"), proto::Vec3{0.0f, 0.0f, 1.6f},
                   proto::status::dead}};
  for (std::uint64_t t = 0; t <= 4'000'000; t += 100'000) {
    in.now_us = t;
    CHECK_FALSE(w.tick(in).request);
  }
  in.mates[0].position = proto::Vec3{0.0f, 0.0f, 1.0f};
  in.mates[0].scene = "KokiriForest_Adult";
  for (std::uint64_t t = 4'000'000; t <= 8'000'000; t += 100'000) {
    in.now_us = t;
    CHECK_FALSE(w.tick(in).request);
  }
  in.mates[0].scene = "KokiriForest_Child";
  in.self_down = true;
  for (std::uint64_t t = 8'000'000; t <= 12'000'000; t += 100'000) {
    in.now_us = t;
    CHECK_FALSE(w.tick(in).request);
  }
  // Letting go starts the count again.
  in.self_down = false;
  in.now_us = 12'000'000;
  w.tick(in);
  in.now_us = 14'000'000;
  in.a_held = false;
  auto o = w.tick(in);
  CHECK(o.log.size() == 1);  // "let go of P2"
  in.a_held = true;
  in.now_us = 14'100'000;
  w.tick(in);
  in.now_us = 16'000'000;
  CHECK_FALSE(w.tick(in).request);
  in.now_us = 17'100'000;
  CHECK(w.tick(in).request == std::optional<std::uint8_t>(2));
  in.a_held = std::nullopt;  // the pad cannot be read: as let go
  in.now_us = 17'200'000;
  CHECK_FALSE(w.tick(in).request);
}

// A hold begun before our connection dropped is not held across the drop:
// back, A must be held its 3 s again (the release may have come while
// nothing could be sent). Before, the hook stopped ticking the watch while
// offline and the first tick back counted the whole gap as held: an instant
// REVIVE.
TEST_CASE("revive watch: our connection lost lets go of the hold") {
  ReviveWatch w;
  ReviveInput in;
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.scene = "KokiriForest_Child";
  in.a_held = true;
  in.mates = {Mate{2, std::string("KokiriForest_Child"), proto::Vec3{0.5f, 0.0f, 0.0f},
                   proto::status::dead}};
  in.now_us = 1'000'000;
  w.tick(in);
  in.now_us = 2'000'000;
  CHECK_FALSE(w.tick(in).request);  // 1 s held
  // Offline for 10 s, the hook still ticking (A held, the mate as last
  // known); back with A held beside the same mate.
  in.connected = false;
  for (std::uint64_t t = 2'100'000; t < 12'000'000; t += 100'000) {
    in.now_us = t;
    const auto o = w.tick(in);
    CHECK_FALSE(o.request);
    CHECK_FALSE(o.progress_to);
    CHECK(o.in_reach == 0);
  }
  in.connected = true;
  in.now_us = 12'000'000;
  auto o = w.tick(in);
  CHECK_FALSE(o.request);
  REQUIRE(o.holding_percent);
  CHECK(*o.holding_percent == 0);  // the count starts again
  for (std::uint64_t t = 12'100'000; t < 15'000'000; t += 100'000) {
    in.now_us = t;
    CHECK_FALSE(w.tick(in).request);
  }
  in.now_us = 15'000'000;
  CHECK(w.tick(in).request == std::optional<std::uint8_t>(2));
}

// ---- P4: the room's rules, the group defeat, reconnects, the progress ------

namespace {

DownedInput room(std::uint64_t now_ms, Vitals v, int mates, bool connected = true,
                 std::optional<bool> event = false) {
  DownedInput in = input(now_ms, v, event);
  in.mates = mates;
  in.connected = connected;
  return in;
}

}  // namespace

TEST_CASE("downed: alone in the room the gate stays open and the game's own game over runs") {
  Downed d;
  auto a = d.tick(room(0, alive(), 0));
  CHECK(logged(a, "alone in the room"));
  CHECK_FALSE(a.gate_closed);  // the game's own death
  CHECK(a.write_gate);
  a = d.tick(room(16, dead(), 0));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "solo, gate open"));
  CHECK_FALSE(a.gate_closed);
  CHECK(d.dead_status());  // the server still sees him dead
  d.request_revive(2);     // nobody can: dropped
  a = d.tick(room(5000, dead(), 0, true, true));
  CHECK(d.state() == DownedState::game_over);
  CHECK(a.bottles.empty());
  // Continue, the reload: unreadable a while, then up with full life.
  d.tick(room(15000, Vitals{}, 0, true, true));
  a = d.tick(room(27000, alive(), 0));
  CHECK(d.state() == DownedState::alive);
  CHECK(logged(a, "back after the game over"));
  CHECK_FALSE(a.gate_closed);  // still alone

  // No co-op session at all: the same.
  Downed off;
  auto in = input(0, alive());
  in.coop = false;
  a = off.tick(in);
  CHECK(logged(a, "no co-op session"));
  CHECK_FALSE(a.gate_closed);
  in = input(16, dead());
  in.coop = false;
  off.tick(in);
  CHECK(off.state() == DownedState::game_over);
}

TEST_CASE("downed: a mate joins, the gate shuts, the last mate leaves, 10 s, then the game over") {
  Downed d;
  CHECK_FALSE(d.tick(room(0, alive(), 0)).gate_closed);
  auto a = d.tick(room(16, alive(), 1));
  CHECK(logged(a, "a mate in the room"));
  CHECK(a.gate_closed);
  a = d.tick(room(32, dead(), 1));
  CHECK(d.state() == DownedState::downed);
  CHECK(a.gate_closed);

  // He leaves and comes back within the grace: stays down, nothing else.
  a = d.tick(room(1000, dead(), 0));
  CHECK(logged(a, "no mate left in the room"));
  CHECK(a.gate_closed);
  CHECK(d.alone_until() == std::optional<std::uint64_t>(11'000'000));
  a = d.tick(room(8000, dead(), 1));
  CHECK(logged(a, "in the room again: stays down"));
  CHECK_FALSE(d.alone_until());
  a = d.tick(room(15000, dead(), 1));
  CHECK(d.state() == DownedState::downed);

  // He leaves for good: 10 s later the game over runs.
  d.tick(room(20000, dead(), 0));
  a = d.tick(room(29990, dead(), 0));
  CHECK(d.state() == DownedState::downed);
  CHECK(a.gate_closed);
  a = d.tick(room(30000, dead(), 0));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "group defeat of one"));
  CHECK_FALSE(a.gate_closed);
  a = d.tick(room(50000, alive(), 0));
  CHECK(d.state() == DownedState::alive);
  CHECK_FALSE(a.gate_closed);
}

TEST_CASE("downed: GROUP_DEFEAT opens the gate, after the reload the gate shuts again") {
  Downed d;
  CHECK_FALSE(d.group_defeat(2));  // alive: dropped
  d.tick(room(0, alive(), 1));
  d.tick(room(16, dead(), 1));
  REQUIRE(d.state() == DownedState::downed);
  CHECK(d.group_defeat(2));
  auto a = d.tick(room(2100, dead(), 1));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "downed -> game_over: group defeat (2 down)"));
  CHECK_FALSE(a.gate_closed);
  CHECK(d.dead_status());
  CHECK_FALSE(d.group_defeat(2));  // the game over runs already
  d.tick(room(2200, dead(), 1, true, true));  // the game over event
  d.tick(room(14000, Vitals{}, 1, true, true));
  a = d.tick(room(26000, alive(), 1));
  CHECK(d.state() == DownedState::alive);
  CHECK(a.gate_closed);  // a mate is in: the next death leaves us down again
  CHECK_FALSE(d.dead_status());
  a = d.tick(room(30000, dead(), 1));
  CHECK(d.state() == DownedState::downed);  // the old defeat is gone
}

TEST_CASE("downed: GROUP_DEFEAT during a revive is everyone's, ours too (D-R66)") {
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  {
    // Getting up, the gate open: the defeat that comes anyway is ours too.
    // The bottle is put back and the game over runs (D-R66 replaces D-R14's
    // "a revive that works forgets the defeat").
    Downed d;
    d.tick(input(0, dead()));
    d.request_revive(2);
    d.tick(input(16, dead()));
    REQUIRE(d.state() == DownedState::reviving);
    d.tick(input(32, dead(), false, with));  // gate open
    CHECK(d.group_defeat(2));
    auto a = d.tick(input(100, dead(), false, with));
    CHECK(d.state() == DownedState::game_over);
    CHECK(logged(a, "reviving -> game_over: group defeat (2 down) while getting up"));
    REQUIRE(a.bottles.size() == 1);
    CHECK(a.bottles[0] == BottleWrite{0, Bottle{kEmpty, false, 0}});  // put back
    CHECK_FALSE(a.gate_closed);  // the game over runs
    CHECK(d.status_dead());
    CHECK_FALSE(d.group_defeat(2));  // once
  }
  {
    // The frame after the revive started, the fairy written but the gate not
    // open yet: put back the same.
    Downed e;
    e.tick(input(0, dead()));
    e.request_revive(2);
    auto a = e.tick(input(16, dead()));
    REQUIRE(e.state() == DownedState::reviving);
    REQUIRE(a.bottles.size() == 1);  // the fairy written
    CHECK(e.group_defeat(3));
    a = e.tick(input(32, dead()));
    CHECK(e.state() == DownedState::game_over);
    REQUIRE(a.bottles.size() == 1);
    CHECK(a.bottles[0] == BottleWrite{0, Bottle{kEmpty, false, 0}});
  }
  {
    Downed d;
    Bottles full;
    for (auto& x : full) x = Bottle{bottle_value("Poe"), true, 1};
    d.tick(input(0, dead(), false, full));
    d.request_revive(2);
    CHECK(d.group_defeat(3));
    auto a = d.tick(input(16, dead(), false, full));
    CHECK(d.state() == DownedState::game_over);  // the defeat is taken before the revive
    CHECK(logged(a, "group defeat (3 down)"));
  }
}

// ORM D-R66, D-R73: the group never splits. The dead bit (PLAYER_STATUS bit
// 4, what the server's group defeat counts) is off from the REVIVE taken: the
// one getting up counts as standing even if his reviver falls in those
// seconds. A revive that fails puts the bit back (the server's 2 s start
// over). During the 3 s hold the bit stays (REVIVE goes only to a dead
// player) and the hold shows as held(); the server counts it
// (tests/server/session_test.cpp, "a downed player being revived").
TEST_CASE("downed: being revived counts as standing for the group defeat, a failed revive is down again (D-R66)") {
  Downed d;
  d.tick(input(0, alive()));
  d.tick(input(16, dead()));
  REQUIRE(d.state() == DownedState::downed);
  CHECK(d.status_dead());
  CHECK(d.dead_status());
  CHECK_FALSE(d.held(16'000));

  // A mate holds A beside us: held while his progress keeps coming, the dead
  // bit still on (the request must reach us).
  d.hear_progress(2, 40, 1'000'000);
  CHECK(d.held(1'500'000));
  CHECK(d.status_dead());
  d.hear_progress(2, 80, 1'750'000);
  CHECK(d.held(2'700'000));
  CHECK_FALSE(d.held(2'800'000));  // he let go (or fell): down again
  CHECK(d.status_dead());

  // The request: getting up, standing for the group, still not on our feet.
  d.hear_progress(2, 90, 3'000'000);
  d.request_revive(2);
  auto a = d.tick(input(3016, dead()));
  REQUIRE(d.state() == DownedState::reviving);
  CHECK_FALSE(d.status_dead());
  CHECK(d.dead_status());  // no hold beside anyone, the other rules see him lying
  CHECK_FALSE(d.held(3'016'000));
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  d.tick(input(3032, dead(), false, with));  // gate open
  CHECK_FALSE(d.status_dead());

  // The game over event never starts: tried again, then down again, the bit back.
  a = d.tick(input(4100, dead(), false, with));
  a = d.tick(input(4116, dead(), false, with));
  a = d.tick(input(5200, dead(), false, with));
  a = d.tick(input(5216, dead(), false, with));
  a = d.tick(input(6300, dead(), false, with));
  CHECK(d.state() == DownedState::downed);
  CHECK(logged(a, "reviving -> downed: the game over event did not start"));
  CHECK(d.status_dead());

  // A second revive that works: the bit stays off to the end and after.
  d.request_revive(3);
  d.tick(input(7000, dead()));
  REQUIRE(d.state() == DownedState::reviving);
  CHECK_FALSE(d.status_dead());
  d.tick(input(7016, dead(), false, with));
  d.tick(input(7100, dead(), true, with));
  CHECK_FALSE(d.status_dead());
  with[0] = Bottle{kEmpty, true, 1};
  d.tick(input(13000, alive(), false, with));
  CHECK(d.state() == DownedState::alive);
  CHECK_FALSE(d.status_dead());

  // A game over running keeps the bit (everyone is down: the group's).
  d.tick(input(14000, dead()));
  CHECK(d.group_defeat(2));
  d.tick(input(14016, dead()));
  REQUIRE(d.state() == DownedState::game_over);
  CHECK(d.status_dead());
}

TEST_CASE("downed: a reconnect while down stays down (rejoin by name), the death word read from memory") {
  FakeMemory m;
  put_link(m, 0, 12, 0x43af);  // the game still shows Link dead
  Downed d;
  d.tick(room(0, alive(), 1));
  auto a = d.tick(room(16, read_vitals(m, kActor), 1));
  REQUIRE(d.state() == DownedState::downed);

  // The connection drops: presence cleared, nobody in.
  a = d.tick(room(1000, read_vitals(m, kActor), 0, false));
  CHECK(logged(a, "our connection is lost"));
  CHECK(logged(a, "not connected"));
  CHECK(a.gate_closed);
  for (std::uint64_t t = 1000; t <= 31000; t += 1000) {
    a = d.tick(room(t, read_vitals(m, kActor), 0, false));
    CHECK(d.state() == DownedState::downed);
    CHECK(a.gate_closed);
    CHECK(d.dead_status());  // what the status bit says on the first connected frame
  }
  // Welcomed back with the mate in: still down, not alive, not a game over.
  a = d.tick(room(31016, read_vitals(m, kActor), 1));
  CHECK(d.state() == DownedState::downed);
  CHECK(logged(a, "in the room again: stays down"));
  CHECK(a.gate_closed);
  CHECK(d.dead_status());
  // And the mate can still get him up.
  d.request_revive(2);
  d.tick(room(32000, read_vitals(m, kActor), 1));
  CHECK(d.state() == DownedState::reviving);

  // A game whose glue started afresh (state alive) finds Link dead while
  // reconnecting: down, not a solo game over.
  Downed fresh;
  a = fresh.tick(room(0, read_vitals(m, kActor), 0, false));
  CHECK(fresh.state() == DownedState::downed);
  CHECK(a.gate_closed);
  CHECK(fresh.dead_status());

  // Offline for good: 60 s, then the game over.
  Downed gone;
  gone.tick(room(0, dead(), 1));
  gone.tick(room(1000, dead(), 0, false));
  CHECK(gone.tick(room(60999, dead(), 0, false)).gate_closed);
  a = gone.tick(room(61000, dead(), 0, false));
  CHECK(gone.state() == DownedState::game_over);
  CHECK(logged(a, "no connection all this time"));
  CHECK_FALSE(a.gate_closed);
}

TEST_CASE("downed: a mate progress shows 1 s, being revived shows 100") {
  Downed d;
  d.hear_progress(2, 30, 1'000);  // alive: dropped
  CHECK_FALSE(d.reviver(1'000));
  d.tick(input(0, dead()));
  d.hear_progress(2, 30, 1'000'000);
  REQUIRE(d.reviver(1'500'000));
  CHECK(d.reviver(1'500'000)->by == 2);
  CHECK(d.reviver(1'500'000)->percent == 30);
  CHECK_FALSE(d.reviver(2'100'000));  // let go: gone after 1 s
  d.hear_progress(2, 101, 2'200'000);  // not a percent
  CHECK_FALSE(d.reviver(2'300'000));
  d.request_revive(2);
  d.tick(input(3000, dead()));
  REQUIRE(d.state() == DownedState::reviving);
  REQUIRE(d.reviver(3'000'000));
  CHECK(d.reviver(3'000'000)->percent == 100);
}

TEST_CASE("revive watch: progress every 250 ms while A is held, then the request") {
  ReviveWatch w;
  ReviveInput in;
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.scene = "KokiriForest_Child";
  in.a_held = false;
  in.mates = {Mate{3, std::string("KokiriForest_Child"), proto::Vec3{1.0f, 0.0f, 0.0f},
                   proto::status::dead}};
  in.now_us = 0;
  auto o = w.tick(in);
  CHECK(o.in_reach == 3);  // the screen says "hold A to revive"
  CHECK_FALSE(o.holding_percent);
  CHECK_FALSE(o.progress_to);

  in.a_held = true;
  std::vector<int> sent;
  for (std::uint64_t t = 1'000'000; t <= 4'100'000; t += 16'667) {
    in.now_us = t;
    o = w.tick(in);
    if (o.progress_to) {
      CHECK(*o.progress_to == 3);
      sent.push_back(o.progress);
    }
    if (o.request) break;
  }
  REQUIRE(o.request == std::optional<std::uint8_t>(3));
  CHECK(o.holding_percent == std::optional<int>(100));
  REQUIRE(sent.size() == 12);  // 0, 8, 16, ... over the 3 s
  CHECK(sent.front() == 0);
  CHECK(sent.back() >= 90);
  CHECK(sent.back() < 100);
  for (std::size_t i = 1; i < sent.size(); ++i) CHECK(sent[i] > sent[i - 1]);
  in.now_us += 300'000;
  o = w.tick(in);
  CHECK_FALSE(o.progress_to);  // asked: no more progress
  CHECK(o.holding_percent == std::optional<int>(100));
}

TEST_CASE("downed: the line on the screen") {
  BannerView v;
  CHECK(banner_text(v).empty());
  v.target = "Saria";
  CHECK(banner_text(v) == "Mant\xc3\xa9n A para revivir a Saria");
  v.holding_percent = 40;
  CHECK(banner_text(v) == "Mant\xc3\xa9n A para revivir a Saria 40 %");
  BannerView down;
  down.state = DownedState::downed;
  CHECK(banner_text(down).find("Has ca\xc3\xad" "do") == 0);
  down.reviver = "Link";
  down.reviver_percent = 58;
  CHECK(banner_text(down) == "Link te est\xc3\xa1 reviviendo 58 %");
  down.reviver.reset();
  down.alone_left_us = 9'200'000;
  CHECK(banner_text(down) == "Has ca\xc3\xad" "do y no queda nadie: game over en 10 s");
  down.state = DownedState::game_over;
  CHECK(banner_text(down).empty());
  // ORM: the group's game over says why (alone, the game's own screen is enough).
  down.group_defeat = true;
  CHECK(banner_text(down) == "Todos hab\xc3\xa9is ca\xc3\xad" "do: game over");
  down.state = DownedState::alive;  // back after the reload: nothing
  CHECK(banner_text(down).empty());
}

TEST_CASE("downed: chat notices and the downed tag note") {
  CHECK(mate_down_notice("Saria") == "Saria ha ca\xc3\xad" "do: ve a su lado y mant\xc3\xa9n A");
  CHECK(mate_up_notice("Saria") == "Saria vuelve a estar en pie");
  CHECK(downed_tag_note(std::nullopt) == "ca\xc3\xad" "do");
  CHECK(downed_tag_note(22.6f) == "ca\xc3\xad" "do, 23 m");
  CHECK(downed_tag_note(0.2f) == "ca\xc3\xad" "do, 0 m");
  CHECK(downed_tag_note(std::numeric_limits<float>::quiet_NaN()) == "ca\xc3\xad" "do");
  CHECK(downed_tag_note(-1.0f) == "ca\xc3\xad" "do");
}

TEST_CASE("revive watch: who went down and who got up, for the chat") {
  ReviveWatch w;
  ReviveInput in;
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.scene = "KokiriForest_Child";
  in.mates = {Mate{1, std::string("KokiriForest_Child"), proto::Vec3{40.0f, 0.0f, 0.0f}, 0},
              Mate{3, std::string("HyruleField_Child"), proto::Vec3{0.0f, 0.0f, 0.0f}, 0}};
  auto o = w.tick(in);
  CHECK(o.went_down.empty());
  CHECK(o.got_up.empty());
  in.mates[0].status = proto::status::dead;  // far away, out of reach: still told
  in.mates[1].status = proto::status::dead;  // another scene: still told
  in.now_us = 1'000'000;
  o = w.tick(in);
  CHECK(o.went_down == std::vector<std::uint8_t>{1, 3});
  CHECK(o.in_reach == 0);
  in.now_us = 2'000'000;
  o = w.tick(in);
  CHECK(o.went_down.empty());  // once
  in.mates[0].status = 0;
  in.now_us = 3'000'000;
  o = w.tick(in);
  CHECK(o.got_up == std::vector<std::uint8_t>{1});
  CHECK(o.went_down.empty());
}

TEST_CASE("screen line: a lost connection stays until it is back, a blip does not show") {
  CHECK(connection_text(false, false, 60'000).empty());  // never welcomed: still joining
  CHECK(connection_text(true, true, 0).empty());
  CHECK(connection_text(true, false, 400).empty());  // a blip
  const std::string lost = connection_text(true, false, 12'300);
  CHECK(lost.find("Sin conexi") == 0);
  CHECK(lost.find("reconectando (12 s)") != std::string::npos);
  CHECK(connection_text(true, true, 12'300).empty());  // back
  CHECK(join_screen_lines("", "").empty());
  CHECK(join_screen_lines("a", "") == "a");
  CHECK(join_screen_lines("", "b") == "b");
  CHECK(join_screen_lines("a", "b") == "a   |   b");
}

// ---- The life after a mate's revive (oot-64 PLAN 2.8.4, D-036) --------------

namespace {

Vitals hearts(std::int32_t life, std::int32_t max, bool up = true) {
  return {life, max, up ? 0x43a0u : 0x43afu};
}

// Down, revived by P2 through a faked bottle; the last tick has the event
// over with Link up at `after`.
DownedActions revive_by_p2(Downed& d, Vitals after) {
  const Vitals down = hearts(0, after.max_life.value_or(12), false);
  Bottles with = none_owned();
  with[0] = Bottle{kFairy, true, 1};
  d.tick(input(0, down));
  REQUIRE(d.state() == DownedState::downed);
  d.request_revive(2);
  d.tick(input(16, down));                    // the fairy written
  d.tick(input(32, down, false, with));       // the gate open
  d.tick(input(100, down, true, with));       // the game over event's fairy branch
  with[0] = Bottle{kEmpty, true, 1};
  auto a = d.tick(input(5000, after, false, with));
  REQUIRE(d.state() == DownedState::alive);
  return a;
}

// game() with PlayerStatus.Life in the Int table and the Int setter's queue.
constexpr std::uint64_t kIntQueue = 0x8220000000ull;
Writable game_with_life() {
  Writable w = game();
  std::vector<std::uint32_t> ints;
  for (int s = 0; s < kBottleCount; ++s) ints.push_back(bottle_keys(s).stock);
  ints.push_back(kLifeFlag);
  table(w.mem, kIntT, ints);
  w.mem.put_u32(kMgr + 0xd0 + 0x20, 4);           // the queue's room
  w.mem.put_u64(kMgr + 0xd0 + 0x28, kIntQueue);   // its slots, 12 bytes each
  w.mem.put_u32(kMgr + 0xd0 + 0x30, 0x30000000);  // round 3, nothing taken
  for (std::size_t i = 0; i < 12 * 4; ++i) w.mem.put_u8(kIntQueue + i, 0);
  return w;
}

}  // namespace

TEST_CASE("downed: the life a revive leaves at least is max(3 hearts, 25 %), whole hearts") {
  CHECK(kLifeFlag == proto::flag_hash("PlayerStatus.Life"));
  CHECK(revive_life(12) == 12);   // 3 hearts: all of them
  CHECK(revive_life(48) == 12);   // 12 hearts: 25 % is 3
  CHECK(revive_life(64) == 16);   // 16 hearts: 4
  CHECK(revive_life(80) == 20);   // 20 hearts: 5
  CHECK(revive_life(84) == 24);   // 21 hearts: 5.25, a whole heart up: 6
  CHECK(revive_life(52) == 16);   // 13 hearts: 3.25 -> 4
  CHECK(revive_life(8) == 8);     // never above the max
  CHECK(revive_life(0) == 0);
  CHECK(revive_life(-4) == 0);
}

TEST_CASE("downed: a mate's revive that leaves less writes the life, by the queue then straight") {
  Downed d;
  auto a = revive_by_p2(d, hearts(12, 80));
  CHECK(logged(a, "reviving -> alive: revived (by P2, life 12 of 80"));
  REQUIRE(a.life);
  CHECK(*a.life == LifeWrite{20, false});  // the game's queue first
  CHECK(logged(a, "life 12 of 80 after the revive, less than max(3 hearts, 25 %) = 20"));
  CHECK(d.healing());

  a = d.tick(input(5300, hearts(12, 80)));
  CHECK_FALSE(a.life);  // checked 0.5 s after the write
  a = d.tick(input(5500, hearts(12, 80)));
  REQUIRE(a.life);
  CHECK(*a.life == LifeWrite{20, true});  // the queue did nothing: straight into the component
  CHECK(logged(a, "life still 12 of 80 0.5 s after the write: written straight into the Life component (try 2)"));
  a = d.tick(input(5700, hearts(20, 80)));
  CHECK_FALSE(a.life);
  CHECK(logged(a, "life 20 of 80 after the revive (max(3 hearts, 25 %), 2 writes)"));
  CHECK_FALSE(d.healing());
  CHECK(d.state() == DownedState::alive);
  // Nothing more, whatever the life does next (a hit is the game's).
  CHECK_FALSE(d.tick(input(9000, hearts(4, 80))).life);
}

TEST_CASE("downed: enough life after the revive, our own fairy, or life never reached: no endless writes") {
  {
    Downed d;  // the fairy filled him: nothing written
    const auto a = revive_by_p2(d, hearts(80, 80));
    CHECK_FALSE(a.life);
    CHECK_FALSE(d.healing());
  }
  {
    Downed d;  // our own fairy while down: the game's fairy, the game's life
    Bottles own = none_owned();
    own[1] = Bottle{kFairy, true, 1};
    d.tick(input(0, hearts(0, 80, false)));
    auto a = d.tick(input(16, hearts(0, 80, false), false, own));
    REQUIRE(d.state() == DownedState::reviving);
    d.tick(input(32, hearts(0, 80, false), false, own));  // the gate open
    d.tick(input(100, hearts(0, 80, false), true, own));
    own[1] = Bottle{kEmpty, true, 1};
    a = d.tick(input(5000, hearts(12, 80), false, own));
    REQUIRE(d.state() == DownedState::alive);
    CHECK(logged(a, "revived (our own fairy"));
    CHECK_FALSE(a.life);
  }
  {
    Downed d;  // the life never takes: three writes, then given up
    auto a = revive_by_p2(d, hearts(12, 80));
    REQUIRE(a.life);
    int writes = 1;
    for (std::uint64_t t = 5100; t <= 9000; t += 100) {
      a = d.tick(input(t, hearts(12, 80)));
      if (a.life) ++writes;
    }
    CHECK(writes == 3);
    CHECK_FALSE(d.healing());
  }
  {
    Downed d;  // killed again before it took: down, no heal left over
    revive_by_p2(d, hearts(12, 80));
    auto a = d.tick(input(5200, hearts(0, 80, false)));
    CHECK(d.state() == DownedState::downed);
    CHECK_FALSE(a.life);
    CHECK_FALSE(d.healing());
  }
}

TEST_CASE("downed: the life is written through the game's queue or straight into the component") {
  Writable w = game_with_life();
  put_link(w.mem, 12, 80, 0x43a0);
  REQUIRE(write_life(w, kActor, 20, false));
  CHECK(read_u32(w.mem, kIntQueue) == 20u);           // the value
  CHECK(read_u32(w.mem, kIntQueue + 4) == kLifeFlag);  // the flag
  CHECK(GameFlags(w).get_int(kLifeFlag) == 20);       // seen as the game's getter sees it
  CHECK(read_vitals(w, kActor).life == 12);           // the component is the game's to update

  REQUIRE(write_life(w, kActor, 20, true));
  CHECK(read_vitals(w, kActor).life == 20);
  REQUIRE(write_life(w, kActor, 0, true));  // what puts a Link down again (D-R62)
  CHECK(read_vitals(w, kActor).life == 0);

  CHECK_FALSE(write_life(w, kActor, 81, true));  // above the max
  CHECK_FALSE(write_life(w, kActor, -1, false));
  CHECK_FALSE(write_life(w, 0x1234, 4, true));   // no Link there
  Writable bare;                                  // no flag manager: the queue cannot
  put_link(bare.mem, 12, 80, 0x43a0);
  CHECK_FALSE(write_life(bare, kActor, 20, false));
  CHECK(write_life(bare, kActor, 20, true));
}

// ---- A reload at the room's age while down (D-R62, oot-64 D-178) -------------

TEST_CASE("downed: a reload while down keeps us down, and a Link the load got up is put down again") {
  Downed d;
  d.tick(input(0, dead()));
  REQUIRE(d.state() == DownedState::downed);
  auto held = input(1000, dead());
  held.hold_down = true;
  auto a = d.tick(held);
  CHECK(logged(a, "we stay down through it (D-R62, oot-64 D-178)"));
  CHECK(d.held_down());
  CHECK(a.gate_closed);
  CHECK(d.status_dead());  // the mates still see us down

  // The load: Link unreadable, then a new Link standing with full life.
  held.vitals = Vitals{};
  held.now_us = 2'000'000;
  CHECK(d.tick(held).log.empty());
  d.request_revive(3);  // a REVIVE in the middle of it waits
  held.vitals = alive();
  for (std::uint64_t t = 3000; t <= 8000; t += 500) {
    held.now_us = t * 1000;
    a = d.tick(held);
    CHECK(d.state() == DownedState::downed);  // never "up without a revive"
    CHECK_FALSE(a.life);
    CHECK(a.gate_closed);
  }
  // The reload is over: up, so put down again where he stands, life 0.
  a = d.tick(input(8100, alive()));
  REQUIRE(a.life);
  CHECK(*a.life == LifeWrite{0, true});
  CHECK(logged(a, "the reload got Link up (life 12 of 12): put down again where he stands, life 0 (try 1)"));
  CHECK(d.state() == DownedState::downed);
  CHECK_FALSE(d.tick(input(8600, alive())).life);  // 1 s between writes
  a = d.tick(input(9100, alive()));
  REQUIRE(a.life);
  CHECK(logged(a, "(try 2)"));
  a = d.tick(input(9200, dead()));
  CHECK(logged(a, "down again after the reload (2 writes of life 0)"));
  CHECK_FALSE(d.held_down());
  // Down as before: the REVIVE that came during the reload is taken now.
  CHECK(d.state() == DownedState::reviving);
  CHECK(logged(a, "downed -> reviving: revive requested by P3"));
}

TEST_CASE("downed: a reload that leaves Link dead writes nothing, one that keeps him up gives up") {
  {
    Downed d;
    d.tick(input(0, dead()));
    auto held = input(1000, dead());
    held.hold_down = true;
    d.tick(held);
    const auto a = d.tick(input(5000, dead()));
    CHECK(logged(a, "the reload is over: still down"));
    CHECK_FALSE(a.life);
    CHECK(d.state() == DownedState::downed);
    CHECK_FALSE(d.held_down());
  }
  {
    Downed d;
    d.tick(input(0, dead()));
    auto held = input(1000, dead());
    held.hold_down = true;
    d.tick(held);
    int writes = 0;
    DownedActions a;
    for (std::uint64_t t = 2000; t <= 7000 && d.state() == DownedState::downed; t += 500) {
      a = d.tick(input(t, alive()));
      if (a.life) ++writes;
    }
    CHECK(writes == 3);
    CHECK(d.state() == DownedState::alive);
    CHECK(logged(a, "3 writes of life 0 did not put him down again"));
    CHECK(logged(a, "downed -> alive: up without a revive"));
  }
}

TEST_CASE("downed: a GROUP_DEFEAT during the reload waits for its end") {
  Downed d;
  d.tick(input(0, dead()));
  auto held = input(1000, dead());
  held.hold_down = true;
  d.tick(held);
  CHECK(d.group_defeat(2));
  held.now_us = 1'500'000;
  d.tick(held);
  CHECK(d.state() == DownedState::downed);  // not while the reload runs
  const auto a = d.tick(input(4000, dead()));
  CHECK(d.state() == DownedState::game_over);
  CHECK(logged(a, "group defeat (2 down)"));
  CHECK_FALSE(a.gate_closed);
}
