// ORM cases moved out of upstream's tests/lichii/puppet_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the puppet director and planner (lichii/hook/coop_puppet.cpp)
// and the cave's encoding (lichii/hook/coop_svc.h). No emulator code: guest
// memory is a set of fake pages.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "coop/host/player_reader.hpp"
#include "coop_detalles.h"
#include "coop_downed.h"
#include "coop_fairy.h"
#include "coop_hold.h"
#include "coop_mascaras.h"
#include "coop_pose.h"
#include "coop_puppet.h"
#include "coop_sites.h"
#include "coop_svc.h"
#include "../lichii/skeleton_fakes.hpp"

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): one Ocarina
// while a puppet plays: the belt one parked out of sight, judged by reading
// it back; put back when the hand one is put away.
#include "coop_check_visual.h"

namespace {

using CoopPuppet::Call;
using CoopPuppet::Director;
using CoopPuppet::Phase;
using CoopPuppet::Target;
namespace addr = CoopPuppet::addr;
namespace block = CoopPuppet::block;

constexpr std::uint64_t kBase = 0x80000000;    // main
constexpr std::uint64_t kHeap = 0x200000000;   // what the fake malloc gives
constexpr std::uint64_t kManager = 0x300000000;
constexpr std::uint64_t kDefault = kBase + 0x3ee4f8c;
constexpr std::uint64_t kRequest = 0x400000000;
constexpr std::uint64_t kActor = 0x400002000;
constexpr std::uint64_t kVtable = kBase + 0x4000000;

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
            if (it == pages_.end()) {
                return false;
            }
            o[i] = it->second[(address + i) & 0xfff];
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) {
                return false;
            }
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

// A game with the manager pointer, the walker's default and a create
// request that is not filled in until Spawn().
struct Game {
    FakeGuest guest;
    std::uint64_t create_result = 1;  // what the game's create returns
    bool queue_anyway = false;        // a request in the handle even so
    std::uint64_t unequip_result = 1; // what the game's armor take-off returns
    Game() {
        guest.Map(kBase + addr::kActorManagerPtr, 8);
        guest.Map(kBase + addr::kGotCreateDefault, 8);
        guest.Map(kHeap, 0x1000);
        guest.Map(kManager, 0x200);
        guest.Map(kRequest, 0x30);
        guest.Map(kActor, 0x400);
        guest.Set(kBase + addr::kActorManagerPtr, kManager);
        guest.Set(kBase + addr::kGotCreateDefault, kDefault);
    }
    // What the manager does: a request in the handle slot at once, loading
    // (state 2); later ready (3) with the actor, which is alive.
    void Request(std::uint64_t handle) {
        guest.Set(kRequest, std::uint32_t{2});
        guest.Set(handle, kRequest);
    }
    void Spawn(std::uint64_t handle, std::uint32_t state = 4) {
        Request(handle);
        guest.Set(kRequest, std::uint32_t{0x10000 | addr::kRequestReady});
        guest.Set(kRequest + addr::kRequestActor, kActor);
        guest.Set(kActor, kVtable);
        guest.Set(kActor + addr::kActorState, state);
    }
};

Target At(std::uint8_t number, float x, float y, float z, std::string actor = "Animal_Cow_LinkHouse") {
    Target t;
    t.number = number;
    t.actor = std::move(actor);
    t.position = {x, y, z};
    return t;
}

CoopPuppet::Input Wants(std::vector<Target> targets, std::string scene = "KokiriForest_Child") {
    CoopPuppet::Input in;
    in.main_base = kBase;
    in.scene = std::move(scene);
    in.targets = std::move(targets);
    return in;
}

// Runs the cave's loop for one frame as the cave would, with the game's
// functions faked. Returns the calls made.
std::vector<Call> Frame(Director& d, Game& g, std::uint64_t now_ms) {
    std::vector<Call> calls;
    auto call = d.Step(g.guest, false, 0, now_ms);
    while (call) {
        calls.push_back(*call);
        std::uint64_t result = 0;
        if (call->function == kBase + addr::kMalloc) {
            result = kHeap;
        } else if (call->function == kBase + addr::kCreate) {
            result = g.create_result;
            if (g.queue_anyway) {
                g.Request(call->x[3]);
            }
        } else if (call->function == kBase + addr::kRequestDelete) {
            result = 1;
        } else if (call->function == kBase + addr::kUnequipArmor) {
            result = g.unequip_result;
        } else if (call->function == kBase + addr::kTakeCreated) {
            REQUIRE(g.guest.Read(call->x[0] + addr::kRequestActor, &result, 8));
        }
        call = d.Step(g.guest, true, result, now_ms);
        REQUIRE(calls.size() < 16);
    }
    return calls;
}

void CheckPosition(FakeGuest& guest, std::uint64_t at, float x, float y, float z) {
    const auto p = guest.Get<std::array<float, 3>>(at);
    CHECK(p[0] == doctest::Approx(x));
    CHECK(p[1] == doctest::Approx(y));
    CHECK(p[2] == doctest::Approx(z));
}

// The AS chain of kActor (actor +0x270, +0x40, +0x18, +0x18), with one slot
// whose frame is `frame`.
constexpr std::uint64_t kAs = 0x500000000;
constexpr std::uint64_t kController = kAs + 0x100;
constexpr std::uint64_t kSlot0 = kAs + 0x200;
void GiveAnimation(Game& g, float frame) {
    g.guest.Map(kAs, 0x1000);
    g.guest.Set(kActor + 0x270, kAs);
    g.guest.Set(kAs + 0x40, kAs + 0x40);
    g.guest.Set(kAs + 0x40 + 0x18, kAs + 0x80);
    g.guest.Set(kAs + 0x80 + 0x18, kController);
    g.guest.Set(kController + addr::kSlotCount, std::int32_t{1});
    g.guest.Set(kController + addr::kSlots, kAs + 0x300);
    g.guest.Set(kAs + 0x300, kSlot0);
    g.guest.Set(kSlot0 + addr::kSlotFrame, frame);
}

Target Playing(std::string name, float frame, std::uint32_t serial,
               CoopPuppet::Show show = CoopPuppet::Show::show) {
    Target t = At(1, 4, 5, 6);
    t.anim = CoopPuppet::Anim{std::move(name), frame, 1.5f};
    t.anim_serial = serial;
    t.show = show;
    return t;
}

// A live puppet P1 at frame 15.
void MakeLive(Director& d, Game& g) {
    d.SetInput(Wants({At(1, 4, 5, 6)}));
    auto calls = Frame(d, g, 0);
    REQUIRE(calls.size() == 2);
    g.Spawn(calls[1].x[3]);
    Frame(d, g, 10);
    Frame(d, g, 15);
    REQUIRE(d.Slots()[0].phase == Phase::live);
}

}  // namespace

TEST_CASE("puppet planner: a player playing the Ocarina carries it in the hand (ORM issue 152)") {
    using namespace CoopPuppet;
    std::vector<Remote> remotes(1);
    remotes[0].number = 1;
    remotes[0].scene = "KokiriForest_Child";
    remotes[0].gear[kGearOcarinaHolder] = "Item_Ocarina_Fairy_Holder";
    remotes[0].gear[kGearOcarina] = "Item_Ocarina_Fairy";
    remotes[0].drawn = std::uint8_t{0x10};  // the shield on the arm
    auto plan = [&](Planner& p, const char* anim) {
        remotes[0].anim = Anim{anim, 0.0f, 1.0f};
        auto in = p.Plan(kBase, std::string("KokiriForest_Child"), std::nullopt, false, remotes, 0);
        REQUIRE(in.targets.size() == 1);
        return in.targets[0];
    };
    Planner p;  // the default: the left hand (slot 1)
    CHECK(p.settings().ocarina_hand == kGearSubWeapon);
    for (const char* anim : {"OcarinaReadyStart", "Ocarina_Child_A_lp", "Ocarina_Child_ZeldaLullaby", "DemoOcarina"}) {
        CAPTURE(anim);
        const Target t = plan(p, anim);
        CHECK(t.gear[kGearSubWeapon] == "Item_Ocarina_Fairy");
        REQUIRE(t.drawn);
        CHECK(*t.drawn == 0x12);  // slot 1 in hand, the shield as sent
        CHECK(t.gear[kGearOcarina] == "Item_Ocarina_Fairy");  // still worn on the belt
        CHECK(t.gear[kGearOcarinaHolder] == "Item_Ocarina_Fairy_Holder");
    }
    // Not playing (a song taught is a cutscene, "DemoOcarinaMelodyGet"): the player's own slot 1. The remake's
    // free play is "DemoOcarina" (ocarina-mano, 2026-11-12): playing, in the list above's sense.
    for (const char* anim : {"Wait", "DemoOcarinaMelodyGet", "DemoOcarinaWarp"}) {
        CAPTURE(anim);
        const Target t = plan(p, anim);
        CHECK_FALSE(t.gear[kGearSubWeapon]);
        REQUIRE(t.drawn);
        CHECK(*t.drawn == 0x10);
    }
    // Drawn bits not known yet: only slot 1 is asked.
    remotes[0].drawn.reset();
    CHECK(*plan(p, "Ocarina_Child_B_lp").drawn == 0x02);
    remotes[0].drawn = std::uint8_t{0};
    // A player with no Ocarina (or not known yet): nothing.
    remotes[0].gear[kGearOcarina] = "";
    CHECK_FALSE(plan(p, "OcarinaReadyStart").gear[kGearSubWeapon]);
    remotes[0].gear[kGearOcarina].reset();
    CHECK_FALSE(plan(p, "OcarinaReadyStart").gear[kGearSubWeapon]);
    remotes[0].gear[kGearOcarina] = "Item_Ocarina_Time";
    CHECK(plan(p, "Ocarina_Adult_A_lp").gear[kGearSubWeapon] == "Item_Ocarina_Time");

    // The right hand (the bow slot, 3), or off (the belt only).
    PlannerSettings s;
    s.ocarina_hand = kGearHeld;
    Planner bow(s);
    Target t = plan(bow, "OcarinaReadyStart");
    CHECK(t.gear[kGearHeld] == "Item_Ocarina_Time");
    CHECK_FALSE(t.gear[kGearSubWeapon]);
    CHECK(*t.drawn == 0x08);
    s.ocarina_hand = kOcarinaHandOff;
    Planner off(s);
    t = plan(off, "OcarinaReadyStart");
    CHECK_FALSE(t.gear[kGearSubWeapon]);
    CHECK_FALSE(t.gear[kGearHeld]);
    CHECK(*t.drawn == 0x00);
    // Only a carried hand piece can take it: not the sword, the belt or the gauntlets.
    for (const std::size_t bad : {std::size_t{0}, kGearOcarina, kGearHand, kGearCount}) {
        Target x;
        x.gear[kGearOcarina] = "Item_Ocarina_Fairy";
        CHECK_FALSE(OcarinaInHand(x, bad));
        CHECK_FALSE(x.drawn);
    }
}

