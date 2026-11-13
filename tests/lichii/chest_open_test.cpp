// ORM cofre (oot-remake docs/DETALLES_MARIONETA.md, "Cofres"): the chest lids
// in the door mirror's game side (lichii/hook/coop_doors.cpp) on fake guest
// memory, and the portable part of lichii/hook/coop_chest_open.cpp.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop_chest_open.h"
#include "coop_doors.h"
#include "coop_notices.h"
#include "coop_puppet.h"
#include "coop_spawn.h"

namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kBase = 0x80000000;   // main
constexpr std::uint64_t kHeap = 0x200000000;  // what the fake malloc gives
constexpr std::uint64_t kManager = 0x300000000;
constexpr std::uint64_t kLists = 0x300001000;
constexpr std::uint64_t kHead = kLists + CoopDoors::addr::kListHead;
constexpr std::uint64_t kChest = 0x400000000;
constexpr std::uint64_t kPlacement = 0x400002000;
constexpr std::uint64_t kText = 0x410000000;
constexpr std::uint64_t kAs = 0x500000000;
constexpr std::uint64_t kController = kAs + 0x100;
constexpr std::uint64_t kSlot0 = kAs + 0x200;
// A Forest Temple small key chest (coop/host/src/chest_list.inc).
constexpr std::uint64_t kHash = 0x1041dd4d83575dda;
const std::string kScene = "ForestTemple";
const CoopDoors::Vec3 kChestAt{10.0f, 100.0f, -20.0f};
const CoopDoors::Vec3 kBeside{10.0f, 100.0f, -18.8f};
const CoopDoors::Vec3 kFar{40.0f, 100.0f, -20.0f};

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

// The Forest Temple: one small chest on the live actor list, its placement
// and its AS (slot 0 "CloseWait", frame 0).
struct Game {
    FakeGuest guest;
    Game() {
        namespace a = CoopDoors::addr;
        guest.Map(kBase + a::kActorManagerPtr, 8);
        guest.Map(kManager, 0x200);
        guest.Map(kLists, 0x1000);
        guest.Map(kChest, 0x1000);
        guest.Map(kPlacement, 0x100);
        guest.Map(kText, 0x1000);
        guest.Map(kAs, 0x1000);
        guest.Map(kHeap, 0x1000);
        guest.Set(kBase + a::kActorManagerPtr, kManager);
        guest.Set(kManager + a::kManagerLists, kLists);
        const std::uint64_t node = kChest + a::kActorNode;
        guest.Set(kHead, node);
        guest.Set(kHead + a::kListCount, std::uint32_t{1});
        guest.Set(node, kHead);
        guest.Set(node + a::kNodeOwner, kChest);
        guest.SetText(kText, "Obj_TBox_A_01");
        guest.Set(kChest + a::kActorName, kText);
        guest.Set(kChest + a::kActorState, a::kStatePresent);
        guest.Set(kChest + a::kActorPosition, kChestAt);
        guest.Set(kChest + a::kActorPlacement, kPlacement);
        guest.Set(kPlacement + a::kPlacementHash, kHash);
        guest.Set(kChest + 0x270, kAs);
        guest.Set(kAs + 0x40, kAs + 0x40);
        guest.Set(kAs + 0x40 + 0x18, kAs + 0x80);
        guest.Set(kAs + 0x80 + 0x18, kController);
        guest.Set(kController + CoopPuppet::addr::kSlotCount, std::int32_t{1});
        guest.Set(kController + CoopPuppet::addr::kSlots, kAs + 0x300);
        guest.Set(kAs + 0x300, kSlot0);
        Plays("CloseWait", 0.0f);
    }
    // What the chest's slot 0 plays now, and its frame.
    void Plays(const std::string& command, float frame) {
        guest.SetText(kText + 0x800, command);
        guest.Set(kSlot0 + CoopPuppet::addr::kSlotName, kText + 0x800);
        guest.Set(kSlot0 + CoopPuppet::addr::kSlotFrame, frame);
    }
};

