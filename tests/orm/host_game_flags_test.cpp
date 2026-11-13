// ORM cases moved out of upstream's tests/host/game_flags_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <functional>
#include <string>
#include <vector>

#include "coop/host/flag_defer.hpp"
#include "coop/host/flag_watcher.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/game_items.hpp"
#include "coop/protocol/codec.hpp"
#include "../host/fake_memory.hpp"

namespace {

using coop::host::GameFlags;
using coop::host::GameFlagStore;
using coop::host::test::FakeMemory;
namespace protocol = coop::protocol;

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
    if (v && after_cas) after_cas();
    return v;
  }
  FakeMemory mem;
  std::function<void()> after_cas;
};

constexpr std::uint64_t kMgr = 0x8000000000ull;
constexpr std::uint64_t kBoolEntries = 0x8100000000ull, kBoolSlots = 0x8110000000ull,
                        kBoolQueue = 0x8120000000ull;
constexpr std::uint64_t kIntEntries = 0x8200000000ull, kIntSlots = 0x8210000000ull,
                        kIntQueue = 0x8220000000ull;
constexpr std::uint64_t kArrEntries = 0x8300000000ull, kArrSlots = 0x8310000000ull,
                        kArrBits = 0x8320000000ull;
constexpr std::uint64_t kKeys = 0x8400000000ull, kKeyQueue = 0x8410000000ull;
constexpr std::uint64_t kUIntEntries = 0x8500000000ull, kUIntSlots = 0x8510000000ull,
                        kUIntQueue = 0x8520000000ull;
constexpr std::uint64_t kEnumEntries = 0x8600000000ull, kEnumSlots = 0x8610000000ull,
                        kEnumQueue = 0x8620000000ull;
constexpr std::uint32_t kCap = 7, kQueueCap = 4;

void zero(FakeMemory& m, std::uint64_t a, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) m.put_u8(a + i, 0);
}

