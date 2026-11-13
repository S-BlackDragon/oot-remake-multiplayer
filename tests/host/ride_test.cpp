// ORM moving floors (coop/host/ride.hpp, oot-remake docs/TEMPLOS.md): the ride
// component on the wire, the sender's detector and the receiver's follower.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "coop/host/ride.hpp"
#include "coop/host/sampler.hpp"
#include "coop/protocol/codec.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kWalkUs = 33'333;
constexpr std::uint64_t kLift = 0x7d1f39d42478c1f6;  // a Forest Temple elevator's placement hash

std::vector<RidePlatform> lift_at(float y) { return {{kLift, {0.0f, y, -3.5f}, "Obj_ForestTemple_Elevator_A_01"}}; }

float len(const proto::Vec3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

}  // namespace

TEST_CASE("ride: the component goes over the wire and back") {
  proto::PlayerSample s;
  s.time = {1'000'000, 7};
  s.position = proto::Vec3{1.0f, 2.0f, 3.0f};
  s.ride = proto::Ride{kLift, {0.5f, 0.1f, -0.25f}};
  const auto bytes = proto::encode(proto::Message{s});
  REQUIRE(bytes.has_value());
  proto::Message back;
  REQUIRE(proto::decode(*bytes, back) == proto::Error::none);
  const auto* got = std::get_if<proto::PlayerSample>(&back);
  REQUIRE(got != nullptr);
  CHECK(*got == s);

  s.ride = proto::Ride{0, {0.0f, 0.0f, 0.0f}};  // no floor has hash 0
  CHECK_FALSE(proto::encode(proto::Message{s}).has_value());
}

TEST_CASE("ride: the names of the floors that carry a Link") {
  CHECK(is_ride_platform("Obj_ForestTemple_Elevator_A_01"));
  CHECK(is_ride_platform("Obj_InsideTheDekuTree_ElevatorLift_A_01"));
  CHECK(is_ride_platform("Obj_FireTemple_MoveFloor_A_01"));
  CHECK(is_ride_platform("Obj_FireTemple_RiseFloor_A_02"));
  CHECK(is_ride_platform("Obj_WaterTemple_FloatFloor_A_02"));
  CHECK(is_ride_platform("Obj_ShadowTemple_PaddleShip_A_01"));
  CHECK_FALSE(is_ride_platform("Obj_WaterTemple_FloatFloorManager_HookShot_A_01"));
  CHECK_FALSE(is_ride_platform("Obj_DodongoCave_MoveFloor_A_01_Airwall"));
  CHECK_FALSE(is_ride_platform("Obj_ShadowTemple_PaddleShip_A_01_Attention"));
  CHECK_FALSE(is_ride_platform("Obj_Firetemple_RollingRock_A_01"));  // the boulders roll at you
  CHECK_FALSE(is_ride_platform("Obj_ShadowTemple_RotateFloor_A_01"));  // turns: not yet
  CHECK_FALSE(is_ride_platform("Obj_LiftRock_A_01"));
}

TEST_CASE("ride: standing on a lift that goes down starts a ride after two walks, a jump ends it") {
  RideDetector d;
  std::uint64_t t = 1'000'000;
  float y = 100.0f;
  const float step = 0.13f;  // 4 m/s, as the Forest Temple's
  // Our Link stands 0.4 m from the middle, on the lift, and goes down with it.
  for (int i = 0; i < 2; ++i, t += kWalkUs, y -= step) {
    CHECK_FALSE(d.update(proto::Vec3{0.4f, y, -3.5f}, lift_at(y), t).has_value());
  }
  const auto& r = d.update(proto::Vec3{0.4f, y, -3.5f}, lift_at(y), t);
  REQUIRE(r.has_value());
  CHECK(r->platform == kLift);
  CHECK(std::fabs(r->offset[0] - 0.4f) < 1e-4f);
  CHECK(std::fabs(r->offset[1]) < 1e-4f);
  CHECK(d.ride_name() == "Obj_ForestTemple_Elevator_A_01");
  // Walking sideways on it still rides it.
  t += kWalkUs;
  y -= step;
  CHECK(d.update(proto::Vec3{0.6f, y, -3.5f}, lift_at(y), t).has_value());
  // A jump: 0.5 m above the floor is off it.
  t += kWalkUs;
  y -= step;
  CHECK_FALSE(d.update(proto::Vec3{0.6f, y + 0.5f, -3.5f}, lift_at(y), t).has_value());
}

TEST_CASE("ride: a Link beside a moving lift, or still while it moves, does not ride it") {
  RideDetector d;
  std::uint64_t t = 1'000'000;
  float y = 100.0f;
  for (int i = 0; i < 6; ++i, t += kWalkUs, y -= 0.13f) {
    CHECK_FALSE(d.update(proto::Vec3{0.4f, 100.0f, -3.5f}, lift_at(y), t).has_value());  // stays up
    CHECK_FALSE(d.update(proto::Vec3{6.0f, y, -3.5f}, lift_at(y), t + 1).has_value());   // 6 m away
  }
}

