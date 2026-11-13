// ORM: the relay sends a PLAYER_POSE only to the sender's place
// (server/orm_interest.hpp, oot-remake docs/ANCHO_DE_BANDA.md).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints the std::string_view results
#include <string>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "orm_interest.hpp"
#include "orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;

namespace {

struct Relay {
  Session session;
  std::uint64_t now = 1'000'000;
  std::vector<std::pair<ConnectionId, Message>> sent;

  explicit Relay(SessionConfig config = {}) : session(orm_test::with_password(std::move(config))) {}

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        sent.emplace_back(s->to, std::move(m));
      }
    }
  }
  void receive(ConnectionId id, const Message& m) {
    std::vector<std::uint8_t> bytes;
    REQUIRE(encode(m, bytes) == Error::none);
    session.received(id, channel_of(m), bytes, now);
    collect();
  }
  void join(ConnectionId id, const std::string& name) {
    REQUIRE(session.connected(id, now, std::nullopt));
    Hello h;
    h.build_id = "test 0.0";
    h.game_build_id = "e0f3ccfaf50065ae";
    h.game_version = "1.1.1";
    h.nickname = name;
    orm_test::join(session, id, h, now);
    collect();
  }
  void in_scene(ConnectionId id, const std::string& scene) {
    PlayerSample s;
    s.time = {now, 1};
    s.position = Vec3{0, 0, 0};
    s.scene = scene;
    receive(id, s);
  }
  // The players a pose from `from` reached.
  std::vector<ConnectionId> pose_from(ConnectionId from) {
    sent.clear();
    PlayerPose p;
    p.time = {now, 1};
    p.skeleton = 7;
    PoseBone b;
    b.bone = 1;
    b.rotation = std::array<std::int16_t, 4>{0, 0, 0, 32767};
    p.bones.push_back(b);
    receive(from, p);
    std::vector<ConnectionId> to;
    for (const auto& [id, m] : sent) {
      if (const auto* pose = std::get_if<PlayerPose>(&m)) {
        CHECK(pose->player_number != 0);  // the relay names the sender
        to.push_back(id);
      }
    }
    return to;
  }
};

}  // namespace

TEST_CASE("orm interest: a scene's place leaves out only the era") {
  CHECK(coop::server::orm::place_of("Graveyard_Child") == "Graveyard");
  CHECK(coop::server::orm::place_of("Graveyard_Adult") == "Graveyard");
  CHECK(coop::server::orm::place_of("Market") == "Market");
  CHECK(coop::server::orm::place_of("_Adult") == "_Adult");  // nothing before the era
  CHECK(coop::server::orm::pose_goes("Graveyard_Child", "Graveyard_Adult"));
  CHECK_FALSE(coop::server::orm::pose_goes("Kakariko", "Market"));
  CHECK(coop::server::orm::pose_goes("", "Market"));  // not known: sent
  CHECK(coop::server::orm::pose_goes("Market", ""));
}

TEST_CASE("orm interest: a pose goes only to the players in the sender's place") {
  Relay r;
  for (ConnectionId id = 1; id <= 6; ++id) r.join(id, "P" + std::to_string(id));
  // Before anyone's scene is known, every pose goes to everyone, as before.
  CHECK(r.pose_from(1).size() == 5);
  r.in_scene(1, "Market");
  r.in_scene(2, "Market");
  r.in_scene(3, "Kakariko");
  r.in_scene(4, "Graveyard_Adult");
  r.in_scene(5, "Graveyard_Child");
  // 6 has sent no sample: its scene is not known, so it still gets poses.
  CHECK(r.pose_from(1) == std::vector<ConnectionId>{2, 6});
  CHECK(r.pose_from(3) == std::vector<ConnectionId>{6});
  CHECK(r.pose_from(4) == std::vector<ConnectionId>{5, 6});  // either era
  CHECK(r.pose_from(6) == std::vector<ConnectionId>{1, 2, 3, 4, 5});  // its own not known
  CHECK(r.session.stats().pose_other_place == 3 + 4 + 3);
  // Samples still go to everyone: the map and the warp need them.
  r.sent.clear();
  r.in_scene(3, "Kakariko");
  std::size_t samples = 0;
  for (const auto& s : r.sent) samples += std::holds_alternative<PlayerSample>(s.second) ? 1 : 0;
  CHECK(samples == 5);
}

TEST_CASE("orm interest: pose_same_place off sends every pose to everyone") {
  SessionConfig config;
  config.pose_same_place = false;
  Relay r(config);
  for (ConnectionId id = 1; id <= 3; ++id) r.join(id, "P" + std::to_string(id));
  r.in_scene(1, "Market");
  r.in_scene(2, "Kakariko");
  r.in_scene(3, "Kakariko");
  CHECK(r.pose_from(1).size() == 2);
  CHECK(r.session.stats().pose_other_place == 0);
}
