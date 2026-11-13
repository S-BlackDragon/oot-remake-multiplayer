// ORM cases moved out of upstream's tests/host/session_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <enet/enet.h>

#include "coop/host/session.hpp"
#include "coop/protocol/orm_status.hpp"
#include "enet_server.hpp"
#include "../host/fake_memory.hpp"

using namespace coop::host;
namespace proto = coop::protocol;
namespace net = coop::net;

namespace {

constexpr FlagKey kChest{0x1000u, 0};
constexpr FlagKey kDoor{0x2000u, 0};
constexpr FlagKey kKey{0x3000u, 2};

proto::FlagEntry entry(FlagKey key, std::uint32_t value) {
  return {key, proto::FlagKind::boolean, value};
}

// The game's flags. Only the emulator thread touches it.
class FakeStore : public FlagStore {
 public:
  FakeStore() {
    for (FlagKey k : {kChest, kDoor, kKey}) set(k, 0);
  }
  std::map<FlagKey, proto::FlagEntry> values;
  void set(FlagKey key, std::uint32_t value) { values[key] = entry(key, value); }
  std::uint32_t get(FlagKey key) const { return values.at(key).value; }
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    if (!values.contains(key_of(e))) return false;
    values[key_of(e)] = e;
    return true;
  }
};

struct EnetLife {
  EnetLife() { REQUIRE(enet_initialize() == 0); }
  ~EnetLife() { enet_deinitialize(); }
};

// Every session has a join password since test10 (no plain way in).
constexpr const char* kTestPassword = "test-pass";

net::ClientConfig config_for(std::uint16_t port, const std::string& name) {
  net::ClientConfig c;
  c.host = "127.0.0.1";
  c.port = port;
  c.hello.build_id = "coop-tests 0";
  c.hello.game_build_id = "e0f3ccfaf50065ae";
  c.hello.game_version = "1.1.1";
  c.hello.nickname = name;
  c.join_password = kTestPassword;
  c.retry_first_ms = 50;
  c.retry_max_ms = 400;
  return c;
}

SessionSettings with_flags() {
  SessionSettings s;
  s.shared = SharedFlags({kChest, kDoor, kKey});
  return s;
}

// One player: a client, its session, and its game.
struct Peer {
  Peer(std::uint16_t port, const std::string& name, SessionSettings settings = with_flags())
      : client(config_for(port, name)), session(client, std::move(settings)) {
    test::make_game(memory);
    REQUIRE(client.start());
  }
  // One emulated frame, then one network round, on one thread.
  void step() {
    last = session.frame(memory, &store);
    session.service();
  }
  bool synced() {
    return session.with_client([](net::Client& c) { return c.synced(); });
  }
  std::uint8_t number() {
    return session.with_client([](net::Client& c) { return c.player_number(); });
  }

  net::Client client;
  Session session;
  test::FakeMemory memory;
  FakeStore store;
  FrameReport last;
};

coop::server::ServerOptions any_port() {
  coop::server::ServerOptions o;
  o.bind = 0x7F000001u;  // 127.0.0.1 only: no Windows firewall prompt for the test binary
  o.port = 0;
  o.session.join_password = kTestPassword;
  return o;
}

bool pump(coop::server::EnetServer& server, const std::vector<Peer*>& peers,
          const std::function<bool()>& done, int timeout_ms = 3000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    server.service(1);
    for (Peer* p : peers) p->step();
    if (done()) return true;
  }
  return false;
}

// Joins `p` and waits until it is welcomed and has the host's table.
void join(coop::server::EnetServer& server, std::vector<Peer*>& peers, Peer& p) {
  peers.push_back(&p);
  REQUIRE(pump(server, peers, [&] { return p.last.connected && p.synced(); }));
}

}  // namespace

// ORM (coop/host/flag_burst.hpp, oot-remake docs/ANCHO_DE_BANDA.md): a burst
// of changes over a few frames (a scene load) goes as two FLAG_SETs, the
// first at once, and every change still reaches the host.
TEST_CASE("session: ORM a burst of flag changes goes together, the first at once") {
  EnetLife enet;
  coop::server::EnetServer server(any_port());
  REQUIRE(server.start());
  std::vector<Peer*> peers;

  Peer a(server.port(), "Link");
  join(server, peers, a);
  REQUIRE(pump(server, peers, [&] { return a.last.hosting; }));
  REQUIRE(pump(server, peers, [&] { return server.session().flag_count() == 3; }));
  SessionSettings settings = with_flags();
  settings.flag_burst_us = 30 * GameTicks::kTickUs;  // no clock: one frame() is one tick
  Peer b(server.port(), "Zelda", std::move(settings));
  join(server, peers, b);
  REQUIRE(b.session.flag_burst().sent() == 0);

  // One change a frame for three frames.
  for (const FlagKey key : {kChest, kDoor, kKey}) {
    b.store.set(key, 1);
    server.service(1);
    a.step();
    b.step();
  }
  CHECK(b.session.flag_burst().sent() == 1);  // the chest went at once
  CHECK(b.session.flag_burst().held_frames() == 2);
  REQUIRE(pump(server, peers, [&] {
    return a.store.get(kChest) == 1 && a.store.get(kDoor) == 1 && a.store.get(kKey) == 1 &&
           b.session.flags()->pending() == 0;
  }));
  CHECK(b.session.flag_burst().sent() == 2);  // the door and the key together
  CHECK(b.session.flags()->stats().changes_sent == 3);
  CHECK(server.session().stats().dropped() == 0);
}

// ORM menus (coop/host/menus.hpp): the pause rides on frames with a Link, the
// title screen on frames with none; both reach the other player's presence.
TEST_CASE("session: our pause and title screen reach the other player's presence (ORM menus)") {
  EnetLife enet;
  coop::server::EnetServer server(any_port());
  REQUIRE(server.start());
  std::vector<Peer*> peers;

  Peer a(server.port(), "Link", SessionSettings{});
  join(server, peers, a);
  Peer b(server.port(), "Zelda", SessionSettings{});
  join(server, peers, b);
  const std::uint8_t na = a.number();
  auto flags_in_b = [&] {
    return b.session.with_presence([&](coop::presence::Presence& p) {
      const auto* player = p.player(na);
      return player ? player->flags() : std::uint8_t{0xff};
    });
  };

  a.session.set_menu_status(proto::status::paused);
  REQUIRE(pump(server, peers, [&] { return flags_in_b() == proto::status::paused; }));

  // The title screen: no Link to read in A's game.
  a.session.set_menu_status(proto::status::loading | proto::orm_status::at_title);
  const std::uint8_t title = proto::status::loading | proto::orm_status::at_title;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool seen = false;
  while (!seen && std::chrono::steady_clock::now() < end) {
    server.service(1);
    a.session.frame(std::nullopt, nullptr);
    a.session.service();
    b.step();
    seen = flags_in_b() == title;
  }
  CHECK(seen);

  // Back: the first reading sends 0.
  a.session.set_menu_status(0);
  REQUIRE(pump(server, peers, [&] { return flags_in_b() == 0; }));
}
