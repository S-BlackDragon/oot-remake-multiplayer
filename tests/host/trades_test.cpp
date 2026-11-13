// ORM Biggoron's trade step (coop/host/trades.hpp, oot-remake docs/BOTELLAS_TRUEQUES.md
// section 6.2): a step back from another game never reaches a game that holds a timed
// trade item; the holder's own step back, a step forward and the host's save do.
// The game is a fake flag store; the watcher is the real one.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/trades.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    if (!values.contains(e.key)) return false;
    values[e.key] = e;
    return true;
  }
};

std::uint32_t step(std::string_view name) { return flag_hash(name); }
FlagKey step_key() { return FlagKey(trade::step_flag(), 0); }
FlagKey mushroom() { return FlagKey(flag_hash("PouchData.Item_OddMushroom.StockNum"), 0); }
FlagKey frog() { return FlagKey(flag_hash("PouchData.Item_EyeballFrog.StockNum"), 0); }
FlagKey drops() { return FlagKey(flag_hash("PouchData.Item_EyeDrop.StockNum"), 0); }
FlagKey give_up04() { return FlagKey(flag_hash("GetSwordBigGoron_Step04_GiveUp"), 0); }

proto::FlagEntry enum_value(std::string_view value) {
  return {step_key(), proto::FlagKind::uint32, step(value)};
}
proto::FlagEntry integer(FlagKey key, std::int32_t v) {
  return {key, proto::FlagKind::int32, static_cast<std::uint32_t>(v)};
}
proto::FlagEntry boolean(FlagKey key, std::uint32_t v) { return {key, proto::FlagKind::boolean, v}; }

// A game at `at` holding `mush` Odd Mushrooms.
FakeStore game(std::string_view at, std::int32_t mush) {
  FakeStore s;
  s.values[step_key()] = enum_value(at);
  s.values[mushroom()] = integer(mushroom(), mush);
  s.values[frog()] = integer(frog(), 0);
  s.values[drops()] = integer(drops(), 0);
  s.values[give_up04()] = boolean(give_up04(), 0);
  return s;
}

bool has(const std::vector<std::string>& lines, std::string_view part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("trade step: the chain's values are the minigame list's, in order") {
  const auto v = trade::step_values();
  REQUIRE(v.size() == 27);
  CHECK(v.front().first == "NotReady");
  CHECK(v.back().first == "Complete");
  CHECK(trade::step_flag() == flag_hash("Step_SubEvent_GetSwordBigGoron"));
  CHECK(*trade::step_rank(step("Step04")) == 12);
  CHECK(*trade::step_rank(step("Step03_03")) == 11);
  CHECK(*trade::step_rank(step("Step10")) == 24);
  CHECK_FALSE(trade::step_rank(0x1234u).has_value());
  CHECK(trade::step_name(0x1234u) == "0x00001234");
  std::set<std::uint32_t> seen;
  for (const auto& [name, hash] : v) {
    CHECK(hash == flag_hash(name));
    CHECK(seen.insert(hash).second);
  }
  const auto items = trade::timed_items();
  REQUIRE(items.size() == 3);
  CHECK(items[0].stock == mushroom().hash);
  CHECK(items[0].step == "Step04");
  CHECK(items[1].step == "Step09");
  CHECK(items[2].step == "Step10");
}

TEST_CASE("trade step: a step back waits while our game holds the mushroom") {
  FakeStore g = game("Step04", 1);
  trade::TradeGuard guard;
  trade::TradeStepStore store(g, guard);
  CHECK_FALSE(store.write(enum_value("Step03_03")));
  CHECK(g.values[step_key()].value == step("Step04"));
  const auto notes = guard.take_notes();
  CHECK(has(notes, "Step04 -> Step03_03 from another game refused: our game holds the Odd Mushroom"));
  CHECK(guard.waiting());
  // The same value again: no second line.
  CHECK_FALSE(store.write(enum_value("Step03_03")));
  CHECK(guard.take_notes().empty());
  CHECK(guard.refused() == 2);
  // The GiveUp of Step04 waits too.
  CHECK_FALSE(store.write(boolean(give_up04(), 1)));
  CHECK(has(guard.take_notes(), "GetSwordBigGoron_Step04_GiveUp = 1 from another game refused"));
  CHECK(g.values[give_up04()].value == 0);
}

TEST_CASE("trade step: what passes") {
  SUBCASE("forward while holding") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(enum_value("Step05")));
    CHECK(g.values[step_key()].value == step("Step05"));
    CHECK(guard.take_notes().empty());
  }
  SUBCASE("back with no timed item here: the holder's item spoiled there") {
    FakeStore g = game("Step04", 0);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(enum_value("Step03_03")));
    CHECK(g.values[step_key()].value == step("Step03_03"));
  }
  SUBCASE("the host's save load") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write_exact(enum_value("Step01")));
    CHECK(g.values[step_key()].value == step("Step01"));
  }
  SUBCASE("a GiveUp of another step than the held item's") {
    FakeStore g = game("Step04", 0);
    g.values[frog()] = integer(frog(), 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(boolean(give_up04(), 1)));
  }
  SUBCASE("off") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard(false);
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(enum_value("Step03_03")));
  }
  SUBCASE("unreadable counts: written as it came, said once") {
    FakeStore g = game("Step04", 1);
    g.values.erase(mushroom());
    g.values.erase(frog());
    g.values.erase(drops());
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(enum_value("Step03_03")));
    CHECK(has(guard.take_notes(), "cannot be read here"));
    CHECK(store.write(enum_value("Step03_02")));
    CHECK(guard.take_notes().empty());
  }
  SUBCASE("an unknown step value: written as it came") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write({step_key(), proto::FlagKind::uint32, 0x1234u}));
  }
  SUBCASE("other flags") {
    FakeStore g = game("Step04", 1);
    const FlagKey other(0x99u, 0);
    g.values[other] = boolean(other, 0);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK(store.write(boolean(other, 1)));
  }
}

