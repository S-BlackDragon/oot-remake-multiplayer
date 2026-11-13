// ORM pause and menus with partners (coop/host/menus.hpp; oot-remake docs/MENUS_PAUSA.md).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/menus.hpp"

using namespace coop::host::menus;
namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kFrame = 16'667;

Input world(std::uint64_t now, std::optional<bool> pause = false) {
  Input in;
  in.now_us = now;
  in.link = true;
  in.scene = true;
  in.is_pause = pause;
  return in;
}

Input nothing(std::uint64_t now) {
  Input in;
  in.now_us = now;
  return in;
}

}  // namespace

TEST_CASE("menus: the pause flag is Sequence_IsPause") {
  CHECK(coop::host::flag_hash("Sequence_IsPause") == kIsPauseFlag);
}

TEST_CASE("menus: status bits for each state") {
  CHECK(status_bits(State::playing) == 0);
  CHECK(status_bits(State::paused) == proto::status::paused);
  CHECK(status_bits(State::title) == (proto::status::loading | proto::orm_status::at_title));
  CHECK((kStatusMask & proto::status::dead) == 0);
  CHECK((kStatusMask & proto::status::in_cutscene) == 0);
}

TEST_CASE("menus: the pause follows Sequence_IsPause, not while the Ocarina plays") {
  Local l;
  std::uint64_t t = 1'000'000;
  CHECK_FALSE(l.frame(world(t += kFrame)));
  CHECK(l.frame(world(t += kFrame, true)) == State::paused);
  CHECK(l.status_bits() == proto::status::paused);
  CHECK_FALSE(l.frame(world(t += kFrame, true)));
  CHECK(l.frame(world(t += kFrame, false)) == State::playing);
  Input ocarina = world(t += kFrame, true);
  ocarina.ocarina = true;
  CHECK_FALSE(l.frame(ocarina));
  CHECK(l.state() == State::playing);
}

