// ORM cases moved out of upstream's tests/lichii/spawn_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for things a player makes (lichii/hook/coop_spawn.cpp): the sync's
// messages and copies, and the director's calls at the cave.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop_projectiles.h"  // Track::kBehindUs
#include "coop_spawn.h"

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): an elemental
// arrow's copy is its own stand-in, the element read from its owner's
// PLAYER_EQUIP arrow; one check line of the path.
#include "coop_check_visual.h"

namespace proto = coop::protocol;
using CoopSpawn::Act;
using CoopSpawn::Copy;
using CoopSpawn::Seen;

namespace {

constexpr std::uint64_t kMs = 1000;
const std::string kScene = "Spot_GraveYard";

Seen Made(std::uint64_t address, float x, std::string name = "Item_Bomb") {
    Seen s;
    s.address = address;
    s.name = std::move(name);
    s.position = {x, 1.0f, 2.0f};
    return s;
}

// Encoded and decoded, as the server would pass it on.
proto::Message Wire(proto::Message m, std::uint8_t from) {
    std::visit([&](auto& v) {
        if constexpr (requires { v.player_number; }) {
            v.player_number = from;
        }
    }, m);
    const auto bytes = proto::encode(m);
    REQUIRE(bytes);
    proto::Message out;
    REQUIRE(proto::decode(*bytes, out) == proto::Error::none);
    return out;
}

template <typename T>
std::vector<T> Of(const std::vector<proto::Message>& send) {
    std::vector<T> out;
    for (const auto& m : send) {
        if (const auto* v = std::get_if<T>(&m)) {
            out.push_back(*v);
        }
    }
    return out;
}

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

// A guest with a director's block and one actor ready to be taken.
struct StandInRig {
    static constexpr std::uint64_t base = 0x8000000;
    static constexpr std::uint64_t block = 0x30000000;
    static constexpr std::uint64_t actor = 0x40000000;
    static constexpr std::uint64_t request = 0x50000000;
    FakeGuest guest;
    StandInRig() {
        namespace addr = CoopPuppet::addr;
        guest.Map(base + addr::kActorManagerPtr, 8);
        guest.Map(base + addr::kGotCreateDefault, 8);
        guest.Set(base + addr::kActorManagerPtr, std::uint64_t{0x20000000});
        guest.Map(block, CoopSpawn::Director::kSlots * CoopSpawn::Director::kSlotSize);
        guest.Map(actor, 0x600);
        guest.Map(request, 0x10);
        guest.Set(actor, std::uint64_t{0xabc});
        guest.Set(actor + addr::kActorState, std::uint32_t{addr::kStatePresent});
    }
    // The name a create call asks for.
    std::string Name(const CoopPuppet::Call& c) {
        char name[32]{};
        REQUIRE(guest.Read(guest.Get<std::uint64_t>(c.x[1]), name, 31));
        return name;
    }
};

}  // namespace