// One hashed table: `hashes` go in entries 0.., slots probed from hash % kCap.
void table(FakeMemory& m, std::uint64_t base, std::uint64_t entries, std::uint64_t slots,
           std::uint64_t stride, std::initializer_list<std::uint32_t> hashes) {
  m.put_u32(kMgr + base, static_cast<std::uint32_t>(hashes.size()));
  m.put_u64(kMgr + base + 8, entries);
  m.put_u64(kMgr + base + 0x10, slots);
  m.put_u32(kMgr + base + 0x1c, kCap);
  zero(m, slots, 8 * kCap);
  zero(m, entries, stride * hashes.size());
  std::uint32_t index = 0;
  for (const auto h : hashes) {
    std::uint32_t s = h % kCap;
    while (*coop::host::read_u32(m, slots + 8 * s) != 0) s = (s + 1) % kCap;
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

// Bools 0x11 (entry 0) and 0x18 (entry 1, same start slot as 0x11), Int 0x22,
// BoolArray 0x33 of 40 elements, 64-bit keys 5, 9, 0x8c44828386468bf, UInt 0x44.
Writable game() {
  Writable w;
  auto& m = w.mem;
  m.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
  table(m, 0x10, kBoolEntries, kBoolSlots, 0x18, {0x11, 0x18});
  queue(m, 0x10, kBoolQueue, 8);
  table(m, 0xd0, kIntEntries, kIntSlots, 0x28, {0x22});
  queue(m, 0xd0, kIntQueue, 12);
  table(m, 0x790, kUIntEntries, kUIntSlots, 0x28, {0x44});
  queue(m, 0x790, kUIntQueue, 12);
  table(m, 0x250, kEnumEntries, kEnumSlots, 0x38, {0x66});
  queue(m, 0x250, kEnumQueue, 8);
  table(m, 0x70, kArrEntries, kArrSlots, 0x40, {0x33});
  m.put_u32(kArrEntries + 0x20, 40);
  m.put_u64(kArrEntries + 0x28, kArrBits);
  m.put_u32(kArrBits, 0);
  m.put_u32(kArrBits + 4, 1u << 3);  // element 35
  m.put_u32(kMgr + 0xc58, 3);
  m.put_u64(kMgr + 0xc60, kKeys);
  const std::uint64_t keys[] = {5, 9, 0x8c44828386468bfull};
  for (int i = 0; i < 3; ++i) {
    zero(m, kKeys + 16 * i, 16);
    m.put_u64(kKeys + 16 * i, keys[i]);
  }
  m.put_u32(kMgr + 0xc68, kQueueCap);
  m.put_u64(kMgr + 0xc70, kKeyQueue);
  m.put_u32(kMgr + 0xc78, 0);
  for (std::uint32_t i = 0; i < kQueueCap; ++i) {
    m.put_u32(kKeyQueue + 8 * i, 0);
    m.put_u32(kKeyQueue + 8 * i + 4, 0xffffffffu);
  }
  return w;
}

}  // namespace

// Enum flags (type 6, M + 0x250), as the game's getter 0x7e73d0 and setter
// 0x3e0980 read them: entries of 0x38, the live u32 at +0x1c (the value
// name's Murmur3 hash), the value count at +0x28 and their hashes at +0x30,
// 8-byte queue slots {value, hash}. Written out again here (docs/flags.md).
namespace {

// kEnumEntries, kEnumSlots and kEnumQueue are upstream's, above.
constexpr std::uint64_t kEnumValues = 0x8630000000ull;
const std::uint32_t kDeku = coop::host::flag_hash("Step_Meet_DekuTree");  // 0x3372689b
const std::uint32_t kForest = coop::host::flag_hash("Step_SageOfForest");
const std::uint32_t kNotReady = coop::host::flag_hash("NotReady");
const std::uint32_t kReady = coop::host::flag_hash("Ready");
const std::uint32_t kStep02 = coop::host::flag_hash("Step02");
const std::uint32_t kComplete = coop::host::flag_hash("Complete");

// The game() above plus Enum flags kDeku (entry 0, NotReady, with its six
// values listed in the entry) and kForest (entry 1, value 0: not one of its
// values).
Writable game_with_enums() {
  Writable w = game();
  auto& m = w.mem;
  table(m, 0x250, kEnumEntries, kEnumSlots, 0x38, {kDeku, kForest});
  queue(m, 0x250, kEnumQueue, 8);
  m.put_u32(kEnumEntries + 0x18, kNotReady);  // the default, as in a run
  m.put_u32(kEnumEntries + 0x1c, kNotReady);
  m.put_u32(kEnumEntries + 0x20, kNotReady);
  const std::uint32_t values[] = {kNotReady, kReady, coop::host::flag_hash("Step01"), kStep02,
                                  coop::host::flag_hash("Step03"), kComplete};
  m.put_u32(kEnumEntries + 0x28, 6);
  m.put_u64(kEnumEntries + 0x30, kEnumValues);
  for (std::uint32_t i = 0; i < 6; ++i) m.put_u32(kEnumValues + 4 * i, values[i]);
  return w;
}

coop::host::EnumFlags story() {
  coop::host::EnumFlags e;
  e.add(kDeku, {kNotReady, kReady, coop::host::flag_hash("Step01"), kStep02,
                coop::host::flag_hash("Step03"), kComplete});
  e.add(kForest, {coop::host::flag_hash("Step01"), coop::host::flag_hash("Step08")});
  return e;
}

}  // namespace

TEST_CASE("game flags: Enum flags are read and queued like Int, in their own table") {
  auto w = game_with_enums();
  GameFlags f(w);
  CHECK(f.get_enum(kDeku) == kNotReady);
  CHECK(f.get_enum(kForest) == 0u);
  CHECK_FALSE(f.get_enum(0x22));  // an Int is not an Enum
  CHECK_FALSE(f.get_int(kDeku));  // nor the other way
  CHECK_FALSE(f.get_bool(kDeku));
  CHECK(f.find_type(kDeku) == 6);
  CHECK_FALSE(GameFlags::set_enum(w, 0x22, kReady));
  REQUIRE(GameFlags::set_enum(w, kDeku, kStep02));
  CHECK(coop::host::read_u32(w.mem, kEnumQueue) == kStep02);
  CHECK(coop::host::read_u32(w.mem, kEnumQueue + 4) == kDeku);
  CHECK(coop::host::read_u32(w.mem, kEnumQueue + 8) == 0u);           // the next slot: 8 bytes
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30100001u);  // 1 taken, 1 finished
  CHECK(coop::host::read_u32(w.mem, kEnumEntries + 0x1c) == kNotReady);  // not applied yet
  CHECK(f.get_enum(kDeku) == kStep02);                                  // but seen
  CHECK(coop::host::read_u32(w.mem, kIntQueue + 4) == 0u);  // the Int queue is untouched
  // A second write takes the next 8-byte slot, and the newest is what reads.
  REQUIRE(GameFlags::set_enum(w, kDeku, kComplete));
  CHECK(coop::host::read_u32(w.mem, kEnumQueue + 8) == kComplete);
  CHECK(coop::host::read_u32(w.mem, kEnumQueue + 12) == kDeku);
  CHECK(f.get_enum(kDeku) == kComplete);
}

