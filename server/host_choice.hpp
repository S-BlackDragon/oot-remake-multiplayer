#pragma once

// ORM (oot-remake docs/PARTIDA_SALA.md, "Un solo anfitrion"): who hosts the
// room. One host for everything the host does: the relay sends it every
// FLAG_SET and takes its FLAG_STATE (the shared flags), its save goes to the
// guests (SAVE_PART), its clock is the room's, its group settings win, and the
// launchers make its upload the room's save. Before, the relay's host was the
// first player in (session.cpp) and the room save's the oldest launcher
// (D-R18), which could be two different players.
//
// The relay picks the host from what it measures, with hysteresis, or a
// player picks it by hand (HOST_CHOICE pick, /anfitrion <name>):
// - measured: each player's round trip to the relay (its PONGs to the relay's
//   PINGs, every 250 ms). Through a relay a message from i to j takes i's half
//   plus j's, so the host's average round trip to the others is its own plus
//   the others' mean; the lowest wins. Only a player whose game is in the world
//   (a scene, not loading, not on the title screen) and whose link is steady
//   (measured long enough, small jitter, no long silence lately) can be picked
//   by measurement. A better one takes over only when it is better by
//   max(better_ms, better_fraction of the host's) for better_hold_us in a row,
//   and not within cooldown_us of the last change.
// - unfit: a host out of the world or with an unsteady link for
//   unfit_hold_us is replaced by the best fit player.
// - wishes: a player may wish to host (the app's toggle, HostWish::me) or
//   never to (room_save_host = false, HostWish::never). Wishes rank before
//   measures: a fit player who wishes to host takes over a host who does not;
//   a host who wishes never to is replaced by any fit player who does not.
//   The measures only move the host within the same wish.
// - by hand: the picked name is kept (case ignored) until the next pick or
//   "auto" or until the room empties: while that player is in the room and
//   its game in the world, it hosts, whatever the measures and wishes say; a
//   player who comes back with that name gets it back once its game is in.
// - the host leaves: the picked player when here, else the best by wish,
//   fitness, measure and number (as before, someone always hosts).
//
// No sockets and no clocks here: the session passes its players and the
// time, so every rule is tested without a network (tests/server/host_choice_test.cpp).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "coop/protocol/messages.hpp"

namespace coop::server {

// The relay's numbers for the choice (SessionConfig::host).
struct HostRules {
  // Moves by measurement and by an unfit host; false: the host changes only
  // when it leaves, by hand and by wishes (coop_server --host-auto 0).
  bool automatic = true;
  // A link counts as steady after this many round trips and this long in the
  // room, with a mean deviation under max_jitter_ms, no gap between PONGs over
  // max_gap_us within stable_window_us, and none going on now.
  std::uint32_t min_samples = 40;  // 10 s of the relay's PINGs
  std::uint64_t warmup_us = 20'000'000;
  double max_jitter_ms = 30.0;
  std::uint64_t max_gap_us = 1'500'000;
  std::uint64_t stable_window_us = 30'000'000;
  // Hysteresis of the measured move.
  double better_ms = 20.0;
  double better_fraction = 0.25;
  std::uint64_t better_hold_us = 30'000'000;
  std::uint64_t cooldown_us = 120'000'000;
  // An unfit host is replaced after this long (a scene load takes a few s).
  std::uint64_t unfit_hold_us = 15'000'000;
};

// One player's link as the relay measures it from its PONGs. A PONG counts
// only when it echoes one of the relay's last PINGs to that player, so a game
// cannot make its own link look faster than it is.
class LinkMeasure {
 public:
  static constexpr std::size_t kPings = 16;

  void ping_sent(std::uint64_t at_us);
  // A PONG echoing `echo_us`, heard at now_us. False when it answers none of
  // our last PINGs (or the same one again): not counted.
  bool pong(std::uint64_t echo_us, std::uint64_t now_us, std::uint64_t max_gap_us);

  // The smoothed round trip (an average over the last few seconds); none
  // before the first PONG.
  std::optional<double> rtt_ms() const;
  // The smoothed deviation of each round trip from the average.
  double jitter_ms() const { return jitter_ms_; }
  std::uint32_t samples() const { return samples_; }
  std::uint64_t last_pong_us() const { return last_pong_us_; }
  // When a gap between two PONGs was over max_gap_us last (0: never).
  std::uint64_t long_gap_at_us() const { return long_gap_at_us_; }

