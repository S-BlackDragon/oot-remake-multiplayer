// Tests for the shared clock (coop/host/clock.hpp; D-R12, oot-64 D-062): host-owned, followers
// nudged smoothly, sets only on joining, arriving outdoors, too far off and jumps.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "coop/host/clock.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kTickUs = 250'000;
constexpr std::uint32_t kDay = proto::kSecondsPerDay;

// One game: its clock runs at `rate` in-game seconds a real second while outdoors and stands
// indoors; it takes the clock's sets (after `set_delay` ticks, standing `stop_ticks` after, as
// the game's time change does) and nudges (unless `nudges_put_back`).
struct Game {
  std::uint8_t number = 1;
  double seconds = 36'000.0;
  double rate = 120.0;
  bool outdoors = true;
  bool loading = false;
  bool nudges_put_back = false;
  int stop_ticks = 0;
  Clock clock;
  int sets = 0;
  std::vector<ClockSetWhy> whys;
  std::int32_t nudged = 0;  // applied since the last tick
  int held = 0, lost = 0;
  std::vector<std::string> log;
  std::int64_t nudge_total = 0;
  bool wrapped_by_nudge = false;

  explicit Game(std::uint8_t n, double s = 36'000.0, ClockSettings st = {})
      : number(n), seconds(s), clock(st) {}

  std::uint32_t now() const {
    return static_cast<std::uint32_t>(std::fmod(std::floor(seconds), kDay));
  }
  void advance() {
    if (stop_ticks > 0) {
      --stop_ticks;
      return;
    }
    if (outdoors && !loading) seconds = std::fmod(seconds + rate * kTickUs / 1e6, kDay);
  }
};

struct World {
  std::vector<Game*> games;
  std::uint8_t host = 1;
  std::uint64_t now_us = 1'000'000;
  std::vector<proto::TimeOfDay> sent;  // everything sent (with the sender's number)

