// Tests for asking the game to start an event by name (lichii/hook/orm_story_events.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "orm_story_events.h"

using OrmStoryEvents::Requester;

namespace {

constexpr std::uint64_t kMain = 0x80000000;
constexpr std::uint64_t kActor = 0x90000000;
constexpr std::uint64_t kTable = 0x90010000;
constexpr std::uint64_t kComponent = 0x90020000;
constexpr std::uint64_t kBlock = 0x90030000;

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
    void Set(std::uint64_t address, const T& v) {
        REQUIRE(Write(address, &v, sizeof(T)));
    }
    template <typename T>
    T Get(std::uint64_t address) {
        T v{};
        REQUIRE(Read(address, &v, sizeof(T)));
        return v;
    }

private:
    std::map<std::uint64_t, std::array<std::uint8_t, 0x1000>> pages_;
};

// Link present, his component table with the event component at +0x90 (phase 0),
// and the block the game's malloc will give.
FakeGuest MakeGame() {
    FakeGuest g;
    g.Map(kActor, 0x1000);
    g.Map(kTable, 0x1000);
    g.Map(kComponent, 0x1000);
    g.Map(kBlock, 0x1000);
    g.Set<std::uint32_t>(kActor + OrmStoryEvents::kActorState, OrmStoryEvents::kActorStatePresent);
    g.Set<std::uint64_t>(kActor + OrmStoryEvents::kActorComponents, kTable);
    g.Set<std::uint64_t>(kTable + OrmStoryEvents::kComponentEvent, kComponent);
    g.Set<std::uint32_t>(kComponent + OrmStoryEvents::kComponentPhase, 0);
    return g;
}

std::string GuestString(FakeGuest& g, std::uint64_t at) {
    std::string s;
    for (char c = g.Get<char>(at); c != 0; c = g.Get<char>(++at)) {
        s += c;
    }
    return s;
}

