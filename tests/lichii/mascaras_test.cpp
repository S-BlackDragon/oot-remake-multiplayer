// Tests for masks, the fishing line and the hookshot chain on puppets (ORM,
// lichii/hook/coop_mascaras.cpp, oot-remake docs/DETALLES_MARIONETA.md).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop_mascaras.h"
#include "coop_puppet.h"
#include "coop_spawn.h"
#include "skeleton_fakes.hpp"

namespace {

using CoopMascaras::Local;
using CoopMascaras::Mat3;
using CoopMascaras::Rows;
using CoopMascaras::Vec3;
using SkeletonFakes::B;
using SkeletonFakes::Bone;

class FakeGuest : public CoopPuppet::Guest {
public:
    void Map(std::uint64_t address, std::size_t size) {
        for (std::uint64_t p = address & ~0xfffULL; p < address + size; p += 0x1000) {
            pages_[p];
        }
    }
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) return false;
            o[i] = it->second[(address + i) & 0xfff];
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) return false;
            it->second[(address + i) & 0xfff] = p[i];
        }
        return true;
    }
    template <typename T>
    T Get(std::uint64_t address) {
        T v{};
        REQUIRE(Read(address, &v, sizeof(T)));
        return v;
    }
    template <typename T>
    void Set(std::uint64_t address, const T& v) {
        REQUIRE(Write(address, &v, sizeof(T)));
    }

private:
    std::map<std::uint64_t, std::array<std::uint8_t, 0x1000>> pages_;
};

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool NearV(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return Near(a[0], b[0], eps) && Near(a[1], b[1], eps) && Near(a[2], b[2], eps);
}

// 90 degrees about z (x to y) and about y (x to -z), column vectors.
constexpr Mat3 kRz{0, -1, 0, 1, 0, 0, 0, 0, 1};
constexpr Mat3 kRy{0, 0, 1, 0, 1, 0, -1, 0, 0};

Local Rotated(const Mat3& m, Rows rows, Vec3 t = {}) {
    Local l;
    CoopMascaras::SetRotation(l, m, rows);
    l.translation[0] = t[0];
    l.translation[1] = t[1];
    l.translation[2] = t[2];
    return l;
}

Local Moved(Vec3 t) {
    Local l;
    l.translation[0] = t[0];
    l.translation[1] = t[1];
    l.translation[2] = t[2];
    return l;
}

std::vector<Local> LocalsOf(const std::vector<Bone>& bones) {
    std::vector<Local> out;
    for (const auto& b : bones) out.push_back(b.local);
    return out;
}

std::vector<std::int16_t> ParentsOf(const std::vector<Bone>& bones) {
    std::vector<std::int16_t> out;
    for (const auto& b : bones) out.push_back(b.parent);
    return out;
}

// Link's spine as Player_Child has it at rest, the spine turned up by
// Spine_1 (x to y), stored by `rows`.
std::vector<Bone> Spine(Rows rows) {
    // Columns x' = (0, 1, 0), y' = (0, 0, 1), z' = (1, 0, 0).
    const Mat3 up{0, 0, 1, 1, 0, 0, 0, 1, 0};
    std::vector<Bone> b{B("Root", -1), B("Skl_Root", 0, {0, 0.584f, 0}), B("Spine_1", 1),
                        B("Spine_2", 2, {0.108f, 0, 0}), B("Neck", 3, {0.123f, -0.003f, 0}),
                        B("Head", 4, {0.102f, 0, 0}), B("Head_2", 5)};
    b[2].local = Rotated(up, rows);
    return b;
}

}  // namespace

TEST_CASE("mascaras: a rotation stored either way reads back, and frames go through the parents") {
    for (const Rows rows : {Rows::axes, Rows::matrix}) {
        const Local l = Rotated(kRz, rows);
        CHECK(CoopMascaras::ToParent(l, rows) == kRz);
        // axes: row 0 is the child's x axis in the parent, y here.
        CHECK(Near(l.rows[1], rows == Rows::axes ? 1.0f : -1.0f));
        const std::vector<Local> locals{Moved({0, 1, 0}), Rotated(kRz, rows, {1, 0, 0}),
                                        Moved({1, 0, 0})};
        const std::vector<std::int16_t> parents{-1, 0, 1};
        const auto f = CoopMascaras::BoneFrame(locals, parents, 2, rows);
        CHECK(NearV(f.origin, {1, 2, 0}));
        CHECK(f.rot == kRz);
        // A loop in the parents ends the walk.
        CHECK(std::isfinite(CoopMascaras::BoneFrame(locals, {1, 0, 1}, 2, rows).origin[0]));
    }
    // Scale goes into the frame: a child point is scaled first.
    Local s = Moved({0, 0, 0});
    s.scale[0] = 2.0f;
    const auto f = CoopMascaras::BoneFrame({s, Moved({1, 0, 0})}, {-1, 0}, 1, Rows::axes);
    CHECK(NearV(f.origin, {2, 0, 0}));
}