TEST_CASE("ride: a stall between two walks starts over") {
  RideDetector d;
  std::uint64_t t = 1'000'000;
  float y = 100.0f;
  for (int i = 0; i < 3; ++i, t += kWalkUs, y -= 0.13f) d.update(proto::Vec3{0.0f, y, -3.5f}, lift_at(y), t);
  REQUIRE(d.ride().has_value());
  CHECK_FALSE(d.update(proto::Vec3{0.0f, y, -3.5f}, lift_at(y), t + 400'000).has_value());
}

TEST_CASE("ride: the receiver draws the puppet on its own lift, near only, and fades") {
  RidePlatforms ours;
  ours.walked(lift_at(99.0f), 1'000'000);
  const auto started = ours.walked(lift_at(98.87f), 1'000'000 + kWalkUs);
  REQUIRE(started.size() == 1);  // our lift starts moving here
  const auto now = ours.at(kLift, {0.0f, 99.0f, -3.5f}, 1'000'000 + kWalkUs);
  REQUIRE(now.has_value());
  // Moved on with its speed, at most 0.1 s past the walk.
  const auto ahead = ours.at(kLift, {0.0f, 99.0f, -3.5f}, 1'000'000 + kWalkUs + 1'000'000);
  REQUIRE(ahead.has_value());
  CHECK(std::fabs((*now)[1] - (*ahead)[1] - 0.39f) < 0.01f);

  RideFollower f;
  const proto::Ride theirs{kLift, {0.4f, 0.0f, 0.0f}};
  // Their lift runs 0.5 m ahead of ours: the puppet is drawn on ours.
  const proto::Vec3 drawn{(*now)[0] + 0.4f, (*now)[1] - 0.5f, (*now)[2]};
  std::uint64_t t = 2'000'000;
  CHECK(len(f.step(theirs, now, drawn, t)) == 0.0f);  // the first frame: nothing yet
  proto::Vec3 s{};
  for (int i = 0; i < 20; ++i) s = f.step(theirs, now, drawn, t += 16'667);
  CHECK(f.on());
  CHECK(std::fabs(s[1] - 0.5f) < 1e-3f);
  // Far apart (a one-way ride the other game took): not followed, fades out.
  const proto::Vec3 away{drawn[0], drawn[1] - 10.0f, drawn[2]};
  for (int i = 0; i < 20; ++i) s = f.step(theirs, now, away, t += 16'667);
  CHECK_FALSE(f.on());
  CHECK(len(s) == 0.0f);
  // Our lift not loaded: nothing.
  CHECK(len(f.step(theirs, std::nullopt, drawn, t += 16'667)) == 0.0f);
}

TEST_CASE("ride: the sampler puts the ride in its samples and sends a sample when it starts") {
  Sampler sampler;
  PlayerReading r;
  r.position = {0.0f, 100.0f, -3.5f};
  r.scene = "ForestTemple";
  r.age = proto::Age::adult;
  std::uint64_t t = 5'000'000;
  sampler.frame(r, t);  // first sample, no ride
  sampler.set_ride(proto::Ride{kLift, {0.0f, 0.0f, 0.0f}});
  t += 20'000;  // past the gap, standing still
  bool sent = false;
  for (const auto& o : sampler.frame(r, t)) {
    proto::Message m;
    REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
    if (const auto* s = std::get_if<proto::PlayerSample>(&m)) {
      sent = true;
      REQUIRE(s->ride.has_value());
      CHECK(s->ride->platform == kLift);
    }
  }
  CHECK(sent);
}

TEST_CASE("ride: the boat's shape: a deck far above and aside of its origin, followed further") {
  CHECK_FALSE(ride_shape("Obj_ForestTemple_Elevator_A_01").has_value());  // a lift: the settings' numbers
  CHECK_FALSE(ride_shape("Obj_ShadowTemple_PaddleShip_A_01_Attention").has_value());
  const auto boat = ride_shape("Obj_ShadowTemple_PaddleShip_A_01");
  REQUIRE(boat.has_value());
  CHECK(boat->reach > RideFollower::kReach);
  CHECK(boat->footprint >= 6.0f);
  CHECK(boat->min_height > 0.0f);

  // Our Link on the deck by the attention point (5.6 m aside, 6.9 m up), the boat going -x.
  const std::uint64_t kBoat = 0x45506786c4d7d94f;
  const auto boat_at = [&](float x) {
    return std::vector<RidePlatform>{{kBoat, {x, 39.26f, -62.21f}, "Obj_ShadowTemple_PaddleShip_A_01"}};
  };
  RideDetector d;
  std::uint64_t t = 1'000'000;
  float x = 136.6f;
  for (int i = 0; i < 3; ++i, t += kWalkUs, x -= 0.1f) d.update(proto::Vec3{x - 5.6f, 46.2f, -62.3f}, boat_at(x), t);
  REQUIRE(d.ride().has_value());
  CHECK(d.ride()->platform == kBoat);
  // The same place by a lift would be off it (4 m footprint, 3 m high at most).
  RideDetector lift;
  t = 1'000'000;
  float y = 100.0f;
  for (int i = 0; i < 3; ++i, t += kWalkUs, y -= 0.13f) lift.update(proto::Vec3{5.6f, y + 6.9f, -3.5f}, lift_at(y), t);
  CHECK_FALSE(lift.ride().has_value());

  // The follower takes the boat's reach: 4.5 m apart is followed with it, not without it.
  const proto::Ride theirs{kBoat, {0.0f, 0.0f, 0.0f}};
  const proto::Vec3 ours{0.0f, 0.0f, 0.0f};
  const proto::Vec3 drawn{4.5f, 0.0f, 0.0f};
  RideFollower with_reach;
  with_reach.step(theirs, ours, drawn, 1'000'000, boat->reach);
  CHECK(with_reach.on());
  RideFollower without;
  without.step(theirs, ours, drawn, 1'000'000);
  CHECK_FALSE(without.on());
}
