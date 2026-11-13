// ORM enemigos (oot-remake docs/ENEMIGOS.md, server/enemies.cpp): oot-64's
// rules for enemies with up to six players, driven without a network.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "coop/protocol/codec.hpp"
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

// Up to six players, as the remake's sessions; each joins with its number.
struct Room {
  Session session;
  std::vector<Sent> sent;
  std::uint64_t now = 1'000'000;

  explicit Room(int players, SessionConfig config = quiet()) : session(orm_test::with_password(std::move(config))) {
    for (int i = 1; i <= players; ++i) join(static_cast<ConnectionId>(i));
    sent.clear();
  }

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

  void list(ConnectionId id, const std::string& scene, std::vector<std::uint64_t> hashes,
            std::uint8_t flags = 0) {
    ObjectPresent p;
    p.scene = scene;
    for (std::uint64_t v : hashes) p.objects.push_back({ObjectId{ObjectKind::placed, 0, v}, flags});
    receive(id, p, kChannelOwnership);
  }

  void stand(ConnectionId id, const std::string& scene, float x) {
    PlayerSample s;
    s.time.server_time_us = 1;
    s.scene = scene;
    s.position = Vec3{x, 0, 0};
    receive(id, s, kChannelPlayerSamples);
  }

  void status(ConnectionId id, std::uint8_t flags) {
    receive(id, PlayerStatus{0, flags}, kChannelSession);
  }

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

ObjectId enemy(std::uint64_t v) { return ObjectId{ObjectKind::placed, 0, v}; }

ObjectState at(float x, std::int32_t life) {
  ObjectState s;
  s.time.server_time_us = 5;
  s.position = Vec3{x, 0, 0};
  s.life = ObjectLife{life, 12};
  return s;
}

std::optional<std::pair<std::uint8_t, std::uint16_t>> owned(std::uint8_t owner,
                                                            std::uint16_t epoch) {
  return std::make_pair(owner, epoch);
}

// Players 1..n in the Field with enemy 7 at x = 0, owned by player 1 (the
// first to list it), with life 12.
struct Field : Room {
  explicit Field(int players, SessionConfig config = quiet()) : Room(players, std::move(config)) {
    for (int i = 1; i <= players; ++i) list(static_cast<ConnectionId>(i), "Field", {7});
    receive(1, ObjectSample{0, enemy(7), 0, at(0, 12)}, kChannelObjectSamples);
    sent.clear();
  }
};

// The owner's sample says it is in a fight (12 s lock), its life unchanged:
// not the owner's own hit.
void fight(Room& r) {
  ObjectState s = at(0, 12);
  s.state = object_state::in_fight;
  r.receive(1, ObjectSample{0, enemy(7), 0, s}, kChannelObjectSamples);
}

// The owner's own hit: its life fell with no DAMAGE from another player.
void owner_hits(Room& r, std::int32_t life) {
  r.receive(1, ObjectSample{0, enemy(7), 0, at(0, life)}, kChannelObjectSamples);
}

}  // namespace

// ---------------------------------------------------------------- downed owner

