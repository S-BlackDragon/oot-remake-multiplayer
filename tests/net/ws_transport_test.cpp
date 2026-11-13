// The WebSocket transport against a tiny Beast loopback server on 127.0.0.1.
// Everything runs on this one thread: each loop step drives the client
// transport and the server's io_context.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "coop/net/transport.hpp"
#include "coop/protocol/messages.hpp"
#include "ws_transport.hpp"

using namespace coop::net;

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

// Accepts one client, echoes every binary frame back unchanged, and keeps a
// copy of every frame it read.
class LoopbackServer {
 public:
  LoopbackServer() : acceptor_(ioc_), ws_(ioc_.get_executor()) {
    tcp::endpoint endpoint(asio::ip::make_address_v4("127.0.0.1"), 0);  // loopback only: no firewall prompt
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(1);
    port_ = acceptor_.local_endpoint().port();
    ws_.binary(true);
    accept();
  }

  std::uint16_t port() const { return port_; }

  void run_for(std::chrono::milliseconds wait) { ioc_.run_for(wait); }

  void close() {
    beast::error_code ignored;
    acceptor_.close(ignored);
    beast::get_lowest_layer(ws_).socket().close(ignored);
  }

  std::deque<std::vector<std::uint8_t>> received;

 private:
  void accept() {
    acceptor_.async_accept(beast::get_lowest_layer(ws_).socket(),
                           [this](beast::error_code ec) {
                             if (ec) return;
                             handshake();
                           });
  }

  void handshake() {
    ws_.async_accept([this](beast::error_code ec) {
      if (ec) return;
      read();
    });
  }

  void read() {
    ws_.async_read(buffer_, [this](beast::error_code ec, std::size_t) {
      if (ec) return;
      const auto data = buffer_.data();
      const auto* bytes = static_cast<const std::uint8_t*>(data.data());
      received.emplace_back(bytes, bytes + data.size());
      buffer_.consume(buffer_.size());
      echo_ = received.back();
      ws_.async_write(asio::buffer(echo_),
                      [this](beast::error_code ec, std::size_t) {
                        if (ec) return;
                        read();
                      });
    });
  }

  asio::io_context ioc_;
  tcp::acceptor acceptor_;
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  std::vector<std::uint8_t> echo_;
  std::uint16_t port_ = 0;
};

// A self-signed certificate for 127.0.0.1, made in memory: the key, the
// certificate the server presents, and its PEM (the client's trusted root).
class TestCertificate {
 public:
  TestCertificate() {
    key = EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<std::size_t>(2048));
    REQUIRE(key != nullptr);

    cert = X509_new();
    REQUIRE(cert != nullptr);
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 24 * 60 * 60);
    X509_set_pubkey(cert, key);

    X509_NAME* name = const_cast<X509_NAME*>(X509_get_subject_name(cert));
    const unsigned char* common_name =
        reinterpret_cast<const unsigned char*>("127.0.0.1");
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, common_name, -1, -1, 0);
    X509_set_issuer_name(cert, name);

    add_extension(NID_subject_alt_name, "IP:127.0.0.1");
    add_extension(NID_basic_constraints, "critical,CA:TRUE");
    REQUIRE(X509_sign(cert, key, EVP_sha256()) > 0);

    BIO* bio = BIO_new(BIO_s_mem());
    REQUIRE(bio != nullptr);
    REQUIRE(PEM_write_bio_X509(bio, cert) > 0);
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(bio, &memory);
    pem.assign(memory->data, memory->length);
    BIO_free(bio);
  }

  ~TestCertificate() {
    if (cert != nullptr) X509_free(cert);
    if (key != nullptr) EVP_PKEY_free(key);
  }

  TestCertificate(const TestCertificate&) = delete;
  TestCertificate& operator=(const TestCertificate&) = delete;

  EVP_PKEY* key = nullptr;
  X509* cert = nullptr;
  std::string pem;

 private:
  void add_extension(int nid, const char* value) {
    X509_EXTENSION* extension =
        X509V3_EXT_conf_nid(nullptr, nullptr, nid, const_cast<char*>(value));
    if (extension == nullptr) return;
    X509_add_ext(cert, extension, -1);
    X509_EXTENSION_free(extension);
  }
};

// The context must carry the certificate before the stream's SSL object is
// created: SSL_new copies the context's certificate slots.
asio::ssl::context make_server_context(const TestCertificate& identity) {
  asio::ssl::context ctx(asio::ssl::context::tls_server);
  REQUIRE(SSL_CTX_use_certificate(ctx.native_handle(), identity.cert) == 1);
  REQUIRE(SSL_CTX_use_PrivateKey(ctx.native_handle(), identity.key) == 1);
  return ctx;
}

