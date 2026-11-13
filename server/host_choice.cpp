// ORM one host for the room (host_choice.hpp, oot-remake docs/PARTIDA_SALA.md).

#include "host_choice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace coop::server {

using protocol::HostWhy;
using protocol::HostWish;

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool same_name(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

std::string ms_text(std::optional<double> ms) {
  if (!ms) return "not measured";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.0f ms", *ms);
  return buf;
}

// Lower is better: no score sorts last.
double score_key(std::optional<double> s) { return s ? *s : std::numeric_limits<double>::infinity(); }

}  // namespace

// ------------------------------------------------------------------ LinkMeasure

void LinkMeasure::ping_sent(std::uint64_t at_us) {
  if (at_us == 0) return;  // 0 marks a free slot
  pings_[next_] = at_us;
  next_ = (next_ + 1) % kPings;
}

bool LinkMeasure::pong(std::uint64_t echo_us, std::uint64_t now_us, std::uint64_t max_gap_us) {
  if (echo_us == 0 || echo_us > now_us) return false;
  std::uint64_t* slot = nullptr;
  for (std::uint64_t& p : pings_) {
    if (p == echo_us) slot = &p;
  }
  if (slot == nullptr) return false;
  *slot = 0;  // the same PONG again is not counted twice
  const double rtt = static_cast<double>(now_us - echo_us) / 1000.0;
  if (samples_ == 0) {
    rtt_ms_ = rtt;
    jitter_ms_ = 0.0;
  } else {
    // RFC 6298's smoothing (1/8 for the average, 1/4 for the deviation).
    jitter_ms_ += (std::fabs(rtt - rtt_ms_) - jitter_ms_) / 4.0;
    rtt_ms_ += (rtt - rtt_ms_) / 8.0;
  }
  if (last_pong_us_ != 0 && now_us > last_pong_us_ && now_us - last_pong_us_ > max_gap_us) {
    long_gap_at_us_ = now_us;
  }
  last_pong_us_ = now_us;
  if (samples_ < std::numeric_limits<std::uint32_t>::max()) ++samples_;
  return true;
}

std::optional<double> LinkMeasure::rtt_ms() const {
  if (samples_ == 0) return std::nullopt;
  return rtt_ms_;
}

bool LinkMeasure::steady(const HostRules& rules, std::uint64_t joined_us, std::uint64_t now_us) const {
  if (samples_ < rules.min_samples || last_pong_us_ == 0) return false;
  if (now_us < joined_us || now_us - joined_us < rules.warmup_us) return false;
  if (jitter_ms_ > rules.max_jitter_ms) return false;
  if (now_us >= last_pong_us_ && now_us - last_pong_us_ > rules.max_gap_us) return false;  // silent now
  if (long_gap_at_us_ != 0 && now_us >= long_gap_at_us_ && now_us - long_gap_at_us_ < rules.stable_window_us) {
    return false;
  }
  return true;
}

// ----------------------------------------------------------------- HostElection

std::optional<double> HostElection::score(const HostCandidate& c, std::span<const HostCandidate> all) {
  if (!c.rtt_ms) return std::nullopt;
  double others = 0.0;
  std::size_t n = 0;
  for (const HostCandidate& o : all) {
    if (o.number == c.number) continue;
    if (!o.rtt_ms) return std::nullopt;
    others += *o.rtt_ms;
    ++n;
  }
  if (n == 0) return *c.rtt_ms;
  return *c.rtt_ms + others / static_cast<double>(n);
}

int HostElection::rank(HostWish wish) {
  switch (wish) {
    case HostWish::me: return 0;
    case HostWish::never: return 2;
    case HostWish::automatic: break;
  }
  return 1;
}

const HostCandidate* HostElection::find(std::uint8_t number, std::span<const HostCandidate> all) const {
  for (const HostCandidate& c : all) {
    if (c.number == number) return &c;
  }
  return nullptr;
}

const HostCandidate* HostElection::pinned_player(std::span<const HostCandidate> all) const {
  if (pinned_.empty()) return nullptr;
  for (const HostCandidate& c : all) {
    if (same_name(c.nickname, pinned_)) return &c;
  }
  return nullptr;
}

const HostCandidate* HostElection::best_fit(std::span<const HostCandidate> all, std::uint8_t except,
                                            int max_rank) const {
  const HostCandidate* best = nullptr;
  for (const HostCandidate& c : all) {
    if (c.number == except || !c.in_world || !c.steady || rank(c.wish) > max_rank) continue;
    if (best == nullptr) {
      best = &c;
      continue;
    }
    const int rc = rank(c.wish);
    const int rb = rank(best->wish);
    if (rc != rb) {
      if (rc < rb) best = &c;
      continue;
    }
    const double sc = score_key(score(c, all));
    const double sb = score_key(score(*best, all));
    if (sc < sb || (sc == sb && c.number < best->number)) best = &c;
  }
  return best;
}

HostMove HostElection::move_to(const HostCandidate& c, HostWhy why, std::span<const HostCandidate> all,
                               std::string note) const {
  HostMove m;
  m.to = c.number;
  m.why = why;
  m.by = why == HostWhy::picked ? pinned_by_ : 0;
  m.score_ms = score(c, all);
  m.note = std::move(note);
  return m;
}

