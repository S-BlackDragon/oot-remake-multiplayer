// ORM: puppet smoothing (PLAN_100 E7): interpolation buffer, bounded extrapolation, snap on
//   teleport, the sender's animation and gait. The rules are described in
//   coop/host/pose_smooth.hpp.
// Doc: oot-remake docs/SUAVIZADO.md.
// Switch: LICHII_ORM_SMOOTH (on by default; LICHII_ORM_SMOOTH=0 turns it off).
// Test: tests/host/pose_smooth_test.cpp.

#include "coop/host/pose_smooth.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "coop/presence/presence.hpp"

namespace coop::host {
namespace {

using protocol::Quat;
using protocol::SampleTime;
using protocol::Vec3;

bool earlier(const SampleTime& a, const SampleTime& b) {
  if (a.server_time_us != b.server_time_us) return a.server_time_us < b.server_time_us;
  return a.sequence < b.sequence;
}

double seconds(std::uint64_t us) { return static_cast<double>(us) / 1e6; }

template <std::size_t N>
bool all_finite(const std::array<float, N>& v) {
  return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

double length(const std::array<double, 3>& v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

std::array<double, 3> sub(const Vec3& a, const Vec3& b) {
  return {static_cast<double>(a[0]) - b[0], static_cast<double>(a[1]) - b[1],
          static_cast<double>(a[2]) - b[2]};
}

float distance(const Vec3& a, const Vec3& b) { return static_cast<float>(length(sub(a, b))); }

float to_float(double x) {
  constexpr double kMax = 3.0e38;
  if (std::isnan(x)) return 0.0f;
  return static_cast<float>(std::clamp(x, -kMax, kMax));
}

std::optional<Quat> unit(const Quat& q) {
  const double n = std::sqrt(static_cast<double>(q[0]) * q[0] + static_cast<double>(q[1]) * q[1] +
                             static_cast<double>(q[2]) * q[2] + static_cast<double>(q[3]) * q[3]);
  if (!std::isfinite(n) || !(n > 1e-9)) return std::nullopt;
  return Quat{to_float(q[0] / n), to_float(q[1] / n), to_float(q[2] / n), to_float(q[3] / n)};
}

// A tangent no longer than `limit` (so the curve cannot swing past the next sample).
std::array<double, 3> bounded(std::array<double, 3> t, double limit) {
  const double n = length(t);
  if (n > limit && n > 0.0) {
    for (double& c : t) c *= limit / n;
  }
  return t;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// "RunStart_L" -> run, start, l; "Move" -> move.
std::vector<std::string> words(std::string_view name) {
  std::vector<std::string> out;
  std::string cur;
  const auto flush = [&] {
    if (!cur.empty()) out.push_back(lower(cur));
    cur.clear();
  };
  for (std::size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (c == '_' || c == '-' || c == ' ' || c == '.') {
      flush();
      continue;
    }
    const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
    if (upper && !cur.empty()) {
      const char prev = name[i - 1];
      if (std::islower(static_cast<unsigned char>(prev)) != 0 ||
          std::isdigit(static_cast<unsigned char>(prev)) != 0) {
        flush();
      }
    }
    cur.push_back(c);
  }
  flush();
  return out;
}

bool any_of(const std::vector<std::string>& w, std::initializer_list<std::string_view> list) {
  for (const std::string& x : w) {
    for (const std::string_view y : list) {
      if (x == y) return true;
    }
  }
  return false;
}

}  // namespace

const char* to_string(Gait gait) {
  switch (gait) {
    case Gait::unknown: return "unknown";
    case Gait::still: return "still";
    case Gait::walk: return "walk";
    case Gait::run: return "run";
    case Gait::other: return "other";
  }
  return "?";
}

const char* to_string(SmoothHow how) {
  switch (how) {
    case SmoothHow::early: return "early";
    case SmoothHow::interpolated: return "interpolated";
    case SmoothHow::extrapolated: return "extrapolated";
    case SmoothHow::held: return "held";
  }
  return "?";
}

float horizontal_speed(const Vec3& v) {
  if (!std::isfinite(v[0]) || !std::isfinite(v[2])) return 0.0f;
  return static_cast<float>(std::hypot(static_cast<double>(v[0]), static_cast<double>(v[2])));
}

Gait gait_of(std::string_view as_command, std::optional<float> sender_speed,
             const GaitSettings& settings) {
  const std::vector<std::string> w = words(as_command);
  if (!w.empty()) {
    // The command says it: these win over any speed.
    if (any_of(w, {"run", "dash", "sprint"})) return Gait::run;
    if (any_of(w, {"walk"})) return Gait::walk;
    if (any_of(w, {"wait", "idle", "stand"})) return Gait::still;
    // A blend command (walk and run are both `Move`): the sender's speed picks.
    if (!any_of(w, {"move", "locomotion"})) return Gait::other;
  }
  if (!sender_speed || !std::isfinite(*sender_speed)) return Gait::unknown;
  if (*sender_speed < settings.still_below) return Gait::still;
  if (*sender_speed < settings.run_from) return Gait::walk;
  return Gait::run;
}

AnimStep next_anim_step(const std::optional<AnimPlayed>& played, const protocol::Animation& wanted,
                        std::optional<float> puppet_frame, bool new_sample, const AnimSlack& slack) {
  if (!played || played->name != wanted.name) return AnimStep::play;
  const float want_rate = std::isfinite(wanted.rate) ? wanted.rate : 1.0f;
  const float had_rate = std::isfinite(played->rate) ? played->rate : 1.0f;
  if (std::fabs(want_rate - had_rate) > slack.rate) return AnimStep::play;
  if (new_sample && std::isfinite(wanted.frame) &&
      (!puppet_frame || !std::isfinite(*puppet_frame) ||
       std::fabs(*puppet_frame - wanted.frame) > slack.frame)) {
    return AnimStep::set_frame;
  }
  return AnimStep::none;
}

std::optional<SmoothSample> smooth_sample_of(const protocol::PlayerSample& sample) {
  if (!sample.position) return std::nullopt;
  SmoothSample s;
  s.time = sample.time;
  s.position = *sample.position;
  s.velocity = sample.velocity;
  s.rotation = sample.rotation;
  s.scene = sample.scene;
  s.animation = sample.animation;
  return s;
}

PoseSmoother::PoseSmoother(SmoothSettings settings, GaitSettings gait)
    : settings_(settings), gait_(gait) {
  if (settings_.capacity < 2) settings_.capacity = 2;
}

void PoseSmoother::reset() {
  buffer_.clear();
  render_us_.reset();
  last_raw_.reset();
  dirty_ = false;
  offset_ = {};
  offset_from_us_ = 0;
}

PoseSmoother::Stats PoseSmoother::take_stats() {
  Stats out = stats_;
  stats_ = {};
  return out;
}

SmoothPush PoseSmoother::push(const SmoothSample& in) {
  if (!all_finite(in.position) || (in.velocity && !all_finite(*in.velocity))) {
    ++stats_.invalid;
    return SmoothPush::invalid;
  }
  SmoothSample s = in;
  if (in.rotation) {
    s.rotation = unit(*in.rotation);
    if (!s.rotation) {
      ++stats_.invalid;
      return SmoothPush::invalid;
    }
  }
  for (const Entry& e : buffer_) {
    if (e.sample.time.sequence == s.time.sequence) {
      ++stats_.duplicate;
      return SmoothPush::duplicate;
    }
  }
  // Older than what was already shown: it would rewrite a past already drawn, so it is
  // dropped, unless it is newer than every buffered sample (then it is the best there is).
  if (render_us_ && s.time.server_time_us < *render_us_ && !buffer_.empty() &&
      earlier(s.time, buffer_.back().sample.time)) {
    ++stats_.too_old;
    return SmoothPush::too_old;
  }
  if (buffer_.size() >= settings_.capacity && earlier(s.time, buffer_.front().sample.time)) {
    ++stats_.too_old;
    return SmoothPush::too_old;
  }
  if (render_us_ && s.time.server_time_us <= *render_us_) ++stats_.late;
  const auto at = std::upper_bound(buffer_.begin(), buffer_.end(), s.time,
                                   [](const SampleTime& key, const Entry& x) {
                                     return earlier(key, x.sample.time);
                                   });
  if (at != buffer_.end()) ++stats_.reordered;
  const std::uint32_t sequence = s.time.sequence;
  buffer_.insert(at, Entry{std::move(s), false, false});
  while (buffer_.size() > settings_.capacity) buffer_.pop_front();
  relink();
  ++stats_.buffered;
  dirty_ = true;
  for (const Entry& e : buffer_) {
    if (e.sample.time.sequence == sequence && e.cut) {
      ++stats_.cuts;
      return SmoothPush::cut;
    }
  }
  return SmoothPush::buffered;
}

void PoseSmoother::relink() {
  for (std::size_t i = 0; i < buffer_.size(); ++i) {
    Entry& e = buffer_[i];
    if (i == 0) {
      // Nothing before it is kept: nothing to snap from or join to.
      e.cut = false;
      e.joined = true;
      continue;
    }
    const SmoothSample& p = buffer_[i - 1].sample;
    const SmoothSample& c = e.sample;
    const std::uint64_t dt = c.time.server_time_us - p.time.server_time_us;  // ordered: >= 0
    const bool scene_change = p.scene && c.scene && *p.scene != *c.scene;
    const float d = distance(p.position, c.position);
    const bool teleport = d > settings_.snap_distance &&
                          static_cast<double>(d) > settings_.max_speed * seconds(dt);
    e.cut = scene_change || teleport;
    e.joined = !e.cut && dt <= settings_.max_gap_us;
  }
}

Vec3 PoseSmoother::reckon(std::size_t i, std::uint64_t at_us) const {
  const Entry& e = buffer_[i];
  const std::uint64_t t = e.sample.time.server_time_us;
  const std::uint64_t ahead = at_us > t ? at_us - t : 0;
  const double used = seconds(std::min(ahead, settings_.extrapolate_us));
  std::array<double, 3> v{0.0, 0.0, 0.0};
  if (e.sample.velocity) {
    v = {(*e.sample.velocity)[0], (*e.sample.velocity)[1], (*e.sample.velocity)[2]};
  } else if (i > 0 && e.joined) {
    const SmoothSample& p = buffer_[i - 1].sample;
    const std::uint64_t dt = t - p.time.server_time_us;
    if (dt > 0) {
      const auto d = sub(e.sample.position, p.position);
      for (std::size_t k = 0; k < 3; ++k) v[k] = d[k] / seconds(dt);
    }
  }
  Vec3 out{};
  for (std::size_t k = 0; k < 3; ++k) out[k] = to_float(e.sample.position[k] + v[k] * used);
  return out;
}

std::optional<PoseSmoother::Raw> PoseSmoother::evaluate(std::uint64_t render_us) const {
  if (buffer_.empty()) return std::nullopt;
  Raw r;
  const auto rotation_up_to = [&](std::size_t i) -> std::optional<Quat> {
    for (std::size_t k = i + 1; k-- > 0;) {
      if (buffer_[k].sample.rotation) return buffer_[k].sample.rotation;
    }
    return std::nullopt;
  };
  if (render_us < buffer_.front().sample.time.server_time_us) {
    r.position = buffer_.front().sample.position;
    r.rotation = buffer_.front().sample.rotation;
    r.how = SmoothHow::early;
    r.a = 0;
    return r;
  }
  std::size_t i = 0;
  while (i + 1 < buffer_.size() && buffer_[i + 1].sample.time.server_time_us <= render_us) ++i;
  r.a = i;
  const SmoothSample& a = buffer_[i].sample;
  if (i + 1 < buffer_.size() && buffer_[i + 1].joined) {
    const SmoothSample& b = buffer_[i + 1].sample;
    const std::uint64_t gap = b.time.server_time_us - a.time.server_time_us;  // > 0 here
    const double s = static_cast<double>(render_us - a.time.server_time_us) / static_cast<double>(gap);
    const auto chord = sub(b.position, a.position);
    if (settings_.hermite && a.velocity && b.velocity) {
      const double d = seconds(gap);
      // Tangents at most twice the chord: with both collinear that keeps the curve from
      // swinging past either end (a stop shown as a stop, not a step forward and back).
      const double limit = 2.0 * length(chord);
      const auto t0 = bounded({(*a.velocity)[0] * d, (*a.velocity)[1] * d, (*a.velocity)[2] * d}, limit);
      const auto t1 = bounded({(*b.velocity)[0] * d, (*b.velocity)[1] * d, (*b.velocity)[2] * d}, limit);
      const double s2 = s * s;
      const double s3 = s2 * s;
      const double h00 = 2 * s3 - 3 * s2 + 1;
      const double h10 = s3 - 2 * s2 + s;
      const double h01 = -2 * s3 + 3 * s2;
      const double h11 = s3 - s2;
      for (std::size_t k = 0; k < 3; ++k) {
        r.position[k] = to_float(h00 * a.position[k] + h10 * t0[k] + h01 * b.position[k] + h11 * t1[k]);
      }
    } else {
      for (std::size_t k = 0; k < 3; ++k) r.position[k] = to_float(a.position[k] + chord[k] * s);
    }
    const std::optional<Quat> ra = rotation_up_to(i);
    if (ra && b.rotation) {
      r.rotation = presence::slerp(*ra, *b.rotation, s);
    } else {
      r.rotation = ra ? ra : b.rotation;
    }
    r.how = SmoothHow::interpolated;
    return r;
  }
  // Past the newest, or the next is not joined (a gap, a teleport): carried on, then held.
  r.ahead_us = render_us - a.time.server_time_us;
  r.position = reckon(i, render_us);
  r.rotation = rotation_up_to(i);
  const bool moving = distance(r.position, a.position) > 0.0f;
  r.how = moving && r.ahead_us <= settings_.extrapolate_us ? SmoothHow::extrapolated : SmoothHow::held;
  return r;
}

Vec3 PoseSmoother::offset_at(std::uint64_t render_us) const {
  if (render_us < offset_from_us_) return offset_;  // not reached: the render clock never goes back
  const std::uint64_t gone = render_us - offset_from_us_;
  if (settings_.correct_us == 0 || gone >= settings_.correct_us) return {};
  const float k = static_cast<float>(1.0 - static_cast<double>(gone) / static_cast<double>(settings_.correct_us));
  return {offset_[0] * k, offset_[1] * k, offset_[2] * k};
}

std::optional<SmoothedPose> PoseSmoother::at(std::uint64_t server_now_us) {
  std::uint64_t render = server_now_us > settings_.delay_us ? server_now_us - settings_.delay_us : 0;
  bool restart = false;
  if (render_us_ && render < *render_us_) {
    if (*render_us_ - render > settings_.clock_back_us) {
      restart = true;  // a new clock: follow it
    } else {
      render = *render_us_;  // never backwards
    }
  }
  const std::optional<Raw> raw = evaluate(render);
  if (!raw) return std::nullopt;

  bool snapped = !last_raw_ || !render_us_ || restart;
  std::array<double, 3> jump{0.0, 0.0, 0.0};
  bool jumped = false;
  if (!snapped) {
    // A sample buffered since the last frame changed what that frame should have shown (it
    // came late, after an extrapolation past it): the difference is spread out.
    if (dirty_) {
      if (const auto before = evaluate(*render_us_)) {
        const auto d = sub(*last_raw_, before->position);
        if (length(d) > 1e-6) {
          for (std::size_t k = 0; k < 3; ++k) jump[k] += d[k];
          jumped = true;
        }
      }
    }
    // Samples the shown time reached since the last frame: a teleport snaps; one not joined
    // (after a gap) is a step from where its predecessor was carried to, spread out.
    if (raw->how != SmoothHow::early) {
      for (std::size_t k = 0; k <= raw->a && k < buffer_.size(); ++k) {
        const std::uint64_t t = buffer_[k].sample.time.server_time_us;
        if (t <= *render_us_ || t > render) continue;
        if (buffer_[k].cut) {
          snapped = true;
          break;
        }
        if (!buffer_[k].joined && k > 0) {
          const auto d = sub(reckon(k - 1, t), buffer_[k].sample.position);
          for (std::size_t c = 0; c < 3; ++c) jump[c] += d[c];
          jumped = true;
        }
      }
    }
  }
  if (snapped) {
    offset_ = {};
    offset_from_us_ = render;
    ++stats_.snaps;
  } else if (jumped) {
    const Vec3 now = offset_at(render);
    Vec3 next{to_float(now[0] + jump[0]), to_float(now[1] + jump[1]), to_float(now[2] + jump[2])};
    if (distance(next, Vec3{}) > settings_.snap_distance) {
      offset_ = {};
      snapped = true;
      ++stats_.snaps;
    } else {
      offset_ = next;
      ++stats_.corrections;
    }
    offset_from_us_ = render;
  }

  SmoothedPose out;
  const Vec3 off = offset_at(render);
  for (std::size_t k = 0; k < 3; ++k) out.position[k] = raw->position[k] + off[k];
  out.rotation = raw->rotation;
  out.how = raw->how;
  out.snapped = snapped;
  out.render_us = render;
  out.ahead_us = raw->ahead_us;
  const SmoothSample& a = buffer_[raw->a].sample;
  for (std::size_t k = raw->a + 1; k-- > 0;) {
    if (buffer_[k].sample.animation) {
      out.animation = buffer_[k].sample.animation;
      out.animation_sequence = buffer_[k].sample.time.sequence;
      break;
    }
  }
  if (a.velocity) out.sender_speed = horizontal_speed(*a.velocity);
  out.gait = gait_of(out.animation ? std::string_view(out.animation->name) : std::string_view{},
                     out.sender_speed, gait_);

  ++stats_.frames;
  if (raw->how == SmoothHow::extrapolated) ++stats_.extrapolated;
  if (raw->how == SmoothHow::held && raw->ahead_us > settings_.extrapolate_us &&
      distance(raw->position, a.position) > 0.0f) {
    ++stats_.held;  // a moving player whose samples ran out
  }
  stats_.max_ahead_us = std::max(stats_.max_ahead_us, raw->ahead_us);

  last_raw_ = raw->position;
  render_us_ = render;
  dirty_ = false;
  return out;
}

DelayTuner::Verdict DelayTuner::feed(const PoseSmoother::Stats& w) {
  Verdict v;
  v.delay_us = delay_us_;
  if (w.frames < settings_.min_frames || std::uint64_t{w.buffered} * 2 < w.frames) {
    return v;  // too short, or the sender was quiet: not the network's doing
  }
  const std::uint32_t late = w.extrapolated + w.held;
  v.share = static_cast<double>(late) / static_cast<double>(w.frames);
  v.change = Change::kept;
  if (v.share > settings_.up_share) {
    calm_ = 0;
    if (delay_us_ < settings_.max_us) {
      delay_us_ = std::min(settings_.max_us, delay_us_ + settings_.step_us);
      v.change = Change::up;
    }
  } else if (v.share < settings_.down_share) {
    if (++calm_ >= settings_.calm && delay_us_ > settings_.min_us) {
      calm_ = 0;
      delay_us_ = delay_us_ - std::min(delay_us_ - settings_.min_us, settings_.step_us);
      v.change = Change::down;
    }
  } else {
    calm_ = 0;
  }
  v.delay_us = delay_us_;
  return v;
}

}  // namespace coop::host
