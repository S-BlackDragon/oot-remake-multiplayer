// ORM steps 5 to 7 (oot-remake docs/NPC_COMPARTIDOS.md): the townsperson pose
// copy (lichii/hook/coop_npc_pose.cpp) and the actor skeleton reader and
// writer it uses (coop_pose.cpp), on fake guest memory.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop_npc_pose.h"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace {

namespace proto = coop::protocol;
using CoopNpcPose::NpcPose;
using CoopPose::Quat;

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

namespace paddr = CoopPose::addr;
constexpr std::uint64_t kMain = 0x80000000;
constexpr std::uint64_t kStride = paddr::kLocalStride;
constexpr std::string_view kMido = "Npc_Kokiri_Mido_Child";
// A live actor as the Mover's Alive (coop_objects.cpp) sees one: state 4 at
// +0x2c and its actor-list node (+0x538) pointing back at it (+0x10).
constexpr std::uint64_t kActorNode = 0x538;
constexpr std::uint64_t kNodeOwner = 0x10;

// An actor whose skeleton has `bones` identity bones of model `model`;
// returns its local transform array.
std::uint64_t MakeActor(FakeGuest& g, std::uint64_t actor, std::uint32_t bones,
                        std::string_view model = kMido) {
    const std::uint64_t parts = actor + 0x1000, first = actor + 0x2000, second = actor + 0x3000,
                        holder = actor + 0x4000, skel = actor + 0x5000, local = actor + 0x10000,
                        res = actor + 0x7000;
    for (std::uint64_t at : {parts, first, second, holder, skel, res}) g.Map(at, 0x400);
    g.Map(actor, 0x600);
    g.Map(local, bones * kStride);
    g.Put(actor + 0x2c, std::uint32_t{4});           // present
    g.Put(actor + kActorNode + kNodeOwner, actor);   // on the actor list
    g.Put(actor + paddr::kSkeletonPath[0], parts);
    g.Put(parts + paddr::kSkeletonPath[1], first);
    g.Put(first + paddr::kSkeletonPath[2], second);
    g.Put(second + paddr::kSkeletonPath[3], holder);
    g.Put(holder + paddr::kSkeletonPath[4], skel);
    g.Put(holder + paddr::kModelResource, res);
    g.Put(res, std::array<char, 4>{'F', 'M', 'D', 'L'});
    const std::uint64_t name = res + 0x100;
    g.Put(res + paddr::kModelName, name);
    g.Put(name, static_cast<std::uint16_t>(model.size()));
    REQUIRE(g.Write(name + 2, model.data(), model.size()));
    g.Put(skel + paddr::kBoneCount, static_cast<std::uint16_t>(bones));
    g.Put(skel + paddr::kLocalArray, local);
    for (std::uint32_t i = 0; i < bones; ++i) {
        const std::uint64_t b = local + i * kStride;
        g.Put(b + 0x10, CoopPose::Vec3{1, 1, 1});
        g.Put(b + 0x20, 1.0f);
        g.Put(b + 0x34, 1.0f);
        g.Put(b + 0x48, 1.0f);
        g.Put(b + 0x50, CoopPose::Vec3{0.0f, 0.1f * static_cast<float>(i), 0.0f});  // bind offsets
    }
    return local;
}

void SetRotation(FakeGuest& g, std::uint64_t local, std::uint32_t bone, const Quat& q) {
    const CoopPose::Rows r = CoopPose::QuatToRows(q);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            g.Put(local + bone * kStride + 0x20 + 4 * (row * 4 + col), r[row * 3 + col]);
        }
    }
}

Quat RotationOf(FakeGuest& g, std::uint64_t local, std::uint32_t bone) {
    CoopPose::Rows r{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            r[row * 3 + col] = g.Get<float>(local + bone * kStride + 0x20 + 4 * (row * 4 + col));
        }
    }
    return CoopPose::RowsToQuat(r);
}

bool SameRotation(const Quat& a, const Quat& b) {
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    return std::fabs(std::fabs(dot) - 1.0f) <= 1e-3f;
}

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

const proto::ObjectId kMidoId{proto::ObjectKind::placed, 0, 0x72d7f3000d11ull};

// As the server relays it: through the codec, with the owner's number.
proto::ObjectPose Relayed(proto::ObjectPose m, std::uint8_t owner) {
    m.player_number = owner;
    std::vector<std::uint8_t> bytes;
    REQUIRE(proto::encode(m, bytes) == proto::Error::none);
    proto::Message back;
    REQUIRE(proto::decode(bytes, back) == proto::Error::none);
    REQUIRE(proto::check_received(back, proto::Sender::server, proto::kChannelObjectSamples) ==
            proto::Error::none);
    return std::get<proto::ObjectPose>(back);
}

}  // namespace

