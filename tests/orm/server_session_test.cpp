// ORM cases moved out of upstream's tests/server/session_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "../server/orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"
#include "world.hpp"

using namespace coop::protocol;
using namespace coop::server;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr const char* kGameBuild = "e0f3ccfaf50065ae";

Bytes bytes_of(const Message& m) {
  Bytes out;
  REQUIRE(encode(m, out) == Error::none);
  return out;
}

Hello hello_from(const std::string& nickname) {
  Hello h;
  h.build_id = "test 0.0";
  h.game_build_id = kGameBuild;
  h.game_version = "1.1.1";
  h.nickname = nickname;
  return h;
}

struct Sent {
  ConnectionId to;
  std::uint8_t channel;
  Message message;
};

// Runs the session and splits what it asked for into decoded sends and
// disconnects.
struct Harness {
  Session session;
  std::vector<Sent> sent;
  std::vector<ConnectionId> dropped;
  std::uint64_t now = 1'000'000;

  explicit Harness(SessionConfig config = {}) : session(orm_test::with_password(std::move(config))) {}

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        // What the server sends must pass a client's own check.
        CHECK(check_received(m, Sender::server, s->channel) == Error::none);
        sent.push_back({s->to, s->channel, m});
      } else if (auto* d = std::get_if<DisconnectAction>(&a)) {
        dropped.push_back(d->who);
      }  // EncryptAction (test10): the transport's, nothing for these tests
    }
  }

  void connect(ConnectionId id, std::optional<std::uint32_t> address = std::nullopt) {
    REQUIRE(session.connected(id, now, address));
    collect();
  }

  void receive(ConnectionId id, const Message& m, std::uint8_t channel) {
    session.received(id, channel, bytes_of(m), now);
    collect();
  }

  void receive_raw(ConnectionId id, const Bytes& b, std::uint8_t channel) {
    session.received(id, channel, b, now);
    collect();
  }

  // Connects and joins a player; returns the WELCOME.
  Welcome join(ConnectionId id, const std::string& nickname,
               std::optional<std::uint32_t> address = std::nullopt) {
    connect(id, address);
    orm_test::join(session, id, hello_from(nickname), now);
    collect();
    for (const Sent& s : to(id)) {
      if (const auto* w = std::get_if<Welcome>(&s.message)) return *w;
    }
    FAIL("no WELCOME");
    return {};
  }

  // ORM one host: HOST_CHOICE (the relay's word after each HOST) is left out
  // here, so upstream's counts and orders stay; tests/server/host_choice_test.cpp
  // checks it.
  std::vector<Sent> to(ConnectionId id) const {
    std::vector<Sent> out;
    for (const Sent& s : sent) {
      if (s.to == id && !std::holds_alternative<HostChoice>(s.message)) out.push_back(s);
    }
    return out;
  }

  void clear() {
    sent.clear();
    dropped.clear();
  }
};

template <typename T>
std::optional<T> only_message_to(const Harness& h, ConnectionId id) {
  auto msgs = h.to(id);
  REQUIRE(msgs.size() == 1);
  if (const auto* m = std::get_if<T>(&msgs[0].message)) return *m;
  return std::nullopt;
}

}  // namespace

TEST_CASE("six players are welcomed as 1 to 6, each seeing all earlier ones") {
  Harness h;
  std::vector<PlayerInfo> earlier;
  for (ConnectionId id = 1; id <= 6; ++id) {
    Welcome w = h.join(id, "p" + std::to_string(id));
    CHECK(w.player_number == id);
    CHECK(w.players == earlier);
    earlier.push_back(PlayerInfo{static_cast<std::uint8_t>(id), "p" + std::to_string(id)});
  }
  CHECK(h.session.player_count() == 6);
  CHECK(h.dropped.empty());
}

namespace {

FlagEntry flag(std::uint32_t hash, std::uint32_t value, std::uint16_t index = 0) {
  return FlagEntry{hash, index, FlagKind::uint32, value};
}

ObjectId placed(std::uint64_t hash) { return ObjectId{ObjectKind::placed, 0, hash}; }

using OwnerEpoch = std::optional<std::pair<std::uint8_t, std::uint16_t>>;

OwnerEpoch owned(std::uint8_t owner, std::uint16_t epoch) {
  return std::make_pair(owner, epoch);
}

ObjectPresent present_in(const std::string& scene, std::vector<ObjectId> ids) {
  ObjectPresent p;
  p.scene = scene;
  for (const ObjectId& id : ids) p.objects.push_back({id, 0});
  return p;
}

ObjectState state_at(float x, std::int32_t life) {
  ObjectState s;
  s.time.server_time_us = 5;
  s.position = Vec3{x, 0, 0};
  s.life = ObjectLife{life, 12};
  return s;
}

template <typename T>
std::vector<T> messages_to(const Harness& h, ConnectionId id) {
  std::vector<T> out;
  for (const Sent& s : h.to(id)) {
    if (const auto* m = std::get_if<T>(&s.message)) out.push_back(*m);
  }
  return out;
}

// Three players in, nothing sent yet about objects.
struct ObjectHarness : Harness {
  ObjectHarness() {
    join(1, "Link");
    join(2, "Zelda");
    join(3, "Saria");
    clear();
  }
};

PlayerSample sample_in(const std::string& scene) {
  PlayerSample s;
  s.time.server_time_us = 1;
  s.scene = scene;
  return s;
}

constexpr std::uint64_t kSecond = 1'000'000;

PlayerSample standing(const std::string& scene, float x) {
  PlayerSample s;
  s.time.server_time_us = 1;
  s.scene = scene;
  s.position = Vec3{x, 0, 0};
  return s;
}

SessionConfig quiet_config() {
  SessionConfig config;
  // The clock runs for a minute in these tests with nobody answering PINGs.
  config.silence_timeout_us = 1'000'000 * kSecond;
  config.ping_interval_us = 1'000'000 * kSecond;
  config.position_stale_us = 1'000'000 * kSecond;
  return config;
}

// Players 1 and 2 in the Field, both with enemy 7 loaded. Player 1 owns it
// and has said it stands at x = 0 with life 12.
struct OwnerHarness : Harness {
  explicit OwnerHarness(SessionConfig config = quiet_config()) : Harness(std::move(config)) {
    join(1, "Link");
    join(2, "Zelda");
    receive(1, present_in("Field", {placed(7)}), kChannelOwnership);
    receive(2, present_in("Field", {placed(7)}), kChannelOwnership);
    receive(1, ObjectSample{0, placed(7), 0, state_at(0, 12)}, kChannelObjectSamples);
    clear();
  }

  void stand(ConnectionId id, float x) {
    receive(id, standing("Field", x), kChannelPlayerSamples);
  }

