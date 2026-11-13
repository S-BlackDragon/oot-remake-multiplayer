// ORM cases moved out of upstream's tests/lichii/pose_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the pose copy's math and its track (lichii/hook/coop_pose.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "coop_pose.h"
#include "coop_puppet.h"

namespace {

using CoopPose::Quat;
using CoopPose::Rows;

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

CoopPose::Pose OneBone(const Quat& q, float x) {
    CoopPose::Pose p;
    p.skeleton = 7;
    CoopPose::BonePose b;
    b.bone = 3;
    b.rotation = q;
    b.translation = CoopPose::Vec3{x, 0, 0};
    p.bones = {b};
    return p;
}

// Guest memory as a few flat regions.
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
    void Put(std::uint64_t at, T value) { REQUIRE(Write(at, &value, sizeof(T))); }
    template <typename T>
    T Get(std::uint64_t at) {
        T value{};
        REQUIRE(Read(at, &value, sizeof(T)));
        return value;
    }

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

constexpr std::uint64_t kMain = 0x80000000;
constexpr std::uint32_t kBones = 8;
namespace paddr = CoopPose::addr;

// An actor whose skeleton has kBones identity bones; returns the local array.
std::uint64_t MakeActor(FakeGuest& g, std::uint64_t actor, std::string_view model = {},
                        std::uint32_t bones = kBones) {
    const std::uint64_t parts = actor + 0x1000, first = actor + 0x2000, second = actor + 0x3000,
                        holder = actor + 0x4000, skel = actor + 0x5000,
                        // a big skeleton's array does not fit before `res`
                        local = actor + (bones * paddr::kLocalStride > 0x1000 ? 0x10000 : 0x6000),
                        res = actor + 0x7000;
    for (std::uint64_t at : {actor, parts, first, second, holder, skel, res}) g.Map(at, 0x400);
    g.Map(local, bones * paddr::kLocalStride);
    g.Put(actor + 0x2c, std::uint32_t{4});
    g.Put(actor + paddr::kSkeletonPath[0], parts);
    g.Put(parts + paddr::kSkeletonPath[1], first);
    g.Put(first + paddr::kSkeletonPath[2], second);
    g.Put(second + paddr::kSkeletonPath[3], holder);
    g.Put(holder + paddr::kSkeletonPath[4], skel);
    g.Put(holder + paddr::kModelResource, res);
    g.Put(res, std::array<char, 4>{'F', 'M', 'D', 'L'});
    if (!model.empty()) {
        const std::uint64_t name = res + 0x100;
        g.Put(res + paddr::kModelName, name);
        g.Put(name, static_cast<std::uint16_t>(model.size()));
        REQUIRE(g.Write(name + 2, model.data(), model.size()));
    }
    g.Put(skel + paddr::kBoneCount, static_cast<std::uint16_t>(bones));
    g.Put(skel + paddr::kLocalArray, local);
    for (std::uint32_t i = 0; i < bones; ++i) {
        const std::uint64_t b = local + i * paddr::kLocalStride;
        g.Put(b + 0x10, CoopPose::Vec3{1, 1, 1});
        g.Put(b + 0x20, 1.0f);
        g.Put(b + 0x34, 1.0f);
        g.Put(b + 0x48, 1.0f);
    }
    return local;
}

// The scene module's chain down to the local player, as PlayerReader walks it.
void MakePlayerChain(FakeGuest& g, std::uint64_t actor) {
    const std::uint64_t scene = 0x500000000, table = scene + 0x1000, array = scene + 0x2000,
                        manager = scene + 0x3000, link = scene + 0x4000, holder = scene + 0x5000;
    g.Map(kMain + 0x4e97000, 0x1000);
    for (std::uint64_t at : {scene, table, array, manager, link, holder}) g.Map(at, 0x400);
    g.Put(kMain + 0x4e97648, scene);
    g.Put(scene + 0x1f0, table);
    g.Put(table + 0x50, std::uint32_t{0x10});
    g.Put(table + 0x58, array);
    g.Put(array + 0x78, manager);
    g.Put(manager + 0x30, std::uint64_t{1});
    g.Put(manager + 0x40, link);
    g.Put(link + 0x10, std::uint32_t{5});
    g.Put(link + 0x8, holder);
    g.Put(holder + 0x48, std::uint32_t{5});
    g.Put(holder + 0x40, actor);
}

}  // namespace

