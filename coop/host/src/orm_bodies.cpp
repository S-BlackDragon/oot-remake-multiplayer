#include "coop/host/orm_bodies.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace coop::host::orm::bodies {

namespace {

bool finite(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

float flat_length(float x, float z) { return std::sqrt(x * x + z * z); }

std::string text(const char* format, auto... args) {
  char buffer[320];
  const int n = std::snprintf(buffer, sizeof(buffer), format, args...);
  if (n <= 0) return {};
  return std::string(buffer, static_cast<std::size_t>(std::min(n, int(sizeof(buffer)) - 1)));
}

std::string vec_text(const Vec3& v) { return text("(%.2f, %.2f, %.2f)", v[0], v[1], v[2]); }

}  // namespace

Vec3 way_of_number(std::uint8_t number) {
  const float angle = 1.0472f * float(number);  // 60 degrees a number
  return {std::cos(angle), 0.0f, std::sin(angle)};
}

Vec3 PuppetAside::step(const AsideIn& in) {
  auto [it, fresh] = states_.try_emplace(in.number);
  State& s = it->second;
  const std::uint64_t dt_ms = fresh ? 0 : in.now_ms - std::min(in.now_ms, s.last_ms);
  s.last_ms = in.now_ms;

  // The target: out to two radii from our Link, or nothing.
  Vec3 target{0, 0, 0};
  bool touching = false;
  bool on_top = false;
  float apart = 0.0f;
  if (in.shown && !in.down && !in.solid && in.ours && finite(*in.ours) && finite(in.sample) &&
      std::fabs(in.sample[1] - (*in.ours)[1]) < kBodyHeight) {
    const float lx = in.sample[0] - (*in.ours)[0], lz = in.sample[2] - (*in.ours)[2];
    apart = flat_length(lx, lz);
    if (apart < 2 * kBodyRadius) {
      touching = true;
      Vec3 way{0, 0, 0};
      if (apart >= kHoldWayM) {
        way = {lx / apart, 0, lz / apart};
        s.held_way = way;
      } else if (s.held_way) {
        way = *s.held_way;
      } else {
        on_top = apart < kFullOverlapM;
        way = on_top ? way_of_number(in.number) : Vec3{lx / apart, 0, lz / apart};
        s.held_way = way;
      }
      const float gap = 2 * kBodyRadius - apart;
      target = {way[0] * gap, 0.0f, way[2] * gap};
    } else {
      s.held_way.reset();
    }
  } else {
    s.held_way.reset();
  }

  // Toward the target at kSpeedMps; at once when nothing is seen moving (its
  // first plan, or not shown).
  if (fresh || !in.shown) {
    s.offset = target;
  } else {
    const float dx = target[0] - s.offset[0], dz = target[2] - s.offset[2];
    const float d = flat_length(dx, dz);
    const float most = kSpeedMps * float(std::min<std::uint64_t>(dt_ms, 100)) / 1000.0f;
    if (d <= most) {
      s.offset = target;
    } else {
      s.offset[0] += dx * most / d;
      s.offset[2] += dz * most / d;
    }
  }

  const float drawn = flat_length(s.offset[0], s.offset[2]);
  if (touching && !s.contact) {
    s.contact = true;
    lines_.push_back(text("bodies: P%u's puppet on our Link (%.2f m%s): drawn %.2f m aside%s, our Link untouched",
                          unsigned(in.number), apart, on_top ? ", on top: its way by its number" : "",
                          flat_length(target[0], target[2]), fresh ? " at once" : ""));
  } else if (!touching && s.contact && drawn < 1e-4f) {
    s.contact = false;
    lines_.push_back(text("bodies: P%u's puppet drawn on its sample again%s", unsigned(in.number),
                          !in.ours ? " (our Link not read)" : ""));
  }
  return s.offset;
}

bool PuppetAside::aside(std::uint8_t number) const {
  const auto it = states_.find(number);
  return it != states_.end() && flat_length(it->second.offset[0], it->second.offset[2]) > 1e-4f;
}

void PuppetAside::prune(std::uint64_t now_ms) {
  std::erase_if(states_, [&](const auto& kv) { return now_ms - std::min(now_ms, kv.second.last_ms) > kForgetMs; });
}

void PuppetAside::reset() {
  states_.clear();
  lines_.clear();
}

std::vector<std::string> PuppetAside::take_lines() {
  std::vector<std::string> out;
  out.swap(lines_);
  return out;
}

std::string ReadBack::judge(std::uint8_t number, const Vec3& drawn, const Vec3& read) {
  if (!finite(drawn) || !finite(read)) return {};
  Said& s = said_[number];
  const float x = read[0] - drawn[0], y = read[1] - drawn[1], z = read[2] - drawn[2];
  const float off = std::sqrt(x * x + y * y + z * z);
  if (off > kReadBackM) {
    if (s.moved) return {};
    s.moved = true;
    return text("check: bodies: P%u's puppet read back at %s, %.2f m from where we drew it aside %s (the game moved "
                "it)",
                unsigned(number), vec_text(read).c_str(), off, vec_text(drawn).c_str());
  }
  if (s.ok) return {};
  s.ok = true;
  return text("bodies: P%u's puppet read back where we drew it aside %s (%.3f m off)", unsigned(number),
              vec_text(drawn).c_str(), off);
}

void ReadBack::end(std::uint8_t number) { said_.erase(number); }

// ---- ORM choque -------------------------------------------------------------

Apart apart_of(const Vec3& sample, const Vec3& ours, bool adult, bool our_adult) {
  Apart a;
  a.flat = flat_length(sample[0] - ours[0], sample[2] - ours[2]);
  const float dy = sample[1] - ours[1];  // his feet over ours
  const float his_head = adult ? kAdultHead : kChildHead;
  const float our_head = our_adult ? kAdultHead : kChildHead;
  // Level unless one's feet are on (or over) the other's head.
  a.level = dy < our_head - kHeadSlack && -dy < his_head - kHeadSlack;
  return a;
}

bool BodyGate::solid(const GateIn& in) {
  State& s = states_[in.number];
  s.last_ms = in.now_ms;
  const char* why = nullptr;  // a ghost now, for this
  float jumped = 0.0f;
  if (!in.shown) {
    why = "not shown";
  } else if (in.down) {
    why = "downed";
  } else if (in.riding) {
    why = "riding";
  } else if (!finite(in.sample)) {
    why = "no place";
  } else if (!in.ours || !finite(*in.ours)) {
    why = "our Link not read";
  }
  if (in.shown && finite(in.sample)) {
    if (s.last) {
      const Vec3& l = *s.last;
      jumped = std::sqrt((in.sample[0] - l[0]) * (in.sample[0] - l[0]) + (in.sample[1] - l[1]) * (in.sample[1] - l[1]) +
                         (in.sample[2] - l[2]) * (in.sample[2] - l[2]));
      if (why == nullptr && jumped > kJumpM) why = "its samples jumped (a warp)";
    }
    s.last = in.sample;
  }
  Apart a;
  if (why == nullptr) a = apart_of(in.sample, *in.ours, in.adult, in.our_adult);
  const bool on_us = why == nullptr && a.level && a.flat < kClearM;
  const bool deep = why == nullptr && a.level && a.flat < kDeepM;
  if (s.solid) {
    if (why == nullptr && !deep) contact(s, in, a);
    if (why != nullptr || deep) {
      s.solid = false;
      s.clear_since_ms.reset();
      s.touch_since_ms.reset();
      s.stand_since_ms.reset();
      s.said_on_us = deep;
      lines_.push_back(why != nullptr
                           ? text("bodies: P%u's body a ghost: %s", unsigned(in.number), why)
                           : text("bodies: P%u's body a ghost until clear: on our Link (%.2f m)", unsigned(in.number),
                                  a.flat));
    }
    return s.solid;
  }
  if (why != nullptr || on_us) {
    s.clear_since_ms.reset();
    if (on_us && !s.said_on_us) {
      s.said_on_us = true;
      lines_.push_back(text("bodies: P%u's body a ghost until clear: on our Link (%.2f m)", unsigned(in.number), a.flat));
    }
    return false;
  }
  if (!s.clear_since_ms) s.clear_since_ms = in.now_ms;
  if (in.now_ms - std::min(in.now_ms, *s.clear_since_ms) < kClearForMs) return false;
  s.solid = true;
  s.said_on_us = false;
  lines_.push_back(text("bodies: P%u's body solid (%.2f m from our Link%s)", unsigned(in.number), a.flat,
                        a.level ? "" : ", one over the other"));
  return true;
}

void BodyGate::contact(State& s, const GateIn& in, const Apart& a) {
  const float his_r = in.adult ? kAdultRadius : kChildRadius;
  const float our_r = in.our_adult ? kAdultRadius : kChildRadius;
  const float his_head = in.adult ? kAdultHead : kChildHead;
  const float over = (*in.ours)[1] - in.sample[1];  // our feet over his
  const bool touching = a.level && a.flat < his_r + our_r + 0.1f;
  const bool standing = !a.level && a.flat < his_r && over > his_head - 0.2f && over < his_head + 0.25f;
  if (touching) {
    if (!s.touch_since_ms) s.touch_since_ms = in.now_ms;
    if (!s.said_touch && in.now_ms - std::min(in.now_ms, *s.touch_since_ms) >= kTouchForMs) {
      s.said_touch = true;
      lines_.push_back(text("bodies: our Link against P%u's body (%.2f m apart, the bodies %.2f)", unsigned(in.number),
                            a.flat, his_r + our_r));
    }
  } else {
    s.touch_since_ms.reset();
    if (a.flat > his_r + our_r + 0.5f) s.said_touch = false;  // a new contact says it again
  }
  if (standing) {
    if (!s.stand_since_ms) s.stand_since_ms = in.now_ms;
    if (!s.said_stand && in.now_ms - std::min(in.now_ms, *s.stand_since_ms) >= kStandForMs) {
      s.said_stand = true;
      lines_.push_back(text("bodies: our Link stands on P%u's head (%.2f m over his feet)", unsigned(in.number), over));
    }
  } else {
    s.stand_since_ms.reset();
    if (a.level) s.said_stand = false;
  }
}

bool BodyGate::is_solid(std::uint8_t number) const {
  const auto it = states_.find(number);
  return it != states_.end() && it->second.solid;
}

void BodyGate::prune(std::uint64_t now_ms) {
  std::erase_if(states_, [&](const auto& kv) { return now_ms - std::min(now_ms, kv.second.last_ms) > kForgetMs; });
}

void BodyGate::reset() { states_.clear(); }

std::vector<std::string> BodyGate::take_lines() {
  std::vector<std::string> out;
  out.swap(lines_);
  return out;
}

}  // namespace coop::host::orm::bodies