// ORM mascaras (coop_mascaras.h): another player's lure brings its fishing
// line as a companion copy, and the hookshot tip's copy carries its chain.
TEST_CASE("spawn: a lure copy brings its fishing line, a hookshot copy its chain (ORM mascaras)") {
    CHECK(std::string(CoopSpawn::StandIn("Weapon_FishingLine")) == "CoopShot_FishingLine");
    CHECK(CoopSpawn::StandInOnly("Weapon_FishingLine"));
    CHECK_FALSE(CoopSpawn::IsShared("Weapon_FishingLine"));  // never sent: only shown
    CHECK(CoopSpawn::StandInsIn("{\"shots\": [{\"actor\": \"CoopShot_FishingLine\"}]}") ==
          std::set<std::string>{"CoopShot_FishingLine"});
    for (const bool on : {true, false}) {
        CoopSpawn::Sync sync;
        sync.SetTethers(on);
        std::vector<proto::Message> send;
        std::vector<Copy> copies;
        std::uint64_t now = 5'000'000;
        sync.Tick(kScene, 2, now, {}, {}, send, copies);
        const std::array<std::pair<std::uint64_t, const char*>, 2> made{
            {{9, "Weapon_Lure"}, {10, "Shootable_Hook"}}};
        for (const auto& [value, actor] : made) {
            proto::ObjectSpawn spawn;
            spawn.id = {proto::ObjectKind::made, 3, value};
            spawn.actor = actor;
            spawn.state.time = {4'990'000, 0};
            spawn.state.position = proto::Vec3{float(value), 1.0f, 2.0f};
            spawn.state.velocity = proto::Vec3{0.0f, 0.0f, 0.0f};
            sync.Receive(Wire(spawn, 3));
        }
        now += 33 * kMs;
        copies.clear();
        sync.Tick(kScene, 2, now, {}, {}, send, copies);
        const auto find = [&](std::string_view actor) {
            const auto it = std::find_if(copies.begin(), copies.end(),
                                         [&](const Copy& c) { return c.actor == actor; });
            return it == copies.end() ? nullptr : &*it;
        };
        const Copy* lure = find("Weapon_Lure");
        const Copy* hook = find("Shootable_Hook");
        const Copy* line = find("Weapon_FishingLine");
        REQUIRE(lure != nullptr);
        REQUIRE(hook != nullptr);
        CHECK(lure->owner == 3);
        CHECK(hook->owner == 3);
        CHECK(lure->tether == CoopMascaras::Tether::none);
        if (!on) {
            CHECK(copies.size() == 2);
            CHECK(line == nullptr);
            CHECK(hook->tether == CoopMascaras::Tether::none);
            continue;
        }
        CHECK(copies.size() == 3);
        CHECK(hook->tether == CoopMascaras::Tether::hook_chain);
        REQUIRE(line != nullptr);
        CHECK(line->key == (lure->key | CoopSpawn::Sync::kLineKeyBit));
        CHECK(line->tether == CoopMascaras::Tether::fishing_line);
        CHECK(line->owner == 3);
        CHECK(line->act == Act::show);
        CHECK(line->position[0] == doctest::Approx(lure->position[0]));
        CHECK_FALSE(line->rotation);

        // Reeled in: the line goes with its lure.
        proto::ObjectRemove remove;
        remove.id = {proto::ObjectKind::made, 3, 9};
        remove.reason = proto::RemoveReason::gone;
        remove.position = {9.0f, 1.0f, 2.0f};
        sync.Receive(Wire(remove, 3));
        // Since test10 a removed copy is kept Track::kBehindUs after the tick that takes its
        // remove (copies show the owner that late: it reaches its end first, upstream cd7b9930).
        now += 33 * kMs;
        sync.Tick(kScene, 2, now, {}, {}, send, copies);
        now += CoopProjectiles::Track::kBehindUs;
        copies.clear();
        sync.Tick(kScene, 2, now, {}, {}, send, copies);
        lure = find("Weapon_Lure");
        line = find("Weapon_FishingLine");
        REQUIRE(lure != nullptr);
        REQUIRE(line != nullptr);
        CHECK(lure->act == Act::remove);
        CHECK(line->act == Act::remove);
    }
}

