// ORM cases moved out of upstream's tests/net/client_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

// ENet pulls in windows.h on Windows; keep its min and max macros out.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>  // doctest prints string_view through it
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "coop/net/client.hpp"
#include "coop/net/enet_waiting.hpp"
#include "coop/protocol/codec.hpp"
#include "coop/protocol/traffic_crypto.hpp"

using namespace coop::net;
namespace proto = coop::protocol;

namespace {

std::uint64_t steady_us() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

struct Received {
  ENetPeer* peer = nullptr;
  proto::Message message;
  std::uint8_t channel = 0;
  std::vector<std::uint8_t> raw;  // the packet as it came (opened, if sealed)
};

// As upstream's tests/net/client_test.cpp FakeServer (test10): every session
// has a join password and the traffic is sealed from the JOIN_PROOF on.
class FakeServer {
 public:
  // Set by send_challenge: seals what it sends, opens what it gets.
  std::unique_ptr<proto::TrafficCipher> cipher;
  std::optional<proto::TrafficKeys> pending_keys;
  FakeServer() {
    REQUIRE(enet_initialize() == 0);
    ENetAddress address{};
    REQUIRE(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
    address.port = 0;  // any free port; ENet reads back the one it got
    host_ = enet_host_create(&address, 4, proto::kChannelCount, 0, 0);
    REQUIRE(host_ != nullptr);
    port_ = host_->address.port;
    REQUIRE(port_ != 0);
  }
  ~FakeServer() {
    enet_host_destroy(host_);
    enet_deinitialize();
  }
  FakeServer(const FakeServer&) = delete;
  FakeServer& operator=(const FakeServer&) = delete;

  std::uint16_t port() const { return port_; }

  void service() {
    if (also_service) also_service();
    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
          ++connects;
          cipher.reset();  // a new connection starts plain
          pending_keys.reset();
          last_peer = event.peer;
          break;
        case ENET_EVENT_TYPE_RECEIVE: {
          Received r;
          r.peer = event.peer;
          r.channel = event.channelID;
          r.raw.assign(event.packet->data, event.packet->data + event.packet->dataLength);
          if (cipher) {
            // Once keyed, everything from the client must open.
            std::vector<std::uint8_t> plain;
            REQUIRE(cipher->open(r.channel, r.raw, plain) == proto::OpenError::none);
            r.raw = std::move(plain);
          }
          const auto decoded = proto::decode(r.raw, r.message);
          REQUIRE(decoded == proto::Error::none);
          // Everything the client sends must pass the server's own check.
          CHECK(proto::check_received(r.message, proto::Sender::client,
                                      r.channel) == proto::Error::none);
          if (pending_keys && std::holds_alternative<proto::JoinProof>(r.message)) {
            cipher = std::make_unique<proto::TrafficCipher>(*pending_keys);
            pending_keys.reset();
          }
          received.push_back(std::move(r));
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
          ++disconnects;
          if (event.peer == last_peer) last_peer = nullptr;
          break;
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
    enet_host_flush(host_);
  }

  void send(const proto::Message& message, std::uint8_t channel) {
    REQUIRE(last_peer != nullptr);
    const auto bytes = proto::encode(message);
    REQUIRE(bytes.has_value());  // a test sends only messages that encode
    send_bytes(*bytes, channel);
  }

  void send_bytes(const std::vector<std::uint8_t>& plain, std::uint8_t channel) {
    REQUIRE(last_peer != nullptr);
    std::vector<std::uint8_t> bytes = plain;
    if (cipher) bytes = *cipher->seal(channel, plain);
    ENetPacket* packet = enet_packet_create(
        bytes.data(), bytes.size(),
        channel < proto::kChannelPlayerSamples ? static_cast<enet_uint32>(ENET_PACKET_FLAG_RELIABLE) : 0u);
    REQUIRE(enet_peer_send(last_peer, channel, packet) == 0);
    enet_host_flush(host_);
  }

  // What the real server does with a join password: JOIN_CHALLENGE with its
  // key share (plain), then the keys from the client's HELLO, the challenge
  // and `password`; everything after is sealed both ways.
  void send_challenge(const proto::JoinNonce& nonce, const std::string& password) {
    std::optional<std::size_t> hello;
    for (std::size_t i = 0; i < received.size(); ++i) {
      if (std::holds_alternative<proto::Hello>(received[i].message)) hello = i;
    }
    REQUIRE(hello.has_value());
    const auto& share = std::get<proto::Hello>(received[*hello].message).key_share;
    REQUIRE(share.has_value());
    const proto::KeyPair keys = proto::KeyPair::generate();
    const auto bytes = proto::encode(proto::JoinChallenge{nonce, keys.share()});
    REQUIRE(bytes.has_value());
    send_bytes(*bytes, proto::kChannelSession);
    const auto derived = proto::derive_traffic_keys(keys, *share, password, received[*hello].raw, *bytes, true);
    REQUIRE(derived.has_value());
    pending_keys = *derived;  // in use from the (plain) JOIN_PROOF on
  }

  // As the real server sends poses and samples: unreliable, in fragments
  // when longer than a datagram (server/enet_server.cpp packet_flags).
  void send_fast(const proto::Message& message) {
    REQUIRE(last_peer != nullptr);
    const auto bytes = proto::encode(message);
    REQUIRE(bytes.has_value());
    ENetPacket* packet = enet_packet_create(bytes->data(), bytes->size(),
                                            static_cast<enet_uint32>(ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
    REQUIRE(enet_peer_send(last_peer, proto::channel_of(message), packet) == 0);
    enet_host_flush(host_);
  }

  // Disconnect the client with notice.
  void drop() {
    REQUIRE(last_peer != nullptr);
    enet_peer_disconnect_now(last_peer, 0);
    last_peer = nullptr;
  }

  // Forget the client without a word, as a crashed server would.
  void vanish() {
    REQUIRE(last_peer != nullptr);
    enet_peer_reset(last_peer);
    last_peer = nullptr;
  }

  // The first received message of type T from index `from` on, if any.
  template <typename T>
  std::optional<std::size_t> find(std::size_t from = 0) const {
    for (std::size_t i = from; i < received.size(); ++i) {
      if (std::holds_alternative<T>(received[i].message)) return i;
    }
    return std::nullopt;
  }

  int connects = 0;
  int disconnects = 0;
  ENetPeer* last_peer = nullptr;
  std::function<void()> also_service;  // run first on every service()
  std::vector<Received> received;

 private:
  ENetHost* host_ = nullptr;
  std::uint16_t port_ = 0;
};

// The join password config_for gives the client (every session has one).
constexpr const char* kTestPassword = "test-pass";

ClientConfig config_for(const FakeServer& server) {
  ClientConfig c;
  c.host = "127.0.0.1";
  c.port = server.port();
  c.hello.build_id = "coop-tests 0";
  c.hello.game_build_id = "e0f3ccfaf50065ae";
  c.hello.game_version = "1.1.1";
  c.hello.nickname = "Link";
  c.join_password = kTestPassword;  // every session has one
  c.retry_first_ms = 50;
  c.retry_max_ms = 400;
  return c;
}

// Services both ends until `done` is true or the time runs out.
bool pump(Client& client, FakeServer& server, const std::function<bool()>& done,
          int timeout_ms = 3000) {
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    client.service(1);
    server.service();
    if (done()) return true;
  }
  return false;
}

// What the real server does after HELLO: JOIN_CHALLENGE, then keys from the
// client's JOIN_PROOF on. Waits until the FakeServer has its keys.
void key_client(Client& client, FakeServer& server, const std::string& password = kTestPassword) {
  proto::JoinNonce nonce{};
  nonce[1] = 3;
  server.send_challenge(nonce, password);
  REQUIRE(pump(client, server, [&] { return server.cipher != nullptr; }));
}

// Collects events as they come, so a test can wait for a kind.
struct Events {
  std::vector<Event> all;
  void drain(Client& client) {
    while (auto e = client.next_event()) all.push_back(std::move(*e));
  }
  std::optional<std::size_t> find(EventKind kind, std::size_t from = 0) const {
    for (std::size_t i = from; i < all.size(); ++i) {
      if (all[i].kind == kind) return i;
    }
    return std::nullopt;
  }
  int count(EventKind kind) const {
    int n = 0;
    for (const auto& e : all) n += e.kind == kind ? 1 : 0;
    return n;
  }
};

bool wait_event(Client& client, FakeServer& server, Events& events,
                EventKind kind, int timeout_ms = 3000) {
  const std::size_t from = events.all.size();
  return pump(client, server, [&] {
    events.drain(client);
    return events.find(kind, from).has_value();
  }, timeout_ms);
}

// Waits for the client's HELLO and answers it with WELCOME.
// Waits from `from_index` on, or from now when it is not given.
proto::Welcome welcome_client(Client& client, FakeServer& server,
                              std::uint64_t server_offset_us = 0,
                              std::optional<std::size_t> from_index = std::nullopt) {
  const std::size_t from = from_index.value_or(server.received.size());
  REQUIRE(pump(client, server, [&] {
    return server.find<proto::Hello>(from).has_value();
  }));
  key_client(client, server);
  proto::Welcome w;
  w.player_number = 2;
  w.server_time_us = steady_us() + server_offset_us;
  w.tick_rate = 30;
  w.players = {{1, "Saria"}};
  server.send(w, proto::kChannelSession);
  return w;
}

}  // namespace

TEST_CASE("client: a server that stops talking is noticed after timeout_ms, whatever the wire says") {
  // A half-open WebSocket (the room proxy or a NAT forgot the connection, the TCP socket did
  // not) never reports a disconnect; ENet's own timeout only covers UDP. The server PINGs every
  // 250 ms, so timeout_ms of nothing from it ends the connection (docs/REVISION_RED.md).
  FakeServer server;  // acknowledges at the ENet level but never sends a message
  ClientConfig config = config_for(server);
  std::uint64_t fake_now = 1'000'000'000'000;
  config.now_us = [&fake_now] { return fake_now; };
  config.timeout_ms = 2000;
  Client client(config);
  Events events;
  REQUIRE(client.start());
  welcome_client(client, server);
  REQUIRE(wait_event(client, server, events, EventKind::welcomed));

  fake_now += 1'500'000;  // quiet, but not for long enough
  pump(client, server, [] { return false; }, 100);
  events.drain(client);
  CHECK(events.count(EventKind::disconnected) == 0);
  CHECK(client.state() == State::connected);

  fake_now += 1'000'000;  // 2.5 s with nothing from the server
  REQUIRE(wait_event(client, server, events, EventKind::disconnected, 1000));
  const Event& lost = events.all[*events.find(EventKind::disconnected)];
  CHECK(lost.drop_reason == DropReason::connection_lost);
  CHECK(lost.link.quiet_ms >= 2000);
}

TEST_CASE("client: our colour goes out after every WELCOME, theirs is kept per player (ORM)") {
  FakeServer server;
  ClientConfig config = config_for(server);
  config.color = std::array<std::uint8_t, 3>{0x2e, 0x9e, 0x3a};
  Client client(config);
  Events events;
  REQUIRE(client.start());
  welcome_client(client, server);  // we are 2, Saria is 1
  REQUIRE(wait_event(client, server, events, EventKind::welcomed));

  auto looks_from = [&](std::size_t from) {
    std::vector<proto::PlayerLook> out;
    for (std::size_t i = from; i < server.received.size(); ++i) {
      if (const auto* l = std::get_if<proto::PlayerLook>(&server.received[i].message)) {
        CHECK(server.received[i].channel == proto::kChannelSession);
        out.push_back(*l);
      }
    }
    return out;
  };
  REQUIRE(pump(client, server, [&] { return looks_from(0).size() == 1; }));
  CHECK(looks_from(0)[0] == proto::PlayerLook{0, {0x2e, 0x9e, 0x3a}, 0});
  REQUIRE(client.find_player(2)->look);
  CHECK(client.find_player(2)->look->player_number == 2);
  CHECK(client.find_player(2)->look->rgb == std::array<std::uint8_t, 3>{0x2e, 0x9e, 0x3a});

  // Theirs: an event and the player list; none for nobody or for us.
  CHECK_FALSE(client.find_player(1)->look);
  server.send(proto::PlayerLook{1, {0x8e, 0x44, 0xd0}, 0}, proto::kChannelSession);
  REQUIRE(wait_event(client, server, events, EventKind::player_look));
  const Event e = events.all[*events.find(EventKind::player_look)];
  CHECK(e.player == 1);
  CHECK(e.look.rgb == std::array<std::uint8_t, 3>{0x8e, 0x44, 0xd0});
  REQUIRE(client.find_player(1)->look);
  CHECK(client.find_player(1)->look->rgb == std::array<std::uint8_t, 3>{0x8e, 0x44, 0xd0});
  const std::size_t rejected = client.rejected_count();
  server.send(proto::PlayerLook{4, {1, 1, 1}, 0}, proto::kChannelSession);
  server.send(proto::PlayerLook{2, {1, 1, 1}, 0}, proto::kChannelSession);
  REQUIRE(pump(client, server, [&] { return client.rejected_count() == rejected + 2; }));

  // A change is sent at once.
  const std::size_t from = server.received.size();
  REQUIRE(client.set_color({255, 0, 0}));
  REQUIRE(pump(client, server, [&] { return looks_from(from).size() == 1; }));
  CHECK(looks_from(from)[0].rgb == std::array<std::uint8_t, 3>{255, 0, 0});
  CHECK(client.find_player(2)->look->rgb == std::array<std::uint8_t, 3>{255, 0, 0});

  // After a drop: no PLAYER_LOOK while down, the new colour again after WELCOME.
  server.drop();
  REQUIRE(wait_event(client, server, events, EventKind::disconnected));
  CHECK_FALSE(client.set_color({0, 0, 255}));
  const std::size_t again = server.received.size();
  welcome_client(client, server);
  REQUIRE(wait_event(client, server, events, EventKind::reconnected));
  REQUIRE(pump(client, server, [&] { return looks_from(again).size() == 1; }));
  CHECK(looks_from(again)[0].rgb == std::array<std::uint8_t, 3>{0, 0, 255});
  CHECK_FALSE(client.find_player(1)->look);  // the room's looks come again from the server
  CHECK(to_string(EventKind::player_look) == "player look");
  client.stop();
}

TEST_CASE("client: with no colour set no PLAYER_LOOK is sent (ORM)") {
  FakeServer server;
  Client client(config_for(server));
  Events events;
  REQUIRE(client.start());
  welcome_client(client, server);
  REQUIRE(wait_event(client, server, events, EventKind::welcomed));
  CHECK(client.send(proto::Chat{0, "marker"}));
  REQUIRE(pump(client, server, [&] { return server.find<proto::Chat>(0).has_value(); }));
  CHECK_FALSE(server.find<proto::PlayerLook>(0).has_value());
  CHECK_FALSE(client.find_player(2)->look);
  client.stop();
}

// ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento").
TEST_CASE("client: ORM a LINK_NOTICE server_protocol before REFUSED gives the server's protocol") {
  FakeServer server;
  Client client(config_for(server));
  Events events;
  REQUIRE(client.start());
  REQUIRE(pump(client, server, [&] { return server.find<proto::Hello>().has_value(); }));
  CHECK_FALSE(client.server_protocol().has_value());
  server.send(proto::LinkNotice{proto::LinkNoticeKind::server_protocol, 108, 0}, proto::kChannelSession);
  server.send(proto::Refused{proto::RefuseReason::wrong_protocol_version}, proto::kChannelSession);
  REQUIRE(wait_event(client, server, events, EventKind::refused));
  REQUIRE(client.server_protocol().has_value());
  CHECK(*client.server_protocol() == 108);
  // Nothing of it is a connected notice.
  CHECK(client.take_link_notices().empty());
}

TEST_CASE("client: ORM LINK_NOTICEs while connected are kept for the game, at most 16") {
  FakeServer server;
  Client client(config_for(server));
  Events events;
  REQUIRE(client.start());
  welcome_client(client, server);
  REQUIRE(wait_event(client, server, events, EventKind::welcomed));
  for (std::uint32_t i = 0; i < 20; ++i) {
    server.send(proto::LinkNotice{proto::LinkNoticeKind::behind, i, 100 + i}, proto::kChannelSession);
  }
  // A chat line after them on the same reliable channel: once it is in, all
  // twenty are.
  proto::Chat marker;
  marker.player_number = 1;
  marker.text = "after the notices";
  server.send(marker, proto::kChannelSession);
  REQUIRE(wait_event(client, server, events, EventKind::chat));
  std::vector<proto::LinkNotice> got;
  got = client.take_link_notices();
  REQUIRE(got.size() == Client::kMaxLinkNotices);
  CHECK(got.front().a == 4);  // the four oldest went
  CHECK(got.back().a == 19);
  CHECK(got.back().b == 119);
  CHECK(client.take_link_notices().empty());
  CHECK(client.state() == State::connected);
}

TEST_CASE("client: ORM a LINK_NOTICE from a client's side is never one we send") {
  // The server alone sends it: Client::send refuses it like the server would.
  FakeServer server;
  Client client(config_for(server));
  Events events;
  REQUIRE(client.start());
  welcome_client(client, server);
  REQUIRE(wait_event(client, server, events, EventKind::welcomed));
  CHECK_FALSE(client.send(proto::LinkNotice{proto::LinkNoticeKind::behind, 1, 1}));
}
