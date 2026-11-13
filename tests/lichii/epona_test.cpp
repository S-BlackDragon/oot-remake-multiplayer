// Tests for the riders' horses (lichii/hook/coop_epona.cpp, ORM D-R19): where
// a horse goes under its rider, which gait it plays, when it waits and goes,
// and the puppet planner showing a rider instead of parking him.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <string>
#include <vector>

#include "coop_epona.h"
#include "coop_puppet.h"

namespace {

using CoopEpona::Rider;
using CoopEpona::Settings;
using CoopPuppet::Input;
using CoopPuppet::Mat3;
using CoopPuppet::Show;
using CoopPuppet::Target;
using CoopPuppet::Vec3;
namespace epona = coop::host::epona;

constexpr std::uint64_t kBase = 0x80000000;

Settings Quick() {
    Settings s;
    s.gait_hold_ms = 0;
    return s;
}

// A yaw of `deg` degrees: forward (column 2) = (sin, 0, cos).
Mat3 Yaw(float deg) {
    const float r = deg * 3.14159265f / 180.0f;
    const float s = std::sin(r), c = std::cos(r);
    return {c, 0, s, 0, 1, 0, -s, 0, c};
}

Target Puppet(std::uint8_t number, Vec3 at, const char* anim, Mat3 rot = Yaw(0)) {
    Target t;
    t.number = number;
    t.actor = "CoopPuppet_Adult";
    t.position = at;
    t.rotation = rot;
    if (anim != nullptr) {
        t.anim = CoopPuppet::Anim{anim, 0.0f, 1.0f};
    }
    return t;
}

Input Plan(std::vector<Target> targets, std::string scene = "HyruleField_Adult") {
    Input in;
    in.main_base = kBase;
    in.scene = std::move(scene);
    in.targets = std::move(targets);
    return in;
}

} // namespace

TEST_CASE("epona horse: upright keeps the heading and drops any tilt") {
    const Mat3 h = CoopEpona::Upright(Yaw(90));
    CHECK(h[2] == doctest::Approx(1.0f));   // forward x
    CHECK(h[8] == doctest::Approx(0.0f));   // forward z
    CHECK(h[4] == doctest::Approx(1.0f));   // up stays up
    // A forward axis tilted up 30 degrees keeps its heading.
    const float c = std::cos(0.5236f), s = std::sin(0.5236f);
    const Mat3 tilted{1, 0, 0, 0, c, s, 0, -s, c};
    const Mat3 u = CoopEpona::Upright(tilted);
    CHECK(u[2] == doctest::Approx(0.0f));
    CHECK(u[5] == doctest::Approx(0.0f));
    CHECK(u[8] == doctest::Approx(1.0f));
    // Straight up: no heading, identity.
    const Mat3 up{1, 0, 0, 0, 0, 1, 0, -1, 0};
    CHECK(CoopEpona::Upright(up)[0] == doctest::Approx(1.0f));
}

TEST_CASE("epona horse: stands the saddle offset under its rider, turned by his heading") {
    const Vec3 saddle{0.0f, 1.2f, 0.3f};  // 0.3 forward of the horse's root
    Vec3 at = CoopEpona::HorseSpot({10, 5, 20}, Yaw(0), saddle);
    CHECK(at[0] == doctest::Approx(10.0f));
    CHECK(at[1] == doctest::Approx(3.8f));
    CHECK(at[2] == doctest::Approx(19.7f));
    at = CoopEpona::HorseSpot({10, 5, 20}, Yaw(90), saddle);
    CHECK(at[0] == doctest::Approx(9.7f));
    CHECK(at[2] == doctest::Approx(20.0f));
    // The probe's offset is the same number read back.
    const Vec3 rider{10, 5, 20};
    const Vec3 off = CoopEpona::SaddleProbe::Offset(at, Yaw(90), rider);
    CHECK(off[0] == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(off[1] == doctest::Approx(1.2f));
    CHECK(off[2] == doctest::Approx(0.3f));
}

TEST_CASE("epona horse: a rider on Epona gets a horse under him, as our Epona pack") {
    Rider r(Quick());
    const std::map<std::uint8_t, std::string> mounts{{2, "Animal_Epona_Adult"}};
    const auto out = r.Follow(Plan({Puppet(2, {0, 10, 0}, "HorseMove"), Puppet(3, {5, 0, 5}, "Walk")}), 1000,
                              ~0u, &mounts);
    REQUIRE(out.targets.size() == 1);  // the walker has none
    const auto& h = out.targets[0];
    CHECK(h.number == 2);
    CHECK(h.actor == "CoopPuppet_Horse");
    CHECK(std::string(CoopEpona::kHorsePack) == "CoopPuppet_Horse");
    CHECK(std::string(CoopEpona::kEventHorse) == "Animal_Horse_For_Event_Base");
    CHECK(h.position[1] == doctest::Approx(10.0f - Settings{}.saddle[1]));
    CHECK(h.show == Show::show);
    REQUIRE(h.anim);
    CHECK(h.anim->name == "Wait");  // standing still
    CHECK(h.anim_serial == 0u);
    CHECK(out.main_base == kBase);
    CHECK(out.scene == "HyruleField_Adult");
    CHECK_FALSE(h.gear[0]);  // no gear for a horse
}

TEST_CASE("epona horse: the gait follows the rider's speed") {
    Rider r(Quick());
    std::uint64_t t = 1000;
    float z = 0.0f;
    auto step = [&](float speed) {
        z += speed * 0.05f;
        t += 50;
        return r.Follow(Plan({Puppet(1, {0, 1.2f, z}, "HorseMove")}), t);
    };
    for (int i = 0; i < 40; ++i) step(15.0f);  // 2 s at a gallop
    CHECK(r.GaitOf(1) == epona::Gait::gallop);
    auto out = step(15.0f);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].anim->name == "Move_Gear_Top");
    for (int i = 0; i < 40; ++i) step(2.0f);
    CHECK(r.GaitOf(1) == epona::Gait::walk);
    for (int i = 0; i < 40; ++i) step(0.0f);
    CHECK(r.GaitOf(1) == epona::Gait::wait);
}