// ORM objetos-uso: Din's Fire and the Megaton Hammer's shock wave are the
// player's own shootables; their game actors burn and hit, so they are only
// ever copied as their effect stand-ins. Farore's Wind's warp point has
// nothing but its effect and sound: the game's own actor is the copy.
TEST_CASE("spawn: spells and the hammer's shock wave are shared, as effect stand-ins only") {
    for (const char* name : {"ExpandFireField", "PlayerShockWave", "FaroreWindWarpPoint"}) {
        CHECK_MESSAGE(CoopSpawn::IsShared(name), name);
        CHECK_FALSE(CoopSpawn::Explodes(name));
    }
    CHECK(CoopSpawn::Flies("ExpandFireField"));
    CHECK(CoopSpawn::Flies("PlayerShockWave"));
    CHECK_FALSE(CoopSpawn::Flies("FaroreWindWarpPoint"));
    CHECK(std::string(CoopSpawn::StandIn("ExpandFireField")) == "CoopShot_DinFire");
    CHECK(std::string(CoopSpawn::StandIn("PlayerShockWave")) == "CoopShot_ShockWave");
    CHECK(CoopSpawn::StandIn("FaroreWindWarpPoint").empty());
    CHECK(CoopSpawn::StandInOnly("ExpandFireField"));
    CHECK(CoopSpawn::StandInOnly("PlayerShockWave"));
    CHECK_FALSE(CoopSpawn::StandInOnly("FaroreWindWarpPoint"));
    CHECK(CoopSpawn::ShowFor("ExpandFireField") == CoopSpawn::Sync::kDinFireShowUs);
    CHECK(CoopSpawn::ShowFor("PlayerShockWave") == CoopSpawn::Sync::kShockWaveShowUs);
    CHECK(CoopSpawn::ShowFor("FaroreWindWarpPoint") == 0);
    CHECK(CoopSpawn::ShowFor("Item_Bomb") == 0);
    // Not shared: the pickups, the get-item models, the lens and the tools in hand.
    for (const char* name : {"Item_DinFire", "Item_NayruLove", "Item_FaroreWind", "Armor_GlassTruth",
                             "Weapon_Range_Hookshot", "Weapon_Hammer"}) {
        CHECK_FALSE_MESSAGE(CoopSpawn::IsShared(name), name);
    }
    CHECK(CoopSpawn::StandInsIn("{\"shots\": [{\"actor\": \"CoopShot_DinFire\", \"effect\": true}, "
                                "{\"actor\": \"CoopShot_Seed\"}, {\"actor\": \"CoopShot_ShockWave\"}]}") ==
          std::set<std::string>{"CoopShot_DinFire", "CoopShot_Seed", "CoopShot_ShockWave"});
}

TEST_CASE("spawn: our Din's Fire is sent, another player's is shown at most kDinFireShowUs") {
    CoopSpawn::Sync sync;
    std::vector<proto::Message> send;
    std::vector<Copy> copies;
    std::map<std::uint64_t, std::uint64_t> live;
    std::uint64_t now = 5'000'000;
    sync.Tick(kScene, 2, now, {}, live, send, copies);

    // Ours: made at Link, sent with the projectile flag.
    now += 33 * kMs;
    sync.Tick(kScene, 2, now, {Made(0x7100, 3.0f, "ExpandFireField")}, live, send, copies);
    const auto spawns = Of<proto::ObjectSpawn>(send);
    REQUIRE(spawns.size() == 1);
    CHECK(spawns[0].actor == "ExpandFireField");
    CHECK(spawns[0].flags == proto::object_spawn::projectile);
    send.clear();

    // Another player's: shown while its samples come, then gone at the limit
    // even though the owner's actor is still on its list (a shooter may keep
    // its next one ready).
    proto::ObjectSpawn spawn;
    spawn.id = {proto::ObjectKind::made, 1, 88};
    spawn.actor = "ExpandFireField";
    spawn.state.time = {now, 0};
    spawn.state.position = proto::Vec3{20.0f, 1.0f, 2.0f};
    spawn.state.velocity = proto::Vec3{0.0f, 0.0f, 0.0f};
    sync.Receive(Wire(spawn, 1));
    const std::uint64_t got = now + 33 * kMs;
    std::uint32_t seq = 0;
    for (now = got; now <= got + CoopSpawn::Sync::kDinFireShowUs + 600 * kMs; now += 500 * kMs) {
        proto::ObjectSample sample;
        sample.id = spawn.id;
        sample.state.time = {now, ++seq};
        sample.state.position = proto::Vec3{20.0f, 1.0f, 2.0f};
        sample.state.velocity = proto::Vec3{0.0f, 0.0f, 0.0f};
        sync.Receive(Wire(sample, 1));
        copies.clear();
        sync.Tick(kScene, 2, now, {Made(0x7100, 3.0f, "ExpandFireField")}, live, send, copies);
        const auto it = std::find_if(copies.begin(), copies.end(),
                                     [](const Copy& c) { return c.actor == "ExpandFireField"; });
        REQUIRE(it != copies.end());
        const bool within = now - got <= CoopSpawn::Sync::kDinFireShowUs;
        CHECK_MESSAGE(it->act == (within ? Act::show : Act::remove), (now - got));
    }
    CHECK(Of<proto::ObjectRemove>(send).empty());  // ours stays on our list
}

