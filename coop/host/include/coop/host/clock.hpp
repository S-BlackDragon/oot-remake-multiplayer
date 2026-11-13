#pragma once

// ORM, one clock for the whole session (oot-remake docs/HORA.md; D-R12 "one world"; oot-64
// D-062 "Reloj"). In the remake each game runs its own time of day; upstream's coop_time shares
// the host's clock by setting the others' with the game's setter, which runs the game's time
// change (the area's grass and objects are made again, the clock stops some seconds: issue 159).
// This keeps one shared clock and moves the others smoothly instead:
//
// - The host owns the shared clock. While its own clock runs it is the host's clock. While the
//   host stands (indoors, a dungeon: the game stops the clock there) or has no clock (a load,
//   the title), it is the clock of the lowest-numbered mate whose clock runs (that mate is
//   outdoors), as oot-64's room clock runs while any group is outdoors. With nobody outdoors it
//   stands. The host sends it in TIME_OF_DAY about once a second.
// - Every other game sends its own clock in a plain TIME_OF_DAY once a second (a report: the
//   host learns who is outdoors). A plain TIME_OF_DAY counts as the shared clock only from the
//   host's number.
// - A follower (every game but the host, and the host itself after it was indoors while a mate
//   kept the clock running) moves its clock to the shared one by nudges: a few in-game seconds
//   added to or taken from the game's own counter per tick, at most `max_skew` of its rate (the
//   sky runs between half and one and a half times as fast). No time change, nothing remade.
//   Within `deadband_s` it does nothing; once it has had to catch up it goes on to a quarter of it. It never nudges a clock that stands (indoors) and never
//   across midnight.
// - The game's setter (a time change) only: when joining (more than `join_set_s` off), on
//   arriving outdoors after standing (more than `arrival_set_s` off: the scene has just loaded,
//   so nothing visible is remade), as a last resort when more than `resync_set_s` off while
//   playing (at most every `set_every_us`), and for a jump.
// - A jump is the game moving its own clock by itself: the Sun's Song, a night's sleep, an event
//   or a scene that sets the time. A read more than `jump_s` from where the clock should be
//   (its rate times the time since the last read, plus our nudges) is one; it is sent once the
//   clock has settled for `jump_settle_us` (a cutscene's time-lapse moves it on every read), with
//   the jump bit, and every other game, the host's too, sets its clock to it at once. Our own
//   set is no jump. The first read after no clock (a load, a save loaded) is no jump either:
//   loading a save must not move everyone's clock (as upstream).
// - Nudges that do not hold (the game puts its counter back: the hook checks each one on the
//   next frame) turn nudging off for the session; following then falls back to the setter
//   where it hides best (docs/RIESGOS_RESUELTOS.md, "Mundo e historia"): on arriving outdoors
//   past `deadband_s` (the scene has just loaded), while playing past `fallback_set_s` (at
//   most every `set_every_us`, issue 159), and upstream's hour rule. Each such set says
//   "nudges do not hold" in its log line, so a run shows which path kept the clocks together.
//
// Plain decisions over plain readings: no game addresses, no sockets, no threads but the inbox.
// The hook (lichii/hook/coop_clock.cpp) reads the clock (upstream's reader, coop_time.cpp),
// writes the nudges on the game's calc thread and runs the setter.
//
// Doc: oot-remake docs/HORA.md.
// Switch: LICHII_ORM_CLOCK=0 (upstream's coop_time_sync rules instead); LICHII_ORM_CLOCK_NUDGE=0
//   (never nudge: sets as upstream, with the shared clock and the jump rules kept).
// Test: tests/host/clock_test.cpp.

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

// Seconds from a to b the short way round midnight (-43200 to 43199).
std::int32_t clock_ahead(std::uint32_t a, std::uint32_t b);

// "HH:MM:SS" of seconds since midnight.
std::string clock_text(std::uint32_t seconds);