// The same echo server as LoopbackServer, over TLS with TestCertificate.
class TlsLoopbackServer {
 public:
  explicit TlsLoopbackServer(const TestCertificate& identity)
      : ctx_(make_server_context(identity)),
        acceptor_(ioc_),
        ws_(ioc_, ctx_) {
    tcp::endpoint endpoint(asio::ip::make_address_v4("127.0.0.1"), 0);  // loopback only: no firewall prompt
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(1);
    port_ = acceptor_.local_endpoint().port();
    ws_.binary(true);
    accept();
  }

  std::uint16_t port() const { return port_; }

  void run_for(std::chrono::milliseconds wait) { ioc_.run_for(wait); }

  std::deque<std::vector<std::uint8_t>> received;

 private:
  void accept() {
    acceptor_.async_accept(beast::get_lowest_layer(ws_).socket(),
                           [this](beast::error_code ec) {
                             if (ec) return;
                             tls_handshake();
                           });
  }

  void tls_handshake() {
    ws_.next_layer().async_handshake(
        ssl::stream_base::server, [this](beast::error_code ec) {
          if (ec) return;
          handshake();
        });
  }

  void handshake() {
    ws_.async_accept([this](beast::error_code ec) {
      if (ec) return;
      read();
    });
  }

  void read() {
    ws_.async_read(buffer_, [this](beast::error_code ec, std::size_t) {
      if (ec) return;
      const auto data = buffer_.data();
      const auto* bytes = static_cast<const std::uint8_t*>(data.data());
      received.emplace_back(bytes, bytes + data.size());
      buffer_.consume(buffer_.size());
      echo_ = received.back();
      ws_.async_write(asio::buffer(echo_),
                      [this](beast::error_code ec, std::size_t) {
                        if (ec) return;
                        read();
                      });
    });
  }

  asio::io_context ioc_;
  asio::ssl::context ctx_;
  tcp::acceptor acceptor_;
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
  beast::flat_buffer buffer_;
  std::vector<std::uint8_t> echo_;
  std::uint16_t port_ = 0;
};

// Drives both ends until `done` is true or the deadline passes. Every client
// event is kept in `events`, oldest first.
template <typename Server>
bool pump(WsTransport& transport, Server& server,
          std::deque<TransportEvent>& events,
          const std::function<bool()>& done, int timeout_ms = 5000) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    server.run_for(std::chrono::milliseconds(1));
    TransportEvent event = transport.service(1);
    if (event.kind != TransportEventKind::none) {
      events.push_back(std::move(event));
    }
    if (done()) return true;
    if (Clock::now() >= deadline) return false;
  }
}

bool has_kind(const std::deque<TransportEvent>& events, TransportEventKind kind) {
  for (const TransportEvent& event : events) {
    if (event.kind == kind) return true;
  }
  return false;
}

const TransportEvent* find_kind(const std::deque<TransportEvent>& events,
                                TransportEventKind kind) {
  for (const TransportEvent& event : events) {
    if (event.kind == kind) return &event;
  }
  return nullptr;
}

// Connects and returns once the transport reports connected.
template <typename Server>
void connect(WsTransport& transport, Server& server,
             std::deque<TransportEvent>& events,
             std::uint32_t timeout_ms = 2000) {
  REQUIRE(transport.start());
  REQUIRE(transport.resolve("127.0.0.1"));
  transport.connect("127.0.0.1", server.port(), "/", timeout_ms);
  REQUIRE(pump(transport, server, events,
               [&] { return has_kind(events, TransportEventKind::connected); }));
}

}  // namespace

TEST_CASE("ws transport connect and handshake reports connected") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);
  CHECK(events.front().kind == TransportEventKind::connected);
}

TEST_CASE("ws transport a channel 0 frame comes back with its channel and bytes") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  const std::vector<std::uint8_t> payload{0x10, 0x00, 0x02, 0x00, 0x41, 0x42};
  REQUIRE(transport.send(0, payload.data(), payload.size()));

  REQUIRE(pump(transport, server, events, [&] {
    return has_kind(events, TransportEventKind::received);
  }));
  const TransportEvent* echoed =
      find_kind(events, TransportEventKind::received);
  REQUIRE(echoed != nullptr);
  CHECK(echoed->channel == 0);
  CHECK(echoed->data == payload);
  REQUIRE(server.received.size() == 1);
  CHECK(server.received.front().size() == payload.size() + 1);
  CHECK(server.received.front().front() == 0);
}

