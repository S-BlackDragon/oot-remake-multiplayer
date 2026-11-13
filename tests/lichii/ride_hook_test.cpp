// ORM moving floors, the hook glue (lichii/hook/orm_ride.h, oot-remake docs/TEMPLOS.md and
// docs/MONTANA_SOMBRA.md): our ride from the objects walk, the others' rides from their samples,
// and the step that draws a partner's puppet on our copy of his lift or boat.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "orm_ride.h"

namespace proto = coop::protocol;
using coop::host::RidePlatform;
using OrmRide::Rides;

namespace {

constexpr std::uint64_t kWalkUs = 33'333;
constexpr std::uint64_t kLift = 0x7d1f39d42478c1f6;  // a Forest Temple elevator (Banc, 0 100 -3.5)
constexpr std::uint64_t kBoat = 0x45506786c4d7d94f;  // the Shadow Temple's boat (Banc, 136.6 39.26 -62.21)
const std::string kForest = "ForestTemple";
const std::string kShadow = "ShadowTemple";

std::vector<RidePlatform> lift_at(float y) { return {{kLift, {0.0f, y, -3.5f}, "Obj_ForestTemple_Elevator_A_01"}}; }
std::vector<RidePlatform> boat_at(float x) {
  return {{kBoat, {x, 39.26f, -62.21f}, "Obj_ShadowTemple_PaddleShip_A_01"}};
}

proto::PlayerSample sample(std::uint8_t player, std::uint32_t sequence, std::optional<proto::Ride> ride) {
  proto::PlayerSample s;
  s.player_number = player;
  s.time = {1'000'000, sequence};
  s.position = proto::Vec3{0.0f, 0.0f, 0.0f};
  s.ride = ride;
  return s;
}

CoopPuppet::Target target(std::uint8_t player, const proto::Vec3& at) {
  CoopPuppet::Target t;
  t.number = player;
  t.actor = "CoopPuppet_Adult";
  t.position = at;
  return t;
}

bool has(const std::vector<std::string>& lines, const std::string& line) {
  for (const auto& l : lines) {
    if (l == line) return true;
  }
  return false;
}

// Three walks of our Link standing 0.4 m from the middle of the lift as it goes down 0.13 m a
// walk (4 m/s): the ride starts on the third. Returns the time of the last walk.
std::uint64_t ride_down(Rides& r, std::uint64_t t, float& y) {
  for (int i = 0; i < 3; ++i, t += kWalkUs, y -= 0.13f) {
    r.Walked(kForest, proto::Vec3{0.4f, y, -3.5f}, lift_at(y), t);
  }
  return t - kWalkUs;
}

}  // namespace

TEST_CASE("ride hook: our ride goes out while the walks are fresh, and the log says on and off") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  float y = 100.0f;
  const std::uint64_t last = ride_down(r, 1'000'000, y);
  const auto ours = r.Ours(last + 1000);
  REQUIRE(ours.has_value());
  CHECK(ours->platform == kLift);
  CHECK(std::fabs(ours->offset[0] - 0.4f) < 1e-4f);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "ride: we ride Obj_ForestTemple_Elevator_A_01 7d1f39d42478c1f6, at 0.40 0.00 0.00 from it");
  // No walk for longer than kWalkFreshUs (the objects walk stopped): not sent.
  CHECK_FALSE(r.Ours(last + Rides::kWalkFreshUs + 1).has_value());
  // A jump off it ends the ride.
  r.Walked(kForest, proto::Vec3{0.4f, y + 0.6f, -3.5f}, lift_at(y), last + kWalkUs);
  CHECK_FALSE(r.Ours(last + kWalkUs).has_value());
  REQUIRE(lines.size() == 2);
  CHECK(lines[1] == "ride: we are off Obj_ForestTemple_Elevator_A_01 7d1f39d42478c1f6");
}

