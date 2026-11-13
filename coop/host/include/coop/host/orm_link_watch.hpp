#pragma once

// ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento";
// docs/ANCHO_DE_BANDA.md): a connection that cannot keep up is told, never
// degraded silently. Six players in one place are about 1.1 Mbit/s up and
// 5.4 Mbit/s down per player at the native pose rate (decision D-R69: no pose
// compression), so a slow line loses poses: the relay drops the oldest
// unreliable frames for a receiver behind (orm-server send_queue.hpp), and
// our own transport drops the oldest of ours when the uplink is behind.
// Both are right (a pose 250 ms late is useless), but the player must know
// why the others jump, or why they see him jump.
//
// LinkWatch decides, from what the client already measures, when to warn and
// when the link is fine again, and gives the log line (English, like the rest
// of the log) and the player's notice (Spanish, for the HUD and the window's
// chat). Nothing changes for a link that keeps up: no notice, no log line.
//
// - Uplink (sample()): our unreliable frames dropped in the last window_ms,
//   our bytes waiting to go out staying high for queued_for_ms, or ENet's
//   loss estimate staying high for loss_for_ms.
// - Downlink (notice()): the relay's LINK_NOTICE behind / caught_up.
// - Each kind of warning reaches the player at most once per repeat_ms; the
//   log has every change. "Fine again" only after calm_ms without a trigger,
//   and only told to the player when he was told it was bad.
//
// No emulator, no clock and no threads of its own: the caller passes the
// time (any steady milliseconds) and calls it from one thread.

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

struct LinkWatchConfig {
  std::uint64_t window_ms = 5000;
  // Our own unreliable frames dropped within window_ms (WebSocket: a full
  // queue). A pose every 33 ms and samples at 60 a second: 15 is a quarter
  // of a second of poses lost in five.
  std::uint64_t dropped_to_warn = 15;
  // Our bytes waiting to go out, held this long. A normal join sends the
  // whole flag table at once (a few hundred KiB for a moment), so it takes
  // a few seconds above it.
  std::uint32_t queued_bytes_to_warn = 256u << 10;
  std::uint64_t queued_for_ms = 3000;
  // ENet's loss estimate (UDP only), held this long.
  std::uint32_t loss_percent_to_warn = 20;
  std::uint64_t loss_for_ms = 5000;
  // No trigger for this long: the link keeps up again.
  std::uint64_t calm_ms = 10000;
  // The same warning reaches the player at most once in this time.
  std::uint64_t repeat_ms = 60000;
};

// What the client measures now (coop::net::LinkInfo, plus whether it is
// connected and whether the loss figure is real).
struct LinkSample {
  std::uint64_t now_ms = 0;
  bool connected = false;
  std::uint32_t send_queued_bytes = 0;
  std::uint64_t send_dropped_frames = 0;  // a total that only grows
  bool has_loss = false;                  // UDP: ENet's estimate is real
  std::uint32_t loss_percent = 0;
};

struct LinkAdvice {
  bool warning = false;  // log as a warning (else as info)
  std::string log;       // "coop: link: ..."
  std::string notice;    // the player's Spanish line, "" for none
};

class LinkWatch {
 public:
  explicit LinkWatch(LinkWatchConfig config = {});

  // Our uplink, every round or so (at least once a second).
  std::vector<LinkAdvice> sample(const LinkSample& s);
  // A LINK_NOTICE from the relay (Client::take_link_notices()).
  std::vector<LinkAdvice> notice(const protocol::LinkNotice& n, std::uint64_t now_ms);

  bool uplink_behind() const { return up_.behind; }
  bool downlink_behind() const { return down_.behind; }

 private:
  struct Side {
    bool behind = false;
    bool told = false;  // the player was told it is behind
    std::uint64_t behind_since_ms = 0;
    std::optional<std::uint64_t> told_ms;  // the last behind notice
  };
  struct Drops {
    std::uint64_t at_ms = 0;
    std::uint64_t total = 0;
  };
  bool may_tell(const Side& side, std::uint64_t now_ms) const;

  LinkWatchConfig config_;
  Side up_;
  Side down_;
  std::deque<Drops> drops_;
  std::optional<std::uint64_t> queued_since_ms_;
  std::optional<std::uint64_t> loss_since_ms_;
  std::uint64_t last_trigger_ms_ = 0;
};

// The player's Spanish line for a REFUSED from the server, with what is known
// of both sides' protocol (the relay says its own first: Client::
// server_protocol()). `ours` is this build's protocol::kProtocolVersion.
std::string refused_notice(protocol::RefuseReason reason, std::optional<std::uint16_t> server_protocol,
                           std::uint16_t ours);

}  // namespace coop::host