TEST_CASE("spawn: Farore's Wind's warp point copy stays while its owner's lives") {
    CoopSpawn::Sync sync;
    std::vector<proto::Message> send;
    std::vector<Copy> copies;
    std::map<std::uint64_t, std::uint64_t> live;
    std::uint64_t now = 1'000'000;
    sync.Tick(kScene, 2, now, {}, live, send, copies);
    proto::ObjectSpawn spawn;
    spawn.id = {proto::ObjectKind::made, 1, 5};
    spawn.actor = "FaroreWindWarpPoint";
    spawn.state.time = {now, 0};
    spawn.state.position = proto::Vec3{1.0f, 0.0f, 1.0f};
    spawn.state.velocity = proto::Vec3{0.0f, 0.0f, 0.0f};
    sync.Receive(Wire(spawn, 1));
    std::uint32_t seq = 0;
    for (int i = 0; i < 20; ++i) {  // 10 s of still samples, every 0.5 s
        now += 500 * kMs;
        proto::ObjectSample sample;
        sample.id = spawn.id;
        sample.state.time = {now, ++seq};
        sample.state.position = proto::Vec3{1.0f, 0.0f, 1.0f};
        sync.Receive(Wire(sample, 1));
        copies.clear();
        sync.Tick(kScene, 2, now, {}, live, send, copies);
        REQUIRE(copies.size() == 1);
        CHECK(copies[0].act == Act::show);
    }
    proto::ObjectRemove remove;
    remove.id = spawn.id;
    remove.reason = proto::RemoveReason::gone;
    remove.position = {1.0f, 0.0f, 1.0f};
    sync.Receive(Wire(remove, 1));
    copies.clear();
    // Kept Track::kBehindUs after its remove since test10 (upstream cd7b9930), then it goes.
    sync.Tick(kScene, 2, now + 33 * kMs, {}, live, send, copies);
    REQUIRE(copies.size() == 1);
    CHECK(copies[0].act == Act::show);
    copies.clear();
    sync.Tick(kScene, 2, now + CoopProjectiles::Track::kBehindUs + 33 * kMs, {}, live, send, copies);
    REQUIRE(copies.size() == 1);
    CHECK(copies[0].act == Act::remove);
}

TEST_CASE("spawn: a Din's Fire copy is its effect stand-in or nothing, the warp point is the game's own") {
    using R = StandInRig;
    namespace addr = CoopPuppet::addr;
    const auto first_create = [](StandInRig& rig, CoopSpawn::Director& d, const char* actor) {
        Copy copy;
        copy.key = 1;
        copy.actor = actor;
        d.SetInput(R::base, {copy});
        std::optional<CoopPuppet::Call> c = d.Step(rig.guest, false, 0, 0);
        if (c && c->function != R::base + addr::kCreate) {
            c = d.Step(rig.guest, true, R::block, 0);  // the slots' malloc
        }
        for (std::uint64_t ms = 16; ms < 200 && !(c && c->function == R::base + addr::kCreate); ms += 16) {
            c = d.Step(rig.guest, false, 0, ms);
        }
        return c && c->function == R::base + addr::kCreate ? rig.Name(*c) : std::string{};
    };
    {
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_DinFire", "CoopShot_ShockWave"});
        CHECK(first_create(rig, d, "ExpandFireField") == "CoopShot_DinFire");
    }
    {
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_DinFire", "CoopShot_ShockWave"});
        CHECK(first_create(rig, d, "PlayerShockWave") == "CoopShot_ShockWave");
    }
    {
        // A pack built before the effect stand-ins: no fire here at all.
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_Seed"});
        CHECK(first_create(rig, d, "ExpandFireField").empty());
        CHECK(d.Live().empty());
    }
    {
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_Seed", "CoopShot_DinFire"});
        CHECK(first_create(rig, d, "FaroreWindWarpPoint") == "FaroreWindWarpPoint");
    }
}