TEST_CASE("pose: a stopped stream is held, then blended out, and a resumed one fades in") {
    CoopPose::Track t;
    CHECK_FALSE(t.Weight(0));
    t.Push(OneBone({0, 0, 0, 1}, 1.0f), 1000);
    CHECK(*t.Weight(1000) == 1.0f);
    CHECK(*t.Weight(1000 + CoopPose::kHoldMs) == 1.0f);  // held
    CHECK(Near(*t.Weight(1000 + CoopPose::kHoldMs + CoopPose::kBlendMs / 2), 0.5f));
    CHECK_FALSE(t.Weight(1000 + CoopPose::kHoldMs + CoopPose::kBlendMs));  // handed over
    // Back after 4 s: not played over the gap in slow motion, faded in.
    t.Push(OneBone({0, 0, 0, 1}, 3.0f), 5000);
    CHECK(t.resume_gap_ms() == 4000);
    CHECK(Near(t.At(5001)->bones[0].translation->at(0), 3.0f));
    CHECK(*t.Weight(5000) == 0.0f);
    CHECK(Near(*t.Weight(5000 + CoopPose::kResumeMs / 2), 0.5f));
    CHECK(*t.Weight(5000 + CoopPose::kResumeMs) == 1.0f);
    // A normal stream (33 ms apart) is no resume.
    t.Push(OneBone({0, 0, 0, 1}, 4.0f), 5233);
    CHECK(t.resume_gap_ms() == 4000);
    CHECK(t.resumed_ms() == 5000);
}

TEST_CASE("pose: the puppet holds the last pose, blends into the game's animation, then lets go") {
    FakeGuest g;
    const std::uint64_t puppet = 0x700000000;
    const std::uint64_t local = MakeActor(g, puppet, "Player_Child");
    std::vector<std::string> lines;
    CoopPose::SetLog([&](const std::string& l) { lines.push_back(l); }, false);
    const std::vector<std::pair<std::uint8_t, std::uint64_t>> puppets{{5, puppet}};
    const std::uint64_t bone1 = local + 1 * paddr::kLocalStride + 0x50;  // translation
    const auto pose = [](float x) {
        coop::protocol::PlayerPose m;
        m.player_number = 5;
        m.skeleton = CoopPose::SkeletonId(kBones, "Player_Child");
        coop::protocol::PoseBone b;
        b.bone = 1;
        b.translation = CoopPose::PackUnit({x, 0.0f, 0.0f});
        m.bones = {b};
        return m;
    };
    constexpr std::uint64_t t0 = 200'000;
    CoopPose::Receive(pose(0.4f), t0);
    CHECK(CoopPose::OnCalcFrame(g, kMain, puppets, t0 + 16) == (1u << 5));  // a whole fresh pose
    CHECK(Near(g.Get<float>(bone1), 0.4f));

    // The poses stop. The game's own animation (the AS command the director
    // plays now) puts 0 there each frame before ours.
    const auto frame = [&](std::uint64_t at) {
        g.Put(bone1, 0.0f);
        return CoopPose::OnCalcFrame(g, kMain, puppets, at);
    };
    CHECK(frame(t0 + 1000) == 0u);  // held, but no longer fresh: not a reveal
    CHECK(Near(g.Get<float>(bone1), 0.4f));
    CHECK(CoopPose::Drives(5, t0 + CoopPose::kHoldMs - 1));
    CHECK(frame(t0 + CoopPose::kHoldMs) == 0u);
    CHECK(Near(g.Get<float>(bone1), 0.4f));  // still the last pose
    CHECK_FALSE(CoopPose::Drives(5, t0 + CoopPose::kHoldMs + 1));
    frame(t0 + CoopPose::kHoldMs + 100);  // a fifth of the way: 0.8 of ours
    CHECK(Near(g.Get<float>(bone1), 0.32f, 2e-3f));
    frame(t0 + CoopPose::kHoldMs + 400);
    CHECK(Near(g.Get<float>(bone1), 0.08f, 2e-3f));
    frame(t0 + CoopPose::kHoldMs + CoopPose::kBlendMs);
    CHECK(g.Get<float>(bone1) == 0.0f);  // the game's animation alone
    int stopped = 0;
    for (const auto& l : lines) stopped += l.find("pose: P5 no pose for") != std::string::npos;
    CHECK(stopped == 1);

    // They come back: faded in from what the puppet shows, not snapped.
    const std::uint64_t t1 = t0 + 6000;
    CoopPose::Receive(pose(0.2f), t1);
    CHECK(frame(t1) == 0u);
    CHECK(g.Get<float>(bone1) == 0.0f);
    frame(t1 + CoopPose::kResumeMs / 2);
    CHECK(Near(g.Get<float>(bone1), 0.1f, 2e-3f));
    CoopPose::Receive(pose(0.2f), t1 + CoopPose::kResumeMs);
    CHECK(frame(t1 + CoopPose::kResumeMs) == (1u << 5));
    CHECK(Near(g.Get<float>(bone1), 0.2f, 2e-3f));
    bool back = false;
    for (const auto& l : lines) back = back || l.find("pose: P5 poses back after 6000 ms") != std::string::npos;
    CHECK(back);
    CoopPose::SetLog({}, false);
}