TEST_CASE("npc pose: the owner's townsperson reaches the copy as CopySkeleton would copy it") {
    FakeGuest g;
    constexpr std::uint32_t kBones = 24;
    const std::uint64_t owner_actor = 0x10000000, copy_actor = 0x20000000, direct = 0x30000000;
    const std::uint64_t owner_local = MakeActor(g, owner_actor, kBones);
    const std::uint64_t copy_local = MakeActor(g, copy_actor, kBones);
    const std::uint64_t direct_local = MakeActor(g, direct, kBones);
    // The copy's own AI turned its head to our Link.
    const float s = std::sqrt(0.5f);
    SetRotation(g, copy_local, 12, {0, s, 0, s});

    NpcPose owner;
    owner.SetSource(CoopNpcPose::Source{kMidoId, 3, owner_actor});
    owner.OnCalcFrame(g, 1000);  // the first read
    // The talk: the head turns the other way, the jaw opens, the root steps.
    SetRotation(g, owner_local, 12, {0, -0.38268343f, 0, 0.92387953f});
    SetRotation(g, owner_local, 20, {0.25881904f, 0, 0, 0.96592583f});
    g.Put(owner_local + 1 * kStride + 0x50, CoopPose::Vec3{0.02f, 0.13f, -0.05f});
    owner.OnCalcFrame(g, 1016);
    // First pose out at once, in one part (24 bones).
    const auto parts = owner.TakeOutgoing(5'000'000, 1016);
    REQUIRE(parts.size() == 1);
    const auto* out = &parts[0];
    CHECK(out->player_number == 0);
    CHECK(out->id == kMidoId);
    CHECK(out->epoch == 3);
    CHECK(out->time.server_time_us == 5'000'000);
    CHECK(out->skeleton == CoopPose::SkeletonId(kBones, kMido));
    CHECK(out->part == 0);
    CHECK(out->parts == 0x01);
    REQUIRE(out->bones.size() == kBones);  // every bone's rotation
    CHECK(out->bones[1].translation);       // the root moved: its translation goes
    CHECK_FALSE(out->bones[2].translation);  // the others are the bind offsets
    CHECK_FALSE(out->bones[12].scale);

    // The other game: its copy takes it, in place of its own AI's look.
    NpcPose copy;
    copy.Receive(Relayed(*out, 2), 2000);
    const auto heard = copy.Fresh(2000);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].id == kMidoId);
    CHECK(heard[0].player == 2);
    CHECK(heard[0].epoch == 3);
    copy.SetTargets({{kMidoId, copy_actor}});
    copy.OnCalcFrame(g, 2001);
    REQUIRE(CoopPose::CopySkeleton(g, owner_actor, direct, nullptr));
    for (std::uint32_t b = 0; b < kBones; ++b) {
        INFO("bone " << b);
        CHECK(SameRotation(RotationOf(g, copy_local, b), RotationOf(g, direct_local, b)));
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(Near(g.Get<float>(copy_local + b * kStride + 0x50 + 4 * i),
                       g.Get<float>(direct_local + b * kStride + 0x50 + 4 * i)));
        }
    }
    const auto n = copy.TakeCounts();
    CHECK(n.received == 1);
    CHECK(n.written == 1);
    CHECK(owner.TakeCounts().read == 2);
}