TEST_CASE("puppet planner: LICHII_ORM_OCARINA_HAND picks the hand piece (ORM issue 152)") {
    using namespace CoopPuppet;
    CHECK(OcarinaHandPiece("") == kGearSubWeapon);
    CHECK(OcarinaHandPiece("sub") == kGearSubWeapon);
    CHECK(OcarinaHandPiece("1") == kGearSubWeapon);
    CHECK(OcarinaHandPiece("bow") == kGearHeld);
    CHECK(OcarinaHandPiece("0") == kOcarinaHandOff);
    CHECK(OcarinaHandPiece("off") == kOcarinaHandOff);
    CHECK(OcarinaHandPiece("no") == kOcarinaHandOff);     // coop_switch.h's off words
    CHECK(OcarinaHandPiece("FALSE") == kOcarinaHandOff);
}

TEST_CASE("puppet gear: the Ocarina goes into the puppet's hand while its player plays, and away after (ORM issue 152)") {
    using CoopPuppet::Anim;
    using CoopPuppet::kGearOcarina;
    using CoopPuppet::kGearOcarinaHolder;
    using CoopPuppet::kGearSubWeapon;
    using CoopPuppet::Planner;
    using CoopPuppet::Remote;
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    MakeLive(d, g);
    constexpr std::uint64_t kParts = 0x500000000;
    constexpr std::uint64_t kEu = kParts + 0x800;
    g.guest.Map(kParts, 0x3000);
    g.guest.Set(kActor + addr::kActorParts, kParts);
    g.guest.Set(kParts + addr::kPartsEquipmentUser, kEu);
    const std::uint64_t slot1_id = kEu + addr::kEquipSlots + addr::kEquipSlotStride * 1 + addr::kEquipSlotId;
    g.guest.Set(slot1_id, std::uint32_t{0xFFFFFFFF});  // no piece in slot 1 yet

    std::vector<Remote> remotes(1);
    remotes[0].number = 1;
    remotes[0].scene = "KokiriForest_Child";
    remotes[0].position = {4, 5, 6};
    remotes[0].gear[kGearOcarinaHolder] = "Item_Ocarina_Fairy_Holder";
    remotes[0].gear[kGearOcarina] = "Item_Ocarina_Fairy";
    remotes[0].drawn = std::uint8_t{0};
    Planner p;
    auto step = [&](const char* anim, std::uint64_t now) {
        remotes[0].anim = Anim{anim, 0.0f, 1.0f};
        auto in = p.Plan(kBase, std::string("KokiriForest_Child"), std::nullopt, false, remotes, now);
        REQUIRE(in.targets.size() == 1);
        Target t = in.targets[0];
        t.anim.reset();  // the gear only: this fake game has no AS chain
        t.actor = "Animal_Cow_LinkHouse";
        d.SetInput(Wants({t}));
        return Frame(d, g, now);
    };
    // Standing: the two belt pieces only.
    auto calls = step("Wait", 30);
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].x[1] == 13u);
    CHECK(calls[1].x[1] == 16u);
    CHECK(step("Wait", 40).empty());

    // Takes the Ocarina out: worn as the sub weapon (kind 1), then drawn into the hand on slot 1.
    calls = step("OcarinaReadyStart", 50);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquip);
    CHECK(calls[0].x[0] == kEu);
    CHECK(calls[0].x[1] == 1u);
    CHECK(calls[0].x[2] == kHeap + block::kGearPtr + 8 * kGearSubWeapon);
    std::array<char, block::kGearCapacity> name{};
    g.guest.Read(g.guest.Get<std::uint64_t>(calls[0].x[2]), name.data(), name.size());
    CHECK(std::string(name.data()) == "Item_Ocarina_Fairy");
    CHECK(step("Ocarina_Child_A_lp", 60).empty());  // the piece is not there yet
    g.guest.Set(slot1_id, std::uint32_t{9});         // the game made it
    calls = step("Ocarina_Child_A_lp", 70);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquipChangeState);
    CHECK(calls[0].x[1] == 1u);
    CHECK(calls[0].x[2] == addr::kEquipStateInHand);
    CHECK(step("Ocarina_Child_B_lp", 80).empty());  // not taken yet: waits before asking again
    g.guest.Set(kEu + addr::kEquipSlotBits + 1, addr::kEquipBitInHand);
    CHECK(step("Ocarina_Child_B_lp", 400).size() == 1);  // asked again, taken now
    CHECK(step("Ocarina_Child_B_lp", 410).empty());

    // Puts it away: the player's own slot 1 is not known (nothing sent), so the piece stays, put away.
    g.guest.Set(kEu + addr::kEquipSlotBits + 1, addr::kEquipBitAway);
    calls = step("Wait", 420);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquipChangeState);
    CHECK(calls[0].x[1] == 1u);
    CHECK(calls[0].x[2] == addr::kEquipStateAway);
    CHECK(step("Wait", 430).empty());
    const auto has = [&](const std::string& part) {
        return std::any_of(log.begin(), log.end(),
                           [&](const std::string& l) { return l.find(part) != std::string::npos; });
    };
    CHECK(has("coop: puppet: P1 wears sub weapon 'Item_Ocarina_Fairy'"));
    CHECK(has("coop: puppet: P1 equipment slot 1 in hand"));
    CHECK(has("coop: puppet: P1 equipment slot 1 put away"));

    // Plays again: the same piece is drawn again, not worn again.
    g.guest.Set(kEu + addr::kEquipSlotBits + 1, addr::kEquipBitInHand);
    calls = step("OcarinaReadyStart", 440);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquipChangeState);
    CHECK(calls[0].x[2] == addr::kEquipStateInHand);
}

TEST_CASE("puppet animation: an Ocarina note or song node it is given is logged (ORM issue 152)") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    MakeLive(d, g);
    GiveAnimation(g, 0.0f);
    const auto has = [&](const std::string& part) {
        return std::any_of(log.begin(), log.end(),
                           [&](const std::string& l) { return l.find(part) != std::string::npos; });
    };
    d.SetInput(Wants({Playing("Ocarina_Child_A_lp", 0.0f, 1)}));
    Frame(d, g, 30);
    CHECK(has("coop: puppet: P1 ocarina note A ('Ocarina_Child_A_lp')"));
    d.SetInput(Wants({Playing("Ocarina_Child_ZeldaLullaby", 0.0f, 2)}));
    Frame(d, g, 40);
    CHECK(has("coop: puppet: P1 ocarina song ZeldaLullaby ('Ocarina_Child_ZeldaLullaby')"));
    const auto before = log.size();
    d.SetInput(Wants({Playing("Wait", 0.0f, 3)}));
    Frame(d, g, 50);
    CHECK(std::none_of(log.begin() + static_cast<std::ptrdiff_t>(before), log.end(),
                       [](const std::string& l) { return l.find(" ocarina ") != std::string::npos; }));
}