TEST_CASE("epona horse: a new gait waits out the hold") {
    Settings s;
    s.gait_hold_ms = 300;
    Rider r(s);
    std::uint64_t t = 1000;
    r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove")}), t);
    CHECK(r.GaitOf(1) == epona::Gait::wait);
    // A jump in speed for 0.2 s: not yet.
    float z = 0;
    for (int i = 0; i < 4; ++i) {
        z += 0.5f;
        t += 50;
        r.Follow(Plan({Puppet(1, {0, 1.2f, z}, "HorseMove")}), t);
    }
    CHECK(r.GaitOf(1) == epona::Gait::wait);
    for (int i = 0; i < 10; ++i) {
        z += 0.5f;
        t += 50;
        r.Follow(Plan({Puppet(1, {0, 1.2f, z}, "HorseMove")}), t);
    }
    CHECK(r.GaitOf(1) != epona::Gait::wait);
}

TEST_CASE("epona horse: waits where he got off, stays when he gets on there, goes when he leaves") {
    std::vector<std::string> lines;
    Rider r(Quick(), [&](const std::string& l) { lines.push_back(l); });
    auto out = r.Follow(Plan({Puppet(4, {3, 1.2f, 3}, "HorseMove")}), 1000);
    REQUIRE(out.targets.size() == 1);
    const Vec3 spot = out.targets[0].position;
    // He gets off and walks away: the horse stays, standing.
    out = r.Follow(Plan({Puppet(4, {4, 0, 3}, "DismountHorse")}), 1100);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(spot[0]));
    CHECK(out.targets[0].anim->name == "Wait");
    out = r.Follow(Plan({Puppet(4, {9, 0, 3}, "Walk")}), 5000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(spot[0]));
    CHECK(out.targets[0].show == Show::show);
    // He gets on beside it: still there (his puppet is shown climbing on).
    Target on = Puppet(4, {4, 0, 3}, "HorseRideOnL_F");
    on.why = "getting on";
    out = r.Follow(Plan({on}), 6000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].show == Show::show);
    CHECK(out.targets[0].position[0] == doctest::Approx(spot[0]));
    // In the saddle: under him again.
    out = r.Follow(Plan({Puppet(4, {3.5f, 1.2f, 3}, "HorseMove")}), 7000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(3.5f));
    // He leaves the place: his horse goes after the grace.
    out = r.Follow(Plan({}), 7100);
    CHECK(out.targets.empty());
    CHECK(r.GaitOf(4));  // kept for a moment (a stall)
    out = r.Follow(Plan({}), 9500);
    CHECK_FALSE(r.GaitOf(4));
    bool rides = false, off = false, left = false;
    for (const auto& l : lines) {
        rides = rides || l == "P4 rides: a horse under him";
        off = off || l == "P4 got off: his horse waits where he left it";
        left = left || l == "P4 left: his horse goes";
    }
    CHECK(rides);
    CHECK(off);
    CHECK(left);
}

TEST_CASE("epona horse: getting on far from the waiting horse brings it to him") {
    Rider r(Quick());
    r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove")}), 1000);
    r.Follow(Plan({Puppet(1, {1, 0, 0}, "DismountHorse")}), 1100);
    auto out = r.Follow(Plan({Puppet(1, {50, 0, 0}, "HorseRideOn")}), 20000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(50.0f));
    CHECK(out.targets[0].position[1] == doctest::Approx(0.0f));  // on the ground beside him
    out = r.Follow(Plan({Puppet(1, {50, 1.2f, 0}, "HorseMove")}), 21000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(50.0f));
}

TEST_CASE("epona horse: seen getting on, in the saddle and getting off (request: always seen)") {
    std::vector<std::string> lines;
    Rider r(Quick(), [&](const std::string& l) { lines.push_back(l); });
    // He starts to get on with no horse of ours anywhere: one stands where he starts.
    auto out = r.Follow(Plan({Puppet(5, {10, 0, 10}, "HorseRideOnL_F", Yaw(90))}), 1000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].show == Show::show);
    CHECK(out.targets[0].position[0] == doctest::Approx(10.0f));
    CHECK(out.targets[0].anim->name == "Wait");
    CHECK(out.targets[0].rotation[2] == doctest::Approx(1.0f));  // facing as he does
    // He climbs (his root moves): the horse stays where it stood.
    out = r.Follow(Plan({Puppet(5, {10.4f, 0.8f, 10}, "HorseRideOnL_F", Yaw(90))}), 1500);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(10.0f));
    // In the saddle: under him.
    out = r.Follow(Plan({Puppet(5, {10.6f, 1.2f, 10}, "HorseMove", Yaw(90))}), 2000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(10.6f));
    CHECK(out.targets[0].position[1] == doctest::Approx(0.0f));
    // Getting off: shown on foot beside it, the horse stays.
    out = r.Follow(Plan({Puppet(5, {11.2f, 0.5f, 10}, "DismountHorse", Yaw(90))}), 3000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].position[0] == doctest::Approx(10.6f));
    CHECK(out.targets[0].show == Show::show);
    bool beside = false;
    for (const auto& l : lines) beside = beside || l == "P5 gets on: a horse beside him";
    CHECK(beside);
    // While his puppet is not live, no horse with him (getting on too).
    Rider r2(Quick());
    out = r2.Follow(Plan({Puppet(6, {0, 0, 0}, "HorseRideOn")}), 1000, 0u);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].show == Show::hide);
}

