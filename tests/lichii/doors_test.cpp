// ORM (oot-remake docs/PUERTAS.md): the door mirror's game side
// (lichii/hook/coop_doors.cpp) on fake guest memory, and the passages in the
// puppet planner and director (lichii/hook/coop_puppet.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "coop_doors.h"
#include "coop_puppet.h"

namespace {

constexpr std::uint64_t kBase = 0x80000000;   // main
constexpr std::uint64_t kHeap = 0x200000000;  // what the fake malloc gives
constexpr std::uint64_t kManager = 0x300000000;
constexpr std::uint64_t kLists = 0x300001000;
constexpr std::uint64_t kHead = kLists + CoopDoors::addr::kListHead;
constexpr std::uint64_t kDoorActor = 0x400000000;
constexpr std::uint64_t kFarActor = 0x400010000;
constexpr std::uint64_t kText = 0x410000000;  // name strings
constexpr std::uint64_t kAs = 0x500000000;
constexpr std::uint64_t kController = kAs + 0x100;
constexpr std::uint64_t kSlot0 = kAs + 0x200;

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
    void SetText(std::uint64_t address, const std::string& text) {
        std::array<char, 64> buf{};
        std::memcpy(buf.data(), text.data(), std::min<std::size_t>(text.size(), 63));
        REQUIRE(Write(address, buf.data(), buf.size()));
    }
    std::string GetText(std::uint64_t address) {
        std::array<char, 64> buf{};
        REQUIRE(Read(address, buf.data(), buf.size()));
        return std::string(buf.data(), strnlen(buf.data(), buf.size()));
    }

private:
    std::map<std::uint64_t, std::array<std::uint8_t, 0x1000>> pages_;
};

// Kakariko: a house door with its AS (slot 0 "Wait"), and a pot 30 m off,
// on the live actor list in that order.
const CoopDoors::Vec3 kDoorAt{-52.6f, 97.9f, 34.5f};
const CoopDoors::Vec3 kInFront{-52.6f, 97.9f, 35.4f};
const CoopDoors::Vec3 kFar{-80.0f, 97.0f, 50.0f};

struct Game {
    FakeGuest guest;
    Game() {
        namespace a = CoopDoors::addr;
        guest.Map(kBase + a::kActorManagerPtr, 8);
        guest.Map(kManager, 0x200);
        guest.Map(kLists, 0x1000);
        guest.Map(kDoorActor, 0x1000);
        guest.Map(kFarActor, 0x1000);
        guest.Map(kText, 0x1000);
        guest.Map(kAs, 0x1000);
        guest.Map(kHeap, 0x1000);
        guest.Set(kBase + a::kActorManagerPtr, kManager);
        guest.Set(kManager + a::kManagerLists, kLists);
        const std::uint64_t door_node = kDoorActor + a::kActorNode;
        const std::uint64_t far_node = kFarActor + a::kActorNode;
        guest.Set(kHead, door_node);
        guest.Set(kHead + a::kListCount, std::uint32_t{2});
        guest.Set(door_node, far_node);
        guest.Set(door_node + a::kNodeOwner, kDoorActor);
        guest.Set(far_node, kHead);
        guest.Set(far_node + a::kNodeOwner, kFarActor);
        Actor(kDoorActor, kText, "Obj_Kakariko_Door_A_01", kDoorAt);
        Actor(kFarActor, kText + 0x100, "Obj_Pot_A_08", {-16.6f, 100.0f, 30.7f});
        // The door's AS chain, as the puppet test builds it.
        guest.Set(kDoorActor + 0x270, kAs);
        guest.Set(kAs + 0x40, kAs + 0x40);
        guest.Set(kAs + 0x40 + 0x18, kAs + 0x80);
        guest.Set(kAs + 0x80 + 0x18, kController);
        guest.Set(kController + CoopPuppet::addr::kSlotCount, std::int32_t{1});
        guest.Set(kController + CoopPuppet::addr::kSlots, kAs + 0x300);
        guest.Set(kAs + 0x300, kSlot0);
        DoorPlays("Wait");
    }
    void Actor(std::uint64_t actor, std::uint64_t text, const std::string& name,
               const CoopDoors::Vec3& at) {
        namespace a = CoopDoors::addr;
        guest.SetText(text, name);
        guest.Set(actor + a::kActorName, text);
        guest.Set(actor + a::kActorState, a::kStatePresent);
        guest.Set(actor + a::kActorPosition, at);
    }
    // What the door's slot 0 plays now (the game's own, or ours).
    void DoorPlays(const std::string& command) {
        guest.SetText(kText + 0x800, command);
        guest.Set(kSlot0 + CoopPuppet::addr::kSlotName, kText + 0x800);
    }
};