TEST_CASE("game flags: an Enum entry is 0x38 bytes and lists the game's values") {
  auto w = game_with_enums();
  const std::uint32_t step08 = coop::host::flag_hash("Step08");
  w.mem.put_u32(kEnumEntries + 0x38 + 0x1c, step08);  // entry 1's live value
  w.mem.put_u32(kEnumEntries + 0x28 + 0x1c, 0x1234);  // where a 0x28 stride would look
  GameFlags f(w);
  CHECK(f.get_enum(kForest) == step08);
  CHECK(f.get_enum(kDeku) == kNotReady);
  const auto values = f.enum_values(kDeku);
  REQUIRE(values);
  CHECK(*values == std::vector<std::uint32_t>{kNotReady, kReady, coop::host::flag_hash("Step01"),
                                              kStep02, coop::host::flag_hash("Step03"), kComplete});
  CHECK_FALSE(f.enum_values(kForest));  // no list in its entry
  CHECK_FALSE(f.enum_values(0x22));     // not an Enum
  CHECK(f.live_address(GameFlags::Live::enum32, kForest) == kEnumEntries + 0x38 + 0x1c);
}

TEST_CASE("game flag store: an Enum flag is nothing unless a list opts it in") {
  auto w = game_with_enums();
  GameFlagStore plain(w);  // today's store: no Enum flags
  CHECK_FALSE(plain.read({kDeku, 0}));
  CHECK_FALSE(plain.write({kDeku, 0, protocol::FlagKind::uint32, kReady}));
  CHECK_FALSE(plain.write({kDeku, 0, protocol::FlagKind::int32, kReady}));
  const coop::host::EnumFlags none;
  GameFlagStore empty(w, &none);
  CHECK_FALSE(empty.read({kDeku, 0}));
  CHECK_FALSE(empty.write({kDeku, 0, protocol::FlagKind::uint32, kReady}));
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30000000u);  // nothing queued
  // Bool and Int flags read as before with Enum flags opted in.
  const auto enums = story();
  GameFlagStore store(w, &enums);
  CHECK(store.read({0x11, 0}) == protocol::FlagEntry{0x11, 0, protocol::FlagKind::boolean, 0});
  CHECK(store.read({0x22, 0})->kind == protocol::FlagKind::int32);
  CHECK_FALSE(store.write({0x22, 0, protocol::FlagKind::uint32, 1}));  // not an Enum flag
}

TEST_CASE("game flag store: an opted-in Enum flag reads and writes its value's hash") {
  auto w = game_with_enums();
  const auto enums = story();
  GameFlagStore store(w, &enums);
  CHECK(store.read({kDeku, 0}) ==
        protocol::FlagEntry{kDeku, 0, protocol::FlagKind::uint32, kNotReady});
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kStep02}));
  CHECK(coop::host::read_u32(w.mem, kEnumQueue) == kStep02);
  CHECK(store.read({kDeku, 0})->value == kStep02);

  // Refused: a value not in the flag's set, another kind, an index.
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::uint32, coop::host::flag_hash("Step04")}));
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::uint32, 3}));  // an index is not a value
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::int32, kComplete}));
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::boolean, 1}));
  CHECK_FALSE(store.write({kDeku, 1, protocol::FlagKind::uint32, kComplete}));
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30100001u);  // still the one write
  CHECK(store.read({kDeku, 0})->value == kStep02);

  // A value the table holds that is not in the set (kForest holds 0) reads
  // as unknown: a wrong layout guess sends nothing.
  CHECK_FALSE(store.read({kForest, 0}));
  // Opted in but missing from the game's Enum table: unknown.
  coop::host::EnumFlags other = story();
  REQUIRE(other.add(0x77, {1, 2}));
  GameFlagStore store2(w, &other);
  CHECK_FALSE(store2.read({0x77, 0}));
  CHECK_FALSE(store2.write({0x77, 0, protocol::FlagKind::uint32, 1}));
}

