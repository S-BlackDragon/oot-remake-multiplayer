// ORM "Levantar" (oot-remake docs/DETALLES_MARIONETA.md): what a player lifts
// is seen in his hands everywhere (lichii/hook/coop_carry.cpp, and the carry
// rules of coop_objects_sync.cpp). Plain logic and fake guest memory.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop_carry.h"
#include "coop_objects_sync.h"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace {

namespace proto = coop::protocol;
using CoopCarry::Frame;
using CoopCarry::Mat3;
using CoopCarry::Vec3;

constexpr std::uint64_t kMs = 1000;
const Mat3 kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};
// A quarter turn about y: x goes to -z, z to x (rows, world = M * local).
const Mat3 kQuarter{0, 0, 1, 0, 1, 0, -1, 0, 0};

bool Near(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a[0] - b[0]) <= eps && std::fabs(a[1] - b[1]) <= eps &&
           std::fabs(a[2] - b[2]) <= eps;
}

bool NearM(const Mat3& a, const Mat3& b, float eps = 1e-3f) {
    for (std::size_t i = 0; i < 9; ++i) {
        if (std::fabs(a[i] - b[i]) > eps) return false;
    }
    return true;
}

class FakeGuest : public CoopPuppet::Guest {
public:
    void Map(std::uint64_t at, std::size_t size) { regions_[at].assign(size, 0); }
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* p = Find(address, size);
        if (p == nullptr) return false;
        std::memcpy(out, p, size);
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        auto* p = Find(address, size);
        if (p == nullptr) return false;
        std::memcpy(p, in, size);
        return true;
    }
    template <typename T>
    void Put(std::uint64_t at, const T& value) {
        REQUIRE(Write(at, &value, sizeof(T)));
    }
    void Unmap(std::uint64_t at) { regions_.erase(at); }

private:
    std::uint8_t* Find(std::uint64_t address, std::size_t size) {
        auto it = regions_.upper_bound(address);
        if (it == regions_.begin()) return nullptr;
        --it;
        if (address + size > it->first + it->second.size()) return nullptr;
        return it->second.data() + (address - it->first);
    }
    std::map<std::uint64_t, std::vector<std::uint8_t>> regions_;
};

// An actor at `at` turned by `rot`, with a skeleton of `bones` world
// matrices (all identity turns, placed at the actor); returns the world array.
std::uint64_t MakeActor(FakeGuest& g, std::uint64_t actor, const Vec3& at, const Mat3& rot,
                        std::uint32_t bones) {
    const std::uint64_t a1 = actor + 0x1000, a2 = actor + 0x2000, a3 = actor + 0x3000,
                        a4 = actor + 0x4000, skel = actor + 0x5000, world = actor + 0x10000;
    g.Map(actor, 0x600);
    for (std::uint64_t m : {a1, a2, a3, a4, skel}) g.Map(m, 0x400);
    g.Map(world, bones * CoopCarry::kWorldStride);
    const auto& path = CoopPose::addr::kSkeletonPath;  // 0x270, 0x8, 0x18, 0x18, 0x140
    g.Put(actor + path[0], a1);
    g.Put(a1 + path[1], a2);
    g.Put(a2 + path[2], a3);
    g.Put(a3 + path[3], a4);
    g.Put(a4 + path[4], skel);
    g.Put(skel + CoopPose::addr::kBoneCount, static_cast<std::uint16_t>(bones));
    g.Put(skel + CoopCarry::kWorldArray, world);
    g.Put(actor + 0x31c, at);
    g.Put(actor + 0x328, rot);
    for (std::uint32_t i = 0; i < bones; ++i) {
        // 4x4 by rows, the place in floats 12 to 14 (the bone census, sondas12).
        const std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, at[0], at[1], at[2], 1};
        g.Put(world + CoopCarry::kWorldStride * i, m);
    }
    return world;
}

void PutBone(FakeGuest& g, std::uint64_t world, std::uint32_t bone, const Mat3& r, const Vec3& p) {
    const std::array<float, 16> m{r[0], r[1], r[2], 0, r[3], r[4], r[5], 0,
                                  r[6], r[7], r[8], 0, p[0], p[1], p[2], 1};
    g.Put(world + CoopCarry::kWorldStride * bone, m);
}

// The old reading's layout: 3x4 rows, 0x30 apart, the place the 4th of each row.
void PutBone34(FakeGuest& g, std::uint64_t world, std::uint32_t bone, const Mat3& r, const Vec3& p) {
    const std::array<float, 12> rows{r[0], r[1], r[2], p[0], r[3], r[4], r[5], p[1],
                                     r[6], r[7], r[8], p[2]};
    g.Put(world + CoopCarry::kWorldStride34 * bone, rows);
}

}  // namespace

// ---------------------------------------------------------------- names and phases

TEST_CASE("carry: what a Link lifts, from the romfs packs with a LiftableRef") {
    using CoopCarry::IsLiftable;
    CHECK(IsLiftable("Obj_Pot_A_08"));
    CHECK(IsLiftable("Obj_Pot_A_02_Respawn"));
    CHECK(IsLiftable("Obj_LiftRock_A_03"));
    CHECK(IsLiftable("Obj_LiftRock_B_01"));  // the silver rock (Silver Gauntlets)
    CHECK(IsLiftable("Obj_LiftRock_C_05"));
    CHECK(IsLiftable("Obj_BrokenBox_A_03"));
    CHECK(IsLiftable("Obj_Plant_Grass_A_01_Respawn"));
    CHECK(IsLiftable("Animal_Cucco"));
    CHECK(IsLiftable("Animal_Cucco_KakarikoVillage_Adult"));
    CHECK(IsLiftable("Animal_SuperCucco"));
    CHECK(IsLiftable("Obj_BombFlower_A_01"));
    CHECK(IsLiftable("Npc_Zora_Ruto_Child_001"));
    CHECK_FALSE(IsLiftable("Animal_FlockCucco"));  // the flock that attacks
    CHECK_FALSE(IsLiftable("Obj_BrokenBox_A_01")); // a crate the sword breaks
    CHECK_FALSE(IsLiftable("Item_Bomb"));          // a projectile (coop_projectiles.h)
    CHECK_FALSE(IsLiftable("Obj_Rupee_Green"));
    CHECK_FALSE(IsLiftable("Obj_BombFlower_A_02"));  // the plant, not the fruit
    CHECK(CoopCarry::IsBombFlower("Obj_BombFlower_Fruit_For_Break_Obj_DeathMtTrail_BrokenRock_A_01"));
}

TEST_CASE("carry: Link's lift commands") {
    using CoopCarry::Phase;
    using CoopCarry::PhaseOf;
    CHECK(PhaseOf("Lift_Ready_Pod") == Phase::lift);
    CHECK(PhaseOf("Lift_Up_High_Rock") == Phase::lift);
    CHECK(PhaseOf("Lift_Wait_RockM") == Phase::lift);
    CHECK(PhaseOf("Lift_Throw_Cucco") == Phase::thrown);
    CHECK(PhaseOf("Lift_Put_Stand_High_Box") == Phase::put);
    CHECK(PhaseOf("Lift_Drop") == Phase::dropped);
    CHECK(PhaseOf("Lift_Fail_Stand") == Phase::dropped);
    CHECK(PhaseOf("LiftThrow") == Phase::thrown);
    CHECK(PhaseOf("Wait") == Phase::none);
    CHECK(PhaseOf("Pose_Rock_Run") == Phase::none);  // walking with it: the box decides
    CHECK(CoopCarry::IsLiftStart("Lift_Up_Cucco"));
    CHECK_FALSE(CoopCarry::IsLiftStart("Lift_Wait_RockM"));
    CHECK(CoopCarry::IsBombLift("Lift_Up_High_Bomb"));
    CHECK(CoopCarry::IsBombLift("Lift_Ready_BombChu"));
    CHECK_FALSE(CoopCarry::IsBombLift("Lift_Up_Box"));
    CHECK_FALSE(CoopCarry::IsBombLift("Item_Bomb"));
}