struct ClockSettings {
  bool nudge = true;                           // LICHII_ORM_CLOCK_NUDGE=0: never nudge
  std::uint64_t send_every_us = 1'000'000;     // TIME_OF_DAY: the host's clock, or our report
  std::uint32_t deadband_s = 20;               // in step: about a sixth of a real second
  double max_skew = 0.5;                       // a nudged clock runs at 0.5x to 1.5x
  double gain_per_s = 0.5;                     // share of the offset a nudge closes per real second
  std::uint32_t jump_s = 600;                  // ten in-game minutes off where the clock should be
  std::uint64_t jump_settle_us = 1'000'000;    // a jump is sent once the clock runs on from it
  std::uint64_t jump_give_up_us = 20'000'000;  // a clock that never settles: send what it reads
  std::uint32_t join_set_s = 120;              // joining (as upstream's kDriftSeconds)
  std::uint32_t arrival_set_s = 1'800;         // arriving outdoors: half an in-game hour
  std::uint64_t stood_for_us = 2'000'000;      // a clock that stood this long, then runs: arrived
  std::uint64_t arrival_window_us = 3'000'000;
  std::uint32_t resync_set_s = 3'600;          // playing: one in-game hour (upstream's kResync)
  std::uint64_t set_every_us = 60'000'000;     // resync sets at most this often (issue 159)
  std::uint64_t set_gap_us = 10'000'000;       // any two sets but jumps at least this far apart
  std::uint64_t set_lands_us = 10'000'000;     // our set has landed, or we stop waiting for it
  std::uint64_t ignore_host_after_jump_us = 5'000'000;  // the host's words from before a jump
  std::uint64_t word_stale_us = 5'000'000;     // a word older than this says nothing
  std::uint64_t extrapolate_us = 3'000'000;    // a word is run forward at most this far
  std::uint64_t track_window_us = 8'000'000;   // the rate of a clock: over its words this recent
  double default_rate = 120.0;                 // in-game s per real s (live, upstream docs/time.md)
  int nudge_checks = 4;                        // checked nudges before judging them
  // Nudges that do not hold: while playing, the setter past this far off (five in-game
  // minutes, about 2.5 real seconds at 120 s/s), at most every set_every_us.
  std::uint32_t fallback_set_s = 300;
};

struct ClockInput {
  std::uint64_t now_us = 0;            // a steady clock, the same one receive() is given
  std::optional<std::uint32_t> game;   // our clock, seconds since midnight; nothing: no clock
  bool connected = true;               // false: reset (no session)
  bool host = false;
  std::uint8_t host_number = 0;        // 0: not known yet
  std::uint8_t our_number = 0;         // 0: not known yet
  std::int32_t nudged = 0;             // seconds the hook added to our clock since the last tick
  int nudges_held = 0;                 // nudges the hook checked since the last tick: they held
  int nudges_lost = 0;                 // ... the game put its counter back
};

// no_nudge_arrival / no_nudge: the sets that stand in for nudges that do not hold.
enum class ClockSetWhy : std::uint8_t { join, arrival, resync, jump, no_nudge_arrival, no_nudge };
const char* clock_set_why(ClockSetWhy why);

struct ClockOutput {
  std::optional<std::uint32_t> set;   // run the game's setter (a time change)
  ClockSetWhy why = ClockSetWhy::join;
  std::int32_t nudge = 0;             // add this to our clock now (no time change); 0: nothing
  std::vector<protocol::TimeOfDay> send;
  std::vector<std::string> log;       // lines without the "coop: clock: " prefix
};

inline constexpr std::uint8_t kClockStands = 0;
inline constexpr std::uint8_t kClockRunsOn = 255;

// What a game says about the clocks, for the debug line and the tests.
struct ClockStatus {
  std::optional<std::uint32_t> shared;  // the shared clock now (the host's, or ours as host)
  std::optional<std::int32_t> off;      // shared minus ours, when both are known
  // Host: whose clock the shared one is: our number (ours), a mate's number, kClockStands
  // (nobody outdoors) or kClockRunsOn (run on at its last rate: no mate in step yet).
  std::uint8_t anchor = 0;
  bool running = false;                 // our clock runs
  bool in_step = false;
  bool nudges = true;                   // nudging still on
  double rate = 0.0;                    // our clock's own rate, in-game s per real s
};

class Clock {
public:
  explicit Clock(ClockSettings settings = {});