CoopDoors::Input With(std::string command, CoopDoors::Vec3 at = kInFront,
                      std::string scene = "KakarikoVillage_Child") {
    CoopDoors::Input in;
    in.main_base = kBase;
    in.scene = std::move(scene);
    in.own = kFar;
    coop::host::DoorMover m;
    m.number = 2;
    m.position = at;
    m.command = std::move(command);
    in.others.push_back(m);
    return in;
}

// One calc frame as the cave runs it: the fake malloc gives kHeap, every
// other call 0. The calls made.
std::vector<CoopPuppet::Call> Frame(CoopDoors::Mirror& m, Game& g, std::uint64_t now_ms) {
    std::vector<CoopPuppet::Call> calls;
    auto call = m.Step(g.guest, false, 0, now_ms);
    while (call) {
        calls.push_back(*call);
        const std::uint64_t result = call->function == kBase + CoopPuppet::addr::kMalloc ? kHeap : 0;
        call = m.Step(g.guest, true, result, now_ms);
        REQUIRE(calls.size() < 8);
    }
    return calls;
}

// The command named by the play request in the block.
std::string Requested(Game& g, std::uint64_t actor) {
    CHECK(g.guest.Get<std::uint64_t>(kHeap + CoopDoors::block::kRequest) == actor);
    CHECK(g.guest.Get<std::int32_t>(kHeap + 0x10) == 0);  // slot 0
    CHECK(g.guest.Get<float>(kHeap + 0x14) == doctest::Approx(1.0f));
    CHECK(g.guest.Get<float>(kHeap + 0x18) == doctest::Approx(-1.0f));
    return g.guest.GetText(g.guest.Get<std::uint64_t>(kHeap + 0x08));
}

bool Logged(const std::vector<std::string>& log, const std::string& part) {
    return std::any_of(log.begin(), log.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

}  // namespace

TEST_CASE("door mirror: a partner's push plays our door's own push, then its close") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    m.SetInput(With("Wait"));
    CHECK(Frame(m, g, 1000).empty());  // nothing to read, no memory asked for
    m.SetInput(With("DoorPushOpenL"));
    auto calls = Frame(m, g, 1100);
    REQUIRE(calls.size() == 1);  // the block first
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kMalloc);
    CHECK(calls[0].x[0] == CoopDoors::block::kSize);
    calls = Frame(m, g, 1200);  // the walk finds the door: our push plays
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kPlayAs);
    CHECK(calls[0].x[0] == kHeap + CoopDoors::block::kRequest);
    CHECK(Requested(g, kDoorActor) == "Push_L_ByPlayer");
    CHECK(m.Plays() == 1);
    CHECK(Logged(log, "doors: P2 'DoorPushOpenL': Obj_Kakariko_Door_A_01 opens here (Push_L_ByPlayer), 0.9 m"));
    CHECK(Frame(m, g, 1300).empty());  // played once

    // The game plays it; he walks through and on.
    g.DoorPlays("Push_L_ByPlayer");
    m.SetInput(With("Walk", {-52.6f, 97.9f, 33.0f}));
    CHECK(Frame(m, g, 2000).empty());
    CHECK(Frame(m, g, 2299).empty());
    calls = Frame(m, g, 2300);
    REQUIRE(calls.size() == 1);
    CHECK(Requested(g, kDoorActor) == "Close_L_ByPlayer");
    // Each name keeps its own cell: the push's is still there.
    CHECK(g.guest.GetText(kHeap + CoopDoors::block::kCells) == "Push_L_ByPlayer");
    CHECK(g.guest.GetText(kHeap + CoopDoors::block::kCells + CoopDoors::block::kCell) ==
          "Close_L_ByPlayer");
    CHECK(Logged(log, "doors: P2 is through Obj_Kakariko_Door_A_01: closing it (Close_L_ByPlayer)"));
    CHECK(m.Walks() == 2);
}