// ---------------------------------------------------------------- frames

TEST_CASE("carry: frames compose, invert and give back what was put in") {
    const Frame link{{10.0f, 2.0f, -4.0f}, kQuarter};
    const Frame pot{{10.3f, 3.6f, -4.1f}, kIdentity};
    const Frame local = CoopCarry::Relative(link, pot);
    const Frame back = CoopCarry::Compose(link, local);
    CHECK(Near(back.p, pot.p));
    CHECK(NearM(back.r, pot.r));
    // Link's forward (+z local) is world +x for a quarter turn.
    CHECK(Near(CoopCarry::Apply(link, {0.0f, 0.0f, 1.0f}), {11.0f, 2.0f, -4.0f}));
    const Frame none = CoopCarry::Compose(link, CoopCarry::Inverse(link));
    CHECK(Near(none.p, {0.0f, 0.0f, 0.0f}));
    CHECK(NearM(none.r, kIdentity));
    // Half way between no turn and a quarter: an eighth, at the mid place.
    const Frame half = CoopCarry::Blend({{0, 0, 0}, kIdentity}, {{2, 0, 0}, kQuarter}, 0.5f);
    CHECK(Near(half.p, {1.0f, 0.0f, 0.0f}));
    CHECK(half.r[0] == doctest::Approx(std::cos(0.785398f)).epsilon(0.001));
}

TEST_CASE("carry: a scaled or mirrored bone matrix is made a rigid turn, a flat one is refused") {
    const Mat3 scaled{2, 0, 0, 0, 2, 0, 0, 0, 2};
    REQUIRE(CoopCarry::Rigid(scaled));
    CHECK(NearM(*CoopCarry::Rigid(scaled), kIdentity));
    const Mat3 mirrored{1, 0, 0, 0, 1, 0, 0, 0, -1};
    REQUIRE(CoopCarry::Rigid(mirrored));
    CHECK(NearM(*CoopCarry::Rigid(mirrored), kIdentity));  // a turn, the same both ends use
    CHECK_FALSE(CoopCarry::Rigid(Mat3{1, 1, 0, 0, 0, 0, 0, 0, 0}));
    CHECK_FALSE(CoopCarry::Rigid(Mat3{NAN, 0, 0, 0, 1, 0, 0, 0, 1}));
}

TEST_CASE("carry: a bone matrix in the world, in the model's frame, or neither") {
    const Frame actor{{100.0f, 5.0f, 50.0f}, kQuarter};
    // In the world: near the actor.
    const std::array<float, 12> world{1, 0, 0, 100.2f, 0, 1, 0, 6.5f, 0, 0, 1, 50.1f};
    const auto w = CoopCarry::BoneWorld(actor, world);
    REQUIRE(w);
    CHECK(Near(w->p, {100.2f, 6.5f, 50.1f}));
    // In the model's frame: near the origin, composed with the actor.
    const std::array<float, 12> model{1, 0, 0, 0.0f, 0, 1, 0, 1.5f, 0, 0, 1, 0.5f};
    const auto m = CoopCarry::BoneWorld(actor, model);
    REQUIRE(m);
    CHECK(Near(m->p, {100.5f, 6.5f, 50.0f}));  // local +z is world +x
    CHECK(NearM(m->r, kQuarter));
    // Neither: something else read there.
    const std::array<float, 12> far{1, 0, 0, 40.0f, 0, 1, 0, 0.0f, 0, 0, 1, 0.0f};
    CHECK_FALSE(CoopCarry::BoneWorld(actor, far));
}

TEST_CASE("carry: a carry goes to the wire and back with its bone") {
    CoopCarry::Local l;
    l.actor = {{0.0f, 1.6f, 0.3f}, kQuarter};
    l.bone = 42;
    l.bone_frame = {{0.05f, 0.1f, 0.0f}, kIdentity};
    const proto::ObjectCarry c = CoopCarry::ToCarry(3, l);
    CHECK(c.holder == 3);
    CHECK(c.bone == 42);
    const CoopCarry::Local back = CoopCarry::FromCarry(c);
    CHECK(Near(back.actor.p, l.actor.p));
    CHECK(NearM(back.actor.r, l.actor.r));
    REQUIRE(back.bone);
    CHECK(*back.bone == 42);
    // Between a carry by bone 42 and one by bone 7: no bone.
    proto::ObjectCarry other = c;
    other.bone = 7;
    CHECK(CoopCarry::BlendCarry(c, other, 0.5f).bone == proto::kCarryNoBone);
    CHECK(CoopCarry::BlendCarry(c, c, 0.5f).bone == 42);
}

// ---------------------------------------------------------------- fake memory: attach

TEST_CASE("carry: a copy is attached to the puppet's bone, else to its actor frame") {
    FakeGuest g;
    const std::uint64_t puppet = 0x40000000;
    const Vec3 at{20.0f, 0.0f, 30.0f};
    const std::uint64_t world = MakeActor(g, puppet, at, kQuarter, 64);
    // Bone 12 (a hand) raised over the puppet's head, turned like the puppet.
    PutBone(g, world, 12, kQuarter, {20.0f, 1.7f, 30.0f});

    proto::ObjectCarry c;
    c.holder = 2;
    c.offset = {0.0f, 1.9f, 0.2f};   // actor frame: up and a little forward
    c.rotation = CoopCarry::QuatOfRows(kIdentity);
    c.bone = 12;
    c.bone_offset = {0.0f, 0.2f, 0.0f};  // just above the hand
    c.bone_rotation = CoopCarry::QuatOfRows(kIdentity);

    CoopCarry::Anchor used = CoopCarry::Anchor::none;
    auto f = CoopCarry::Attached(g, puppet, c, true, &used);
    REQUIRE(f);
    CHECK(used == CoopCarry::Anchor::bone);
    CHECK(Near(f->p, {20.0f, 1.9f, 30.0f}));

    // The bone switch off: the actor frame (+z local is world +x).
    f = CoopCarry::Attached(g, puppet, c, false, &used);
    REQUIRE(f);
    CHECK(used == CoopCarry::Anchor::actor);
    CHECK(Near(f->p, {20.2f, 1.9f, 30.0f}));

    // The puppet's arms move: the copy goes with the bone.
    PutBone(g, world, 12, kQuarter, {20.5f, 1.2f, 30.0f});
    f = CoopCarry::Attached(g, puppet, c, true, &used);
    REQUIRE(f);
    CHECK(Near(f->p, {20.5f, 1.4f, 30.0f}));

    // A bone matrix that is not one (another layout in this build): the
    // actor frame, never a copy thrown across the room.
    PutBone(g, world, 12, kQuarter, {300.0f, 0.0f, 0.0f});
    f = CoopCarry::Attached(g, puppet, c, true, &used);
    REQUIRE(f);
    CHECK(used == CoopCarry::Anchor::actor);
    CHECK(Near(f->p, {20.2f, 1.9f, 30.0f}));

    // A bone past the skeleton: the actor frame too.
    c.bone = 200;
    f = CoopCarry::Attached(g, puppet, c, true, &used);
    REQUIRE(f);
    CHECK(used == CoopCarry::Anchor::actor);

    // The puppet is gone (its memory unmapped): nothing, the mover keeps the
    // samples' place.
    g.Unmap(puppet);
    CHECK_FALSE(CoopCarry::Attached(g, puppet, c, true, &used));
    CHECK(used == CoopCarry::Anchor::none);
}