TEST_CASE("npc pose: at most 20 a second, only the bones that changed, all every 500 ms, one source only") {
    FakeGuest g;
    const std::uint64_t actor = 0x10000000;
    const std::uint64_t local = MakeActor(g, actor, 8);
    NpcPose p;
    p.OnCalcFrame(g, 0);
    CHECK(p.TakeOutgoing(1, 0).empty());  // no source: nothing
    p.SetSource(CoopNpcPose::Source{kMidoId, 0, actor});
    p.OnCalcFrame(g, 10);
    const auto first = p.TakeOutgoing(100, 10);
    REQUIRE(first.size() == 1);
    CHECK(first[0].bones.size() == 8);  // the first: every bone
    CHECK(p.TakeOutgoing(100, 11).empty());  // no new read
    SetRotation(g, local, 3, {0, 0, 0.38268343f, 0.92387953f});
    p.OnCalcFrame(g, 30);
    CHECK(p.TakeOutgoing(200, 30).empty());  // 20 ms after the last: too soon
    p.OnCalcFrame(g, 60);
    const auto second = p.TakeOutgoing(300, 60);
    REQUIRE(second.size() == 1);
    CHECK(second[0].time.sequence == first[0].time.sequence + 1);
    REQUIRE(second[0].bones.size() == 1);  // only the bone that turned
    CHECK(second[0].bones[0].bone == 3);
    CHECK(second[0].parts == 0x01);
    // It stands still: bone 3 again in the next kRepeats poses (one lost
    // pose does not leave it wrong), then nothing until the refresh.
    for (std::uint64_t t : {110, 160}) {
        p.OnCalcFrame(g, t);
        const auto again = p.TakeOutgoing(300 + t, t);
        REQUIRE(again.size() == 1);
        CHECK(again[0].bones == second[0].bones);
    }
    static_assert(CoopNpcPose::kRepeats == 2);
    p.OnCalcFrame(g, 210);
    CHECK(p.TakeOutgoing(500, 210).empty());
    p.OnCalcFrame(g, 400);
    CHECK(p.TakeOutgoing(500, 400).empty());
    // 500 ms after the first: every bone again.
    p.OnCalcFrame(g, 511);
    const auto refresh = p.TakeOutgoing(600, 511);
    REQUIRE(refresh.size() == 1);
    REQUIRE(refresh[0].bones.size() == 8);
    CHECK(refresh[0].bones[3] == second[0].bones[0]);
    // The talk is over: nothing more.
    p.SetSource(std::nullopt);
    p.OnCalcFrame(g, 700);
    CHECK(p.TakeOutgoing(700, 700).empty());
    const auto n = p.TakeCounts();
    CHECK(n.sent == 5);
    CHECK(n.sent_parts == 5);
    CHECK(n.sent_bones == 8 + 1 + 1 + 1 + 8);
    // A new townsperson's first pose goes at once, every bone.
    p.SetSource(CoopNpcPose::Source{proto::ObjectId{proto::ObjectKind::placed, 0, 5}, 1, actor});
    p.OnCalcFrame(g, 705);
    const auto other = p.TakeOutgoing(800, 705);
    REQUIRE(other.size() == 1);
    CHECK(other[0].id.value == 5);
    CHECK(other[0].bones.size() == 8);
}

TEST_CASE("npc pose: the newest pose of an object wins, a new owner starts its track over") {
    NpcPose p;
    proto::ObjectPose m;
    m.id = kMidoId;
    m.skeleton = CoopPose::SkeletonId(8, kMido);
    m.bones = {proto::PoseBone{2, 0, std::array<std::int16_t, 4>{0, 0, 0, 32767}, std::nullopt,
                               std::nullopt}};
    const auto at = [&](std::uint8_t player, std::uint16_t epoch, std::uint64_t t,
                        std::uint32_t seq) {
        proto::ObjectPose x = m;
        x.player_number = player;
        x.epoch = epoch;
        x.time = {t, seq};
        return x;
    };
    p.Receive(at(0, 0, 100, 1), 10);  // not relayed: no owner named
    CHECK(p.Fresh(10).empty());
    p.Receive(at(2, 4, 100, 5), 10);
    p.Receive(at(2, 4, 90, 9), 11);   // older
    p.Receive(at(2, 4, 100, 5), 12);  // the same again
    p.Receive(at(2, 3, 200, 9), 13);  // an older epoch
    auto n = p.TakeCounts();
    CHECK(n.received == 1);
    CHECK(n.stale == 3);
    p.Receive(at(2, 4, 100, 6), 14);  // same time, later sequence: newer
    auto heard = p.Fresh(20);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].player == 2);
    CHECK(heard[0].epoch == 4);
    // The object moved to player 3 (epoch 5): its poses replace player 2's,
    // even with an earlier server time.
    p.Receive(at(3, 5, 50, 1), 30);
    heard = p.Fresh(40);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].player == 3);
    CHECK(heard[0].epoch == 5);
    CHECK(p.TakeCounts().received == 2);
    // Another object at epoch 65535: the epoch after the wrap (0) is newer.
    proto::ObjectPose w = at(4, 65535, 70, 1);
    w.id.value = 0x51;
    p.Receive(w, 31);
    w.epoch = 0;
    w.time = {20, 1};
    p.Receive(w, 31);
    n = p.TakeCounts();
    CHECK(n.received == 2);
    CHECK(n.stale == 0);
    heard = p.Fresh(40);
    REQUIRE(heard.size() == 2);
    CHECK(heard[0].id.value == 0x51);  // in id order
    CHECK(heard[0].epoch == 0);
    CHECK(heard[1].id == kMidoId);
    // Not heard for a second: no copy follows it any more.
    CHECK(p.Fresh(31 + CoopNpcPose::kFreshMs).empty());
}