namespace {
std::uint32_t CaveWord(std::size_t at) {
    std::uint32_t w = 0;
    std::memcpy(&w, CoopDowned::kCave.data() + at, 4);
    return w;
}
std::int64_t Signed(std::uint32_t imm, int bits) {
    const std::int64_t v = imm;
    return (v & (std::int64_t{1} << (bits - 1))) ? v - (std::int64_t{1} << bits) : v;
}
}  // namespace

TEST_CASE("downed gate cave: the call, the tail branch and the gate word decode right") {
    using namespace CoopDowned;
    // The game's word is bl 0x4aa890; ours lands on the cave.
    auto bl_target = [](std::uint64_t at, std::uint32_t word) {
        REQUIRE((word & 0xfc000000u) == 0x94000000u);
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(at) +
                                          Signed(word & 0x3ffffff, 26) * 4);
    };
    CHECK(bl_target(kHookOffset, kHookOriginal) == kStartEvent);
    CHECK(bl_target(kHookOffset, HookWord()) == kCaveOffset);
    CHECK(HookWord() == 0x94de6051u);
    // After the AI hold's table, before rodata; the gate word past the code.
    CHECK(kCaveOffset >= CoopHold::kTableOffset + CoopHold::kTableSize);
    CHECK(kCaveOffset + kCave.size() <= kGateOffset);
    CHECK(kGateEnd <= CoopSvc::kCaveEnd);
    // +0x00 adr x16, gate word.
    const std::uint32_t adr = CaveWord(0);
    REQUIRE((adr & 0x9f00001fu) == 0x10000010u);
    const std::uint32_t adr_imm = ((adr >> 5) & 0x7ffff) << 2 | ((adr >> 29) & 3);
    CHECK(kCaveOffset + Signed(adr_imm, 21) == kGateOffset);
    // +0x04 ldr w16, [x16]; +0x08 cbnz w16 to the closed path at +0x10.
    CHECK(CaveWord(4) == 0xb9400210u);
    const std::uint32_t cbnz = CaveWord(8);
    REQUIRE((cbnz & 0xff00001fu) == 0x35000010u);
    CHECK(8 + Signed((cbnz >> 5) & 0x7ffff, 19) * 4 == 0x10);
    // +0x0c b 0x4aa890: open, the game's own call, returning to its caller.
    const std::uint32_t b = CaveWord(0xc);
    REQUIRE((b & 0xfc000000u) == 0x14000000u);
    CHECK(static_cast<std::uint64_t>(static_cast<std::int64_t>(kCaveOffset + 0xc) +
                                     Signed(b & 0x3ffffff, 26) * 4) == kStartEvent);
    // +0x10 mov w0, wzr; +0x14 ret: closed, "not started" (the caller's tbnz
    // w0, #0 then skips the game over, as with no death pending).
    CHECK(CaveWord(0x10) == 0x2a1f03e0u);
    CHECK(CaveWord(0x14) == 0xd65f03c0u);
    CHECK(kCave.size() == 0x18);
}

namespace {
// A skeleton on kActor the pose copy finds (coop_pose.h, kSkeletonPath):
// actor +0x270 (kAs), +0x8, +0x18, +0x18, then the model object's holder.
// kSkelBones bones, no model name. Returns the local transforms.
constexpr std::uint64_t kSk = 0x680000000;
constexpr std::uint32_t kSkelBones = 4;
std::uint64_t GiveSkeleton(Game& g) {
    g.guest.Map(kAs, 0x1000);
    g.guest.Map(kSk, 0x1000);
    g.guest.Set(kActor + 0x270, kAs);
    g.guest.Set(kAs + 0x8, kSk);
    g.guest.Set(kSk + 0x18, kSk + 0x100);
    g.guest.Set(kSk + 0x100 + 0x18, kSk + 0x200);   // the holder
    g.guest.Set(kSk + 0x200 + 0x140, kSk + 0x400);  // the skeleton object
    g.guest.Set(kSk + 0x200 + 0x120, kSk + 0x500);  // the model resource
    g.guest.Set(kSk + 0x500, std::array<char, 4>{'F', 'M', 'D', 'L'});
    g.guest.Set(kSk + 0x400 + 0x48, static_cast<std::uint16_t>(kSkelBones));
    g.guest.Set(kSk + 0x400 + 0x18, kSk + 0x800);
    return kSk + 0x800;
}

coop::protocol::PlayerPose PoseFor(std::uint8_t number) {
    coop::protocol::PlayerPose m;
    m.player_number = number;
    m.skeleton = CoopPose::SkeletonId(kSkelBones, "");
    coop::protocol::PoseBone b;
    b.bone = 1;
    b.translation = CoopPose::PackUnit({0.25f, 0.0f, 0.0f});
    m.bones = {b};
    return m;
}

bool Calls(const std::vector<Call>& calls, std::uint64_t function) {
    return std::any_of(calls.begin(), calls.end(),
                       [&](const Call& c) { return c.function == kBase + function; });
}

std::size_t Count(const std::vector<std::string>& log, const std::string& text) {
    return static_cast<std::size_t>(std::count_if(log.begin(), log.end(), [&](const std::string& l) {
        return l.find(text) != std::string::npos;
    }));
}
}  // namespace

TEST_CASE("puppet reveal: made parked, shown with its first pose and a fresh sample (no T-pose)") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    d.SetPoseCopy(true);
    d.SetRevealGate(true);
    CoopPose::SetLog({}, false);
    constexpr std::uint64_t t0 = 50'000'000;  // apart from the pose tests' times
    const float down = 5.0f - Director::kHideDrop;
    Target t = At(6, 4, 5, 6);
    t.sample_age_ms = 900;  // an old sample
    t.gear[0] = "Weapon_Sword_Kokiri";
    d.SetInput(Wants({t}));
    auto calls = Frame(d, g, t0);
    REQUIRE(calls.size() == 2);
    CheckPosition(g.guest, kHeap + 0x00, 4, down, 6);  // made under its spot
    g.Spawn(calls[1].x[3]);
    const std::uint64_t local = GiveSkeleton(g);
    g.guest.Set(kAs + addr::kPartsEquipmentUser, kAs + 0x800);  // gear could go on
    Frame(d, g, t0 + 10);  // taken
    Frame(d, g, t0 + 20);  // live
    REQUIRE(d.Slots()[0].phase == Phase::live);
    calls = Frame(d, g, t0 + 30);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);
    CHECK((d.LiveMask() & (1u << 6)) == 0u);  // its Navi stays parked as well
    CHECK_FALSE(Calls(calls, addr::kEquip));   // nothing made under the floor

    // A pose comes and is written on it, out of sight; the sample is still old.
    CoopPose::Receive(PoseFor(6), t0 + 35);
    Frame(d, g, t0 + 40);
    Frame(d, g, t0 + 50);
    CHECK(g.guest.Get<float>(local + 1 * 0x60 + 0x50) == doctest::Approx(0.25f).epsilon(1e-3));
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);

    // A fresh sample: shown at it in this frame, and the gear goes on.
    t.sample_age_ms = 40;
    t.position = {7, 5, 6};
    d.SetInput(Wants({t}));
    calls = Frame(d, g, t0 + 60);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 7, 5, 6);
    CHECK((d.LiveMask() & (1u << 6)) != 0u);
    CHECK(Calls(calls, addr::kEquip));
    CHECK(Count(log, "parked until it has a pose and a fresh sample") == 1);
    CHECK(Count(log, "P6 shown after 40 ms (new): its pose on it, sample 40 ms old") == 1);

    // Out of the plan for 1 s (a reconnect), then back at a new place with
    // only the old pose: parked again, not snapped there.
    d.SetInput(Wants({}));
    Frame(d, g, t0 + 100);
    Frame(d, g, t0 + 1100);
    REQUIRE(d.Slots()[0].phase == Phase::live);  // within the leave grace
    t.position = {9, 5, 6};
    t.sample_age_ms = 30;
    d.SetInput(Wants({t}));
    Frame(d, g, t0 + 1110);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 9, down, 6);
    CHECK(Count(log, "P6 back after 1010 ms out of the plan") == 1);
    // New poses fade in on it while it is parked; then it shows.
    CoopPose::Receive(PoseFor(6), t0 + 1120);
    Frame(d, g, t0 + 1130);
    Frame(d, g, t0 + 1140);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 9, down, 6);
    CoopPose::Receive(PoseFor(6), t0 + 1120 + CoopPose::kResumeMs);
    Frame(d, g, t0 + 1120 + CoopPose::kResumeMs);
    Frame(d, g, t0 + 1130 + CoopPose::kResumeMs);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 9, 5, 6);
    CHECK(Count(log, "P6 shown after") == 2);
    CHECK(Count(log, "(back): its pose on it") == 1);
}