  void tick() {
    now_us += kTickUs;
    std::vector<std::pair<std::uint8_t, proto::TimeOfDay>> out;
    for (Game* g : games) {
      g->advance();
      ClockInput in;
      in.now_us = now_us;
      if (!g->loading) in.game = g->now();
      in.host = g->number == host;
      in.host_number = host;
      in.our_number = g->number;
      in.nudged = g->nudged;
      in.nudges_held = g->held;
      in.nudges_lost = g->lost;
      g->nudged = 0;
      g->held = g->lost = 0;
      auto o = g->clock.tick(in);
      for (auto& l : o.log) g->log.push_back(l);
      if (o.set) {
        ++g->sets;
        g->whys.push_back(o.why);
        g->seconds = *o.set;
        g->stop_ticks = 8;  // the time change stands the clock about two seconds here
      } else if (o.nudge != 0) {
        if (g->nudges_put_back) {
          if (std::abs(o.nudge) >= 10) ++g->lost;
        } else {
          if (g->seconds + o.nudge >= kDay || g->seconds + o.nudge < 0) g->wrapped_by_nudge = true;
          g->seconds = std::fmod(g->seconds + o.nudge + kDay, kDay);
          g->nudged = o.nudge;
          g->nudge_total += o.nudge;
          if (std::abs(o.nudge) >= 10) ++g->held;
        }
      }
      for (auto m : o.send) {
        m.player_number = g->number;
        out.emplace_back(g->number, m);
        sent.push_back(m);
      }
    }
    for (auto& [from, m] : out) {
      for (Game* g : games) {
        if (g->number != from) g->clock.receive(m, now_us + 20'000);  // 20 ms on the way
      }
    }
  }
  void run_s(double seconds) {
    const int n = static_cast<int>(seconds * 1e6 / kTickUs);
    for (int i = 0; i < n; ++i) tick();
  }
};

std::int32_t off(const Game& a, const Game& b) { return clock_ahead(a.now(), b.now()); }

bool has(const std::vector<std::string>& log, const std::string& part) {
  for (const auto& l : log) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("clock: ahead goes the short way round midnight, text is HH:MM:SS") {
  CHECK(clock_ahead(100, 160) == 60);
  CHECK(clock_ahead(160, 100) == -60);
  CHECK(clock_ahead(86'390, 10) == 20);
  CHECK(clock_ahead(10, 86'390) == -20);
  CHECK(clock_text(0) == "00:00:00");
  CHECK(clock_text(36'075) == "10:01:15");
}

TEST_CASE("clock: the host says the shared clock once a second, a guest reports its own") {
  Game a(1), b(2);
  World w{{&a, &b}};
  w.run_s(10);
  int from_a = 0, from_b = 0;
  for (const auto& m : w.sent) {
    CHECK((m.flags & proto::time_of_day::jump) == 0);
    (m.player_number == 1 ? from_a : from_b)++;
  }
  CHECK(from_a >= 9);
  CHECK(from_a <= 11);
  CHECK(from_b >= 9);
  CHECK(from_b <= 11);
  CHECK(a.sets == 0);
  CHECK(b.sets == 0);
}

TEST_CASE("clock: a guest joining far off takes the host's clock once with the setter") {
  Game a(1, 36'000), b(2, 60'000);
  World w{{&a, &b}};
  w.run_s(20);
  REQUIRE(b.sets == 1);
  CHECK(b.whys[0] == ClockSetWhy::join);
  CHECK(std::abs(off(b, a)) <= 40);
  CHECK(a.sets == 0);
  CHECK(has(b.log, "joined: the host's clock is"));
}

TEST_CASE("clock: a guest a minute off is nudged, never set, and the sky never jumps") {
  Game a(1, 36'000), b(2, 36'000 - 100);  // within join_set_s: no set
  World w{{&a, &b}};
  double before = b.seconds;
  double worst_step = 0, least_step = 1e9;
  for (int i = 0; i < 120; ++i) {
    w.tick();
    const double step = std::fmod(b.seconds - before + kDay, kDay);
    if (i > 8) {  // after the first words
      worst_step = std::max(worst_step, step);
      least_step = std::min(least_step, step);
    }
    before = b.seconds;
  }
  CHECK(b.sets == 0);
  CHECK(std::abs(off(b, a)) <= 20);
  // One tick is 30 in-game seconds at 120 s/s; a nudge adds at most half of that.
  CHECK(worst_step <= 45.0 + 1);
  CHECK(least_step >= 0.0);
  CHECK(b.nudge_total > 60);
  CHECK(has(b.log, "catching up"));
  CHECK(has(b.log, "in step with the shared clock"));
}

TEST_CASE("clock: an indoor clock stands and is not nudged, arriving far off sets it") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  b.outdoors = false;  // B goes into a house for a while; the host stays out
  const std::int64_t nudged_before = b.nudge_total;
  w.run_s(40);         // 40 real s: the host runs 4800 in-game s on
  CHECK(b.nudge_total == nudged_before);
  CHECK(b.sets == 0);
  b.outdoors = true;   // back out: the scene has just loaded
  w.run_s(10);
  REQUIRE(b.sets == 1);
  CHECK(b.whys[0] == ClockSetWhy::arrival);
  w.run_s(15);
  CHECK(std::abs(off(b, a)) <= 20);
}

TEST_CASE("clock: indoors a short while, the guest catches up by nudges") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  b.outdoors = false;
  w.run_s(5);  // 600 in-game s behind: below arrival_set_s
  b.outdoors = true;
  w.run_s(40);
  CHECK(b.sets == 0);
  CHECK(std::abs(off(b, a)) <= 20);
}

TEST_CASE("clock: the host indoors, a mate outdoors carries the shared clock, nobody out: it stands") {
  Game a(1, 36'000), b(2, 36'000), c(3, 36'000);
  World w{{&a, &b, &c}};
  w.run_s(5);
  a.outdoors = false;  // the host goes into a dungeon
  w.run_s(20);
  CHECK(has(a.log, "the shared clock is P2's"));
  CHECK(std::abs(off(c, b)) <= 20);  // C follows B through the host
  CHECK(c.sets == 0);
  CHECK(b.sets == 0);
  b.outdoors = false;
  c.outdoors = false;
  w.run_s(10);
  CHECK(has(a.log, "the shared clock stands"));
  const auto shared = a.clock.status().shared;
  w.run_s(5);
  CHECK(a.clock.status().shared == shared);
}

TEST_CASE("clock: the host comes back out far behind a mate's clock and follows it, then owns it") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  a.outdoors = false;
  w.run_s(40);  // B kept the shared clock running: the host is 4800 s behind
  a.outdoors = true;
  w.run_s(10);
  REQUIRE(a.sets == 1);
  CHECK(a.whys[0] == ClockSetWhy::arrival);
  CHECK(b.sets == 0);  // B never moved
  w.run_s(20);
  CHECK(std::abs(off(a, b)) <= 20);
  CHECK(has(a.log, "the shared clock is ours"));
  CHECK(a.clock.status().anchor == 1);
}

TEST_CASE("clock: a guest's Sun's Song jumps everyone, host included, without bouncing back") {
  Game a(1, 36'000), b(2, 36'000), c(3, 36'000);
  World w{{&a, &b, &c}};
  w.run_s(5);
  b.seconds = 0;  // the Sun's Song in B's game: midnight
  b.stop_ticks = 4;
  w.run_s(5);
  int jumps = 0;
  for (const auto& m : w.sent) {
    if ((m.flags & proto::time_of_day::jump) != 0) {
      ++jumps;
      CHECK(m.player_number == 2);
    }
  }
  CHECK(jumps == 1);
  CHECK(b.sets == 0);  // its own jump: not set again
  REQUIRE(a.sets == 1);
  CHECK(a.whys[0] == ClockSetWhy::jump);
  REQUIRE(c.sets == 1);
  w.run_s(30);
  CHECK(a.sets == 1);  // nobody goes back to the old time
  CHECK(b.sets == 0);
  CHECK(c.sets == 1);
  CHECK(std::abs(off(a, b)) <= 20);
  CHECK(std::abs(off(c, b)) <= 20);
  CHECK(a.now() < 3'600 + 40 * 120);  // night, not the old morning
}

TEST_CASE("clock: a time-lapse is sent once, where it settles") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  for (int i = 0; i < 12; ++i) {  // a cutscene runs the sky an hour a tick for 3 s
    a.seconds = std::fmod(a.seconds + 3'600, kDay);
    w.tick();
  }
  w.run_s(15);  // B ignores the host's old words for 5 s after the jump, then nudges
  std::vector<std::uint32_t> jumps;
  for (const auto& m : w.sent) {
    if ((m.flags & proto::time_of_day::jump) != 0) jumps.push_back(m.seconds);
  }
  REQUIRE(jumps.size() == 1);
  CHECK(b.sets == 1);
  CHECK(std::abs(off(b, a)) <= 20);
}

TEST_CASE("clock: no jump after a load, and none for our own set") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  a.loading = true;  // the host loads a save: its clock comes back elsewhere
  w.run_s(3);
  a.seconds = 70'000;
  a.loading = false;
  w.run_s(3);
  b.clock.our_set(10'000, w.now_us);  // the test switch's quiet offset
  b.seconds = 10'000;
  w.run_s(3);
  for (const auto& m : w.sent) CHECK((m.flags & proto::time_of_day::jump) == 0);
}

TEST_CASE("clock: nudges the game puts back turn nudging off, the setter stands in past five minutes") {
  Game a(1, 36'000), b(2, 36'000 - 110);
  b.nudges_put_back = true;
  World w{{&a, &b}};
  w.run_s(5);
  b.rate = 100.0;  // B runs slow and drifts
  w.run_s(10);
  CHECK(has(b.log, "nudges do not hold"));
  CHECK(has(b.log, "setting instead, at a scene load past 20 s off"));
  CHECK_FALSE(b.clock.status().nudges);
  w.run_s(200);  // 200 s at 20 s/s slower: past five minutes off within 15 s, then again
  REQUIRE(b.sets >= 2);
  for (const auto why : b.whys) CHECK(why == ClockSetWhy::no_nudge);  // never the hour rule's
  CHECK(has(b.log, "(nudges do not hold)"));
  CHECK(b.sets <= 5);  // at most once a minute (200 s: the first one and three more)
  CHECK(std::abs(off(b, a)) < 3'600);
}

TEST_CASE("clock: nudges put back, coming outdoors a few minutes off sets at the load") {
  Game a(1, 36'000), b(2, 36'000 - 110);
  b.nudges_put_back = true;
  World w{{&a, &b}};
  w.run_s(15);
  REQUIRE(has(b.log, "nudges do not hold"));
  CHECK(b.sets == 0);  // 110 s off: below five minutes, the setter waits for a load
  b.outdoors = false;  // a house: the host runs 600 in-game s on
  w.run_s(5);
  b.outdoors = true;
  w.run_s(5);
  REQUIRE(b.sets == 1);
  CHECK(b.whys[0] == ClockSetWhy::no_nudge_arrival);
  CHECK(has(b.log, "(arriving outdoors, nudges do not hold)"));
  CHECK(std::abs(off(b, a)) < 300);  // the time change stands B's clock two seconds here
}

TEST_CASE("clock: nudges that hold never take the fallback sets") {
  Game a(1, 36'000), b(2, 36'000 - 110);
  World w{{&a, &b}};
  w.run_s(5);
  b.outdoors = false;
  w.run_s(5);
  b.outdoors = true;
  w.run_s(40);
  CHECK(b.sets == 0);
  CHECK_FALSE(has(b.log, "nudges do not hold"));
}

TEST_CASE("clock: a nudge never crosses midnight") {
  Game a(1, 86'390), b(2, 86'300);
  World w{{&a, &b}};
  w.run_s(10);
  CHECK_FALSE(b.wrapped_by_nudge);
  CHECK(b.nudge_total > 0);
  CHECK(b.sets == 0);
  CHECK(std::abs(off(b, a)) <= 20);
}

TEST_CASE("clock: a new host keeps the clock where it was") {
  Game a(1, 36'000), b(2, 36'000);
  World w{{&a, &b}};
  w.run_s(5);
  w.host = 2;  // the host leaves the room: B hosts
  w.games = {&b};
  w.run_s(5);
  CHECK(has(b.log, "we host"));
  CHECK(b.sets == 0);
  REQUIRE(b.clock.status().shared);
  CHECK(std::abs(clock_ahead(b.now(), *b.clock.status().shared)) <= 1);
}

TEST_CASE("clock: words from ourselves, bad values and unknown bits are dropped") {
  Clock c;
  c.receive(proto::TimeOfDay{2, kDay, 0}, 1);
  c.receive(proto::TimeOfDay{2, 100, 0x80}, 1);
  c.receive(proto::TimeOfDay{0, 100, 0}, 1);
  ClockInput in;
  in.now_us = 2'000'000;
  in.game = 1'000;
  in.host = false;
  in.host_number = 2;
  in.our_number = 1;
  const auto out = c.tick(in);
  CHECK_FALSE(out.set);
  CHECK(out.nudge == 0);
  CHECK_FALSE(c.status().shared);
}

TEST_CASE("clock: disconnected resets") {
  Game a(1, 36'000), b(2, 60'000);
  World w{{&a, &b}};
  w.run_s(5);
  CHECK(b.sets == 1);
  ClockInput in;
  in.connected = false;
  b.clock.tick(in);
  CHECK_FALSE(b.clock.status().shared);
}