TEST_CASE("npc pose: another skeleton is not written, one past the skeleton id's count is not sent") {
    FakeGuest g;
    const std::uint64_t big = 0x10000000, saria = 0x20000000;
    MakeActor(g, big, CoopNpcPose::kMaxBones + 1);
    const std::uint64_t saria_local = MakeActor(g, saria, 8, "Npc_Kokiri_Saria_Child");
    NpcPose p;
    p.SetSource(CoopNpcPose::Source{kMidoId, 0, big});
    p.OnCalcFrame(g, 10);
    CHECK(p.TakeOutgoing(100, 10).empty());
    auto n = p.TakeCounts();
    CHECK(n.read == 0);
    CHECK(n.not_read == 1);

    // Mido's pose (8 bones) onto a copy that is Saria (8 bones, another model).
    proto::ObjectPose m;
    m.player_number = 2;
    m.id = kMidoId;
    m.time = {1, 1};
    m.skeleton = CoopPose::SkeletonId(8, kMido);
    m.bones = {proto::PoseBone{3, 0, std::array<std::int16_t, 4>{0, 0, 12539, 30273}, std::nullopt,
                               std::nullopt}};
    p.Receive(m, 20);
    p.SetTargets({{kMidoId, saria}});
    p.OnCalcFrame(g, 21);
    n = p.TakeCounts();
    CHECK(n.written == 0);
    CHECK(n.other_skeleton == 1);
    CHECK(SameRotation(RotationOf(g, saria_local, 3), {0, 0, 0, 1}));  // untouched
    // A target that is not there at all (gone from memory) is counted too.
    p.SetTargets({{kMidoId, 0x70000000}});
    p.OnCalcFrame(g, 22);
    CHECK(p.TakeCounts().gone == 1);
}

TEST_CASE("npc pose: a copy or source that is no live actor any more is neither written nor read") {
    // Review 2026-11-11 (oot-remake docs/REVISION_OBJETOS.md): the targets and
    // the source come from the last walk; by the calc frame the game may have
    // deleted that townsperson (a scene change, it walked out of range). Its
    // memory still held the skeleton chain, so the pose went into freed memory.
    FakeGuest g;
    const std::uint64_t copy = 0x20000000, source = 0x10000000;
    const std::uint64_t local = MakeActor(g, copy, 8);
    MakeActor(g, source, 8);
    NpcPose p;
    proto::ObjectPose m;
    m.player_number = 2;
    m.id = kMidoId;
    m.time = {1, 1};
    m.skeleton = CoopPose::SkeletonId(8, kMido);
    m.bones = {proto::PoseBone{3, 0, std::array<std::int16_t, 4>{0, 0, 12539, 30273}, std::nullopt,
                               std::nullopt}};
    p.Receive(m, 100);
    p.SetTargets({{kMidoId, copy}});

    // Deleted (state 8), its memory not yet reused: not written.
    g.Put(copy + 0x2c, std::uint32_t{8});
    p.OnCalcFrame(g, 101);
    CHECK(SameRotation(RotationOf(g, local, 3), {0, 0, 0, 1}));
    auto n = p.TakeCounts();
    CHECK(n.written == 0);
    CHECK(n.gone == 1);
    // Reads state 4 but is off the actor list (freed, reused): not written.
    g.Put(copy + 0x2c, std::uint32_t{4});
    g.Put(copy + kActorNode + kNodeOwner, std::uint64_t{0x55550000});
    p.OnCalcFrame(g, 102);
    CHECK(SameRotation(RotationOf(g, local, 3), {0, 0, 0, 1}));
    CHECK(p.TakeCounts().written == 0);
    // A live actor again: written.
    g.Put(copy + kActorNode + kNodeOwner, copy);
    p.OnCalcFrame(g, 103);
    CHECK(p.TakeCounts().written == 1);
    CHECK_FALSE(SameRotation(RotationOf(g, local, 3), {0, 0, 0, 1}));

    // The source deleted: nothing read, nothing to send.
    p.SetSource(CoopNpcPose::Source{kMidoId, 0, source});
    g.Put(source + 0x2c, std::uint32_t{8});
    p.OnCalcFrame(g, 110);
    CHECK(p.TakeOutgoing(200, 110).empty());
    n = p.TakeCounts();
    CHECK(n.read == 0);
    CHECK(n.not_read == 1);
}