TEST_CASE("epona horse: another actor from now on (the fallback to the event horse)") {
    Rider r(Quick());
    r.SetActor(CoopEpona::kEventHorse);
    const auto out = r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove")}), 1000);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].actor == "Animal_Horse_For_Event_Base");
}

TEST_CASE("epona horse: waits only so long when linger_ms is set, and never across a scene") {
    Settings s = Quick();
    s.linger_ms = 10000;
    Rider r(s);
    r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove")}), 1000);
    CHECK(r.Follow(Plan({Puppet(1, {1, 0, 0}, "Walk")}), 2000).targets.size() == 1);
    CHECK(r.Follow(Plan({Puppet(1, {1, 0, 0}, "Walk")}), 11000).targets.size() == 1);
    CHECK(r.Follow(Plan({Puppet(1, {1, 0, 0}, "Walk")}), 12500).targets.empty());
    // A new scene: no horse of the old one.
    r.Follow(Plan({Puppet(2, {0, 1.2f, 0}, "HorseMove")}), 13000);
    CHECK(r.Follow(Plan({Puppet(2, {0, 0, 0}, "Walk")}, "LonLonRanch"), 13100).targets.empty());
}

TEST_CASE("epona horse: never under a rider whose puppet is not live") {
    Rider r(Quick());
    auto out = r.Follow(Plan({Puppet(3, {0, 1.2f, 0}, "HorseMove")}), 1000, 0u);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].show == Show::hide);
    CHECK(std::string(out.targets[0].why) == "its rider is not shown");
    out = r.Follow(Plan({Puppet(3, {0, 1.2f, 0}, "HorseMove")}), 1100, 1u << 3);
    CHECK(out.targets[0].show == Show::show);
    // The test puppet (number 0) rides nothing.
    out = r.Follow(Plan({Puppet(0, {0, 1.2f, 0}, "HorseMove")}), 1200);
    CHECK(out.targets.empty());
}

TEST_CASE("epona horse: the planner shows a rider getting on and riding with the horses on") {
    std::vector<CoopPuppet::Remote> remotes(2);
    remotes[0].number = 1;
    remotes[0].scene = "HyruleField_Adult";
    remotes[0].adult = true;
    remotes[0].anim = CoopPuppet::Anim{"HorseMove", 0.0f, 1.0f};
    remotes[1].number = 2;
    remotes[1].scene = "HyruleField_Adult";
    remotes[1].adult = true;
    remotes[1].anim = CoopPuppet::Anim{"HorseRideOn", 0.0f, 1.0f};
    CoopPuppet::PlannerSettings s;
    s.riders = true;
    CoopPuppet::Planner p(s);
    auto in = p.Plan(kBase, std::string("HyruleField_Adult"), std::nullopt, false, remotes, 0);
    REQUIRE(in.targets.size() == 2);
    CHECK(in.targets[0].show == Show::show);
    CHECK(std::string(in.targets[0].why) == "riding");
    CHECK(in.targets[1].show == Show::show);
    CHECK(std::string(in.targets[1].why) == "getting on");
    // Riders off (LICHII_ORM_HORSE=0; the planner's own default): test10's riding rule
    // (Visibility::ride, upstream's horse copy CoopRide_Epona) shows them both, never parked;
    // the getting on is not named (PIN_TEST10.md, Escena).
    CoopPuppet::Planner old;
    in = old.Plan(kBase, std::string("HyruleField_Adult"), std::nullopt, false, remotes, 0);
    REQUIRE(in.targets.size() == 2);
    CHECK(in.targets[0].show == Show::show);
    CHECK(std::string(in.targets[0].why) == "riding");
    CHECK(in.targets[1].show == Show::show);
    CHECK(std::string(in.targets[1].why) == "riding");
    // Six on horses in Hyrule Field: five horses in each game, one per rider.
    std::vector<CoopPuppet::Remote> five(5);
    for (std::uint8_t i = 0; i < 5; ++i) {
        five[i].number = static_cast<std::uint8_t>(i + 2);
        five[i].scene = "HyruleField_Adult";
        five[i].adult = true;
        five[i].position = {static_cast<float>(i) * 4.0f, 1.2f, 0.0f};
        five[i].anim = CoopPuppet::Anim{"HorseMove", 0.0f, 1.0f};
    }
    in = p.Plan(kBase, std::string("HyruleField_Adult"), std::nullopt, false, five, 0);
    Rider r(Quick());
    const auto horses = r.Follow(in, 1000);
    REQUIRE(horses.targets.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(horses.targets[i].number == i + 2);
        CHECK(horses.targets[i].show == Show::show);
        CHECK(horses.targets[i].position[0] == doctest::Approx(static_cast<float>(i) * 4.0f));
    }
}

