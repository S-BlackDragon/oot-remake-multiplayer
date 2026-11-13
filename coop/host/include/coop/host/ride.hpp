#pragma once

// ORM: moving floors that carry a partner (oot-remake docs/TEMPLOS.md,
// "Plataformas"). Every game runs its own lifts, rising, sinking and floating
// floors, and the Shadow Temple's boat (docs/sync-coverage.md: "each rides
// their own"); a shared switch starts them in every game, but never in the
// same frame, and a one-way ride (the Forest Temple's elevator) only moves in
// the game of whoever rides it. A puppet is drawn where its player's samples
// say, so a partner standing on a lift that runs a little ahead in his game
// floated above ours or sank through it, and slid off it when its lift turned.
//
// The rule (ZMP D-144/D-198, "the platform carries whoever stands on it"):
//   - The sender works out, from its own actor walk, whether a moving floor is
//     carrying its Link (RideDetector) and puts that floor's placement hash and
//     its Link's place on it in its PLAYER_SAMPLE (protocol::Ride).
//   - A receiver whose game has the same floor loaded draws the puppet on ITS
//     floor at that place (RideFollower), as long as that is within kReach of
//     where the samples put it: a floor that is far from the partner's (a
//     one-way ride the other game never took) is not followed, the puppet
//     stays honest. The step fades in and out, it never jumps.
//   - Presentation only: nothing in the game changes, the sample's own
//     position is sent and kept as it is.
//
// Only floors that translate are listed: the rotating floors (Gerudo Training
// Ground, Shadow Temple) and the Forest Temple's twisted corridor are left out
// until the actor's matrix layout is seen in a run; a puppet on them is drawn
// on its samples, as before.
//
// Doc: oot-remake docs/TEMPLOS.md. Switch: LICHII_COOP_RIDE (on; 0 turns it
// off), lichii/hook/orm_ride.h. Test: tests/host/ride_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

// True for an actor that carries a Link standing on it and moves (by name,
// from the game's Pack/Actor list, 2026-11-11): the lifts (`*ElevatorLift*`,
// the Forest Temple's `Obj_ForestTemple_Elevator_A_01`), the moving, rising
// and sinking floors (`Obj_*_MoveFloor_*`, `Obj_FireTemple_RiseFloor_*`,
// `*_SinkFloor_*`), the Water Temple's floating floors
// (`Obj_WaterTemple_FloatFloor_*`, not their manager), the Shadow Temple's
// floor and boat, and Ganon's Castle's extending platform. Not their air
// walls, managers, attention points or picture-book entries.
bool is_ride_platform(std::string_view actor);

// Where a Link standing on a floor is, seen from the floor's origin, and how
// far a receiver follows it. The defaults are a lift's or a moving floor's (a
// few metres across, the origin under the feet: RideSettings has the same).
struct RideShape {
  float footprint = 4.0f;   // sideways from the origin
  float min_height = -1.0f;  // above the origin
  float max_height = 3.0f;
  float reach = 3.0f;  // RideFollower: our floor followed within this of the samples
  bool operator==(const RideShape&) const = default;
};

// The floors that are not lift-sized (nothing: RideSettings' numbers, and
// RideFollower::kReach). The Shadow Temple's boat (Obj_ShadowTemple_PaddleShip_A_01,
// its Banc origin at 136.6 39.3 -62.2, its attention point 5.6 m aside and
// 6.9 m up, StartPos_SageOfShadow_BattleOnShip 14.5 m along and 6.8 m up): a
// deck up to 16 m from the origin and 2 to 9 m above it (a ride still starts
// only once our Link moves with the boat, so the dock beside it never does),
// and a reach of 6 m, as each game's boat sets off when its own player plays
// the lullaby (oot-64 D-153: each player rides his own), a second or two
// apart. Numbers from the Banc, to be measured in a run (the "we ride" log
// line has the offset).
std::optional<RideShape> ride_shape(std::string_view actor);

// One moving floor of an actor walk.
struct RidePlatform {
  std::uint64_t key = 0;     // its placement hash: the same in every game
  protocol::Vec3 position{};  // the actor's place, in game units (metres)
  std::string name;           // the actor, for the log
  bool operator==(const RidePlatform&) const = default;
};