TEST_CASE("ride hook: a new scene ends our ride, and no scene carries nobody") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  float y = 100.0f;
  const std::uint64_t last = ride_down(r, 1'000'000, y);
  REQUIRE(r.Ours(last).has_value());
  r.Walked("ForestTemple_Boss", proto::Vec3{0.4f, y, -3.5f}, {}, last + kWalkUs);
  CHECK_FALSE(r.Ours(last + kWalkUs).has_value());
  CHECK(has(lines, "ride: we are off Obj_ForestTemple_Elevator_A_01 7d1f39d42478c1f6"));
  r.Walked("", proto::Vec3{0.4f, y, -3.5f}, lift_at(y), last + 2 * kWalkUs);
  CHECK_FALSE(r.Ours(last + 2 * kWalkUs).has_value());
}

TEST_CASE("ride hook: the others' rides, newest sample first, dropped when old or gone") {
  Rides r;
  const proto::Ride lift{kLift, {0.4f, 0.0f, 0.0f}};
  r.Received(sample(2, 5, lift), 1'000'000);
  REQUIRE(r.Theirs(2, 1'000'000).has_value());
  CHECK(r.Theirs(2, 1'000'000)->platform == kLift);
  CHECK_FALSE(r.Theirs(3, 1'000'000).has_value());
  r.Received(sample(2, 4, std::nullopt), 1'010'000);  // late and older: skipped
  CHECK(r.Theirs(2, 1'010'000).has_value());
  r.Received(sample(2, 6, std::nullopt), 1'020'000);  // newer, not riding
  CHECK_FALSE(r.Theirs(2, 1'020'000).has_value());
  r.Received(sample(2, 7, lift), 1'030'000);
  CHECK_FALSE(r.Theirs(2, 1'030'000 + Rides::kStaleUs + 1).has_value());  // no sample for a second
  r.Left(2);
  CHECK_FALSE(r.Theirs(2, 1'030'000).has_value());
  // After a reconnect the sequences start again: a low one is taken.
  r.Received(sample(4, 0xffffffffu, lift), 2'000'000);
  r.Received(sample(4, 0, std::nullopt), 2'000'100);  // wrapped: newer
  CHECK_FALSE(r.Theirs(4, 2'000'100).has_value());
  r.Received(sample(4, 1000, lift), 2'000'200);
  r.ForgetRemotes();
  r.Received(sample(4, 1, lift), 2'000'300);
  CHECK(r.Theirs(4, 2'000'300).has_value());
  r.Received(sample(0, 1, lift), 2'000'300);  // not a relayed number
  r.Received(sample(9, 1, lift), 2'000'300);
  CHECK_FALSE(r.Theirs(0, 2'000'300).has_value());
  CHECK_FALSE(r.Theirs(9, 2'000'300).has_value());
}

TEST_CASE("ride hook: a partner on his lift is drawn on ours, near only, and the director gets the step") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  float y = 100.0f;
  const std::uint64_t last = ride_down(r, 1'000'000, y);  // our lift moves here (we ride it too)
  // Past kMaxAheadUs our floor is where its last walk put it plus 0.1 s of its speed.
  const float floor_y = (y + 0.13f) - 0.13f * 3.0f;
  std::uint64_t t = last + 200'000;
  const proto::Ride theirs{kLift, {-0.4f, 0.0f, 0.0f}};
  r.Received(sample(2, 1, theirs), t);
  // His lift runs 0.5 m ahead of ours (lower): his samples put him under our floor.
  const proto::Vec3 sampled{-0.4f, floor_y - 0.5f, -3.5f};
  std::vector<CoopPuppet::Target> plan;
  for (int i = 0; i < 20; ++i, t += 16'667) {
    plan = {target(2, sampled)};
    r.Draw(plan, kForest, t);
  }
  REQUIRE(plan.size() == 1);
  CHECK(std::fabs(plan[0].position[1] - floor_y) < 1e-3f);
  CHECK(std::fabs(plan[0].position[0] + 0.4f) < 1e-4f);
  CHECK(std::fabs(plan[0].offset[1] - 0.5f) < 1e-3f);  // smoothing adds the same step
  REQUIRE(r.FollowerOf(2) != nullptr);
  CHECK(r.FollowerOf(2)->on());
  CHECK(has(lines, "ride: P2 rides 7d1f39d42478c1f6: drawn on our floor"));
  // His lift is 10 m from ours (a one-way ride our game did not take): drawn where he is.
  for (int i = 0; i < 20; ++i, t += 16'667) {
    plan = {target(2, {-0.4f, floor_y - 10.0f, -3.5f})};
    r.Draw(plan, kForest, t);
  }
  CHECK(std::fabs(plan[0].position[1] - (floor_y - 10.0f)) < 1e-4f);
  CHECK(has(lines, "ride: P2 rides 7d1f39d42478c1f6: our floor is 10.00 m from theirs, drawn where they are"));
  // Off his lift: drawn on his samples, one line.
  r.Received(sample(2, 2, std::nullopt), t);
  plan = {target(2, sampled)};
  r.Draw(plan, kForest, t);
  CHECK(plan[0].position == sampled);
  CHECK(lines.back() == "ride: P2 off its floor");
  const auto count = lines.size();
  r.Draw(plan, kForest, t + 16'667);
  CHECK(lines.size() == count);  // a change is logged once, not every frame
}