TEST_CASE("epona horse: each horse with its own look, Epona's only for Epona") {
    Rider r(Quick());
    std::map<std::uint8_t, std::string> mounts{{1, "Animal_Epona_Adult"}, {2, "Animal_Horse_For_Ingo"},
                                               {3, "Animal_Horse"}, {4, ""}};
    auto out = r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove"), Puppet(2, {5, 1.2f, 0}, "HorseMove"),
                              Puppet(3, {10, 1.2f, 0}, "HorseMove"), Puppet(4, {15, 1.2f, 0}, "HorseMove"),
                              Puppet(5, {20, 1.2f, 0}, "HorseMove")}),
                        1000, ~0u, &mounts);
    REQUIRE(out.targets.size() == 5);
    CHECK(out.targets[0].actor == "CoopPuppet_Horse");
    CHECK(out.targets[1].actor == "CoopPuppet_Horse_Ingo");
    CHECK(out.targets[2].actor == "CoopPuppet_Horse_Ranch");
    CHECK(out.targets[3].actor == "Animal_Horse_For_Event_Base");  // his game found none: plain
    CHECK(out.targets[4].actor == "Animal_Horse_For_Event_Base");  // an older game: not said
    // He gets off a ranch horse: it waits with its own coat, though his mount is "" now.
    mounts[3] = "";
    out = r.Follow(Plan({Puppet(3, {11, 0, 0}, "DismountHorse")}), 1100, ~0u, &mounts);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].actor == "CoopPuppet_Horse_Ranch");
    // The mount said late: the plain horse becomes his own.
    mounts[5] = "Animal_Epona_Adult";
    out = r.Follow(Plan({Puppet(5, {20, 1.2f, 0}, "HorseMove")}), 1200, ~0u, &mounts);
    CHECK(r.ActorOf(5) == std::string("CoopPuppet_Horse"));
    // A pack the game could not make: the plain horse for it, never Epona's for another.
    r.MarkBroken("CoopPuppet_Horse_Ingo");
    out = r.Follow(Plan({Puppet(2, {5, 1.2f, 0}, "HorseMove")}), 1300, ~0u, &mounts);
    CHECK(out.targets[0].actor == "Animal_Horse_For_Event_Base");
    CHECK(r.ActorFor(std::string_view("Animal_Epona_Adult")) == "CoopPuppet_Horse");
    // One actor for all (LICHII_COOP_HORSE_ACTOR).
    Settings s = Quick();
    s.actor = "Animal_Horse_For_Zelda";
    Rider one(s);
    out = one.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove")}), 1000, ~0u, &mounts);
    CHECK(out.targets[0].actor == "Animal_Horse_For_Zelda");
}

TEST_CASE("epona horse: our own horse is the nearest of the game's horses under us") {
    std::vector<CoopEpona::SeenHorse> seen{{"Animal_Horse", {10, 0, 0}},
                                           {"Animal_Epona_Adult", {0.2f, 0.0f, 0.1f}},
                                           {"CoopPuppet_Horse", {0, 0, 0}},  // another player's, ours
                                           {"Animal_Cow", {0, 0, 0}}};
    CHECK(CoopEpona::PickMount(seen, {0, 1.2f, 0}) == std::string("Animal_Epona_Adult"));
    CHECK(CoopEpona::PickMount(seen, {10, 1.2f, 0}) == std::string("Animal_Horse"));
    CHECK_FALSE(CoopEpona::PickMount(seen, {50, 1.2f, 0}));  // none within 3
    CHECK_FALSE(CoopEpona::PickMount({}, {0, 0, 0}));
    // The reading sent: the mount on a horse, "" on foot, none without equipment.
    CoopEpona::MountReader m;
    coop::host::PlayerReading on;
    on.animation = coop::protocol::Animation{"HorseMove", 0.0f, 1.0f};
    on.equipment = coop::protocol::Equipment{};
    m.Stamp(on);
    CHECK(on.equipment->mount == std::string(""));  // nothing found yet
    coop::host::PlayerReading foot;
    foot.animation = coop::protocol::Animation{"Walk", 0.0f, 1.0f};
    foot.equipment = coop::protocol::Equipment{};
    m.Stamp(foot);
    CHECK(foot.equipment->mount == std::string(""));
    coop::host::PlayerReading none;
    m.Stamp(none);
    CHECK_FALSE(none.equipment);
}

// ORM smoothing (REVISION_NUEVAS orm/epona): one smoothed place for a rider and his horse.
namespace {

coop::host::SmoothedPose Shown(Vec3 at, float yaw_deg) {
    coop::host::SmoothedPose p;
    p.position = at;
    const float half = yaw_deg * 3.14159265f / 360.0f;
    p.rotation = coop::protocol::Quat{0.0f, std::sin(half), 0.0f, std::cos(half)};
    p.animation = coop::protocol::Animation{"HorseMove", 3.0f, 1.0f};
    p.sender_speed = 11.0f;
    p.gait = coop::host::Gait::run;
    return p;
}

} // namespace