TEST_CASE("enemigos: a downed owner gives its enemy to a standing player after 1 s, in a fight") {
  Field r(2);
  r.stand(1, "Field", 2);
  r.stand(2, "Field", 20);
  fight(r);
  r.sent.clear();
  r.status(1, status::dead);
  r.run(750'000);
  CHECK(r.to<HandoverAsk>(1).empty());
  r.run(500'000);
  REQUIRE(r.to<HandoverAsk>(1).size() == 1);
  CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{enemy(7), 0, 2});
  CHECK(r.session.stats().downed_handed == 1);
  r.receive(1, HandoverState{0, enemy(7), 0, at(1, 11)}, kChannelOwnership);
  CHECK(r.session.object_owner(enemy(7)) == owned(2, 1));
}

TEST_CASE("enemigos: up again within the second, the downed owner keeps it") {
  Field r(2);
  fight(r);
  r.status(1, status::dead);
  r.run(500'000);
  r.status(1, 0);  // revived (or the fairy)
  r.run(3 * kSec);
  CHECK(r.to<HandoverAsk>(1).empty());
}

TEST_CASE("enemigos: with everyone else downed too the enemy stays where it is") {
  Field r(3);
  fight(r);
  r.status(2, status::dead);
  r.status(3, status::dead);
  r.status(1, status::dead);
  r.run(3 * kSec);
  CHECK(r.to<HandoverAsk>(1).empty());
  CHECK(r.session.object_owner(enemy(7)) == owned(1, 0));
}

TEST_CASE("enemigos: owner_downed_us 0 keeps daurosk's 10 s") {
  SessionConfig config = quiet();
  config.owner_downed_us = 0;
  Field r(2, config);
  r.status(1, status::dead);
  r.run(9'750'000);
  CHECK(r.to<HandoverAsk>(1).empty());
  r.run(500'000);
  CHECK(r.to<HandoverAsk>(1).size() == 1);
}

// ------------------------------------------------- owner leaves or disconnects

TEST_CASE("enemigos: an owner that leaves gives its enemy to the nearest standing player, not a downed one") {
  for (int how = 0; how < 3; ++how) {
    CAPTURE(how);
    Field r(4);
    r.stand(1, "Field", 0);
    r.stand(2, "Field", 2);   // nearest, downed
    r.stand(3, "Field", 25);  // standing, far
    r.stand(4, "Field", 12);  // standing, nearer: takes it
    r.status(2, status::dead);
    r.sent.clear();
    if (how == 0) r.session.disconnected(1, r.now);           // the game closes
    if (how == 1) r.stand(1, "Kakariko", 0);                  // walks out of the scene
    if (how == 2) r.list(1, "Field", {});                     // unloads it
    r.collect();
    CHECK(r.session.object_owner(enemy(7)) == owned(4, 1));
    CHECK(r.to<HandoverState>(4).size() == 1);  // with the last state (life 12)
  }
}

TEST_CASE("enemigos: with only downed players left, the nearest of them keeps it shared") {
  Field r(3);
  r.stand(2, "Field", 9);
  r.stand(3, "Field", 4);
  r.status(2, status::dead);
  r.status(3, status::dead);
  r.session.disconnected(1, r.now);
  r.collect();
  CHECK(r.session.object_owner(enemy(7)) == owned(3, 1));
}

// --------------------------------------------- the nearest standing in a fight

TEST_CASE("enemigos: in a fight the enemy goes to the player clearly nearer it, held 3 s") {
  Field r(3);
  r.stand(1, "Field", 30);  // the owner fought it and walked off
  r.stand(2, "Field", 3);   // fights it now
  r.stand(3, "Field", 5);
  fight(r);
  r.sent.clear();
  r.run(2'750'000);
  CHECK(r.to<HandoverAsk>(1).empty());
  r.run(500'000);
  REQUIRE(r.to<HandoverAsk>(1).size() == 1);
  CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{enemy(7), 0, 2});  // the nearest of the two
  CHECK(r.session.stats().fight_near_moved == 1);
  r.receive(1, HandoverState{0, enemy(7), 0, at(0, 11)}, kChannelOwnership);
  REQUIRE(r.session.object_owner(enemy(7)) == owned(2, 1));

  // Player 3 steps right beside it: 2 is 3 m off, 3 is 0.5 m off: not 6 m
  // nearer, nothing moves; nor within 8 s of the change anyway.
  r.stand(3, "Field", 0.5f);
  r.receive(2, ObjectSample{0, enemy(7), 1, at(0, 10)}, kChannelObjectSamples);
  r.sent.clear();
  r.run(10 * kSec);
  CHECK(r.to<HandoverAsk>(2).empty());
}

TEST_CASE("enemigos: no near-player move without the margin, beyond 10 m, downed, or while the owner hits it") {
  SUBCASE("5 m nearer only") {
    Field r(2);
    r.stand(1, "Field", 8);
    r.stand(2, "Field", 3);
    fight(r);
    r.sent.clear();
    r.run(4 * kSec);
    CHECK(r.to<HandoverAsk>(1).empty());
  }
  SUBCASE("12 m from it") {
    Field r(2);
    r.stand(1, "Field", 35);
    r.stand(2, "Field", 12);
    fight(r);
    r.sent.clear();
    r.run(4 * kSec);
    CHECK(r.to<HandoverAsk>(1).empty());
  }
  SUBCASE("the near one is downed") {
    Field r(2);
    r.stand(1, "Field", 30);
    r.stand(2, "Field", 1);
    r.status(2, status::dead);
    fight(r);
    r.sent.clear();
    r.run(4 * kSec);
    CHECK(r.to<HandoverAsk>(1).empty());
  }
  SUBCASE("its owner keeps hitting it (an arrow from afar)") {
    Field r(2);
    r.stand(1, "Field", 30);
    r.stand(2, "Field", 3);
    r.sent.clear();
    for (int life = 11; life > 6; --life) {
      owner_hits(r, life);
      r.run(kSec);
    }
    CHECK(r.to<HandoverAsk>(1).empty());
  }
  SUBCASE("the switch off") {
    SessionConfig config = quiet();
    config.fight_near_hold_us = 0;
    Field r(2, config);
    r.stand(1, "Field", 30);
    r.stand(2, "Field", 3);
    fight(r);
    r.sent.clear();
    r.run(4 * kSec);
    CHECK(r.to<HandoverAsk>(1).empty());
  }
}

TEST_CASE("enemigos: a boss keeps its own near rule, not the enemy one") {
  SessionConfig config = quiet();
  config.boss_near_hold_us = 0;  // the boss rule off: an enemy rule must not stand in for it
  Field r(2, config);
  r.receive(1, ObjectGroup{0, {{enemy(7), enemy(7), object_group::boss}}}, kChannelOwnership);
  r.stand(1, "Field", 30);
  r.stand(2, "Field", 3);
  fight(r);
  r.sent.clear();
  r.run(5 * kSec);
  CHECK(r.to<HandoverAsk>(1).empty());
  CHECK(r.session.stats().fight_near_moved == 0);
}

// ------------------------------------- ORM boss families: a grab keeps the grabber (JEFES 9)

namespace {

// The owner's sample says its enemy holds its Link (a Like Like, a Wallmaster): owner_keeps.
void holds(Room& r, bool fight_too = true) {
  ObjectState s = at(0, 12);
  s.state = static_cast<std::uint8_t>(object_state::owner_keeps | (fight_too ? object_state::in_fight : 0));
  r.receive(1, ObjectSample{0, enemy(7), 0, s}, kChannelObjectSamples);
}

}  // namespace

TEST_CASE("enemigos: an enemy that holds its owner's Link is not moved to a nearer player nor to a hitter") {
  SUBCASE("the fight's near rule waits") {
    Field r(2);
    r.stand(1, "Field", 30);
    r.stand(2, "Field", 3);
    holds(r);
    r.sent.clear();
    r.run(4 * kSec);
    CHECK(r.to<HandoverAsk>(1).empty());
    CHECK(r.session.stats().fight_near_moved == 0);
    CHECK(r.session.stats().owner_kept >= 1);
    fight(r);  // let go: the next check asks
    r.run(500'000);
    REQUIRE(r.to<HandoverAsk>(1).size() == 1);
    CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{enemy(7), 0, 2});
  }
  SUBCASE("a hit goes to the owner and moves nothing") {
    Field r(2);
    r.stand(1, "Field", 30);
    r.stand(2, "Field", 1);
    holds(r, false);
    r.sent.clear();
    r.receive(2, Damage{0, enemy(7), 0, 1, 1, {}}, kChannelOwnership);
    CHECK(r.to<Damage>(1).size() == 1);  // the mate's sword on the Like Like frees him (ZMP D-135)
    CHECK(r.to<HandoverAsk>(1).empty());
    CHECK(r.session.stats().owner_kept == 1);
    r.receive(1, ObjectSample{0, enemy(7), 0, at(0, 11)}, kChannelObjectSamples);  // let go
    r.run(2 * kSec);  // a while later (the fight rule holds 3 s)
    r.sent.clear();
    r.receive(2, Damage{0, enemy(7), 0, 1, 1, {}}, kChannelOwnership);
    REQUIRE(r.to<HandoverAsk>(1).size() == 1);
    CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{enemy(7), 0, 2});
  }
}

// ------------------------------------------------------- mini-boss, two hitters

TEST_CASE("enemigos: two players hitting a mini-boss at once: both hits count, it dies once") {
  SessionConfig config = quiet();
  config.boss_hit_handover = true;  // test10 keeps a hurt boss with its owner by default
  Field r(3, config);
  // An Iron Knuckle: the hook sends it alone with the boss flag (coop_enemies.h).
  r.receive(1, ObjectGroup{0, {{enemy(7), enemy(7), object_group::boss}}}, kChannelOwnership);
  r.stand(1, "Field", 2);
  r.stand(2, "Field", 1);
  r.stand(3, "Field", 1.5f);
  r.sent.clear();
  r.receive(2, Damage{0, enemy(7), 0, 4, 1, {}}, kChannelOwnership);
  r.receive(3, Damage{0, enemy(7), 0, 4, 1, {}}, kChannelOwnership);
  // Both reach the owner, whatever the hand-over does.
  CHECK(r.to<Damage>(1).size() == 2);
  // The first hitter is asked for (issue 150), once: the second hit finds an ask running.
  REQUIRE(r.to<HandoverAsk>(1).size() == 1);
  CHECK(r.to<HandoverAsk>(1)[0] == HandoverAsk{enemy(7), 0, 2});
  // The owner answers before its game took them: 12 - 4 - 4 for the new owner.
  r.receive(1, HandoverState{0, enemy(7), 0, at(0, 12)}, kChannelOwnership);
  REQUIRE(r.to<HandoverState>(2).size() == 1);
  REQUIRE(r.to<HandoverState>(2)[0].state.life.has_value());
  CHECK(r.to<HandoverState>(2)[0].state.life->life == 4);
  CHECK(r.session.object_owner(enemy(7)) == owned(2, 1));

  // The old owner's own last hit killed it before it heard: dies for all, once.
  r.sent.clear();
  r.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {0, 0, 1}}, kChannelOwnership);
  CHECK(r.to<ObjectRemove>(2).size() == 1);
  CHECK(r.to<ObjectRemove>(3).size() == 1);
  // The new owner's game kills it too on that news and says so: not again.
  r.receive(2, ObjectRemove{0, enemy(7), 1, RemoveReason::dead, {0, 0, 1}}, kChannelOwnership);
  CHECK(r.to<ObjectRemove>(1).empty());
  CHECK(r.to<ObjectRemove>(3).size() == 1);
  CHECK(r.session.object_owner(enemy(7)) == owned(0, 1));  // nobody's: removed
}