HostElection::PickResult HostElection::pick(std::string_view name, std::uint8_t by,
                                            std::span<const HostCandidate> all) {
  while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
  if (name.empty()) {
    pinned_.clear();
    pinned_by_ = 0;
    better_since_ = 0;
    unfit_since_ = 0;
    return PickResult::cleared;
  }
  for (const HostCandidate& c : all) {
    if (!same_name(c.nickname, name)) continue;
    if (c.wish == HostWish::never) return PickResult::never;
    pinned_ = c.nickname;
    pinned_by_ = by;
    return PickResult::picked;
  }
  return PickResult::not_here;
}

std::optional<HostMove> HostElection::check(std::uint8_t host, std::span<const HostCandidate> all,
                                            std::uint64_t now_us) {
  const HostCandidate* h = find(host, all);
  if (h == nullptr) return std::nullopt;

  // By hand: the picked player hosts once its game is in the world.
  if (const HostCandidate* p = pinned_player(all)) {
    better_since_ = 0;
    unfit_since_ = 0;
    if (p->number == host || !p->in_world) return std::nullopt;
    return move_to(*p, HostWhy::picked, all, "picked by hand (" + pinned_ + ")");
  }

  // Wishes: a fit player of a better rank takes over at once.
  const int host_rank = rank(h->wish);
  if (host_rank > 0) {
    if (const HostCandidate* w = best_fit(all, host, host_rank - 1)) {
      better_since_ = 0;
      unfit_since_ = 0;
      return move_to(*w, HostWhy::wished, all,
                     w->wish == HostWish::me ? w->nickname + " asked to host"
                                             : h->nickname + " asked never to host");
    }
  }

  if (!rules_.automatic) {
    better_since_ = 0;
    unfit_since_ = 0;
    return std::nullopt;
  }

  // An unfit host, for unfit_hold_us in a row.
  if (!h->in_world || !h->steady) {
    better_since_ = 0;
    if (unfit_since_ == 0) unfit_since_ = now_us == 0 ? 1 : now_us;
    if (now_us - unfit_since_ < rules_.unfit_hold_us) return std::nullopt;
    // Any fit player who does not wish never to host (a host who wished to
    // host but cannot is replaced too: someone in the world hosts better).
    const HostCandidate* b = best_fit(all, host, 1);
    if (b == nullptr) return std::nullopt;
    return move_to(*b, HostWhy::unfit, all,
                   h->nickname + (h->in_world ? "'s link is unsteady" : "'s game is out of the world") +
                       " for " + std::to_string(rules_.unfit_hold_us / 1'000'000) + " s");
  }
  unfit_since_ = 0;

  // Measured, with hysteresis: not within the cooldown of the last change.
  if (changed_at_ != 0 && now_us >= changed_at_ && now_us - changed_at_ < rules_.cooldown_us) {
    better_since_ = 0;
    return std::nullopt;
  }
  const std::optional<double> host_score = score(*h, all);
  const HostCandidate* b = best_fit(all, host, host_rank);
  const std::optional<double> best_score = b != nullptr ? score(*b, all) : std::nullopt;
  const double need = host_score ? std::max(rules_.better_ms, rules_.better_fraction * *host_score) : 0.0;
  if (!host_score || !best_score || *host_score - *best_score < need) {
    better_since_ = 0;
    return std::nullopt;
  }
  if (better_since_ == 0) {
    better_since_ = now_us == 0 ? 1 : now_us;
    better_who_ = b->number;
  }
  if (now_us - better_since_ < rules_.better_hold_us) return std::nullopt;
  return move_to(*b, HostWhy::measured, all,
                 b->nickname + " averages " + ms_text(best_score) + " to the others, the host " +
                     h->nickname + " " + ms_text(host_score) + ", for " +
                     std::to_string(rules_.better_hold_us / 1'000'000) + " s");
}

HostMove HostElection::after_leave(std::span<const HostCandidate> remaining) const {
  if (const HostCandidate* p = pinned_player(remaining)) {
    return move_to(*p, HostWhy::picked, remaining, "the host left; picked by hand (" + pinned_ + ")");
  }
  const HostCandidate* best = nullptr;
  for (const HostCandidate& c : remaining) {
    if (best == nullptr) {
      best = &c;
      continue;
    }
    const int rc = rank(c.wish);
    const int rb = rank(best->wish);
    if (rc != rb) {
      if (rc < rb) best = &c;
      continue;
    }
    const bool fc = c.in_world && c.steady;
    const bool fb = best->in_world && best->steady;
    if (fc != fb) {
      if (fc) best = &c;
      continue;
    }
    const double sc = score_key(score(c, remaining));
    const double sb = score_key(score(*best, remaining));
    if (sc < sb || (sc == sb && c.number < best->number)) best = &c;
  }
  if (best == nullptr) return HostMove{0, HostWhy::left, 0, std::nullopt, "nobody left"};
  return move_to(*best, HostWhy::left, remaining, "the host left; the best of the others");
}

void HostElection::moved(std::uint64_t now_us) {
  changed_at_ = now_us == 0 ? 1 : now_us;
  better_since_ = 0;
  better_who_ = 0;
  unfit_since_ = 0;
}

void HostElection::clear() {
  pinned_.clear();
  pinned_by_ = 0;
  changed_at_ = 0;
  better_since_ = 0;
  better_who_ = 0;
  unfit_since_ = 0;
}

const char* to_string(HostWhy why) {
  switch (why) {
    case HostWhy::first: return "first in";
    case HostWhy::left: return "the host left";
    case HostWhy::measured: return "a better link";
    case HostWhy::unfit: return "the host could not host";
    case HostWhy::picked: return "picked by hand";
    case HostWhy::wished: return "a wish";
    case HostWhy::refused: return "refused";
  }
  return "unknown";
}

}  // namespace coop::server