TEST_CASE("epona horse smoothing: the horse is the saddle under the rider's own smoothed pose") {
    const Vec3 saddle{0.0f, 1.2f, 0.3f};
    // Facing +x (yaw 90): the saddle's 0.3 forward is along x.
    const auto rider = Shown({10.0f, 5.0f, 20.0f}, 90.0f);
    const auto horse = CoopEpona::HorseUnder(rider, Yaw(0), saddle);
    CHECK(horse.position[0] == doctest::Approx(9.7f));
    CHECK(horse.position[1] == doctest::Approx(3.8f));
    CHECK(horse.position[2] == doctest::Approx(20.0f));
    REQUIRE(horse.rotation);
    const Mat3 r = CoopPuppet::QuatToMatrix(*horse.rotation);
    CHECK(r[2] == doctest::Approx(1.0f));  // forward x, upright
    CHECK(r[8] == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(r[4] == doctest::Approx(1.0f));
    // None of the rider's animation: the horse's director plays the gait.
    CHECK_FALSE(horse.animation);
    CHECK_FALSE(horse.sender_speed);
    CHECK(horse.gait == coop::host::Gait::unknown);
    // A rider pose without a rotation: the planned heading turns the saddle.
    auto bare = rider;
    bare.rotation.reset();
    const auto h2 = CoopEpona::HorseUnder(bare, Yaw(180), saddle);
    CHECK(h2.position[0] == doctest::Approx(10.0f).epsilon(1e-5));
    CHECK(h2.position[2] == doctest::Approx(20.3f));
    // A tilted rider (leaning into a gallop) still stands his horse upright.
    auto lean = rider;
    lean.rotation = coop::protocol::Quat{std::sin(0.125f), 0.0f, 0.0f, std::cos(0.125f)};  // pitched 0.25 rad
    const auto h3 = CoopEpona::HorseUnder(lean, Yaw(90), saddle);
    CHECK(h3.position[0] == doctest::Approx(10.0f));  // facing +z: forward 0.3 is along z
    CHECK(h3.position[1] == doctest::Approx(3.8f));
    CHECK(h3.position[2] == doctest::Approx(19.7f));
}

TEST_CASE("epona horse smoothing: at a gallop rider and horse move as one, frame by frame") {
    // The bug: the rider drawn from the smoother on the calc thread, the horse
    // from the plan on the present thread; at 11 units/s one frame apart is
    // about 18 cm. Now both come from the same pose each frame.
    const Vec3 saddle{0.0f, 1.2f, 0.0f};
    CoopEpona::UnderRiders under;
    under.Set({{2, Yaw(0)}});
    std::optional<CoopPuppet::ShownPose> last;
    const auto lookup = [&](std::uint8_t n) -> std::optional<CoopPuppet::ShownPose> {
        CHECK(n == 2);
        return last;
    };
    const Vec3 offset{0.4f, 0.0f, 0.0f};  // his side step, added to both by the director
    for (int frame = 0; frame < 10; ++frame) {
        const std::uint64_t server = 5'000'000 + static_cast<std::uint64_t>(frame) * 16'667;
        const float z = 11.0f * static_cast<float>(frame) / 60.0f;
        last = CoopPuppet::ShownPose{server, Shown({0.0f, 1.2f, z}, 0.0f)};
        const auto horse = under.At(2, server + 300, lookup, saddle);  // the horses' director, same frame
        REQUIRE(horse);
        const Vec3 rider_drawn{last->pose.position[0] + offset[0], last->pose.position[1] + offset[1],
                               last->pose.position[2] + offset[2]};
        const Vec3 horse_drawn{horse->position[0] + offset[0], horse->position[1] + offset[1],
                               horse->position[2] + offset[2]};
        CHECK(rider_drawn[0] - horse_drawn[0] == doctest::Approx(0.0f));
        CHECK(rider_drawn[1] - horse_drawn[1] == doctest::Approx(1.2f));
        CHECK(rider_drawn[2] - horse_drawn[2] == doctest::Approx(0.0f));
    }
}

TEST_CASE("epona horse smoothing: only a horse with its rider in the saddle, and only this frame's pose") {
    const Vec3 saddle{0.0f, 1.2f, 0.0f};
    CoopEpona::UnderRiders under;
    std::optional<CoopPuppet::ShownPose> last = CoopPuppet::ShownPose{1'000'000, Shown({1, 2, 3}, 0)};
    const CoopEpona::UnderRiders::Lookup lookup = [&](std::uint8_t) { return last; };
    // Nobody in the saddle (a horse waiting where he got off, or getting on): the plan.
    CHECK_FALSE(under.At(1, 1'000'000, lookup, saddle));
    under.Set({{1, Yaw(0)}});
    CHECK(under.At(1, 1'000'000, lookup, saddle));
    CHECK_FALSE(under.At(3, 1'000'000, lookup, saddle));  // another player
    // The rider's pose is from another frame (his puppet held, or the smoothing off).
    CHECK(under.At(1, 1'000'000 + CoopEpona::UnderRiders::kSameFrameUs, lookup, saddle));
    CHECK_FALSE(under.At(1, 1'000'000 + CoopEpona::UnderRiders::kSameFrameUs + 1, lookup, saddle));
    CHECK_FALSE(under.At(1, 1'000'000 - CoopEpona::UnderRiders::kSameFrameUs - 1, lookup, saddle));
    last.reset();  // never drawn smoothed
    CHECK_FALSE(under.At(1, 1'000'000, lookup, saddle));
    CHECK_FALSE(under.At(1, 1'000'000, CoopEpona::UnderRiders::Lookup{}, saddle));
}

TEST_CASE("epona horse smoothing: Follow says who is in the saddle and passes the time and side step on") {
    Rider r(Quick());
    auto in = Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove"), Puppet(2, {5, 1.2f, 0}, "HorseRideOn")});
    in.targets[0].offset = {0.25f, 0.0f, 0.0f};
    in.targets[1].offset = {0.5f, 0.0f, 0.0f};
    in.server_now_us = 7'000'000;
    in.server_at_ms = 990;
    auto out = r.Follow(in, 1000);
    CHECK(out.server_now_us == 7'000'000);
    CHECK(out.server_at_ms == 990);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].offset[0] == doctest::Approx(0.25f));  // in the saddle
    CHECK(out.targets[1].offset[0] == doctest::Approx(0.0f));   // getting on: his horse stands
    auto riding = r.Riding();
    REQUIRE(riding.size() == 1);
    CHECK(riding.contains(1));
    // He gets off: his horse waits and keeps its place, unsmoothed.
    out = r.Follow(Plan({Puppet(1, {0, 0, 0}, "Wait"), Puppet(2, {5, 1.2f, 0}, "HorseMove")}), 1100);
    riding = r.Riding();
    CHECK_FALSE(riding.contains(1));
    CHECK(riding.contains(2));
}