  // Lets the clock run, ticking every 250 ms as the rules do.
  void run(std::uint64_t us) {
    for (std::uint64_t t = 0; t < us; t += 250'000) {
      now += 250'000;
      session.tick(now);
      collect();
    }
  }

  std::size_t asks() const { return messages_to<HandoverAsk>(*this, 1).size(); }

  // The owner at `owner_x` and the other player at `other_x`, 4 s long:
  // whether the server asked the owner to let go.
  static bool moves(float owner_x, float other_x) {
    OwnerHarness h;
    h.stand(1, owner_x);
    h.stand(2, other_x);
    h.run(4 * kSecond);
    return h.asks() == 1;
  }
};

}  // namespace

TEST_CASE("the host's save with six players: players 5 and 6 ask, get their own parts and the host's load (ORM)") {
  Harness h;
  h.join(1, "Link");  // the first player hosts
  for (ConnectionId id : {2, 3, 4, 5, 6}) h.join(id, "P" + std::to_string(id));
  h.clear();

  h.receive(6, SaveAsk{0}, kChannelSession);
  auto ask = only_message_to<SaveAsk>(h, 1);
  REQUIRE(ask.has_value());
  CHECK(ask->player_number == 6);
  h.clear();

  SavePart part;
  part.transfer_id = 9;
  part.reason = SaveReason::join;
  part.total_size = 3;
  part.crc32 = 0x1234;
  part.data = {1, 2, 3};
  for (std::uint8_t to : {5, 6}) {
    part.to_player = to;
    h.receive(1, part, kChannelSession);
    auto got = only_message_to<SavePart>(h, to);
    REQUIRE(got.has_value());
    CHECK(got->player_number == 1);
    for (ConnectionId other : {2, 3, 4, 5, 6}) {
      if (other != to) CHECK(h.to(other).empty());
    }
    h.clear();
  }

  part.to_player = 0;
  part.reason = SaveReason::host_load;
  h.receive(1, part, kChannelSession);
  for (ConnectionId id : {2, 3, 4, 5, 6}) CHECK(only_message_to<SavePart>(h, id).has_value());
  CHECK(h.to(1).empty());
  h.clear();

  part.to_player = 7;  // past kMaxPlayers: the codec refuses it before the session
  Bytes bytes;
  CHECK(encode(part, bytes) == Error::bad_value);
}

TEST_CASE("a revive goes to the downed player only, while it is dead and in the same scene (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  for (ConnectionId id : {1, 2, 3}) h.receive(id, sample_in("KokiriForest_Child"), kChannelPlayerSamples);
  h.clear();

  // Not dead yet: dropped.
  h.receive(2, Revive{0, 1, kReviveDone}, kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().revive_dropped == 1);

  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  h.clear();
  h.receive(2, Revive{0, 1, kReviveDone}, kChannelSession);
  const auto r = only_message_to<Revive>(h, 1);
  REQUIRE(r.has_value());
  CHECK(*r == Revive{2, 1, kReviveDone});
  CHECK(h.to(2).empty());
  CHECK(h.to(3).empty());
  CHECK(h.sent[0].channel == kChannelSession);
  CHECK(h.session.stats().revive_relayed == 1);

  // From another scene, to a player not there, or to itself: dropped.
  h.receive(3, sample_in("KokiriForest_Adult"), kChannelPlayerSamples);
  h.clear();
  h.receive(3, Revive{0, 1, kReviveDone}, kChannelSession);
  h.receive(2, Revive{0, 5, kReviveDone}, kChannelSession);
  h.receive(1, Revive{0, 1, kReviveDone}, kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().revive_dropped == 4);
  CHECK(h.session.stats().dropped() >= 4);

  // Up again: no more.
  h.receive(1, PlayerStatus{0, 0}, kChannelSession);
  h.clear();
  h.receive(2, Revive{0, 1, kReviveDone}, kChannelSession);
  CHECK(h.sent.empty());
}