TEST_CASE("mascaras: a Link's spine tells how the rows of a rotation are stored") {
    for (const Rows rows : {Rows::axes, Rows::matrix}) {
        const auto bones = Spine(rows);
        const auto found = CoopMascaras::DetectRows(LocalsOf(bones), ParentsOf(bones), 5, 1);
        REQUIRE(found);
        CHECK(*found == rows);
    }
    // A spine lying down (crawling) says nothing.
    auto flat = Spine(Rows::axes);
    flat[2].local = Local{};
    CHECK_FALSE(CoopMascaras::DetectRows(LocalsOf(flat), ParentsOf(flat), 5, 1));
    CHECK_FALSE(CoopMascaras::DetectRows(LocalsOf(flat), ParentsOf(flat), 50, 1));

    CoopMascaras::Reset();
    CHECK(CoopMascaras::CurrentRows() == CoopMascaras::kDefaultRows);
    CHECK(CoopMascaras::NoteRows(Rows::matrix));
    CHECK_FALSE(CoopMascaras::NoteRows(Rows::matrix));
    CHECK(CoopMascaras::CurrentRows() == Rows::matrix);
    CoopMascaras::Reset();
}

TEST_CASE("mascaras: a chain's locals put each bone on its point, along the chain, either way") {
    for (const Rows rows : {Rows::axes, Rows::matrix}) {
        // Skl_Root turned as the hookshot tip's (x to -z), the links from it.
        const Vec3 end{1, 2, 2};  // 3 away
        const auto points = CoopMascaras::HookPoints({0, 0, 0}, end, 0.383f, 10);
        REQUIRE(points.size() == 10);
        CHECK(NearV(points[0], {0.383f / 3, 0.766f / 3, 0.766f / 3}));
        CHECK(NearV(points[6], {2.681f / 3, 5.362f / 3, 5.362f / 3}));
        CHECK(NearV(points[7], end));  // past the hand: folded onto it
        CHECK(NearV(points[8], end));
        CHECK(NearV(points[9], end));

        std::vector<Local> locals{Local{}, Rotated(kRy, rows)};
        std::vector<std::int16_t> parents{-1, 0};
        for (int k = 0; k < 10; ++k) {
            locals.push_back(Moved({0.383f, 0, 0}));
            parents.push_back(static_cast<std::int16_t>(k == 0 ? 1 : 1 + k));
        }
        const auto parent = CoopMascaras::BoneFrame(locals, parents, 1, rows);
        const auto chain = CoopMascaras::ChainLocals(parent, points);
        REQUIRE(chain.size() == 10);
        for (std::size_t k = 0; k < chain.size(); ++k) {
            CoopMascaras::Store(locals[2 + k], chain[k], rows);
        }
        for (std::size_t k = 0; k < 10; ++k) {
            const auto f = CoopMascaras::BoneFrame(locals, parents, 2 + k, rows);
            CHECK_MESSAGE(NearV(f.origin, points[k]), k);
            // The x axis runs along the chain (folded ones keep the last way).
            const Vec3 x{f.rot[0], f.rot[3], f.rot[6]};
            CHECK_MESSAGE(NearV(x, {1.0f / 3, 2.0f / 3, 2.0f / 3}), k);
        }
    }
}

TEST_CASE("mascaras: the fishing line runs from the lure to the rod and sags across") {
    const auto line = CoopMascaras::LinePoints({0, 0, 0}, {4, 0, 0}, {0, -1, 0}, 5, 0.05f);
    REQUIRE(line.size() == 5);
    CHECK(NearV(line[0], {0, 0, 0}));
    CHECK(NearV(line[4], {4, 0, 0}));
    CHECK(NearV(line[2], {2, -0.2f, 0}));
    // Straight up from the lure: nothing across, no sag.
    const auto up = CoopMascaras::LinePoints({0, 0, 0}, {0, 4, 0}, {0, -1, 0}, 5, 0.05f);
    CHECK(NearV(up[2], {0, 2, 0}));
    CHECK(CoopMascaras::LinePoints({0, 0, 0}, {1, 0, 0}, {0, -1, 0}, 0, 0.05f).empty());
}

