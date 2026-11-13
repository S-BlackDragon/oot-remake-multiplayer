// ORM: one clock for the whole session (D-R12; oot-64 D-062 "Reloj"). The rules are described in
//   coop/host/clock.hpp.
// Doc: oot-remake docs/HORA.md.
// Switch: LICHII_ORM_CLOCK=0, LICHII_ORM_CLOCK_NUDGE=0 (lichii/hook/coop_clock.cpp).
// Test: tests/host/clock_test.cpp.

#include "coop/host/clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace coop::host {

namespace proto = coop::protocol;

namespace {

constexpr std::uint32_t kDay = proto::kSecondsPerDay;

std::uint32_t wrap(std::int64_t seconds) {
  seconds %= static_cast<std::int64_t>(kDay);
  if (seconds < 0) seconds += kDay;
  return static_cast<std::uint32_t>(seconds);
}

std::uint32_t magnitude(std::int32_t d) {
  return d < 0 ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(d))
               : static_cast<std::uint32_t>(d);
}

std::string player(std::uint8_t number) { return "P" + std::to_string(number); }

std::string signed_seconds(std::int32_t d) {
  return (d >= 0 ? "+" : "") + std::to_string(d) + " s";
}

}  // namespace

std::int32_t clock_ahead(std::uint32_t a, std::uint32_t b) {
  const auto day = static_cast<std::int32_t>(kDay);
  std::int32_t d = static_cast<std::int32_t>(b % kDay) - static_cast<std::int32_t>(a % kDay);
  if (d >= day / 2) {
    d -= day;
  } else if (d < -day / 2) {
    d += day;
  }
  return d;
}

std::string clock_text(std::uint32_t seconds) {
  seconds %= kDay;
  char text[40];
  std::snprintf(text, sizeof(text), "%02u:%02u:%02u", seconds / 3600, seconds / 60 % 60,
                seconds % 60);
  return text;
}

const char* clock_set_why(ClockSetWhy why) {
  switch (why) {
    case ClockSetWhy::join: return "joining";
    case ClockSetWhy::arrival: return "arriving outdoors";
    case ClockSetWhy::resync: return "too far off to nudge";
    case ClockSetWhy::jump: return "a jump";
    case ClockSetWhy::no_nudge_arrival: return "arriving outdoors, nudges do not hold";
    case ClockSetWhy::no_nudge: return "nudges do not hold";
  }
  return "?";
}

// ---------------------------------------------------------------- Track

void Clock::Track::add(std::uint32_t seconds, std::uint64_t at_us, const ClockSettings& s) {
  if (!words.empty()) {
    const Word back = words.back();
    if (at_us <= back.at_us) {  // the same moment (or out of order): the newer word wins
      words.back().seconds = seconds;
      return;
    }
    const double dt = static_cast<double>(at_us - back.at_us) / 1e6;
    const std::int32_t d = clock_ahead(back.seconds, seconds);
    const double most = std::max(rate, s.default_rate) * dt * 3.0 + s.jump_s;
    if (d < 0 || d > most) {
      words.clear();  // set back, or a jump: the words before say nothing about the rate now
    } else if (words.size() >= 2 && (rate > 0.0) != (d > 0)) {
      words.clear();  // started or stopped: a new stretch
      words.push_back(back);
    }
  }
  words.push_back(Word{seconds, at_us});
  while (words.size() > 2 && at_us - words.front().at_us > s.track_window_us) words.pop_front();
  rate = 0.0;
  if (words.size() >= 2) {
    const double span = static_cast<double>(words.back().at_us - words.front().at_us) / 1e6;
    const std::int32_t moved = clock_ahead(words.front().seconds, words.back().seconds);
    if (span > 0.0 && moved > 0) rate = moved / span;
  }
}

bool Clock::Track::fresh(std::uint64_t now_us, const ClockSettings& s) const {
  return !words.empty() &&
         (now_us <= words.back().at_us || now_us - words.back().at_us <= s.word_stale_us);
}

bool Clock::Track::running(std::uint64_t now_us, const ClockSettings& s) const {
  return fresh(now_us, s) && rate > 0.0;
}

std::optional<std::uint32_t> Clock::Track::at(std::uint64_t now_us,
                                              const ClockSettings& s) const {
  if (!fresh(now_us, s) || words.size() < 2) return std::nullopt;  // one word: no rate yet
  const Word& w = words.back();
  const std::uint64_t ahead_us =
      now_us > w.at_us ? std::min<std::uint64_t>(now_us - w.at_us, s.extrapolate_us) : 0;
  return wrap(static_cast<std::int64_t>(w.seconds) +
              std::llround(rate * static_cast<double>(ahead_us) / 1e6));
}