TEST_CASE("npc pose: a track stuck at an old epoch is forgotten after kForgetMs (a restarted server)") {
    // Review 2026-11-11: a restarted server hands out epochs from 0 again. The
    // old track (epoch 7) refused each new pose as stale, and was only dropped
    // on a pose that was kept: with one townsperson talked to, never.
    NpcPose p;
    proto::ObjectPose m;
    m.id = kMidoId;
    m.skeleton = CoopPose::SkeletonId(8, kMido);
    m.bones = {proto::PoseBone{2, 0, std::array<std::int16_t, 4>{0, 0, 0, 32767}, std::nullopt,
                               std::nullopt}};
    const auto at = [&](std::uint16_t epoch, std::uint64_t t) {
        proto::ObjectPose x = m;
        x.player_number = 2;
        x.epoch = epoch;
        x.time = {t, 1};
        return x;
    };
    p.Receive(at(7, 9'000'000), 10);
    p.Receive(at(0, 100), 20);  // within kForgetMs of the last kept: stale, as before
    CHECK(p.TakeCounts().stale == 1);
    p.Receive(at(0, 200), 10 + CoopNpcPose::kForgetMs + 1);
    const auto heard = p.Fresh(10 + CoopNpcPose::kForgetMs + 1);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].epoch == 0);
    CHECK(p.TakeCounts().received == 1);
}

TEST_CASE("npc pose: the writer tells whether the game kept the last write") {
    FakeGuest g;
    const std::uint64_t copy = 0x20000000;
    const std::uint64_t local = MakeActor(g, copy, 8);
    NpcPose p;
    proto::ObjectPose m;
    m.player_number = 4;
    m.id = kMidoId;
    m.time = {1, 1};
    m.skeleton = CoopPose::SkeletonId(8, kMido);
    for (std::uint16_t b = 0; b < 8; ++b) {
        m.bones.push_back(proto::PoseBone{b, 0, std::array<std::int16_t, 4>{0, 12539, 0, 30273},
                                          std::nullopt, std::nullopt});
    }
    p.Receive(m, 100);
    p.SetTargets({{kMidoId, copy}});
    p.OnCalcFrame(g, 101);
    p.OnCalcFrame(g, 117);  // nothing wrote over it
    auto n = p.TakeCounts();
    CHECK(n.written == 2);
    CHECK(n.kept == 1);
    CHECK(n.overwritten == 0);
    // The copy's own animation writes bone 0 between our writes.
    SetRotation(g, local, 0, {0, 0, 0, 1});
    p.OnCalcFrame(g, 133);
    n = p.TakeCounts();
    CHECK(n.overwritten == 1);
    CHECK(SameRotation(RotationOf(g, local, 0), CoopPose::Unpack({0, 12539, 0, 30273})));
    // Reset forgets the track: nothing is written.
    p.Reset();
    p.OnCalcFrame(g, 150);
    CHECK(p.TakeCounts().written == 0);
}

TEST_CASE("npc pose: slot 0's command goes on each partial slot (step 6)") {
    FakeGuest g;
    const std::uint64_t actor = 0x40000000, a = actor + 0x1000, b = actor + 0x2000,
                        c = actor + 0x3000, controller = actor + 0x4000;
    for (std::uint64_t at : {actor, a, b, c, controller}) g.Map(at, 0x400);
    namespace pu = CoopPuppet::addr;
    g.Put(actor + pu::kControllerPath[0], a);
    g.Put(a + pu::kControllerPath[1], b);
    g.Put(b + pu::kControllerPath[2], c);
    g.Put(c + pu::kControllerPath[3], controller);
    g.Put(controller + pu::kSlotCount, std::int32_t{4});
    CHECK(CoopNpcPose::AsSlotCount(g, actor) == 4);
    CHECK(CoopNpcPose::AsSlotCount(g, 0x70000000) == 0);  // no AS chain
    const auto calls = CoopNpcPose::PartialCalls(kMain, actor, 4);
    REQUIRE(calls.size() == 3);
    for (std::size_t i = 0; i < calls.size(); ++i) {
        CHECK(calls[i].function == kMain + 0x3abec10);
        CHECK(calls[i].x[0] == actor);
        CHECK(calls[i].x[1] == 0);      // from slot 0
        CHECK(calls[i].x[2] == i + 1);  // to slots 1, 2, 3
        CHECK(calls[i].x[3] == 0);
        CHECK(calls[i].x[4] == 0);
    }
    CHECK(CoopNpcPose::PartialCalls(kMain, actor, 1).empty());  // slot 0 only
    CHECK(CoopNpcPose::PartialCalls(kMain, actor, 20).size() ==
          static_cast<std::size_t>(pu::kMaxAsSlots - 1));
    CHECK(CoopNpcPose::PartialCalls(0, actor, 4).empty());
}