TEST_CASE("mascaras: a placed bone carries its transform bits, the hierarchy bits cleared") {
    namespace ma = CoopMascaras::addr;
    Local l;
    CHECK(CoopMascaras::TransformBits(l) ==
          (ma::kScaleOne | ma::kScaleUniform | ma::kScaleVolumeOne | ma::kRotZero | ma::kTransZero));
    l.flags = 0x1 | ma::kHierarchyBits | ma::kRotZero | ma::kTransZero;
    CoopMascaras::ChainBone b;
    b.rot = kRz;
    b.t = {0.5f, 0, 0};
    CoopMascaras::Store(l, b, Rows::axes);
    CHECK((l.flags & 0x1u) == 0x1u);  // other bits kept
    CHECK((l.flags & ma::kHierarchyBits) == 0);
    CHECK((l.flags & (ma::kRotZero | ma::kTransZero)) == 0);
    CHECK((l.flags & ma::kScaleOne) != 0);
    CHECK(Near(l.translation[0], 0.5f));
}

TEST_CASE("mascaras: a skeleton's names and parents come from its FSKL") {
    CoopMascaras::Reset();
    FakeGuest g;
    const std::uint64_t actor = 0x600000000;
    SkeletonFakes::MakeActor(g, actor, {0, 0, 0});
    const auto bones = Spine(Rows::axes);
    const std::uint64_t local = SkeletonFakes::MakeSkeleton(g, actor, 0x610000000, "Player_Child", bones);
    const auto s = CoopMascaras::ReadSkeleton(g, actor);
    REQUIRE(s);
    CHECK(s->model == "Player_Child");
    CHECK(s->local == local);
    CHECK(s->count == bones.size());
    CHECK(s->names[5] == "Head");
    CHECK(s->parents[5] == 4);
    CHECK(s->parents[0] == -1);
    CHECK(s->Find("Neck") == 4);
    CHECK(s->Find("Weapon_R") == -1);
    std::vector<Local> locals;
    REQUIRE(CoopMascaras::ReadLocals(g, *s, locals));
    CHECK(Near(locals[1].translation[1], 0.584f));

    // The FSKL's count must be the skeleton's: else nothing (but the quick read).
    const std::uint64_t fskl = 0x610000000 + 0x6000;
    g.Set(fskl + CoopMascaras::addr::kFsklBoneCount, std::uint16_t{3});
    CHECK_FALSE(CoopMascaras::ReadSkeleton(g, actor));
    CHECK(CoopMascaras::ReadSkeleton(g, actor, false));
    g.Set(fskl, std::array<char, 4>{'X', 'X', 'X', 'X'});
    CHECK_FALSE(CoopMascaras::ReadSkeleton(g, actor));
}

namespace {

constexpr std::uint64_t kPuppet = 0x700000000;
constexpr std::uint64_t kMask = 0x710000000;
constexpr std::uint64_t kEu = 0x720000000;
constexpr std::uint64_t kHolders = 0x730000000;

std::vector<Bone> MaskBones() {
    return {B("Root", -1), B("locator1", 0, {9, 9, 9}), B("Skl_Root", 1, {0, -0.332f, 0.003f}),
            B("Spine_1", 2), B("Spine_2", 3), B("Neck", 4), B("Head", 5), B("Head_2", 6)};
}

}  // namespace