TEST_CASE("carry: the skeleton's layout is told from bone 0: 4x4 by rows or columns, or the old 3x4") {
    // sondas12, the bone census on Link: at 0x40 all 309 matrices read as
    // rigid frames; at 0x30 (the old reading) only bone 0 was right.
    FakeGuest g;
    const std::uint64_t link = 0x50000000;
    const Vec3 at{10.0f, 2.0f, -4.0f};
    std::uint64_t world = MakeActor(g, link, at, kQuarter, 8);
    PutBone(g, world, 0, kQuarter, at);
    auto s = CoopCarry::FindSkeleton(g, link);
    REQUIRE(s);
    CHECK(s->layout == CoopCarry::Layout::m44);
    CHECK_FALSE(s->columns);
    CHECK(std::string(CoopCarry::NameOf(*s)) == "4x4 by rows");
    // Stored by columns: bone 0's turn is the actor's transposed.
    const Mat3 t{kQuarter[0], kQuarter[3], kQuarter[6], kQuarter[1], kQuarter[4], kQuarter[7],
                 kQuarter[2], kQuarter[5], kQuarter[8]};
    PutBone(g, world, 0, t, at);
    PutBone(g, world, 3, t, {10.0f, 3.0f, -4.0f});
    s = CoopCarry::FindSkeleton(g, link);
    REQUIRE(s);
    CHECK(s->columns);
    const Frame actor{at, kQuarter};
    const auto bone = CoopCarry::ReadBone(g, *s, actor, 3);
    REQUIRE(bone);
    CHECK(NearM(bone->r, kQuarter));
    CHECK(Near(bone->p, {10.0f, 3.0f, -4.0f}));
    // The old 3x4 layout still reads (the fallback), bones 0x30 apart.
    FakeGuest old;
    world = MakeActor(old, link, at, kIdentity, 8);
    for (std::uint32_t i = 0; i < 8; ++i) PutBone34(old, world, i, kIdentity, at);
    PutBone34(old, world, 5, kIdentity, {10.0f, 3.5f, -4.0f});
    s = CoopCarry::FindSkeleton(old, link);
    REQUIRE(s);
    CHECK(s->layout == CoopCarry::Layout::m34);
    const auto near = CoopCarry::NearestBone(old, *s, Frame{at, kIdentity}, {10.0f, 3.4f, -4.0f});
    REQUIRE(near);
    CHECK(near->first == 5);
}

TEST_CASE("carry: our Link's nearest bone to what he lifts, from one read of the array") {
    FakeGuest g;
    const std::uint64_t link = 0x50000000;
    const std::uint64_t world = MakeActor(g, link, {0.0f, 0.0f, 0.0f}, kIdentity, 32);
    PutBone(g, world, 5, kIdentity, {0.0f, 1.0f, 0.4f});
    PutBone(g, world, 9, kIdentity, {0.1f, 1.6f, 0.2f});
    const auto skeleton = CoopCarry::FindSkeleton(g, link);
    REQUIRE(skeleton);
    CHECK(skeleton->count == 32);
    const Frame actor{{0, 0, 0}, kIdentity};
    const auto near = CoopCarry::NearestBone(g, *skeleton, actor, {0.1f, 1.7f, 0.2f});
    REQUIRE(near);
    CHECK(near->first == 9);
    CHECK(near->second == doctest::Approx(0.1f).epsilon(0.01));
    // No skeleton: nothing.
    CHECK_FALSE(CoopCarry::FindSkeleton(g, 0x60000000));
}

// ---------------------------------------------------------------- ours: the lifter

namespace {

CoopCarry::LinkNow LinkAt(const Vec3& at, const std::string& command, const Mat3& rot = kIdentity) {
    CoopCarry::LinkNow l;
    l.frame = {at, rot};
    l.command = command;
    return l;
}

CoopCarry::Candidate Pot(std::uint64_t address, const Vec3& at, std::string name = "Obj_Pot_A_08") {
    CoopCarry::Candidate c;
    c.address = address;
    c.name = std::move(name);
    c.position = at;
    return c;
}

}  // namespace

TEST_CASE("carry: our Link lifts the pot in front of him, it is claimed and carried, then thrown") {
    CoopCarry::Lifter lifter;
    const std::string scene = "KakarikoVillage_Child";
    // Two pots: one in front (+z, Link's forward), one as near behind him.
    std::vector<CoopCarry::Candidate> pots{Pot(0x100, {0.0f, 0.0f, 0.8f}),
                                           Pot(0x200, {0.0f, 0.0f, -0.7f})};
    lifter.Update(scene, LinkAt({0, 0, 0}, "Wait"), pots, 0);
    CHECK_FALSE(lifter.Held());
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Ready_Pod"), pots, 33 * kMs);
    REQUIRE(lifter.Held());
    CHECK(*lifter.Held() == 0x100);
    auto events = lifter.TakeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == CoopCarry::Event::Kind::lifted);
    CHECK(lifter.Claimed(33 * kMs) == std::set<std::uint64_t>{0x100});

    // Raised over his head and carried: the carry is where it sits in his frame.
    pots[0].position = {0.0f, 1.7f, 0.1f};
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Up_Pod"), pots, 300 * kMs);
    auto carry = lifter.CarryOf(0x100, 1);
    REQUIRE(carry);
    CHECK(carry->holder == 1);
    CHECK(carry->bone == proto::kCarryNoBone);  // no bones given
    CHECK(Near(carry->offset, {0.0f, 1.7f, 0.1f}));
    // He walks on with it (another command, the box keeps it).
    pots[0].position = {3.0f, 1.7f, 0.1f};
    lifter.Update(scene, LinkAt({3, 0, 0}, "Run"), pots, 600 * kMs);
    REQUIRE(lifter.Held());
    CHECK(Near(lifter.CarryOf(0x100, 1)->offset, {0.0f, 1.7f, 0.1f}));
    CHECK_FALSE(lifter.CarryOf(0x200, 1));

    // Thrown: let go at once; the claim stays kAfterUs for the throw and the break.
    lifter.Update(scene, LinkAt({3, 0, 0}, "Lift_Throw_Pod"), pots, 900 * kMs);
    CHECK_FALSE(lifter.Held());
    CHECK_FALSE(lifter.CarryOf(0x100, 1));
    events = lifter.TakeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == CoopCarry::Event::Kind::released);
    CHECK(events[0].why == CoopCarry::Release::thrown);
    CHECK(lifter.Claimed(900 * kMs + CoopCarry::Lifter::kAfterUs - 1).contains(0x100));
    CHECK(lifter.Claimed(900 * kMs + CoopCarry::Lifter::kAfterUs).empty());
}

TEST_CASE("carry: a lift is taken only at its start, of a liftable, not one in another's hands") {
    CoopCarry::Lifter lifter;
    const std::string scene = "S";
    std::vector<CoopCarry::Candidate> near{Pot(0x100, {0.0f, 0.0f, 0.8f})};
    // Standing with a lift command long after it began: no new pot.
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Up_Pod"), {}, 0);
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Up_Pod"), near,
                  CoopCarry::Lifter::kStartWindowUs + 1);
    CHECK_FALSE(lifter.Held());
    lifter.Reset();
    // Another player's puppet holds our copy: skipped.
    near[0].held_by_other = true;
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Ready_Pod"), near, 0);
    CHECK_FALSE(lifter.Held());
    lifter.Reset();
    // Too far, or not a liftable: nothing.
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Ready_Pod"),
                  {Pot(0x100, {0.0f, 0.0f, 2.5f}), Pot(0x300, {0.0f, 0.0f, 0.5f}, "Enemy_Stalchild")}, 0);
    CHECK_FALSE(lifter.Held());
    lifter.Reset();
    // A bomb out of the bag beside a pot: the pot is not taken; a bomb flower is.
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Up_Bomb"), {Pot(0x100, {0.0f, 0.0f, 0.8f})}, 0);
    CHECK_FALSE(lifter.Held());
    lifter.Reset();
    lifter.Update(scene, LinkAt({0, 0, 0}, "Lift_Up_Bomb"),
                  {Pot(0x100, {0.0f, 0.0f, 0.8f}), Pot(0x400, {0.3f, 0.0f, 0.9f}, "Obj_BombFlower_A_01")}, 0);
    REQUIRE(lifter.Held());
    CHECK(*lifter.Held() == 0x400);
}