TEST_CASE("enemigos: six players' hits on one mini-boss all go to its one owner") {
  Field r(6);
  r.receive(1, ObjectGroup{0, {{enemy(7), enemy(7), object_group::boss}}}, kChannelOwnership);
  owner_hits(r, 11);  // the owner hits it too
  for (ConnectionId p = 1; p <= 6; ++p) r.stand(p, "Field", static_cast<float>(p));
  r.sent.clear();
  for (ConnectionId p = 2; p <= 6; ++p) r.receive(p, Damage{0, enemy(7), 0, 1, 1, {}}, kChannelOwnership);
  CHECK(r.to<Damage>(1).size() == 5);
  for (ConnectionId p = 2; p <= 6; ++p) CHECK(r.to<Damage>(p).empty());
  CHECK(r.to<HandoverAsk>(1).empty());  // its owner hit it just now: it stays
}

// ------------------------------------------- a kill and a reload of its room

TEST_CASE("enemigos: a kill stays dead for a player who reloads its room while another stayed in it") {
  Room r(3);
  r.stand(1, "DekuTree", 0);
  r.list(1, "DekuTree", {7});
  r.stand(2, "DekuTree", 2);
  r.list(2, "DekuTree", {7});
  REQUIRE(r.session.object_owner(enemy(7)) == owned(1, 0));
  r.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}, kChannelOwnership);
  r.list(1, "DekuTree", {});
  r.list(2, "DekuTree", {});  // its copy died here too, beside the place

  SUBCASE("it left the scene and came back, player 1 still beside the place") {
    r.stand(2, "KokiriForest", 0);
    r.list(2, "KokiriForest", {});
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2) ==
          std::vector<ObjectRemove>{{1, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}});
    CHECK_FALSE(r.session.object_owner(enemy(7)).has_value());
    CHECK(r.session.stats().dead_kept_inside == 1);
    CHECK(r.session.stats().dead_respawned == 0);
    // Once: its next list (the copy dying) brings nothing more.
    r.sent.clear();
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).empty());
    // Its copy dies beside the place and a later listing is a respawn of the
    // enemy itself (one that comes back by itself): alive again.
    r.stand(2, "DekuTree", 3);
    r.list(2, "DekuTree", {});
    r.list(2, "DekuTree", {7});
    CHECK(r.session.stats().dead_respawned == 1);
    CHECK(r.session.object_owner(enemy(7)) == owned(2, 0));
  }
  SUBCASE("it walked off within the scene (its room unloaded) and back") {
    r.stand(2, "DekuTree", 50);
    r.list(2, "DekuTree", {});  // 49 m from the place: it left that room
    r.sent.clear();
    r.stand(2, "DekuTree", 4);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).size() == 1);
    CHECK(r.session.stats().dead_kept_inside == 1);
  }
  SUBCASE("nobody stayed near the place: the reload is a respawn, as before") {
    r.stand(1, "DekuTree", 100);  // player 1 is in another room
    r.stand(2, "KokiriForest", 0);
    r.list(2, "KokiriForest", {});
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).empty());
    CHECK(r.session.object_owner(enemy(7)) == owned(2, 0));
    CHECK(r.session.stats().dead_respawned == 1);
  }
  SUBCASE("the one who stayed is downed beside it: the room is still loaded") {
    r.status(1, status::dead);
    r.stand(2, "KokiriForest", 0);
    r.list(2, "KokiriForest", {});
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).size() == 1);
  }
}