TEST_CASE("epona horse: only the looks the puppet pack lists are asked for") {
    // build.py's coop_puppet.json: "horses" with each "actor"; members and packs name them too.
    const std::string manifest =
        "{\"actors\": [{\"actor\": \"CoopPuppet_Adult\"}], \"horses\": [{\"actor\": \"CoopPuppet_Horse_Ranch\", "
        "\"pack\": \"Pack/Actor/CoopPuppet_Horse_Ranch.pack.zs\", \"members\": "
        "[\"Actor/CoopPuppet_Horse.engine__actor__ActorParam.bgyml\"]}]}";
    const auto looks = CoopEpona::HorseLooksIn(manifest);
    CHECK(looks == std::set<std::string>{"CoopPuppet_Horse_Ranch"});  // not Epona's from the other names
    CHECK(CoopEpona::HorseLooksIn("{\"actors\": [], \"shots\": []}").empty());  // a pack from before the horses
    CHECK(CoopEpona::HorseLooksIn("\"CoopPuppet_Horse\" \"CoopPuppet_Horse_Ingo\"") ==
          std::set<std::string>{"CoopPuppet_Horse", "CoopPuppet_Horse_Ingo"});
    // What the glue does with a look the pack lacks: the plain horse for it.
    Rider r(Quick());
    for (const std::string_view look : {epona::kEponaLook, epona::kRanchLook, epona::kIngoLook}) {
        if (!looks.contains(std::string(look))) r.MarkBroken(std::string(look));
    }
    const std::map<std::uint8_t, std::string> mounts{{1, "Animal_Epona_Adult"}, {2, "Animal_Horse"}};
    const auto out = r.Follow(Plan({Puppet(1, {0, 1.2f, 0}, "HorseMove"), Puppet(2, {5, 1.2f, 0}, "HorseMove")}),
                              1000, ~0u, &mounts);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].actor == "Animal_Horse_For_Event_Base");
    CHECK(out.targets[1].actor == "CoopPuppet_Horse_Ranch");
}

// ---------------------------------------------------------------------------
// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): the saddle and the gait
// lines measured on our own horse, not the estimates.

#include <cstring>
#include <map>

#include "coop_check_visual.h"
#include "coop_fairy.h"

namespace {

using CoopEpona::RideMeasure;

class RideGuest : public CoopPuppet::Guest {
public:
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(address + i);
            if (it == bytes.end()) {
                return false;
            }
            o[i] = it->second;
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            bytes[address + i] = p[i];
        }
        return true;
    }
    template <typename T>
    void Set(std::uint64_t address, const T& value) {
        Write(address, &value, sizeof(T));
    }
    void SetText(std::uint64_t address, const std::string& text) {
        Write(address, text.c_str(), text.size() + 1);
        for (std::size_t i = text.size() + 1; i < 64; ++i) {
            bytes[address + i] = 0;
        }
    }
    std::map<std::uint64_t, std::uint8_t> bytes;
};

// A horse actor with an AS controller whose slot 0 plays `command`.
struct HorseGame {
    static constexpr std::uint64_t kHorse = 0x10000;
    static constexpr std::uint64_t kAs = 0x20000;
    static constexpr std::uint64_t kController = 0x21000;
    static constexpr std::uint64_t kSlot0 = 0x22000;
    static constexpr std::uint64_t kText = 0x23000;
    static constexpr std::uint64_t kName = 0x24000;
    RideGuest guest;
    HorseGame() {
        guest.Set(kHorse + 0x270, kAs);
        guest.Set(kAs + 0x40, kAs + 0x40);
        guest.Set(kAs + 0x40 + 0x18, kAs + 0x80);
        guest.Set(kAs + 0x80 + 0x18, kController);
        guest.Set(kController + CoopPuppet::addr::kSlotCount, std::int32_t{1});
        guest.Set(kController + CoopPuppet::addr::kSlots, kAs + 0x300);
        guest.Set(kAs + 0x300, kSlot0);
        guest.Set(kSlot0 + CoopPuppet::addr::kSlotName, kText);
        guest.SetText(kName, "Animal_Epona_Adult");
        guest.Set(kHorse + CoopFairy::addr::kActorName, kName);
        Plays("Wait");
        At({10, 0, 0}, Yaw(0));
    }
    void Plays(const std::string& command) { guest.SetText(kText, command); }
    void At(const Vec3& at, const Mat3& rot) {
        guest.Set(kHorse + CoopPuppet::addr::kActorPosition, at);
        guest.Set(kHorse + CoopPuppet::addr::kActorRotation, rot);
    }
};

}  // namespace