TEST_CASE("puppet reveal: without poses its AS command shows it, with nothing it shows after the wait") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    d.SetRevealGate(true);
    const float down = 5.0f - Director::kHideDrop;
    Target t = Playing("Wait", 4.0f, 1);  // P1 at (4, 5, 6)
    t.sample_age_ms = 20;
    d.SetInput(Wants({t}));
    auto calls = Frame(d, g, 0);
    REQUIRE(calls.size() == 2);
    g.Spawn(calls[1].x[3]);
    Frame(d, g, 10);
    Frame(d, g, 20);
    REQUIRE(d.Slots()[0].phase == Phase::live);
    Frame(d, g, 30);  // no AS chain yet: nothing played, parked
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);
    GiveAnimation(g, 0.0f);
    calls = Frame(d, g, 40);  // played and its frame set, still parked
    CHECK(Calls(calls, addr::kPlayAs));
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);
    Frame(d, g, 50);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    CHECK(Count(log, "P1 shown after 30 ms (new): its animation playing, sample 20 ms old") == 1);

    // A moment out of the plan (under kBackHideMs) is no reconnect: not hidden.
    d.SetInput(Wants({}));
    Frame(d, g, 60);
    d.SetInput(Wants({t}));
    Frame(d, g, 60 + Director::kBackHideMs - 1);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    CHECK(Count(log, "back after") == 0);

    // A player with nothing to show it with: shown after the wait, with a line.
    Game g2;
    std::vector<std::string> log2;
    Director d2([&](const std::string& line) { log2.push_back(line); });
    d2.SetRevealGate(true);
    Target bare = At(2, 4, 5, 6);
    bare.sample_age_ms = 20;
    d2.SetInput(Wants({bare}));
    calls = Frame(d2, g2, 0);
    g2.Spawn(calls[1].x[3]);
    Frame(d2, g2, 10);
    Frame(d2, g2, 20);
    Frame(d2, g2, 20 + Director::kRevealTimeoutMs - 1);
    CheckPosition(g2.guest, kActor + addr::kActorPosition, 4, down, 6);
    Frame(d2, g2, 20 + Director::kRevealTimeoutMs);
    CheckPosition(g2.guest, kActor + addr::kActorPosition, 4, 5, 6);
    CHECK(Count(log2, "P2 shown after 3000 ms (new): no pose yet, sample 20 ms old") == 1);
    CHECK(Count(log2, "waited the most") == 1);

    // The debug puppet is made where it stands.
    Game g3;
    Director d3;
    d3.SetRevealGate(true);
    d3.SetInput(Wants({At(0, 4, 5, 6)}));
    calls = Frame(d3, g3, 0);
    REQUIRE(calls.size() == 2);
    CheckPosition(g3.guest, kHeap + 0x00, 4, 5, 6);
}

TEST_CASE("puppet reveal: one the game takes out of state 4 while parked is made again, not left under the floor") {
    // Review 2026-11-11 (oot-remake docs/REVISION_OBJETOS.md): a live puppet
    // the game puts in a state that is neither present (4) nor gone (7 to 11),
    // such as 6 (seen on a Gold Skulltula 25 m from Link, docs/objects.md), was
    // never posed, never revealed and never deleted: parked 500 below for good.
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    d.SetRevealGate(true);
    const float down = 5.0f - Director::kHideDrop;
    Target t = At(3, 4, 5, 6);
    t.sample_age_ms = 20;
    d.SetInput(Wants({t}));
    auto calls = Frame(d, g, 0);
    REQUIRE(calls.size() == 2);
    g.Spawn(calls[1].x[3]);
    Frame(d, g, 10);
    Frame(d, g, 20);
    REQUIRE(d.Slots()[0].phase == Phase::live);
    Frame(d, g, 30);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);  // waits for its reveal
    g.guest.Set(kActor + addr::kActorState, std::uint32_t{6});
    Frame(d, g, 40);
    // Not for a moment: 3 s (Director::kAbsentMs) out of state 4 first.
    calls = Frame(d, g, 40 + 3000 - 1);
    CHECK_FALSE(Calls(calls, addr::kRequestDelete));
    CHECK(d.Slots()[0].phase == Phase::live);
    calls = Frame(d, g, 40 + 3000);
    CHECK(Calls(calls, addr::kRequestDelete));
    CHECK(Count(log, "P3 not present (state 6) for 3000 ms: made again") == 1);
    // Deleted, it is made again, parked for its reveal as any new one.
    g.guest.Set(kActor + addr::kActorState, std::uint32_t{8});
    calls = Frame(d, g, 40 + 3010);
    REQUIRE(Calls(calls, addr::kCreate));
    CheckPosition(g.guest, kHeap + 0x00, 4, down, 6);

    // Hidden on purpose (our cutscene): it may stay out of state 4, it is kept.
    Game g2;
    Director d2;
    Target shown = At(4, 4, 5, 6);
    d2.SetInput(Wants({shown}));
    calls = Frame(d2, g2, 0);
    g2.Spawn(calls[1].x[3]);
    Frame(d2, g2, 10);
    Frame(d2, g2, 20);
    REQUIRE(d2.Slots()[0].phase == Phase::live);
    Target hidden = shown;
    hidden.show = CoopPuppet::Show::hide;
    d2.SetInput(Wants({hidden}));
    g2.guest.Set(kActor + addr::kActorState, std::uint32_t{5});
    for (std::uint64_t now = 30; now <= 8000; now += 500) {
        CHECK_FALSE(Calls(Frame(d2, g2, now), addr::kRequestDelete));
    }
    // Back in state 4 and shown: the count starts over.
    g2.guest.Set(kActor + addr::kActorState, std::uint32_t{4});
    d2.SetInput(Wants({shown}));
    Frame(d2, g2, 8100);
    g2.guest.Set(kActor + addr::kActorState, std::uint32_t{6});
    CHECK_FALSE(Calls(Frame(d2, g2, 8200), addr::kRequestDelete));
    CHECK_FALSE(Calls(Frame(d2, g2, 8200 + 3000 - 1), addr::kRequestDelete));
    CHECK(Calls(Frame(d2, g2, 8200 + 3000), addr::kRequestDelete));
}

TEST_CASE("puppet planner: a puppet on our Link after an event is drawn a step aside, its sample kept") {
    using CoopPuppet::Planner;
    using CoopPuppet::Vec3;
    Planner p;
    std::vector<CoopPuppet::Remote> remotes(1);
    remotes[0].number = 2;
    remotes[0].scene = "DekuTree";
    remotes[0].position = {0.2f, 0.0f, 0.0f};
    remotes[0].rotation = CoopPuppet::Quat{0, 0, 0, 1};
    const Vec3 ours{0, 0, 0};
    const auto plan = [&](Planner& planner, bool event, std::uint64_t at) {
        return planner.Plan(kBase, std::string("DekuTree"), ours, event, remotes, at).targets.at(0);
    };
    // Seen first 3 m off, then on one spot with no event before: left alone.
    remotes[0].position = {3.0f, 0.0f, 0.0f};
    CHECK(plan(p, false, 500).position[0] == 3.0f);
    remotes[0].position = {0.2f, 0.0f, 0.0f};
    CHECK(plan(p, false, 1000).position[0] == 0.2f);
    CHECK(plan(p, false, 3000).position[0] == 0.2f);
    // The Tree's talk runs, then ends: after 1 s on one spot, a step aside.
    plan(p, true, 4000);
    CHECK(plan(p, false, 5000).side_step == Vec3{});
    CHECK(plan(p, false, 5900).side_step == Vec3{});
    auto t = plan(p, false, 6000);
    CHECK(t.side_step[0] == doctest::Approx(Planner::kSideStep));  // away from us, along +x
    CHECK(t.position[0] > 0.2f);
    CHECK(t.position[0] < 0.2f + Planner::kSideStep);  // fading in
    plan(p, false, 6100);
    t = plan(p, false, 6200);
    CHECK(t.position[0] == doctest::Approx(0.2f + Planner::kSideStep));
    CHECK(remotes[0].position[0] == 0.2f);  // the sample is theirs, untouched
    // They walk off: the step fades out.
    remotes[0].position = {1.4f, 0.0f, 0.0f};
    t = plan(p, false, 6300);
    CHECK(t.position[0] > 1.4f);
    CHECK(t.position[0] < 1.4f + Planner::kSideStep);
    for (std::uint64_t at = 6400; at <= 6800; at += 100) {
        t = plan(p, false, at);
    }
    CHECK(t.side_step == Vec3{});
    CHECK(t.position[0] == 1.4f);

    // Their cutscene ending counts too; facing away, the step still goes away from us.
    Planner q;
    remotes[0].position = {3.0f, 0.0f, 0.0f};
    plan(q, false, 500);
    remotes[0].position = {0.2f, 0.0f, 0.0f};
    remotes[0].rotation = CoopPuppet::Quat{0, 1, 0, 0};  // half a turn: its x axis is our -x
    remotes[0].status = coop::protocol::status::in_cutscene;
    plan(q, false, 1000);
    remotes[0].status = 0;
    plan(q, false, 2000);
    t = plan(q, false, 3000);
    CHECK(t.side_step[0] == doctest::Approx(Planner::kSideStep));

    // The director logs the step once each way.
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    MakeLive(d, g);
    Target stepped = At(1, 4.6f, 5, 6);
    stepped.side_step = {0.6f, 0.0f, 0.0f};
    d.SetInput(Wants({stepped}));
    Frame(d, g, 20);
    Frame(d, g, 30);
    CHECK(Count(log, "P1 stands on our Link after an event: drawn 0.60 to its side") == 1);
    d.SetInput(Wants({At(1, 4, 5, 6)}));
    Frame(d, g, 40);
    CHECK(Count(log, "P1 drawn on its sample again") == 1);
}