TEST_CASE("npc pose: a copy our talk let go blends its bones back to the owner's pose") {
    // ORM "Dos jugadores con el mismo PNJ": while our player talked, our copy
    // showed our own game's talk (its head turned to our Link). When our talk
    // is over its owner's pose comes back blended (Target weight, from
    // Sync::RejoinWeight), not at once.
    FakeGuest g;
    constexpr std::uint32_t kBones = 16;
    const std::uint64_t copy_actor = 0x20000000;
    const std::uint64_t copy_local = MakeActor(g, copy_actor, kBones);
    const float s = std::sqrt(0.5f);
    const Quat ours{0, s, 0, s};  // our talk: head 90 degrees to our Link
    SetRotation(g, copy_local, 12, ours);
    g.Put(copy_local + 1 * kStride + 0x50, CoopPose::Vec3{0.4f, 0.1f, 0.0f});

    CoopPose::Pose owner_pose;
    owner_pose.skeleton = CoopPose::SkeletonId(kBones, kMido);
    CoopPose::BonePose root;
    root.bone = 1;
    root.translation = CoopPose::Vec3{0.0f, 0.1f, 0.0f};
    CoopPose::BonePose head;
    head.bone = 12;
    head.rotation = Quat{0, 0, 0, 1};  // the owner's Mido looks ahead
    owner_pose.bones = {root, head};

    // Half way: 45 degrees, the root half way back.
    REQUIRE(CoopPose::WriteActorPose(g, copy_actor, owner_pose, nullptr, nullptr, 0.5f) ==
            CoopPose::WriteResult::written);
    const Quat half{0, 0.38268343f, 0, 0.92387953f};
    CHECK(SameRotation(RotationOf(g, copy_local, 12), half));
    CHECK(Near(g.Get<float>(copy_local + 1 * kStride + 0x50), 0.2f));
    // Weight 0 leaves the copy as it is; 1 (and out of range) is the owner's pose.
    REQUIRE(CoopPose::WriteActorPose(g, copy_actor, owner_pose, nullptr, nullptr, 0.0f) ==
            CoopPose::WriteResult::written);
    CHECK(SameRotation(RotationOf(g, copy_local, 12), half));
    REQUIRE(CoopPose::WriteActorPose(g, copy_actor, owner_pose, nullptr, nullptr, 7.0f) ==
            CoopPose::WriteResult::written);
    CHECK(SameRotation(RotationOf(g, copy_local, 12), Quat{0, 0, 0, 1}));
    CHECK(Near(g.Get<float>(copy_local + 1 * kStride + 0x50), 0.0f));

    // Through NpcPose: a target with weight 0.25 moves a quarter of the way.
    SetRotation(g, copy_local, 12, ours);
    NpcPose owner;
    const std::uint64_t owner_actor = 0x10000000;
    const std::uint64_t owner_local = MakeActor(g, owner_actor, kBones);
    owner.SetSource(CoopNpcPose::Source{kMidoId, 1, owner_actor});
    owner.OnCalcFrame(g, 1000);
    SetRotation(g, owner_local, 12, Quat{0, 0, 0, 1});
    owner.OnCalcFrame(g, 1016);
    const auto out = owner.TakeOutgoing(5'000'000, 1016);
    REQUIRE(out.size() == 1);
    NpcPose copy;
    copy.Receive(Relayed(out[0], 2), 2000);
    copy.SetTargets({{kMidoId, copy_actor, 0.25f}});
    copy.OnCalcFrame(g, 2001);
    const Quat quarter = CoopPose::Nlerp(ours, Quat{0, 0, 0, 1}, 0.25f);
    CHECK(SameRotation(RotationOf(g, copy_local, 12), quarter));
    CHECK_FALSE(SameRotation(RotationOf(g, copy_local, 12), Quat{0, 0, 0, 1}));
    CHECK(copy.TakeCounts().written == 1);
    // Blended in: weight 1, the owner's head.
    copy.SetTargets({{kMidoId, copy_actor, 1.0f}});
    copy.OnCalcFrame(g, 2017);
    CHECK(SameRotation(RotationOf(g, copy_local, 12), Quat{0, 0, 0, 1}));
}

// ORM "PNJ con muchos huesos" (oot-remake docs/NPC_COMPARTIDOS.md): Ruto has
// 300 bones, Zelda 292, the Kokiri up to 264. Their talk poses were not sent
// at all (over the old 160-bone limit); now a pose goes in parts of 128 bones
// and each part carries only what changed.
namespace {

constexpr std::string_view kRuto = "Npc_Zora_Ruto_Adult";
const proto::ObjectId kRutoId{proto::ObjectKind::placed, 0, 0x5a07a000beefull};

}  // namespace