TEST_CASE("menus: an unreadable flag keeps the pause 0.5 s, then it is over") {
  Local l;
  std::uint64_t t = 1'000'000;
  REQUIRE(l.frame(world(t, true)) == State::paused);
  CHECK_FALSE(l.frame(world(t += 400'000, std::nullopt)));  // first unknown frame
  CHECK_FALSE(l.frame(world(t += 400'000, std::nullopt)));
  CHECK(l.frame(world(t += 200'000, std::nullopt)) == State::playing);
  // Unknown while playing never makes a pause.
  CHECK_FALSE(l.frame(world(t += kFrame, std::nullopt)));
}

TEST_CASE("menus: the title screen after 3 s with no Link and no scene, back with Link") {
  Local l;
  std::uint64_t t = 1'000'000;
  REQUIRE(l.frame(world(t, true)) == State::paused);  // the menu's Back to Title
  CHECK_FALSE(l.frame(nothing(t += kFrame)) == State::title);
  t += 2'900'000;
  CHECK(l.frame(nothing(t)) != State::title);
  CHECK(l.frame(nothing(t += 200'000)) == State::title);
  CHECK(l.status_bits() == (proto::status::loading | proto::orm_status::at_title));
  // The save loads: a scene, no Link yet: still the title.
  Input loading = nothing(t += kFrame);
  loading.scene = true;
  CHECK_FALSE(l.frame(loading));
  CHECK(l.frame(world(t += kFrame)) == State::playing);
}

TEST_CASE("menus: a scene load said by the game stops the title clock") {
  Local l;
  std::uint64_t t = 1'000'000;
  Input door = nothing(t);
  door.loading_scene = true;
  for (int i = 0; i < 300; ++i) {
    door.now_us = t += kFrame;
    CHECK_FALSE(l.frame(door));
  }
  CHECK(l.state() == State::playing);
}

TEST_CASE("menus: a mate's state and tag") {
  CHECK(mate_state(0) == State::playing);
  CHECK(mate_state(proto::status::paused | proto::status::dead) == State::paused);
  CHECK(mate_state(status_bits(State::title)) == State::title);
  CHECK(mate_state(status_bits(State::title) | proto::status::paused) == State::title);
  CHECK(tag_note(proto::status::paused) == "pausa");
  CHECK(tag_note(0).empty());
  CHECK(tag_note(status_bits(State::title)).empty());
}

TEST_CASE("menus: notices for the title screen and back, none for a pause or a first sight") {
  Mates m;
  const std::uint8_t title = status_bits(State::title);
  std::vector<Seen> seen{{2, "Ana", 0, true}, {3, "", title, false}};
  CHECK(m.frame(seen).empty());  // first sight

  seen[0].status_flags = proto::status::paused;
  auto c = m.frame(seen);
  REQUIRE(c.size() == 1);
  CHECK(c[0].to == State::paused);
  CHECK(c[0].notice.empty());

  seen[0].status_flags = title;
  c = m.frame(seen);
  REQUIRE(c.size() == 1);
  CHECK(c[0].notice == "Ana sali\xc3\xb3 al men\xc3\xba principal");

  seen[0].status_flags = 0;
  seen[1].status_flags = 0;
  seen[1].in_world = true;
  c = m.frame(seen);
  REQUIRE(c.size() == 2);
  CHECK(c[0].notice == "Ana volvi\xc3\xb3 a la partida");
  CHECK(c[1].notice == "P3 entr\xc3\xb3 en la partida");  // joined on its title screen

  // Gone from the list: forgotten; listed again, a first sight.
  CHECK(m.frame(std::vector<Seen>{{3, "", 0, true}}).empty());
  CHECK(m.frame(std::vector<Seen>{{2, "Ana", title, true}, {3, "", 0, true}}).empty());
}

TEST_CASE("menus: a title screen of one never in the world has no notice") {
  Mates m;
  std::vector<Seen> seen{{2, "Ana", 0, false}};
  CHECK(m.frame(seen).empty());
  seen[0].status_flags = status_bits(State::title);
  auto c = m.frame(seen);
  REQUIRE(c.size() == 1);
  CHECK(c[0].notice.empty());
}

TEST_CASE("menus: leave notices") {
  CHECK(leave_notice("Ana", proto::LeaveReason::left) == "Ana sali\xc3\xb3 de la sesi\xc3\xb3n");
  CHECK(leave_notice("Ana", proto::LeaveReason::timed_out) == "Ana perdi\xc3\xb3 la conexi\xc3\xb3n");
  CHECK(leave_notice("", proto::LeaveReason::kicked) == "Un jugador sali\xc3\xb3 de la sala (expulsado)");
  CHECK(leave_notice("Ana", static_cast<proto::LeaveReason>(9)) == "Ana sali\xc3\xb3 de la sesi\xc3\xb3n");
}

TEST_CASE("menus: a save from the menu counts, a door's autosave does not") {
  SaveWatch w;
  std::uint64_t t = 1'000'000;
  CHECK_FALSE(w.frame(t, false, std::nullopt));
  CHECK_FALSE(w.frame(t += kFrame, false, 100));  // the mark
  CHECK_FALSE(w.frame(t += kFrame, false, 200));  // a door
  CHECK_FALSE(w.frame(t += kFrame, true, 200));
  CHECK(w.frame(t += kFrame, true, 300));         // the Save screen
  CHECK_FALSE(w.frame(t += kFrame, true, 300));   // once
  CHECK_FALSE(w.frame(t += kFrame, false, 300));  // the menu closes
  CHECK(w.frame(t += 1'000'000, false, 400));     // landed as it closed
  CHECK_FALSE(w.frame(t += 3'000'000, false, 500));
}

TEST_CASE("menus: the guest's save notice") {
  CHECK(guest_save_notice("Ana", true) ==
        "Guardado en tu copia de la partida del grupo. La partida del grupo solo la guarda Ana (anfitri\xc3\xb3n).");
  CHECK(guest_save_notice("", false) ==
        "Guardado en tu propia partida. La partida del grupo solo la guarda el anfitri\xc3\xb3n.");
}

TEST_CASE("menus: the host's save given to us is not a menu save") {
  SaveWatch w;
  std::uint64_t t = 1'000'000;
  w.take_mark(50);                                 // before any reading: the first mark
  CHECK_FALSE(w.frame(t, true, 50));
  w.take_mark(200);                                // the host's save written while we pause
  CHECK_FALSE(w.frame(t += kFrame, true, 200));
  w.take_mark(100);                                // never back
  CHECK(w.frame(t += kFrame, true, 300));          // our own Save screen still counts
}

TEST_CASE("menus: a mate on the title screen") {
  CHECK(at_title(coop::protocol::status::loading | coop::protocol::orm_status::at_title));
  CHECK_FALSE(at_title(coop::protocol::status::loading));
  CHECK_FALSE(at_title(coop::protocol::status::paused | coop::protocol::status::dead));
}