TEST_CASE("carry: our Link's bone at the lift, read again each walk") {
    CoopCarry::Lifter lifter;
    Frame hand{{0.1f, 1.5f, 0.3f}, kIdentity};
    int finds = 0;
    const CoopCarry::Lifter::BoneFinder find = [&](const Vec3&) {
        ++finds;
        return std::optional(std::pair<std::uint16_t, float>{17, 0.2f});
    };
    const CoopCarry::Lifter::BoneReader bone = [&](std::uint16_t b) {
        return b == 17 ? std::optional(hand) : std::nullopt;
    };
    std::vector<CoopCarry::Candidate> pot{Pot(0x100, {0.1f, 1.7f, 0.3f})};
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Up_Pod"), pot, 0, find, bone);
    REQUIRE(lifter.CarryOf(0x100, 2));
    CHECK(lifter.CarryOf(0x100, 2)->bone == 17);
    CHECK(Near(lifter.CarryOf(0x100, 2)->bone_offset, {0.0f, 0.2f, 0.0f}));
    // The hand moves and the pot with it: the bone frame stays the same.
    hand.p = {0.4f, 1.2f, 0.5f};
    pot[0].position = {0.4f, 1.4f, 0.5f};
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Up_Pod"), pot, 33 * kMs, find, bone);
    CHECK(Near(lifter.CarryOf(0x100, 2)->bone_offset, {0.0f, 0.2f, 0.0f}));
    CHECK(Near(lifter.CarryOf(0x100, 2)->offset, {0.4f, 1.4f, 0.5f}));
    CHECK(finds == 1);  // the nearest bone is looked for once, at the lift
    // A bone too far away at the lift is not used.
    CoopCarry::Lifter far;
    const CoopCarry::Lifter::BoneFinder away = [](const Vec3&) {
        return std::optional(std::pair<std::uint16_t, float>{3, 2.0f});
    };
    far.Update("S", LinkAt({0, 0, 0}, "Lift_Up_Pod"), pot, 0, away, bone);
    REQUIRE(far.CarryOf(0x100, 2));
    CHECK(far.CarryOf(0x100, 2)->bone == proto::kCarryNoBone);
}

TEST_CASE("carry: let go when out of his hands, gone, or Link unseen") {
    std::vector<CoopCarry::Candidate> pot{Pot(0x100, {0.0f, 0.2f, 0.7f})};
    CoopCarry::Lifter lifter;
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Ready_Pod"), pot, 0);
    REQUIRE(lifter.Held());
    // Knocked out of his hands (no command read): three walks out of the box.
    pot[0].position = {0.0f, 0.0f, 4.0f};
    lifter.Update("S", LinkAt({0, 0, 0}, "Damage_S"), pot, 33 * kMs);
    lifter.Update("S", LinkAt({0, 0, 0}, "Damage_S"), pot, 66 * kMs);
    CHECK(lifter.Held());
    lifter.Update("S", LinkAt({0, 0, 0}, "Damage_S"), pot, 99 * kMs);
    CHECK_FALSE(lifter.Held());
    auto events = lifter.TakeEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[1].why == CoopCarry::Release::lost);

    // Broken in his hands (it left the walk).
    pot[0].position = {0.0f, 0.2f, 0.7f};
    pot[0].address = 0x500;
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Ready_Pod"), pot, 0);
    REQUIRE(lifter.Held());
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Up_Pod"), {}, 33 * kMs);
    CHECK_FALSE(lifter.Held());
    events = lifter.TakeEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[1].why == CoopCarry::Release::gone);

    // Our Link not in the walk (a load).
    pot[0].address = 0x600;
    lifter.Update("S", LinkAt({0, 0, 0}, "Lift_Ready_Pod"), pot, 0);
    REQUIRE(lifter.Held());
    lifter.Update("S", std::nullopt, pot, 33 * kMs);
    CHECK_FALSE(lifter.Held());
    events = lifter.TakeEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[1].why == CoopCarry::Release::no_link);
}

TEST_CASE("carry: a scene change while carrying lets go and drops every claim") {
    std::vector<CoopCarry::Candidate> pot{Pot(0x100, {0.0f, 0.2f, 0.7f})};
    CoopCarry::Lifter lifter;
    lifter.Update("KokiriForest_Child", LinkAt({0, 0, 0}, "Lift_Ready_Pod"), pot, 0);
    REQUIRE(lifter.Held());
    // Through a loading zone with it: the new scene's walk.
    lifter.Update("LostWoods_Child", LinkAt({0, 0, 0}, "Lift_Up_Pod"), {}, 500 * kMs);
    CHECK_FALSE(lifter.Held());
    CHECK(lifter.Claimed(500 * kMs).empty());  // the old scene's address means nothing here
    const auto events = lifter.TakeEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[1].why == CoopCarry::Release::scene);
}

// ---------------------------------------------------------------- the objects sync

namespace {

using CoopObjects::Seen;
using CoopObjects::Sync;
const std::string kScene = "KakarikoVillage_Child";

Seen PotSeen(std::uint64_t address, std::uint64_t hash, const Vec3& at = {0.0f, 0.0f, 0.0f}) {
    Seen s;
    s.address = address;
    s.name = "Obj_Pot_A_08";
    s.hash = hash;
    s.position = at;
    s.life = proto::ObjectLife{1, 1};
    return s;
}

struct Step {
    std::vector<proto::Message> send;
    std::vector<CoopObjects::LifeWrite> writes;
};

Step TickAt(Sync& sync, std::uint64_t now, const std::vector<Seen>& seen, std::uint8_t self = 1,
            const std::string& scene = kScene) {
    Step step;
    sync.Tick(scene, self, now, seen, step.send, step.writes);
    return step;
}

proto::ObjectCarry HeldBy(std::uint8_t holder, float up = 1.7f) {
    proto::ObjectCarry c;
    c.holder = holder;
    c.offset = {0.0f, up, 0.2f};
    return c;
}

proto::ObjectSample PotSample(const proto::ObjectId& id, std::uint8_t owner, std::uint64_t t,
                              const Vec3& at, std::optional<proto::ObjectCarry> carry,
                              std::uint16_t epoch = 0) {
    proto::ObjectSample s;
    s.player_number = owner;
    s.id = id;
    s.epoch = epoch;
    s.state.time = {t, 1};
    s.state.position = at;
    s.state.life = proto::ObjectLife{1, 1};
    s.state.carry = carry;
    return s;
}

std::uint8_t Flags(const Step& step, const proto::ObjectId& id) {
    for (const auto& m : step.send) {
        if (const auto* p = std::get_if<proto::ObjectPresent>(&m)) {
            for (const auto& e : p->objects) {
                if (e.id == id) return e.flags;
            }
        }
    }
    return 0xff;
}

constexpr std::uint32_t kLive2 = 1u << 2;

}  // namespace