// ORM (oot-remake docs/SONDAS.md round 7, sondas22r7): two players who pressed Continue at one spot.
TEST_CASE("puppet planner: a puppet first seen on our Link is drawn a full step aside at once") {
    using CoopPuppet::Planner;
    using CoopPuppet::Vec3;
    Planner p;
    std::vector<CoopPuppet::Remote> remotes(1);
    remotes[0].number = 1;
    remotes[0].scene = "KokiriForest_Child";
    remotes[0].position = {0.07f, 0.0f, 0.16f};  // B's Continue spot, 0.17 m from ours
    remotes[0].rotation = CoopPuppet::Quat{0, 0, 0, 1};
    const Vec3 ours{0, 0, 0};
    const auto plan = [&](bool event, std::uint64_t at) {
        return p.Plan(kBase, std::string("KokiriForest_Child"), ours, event, remotes, at).targets.at(0);
    };
    auto t = plan(false, 1000);
    CHECK(t.side_step[0] == doctest::Approx(Planner::kFirstSightStep));  // its +x, away from us
    CHECK(t.position[0] == doctest::Approx(0.07f + Planner::kFirstSightStep));  // whole at once
    CHECK(remotes[0].position[0] == 0.07f);  // the sample is theirs
    // It stays while they stand there, through their boot event too (sondas26r7); we walk off past
    // kSideStepApart: it fades back onto their sample.
    remotes[0].status = coop::protocol::status::in_cutscene;
    t = plan(false, 1100);
    CHECK(t.position[0] == doctest::Approx(0.07f + Planner::kFirstSightStep));
    remotes[0].status = 0;
    t = plan(false, 3000);
    CHECK(t.position[0] == doctest::Approx(0.07f + Planner::kFirstSightStep));
    const Vec3 far{-2.0f, 0.0f, 0.0f};
    for (std::uint64_t at = 3100; at <= 4000; at += 100) {
        t = p.Plan(kBase, std::string("KokiriForest_Child"), far, false, remotes, at).targets.at(0);
    }
    CHECK(t.side_step == Vec3{});
    CHECK(t.position[0] == 0.07f);
    // Our Link not read yet at its first plans: the first sight is the first plan with it read, in the
    // game's boot event too (sondas23r7).
    Planner q;
    auto u = q.Plan(kBase, std::string("KokiriForest_Child"), std::nullopt, false, remotes, 1000).targets.at(0);
    CHECK(u.side_step == Vec3{});
    u = q.Plan(kBase, std::string("KokiriForest_Child"), ours, true, remotes, 1100).targets.at(0);
    CHECK(u.side_step[0] == doctest::Approx(Planner::kFirstSightStep));
    // A mate still loading (its puppet kept out of sight, arriving): judged once it is drawn.
    {
        Planner a;
        remotes[0].scene.reset();  // still on the title screen: no puppet here
        CHECK(a.Plan(kBase, std::string("KokiriForest_Child"), ours, false, remotes, 1000).targets.empty());
        remotes[0].scene = "KokiriForest_Child";  // lands on our spot: aside as soon as it is drawn
        CoopPuppet::Target h;
        for (std::uint64_t at = 1100; at <= 12000; at += 100) {
            h = a.Plan(kBase, std::string("KokiriForest_Child"), ours, false, remotes, at).targets.at(0);
            if (h.show == CoopPuppet::Show::show) break;
        }
        CHECK(h.show == CoopPuppet::Show::show);
        CHECK(h.side_step[0] == doctest::Approx(Planner::kFirstSightStep));
    }
    // Only the first sight: one that comes onto us later, with no event before, is left alone.
    Planner w;
    remotes[0].position = {3.0f, 0.0f, 0.0f};
    w.Plan(kBase, std::string("KokiriForest_Child"), ours, false, remotes, 1000);
    remotes[0].position = {0.07f, 0.0f, 0.16f};
    u = w.Plan(kBase, std::string("KokiriForest_Child"), ours, false, remotes, 1100).targets.at(0);
    CHECK(u.side_step == Vec3{});
}

// ORM smoothing (coop/host/pose_smooth.hpp, oot-remake docs/SUAVIZADO.md).
TEST_CASE("puppet smoothing: the calc frame's place comes from the smoother, side step added") {
    Game g;
    Director d;
    std::vector<std::uint64_t> asked;
    bool have = true;
    d.SetSmoothing(
        [&](std::uint8_t number, std::uint64_t server_now_us) -> std::optional<coop::host::SmoothedPose> {
            CHECK(number == 1);
            asked.push_back(server_now_us);
            if (!have) return std::nullopt;
            coop::host::SmoothedPose p;
            p.position = {1.0f, 2.0f, 3.0f};
            return p;
        },
        100'000);
    MakeLive(d, g);
    asked.clear();
    auto in = Wants({At(1, 4, 5, 6)});
    in.targets[0].offset = {0.5f, 0.0f, 0.0f};
    in.server_now_us = 5'000'000;
    in.server_at_ms = 20;
    d.SetInput(in);
    Frame(d, g, 36);  // 16 ms after the plan: the server time run on
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == 5'016'000);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 1.5f, 2.0f, 3.0f);
    // Nothing from the smoother yet: the plan's place, the 3.6 m step eased there (test10's
    // issue 194 draw, coop_snap.h: drawn where it was, the gap closed over EaseMs).
    have = false;
    Frame(d, g, 52);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 1.5f, 2.0f, 3.0f);
    Frame(d, g, 52 + CoopSnap::EaseMs(CoopSnap::Distance({1.5f, 2.0f, 3.0f}, {4, 5, 6})));
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    // No server time known: not asked at all.
    have = true;
    asked.clear();
    d.SetInput(Wants({At(1, 7, 8, 9)}));
    Frame(d, g, 800);
    CHECK(asked.empty());
    Frame(d, g, 800 + CoopSnap::EaseMs(CoopSnap::Distance({4, 5, 6}, {7, 8, 9})));
    CHECK(asked.empty());
    CheckPosition(g.guest, kActor + addr::kActorPosition, 7, 8, 9);
}

TEST_CASE("puppet smoothing: a rate change alone plays the command again") {
    Game g;
    Director d;
    MakeLive(d, g);
    GiveAnimation(g, 0.0f);
    d.SetInput(Wants({Playing("Move", 4.0f, 1)}));
    auto calls = Frame(d, g, 30);
    REQUIRE_FALSE(calls.empty());
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    const std::uint64_t request = calls[0].x[0];
    Frame(d, g, 40);
    auto faster = Playing("Move", 5.0f, 2);
    faster.anim->rate = 2.5f;
    d.SetInput(Wants({faster}));
    calls = Frame(d, g, 50);
    REQUIRE_FALSE(calls.empty());
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    CHECK(g.guest.Get<float>(request + 0x14) == 2.5f);
    // The same rate again: no new play.
    auto same = Playing("Move", 5.5f, 3);
    same.anim->rate = 2.5f;
    d.SetInput(Wants({same}));
    calls = Frame(d, g, 60);
    for (const auto& c : calls) CHECK(c.function != kBase + addr::kPlayAs);
}

