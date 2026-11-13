#include "coop/host/ride.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace coop::host {
namespace {

using protocol::Vec3;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

float norm(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

float sideways(const Vec3& a, const Vec3& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }

bool finite(const Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// The floor `key` in `list` nearest `close_to`, or null.
template <typename T, typename Get>
const T* nearest(const std::vector<T>& list, std::uint64_t key, const Vec3& close_to, Get get) {
  const T* best = nullptr;
  float best_d = std::numeric_limits<float>::infinity();
  for (const T& item : list) {
    const RidePlatform& p = get(item);
    if (p.key != key) continue;
    const float d = norm(sub(p.position, close_to));
    if (d < best_d) {
      best_d = d;
      best = &item;
    }
  }
  return best;
}

const RidePlatform* nearest_platform(const std::vector<RidePlatform>& list, std::uint64_t key,
                                     const Vec3& close_to) {
  return nearest(list, key, close_to, [](const RidePlatform& p) -> const RidePlatform& { return p; });
}

}  // namespace

bool is_ride_platform(std::string_view actor) {
  if (!actor.starts_with("Obj_")) return false;
  constexpr std::string_view kNot[] = {"_Dictionary", "Airwall", "AirWall", "Manager", "_Attention",
                                       "LiftRock"};
  for (const std::string_view n : kNot) {
    if (actor.find(n) != std::string_view::npos) return false;
  }
  constexpr std::string_view kParts[] = {
      "ElevatorLift",  // the Deku Tree's, Jabu-Jabu's, the base one
      "_Elevator_",    // the Forest Temple's (Obj_ForestTemple_Elevator_A_01)
      "_MoveFloor_",  "_RiseFloor_",     "_SinkFloor_", "_FloatFloor_",
      "_GimmickFloor_",  // the Shadow Temple's
      "_PaddleShip_",    // the Shadow Temple's boat
      "_ExtendingPlatform_",  // Ganon's Castle
  };
  for (const std::string_view p : kParts) {
    if (actor.find(p) != std::string_view::npos) return true;
  }
  return false;
}

std::optional<RideShape> ride_shape(std::string_view actor) {
  if (actor.starts_with("Obj_ShadowTemple_PaddleShip_") && is_ride_platform(actor)) {
    return RideShape{16.0f, 2.0f, 9.0f, 6.0f};
  }
  return std::nullopt;
}

void RideDetector::reset() {
  link_.reset();
  platforms_.clear();
  at_us_ = 0;
  candidate_ = 0;
  candidate_walks_ = 0;
  ride_.reset();
  ride_name_.clear();
  ride_height_ = 0.0f;
}

const std::optional<protocol::Ride>& RideDetector::update(const std::optional<Vec3>& link,
                                                          const std::vector<RidePlatform>& platforms,
                                                          std::uint64_t now_us) {
  const bool continuous = link && link_ && finite(*link) && at_us_ != 0 && now_us > at_us_ &&
                          now_us - at_us_ <= settings_.max_gap_us;
  // How far our Link's move is from a floor's move: up and down only for a
  // floor that moves up and down (walking on a lift is still riding it).
  const auto error = [this](const Vec3& dl, const Vec3& dq) {
    if (std::hypot(dq[0], dq[2]) < settings_.min_move) return std::fabs(dl[1] - dq[1]);
    return norm(sub(dl, dq));
  };
  const auto carried = [&](const Vec3& dl, const Vec3& dq) {
    const float moved = norm(dq);
    return moved >= settings_.min_move && moved <= settings_.max_step &&
           error(dl, dq) <= settings_.tolerance + settings_.tolerance_share * moved;
  };
  // The floor's own size (the boat), else the settings' (a lift).
  const auto shape_of = [this](const RidePlatform& p) {
    if (const auto special = ride_shape(p.name)) return *special;
    return RideShape{settings_.footprint, settings_.min_height, settings_.max_height, RideFollower::kReach};
  };
  if (!continuous) {
    ride_.reset();
    ride_name_.clear();
    candidate_ = 0;
    candidate_walks_ = 0;
  } else {
    const Vec3 dl = sub(*link, *link_);
    if (ride_) {
      const RidePlatform* p = nearest_platform(platforms, ride_->platform, *link);
      const float height = p ? (*link)[1] - p->position[1] : 0.0f;
      if (p != nullptr && finite(p->position) &&
          sideways(*link, p->position) <= shape_of(*p).footprint &&
          std::fabs(height - ride_height_) <= settings_.height_slack) {
        ride_->offset = sub(*link, p->position);
        // Moved with it this walk: its height on the floor is the new anchor
        // (a jump or a fall moves our Link apart from the floor, so it never
        // moves the anchor along and ends the ride past height_slack).
        if (const RidePlatform* before = nearest_platform(platforms_, p->key, p->position);
            before != nullptr && carried(dl, sub(p->position, before->position))) {
          ride_height_ = height;
        }
      } else {
        ride_.reset();  // off it, it is gone, or our Link left its height
        ride_name_.clear();
      }
    }
    if (!ride_) {
      const RidePlatform* best = nullptr;
      float best_error = std::numeric_limits<float>::infinity();
      for (const RidePlatform& p : platforms) {
        if (p.key == 0 || !finite(p.position)) continue;
        const RideShape shape = shape_of(p);
        if (sideways(*link, p.position) > shape.footprint) continue;
        const float height = (*link)[1] - p.position[1];
        if (height < shape.min_height || height > shape.max_height) continue;
        const RidePlatform* before = nearest_platform(platforms_, p.key, p.position);
        if (before == nullptr) continue;
        const Vec3 dq = sub(p.position, before->position);
        if (!carried(dl, dq)) continue;
        const float e = error(dl, dq);
        if (e < best_error) {
          best_error = e;
          best = &p;
        }
      }
      if (best == nullptr) {
        candidate_ = 0;
        candidate_walks_ = 0;
      } else {
        if (best->key == candidate_) {
          ++candidate_walks_;
        } else {
          candidate_ = best->key;
          candidate_walks_ = 1;
        }
        if (candidate_walks_ >= std::max(1u, settings_.start_walks)) {
          ride_ = protocol::Ride{best->key, sub(*link, best->position)};
          ride_name_ = best->name;
          ride_height_ = (*link)[1] - best->position[1];
          candidate_ = 0;
          candidate_walks_ = 0;
        }
      }
    }
  }
  link_ = link;
  platforms_ = platforms;
  at_us_ = link ? now_us : 0;
  return ride_;
}

std::vector<RidePlatform> RidePlatforms::walked(const std::vector<RidePlatform>& platforms,
                                                std::uint64_t now_us) {
  constexpr std::uint64_t kMaxGapUs = 250'000;  // as RideSettings::max_gap_us
  constexpr float kMinMove = 0.01f;             // as RideSettings::min_move
  constexpr float kMaxStep = 1.5f;              // as RideSettings::max_step
  std::vector<Seen> next;
  next.reserve(platforms.size());
  std::vector<RidePlatform> started;
  for (const RidePlatform& p : platforms) {
    if (p.key == 0 || !finite(p.position)) continue;
    Seen s;
    s.platform = p;
    s.at_us = now_us;
    const Seen* before = nearest(seen_, p.key, p.position,
                                 [](const Seen& x) -> const RidePlatform& { return x.platform; });
    if (before != nullptr && now_us > before->at_us && now_us - before->at_us <= kMaxGapUs) {
      const Vec3 dq = sub(p.position, before->platform.position);
      const float moved = norm(dq);
      if (moved <= kMaxStep) {
        const float dt = static_cast<float>(now_us - before->at_us);
        s.speed = {dq[0] / dt, dq[1] / dt, dq[2] / dt};
        s.moving = moved >= kMinMove;
      }
      if (s.moving && !before->moving) started.push_back(p);
    }
    next.push_back(std::move(s));
  }
  seen_ = std::move(next);
  return started;
}

std::optional<Vec3> RidePlatforms::at(std::uint64_t key, const Vec3& close_to, std::uint64_t now_us) const {
  const Seen* s = nearest(seen_, key, close_to, [](const Seen& x) -> const RidePlatform& { return x.platform; });
  if (s == nullptr) return std::nullopt;
  const std::uint64_t ahead = now_us > s->at_us ? std::min(now_us - s->at_us, kMaxAheadUs) : 0;
  const float t = static_cast<float>(ahead);
  return Vec3{s->platform.position[0] + s->speed[0] * t, s->platform.position[1] + s->speed[1] * t,
              s->platform.position[2] + s->speed[2] * t};
}

Vec3 RideFollower::step(const std::optional<protocol::Ride>& theirs, const std::optional<Vec3>& ours,
                        const Vec3& drawn, std::uint64_t now_us, float reach) {
  const std::uint64_t dt =
      last_us_ != 0 && now_us > last_us_ ? std::min(now_us - last_us_, kFadeUs) : 0;
  last_us_ = now_us;
  bool want = false;
  if (theirs && ours && finite(theirs->offset) && finite(*ours) && finite(drawn)) {
    const Vec3 target{(*ours)[0] + theirs->offset[0], (*ours)[1] + theirs->offset[1],
                      (*ours)[2] + theirs->offset[2]};
    const Vec3 full = sub(target, drawn);
    if (norm(full) <= reach) {
      want = true;
      full_ = full;
    }
  }
  on_ = want;
  const float d = static_cast<float>(dt) / static_cast<float>(kFadeUs);
  weight_ = want ? std::min(1.0f, weight_ + d) : std::max(0.0f, weight_ - d);
  if (!want && weight_ <= 0.0f) full_ = {};
  return {full_[0] * weight_, full_[1] * weight_, full_[2] * weight_};
}

}  // namespace coop::host
