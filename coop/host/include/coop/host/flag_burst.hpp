#pragma once

// ORM (oot-remake docs/ANCHO_DE_BANDA.md, "Ráfagas de banderas"): the shared
// flag changes of one burst go out as one message. A scene load (or a room's
// gimmick, a cutscene) sets dozens of flags over a few frames, and the flag
// watcher sends what changed each frame: a FLAG_SET of one or two entries per
// frame, each also answered by the host's FLAG_STATE to every player. Each
// message costs its transport's overhead (a datagram, or a WebSocket frame in
// its own TCP segment, some 60 bytes) and one of the relay's per-player
// reliable messages a second (60, server/session.hpp).
//
// The rule: a change after a quiet window goes at once (nothing waits for a
// lone chest or switch); the changes that come within `window_us` of the last
// message sent are held and go together when the window ends. The client's
// set_flags merges one key given twice (the last value; count deltas added),
// so holding never loses a change. Not thread-safe: the emulator thread
// (coop::host::Session::frame) owns it.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

class FlagBurst {
 public:
  // 0: no holding, every change goes at once.
  explicit FlagBurst(std::uint64_t window_us = 0) : window_us_(window_us) {}

  void set_window_us(std::uint64_t us) { window_us_ = us; }
  std::uint64_t window_us() const { return window_us_; }

  // This frame's changes (may be empty) at `now_us`. Returns what to send
  // now: nothing while the window holds them.
  std::optional<std::vector<protocol::FlagEntry>> push(std::span<const protocol::FlagEntry> changes,
                                                       std::uint64_t now_us);
  // What is held, at once (before a FLAG_RESET or the whole table, which go
  // in order after it; a role change). Nothing when none is. The window
  // starts again at `now_us`, as after any message sent.
  std::optional<std::vector<protocol::FlagEntry>> flush(std::uint64_t now_us);

  bool holding() const { return !held_.empty(); }
  // Lists handed out to send, and frames whose changes waited for one (for
  // the log and the tests).
  std::uint64_t sent() const { return sent_; }
  std::uint64_t held_frames() const { return held_frames_; }

 private:
  std::optional<std::vector<protocol::FlagEntry>> take(std::uint64_t now_us);

  std::uint64_t window_us_ = 0;
  std::vector<protocol::FlagEntry> held_;
  std::optional<std::uint64_t> last_sent_us_;
  std::uint64_t sent_ = 0;
  std::uint64_t held_frames_ = 0;
};

}  // namespace coop::host