TEST_CASE("mascaras: a mask is put on its puppet's head, the default head is left alone") {
    CoopMascaras::Reset();
    FakeGuest g;
    namespace pa = CoopPuppet::addr;
    SkeletonFakes::MakeActor(g, kPuppet, {1, 2, 3}, kRy);
    auto link_bones = Spine(Rows::axes);
    link_bones[5].local.translation[2] = 0.05f;  // the head nods
    const std::uint64_t puppet_local =
        SkeletonFakes::MakeSkeleton(g, kPuppet, 0x701000000, "Player_Child", link_bones);
    SkeletonFakes::MakeActor(g, kMask, {50, 0, 50});
    const std::uint64_t mask_local =
        SkeletonFakes::MakeSkeleton(g, kMask, 0x711000000, "Armor_Child_Mask_Keaton", MaskBones());
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::ArmorLink(0), kHolders, kMask, 0x31);

    std::vector<std::string> log;
    CoopMascaras::Heads heads;
    const auto say = [&](const std::string& s) { log.push_back(s); };
    CHECK(CoopMascaras::Heads::HeadPiece(g, kPuppet) == kMask);
    REQUIRE(heads.Follow(g, 2, kPuppet, say));
    CHECK(NearV(g.Get<Vec3>(kMask + pa::kActorPosition), {1, 2, 3}));
    CHECK(NearV(g.Get<Vec3>(kMask + pa::kActorPositionRequest), {1, 2, 3}));
    CHECK(g.Get<Mat3>(kMask + pa::kActorRotation) == kRy);
    CHECK(g.Get<Mat3>(kMask + pa::kActorRotationRequest) == kRy);
    // The bones of the same name are the puppet's: Skl_Root, the spine, the head.
    CHECK(Near(SkeletonFakes::LocalOf(g, mask_local, 2).translation[1], 0.584f));
    CHECK(Near(SkeletonFakes::LocalOf(g, mask_local, 6).translation[2], 0.05f));
    CHECK(SkeletonFakes::LocalOf(g, mask_local, 3).rows[1] ==
          SkeletonFakes::LocalOf(g, puppet_local, 2).rows[1]);
    CHECK(Near(SkeletonFakes::LocalOf(g, mask_local, 1).translation[0], 9.0f));  // its own bone
    REQUIRE(log.size() == 1);
    CHECK(log[0].find("P2 head 'Armor_Child_Mask_Keaton'") == 0);
    CHECK(log[0].find("7 bones of the same name") != std::string::npos);  // Root too, not locator1

    // Every frame, one line only.
    g.Set(kPuppet + pa::kActorPosition, Vec3{4, 5, 6});
    REQUIRE(heads.Follow(g, 2, kPuppet, say));
    CHECK(NearV(g.Get<Vec3>(kMask + pa::kActorPosition), {4, 5, 6}));
    CHECK(log.size() == 1);

    // The default head is not a bound piece: not moved.
    const std::uint64_t plain = 0x740000000;
    SkeletonFakes::MakeActor(g, plain, {50, 0, 50});
    SkeletonFakes::MakeSkeleton(g, plain, 0x741000000, "Armor_Child_Default_Head", MaskBones());
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::ArmorLink(0), kHolders, plain, 0x32);
    CHECK_FALSE(heads.Follow(g, 2, kPuppet, say));
    CHECK(NearV(g.Get<Vec3>(plain + pa::kActorPosition), {50, 0, 50}));

    // Nothing in armor link 0: hand slot 7 (Helmet), as orm/detalles found.
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::ArmorLink(0), kHolders, plain, 0xFFFFFFFFu);
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::HandSlot(7), kHolders + 0x100, kMask, 0x33);
    CHECK(CoopMascaras::Heads::HeadPiece(g, kPuppet) == kMask);
    REQUIRE(heads.Follow(g, 2, kPuppet, say));
    CHECK(log.size() == 2);  // a new piece seen: one more line

    // A mask that is gone (state 7) is no head piece.
    g.Set(kMask + pa::kActorState, std::uint32_t{7});
    CHECK_FALSE(heads.Follow(g, 2, kPuppet, say));

    CHECK(CoopMascaras::IsBoundHead("Armor_Child_Mask_Bunny"));
    CHECK(CoopMascaras::IsBoundHead("Armor_Adult_GlassTruth"));
    CHECK_FALSE(CoopMascaras::IsBoundHead("Armor_Adult_Default_Head"));
    CHECK_FALSE(CoopMascaras::IsBoundHead("Armor_Adult_Goron"));
}