TEST_CASE(
    "ws transport three channel 3 frames queued before a write all go, in "
    "order") {
  // A PLAYER_SAMPLE and a PLAYER_POSE share channel 3; keeping one slot lost
  // one of them every tick (2026-11-10).
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  const std::vector<std::uint8_t> first{0xaa};
  const std::vector<std::uint8_t> second{0xbb};
  const std::vector<std::uint8_t> third{0xcc};
  REQUIRE(transport.send(3, first.data(), first.size()));
  REQUIRE(transport.send(3, second.data(), second.size()));
  REQUIRE(transport.send(3, third.data(), third.size()));

  REQUIRE(pump(transport, server, events,
               [&] { return server.received.size() >= 3; }));
  REQUIRE(server.received.size() == 3);
  const std::uint8_t expected[] = {0xaa, 0xbb, 0xcc};
  for (std::size_t i = 0; i < 3; ++i) {
    const std::vector<std::uint8_t>& frame = server.received[i];
    REQUIRE(frame.size() == 2);
    CHECK(frame[0] == 3);
    CHECK(frame[1] == expected[i]);
  }
}

TEST_CASE(
    "ws transport a full channel 3 queue drops the oldest frames, never the "
    "newest") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  // 70 frames queued at once: the queue holds 64, so the first 6 go.
  for (std::uint8_t i = 0; i < 70; ++i) {
    const std::vector<std::uint8_t> payload{i};
    REQUIRE(transport.send(3, payload.data(), payload.size()));
  }
  REQUIRE(pump(transport, server, events,
               [&] { return server.received.size() >= 64; }));
  // Give a wrongly kept earlier frame time to show up too.
  const auto until = Clock::now() + std::chrono::milliseconds(50);
  while (Clock::now() < until) {
    server.run_for(std::chrono::milliseconds(1));
    transport.service(1);
  }
  REQUIRE(server.received.size() == 64);
  CHECK(server.received.front()[1] == 6);
  CHECK(server.received.back()[1] == 69);
  for (std::size_t i = 1; i < server.received.size(); ++i) {
    CHECK(server.received[i][1] == server.received[i - 1][1] + 1);
  }
}

TEST_CASE("ws transport a frame bigger than any protocol message ends the connection") {
  // The channel byte, the header and a u16 body are 65540 bytes at most; Beast would otherwise
  // put together up to 16 MB from a server before the client saw it (docs/REVISION_RED.md).
  LoopbackServer server;  // echoes what it gets
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  std::vector<std::uint8_t> biggest(coop::protocol::kHeaderSize + 0xFFFF, 0x21);
  REQUIRE(transport.send(0, biggest.data(), biggest.size()));
  REQUIRE(pump(transport, server, events, [&] { return has_kind(events, TransportEventKind::received); }));
  CHECK(find_kind(events, TransportEventKind::received)->data.size() == biggest.size());

  std::vector<std::uint8_t> huge(100'000, 0x33);
  REQUIRE(transport.send(0, huge.data(), huge.size()));
  REQUIRE(pump(transport, server, events,
               [&] { return has_kind(events, TransportEventKind::disconnected); }));
  int received = 0;
  for (const TransportEvent& e : events) received += e.kind == TransportEventKind::received ? 1 : 0;
  CHECK(received == 1);
}

TEST_CASE(
    "ws transport a reconnect does not send the channel 3 frames queued for the "
    "old connection") {
  LoopbackServer first;
  LoopbackServer second;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, first, events);

  // Queued, never serviced: they belong to the first connection only.
  for (std::uint8_t i = 0; i < 3; ++i) {
    const std::vector<std::uint8_t> stale{i};
    REQUIRE(transport.send(3, stale.data(), stale.size()));
  }
  std::deque<TransportEvent> again;
  transport.connect("127.0.0.1", second.port(), "/", 2000);
  REQUIRE(pump(transport, second, again,
               [&] { return has_kind(again, TransportEventKind::connected); }));
  const std::vector<std::uint8_t> fresh{0x7f};
  REQUIRE(transport.send(3, fresh.data(), fresh.size()));
  REQUIRE(pump(transport, second, again,
               [&] { return !second.received.empty(); }));
  // Give a wrongly kept stale frame time to show up too.
  const auto until = Clock::now() + std::chrono::milliseconds(50);
  while (Clock::now() < until) {
    second.run_for(std::chrono::milliseconds(1));
    transport.service(1);
  }
  REQUIRE(second.received.size() == 1);
  CHECK(second.received.front() == std::vector<std::uint8_t>{3, 0x7f});
}

