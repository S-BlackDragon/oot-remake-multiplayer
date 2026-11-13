// Tests for the escape going with the group (lichii/hook/coop_escape.cpp): the hook glue's
// decision inputs over coop/host/group_exit (oot-64 D-165, ORM D-R17), and the trip it drives
// (lichii/hook/coop_teleport.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "../host/fake_memory.hpp"
#include "coop_escape.h"
#include "coop_escape_timer.h"

namespace proto = coop::protocol;
using coop::host::DownedState;
using CoopEscape::Actions;
using CoopEscape::AnswerTo;
using CoopEscape::Escape;
using CoopEscape::Input;
using CoopEscape::Mate;
using CoopTeleport::Answer;
using CoopTeleport::Grounded;
using CoopTeleport::Local;
using CoopTeleport::Trip;
using CoopTeleport::Vec;
using Phase = Trip::Phase;

namespace {

constexpr std::uint64_t kS = 1'000'000;

Input In(std::uint64_t now_s, const char* ours, std::vector<Mate> mates, Phase trip = Phase::idle,
         std::uint8_t trip_player = 0) {
    Input in;
    in.now_us = now_s * kS;
    in.scene = ours;
    in.mates = std::move(mates);
    in.trip = trip;
    in.trip_player = trip_player;
    return in;
}

Mate At(std::uint8_t n, const char* scene, bool up = true) {
    return Mate{n, std::string(scene), up};
}

bool Has(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines) {
        if (l.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

Local Standing(const char* scene) {
    Local l;
    l.scene = scene;
    l.position = Vec{1, 2, 3};
    l.age = proto::Age::adult;
    return l;
}

} // namespace

TEST_CASE("escape: LICHII_ORM_GROUP_EXIT off words turn it off, anything else keeps it on") {
    CHECK(CoopEscape::SettingsFrom(nullptr).on);
    CHECK(CoopEscape::SettingsFrom("1").on);
    CHECK_FALSE(CoopEscape::SettingsFrom("0").on);
    CHECK_FALSE(CoopEscape::SettingsFrom("off").on);  // coop_switch.h's words
    CHECK(CoopEscape::SettingsFrom("maybe").on);      // unknown: the default
    CHECK_FALSE(CoopEscape::SettingsFrom(false).on);
    Escape off(CoopEscape::SettingsFrom("0"));
    CHECK_FALSE(off.On());
    off.Frame(In(0, "CrumblingGanonCastle", {At(2, "FinalBattleField")}));
    const Actions a = off.Frame(In(5, "CrumblingGanonCastle", {At(2, "FinalBattleField")}));
    CHECK_FALSE(a.follow);
    CHECK(a.log.empty());
    CHECK_FALSE(off.ExitEnd(std::string("FinalBattleField")));
}

TEST_CASE("escape: a game still in the collapse follows the mate at Ganon, logged as escape") {
    Escape e;
    CHECK_FALSE(e.Frame(In(0, "CrumblingGanonCastle", {At(2, "FinalBattleField")})).follow);
    const Actions a = e.Frame(In(2, "CrumblingGanonCastle", {At(2, "FinalBattleField")}));
    REQUIRE(a.follow);
    CHECK(*a.follow == 2);
    REQUIRE(a.log.size() == 1);
    CHECK(a.log[0] ==
          "escape: P2 is in FinalBattleField, we are still in CrumblingGanonCastle: following (first ask)");
    // The trip runs: not asked again over it, even past the rule's retry.
    CHECK_FALSE(e.Frame(In(20, "CrumblingGanonCastle", {At(2, "FinalBattleField")}, Phase::travelling, 2)).follow);
    // The trip gave up (the game took no scene change): asked again.
    const Actions again = e.Frame(In(32, "CrumblingGanonCastle", {At(2, "FinalBattleField")}));
    REQUIRE(again.follow);
    CHECK(again.log[0].ends_with("(asked again)"));
}

TEST_CASE("escape: a downed Link is not moved, and a trip stops if he goes down on it (D-R17)") {
    Escape e;
    Input in = In(0, "CrumblingGanonCastle", {At(2, "FinalBattleField")});
    in.downed = DownedState::downed;
    e.Frame(in);
    in.now_us = 3 * kS;
    Actions a = e.Frame(in);
    CHECK_FALSE(a.follow);
    CHECK(Has(a.log, "escape: P2 is in FinalBattleField: we follow when our Link is up"));
    // A game over runs its course: still no move.
    in.downed = DownedState::game_over;
    in.now_us = 4 * kS;
    CHECK_FALSE(e.Frame(in).follow);
    // Up again: follows.
    in.downed = DownedState::alive;
    in.now_us = 5 * kS;
    a = e.Frame(in);
    REQUIRE(a.follow);
    // On the way, he goes down: the trip stops.
    Input down = In(6, "CrumblingGanonCastle", {At(2, "FinalBattleField")}, Phase::travelling, 2);
    down.downed = DownedState::downed;
    a = e.Frame(down);
    CHECK(a.stop);
    CHECK_FALSE(a.follow);
    CHECK(Has(a.log, "escape: our Link is down (downed): the trip to P2 stops"));
    // Not said twice once the trip is idle.
    down.trip = Phase::idle;
    down.now_us = 7 * kS;
    CHECK_FALSE(e.Frame(down).stop);
}

TEST_CASE("escape: a downed mate does not lead, nobody leads from outside the collapse") {
    Escape e;
    e.Frame(In(0, "CrumblingGanonCastle", {At(2, "FinalBattleField", false)}));
    CHECK_FALSE(e.Frame(In(5, "CrumblingGanonCastle", {At(2, "FinalBattleField", false)})).follow);
    Escape trials;
    trials.Frame(In(0, "InsideGanonCastle", {At(2, "FinalBattleField")}));
    CHECK_FALSE(trials.Frame(In(5, "InsideGanonCastle", {At(2, "FinalBattleField")})).follow);
}

TEST_CASE("escape: an event in our game waits, a /teleport of the player's own is not replaced") {
    Escape e;
    Input ev = In(0, "CrumblingGanonCastle", {At(3, "FinalBattleField")});
    ev.event = true;
    e.Frame(ev);
    ev.now_us = 2 * kS;
    Actions a = e.Frame(ev);
    CHECK_FALSE(a.follow);
    CHECK(Has(a.log, "we follow when our event ends"));

    Escape own;
    own.Frame(In(0, "CrumblingGanonCastle", {At(3, "FinalBattleField")}, Phase::asking, 4));
    a = own.Frame(In(2, "CrumblingGanonCastle", {At(3, "FinalBattleField")}, Phase::asking, 4));
    CHECK_FALSE(a.follow);
    CHECK(Has(a.log, "escape: a /teleport to P4 runs; the escape does not replace it"));
    // The player's trip ends with our Link still in the collapse: the escape asks after the retry.
    CHECK(own.Frame(In(13, "CrumblingGanonCastle", {At(3, "FinalBattleField")})).follow);
}

TEST_CASE("escape: a mate seen in the collapse lately is an escaper, for a while") {
    Escape e;
    e.Frame(In(0, "FinalBattleField", {At(2, "CrumblingGanonCastle"), At(3, "InsideGanonCastle")}));
    CHECK(e.Escaping(2, 0));
    CHECK_FALSE(e.Escaping(3, 0));
    CHECK_FALSE(e.Escaping(0, 0));
    CHECK_FALSE(e.Escaping(9, 0));
    // P2 lands here (the trip's second ask): still an escaper.
    e.Frame(In(30, "FinalBattleField", {At(2, "FinalBattleField")}));
    CHECK(e.Escaping(2, 30 * kS));
    CHECK_FALSE(e.Escaping(2, Escape::kEscaperUs + 1));
    CHECK(e.ExitEnd(std::string("FinalBattleField")));
    CHECK_FALSE(e.ExitEnd(std::string("CrumblingGanonCastle")));
    CHECK_FALSE(e.ExitEnd(std::nullopt));
    e.Reset();
    CHECK_FALSE(e.Escaping(2, 30 * kS));
}

TEST_CASE("escape: our boss fight (or /teleport off) does not refuse an escaper at Ganon") {
    Grounded g;
    Local l = Standing("FinalBattleField");
    g.Add(l, 0);
    g.Add(l, 400'000);
    Local fighting = l;
    fighting.fighting = true;
    const Answer normal = CoopTeleport::AnswerFor(fighting, g);
    REQUIRE_FALSE(normal.accept);
    REQUIRE(normal.reason == proto::JoinRefuseReason::cannot_warp);
    const Answer group = CoopTeleport::AnswerFor(l, g);
    REQUIRE(group.accept);

    Answer a = AnswerTo(normal, group, true, true);
    CHECK(a.accept);
    CHECK(a.target.scene == "FinalBattleField");
    // Not an escaper, or we are not where the escape goes: the fight refuses.
    CHECK_FALSE(AnswerTo(normal, group, false, true).accept);
    CHECK_FALSE(AnswerTo(normal, group, true, false).accept);
    // /teleport off here: an escaper still comes.
    Answer declined;
    declined.reason = proto::JoinRefuseReason::declined;
    CHECK(AnswerTo(declined, group, true, true).accept);
    // Busy (a load, an event, in the air) stays busy.
    Answer busy;
    busy.reason = proto::JoinRefuseReason::busy;
    CHECK_FALSE(AnswerTo(busy, group, true, true).accept);
    // No group answer (not grounded yet): refused as before.
    CHECK_FALSE(AnswerTo(normal, Answer{}, true, true).accept);
}

TEST_CASE("escape: the trip it starts goes to the mate's scene, and a stop clears the scene change") {
    Trip trip;
    const Local here = Standing("CrumblingGanonCastle");
    auto a = trip.Start(2, "P2", here, 0);
    REQUIRE(a.ask);
    proto::JoinTarget t;
    t.scene = "FinalBattleField";
    t.position = {5, 0, 5};
    t.rotation = {0, 0, 0, 1};
    a = trip.Accepted(2, t, here, kS);
    REQUIRE(a.scene);
    CHECK(*a.scene == "FinalBattleField");
    CHECK(trip.phase() == Phase::travelling);
    a = trip.Stop("escape: you are down");
    CHECK(a.cancel_scene);
    CHECK(Has(a.lines, "escape: you are down"));
    CHECK(trip.phase() == Phase::idle);
    // Idle: nothing to stop.
    a = trip.Stop("again");
    CHECK_FALSE(a.cancel_scene);
    CHECK(a.lines.empty());
    // Asking (nothing asked of the game yet): stops without a scene to clear.
    trip.Start(2, "P2", here, 2 * kS);
    a = trip.Stop("stop");
    CHECK_FALSE(a.cancel_scene);
    CHECK(trip.phase() == Phase::idle);
}

// ---- The escape's countdown looked for in our game (coop_escape_timer.h) -------------------

namespace {

constexpr std::uint64_t kUiPtr = 0x4e7cfa8;  // addresses.hpp kGameUiModulePtr
constexpr std::uint64_t kUi = 0x80000000;

coop::host::test::FakeMemory TimerGame() {
    coop::host::test::FakeMemory m;
    m.put_u64(coop::host::test::FakeMemory::kBase + kUiPtr, kUi);
    for (std::uint64_t a = 0; a < 0x2000; a += 8) {
        m.put_u64(kUi + a, 0);
    }
    return m;
}

} // namespace

TEST_CASE("escape timer: which path runs is said once at start") {
    using CoopEscapeTimer::Driver;
    using CoopEscapeTimer::Settings;
    Settings s;
    CHECK(Driver(s).Begin().empty());  // the clock off: nothing, no search
    CHECK_FALSE(Driver(s).On());
    s.clock = true;
    CHECK(Driver(s).On());
    CHECK(Has(Driver(s).Begin(), "coop: escape clock: search on: our countdown is looked for in CrumblingGanonCastle"));
    s.length_s = "240";
    CHECK(Has(Driver(s).Begin(), "up to 240 s"));
    s.search = "0";
    CHECK_FALSE(Driver(s).On());
    CHECK(Has(Driver(s).Begin(), "search off (LICHII_ORM_ESCAPE_CLOCK_SEARCH=0)"));
    s.search = "main+zz";
    CHECK(Driver(s).On());
    CHECK(Has(Driver(s).Begin(), "LICHII_ORM_ESCAPE_CLOCK_SEARCH main+zz: "));
    CHECK(Has(Driver(s).Begin(), "the GameUI module only"));
    s.search.reset();
    s.word_given = true;
    CHECK_FALSE(Driver(s).On());
    CHECK(Has(Driver(s).Begin(), "search off: the word is given by hand"));
}

TEST_CASE("escape timer: looked for only in the collapse with no event, found, it is the word") {
    CoopEscapeTimer::Settings s;
    s.clock = true;
    CoopEscapeTimer::Driver d(s);
    auto m = TimerGame();
    const std::optional<std::string> collapse = std::string(CoopEscapeTimer::kScene);
    // Elsewhere, loading, no game: nothing read.
    CHECK(d.Frame(&m, std::string("FinalBattleField"), false, 0).empty());
    CHECK(d.Frame(&m, std::nullopt, false, 0).empty());
    CHECK(d.Frame(nullptr, collapse, false, 0).empty());
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::idle);
    // In the scene before its first event: the countdown does not run yet, nothing read.
    m.put_f32(kUi + 0x1a0, 176.0f);
    CHECK(d.Frame(&m, collapse, false, 0).empty());
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::idle);
    // The collapse's event runs, then ends: armed.
    CHECK(d.Frame(&m, collapse, true, 0).empty());
    std::vector<std::string> lines;
    for (int t = 0; t <= 5; ++t) {
        m.put_f32(kUi + 0x1a0, 176.0f - static_cast<float>(t));
        for (auto& l : d.Frame(&m, collapse, false, static_cast<std::uint64_t>(t) * kS)) {
            lines.push_back(l);
        }
    }
    CHECK(Has(lines, "coop: escape clock: an event of the collapse ended: our countdown should run now"));
    CHECK(Has(lines, "coop: escape clock: looking for our countdown (try 1 of 3)"));
    CHECK(Has(lines, "coop: escape clock: our countdown found at main+0x4e7cfa8,0x1a0:f32"));
    REQUIRE(d.Found());
    CHECK(*d.Found() == "main+0x4e7cfa8,0x1a0:f32");
    // Leaving the scene keeps it (the next escape uses the same word).
    CHECK(d.Frame(&m, std::string("FinalBattleField"), false, 9 * kS).empty());
    CHECK(d.Found());
}

// ORM (docs/SONDAS.md, 2026-11-12): three searches ran out before the remake's countdown showed.
TEST_CASE("escape clock: LICHII_ORM_ESCAPE_CLOCK_TRIES sets how many searches, a bad value keeps 3") {
    CoopEscapeTimer::Settings s;
    s.clock = true;
    s.tries = "20";
    CoopEscapeTimer::Driver d(s);
    CHECK(d.On());
    CHECK(d.Search().tries_used() == 0);
    s.tries = "0";
    CoopEscapeTimer::Driver bad(s);
    bool said = false;
    for (const auto& l : bad.Begin()) {
        said = said || l.find("LICHII_ORM_ESCAPE_CLOCK_TRIES 0: not 1 to 720; 3 tries") != std::string::npos;
    }
    CHECK(said);
}

TEST_CASE("escape timer: a cutscene in the middle starts the samples again, never found: the fallback line") {
    CoopEscapeTimer::Settings s;
    s.clock = true;
    CoopEscapeTimer::Driver d(s);
    auto m = TimerGame();
    const std::optional<std::string> collapse = std::string(CoopEscapeTimer::kScene);
    std::vector<std::string> lines;
    std::uint64_t t = 0;
    auto frame = [&](bool event) {
        for (auto& l : d.Frame(&m, collapse, event, t * kS)) {
            lines.push_back(l);
        }
        ++t;
    };
    m.put_f32(kUi + 0x10, 60.0f);  // reads as seconds left, never drops
    frame(true);   // the collapse's event
    frame(false);  // armed: try 1 starts
    frame(false);
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::searching);
    frame(true);  // a floor's cutscene: the samples start again, no try lost
    CHECK(d.Search().tries_used() == 0);
    for (int i = 0; i < 20; ++i) {
        frame(false);  // nothing ever counts down here
    }
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::failed);
    CHECK_FALSE(d.Found());
    CHECK(Has(lines, "coop: escape clock: our countdown was not found"));
    CHECK(Has(lines, "the group's clock goes on without the game's timer"));
}