TEST_CASE("npc pose: a townsperson of 300 bones goes in three parts and reaches the copy whole") {
    FakeGuest g;
    constexpr std::uint32_t kBones = 300;
    const std::uint64_t owner_actor = 0x10000000, copy_actor = 0x20000000, direct = 0x30000000;
    const std::uint64_t owner_local = MakeActor(g, owner_actor, kBones, kRuto);
    const std::uint64_t copy_local = MakeActor(g, copy_actor, kBones, kRuto);
    const std::uint64_t direct_local = MakeActor(g, direct, kBones, kRuto);
    const float s = std::sqrt(0.5f);
    SetRotation(g, copy_local, 250, {0, s, 0, s});  // the copy's own AI: head to our Link

    NpcPose owner;
    owner.SetSource(CoopNpcPose::Source{kRutoId, 2, owner_actor});
    owner.OnCalcFrame(g, 1000);
    SetRotation(g, owner_local, 40, {0.25881904f, 0, 0, 0.96592583f});    // a hand, part 0
    SetRotation(g, owner_local, 140, {0, 0, 0.38268343f, 0.92387953f});   // part 1
    SetRotation(g, owner_local, 250, {0, -0.38268343f, 0, 0.92387953f});  // the head, part 1
    SetRotation(g, owner_local, 299, {0, 0.25881904f, 0, 0.96592583f});   // the last, part 2
    owner.OnCalcFrame(g, 1016);
    const auto out = owner.TakeOutgoing(5'000'000, 1016);
    REQUIRE(out.size() == 3);
    const std::size_t sizes[] = {128, 128, 44};
    for (std::size_t p = 0; p < out.size(); ++p) {
        INFO("part " << p);
        CHECK(out[p].part == p);
        CHECK(out[p].parts == 0x07);
        CHECK(out[p].time == out[0].time);  // one pose: one time for all its parts
        CHECK(out[p].skeleton == CoopPose::SkeletonId(kBones, kRuto));
        REQUIRE(out[p].bones.size() == sizes[p]);
        CHECK(out[p].bones.front().bone == p * 128);
        CHECK(out[p].bones.back().bone == p * 128 + sizes[p] - 1);
    }

    // The other game: two parts are not a pose yet; the third makes it whole.
    NpcPose copy;
    copy.SetTargets({{kRutoId, copy_actor}});
    copy.Receive(Relayed(out[0], 3), 2000);
    copy.Receive(Relayed(out[2], 3), 2001);
    CHECK(copy.Fresh(2001).empty());
    copy.OnCalcFrame(g, 2002);
    CHECK(copy.TakeCounts().written == 0);
    CHECK(SameRotation(RotationOf(g, copy_local, 250), {0, s, 0, s}));
    copy.Receive(Relayed(out[1], 3), 2003);
    const auto heard = copy.Fresh(2003);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].player == 3);
    copy.OnCalcFrame(g, 2004);
    REQUIRE(CoopPose::CopySkeleton(g, owner_actor, direct, nullptr));
    for (std::uint32_t b = 0; b < kBones; ++b) {
        INFO("bone " << b);
        CHECK(SameRotation(RotationOf(g, copy_local, b), RotationOf(g, direct_local, b)));
    }
    auto n = copy.TakeCounts();
    CHECK(n.received == 1);
    CHECK(n.written == 1);
    CHECK(n.incomplete == 0);
    CHECK(n.bad == 0);

    // Only the head turns on: one part with one bone, and not before the
    // bone budget allows (300 bones: 93 ms, not 50).
    const Quat on{0, -0.5f, 0, 0.8660254f};
    SetRotation(g, owner_local, 250, on);
    owner.OnCalcFrame(g, 1070);
    CHECK(owner.TakeOutgoing(5'054'000, 1070).empty());
    owner.OnCalcFrame(g, 1110);
    const auto delta = owner.TakeOutgoing(5'094'000, 1110);
    REQUIRE(delta.size() == 1);
    CHECK(delta[0].part == 1);
    CHECK(delta[0].parts == 0x02);
    CHECK(delta[0].time.sequence == out[0].time.sequence + 1);
    REQUIRE(delta[0].bones.size() == 1);
    CHECK(delta[0].bones[0].bone == 250);
    // The copy keeps the other 299 bones and shows the new head.
    copy.Receive(Relayed(delta[0], 3), 2050);
    copy.OnCalcFrame(g, 2200);
    CHECK(SameRotation(RotationOf(g, copy_local, 250), on));
    CHECK(SameRotation(RotationOf(g, copy_local, 40), RotationOf(g, direct_local, 40)));
    CHECK(SameRotation(RotationOf(g, copy_local, 299), RotationOf(g, direct_local, 299)));
    n = copy.TakeCounts();
    CHECK(n.received == 1);
    CHECK(n.written == 1);

    // The head again in the next two poses, then nothing; at the refresh
    // (500 ms after the first) every bone, in three parts.
    for (std::uint64_t t : {1170, 1230}) {
        owner.OnCalcFrame(g, t);
        const auto again = owner.TakeOutgoing(5'000'000 + 1000 * (t - 1016), t);
        REQUIRE(again.size() == 1);
        CHECK(again[0].bones == delta[0].bones);
    }
    owner.OnCalcFrame(g, 1290);
    CHECK(owner.TakeOutgoing(5'274'000, 1290).empty());
    owner.OnCalcFrame(g, 1516);
    const auto refresh = owner.TakeOutgoing(5'500'000, 1516);
    REQUIRE(refresh.size() == 3);
    CHECK(refresh[2].bones.size() == 44);
    n = owner.TakeCounts();
    CHECK(n.sent == 5);
    CHECK(n.sent_parts == 3 + 1 + 1 + 1 + 3);
    CHECK(n.sent_bones == 300 + 1 + 1 + 1 + 300);
    CHECK(n.bones == kBones);
}

