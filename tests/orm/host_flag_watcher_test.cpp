// ORM cases moved out of upstream's tests/host/flag_watcher_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results
#include <algorithm>
#include <cstddef>

#include <map>
#include <set>
#include <variant>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/game_items.hpp"
#include "coop/protocol/codec.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr FlagKey kChest{0x1000u, 0};
constexpr FlagKey kDoor{0x2000u, 0};
constexpr FlagKey kKey0{0x3000u, 0};
constexpr FlagKey kKey1{0x3000u, 1};
constexpr FlagKey kHearts{0x4000u, 0};  // in the game, not shared

proto::FlagEntry entry(FlagKey key, std::uint32_t value,
                       proto::FlagKind kind = proto::FlagKind::boolean) {
  return {key, kind, value};
}

// The game's flags, as the emulator would give them.
class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  std::set<FlagKey> unreadable;
  std::set<FlagKey> unwritable;
  int writes = 0;

  void set(FlagKey key, std::uint32_t value) { values[key] = entry(key, value); }
  std::uint32_t get(FlagKey key) const { return values.at(key).value; }

  std::optional<proto::FlagEntry> read(FlagKey key) override {
    if (unreadable.contains(key)) return std::nullopt;
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    const FlagKey key = key_of(e);
    if (unwritable.contains(key) || !values.contains(key)) return false;
    ++writes;
    values[key] = e;
    return true;
  }
};

SharedFlags shared() { return SharedFlags({kChest, kDoor, kKey0, kKey1}); }

FakeStore game() {
  FakeStore store;
  for (FlagKey k : {kChest, kDoor, kKey0, kKey1, kHearts}) store.set(k, 0);
  return store;
}

std::vector<proto::Message> decode(const std::vector<Outgoing>& out) {
  std::vector<proto::Message> messages;
  for (const auto& o : out) {
    proto::Message m;
    REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
    CHECK(proto::check_received(m, proto::Sender::client, o.channel) == proto::Error::none);
    CHECK(o.channel == proto::kChannelProgress);
    messages.push_back(std::move(m));
  }
  return messages;
}

template <typename M>
std::vector<proto::FlagEntry> only(const std::vector<Outgoing>& out) {
  const auto messages = decode(out);
  REQUIRE(messages.size() == 1);
  const auto* m = std::get_if<M>(&messages[0]);
  REQUIRE(m);
  return m->flags;
}

proto::FlagState state(std::vector<proto::FlagEntry> flags) { return {1, std::move(flags)}; }

constexpr FlagKey kTokens{0x04735978u, 0};

// The game's flags with the token count, a count as ItemFlagStore makes it.
class TokenStore : public FakeStore {
 public:
  bool is_count(FlagKey key) override { return key == kTokens; }
  bool write(const proto::FlagEntry& e) override {
    if (e.kind != proto::FlagKind::count_delta) return FakeStore::write(e);
    const FlagKey key = key_of(e);
    if (!values.contains(key)) return false;
    proto::FlagEntry now = values[key];
    now.value = static_cast<std::uint32_t>(static_cast<std::int32_t>(now.value) +
                                           static_cast<std::int32_t>(e.value));
    return FakeStore::write(now);
  }
  void tokens(std::int32_t n) {
    values[kTokens] = proto::FlagEntry{kTokens, proto::FlagKind::int32, static_cast<std::uint32_t>(n)};
  }
  std::int32_t tokens() const { return static_cast<std::int32_t>(values.at(kTokens).value); }
};

bool has(const std::vector<proto::FlagEntry>& all, const proto::FlagEntry& e) {
  return std::find(all.begin(), all.end(), e) != all.end();
}

}  // namespace

