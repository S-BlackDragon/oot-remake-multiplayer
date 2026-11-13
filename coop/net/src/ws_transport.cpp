#include "ws_transport.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::net {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

// Channels 0 to 2 are reliable; 3 and 4 keep a short queue each and drop the
// oldest frame when it is full. One slot per channel (the first version) lost
// whichever of a PLAYER_SAMPLE and a PLAYER_POSE was queued first in the same
// tick (both go on channel 3), so the puppets got half the poses and fell back
// to the generic walk (a friend saw a mate walking while he ran, 2026-11-10).
constexpr std::uint8_t kFirstUnreliableChannel = protocol::kChannelPlayerSamples;
constexpr std::size_t kMaxUnreliableWaiting = 64;
// ORM (oot-remake docs/RIESGOS_RESUELTOS.md): most bytes the reliable queue
// may hold. Unbounded before: a socket that never drains (the line is gone
// but the TCP connection is not, or the uplink carries a fraction of what we
// hand it) grew it for ever. Past this the connection ends and the client
// reconnects; a whole flag snapshot at the server's max_flags is under 5 MB.
constexpr std::size_t kMaxReliableQueuedBytes = std::size_t{16} << 20;
// Most io_context polls one flush() or service() spends sending what is queued.
constexpr int kMaxPumpRounds = 256;
// The biggest frame there is: the channel byte, a header and a u16 body, and
// what sealing adds since protocol 110 (traffic_crypto.hpp: the frame carries
// the sealed packet). Beast would otherwise put together up to 16 MB from a
// server before we saw it; a bigger frame ends the connection
// (docs/REVISION_RED.md).
constexpr std::size_t kMaxFrameBytes = 1 + protocol::kHeaderSize + 0xFFFF + protocol::kSealOverheadBytes;

using Clock = std::chrono::steady_clock;

using DoneHandler = std::function<void(beast::error_code)>;
using ReadHandler = std::function<void(beast::error_code, std::size_t)>;

void add_trusted_certificate(ssl::context& ctx, X509* cert) {
  X509_STORE* store = SSL_CTX_get_cert_store(ctx.native_handle());
  if (store != nullptr && cert != nullptr) {
    X509_STORE_add_cert(store, cert);
  }
}

// The Windows system store keeps the roots TLS clients trust; load every one
// of them into the context. Elsewhere OpenSSL's default bundle does the job.
void load_system_roots(ssl::context& ctx) {
#ifdef _WIN32
  HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
  if (store == nullptr) return;
  PCCERT_CONTEXT current = nullptr;
  while ((current = CertEnumCertificatesInStore(store, current)) != nullptr) {
    const unsigned char* encoded = current->pbCertEncoded;
    X509* cert =
        d2i_X509(nullptr, &encoded, static_cast<long>(current->cbCertEncoded));
    if (cert != nullptr) {
      add_trusted_certificate(ctx, cert);
      X509_free(cert);
    }
  }
  CertCloseStore(store, 0);
#else
  ctx.set_default_verify_paths();
#endif
}

void load_pem_roots(ssl::context& ctx, const std::string& pem) {
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (bio == nullptr) return;
  for (;;) {
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) break;
    add_trusted_certificate(ctx, cert);
    X509_free(cert);
  }
  BIO_free(bio);
}

ssl::context make_tls_context(const std::string& trusted_roots_pem) {
  ssl::context ctx(ssl::context::tls_client);
  ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                  ssl::context::no_sslv3);
  ctx.set_verify_mode(ssl::verify_peer);
  if (trusted_roots_pem.empty()) {
    load_system_roots(ctx);
  } else {
    load_pem_roots(ctx, trusted_roots_pem);
  }
  return ctx;
}

// The two WebSocket stream kinds behind the same calls: plain TCP, and TLS.
class StreamOps {
 public:
  virtual ~StreamOps() = default;

  virtual tcp::socket& socket() = 0;
  virtual void binary(bool value) = 0;
  virtual void async_tls_handshake(const std::string& host,
                                   DoneHandler done) = 0;
  virtual void async_websocket_handshake(const std::string& host_header,
                                         const std::string& path,
                                         DoneHandler done) = 0;
  virtual void async_read(beast::flat_buffer& buffer, ReadHandler done) = 0;
  virtual void async_write(const std::vector<std::uint8_t>& frame,
                           ReadHandler done) = 0;
};

class PlainOps final : public StreamOps {
 public:
  explicit PlainOps(asio::io_context& ioc) : ws_(ioc) { ws_.read_message_max(kMaxFrameBytes); }