TEST_CASE("ride measure: the saddle is the median of agreeing still readings, out of range left out") {
    RideMeasure m;
    CHECK_FALSE(m.Saddle());
    CHECK(m.SaddleWhy().find("not sat still") != std::string::npos);
    m.AddSaddle({0.0f, 5.0f, 0.0f}, "Animal_Epona_Adult");  // far over the saddle: not a seat
    m.AddSaddle({0.02f, 1.31f, -0.11f}, "Animal_Epona_Adult");
    m.AddSaddle({0.01f, 1.29f, -0.10f}, "Animal_Epona_Adult");
    CHECK_FALSE(m.Saddle());  // two of three
    CHECK(m.SaddleWhy().find("2 of 3") != std::string::npos);
    CHECK(m.SaddleWhy().find("1 out of range") != std::string::npos);
    m.AddSaddle({0.00f, 1.30f, -0.12f}, "Animal_Epona_Adult");
    const auto s = m.Saddle();
    REQUIRE(s);
    CHECK((*s)[0] == doctest::Approx(0.01f));
    CHECK((*s)[1] == doctest::Approx(1.30f));
    CHECK((*s)[2] == doctest::Approx(-0.11f));
    CHECK(m.SaddleSays().find("median of 3 still readings on our own Animal_Epona_Adult") != std::string::npos);
    // A horse that shifted: the newest three disagree, nothing until they settle.
    m.AddSaddle({0.40f, 1.30f, -0.12f}, "Animal_Epona_Adult");
    CHECK_FALSE(m.Saddle());
    CHECK(m.SaddleWhy().find("disagree") != std::string::npos);
    // Another horse starts its own readings.
    m.AddSaddle({0.0f, 1.10f, 0.0f}, "Animal_Horse");
    CHECK_FALSE(m.Saddle());
    // NaN never counts.
    m.AddSaddle({std::nanf(""), 1.2f, 0.0f}, "Animal_Horse");
    CHECK(m.SaddleWhy().find("out of range") != std::string::npos);
}

TEST_CASE("ride measure: gait lines halfway between the measured gaits, only with all four rising") {
    RideMeasure m;
    const epona::GaitSpeeds estimate;
    CHECK_FALSE(m.Speeds(estimate));
    for (int k = 0; k < 12; ++k) {
        m.AddGait("Move_Gear_1", 1.8f + 0.01f * static_cast<float>(k % 3));
        m.AddGait("Move_Gear_2", 5.0f);
        m.AddGait("Move_Gear_3", 9.0f);
        m.AddGait("Wait", 0.0f);        // not a gait
        m.AddGait("HorseMove", 20.0f);  // not the horse's gait either
    }
    CHECK_FALSE(m.Speeds(estimate));  // no gallop yet
    CHECK(m.SpeedsWhy().find("Move_Gear_Top 0") != std::string::npos);
    for (int k = 0; k < 12; ++k) {
        m.AddGait("Move_Gear_Top", 14.0f);
    }
    const auto g = m.Speeds(estimate);
    REQUIRE(g);
    CHECK(g->walk == doctest::Approx(0.5f));  // the estimate is lower than half the walk
    CHECK(g->trot == doctest::Approx(0.5f * (1.81f + 5.0f)).epsilon(0.01));
    CHECK(g->canter == doctest::Approx(7.0f));
    CHECK(g->gallop == doctest::Approx(11.5f));
    // Each gait's own speed picks that gait, from either side.
    CHECK(epona::gait_for(1.81f, epona::Gait::wait, *g) == epona::Gait::walk);
    CHECK(epona::gait_for(5.0f, epona::Gait::walk, *g) == epona::Gait::trot);
    CHECK(epona::gait_for(9.0f, epona::Gait::gallop, *g) == epona::Gait::canter);
    CHECK(epona::gait_for(14.0f, epona::Gait::wait, *g) == epona::Gait::gallop);
    // Gaits that do not get faster in order: not drawn with.
    RideMeasure bad;
    for (int k = 0; k < 12; ++k) {
        bad.AddGait("Move_Gear_1", 6.0f);
        bad.AddGait("Move_Gear_2", 5.0f);
        bad.AddGait("Move_Gear_3", 9.0f);
        bad.AddGait("Move_Gear_Top", 14.0f);
    }
    CHECK_FALSE(bad.Speeds(estimate));
    CHECK(bad.SpeedsWhy().find("do not get faster") != std::string::npos);
}

TEST_CASE("ride measure: the file keeps the readings for the next run, junk left out") {
    RideMeasure m;
    for (int k = 0; k < 3; ++k) {
        m.AddSaddle({0.0f, 1.3f, -0.1f}, "Animal_Epona_Adult");
    }
    for (int k = 0; k < 10; ++k) {
        m.AddGait("Move_Gear_1", 2.0f);
        m.AddGait("Move_Gear_2", 5.0f);
        m.AddGait("Move_Gear_3", 9.0f);
        m.AddGait("Move_Gear_Top", 14.0f);
    }
    const std::string text = m.Save();
    RideMeasure next;
    // 3 saddles and 40 gait readings; the out-of-range saddle, gait 7 and junk left out.
    CHECK(next.Load(text + "saddle 0 9 0 Animal_Epona_Adult\ngait 7 1.0\nnonsense\n") == 43);
    CHECK(next.FromFile());
    REQUIRE(next.Saddle());
    CHECK((*next.Saddle())[1] == doctest::Approx(1.3f));
    REQUIRE(next.Speeds());
    CHECK(next.SaddleSays().find("an earlier run") != std::string::npos);
    // A reading of this run replaces the file's of its own kind, never mixed
    // with it; the other kind stays (ORM review: a first trot used to drop the
    // file's saddle, and the next save wrote the file without it).
    next.AddSaddle({0.0f, 1.0f, 0.0f}, "Animal_Epona_Adult");
    CHECK_FALSE(next.FromFile());
    CHECK_FALSE(next.Saddle());
    REQUIRE(next.Speeds());
    CHECK(next.SpeedsSays().find("(an earlier run)") != std::string::npos);
    RideMeasure trot;
    REQUIRE(trot.Load(text) == 43);
    trot.AddGait("Move_Gear_2", 5.5f);
    CHECK_FALSE(trot.FromFile());
    CHECK_FALSE(trot.Speeds());  // this run's gaits start over
    REQUIRE(trot.Saddle());      // the file's saddle stays
    CHECK((*trot.Saddle())[1] == doctest::Approx(1.3f));
    CHECK(trot.SaddleSays().find("an earlier run") != std::string::npos);
    // The next save keeps the file's saddle with this run's gait.
    RideMeasure again;
    again.Load(trot.Save());
    REQUIRE(again.Saddle());
    CHECK((*again.Saddle())[1] == doctest::Approx(1.3f));
    RideMeasure none;
    CHECK(none.Load("") == 0);
    CHECK_FALSE(none.FromFile());
}