CoopDoors::Input With(CoopDoors::Vec3 own, std::string own_command = "Wait",
                      std::uint64_t server_us = 50'000'000) {
    CoopDoors::Input in;
    in.main_base = kBase;
    in.scene = kScene;
    in.own = own;
    in.own_command = std::move(own_command);
    in.server_now_us = server_us;
    return in;
}

proto::ChestOpen From(std::uint8_t player, float frame = 4.0f, std::uint64_t server_us = 49'000'000) {
    proto::ChestOpen m;
    m.player_number = player;
    m.chest = kHash;
    m.scene = kScene;
    m.animation = proto::Animation{"FrontOpenSmall", frame, 1.0f};
    m.server_time_us = server_us;
    return m;
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

std::string Requested(Game& g, std::uint64_t actor) {
    CHECK(g.guest.Get<std::uint64_t>(kHeap + CoopDoors::block::kRequest) == actor);
    CHECK(g.guest.Get<std::int32_t>(kHeap + 0x10) == 0);  // slot 0
    return g.guest.GetText(g.guest.Get<std::uint64_t>(kHeap + 0x08));
}

bool Logged(const std::vector<std::string>& log, const std::string& part) {
    return std::any_of(log.begin(), log.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

}  // namespace

TEST_CASE("chest lids: another player's CHEST_OPEN opens our chest of that placement, at his frame") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    m.SetInput(With(kFar));
    CHECK(Frame(m, g, 900).empty());  // nothing to read
    m.ReceiveChestOpen(From(2));
    auto calls = Frame(m, g, 1000);
    REQUIRE(calls.size() == 1);  // the block first
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kMalloc);
    calls = Frame(m, g, 1100);  // the walk finds it by its hash: the lid plays, then its frame
    REQUIRE(calls.size() == 2);
    CHECK(calls[0].function == kBase + CoopPuppet::addr::kPlayAs);
    CHECK(Requested(g, kChest) == "FrontOpenSmall");
    CHECK(calls[1].function == kBase + CoopPuppet::addr::kSetAsFrame);
    CHECK(calls[1].x[0] == kController);
    CHECK(calls[1].x[1] == 0);
    CHECK(calls[1].s0 == doctest::Approx(34.0f));  // 4 + 1 s at 30 frames a second
    CHECK(m.Plays() == 1);
    CHECK(m.LidsOpened() == 1);
    CHECK(m.LidsShown() == std::vector<std::uint64_t>{kChest});
    CHECK(Logged(log, "chests: P2 opened 0x1041dd4d83575dda: Obj_TBox_A_01 opens here too ('FrontOpenSmall'"));
    g.Plays("FrontOpenSmall", 34.0f);
    CHECK(Frame(m, g, 1200).empty());  // once
    CHECK(m.TakeChestOpens().empty());  // nothing of ours
}

TEST_CASE("chest lids: his puppet's chest move and his CHEST_OPEN open the lid once") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    CoopDoors::Input in = With(kFar);
    coop::host::DoorMover p2;
    p2.number = 2;
    p2.position = kBeside;
    p2.command = "DemoOpenTreasureBoxFront";
    in.others.push_back(p2);
    m.SetInput(in);
    m.ReceiveChestOpen(From(2));
    REQUIRE(Frame(m, g, 1000).size() == 1);  // the block
    const auto calls = Frame(m, g, 1100);
    REQUIRE(calls.size() == 2);  // the lid and its frame: the door rule leaves it
    CHECK(Requested(g, kChest) == "FrontOpenSmall");
    CHECK(m.Plays() == 1);
    CHECK(Logged(log, "left to its CHEST_OPEN, which opened it"));
}