  tcp::socket& socket() override { return beast::get_lowest_layer(ws_).socket(); }
  void binary(bool value) override { ws_.binary(value); }
  void async_tls_handshake(const std::string&, DoneHandler done) override {
    done(beast::error_code{});
  }
  void async_websocket_handshake(const std::string& host_header,
                                 const std::string& path,
                                 DoneHandler done) override {
    ws_.async_handshake(host_header, path, std::move(done));
  }
  void async_read(beast::flat_buffer& buffer, ReadHandler done) override {
    ws_.async_read(buffer, std::move(done));
  }
  void async_write(const std::vector<std::uint8_t>& frame,
                   ReadHandler done) override {
    ws_.async_write(asio::buffer(frame), std::move(done));
  }

 private:
  websocket::stream<beast::tcp_stream> ws_;
};

class TlsOps final : public StreamOps {
 public:
  TlsOps(asio::io_context& ioc, const std::string& trusted_roots_pem)
      : ctx_(make_tls_context(trusted_roots_pem)), ws_(ioc, ctx_) {
    ws_.read_message_max(kMaxFrameBytes);
  }

  tcp::socket& socket() override { return beast::get_lowest_layer(ws_).socket(); }
  void binary(bool value) override { ws_.binary(value); }
  void async_tls_handshake(const std::string& host, DoneHandler done) override {
    // SNI, then the certificate must match `host` and chain to a trusted root.
    SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host.c_str());
    ws_.next_layer().set_verify_callback(ssl::host_name_verification(host));
    ws_.next_layer().async_handshake(ssl::stream_base::client, std::move(done));
  }
  void async_websocket_handshake(const std::string& host_header,
                                 const std::string& path,
                                 DoneHandler done) override {
    ws_.async_handshake(host_header, path, std::move(done));
  }
  void async_read(beast::flat_buffer& buffer, ReadHandler done) override {
    ws_.async_read(buffer, std::move(done));
  }
  void async_write(const std::vector<std::uint8_t>& frame,
                   ReadHandler done) override {
    ws_.async_write(asio::buffer(frame), std::move(done));
  }

 private:
  ssl::context ctx_;
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
};

}  // namespace

struct WsTransport::Impl {
  enum class State : std::uint8_t {
    idle,
    resolving,
    connecting,
    tls_handshaking,
    handshaking,
    open,
    closed,
  };

  asio::io_context ioc;
  tcp::resolver resolver{ioc};
  std::shared_ptr<StreamOps> wire;

  bool tls = false;
  std::string trusted_roots_pem;

  beast::flat_buffer read_buffer;
  std::vector<std::uint8_t> writing_frame;

  std::deque<TransportEvent> events;
  std::deque<std::vector<std::uint8_t>> reliable_writes;
  std::array<std::deque<std::vector<std::uint8_t>>, protocol::kChannelCount>
      unreliable_writes;
  std::size_t dropped_unreliable = 0;  // frames dropped from a full queue
  // ORM: bytes and frames in the queues above (not the one being written).
  std::size_t queued_bytes = 0;
  std::size_t queued_frames = 0;
  std::size_t reliable_bytes = 0;

  bool writing = false;
  State state = State::idle;
  std::string host_header;
  std::string path = "/";
  Clock::time_point deadline{};
  bool has_deadline = false;
  std::uint32_t generation = 0;

  std::string resolved_host;
  bool resolved = false;

  void reset_wire() {
    writing = false;
    writing_frame.clear();
    reliable_writes.clear();
    for (auto& queue : unreliable_writes) queue.clear();
    queued_bytes = 0;
    queued_frames = 0;
    reliable_bytes = 0;
    if (wire) {
      beast::error_code ignored;
      wire->socket().close(ignored);
      wire.reset();
    }
  }

  void fail(std::uint32_t gen) {
    if (gen != generation || state == State::closed) return;
    state = State::closed;
    has_deadline = false;
    reset_wire();
    events.push_back(TransportEvent{TransportEventKind::disconnected, 0, {}});
  }

  void start_websocket_handshake(std::uint32_t gen) {
    state = State::handshaking;
    wire->async_websocket_handshake(
        host_header, path, [this, gen](beast::error_code ec) {
          if (gen != generation || state != State::handshaking) return;
          if (ec) {
            fail(gen);
            return;
          }
          state = State::open;
          has_deadline = false;
          events.push_back(
              TransportEvent{TransportEventKind::connected, 0, {}});
          start_read(gen);
        });
  }

