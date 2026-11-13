#include "enet_transport.hpp"

// ENet pulls in windows.h on Windows; keep its min and max macros out.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <enet/enet.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "coop/net/client.hpp"
#include "coop/net/enet_path.hpp"
#include "coop/net/enet_waiting.hpp"
#include "coop/net/path_mark.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::net {

namespace {

// The longest packet a message can fill: the 4-byte header and a body of
// at most 65535 bytes, and what sealing adds (traffic_crypto.hpp). ENet
// refuses anything longer before assembling it.
constexpr std::size_t kMaxPacketBytes = 4 + 0xffff + protocol::kSealOverheadBytes;

}  // namespace

std::uint32_t packet_flags(std::uint8_t channel) {
  return channel < protocol::kChannelPlayerSamples
             ? static_cast<std::uint32_t>(ENET_PACKET_FLAG_RELIABLE)
             : static_cast<std::uint32_t>(ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
}

struct EnetTransport::Impl {
  std::uint32_t mtu = protocol::kDatagramBytes;
  std::size_t max_waiting_bytes = std::size_t{8} << 20;
  bool enet_started = false;
  _ENetHost* host = nullptr;
  _ENetPeer* peer = nullptr;

  bool address_known = false;
  std::uint32_t address_host = 0;  // ENet's form, network byte order

  bool has_pending = false;
  TransportEvent pending;
  PathMark path_mark;  // this connection's small path watch (enet_path.hpp)
};

EnetTransport::EnetTransport(std::uint32_t mtu, std::size_t max_waiting_bytes)
    : impl_(std::make_unique<Impl>()) {
  impl_->mtu = mtu;
  impl_->max_waiting_bytes = max_waiting_bytes;
}

EnetTransport::~EnetTransport() {
  disconnect_now();
  if (impl_->host != nullptr) enet_host_destroy(impl_->host);
  if (impl_->enet_started) enet_deinitialize();
}

bool EnetTransport::start() {
  if (enet_initialize() != 0) return false;
  impl_->enet_started = true;
  impl_->host = enet_host_create(nullptr, 1, protocol::kChannelCount, 0, 0);
  if (impl_->host == nullptr) return false;
  impl_->host->maximumPacketSize = kMaxPacketBytes;
  impl_->host->maximumWaitingData = impl_->max_waiting_bytes;
  impl_->host->mtu = std::clamp<std::uint32_t>(impl_->mtu, ENET_PROTOCOL_MINIMUM_MTU,
                                               ENET_PROTOCOL_MAXIMUM_MTU);  // issue 143
  return true;
}

bool EnetTransport::resolve(const std::string& host) {
  if (impl_->address_known) return true;
  ENetAddress address{};
  // May block on a DNS name; an IP is read at once. Kept once it works.
  if (enet_address_set_host(&address, host.c_str()) != 0) return false;
  impl_->address_host = address.host;
  impl_->address_known = true;
  return true;
}

void EnetTransport::connect(const std::string& host, std::uint16_t port,
                            const std::string& /*path*/,
                            std::uint32_t timeout_ms) {
  if (impl_->host == nullptr) {
    impl_->has_pending = true;
    impl_->pending = TransportEvent{TransportEventKind::disconnected, 0, {}};
    return;
  }
  ENetAddress address{};
  address.port = port;
  if (!resolve(host)) {
    impl_->has_pending = true;
    impl_->pending = TransportEvent{TransportEventKind::disconnected, 0, {}};
    return;
  }
  address.host = impl_->address_host;
  impl_->path_mark = PathMark{};
  impl_->peer = enet_host_connect(impl_->host, &address,
                                  protocol::kChannelCount, 0);
  if (impl_->peer == nullptr) {
    // Nothing to retry on this host; the attempt is over.
    impl_->has_pending = true;
    impl_->pending = TransportEvent{TransportEventKind::disconnected, 0, {}};
    return;
  }
  enet_peer_timeout(impl_->peer, 0, timeout_ms, timeout_ms);
}

void EnetTransport::disconnect_now() {
  if (impl_->peer != nullptr) {
    // Sends the goodbye at once and forgets the peer; no reply is awaited.
    enet_peer_disconnect_now(impl_->peer, 0);
    impl_->peer = nullptr;
  }
}

bool EnetTransport::send(std::uint8_t channel, const std::uint8_t* data,
                         std::size_t size) {
  if (impl_->peer == nullptr) return false;
  ENetPacket* packet = enet_packet_create(data, size, packet_flags(channel));
  if (packet == nullptr) return false;
  if (enet_peer_send(impl_->peer, channel, packet) != 0) {
    enet_packet_destroy(packet);
    return false;
  }
  return true;
}

TransportEvent EnetTransport::service(std::uint32_t wait_ms) {
  if (impl_->has_pending) {
    impl_->has_pending = false;
    TransportEvent event = std::move(impl_->pending);
    impl_->pending = TransportEvent{};
    return event;
  }
  if (impl_->host == nullptr) return TransportEvent{};

  ENetEvent event;
  std::uint32_t wait = wait_ms;
  for (;;) {
    if (enet_host_service(impl_->host, &event, wait) <= 0) {
      return TransportEvent{};
    }
    wait = 0;
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        if (event.peer == impl_->peer) {
          return TransportEvent{TransportEventKind::connected, 0, {}};
        }
        break;
      case ENET_EVENT_TYPE_RECEIVE: {
        if (event.peer == impl_->peer) {
          TransportEvent received;
          received.kind = TransportEventKind::received;
          received.channel = event.channelID;
          received.data.assign(event.packet->data,
                               event.packet->data + event.packet->dataLength);
          enet_packet_destroy(event.packet);
          return received;
        }
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT:
        if (event.peer == impl_->peer) {
          impl_->peer = nullptr;  // ENet has already reset it
          TransportEvent gone{TransportEventKind::disconnected, 0, {}};
          gone.small_path = event.data == kDisconnectSmallDatagrams;
          return gone;
        }
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

void EnetTransport::flush() {
  if (impl_->host != nullptr) enet_host_flush(impl_->host);
}

bool EnetTransport::stats(TransportStats& out) const {
  const _ENetPeer* peer = impl_->peer;
  if (peer == nullptr) return false;
  out.round_trip_ms = peer->roundTripTime;
  out.loss_percent = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(peer->packetLoss) * 100 / ENET_PEER_PACKET_LOSS_SCALE);
  out.mtu = peer->mtu;
  out.waiting_bytes = static_cast<std::uint32_t>(
      std::min<std::size_t>(peer->totalWaitingData, 0xffffffffu));
  out.has_round_trip = true;
  // ORM: what waits to go out (orm_link_watch.hpp). Each queued command holds
  // one fragment or one whole packet; reliable data already sent but not
  // acknowledged counts too (a line that cannot carry it grows it).
  std::uint64_t bytes = peer->reliableDataInTransit;
  std::uint64_t frames = 0;
  for (const ENetList* list : {&peer->outgoingCommands, &peer->outgoingSendReliableCommands}) {
    for (ENetListIterator it = enet_list_begin(list); it != enet_list_end(list); it = enet_list_next(it)) {
      const auto* command = reinterpret_cast<const ENetOutgoingCommand*>(it);
      bytes += command->fragmentLength;
      ++frames;
    }
  }
  out.send_queued_bytes = static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes, 0xffffffffu));
  out.send_queued_frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, 0xffffffffu));
  return true;
}

std::uint64_t EnetTransport::mend_waiting_data() {
  if (impl_->peer == nullptr) return 0;
  return coop::net::mend_waiting_data(*impl_->peer);
}

bool EnetTransport::full_datagrams_lost() {
  if (impl_->peer == nullptr || impl_->host == nullptr) return false;
  return coop::net::full_datagrams_lost(impl_->path_mark, *impl_->peer, impl_->host->serviceTime);
}

void EnetTransport::use_small_datagrams() {
  impl_->mtu = kSmallDatagramBytes;
  if (impl_->host != nullptr && impl_->host->mtu > kSmallDatagramBytes) {
    impl_->host->mtu = kSmallDatagramBytes;
  }
}

}  // namespace coop::net