TEST_CASE("game flag store: an Enum flag goes through FLAG_SET and FLAG_STATE as uint32") {
  // Two games, each with an opted-in story list: A is a player, H the host.
  auto list = coop::host::SharedFlags::parse(
      "enum:Step_Meet_DekuTree NotReady Ready Step01 Step02 Step03 Complete\n");
  REQUIRE(list);
  auto a = game_with_enums();
  auto h = game_with_enums();
  GameFlagStore store_a(a, &list->enums());
  GameFlagStore store_h(h, &list->enums());
  coop::host::FlagWatcher player(*list, coop::host::FlagRole::player);
  coop::host::FlagWatcher host(*list, coop::host::FlagRole::host);
  CHECK(player.frame(store_a).empty());  // baselines
  CHECK(host.frame(store_h).empty());

  // A's game moves the story on, as its own setter would.
  REQUIRE(GameFlags::set_enum(a, kDeku, kStep02));
  auto out = player.frame(store_a);
  REQUIRE(out.size() == 1);
  protocol::Message m;
  REQUIRE(protocol::decode(out[0].bytes, m) == protocol::Error::none);
  const auto* set = std::get_if<protocol::FlagSet>(&m);
  REQUIRE(set);
  REQUIRE(set->flags.size() == 1);
  CHECK(set->flags[0] == protocol::FlagEntry{kDeku, 0, protocol::FlagKind::uint32, kStep02});

  // The host writes it into its game and answers FLAG_STATE with the same.
  CHECK(host.receive(out[0].bytes));
  out = host.frame(store_h);
  CHECK(GameFlags(h).get_enum(kDeku) == kStep02);
  REQUIRE(out.size() == 1);
  REQUIRE(protocol::decode(out[0].bytes, m) == protocol::Error::none);
  const auto* state = std::get_if<protocol::FlagState>(&m);
  REQUIRE(state);
  REQUIRE(state->flags.size() == 1);
  CHECK(state->flags[0] == protocol::FlagEntry{kDeku, 0, protocol::FlagKind::uint32, kStep02});

  // The player applies the answer; no echo.
  CHECK(player.receive(out[0].bytes));
  CHECK(player.frame(store_a).empty());
  CHECK(player.pending() == 0);

  // A FLAG_STATE with a value outside the set is not written into the game.
  const auto bad = protocol::encode(protocol::FlagState{
      0, {{kDeku, 0, protocol::FlagKind::uint32, coop::host::flag_hash("Step04")}}});
  REQUIRE(bad);
  CHECK(player.receive(*bad));
  player.frame(store_a);
  CHECK(player.stats().write_failures >= 1);
  CHECK(GameFlags(a).get_enum(kDeku) == kStep02);
}

namespace {

// kDeku opted in as a story step (only forward), in the game's order.
coop::host::EnumFlags steps() {
  coop::host::EnumFlags e;
  e.add(kDeku,
        {kNotReady, kReady, coop::host::flag_hash("Step01"), kStep02,
         coop::host::flag_hash("Step03"), kComplete},
        true);
  return e;
}

}  // namespace