// ORM cofre (coop_chest_open.h): a find held over another player's puppet is
// its model-only stand-in, never the game's own item-get actor.
TEST_CASE("spawn: a chest find is only ever its CoopGet stand-in (cofre)") {
    using R = StandInRig;
    namespace addr = CoopPuppet::addr;
    Copy copy;
    copy.key = CoopSpawn::Director::kFindKeyBit | (std::uint64_t{1} << 8) | 2;
    copy.actor = "Item_Dungeon_SmallKey";
    copy.position = {1.0f, 2.25f, 3.0f};
    {
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopGet_Item_Dungeon_SmallKey"});
        d.SetFinds(R::base, {copy});
        REQUIRE(d.Step(rig.guest, false, 0, 0));  // the slots' malloc
        auto c = d.Step(rig.guest, true, R::block, 0);
        REQUIRE(c);
        REQUIRE(c->function == R::base + addr::kCreate);
        CHECK(rig.Name(*c) == "CoopGet_Item_Dungeon_SmallKey");
    }
    {
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_Seed"});  // an old pack: no CoopGet_*
        d.SetFinds(R::base, {copy});
        auto c = d.Step(rig.guest, false, 0, 0);
        if (c && c->function != R::base + addr::kCreate) {
            c = d.Step(rig.guest, true, R::block, 0);
        }
        CHECK_FALSE((c && c->function == R::base + addr::kCreate));
        for (std::uint64_t ms = 16; ms < 200; ms += 16) {
            c = d.Step(rig.guest, false, 0, ms);
            CHECK_FALSE((c && c->function == R::base + addr::kCreate));
        }
        CHECK(d.Live().empty());
    }
}

// ORM efectos2 (coop_effects.h): a status stand-in on a puppet is asked for
// by its own name, only when the pack has it, placed as the puppet is, and
// deleted when the plan no longer has it.
TEST_CASE("spawn: a status stand-in is its own stand-in or nothing, placed each frame and deleted with its status") {
    using R = StandInRig;
    namespace addr = CoopPuppet::addr;
    for (const std::string_view fx : CoopEffects::kFxStandIns) {
        CHECK(CoopSpawn::StandIn(fx) == fx);
        CHECK(CoopSpawn::StandInOnly(fx));
    }
    CHECK(CoopSpawn::StandInsIn("{\"shots\": [{\"actor\": \"CoopFx_Burn\", \"effect\": true, \"status\": true}, "
                                "{\"actor\": \"CoopShot_Seed\"}]}") ==
          std::set<std::string>{"CoopFx_Burn", "CoopShot_Seed"});

    const auto place = CoopEffects::FxAt(2, CoopEffects::Status::burn, {1.0f, 2.0f, 3.0f});
    Copy copy;
    copy.key = place.key;
    copy.actor = place.actor;
    copy.position = place.position;
    {
        // A pack without it: never a create for a name the game does not know.
        StandInRig rig;
        CoopSpawn::Director d;
        d.SetStandIns({"CoopShot_Seed"});
        d.SetEffects(R::base, {copy});
        auto c = d.Step(rig.guest, false, 0, 0);
        REQUIRE(c);
        CHECK(c->function == R::base + addr::kMalloc);
        CHECK_FALSE(d.Step(rig.guest, true, R::block, 0));
        CHECK_FALSE(d.Step(rig.guest, false, 0, 16));
        CHECK(d.Live().empty());
    }
    StandInRig rig;
    CoopSpawn::Director d;
    d.SetStandIns({"CoopFx_Burn"});
    d.SetEffects(R::base, {copy});
    REQUIRE(d.Step(rig.guest, false, 0, 0));  // malloc
    auto c = d.Step(rig.guest, true, R::block, 0);
    REQUIRE(c);
    REQUIRE(c->function == R::base + addr::kCreate);
    CHECK(rig.Name(*c) == "CoopFx_Burn");
    const std::uint64_t handle = c->x[3];
    d.Step(rig.guest, true, 0, 0);
    rig.guest.Set(handle, R::request);
    rig.guest.Set(R::request, std::uint32_t{addr::kRequestReady});
    REQUIRE(d.Step(rig.guest, false, 0, 16));  // take
    d.Step(rig.guest, true, R::actor, 16);

    // It follows the puppet: the place and the pending transform each frame.
    copy.position = {4.0f, 5.0f + CoopEffects::kFxLift, 6.0f};
    d.SetEffects(R::base, {copy});
    CHECK_FALSE(d.Step(rig.guest, false, 0, 33));
    CHECK(d.Live().at(place.key) == R::actor);
    CHECK(rig.guest.Get<float>(R::actor + addr::kActorPosition) == doctest::Approx(4.0f));
    CHECK(rig.guest.Get<float>(R::actor + addr::kActorPositionRequest + 4) ==
          doctest::Approx(5.0f + CoopEffects::kFxLift));

    // The burn is over: out of the plan, deleted.
    d.SetEffects(R::base, {});
    c = d.Step(rig.guest, false, 0, 50);
    REQUIRE(c);
    CHECK(c->function == R::base + addr::kRequestDelete);
    CHECK(c->x[0] == R::actor);
    CHECK(d.Live().empty());
    d.Step(rig.guest, true, 0, 50);

    // ORM review (REVISION_FINAL.md "Visual"): a key is made once, so the same
    // player's second burn in this scene under the first burn's key shows
    // nothing; CoopEffects::FxKeys gives it a new episode, which is made.
    d.SetEffects(R::base, {copy});
    CHECK_FALSE(d.Step(rig.guest, false, 0, 66));
    Copy again = copy;
    again.key = CoopEffects::FxKey(2, CoopEffects::Status::burn, 0, 1);
    REQUIRE(again.key != copy.key);
    d.SetEffects(R::base, {again});
    c = d.Step(rig.guest, false, 0, 83);
    REQUIRE(c);
    CHECK(c->function == R::base + addr::kCreate);
    CHECK(rig.Name(*c) == "CoopFx_Burn");
}