TEST_CASE("chest lids: the chest our Link opens is told as CHEST_OPEN when its lid moves") {
    std::vector<std::string> log;
    Game g;
    CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
    m.SetInput(With(kBeside, "DemoOpenTreasureBoxFront"));
    REQUIRE(Frame(m, g, 1000).size() == 1);  // the block
    CHECK(Frame(m, g, 1100).empty());        // the chest, still closed
    CHECK(m.TakeChestOpens().empty());
    g.Plays("FrontOpenSmall", 2.0f);  // the game opens it for our Link
    CHECK(Frame(m, g, 1200).empty());  // nothing played: ours, the game's own
    auto told = m.TakeChestOpens();
    REQUIRE(told.size() == 1);
    CHECK(told[0].player_number == proto::kPlayerSelf);
    CHECK(told[0].chest == kHash);
    CHECK(told[0].scene == kScene);
    CHECK(told[0].animation.name == "FrontOpenSmall");
    CHECK(told[0].animation.frame == doctest::Approx(2.0f));
    CHECK(told[0].server_time_us == 50'000'000);
    CHECK(m.TakeChestOpens().empty());
    CHECK(Frame(m, g, 1300).empty());
    CHECK(m.TakeChestOpens().empty());  // once
    CHECK(Logged(log, "chests: our Link opens Obj_TBox_A_01 0x1041dd4d83575dda (Item_Dungeon_SmallKey)"));
    // What is told goes on the wire as type 249.
    auto bytes = proto::encode(proto::Message{told[0]});
    REQUIRE(bytes);
    proto::Message back;
    REQUIRE(proto::decode(*bytes, back) == proto::Error::none);
    CHECK(proto::type_of(back) == proto::MessageType::chest_open);
}

TEST_CASE("chest lids: off, nothing is told or opened, the door rule is as before") {
    Game g;
    CoopDoors::Mirror m;
    m.SetChestLids(false);
    m.SetInput(With(kBeside, "DemoOpenTreasureBoxFront"));
    m.ReceiveChestOpen(From(2));
    CHECK(Frame(m, g, 1000).empty());  // nothing wants the actors: no block either
    g.Plays("FrontOpenSmall", 2.0f);
    CHECK(Frame(m, g, 1100).empty());
    CHECK(m.TakeChestOpens().empty());
    CHECK(m.LidsShown().empty());
    CHECK(m.Plays() == 0);
}

TEST_CASE("chest finds: the others as finders, and their finds as stand-in copies") {
    std::vector<CoopPuppet::Remote> remotes(3);
    remotes[0].number = 2;
    remotes[0].scene = kScene;
    remotes[0].position = kBeside;
    remotes[0].adult = true;
    remotes[0].anim = CoopPuppet::Anim{"DemoGet", 3.0f, 1.0f};
    remotes[1].number = 3;
    remotes[1].fade = 0.0f;  // faded out: not drawn here
    remotes[2].number = 0;   // not a player
    const auto mates = CoopChestOpen::Mates(remotes);
    REQUIRE(mates.size() == 2);
    CHECK(mates[0].number == 2);
    CHECK(mates[0].command == "DemoGet");
    CHECK(mates[0].adult);
    CHECK(mates[0].shown);
    CHECK(mates[0].scene == kScene);
    CHECK(mates[1].command.empty());
    CHECK_FALSE(mates[1].shown);

    coop::host::HeldFind h;
    h.number = 2;
    h.serial = 7;
    h.drop = "Item_Dungeon_SmallKey";
    h.position = {1.0f, 2.0f, 3.0f};
    const auto copies = CoopChestOpen::Copies({h, coop::host::HeldFind{}});
    REQUIRE(copies.size() == 1);
    CHECK(copies[0].key == (CoopSpawn::Director::kFindKeyBit | (std::uint64_t{7} << 8) | 2));
    CHECK(copies[0].actor == "Item_Dungeon_SmallKey");
    CHECK(copies[0].act == CoopSpawn::Act::show);
    CHECK(copies[0].place);
    CHECK(copies[0].position == proto::Vec3{1.0f, 2.0f, 3.0f});
    // Never the game's own item-get actor: only its model-only stand-in.
    CHECK(std::string(CoopSpawn::StandIn("Item_Dungeon_SmallKey")) == "CoopGet_Item_Dungeon_SmallKey");
    CHECK(CoopSpawn::StandInOnly("Item_Dungeon_SmallKey"));
    CHECK(CoopSpawn::StandInOnly("TBox_Rupee_Red"));
    CHECK_FALSE(CoopSpawn::IsShared("Item_Dungeon_SmallKey"));  // ours is never sent as a shot
    CHECK(CoopSpawn::StandInsIn("{\"shots\": [{\"actor\": \"CoopGet_Item_Dungeon_SmallKey\"}, "
                                "{\"actor\": \"CoopShot_Seed\"}]}") ==
          std::set<std::string>{"CoopGet_Item_Dungeon_SmallKey", "CoopShot_Seed"});
}