  // Any thread (the network's): a TIME_OF_DAY relayed from another game, `at_us` on the same
  // steady clock as tick(). Applied on the next tick().
  void receive(const protocol::TimeOfDay& message, std::uint64_t at_us);

  // A few times a second (the hook: every quarter second).
  ClockOutput tick(const ClockInput& in);

  // A set the hook made by itself (a test switch that puts our clock off without telling the
  // others): no jump, and it lands like ours.
  void our_set(std::uint32_t seconds, std::uint64_t now_us);

  void reset();
  ClockStatus status() const { return status_; }

private:
  enum class Anchor : std::uint8_t { stands, ours, mate, runs_on };

  // One game's clock as its words say it: the rate over its recent words, run forward.
  struct Track {
    struct Word {
      std::uint32_t seconds = 0;
      std::uint64_t at_us = 0;
    };
    std::deque<Word> words;
    double rate = 0.0;  // 0: stands (or one word only)
    void add(std::uint32_t seconds, std::uint64_t at_us, const ClockSettings& s);
    void clear() { words.clear(); rate = 0.0; }
    bool fresh(std::uint64_t now_us, const ClockSettings& s) const;
    bool running(std::uint64_t now_us, const ClockSettings& s) const;
    std::optional<std::uint32_t> at(std::uint64_t now_us, const ClockSettings& s) const;
  };
  struct Inbox {
    protocol::TimeOfDay message;
    std::uint64_t at_us = 0;
  };

  void read_ours(const ClockInput& in, ClockOutput& out);
  void take_words(const ClockInput& in, ClockOutput& out, std::vector<Inbox>& inbox);
  void host_shared(const ClockInput& in, ClockOutput& out);
  void follow(const ClockInput& in, std::optional<std::uint32_t> target, ClockOutput& out);
  void set(std::uint32_t seconds, ClockSetWhy why, const ClockInput& in, ClockOutput& out);
  void note(bool in_step, std::int32_t off, ClockOutput& out);
  void anchor(Anchor a, std::uint8_t mate, ClockOutput& out);
  std::optional<std::uint32_t> shared_at(std::uint64_t now_us) const;

  ClockSettings s_;

  // Our clock.
  std::optional<std::uint32_t> last_;  // at the last tick
  std::uint64_t last_at_ = 0;
  double dt_ = 0.0;                    // real seconds since the tick before (at most 1)
  double rate_ = 0.0;                  // our own rate (without our nudges); 0: not measured
  bool running_ = false;
  std::optional<std::uint64_t> running_since_;
  std::optional<std::uint64_t> stood_since_;  // standing or no clock since
  std::optional<std::uint64_t> arrived_at_;
  std::optional<std::uint32_t> jump_;   // a jump of ours not sent yet: where it went
  std::uint64_t jump_at_ = 0;           // when it was seen (restarts while the clock still jumps)
  std::uint64_t jump_first_at_ = 0;
  std::optional<std::uint32_t> landing_;  // a set of ours that has not landed yet
  std::uint64_t set_at_ = 0;
  bool set_once_ = false;
  std::uint64_t resync_at_ = 0;
  bool resync_once_ = false;
  std::uint64_t fallback_at_ = 0;  // the last no_nudge set (playing)
  bool fallback_once_ = false;
  std::uint64_t ignore_host_until_ = 0;

  // The words.
  std::map<std::uint8_t, Track> tracks_;  // by player number
  bool joined_ = false;                   // the host's first word was seen (follower)
  std::uint64_t sent_at_ = 0;
  bool sent_once_ = false;

  // The shared clock (host).
  bool was_host_ = false;
  std::optional<std::uint32_t> shared_;
  std::uint64_t shared_at_us_ = 0;
  double shared_rate_ = 0.0;
  Anchor anchor_ = Anchor::stands;
  std::uint8_t anchor_mate_ = 0;
  bool detached_ = false;    // host: our clock follows the shared one (we were indoors)

  // Nudges.
  bool nudges_ = true;
  int held_ = 0;
  int lost_ = 0;
  bool in_step_ = true;

  ClockStatus status_;

  mutable std::mutex inbox_mutex_;
  std::vector<Inbox> inbox_;
};

}  // namespace coop::host