TEST_CASE("ride hook: no step for a floor not loaded here, a hidden puppet, another scene or no player") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  float y = 100.0f;
  const std::uint64_t last = ride_down(r, 1'000'000, y);
  std::uint64_t t = last + 50'000;
  r.Received(sample(3, 1, proto::Ride{0x1234, {0.0f, 0.0f, 0.0f}}), t);  // a floor we do not have
  r.Received(sample(4, 1, proto::Ride{kLift, {0.0f, 0.0f, 0.0f}}), t);
  const proto::Vec3 at{0.0f, y - 0.4f, -3.5f};
  for (int i = 0; i < 20; ++i, t += 16'667) {
    std::vector<CoopPuppet::Target> plan = {target(3, at), target(4, at), target(0, at)};
    plan[1].show = CoopPuppet::Show::hold;  // his cutscene: frozen as it is
    r.Draw(plan, kForest, t);
    CHECK(plan[0].position == at);
    CHECK(plan[1].position == at);
    CHECK(plan[2].position == at);  // the debug puppet
  }
  CHECK(has(lines, "ride: P3 rides 0000000000001234: not loaded here, drawn where they are"));
  // Our floors are the last walk's scene's: a plan of another scene gets nothing.
  std::vector<CoopPuppet::Target> plan = {target(4, at)};
  r.Draw(plan, "ForestTemple_Boss", t);
  CHECK(plan[0].position == at);
  // A puppet no longer planned is forgotten.
  plan.clear();
  r.Draw(plan, kForest, t);
  CHECK(r.FollowerOf(3) == nullptr);
}

TEST_CASE("ride hook: the Shadow Temple's boat: our deck carries us, a partner on his boat is drawn on ours") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  // The boat sets off along -x at 3 m/s; our Link stands on its deck near the attention point,
  // 5.6 m from its origin and 6.9 m above it: further than a lift's footprint.
  float x = 136.6f;
  std::uint64_t t = 1'000'000;
  for (int i = 0; i < 3; ++i, t += kWalkUs, x -= 0.1f) {
    r.Walked(kShadow, proto::Vec3{x - 5.6f, 46.2f, -62.3f}, boat_at(x), t);
  }
  const auto ours = r.Ours(t - kWalkUs);
  REQUIRE(ours.has_value());
  CHECK(ours->platform == kBoat);
  CHECK(std::fabs(ours->offset[0] + 5.6f) < 1e-3f);
  CHECK(std::fabs(ours->offset[1] - 6.94f) < 1e-3f);
  const float deck_x = (x + 0.1f) - 0.3f;  // our boat 0.1 s past its last walk
  // His boat set off 1.5 s after ours (he played the lullaby later): 4.5 m behind, inside the
  // boat's 6 m reach, so he is drawn on our deck.
  std::uint64_t now = t + 200'000;
  r.Received(sample(2, 1, proto::Ride{kBoat, {-3.0f, 6.9f, 1.0f}}), now);
  std::vector<CoopPuppet::Target> plan;
  for (int i = 0; i < 20; ++i, now += 16'667) {
    plan = {target(2, {deck_x - 3.0f + 4.5f, 39.26f + 6.9f, -62.21f + 1.0f})};
    r.Draw(plan, kShadow, now);
  }
  CHECK(std::fabs(plan[0].position[0] - (deck_x - 3.0f)) < 1e-3f);
  CHECK(has(lines, "ride: P2 rides 45506786c4d7d94f: drawn on our floor"));
  // 8 m behind: too far even for the boat, drawn where he is.
  for (int i = 0; i < 20; ++i, now += 16'667) {
    plan = {target(2, {deck_x - 3.0f + 8.0f, 39.26f + 6.9f, -62.21f + 1.0f})};
    r.Draw(plan, kShadow, now);
  }
  CHECK(std::fabs(plan[0].position[0] - (deck_x - 3.0f + 8.0f)) < 1e-4f);
}

