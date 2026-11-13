// ORM: coop/host/shoulder.hpp (docs/RIESGOS_RESUELTOS.md "Mundo e historia"): L shared by the quick
// messages (L + d-pad) and the warp-song invitation (L held 0.75 s).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results
#include <cstdint>
#include <optional>
#include <string>

#include "coop/host/shoulder.hpp"
#include "coop/host/songs.hpp"

using coop::host::ShoulderArbiter;
namespace qc = coop::host::quick_chat;

namespace {

ShoulderArbiter both() {
  ShoulderArbiter a(qc::kL);
  a.set_quick(true);
  a.set_invite(qc::kL);
  return a;
}

bool noted(ShoulderArbiter& a, const std::string& part) {
  for (const auto& line : a.take_notes()) {
    if (line.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("shoulder: L alone is the invitation's hold, L + d-pad a quick message that cancels it") {
  ShoulderArbiter a = both();
  CHECK(noted(a, "share a button"));
  a.step(qc::kL);
  CHECK(a.invite_held());
  CHECK(a.mask() == qc::kDpad);
  CHECK_FALSE(a.take_quick());
  a.step(qc::kL | qc::kDUp);
  CHECK(a.take_quick() == qc::Quick::here);
  CHECK_FALSE(a.invite_held());  // cancelled at once
  CHECK(noted(a, "DUP while the button is held: a quick message; the invitation's hold is cancelled"));
  a.step(qc::kL);  // the d-pad let go, L still held: still cancelled
  CHECK_FALSE(a.invite_held());
  CHECK_FALSE(a.take_quick());
  a.step(0);  // L let go
  CHECK_FALSE(a.invite_held());
  a.step(qc::kL);  // a new hold
  CHECK(a.invite_held());
}

TEST_CASE("shoulder: reading the pad twice a frame changes nothing, whoever reads first") {
  ShoulderArbiter a = both();
  a.step(qc::kL);
  a.step(qc::kL | qc::kDLeft);
  a.step(qc::kL | qc::kDLeft);  // the songs glue after the quick chat glue
  CHECK(a.take_quick() == qc::Quick::help);
  CHECK_FALSE(a.take_quick());  // one press, one message
  CHECK_FALSE(a.invite_held());
}

TEST_CASE("shoulder: the invitation's 0.75 s never completes across a quick message") {
  ShoulderArbiter a = both();
  // WarpInvite times `held` (0.75 s); here the hold it would see is summed frame by frame.
  std::uint64_t held_us = 0;
  std::uint64_t longest = 0;
  for (int f = 0; f < 60; ++f) {  // one second at 60 fps; DRIGHT at 0.5 s
    a.step(f >= 30 && f < 33 ? qc::kL | qc::kDRight : qc::kL);
    held_us = a.invite_held() ? held_us + 16'667 : 0;
    longest = held_us > longest ? held_us : longest;
  }
  CHECK(longest < 750'000);
  CHECK(a.take_quick() == qc::Quick::go);
}

TEST_CASE("shoulder: a d-pad already down when L goes down is neither") {
  ShoulderArbiter a = both();
  a.step(qc::kDDown);
  a.step(qc::kDDown | qc::kL);
  CHECK_FALSE(a.take_quick());
  CHECK(a.invite_held());
}

TEST_CASE("shoulder: quick messages off, or another button for them: nothing is arbitrated") {
  ShoulderArbiter off(qc::kL);
  off.set_invite(qc::kL);
  CHECK(off.take_notes().empty());
  off.step(qc::kL | qc::kDUp);
  CHECK_FALSE(off.take_quick());
  CHECK(off.invite_held());
  CHECK(off.mask() == 0);

  ShoulderArbiter apart(qc::kZR);
  apart.set_quick(true);
  apart.set_invite(qc::kL);
  CHECK(apart.take_notes().empty());
  apart.step(qc::kL);
  apart.step(qc::kL | qc::kZR | qc::kDUp);
  CHECK(apart.take_quick() == qc::Quick::here);
  CHECK(apart.invite_held());  // L is not the quick button: not cancelled
}

TEST_CASE("shoulder: when the game uses L the invitation takes L and its fallback together") {
  ShoulderArbiter a = both();
  a.set_invite(qc::kL | qc::kR);
  a.step(qc::kL);
  CHECK_FALSE(a.invite_held());  // L alone is the game's now
  a.step(qc::kL | qc::kR);
  CHECK(a.invite_held());
  a.step(qc::kL | qc::kR | qc::kDUp);  // a quick message still cancels it
  CHECK(a.take_quick() == qc::Quick::here);
  CHECK_FALSE(a.invite_held());
  a.step(std::nullopt);  // the pad gone: all forgotten
  CHECK_FALSE(a.invite_held());
  CHECK_FALSE(a.cancelled());
}

TEST_CASE("shoulder: InviteButton gives the bits the invitation needs, its own or with the fallback") {
  coop::host::InviteButton free_l("L", qc::kL, "R", qc::kR);
  CHECK(free_l.bits() == qc::kL);
  coop::host::InviteButton taken("L", qc::kL, "R", qc::kR);
  coop::host::InviteButton::Input in;
  in.standing = true;
  std::uint64_t now = 1'000'000;
  for (int press = 0; press < 3; ++press) {
    in.now_us = now;
    in.buttons = 0;
    in.animation = "Wait";
    taken.frame(in);
    now += 400'000;
    in.now_us = now;
    in.buttons = qc::kL;
    taken.frame(in);
    in.now_us = now + 100'000;
    in.animation = "Guard";  // the game answers L
    taken.frame(in);
    in.now_us = now + 350'000;
    taken.frame(in);
    now += 1'000'000;
    in.now_us = now;
    in.buttons = 0;
    in.animation = "Wait";
    taken.frame(in);
    now += 400'000;
  }
  REQUIRE(taken.decided());
  CHECK(taken.taken());
  CHECK(taken.bits() == (qc::kL | qc::kR));
}