TEST_CASE("carry: another player's pot is attached to his puppet while carried, then follows his throw") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});

    // P2 lifts it at 1000: its samples carry it.
    for (std::uint64_t t = 1000; t <= 1100; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    }
    TickAt(sync, 1110 * kMs, {PotSeen(0x200, 0x31)});
    CHECK(sync.CarriedByOther(0x200, 1110 * kMs));

    // Shown 100 ms late: at 1150 the shown time is the sample of 1050, and
    // the hold started at 1000: weight 0.2 (blending in from the samples).
    std::vector<Sync::CarryPose> carried;
    sync.Carried(1150 * kMs, kLive2, carried);
    REQUIRE(carried.size() == 1);
    CHECK(carried[0].address == 0x200);
    CHECK(carried[0].carry.holder == 2);
    CHECK_FALSE(carried[0].fall);
    CHECK(carried[0].weight == doctest::Approx(0.2f));
    CHECK(Near(carried[0].fallback.position, {0.0f, 1.7f, 0.2f}));
    for (std::uint64_t t = 1150; t <= 1400; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    }
    TickAt(sync, 1410 * kMs, {PotSeen(0x200, 0x31)});
    carried.clear();
    sync.Carried(1450 * kMs, kLive2, carried);
    REQUIRE(carried.size() == 1);
    CHECK(carried[0].weight == doctest::Approx(1.0f));

    // Thrown at 1450: the samples stop carrying it and fly it away.
    const auto flight = [&](std::uint64_t from, std::uint64_t to) {
        for (std::uint64_t t = from; t <= to; t += 50) {
            const float x = 0.01f * static_cast<float>(t - 1450);
            sync.Receive(PotSample(id, 2, t * kMs, {x, 1.5f, 0.2f}, std::nullopt));
        }
    };
    flight(1450, 1500);
    TickAt(sync, 1510 * kMs, {PotSeen(0x200, 0x31)});
    // Up to the throw's sample (shown 100 ms late) it is still in his hands...
    carried.clear();
    sync.Carried(1540 * kMs, kLive2, carried);
    CHECK(carried.size() == 1);
    // ...then it follows the flight: no carry, and Poses places it.
    carried.clear();
    sync.Carried(1600 * kMs, kLive2, carried);
    CHECK(carried.empty());
    flight(1550, 1700);
    TickAt(sync, 1710 * kMs, {PotSeen(0x200, 0x31)});
    CHECK_FALSE(sync.CarriedByOther(0x200, 1710 * kMs));
    std::vector<CoopObjects::PoseWrite> poses;
    sync.Poses(1750 * kMs, poses);
    REQUIRE(poses.size() == 1);
    CHECK(poses[0].position[0] == doctest::Approx(2.0f));
}

TEST_CASE("carry: the holder leaves while carrying: the copy falls here, the new owner's samples take it") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    for (std::uint64_t t = 1000; t <= 1300; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    }
    TickAt(sync, 1310 * kMs, {PotSeen(0x200, 0x31)});
    std::vector<Sync::CarryPose> carried;
    // P2's puppet is gone from here (he left the session, or went through a
    // loading zone with it): the copy is let go, nothing places it.
    sync.Carried(1350 * kMs, 0, carried);
    REQUIRE(carried.size() == 1);
    CHECK(carried[0].fall);

    // The server hands the pot to P3: until P3's first sample the hold keeps
    // P2's carry, so it still falls rather than float in the air.
    sync.Receive(proto::ObjectOwner{id, 3, 1, 0});
    TickAt(sync, 1400 * kMs, {PotSeen(0x200, 0x31, {0.0f, 1.7f, 0.2f})});
    carried.clear();
    sync.Carried(1450 * kMs, 1u << 3, carried);
    REQUIRE(carried.size() == 1);
    CHECK(carried[0].fall);
    // P3's game dropped it: its samples have no carry, the copy follows them.
    sync.Receive(PotSample(id, 3, 1500 * kMs, {0.0f, 0.0f, 0.2f}, std::nullopt, 1));
    TickAt(sync, 1510 * kMs, {PotSeen(0x200, 0x31)});
    carried.clear();
    sync.Carried(1700 * kMs, 1u << 3, carried);
    CHECK(carried.empty());
    std::vector<CoopObjects::PoseWrite> poses;
    sync.Poses(1700 * kMs, poses);
    REQUIRE(poses.size() == 1);
    CHECK(Near(poses[0].position, {0.0f, 0.0f, 0.2f}));
    // A late sample of P2 (an older epoch) cannot put it back in his hands.
    sync.Receive(PotSample(id, 2, 1600 * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2), 0));
    TickAt(sync, 1710 * kMs, {PotSeen(0x200, 0x31)});
    carried.clear();
    sync.Carried(1800 * kMs, kLive2 | (1u << 3), carried);
    CHECK(carried.empty());
}

