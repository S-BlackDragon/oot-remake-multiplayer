// ORM (oot-remake docs/PUERTAS.md): coop/host/passages.hpp.
#include <doctest/doctest.h>

#include <optional>
#include <ostream>  // doctest prints the std::string_view results
#include <string_view>

#include "coop/host/passages.hpp"

using coop::host::Passage;
using coop::host::Passages;
using coop::protocol::Vec3;

namespace {
constexpr std::string_view kKak = "KakarikoVillage_Child";
constexpr std::string_view kField = "HyruleField_Child";
}  // namespace

TEST_CASE("passages: a player walking out is drawn a moment, then parked where it left, then let go") {
  Passages p;
  const Vec3 door{-131.0f, 112.0f, 52.6f};
  CHECK(p.note(2, kKak, kKak, {-128, 112, 52}, 0) == Passage::none);  // first note: here
  CHECK(p.note(2, kKak, kKak, door, 33) == Passage::none);
  // Their newest sample names the field (their load began): the walk into
  // the exit plays out for leave_show_ms, on the shown pose.
  CHECK(p.note(2, kKak, kField, door, 66) == Passage::leaving);
  CHECK(std::string_view(p.why(2)) == "walking out");
  CHECK(p.note(2, kKak, kField, door, 66 + 149) == Passage::leaving);
  CHECK(p.note(2, kKak, kField, door, 66 + 150) == Passage::gone);
  CHECK(std::string_view(p.why(2)) == "left the scene");
  CHECK(p.note(2, kKak, kField, {200, 159, 19}, 66 + 1149) == Passage::gone);
  CHECK(p.note(2, kKak, kField, {200, 159, 19}, 66 + 1150) == Passage::none);  // let go
  CHECK(p.note(2, kKak, kField, {200, 159, 19}, 66 + 5000) == Passage::none);
  // Not knowing their scene (their load) is leaving too.
  Passages q;
  q.note(3, kKak, kKak, door, 0);
  CHECK(q.note(3, kKak, std::nullopt, door, 20) == Passage::leaving);
}

TEST_CASE("passages: one not planned for a while before its scene changed gets no passage") {
  Passages p;
  p.note(2, kKak, kKak, {0, 100, 0}, 0);
  // Stalled (faded, not noted) for 2 s, then seen in another scene: it was
  // gone already, and its shown pose is the other scene's.
  CHECK(p.note(2, kKak, kField, {200, 159, 19}, 2000) == Passage::none);
  CHECK(p.note(2, kKak, kField, {200, 159, 19}, 2050) == Passage::none);
}

TEST_CASE("passages: a player coming in is kept out of sight until it moved and settled") {
  Passages p;
  const Vec3 stale{200.0f, 159.0f, 19.0f};  // where it stood in the field
  CHECK(p.note(4, kKak, kField, stale, 0) == Passage::none);
  CHECK(p.note(4, kKak, kField, stale, 500) == Passage::none);
  // Its first samples with our scene still carry the field's place.
  CHECK(p.note(4, kKak, kKak, stale, 600) == Passage::arriving);
  CHECK(std::string_view(p.why(4)) == "arriving");
  CHECK(p.note(4, kKak, kKak, {200.5f, 159.0f, 19.0f}, 700) == Passage::arriving);  // within 1 m
  // At the entrance now (the slide of the shown pose may still go on).
  CHECK(p.note(4, kKak, kKak, {-119.8f, 96.6f, 58.7f}, 800) == Passage::arriving);
  CHECK(p.note(4, kKak, kKak, {-119.5f, 96.6f, 58.2f}, 1199) == Passage::arriving);
  CHECK(p.note(4, kKak, kKak, {-119.0f, 96.6f, 57.5f}, 1200) == Passage::none);  // 400 ms after
  CHECK(p.note(4, kKak, kKak, {-118.0f, 96.6f, 56.0f}, 1300) == Passage::none);
}

TEST_CASE("passages: one that never moves from its old place is shown after the timeout") {
  Passages p;
  const Vec3 spot{5.0f, 100.0f, 5.0f};
  p.note(5, kKak, kField, spot, 0);
  CHECK(p.note(5, kKak, kKak, spot, 100) == Passage::arriving);
  CHECK(p.note(5, kKak, kKak, spot, 3099) == Passage::arriving);
  CHECK(p.note(5, kKak, kKak, spot, 3100) == Passage::none);
  CHECK(std::string_view(p.why(5)) == "arrived (waited the most)");
  CHECK(p.note(5, kKak, kKak, spot, 3200) == Passage::none);
  CHECK(std::string_view(p.why(5)) == "");
}

TEST_CASE("passages: whoever is in our new place at our first note there is no arrival") {
  Passages p;
  p.note(2, kField, kField, {0, 0, 0}, 0);
  p.note(3, kField, kKak, {1, 1, 1}, 0);
  // We go to Kakariko: 3 was there before us, 2 is now elsewhere (no
  // leaving: we are the ones who left).
  CHECK(p.note(3, kKak, kKak, {1, 1, 1}, 100) == Passage::none);
  CHECK(p.note(2, kKak, kField, {0, 0, 0}, 100) == Passage::none);
  // 2 follows us through the same exit a second later: an arrival.
  CHECK(p.note(2, kKak, kKak, {-119.8f, 96.6f, 58.7f}, 1100) == Passage::arriving);
  // Our own load (scene not known) forgets everyone.
  CHECK(p.note(2, std::nullopt, kKak, {0, 0, 0}, 1200) == Passage::none);
  CHECK(p.note(2, kKak, kKak, {0, 0, 0}, 1300) == Passage::none);
}

TEST_CASE("passages: the twin scenes of the two eras are one place, numbers out of range give none") {
  Passages p;
  p.note(2, "Graveyard_Child", "Graveyard_Adult", {0, 0, 0}, 0);
  CHECK(p.note(2, "Graveyard_Child", "Graveyard_Adult", {0, 0, 0}, 50) == Passage::none);
  CHECK(p.note(0, kKak, kField, {0, 0, 0}, 0) == Passage::none);
  CHECK(p.note(7, kKak, kField, {0, 0, 0}, 0) == Passage::none);
  p.forget(2);
  CHECK(p.note(2, "Graveyard_Child", kField, {0, 0, 0}, 60) == Passage::none);  // first note again
}