// ORM review (REVISION_FINAL.md "Visual"): a head piece made again at the same
// actor and local array addresses (the pool gives a freed block back) is read
// anew: the last model's bound flag and bone map are not kept.
TEST_CASE("mascaras: a head piece made again at the same addresses is learned again") {
    CoopMascaras::Reset();
    FakeGuest g;
    namespace pa = CoopPuppet::addr;
    SkeletonFakes::MakeActor(g, kPuppet, {1, 2, 3}, kRy);
    SkeletonFakes::MakeSkeleton(g, kPuppet, 0x701000000, "Player_Child", Spine(Rows::axes));
    SkeletonFakes::MakeActor(g, kMask, {50, 0, 50});
    const std::uint64_t local =
        SkeletonFakes::MakeSkeleton(g, kMask, 0x711000000, "Armor_Child_Mask_Keaton", MaskBones());
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::ArmorLink(0), kHolders, kMask, 0x31);
    std::vector<std::string> log;
    const auto say = [&](const std::string& s) { log.push_back(s); };
    CoopMascaras::Heads heads;
    REQUIRE(heads.Follow(g, 2, kPuppet, say));
    REQUIRE(log.size() == 1);

    // The mask taken off: the default head, three bones, at the same addresses.
    SkeletonFakes::MakeSkeleton(g, kMask, 0x711000000, "Armor_Child_Default_Head",
                                {B("Root", -1), B("Skl_Root", 0), B("Head", 1)});
    g.Set(kMask + pa::kActorPosition, Vec3{50, 0, 50});
    const CoopMascaras::Local past = SkeletonFakes::LocalOf(g, local, 6);  // past its three bones
    CHECK_FALSE(heads.Follow(g, 2, kPuppet, say));
    CHECK(NearV(g.Get<Vec3>(kMask + pa::kActorPosition), {50, 0, 50}));  // not a bound head: left alone
    const CoopMascaras::Local after = SkeletonFakes::LocalOf(g, local, 6);
    CHECK(std::memcmp(&past, &after, sizeof(past)) == 0);  // no stale map write
    CoopMascaras::Reset();
}

namespace {

constexpr std::uint64_t kHookshot = 0x750000000;

// A puppet at (10, 0, 10) with its right hand 0.3 aside, 1 up, 0.2 ahead,
// and the hookshot body in hand slot 3 at `piece_at`.
void MakeHolder(FakeGuest& g, Vec3 piece_at, bool with_piece = true) {
    SkeletonFakes::MakeActor(g, kPuppet, {10, 0, 10});
    SkeletonFakes::MakeSkeleton(g, kPuppet, 0x701000000, "Player_Child",
                                {B("Root", -1), B("Weapon_R", 0, {0.3f, 1.0f, 0.2f})});
    if (!with_piece) return;
    SkeletonFakes::MakeActor(g, kHookshot, piece_at);
    SkeletonFakes::MakeSkeleton(g, kHookshot, 0x751000000, "Item_Hookshot_Body",
                                {B("Root", -1), B("Skl_Root", 0, {-0.21f, 0, 0}),
                                 B("FirePos_HookShot", 1, {-0.12f, 0, 0})});
    SkeletonFakes::Link(g, kPuppet, kEu, SkeletonFakes::HandSlot(3), kHolders, kHookshot, 0x43);
}

}  // namespace

TEST_CASE("mascaras: a tether starts at the piece's bone in the hand, else from the hand") {
    {
        CoopMascaras::Reset();
        FakeGuest g;
        MakeHolder(g, {10.3f, 1.0f, 10.2f});
        const auto a = CoopMascaras::FindAnchor(g, kPuppet, "Shootable_Hook");
        REQUIRE(a);
        CHECK(std::string(a->from) == "piece");
        CHECK(a->bone == "FirePos_HookShot");
        CHECK(NearV(a->at, {9.97f, 1.0f, 10.2f}));
        CHECK(Near(a->off, 0.0f));
        // The longshot's bone is not on this piece: the hand itself.
        const auto l = CoopMascaras::FindAnchor(g, kPuppet, "Shootable_HookLong");
        REQUIRE(l);
        CHECK(std::string(l->from) == "hand");
        CHECK(NearV(l->at, {10.3f, 1.0f, 10.2f}));
    }
    {
        // The piece's actor stands at the puppet's feet: its bone from the hand's frame.
        CoopMascaras::Reset();
        FakeGuest g;
        MakeHolder(g, {10, 0, 10});
        const auto a = CoopMascaras::FindAnchor(g, kPuppet, "Shootable_Hook");
        REQUIRE(a);
        CHECK(std::string(a->from) == "hand frame");
        CHECK(NearV(a->at, {9.97f, 1.0f, 10.2f}));
        CHECK(a->off > CoopMascaras::kPieceOnHand);
    }
    {
        CoopMascaras::Reset();
        FakeGuest g;
        MakeHolder(g, {}, false);
        const auto a = CoopMascaras::FindAnchor(g, kPuppet, CoopMascaras::kLineActor);
        REQUIRE(a);
        CHECK(std::string(a->from) == "hand");
        CHECK(a->bone == "Weapon_R");
        CHECK_FALSE(CoopMascaras::FindAnchor(g, 0x790000000, "Shootable_Hook"));  // no actor there
    }
    CHECK(CoopMascaras::AnchorBones("Weapon_FishingLine").front() == "Rod_4");
    CHECK(CoopMascaras::AnchorBones("Item_Bomb").empty());
    CHECK(CoopMascaras::TetherOf("Shootable_Hook") == CoopMascaras::Tether::hook_chain);
    CHECK(CoopMascaras::TetherOf("Shootable_HookLong") == CoopMascaras::Tether::hook_chain);
    CHECK(CoopMascaras::TetherOf("Weapon_FishingLine") == CoopMascaras::Tether::fishing_line);
    CHECK(CoopMascaras::TetherOf("Weapon_Lure") == CoopMascaras::Tether::none);
    CHECK(CoopMascaras::TetherOf("NormalArrow") == CoopMascaras::Tether::none);
    CoopMascaras::Reset();
}