  void maybe_start_write() {
    if (state != State::open || !wire || writing) return;
    std::vector<std::uint8_t> frame;
    if (!reliable_writes.empty()) {
      frame = std::move(reliable_writes.front());
      reliable_writes.pop_front();
      reliable_bytes -= frame.size();
    } else {
      bool found = false;
      for (std::uint8_t channel = kFirstUnreliableChannel;
           channel < protocol::kChannelCount; ++channel) {
        auto& queue = unreliable_writes[channel];
        if (!queue.empty()) {
          frame = std::move(queue.front());
          queue.pop_front();
          found = true;
          break;
        }
      }
      if (!found) return;
    }
    queued_bytes -= frame.size();
    --queued_frames;
    writing = true;
    writing_frame = std::move(frame);
    const std::uint32_t gen = generation;
    wire->async_write(writing_frame,
                      [this, gen](beast::error_code ec, std::size_t) {
                        if (gen != generation || state != State::open) {
                          writing = false;
                          return;
                        }
                        writing = false;
                        writing_frame.clear();
                        if (ec) {
                          fail(gen);
                          return;
                        }
                        maybe_start_write();
                      });
  }

  void start_read(std::uint32_t gen) {
    if (!wire) return;
    wire->async_read(read_buffer, [this, gen](beast::error_code ec, std::size_t) {
      if (gen != generation || state != State::open) return;
      if (ec) {
        fail(gen);
        return;
      }
      const auto data = read_buffer.data();
      const auto* bytes = static_cast<const std::uint8_t*>(data.data());
      const std::size_t size = data.size();
      if (size >= 1) {
        TransportEvent event;
        event.kind = TransportEventKind::received;
        event.channel = bytes[0];
        event.data.assign(bytes + 1, bytes + size);
        events.push_back(std::move(event));
      }
      read_buffer.consume(read_buffer.size());
      start_read(gen);
    });
  }

  // Sends what is queued now instead of at the next service(): starts the
  // next write and runs the handlers that are ready, so each write that has
  // finished starts the one after it. Bounded, like Client::service.
  void pump_writes() {
    maybe_start_write();
    for (int round = 0; round < kMaxPumpRounds && writing; ++round) {
      if (ioc.poll() == 0) break;
    }
  }

  void check_timeout() {
    if (!has_deadline || state == State::open || state == State::closed) return;
    if (Clock::now() < deadline) return;
    fail(generation);
  }
};

WsTransport::WsTransport(bool tls) : impl_(std::make_unique<Impl>()) {
  impl_->tls = tls;
}

WsTransport::~WsTransport() {
  disconnect_now();
  impl_->ioc.stop();
}

void WsTransport::set_trusted_roots(std::string pem) {
  impl_->trusted_roots_pem = std::move(pem);
}

bool WsTransport::start() { return true; }

bool WsTransport::resolve(const std::string& host) {
  if (host.empty()) return false;
  impl_->resolved_host = host;
  impl_->resolved = true;
  return true;
}

void WsTransport::connect(const std::string& host, std::uint16_t port,
                          const std::string& path,
                          std::uint32_t timeout_ms) {
  disconnect_now();
  Impl& d = *impl_;
  const std::uint32_t gen = ++d.generation;
  d.state = Impl::State::resolving;
  d.path = path.empty() ? "/" : path;
  d.host_header = host + ":" + std::to_string(port);
  d.deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  d.has_deadline = true;
  if (d.tls) {
    d.wire = std::make_shared<TlsOps>(d.ioc, d.trusted_roots_pem);
  } else {
    d.wire = std::make_shared<PlainOps>(d.ioc);
  }
  d.wire->binary(true);

  d.resolver.async_resolve(
      host, std::to_string(port),
      [this, gen, host](beast::error_code ec,
                        tcp::resolver::results_type results) {
        Impl& d = *impl_;
        if (gen != d.generation || d.state != Impl::State::resolving) return;
        if (ec || results.empty()) {
          d.fail(gen);
          return;
        }
        d.state = Impl::State::connecting;
        asio::async_connect(
            d.wire->socket(), results,
            [this, gen, host](beast::error_code ec, const tcp::endpoint&) {
              Impl& d = *impl_;
              if (gen != d.generation || d.state != Impl::State::connecting) {
                return;
              }
              if (ec) {
                d.fail(gen);
                return;
              }
              // Nagle off: our frames are small and each one is due now. With
              // it on, a frame written while the previous one is unacknowledged
              // waits for that ACK (a round trip, or the peer's delayed ACK of
              // up to 40 ms or more).
              beast::error_code ignored;
              d.wire->socket().set_option(tcp::no_delay(true), ignored);
              if (!d.tls) {
                d.start_websocket_handshake(gen);
                return;
              }
              d.state = Impl::State::tls_handshaking;
              d.wire->async_tls_handshake(
                  host, [this, gen](beast::error_code ec) {
                    Impl& d = *impl_;
                    if (gen != d.generation ||
                        d.state != Impl::State::tls_handshaking) {
                      return;
                    }
                    if (ec) {
                      d.fail(gen);
                      return;
                    }
                    d.start_websocket_handshake(gen);
                  });
            });
      });
}