TEST_CASE("game flag store: a story step never goes back") {
  auto w = game_with_enums();
  const auto enums = steps();
  GameFlagStore store(w, &enums);
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kStep02}));  // forward: queued
  CHECK(GameFlags(w).get_enum(kDeku) == kStep02);
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30100001u);
  // Back (Ready) or the same (Step02): taken as done, nothing queued.
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kReady}));
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kStep02}));
  CHECK(coop::host::read_u32(w.mem, kMgr + 0x280) == 0x30100001u);
  CHECK(GameFlags(w).get_enum(kDeku) == kStep02);
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kComplete}));
  CHECK(GameFlags(w).get_enum(kDeku) == kComplete);
  // Still refused: a value not in the set, another kind.
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::uint32, coop::host::flag_hash("Step04")}));
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::int32, kComplete}));
  // A plain Enum (not steps) goes back when told to.
  auto w2 = game_with_enums();
  const auto plain = story();
  GameFlagStore store2(w2, &plain);
  CHECK(store2.write({kDeku, 0, protocol::FlagKind::uint32, kComplete}));
  CHECK(store2.write({kDeku, 0, protocol::FlagKind::uint32, kReady}));
  CHECK(GameFlags(w2).get_enum(kDeku) == kReady);
}

TEST_CASE("game flag store: a later story step wins between a player and the host") {
  // The player's game is at Complete, the host's at Step01: the host's
  // snapshot does not move the player back, and the player's Complete ends
  // up in both games.
  coop::host::SharedFlags list({}, steps());
  auto a = game_with_enums();
  auto h = game_with_enums();
  const auto enums = steps();
  GameFlagStore store_a(a, &enums);
  GameFlagStore store_h(h, &enums);
  coop::host::FlagWatcher player(list, coop::host::FlagRole::player);
  coop::host::FlagWatcher host(list, coop::host::FlagRole::host);
  a.mem.put_u32(kEnumEntries + 0x1c, kComplete);
  h.mem.put_u32(kEnumEntries + 0x1c, coop::host::flag_hash("Step01"));
  CHECK(player.frame(store_a).empty());
  CHECK(host.frame(store_h).empty());

  // The host's whole table comes to the player.
  host.request_snapshot();
  auto out = host.frame(store_h);
  REQUIRE_FALSE(out.empty());
  for (const auto& o : out) CHECK(player.receive(o.bytes));
  auto back = player.frame(store_a);  // written as done: the game keeps Complete
  CHECK(GameFlags(a).get_enum(kDeku) == kComplete);
  if (back.empty()) back = player.frame(store_a);  // the next read sees Complete again
  REQUIRE(back.size() == 1);
  protocol::Message m;
  REQUIRE(protocol::decode(back[0].bytes, m) == protocol::Error::none);
  const auto* set = std::get_if<protocol::FlagSet>(&m);
  REQUIRE(set);
  REQUIRE(set->flags.size() == 1);
  CHECK(set->flags[0] == protocol::FlagEntry{kDeku, 0, protocol::FlagKind::uint32, kComplete});

  // The host takes it (forward for it) and answers Complete.
  CHECK(host.receive(back[0].bytes));
  out = host.frame(store_h);
  CHECK(GameFlags(h).get_enum(kDeku) == kComplete);
  REQUIRE(out.size() == 1);
  CHECK(player.receive(out[0].bytes));
  CHECK(player.frame(store_a).empty());
  CHECK(GameFlags(a).get_enum(kDeku) == kComplete);

  // A player at an earlier step asking the host to go back is answered with
  // the host's later one.
  auto b = game_with_enums();
  GameFlagStore store_b(b, &enums);
  coop::host::FlagWatcher late(list, coop::host::FlagRole::player);
  CHECK(late.frame(store_b).empty());  // NotReady
  REQUIRE(GameFlags::set_enum(b, kDeku, kReady));
  auto ask = late.frame(store_b);
  REQUIRE(ask.size() == 1);
  CHECK(host.receive(ask[0].bytes));
  out = host.frame(store_h);
  CHECK(GameFlags(h).get_enum(kDeku) == kComplete);
  REQUIRE(out.size() == 1);
  REQUIRE(protocol::decode(out[0].bytes, m) == protocol::Error::none);
  const auto* state = std::get_if<protocol::FlagState>(&m);
  REQUIRE(state);
  REQUIRE(state->flags.size() == 1);
  CHECK(state->flags[0].value == kComplete);
  CHECK(late.receive(out[0].bytes));
  late.frame(store_b);
  CHECK(GameFlags(b).get_enum(kDeku) == kComplete);
}