bool Mentions(const std::vector<std::string>& log, const std::string& what) {
    for (const auto& line : log) {
        if (line.find(what) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("story: event names") {
    CHECK(OrmStoryEvents::ValidName("KokiriForest_Meet_DekuTree_MeetDekuTree_01"));
    CHECK(OrmStoryEvents::ValidName("DmF_SY_GameOver"));
    CHECK(OrmStoryEvents::ValidName(std::string(127, 'a')));
    CHECK_FALSE(OrmStoryEvents::ValidName(""));
    CHECK_FALSE(OrmStoryEvents::ValidName(std::string(128, 'a')));
    CHECK_FALSE(OrmStoryEvents::ValidName("Has space"));
    CHECK_FALSE(OrmStoryEvents::ValidName("Dm-0070"));
}

TEST_CASE("story: a request is a malloc once, then main+0x4aa890(Link's event component, &box)") {
    FakeGuest g = MakeGame();
    Requester r;
    // Nothing asked: no call.
    CHECK_FALSE(r.Step(g, false, 0));
    REQUIRE(r.Ask(kMain, kActor, "KokiriForest_Meet_DekuTree_MeetDekuTree_01"));

    auto c = r.Step(g, false, 0);
    REQUIRE(c);
    CHECK(c->function == kMain + OrmStoryEvents::kMalloc);
    CHECK(c->x[0] == OrmStoryEvents::kBlockSize);
    CHECK(OrmStoryEvents::kBlockSize >= OrmStoryEvents::kBlockName + 128);

    c = r.Step(g, true, kBlock);  // the malloc's answer: the request follows at once
    REQUIRE(c);
    CHECK(c->function == kMain + OrmStoryEvents::kRequestEvent);
    CHECK(c->x[0] == kComponent);
    CHECK(c->x[1] == kBlock + OrmStoryEvents::kBlockBox);
    // The box's first field is the name's char*, the name NUL-terminated in our block.
    CHECK(g.Get<std::uint64_t>(kBlock + OrmStoryEvents::kBlockBox) == kBlock + OrmStoryEvents::kBlockName);
    CHECK(GuestString(g, kBlock + OrmStoryEvents::kBlockName) == "KokiriForest_Meet_DekuTree_MeetDekuTree_01");
    auto log = r.TakeLog();
    CHECK(Mentions(log, "asking 'KokiriForest_Meet_DekuTree_MeetDekuTree_01'"));

    // The game takes it: bit 0 of x0, and the component's phase is logged.
    g.Set<std::uint32_t>(kComponent + OrmStoryEvents::kComponentPhase, 1);
    CHECK_FALSE(r.Step(g, true, 0x101));
    CHECK(r.LastAnswer() == true);
    log = r.TakeLog();
    CHECK(Mentions(log, "the game took 'KokiriForest_Meet_DekuTree_MeetDekuTree_01'"));
    CHECK(Mentions(log, "component phase 1"));
    CHECK_FALSE(r.Step(g, false, 0));  // done: nothing more

    // A second request reuses the block (no malloc) and a refusal is logged.
    REQUIRE(r.Ask(kMain, kActor, "Dm_Kokiri_0070"));
    c = r.Step(g, false, 0);
    REQUIRE(c);
    CHECK(c->function == kMain + OrmStoryEvents::kRequestEvent);
    CHECK(GuestString(g, kBlock + OrmStoryEvents::kBlockName) == "Dm_Kokiri_0070");
    CHECK_FALSE(r.Step(g, true, 0x100));  // bit 0 clear: refused
    CHECK(r.LastAnswer() == false);
    CHECK(Mentions(r.TakeLog(), "the game refused 'Dm_Kokiri_0070'"));
}

TEST_CASE("story: no call when Link or his component cannot be read, nor for a bad name") {
    Requester r;
    CHECK_FALSE(r.Ask(kMain, kActor, "bad name"));
    CHECK_FALSE(r.Ask(0, kActor, "Dm_Kokiri_0070"));
    CHECK_FALSE(r.Ask(kMain, 0, "Dm_Kokiri_0070"));
    CHECK(r.TakeLog().size() == 3);

    SUBCASE("Link's actor gone") {
        FakeGuest g = MakeGame();
        g.Set<std::uint32_t>(kActor + OrmStoryEvents::kActorState, 8);
        REQUIRE(r.Ask(kMain, kActor, "Dm_Kokiri_0070"));
        REQUIRE(r.Step(g, false, 0));               // the malloc
        CHECK_FALSE(r.Step(g, true, kBlock));       // no request
        CHECK(Mentions(r.TakeLog(), "Link's actor is gone"));
        CHECK_FALSE(r.Step(g, false, 0));           // and not asked again
    }
    SUBCASE("no event component") {
        FakeGuest g = MakeGame();
        g.Set<std::uint64_t>(kTable + OrmStoryEvents::kComponentEvent, 0);
        REQUIRE(r.Ask(kMain, kActor, "Dm_Kokiri_0070"));
        REQUIRE(r.Step(g, false, 0));
        CHECK_FALSE(r.Step(g, true, kBlock));
        CHECK(Mentions(r.TakeLog(), "event component is not readable"));
    }
    SUBCASE("the malloc gives nothing: never asked again") {
        FakeGuest g = MakeGame();
        REQUIRE(r.Ask(kMain, kActor, "Dm_Kokiri_0070"));
        REQUIRE(r.Step(g, false, 0));
        CHECK_FALSE(r.Step(g, true, 0));
        CHECK(Mentions(r.TakeLog(), "malloc gave nothing"));
        REQUIRE(r.Ask(kMain, kActor, "Dm_Kokiri_0070"));
        CHECK_FALSE(r.Step(g, false, 0));
        CHECK(Mentions(r.TakeLog(), "the malloc failed before"));
    }
}

TEST_CASE("story: a request not made yet is replaced by the next") {
    FakeGuest g = MakeGame();
    Requester r;
    REQUIRE(r.Ask(kMain, kActor, "First_Event"));
    REQUIRE(r.Ask(kMain, kActor, "Second_Event"));
    CHECK(Mentions(r.TakeLog(), "'First_Event' was not asked yet; replaced"));
    REQUIRE(r.Step(g, false, 0));
    REQUIRE(r.Step(g, true, kBlock));
    CHECK(GuestString(g, kBlock + OrmStoryEvents::kBlockName) == "Second_Event");
}

TEST_CASE("orm story events: LICHII_ORM_STORY_EVENTS, or the old LICHII_COOP_STORY=1 of a pre-test9 kit") {
    using OrmStoryEvents::SwitchOn;
    const std::optional<std::string> none;
    CHECK_FALSE(SwitchOn(none, none));                 // upstream's story flags (on by default) may be on; our starts are not
    CHECK(SwitchOn(std::string("1"), none));
    CHECK_FALSE(SwitchOn(std::string("0"), none));
    CHECK(SwitchOn(none, std::string("1")));           // an old kit's coop-options.ini
    CHECK_FALSE(SwitchOn(none, std::string("0")));     // upstream's "story off" also turns ours off
    CHECK_FALSE(SwitchOn(std::string("0"), std::string("1")));  // the new name wins
    CHECK(SwitchOn(std::string("1"), std::string("0")));
}

// ---- The game's force skip (ZMP D-048) ----------------------------------------------

namespace {

constexpr std::uint64_t kManager = 0x90040000;
constexpr std::uint64_t kSession = 0x90050000;
constexpr std::uint64_t kEvents = 0x90060000;
constexpr std::uint64_t kEvent = 0x90070000;

// An event running: the manager's session with two events, the second current.
FakeGuest MakeRunning() {
    FakeGuest g;
    g.Map(kMain + OrmStoryEvents::kEventManagerPtr, 8);
    g.Map(kManager, 0x1000);
    g.Map(kSession, 0x1000);
    g.Map(kEvents, 0x1000);
    g.Set<std::uint64_t>(kMain + OrmStoryEvents::kEventManagerPtr, kManager);
    g.Set<std::uint64_t>(kManager + OrmStoryEvents::kEventSession, kSession);
    g.Set<std::uint64_t>(kSession + OrmStoryEvents::kSessionEvents, kEvents);
    g.Set<std::uint32_t>(kSession + OrmStoryEvents::kSessionCount, 2);
    g.Set<std::uint32_t>(kSession + OrmStoryEvents::kSessionCurrent, 1);
    g.Set<std::uint64_t>(kEvents + 0, 0x90080000);
    g.Set<std::uint64_t>(kEvents + 8, kEvent);
    return g;
}

} // namespace

TEST_CASE("story skip: main+0x3b3af30(the current event), once a want, as the boss rooms do") {
    FakeGuest g = MakeRunning();
    OrmStoryEvents::Skipper s;
    CHECK_FALSE(s.Step(g, false));  // nothing wanted
    REQUIRE(s.Want(kMain, "the owner's copy is over"));
    CHECK_FALSE(s.Step(g, true));  // a call's answer: nothing
    auto c = s.Step(g, false);
    REQUIRE(c);
    CHECK(c->function == kMain + OrmStoryEvents::kForceSkip);
    CHECK(c->x[0] == kEvent);  // the current one, not the first
    CHECK(Mentions(s.TakeLog(), "force skip of the event 0x90070000 (the owner's copy is over)"));
    CHECK_FALSE(s.Step(g, true));
    CHECK_FALSE(s.Step(g, false));  // one skip a want
    // The offsets are the boss rooms' (coop_boss_game.cpp) and docs/dungeons-boss.md's.
    CHECK(OrmStoryEvents::kForceSkip == 0x3b3af30);
    CHECK(OrmStoryEvents::kEventManagerPtr == 0x4e94840);
    CHECK(OrmStoryEvents::kSessionEvents == 0xeb8);
    CHECK(OrmStoryEvents::kSessionCount == 0xeb0);
    CHECK(OrmStoryEvents::kSessionCurrent == 0xf18);
}

TEST_CASE("story skip: no call with no game, no event running, or a current index out of range") {
    OrmStoryEvents::Skipper s;
    CHECK_FALSE(s.Want(0, "x"));
    CHECK(Mentions(s.TakeLog(), "no game"));

    SUBCASE("no event runs (the session pointer is 0)") {
        FakeGuest g = MakeRunning();
        g.Set<std::uint64_t>(kManager + OrmStoryEvents::kEventSession, 0);
        REQUIRE(s.Want(kMain, "x"));
        CHECK_FALSE(s.Step(g, false));
        CHECK(Mentions(s.TakeLog(), "no event to skip (x): none runs"));
        CHECK_FALSE(s.Step(g, false));  // not tried again
    }
    SUBCASE("the current index is past the count") {
        FakeGuest g = MakeRunning();
        g.Set<std::uint32_t>(kSession + OrmStoryEvents::kSessionCurrent, 2);
        REQUIRE(s.Want(kMain, "x"));
        CHECK_FALSE(s.Step(g, false));
        CHECK(Mentions(s.TakeLog(), "the current event is not readable"));
    }
    SUBCASE("the event slot is empty") {
        FakeGuest g = MakeRunning();
        g.Set<std::uint64_t>(kEvents + 8, 0);
        REQUIRE(s.Want(kMain, "x"));
        CHECK_FALSE(s.Step(g, false));
        CHECK(Mentions(s.TakeLog(), "the current event is not readable"));
    }
    SUBCASE("no manager") {
        FakeGuest g;
        REQUIRE(s.Want(kMain, "x"));
        CHECK_FALSE(s.Step(g, false));
        CHECK(Mentions(s.TakeLog(), "none runs"));
    }
}
