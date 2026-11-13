// Tests for the townspeople's look at the puppets (lichii/hook/coop_npc_gaze.cpp),
// on fake memory laid out as docs/NPC_MIRADA.md says the game has it.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop_npc_gaze.h"

namespace gz = CoopNpcGaze;

namespace {

bool Has(const std::vector<std::string>& log, const std::string& part) {
    for (const auto& line : log) {
        if (line.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
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
        ++writes;
        return true;
    }
    template <typename T>
    void Put(std::uint64_t at, const T& value) {
        REQUIRE(Write(at, &value, sizeof(T)));
    }
    template <typename T>
    T Peek(std::uint64_t at) {
        T v{};
        REQUIRE(Read(at, &v, sizeof(T)));
        return v;
    }
    int writes = 0;

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

// One townsperson with a Target component, our Link, one puppet, and the
// link's data, as f_808aefe0 leaves them: slot `slot` links our Link.
struct World {
    FakeGuest g;
    static constexpr std::uint64_t kNpc = 0x2000000;
    static constexpr std::uint64_t kTable = 0x2100000;
    static constexpr std::uint64_t kComponent = 0x2200000;
    static constexpr std::uint64_t kLink = 0x3000000;
    static constexpr std::uint64_t kLinkData = 0x3100000;
    static constexpr std::uint64_t kPuppet = 0x4000000;
    static constexpr std::uint32_t kLinkKey = 0x1234;
    static constexpr std::uint32_t kPuppetKey = 0x5678;
    static constexpr std::uint32_t kLinkId = 77;

    World() {
        g.Map(kNpc, 0x1000);
        g.Map(kTable, 0x1000);
        g.Map(kComponent, gz::kSlotsAt + gz::kSlotSize * gz::kSlotCount);
        g.Map(kLink, 0x1000);
        g.Map(kLinkData, 0x100);
        g.Map(kPuppet, 0x1000);
        g.Put<std::uint64_t>(kNpc + gz::kComponentTable, kTable);
        g.Put<std::uint64_t>(kTable + gz::kTargetComponent, kComponent);
        g.Put<std::uint32_t>(kLink + gz::kActorKey, kLinkKey);
        g.Put<std::uint32_t>(kPuppet + gz::kActorKey, kPuppetKey);
        g.Put<std::uint64_t>(kLinkData + gz::kLinkDataActor, kLink);
        g.Put<std::uint32_t>(kLinkData + gz::kLinkDataId, kLinkId);
        SetPuppet({4.0f, 0.0f, 2.0f});
    }
    std::uint64_t Slot(unsigned i) const { return kComponent + gz::kSlotsAt + gz::kSlotSize * i; }
    // The game links our Link into slot `i` (f_808aefe0): kind 2, its link and key.
    void LinkLink(unsigned i, std::uint8_t flags = 0x01) {
        g.Put<std::uint8_t>(Slot(i) + gz::kSlotKind, gz::kKindActor);
        g.Put<std::uint8_t>(Slot(i) + gz::kSlotFlags, flags);
        g.Put<std::uint64_t>(Slot(i) + gz::kSlotLinkData, kLinkData);
        g.Put<std::uint32_t>(Slot(i) + gz::kSlotLinkId, kLinkId);
        g.Put<std::uint32_t>(Slot(i) + gz::kSlotActorKey, kLinkKey);
    }
    void SetPuppet(const gz::Vec3& feet) {
        REQUIRE(g.Write(kPuppet + gz::kActorPosition, feet.data(), 12));
    }
    gz::Vec3 Place(unsigned i) {
        gz::Vec3 v{};
        REQUIRE(g.Read(Slot(i) + gz::kSlotPlace, v.data(), 12));
        return v;
    }
    std::uint8_t Kind(unsigned i) { return g.Peek<std::uint8_t>(Slot(i) + gz::kSlotKind); }
    std::uint8_t Flags(unsigned i) { return g.Peek<std::uint8_t>(Slot(i) + gz::kSlotFlags); }
};

const gz::Writer::Alive kAll = [](CoopPuppet::Guest&, std::uint64_t) { return true; };

gz::Npc Npc(std::uint64_t actor, gz::Vec3 at, const char* name = "Npc_Kokiri_002_Child") {
    gz::Npc n;
    n.actor = actor;
    n.name = name;
    n.at = at;
    return n;
}

} // namespace

TEST_CASE("npc gaze: the slots of the Target component, and which name our Link") {
    World w;
    CHECK(gz::ReadSlots(w.g, w.kNpc).empty());  // all kind 0
    w.LinkLink(3);
    w.g.Put<std::uint8_t>(w.Slot(5) + gz::kSlotKind, gz::kKindPlace);
    const auto slots = gz::ReadSlots(w.g, w.kNpc);
    REQUIRE(slots.size() == 2);
    CHECK(slots[0].index == 3);
    CHECK(slots[0].at == w.Slot(3));
    REQUIRE(slots[0].link_actor);
    CHECK(*slots[0].link_actor == w.kLink);
    CHECK(slots[1].index == 5);
    CHECK(gz::SlotNames(slots[0], w.kLink, w.kLinkKey) == gz::Names::both);
    CHECK(gz::SlotNames(slots[0], w.kLink, std::nullopt) == gz::Names::by_link);
    CHECK(gz::SlotNames(slots[1], w.kLink, w.kLinkKey) == gz::Names::no);  // a place slot names no actor
    CHECK(gz::SlotNames(slots[0], w.kPuppet, w.kPuppetKey) == gz::Names::no);
    // The link's data no longer agrees with the slot's id (a stale link): only the key tells.
    w.g.Put<std::uint32_t>(w.kLinkData + gz::kLinkDataId, 78);
    const auto stale = gz::ReadSlots(w.g, w.kNpc);
    CHECK_FALSE(stale[0].link_actor);
    CHECK(gz::SlotNames(stale[0], w.kLink, w.kLinkKey) == gz::Names::by_key);
    CHECK(gz::SlotNames(stale[0], w.kLink, 0xffffffffu) == gz::Names::no);  // -1 never matches
    // No component: nothing.
    w.g.Put<std::uint64_t>(w.kTable + gz::kTargetComponent, 0);
    CHECK_FALSE(gz::TargetComponent(w.g, w.kNpc));
    CHECK(gz::ReadSlots(w.g, w.kNpc).empty());
    CHECK(gz::ReadSlots(w.g, 0).empty());
}

TEST_CASE("npc gaze rule: the nearest player within reach, by a margin, held a second") {
    gz::Rule rule;
    std::vector<std::string> log;
    std::uint64_t now = 1'000'000;
    const std::vector<gz::Npc> npcs{Npc(0x10, {0.0f, 0.0f, 0.0f})};
    gz::Vec3 self{0.0f, 0.0f, 5.0f};
    std::vector<gz::Partner> partners{{2, {0.0f, 0.0f, 4.5f}}};
    // P2 nearer, but not by the margin: our Link keeps it (the game's own).
    CHECK(rule.Plan(npcs, self, partners, now, log).empty());
    // P2 at 2 m: his.
    partners[0].at = {0.0f, 0.0f, 2.0f};
    auto orders = rule.Plan(npcs, self, partners, now += 100'000, log);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].player == 2);
    CHECK(Has(log, "looks at P2 (nearest, 2.0 m; our Link 5.0 m)"));
    // Our Link walks nearer than P2 within the hold: still P2.
    self = {0.0f, 0.0f, 0.5f};
    CHECK(rule.Plan(npcs, self, partners, now += 500'000, log).size() == 1);
    // After the hold: ours again.
    CHECK(rule.Plan(npcs, self, partners, now += 600'000, log).empty());
    CHECK(Has(log, "looks as the game has it again (our Link 0.5 m)"));
    CHECK(rule.LookOf(0x10) == 0);
    // Out of reach (look_m 10): nobody's.
    self = {0.0f, 0.0f, 30.0f};
    partners[0].at = {0.0f, 0.0f, 12.0f};
    CHECK(rule.Plan(npcs, self, partners, now += 2'000'000, log).empty());
    // P3 and P2 both near: the nearer partner; P3 leaves: P2 at once (the one looked at is gone).
    partners = {{2, {0.0f, 0.0f, 3.0f}}, {3, {0.0f, 0.0f, 1.0f}}};
    orders = rule.Plan(npcs, self, partners, now += 100'000, log);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].player == 3);
    partners.pop_back();
    orders = rule.Plan(npcs, self, partners, now += 100'000, log);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].player == 2);
    // Our Link not known: nothing turned.
    CHECK(rule.Plan(npcs, std::nullopt, partners, now += 100'000, log).empty());
}

TEST_CASE("npc gaze rule: the partner who talks to it wins, our own talk or event gives it back") {
    gz::Rule rule;
    std::vector<std::string> log;
    std::uint64_t now = 1'000'000;
    gz::Npc mido = Npc(0x20, {0.0f, 0.0f, 0.0f}, "Npc_Kokiri_Mido_Child");
    const gz::Vec3 self{0.0f, 0.0f, 1.0f};  // we are nearest
    const std::vector<gz::Partner> partners{{2, {0.0f, 0.0f, 3.0f}}, {4, {0.0f, 0.0f, 2.0f}}};
    CHECK(rule.Plan({mido}, self, partners, now, log).empty());
    mido.talked_by = 2;
    auto orders = rule.Plan({mido}, self, partners, now += 100'000, log);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].player == 2);
    CHECK(orders[0].name == "Npc_Kokiri_Mido_Child");
    CHECK(Has(log, "looks at P2 (talks to it"));
    // The talker's puppet is not here (not live in our place): the distances decide.
    mido.talked_by = 5;
    CHECK(rule.Plan({mido}, self, partners, now += 100'000, log).empty());
    // P2's talk is over: the distances (our Link is nearest), at once (no hold after a talk).
    mido.talked_by = 2;
    rule.Plan({mido}, self, partners, now += 100'000, log);
    mido.talked_by = 0;
    CHECK(rule.Plan({mido}, self, partners, now += 100'000, log).empty());
    // Our own talk: never turned, even with P2 talking (two talks at once, each game its own).
    mido.talked_by = 2;
    mido.ours_talk = true;
    CHECK(rule.Plan({mido}, self, partners, now += 100'000, log).empty());
}

TEST_CASE("npc gaze rule: at most max_npcs turned, the nearest to our Link") {
    gz::Settings s;
    s.max_npcs = 2;
    gz::Rule rule(s);
    std::vector<std::string> log;
    std::vector<gz::Npc> npcs;
    for (int i = 0; i < 4; ++i) {
        npcs.push_back(Npc(0x100 + i, {static_cast<float>(i) * 3.0f, 0.0f, 0.0f}));
    }
    // Each one's nearest is a partner standing on it; our Link far to the -x.
    std::vector<gz::Partner> partners;
    for (int i = 0; i < 4; ++i) {
        partners.push_back({static_cast<std::uint8_t>(2 + i), {static_cast<float>(i) * 3.0f, 0.0f, 0.5f}});
    }
    const auto orders = rule.Plan(npcs, gz::Vec3{-6.0f, 0.0f, 0.0f}, partners, 1'000'000, log);
    REQUIRE(orders.size() == 2);
    CHECK(orders[0].npc == 0x100);
    CHECK(orders[1].npc == 0x101);
}

TEST_CASE("npc gaze writer: the slot that links our Link turned to the puppet's eyes, then given back") {
    World w;
    w.LinkLink(2, 0x03);
    w.LinkLink(6);
    gz::Writer writer;
    const std::vector<gz::PuppetAt> puppets{{2, w.kPuppet, false}};
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    // Both slots that link our Link: place slots at the child puppet's eyes, no rotation.
    for (const unsigned i : {2u, 6u}) {
        CHECK(w.Kind(i) == gz::kKindPlace);
        CHECK((w.Flags(i) & gz::kPlaceFlags) == 0);
        const gz::Vec3 p = w.Place(i);
        CHECK(p[0] == doctest::Approx(4.0f));
        CHECK(p[1] == doctest::Approx(1.1f));
        CHECK(p[2] == doctest::Approx(2.0f));
    }
    // The link itself untouched.
    CHECK(w.g.Peek<std::uint64_t>(w.Slot(2) + gz::kSlotLinkData) == w.kLinkData);
    CHECK(w.g.Peek<std::uint32_t>(w.Slot(2) + gz::kSlotLinkId) == w.kLinkId);
    auto log = writer.TakeLog();
    CHECK(Has(log, "npc gaze: Npc_Kokiri_002_Child 0x2000000 slot 2 links our Link (by its link and its key): "
                   "turned to P2's puppet"));
    // The puppet walks: the place follows.
    w.SetPuppet({5.0f, 1.0f, 2.0f});
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Place(2)[0] == doctest::Approx(5.0f));
    CHECK(w.Place(2)[1] == doctest::Approx(2.1f));
    gz::Counts c = writer.TakeCounts();
    CHECK(c.written == 4);
    CHECK(c.kept == 2);
    // The order goes (our Link nearer again): both given back as they were.
    writer.SetOrders({}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(2) == gz::kKindActor);
    CHECK(w.Flags(2) == 0x03);
    CHECK(w.Kind(6) == gz::kKindActor);
    CHECK(w.Flags(6) == 0x01);
    c = writer.TakeCounts();
    CHECK(c.given_back == 2);
    const int writes = w.g.writes;
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.g.writes == writes);  // nothing more
}

TEST_CASE("npc gaze writer: a slot the game set again is its own, a gone puppet or townsperson gives back or skips") {
    World w;
    w.LinkLink(0);
    gz::Writer writer;
    std::vector<gz::PuppetAt> puppets{{3, w.kPuppet, true}};
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 3}}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(0) == gz::kKindPlace);
    CHECK(w.Place(0)[1] == doctest::Approx(1.6f));  // the adult's eyes
    // The AI re-targets another actor into slot 0 (kind 2, another link): not ours any more.
    w.g.Put<std::uint8_t>(w.Slot(0) + gz::kSlotKind, gz::kKindActor);
    w.g.Put<std::uint32_t>(w.Slot(0) + gz::kSlotLinkId, 99);
    w.g.Put<std::uint32_t>(w.Slot(0) + gz::kSlotActorKey, 0x9999);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(0) == gz::kKindActor);  // left as the game set it
    CHECK(w.g.Peek<std::uint32_t>(w.Slot(0) + gz::kSlotLinkId) == 99);
    gz::Counts c = writer.TakeCounts();
    CHECK(c.overwritten == 1);
    CHECK(c.no_slot == 1);
    CHECK(Has(writer.TakeLog(), "no slot links our Link (slots in use: 0:2"));
    // It links our Link again: turned again.
    w.LinkLink(0);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(0) == gz::kKindPlace);
    // The puppet is gone (hidden, left): given back at once.
    puppets.clear();
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(0) == gz::kKindActor);
    // A townsperson that is no live actor is not read or written.
    puppets = {{3, w.kPuppet, true}};
    const int writes = w.g.writes;
    writer.OnCalcFrame(w.g, puppets, [](CoopPuppet::Guest&, std::uint64_t a) { return a != World::kNpc; });
    CHECK(w.g.writes == writes);
    // Our Link not known: nothing written.
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 3}}, 0);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.g.writes == writes);
}

TEST_CASE("npc gaze writer: a key that is the puppet's too is not ours to tell apart") {
    World w;
    w.LinkLink(1);
    w.g.Put<std::uint32_t>(w.kPuppet + gz::kActorKey, w.kLinkKey);  // the same key on both
    w.g.Put<std::uint64_t>(w.Slot(1) + gz::kSlotLinkData, 0);       // and no link to tell by
    gz::Writer writer;
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    writer.OnCalcFrame(w.g, {{2, w.kPuppet, false}}, kAll);
    CHECK(w.Kind(1) == gz::kKindActor);
    CHECK(writer.TakeCounts().ambiguous == 1);
}

TEST_CASE("npc gaze writer: the self-check, ok when the slots stay as written") {
    World w;
    w.LinkLink(0);
    gz::Settings s;
    s.judge_frames = 10;
    gz::Writer writer(s);
    const std::vector<gz::PuppetAt> puppets{{2, w.kPuppet, false}};
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    for (int i = 0; i < 12; ++i) {
        writer.OnCalcFrame(w.g, puppets, kAll);
    }
    const auto log = writer.TakeLog();
    CHECK(Has(log, "check: npc gaze ok (10 of 10 slot-frames kept as written over 10 frames)"));
    CHECK_FALSE(writer.fell_back());
    CHECK(w.Kind(0) == gz::kKindPlace);
}

TEST_CASE("npc gaze writer: the self-check falls back when the game sets the slot again every frame") {
    World w;
    w.LinkLink(0);
    gz::Settings s;
    s.judge_frames = 10;
    gz::Writer writer(s);
    const std::vector<gz::PuppetAt> puppets{{2, w.kPuppet, false}};
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    for (int i = 0; i < 12; ++i) {
        writer.OnCalcFrame(w.g, puppets, kAll);
        w.LinkLink(0);  // the game's AI links our Link again within the frame
    }
    const auto log = writer.TakeLog();
    CHECK(Has(log, "check: npc gaze fallback (the game set the slots again in 10 of 10 slot-frames)"));
    CHECK(writer.fell_back());
    const int writes = w.g.writes;
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.g.writes == writes);  // nothing more this run
    CHECK(w.Kind(0) == gz::kKindActor);
    writer.Reset();
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.g.writes == writes);  // a new scene does not undo the fallback
}

TEST_CASE("npc gaze writer: a give-back after the game took the slot leaves it alone") {
    World w;
    w.LinkLink(0);
    gz::Writer writer;
    const std::vector<gz::PuppetAt> puppets{{2, w.kPuppet, false}};
    writer.SetOrders({{w.kNpc, "Npc_Kokiri_002_Child", 2}}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    // The game puts a place of its own in the slot (another point), then the order goes.
    const gz::Vec3 its{9.0f, 9.0f, 9.0f};
    REQUIRE(w.g.Write(w.Slot(0) + gz::kSlotPlace, its.data(), 12));
    writer.SetOrders({}, w.kLink);
    writer.OnCalcFrame(w.g, puppets, kAll);
    CHECK(w.Kind(0) == gz::kKindPlace);  // its own place slot, not set back to the link
    CHECK(w.Place(0)[0] == doctest::Approx(9.0f));
    const gz::Counts c = writer.TakeCounts();
    CHECK(c.overwritten == 1);
    CHECK(c.given_back == 0);
}