TEST_CASE("game flag store: the host's save load puts a story step back, exactly (REVISION_HISTORIA H1)") {
  // docs/flags.md "Saves": a FLAG_RESET is written exactly, with no max rule.
  // The step rule kept the later step, so the group's Bools went back with
  // the host's save (the Deku Tree's mouth shut) while its step stayed at
  // Complete, and the player's next diff sent Complete back to the host.
  auto w = game_with_enums();
  const auto enums = steps();
  GameFlagStore store(w, &enums);
  REQUIRE(store.write({kDeku, 0, protocol::FlagKind::uint32, kComplete}));
  CHECK(store.write_exact({kDeku, 0, protocol::FlagKind::uint32, kReady}));
  CHECK(GameFlags(w).get_enum(kDeku) == kReady);
  // Exact is no licence for garbage: a value outside the set, another kind.
  CHECK_FALSE(store.write_exact({kDeku, 0, protocol::FlagKind::uint32, coop::host::flag_hash("Step04")}));
  CHECK_FALSE(store.write_exact({kDeku, 0, protocol::FlagKind::int32, kComplete}));
  CHECK(GameFlags(w).get_enum(kDeku) == kReady);

  // As the hook chains the stores: a player at Complete takes the host's
  // FLAG_RESET to Ready, and sends nothing back.
  coop::host::SharedFlags list({}, steps());
  auto a = game_with_enums();
  GameFlagStore game_a(a, &enums);
  coop::host::ItemFlagStore items(game_a);
  coop::host::FlagWatcher player(list, coop::host::FlagRole::player);
  a.mem.put_u32(kEnumEntries + 0x1c, kComplete);
  CHECK(player.frame(items).empty());  // the baseline
  const auto reset =
      protocol::encode(protocol::FlagReset{0, {{kDeku, 0, protocol::FlagKind::uint32, kReady}}});
  REQUIRE(reset);
  CHECK(player.receive(*reset));
  CHECK(player.frame(items).empty());
  CHECK(GameFlags(a).get_enum(kDeku) == kReady);
  CHECK(player.frame(items).empty());  // no Complete sent back to the host
  CHECK(player.stats().reset_applied == 1);
}

TEST_CASE("game flag store: an Enum value the game's own list lacks is never written (REVISION_HISTORIA H3)") {
  // The list's set is ours (a user's LICHII_COOP_STORY_LIST, an enum: line):
  // a value it has and the game's entry (+0x28/+0x30) does not would put a
  // hash the game's flows never test into the save.
  auto w = game_with_enums();
  coop::host::EnumFlags enums;
  const std::uint32_t step04 = coop::host::flag_hash("Step04");
  REQUIRE(enums.add(kDeku, {kNotReady, kReady, kStep02, step04, kComplete}, true));
  GameFlagStore store(w, &enums);
  CHECK(store.write({kDeku, 0, protocol::FlagKind::uint32, kStep02}));
  CHECK_FALSE(store.write({kDeku, 0, protocol::FlagKind::uint32, step04}));
  CHECK_FALSE(store.write_exact({kDeku, 0, protocol::FlagKind::uint32, step04}));
  CHECK(GameFlags(w).get_enum(kDeku) == kStep02);
  // A game entry with no readable list (kForest): the list's set alone, as before.
  coop::host::EnumFlags forest;
  REQUIRE(forest.add(kForest, {coop::host::flag_hash("Step01"), coop::host::flag_hash("Step08")}));
  GameFlagStore store2(w, &forest);
  CHECK(store2.write({kForest, 0, protocol::FlagKind::uint32, coop::host::flag_hash("Step08")}));
}