TEST_CASE("spawn (riesgos-visual): a fire arrow is made as its stand-in, a plain one with an older pack") {
    using R = StandInRig;
    namespace addr = CoopPuppet::addr;
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });
    CHECK(CoopSpawn::StandInsIn("{\"shots\": [{\"actor\": \"CoopShot_Arrow\"}, {\"actor\": \"CoopShot_Arrow_Fire\"}]}") ==
          std::set<std::string>{"CoopShot_Arrow", "CoopShot_Arrow_Fire"});
    CoopSpawn::Sync sync;
    sync.SetArrow(2, "Item_Arrow_Fire");
    CHECK(sync.ArrowOf(2) == CoopProjectiles::Element::fire);
    CHECK(sync.ArrowOf(3) == CoopProjectiles::Element::none);
    sync.SetArrow(2, "");
    CHECK(sync.ArrowOf(2) == CoopProjectiles::Element::none);
    for (const bool has_fire : {true, false}) {
        StandInRig rig;
        CoopSpawn::Director d;
        if (has_fire) {
            d.SetStandIns({"CoopShot_Arrow", "CoopShot_Arrow_Fire"});
        } else {
            d.SetStandIns({"CoopShot_Arrow"});
        }
        Copy copy;
        copy.key = 9;
        copy.actor = "NormalArrow";
        copy.owner = 2;
        copy.element = CoopProjectiles::Element::fire;
        d.SetInput(R::base, {copy});
        REQUIRE(d.Step(rig.guest, false, 0, 0));  // malloc
        auto c = d.Step(rig.guest, true, R::block, 0);
        REQUIRE(c);
        REQUIRE(c->function == R::base + addr::kCreate);
        CHECK(rig.Name(*c) == (has_fire ? "CoopShot_Arrow_Fire" : "CoopShot_Arrow"));
    }
    REQUIRE(checks.size() == 2);
    CHECK(checks[0] == "coop: check: arrow element ok P2's fire arrow drawn as CoopShot_Arrow_Fire (his PLAYER_EQUIP "
                       "arrow, PlayerStatus.EquipArrow)");
    CHECK(checks[1] == "coop: check: arrow element fallback P2's fire arrow drawn as a plain arrow: the puppet pack has "
                       "no CoopShot_Arrow_Fire (run make-puppet-pack again)");
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