TEST_CASE("group defeat: all of two or more dead 2 s running, once, to everyone (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  auto tick = [&](std::uint64_t us) {
    h.now += us;
    h.session.tick(h.now);
    h.collect();
  };
  auto defeats = [&](ConnectionId id) { return messages_to<GroupDefeat>(h, id); };
  h.clear();

  // Two of three down: nothing, however long.
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(2, PlayerStatus{0, status::dead | status::in_cutscene}, kChannelSession);
  tick(3 * kSecond);
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).empty());

  // The third goes down: 2 s from then, everyone gets it once.
  h.receive(3, PlayerStatus{0, status::dead}, kChannelSession);
  tick(1'900'000);
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).empty());
  tick(100'000);
  for (ConnectionId id : {1, 2, 3}) {
    const auto d = defeats(id);
    REQUIRE(d.size() == 1);
    CHECK(d[0].down == 3);
    CHECK(d[0].server_time_ms == static_cast<std::uint32_t>(h.now / 1000));
  }
  CHECK(h.session.stats().group_defeats == 1);
  h.clear();
  tick(kSecond);
  CHECK(defeats(1).empty());  // once

  // Up again (the reload), down again: another one.
  for (ConnectionId id : {1, 2, 3}) h.receive(id, PlayerStatus{0, 0}, kChannelSession);
  tick(100'000);
  for (ConnectionId id : {1, 2, 3}) h.receive(id, PlayerStatus{0, status::dead}, kChannelSession);
  tick(2 * kSecond);
  CHECK(defeats(2).size() == 1);
  CHECK(h.session.stats().group_defeats == 2);
}

TEST_CASE("group defeat: one alive breaks it, a player alone is left to its own game (ORM)") {
  SessionConfig config;
  config.silence_timeout_us = 1000 * kSecond;  // the test does not ping
  Harness h(config);
  h.join(1, "Link");
  h.join(2, "Zelda");
  auto tick = [&](std::uint64_t us) {
    h.now += us;
    h.session.tick(h.now);
    h.collect();
  };
  h.clear();
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  tick(kSecond);
  h.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  tick(kSecond);
  h.receive(2, PlayerStatus{0, 0}, kChannelSession);  // revived before the 2 s
  tick(kSecond);
  h.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  tick(1'500'000);
  CHECK(messages_to<GroupDefeat>(h, 1).empty());  // the 2 s count again
  tick(500'000);
  CHECK(messages_to<GroupDefeat>(h, 1).size() == 1);
  h.clear();

  // Player 2 leaves; player 1 alone and dead: no group defeat from the server.
  for (ConnectionId id : {1, 2}) h.receive(id, PlayerStatus{0, 0}, kChannelSession);
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  h.session.disconnected(2, h.now);
  h.collect();
  tick(5 * kSecond);
  CHECK(messages_to<GroupDefeat>(h, 1).empty());
  CHECK(h.session.stats().group_defeats == 1);

  // A rejoiner that has not said its status yet is not dead.
  h.join(2, "Zelda");
  h.clear();
  tick(3 * kSecond);
  CHECK(messages_to<GroupDefeat>(h, 1).empty());
  h.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  tick(2 * kSecond);
  CHECK(messages_to<GroupDefeat>(h, 1).size() == 1);
  CHECK(messages_to<GroupDefeat>(h, 2).size() == 1);
}

// ORM D-R66, D-R73: the group never splits. A downed player a mate holds A
// beside (the 3 s, REVIVE progress relayed to it) counts as up; once the hold
// stops (A let go, the reviver fallen) it is down again and the 2 s count
// from the hold's end. Getting up after the request, its game sends no dead
// bit (coop::host::Downed::status_dead, tests/host/downed_test.cpp).
TEST_CASE("group defeat: a downed player being revived counts as up, the hold's end starts the 2 s (ORM D-R66)") {
  SessionConfig config;
  config.silence_timeout_us = 1000 * kSecond;  // the test does not ping
  Harness h(config);
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  for (ConnectionId id : {1, 2, 3}) h.receive(id, sample_in("SpiritTemple"), kChannelPlayerSamples);
  auto tick = [&](std::uint64_t us) {
    h.now += us;
    h.session.tick(h.now);
    h.collect();
  };
  auto defeats = [&](ConnectionId id) { return messages_to<GroupDefeat>(h, id); };
  h.clear();

  // 1 and 2 down; 3 holds A beside 1 (progress every 250 ms) and is hit down
  // itself while its last progress is still on the way: nobody stands, but 1
  // is being held. No defeat while the hold lasts.
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  for (int i = 0; i < 4; ++i) {
    h.receive(3, Revive{0, 1, static_cast<std::uint8_t>(10 * i)}, kChannelSession);
    tick(250'000);
  }
  h.receive(3, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(3, Revive{0, 1, 40}, kChannelSession);  // relayed: 1 is dead, same scene
  CHECK(h.session.stats().revive_relayed == 5);
  tick(900'000);  // within revive_hold_us of the last progress
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).empty());
  tick(100'000);  // the hold is over now: 1 down again, its 2 s start
  tick(1'900'000);
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).empty());
  tick(100'000);
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).size() == 1);
  CHECK(h.session.stats().group_defeats == 1);
  h.clear();

  // The reload; then down again, a hold, and the reviver lets go: the group
  // check runs as usual from the hold's end, and a hold is not kept for a
  // later fall (up clears it).
  for (ConnectionId id : {1, 2, 3}) h.receive(id, PlayerStatus{0, 0}, kChannelSession);
  tick(100'000);
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(3, Revive{0, 1, 50}, kChannelSession);
  h.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(3, PlayerStatus{0, status::dead}, kChannelSession);
  tick(2'500'000);  // 1 s of hold, then 1.5 s of the 2 s
  CHECK(defeats(1).empty());
  tick(500'000);
  CHECK(defeats(1).size() == 1);
  CHECK(h.session.stats().group_defeats == 2);
  h.clear();

  // The request (the 3 s done) counts as the hold too, until the getting up
  // takes the dead bit off; then 1 is up for the server.
  for (ConnectionId id : {1, 2, 3}) h.receive(id, PlayerStatus{0, 0}, kChannelSession);
  tick(100'000);
  for (ConnectionId id : {1, 2}) h.receive(id, PlayerStatus{0, status::dead}, kChannelSession);
  h.receive(3, Revive{0, 1, kReviveDone}, kChannelSession);
  h.receive(3, PlayerStatus{0, status::dead}, kChannelSession);  // the reviver falls at once
  tick(500'000);
  h.receive(1, PlayerStatus{0, 0}, kChannelSession);  // getting up: no dead bit (D-R66)
  tick(10 * kSecond);
  for (ConnectionId id : {1, 2, 3}) CHECK(defeats(id).empty());
  CHECK(h.session.stats().group_defeats == 2);

  // revive_hold_us 0: a hold counts for nothing.
  SessionConfig off = config;
  off.revive_hold_us = 0;
  Harness o(off);
  o.join(1, "Link");
  o.join(2, "Zelda");
  o.clear();
  o.receive(1, PlayerStatus{0, status::dead}, kChannelSession);
  o.receive(2, PlayerStatus{0, status::dead}, kChannelSession);
  o.receive(2, Revive{0, 1, 20}, kChannelSession);
  o.now += 2 * kSecond;
  o.session.tick(o.now);
  o.collect();
  CHECK(messages_to<GroupDefeat>(o, 1).size() == 1);
}

// A game sends PLAYER_STATUS only when its flags change, so a player who
// joins (or rejoins after a drop: its presence starts empty) would see a mate
// lying down as standing until that mate's status changed again: no "caido"
// tag, no revive by A (REVIVE needs the dead bit on the reviver's side), no
// spectator choice. The server gives the joiner each other player's last
// non-zero status after its WELCOME.
TEST_CASE("player status: a joiner gets each player's last non-zero status after its WELCOME (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.receive(1, PlayerStatus{0, status::paused}, kChannelSession);
  h.receive(1, PlayerStatus{0, status::dead}, kChannelSession);  // the last one counts
  h.receive(2, PlayerStatus{0, status::in_cutscene}, kChannelSession);
  h.receive(2, PlayerStatus{0, 0}, kChannelSession);  // back to nothing: not sent
  h.clear();

  h.join(4, "Darunia");
  const auto all = h.to(4);
  std::size_t welcome_at = all.size();
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (std::holds_alternative<Welcome>(all[i].message)) welcome_at = i;
  }
  REQUIRE(welcome_at < all.size());
  std::vector<PlayerStatus> statuses;
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (const auto* st = std::get_if<PlayerStatus>(&all[i].message)) {
      CHECK(i > welcome_at);  // after WELCOME, so the player is known
      CHECK(all[i].channel == kChannelSession);
      statuses.push_back(*st);
    }
  }
  REQUIRE(statuses.size() == 1);
  CHECK(statuses[0] == PlayerStatus{1, status::dead});
  for (ConnectionId id : {1, 2, 3}) CHECK(messages_to<PlayerStatus>(h, id).empty());

  // The same for a rejoin by name after a drop: the downed mate is known at once.
  h.session.disconnected(4, h.now);
  h.collect();
  h.clear();
  h.join(5, "Darunia");
  const auto again = messages_to<PlayerStatus>(h, 5);
  REQUIRE(again.size() == 1);
  CHECK(again[0] == PlayerStatus{1, status::dead});
}

namespace {

StoryEvent story(StoryKind kind, const std::string& scene, const std::string& name) {
  StoryEvent s;
  s.kind = kind;
  s.scene = scene;
  s.name = name;
  s.position = {52.3f, 119.1f, -20.0f};
  return s;
}

constexpr const char* kMeet = "KokiriForest_Meet_DekuTree_MeetDekuTree_01";

}  // namespace

TEST_CASE("story event: the first start goes to everyone else, stamped, once (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.clear();

  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(h.to(1).empty());
  for (ConnectionId id : {2, 3}) {
    const auto s = only_message_to<StoryEvent>(h, id);
    REQUIRE(s.has_value());
    CHECK(s->player_number == 1);
    CHECK(s->kind == StoryKind::start);
    CHECK(s->scene == "KokiriForest_Child");
    CHECK(s->name == kMeet);
    CHECK(s->position == Vec3{52.3f, 119.1f, -20.0f});
    CHECK(s->server_time_ms == static_cast<std::uint32_t>(h.now / 1000));
  }
  CHECK(h.sent[0].channel == kChannelSession);
  CHECK(h.session.stats().story_relayed == 1);

  // Another game starting it too (its own trigger, or our request): not relayed.
  h.clear();
  h.now += 300'000;
  h.receive(2, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().story_repeats == 2);

  // The same name in another scene is another pair.
  h.receive(3, story(StoryKind::start, "KokiriForest_Adult", kMeet), kChannelSession);
  CHECK(messages_to<StoryEvent>(h, 1).size() == 1);
  CHECK(messages_to<StoryEvent>(h, 2).size() == 1);
  CHECK(h.to(3).empty());
}

TEST_CASE("story event: only the owner's end goes on, once, played and strays are dropped (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  h.clear();

  // An end of a pair never started, or from a player who does not own it.
  h.receive(1, story(StoryKind::end, "KokiriForest_Child", "Dm_Kokiri_0070"), kChannelSession);
  h.receive(2, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  // A client never sends played.
  h.receive(2, story(StoryKind::played, "KokiriForest_Child", "Other_Event"), kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().story_dropped == 3);

  h.receive(1, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  const auto s = only_message_to<StoryEvent>(h, 2);
  REQUIRE(s.has_value());
  CHECK(s->kind == StoryKind::end);
  CHECK(s->player_number == 1);
  h.clear();
  h.receive(1, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(h.sent.empty());  // once
  // Still started: no new owner.
  h.receive(2, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(h.sent.empty());
  // played was never kept as a pair.
  h.receive(2, story(StoryKind::start, "KokiriForest_Child", "Other_Event"), kChannelSession);
  CHECK(messages_to<StoryEvent>(h, 1).size() == 1);
}

TEST_CASE("story event: only the owner's skipped goes on, once, as his end (ORM, ZMP D-048)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  h.clear();
  h.receive(2, story(StoryKind::skipped, "KokiriForest_Child", kMeet), kChannelSession);  // not the owner
  CHECK(messages_to<StoryEvent>(h, 1).empty());
  h.receive(1, story(StoryKind::skipped, "KokiriForest_Child", kMeet), kChannelSession);
  const auto relayed = messages_to<StoryEvent>(h, 2);
  REQUIRE(relayed.size() == 1);
  CHECK(relayed[0].kind == StoryKind::skipped);
  CHECK(relayed[0].player_number == 1);
  h.clear();
  // Its end is said: a later end or skipped of it is dropped.
  h.receive(1, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  h.receive(1, story(StoryKind::skipped, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(messages_to<StoryEvent>(h, 2).empty());
  CHECK(h.session.stats().story_dropped == 3);
}

TEST_CASE("story event: a joiner gets each started pair as played, an empty room forgets (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  const std::uint32_t started = static_cast<std::uint32_t>(h.now / 1000);
  h.now += 5'000'000;
  h.receive(2, story(StoryKind::start, "InsideTheDekuTree", "InsideTheDekuTree_Arrive"),
            kChannelSession);
  h.receive(1, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  h.receive(2, story(StoryKind::skipped, "InsideTheDekuTree", "InsideTheDekuTree_Arrive"), kChannelSession);
  h.clear();

  h.join(3, "Saria");
  const auto sent = messages_to<StoryEvent>(h, 3);
  // Each pair as played, then its owner's last word: `end`, or `skipped`
  // (ZMP D-048: a joiner's own start of a skipped one is skipped).
  REQUIRE(sent.size() == 4);
  std::vector<StoryEvent> played;
  std::vector<StoryEvent> last;
  for (const auto& s : sent) (s.kind == StoryKind::played ? played : last).push_back(s);
  REQUIRE(played.size() == 2);
  REQUIRE(last.size() == 2);
  const auto meet = played[0].scene == "KokiriForest_Child" ? played[0] : played[1];
  CHECK(meet.name == kMeet);
  CHECK(meet.player_number == 1);
  CHECK(meet.server_time_ms == started);
  CHECK(meet.position == Vec3{52.3f, 119.1f, -20.0f});
  const auto meet_end = last[0].name == kMeet ? last[0] : last[1];
  const auto arrive_end = last[0].name == kMeet ? last[1] : last[0];
  CHECK(meet_end.kind == StoryKind::end);
  CHECK(meet_end.scene == "KokiriForest_Child");
  CHECK(meet_end.player_number == 1);
  CHECK(arrive_end.kind == StoryKind::skipped);
  CHECK(arrive_end.name == "InsideTheDekuTree_Arrive");
  CHECK(arrive_end.player_number == 2);
  // Each last word comes after its played.
  const auto at = [&](const std::string& name, bool first) {
    for (std::size_t i = 0; i < sent.size(); ++i) {
      if (sent[i].name == name && (sent[i].kind == StoryKind::played) == first) return i;
    }
    return sent.size();
  };
  CHECK(at(kMeet, true) < at(kMeet, false));
  CHECK(at("InsideTheDekuTree_Arrive", true) < at("InsideTheDekuTree_Arrive", false));
  // Nobody else got them again.
  CHECK(messages_to<StoryEvent>(h, 1).empty());
  CHECK(messages_to<StoryEvent>(h, 2).empty());

  // Everyone leaves: the next room starts with nothing played.
  for (ConnectionId id : {1, 2, 3}) h.session.disconnected(id, h.now);
  h.collect();
  h.clear();
  h.join(4, "Link");
  CHECK(messages_to<StoryEvent>(h, 4).empty());
}

TEST_CASE("story event: the host's save load forgets what the room played (ORM, REVISION_HISTORIA H4)") {
  // The host's FLAG_RESET puts the save's HavePlayedEvent 0s back in every
  // game: the cutscene will play again, and it must be everyone's again (a
  // kept pair made the next start a repeat, never relayed).
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.receive(1, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  h.receive(1, story(StoryKind::end, "KokiriForest_Child", kMeet), kChannelSession);
  h.receive(2, FlagReset{0, {flag(5, 0)}}, kChannelProgress);  // not the host: nothing
  h.clear();
  h.receive(2, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  CHECK(h.sent.empty());

  h.receive(1, FlagReset{0, {flag(5, 0)}}, kChannelProgress);  // the host loaded a save
  h.clear();
  h.receive(2, story(StoryKind::start, "KokiriForest_Child", kMeet), kChannelSession);
  const auto s = only_message_to<StoryEvent>(h, 1);
  REQUIRE(s.has_value());
  CHECK(s->kind == StoryKind::start);
  CHECK(s->player_number == 2);
  h.clear();
  h.join(3, "Saria");  // a joiner gets only what was played since
  const auto played = messages_to<StoryEvent>(h, 3);
  REQUIRE(played.size() == 1);
  CHECK(played[0].player_number == 2);
}

TEST_CASE("player look: relayed to the others with the sender's number, a repeat is not (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.clear();

  h.receive(2, PlayerLook{0, {0x8e, 0x44, 0xd0}, 0}, kChannelSession);
  CHECK(h.to(2).empty());
  for (ConnectionId id : {1, 3}) {
    const auto look = only_message_to<PlayerLook>(h, id);
    REQUIRE(look.has_value());
    CHECK(look->player_number == 2);
    CHECK(look->rgb == std::array<std::uint8_t, 3>{0x8e, 0x44, 0xd0});
    CHECK(look->style == 0);
  }
  CHECK(h.sent[0].channel == kChannelSession);
  CHECK(h.session.stats().look_relayed == 1);

  // The same look again (sent after a WELCOME, or twice): kept, not relayed.
  h.clear();
  h.receive(2, PlayerLook{0, {0x8e, 0x44, 0xd0}, 0}, kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().look_repeats == 1);

  // A change goes on.
  h.receive(2, PlayerLook{0, {1, 2, 3}, 0}, kChannelSession);
  CHECK(messages_to<PlayerLook>(h, 1).size() == 1);
  CHECK(messages_to<PlayerLook>(h, 3).size() == 1);
  CHECK(h.session.stats().look_relayed == 2);
}

TEST_CASE("player look: a joiner gets each player's last look after its WELCOME (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.receive(3, PlayerLook{0, {30, 30, 30}, 0}, kChannelSession);
  h.receive(1, PlayerLook{0, {10, 10, 10}, 0}, kChannelSession);
  h.receive(1, PlayerLook{0, {11, 12, 13}, 0}, kChannelSession);  // the last one counts
  h.clear();

  h.join(4, "Darunia");
  const auto all = h.to(4);
  std::size_t welcome_at = all.size();
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (std::holds_alternative<Welcome>(all[i].message)) welcome_at = i;
  }
  REQUIRE(welcome_at < all.size());
  std::vector<PlayerLook> looks;
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (const auto* look = std::get_if<PlayerLook>(&all[i].message)) {
      CHECK(i > welcome_at);  // after WELCOME, so the player is known
      CHECK(all[i].channel == kChannelSession);
      looks.push_back(*look);
    }
  }
  REQUIRE(looks.size() == 2);  // player 2 never sent one
  CHECK(looks[0].player_number == 1);
  CHECK(looks[0].rgb == std::array<std::uint8_t, 3>{11, 12, 13});
  CHECK(looks[1].player_number == 3);
  CHECK(looks[1].rgb == std::array<std::uint8_t, 3>{30, 30, 30});
  // The others were not sent them again.
  for (ConnectionId id : {1, 2, 3}) CHECK(messages_to<PlayerLook>(h, id).empty());
}

TEST_CASE("player look: a leaver's look is not given to later joiners, a rejoiner sends again (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.receive(2, PlayerLook{0, {200, 100, 50}, 0}, kChannelSession);
  h.session.disconnected(2, h.now);
  h.collect();
  h.clear();

  h.join(3, "Saria");
  CHECK(messages_to<PlayerLook>(h, 3).empty());
  // Zelda comes back: nothing kept, so its first look is relayed again.
  h.join(4, "Zelda");
  CHECK(messages_to<PlayerLook>(h, 4).empty());
  h.clear();
  h.receive(4, PlayerLook{0, {200, 100, 50}, 0}, kChannelSession);
  CHECK(messages_to<PlayerLook>(h, 1).size() == 1);
  CHECK(messages_to<PlayerLook>(h, 3).size() == 1);
  CHECK(h.session.stats().look_repeats == 0);
}

TEST_CASE("player look: a client's look with a player number is refused (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.clear();
  h.receive(1, PlayerLook{2, {1, 2, 3}, 0}, kChannelSession);  // posing as player 2
  h.receive(1, PlayerLook{0, {1, 2, 3}, 0}, kChannelProgress);  // wrong channel
  CHECK(h.sent.empty());
  CHECK(h.session.stats().look_relayed == 0);
}

namespace {

ObjectPose pose_of(const ObjectId& id, std::uint16_t epoch, std::uint32_t sequence) {
  PoseBone head{12, 0, std::array<std::int16_t, 4>{0, 12539, 0, 30273}, std::nullopt, std::nullopt};
  return ObjectPose{0, id, epoch, {5, sequence}, 0x1234, {head}};
}

}  // namespace

TEST_CASE("object poses go only from the owner of the current epoch, to its scene (ORM)") {
  ObjectHarness h;
  h.receive(1, present_in("KokiriForest", {placed(7)}), kChannelOwnership);
  h.receive(2, present_in("KokiriForest", {placed(7)}), kChannelOwnership);
  h.receive(3, present_in("Market", {}), kChannelOwnership);
  h.clear();

  h.receive(1, pose_of(placed(7), 0, 1), kChannelObjectSamples);
  REQUIRE(h.sent.size() == 1);  // not to 3, in the Market, nor back to 1
  CHECK(h.sent[0].to == 2);
  CHECK(h.sent[0].channel == kChannelObjectSamples);
  ObjectPose relayed = pose_of(placed(7), 0, 1);
  relayed.player_number = 1;
  CHECK(std::get<ObjectPose>(h.sent[0].message) == relayed);

  h.clear();
  h.now += 100'000;
  h.receive(2, pose_of(placed(7), 0, 2), kChannelObjectSamples);  // not the owner
  h.receive(1, pose_of(placed(7), 1, 2), kChannelObjectSamples);  // another epoch
  h.receive(1, pose_of(placed(8), 0, 2), kChannelObjectSamples);  // unknown
  CHECK(h.sent.empty());
  CHECK(h.session.stats().object_pose_refused == 3);
  CHECK(h.session.stats().object_pose_relayed == 1);
  // A pose is not an object sample, and nothing of it is kept: a player
  // who enters the scene later is told the owner, not the pose.
  CHECK(h.session.stats().object_relayed == 0);
  h.receive(3, present_in("KokiriForest", {placed(7)}), kChannelOwnership);
  CHECK(messages_to<ObjectPose>(h, 3).empty());
  CHECK(messages_to<ObjectOwner>(h, 3).size() == 1);
  // On another channel it never reaches the session's rules.
  h.clear();
  h.now += 100'000;
  h.receive(1, pose_of(placed(7), 0, 3), kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().object_pose_relayed == 1);
}

TEST_CASE("one player's object poses: one every 25 ms, one object at a time (ORM)") {
  ObjectHarness h;
  h.receive(1, present_in("KokiriForest", {placed(7), placed(8)}), kChannelOwnership);
  h.receive(2, present_in("KokiriForest", {placed(7), placed(8)}), kChannelOwnership);
  h.clear();

  h.receive(1, pose_of(placed(7), 0, 1), kChannelObjectSamples);
  h.now += 10'000;
  h.receive(1, pose_of(placed(7), 0, 2), kChannelObjectSamples);  // 10 ms later: dropped
  h.now += 20'000;
  h.receive(1, pose_of(placed(7), 0, 3), kChannelObjectSamples);  // 30 ms after the first
  h.now += 30'000;
  h.receive(1, pose_of(placed(8), 0, 4), kChannelObjectSamples);  // a second object: dropped
  CHECK(messages_to<ObjectPose>(h, 2).size() == 2);
  CHECK(h.session.stats().object_pose_dropped == 2);
  // The talk moved on: half a second after the last pose of 7, 8 may come.
  h.now += 500'000;
  h.receive(1, pose_of(placed(8), 0, 5), kChannelObjectSamples);
  const auto got = messages_to<ObjectPose>(h, 2);
  REQUIRE(got.size() == 3);
  CHECK(got[0].time.sequence == 1);
  CHECK(got[1].time.sequence == 3);
  CHECK(got[2].id == placed(8));
  CHECK(h.session.stats().object_pose_relayed == 3);
  // Another player's poses have their own gap.
  h.receive(2, pose_of(placed(7), 0, 6), kChannelObjectSamples);  // 2 owns neither
  CHECK(h.session.stats().object_pose_refused == 1);
}

// ---- ORM endgame: the owner's game keeps a boss (oot-remake docs/FINAL.md, ZMP D-231) ---------

namespace {

ObjectState kept_state(std::int32_t life, std::uint8_t bits) {
  ObjectState s = state_at(0, life);
  s.state = bits;
  return s;
}

// The boss hand-overs these rules sit on: test10 turned both off by default
// (boss_hit_handover false, boss_near_hold_us 0: each move jumps the boss's
// phase, issue 150); a room that turns them back on still has our keeps.
SessionConfig boss_handovers_on() {
  SessionConfig config = quiet_config();
  config.boss_hit_handover = true;
  config.boss_near_hold_us = 4'000'000;
  return config;
}

void make_boss(OwnerHarness& h) {
  h.receive(1, ObjectGroup{0, {{placed(7), placed(7), object_group::boss}}}, kChannelOwnership);
}

}  // namespace

TEST_CASE("ORM endgame: a boss its owner's game keeps (Twinrova's ice) is not handed to a hitter") {
  OwnerHarness h(boss_handovers_on());
  make_boss(h);
  h.stand(1, 4);
  h.stand(2, 6);
  h.receive(1, ObjectSample{0, placed(7), 0, kept_state(12, object_state::owner_keeps)}, kChannelObjectSamples);
  h.clear();
  h.receive(2, Damage{0, placed(7), 0, 1, 1, {}}, kChannelOwnership);
  CHECK(messages_to<Damage>(h, 1).size() == 1);  // the hit still goes to the owner
  CHECK(messages_to<HandoverAsk>(h, 1).empty());
  CHECK(h.session.stats().owner_kept == 1);
  CHECK(h.session.stats().boss_to_hitter == 0);
  CHECK(h.session.object_owner(placed(7)) == owned(1, 0));
  // The ice thaws: the owner's next sample says nothing, and the next hit takes it.
  h.receive(1, ObjectSample{0, placed(7), 0, state_at(0, 12)}, kChannelObjectSamples);
  h.clear();
  h.receive(2, Damage{0, placed(7), 0, 1, 1, {}}, kChannelOwnership);
  CHECK(messages_to<HandoverAsk>(h, 1) == std::vector<HandoverAsk>{{placed(7), 0, 2}});
  CHECK(h.session.stats().boss_to_hitter == 1);
  // The new owner starts with nothing kept.
  h.receive(1, HandoverState{0, placed(7), 0, kept_state(12, object_state::owner_keeps)}, kChannelOwnership);
  CHECK(h.session.object_owner(placed(7)) == owned(2, 1));
  // ... so its last state's bits (the old owner's answer) keep nothing: 8 s later a hit takes it back.
  h.run(8 * kSecond);
  h.clear();
  h.receive(1, Damage{0, placed(7), 1, 1, 1, {}}, kChannelOwnership);
  CHECK(messages_to<HandoverAsk>(h, 2) == std::vector<HandoverAsk>{{placed(7), 1, 1}});

  // owner_keep_max_us 0: the bits are not read.
  SessionConfig off = boss_handovers_on();
  off.owner_keep_max_us = 0;
  OwnerHarness h0(off);
  make_boss(h0);
  h0.stand(1, 4);
  h0.stand(2, 6);
  h0.receive(1, ObjectSample{0, placed(7), 0, kept_state(12, object_state::owner_keeps)}, kChannelObjectSamples);
  h0.clear();
  h0.receive(2, Damage{0, placed(7), 0, 1, 1, {}}, kChannelOwnership);
  CHECK(messages_to<HandoverAsk>(h0, 1).size() == 1);
  CHECK(h0.session.stats().owner_kept == 0);
}

TEST_CASE("ORM endgame: owner_keeps_near (a part charge) holds the nearer player back, not a hitter") {
  OwnerHarness h(boss_handovers_on());
  make_boss(h);
  h.receive(1, ObjectSample{0, placed(7), 0, state_at(0, 11)}, kChannelObjectSamples);  // a fight
  h.receive(1, ObjectSample{0, placed(7), 0, kept_state(11, object_state::owner_keeps_near)},
            kChannelObjectSamples);
  REQUIRE(h.session.object_in_fight(placed(7), h.now));
  h.stand(1, 12);
  h.stand(2, 3);
  h.clear();
  h.run(10 * kSecond + 500'000);  // the near rule would have asked by now (issue 150 above)
  CHECK(h.asks() == 0);
  CHECK(h.session.stats().boss_to_near == 0);
  CHECK(h.session.stats().owner_kept >= 1);
  h.receive(2, Damage{0, placed(7), 0, 1, 1, {}}, kChannelOwnership);
  CHECK(messages_to<HandoverAsk>(h, 1) == std::vector<HandoverAsk>{{placed(7), 0, 2}});
  CHECK(h.session.stats().boss_to_hitter == 1);
}

TEST_CASE("ORM endgame: a keep holds 30 s at most, then the near rule asks") {
  OwnerHarness h(boss_handovers_on());
  make_boss(h);
  h.receive(1, ObjectSample{0, placed(7), 0, state_at(0, 11)}, kChannelObjectSamples);  // a fight
  h.receive(1, ObjectSample{0, placed(7), 0, kept_state(11, object_state::owner_keeps)}, kChannelObjectSamples);
  h.stand(1, 12);
  h.stand(2, 3);
  h.clear();
  h.run(29 * kSecond);
  CHECK(h.asks() == 0);
  // A sample saying it again does not start the 30 s over.
  h.receive(1, ObjectSample{0, placed(7), 0, kept_state(11, object_state::owner_keeps)}, kChannelObjectSamples);
  h.run(2 * kSecond);
  CHECK(messages_to<HandoverAsk>(h, 1) == std::vector<HandoverAsk>{{placed(7), 0, 2}});
  CHECK(h.session.stats().boss_to_near == 1);
}

TEST_CASE("ocarina notes: relayed to the others with the sender's number, nothing kept (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.clear();

  h.receive(2, OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 0, 1}, kChannelSession);
  CHECK(h.to(2).empty());
  for (ConnectionId id : {1, 3}) {
    const auto note = only_message_to<OrmOcarinaNote>(h, id);
    REQUIRE(note.has_value());
    CHECK(*note == OrmOcarinaNote{2, OcarinaKind::note, 0, 11, 0, 1});
  }
  CHECK(h.sent[0].channel == kChannelSession);
  CHECK(h.session.stats().ocarina_note_relayed == 1);

  // The same note again is relayed too: the receiver drops a repeated serial.
  h.clear();
  h.receive(2, OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 0, 1}, kChannelSession);
  h.receive(2, OrmOcarinaNote{0, OcarinaKind::song, 0, 0, 2, 2}, kChannelSession);
  const auto got = messages_to<OrmOcarinaNote>(h, 1);
  REQUIRE(got.size() == 2);
  CHECK(got[1] == OrmOcarinaNote{2, OcarinaKind::song, 0, 0, 2, 2});
  CHECK(h.session.stats().ocarina_note_relayed == 3);

  // Nothing is kept for a joiner.
  h.clear();
  h.join(4, "Darunia");
  CHECK(messages_to<OrmOcarinaNote>(h, 4).empty());

  // A client's note with a player number is refused before the relay.
  h.clear();
  Message spoofed = OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, 3};
  REQUIRE(set_sender(spoofed, 1));
  h.receive(2, spoofed, kChannelSession);
  CHECK(h.sent.empty());
  CHECK(h.session.stats().ocarina_note_relayed == 3);
}

TEST_CASE("ocarina notes: each player held to 20 a second, bursts of 20, its own bucket (ORM)") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.join(3, "Saria");
  h.clear();

  // A burst of 25 at once: 20 go on, 5 are dropped and counted.
  for (int i = 0; i < 25; ++i) {
    h.receive(2, OrmOcarinaNote{0, OcarinaKind::note, 0, 4, 0, static_cast<std::uint8_t>(i)},
              kChannelSession);
  }
  CHECK(messages_to<OrmOcarinaNote>(h, 1).size() == 20);
  CHECK(h.session.stats().ocarina_note_relayed == 20);
  CHECK(h.session.stats().ocarina_note_dropped == 5);
  CHECK(h.session.stats().rate_limited == 0);  // under the general reliable rate
  CHECK(h.session.stats().dropped() == 5);

  // Another player's bucket is its own.
  h.receive(3, OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, 0}, kChannelSession);
  CHECK(messages_to<OrmOcarinaNote>(h, 1).size() == 21);

  // 125 ms later two and a half have come back (20 a second): two fit, the third does not.
  h.now += 125'000;
  for (int i = 0; i < 3; ++i) {
    h.receive(2, OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, static_cast<std::uint8_t>(30 + i)},
              kChannelSession);
  }
  CHECK(h.session.stats().ocarina_note_relayed == 23);
  CHECK(h.session.stats().ocarina_note_dropped == 6);

  // A long rest fills the bucket to its burst, not more.
  h.now += 10'000'000;
  h.clear();
  for (int i = 0; i < 22; ++i) {
    h.receive(2, OrmOcarinaNote{0, OcarinaKind::note, 0, 9, 0, static_cast<std::uint8_t>(i)},
              kChannelSession);
  }
  CHECK(messages_to<OrmOcarinaNote>(h, 3).size() == 20);
  CHECK(h.session.stats().ocarina_note_dropped == 8);
}

TEST_CASE("ocarina notes: the rate is a setting, and 0 turns its own bucket off (ORM)") {
  SessionConfig config;
  config.ocarina_note_per_second = 0;
  Harness h(config);
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.clear();
  for (int i = 0; i < 40; ++i) {
    h.receive(2, OrmOcarinaNote{0, OcarinaKind::note, 0, 4, 0, static_cast<std::uint8_t>(i)},
              kChannelSession);
  }
  CHECK(messages_to<OrmOcarinaNote>(h, 1).size() == 40);
  CHECK(h.session.stats().ocarina_note_dropped == 0);

  SessionConfig slow;
  slow.ocarina_note_per_second = 2;
  slow.ocarina_note_burst = 3;
  Harness s(slow);
  s.join(1, "Link");
  s.join(2, "Zelda");
  s.clear();
  for (int i = 0; i < 5; ++i) {
    s.receive(2, OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, static_cast<std::uint8_t>(i)},
              kChannelSession);
  }
  CHECK(messages_to<OrmOcarinaNote>(s, 1).size() == 3);
  s.now += 500'000;  // one more after half a second
  s.receive(2, OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, 9}, kChannelSession);
  s.receive(2, OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, 10}, kChannelSession);
  CHECK(messages_to<OrmOcarinaNote>(s, 1).size() == 4);
  CHECK(s.session.stats().ocarina_note_dropped == 3);
}

TEST_CASE("chat: a player's lines are held to chat_count in chat_window_us (ORM quick messages)") {
  std::vector<std::string> lines;
  SessionConfig config;
  config.log = [&lines](const std::string& line) { lines.push_back(line); };
  Harness h(config);
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.clear();
  lines.clear();
  const std::uint64_t t0 = h.now;
  // Ten lines in one second: all relayed.
  for (int i = 0; i < 10; ++i) {
    h.receive(1, Chat{0, "line " + std::to_string(i)}, kChannelSession);
    h.now += 100'000;
  }
  CHECK(h.to(2).size() == 10);
  CHECK(h.session.stats().chat_limited == 0);
  // The eleventh and twelfth within ten seconds of the first: dropped, one log line.
  h.receive(1, Chat{0, "eleven"}, kChannelSession);
  h.receive(1, Chat{0, "twelve"}, kChannelSession);
  CHECK(h.to(2).size() == 10);
  CHECK(h.session.stats().chat_limited == 2);
  CHECK(h.session.stats().rate_limited == 0);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find("player 1 sent chat over its rate (10 lines in 10 s)") != std::string::npos);
  // Not a bad packet: not in dropped().
  CHECK(h.session.stats().dropped() == 0);
  // The other player and the other messages are not held.
  h.receive(2, Chat{0, "hi"}, kChannelSession);
  h.receive(1, PlayerStatus{0, status::paused}, kChannelSession);
  CHECK(h.to(1).size() == 1);
  CHECK(h.to(2).size() == 11);
  // Ten seconds after the first line it is out of the window: one more goes,
  // the next does not (the second line is still in), and it is logged again.
  h.clear();
  h.now = t0 + 10'000'000;
  h.receive(1, Chat{0, "again"}, kChannelSession);
  h.receive(1, Chat{0, "too soon"}, kChannelSession);
  const auto msgs = h.to(2);
  REQUIRE(msgs.size() == 1);
  CHECK(std::get<Chat>(msgs[0].message) == Chat{1, "again"});
  CHECK(h.session.stats().chat_limited == 3);
  CHECK(lines.size() == 2);
}

TEST_CASE("chat: chat_gap_us holds lines apart, and chat_count 0 turns the limit off (ORM)") {
  SessionConfig config;
  config.chat_gap_us = 250'000;
  Harness h(config);
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.clear();
  h.receive(1, Chat{0, "one"}, kChannelSession);
  h.now += 100'000;
  h.receive(1, Chat{0, "too soon"}, kChannelSession);
  h.now += 150'000;
  h.receive(1, Chat{0, "two"}, kChannelSession);
  auto msgs = h.to(2);
  REQUIRE(msgs.size() == 2);
  CHECK(std::get<Chat>(msgs[1].message) == Chat{1, "two"});
  CHECK(h.session.stats().chat_limited == 1);

  SessionConfig off;
  off.chat_count = 0;
  Harness o(off);
  o.join(1, "Link");
  o.join(2, "Zelda");
  o.clear();
  for (int i = 0; i < 50; ++i) o.receive(1, Chat{0, "spam"}, kChannelSession);
  CHECK(o.to(2).size() == 50);
  CHECK(o.session.stats().chat_limited == 0);
}

TEST_CASE("the parts of one object pose are relayed together, each once (ORM)") {
  // oot-remake docs/NPC_COMPARTIDOS.md, "PNJ con muchos huesos": Ruto's 300
  // bones go in three parts of one time and one mask. The 25 ms rule holds
  // for poses, not for the parts of one.
  ObjectHarness h;
  h.receive(1, present_in("ZorasDomain", {placed(7), placed(8)}), kChannelOwnership);
  h.receive(2, present_in("ZorasDomain", {placed(7), placed(8)}), kChannelOwnership);
  h.clear();
  const auto part = [](std::uint32_t sequence, std::uint8_t index, std::uint8_t mask,
                       std::uint16_t bone, ObjectId id) {
    ObjectPose p = pose_of(id, 0, sequence);
    p.part = index;
    p.parts = mask;
    p.bones[0].bone = bone;
    return p;
  };
  h.receive(1, part(1, 0, 0x07, 12, placed(7)), kChannelObjectSamples);
  h.receive(1, part(1, 1, 0x07, 140, placed(7)), kChannelObjectSamples);
  h.receive(1, part(1, 2, 0x07, 260, placed(7)), kChannelObjectSamples);
  CHECK(messages_to<ObjectPose>(h, 2).size() == 3);
  h.receive(1, part(1, 1, 0x07, 140, placed(7)), kChannelObjectSamples);  // a part again
  h.receive(1, part(1, 0, 0x03, 12, placed(7)), kChannelObjectSamples);   // another mask: a new pose, too soon
  CHECK(h.session.stats().object_pose_dropped == 2);
  h.now += 10'000;
  h.receive(1, part(2, 0, 0x01, 12, placed(7)), kChannelObjectSamples);  // a new pose 10 ms later
  CHECK(h.session.stats().object_pose_dropped == 3);
  h.now += 20'000;
  h.receive(1, part(3, 1, 0x02, 140, placed(7)), kChannelObjectSamples);  // 30 ms after the first
  h.receive(1, part(3, 1, 0x02, 140, placed(8)), kChannelObjectSamples);  // its time, another object
  const auto got = messages_to<ObjectPose>(h, 2);
  REQUIRE(got.size() == 4);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(got[i].part == i);
    CHECK(got[i].parts == 0x07);
    CHECK(got[i].time.sequence == 1);
    CHECK(got[i].player_number == 1);
  }
  CHECK(got[3].part == 1);
  CHECK(got[3].parts == 0x02);
  CHECK(got[3].bones[0].bone == 140);
  CHECK(h.session.stats().object_pose_relayed == 4);
  CHECK(h.session.stats().object_pose_dropped == 4);
}

// ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento"): a
// player over its rate is told, once a minute, never silently.
TEST_CASE("ORM a player over its rate gets LINK_NOTICE over_rate, at once for reliable, after ten unreliable, once a minute") {
  Harness h;
  h.join(1, "Link");
  h.join(2, "Zelda");
  h.clear();
  const auto notices_to = [&](ConnectionId id) {
    std::vector<LinkNotice> out;
    for (const Sent& s : h.to(id)) {
      if (const auto* n = std::get_if<LinkNotice>(&s.message)) {
        CHECK(s.channel == kChannelSession);
        out.push_back(*n);
      }
    }
    return out;
  };
  PlayerSample sample;
  sample.time = {1, 1};
  // 480 go (the burst); the next nine are dropped quietly, the tenth is told.
  for (int i = 0; i < 489; ++i) h.receive(1, sample, kChannelPlayerSamples);
  CHECK(notices_to(1).empty());
  h.receive(1, sample, kChannelPlayerSamples);
  auto told = notices_to(1);
  REQUIRE(told.size() == 1);
  CHECK(told[0].kind == LinkNoticeKind::over_rate);
  CHECK(told[0].a == 10);
  CHECK(told[0].b == 0);
  CHECK(notices_to(2).empty());  // the others hear nothing of it
  // More within the minute: counted, not told.
  h.clear();
  for (int i = 0; i < 200; ++i) h.receive(1, sample, kChannelPlayerSamples);
  CHECK(notices_to(1).empty());
  // A minute later a dropped reliable one is told at once, with the unreliable ones since.
  h.clear();
  h.now += 61'000'000;
  for (int i = 0; i < 121; ++i) h.receive(1, Chat{0, "spam"}, kChannelSession);
  told = notices_to(1);
  REQUIRE(told.size() == 1);
  CHECK(told[0].b == 1);
  CHECK(told[0].a == 200);
}