TEST_CASE("flag defer: on the game's queue, a held talk flag is queued only once nobody sees it") {
  // As the hook chains them: FlagWatcher -> DeferringFlagStore -> ItemFlagStore
  // -> GameFlagStore over the game's memory. Bool 0x11 stands for Mido's
  // blocker flag, Bool 0x18 for a door.
  auto w = game();
  GameFlagStore game_store(w);
  coop::host::ItemFlagStore items(game_store);
  auto rules = coop::host::DeferRules::parse("0x11 scene:KokiriForest actor:Npc_Kokiri_Mido_Child\n");
  REQUIRE(rules);
  coop::host::FlagDeferral deferral(std::move(*rules));
  coop::host::FlagWatcher watcher(coop::host::SharedFlags({{0x11, 0}, {0x18, 0}}),
                                  coop::host::FlagRole::player);
  coop::host::DeferView view;
  view.scene = "KokiriForest_Child";
  view.actors = std::vector<coop::host::SeenActor>{{"Npc_Kokiri_Mido_Child", std::nullopt, false}};
  auto frame = [&] {
    deferral.tick(items, view);
    coop::host::DeferringFlagStore store(deferral, items, view);
    return watcher.frame(store);
  };
  CHECK(frame().empty());
  CHECK(watcher.receive(protocol::FlagState{1, {{{0x11, 0}, protocol::FlagKind::boolean, 1},
                                                {{0x18, 0}, protocol::FlagKind::boolean, 1}}}));
  CHECK(frame().empty());
  GameFlags flags(w);
  // The door went into the game's queue at once; the talk flag did not.
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 4) == 0x18u);
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == 0u);
  CHECK(flags.get_bool(0x18) == true);
  CHECK(flags.get_bool(0x11) == false);
  CHECK(frame().empty());
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == 0u);

  view.scene = "LostWood_Child";  // our player left the forest
  CHECK(frame().empty());
  CHECK(coop::host::read_u8(w.mem, kBoolQueue + 8) == 1);
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == 0x11u);
  CHECK(flags.get_bool(0x11) == true);
  CHECK(deferral.held() == 0);
  CHECK(frame().empty());
}

// ORM shared talk list (tools/release/kit/shared_talk.txt, LICHII_COOP_TALK):
// the kit's own file, as the hook uses it. Its flags are shared (FlagWatcher's
// list) and its rules go over the built-in ones; the game's Bool table holds
// Mido's blocker (1: he blocks) and a door (0x18, no rule).
TEST_CASE("talk list: the kit's Mido blocker waits while Mido is loaded and is queued after") {
  std::string error;
  const auto talk = coop::host::DeferRules::load(
      COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  REQUIRE_MESSAGE(talk, error);
  const std::uint32_t mido = coop::host::flag_hash("KokiriForest_Npc_Kokiri_Mido_Child_Blocker");
  REQUIRE(mido == 0x502f6ceau);
  auto w = game();
  table(w.mem, 0x10, kBoolEntries, kBoolSlots, 0x18, {mido, 0x18});
  queue(w.mem, 0x10, kBoolQueue, 8);
  w.mem.put_u8(kBoolEntries + 0x16, 1);  // Mido blocks
  GameFlagStore game_store(w);
  coop::host::ItemFlagStore items(game_store);
  auto rules = coop::host::default_defer_rules();
  rules.merge(*talk);
  coop::host::FlagDeferral deferral(std::move(rules));
  auto keys = talk->keys();
  keys.push_back({0x18, 0});
  coop::host::FlagWatcher watcher(coop::host::SharedFlags(keys), coop::host::FlagRole::player);
  coop::host::FlagChanges log(talk->keys());
  coop::host::DeferView view;
  view.scene = "KokiriForest_Child";
  view.link = protocol::Vec3{20, 111, 0};
  view.now_us = 1'000'000;
  // Mido 5.5 m from our Link, moved by this game (not a copy).
  view.actors = std::vector<coop::host::SeenActor>{
      {"Npc_Kokiri_Mido_Child", protocol::Vec3{25.5f, 111.66f, 0.84f}, false},
      {"FldObj_AirWall_Player_GenerateNavMesh", std::nullopt, false}};
  auto frame = [&] {
    deferral.tick(items, view);
    coop::host::DeferringFlagStore store(deferral, items, view);
    return watcher.frame(store);
  };
  CHECK(frame().empty());
  CHECK(log.read(game_store).empty());  // the first read only records
  // Another player's talk: Mido lets him pass, and a door opened.
  CHECK(watcher.receive(protocol::FlagState{1, {{{mido, 0}, protocol::FlagKind::boolean, 0},
                                                {{0x18, 0}, protocol::FlagKind::boolean, 1}}}));
  CHECK(frame().empty());  // nothing sent back
  GameFlags flags(w);
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 4) == 0x18u);  // the door at once
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == 0u);  // Mido waits
  CHECK(flags.get_bool(mido) == true);
  CHECK(deferral.held() == 1);
  auto notes = deferral.take_notes();
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].why == coop::host::DeferWhy::held);
  CHECK(deferral.rules().name({mido, 0}) == "KokiriForest_Npc_Kokiri_Mido_Child_Blocker");
  CHECK(log.read(game_store).empty());  // the game still has 1

  // Still loaded and near a minute later: still waiting.
  view.now_us += 60'000'000;
  CHECK(frame().empty());
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == 0u);
  CHECK(deferral.take_notes().empty());

  // Mido unloaded (our player walked away, the walk no longer lists him): queued.
  view.actors = std::vector<coop::host::SeenActor>{{"FldObj_AirWall_Player_GenerateNavMesh", std::nullopt, false}};
  CHECK(frame().empty());
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 8 + 4) == mido);
  CHECK(coop::host::read_u8(w.mem, kBoolQueue + 8) == 0);
  CHECK(flags.get_bool(mido) == false);
  CHECK(deferral.held() == 0);
  notes = deferral.take_notes();
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].why == coop::host::DeferWhy::actors_gone);
  const auto changes = log.read(game_store);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].key.hash == mido);
  CHECK(changes[0].before->value == 1u);
  CHECK(changes[0].now.value == 0u);
  CHECK(frame().empty());  // and nothing goes back to the others
}

