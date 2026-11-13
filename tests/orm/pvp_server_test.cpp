// The server's orm/pvp rules (server/orm_session.cpp, oot-remake docs/COMANDOS.md):
// the room's switches (any player sets them, everyone is told, a joiner gets
// them), PLAYER_HIT only with PvP on and to the victim alone, PLAYER_CALL
// (gather from the host only), PLAYER_RENAME (taken, refused, kept for
// joiners) and a fall to PvP that does not end the room's game.
#include <doctest/doctest.h>

#include <ostream>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "../server/orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;

namespace {

struct Sent {
  ConnectionId to;
  std::uint8_t channel;
  Message message;
};

// As group_settings_server_test.cpp's harness.
struct Room {
  Session session;
  std::vector<Sent> sent;
  std::uint64_t now = 1'000'000;

  explicit Room(SessionConfig config = {}) : session(orm_test::with_password(config)) {}

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        CHECK(check_received(m, Sender::server, s->channel) == Error::none);
        sent.push_back({s->to, s->channel, m});
      }
    }
  }
  void receive(ConnectionId id, const Message& m, std::uint8_t channel = kChannelSession) {
    std::vector<std::uint8_t> bytes;
    REQUIRE(encode(m, bytes) == Error::none);
    session.received(id, channel, bytes, now);
    collect();
  }
  void join(ConnectionId id, const std::string& nickname) {
    REQUIRE(session.connected(id, now, std::nullopt));
    collect();
    Hello h;
    h.build_id = "test 0.0";
    h.game_build_id = "e0f3ccfaf50065ae";
    h.game_version = "1.1.1";
    h.nickname = nickname;
    orm_test::join(session, id, h, now);
    collect();
  }
  template <typename T>
  std::vector<T> got(ConnectionId id) const {
    std::vector<T> out;
    for (const Sent& s : sent) {
      if (s.to != id) continue;
      if (const auto* m = std::get_if<T>(&s.message)) out.push_back(*m);
    }
    return out;
  }
  void tick() {
    session.tick(now);
    collect();
  }
};

RoomSwitch Ask(RoomSwitchKey key, bool on) {
  RoomSwitch m;
  m.entries.push_back({static_cast<std::uint8_t>(key), on});
  return m;
}

PlayerHit Hit(std::uint8_t victim, std::uint32_t id) {
  PlayerHit h;
  h.victim = victim;
  h.hit_id = id;
  h.damage = 2;
  return h;
}

}  // namespace

TEST_CASE("pvp server: any player sets a switch, everyone is told who and when, a joiner gets it") {
  Room r;
  r.join(1, "Anju");
  r.join(2, "Saria");
  r.now = 2'000'000;
  r.receive(2, Ask(RoomSwitchKey::pvp, true));
  for (ConnectionId id : {ConnectionId{1}, ConnectionId{2}}) {
    const auto got = r.got<RoomSwitch>(id);
    REQUIRE(got.size() == 1);
    CHECK(got[0].player_number == 2);
    CHECK(got[0].server_time_us == 2'000'000);
    CHECK(got[0].entries == std::vector<RoomSwitchEntry>{{1, true}});
  }
  r.receive(1, Ask(RoomSwitchKey::pvp, true));  // the same again: not relayed
  CHECK(r.got<RoomSwitch>(2).size() == 1);
  r.join(3, "Mido");
  const auto joiner = r.got<RoomSwitch>(3);
  REQUIRE(joiner.size() == 1);
  CHECK(joiner[0].player_number == 2);
  CHECK(joiner[0].server_time_us == 2'000'000);
  CHECK(joiner[0].entries[0].on);
}

TEST_CASE("pvp server: a hit goes to the victim alone, with the attacker's number, only with PvP on") {
  Room r;
  r.join(1, "Anju");
  r.join(2, "Saria");
  r.join(3, "Mido");
  r.receive(1, Hit(2, 1));
  CHECK(r.got<PlayerHit>(2).empty());  // PvP off
  r.receive(1, Ask(RoomSwitchKey::pvp, true));
  r.receive(1, Hit(2, 2));
  const auto got = r.got<PlayerHit>(2);
  REQUIRE(got.size() == 1);
  CHECK(got[0].player_number == 1);
  CHECK(got[0].hit_id == 2);
  CHECK(r.got<PlayerHit>(3).empty());
  CHECK(r.got<PlayerHit>(1).empty());
  r.receive(1, Hit(5, 3));  // nobody is 5
  CHECK(r.got<PlayerHit>(2).size() == 1);
}