void WsTransport::disconnect_now() {
  Impl& d = *impl_;
  ++d.generation;
  d.state = Impl::State::closed;
  d.has_deadline = false;
  d.events.clear();
  d.reset_wire();
}

bool WsTransport::send(std::uint8_t channel, const std::uint8_t* data,
                       std::size_t size) {
  Impl& d = *impl_;
  if (d.state != Impl::State::open || !d.wire) return false;
  if (channel >= protocol::kChannelCount) return false;
  std::vector<std::uint8_t> frame;
  frame.reserve(size + 1);
  frame.push_back(channel);
  frame.insert(frame.end(), data, data + size);
  const std::size_t frame_size = frame.size();
  if (channel < kFirstUnreliableChannel) {
    if (d.reliable_bytes + frame_size > kMaxReliableQueuedBytes) {
      // ORM: the socket has not taken our reliable frames for a long while;
      // nothing can be dropped from this queue, so the connection ends and
      // the client reconnects (Client logs the link as it was).
      d.fail(d.generation);
      return false;
    }
    d.reliable_bytes += frame_size;
    d.reliable_writes.push_back(std::move(frame));
  } else {
    auto& queue = d.unreliable_writes[channel];
    if (queue.size() >= kMaxUnreliableWaiting) {
      d.queued_bytes -= queue.front().size();
      --d.queued_frames;
      queue.pop_front();  // the connection is behind: the oldest goes, as a lost packet would on UDP
      ++d.dropped_unreliable;
    }
    queue.push_back(std::move(frame));
  }
  d.queued_bytes += frame_size;
  ++d.queued_frames;
  return true;
}

TransportEvent WsTransport::service(std::uint32_t wait_ms) {
  Impl& d = *impl_;
  // An io_context that once ran out of work stays stopped until restarted.
  if (d.ioc.stopped()) d.ioc.restart();
  d.pump_writes();
  d.ioc.poll();
  d.maybe_start_write();
  if (d.events.empty() && wait_ms > 0) {
    // Wait for the first event, not for the whole wait: run_for() kept a frame
    // that had just arrived until the time was up.
    const auto until = Clock::now() + std::chrono::milliseconds(wait_ms);
    while (d.events.empty() && Clock::now() < until) {
      if (d.ioc.run_one_until(until) == 0) break;
      d.maybe_start_write();
    }
  }
  d.check_timeout();
  if (!d.events.empty()) {
    TransportEvent event = std::move(d.events.front());
    d.events.pop_front();
    return event;
  }
  return TransportEvent{};
}

void WsTransport::flush() {
  // Client::service() sends its PINGs and answers after the transport's
  // service(); without this they waited for the next round (2 ms or more in
  // the hook) before going out.
  Impl& d = *impl_;
  if (d.ioc.stopped()) d.ioc.restart();
  d.pump_writes();
}

bool WsTransport::stats(TransportStats& out) const {
  // ORM (orm_link_watch.hpp): what waits to go out. No round trip of its own
  // (has_round_trip stays false; the client has its PING times).
  const Impl& d = *impl_;
  if (d.state != Impl::State::open || !d.wire) return false;
  const std::size_t bytes = d.queued_bytes + (d.writing ? d.writing_frame.size() : 0);
  const std::size_t frames = d.queued_frames + (d.writing ? 1 : 0);
  out.send_queued_bytes = static_cast<std::uint32_t>(std::min<std::size_t>(bytes, 0xffffffffu));
  out.send_queued_frames = static_cast<std::uint32_t>(std::min<std::size_t>(frames, 0xffffffffu));
  out.send_dropped_frames = d.dropped_unreliable;
  return true;
}

bool WsTransport::no_delay() const {
  const Impl& d = *impl_;
  if (!d.wire || !d.wire->socket().is_open()) return false;
  tcp::no_delay option;
  beast::error_code ec;
  d.wire->socket().get_option(option, ec);
  return !ec && option.value();
}

}  // namespace coop::net