namespace {

constexpr std::uint64_t kCopy = 0x760000000;

// The hookshot tip's model in short: the claws' head, six links and a sub line.
std::vector<Bone> ChainBones() {
    std::vector<Bone> b{B("Root", -1), B("Skl_Root", 0), B("Head", 1)};
    b[1].local = Rotated(kRy, CoopMascaras::kDefaultRows);
    for (int k = 0; k < 6; ++k) {
        b.push_back(B("MainLine_0" + std::to_string(k), static_cast<std::int16_t>(k == 0 ? 1 : 2 + k),
                      {0.383f, 0, 0}));
    }
    b.push_back(B("SubLine_00", 3));
    return b;
}

// A bone's place in the world, through the actor's place and turn.
Vec3 World(FakeGuest& g, std::uint64_t actor, std::size_t bone) {
    namespace pa = CoopPuppet::addr;
    const auto s = CoopMascaras::ReadSkeleton(g, actor);
    REQUIRE(s);
    std::vector<Local> locals;
    REQUIRE(CoopMascaras::ReadLocals(g, *s, locals));
    const auto f = CoopMascaras::BoneFrame(locals, s->parents, bone, CoopMascaras::CurrentRows());
    const Vec3 at = g.Get<Vec3>(actor + pa::kActorPosition);
    const Mat3 rot = g.Get<Mat3>(actor + pa::kActorRotation);
    const Vec3 r = CoopMascaras::Apply(rot, f.origin);
    return {r[0] + at[0], r[1] + at[1], r[2] + at[2]};
}

CoopMascaras::Anchor AnchorAt(Vec3 at) {
    CoopMascaras::Anchor a;
    a.at = at;
    a.from = "piece";
    a.bone = "FirePos_HookShot";
    return a;
}

std::size_t Count(const std::vector<std::string>& log, const std::string& text) {
    std::size_t n = 0;
    for (const auto& line : log) n += line.find(text) != std::string::npos ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("mascaras: a hookshot copy's chain is laid to the hand, folded without one") {
    CoopMascaras::Reset();
    FakeGuest g;
    SkeletonFakes::MakeActor(g, kCopy, {5, 0, 0}, kRz);
    const std::uint64_t local =
        SkeletonFakes::MakeSkeleton(g, kCopy, 0x761000000, "Item_Hookshot_Chain", ChainBones());
    std::vector<std::string> log;
    const auto say = [&](const std::string& s) { log.push_back(s); };
    CoopMascaras::Tethers t;
    using CoopMascaras::Tether;

    // The hand 1 away: two whole links, the other four folded at the hand.
    REQUIRE(t.Lay(g, 3, kCopy, Tether::hook_chain, {5, 0, 0}, kRz, AnchorAt({5, 1, 0}), say));
    CHECK(NearV(World(g, kCopy, 3), {5, 0.383f, 0}));
    CHECK(NearV(World(g, kCopy, 4), {5, 0.766f, 0}));
    for (std::size_t b = 5; b <= 8; ++b) CHECK_MESSAGE(NearV(World(g, kCopy, b), {5, 1, 0}), b);
    CHECK(NearV(World(g, kCopy, 2), {5, 0, 0}));  // the claws stay at the tip
    CHECK(SkeletonFakes::LocalOf(g, local, 2).translation[0] == 0.0f);
    CHECK(Count(log, "Item_Hookshot_Chain: copy 3: hookshot chain of 6 bones from 'MainLine_00', link 0.38") == 1);
    CHECK(Count(log, "laid to the puppet's FirePos_HookShot (piece") == 1);

    // Each frame again, one line each way only.
    REQUIRE(t.Lay(g, 3, kCopy, Tether::hook_chain, {5, 0, 0}, kRz, AnchorAt({5, 2, 0}), say));
    CHECK(NearV(World(g, kCopy, 8), {5, 2, 0}));
    REQUIRE(t.Lay(g, 3, kCopy, Tether::hook_chain, {5, 0, 0}, kRz, std::nullopt, say));
    for (std::size_t b = 3; b <= 8; ++b) CHECK_MESSAGE(NearV(World(g, kCopy, b), {5, 0, 0}), b);
    CHECK(Count(log, "folded (its owner's puppet is not here)") == 1);
    REQUIRE(t.Lay(g, 3, kCopy, Tether::hook_chain, {5, 0, 0}, kRz, AnchorAt({5, 100, 0}), say));
    CHECK(NearV(World(g, kCopy, 8), {5, 0, 0}));
    CHECK(Count(log, "folded") == 1);
    CHECK(Count(log, "laid to") == 1);
    CHECK(t.Count() == 1);
    t.Keep({});
    CHECK(t.Count() == 0);

    // A model without the chain's bones: nothing written, said once.
    const std::uint64_t other = 0x770000000;
    SkeletonFakes::MakeActor(g, other, {0, 0, 0});
    SkeletonFakes::MakeSkeleton(g, other, 0x771000000, "Item_Shootable_Seed", {B("Root", -1)});
    CHECK_FALSE(t.Lay(g, 4, other, Tether::hook_chain, {0, 0, 0}, CoopMascaras::kIdentity,
                      AnchorAt({1, 0, 0}), say));
    CHECK_FALSE(t.Lay(g, 4, other, Tether::hook_chain, {0, 0, 0}, CoopMascaras::kIdentity,
                      AnchorAt({1, 0, 0}), say));
    CHECK(Count(log, "not laid: no chain of bones") == 1);
    CHECK_FALSE(t.Lay(g, 5, kCopy, Tether::none, {0, 0, 0}, CoopMascaras::kIdentity,
                      AnchorAt({1, 0, 0}), say));
    CoopMascaras::Reset();
}

TEST_CASE("mascaras: a fishing line copy runs from the lure to the rod's tip, sagging") {
    CoopMascaras::Reset();
    FakeGuest g;
    SkeletonFakes::MakeActor(g, kCopy, {0, 0, 0});
    SkeletonFakes::MakeSkeleton(g, kCopy, 0x761000000, "Item_FishingRod_Rope",
                                {B("Root", -1), B("Rope_002", 0, {0.25f, 0, 0}),
                                 B("Rope_003", 1, {0.25f, 0, 0}), B("Rope_004", 2, {0.25f, 0, 0}),
                                 B("Tip", 3, {0.25f, 0, 0})});
    CoopMascaras::Tethers t;
    std::vector<std::string> log;
    REQUIRE(t.Lay(g, 9, kCopy, CoopMascaras::Tether::fishing_line, {0, 0, 0}, CoopMascaras::kIdentity,
                  AnchorAt({0, 2, 3}), [&](const std::string& s) { log.push_back(s); }));
    CHECK(NearV(World(g, kCopy, 0), {0, 0, 0}));
    CHECK(NearV(World(g, kCopy, 4), {0, 2, 3}));
    CHECK(NearV(World(g, kCopy, 2), {0, 0.85f, 1.5f}));  // 0.05 of the 3 across, down
    CHECK(Count(log, "fishing line of 5 bones from 'Root'") == 1);
    CoopMascaras::Reset();
}

TEST_CASE("mascaras: the spawn director lays a hookshot copy's chain from its owner's hand") {
    CoopMascaras::Reset();
    namespace addr = CoopPuppet::addr;
    constexpr std::uint64_t base = 0x8000000;
    constexpr std::uint64_t block = 0x30000000;
    constexpr std::uint64_t request = 0x50000000;
    FakeGuest g;
    g.Map(base + addr::kActorManagerPtr, 8);
    g.Map(base + addr::kGotCreateDefault, 8);
    g.Set(base + addr::kActorManagerPtr, std::uint64_t{0x20000000});
    g.Map(block, CoopSpawn::Director::kBlockSize);
    g.Map(request, 0x10);
    SkeletonFakes::MakeActor(g, kCopy, {0, 0, 0});
    g.Set(kCopy, std::uint64_t{0xabc});  // its vtable
    SkeletonFakes::MakeSkeleton(g, kCopy, 0x761000000, "Item_Hookshot_Chain", ChainBones());

    std::vector<std::string> said;
    CoopSpawn::Director d([&](const std::string& s) { said.push_back(s); });
    d.SetStandIns({"CoopShot_Hook", "CoopShot_FishingLine"});
    std::vector<std::uint8_t> asked;
    d.SetAnchors([&](CoopPuppet::Guest&, std::uint8_t owner,
                     std::string_view actor) -> std::optional<CoopMascaras::Anchor> {
        asked.push_back(owner);
        CHECK(actor == "Shootable_Hook");
        if (owner != 2) return std::nullopt;
        return AnchorAt({5, 1, 0});
    });
    CoopSpawn::Copy copy;
    copy.key = 3;
    copy.actor = "Shootable_Hook";
    copy.position = {5, 0, 0};
    copy.rotation = kRz;
    copy.owner = 2;
    copy.tether = CoopMascaras::Tether::hook_chain;
    d.SetInput(base, {copy});
    REQUIRE(d.Step(g, false, 0, 0));  // malloc
    auto c = d.Step(g, true, block, 0);
    REQUIRE(c);
    REQUIRE(c->function == base + addr::kCreate);
    const std::uint64_t handle = c->x[3];
    d.Step(g, true, 0, 0);
    g.Set(handle, request);
    g.Set(request, std::uint32_t{addr::kRequestReady});
    REQUIRE(d.Step(g, false, 0, 16));  // take
    d.Step(g, true, kCopy, 16);

    // Live: placed, then its chain laid to P2's hand.
    CHECK_FALSE(d.Step(g, false, 0, 33));
    CHECK(asked == std::vector<std::uint8_t>{2});
    CHECK(NearV(World(g, kCopy, 8), {5, 1, 0}));
    CHECK(Count(said, "spawn: Item_Hookshot_Chain: copy 3: hookshot chain laid to") == 1);

    // Its owner's puppet is not here: folded.
    copy.owner = 4;
    d.SetInput(base, {copy});
    CHECK_FALSE(d.Step(g, false, 0, 50));
    CHECK(NearV(World(g, kCopy, 8), {5, 0, 0}));

    // No tether: no anchor asked, nothing laid.
    copy.tether = CoopMascaras::Tether::none;
    d.SetInput(base, {copy});
    CHECK_FALSE(d.Step(g, false, 0, 66));
    CHECK(asked.size() == 2);
    CoopMascaras::Reset();
}

// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): how the rows are stored is
// decided from a puppet's spine, and the run's log says which way and why.
#include "coop_check_visual.h"

TEST_CASE("mascaras (riesgos-visual): the rows check line says the way the spine decided, or why not yet") {
    std::vector<std::string> lines;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { lines.push_back(line); });
    CoopMascaras::Reset();
    // A spine lying down: neither way fits, nothing decided yet: the fallback, once.
    auto flat = Spine(Rows::axes);
    flat[2].local = Local{};
    int fits = -1;
    CHECK_FALSE(CoopMascaras::DetectRows(LocalsOf(flat), ParentsOf(flat), 5, 1, &fits));
    CHECK(fits == 0);
    CoopMascaras::RowsCheck(fits, std::nullopt, 0.0f);
    CoopMascaras::RowsCheck(fits, std::nullopt, 0.0f);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "coop: check: bone rows fallback axes until a puppet's spine decides (neither way puts its "
                      "head over its hips)");
    // An upright spine stored as a matrix decides it: the ok line.
    const auto bones = Spine(Rows::matrix);
    const auto rows = CoopMascaras::DetectRows(LocalsOf(bones), ParentsOf(bones), 5, 1, &fits);
    REQUIRE(rows);
    CHECK(fits == 1);
    CoopMascaras::NoteRows(*rows);
    CoopMascaras::RowsCheck(fits, rows, 0.6f);
    REQUIRE(lines.size() == 2);
    CHECK(lines[1] == "coop: check: bone rows ok matrix (a puppet's head sits 0.60 over its hips only that way)");
    // Once decided, a later reading that does not decide says nothing more.
    CoopMascaras::RowsCheck(2, std::nullopt, 0.0f);
    CHECK(lines.size() == 2);
    CoopMascaras::Reset();
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
