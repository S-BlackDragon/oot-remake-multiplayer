// ORM "a mate is down" sound (coop/host/mate_down_sound.hpp): once a fall, at
// his puppet when near, else on the interface.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "coop/host/mate_down_sound.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

Mate mate(std::uint8_t n, bool down, std::string scene = "KokiriForest_Child",
          proto::Vec3 at = {0.0f, 0.0f, 5.0f}) {
  Mate m;
  m.number = n;
  m.scene = std::move(scene);
  m.position = at;
  m.status = down ? proto::status::dead : 0;
  return m;
}

MateDownSoundInput room(std::uint64_t ms, std::vector<Mate> mates) {
  MateDownSoundInput in;
  in.now_us = ms * 1000;
  in.scene = "KokiriForest_Child";
  in.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  in.mates = std::move(mates);
  return in;
}

bool logged(const MateDownSoundOutput& o, const std::string& part) {
  for (const auto& l : o.log) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("mate down sound: a near mate's fall plays once at his puppet") {
  MateDownSound s;
  CHECK_FALSE(s.tick(room(0, {mate(2, false)})).cue);
  auto o = s.tick(room(100, {mate(2, true)}));
  REQUIRE(o.cue);
  CHECK(*o.cue == MateDownCue{2, true});
  CHECK(logged(o, "P2 is down: the sound at his puppet (5.0 away)"));
  // Still down: nothing more, however long.
  for (std::uint64_t t = 200; t < 20000; t += 500) {
    CHECK_FALSE(s.tick(room(t, {mate(2, true)})).cue);
  }
  // Up and down again: a new fall.
  CHECK_FALSE(s.tick(room(20000, {mate(2, false)})).cue);
  o = s.tick(room(20100, {mate(2, true)}));
  REQUIRE(o.cue);
  CHECK(o.cue->mate == 2);
}

TEST_CASE("mate down sound: far, in another scene or with no place: on the interface") {
  MateDownSound s;
  s.tick(room(0, {mate(2, false, "KokiriForest_Child", {0.0f, 0.0f, 30.0f}), mate(3, false, "Market_Child"),
                  mate(4, false)}));
  auto o = s.tick(room(100, {mate(2, true, "KokiriForest_Child", {0.0f, 0.0f, 30.0f}), mate(3, false, "Market_Child"),
                             mate(4, false)}));
  REQUIRE(o.cue);
  CHECK(*o.cue == MateDownCue{2, false});
  CHECK(logged(o, "P2 is down: the sound on the interface (30 away, beyond 20)"));

  o = s.tick(room(5000, {mate(2, true, "KokiriForest_Child", {0.0f, 0.0f, 30.0f}), mate(3, true, "Market_Child"),
                         mate(4, false)}));
  REQUIRE(o.cue);
  CHECK(*o.cue == MateDownCue{3, false});
  CHECK(logged(o, "P3 is down: the sound on the interface (in another scene)"));

  Mate lost = mate(4, true);
  lost.position.reset();
  o = s.tick(room(10000, {mate(2, true, "KokiriForest_Child", {0.0f, 0.0f, 30.0f}), mate(3, true, "Market_Child"),
                          lost}));
  REQUIRE(o.cue);
  CHECK(*o.cue == MateDownCue{4, false});
  CHECK(logged(o, "his place or ours not known"));
}

TEST_CASE("mate down sound: first seen down, gone and back, our game over, or close together: no sound") {
  MateDownSound s;
  // Seen down at once (our join, or his): no fall seen, no sound.
  CHECK_FALSE(s.tick(room(0, {mate(2, true)})).cue);
  CHECK_FALSE(s.tick(room(100, {mate(2, true)})).cue);
  // Gone (left, or on the title screen) and back down: none either.
  s.tick(room(200, {mate(3, false)}));
  CHECK_FALSE(s.tick(room(300, {mate(2, false), mate(3, false)})).cue);  // 2 back up: seen up now
  CHECK_FALSE(s.tick(room(400, {mate(3, false)})).cue);                  // 2 gone again
  CHECK_FALSE(s.tick(room(500, {mate(2, true), mate(3, false)})).cue);   // back, down

  // Two falls close together: one sound, the nearer one's.
  auto o = s.tick(room(1000, {mate(2, true), mate(3, true, "KokiriForest_Child", {1.0f, 0.0f, 0.0f})}));
  REQUIRE(o.cue);
  CHECK(*o.cue == MateDownCue{3, true});
  CHECK_FALSE(logged(o, "P2 is down"));  // 2 was down already
  s.tick(room(1100, {mate(2, false), mate(3, true)}));
  o = s.tick(room(1500, {mate(2, true), mate(3, true)}));
  CHECK_FALSE(o.cue);
  CHECK(logged(o, "P2 is down: no sound (one played 0.5 s ago)"));
  // The gap over: the next fall sounds.
  s.tick(room(4000, {mate(2, false), mate(3, true)}));
  CHECK(s.tick(room(4100, {mate(2, true), mate(3, true)})).cue);

  // Our own game over runs: the fall is seen, no sound.
  s.tick(room(9000, {mate(2, false), mate(3, true)}));
  auto over = room(9100, {mate(2, true), mate(3, true)});
  over.game_over = true;
  o = s.tick(over);
  CHECK_FALSE(o.cue);
  CHECK(logged(o, "no sound (our own game over runs)"));
  // Nor later for that same fall.
  CHECK_FALSE(s.tick(room(15000, {mate(2, true), mate(3, true)})).cue);
}

TEST_CASE("mate down sound: numbers out of range are not players") {
  MateDownSound s;
  s.tick(room(0, {mate(0, false), mate(7, false)}));
  CHECK_FALSE(s.tick(room(100, {mate(0, true), mate(7, true)})).cue);
}