TEST_CASE("ride pick: switch, measure or estimate, with one check line per path") {
    static std::vector<std::string> lines;
    lines.clear();
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([](const std::string& line) { lines.push_back(line); });
    RideMeasure m;
    Settings est;
    // Nobody drawn on a horse, nothing measured: no line, the estimates.
    auto now = CoopEpona::PickRide(m, std::nullopt, false, est);
    CHECK(lines.empty());
    CHECK(now.saddle[1] == doctest::Approx(1.2f));
    CHECK_FALSE(now.saddle_measured);
    // A horse drawn: the fallback lines, once.
    now = CoopEpona::PickRide(m, std::nullopt, true, est);
    CoopEpona::PickRide(m, std::nullopt, true, est);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].rfind("coop: check: saddle fallback (0.00, 1.20, 0.00), the estimate: we have not sat still", 0) == 0);
    CHECK(lines[1].rfind("coop: check: horse gaits fallback the estimates 0.50 / 3.50 / 7.00 / 11.00", 0) == 0);
    // Measured: the ok line and the measured saddle.
    for (int k = 0; k < 3; ++k) {
        m.AddSaddle({0.0f, 1.42f, -0.2f}, "Animal_Epona_Adult");
    }
    now = CoopEpona::PickRide(m, std::nullopt, false, est);
    CHECK(now.saddle_measured);
    CHECK(now.saddle[1] == doctest::Approx(1.42f));
    REQUIRE(lines.size() == 3);
    CHECK(lines[2].rfind("coop: check: saddle ok (0.00, 1.42, -0.20), the median of 3", 0) == 0);
    // The switch wins over the measure.
    CoopVCheck::ResetForTest();
    lines.clear();
    now = CoopEpona::PickRide(m, Vec3{0.0f, 1.0f, 0.0f}, true, est);
    CHECK(now.saddle[1] == doctest::Approx(1.0f));
    REQUIRE_FALSE(lines.empty());
    CHECK(lines[0] == "coop: check: saddle ok (0.00, 1.00, 0.00) as LICHII_ORM_HORSE_SADDLE sets it");
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

TEST_CASE("saddle probe: our horse's gait command with our speed, and where we sit when still") {
    HorseGame g;
    RideMeasure m;
    CoopEpona::SaddleProbe probe("Animal_Epona_Adult", {}, &m);
    // Riding forward at 5 units a second on a trotting horse.
    g.Plays("Move_Gear_2");
    std::uint64_t t = 1000;
    for (int k = 0; k < 40; ++k, t += 100) {
        probe.SetOwn(Vec3{0.5f * static_cast<float>(k), 1.3f, 0.0f}, true, t);
        probe.Frame(g.guest, kBase, t, HorseGame::kHorse);
    }
    // A still stretch on the same horse, turned 90 degrees: where we sit in its frame.
    g.Plays("Wait");
    g.At({30, 0, 0}, Yaw(90));
    for (int k = 0; k < 40; ++k, t += 100) {
        probe.SetOwn(Vec3{30.0f, 1.25f, 0.3f}, true, t);
        probe.Frame(g.guest, kBase, t, HorseGame::kHorse);
    }
    const auto s = m.Saddle();
    REQUIRE(s);
    CHECK((*s)[1] == doctest::Approx(1.25f));
    CHECK(std::fabs((*s)[0]) == doctest::Approx(0.3f));  // the side the horse's turn puts it on
    CHECK(m.SaddleSays().find("Animal_Epona_Adult") != std::string::npos);
    // The trot's readings are in: with the other gaits the lines come, and
    // the trot's median (near 5 a second) sits between them.
    for (int k = 0; k < 12; ++k) {
        m.AddGait("Move_Gear_1", 2.0f);
        m.AddGait("Move_Gear_3", 9.0f);
        m.AddGait("Move_Gear_Top", 14.0f);
    }
    const auto lines = m.Speeds();
    REQUIRE(lines);
    CHECK(lines->trot > 2.0f);
    CHECK(lines->trot < 5.0f);
    CHECK(lines->canter > 5.0f);
    // On foot: nothing is read.
    RideMeasure foot;
    CoopEpona::SaddleProbe off("Animal_Epona_Adult", {}, &foot);
    for (int k = 0; k < 20; ++k, t += 100) {
        off.SetOwn(Vec3{30.0f, 1.25f, 0.3f}, false, t);
        off.Frame(g.guest, kBase, t, HorseGame::kHorse);
    }
    CHECK_FALSE(foot.Saddle());
}
