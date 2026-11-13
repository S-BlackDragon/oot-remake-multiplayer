// ORM menus (oot-remake docs/MENUS_PAUSA.md, M3 and M6): a paused owner gives
// its enemies up after 1 s, and a player on the title screen is left out of
// the group defeat. Driven without a network, as enemies_test.cpp.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_status.hpp"
#include "orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;

namespace {

constexpr std::uint64_t kSec = 1'000'000;

struct Sent {
  ConnectionId to;
  std::uint8_t channel;
  Message message;
};

SessionConfig quiet() {
  SessionConfig config;
  config.silence_timeout_us = 1'000'000 * kSec;
  config.ping_interval_us = 1'000'000 * kSec;
  config.position_stale_us = 1'000'000 * kSec;
  return config;
}

// Players 1..n, each joined with its number; enemy 7 in the Field, owned by
// player 1 (the first to list it), in a fight.
struct Room {
  Session session;
  std::vector<Sent> sent;
  std::uint64_t now = 1'000'000;

  explicit Room(int players, SessionConfig config = quiet()) : session(orm_test::with_password(std::move(config))) {
    for (int i = 1; i <= players; ++i) join(static_cast<ConnectionId>(i));
    for (int i = 1; i <= players; ++i) {
      ObjectPresent p;
      p.scene = "Field";
      p.objects.push_back({enemy(), 0});
      receive(static_cast<ConnectionId>(i), p, kChannelOwnership);
    }
    ObjectState s;
    s.time.server_time_us = 5;
    s.position = Vec3{0, 0, 0};
    s.life = ObjectLife{12, 12};
    s.state = object_state::in_fight;
    receive(1, ObjectSample{0, enemy(), 0, s}, kChannelObjectSamples);
    sent.clear();
  }

  static ObjectId enemy() { return ObjectId{ObjectKind::placed, 0, 7}; }

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        sent.push_back({s->to, s->channel, m});
      }
    }
  }

  void receive(ConnectionId id, const Message& m, std::uint8_t channel) {
    std::vector<std::uint8_t> bytes;
    REQUIRE(encode(m, bytes) == Error::none);
    session.received(id, channel, bytes, now);
    collect();
  }

  void join(ConnectionId id) {
    REQUIRE(session.connected(id, now, std::nullopt));
    Hello h;
    h.build_id = "test 0.0";
    h.game_build_id = "e0f3ccfaf50065ae";
    h.game_version = "1.1.1";
    h.nickname = "P" + std::to_string(id);
    orm_test::join(session, id, h, now);
    collect();
  }

  void status(ConnectionId id, std::uint8_t flags) { receive(id, PlayerStatus{0, flags}, kChannelSession); }

  void run(std::uint64_t us) {
    for (std::uint64_t t = 0; t < us; t += 250'000) {
      now += 250'000;
      session.tick(now);
      collect();
    }
  }

  template <typename T>
  std::vector<T> to(ConnectionId id) const {
    std::vector<T> out;
    for (const Sent& s : sent) {
      if (s.to != id) continue;
      if (const auto* m = std::get_if<T>(&s.message)) out.push_back(*m);
    }
    return out;
  }
};

constexpr std::uint8_t kTitle = status::loading | orm_status::at_title;

}  // namespace

// ---------------------------------------------------------------- paused owner (M3)

TEST_CASE("server menus: the default pause before an owner gives up is 1 s") {
  CHECK(SessionConfig{}.owner_paused_us == kSec);
}

TEST_CASE("server menus: a paused owner gives its enemy to a standing player after 1 s, in a fight") {
  Room r(2);
  r.status(1, status::paused);
  r.run(750'000);
  CHECK(r.to<HandoverAsk>(1).empty());
  r.run(500'000);
  REQUIRE(r.to<HandoverAsk>(1).size() == 1);
  CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{Room::enemy(), 0, 2});
}

TEST_CASE("server menus: a glance at the map under 1 s moves nothing") {
  Room r(2);
  r.status(1, status::paused);
  r.run(750'000);
  r.status(1, 0);
  r.run(3 * kSec);
  CHECK(r.to<HandoverAsk>(1).empty());
}

TEST_CASE("server menus: nobody else standing, the paused owner keeps it") {
  Room r(3);
  r.status(2, status::paused);
  r.status(3, kTitle);
  r.status(1, status::paused);
  r.run(3 * kSec);
  CHECK(r.to<HandoverAsk>(1).empty());
}

TEST_CASE("server menus: an owner on the title screen gives it up at once (it is loading)") {
  Room r(2);
  r.status(1, kTitle);
  r.run(250'000);
  CHECK(r.to<HandoverAsk>(1).size() == 1);
}

// ---------------------------------------------------------------- group defeat (M6)

TEST_CASE("server menus: a player on the title screen is left out of the group defeat") {
  SessionConfig config = quiet();
  config.group_defeat_us = 2 * kSec;
  Room r(3, config);
  r.status(3, kTitle);
  r.status(1, status::dead);
  r.status(2, status::dead);
  r.run(1'750'000);
  CHECK(r.to<GroupDefeat>(1).empty());
  r.run(500'000);
  const auto d = r.to<GroupDefeat>(1);
  REQUIRE(d.size() == 1);
  CHECK(d[0].down == 2);  // the two in the world
  CHECK(r.to<GroupDefeat>(3).size() == 1);  // told too: it is in the room
}

TEST_CASE("server menus: one in the world and one on the title screen is a player alone") {
  SessionConfig config = quiet();
  config.group_defeat_us = 2 * kSec;
  Room r(2, config);
  r.status(2, kTitle);
  r.status(1, status::dead);
  r.run(5 * kSec);
  CHECK(r.to<GroupDefeat>(1).empty());
  // Back in the world (loaded, not dead): still nobody down but player 1.
  r.status(2, 0);
  r.run(5 * kSec);
  CHECK(r.to<GroupDefeat>(1).empty());
}