TEST_CASE("carry: a scene change while another carries forgets the hold") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    sync.Receive(PotSample(id, 2, 1000 * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    TickAt(sync, 1010 * kMs, {PotSeen(0x200, 0x31)});
    std::vector<Sync::CarryPose> carried;
    sync.Carried(1150 * kMs, kLive2, carried);
    REQUIRE(carried.size() == 1);
    // We go to another scene: nothing of the old one is placed.
    TickAt(sync, 1200 * kMs, {}, 1, "KakarikoVillage_Child_Inside");
    carried.clear();
    sync.Carried(1250 * kMs, kLive2, carried);
    CHECK(carried.empty());
    CHECK_FALSE(sync.CarriedByOther(0x200, 1250 * kMs));
}

namespace {

bool AnyNote(const std::vector<std::string>& notes, std::string_view part) {
    return std::any_of(notes.begin(), notes.end(),
                       [part](const std::string& n) { return n.find(part) != std::string::npos; });
}

bool AnyDamage(const Step& step) {
    return std::any_of(step.send.begin(), step.send.end(),
                       [](const proto::Message& m) { return std::holds_alternative<proto::Damage>(m); });
}

Seen PotLife(std::int32_t life, std::int32_t max = 1) {
    Seen s = PotSeen(0x200, 0x31, {0.0f, 1.7f, 0.2f});
    s.life = proto::ObjectLife{life, max};
    return s;
}

}  // namespace

TEST_CASE("carry: a copy in another's puppet's hands keeps a raised life, and a bump or break there is no hit of ours") {
    // Seen in a game (sondas9, levantar-vasija): B's copy of A's pot, drawn in
    // A's puppet's hands, went 1 -> 0 as the puppet started walking; B sent
    // DAMAGE (and WORLD_BREAK) and A's game broke the pot in A's own hands.
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    sync.TakeNotes();
    for (std::uint64_t t = 1000; t <= 1100; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    }
    // The samples are taken in at the end of a tick; the next walk sees the
    // hold: our copy's life goes up to kHeldLife, once.
    auto step = TickAt(sync, 1110 * kMs, {PotLife(1)});
    CHECK(step.writes.empty());
    step = TickAt(sync, 1130 * kMs, {PotLife(1)});
    REQUIRE(step.writes.size() == 1);
    CHECK(step.writes[0].address == 0x200);
    CHECK(step.writes[0].life == Sync::kHeldLife);
    CHECK(step.writes[0].max == Sync::kHeldLife);  // the game clamps life to its max
    CHECK(AnyNote(sync.TakeNotes(), "in P2's puppet's hands here: life and max kept at " +
                                        std::to_string(Sync::kHeldLife) + " while held"));
    constexpr std::int32_t kH = Sync::kHeldLife;
    step = TickAt(sync, 1160 * kMs, {PotLife(kH, kH)});
    CHECK(step.writes.empty());
    // A sample of the owner's life while held does not undo the raise.
    sync.Receive(PotSample(id, 2, 1150 * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    step = TickAt(sync, 1210 * kMs, {PotLife(kH, kH)});
    CHECK(step.writes.empty());
    // A bump of the hands: put back, no DAMAGE now or after the summing time.
    step = TickAt(sync, 1260 * kMs, {PotLife(kH - 1, kH)});
    REQUIRE(step.writes.size() == 1);
    CHECK(step.writes[0].life == Sync::kHeldLife);
    CHECK(AnyNote(sync.TakeNotes(), "took a bump of 1 in P2's puppet's hands: life put back"));
    CHECK_FALSE(AnyDamage(step));
    // SONDAS.md round 6: the game's own break hit (1,000,000,000, the copy landing on the speed its body kept)
    // leaves life over 0: put back the same, no break, no DAMAGE.
    static_assert(Sync::kHeldLife > 1'000'000'000);
    step = TickAt(sync, 1300 * kMs, {PotLife(kH - 1'000'000'000, kH)});
    REQUIRE(step.writes.size() == 1);
    CHECK(step.writes[0].life == Sync::kHeldLife);
    CHECK(AnyNote(sync.TakeNotes(), "took a bump of 1000000000 in P2's puppet's hands: life put back"));
    CHECK_FALSE(AnyDamage(step));
    CHECK_FALSE(AnyDamage(TickAt(sync, 1500 * kMs, {PotLife(kH, kH)})));
    // Thrown: the samples stop carrying it; the owner's life comes back once.
    for (std::uint64_t t = 1550; t <= 1700; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.5f, 1.5f, 0.2f}, std::nullopt));
    }
    step = TickAt(sync, 1710 * kMs, {PotLife(kH, kH)});
    CHECK(step.writes.empty());
    // SONDAS.md round 6: thrown, it keeps the raise kThrownKeepUs (its own landing broke it in sondas18r6,
    // 1.2 s before the holder's break came): the landing's break hit is put back.
    step = TickAt(sync, 1760 * kMs, {PotLife(kH, kH)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "just out of P2's puppet's hands (thrown): life kept raised"));
    step = TickAt(sync, 2000 * kMs, {PotLife(kH - 1'000'000'000, kH)});
    REQUIRE(step.writes.size() == 1);
    CHECK(step.writes[0].life == Sync::kHeldLife);
    CHECK(AnyNote(sync.TakeNotes(), "took a bump of 1000000000 just out of P2's puppet's hands (thrown)"));
    CHECK_FALSE(AnyDamage(step));
    // No break of the holder's came: after kThrownKeepUs the owner's life comes back once.
    bool back = false;
    for (std::uint64_t t = 2050; t <= 1700 + Sync::kThrownKeepUs / kMs + 200 && !back; t += 50) {
        step = TickAt(sync, t * kMs, {PotLife(kH, kH)});
        if (!step.writes.empty()) {
            back = true;
            CHECK(step.writes[0].life == 1);
            CHECK(step.writes[0].max == 1);
        }
        CHECK_FALSE(AnyDamage(step));
    }
    CHECK(back);
    CHECK(AnyNote(sync.TakeNotes(), "out of the puppet's hands: life back to 1 of 1"));
}

TEST_CASE("carry: a copy just thrown that the holder's break reaches breaks then, no hit of ours") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    for (std::uint64_t t = 1000; t <= 1100; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    }
    TickAt(sync, 1110 * kMs, {PotLife(1)});
    REQUIRE(TickAt(sync, 1130 * kMs, {PotLife(1)}).writes.size() == 1);  // raised
    constexpr std::int32_t kH = Sync::kHeldLife;
    for (std::uint64_t t = 1150; t <= 1300; t += 50) {
        sync.Receive(PotSample(id, 2, t * kMs, {0.5f, 1.5f, 0.2f}, std::nullopt));
    }
    TickAt(sync, 1310 * kMs, {PotLife(kH, kH)});
    TickAt(sync, 1460 * kMs, {PotLife(kH, kH)});
    sync.TakeNotes();
    // The holder's WORLD_BREAK wrote 0 (coop_world): it breaks now, and it is not ours to send.
    auto step = TickAt(sync, 2400 * kMs, {PotLife(0, kH)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "broke just out of P2's puppet's hands (thrown) here anyway (life 0, raised)"));
    for (std::uint64_t t = 2450; t <= 3000; t += 50) {
        CHECK_FALSE(AnyDamage(TickAt(sync, t * kMs, {})));
    }
}

TEST_CASE("carry: a copy that breaks in the puppet's hands anyway is no hit of ours, a free one is") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    sync.Receive(PotSample(id, 2, 1000 * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    TickAt(sync, 1010 * kMs, {PotLife(1)});
    REQUIRE(TickAt(sync, 1060 * kMs, {PotLife(1)}).writes.size() == 1);  // raised
    sync.TakeNotes();
    // It reads 0 in the hands all the same.
    auto step = TickAt(sync, 1110 * kMs, {PotLife(0, Sync::kHeldLife)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "broke in P2's puppet's hands here anyway (life 0, raised)"));
    for (std::uint64_t t = 1150; t <= 1600; t += 50) {
        CHECK_FALSE(AnyDamage(TickAt(sync, t * kMs, {})));
    }
    // A pot of P2's nobody holds, hit here: DAMAGE as before.
    Sync free;
    TickAt(free, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId fid = *free.IdOf(0x200);
    free.Receive(proto::ObjectOwner{fid, 2, 0, 0});
    TickAt(free, 10 * kMs, {PotSeen(0x200, 0x31)});
    free.Receive(PotSample(fid, 2, 1000 * kMs, {0.0f, 1.7f, 0.2f}, std::nullopt));
    TickAt(free, 1010 * kMs, {PotLife(1)});
    bool damage = AnyDamage(TickAt(free, 1060 * kMs, {PotLife(0)}));
    for (std::uint64_t t = 1100; t <= 1400 && !damage; t += 50) {
        damage = AnyDamage(TickAt(free, t * kMs, {PotLife(0)}));
    }
    CHECK(damage);
}

TEST_CASE("carry: a raise the game does not keep is written once, then only the break is kept back") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    TickAt(sync, 10 * kMs, {PotSeen(0x200, 0x31)});
    sync.Receive(PotSample(id, 2, 1000 * kMs, {0.0f, 1.7f, 0.2f}, HeldBy(2)));
    TickAt(sync, 1010 * kMs, {PotLife(1)});
    REQUIRE(TickAt(sync, 1060 * kMs, {PotLife(1)}).writes.size() == 1);  // raised
    sync.TakeNotes();
    // sondas12: a life of 100 written alone read back 1 of 1.
    auto step = TickAt(sync, 1110 * kMs, {PotLife(1, 1)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "the raise did not take (1 of 1): fallback, its break here is not sent"));
    CHECK(TickAt(sync, 1160 * kMs, {PotLife(1, 1)}).writes.empty());
    step = TickAt(sync, 1210 * kMs, {PotLife(0, 1)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "anyway (life 0, the raise did not take)"));
    for (std::uint64_t t = 1250; t <= 1700; t += 50) {
        const auto later = TickAt(sync, t * kMs, {});
        CHECK_FALSE(AnyDamage(later));
    }
}

TEST_CASE("carry: the holder's game takes no DAMAGE for what its Link holds") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 1, 0, 0});
    sync.SetCarry({0x200}, {{0x200, HeldBy(1)}});
    TickAt(sync, 10 * kMs, {PotLife(1)});
    sync.TakeNotes();
    proto::Damage d;
    d.player_number = 2;
    d.id = id;
    d.amount = 1;
    sync.Receive(d);
    auto step = TickAt(sync, 20 * kMs, {PotLife(1)});
    CHECK(step.writes.empty());
    CHECK(AnyNote(sync.TakeNotes(), "P2's DAMAGE of 1 to Obj_Pot_A_08, which our Link holds: not taken"));
    // Let go (and the claim's after-time over): a DAMAGE is taken again.
    sync.SetCarry({}, {});
    sync.Receive(d);
    step = TickAt(sync, 30 * kMs, {PotLife(1)});
    REQUIRE(step.writes.size() == 1);
    CHECK(step.writes[0].life == 0);
}

TEST_CASE("carry: our lift claims the pot, frees our copy, and once ours its samples carry it") {
    Sync sync;
    TickAt(sync, 0, {PotSeen(0x200, 0x31)});
    const proto::ObjectId id = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{id, 2, 0, 0});
    auto step = TickAt(sync, 2100 * kMs, {PotSeen(0x200, 0x31)});
    REQUIRE(Flags(step, id) != 0xff);
    CHECK((Flags(step, id) & proto::object_presence::targeting) == 0);
    sync.Receive(PotSample(id, 2, 2100 * kMs, {0.0f, 0.0f, 0.0f}, std::nullopt));
    TickAt(sync, 2110 * kMs, {PotSeen(0x200, 0x31)});
    std::vector<CoopObjects::PoseWrite> poses;
    sync.Poses(2200 * kMs, poses);
    REQUIRE(poses.size() == 1);

    // Our Link lifts it: flagged targeting at once, our copy left to our game.
    sync.SetCarry({0x200}, {});
    CHECK(sync.CarryFreed(0x200));
    step = TickAt(sync, 2200 * kMs, {PotSeen(0x200, 0x31, {0.0f, 1.0f, 0.5f})});
    CHECK((Flags(step, id) & proto::object_presence::targeting) != 0);
    poses.clear();
    sync.Poses(2300 * kMs, poses);
    CHECK(poses.empty());
    std::vector<Sync::CarryPose> carried;
    sync.Carried(2300 * kMs, ~0u, carried);
    CHECK(carried.empty());

    // The server hands it to us: our samples now say it is in our hands.
    sync.Receive(proto::ObjectOwner{id, 1, 1, 0});
    sync.SetCarry({0x200}, {{0x200, HeldBy(1)}});
    step = TickAt(sync, 2800 * kMs, {PotSeen(0x200, 0x31, {0.0f, 1.7f, 0.2f})});
    std::optional<proto::ObjectSample> sent;
    for (const auto& m : step.send) {
        if (const auto* s = std::get_if<proto::ObjectSample>(&m); s && s->id == id) sent = *s;
    }
    REQUIRE(sent);
    REQUIRE(sent->state.carry);
    CHECK(sent->state.carry->holder == 1);
    CHECK(sent->epoch == 1);
    // Thrown: the carry is gone from the next sample (at once: a change).
    sync.SetCarry({0x200}, {});
    step = TickAt(sync, 2860 * kMs, {PotSeen(0x200, 0x31, {0.5f, 1.6f, 0.4f})});
    sent.reset();
    for (const auto& m : step.send) {
        if (const auto* s = std::get_if<proto::ObjectSample>(&m); s && s->id == id) sent = *s;
    }
    REQUIRE(sent);
    CHECK_FALSE(sent->state.carry);
    // The claim let go: no flag, our copy placed by its owner again if it changes hands.
    sync.SetCarry({}, {});
    CHECK_FALSE(sync.CarryFreed(0x200));
}

TEST_CASE("carry: bomb flowers are shared only while the switch is on") {
    CoopCarry::SetShareFlowers(false);
    CHECK_FALSE(CoopObjects::IsShared("Obj_BombFlower_A_01"));
    CoopCarry::SetShareFlowers(true);
    CHECK(CoopObjects::IsShared("Obj_BombFlower_A_01"));
    CHECK_FALSE(CoopObjects::IsShared("Obj_BombFlower_A_02"));
    CoopCarry::SetShareFlowers(false);
}

// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): the bone matrices and the
// carried copy's hold, each with its check line.
#include "coop_check_visual.h"

TEST_CASE("carry (riesgos-visual): the bone path says ok once it reads, fallback when it does not") {
    std::vector<std::string> lines;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { lines.push_back(line); });
    FakeGuest g;
    const std::uint64_t puppet = 0x40000000;
    const std::uint64_t world = MakeActor(g, puppet, {20.0f, 0.0f, 30.0f}, kQuarter, 64);
    PutBone(g, world, 12, kQuarter, {20.0f, 1.7f, 30.0f});
    proto::ObjectCarry c;
    c.holder = 2;
    c.offset = {0.0f, 1.9f, 0.2f};
    c.rotation = CoopCarry::QuatOfRows(kIdentity);
    c.bone = 12;
    c.bone_offset = {0.0f, 0.2f, 0.0f};
    c.bone_rotation = CoopCarry::QuatOfRows(kIdentity);
    CoopCarry::Anchor used = CoopCarry::Anchor::none;
    REQUIRE(CoopCarry::Attached(g, puppet, c, true, &used));
    REQUIRE(CoopCarry::Attached(g, puppet, c, true, &used));
    REQUIRE(lines.size() == 1);  // once
    CHECK(lines[0] == "coop: check: carry bone ok the puppet's bone 12 reads as a rigid frame near it: lifted things "
                      "follow its hands (4x4 by rows)");
    PutBone(g, world, 12, kQuarter, {300.0f, 0.0f, 0.0f});
    REQUIRE(CoopCarry::Attached(g, puppet, c, true, &used));
    CHECK(used == CoopCarry::Anchor::actor);
    REQUIRE(lines.size() == 2);
    CHECK(lines[1] == "coop: check: carry bone fallback the puppet's bone 12 does not read as a rigid frame near it: "
                      "lifted things follow its actor frame instead");
    // The switch off asks no bone: no line.
    REQUIRE(CoopCarry::Attached(g, puppet, c, false, &used));
    CHECK(lines.size() == 2);
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

TEST_CASE("carry (riesgos-visual): a copy that stays put is ok, one that falls between writes is the fallback, then the fix is judged") {
    std::vector<std::string> lines;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { lines.push_back(line); });
    {
        CoopCarry::HoldWatch w;
        const Vec3 hand{1.0f, 1.5f, 2.0f};
        for (int k = 0; k <= CoopCarry::HoldWatch::kJudgeFrames; ++k) {
            w.Note(7, {1.0f, 1.499f, 2.0f}, hand);  // a millimetre of jitter
        }
        CHECK(w.Judged());
        CHECK_FALSE(w.Falling());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].rfind("coop: check: carry hold ok carried copies held still between our writes", 0) == 0);
    }
    CoopVCheck::ResetForTest();
    lines.clear();
    {
        // Issue 169's way: each frame the body has fallen 5 cm more under our hand.
        CoopCarry::HoldWatch w;
        const Vec3 hand{1.0f, 1.5f, 2.0f};
        for (int k = 0; k <= CoopCarry::HoldWatch::kJudgeFrames; ++k) {
            w.Note(7, {1.0f, 1.45f, 2.0f}, hand);
        }
        CHECK(w.Falling());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].rfind("coop: check: carry hold fallback a carried copy fell between our writes in 60 of 60 "
                             "frames (up to 0.050 a frame)",
                             0) == 0);
        // With the pending place written it holds: the fix's ok line.
        for (int k = 0; k < CoopCarry::HoldWatch::kJudgeFrames; ++k) {
            w.Note(7, hand, hand);
        }
        REQUIRE(lines.size() == 2);
        CHECK(lines[1].rfind("coop: check: carry hold fix ok with the pending place written", 0) == 0);
        // Judged twice: nothing more.
        for (int k = 0; k < 3 * CoopCarry::HoldWatch::kJudgeFrames; ++k) {
            w.Note(7, {1.0f, 1.0f, 2.0f}, hand);
        }
        CHECK(lines.size() == 2);
    }
    {
        // A holder who jumps far sideways in a frame is not a fall; a forgotten copy starts again.
        CoopCarry::HoldWatch w;
        w.Note(9, {0, 0, 0}, {0, 1, 0});
        w.Note(9, {10.0f, -5.0f, 0.0f}, {10.0f, 1.0f, 0.0f});
        w.Forget(9);
        CHECK_FALSE(w.Judged());
    }
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