TEST_CASE("flag watcher: a quiet flag's fake value is never sent, and incoming waits for it") {
  FakeStore store = game();
  FlagWatcher w(shared(), FlagRole::player);
  w.frame(store);
  w.set_quiet({kKey0});
  store.set(kKey0, 1);  // a revive's fake bottle
  CHECK(w.frame(store).empty());
  // Another player's value for it is held, not written over the fake.
  CHECK(w.receive(proto::Message{state({entry(kKey0, 1), entry(kDoor, 1)})}));
  w.frame(store);
  CHECK(store.get(kDoor) == 1);
  CHECK(w.stats().quiet_held == 1);
  store.set(kKey0, 0);  // the real value back
  CHECK(w.frame(store).empty());
  // Quiet over: the held value is written, and nothing goes out.
  w.set_quiet({});
  CHECK(w.frame(store).empty());
  CHECK(store.get(kKey0) == 1);
  CHECK(w.frame(store).empty());
  // A real change after the quiet goes out as usual.
  store.set(kKey0, 0);
  CHECK(only<proto::FlagSet>(w.frame(store)) == std::vector{entry(kKey0, 0)});
}

TEST_CASE("flag watcher: without the quiet set, the fake would have gone out") {
  FakeStore store = game();
  FlagWatcher w(shared(), FlagRole::player);
  w.frame(store);
  store.set(kKey0, 1);
  CHECK(only<proto::FlagSet>(w.frame(store)) == std::vector{entry(kKey0, 1)});
}

TEST_CASE("flag watcher: a host's snapshot gives a quiet flag's last real value") {
  FakeStore store = game();
  FlagWatcher w(shared(), FlagRole::host);
  w.frame(store);
  w.set_quiet({kKey0});
  store.set(kKey0, 1);
  w.request_snapshot();
  const auto flags = only<proto::FlagSnapshot>(w.frame(store));
  const auto it = std::find_if(flags.begin(), flags.end(),
                               [](const proto::FlagEntry& e) { return e.key == kKey0; });
  REQUIRE(it != flags.end());
  CHECK(it->value == 0);
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): a chest our game
// opens second gives its counted find here too; expect_double_find leaves our
// gain out, so the group counts the find once (the first opener's).
namespace {

const FlagKey kSmallKey{proto::flag_hash("DungeonInfo.ForestTemple.SmallKeyNum"), 0};
const FlagKey kRupees{proto::flag_hash("PlayerStatus.CurrentRupee"), 0};
const FlagKey kHookshot{proto::flag_hash("IsGet.Item_Hookshot"), 0};

class CountStore : public FakeStore {
 public:
  bool is_count(FlagKey key) override { return key == kSmallKey || key == kRupees; }
  bool write(const proto::FlagEntry& e) override {
    if (e.kind != proto::FlagKind::count_delta) return FakeStore::write(e);
    const FlagKey key = key_of(e);
    if (!values.contains(key)) return false;
    proto::FlagEntry now = values[key];
    now.value = static_cast<std::uint32_t>(static_cast<std::int32_t>(now.value) +
                                           static_cast<std::int32_t>(e.value));
    return FakeStore::write(now);
  }
  void count(FlagKey key, std::int32_t n) {
    values[key] = proto::FlagEntry{key, proto::FlagKind::int32, static_cast<std::uint32_t>(n)};
  }
  std::int32_t count(FlagKey key) const { return static_cast<std::int32_t>(values.at(key).value); }
};

CountStore count_game() {
  CountStore store;
  store.count(kSmallKey, 1);
  store.count(kRupees, 50);
  store.set(kHookshot, 0);
  return store;
}

proto::FlagEntry count_delta(FlagKey key, std::int32_t n) {
  return {key, proto::FlagKind::count_delta, static_cast<std::uint32_t>(n)};
}
proto::FlagEntry count_total(FlagKey key, std::int32_t n) {
  return {key, proto::FlagKind::int32, static_cast<std::uint32_t>(n)};
}

std::vector<proto::FlagEntry> sent_entries(const std::vector<Outgoing>& out) {
  std::vector<proto::FlagEntry> all;
  for (const auto& m : decode(out)) {
    if (const auto* s = std::get_if<proto::FlagSet>(&m)) all.insert(all.end(), s->flags.begin(), s->flags.end());
    if (const auto* s = std::get_if<proto::FlagState>(&m)) all.insert(all.end(), s->flags.begin(), s->flags.end());
  }
  return all;
}

bool sends(const std::vector<proto::FlagEntry>& all, FlagKey key) {
  return std::any_of(all.begin(), all.end(), [&](const proto::FlagEntry& e) { return e.key == key; });
}

}  // namespace

