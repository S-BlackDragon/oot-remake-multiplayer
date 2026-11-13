// The client's round trip statistics: nothing until the first PONG, and the
// last samples' median once a server answers the PINGs.
//
// Everything runs on this one thread: each loop step services the server and
// then the client.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <chrono>
#include <cstdint>
#include <functional>

#include "coop/net/client.hpp"
#include "enet_server.hpp"

using namespace coop::net;

namespace {

// ENet has to be started before the server and the client use it.
struct EnetLife {
  EnetLife() { REQUIRE(enet_initialize() == 0); }
  ~EnetLife() { enet_deinitialize(); }
};

// Every session has a join password since test10 (no plain way in).
constexpr const char* kTestPassword = "test-pass";

ClientConfig config_for(std::uint16_t port) {
  ClientConfig c;
  c.host = "127.0.0.1";
  c.port = port;
  c.hello.build_id = "coop-tests 0";
  c.hello.game_build_id = "e0f3ccfaf50065ae";
  c.hello.game_version = "1.1.1";
  c.hello.nickname = "Link";
  c.join_password = kTestPassword;
  // A PING every 20 ms: a few samples in a fraction of a second.
  c.ping_interval_ms = 20;
  c.retry_first_ms = 50;
  c.retry_max_ms = 400;
  return c;
}

// Services the server and the client until `done` is true or the time runs
// out.
bool pump(coop::server::EnetServer& server, Client& client,
          const std::function<bool()>& done, int timeout_ms = 3000) {
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    server.service(0);
    client.service(1);
    if (done()) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("client rtt: zeros before the first PONG") {
  Client client(config_for(1));  // nothing answers there
  const RttStats fresh = client.rtt();
  CHECK(fresh.samples == 0);
  CHECK(fresh.median_us == 0);
  CHECK(fresh.min_us == 0);
  CHECK(fresh.max_us == 0);

  // Started, but with no server to answer: still no sample.
  REQUIRE(client.start());
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < end) {
    client.service(1);
  }
  const RttStats stats = client.rtt();
  CHECK(stats.samples == 0);
  CHECK(stats.median_us == 0);
  CHECK(stats.min_us == 0);
  CHECK(stats.max_us == 0);
}

TEST_CASE("client rtt: pings answered give a median under a second") {
  EnetLife enet;
  coop::server::ServerOptions options;
  options.bind = 0x7F000001u;  // 127.0.0.1 only: no firewall prompt
  options.port = 0;  // any free port
  options.session.join_password = kTestPassword;
  coop::server::EnetServer server(options);
  REQUIRE(server.start());

  Client client(config_for(server.port()));
  REQUIRE(client.start());

  // The client joins: the WELCOME makes it connected.
  REQUIRE(pump(server, client, [&] { return client.state() == State::connected; }));

  // About 300 ms with a PING every 20 ms: a few samples, none of them slow.
  pump(server, client, [] { return false; }, 300);
  const RttStats stats = client.rtt();
  CHECK(stats.samples >= 3);
  CHECK(stats.min_us <= stats.median_us);
  CHECK(stats.median_us <= stats.max_us);
  CHECK(stats.median_us < 1'000'000u);
}