// ---------------------------------------------------------------- Clock

Clock::Clock(ClockSettings settings) : s_(settings), nudges_(settings.nudge) {}

void Clock::receive(const proto::TimeOfDay& message, std::uint64_t at_us) {
  if (message.seconds >= kDay || (message.flags & ~proto::time_of_day::jump) != 0 ||
      message.player_number == 0) {
    return;
  }
  std::lock_guard lock(inbox_mutex_);
  if (inbox_.size() < 256) inbox_.push_back(Inbox{message, at_us});
}

void Clock::reset() {
  const ClockSettings keep = s_;
  {
    std::lock_guard lock(inbox_mutex_);
    inbox_.clear();
  }
  last_.reset();
  last_at_ = 0;
  dt_ = 0.0;
  rate_ = 0.0;
  running_ = false;
  running_since_.reset();
  stood_since_.reset();
  arrived_at_.reset();
  jump_.reset();
  jump_at_ = 0;
  jump_first_at_ = 0;
  landing_.reset();
  set_at_ = 0;
  set_once_ = false;
  resync_at_ = 0;
  resync_once_ = false;
  fallback_at_ = 0;
  fallback_once_ = false;
  ignore_host_until_ = 0;
  tracks_.clear();
  joined_ = false;
  sent_at_ = 0;
  sent_once_ = false;
  was_host_ = false;
  shared_.reset();
  shared_at_us_ = 0;
  shared_rate_ = 0.0;
  anchor_ = Anchor::stands;
  anchor_mate_ = 0;
  detached_ = false;
  nudges_ = keep.nudge;
  held_ = 0;
  lost_ = 0;
  in_step_ = true;
  status_ = {};
}

void Clock::our_set(std::uint32_t seconds, std::uint64_t now_us) {
  landing_ = seconds % kDay;
  set_at_ = now_us;
  set_once_ = true;
  jump_.reset();
}

std::optional<std::uint32_t> Clock::shared_at(std::uint64_t now_us) const {
  if (!shared_) return std::nullopt;
  const std::uint64_t dt_us = now_us > shared_at_us_ ? now_us - shared_at_us_ : 0;
  return wrap(static_cast<std::int64_t>(*shared_) +
              std::llround(shared_rate_ * static_cast<double>(dt_us) / 1e6));
}

void Clock::set(std::uint32_t seconds, ClockSetWhy why, const ClockInput& in, ClockOutput& out) {
  out.set = seconds;
  out.why = why;
  out.nudge = 0;
  landing_ = seconds;
  set_at_ = in.now_us;
  set_once_ = true;
  if (why == ClockSetWhy::resync) {
    resync_at_ = in.now_us;
    resync_once_ = true;
  }
  if (why == ClockSetWhy::no_nudge) {
    fallback_at_ = in.now_us;
    fallback_once_ = true;
  }
  jump_.reset();
  out.log.push_back("set to " + clock_text(seconds) + " (" + clock_set_why(why) + ")");
}

void Clock::note(bool in_step, std::int32_t off, ClockOutput& out) {
  if (in_step == in_step_) return;
  in_step_ = in_step;
  if (in_step) {
    out.log.push_back("in step with the shared clock");
  } else {
    out.log.push_back("catching up: the shared clock is " + signed_seconds(off) + " from ours" +
                      (running_ ? "" : " (ours stands: we catch up outdoors)"));
  }
}

void Clock::anchor(Anchor a, std::uint8_t mate, ClockOutput& out) {
  if (a == anchor_ && (a != Anchor::mate || mate == anchor_mate_)) return;
  anchor_ = a;
  anchor_mate_ = a == Anchor::mate ? mate : 0;
  switch (a) {
    case Anchor::ours: out.log.push_back("the shared clock is ours"); break;
    case Anchor::mate:
      out.log.push_back("the shared clock is " + player(mate) + "'s (ours stands or follows)");
      break;
    case Anchor::runs_on: out.log.push_back("the shared clock runs on (no mate in step)"); break;
    case Anchor::stands: out.log.push_back("the shared clock stands (nobody outdoors)"); break;
  }
}