TEST_CASE("door mirror: no door near, a gone actor, a scene change, no game") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    // He pushes 10 m from our door: a line, no play.
    m.SetInput(With("Wait", {-52.6f, 97.9f, 44.5f}));
    Frame(m, g, 0);
    m.SetInput(With("DoorPushOpenR", {-52.6f, 97.9f, 44.5f}));
    Frame(m, g, 100);  // block
    CHECK(Frame(m, g, 200).empty());
    CHECK(Logged(log, "doors: P2 'DoorPushOpenR': no door (right) within 2.0 m of its puppet here"));

    // Next to it, but the door actor is being deleted (state 7): skipped by the walk.
    m.SetInput(With("Wait"));
    Frame(m, g, 300);
    g.guest.Set(kDoorActor + CoopDoors::addr::kActorState, std::uint32_t{7});
    m.SetInput(With("DoorPushOpenR"));
    CHECK(Frame(m, g, 400).empty());
    CHECK(Logged(log, "doors: P2 'DoorPushOpenR': no door (right) within 2.0 m"));
    g.guest.Set(kDoorActor + CoopDoors::addr::kActorState, CoopDoors::addr::kStatePresent);

    // Opened, then our scene changes before he is through: nothing closes.
    m.SetInput(With("Wait"));
    Frame(m, g, 500);
    m.SetInput(With("DoorPushOpenR"));
    REQUIRE(Frame(m, g, 600).size() == 1);
    CHECK(Requested(g, kDoorActor) == "Push_R_ByPlayer");
    g.DoorPlays("Push_R_ByPlayer");
    m.SetInput(With("Walk", kInFront, "HyruleField_Child"));
    CHECK(Frame(m, g, 700).empty());
    CHECK(Frame(m, g, 2000).empty());
    CHECK(m.Rules().opened() == 0);

    // No game: nothing at all.
    CoopDoors::Input none;
    m.SetInput(none);
    CHECK(Frame(m, g, 3000).empty());
}

TEST_CASE("door mirror: the movers are the shown players in our place who carry a command") {
    std::vector<CoopPuppet::Remote> remotes(6);
    for (std::size_t i = 0; i < remotes.size(); ++i) {
        remotes[i].number = static_cast<std::uint8_t>(i + 1);
        remotes[i].scene = "KakarikoVillage_Child";
        remotes[i].position = {float(i), 0, 0};
        remotes[i].anim = CoopPuppet::Anim{"DoorPushOpenL", 0.0f, 1.0f};
    }
    remotes[1].scene = "HyruleField_Child";       // elsewhere
    remotes[2].fade = 0.0f;                         // not shown
    remotes[3].anim.reset();                        // no command known
    remotes[4].scene = "KakarikoVillage_Adult";     // the same place seven years on
    remotes[4].adult = true;
    remotes[5].scene.reset();                       // scene not known
    auto in = CoopDoors::Plan(kBase, std::string("KakarikoVillage_Child"), kFar, remotes);
    REQUIRE(in.others.size() == 2);
    CHECK(in.others[0].number == 1);
    CHECK(in.others[0].command == "DoorPushOpenL");
    CHECK(in.others[1].number == 5);
    CHECK(in.others[1].adult);
    CHECK(in.own == kFar);
    CHECK(CoopDoors::Plan(0, std::string("KakarikoVillage_Child"), kFar, remotes).others.empty());
    CHECK(CoopDoors::Plan(kBase, std::nullopt, kFar, remotes).others.empty());
}