TEST_CASE("escape timer: a reload of 2 s or leaving the scene starts afresh, a word found stays") {
    CoopEscapeTimer::Settings s;
    s.clock = true;
    CoopEscapeTimer::Driver d(s);
    auto m = TimerGame();
    const std::optional<std::string> collapse = std::string(CoopEscapeTimer::kScene);
    m.put_f32(kUi + 0x10, 60.0f);  // never drops
    std::uint64_t t = 0;
    (void)d.Frame(&m, collapse, true, t);
    for (int i = 0; i < 12; ++i) {
        t += kS;
        (void)d.Frame(&m, collapse, false, t);
    }
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::failed);
    // A short read gap: still failed (the same visit).
    (void)d.Frame(&m, std::nullopt, false, t + 100'000);
    (void)d.Frame(&m, collapse, false, t + 200'000);
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::failed);
    // A Continue's reload (2 s loading): a new visit; it waits for the event again.
    (void)d.Frame(&m, std::nullopt, false, t + 1 * kS);
    (void)d.Frame(&m, std::nullopt, false, t + 3 * kS + 1);
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::idle);
    CHECK(d.Frame(&m, collapse, false, t + 4 * kS).empty());
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::idle);
    // This time it counts down: found; leaving and coming back keeps it.
    t += 10 * kS;
    (void)d.Frame(&m, collapse, true, t);
    for (int i = 0; i <= 5; ++i) {
        m.put_f32(kUi + 0x10, 150.0f - static_cast<float>(i));
        (void)d.Frame(&m, collapse, false, t + static_cast<std::uint64_t>(i) * kS);
    }
    REQUIRE(d.Found());
    (void)d.Frame(&m, std::string("FinalBattleField"), false, t + 20 * kS);
    (void)d.Frame(&m, std::nullopt, false, t + 21 * kS);
    (void)d.Frame(&m, std::nullopt, false, t + 25 * kS);
    CHECK(d.Found() == std::optional<std::string>("main+0x4e7cfa8,0x10:f32"));
}