struct RideSettings {
  // Our Link within this, sideways, of the floor's origin can be on it.
  float footprint = 4.0f;
  // Our Link's height above the floor's origin while on it.
  float min_height = -1.0f;
  float max_height = 3.0f;
  // The floor moved at least this between two walks: it is moving.
  float min_move = 0.01f;
  // Our Link moved with it: the two moves differ by less than this plus
  // `tolerance_share` of the floor's move (the walk reads them a moment apart).
  float tolerance = 0.02f;
  float tolerance_share = 0.15f;
  // A floor that jumps further than this between two walks was reloaded or
  // placed again, not moved: it starts nothing.
  float max_step = 1.5f;
  // On a floor, our height on it may change by this much (a step, a crouch)
  // before the ride ends (a jump, a fall, a step off).
  float height_slack = 0.35f;
  // Walks in a row our Link must move with the same floor before the ride
  // starts (a jump beside a lift does not).
  unsigned start_walks = 2;
  // Two walks further apart than this start over (a load, a pause).
  std::uint64_t max_gap_us = 250'000;
};

// Our side, once per actor walk (about 30 a second): is our Link carried by
// one of the moving floors loaded here, and where does it stand on it?
class RideDetector {
 public:
  explicit RideDetector(RideSettings settings = {}) : settings_(settings) {}

  // `link`: our Link's place in this walk (nothing: not read, which ends a
  // ride). Returns the ride for our next PLAYER_SAMPLE, or nothing.
  const std::optional<protocol::Ride>& update(const std::optional<protocol::Vec3>& link,
                                              const std::vector<RidePlatform>& platforms,
                                              std::uint64_t now_us);
  const std::optional<protocol::Ride>& ride() const { return ride_; }
  // The floor we ride, by name ("" none): for the log.
  const std::string& ride_name() const { return ride_name_; }
  // A scene change, a reconnect: forget the last walk.
  void reset();

 private:
  RideSettings settings_;
  std::optional<protocol::Vec3> link_;  // the last walk's
  std::vector<RidePlatform> platforms_;  // the last walk's
  std::uint64_t at_us_ = 0;              // the last walk; 0 none
  std::uint64_t candidate_ = 0;          // the floor our Link moved with, walks in a row
  unsigned candidate_walks_ = 0;
  std::optional<protocol::Ride> ride_;
  std::string ride_name_;
  float ride_height_ = 0.0f;  // our height on the floor, kept up while it carries us
};

// The receiver's moving floors: where each of them is now, from the last two
// actor walks (moved on with their speed until the next walk, as the walk is
// 30 a second and the puppets are drawn every frame).
class RidePlatforms {
 public:
  // At most this long past a walk a floor is moved on with its speed.
  static constexpr std::uint64_t kMaxAheadUs = 100'000;

  // One walk of the scene. Returns the floors that move now and did not in
  // the walk before (for the log line of a lift that starts).
  std::vector<RidePlatform> walked(const std::vector<RidePlatform>& platforms, std::uint64_t now_us);
  void clear() { seen_.clear(); }

  // The floor `key` nearest `close_to` (two placements may share a hash only in
  // theory; the nearest wins), where it is at `now_us`. Nothing: not loaded.
  std::optional<protocol::Vec3> at(std::uint64_t key, const protocol::Vec3& close_to,
                                   std::uint64_t now_us) const;

 private:
  struct Seen {
    RidePlatform platform;
    protocol::Vec3 speed{};  // game units a microsecond
    bool moving = false;
    std::uint64_t at_us = 0;
  };
  std::vector<Seen> seen_;
};

// One remote player's puppet: the step to draw it with so it stands on our
// copy of the floor its player rides.
class RideFollower {
 public:
  // Our floor is followed only while it puts the puppet within this of where
  // its samples do (ZMP's lesson: never move a Link onto a thing far away).
  static constexpr float kReach = 3.0f;
  // The step fades in and out over this.
  static constexpr std::uint64_t kFadeUs = 200'000;

  // Once a frame. `theirs`: the ride of the player's newest sample (nothing:
  // not riding, or no sample for a while). `ours`: our floor with that key,
  // where it is now (RidePlatforms::at; nothing: not loaded here). `drawn`:
  // where the puppet is drawn without the step. Returns the step to add.
  // `reach`: kReach, or the floor's RideShape::reach.
  protocol::Vec3 step(const std::optional<protocol::Ride>& theirs,
                      const std::optional<protocol::Vec3>& ours, const protocol::Vec3& drawn,
                      std::uint64_t now_us, float reach = kReach);

  // True while the step is wanted (drawn on our floor), for the log.
  bool on() const { return on_; }
  // How much of the step is drawn, 0 to 1.
  float weight() const { return weight_; }
  // A puppet made again, a scene change.
  void reset() { *this = RideFollower{}; }

 private:
  protocol::Vec3 full_{};  // the whole step: last wanted one, kept for the fade out
  float weight_ = 0.0f;
  bool on_ = false;
  std::uint64_t last_us_ = 0;
};

}  // namespace coop::host