TEST_CASE("puppet smoothing: the AS fallback plays the sender's command of the shown time") {
    Game g;
    Director d;
    d.SetSmoothing(
        [](std::uint8_t, std::uint64_t) -> std::optional<coop::host::SmoothedPose> {
            coop::host::SmoothedPose p;
            p.position = {4.0f, 5.0f, 6.0f};
            p.animation = coop::protocol::Animation{"Wait", 4.0f, 1.0f};
            p.animation_sequence = 7;
            return p;
        },
        100'000);
    MakeLive(d, g);
    GiveAnimation(g, 0.0f);
    auto in = Wants({Playing("Run", 9.0f, 1)});  // the newest: already running
    in.server_now_us = 5'000'000;
    in.server_at_ms = 20;
    d.SetInput(in);
    const auto calls = Frame(d, g, 30);
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    CHECK(d.Slots()[0].anim_name == "Wait");
    CHECK(calls[1].s0 == 4.0f);
}

// ORM efectos2 (lichii/hook/coop_effects.h): what a puppet shows beyond the
// copied pose. P4 and times from 70 000 000 ms keep these apart from the
// other tests' pose tracks (CoopPose's state is one for the process).
namespace {
void MakeLiveAt(Director& d, Game& g, std::uint8_t number, std::uint64_t t) {
    d.SetInput(Wants({At(number, 4, 5, 6)}));
    auto calls = Frame(d, g, t);
    REQUIRE(calls.size() == 2);
    g.Spawn(calls[1].x[3]);
    Frame(d, g, t + 10);
    Frame(d, g, t + 15);
    REQUIRE(d.Slots()[0].phase == Phase::live);
}

std::string PlayedName(Game& g) {
    const auto at = g.guest.Get<std::uint64_t>(kHeap + block::kPlayRequest + 0x08);
    std::array<char, block::kAnimCapacity> name{};
    REQUIRE(g.guest.Read(at, name.data(), name.size()));
    return std::string(name.data());
}
}  // namespace

TEST_CASE("puppet effects: an allowed command plays under the copied pose, the rest after it, never a skipped one") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    d.SetPoseCopy(true);
    CoopPose::SetLog({}, false);
    constexpr std::uint64_t t0 = 70'000'000;
    MakeLiveAt(d, g, 4, t0);
    GiveAnimation(g, 0.0f);
    CoopPose::Receive(PoseFor(4), t0 + 20);  // its poses come: the pose copy drives it
    REQUIRE(CoopPose::Drives(4, t0 + 30));

    Target t = At(4, 4, 5, 6);
    t.effects = true;
    // An attack (a hitbox): not played, as before.
    t.anim = CoopPuppet::Anim{"Sword_Attack_S_R", 2.0f, 1.0f};
    t.anim_serial = 1;
    d.SetInput(Wants({t}));
    CHECK_FALSE(Calls(Frame(d, g, t0 + 30), addr::kPlayAs));

    // Din's Fire's cast: played at its frame, for its effects.
    t.anim = CoopPuppet::Anim{"Magic_Din_St", 2.0f, 1.0f};
    t.anim_serial = 2;
    d.SetInput(Wants({t}));
    auto calls = Frame(d, g, t0 + 40);
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    CHECK(PlayedName(g) == "Magic_Din_St");
    CHECK(calls[1].function == kBase + addr::kSetAsFrame);
    CHECK(calls[1].s0 == 2.0f);
    CHECK(d.Slots()[0].anim_name == "Magic_Din_St");
    CHECK(Count(log, "P4 effects: 'Magic_Din_St' played under the copied pose") == 1);
    CHECK(Frame(d, g, t0 + 50).empty());

    // An attack after it: the rest command once, so the cast's effect stops.
    t.anim = CoopPuppet::Anim{"Sword_Attack_S_R", 1.0f, 1.0f};
    t.anim_serial = 3;
    d.SetInput(Wants({t}));
    calls = Frame(d, g, t0 + 60);
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    CHECK(PlayedName(g) == "Wait");
    CHECK(calls[1].s0 == 0.0f);
    CHECK(d.Slots()[0].anim_name == "Wait");
    CHECK(Frame(d, g, t0 + 70).empty());
    t.anim_serial = 4;
    d.SetInput(Wants({t}));
    CHECK(Frame(d, g, t0 + 80).empty());

    // Without the effects (the switch off): nothing while posed, as before.
    t.effects = false;
    t.anim = CoopPuppet::Anim{"Swim_Move_F", 1.0f, 1.0f};
    t.anim_serial = 5;
    d.SetInput(Wants({t}));
    CHECK(Frame(d, g, t0 + 90).empty());

    // Its poses stop: the AS fallback plays its command, whatever it is.
    t.anim = CoopPuppet::Anim{"Sword_Attack_S_R", 1.0f, 1.0f};
    t.anim_serial = 6;
    d.SetInput(Wants({t}));
    REQUIRE_FALSE(CoopPose::Drives(4, t0 + 20 + CoopPose::kHoldMs + 10));
    calls = Frame(d, g, t0 + 20 + CoopPose::kHoldMs + 10);
    REQUIRE_FALSE(calls.empty());
    CHECK(calls[0].function == kBase + addr::kPlayAs);
    CHECK(PlayedName(g) == "Sword_Attack_S_R");
}

TEST_CASE("puppet effects: the damage flash blinks a shown puppet under its spot, its Navi stays") {
    Game g;
    Director d;
    MakeLive(d, g);  // P1 at (4, 5, 6)
    Target t = At(1, 4, 5, 6);
    t.flash_since_ms = 100;
    d.SetInput(Wants({t}));
    const float down = 5.0f - Director::kHideDrop;
    Frame(d, g, 100);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    Frame(d, g, 100 + CoopEffects::kBlinkMs);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);
    CheckPosition(g.guest, kActor + addr::kActorPositionRequest, 4, down, 6);
    CHECK((d.LiveMask() & (1u << 1)) != 0u);  // still shown: its Navi does not blink
    Frame(d, g, 100 + 2 * CoopEffects::kBlinkMs);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    Frame(d, g, 100 + CoopEffects::kFlashMs - 1);  // the last blink
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, down, 6);
    Frame(d, g, 100 + CoopEffects::kFlashMs);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
    // Held (their cutscene) or parked: no blink.
    t.flash_since_ms = 2000;
    t.show = CoopPuppet::Show::hold;
    d.SetInput(Wants({t}));
    Frame(d, g, 2000 + CoopEffects::kBlinkMs);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4, 5, 6);
}

TEST_CASE("puppet planner: with the effects on a hit flashes, a burn shows its stand-in, out of sight none") {
    std::vector<CoopPuppet::Remote> remotes(1);
    remotes[0].number = 2;
    remotes[0].scene = "Kokiri";
    remotes[0].position = {1, 2, 3};
    remotes[0].anim = CoopPuppet::Anim{"Damage_Burning", 0.0f, 1.0f};
    remotes[0].anim_serial = 1;

    // Off (the default here): nothing of it.
    CoopPuppet::Planner off;
    auto in = off.Plan(kBase, std::string("Kokiri"), CoopPuppet::Vec3{10, 0, 10}, false, remotes, 1000);
    REQUIRE(in.targets.size() == 1);
    CHECK_FALSE(in.targets[0].effects);
    CHECK(in.targets[0].flash_since_ms == 0u);
    CHECK(in.targets[0].status == CoopEffects::Status::none);
    CHECK(off.TakeEffectLines().empty());

    CoopPuppet::PlannerSettings s;
    s.effects.as_effects = s.effects.flash = s.effects.status = true;
    CoopPuppet::Planner p(s);
    in = p.Plan(kBase, std::string("Kokiri"), CoopPuppet::Vec3{10, 0, 10}, false, remotes, 1000);
    REQUIRE(in.targets.size() == 1);
    CHECK(in.targets[0].effects);
    CHECK(in.targets[0].flash_since_ms == 1000u);
    CHECK(in.targets[0].status == CoopEffects::Status::burn);
    const auto lines = p.TakeEffectLines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "P2 effects: flash ('Damage_Burning'); shows burn (was none)");
    CHECK(p.TakeEffectLines().empty());

    // Out of sight (another place; our cutscene hides nobody since D-R12): no
    // stand-in on a puppet that is not shown.
    // 5 s on: past the passages' fresh_ms, so no leaving walk (docs/PUERTAS.md).
    remotes[0].scene = "Market";
    in = p.Plan(kBase, std::string("Kokiri"), CoopPuppet::Vec3{10, 0, 10}, false, remotes, 5000);
    for (const auto& t : in.targets) {
        CHECK(t.show != CoopPuppet::Show::show);
        CHECK(t.status == CoopEffects::Status::none);
    }
}

// ORM menus (oot-remake docs/MENUS_PAUSA.md M2): a paused player is held
// where it is, never hidden (D-R37), and still wears what it changes.
TEST_CASE("puppet planner: a paused player holds where it is, with its gear (ORM menus)") {
    CoopPuppet::Planner p;
    std::vector<CoopPuppet::Remote> remotes(2);
    remotes[0].number = 1;
    remotes[0].scene = "KokiriForest";
    remotes[0].position = {1.0f, 0.0f, 2.0f};
    remotes[0].status = coop::protocol::status::paused;
    remotes[0].gear[1] = "Weapon_Shield_Deku";  // kGearNames[1]: shield
    remotes[1].number = 2;
    remotes[1].scene = "KokiriForest";
    remotes[1].status = coop::protocol::status::paused | coop::protocol::status::dead;

    auto in = p.Plan(kBase, std::string("KokiriForest"), std::nullopt, false, remotes, 0);
    REQUIRE(in.targets.size() == 2);
    CHECK(in.targets[0].show == CoopPuppet::Show::hold);
    CHECK(std::string(in.targets[0].why) == "paused");
    CHECK(in.targets[0].position == CoopPuppet::Vec3{1.0f, 0.0f, 2.0f});
    REQUIRE(in.targets[0].gear[1].has_value());
    CHECK(*in.targets[0].gear[1] == "Weapon_Shield_Deku");
    CHECK(in.targets[1].show == CoopPuppet::Show::hold);  // downed and paused: held too

    // The menu closed: shown again; elsewhere: not planned at all.
    remotes[0].status = 0;
    // (5 s on: past the passages' fresh_ms, so no leaving walk, docs/PUERTAS.md.)
    remotes[1].scene = "LostWoods";
    in = p.Plan(kBase, std::string("KokiriForest"), std::nullopt, false, remotes, 5000);
    REQUIRE(in.targets.size() == 1);
    CHECK(in.targets[0].show == CoopPuppet::Show::show);
    CHECK(std::string(in.targets[0].why) == "");
}