// ---------------------------------------------------------------- a still double while carried

namespace {

// An actor whose main rigid body (actor+0x270 -> +0x50 -> +0x20 -> +0x190)
// has these flags and speeds; returns the body's address.
std::uint64_t MakeBody(FakeGuest& g, std::uint64_t actor, std::uint64_t flags, const Vec3& linear,
                       const Vec3& angular = {}) {
    const std::uint64_t parts = actor + 0x1000, physics = actor + 0x2000, root = actor + 0x3000,
                        body = actor + 0x4000;
    g.Map(actor, 0x600);
    for (std::uint64_t m : {parts, physics, root, body}) g.Map(m, 0x400);
    g.Put(actor + CoopCarry::still::kRootPath[0], parts);
    g.Put(parts + CoopCarry::still::kRootPath[1], physics);
    g.Put(physics + CoopCarry::still::kRootPath[2], root);
    g.Put(root + CoopCarry::still::kRootBody, body);
    g.Put(body + CoopCarry::still::kBodyFlags, flags);
    g.Put(body + CoopCarry::still::kBodyLinear, linear);
    g.Put(body + CoopCarry::still::kBodyAngular, angular);
    return body;
}

}  // namespace

TEST_CASE("carry (levantar-vasija): the copy's main rigid body is read through the physics root") {
    FakeGuest g;
    const std::uint64_t actor = 0x50000000;
    const std::uint64_t body = MakeBody(g, actor, 0x8, {0.0f, -33.0f, 0.5f}, {0.0f, 1.0f, 0.0f});
    const auto b = CoopCarry::ReadBody(g, actor);
    REQUIRE(b);
    CHECK(b->address == body);
    CHECK(b->flags == 0x8);
    CHECK(Near(b->linear, {0.0f, -33.0f, 0.5f}));
    CHECK(Near(b->angular, {0.0f, 1.0f, 0.0f}));
    // A link of the path that is 0, a speed that is not finite, an actor not mapped: nothing.
    g.Put(actor + 0x4000 + CoopCarry::still::kBodyLinear, Vec3{NAN, 0.0f, 0.0f});
    CHECK_FALSE(CoopCarry::ReadBody(g, actor));
    g.Put(actor + 0x3000 + CoopCarry::still::kRootBody, std::uint64_t{0});
    CHECK_FALSE(CoopCarry::ReadBody(g, actor));
    CHECK_FALSE(CoopCarry::ReadBody(g, 0x70000000));
    CHECK_FALSE(CoopCarry::ReadBody(g, 0));
}

