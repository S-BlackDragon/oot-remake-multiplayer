// ORM on-screen statistics (coop/host/orm_stats.hpp, oot-remake docs/COMANDOS.md
// "Estadísticas"): the saved toggles, the corner lines, the reports, the fps
// meter and the kbps rate.
#include <doctest/doctest.h>

#include <ostream>

#include <string>
#include <vector>

#include "coop/host/orm_stats.hpp"

using namespace coop::host::orm::stats;

TEST_CASE("stats: the toggles round-trip through the file, unknown lines skipped") {
  Prefs p;
  CHECK(decode(encode(p)) == p);
  p.fps = true;
  p.net = true;
  p.panel = false;
  p.mute = true;
  CHECK(decode(encode(p)) == p);
  const Prefs q = decode("# x\r\nfps=1\r\nping = 1\nnuevo=1\nred=2\nmapa=0\n");
  CHECK(q.fps);
  CHECK(q.ping);
  CHECK_FALSE(q.net);  // "2" is not a value
  CHECK_FALSE(q.map);
  CHECK(q.panel);      // not in the file: the default
  CHECK(decode("") == Prefs{});
}

TEST_CASE("stats: the corner shows what is switched on, and dashes for what is not known") {
  Input in;
  in.fps = 59.6f;
  in.ping_ms = 45;
  in.up_kbps = 12.4;
  in.down_kbps = 30.0;
  in.loss_percent = 0;
  in.puppets = 5;
  in.hook_ms = 1.23f;
  Prefs p;
  CHECK(corner_lines(p, in).empty());
  p.fps = true;
  CHECK(corner_lines(p, in) == std::vector<std::string>{"60 fps"});
  p.ping = true;
  p.net = true;
  CHECK(corner_lines(p, in) == std::vector<std::string>{"60 fps", "ping 45 ms", "red 12/30 kbps, 0% perdido"});
  Prefs all;
  all.all = true;
  const auto lines = corner_lines(all, in);
  REQUIRE(lines.size() == 4);
  CHECK(lines[3] == "5 marionetas, gancho 1.2 ms");
  Input none;
  CHECK(corner_lines(all, none) ==
        std::vector<std::string>{"- fps", "ping - ms", "red -/- kbps, -% perdido", "0 marionetas, gancho - ms"});
}

TEST_CASE("stats: the reports name every mate") {
  Input in;
  in.ping_ms = 40;
  in.mates.push_back({"Saria", 90});
  in.mates.push_back({"Mido", std::nullopt});
  const auto ping = ping_report(in);
  REQUIRE(ping.size() == 3);
  CHECK(ping[0] == "Ping al servidor: 40 ms");
  CHECK(ping[1] == "Saria: ~90 ms (su última muestra)");
  CHECK(ping[2] == "Mido: ~- ms (su última muestra)");
  CHECK(report(in).size() == 6);
}

TEST_CASE("stats: fps over the last second, 0 when frames stop") {
  FpsMeter m;
  CHECK_FALSE(m.fps(0));
  for (std::uint64_t t = 0; t <= 2'000'000; t += 16'667) m.frame(t);
  REQUIRE(m.fps(2'000'000));
  CHECK(*m.fps(2'000'000) == doctest::Approx(60.0f).epsilon(0.02));
  CHECK(*m.fps(4'000'000) == 0.0f);
}

TEST_CASE("stats: kbps from a growing counter, and a new connection starts over") {
  Rate r;
  r.add(0, 0);
  CHECK_FALSE(r.kbps());
  r.add(1000, 1'000'000);  // inside the window: not yet
  CHECK_FALSE(r.kbps());
  r.add(5000, 2'000'000);
  REQUIRE(r.kbps());
  CHECK(*r.kbps() == doctest::Approx(20.0));  // 5000 bytes in 2 s = 20 kbps
  r.add(100, 3'000'000);  // the counter went down: a new connection
  CHECK_FALSE(r.kbps());
}