TEST_CASE("pose: timed poses are shown by their sender time, bursts and reordering do not show") {
    // ORM smoothing (oot-remake docs/SUAVIZADO.md): two poses that arrive in
    // one burst, the newer first, are still played over the 33 ms between them.
    CoopPose::Track t;
    constexpr std::uint64_t kUs = 50'000'000;
    t.Push(OneBone({0, 0, 0, 1}, 2.0f), 1000, {kUs + 33'000, 2});
    t.Push(OneBone({0, 0, 0, 1}, 0.0f), 1000, {kUs, 1});
    t.Push(OneBone({0, 0, 0, 1}, 9.0f), 1000, {kUs, 1});  // a duplicate is dropped
    REQUIRE(t.At(1000, kUs));
    CHECK(Near(t.At(1000, kUs)->bones[0].translation->at(0), 0.0f));
    CHECK(Near(t.At(1000, kUs + 16'500)->bones[0].translation->at(0), 1.0f));
    CHECK(Near(t.At(1000, kUs + 33'000)->bones[0].translation->at(0), 2.0f));
    CHECK(Near(t.At(1000, kUs + 900'000)->bones[0].translation->at(0), 2.0f));  // held
    CHECK(Near(t.At(1000, kUs - 5'000)->bones[0].translation->at(0), 0.0f));    // early: the oldest
    // Without a render time: by arrival, as before (the newest arrived last).
    CHECK(Near(t.At(1000)->bones[0].translation->at(0), 9.0f));
    // Back after a gap: the timeline starts over with the new pose.
    t.Push(OneBone({0, 0, 0, 1}, 5.0f), 1000 + CoopPose::kResumeGapMs + 1, {kUs + 900'000, 3});
    CHECK(Near(t.At(2000, kUs + 33'000)->bones[0].translation->at(0), 5.0f));
}

TEST_CASE("pose: a pose is stamped with when it was read, not when it was sent") {
    FakeGuest g;
    const std::uint64_t player = 0x600000000;
    const std::uint64_t mine = MakeActor(g, player, "Player_Child");
    MakePlayerChain(g, player);
    CoopPose::SetLog({}, false);
    CoopPose::OnCalcFrame(g, kMain, {}, 300'000);
    g.Put(mine + 1 * paddr::kLocalStride + 0x50, CoopPose::Vec3{0.3f, 0, 0});
    CoopPose::OnCalcFrame(g, kMain, {}, 300'016);  // read at 300 016 ms
    const auto out = CoopPose::TakeOutgoing(9'000'000, 300'036);  // sent 20 ms later
    REQUIRE_FALSE(out.empty());
    CHECK(out[0].time.server_time_us == 9'000'000 - 20'000);
}

// ORM (coop_interest.h, oot-remake docs/ANCHO_DE_BANDA.md): the gap between
// two poses is the one given (the native one by default); none sends nothing.
TEST_CASE("pose: ORM the gap the interest gives, and none for nobody in our place") {
    FakeGuest g;
    const std::uint64_t player = 0x600000000;
    MakeActor(g, player, "Player_Child");
    MakePlayerChain(g, player);
    CoopPose::SetLog({}, false);
    const std::vector<std::pair<std::uint8_t, std::uint64_t>> puppets;
    const std::uint64_t t0 = 50'000'000;  // after every other test's clock
    CoopPose::OnCalcFrame(g, kMain, puppets, t0);
    CHECK(CoopPose::TakeOutgoing(5, t0, std::nullopt).empty());  // alone: no pose
    CHECK(CoopPose::TakeOutgoing(5, t0, 100).size() == 1);       // any gap given
    CoopPose::OnCalcFrame(g, kMain, puppets, t0 + 50);
    CHECK(CoopPose::TakeOutgoing(5, t0 + 50, 100).empty());  // within its 100 ms
    CHECK(CoopPose::TakeOutgoing(5, t0 + 50).size() == 1);   // the default 1000 / kSendHz
    CoopPose::OnCalcFrame(g, kMain, puppets, t0 + 70);
    CHECK(CoopPose::TakeOutgoing(5, t0 + 70).empty());        // 20 ms: under 33
    CoopPose::OnCalcFrame(g, kMain, puppets, t0 + 84);
    CHECK(CoopPose::TakeOutgoing(5, t0 + 84).size() == 1);
}

// ORM (oot-remake docs/RENDIMIENTO.md): what the pose copy itself costs on the
// calc thread with six players, without a game: our Link's 309 bones read and
// five puppets of 309 bones written every frame, a pose of 305 moving bones
// from each player 30 times a second. Not a check: it prints the time. Skipped
// in the suite; run it with
//   coop_tests --no-skip -tc="*bench: the pose copy*"
TEST_CASE("pose: bench: the pose copy for five puppets of 309 bones" * doctest::skip()) {
    FakeGuest g;
    constexpr std::uint32_t kLinkBones = 309;
    constexpr std::uint32_t kMoving = 305;
    const std::uint64_t player = 0x600000000;
    MakeActor(g, player, "Player_Child", kLinkBones);
    MakePlayerChain(g, player);
    std::vector<std::pair<std::uint8_t, std::uint64_t>> puppets;
    for (std::uint8_t n = 2; n <= 6; ++n) {
        const std::uint64_t actor = 0x700000000 + std::uint64_t{n} * 0x10000000;
        MakeActor(g, actor, "Player_Child", kLinkBones);
        puppets.emplace_back(n, actor);
    }
    const auto pose_at = [&](float turn) {
        CoopPose::Pose p;
        p.skeleton = CoopPose::SkeletonId(kLinkBones, "Player_Child");
        for (std::uint16_t i = 0; i < kMoving; ++i) {
            CoopPose::BonePose b;
            b.bone = i;
            const float a = turn + 0.01f * static_cast<float>(i);
            b.rotation = Quat{0, std::sin(a), 0, std::cos(a)};
            b.translation = CoopPose::Vec3{0.1f, 0.2f + turn, 0.3f};
            b.scale = CoopPose::Vec3{1, 1, 1};
            p.bones.push_back(b);
        }
        return p;
    };
    constexpr int kFrames = 3000;
    std::uint64_t now_ms = 100'000;
    std::uint64_t calc_ns = 0, receive_ns = 0;
    std::uint32_t posed = 0;
    for (int frame = 0; frame < kFrames; ++frame) {
        now_ms += 16;
        if (frame % 2 == 0) {  // 30 poses a second from each player (the network thread)
            const auto t0 = std::chrono::steady_clock::now();
            for (std::uint8_t n = 2; n <= 6; ++n) {
                coop::protocol::PlayerPose m;
                m.player_number = n;
                m.skeleton = CoopPose::SkeletonId(kLinkBones, "Player_Child");
                m.bones = CoopPose::EncodeBones(pose_at(0.001f * static_cast<float>(frame + n)));
                CoopPose::Receive(m, now_ms);
            }
            receive_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        }
        const auto t0 = std::chrono::steady_clock::now();
        posed |= CoopPose::OnCalcFrame(g, kMain, puppets, now_ms);
        calc_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    }
    CHECK(posed == 0b1111100u);  // every puppet got whole fresh poses
    const double calc_us = static_cast<double>(calc_ns) / 1e3 / kFrames;
    MESSAGE("pose copy, calc thread: " << calc_us << " us per frame for 5 puppets ("
                                       << calc_us / 5.0 << " us per puppet, our own read included); "
                                       << "network thread (encode + receive of 5 poses, test side included): "
                                       << static_cast<double>(receive_ns) / 1e3 / (kFrames / 2) << " us per round");
}