TEST_CASE("trade step: the wait ends") {
  SUBCASE("our hand-over moves the step on: the other's is dropped") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK_FALSE(store.write(enum_value("Step03_03")));
    guard.take_notes();
    g.values[step_key()] = enum_value("Step05");
    g.values[mushroom()] = integer(mushroom(), 0);
    guard.frame(g, 1'000'000);
    CHECK(has(guard.take_notes(), "our game moved Biggoron's step itself (Step04 -> Step05)"));
    CHECK_FALSE(guard.waiting());
  }
  SUBCASE("the item went with no step of ours: written after the grace") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    std::uint64_t t = 1'000'000;
    guard.frame(g, t);
    CHECK_FALSE(store.write(enum_value("Step03_03")));
    guard.take_notes();
    g.values[mushroom()] = integer(mushroom(), 0);
    guard.frame(g, t += 16'000);
    CHECK_FALSE(store.write(enum_value("Step03_03")));  // the grace: our step may come next
    guard.frame(g, t += trade::TradeGuard::kGraceUs);
    CHECK(store.write(enum_value("Step03_03")));
    guard.frame(g, t += 16'000);
    CHECK(has(guard.take_notes(), "Step03_03 from another game written after 2 refusals"));
    CHECK_FALSE(guard.waiting());
  }
  SUBCASE("a hand-over in two writes: the count first, the step a frame later") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    std::uint64_t t = 1'000'000;
    guard.frame(g, t);
    CHECK_FALSE(store.write(enum_value("Step03_03")));
    guard.take_notes();
    g.values[mushroom()] = integer(mushroom(), 0);
    guard.frame(g, t += 16'000);
    CHECK_FALSE(store.write(enum_value("Step03_03")));  // never under our coming Step05
    g.values[step_key()] = enum_value("Step05");
    guard.frame(g, t += 16'000);
    CHECK(has(guard.take_notes(), "our game moved Biggoron's step itself (Step04 -> Step05)"));
    CHECK(g.values[step_key()].value == step("Step05"));
  }
  SUBCASE("no longer asked for") {
    FakeStore g = game("Step04", 1);
    trade::TradeGuard guard;
    trade::TradeStepStore store(g, guard);
    CHECK_FALSE(store.write(enum_value("Step03_03")));
    guard.take_notes();
    for (std::uint32_t i = 0; i <= trade::TradeGuard::kIdleFrames; ++i) guard.frame(g, 1'000'000 + i * 16'000ull);
    CHECK(has(guard.take_notes(), "no longer waits"));
    CHECK_FALSE(guard.waiting());
  }
}

TEST_CASE("trade step: with the real watcher, the holder keeps Step04 and the mushroom") {
  // A (player 1, the holder) and B (player 2): B's game puts the step back (it
  // talked to the old woman without the mushroom). The step reaches A as the
  // host's FLAG_STATE; A's guard refuses it, the watcher keeps it and A's next own
  // change (the hand-over) drops it.
  EnumFlags enums;
  std::vector<std::uint32_t> values;
  for (const auto& [name, hash] : trade::step_values()) values.push_back(hash);
  REQUIRE(enums.add(trade::step_flag(), values));
  SharedFlags shared({step_key()}, enums);
  FlagWatcher a(shared, FlagRole::player);
  FakeStore ga = game("Step04", 1);
  trade::TradeGuard guard;
  trade::TradeStepStore sa(ga, guard);
  (void)a.frame(sa);  // first look
  a.receive(proto::Message{proto::FlagState{2, {enum_value("Step03_03")}}});
  (void)a.frame(sa);
  CHECK(ga.values[step_key()].value == step("Step04"));
  CHECK(guard.refused() >= 1);
  (void)a.frame(sa);  // tried again: still refused
  CHECK(ga.values[step_key()].value == step("Step04"));
  // A hands the mushroom over: its own step moves on and goes out; B's is dropped.
  ga.values[step_key()] = enum_value("Step05");
  ga.values[mushroom()] = integer(mushroom(), 0);
  const auto out = a.frame(sa);
  guard.frame(ga, 2'000'000);
  CHECK(ga.values[step_key()].value == step("Step05"));
  bool sent = false;
  for (const auto& o : out) {
    proto::Message m;
    REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
    if (const auto* set = std::get_if<proto::FlagSet>(&m)) {
      for (const auto& e : set->flags) sent = sent || (e.key == step_key() && e.value == step("Step05"));
    }
  }
  CHECK(sent);
  CHECK_FALSE(guard.waiting());
  // Mutant: without the guard the step back is written under the mushroom.
  FlagWatcher m(shared, FlagRole::player);
  FakeStore gm = game("Step04", 1);
  (void)m.frame(gm);
  m.receive(proto::Message{proto::FlagState{2, {enum_value("Step03_03")}}});
  (void)m.frame(gm);
  CHECK(gm.values[step_key()].value == step("Step03_03"));
}
