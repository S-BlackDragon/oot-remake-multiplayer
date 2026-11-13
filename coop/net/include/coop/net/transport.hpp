#pragma once

// The link between Client and one wire: ENet over UDP, or WebSocket over TCP.
// Client owns one of these and calls it from service(); no threads and no
// game code. Every call is single-threaded, like the rest of coop::net.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::net {

enum class TransportEventKind : std::uint8_t {
  none,          // nothing happened in the time given
  connected,     // the attempt got a connection (and the WS handshake)
  received,      // one protocol message: `channel` and `data`
  disconnected,  // the attempt or the connection ended
};

struct TransportEvent {
  TransportEventKind kind = TransportEventKind::none;
  std::uint8_t channel = 0;
  std::vector<std::uint8_t> data;  // the encoded protocol message (sealed once the keys are made), no channel byte
  // disconnected only: the server ended it to have us connect again with
  // small datagrams (ENet's kDisconnectSmallDatagrams, enet_path.hpp).
  bool small_path = false;
};

// How the wire looks now (issue 143), for Client::link(). ENet fills it from its
// peer; a wire with no such numbers leaves it as is and says false.
struct TransportStats {
  std::uint32_t round_trip_ms = 0;
  std::uint32_t loss_percent = 0;
  std::uint32_t mtu = 0;
  std::uint32_t waiting_bytes = 0;  // what ENet holds for us now (enet_waiting.hpp)
  // ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento"):
  // what we have handed over and is not on the wire yet, so a link that
  // cannot carry our poses is told, never degraded silently (coop/host
  // orm_link_watch.hpp). ENet: its outgoing queues plus reliable data in
  // transit; WebSocket: the frames waiting and the one being written.
  std::uint32_t send_queued_bytes = 0;
  std::uint32_t send_queued_frames = 0;
  // Our unreliable frames dropped before they went out, since start()
  // (WebSocket: a full queue). ENet never says (its throttle drops silently).
  std::uint64_t send_dropped_frames = 0;
  bool has_round_trip = false;  // round_trip_ms, loss_percent and mtu are real (ENet)
};

class Transport {
 public:
  virtual ~Transport() = default;
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  // Takes whatever the transport needs (ENet, an io_context). False when it
  // cannot start.
  virtual bool start() = 0;
  // Resolves and caches `host`. False when it cannot be resolved; a connect
  // that cannot route then reports disconnected.
  virtual bool resolve(const std::string& host) = 0;
  // Starts one attempt to the cached host. `path` is the WebSocket path
  // ("/" when empty); `timeout_ms` is the connect (and WS handshake) limit.
  virtual void connect(const std::string& host, std::uint16_t port,
                       const std::string& path, std::uint32_t timeout_ms) = 0;
  // Ends the current attempt or connection at once, sending a goodbye when
  // the wire has one.
  virtual void disconnect_now() = 0;
  // Sends one protocol message on `channel`. False when not connected.
  virtual bool send(std::uint8_t channel, const std::uint8_t* data,
                    std::size_t size) = 0;
  // Drives the wire, waiting at most `wait_ms` for traffic, and returns the
  // next event.
  virtual TransportEvent service(std::uint32_t wait_ms) = 0;
  // Sends what is queued now.
  virtual void flush() = 0;
  // The link's numbers while there is a peer. False when there is none or the
  // wire has no such numbers.
  virtual bool stats(TransportStats& /*out*/) const { return false; }
  // Sets ENet's count of the peer's waiting data to what really waits and
  // returns the bytes taken off (issue 175, enet_waiting.hpp). Wires without
  // such a count return 0.
  virtual std::uint64_t mend_waiting_data() { return 0; }
  // Upstream's small path check (issue 143, enet_path.hpp), once per
  // service() while there is a peer: true when full datagrams are lost while
  // small ones get through. Wires without datagrams (WebSocket) never are.
  virtual bool full_datagrams_lost() { return false; }
  // From now on, every connection uses ENet's smallest datagram
  // (kSmallDatagramBytes). Nothing on other wires.
  virtual void use_small_datagrams() {}

 protected:
  Transport() = default;
};

}  // namespace coop::net