TEST_CASE("ws transport the server closing the socket reports disconnected") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  server.close();
  REQUIRE(pump(transport, server, events,
               [&] { return has_kind(events, TransportEventKind::disconnected); }));
  CHECK(find_kind(events, TransportEventKind::disconnected) != nullptr);
}

TEST_CASE(
    "ws transport a connect to a closed port reports disconnected within the "
    "timeout") {
  std::uint16_t closed_port = 0;
  {
    asio::io_context ioc;
    tcp::acceptor probe(ioc);
    tcp::endpoint endpoint(asio::ip::make_address_v4("127.0.0.1"), 0);  // loopback only: no firewall prompt
    probe.open(endpoint.protocol());
    probe.bind(endpoint);
    probe.listen(1);
    closed_port = probe.local_endpoint().port();
    beast::error_code ignored;
    probe.close(ignored);
  }

  WsTransport transport;
  REQUIRE(transport.start());
  REQUIRE(transport.resolve("127.0.0.1"));
  const auto started = Clock::now();
  transport.connect("127.0.0.1", closed_port, "/", 500);

  bool disconnected = false;
  const auto deadline = started + std::chrono::milliseconds(5000);
  while (Clock::now() < deadline) {
    const TransportEvent event = transport.service(5);
    if (event.kind == TransportEventKind::disconnected) {
      disconnected = true;
      break;
    }
  }
  REQUIRE(disconnected);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           Clock::now() - started)
                           .count();
  CHECK(elapsed <= 1500);
}

TEST_CASE("wss transport connect and handshake reports connected") {
  TestCertificate identity;
  TlsLoopbackServer server(identity);
  WsTransport transport(true);
  transport.set_trusted_roots(identity.pem);
  std::deque<TransportEvent> events;
  connect(transport, server, events);
  CHECK(events.front().kind == TransportEventKind::connected);
}

TEST_CASE(
    "wss transport a channel 0 frame comes back with its channel and bytes") {
  TestCertificate identity;
  TlsLoopbackServer server(identity);
  WsTransport transport(true);
  transport.set_trusted_roots(identity.pem);
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  const std::vector<std::uint8_t> payload{0x10, 0x00, 0x02, 0x00, 0x41, 0x42};
  REQUIRE(transport.send(0, payload.data(), payload.size()));

  REQUIRE(pump(transport, server, events, [&] {
    return has_kind(events, TransportEventKind::received);
  }));
  const TransportEvent* echoed =
      find_kind(events, TransportEventKind::received);
  REQUIRE(echoed != nullptr);
  CHECK(echoed->channel == 0);
  CHECK(echoed->data == payload);
}

TEST_CASE(
    "wss transport system store verification against a self-signed server "
    "reports disconnected") {
  TestCertificate identity;
  TlsLoopbackServer server(identity);
  WsTransport transport(true);  // No trusted roots: the Windows ROOT store.
  std::deque<TransportEvent> events;
  REQUIRE(transport.start());
  REQUIRE(transport.resolve("127.0.0.1"));
  transport.connect("127.0.0.1", server.port(), "/", 2000);

  REQUIRE(pump(transport, server, events, [&] {
    return has_kind(events, TransportEventKind::disconnected);
  }));
  CHECK_FALSE(has_kind(events, TransportEventKind::connected));
  CHECK(find_kind(events, TransportEventKind::disconnected) != nullptr);
}

// Latency (oot-remake/docs/LATENCIA.md): Nagle off, frames out at flush(),
// and a wait that ends when the first frame arrives.

TEST_CASE("wss transport a trusted certificate for another host name reports disconnected") {
  // The certificate is good and its root trusted, but it names 127.0.0.1, not "localhost": the
  // host name check must refuse it, as it would a stolen certificate for another site.
  TestCertificate identity;
  TlsLoopbackServer server(identity);
  WsTransport transport(true);
  transport.set_trusted_roots(identity.pem);
  std::deque<TransportEvent> events;
  REQUIRE(transport.start());
  REQUIRE(transport.resolve("localhost"));
  transport.connect("localhost", server.port(), "/", 2000);
  REQUIRE(pump(transport, server, events,
               [&] { return has_kind(events, TransportEventKind::disconnected); }, 4000));
  CHECK_FALSE(has_kind(events, TransportEventKind::connected));
}

TEST_CASE("ws transport turns Nagle off on the connected socket") {
  LoopbackServer server;
  WsTransport transport;
  CHECK_FALSE(transport.no_delay());  // no socket yet
  std::deque<TransportEvent> events;
  connect(transport, server, events);
  CHECK(transport.no_delay());
}

