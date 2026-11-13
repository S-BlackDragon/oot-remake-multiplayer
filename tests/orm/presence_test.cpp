// ORM cases moved out of upstream's tests/presence/presence_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <limits>
#include <cstdint>
#include <string>

#include "coop/presence/presence.hpp"

using namespace coop::presence;
namespace proto = coop::protocol;

namespace {

// A presence that knows it is player 1 and that player 2 is in.
Presence two_players(Settings settings = {}) {
  Presence p(settings);
  proto::Welcome w;
  w.player_number = 1;
  w.players = {{2, "Two"}};
  p.on_welcome(w);
  return p;
}

}  // namespace

// ORM: presence is the one store of the others' PLAYER_STATUS flags (the
// downed glue, the NPC claim, the HUD tags, the spectator and the escape read
// it). A status is sent only on a change, so one left behind (in a cutscene
// when he went) must not stick to the next player given that number, and a
// reconnect forgets them all (the server sends the non-zero ones again after
// WELCOME).
TEST_CASE("presence: a status goes with its player (leave, rejoin, welcome)") {
  Presence p = two_players();
  CHECK(p.on_status({2, proto::status::in_cutscene}));
  CHECK_FALSE(p.on_status({proto::kPlayerSelf, proto::status::paused}));  // not relayed
  CHECK_FALSE(p.on_status({1, proto::status::paused}));                   // our own number
  CHECK(p.player(2)->has_flag(proto::status::in_cutscene));
  CHECK(p.on_left(proto::PlayerLeft{2, proto::LeaveReason::left}));
  CHECK_FALSE(p.player(2));
  CHECK(p.on_joined({2, "Next"}));
  CHECK(p.player(2)->flags() == 0);
  // Back in while a PLAYER_LEFT was lost: started afresh.
  CHECK(p.on_status({2, proto::status::dead}));
  CHECK(p.on_joined({2, "Again"}));
  CHECK(p.player(2)->flags() == 0);
  CHECK(p.on_status({2, proto::status::dead}));
  proto::Welcome w;
  w.player_number = 1;
  w.players = {{2, "Two"}};
  p.on_welcome(w);
  CHECK(p.player(2)->flags() == 0);
}

TEST_CASE("presence: a player's colour from PLAYER_LOOK, kept until it leaves (ORM)") {
  Presence p;
  proto::Welcome w;
  w.player_number = 1;
  w.players = {{2, "Ana"}, {3, "Ben"}};
  p.on_welcome(w);
  CHECK_FALSE(p.player(2)->color());
  CHECK(p.apply(proto::PlayerLook{2, {10, 20, 30}, 0}));
  REQUIRE(p.player(2)->color());
  CHECK(*p.player(2)->color() == proto::Rgb{10, 20, 30});
  CHECK_FALSE(p.player(3)->color());
  // Ours, or a number not in: ignored.
  CHECK_FALSE(p.apply(proto::PlayerLook{1, {1, 1, 1}, 0}));
  CHECK_FALSE(p.apply(proto::PlayerLook{5, {1, 1, 1}, 0}));
  // A change; then a leave and a new player in that number start with none.
  CHECK(p.on_look(proto::PlayerLook{2, {40, 50, 60}, 0}));
  CHECK(*p.player(2)->color() == proto::Rgb{40, 50, 60});
  p.apply(proto::PlayerLeft{2, proto::LeaveReason::left});
  p.apply(proto::PlayerJoined{2, "Cid"});
  CHECK_FALSE(p.player(2)->color());
}