// ORM (oot-remake docs/DETALLES_MARIONETA.md, coop_detalles.h): armor the
// puppet put on by itself from our own flags, which its player does not wear.
namespace {
constexpr std::uint64_t kStrayParts = 0x500000000;
constexpr std::uint64_t kStrayEu = kStrayParts + 0x800;
constexpr std::uint64_t kStrayPieces = 0x600000000;

void GiveEquipmentUser(Game& g) {
    g.guest.Map(kStrayParts, 0x1000);
    g.guest.Set(kActor + addr::kActorParts, kStrayParts);
    g.guest.Set(kStrayParts + addr::kPartsEquipmentUser, kStrayEu);
    g.guest.Map(kStrayPieces, 0x4000 * 4);
}

// Armor link `index` of the puppet's EquipmentUser holds a live piece `name`.
void PutArmor(Game& g, std::uint32_t index, const std::string& name) {
    const std::uint64_t link = kStrayEu + addr::kArmorLinks + addr::kEquipSlotStride * index;
    const std::uint64_t holder = kStrayPieces + 0x4000 * index;
    const std::uint64_t piece = holder + 0x1000;
    const std::uint64_t text = holder + 0x2000;
    const std::uint32_t id = 700 + index;
    g.guest.Set(link + addr::kEquipSlotHolder, holder);
    g.guest.Set(link + addr::kEquipSlotId, id);
    g.guest.Set(holder + addr::kHolderActor, piece);
    g.guest.Set(holder + addr::kHolderId, id);
    g.guest.Set(piece + addr::kActorState, addr::kStatePresent);
    g.guest.Set(piece + 0x260, text);
    std::array<char, 80> bytes{};
    std::memcpy(bytes.data(), name.data(), name.size());
    g.guest.Set(text, bytes);
}

void EmptyArmor(Game& g, std::uint32_t index) {
    g.guest.Set(kStrayEu + addr::kArmorLinks + addr::kEquipSlotStride * index + addr::kEquipSlotId,
                std::uint32_t{0xFFFFFFFFu});
}
}  // namespace

TEST_CASE("puppet gear: boots and gauntlets of ours its player does not wear are taken off (ORM)") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    MakeLive(d, g);
    GiveEquipmentUser(g);
    // The puppet put on our Iron Boots, our gauntlets, the default head and
    // the base tunic by itself; its player wears no boots and no gauntlets.
    PutArmor(g, 0, "Armor_Adult_Default_Head");
    PutArmor(g, 1, "Armor_Adult_Normal");
    PutArmor(g, 2, "Armor_Adult_HeavyBoot");
    PutArmor(g, 3, "Armor_Adult_Gauntlet_Silver");
    Target t = At(1, 4, 5, 6);
    t.gear[2] = "";  // head
    t.gear[3] = "";  // tunic: an old peer that could not tell
    t.gear[4] = "";  // boots
    t.gear[CoopPuppet::kGearHand] = "";
    d.SetInput(Wants({t}));
    CHECK(Frame(d, g, 20).empty());
    CHECK(Frame(d, g, 20 + Director::kTakeOffMs - 10).empty());
    auto calls = Frame(d, g, 20 + Director::kTakeOffMs);
    // Only the boots and the gauntlets: never the default head or the base tunic.
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].function == kBase + addr::kUnequipArmor);
    CHECK(calls[0].x[0] == kStrayEu);
    CHECK(calls[0].x[1] == 2u);  // lower
    CHECK(calls[0].x[2] == 0u);
    CHECK(calls[1].x[1] == 3u);  // hand
    CHECK(Count(log, "P1 takes off boots 'Armor_Adult_HeavyBoot' (ours, not its player's)") == 1);
    CHECK(Count(log, "P1 takes off hand 'Armor_Adult_Gauntlet_Silver' (ours, not its player's)") == 1);
    // The game took them off.
    EmptyArmor(g, 2);
    EmptyArmor(g, 3);
    CHECK(Frame(d, g, 100 + Director::kTakeOffMs).empty());
    CHECK(Frame(d, g, 5000 + Director::kTakeOffMs).empty());
    CHECK(d.Slots()[0].phase == Phase::live);  // no remake

    // Its player puts boots on: worn with the equip call as before.
    t.gear[4] = "Armor_Adult_HoverBoot";
    d.SetInput(Wants({t}));
    calls = Frame(d, g, 9000);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquip);
    CHECK(calls[0].x[1] == 9u);
}

TEST_CASE("puppet gear: a stray piece waits, is not taken while unknown, and gives up (ORM)") {
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    MakeLive(d, g);
    GiveEquipmentUser(g);
    PutArmor(g, 0, "Armor_Child_Mask_Keaton");
    Target t = At(1, 4, 5, 6);
    // Not known yet (no PLAYER_EQUIP): nothing.
    d.SetInput(Wants({t}));
    CHECK(Frame(d, g, 20).empty());
    CHECK(Frame(d, g, 20 + 2 * Director::kTakeOffMs).empty());
    // Its player wears a mask of its own: worn, ours replaced by the game.
    t.gear[2] = "Armor_Child_Mask_Bunny";
    d.SetInput(Wants({t}));
    auto calls = Frame(d, g, 4000);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kEquip);

    // A puppet whose player wears no mask: ours goes after kTakeOffMs; if
    // the game keeps putting it back, it is left on after kStrayTries.
    Game g2;
    std::vector<std::string> log2;
    Director d2([&](const std::string& line) { log2.push_back(line); });
    MakeLive(d2, g2);
    GiveEquipmentUser(g2);
    PutArmor(g2, 0, "Armor_Child_Mask_Keaton");
    Target t2 = At(1, 4, 5, 6);
    t2.gear[2] = "";
    d2.SetInput(Wants({t2}));
    std::uint64_t now = 20;
    int takes = 0;
    for (int round = 0; round < 6; ++round) {
        Frame(d2, g2, now);  // first seen
        now += Director::kTakeOffMs;
        for (const Call& c : Frame(d2, g2, now)) {
            if (c.function == kBase + addr::kUnequipArmor) {
                CHECK(c.x[1] == 0u);
                ++takes;
            }
        }
        now += 100;
    }
    CHECK(takes == CoopDetalles::kStrayTries);
    CHECK(Count(log2, "P1 head 'Armor_Child_Mask_Keaton' (ours) is back after 3 take-offs: left on") ==
          1);

    // A refused take-off of a stray piece leaves it on: no remake, no retry.
    Game g3;
    std::vector<std::string> log3;
    Director d3([&](const std::string& line) { log3.push_back(line); });
    MakeLive(d3, g3);
    GiveEquipmentUser(g3);
    g3.unequip_result = 0;
    PutArmor(g3, 2, "Armor_Adult_HoverBoot");
    Target t3 = At(1, 4, 5, 6);
    t3.gear[4] = "";
    d3.SetInput(Wants({t3}));
    Frame(d3, g3, 20);
    calls = Frame(d3, g3, 20 + Director::kTakeOffMs);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + addr::kUnequipArmor);
    CHECK(Count(log3, "the game's call returned 0; left on") == 1);
    CHECK(Frame(d3, g3, 40 + Director::kTakeOffMs).empty());
    CHECK(Frame(d3, g3, 40 + 3 * Director::kTakeOffMs).empty());
    CHECK(d3.Slots()[0].phase == Phase::live);
}

// ORM mascaras (coop_mascaras.h): a mask is bound to its wearer by bone name
// and drawn where its own actor stands, so the director puts it on the
// puppet's head each frame, after the pose copy.
TEST_CASE("puppet head: a live puppet's mask is put on its head each frame (ORM mascaras)") {
    using SkeletonFakes::B;
    CoopMascaras::Reset();
    Game g;
    std::vector<std::string> log;
    Director d([&](const std::string& line) { log.push_back(line); });
    d.SetHeadFollow(true);
    MakeLive(d, g);
    SkeletonFakes::MakeSkeleton(g.guest, kActor, 0x600000000, "Player_Child",
                                {B("Root", -1), B("Skl_Root", 0, {0, 0.584f, 0}), B("Head", 1, {0, 0.3f, 0})});
    constexpr std::uint64_t mask = 0x610000000;
    SkeletonFakes::MakeActor(g.guest, mask, {50, 0, 50});
    const std::uint64_t mask_local =
        SkeletonFakes::MakeSkeleton(g.guest, mask, 0x611000000, "Armor_Child_Mask_Keaton",
                                    {B("Root", -1), B("locator1", 0), B("Skl_Root", 1), B("Head", 2)});
    SkeletonFakes::Link(g.guest, kActor, 0x620000000, SkeletonFakes::ArmorLink(0), 0x630000000, mask, 0x51);

    d.SetInput(Wants({At(1, 4, 5, 6)}));
    Frame(d, g, 30);
    CheckPosition(g.guest, mask + addr::kActorPosition, 4, 5, 6);
    CheckPosition(g.guest, mask + addr::kActorPositionRequest, 4, 5, 6);
    CHECK(SkeletonFakes::LocalOf(g.guest, mask_local, 3).translation[1] == doctest::Approx(0.3f));
    CHECK(SkeletonFakes::LocalOf(g.guest, mask_local, 2).translation[1] == doctest::Approx(0.584f));
    Frame(d, g, 45);
    CHECK(std::count_if(log.begin(), log.end(), [](const std::string& l) {
              return l.find("coop: puppet: P1 head 'Armor_Child_Mask_Keaton'") == 0;
          }) == 1);

    // Off (LICHII_ORM_MASK_FOLLOW=0): the mask is left where it is. (A step under
    // CoopSnap::kSnapM, drawn at once: a longer one is eased, issue 194.)
    d.SetHeadFollow(false);
    d.SetInput(Wants({At(1, 4.5f, 5, 6)}));
    Frame(d, g, 60);
    CheckPosition(g.guest, kActor + addr::kActorPosition, 4.5f, 5, 6);
    CheckPosition(g.guest, mask + addr::kActorPosition, 4, 5, 6);
    CoopMascaras::Reset();
}