TEST_CASE("carry (levantar-vasija): a copy in a puppet's hands has its speeds zeroed by the game's calls, then is let go") {
    std::vector<std::string> lines;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { lines.push_back(line); });
    FakeGuest g;
    const std::uint64_t main = 0x8000000;
    const std::uint64_t zero = 0x9000030;
    const std::uint64_t actor = 0x50000000;
    const std::uint64_t body = MakeBody(g, actor, 0, {0.0f, -33.0f, 0.0f});
    using Cell = CoopCarry::Still::Cell;
    CoopCarry::Still s;
    s.Frame();
    auto calls = s.Hold(g, main, actor, zero, Cell::ready);
    REQUIRE(calls.size() == 2);
    // Linear first, then the turn: the game's own setters, the body and the zero cell.
    CHECK(calls[0].function == main + 0x12dbe0);
    CHECK(calls[0].x == std::array<std::uint64_t, 3>{body, zero, 0});
    CHECK(calls[1].function == main + 0x12b790);
    CHECK(calls[1].x[0] == body);
    CHECK(calls[1].x[1] == zero);
    CHECK(s.Holding(actor));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].rfind("coop: check: carry still ok copy 0x50000000 in a puppet's hands: its body 0x50004000 "
                         "(flags 0) had linear speed (0.00, -33.00, 0.00); zeroed with the game's own calls "
                         "after each place",
                         0) == 0);
    // Each frame in the hands: the calls again, the line once.
    for (int k = 0; k < 9; ++k) {
        s.Frame();
        CHECK(s.Hold(g, main, actor, zero, Cell::ready).size() == 2);
    }
    CHECK(lines.size() == 1);
    CHECK(s.TakeStillFrames() == 10);
    CHECK(s.TakeStillFrames() == 0);
    // One frame the cave's budget skipped it: still held. Then thrown: let go after kLetGoFrames.
    s.Frame();  // closes the last frame it was placed in
    s.Frame();
    CHECK(s.TakeLines().empty());
    CHECK(s.Holding(actor));
    for (int k = 1; k < CoopCarry::Still::kLetGoFrames; ++k) s.Frame();
    CHECK_FALSE(s.Holding(actor));
    const auto let_go = s.TakeLines();
    REQUIRE(let_go.size() == 1);
    CHECK(let_go[0] == "coop: carry: copy 0x50000000 out of the puppet's hands: its own physics again (held "
                       "still 10 frames)");
    CHECK(s.TakeLines().empty());
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

TEST_CASE("carry (levantar-vasija): a body that keeps its turn gets the linear call only") {
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([](const std::string&) {});
    FakeGuest g;
    const std::uint64_t actor = 0x50000000;
    MakeBody(g, actor, CoopCarry::still::kAngularKept, {});
    CoopCarry::Still s;
    const auto calls = s.Hold(g, 0x8000000, actor, 0x9000030, CoopCarry::Still::Cell::ready);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == 0x8000000 + CoopCarry::still::kSetLinearVelocity);
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

TEST_CASE("carry (levantar-vasija): the fallbacks: no body, a body that keeps its speed, no zero cell, the switch off") {
    std::vector<std::string> lines;
    CoopVCheck::SetSinkForTest([&](const std::string& line) { lines.push_back(line); });
    using Cell = CoopCarry::Still::Cell;
    const std::uint64_t main = 0x8000000, zero = 0x9000030, actor = 0x50000000;
    {
        CoopVCheck::ResetForTest();
        lines.clear();
        FakeGuest g;
        g.Map(actor, 0x600);  // no physics component
        CoopCarry::Still s;
        CHECK(s.Hold(g, main, actor, zero, Cell::ready).empty());
        CHECK_FALSE(s.Holding(actor));
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == "coop: check: carry still fallback copy 0x50000000 in a puppet's hands: no physics body "
                          "read at actor+0x270+0x50+0x20+0x190; placed with the setter and its pending place (it "
                          "may fall between places)");
    }
    {
        CoopVCheck::ResetForTest();
        lines.clear();
        FakeGuest g;
        MakeBody(g, actor, CoopCarry::still::kLinearKept, {});
        CoopCarry::Still s;
        CHECK(s.Hold(g, main, actor, zero, Cell::ready).empty());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find("its body 0x50004000 has flags 0x40000000: bit 30 keeps its linear speed; placed with "
                            "the setter and its pending place") != std::string::npos);
    }
    {
        CoopVCheck::ResetForTest();
        lines.clear();
        FakeGuest g;
        MakeBody(g, actor, 0, {});
        CoopCarry::Still s;
        // The block on its way: nothing this frame, and no line.
        CHECK(s.Hold(g, main, actor, 0, Cell::coming).empty());
        CHECK(lines.empty());
        // The block could not be made: the fallback, said.
        CHECK(s.Hold(g, main, actor, 0, Cell::none).empty());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find("no zero cell for the speed calls") != std::string::npos);
        // Once the block is there, the still path (both lines said, each once).
        CHECK(s.Hold(g, main, actor, zero, Cell::ready).size() == 2);
        REQUIRE(lines.size() == 2);
        CHECK(lines[1].rfind("coop: check: carry still ok", 0) == 0);
    }
    {
        CoopVCheck::ResetForTest();
        lines.clear();
        FakeGuest g;
        MakeBody(g, actor, 0, {});
        CoopCarry::Still s;
        s.SetOn(false);
        CHECK(s.Hold(g, main, actor, zero, Cell::ready).empty());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find("the zeroing is off (LICHII_COOP_CARRY_STILL=0)") != std::string::npos);
        // A copy that falls back is not held still: no "out of the hands" line for it later.
        for (int k = 0; k < 2 * CoopCarry::Still::kLetGoFrames; ++k) s.Frame();
        CHECK(s.TakeLines().empty());
    }
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
