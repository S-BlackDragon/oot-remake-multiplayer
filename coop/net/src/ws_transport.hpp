#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "coop/net/transport.hpp"

namespace coop::net {

// The same protocol over a WebSocket: one binary frame per protocol message,
// byte 0 the channel, then the encoded message. Single-threaded: an asio
// io_context is driven by service().
class WsTransport final : public Transport {
 public:
  // `tls` is true for wss://: TCP, then TLS, then the WebSocket handshake.
  explicit WsTransport(bool tls = false);
  ~WsTransport() override;

  // Test-only hook: PEM certificates to trust instead of the system store.
  // Empty (the default) verifies against the Windows "ROOT" store. Takes
  // effect on the next connect().
  void set_trusted_roots(std::string pem);

  // Test-only: whether the connected socket has Nagle off (TCP_NODELAY).
  // False while there is no socket.
  bool no_delay() const;

  bool start() override;
  bool resolve(const std::string& host) override;
  void connect(const std::string& host, std::uint16_t port,
               const std::string& path, std::uint32_t timeout_ms) override;
  void disconnect_now() override;
  bool send(std::uint8_t channel, const std::uint8_t* data,
            std::size_t size) override;
  TransportEvent service(std::uint32_t wait_ms) override;
  void flush() override;
  // ORM: what waits to go out (TransportStats::send_queued_*).
  bool stats(TransportStats& out) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace coop::net