TEST_CASE("chest finds: the HUD's notice board keeps 3 lines 5 s, the name apart") {
    CoopNotices::Board b;
    b.Add(2, "Ana", "Ana encontró una llave pequeña", 1000);
    auto lines = b.Current(1000);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].number == 2);
    CHECK(lines[0].name == "Ana");
    CHECK(lines[0].rest == " encontró una llave pequeña");
    b.Add(0, "", "Alguien abrió un cofre", 2000);
    b.Add(3, "Tobi", "Tobi encontró el mapa", 3000);
    b.Add(3, "Tobi", "Tobi encontró la brújula", 4000);
    lines = b.Current(4000);
    REQUIRE(lines.size() == 3);  // the newest three
    CHECK(lines[0].rest == "Alguien abrió un cofre");
    CHECK(lines[0].name.empty());
    CHECK(lines[2].rest == " encontró la brújula");
    lines = b.Current(7000);  // 5 s each
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].rest == " encontró el mapa");
    CHECK(b.Current(9000).empty());
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): a chest whose
// lid opened here for another player, opened again by our Link before its
// flag came, is told; none in the window is the ok path.
#include "coop_check_visual.h"

TEST_CASE("chest lids (riesgos-visual): our Link opening a chest its lid opened for another, within 5 s, is told") {
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });
    {
        // Nobody opens it again: the ok line once the window is over.
        Game g;
        CoopDoors::Mirror m;
        m.SetInput(With(kFar));
        Frame(m, g, 900);
        m.ReceiveChestOpen(From(2));
        Frame(m, g, 1000);                       // the block
        REQUIRE(Frame(m, g, 1100).size() == 2);  // the lid and its frame
        g.Plays("FrontOpenSmall", 34.0f);
        Frame(m, g, 3000);
        CHECK(checks.empty());
        Frame(m, g, 6200);
        REQUIRE(checks.size() >= 1);
        CHECK(std::find(checks.begin(), checks.end(),
                        "coop: check: chest reopen ok Obj_TBox_A_01: not opened again here in the 5 s after its lid "
                        "opened for another player") != checks.end());
        CHECK(m.Reopens() == 0);
    }
    {
        // Our Link walks up and opens it 1.4 s after: the fallback line.
        Game g;
        std::vector<std::string> log;
        CoopDoors::Mirror m([&](const std::string& line) { log.push_back(line); });
        m.SetInput(With(kFar));
        Frame(m, g, 900);
        m.ReceiveChestOpen(From(2));
        Frame(m, g, 1000);
        REQUIRE(Frame(m, g, 1100).size() == 2);
        g.Plays("FrontOpenSmall", 34.0f);
        m.SetInput(With({10.0f, 100.0f, -21.0f}));
        Frame(m, g, 2000);
        m.SetInput(With({10.0f, 100.0f, -21.0f}, "Demo_Front_OpenTreasureBox_Small"));
        Frame(m, g, 2500);
        CHECK(m.Reopens() == 1);
        CHECK(m.TakeSecondFinds() == std::vector<std::uint64_t>{kHash});  // its find is his: ours left out
        CHECK(m.TakeSecondFinds().empty());
        CHECK(std::find(checks.begin(), checks.end(),
                        "coop: check: chest reopen fallback Obj_TBox_A_01: our Link opens it 1400 ms after its lid "
                        "opened here for another player, before its opened flag came: its find is his, ours is not counted again") !=
              checks.end());
        CHECK(Logged(log, "chests: Obj_TBox_A_01: our Link opens it 1400 ms after"));
    }
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