  // Steady by the rules: see HostRules. `joined_us`: when the player joined.
  bool steady(const HostRules& rules, std::uint64_t joined_us, std::uint64_t now_us) const;

 private:
  std::uint64_t pings_[kPings] = {};
  std::size_t next_ = 0;
  double rtt_ms_ = 0.0;
  double jitter_ms_ = 0.0;
  std::uint32_t samples_ = 0;
  std::uint64_t last_pong_us_ = 0;
  std::uint64_t long_gap_at_us_ = 0;
};

// One player as the choice sees it.
struct HostCandidate {
  std::uint8_t number = 0;
  std::string nickname;
  bool in_world = false;  // a scene, not loading, not on the title screen
  bool steady = false;    // LinkMeasure::steady
  std::optional<double> rtt_ms;  // to the relay; none: not measured
  protocol::HostWish wish = protocol::HostWish::automatic;
};

// A change of host the session should make now.
struct HostMove {
  std::uint8_t to = 0;
  protocol::HostWhy why = protocol::HostWhy::measured;
  std::uint8_t by = 0;  // picked: who picked it
  std::optional<double> score_ms;  // the new host's average to the others
  std::string note;  // for the log, English
};

class HostElection {
 public:
  explicit HostElection(HostRules rules = {}) : rules_(rules) {}

  const HostRules& rules() const { return rules_; }

  // The average round trip from `c` to the other players through the relay:
  // its own plus the mean of the others' (its own alone when it is the only
  // one); none when it or any other is not measured.
  static std::optional<double> score(const HostCandidate& c, std::span<const HostCandidate> all);

  enum class PickResult : std::uint8_t {
    picked,     // the name is kept; the move follows in check()
    cleared,    // back to the measures
    not_here,   // nobody of that name in the room
    never,      // that player wishes never to host
  };
  // A player (`by`) picks `name` (empty, "auto": back to the measures).
  // `name` is matched to a player's nickname with ASCII case ignored; the
  // kept name is that player's own spelling.
  PickResult pick(std::string_view name, std::uint8_t by, std::span<const HostCandidate> all);
  // The name kept by hand (empty: none) and who picked it.
  const std::string& pinned() const { return pinned_; }
  std::uint8_t pinned_by() const { return pinned_by_; }

  // The move due now for host `host`, or none. Call every second or so; the
  // hold times count between calls. The caller makes the move and then calls
  // moved().
  std::optional<HostMove> check(std::uint8_t host, std::span<const HostCandidate> all,
                                std::uint64_t now_us);
  // The host left: who takes over among `remaining` (never empty).
  HostMove after_leave(std::span<const HostCandidate> remaining) const;
  // The session changed the host (any reason): the timers start over.
  void moved(std::uint64_t now_us);
  // The room emptied: the kept name and the timers go.
  void clear();

 private:
  // 0 me, 1 automatic, 2 never (an unknown wish counts as automatic).
  static int rank(protocol::HostWish wish);
  const HostCandidate* find(std::uint8_t number, std::span<const HostCandidate> all) const;
  const HostCandidate* pinned_player(std::span<const HostCandidate> all) const;
  // The best of `all` but `except` that is fit (in the world and steady) with a
  // rank at most `max_rank`: lowest rank, then score, then number.
  const HostCandidate* best_fit(std::span<const HostCandidate> all, std::uint8_t except,
                                int max_rank) const;
  HostMove move_to(const HostCandidate& c, protocol::HostWhy why,
                   std::span<const HostCandidate> all, std::string note) const;

  HostRules rules_;
  std::string pinned_;
  std::uint8_t pinned_by_ = 0;
  std::uint64_t changed_at_ = 0;  // the last change (0: none yet)
  std::uint64_t better_since_ = 0;
  std::uint8_t better_who_ = 0;
  std::uint64_t unfit_since_ = 0;
};

// The HostWhy's English word, for the log.
const char* to_string(protocol::HostWhy why);

}  // namespace coop::server
