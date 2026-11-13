// ORM: see coop/host/passages.hpp and oot-remake docs/PUERTAS.md.
#include "coop/host/passages.hpp"

#include <cmath>

#include "coop/host/scene.hpp"

namespace coop::host {

namespace {

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool finite(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

}  // namespace

const char* passage_text(Passage passage) {
  switch (passage) {
    case Passage::none: return "none";
    case Passage::leaving: return "leaving";
    case Passage::gone: return "gone";
    case Passage::arriving: return "arriving";
  }
  return "?";
}

Passage Passages::note(std::uint8_t number, std::optional<std::string_view> local,
                       std::optional<std::string_view> remote, const protocol::Vec3& shown,
                       std::uint64_t now_ms) {
  if (number == 0 || number >= tracks_.size()) return Passage::none;
  // Our own place: a change, or not knowing it, starts every track again.
  const bool local_known = local && !local->empty();
  const std::string_view place = local_known ? place_of(*local) : std::string_view();
  if (!local_known || !local_known_ || place != local_place_) {
    reset();
    local_known_ = local_known;
    local_place_ = std::string(place);
    if (!local_known) return Passage::none;
  }

  Track& t = tracks_[number];
  const bool same = same_place(local, remote);
  const std::uint64_t previous_ms = t.last_ms;
  t.last_ms = now_ms;
  t.why = "";
  if (!t.known) {
    t.known = true;
    t.here = same;
    if (!same && finite(shown)) t.elsewhere = shown;
    return Passage::none;
  }

  if (t.here && !same) {
    // Out of our place. Only a player planned until just now walked out in
    // front of us; one not seen for a while was gone already.
    t.here = false;
    t.arriving = false;
    t.left_ms = now_ms - previous_ms <= settings_.fresh_ms ? now_ms : 0;
  } else if (!t.here && same) {
    t.here = true;
    t.left_ms = 0;
    t.arriving = true;
    t.arrived_ms = now_ms;
    t.moved = false;
    t.moved_ms = 0;
  }

  if (!same) {
    // Where it is while elsewhere: what a first sample with our scene may
    // still carry. Kept from every note, the leaving ones too.
    if (finite(shown)) t.elsewhere = shown;
    if (t.left_ms == 0) return Passage::none;
    const std::uint64_t since = now_ms - t.left_ms;
    if (since < settings_.leave_show_ms) {
      t.why = "walking out";
      return Passage::leaving;
    }
    if (since < settings_.leave_show_ms + settings_.leave_park_ms) {
      t.why = "left the scene";
      return Passage::gone;
    }
    t.left_ms = 0;
    return Passage::none;
  }

  if (t.arriving) {
    if (!t.moved && (!t.elsewhere || !finite(shown) ||
                     distance(shown, *t.elsewhere) > settings_.moved_m)) {
      t.moved = true;
      t.moved_ms = now_ms;
    }
    if (t.moved && now_ms - t.moved_ms >= settings_.arrive_settle_ms) {
      t.arriving = false;
      t.elsewhere.reset();
      return Passage::none;
    }
    if (now_ms - t.arrived_ms >= settings_.arrive_timeout_ms) {
      t.arriving = false;
      t.elsewhere.reset();
      t.why = "arrived (waited the most)";
      return Passage::none;
    }
    t.why = "arriving";
    return Passage::arriving;
  }
  return Passage::none;
}

const char* Passages::why(std::uint8_t number) const {
  return number < tracks_.size() ? tracks_[number].why : "";
}

void Passages::forget(std::uint8_t number) {
  if (number < tracks_.size()) tracks_[number] = Track{};
}

void Passages::reset() {
  for (auto& t : tracks_) t = Track{};
}

}  // namespace coop::host