TEST_CASE("talk list: Mido as a copy another game moves releases at once, and a host save load is not held") {
  std::string error;
  const auto talk = coop::host::DeferRules::load(
      COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  REQUIRE_MESSAGE(talk, error);
  const std::uint32_t mido = coop::host::flag_hash("KokiriForest_Npc_Kokiri_Mido_Child_Blocker");
  auto w = game();
  table(w.mem, 0x10, kBoolEntries, kBoolSlots, 0x18, {mido, 0x18});
  queue(w.mem, 0x10, kBoolQueue, 8);
  w.mem.put_u8(kBoolEntries + 0x16, 1);
  GameFlagStore game_store(w);
  coop::host::ItemFlagStore items(game_store);
  coop::host::FlagDeferral deferral(*talk);
  coop::host::DeferView view;
  view.scene = "KokiriForest_Child";
  view.link = protocol::Vec3{20, 111, 0};
  view.actors = std::vector<coop::host::SeenActor>{
      {"Npc_Kokiri_Mido_Child", protocol::Vec3{25.5f, 111.66f, 0.84f}, true}};  // A talks: A owns him
  {
    coop::host::DeferringFlagStore store(deferral, items, view);
    CHECK(store.write({{mido, 0}, protocol::FlagKind::boolean, 0}));
  }
  CHECK(deferral.held() == 0);
  CHECK(coop::host::read_u32(w.mem, kBoolQueue + 4) == mido);
  REQUIRE(deferral.take_notes().empty());  // passed, not held

  // Held while ours; the host's save load (write_exact) goes through and drops it.
  view.actors = std::vector<coop::host::SeenActor>{
      {"Npc_Kokiri_Mido_Child", protocol::Vec3{25.5f, 111.66f, 0.84f}, false}};
  auto w2 = game();
  table(w2.mem, 0x10, kBoolEntries, kBoolSlots, 0x18, {mido, 0x18});
  queue(w2.mem, 0x10, kBoolQueue, 8);
  w2.mem.put_u8(kBoolEntries + 0x16, 1);
  GameFlagStore game2(w2);
  coop::host::ItemFlagStore items2(game2);
  coop::host::DeferringFlagStore store2(deferral, items2, view);
  CHECK(store2.write({{mido, 0}, protocol::FlagKind::boolean, 0}));
  CHECK(deferral.held() == 1);
  CHECK(store2.read({mido, 0})->value == 0u);  // reads back as written
  // The save's 0 goes into the game's queue now (a plain write would hold it).
  CHECK(store2.write_exact({{mido, 0}, protocol::FlagKind::boolean, 0}));
  CHECK(deferral.held() == 0);
  CHECK(coop::host::read_u32(w2.mem, kBoolQueue + 4) == mido);
  CHECK(GameFlags(w2).get_bool(mido) == false);
}