TEST_CASE("enemigos: the room itself decides before the radius (RIESGOS_RESUELTOS, Mundo e historia)") {
  std::vector<std::string> lines;
  SessionConfig config = quiet();
  config.log = [&lines](const std::string& line) { lines.push_back(line); };
  Room r(3, config);
  // Enemy 7 and pot 8 in one room; players 1 and 2 have both loaded.
  r.stand(1, "DekuTree", 0);
  r.list(1, "DekuTree", {7, 8});
  r.stand(2, "DekuTree", 2);
  r.list(2, "DekuTree", {7, 8});
  r.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}, kChannelOwnership);
  r.list(1, "DekuTree", {8});
  r.list(2, "DekuTree", {8});
  const auto line_with = [&lines](const std::string& part) {
    for (const auto& l : lines) {
      if (l.find(part) != std::string::npos) return true;
    }
    return false;
  };
  // Player 2 leaves the scene and comes back: its game loads the room anew.
  r.stand(2, "KokiriForest", 0);
  r.list(2, "KokiriForest", {});

  SUBCASE("player 1 far by the radius but still listing the room's pot: it stays dead") {
    r.stand(1, "DekuTree", 100);  // 99 m: the old radius alone said respawn
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7, 8});
    CHECK(r.to<ObjectRemove>(2).size() == 1);
    CHECK(r.session.stats().dead_kept_inside == 1);
    CHECK(line_with("a kill again: another player stayed in its room (player 1 has its room loaded"));
  }
  SUBCASE("player 1 lists nothing of the room: the radius decides, and the line says so") {
    r.list(1, "DekuTree", {});  // beside the place, yet its game lists no object of that room
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7, 8});
    CHECK(r.to<ObjectRemove>(2).size() == 1);  // kept dead: the lesser error
    CHECK(line_with("no other game lists the 1 objects loaded with it: player 1 stands 1 m from it"));
  }
  SUBCASE("player 1 lists nothing of the room and stands far: it lives again") {
    r.list(1, "DekuTree", {});
    r.stand(1, "DekuTree", 100);
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7, 8});
    CHECK(r.to<ObjectRemove>(2).empty());
    CHECK(r.session.stats().dead_respawned == 1);
    CHECK(line_with("loaded a kill's room again: it lives (no other game lists the 1 objects loaded with it, "
                    "and nobody within 40 m on its floor)"));
  }
}