TEST_CASE("pvp server: hits are held to the attacker's rate") {
  SessionConfig config;
  config.hits_per_second = 4;
  Room r(config);
  r.join(1, "Anju");
  r.join(2, "Saria");
  r.receive(1, Ask(RoomSwitchKey::pvp, true));
  for (std::uint32_t i = 1; i <= 10; ++i) r.receive(1, Hit(2, i));
  CHECK(r.got<PlayerHit>(2).size() == 4);
  r.now += 1'000'000;
  r.receive(1, Hit(2, 11));
  CHECK(r.got<PlayerHit>(2).size() == 5);
}

TEST_CASE("pvp server: come to one mate, gather from the host only") {
  Room r;
  r.join(1, "Anju");  // hosts: the first in
  r.join(2, "Saria");
  r.join(3, "Mido");
  r.receive(2, PlayerCall{0, 3, CallKind::come, 1});
  REQUIRE(r.got<PlayerCall>(3).size() == 1);
  CHECK(r.got<PlayerCall>(3)[0].player_number == 2);
  CHECK(r.got<PlayerCall>(1).empty());
  r.now += 2'000'000;
  r.receive(2, PlayerCall{0, 0, CallKind::gather, 2});  // not the host
  CHECK(r.got<PlayerCall>(1).empty());
  r.receive(1, PlayerCall{0, 0, CallKind::gather, 3});
  CHECK(r.got<PlayerCall>(2).size() == 1);
  CHECK(r.got<PlayerCall>(3).size() == 2);
  CHECK(r.got<PlayerCall>(1).empty());  // not to himself
  r.receive(1, PlayerCall{0, 0, CallKind::gather, 4});  // too soon after the last
  CHECK(r.got<PlayerCall>(2).size() == 1);
}

TEST_CASE("pvp server: a new name is taken for everyone and kept, a name in use is refused to the asker") {
  Room r;
  r.join(1, "Anju");
  r.join(2, "Saria");
  r.receive(1, PlayerRename{0, RenameResult::ok, "saria"});
  const auto refused = r.got<PlayerRename>(1);
  REQUIRE(refused.size() == 1);
  CHECK(refused[0].result == RenameResult::name_taken);
  CHECK(r.got<PlayerRename>(2).empty());
  r.now += 2'000'000;
  r.receive(1, PlayerRename{0, RenameResult::ok, "Anju-PC"});
  for (ConnectionId id : {ConnectionId{1}, ConnectionId{2}}) {
    const auto got = r.got<PlayerRename>(id);
    REQUIRE_FALSE(got.empty());
    CHECK(got.back().player_number == 1);
    CHECK(got.back().result == RenameResult::ok);
    CHECK(got.back().nickname == "Anju-PC");
  }
  r.join(3, "Mido");
  const auto welcome = r.got<Welcome>(3);
  REQUIRE(welcome.size() == 1);
  bool found = false;
  for (const PlayerInfo& p : welcome[0].players) found = found || (p.number == 1 && p.nickname == "Anju-PC");
  CHECK(found);
  // The old name is free again.
  r.join(4, "Anju");
  CHECK(r.got<Welcome>(4).size() == 1);
}

TEST_CASE("pvp server: a fall to PvP does not end the room's game at once") {
  SessionConfig config;
  config.group_defeat_us = 2'000'000;
  config.pvp_down_hold_us = 15'000'000;
  config.silence_timeout_us = 60'000'000;  // the test's clock jumps
  Room r(config);
  r.join(1, "Anju");
  r.join(2, "Saria");
  r.receive(1, Ask(RoomSwitchKey::pvp, true));
  r.receive(2, Hit(1, 1));                                  // Saria hits Anju
  r.receive(1, PlayerStatus{0, status::dead});              // Anju falls to it
  r.receive(2, PlayerStatus{0, status::dead});              // Saria falls to the world
  r.now += 3'000'000;
  r.tick();
  CHECK(r.got<GroupDefeat>(1).empty());  // Anju's fall counts as up for a while
  r.now += 15'000'000;
  r.tick();
  CHECK(r.got<GroupDefeat>(1).size() == 1);  // nobody could get anyone up: the defeat comes
}