namespace {
CoopPuppet::Remote At(std::uint8_t number, std::string scene, CoopPuppet::Vec3 at) {
    CoopPuppet::Remote r;
    r.number = number;
    r.scene = std::move(scene);
    r.position = at;
    return r;
}
const CoopPuppet::Target* Find(const CoopPuppet::Input& in, std::uint8_t number) {
    for (const auto& t : in.targets) {
        if (t.number == number) {
            return &t;
        }
    }
    return nullptr;
}
}  // namespace

TEST_CASE("puppet planner (passages): walking out, gone where it left, then let go") {
    CoopPuppet::Planner p;
    const std::string kak = "KakarikoVillage_Child";
    const CoopPuppet::Vec3 exit{-131.0f, 112.0f, 52.6f};
    p.Plan(kBase, kak, kFar, false, {At(2, kak, {-128, 112, 52})}, 0);
    p.Plan(kBase, kak, kFar, false, {At(2, kak, exit)}, 33);
    auto in = p.Plan(kBase, kak, kFar, false, {At(2, "HyruleField_Child", exit)}, 66);
    const auto* t = Find(in, 2);
    REQUIRE(t != nullptr);
    CHECK(t->show == CoopPuppet::Show::show);  // the last of the walk into the exit
    CHECK(t->keep_only);
    CHECK(std::string(t->why) == "walking out");
    CHECK(t->position == exit);
    in = p.Plan(kBase, kak, kFar, false, {At(2, "HyruleField_Child", {200, 159, 19})}, 300);
    t = Find(in, 2);
    REQUIRE(t != nullptr);
    CHECK(t->show == CoopPuppet::Show::hide);  // out of sight where it left, not frozen there
    CHECK(t->keep_only);
    CHECK(std::string(t->why) == "left the scene");
    in = p.Plan(kBase, kak, kFar, false, {At(2, "HyruleField_Child", {200, 159, 19})}, 1300);
    CHECK(Find(in, 2) == nullptr);  // let go: the director deletes it after its grace

    // Off: as before, out of the plan at once.
    CoopPuppet::PlannerSettings s;
    s.passages = false;
    CoopPuppet::Planner off(s);
    off.Plan(kBase, kak, kFar, false, {At(2, kak, exit)}, 0);
    CHECK(off.Plan(kBase, kak, kFar, false, {At(2, "HyruleField_Child", exit)}, 33).targets.empty());
}

TEST_CASE("puppet planner (passages): one coming in is made out of sight and shown at the entrance") {
    CoopPuppet::Planner p;
    const std::string kak = "KakarikoVillage_Child";
    const CoopPuppet::Vec3 field{200.0f, 159.0f, 19.0f};
    const CoopPuppet::Vec3 entrance{-119.8f, 96.6f, 58.7f};
    p.Plan(kBase, kak, kFar, false, {At(3, "HyruleField_Child", field)}, 0);
    auto in = p.Plan(kBase, kak, kFar, false, {At(3, kak, field)}, 100);  // stale place
    auto* t = Find(in, 3);
    REQUIRE(t != nullptr);
    CHECK(t->show == CoopPuppet::Show::hide);
    CHECK_FALSE(t->keep_only);  // made now, so it is ready when it stands there
    CHECK(std::string(t->why) == "arriving");
    in = p.Plan(kBase, kak, kFar, false, {At(3, kak, entrance)}, 200);
    CHECK(Find(in, 3)->show == CoopPuppet::Show::hide);
    in = p.Plan(kBase, kak, kFar, false, {At(3, kak, entrance)}, 600);
    t = Find(in, 3);
    CHECK(t->show == CoopPuppet::Show::show);
    CHECK(t->position == entrance);
}

TEST_CASE("puppet director (passages): a keep-only target never makes a puppet") {
    FakeGuest guest;
    CoopPuppet::Director d;
    CoopPuppet::Target t;
    t.number = 2;
    t.actor = "CoopPuppet_Child";
    t.keep_only = true;
    CoopPuppet::Input in;
    in.main_base = kBase;
    in.scene = "KakarikoVillage_Child";
    in.targets = {t};
    d.SetInput(in);
    CHECK_FALSE(d.Step(guest, false, 0, 0));
    in.targets[0].keep_only = false;
    d.SetInput(in);
    const auto call = d.Step(guest, false, 0, 10);
    REQUIRE(call);
    CHECK(call->function == kBase + CoopPuppet::addr::kMalloc);
}