// Our clock: whether it runs, its own rate, our jumps, our sets landing.
void Clock::read_ours(const ClockInput& in, ClockOutput& out) {
  const std::uint64_t now = in.now_us;
  if (!in.game) {  // a load, the title: the first read after it is no jump
    last_.reset();
    running_ = false;
    running_since_.reset();
    if (!stood_since_) stood_since_ = now;
    jump_.reset();
    return;
  }
  const std::uint32_t game = *in.game;
  if (!last_ || now <= last_at_) {
    if (!last_) {
      running_ = false;
      running_since_.reset();
      if (!stood_since_) stood_since_ = now;
    }
    last_ = game;
    last_at_ = now;
    dt_ = 0.0;
    return;
  }
  const double dt = static_cast<double>(now - last_at_) / 1e6;
  dt_ = std::min(dt, 1.0);
  const std::int32_t natural = clock_ahead(*last_, game) - in.nudged;
  last_ = game;
  last_at_ = now;

  // Our set: until it lands its reads are no jumps; then the clock starts again.
  if (landing_) {
    const bool landed = magnitude(clock_ahead(*landing_, game)) <= s_.jump_s;
    if (!landed && now - set_at_ < s_.set_lands_us) {
      running_ = false;
      running_since_.reset();
      return;
    }
    landing_.reset();
    running_ = false;
    running_since_.reset();
    stood_since_ = now;  // the time change stops the clock some seconds
    return;
  }

  const double rate = rate_ > 0.0 ? rate_ : s_.default_rate;
  const bool big = natural < -static_cast<std::int32_t>(s_.jump_s) ||
                   natural > rate * dt * 3.0 + s_.jump_s;
  if (big) {  // the game moved its clock: maybe a time-lapse, so wait for it to settle
    if (!jump_) jump_first_at_ = now;
    jump_ = game;
    jump_at_ = now;
    running_ = false;
    running_since_.reset();
    return;
  }
  if (jump_) {
    jump_ = game;
    if (now - jump_at_ >= s_.jump_settle_us || now - jump_first_at_ >= s_.jump_give_up_us) {
      proto::TimeOfDay t;
      t.seconds = game;
      t.flags = proto::time_of_day::jump;
      out.send.push_back(t);
      sent_at_ = now;
      sent_once_ = true;
      out.log.push_back("sent a jump to " + clock_text(game) +
                        " (the Sun's Song, a sleep or an event)");
      jump_.reset();
      tracks_.clear();  // every word we hold is from before the jump
      if (in.host) {
        shared_ = game;
        shared_at_us_ = now;
        shared_rate_ = 0.0;
        anchor(Anchor::ours, 0, out);
        detached_ = false;
      } else {
        ignore_host_until_ = now + s_.ignore_host_after_jump_us;
      }
    } else {
      return;  // still settling
    }
  }

  if (natural > 0) {
    if (!running_since_) {
      running_since_ = now;
      if (stood_since_ && now - *stood_since_ >= s_.stood_for_us) arrived_at_ = now;
    }
    stood_since_.reset();
    running_ = true;
    const double r = std::clamp(natural / dt, 1.0, 10'000.0);
    rate_ = rate_ > 0.0 ? rate_ * 0.8 + r * 0.2 : r;
  } else {
    running_ = false;
    running_since_.reset();
    if (!stood_since_) stood_since_ = now;
  }
}

// The others' words: jumps at once, the rest into the tracks; joining.
void Clock::take_words(const ClockInput& in, ClockOutput& out, std::vector<Inbox>& inbox) {
  const std::uint64_t now = in.now_us;
  for (const auto& w : inbox) {
    const auto& m = w.message;
    if (in.our_number != 0 && m.player_number == in.our_number) continue;
    if ((m.flags & proto::time_of_day::jump) != 0) {
      tracks_.clear();  // every word from before it is old
      jump_.reset();    // theirs is the newer
      out.log.push_back("jump to " + clock_text(m.seconds) + " from " + player(m.player_number));
      if (in.host) {
        shared_ = m.seconds;
        shared_at_us_ = now;
        shared_rate_ = 0.0;
        anchor(Anchor::ours, 0, out);
        detached_ = false;
      } else {
        ignore_host_until_ = now + s_.ignore_host_after_jump_us;
      }
      if (in.game && magnitude(clock_ahead(*in.game, m.seconds)) > s_.join_set_s) {
        set(m.seconds, ClockSetWhy::jump, in, out);
      }
      continue;
    }
    tracks_[m.player_number].add(m.seconds, w.at_us, s_);
    if (!in.host && in.host_number != 0 && m.player_number == in.host_number && !joined_ &&
        in.game && now >= ignore_host_until_) {
      joined_ = true;
      out.log.push_back("joined: the host's clock is " + clock_text(m.seconds) + ", ours " +
                        clock_text(*in.game));
      if (magnitude(clock_ahead(*in.game, m.seconds)) > s_.join_set_s && !landing_) {
        set(m.seconds, ClockSetWhy::join, in, out);
      }
    }
  }
}

// Host: what the shared clock is now, and whether our own clock follows it.
void Clock::host_shared(const ClockInput& in, ClockOutput& out) {
  const std::uint64_t now = in.now_us;
  if (!was_host_) {
    was_host_ = true;
    detached_ = false;
    // A new host starts from its own clock (it followed the old host), else the old host's words.
    std::optional<std::uint32_t> start = in.game;
    if (!start && in.host_number != 0) {
      if (auto it = tracks_.find(in.host_number); it != tracks_.end()) start = it->second.at(now, s_);
    }
    shared_ = start;
    shared_at_us_ = now;
    shared_rate_ = in.game && running_ ? rate_ : 0.0;
    out.log.push_back("we host: the shared clock starts at " +
                      (start ? clock_text(*start) : std::string("(no clock yet)")));
  }
  const auto s_now = shared_at(now);
  if (jump_) {  // our clock is jumping (a time-lapse?): the shared clock runs on until it settles
    shared_ = s_now;
    shared_at_us_ = now;
    return;
  }
  // While our own set lands our clock does not run (running_ is false), so the rules below keep
  // the shared clock on a mate's or standing: after a jump's set it stands at the jump (the
  // tracks were cleared), after an arrival's it stays the mate's we follow.
  // A mate who is outdoors and in step with the shared clock can carry it.
  std::optional<std::uint8_t> mate;
  bool mate_runs = false;
  for (const auto& [number, track] : tracks_) {
    if (number == in.our_number || !track.running(now, s_)) continue;
    mate_runs = true;
    const auto theirs = track.at(now, s_);
    const bool fits = !s_now || anchor_ == Anchor::stands ||
                      (theirs && magnitude(clock_ahead(*s_now, *theirs)) <= 3 * s_.deadband_s);
    if (fits && !mate) mate = number;  // the map is in number order: the lowest
  }
  const bool ours_runs = in.game && running_;
  const bool ours_in_step =
      ours_runs && s_now && magnitude(clock_ahead(*in.game, *s_now)) <= s_.deadband_s;
  const auto from_mate = [&](std::uint8_t number) {
    const Track& t = tracks_.at(number);
    shared_ = t.at(now, s_);
    shared_rate_ = t.rate;
    shared_at_us_ = now;
    anchor(Anchor::mate, number, out);
  };
  if (ours_runs && (anchor_ == Anchor::ours || ours_in_step || !s_now || !mate_runs)) {
    if (detached_) out.log.push_back("our clock caught up with the shared one");
    detached_ = false;
    shared_ = in.game;
    shared_at_us_ = now;
    shared_rate_ = rate_;
    anchor(Anchor::ours, 0, out);
    return;
  }
  if (ours_runs) {
    detached_ = true;  // we were indoors while a mate kept the clock running: follow it
  } else {
    detached_ = false;
  }
  if (mate) {
    from_mate(*mate);
  } else if (mate_runs) {
    shared_ = s_now;
    shared_at_us_ = now;
    anchor(Anchor::runs_on, 0, out);
  } else {
    shared_ = s_now ? s_now : in.game;
    shared_at_us_ = now;
    shared_rate_ = 0.0;
    anchor(Anchor::stands, 0, out);
  }
}

// A follower moves its clock to `target`: nudges, and sets only where they hide.
void Clock::follow(const ClockInput& in, std::optional<std::uint32_t> target, ClockOutput& out) {
  if (in.host && !detached_) {
    note(true, 0, out);
    return;
  }
  if (!in.game || !target || landing_ || jump_ || out.set) return;
  if (!in.host && !joined_) return;
  const std::uint64_t now = in.now_us;
  const std::int32_t e = clock_ahead(*in.game, *target);
  const std::uint32_t off = magnitude(e);
  // Hysteresis: once out of step, catch up to a quarter of the dead band, so the clock does not
  // hover at its edge (the words come a little late: the target we see trails the host's).
  if (off <= (in_step_ ? s_.deadband_s : s_.deadband_s / 4)) {
    note(true, e, out);
    return;
  }
  note(false, e, out);
  if (!running_) return;  // indoors: our clock stands; we catch up when we come out
  const bool gap_ok = !set_once_ || now - set_at_ >= s_.set_gap_us;
  // Nudges that do not hold (tick turned them off): the setter stands in for them where it
  // hides best, and the log line says so (docs/HORA.md, "nudges do not hold").
  const bool fallback = s_.nudge && !nudges_;
  const bool arriving = arrived_at_ && now - *arrived_at_ <= s_.arrival_window_us && gap_ok;
  if (arriving && off > s_.arrival_set_s) {
    set(*target, ClockSetWhy::arrival, in, out);
    return;
  }
  if (arriving && fallback && off > s_.deadband_s) {
    set(*target, ClockSetWhy::no_nudge_arrival, in, out);
    return;
  }
  const bool ran = running_since_ && now - *running_since_ >= s_.stood_for_us;
  if (fallback && off > s_.fallback_set_s && off <= s_.resync_set_s && ran && gap_ok &&
      (!fallback_once_ || now - fallback_at_ >= s_.set_every_us)) {
    set(*target, ClockSetWhy::no_nudge, in, out);
    return;
  }
  if (off > s_.resync_set_s && ran && gap_ok &&
      (!resync_once_ || now - resync_at_ >= s_.set_every_us)) {
    set(*target, ClockSetWhy::resync, in, out);
    return;
  }
  if (!s_.nudge || !nudges_ || dt_ <= 0.0) return;
  const double rate = rate_ > 0.0 ? rate_ : s_.default_rate;
  const double cap = s_.max_skew * rate * dt_;
  const double want = e * std::min(1.0, s_.gain_per_s * dt_);
  std::int64_t n = std::llround(std::clamp(want, -cap, cap));
  // Never across midnight: the game's day count stays its own.
  const std::int64_t game = *in.game;
  if (game + n >= kDay) n = static_cast<std::int64_t>(kDay) - 1 - game;
  if (game + n < 0) n = -game;
  out.nudge = static_cast<std::int32_t>(n);
}

ClockOutput Clock::tick(const ClockInput& in) {
  ClockOutput out;
  if (!in.connected) {
    if (was_host_ || joined_ || last_ || !tracks_.empty()) reset();
    return out;
  }
  std::vector<Inbox> inbox;
  {
    std::lock_guard lock(inbox_mutex_);
    inbox.swap(inbox_);
  }
  const std::uint64_t now = in.now_us;

  held_ += std::max(0, in.nudges_held);
  lost_ += std::max(0, in.nudges_lost);
  if (nudges_ && held_ + lost_ >= s_.nudge_checks && lost_ > held_) {
    nudges_ = false;
    out.log.push_back("nudges do not hold (" + std::to_string(held_) + " held, " +
                      std::to_string(lost_) + " put back): setting instead, at a scene load past " +
                      std::to_string(s_.deadband_s) + " s off, while playing past " +
                      std::to_string(s_.fallback_set_s) +
                      " s off (at most once a minute), and past an hour as upstream");
  }

  read_ours(in, out);
  take_words(in, out, inbox);
  if (in.host) {
    host_shared(in, out);
  } else {
    if (was_host_) {
      out.log.push_back("we no longer host: following " + player(in.host_number));
      joined_ = true;  // we were in the session already: no join set
    }
    was_host_ = false;
    detached_ = false;
    shared_.reset();
    anchor_ = Anchor::stands;
    anchor_mate_ = 0;
  }

  std::optional<std::uint32_t> target;
  if (in.host) {
    if (detached_) target = shared_at(now);
  } else if (in.host_number != 0 && now >= ignore_host_until_) {
    if (auto it = tracks_.find(in.host_number); it != tracks_.end()) target = it->second.at(now, s_);
  }
  follow(in, target, out);

  // The host says the shared clock; the others report their own.
  if (!sent_once_ || now - sent_at_ >= s_.send_every_us) {
    std::optional<std::uint32_t> word;
    if (in.host) {
      word = shared_at(now);
    } else if (in.game && !landing_ && !jump_ && !out.set) {
      word = *in.game;
    }
    if (word) {
      proto::TimeOfDay t;
      t.seconds = *word;
      out.send.push_back(t);
      sent_at_ = now;
      sent_once_ = true;
    }
  }

  status_ = {};
  status_.shared = in.host ? shared_at(now) : target;
  if (status_.shared && in.game) status_.off = clock_ahead(*in.game, *status_.shared);
  switch (anchor_) {
    case Anchor::ours: status_.anchor = in.our_number; break;
    case Anchor::mate: status_.anchor = anchor_mate_; break;
    case Anchor::runs_on: status_.anchor = kClockRunsOn; break;
    case Anchor::stands: status_.anchor = kClockStands; break;
  }
  status_.running = running_;
  status_.in_step = in_step_;
  status_.nudges = nudges_ && s_.nudge;
  status_.rate = rate_;
  return out;
}

}  // namespace coop::host