namespace {
struct BeltGame {
    static constexpr std::uint64_t kParts = 0x500000000;
    static constexpr std::uint64_t kEu = kParts + 0x800;
    static constexpr std::uint64_t kHandHolder = kParts + 0x2000;
    static constexpr std::uint64_t kHandPiece = kParts + 0x2100;
    static constexpr std::uint64_t kBeltHolder = kParts + 0x2400;
    static constexpr std::uint64_t kBeltPiece = kParts + 0x2500;
    static constexpr std::uint64_t kName = kParts + 0x2f00;
    void Set(Game& g, const CoopPuppet::Vec3& own) {
        g.guest.Map(kParts, 0x3000);
        g.guest.Set(kActor + addr::kActorParts, kParts);
        g.guest.Set(kParts + addr::kPartsEquipmentUser, kEu);
        for (std::uint32_t s = 0; s < 8; ++s) {
            g.guest.Set(kEu + addr::kEquipSlots + addr::kEquipSlotStride * s + addr::kEquipSlotId,
                        std::uint32_t{0xFFFFFFFF});
        }
        // Slot 1 (the sub weapon): the Ocarina in the left hand.
        const std::uint64_t hand = kEu + addr::kEquipSlots + addr::kEquipSlotStride * 1;
        g.guest.Set(hand + addr::kEquipSlotId, std::uint32_t{0x41});
        g.guest.Set(hand + addr::kEquipSlotHolder, kHandHolder);
        g.guest.Set(kHandHolder + addr::kHolderActor, kHandPiece);
        g.guest.Set(kHandPiece + addr::kActorPosition, CoopPuppet::Vec3{own[0] - 0.3f, own[1] + 1.3f, own[2] + 0.2f});
        // Armor link 4: the belt one, by its name.
        const std::uint64_t belt = kEu + addr::kArmorLinks + addr::kEquipSlotStride * 4;
        g.guest.Set(belt + addr::kEquipSlotId, std::uint32_t{0x52});
        g.guest.Set(belt + addr::kEquipSlotHolder, kBeltHolder);
        g.guest.Set(kBeltHolder + addr::kHolderActor, kBeltPiece);
        g.guest.Set(kBeltPiece + 0x260, kName);
        const char name[] = "Item_Ocarina_Fairy";
        g.guest.Write(kName, name, sizeof(name));
        g.guest.Set(kBeltPiece + addr::kActorPosition, Belt(own));
    }
    static CoopPuppet::Vec3 Belt(const CoopPuppet::Vec3& own) { return {own[0] + 0.2f, own[1] + 0.8f, own[2]}; }
};

Target Playing(bool in_hand) {
    Target t = At(1, 4, 5, 6);
    t.gear[CoopPuppet::kGearOcarina] = "Item_Ocarina_Fairy";
    t.gear[CoopPuppet::kGearOcarinaHolder] = "Item_Ocarina_Fairy_Holder";
    if (in_hand) {
        REQUIRE(CoopPuppet::OcarinaInHand(t, CoopPuppet::kGearSubWeapon));
    }
    return t;
}
}  // namespace

TEST_CASE("puppet ocarina (riesgos-visual): the belt one is parked while the hand one shows, then put back") {
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });
    {
        Game g;
        Director d;
        MakeLive(d, g);
        CoopPuppet::Vec3 own{};
        g.guest.Read(kActor + addr::kActorPosition, &own, sizeof(own));
        BeltGame b;
        b.Set(g, own);
        d.SetInput(Wants({Playing(true)}));
        Frame(d, g, 100);
        auto belt = g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition);
        CHECK(belt[1] == doctest::Approx(own[1] + 0.8f - Director::kHideDrop));
        for (int k = 1; k <= Director::kBeltJudgeFrames; ++k) {
            Frame(d, g, 100 + 16 * static_cast<std::uint64_t>(k));
        }
        REQUIRE(checks.size() == 1);
        CHECK(checks[0] == "coop: check: ocarina belt ok P1's belt Ocarina stays parked out of sight while the one in "
                           "the hand shows: one Ocarina");
        // Put away: back on the belt.
        d.SetInput(Wants({Playing(false)}));
        Frame(d, g, 1000);
        belt = g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition);
        CHECK(belt[1] == doctest::Approx(own[1] + 0.8f));
    }
    {
        // The game's binding puts it back every frame: the fallback, and no more parking.
        Game g;
        Director d;
        MakeLive(d, g);
        CoopPuppet::Vec3 own{};
        g.guest.Read(kActor + addr::kActorPosition, &own, sizeof(own));
        BeltGame b;
        b.Set(g, own);
        d.SetInput(Wants({Playing(true)}));
        for (int k = 0; k <= Director::kBeltJudgeFrames; ++k) {
            Frame(d, g, 100 + 16 * static_cast<std::uint64_t>(k));
            g.guest.Set(BeltGame::kBeltPiece + addr::kActorPosition, BeltGame::Belt(own));  // the binding
        }
        REQUIRE(checks.size() == 2);
        CHECK(checks[1] == "coop: check: ocarina belt fallback P1's belt Ocarina is put back on the belt by the game "
                           "each frame: two Ocarinas show while playing (as before)");
        Frame(d, g, 2000);
        const auto belt = g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition);
        CHECK(belt[1] == doctest::Approx(own[1] + 0.8f));  // left alone
    }
    {
        // ORM review (REVISION_FINAL.md "Visual"): a parked piece that is gone,
        // or no longer in the puppet's belt link, is never written again.
        Game g;
        Director d;
        MakeLive(d, g);
        CoopPuppet::Vec3 own{};
        g.guest.Read(kActor + addr::kActorPosition, &own, sizeof(own));
        BeltGame b;
        b.Set(g, own);
        d.SetInput(Wants({Playing(true)}));
        Frame(d, g, 100);
        REQUIRE(g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition)[1] ==
                doctest::Approx(own[1] + 0.8f - Director::kHideDrop));
        const CoopPuppet::Vec3 mark{7.0f, 8.0f, 9.0f};
        g.guest.Set(BeltGame::kBeltPiece + addr::kActorState, std::uint32_t{7});  // deleted
        g.guest.Set(BeltGame::kBeltPiece + addr::kActorPosition, mark);
        Frame(d, g, 116);
        CHECK(g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition) == mark);
        d.SetInput(Wants({Playing(false)}));  // put away: no put-back write either
        Frame(d, g, 132);
        CHECK(g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition) == mark);

        // Parked again with a live piece, then the link emptied: left alone too.
        g.guest.Set(BeltGame::kBeltPiece + addr::kActorState, std::uint32_t{addr::kStatePresent});
        g.guest.Set(BeltGame::kBeltPiece + addr::kActorPosition, BeltGame::Belt(own));
        d.SetInput(Wants({Playing(true)}));
        Frame(d, g, 148);
        REQUIRE(g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition)[1] ==
                doctest::Approx(own[1] + 0.8f - Director::kHideDrop));
        g.guest.Set(BeltGame::kEu + addr::kArmorLinks + addr::kEquipSlotStride * 4 + addr::kEquipSlotId,
                    std::uint32_t{0xFFFFFFFF});
        g.guest.Set(BeltGame::kBeltPiece + addr::kActorPosition, mark);
        d.SetInput(Wants({Playing(false)}));
        Frame(d, g, 164);
        CHECK(g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition) == mark);
    }
    {
        // Not playing: never touched.
        Game g;
        Director d;
        MakeLive(d, g);
        CoopPuppet::Vec3 own{};
        g.guest.Read(kActor + addr::kActorPosition, &own, sizeof(own));
        BeltGame b;
        b.Set(g, own);
        d.SetInput(Wants({Playing(false)}));
        Frame(d, g, 100);
        const auto belt = g.guest.Get<CoopPuppet::Vec3>(BeltGame::kBeltPiece + addr::kActorPosition);
        CHECK(belt[1] == doctest::Approx(own[1] + 0.8f));
    }
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