TEST_CASE("door mirror: a new game asks for its own block, nothing of the old one is written") {
    Game g;
    CoopDoors::Mirror m;
    m.SetInput(With("Wait"));
    CHECK(Frame(m, g, 1000).empty());
    m.SetInput(With("DoorPushOpenL"));
    REQUIRE(Frame(m, g, 1100).size() == 1);  // the block
    auto calls = Frame(m, g, 1200);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kPlayAs);
    // The game stops (no main for a frame) and starts again in the same place.
    m.SetInput(CoopDoors::Input{});
    CHECK(Frame(m, g, 1300).empty());
    m.SetInput(With("Wait"));
    CHECK(Frame(m, g, 1400).empty());
    // A push in the new game: a new block first, not a play written in the old one.
    m.SetInput(With("DoorPushOpenR"));
    calls = Frame(m, g, 1500);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kMalloc);
    calls = Frame(m, g, 1600);
    REQUIRE(calls.size() == 1);
    CHECK(Requested(g, kDoorActor) == "Push_R_ByPlayer");
}

TEST_CASE("door mirror: no play on a door whose animation slots are not there") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    g.guest.Set(kController + CoopPuppet::addr::kSlotCount, std::int32_t{0});  // no slot yet
    m.SetInput(With("Wait"));
    Frame(m, g, 1000);
    m.SetInput(With("DoorPushOpenL"));
    REQUIRE(Frame(m, g, 1100).size() == 1);  // the block
    CHECK(Frame(m, g, 1200).empty());        // chosen, but the AS play is not called
    CHECK(m.Plays() == 0);
    CHECK(Logged(log, "doors: Push_L_ByPlayer not played: the actor has no animation slot"));
}

// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): the door's own logic
// playing over what we played is seen in its slot 0 and played again, at
// most twice; ours kept is the ok path. One check line for each.
#include "coop_check_visual.h"

namespace {
std::vector<CoopPuppet::Call> PushDoor(CoopDoors::Mirror& m, Game& g, std::uint64_t t) {
    m.SetInput(With("Wait"));
    Frame(m, g, t);
    m.SetInput(With("DoorPushOpenL"));
    REQUIRE(Frame(m, g, t + 100).size() == 1);  // the block
    return Frame(m, g, t + 200);                // the push
}
}  // namespace

