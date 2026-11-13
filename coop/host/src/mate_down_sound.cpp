// ORM "a mate is down" sound: the rule (coop/host/mate_down_sound.hpp).
// Doc: oot-remake docs/CAIDO_REVIVIR.md, "Sonido de compañero caído".
// Switch: LICHII_ORM_MATE_DOWN_SOUND.
// Test: tests/host/mate_down_sound_test.cpp.

#include "coop/host/mate_down_sound.hpp"

#include <cmath>
#include <cstdio>

namespace coop::host {
namespace {

std::string text(const char* format, auto... args) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), format, args...);
  return buf;
}

double distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    sum += d * d;
  }
  return std::sqrt(sum);
}

}  // namespace

MateDownSoundOutput MateDownSound::tick(const MateDownSoundInput& in) {
  MateDownSoundOutput out;
  std::array<bool, protocol::kMaxPlayers + 1> listed{};
  std::vector<const Mate*> fell;
  for (const Mate& m : in.mates) {
    if (m.number == 0 || m.number > protocol::kMaxPlayers) continue;
    listed[m.number] = true;
    const bool down = (m.status & protocol::status::dead) != 0;
    Seen& s = seen_[m.number];
    if (down && s == Seen::up) fell.push_back(&m);
    s = down ? Seen::down : Seen::up;
  }
  for (std::size_t n = 1; n < listed.size(); ++n) {
    if (!listed[n]) seen_[n] = Seen::unknown;  // gone: one back down is no new fall
  }
  if (fell.empty()) return out;

  if (in.game_over) {
    for (const Mate* m : fell) {
      out.log.push_back(text("P%u is down: no sound (our own game over runs)", unsigned{m->number}));
    }
    return out;
  }
  if (played_ && in.now_us - last_ < settings_.gap_us) {
    for (const Mate* m : fell) {
      out.log.push_back(text("P%u is down: no sound (one played %.1f s ago)", unsigned{m->number},
                             static_cast<double>(in.now_us - last_) / 1e6));
    }
    return out;
  }
  // The nearest of those in our scene; else the first.
  const Mate* pick = nullptr;
  double best = 0.0;
  for (const Mate* m : fell) {
    if (!in.scene || !in.position || !m->scene || *m->scene != *in.scene || !m->position) continue;
    const double d = distance(*in.position, *m->position);
    if (pick == nullptr || d < best) {
      pick = m;
      best = d;
    }
  }
  const bool in_scene = pick != nullptr;
  if (pick == nullptr) pick = fell.front();
  const bool near = in_scene && best <= settings_.near;
  out.cue = MateDownCue{pick->number, near};
  played_ = true;
  last_ = in.now_us;
  if (near) {
    out.log.push_back(text("P%u is down: the sound at his puppet (%.1f away)", unsigned{pick->number}, best));
  } else if (in_scene) {
    out.log.push_back(text("P%u is down: the sound on the interface (%.0f away, beyond %.0f)",
                           unsigned{pick->number}, best, static_cast<double>(settings_.near)));
  } else {
    out.log.push_back(text("P%u is down: the sound on the interface (%s)", unsigned{pick->number},
                           pick->scene && in.scene && *pick->scene != *in.scene ? "in another scene"
                                                                                 : "his place or ours not known"));
  }
  for (const Mate* m : fell) {
    if (m != pick) {
      out.log.push_back(text("P%u is down too: one sound for both", unsigned{m->number}));
    }
  }
  return out;
}

}  // namespace coop::host