TEST_CASE("wss transport turns Nagle off on the connected socket") {
  TestCertificate identity;
  TlsLoopbackServer server(identity);
  WsTransport transport(true);
  transport.set_trusted_roots(identity.pem);
  std::deque<TransportEvent> events;
  connect(transport, server, events);
  CHECK(transport.no_delay());
}

TEST_CASE("ws transport flush puts a queued frame on the wire without service") {
  // Client::service() queues its PINGs after the transport's service(); they
  // used to wait for the next round.
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  const std::vector<std::uint8_t> first{0x01};
  const std::vector<std::uint8_t> second{0x02};
  REQUIRE(transport.send(0, first.data(), first.size()));
  REQUIRE(transport.send(3, second.data(), second.size()));
  transport.flush();
  const auto until = Clock::now() + std::chrono::milliseconds(500);
  while (server.received.size() < 2 && Clock::now() < until) {
    server.run_for(std::chrono::milliseconds(1));
    transport.flush();  // never service(): only flush() may send
  }
  REQUIRE(server.received.size() == 2);
  CHECK(server.received[0][1] == 0x01);
  CHECK(server.received[1][1] == 0x02);
}

TEST_CASE("ws transport service returns as soon as a frame arrives, not at the end of its wait") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  const std::vector<std::uint8_t> payload{0x07};
  REQUIRE(transport.send(0, payload.data(), payload.size()));
  // The server echoes on its own thread while the transport waits here.
  std::thread echo([&] { server.run_for(std::chrono::milliseconds(400)); });
  const auto started = Clock::now();
  TransportEvent event;
  const auto until = started + std::chrono::milliseconds(1500);
  while (event.kind != TransportEventKind::received && Clock::now() < until) {
    event = transport.service(250);
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
  echo.join();
  REQUIRE(event.kind == TransportEventKind::received);
  CHECK(event.data == payload);
  // run_for(250) answered at 250 ms whatever the frame's arrival.
  CHECK(elapsed < 100);
}

// ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento"): what
// waits to go out is in stats(), so the game can tell a player whose uplink
// cannot keep up; the reliable queue has a byte limit.
TEST_CASE("ws transport ORM stats count what waits, what was dropped, and empty out once written") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  TransportStats before;
  REQUIRE(transport.stats(before));
  CHECK(before.send_queued_bytes == 0);
  CHECK(before.send_queued_frames == 0);
  CHECK(before.send_dropped_frames == 0);
  CHECK_FALSE(before.has_round_trip);

  // 100 frames on channel 3 with no service(): 64 wait, the 36 oldest go.
  const std::vector<std::uint8_t> pose(1000, 0x5A);
  for (int i = 0; i < 100; ++i) REQUIRE(transport.send(3, pose.data(), pose.size()));
  TransportStats behind;
  REQUIRE(transport.stats(behind));
  CHECK(behind.send_queued_frames == 64);
  CHECK(behind.send_queued_bytes == 64 * (pose.size() + 1));
  CHECK(behind.send_dropped_frames == 36);

  REQUIRE(pump(transport, server, events, [&] { return server.received.size() == 64; }));
  TransportStats after;
  REQUIRE(transport.stats(after));
  CHECK(after.send_queued_frames == 0);
  CHECK(after.send_queued_bytes == 0);
  CHECK(after.send_dropped_frames == 36);  // a total since start()
}

TEST_CASE("ws transport ORM a reliable queue past 16 MiB ends the connection instead of growing for ever") {
  LoopbackServer server;
  WsTransport transport;
  std::deque<TransportEvent> events;
  connect(transport, server, events);

  // No service(): nothing is written. 65000-byte frames: the 259th is over.
  const std::vector<std::uint8_t> big(65000, 0x11);
  int accepted = 0;
  bool refused = false;
  for (int i = 0; i < 400 && !refused; ++i) {
    if (transport.send(0, big.data(), big.size())) {
      ++accepted;
    } else {
      refused = true;
    }
  }
  CHECK(refused);
  CHECK(accepted == static_cast<int>((std::size_t{16} << 20) / (big.size() + 1)));
  // Closed: the event says so, and nothing more is taken.
  TransportEvent event = transport.service(0);
  CHECK(event.kind == TransportEventKind::disconnected);
  CHECK_FALSE(transport.send(0, big.data(), big.size()));
  TransportStats stats;
  CHECK_FALSE(transport.stats(stats));
}