TEST_CASE("enemigos: with nothing else in the room, the radius counts only the same floor") {
  std::vector<std::string> lines;
  SessionConfig config = quiet();
  config.log = [&lines](const std::string& line) { lines.push_back(line); };
  Room r(3, config);
  r.stand(1, "DekuTree", 0);
  r.list(1, "DekuTree", {7});
  r.stand(2, "DekuTree", 2);
  r.list(2, "DekuTree", {7});
  r.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}, kChannelOwnership);
  r.list(1, "DekuTree", {});
  r.list(2, "DekuTree", {});
  r.stand(2, "KokiriForest", 0);
  r.list(2, "KokiriForest", {});
  const auto height = [&r](float y) {
    PlayerSample s;
    s.time.server_time_us = 1;
    s.scene = "DekuTree";
    s.position = Vec3{1, y, 0};
    r.receive(1, s, kChannelPlayerSamples);
  };
  const auto last_has = [&lines](const std::string& part) {
    for (const auto& l : lines) {
      if (l.find(part) != std::string::npos) return true;
    }
    return false;
  };
  SUBCASE("player 1 one floor up (15 m): another room, it lives again") {
    height(15);
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).empty());
    CHECK(last_has("no other object in its room to tell by, and nobody within 40 m on its floor"));
  }
  SUBCASE("player 1 on the same floor (3 m up): it stays dead, the fallback said so") {
    height(3);
    r.sent.clear();
    r.stand(2, "DekuTree", 30);
    r.list(2, "DekuTree", {7});
    CHECK(r.to<ObjectRemove>(2).size() == 1);
    CHECK(last_has("no other object in its room to tell by: player 1 stands"));
  }
  SUBCASE("dead_room_up_m 0: any height counts, as before") {
    Room q(2, [] {
      SessionConfig c = quiet();
      c.dead_room_up_m = 0;
      return c;
    }());
    q.stand(1, "DekuTree", 0);
    q.list(1, "DekuTree", {7});
    q.stand(2, "DekuTree", 2);
    q.list(2, "DekuTree", {7});
    q.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}, kChannelOwnership);
    q.list(1, "DekuTree", {});
    q.list(2, "DekuTree", {});
    q.stand(2, "KokiriForest", 0);
    q.list(2, "KokiriForest", {});
    PlayerSample s;
    s.time.server_time_us = 1;
    s.scene = "DekuTree";
    s.position = Vec3{1, 15, 0};
    q.receive(1, s, kChannelPlayerSamples);
    q.sent.clear();
    q.stand(2, "DekuTree", 30);
    q.list(2, "DekuTree", {7});
    CHECK(q.to<ObjectRemove>(2).size() == 1);
  }
}

TEST_CASE("enemigos: a newcomer is told the kill as before, and an empty scene still forgets it") {
  Room r(3);
  r.stand(1, "DekuTree", 0);
  r.list(1, "DekuTree", {7});
  r.receive(1, ObjectRemove{0, enemy(7), 0, RemoveReason::dead, {1, 0, 0}}, kChannelOwnership);
  r.list(1, "DekuTree", {});
  r.sent.clear();
  r.stand(3, "DekuTree", 40);
  r.list(3, "DekuTree", {7});
  CHECK(r.to<ObjectRemove>(3).size() == 1);
  CHECK(r.session.stats().dead_told == 1);
  CHECK(r.session.stats().dead_kept_inside == 0);
  // Everyone out: the next game in loads the room anew.
  r.stand(1, "KokiriForest", 0);
  r.list(1, "KokiriForest", {});
  r.stand(3, "KokiriForest", 0);
  r.list(3, "KokiriForest", {});
  r.sent.clear();
  r.stand(2, "DekuTree", 0);
  r.list(2, "DekuTree", {7});
  CHECK(r.to<ObjectRemove>(2).empty());
  CHECK(r.session.object_owner(enemy(7)) == owned(2, 0));
}