// ---- ORM boss families (oot-remake docs/JEFES.md section 9): a held partner rides the part ----

TEST_CASE("ride hook: what holds our Link goes out instead of a floor, and a held partner is drawn on our copy") {
  std::vector<std::string> lines;
  Rides r([&](const std::string& l) { lines.push_back(l); });
  constexpr std::uint64_t kHand = (std::uint64_t{1} << 63) | 0x0002'0000'0000'0007ull;  // family_key of a made id
  const RidePlatform hand{kHand, {2.0f, 1.0f, 0.0f}, "Enemy_DungeonBoss_BongoBongo_RightHand"};
  // Our side: held by the hand, our ride is the hand's (no floor detected).
  OrmRide::Grabs grabs;
  grabs.parts = {hand};
  grabs.ours = proto::Ride{kHand, {0.0f, 0.5f, 0.0f}};
  std::uint64_t t = 1'000'000;
  r.Walked(kShadow, proto::Vec3{2.0f, 1.5f, 0.0f}, {}, t, grabs);
  const auto ours = r.Ours(t);
  REQUIRE(ours.has_value());
  CHECK(ours->platform == kHand);
  CHECK(ours->offset[1] == doctest::Approx(0.5f));
  // Let go: nothing goes out.
  r.Walked(kShadow, proto::Vec3{2.0f, 0.0f, 0.0f}, {}, t + kWalkUs, OrmRide::Grabs{{hand}, std::nullopt});
  CHECK_FALSE(r.Ours(t + kWalkUs).has_value());
  // A hand that moves with our Link is never taken for a floor by our own detector.
  for (int i = 0; i < 4; ++i) {
    const float x = 2.0f + 0.1f * static_cast<float>(i);
    r.Walked(kShadow, proto::Vec3{x, 1.5f, 0.0f}, {},
             t + static_cast<std::uint64_t>(i + 2) * kWalkUs,
             OrmRide::Grabs{{RidePlatform{kHand, {x, 1.0f, 0.0f}, hand.name}}, std::nullopt});
  }
  CHECK_FALSE(r.Ours(t + 5 * kWalkUs).has_value());

  // Their side (another game): P2 is held by his hand, our copy of it stands still 0.3 m off
  // his: drawn on ours.
  Rides other([&](const std::string& l) { lines.push_back(l); });
  t += 6 * kWalkUs;
  other.Walked(kShadow, proto::Vec3{9.0f, 0.0f, 0.0f}, {}, t, OrmRide::Grabs{{hand}, std::nullopt});
  t += kWalkUs;
  other.Walked(kShadow, proto::Vec3{9.0f, 0.0f, 0.0f}, {}, t, OrmRide::Grabs{{hand}, std::nullopt});
  other.Received(sample(2, 1, proto::Ride{kHand, {0.0f, 0.5f, 0.0f}}), t);
  const proto::Vec3 sampled{2.3f, 1.5f, 0.0f};
  std::vector<CoopPuppet::Target> plan;
  for (int i = 0; i < 20; ++i, t += 16'667) {
    plan = {target(2, sampled)};
    other.Draw(plan, kShadow, t);
  }
  CHECK(plan[0].position[0] == doctest::Approx(2.0f));
  CHECK(plan[0].position[1] == doctest::Approx(1.5f));
  CHECK(has(lines, "ride: P2 is held by Enemy_DungeonBoss_BongoBongo_RightHand 8002000000000007: drawn on our copy of it"));
  // Let go: one line, drawn on his samples.
  other.Received(sample(2, 2, std::nullopt), t);
  plan = {target(2, sampled)};
  other.Draw(plan, kShadow, t);
  CHECK(lines.back() == "ride: P2 is let go");
}