// Seen in a game (final-huida-reloj, 2026-11-12, docs/SONDAS.md row 1): the warp's DefaultBootEvent ended before
// the collapse and the search spent its tries on a timer that was not running yet.
TEST_CASE("escape timer: a load event does not arm the search, the collapse's does") {
    CoopEscapeTimer::Settings s;
    s.clock = true;
    CoopEscapeTimer::Driver d(s);
    auto m = TimerGame();
    const std::optional<std::string> collapse = std::string(CoopEscapeTimer::kScene);
    CHECK(CoopEscapeTimer::Driver::IsLoadEvent("DefaultBootEvent"));
    CHECK(CoopEscapeTimer::Driver::IsLoadEvent("DefaultParentEvent"));
    CHECK_FALSE(CoopEscapeTimer::Driver::IsLoadEvent("CrumblingGanonCastle_Arrive"));
    d.Frame(&m, collapse, true, 0, std::string("DefaultBootEvent"));
    CHECK(d.Frame(&m, collapse, false, kS).empty());
    CHECK(d.Search().state() == coop::host::CountdownSearch::State::idle);
    d.Frame(&m, collapse, true, 2 * kS, std::string("CrumblingGanonCastle_Arrive"));
    const auto lines = d.Frame(&m, collapse, false, 3 * kS);
    CHECK(Has(lines, "coop: escape clock: an event of the collapse ended: our countdown should run now"));
}
