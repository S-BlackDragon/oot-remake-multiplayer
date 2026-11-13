// Tests for the pause detector (coop/host/pause_detect.hpp; docs/MENUS_PAUSA.md, docs/
// RIESGOS_RESUELTOS.md "Mundo e historia"): Sequence_IsPause once seen set is trusted, a
// frozen frame says paused until then, and alone when the flag cannot be read or stays 0
// through two frozen pauses.
// Mutants these catch: the rate check dropped (a held pose reads as a pause): "a pose with no
// rate never freezes"; the frozen frame kept after the flag is trusted: "a hitch after the
// flag was seen is no pause"; no fallback: "a flag that stays 0".
#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

#include "coop/host/pause_detect.hpp"

namespace {

using coop::host::PauseDetect;
using coop::host::PauseInput;
using coop::host::PauseOutput;
using coop::host::PauseSource;
namespace protocol = coop::protocol;

constexpr std::uint64_t kFrame = 16'667;

// Our game, frame by frame: playing (the frame moves) or paused (all stands).
struct Game {
  std::uint64_t now = 1'000'000;
  float frame = 0.0f;
  protocol::Vec3 at{10.0f, 100.0f, -5.0f};
  std::optional<bool> flag = false;  // Sequence_IsPause as the game sets it
  bool paused = false;
  bool set_flag_in_pause = true;     // the remake sets the flag in a pause
  float rate = 1.0f;
  std::vector<std::string> log;