TEST_CASE("door mirror (riesgos-visual): ours kept is ok, the door's own logic over it is played again, then left") {
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });

    {  // Ours stays 300 ms: the ok line, no second play.
        Game g;
        std::vector<std::string> log;
        CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
        REQUIRE(PushDoor(m, g, 1000).size() == 1);
        g.DoorPlays("Push_L_ByPlayer");
        CHECK(Frame(m, g, 1250).empty());
        CHECK(Frame(m, g, 1549).empty());
        CHECK(checks.empty());
        CHECK(Frame(m, g, 1550).empty());
        REQUIRE(checks.size() == 1);
        CHECK(checks[0] ==
              "coop: check: door mirror ok Obj_Kakariko_Door_A_01 kept our 'Push_L_ByPlayer' 300 ms in its slot 0");
        CHECK(m.Replays() == 0);
        CHECK(m.Plays() == 1);
    }
    {  // Its own logic takes over twice: played again each time, then left.
        Game g;
        std::vector<std::string> log;
        CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
        REQUIRE(PushDoor(m, g, 1000).size() == 1);
        g.DoorPlays("Push_L_ByPlayer");
        CHECK(Frame(m, g, 1250).empty());  // ours shows
        g.DoorPlays("Wait");               // the door's own logic
        auto calls = Frame(m, g, 1300);
        REQUIRE(calls.size() == 1);
        CHECK(Requested(g, kDoorActor) == "Push_L_ByPlayer");
        CHECK(Logged(log, "doors: Obj_Kakariko_Door_A_01: its own 'Wait' took over our 'Push_L_ByPlayer' 50 ms "
                          "after it showed; played again (1 of 2)"));
        g.DoorPlays("Push_L_ByPlayer");
        CHECK(Frame(m, g, 1350).empty());
        g.DoorPlays("Wait");
        REQUIRE(Frame(m, g, 1400).size() == 1);  // the second replay
        CHECK(m.Replays() == 2);
        g.DoorPlays("Push_L_ByPlayer");
        CHECK(Frame(m, g, 1450).empty());
        g.DoorPlays("Wait");
        CHECK(Frame(m, g, 1500).empty());  // no third: left as its game has it
        CHECK(m.Replays() == 2);
        REQUIRE(checks.size() == 2);
        CHECK(checks[1].rfind("coop: check: door mirror fallback Obj_Kakariko_Door_A_01: its own 'Wait' took over "
                              "our 'Push_L_ByPlayer'",
                              0) == 0);
        CHECK(checks[1].find("after 2 replays; left as its game has it") != std::string::npos);
        CHECK(Frame(m, g, 3000).empty());  // nothing watched any more
    }
    {  // Ours never shows in 1 s (the play did not take): played again too.
        Game g;
        std::vector<std::string> log;
        CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
        REQUIRE(PushDoor(m, g, 1000).size() == 1);  // played at 1200; the slot keeps "Wait"
        CHECK(Frame(m, g, 2199).empty());
        REQUIRE(Frame(m, g, 2200).size() == 1);
        CHECK(Logged(log, "our 'Push_L_ByPlayer' never showed in its slot 0 in 1000 ms (it shows 'Wait'); played "
                          "again (1 of 2)"));
        // A scene change forgets what was watched.
        m.SetInput(With("Walk", kInFront, "HyruleField_Child"));
        CHECK(Frame(m, g, 4000).empty());
        CHECK(m.Replays() == 1);
    }
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

// ORM review (oot-remake docs/REVISION_FINAL.md "Visual"): a play is made only
// on the actor it was chosen for. A room unloaded behind a door may leave
// another actor at the same address, present too: no replay lands on it.
TEST_CASE("door mirror (review): no replay on another actor now at the watched door's address") {
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([](const std::string&) {});
    Game g;
    std::vector<std::string> log;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    REQUIRE(PushDoor(m, g, 1000).size() == 1);  // played at 1200; the slot keeps "Wait"
    // The door is freed and a pot is made at its address (state 4, the same AS chain).
    g.guest.SetText(kText, "Obj_Pot_A_08");
    CHECK(Frame(m, g, 2200).empty());  // without the name check: "never showed", played again on the pot
    CHECK(m.Replays() == 0);
    CHECK(m.Plays() == 1);
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}

// ORM review: a chest lid we opened that goes on to its own open command
// (OpenWait once the opening ends, as a lid set near its end by the opener's
// frame does at once) is ours kept: played again it would shut and open again.
TEST_CASE("door mirror (review): a chest that goes on from our opening to its own open wait is not played again") {
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });
    Game g;
    g.guest.SetText(kText, "Obj_TBox_A_01");  // the actor is a small chest
    std::vector<std::string> log;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    m.SetChestLids(false);  // the door rule's chest play alone
    m.SetInput(With("Wait"));
    Frame(m, g, 1000);
    m.SetInput(With("DemoOpenBoxFront_Mini"));
    REQUIRE(Frame(m, g, 1100).size() == 1);  // the block
    REQUIRE(Frame(m, g, 1200).size() == 1);  // its opening
    CHECK(Requested(g, kDoorActor) == "FrontOpenSmall");
    g.DoorPlays("OpenWait");  // already on to its own open wait
    CHECK(Frame(m, g, 1300).empty());
    CHECK(Frame(m, g, 2300).empty());  // no "never showed" replay after 1 s either
    CHECK(m.Replays() == 0);
    CHECK(m.Plays() == 1);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0] == "coop: check: door mirror ok Obj_TBox_A_01 went on from our 'FrontOpenSmall' to its own "
                       "'OpenWait' (open)");
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