TEST_CASE("npc pose: parts wait for their pose's mask, a newer pose replaces a lost one, bad parts are dropped") {
    FakeGuest g;
    constexpr std::uint32_t kBones = 300;
    const std::uint64_t copy_actor = 0x20000000;
    const std::uint64_t copy_local = MakeActor(g, copy_actor, kBones, kRuto);
    const std::array<std::int16_t, 4> y45{0, 12539, 0, 30273}, z45{0, 0, 12539, 30273};
    const auto part = [&](std::uint32_t seq, std::uint8_t index, std::uint8_t mask,
                          std::vector<std::uint16_t> bones, std::array<std::int16_t, 4> r) {
        proto::ObjectPose m;
        m.player_number = 2;
        m.id = kRutoId;
        m.epoch = 1;
        m.time = {100ull * seq, seq};
        m.skeleton = CoopPose::SkeletonId(kBones, kRuto);
        m.part = index;
        m.parts = mask;
        for (std::uint16_t b : bones) {
            m.bones.push_back(proto::PoseBone{b, 0, r, std::nullopt, std::nullopt});
        }
        return m;
    };
    NpcPose p;
    p.SetTargets({{kRutoId, copy_actor}});
    // Against its skeleton (the codec checked the ranges): 300 bones are
    // three parts, bones 0 to 299.
    p.Receive(part(1, 3, 0x08, {}, y45), 10);     // a fourth part
    p.Receive(part(1, 0, 0x09, {10}, y45), 10);   // a mask with a fourth part
    p.Receive(part(1, 2, 0x04, {300}, y45), 10);  // past the last bone
    proto::ObjectPose none = part(1, 0, 0x01, {10}, y45);
    none.skeleton = CoopPose::SkeletonId(0, kRuto);  // no bones at all
    p.Receive(none, 10);
    auto n = p.TakeCounts();
    CHECK(n.bad == 4);
    CHECK(n.received == 0);
    CHECK(p.Fresh(10).empty());  // nothing kept

    // Pose 1 in three parts; part 1 is lost.
    p.Receive(part(1, 0, 0x07, {10}, y45), 20);
    p.Receive(part(1, 2, 0x07, {260}, y45), 21);
    CHECK(p.Fresh(21).empty());
    p.OnCalcFrame(g, 22);
    CHECK(p.TakeCounts().written == 0);
    // Pose 2, part 0 only: shown, with what pose 1's parts brought.
    p.Receive(part(2, 0, 0x01, {10}, z45), 70);
    p.Receive(part(2, 0, 0x01, {10}, z45), 71);   // that part again
    p.Receive(part(1, 1, 0x07, {140}, y45), 72);  // pose 1's lost part, late
    n = p.TakeCounts();
    CHECK(n.received == 1);
    CHECK(n.incomplete == 1);
    CHECK(n.stale == 2);
    REQUIRE(p.Fresh(72).size() == 1);
    p.OnCalcFrame(g, 73);
    CHECK(p.TakeCounts().written == 1);
    CHECK(SameRotation(RotationOf(g, copy_local, 10), CoopPose::Unpack(z45)));
    CHECK(SameRotation(RotationOf(g, copy_local, 260), CoopPose::Unpack(y45)));
    CHECK(SameRotation(RotationOf(g, copy_local, 140), {0, 0, 0, 1}));  // never came
    // A part of the same pose with another mask: dropped.
    p.Receive(part(3, 0, 0x03, {10}, y45), 120);
    p.Receive(part(3, 1, 0x02, {140}, y45), 121);
    n = p.TakeCounts();
    CHECK(n.stale == 1);
    CHECK(n.received == 0);
}