  PauseOutput step(PauseDetect& d) {
    now += kFrame;
    if (!paused) frame += rate;
    PauseInput in;
    in.now_us = now;
    in.link = true;
    in.position = at;
    in.animation = protocol::Animation{"Wait", frame, rate};
    in.is_pause = flag ? std::optional<bool>(paused && set_flag_in_pause) : std::nullopt;
    auto o = d.frame(in);
    for (auto& l : o.log) log.push_back(l);
    return o;
  }
  // Runs `seconds`, returns whether the last frame said paused.
  bool run(PauseDetect& d, double seconds) {
    PauseOutput o;
    const int n = static_cast<int>(seconds * 1e6 / kFrame);
    for (int i = 0; i < n; ++i) o = step(d);
    return o.is_pause.value_or(false);
  }
};

bool has(const std::vector<std::string>& log, const std::string& part) {
  for (const auto& l : log) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("pause detect: at first the flag or a frozen frame, the flag set in a pause is trusted") {
  PauseDetect d(PauseDetect::Mode::either);
  Game g;
  CHECK_FALSE(g.run(d, 2));
  CHECK(has(g.log, "pause detector: Sequence_IsPause or a frozen frame, until the flag is seen set in a pause"));
  CHECK(d.source() == PauseSource::flag_or_frozen);
  g.paused = true;
  CHECK(g.run(d, 0.1));  // the flag says it at once
  CHECK(d.source() == PauseSource::flag);
  CHECK(has(g.log, "pause detector: Sequence_IsPause (set in a pause: trusted from now)"));
  g.paused = false;
  CHECK_FALSE(g.run(d, 1));
}

TEST_CASE("pause detect: a hitch after the flag was seen is no pause") {
  PauseDetect d(PauseDetect::Mode::either);
  Game g;
  g.run(d, 1);
  g.paused = true;
  g.run(d, 1);
  g.paused = false;
  g.run(d, 1);
  REQUIRE(d.source() == PauseSource::flag);
  // The emulator hitches 2 s (the frame stands) with the flag at 0: not a pause.
  g.paused = true;
  g.set_flag_in_pause = false;
  CHECK_FALSE(g.run(d, 2));
}

TEST_CASE("pause detect: a flag that stays 0 through two frozen pauses gives way to the frozen frame") {
  PauseDetect d(PauseDetect::Mode::either);
  Game g;
  g.set_flag_in_pause = false;  // another flag pauses the remake
  g.run(d, 1);
  g.paused = true;
  CHECK_FALSE(g.run(d, 0.3));  // under half a second: not yet
  CHECK(g.run(d, 1.2));        // frozen: paused, the flag or the frame
  CHECK(d.source() == PauseSource::flag_or_frozen);
  g.paused = false;
  CHECK_FALSE(g.run(d, 1));
  g.paused = true;
  CHECK(g.run(d, 1.5));
  g.paused = false;
  CHECK_FALSE(g.run(d, 0.5));
  CHECK(d.source() == PauseSource::frozen);
  CHECK(has(g.log, "pause detector: a frozen frame (Sequence_IsPause stayed 0 through 2 frozen pauses)"));
  g.paused = true;
  CHECK(g.run(d, 1));
}

TEST_CASE("pause detect: an unreadable flag gives way to the frozen frame after 2 s") {
  PauseDetect d(PauseDetect::Mode::either);
  Game g;
  g.flag = std::nullopt;
  g.run(d, 2.5);
  CHECK(d.source() == PauseSource::frozen);
  CHECK(has(g.log, "pause detector: a frozen frame (Sequence_IsPause not readable for 2 s)"));
  g.paused = true;
  CHECK(g.run(d, 1));
  g.paused = false;
  CHECK_FALSE(g.run(d, 0.5));
  // Readable again and set in a pause: trusted.
  g.flag = false;
  g.paused = true;
  g.run(d, 0.1);
  CHECK(d.source() == PauseSource::flag);
}

TEST_CASE("pause detect: a pose with no rate, an event, a load or the Ocarina never freezes") {
  SUBCASE("no rate") {
    PauseDetect d(PauseDetect::Mode::either);
    Game g;
    g.rate = 0.0f;  // a held pose: the frame stands in play too
    g.flag = std::nullopt;
    CHECK_FALSE(g.run(d, 5));
  }
  SUBCASE("an event, a scene load, the Ocarina") {
    for (int which = 0; which < 3; ++which) {
      PauseDetect d(PauseDetect::Mode::either);
      std::uint64_t now = 1'000'000;
      PauseOutput o;
      for (int i = 0; i < 120; ++i) {
        now += kFrame;
        PauseInput in;
        in.now_us = now;
        in.link = true;
        in.position = protocol::Vec3{1.0f, 2.0f, 3.0f};
        in.animation = protocol::Animation{"DemoWait", 4.0f, 1.0f};  // stands: a cutscene's pose
        in.is_pause = false;
        in.event_running = which == 0;
        in.loading_scene = which == 1;
        in.ocarina = which == 2;
        o = d.frame(in);
      }
      CAPTURE(which);
      CHECK_FALSE(o.frozen);
      CHECK(o.is_pause == std::optional<bool>(false));
    }
  }
}

TEST_CASE("pause detect: LICHII_ORM_PAUSE_DETECT=flag keeps the flag alone") {
  PauseDetect d(PauseDetect::Mode::flag_only);
  Game g;
  g.set_flag_in_pause = false;
  g.run(d, 1);
  g.paused = true;
  CHECK_FALSE(g.run(d, 3));
  CHECK(d.source() == PauseSource::flag);
  CHECK(has(g.log, "pause detector: Sequence_IsPause alone (LICHII_ORM_PAUSE_DETECT=flag)"));
}

// Seen in a game (docs/SONDAS.md row 4): the flag is set in a real pause, and a Link standing idle
// reads as a frozen frame. The default trusts the flag from the start.
TEST_CASE("pause detect: flag first by default: an idle Link is no pause, the flag is") {
  PauseDetect d;
  Game g;
  CHECK(d.source() == PauseSource::flag);
  g.run(d, 0.1);
  CHECK(has(g.log, "pause detector: Sequence_IsPause, a frozen frame only while it cannot be read"));
  g.paused = true;
  g.set_flag_in_pause = false;  // standing idle: the frame does not move, the flag is 0
  CHECK_FALSE(g.run(d, 3));
  CHECK_FALSE(g.run(d, 3));
  CHECK(d.source() == PauseSource::flag);  // never dropped for idles
  g.paused = false;
  g.run(d, 0.5);
  g.paused = true;
  g.set_flag_in_pause = true;  // the pause menu
  CHECK(g.run(d, 0.1));
  g.paused = false;
  CHECK_FALSE(g.run(d, 0.5));
  // Unreadable: the frozen frame while it lasts, the flag again once it reads.
  g.flag = std::nullopt;
  g.run(d, 2.5);
  CHECK(d.source() == PauseSource::frozen);
  g.flag = false;
  g.run(d, 0.1);
  CHECK(d.source() == PauseSource::flag);
  CHECK(has(g.log, "pause detector: Sequence_IsPause (readable again)"));
  CHECK(PauseDetect::mode_of("") == PauseDetect::Mode::flag_first);
  CHECK(PauseDetect::mode_of("either") == PauseDetect::Mode::either);
  CHECK(PauseDetect::mode_of("flag") == PauseDetect::Mode::flag_only);
}