TEST_CASE("double find: a small key from a chest another player opened first is counted once") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kSmallKey, kRupees, kHookshot}), FlagRole::player);
  w.frame(store);
  CHECK(w.expect_double_find(kSmallKey));
  // Our Link opened it too: our game gives the key again.
  store.count(kSmallKey, 2);
  CHECK_FALSE(sends(sent_entries(w.frame(store)), kSmallKey));  // not sent
  CHECK(store.count(kSmallKey) == 1);                            // and put back
  const auto undone = w.take_doubles_undone();
  REQUIRE(undone.size() == 1);
  CHECK(undone[0].count == kSmallKey);
  CHECK(undone[0].amount == 1);
  CHECK_FALSE(undone[0].after_sent);
  // The first opener's key reaches us: one key for the group.
  w.receive(state({count_total(kSmallKey, 2)}));
  w.frame(store);
  CHECK(store.count(kSmallKey) == 2);
  // Disarmed: a key of our own later counts.
  store.count(kSmallKey, 3);
  CHECK(has(sent_entries(w.frame(store)), count_delta(kSmallKey, 1)));
}

TEST_CASE("double find: rupees of a shared wallet are left out once, a wallet of each player is not armed") {
  {
    CountStore store = count_game();
    FlagWatcher w(SharedFlags({kSmallKey, kRupees}), FlagRole::player);
    w.frame(store);
    CHECK(w.expect_double_find(kRupees));
    store.count(kRupees, 70);  // a red rupee (20) from the chest again
    CHECK_FALSE(sends(sent_entries(w.frame(store)), kRupees));
    CHECK(store.count(kRupees) == 50);
    const auto undone = w.take_doubles_undone();
    REQUIRE(undone.size() == 1);
    CHECK(undone[0].amount == 20);
  }
  {
    // The kit's wallet of each player (cartera-cada-uno): not shared, our own rupees stay ours.
    CountStore store = count_game();
    FlagWatcher w(SharedFlags({kSmallKey}), FlagRole::player);
    w.frame(store);
    CHECK_FALSE(w.expect_double_find(kRupees));
    store.count(kRupees, 70);
    w.frame(store);
    CHECK(store.count(kRupees) == 70);
    CHECK(w.take_doubles_undone().empty());
  }
}

TEST_CASE("double find: a unique item twice is the same item, nothing is undone") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kSmallKey, kHookshot}), FlagRole::player);
  w.frame(store);
  // The first opener's Hookshot comes; our second opening sets the same 1.
  w.receive(state({entry(kHookshot, 1)}));
  w.frame(store);
  CHECK(store.get(kHookshot) == 1);
  store.set(kHookshot, 1);
  CHECK(sent_entries(w.frame(store)).empty());
  CHECK(store.get(kHookshot) == 1);
  CHECK(w.take_doubles_undone().empty());
}

TEST_CASE("double find: a gain already sent before the CHEST_OPEN came is taken back once") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kSmallKey}), FlagRole::player);
  w.frame(store);
  store.count(kSmallKey, 2);
  CHECK(has(sent_entries(w.frame(store)), count_delta(kSmallKey, 1)));  // out already
  CHECK(w.expect_double_find(kSmallKey));                                 // then his CHEST_OPEN, earlier
  CHECK(has(sent_entries(w.frame(store)), count_delta(kSmallKey, -1)));
  CHECK(store.count(kSmallKey) == 1);
  const auto undone = w.take_doubles_undone();
  REQUIRE(undone.size() == 1);
  CHECK(undone[0].after_sent);
  CHECK(sent_entries(w.frame(store)).empty());  // once
}

TEST_CASE("double find: the arming ends after its window, a later gain counts") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kSmallKey}), FlagRole::player);
  w.frame(store);
  CHECK(w.expect_double_find(kSmallKey, 30));
  for (int f = 0; f < 40; ++f) w.frame(store);
  store.count(kSmallKey, 2);
  CHECK(has(sent_entries(w.frame(store)), count_delta(kSmallKey, 1)));
  CHECK(store.count(kSmallKey) == 2);
}
