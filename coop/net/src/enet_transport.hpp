#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "coop/net/transport.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::net {

// The original ENet link: one host, one peer, reliable channels 0 to 2 and
// unreliable-sequenced 3 and 4 (packet_flags in client.hpp). `mtu` is ENet's
// datagram size (ClientConfig::mtu, issue 143), held to ENet's range;
// `max_waiting_bytes` is ClientConfig::max_waiting_bytes (issue 175).
class EnetTransport final : public Transport {
 public:
  explicit EnetTransport(std::uint32_t mtu = protocol::kDatagramBytes,
                         std::size_t max_waiting_bytes = std::size_t{8} << 20);
  ~EnetTransport() override;

  bool start() override;
  bool resolve(const std::string& host) override;
  void connect(const std::string& host, std::uint16_t port,
               const std::string& path, std::uint32_t timeout_ms) override;
  void disconnect_now() override;
  bool send(std::uint8_t channel, const std::uint8_t* data,
            std::size_t size) override;
  TransportEvent service(std::uint32_t wait_ms) override;
  void flush() override;
  bool stats(TransportStats& out) const override;
  std::uint64_t mend_waiting_data() override;
  bool full_datagrams_lost() override;
  void use_small_datagrams() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace coop::net
