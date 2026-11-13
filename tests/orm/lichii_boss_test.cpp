// ORM cases moved out of upstream's tests/lichii/boss_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for boss rooms together (lichii/hook/coop_boss.cpp).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "../host/fake_memory.hpp"
#include "coop/host/downed.hpp"
#include "coop_boss.h"

namespace proto = coop::protocol;
using CoopBoss::Actions;
using CoopBoss::Local;
using CoopBoss::Sync;
using CoopBoss::Vec;

namespace {

constexpr std::uint64_t kS = 1'000'000;

const CoopBoss::Room& Forest() {
    return *CoopBoss::RoomOfScene("ForestTemple");
}

Local At(const char* scene, Vec p, bool event = false, std::uint8_t self = 1) {
    Local l;
    l.scene = scene;
    l.position = p;
    l.event = event;
    l.self = self;
    return l;
}

// A place in the Forest Temple outside the boss room (its entrance).
constexpr Vec kForestEntrance{0.0f, 101.5f, 78.4f};

std::vector<proto::BossState> States(const Actions& a) {
    std::vector<proto::BossState> out;
    for (const auto& m : a.send) {
        out.push_back(m.state);
    }
    return out;
}

bool Has(const Actions& a, proto::BossState s) {
    const auto v = States(a);
    return std::find(v.begin(), v.end(), s) != v.end();
}

proto::BossRoom From(std::uint8_t player, const CoopBoss::Room& room, proto::BossState state) {
    return {player, std::string(room.scene), room.index, state};
}

}  // namespace

// ---- ORM: Ganondorf's and Ganon's rooms (oot-remake docs/FINAL.md) ----

TEST_CASE("boss: Ganondorf's room is the tower's top floor, not the floors under it") {
    const auto* g = CoopBoss::RoomOfScene("InsideGanonCastle");
    REQUIRE(g != nullptr);
    CHECK(g->index == 8);
    CHECK(g->boss == "Enemy_DungeonBoss_Ganondorf");
    CHECK(g->start == "StartPos_Defeat_Ganon");
    // StartPos_Defeat_Ganon and Ganondorf's own place (Banc InsideGanonCastle) are in it.
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{0.0f, 171.1f, -11.3f}) == g);
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{0.1f, 174.1f, -24.6f}) == g);
    // The tiles' corners are in it (a fight at the edge is still the fight).
    CHECK(CoopBoss::Inside(*g, Vec{15.1f, 171.0f, -39.6f}));
    CHECK(CoopBoss::Inside(*g, Vec{-14.9f, 171.0f, -9.6f}));
    // The stair room under it (62, y 156.6), the tower's fight rooms (19 to 25), the central
    // room and the trials: none.
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{0.0f, 157.0f, -45.7f}) == nullptr);
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{11.6f, 156.6f, -36.2f}) == nullptr);
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{-1.5f, 150.0f, -24.7f}) == nullptr);
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{0.0f, 110.3f, 91.6f}) == nullptr);  // Entrance
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{0.2f, 103.8f, 5.4f}) == nullptr);
    CHECK(CoopBoss::RoomAt("InsideGanonCastle", Vec{-94.3f, 100.0f, -21.0f}) == nullptr);
}

TEST_CASE("boss: Ganon's room is the field's fire ring, Zelda and the Master Sword in it") {
    const auto* g = CoopBoss::RoomOfScene("FinalBattleField");
    REQUIRE(g != nullptr);
    CHECK(g->index == 9);
    CHECK(g->start == "StartPos_Defeat_Ganon_InBattle");
    // Ganon, the field's entrance, Zelda and the sword (Banc GanonCastle/FinalBattleField).
    CHECK(CoopBoss::Inside(*g, Vec{-1.4f, 100.0f, -1.0f}));
    CHECK(CoopBoss::Inside(*g, Vec{28.2f, 100.0f, -0.8f}));
    CHECK(CoopBoss::Inside(*g, Vec{28.7f, 100.0f, -2.5f}));
    CHECK(CoopBoss::Inside(*g, Vec{35.6f, 101.0f, -0.9f}));
    // The outer fire walls too.
    CHECK(CoopBoss::Inside(*g, Vec{-4.3f, 103.5f, 70.3f}));
    CHECK(CoopBoss::Inside(*g, Vec{47.6f, 101.8f, 19.2f}));
    // Far out on the ruins: not.
    CHECK_FALSE(CoopBoss::Inside(*g, Vec{-300.0f, 100.0f, 0.0f}));
}

TEST_CASE("boss: a late player in the castle is pulled into Ganondorf's room once a mate is past the door") {
    const auto* g = CoopBoss::RoomOfScene("InsideGanonCastle");
    REQUIRE(g != nullptr);
    Sync s;
    // We are two floors down the tower; P2 is in the room and its intro has started.
    const Vec below{-1.5f, 150.0f, -24.7f};
    s.Tick(At("InsideGanonCastle", below), 1 * kS);
    s.Receive(From(2, *g, proto::BossState::in_room));
    s.Receive(From(2, *g, proto::BossState::intro_start));
    const auto a = s.Tick(At("InsideGanonCastle", below), 2 * kS);
    REQUIRE(a.pull.has_value());
    CHECK(a.pull->room == g);
    CHECK(CoopBoss::Inside(*g, a.pull->to));
}

// ---- ORM: a downed player in the boss room (oot-remake docs/ESPECTADOR_Y_JEFES.md B.2, B0) ----

namespace {

// In the Forest Temple's boss room, in the fight (intro seen and over).
Sync* InTheFight(Sync& s, std::uint8_t self = 1) {
    s.Tick(At("ForestTemple", Forest().arrive, false, self), 1 * kS);
    s.Tick(At("ForestTemple", Forest().arrive, true, self), 2 * kS);
    s.Tick(At("ForestTemple", Forest().arrive, false, self), 20 * kS);
    REQUIRE(s.phase() == Sync::Phase::fight);
    return &s;
}

Local Down(Local l, const char* event_name = "", bool game_over = false) {
    l.downed = true;
    l.game_over = game_over;
    l.event_name = event_name;
    return l;
}

Local Named(Local l, const char* event_name) {
    l.event_name = event_name;
    return l;
}

bool Logged(const Actions& a, const std::string& part) {
    return std::any_of(a.log.begin(), a.log.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

} // namespace

TEST_CASE("boss: the game over event by name") {
    CHECK(CoopBoss::IsGameOverEvent("DmF_SY_GameOver"));
    CHECK(CoopBoss::IsGameOverEvent("Delayed_GameOver"));
    CHECK_FALSE(CoopBoss::IsGameOverEvent(""));
    CHECK_FALSE(CoopBoss::IsGameOverEvent("Dm_ForestTemple_BossOut"));
    CHECK_FALSE(CoopBoss::IsGameOverEvent("Dm_ForestTemple_BossIn"));
}

TEST_CASE("boss: a downed player's revive is not the boss's death, the real one still is") {
    Sync s;
    InTheFight(s);
    const Vec in = Forest().arrive;
    // Down (the gate shut, no event), then a mate revives: the game over
    // event's fairy branch runs, its name not read the first frame.
    auto a = s.Tick(Down(At("ForestTemple", in)), 30 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    a = s.Tick(Down(At("ForestTemple", in, true)), 31 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    CHECK(Logged(a, "not the boss's"));
    CHECK(s.phase() == Sync::Phase::fight);
    a = s.Tick(Down(At("ForestTemple", in, true), "DmF_SY_GameOver"), 32 * kS);
    // Still fighting: the heartbeat goes on (since test8 it says intro_end in
    // the fight, issue 148).
    CHECK(Has(a, proto::BossState::intro_end));
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    // Up before the event is over (the state machine waits for its end, this
    // does not count on it): the rest of it is still not the boss's.
    a = s.Tick(At("ForestTemple", in, true), 36 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    a = s.Tick(At("ForestTemple", in, false), 38 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    CHECK(s.phase() == Sync::Phase::fight);
    // The boss dies: the next event is its death, as before.
    a = s.Tick(Named(At("ForestTemple", in, true), "Dm_ForestTemple_BossOut"), 60 * kS);
    CHECK(Has(a, proto::BossState::defeated));
    CHECK(s.phase() == Sync::Phase::outro);
}

TEST_CASE("boss: the game over event by name is never the boss's death, even without the downed state") {
    // No LICHII_COOP_DOWNED: the game's own death and its fairy, or game over.
    Sync s;
    InTheFight(s);
    auto a = s.Tick(Named(At("ForestTemple", Forest().arrive, true), "DmF_SY_GameOver"), 30 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    CHECK(s.phase() == Sync::Phase::fight);
    s.Tick(At("ForestTemple", Forest().arrive, false), 37 * kS);
    a = s.Tick(Named(At("ForestTemple", Forest().arrive, true), "Delayed_GameOver"), 40 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    s.Tick(At("ForestTemple", Forest().arrive, false), 45 * kS);
    // A fairy, no load: the room goes on as it was.
    for (std::uint64_t t = 46; t <= 56; ++t) {
        a = s.Tick(At("ForestTemple", Forest().arrive), t * kS);
        CHECK_FALSE(Has(a, proto::BossState::left));
    }
    CHECK(s.phase() == Sync::Phase::fight);
    // A later load (no game over in between) changes nothing either.
    s.Tick(Local{}, 57 * kS);
    a = s.Tick(At("ForestTemple", Forest().arrive), 60 * kS);
    CHECK_FALSE(Has(a, proto::BossState::left));
    CHECK(s.phase() == Sync::Phase::fight);
}

TEST_CASE("boss: an event while down is not the intro, and the force skip never hits it") {
    Sync s;
    s.Tick(At("ForestTemple", Forest().arrive, false, 2), 1 * kS);
    REQUIRE(s.phase() == Sync::Phase::waiting);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    auto a = s.Tick(Down(At("ForestTemple", Forest().arrive, true, 2), "DmF_SY_GameOver"), 2 * kS);
    CHECK_FALSE(Has(a, proto::BossState::intro_start));
    CHECK_FALSE(a.skip_intro);
    CHECK(s.phase() == Sync::Phase::waiting);
    s.Tick(At("ForestTemple", Forest().arrive, false, 2), 8 * kS);
    // Up, in the room: the next event is the intro again (since test10, issue 148, intro_start
    // goes out only once past the door: shut in, a long enough intro, or moved in).
    a = s.Tick(At("ForestTemple", Forest().arrive, true, 2), 9 * kS);
    CHECK(Logged(a, "intro started"));
    CHECK(s.phase() == Sync::Phase::intro);
}

TEST_CASE("boss: a downed player outside the room is not pulled in, once up he is") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));  // past the door (issue 148)
    auto a = s.Tick(Down(At("ForestTemple", kForestEntrance, false, 2)), 1 * kS);
    CHECK_FALSE(a.pull);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    a = s.Tick(Down(At("ForestTemple", kForestEntrance, true, 2), "DmF_SY_GameOver"), 2 * kS);
    CHECK_FALSE(a.pull);  // no scene change asked during the revive either
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    a = s.Tick(At("ForestTemple", kForestEntrance, true, 2), 3 * kS);  // up, the event's tail
    CHECK_FALSE(a.pull);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    a = s.Tick(At("ForestTemple", kForestEntrance, false, 2), 4 * kS);
    REQUIRE(a.pull);
    CHECK(a.pull->room == &Forest());
    CHECK_FALSE(a.pull->by_event);
}

TEST_CASE("boss: after a game over and its reload the room starts over") {
    Sync s;
    InTheFight(s);
    const Vec in = Forest().arrive;
    // Group defeat: down, then the game over runs (its event), Continue.
    s.Tick(Down(At("ForestTemple", in)), 30 * kS);
    auto a = s.Tick(Down(At("ForestTemple", in, true), "DmF_SY_GameOver", true), 32 * kS);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    CHECK(Logged(a, "game over"));
    s.Tick(Down(At("ForestTemple", in, false), "", true), 38 * kS);
    // The reload: nothing readable for a while.
    s.Tick(Down(Local{}, "", true), 39 * kS);
    s.Tick(Down(Local{}, "", true), 45 * kS);
    // Back in the same scene, in the room, with the load's own wait event
    // running (and the state machine still in game over for a frame).
    a = s.Tick(Down(At("ForestTemple", in, true), "", true), 50 * kS);
    CHECK(Logged(a, "starts over"));
    CHECK(States(a) == std::vector{proto::BossState::left, proto::BossState::in_room});
    CHECK(s.phase() == Sync::Phase::waiting);
    a = s.Tick(At("ForestTemple", in, false), 51 * kS);  // the wait ends: not the intro
    CHECK_FALSE(Has(a, proto::BossState::intro_end));
    CHECK(s.phase() == Sync::Phase::waiting);
    // A fresh fight: the intro is not played again, 10 s, then the fight
    // (since test8 a fight with no intro moves nobody in: no intro_end, issue 148).
    a = s.Tick(At("ForestTemple", in), 60 * kS);
    CHECK(Logged(a, "no intro"));
    CHECK_FALSE(Has(a, proto::BossState::intro_end));
    CHECK(s.phase() == Sync::Phase::fight);
    a = s.Tick(Named(At("ForestTemple", in, true), "Dm_ForestTemple_BossOut"), 90 * kS);
    CHECK(Has(a, proto::BossState::defeated));
    // The state machine's game_over that lasted into the first frame after the
    // load did not arm another restart: a later load changes nothing.
    s.Tick(Local{}, 95 * kS);
    a = s.Tick(At("ForestTemple", in), 96 * kS);
    CHECK_FALSE(Has(a, proto::BossState::left));
    CHECK(s.phase() == Sync::Phase::done);
}

TEST_CASE("boss: after the reload our own defeat is forgotten, one heard from another game is kept") {
    SUBCASE("ours (here the old fault: an event before its name was read, taken for the death)") {
        Sync s;
        InTheFight(s);
        auto a = s.Tick(At("ForestTemple", Forest().arrive, true), 30 * kS);
        REQUIRE(Has(a, proto::BossState::defeated));
        s.Tick(Named(At("ForestTemple", Forest().arrive, true), "DmF_SY_GameOver"), 31 * kS);
        s.Tick(Local{}, 40 * kS);
        a = s.Tick(At("ForestTemple", Forest().arrive), 50 * kS);
        CHECK(Has(a, proto::BossState::in_room));
        CHECK(s.phase() == Sync::Phase::waiting);
    }
    SUBCASE("another game's: the room stays beaten after the reload") {
        Sync s;
        InTheFight(s);
        s.Receive(From(3, Forest(), proto::BossState::defeated));
        s.Tick(At("ForestTemple", Forest().arrive), 30 * kS);
        REQUIRE(s.phase() == Sync::Phase::done);
        s.Tick(Down(At("ForestTemple", Forest().arrive, true), "DmF_SY_GameOver", true), 31 * kS);
        s.Tick(Local{}, 40 * kS);
        auto a = s.Tick(At("ForestTemple", Forest().arrive), 50 * kS);
        CHECK(Has(a, proto::BossState::left));
        CHECK_FALSE(Has(a, proto::BossState::in_room));
        CHECK(s.phase() == Sync::Phase::done);
    }
}

TEST_CASE("boss: after the reload at the dungeon's entrance, a mate in the room pulls us again") {
    Sync s;
    s.Tick(At("ForestTemple", kForestEntrance, false, 2), 1 * kS);
    // Five pulls used up in the last fight (the mate past the door: issue 148).
    for (std::uint64_t t = 2; t <= 30; ++t) {
        s.Receive(From(1, Forest(), proto::BossState::intro_end));
        s.Tick(At("ForestTemple", kForestEntrance, false, 2), t * kS);
    }
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    CHECK_FALSE(s.Tick(At("ForestTemple", kForestEntrance, false, 2), 31 * kS).pull);
    // Our own game over (offline too long), Continue, the entrance.
    s.Tick(Down(At("ForestTemple", kForestEntrance, true, 2), "DmF_SY_GameOver", true), 32 * kS);
    s.Tick(Local{}, 35 * kS);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    auto a = s.Tick(At("ForestTemple", kForestEntrance, false, 2), 45 * kS);
    CHECK(a.pull);
}

TEST_CASE("boss: a solo fairy through the state machine's game_over, no load: nothing starts over") {
    // Alone in the room the state machine goes alive -> game_over -> alive
    // with the game's own fairy, and no reload.
    Sync s;
    InTheFight(s);
    s.Tick(Down(At("ForestTemple", Forest().arrive, false), "", true), 30 * kS);
    s.Tick(Down(At("ForestTemple", Forest().arrive, true), "DmF_SY_GameOver", true), 31 * kS);
    s.Tick(At("ForestTemple", Forest().arrive, false), 38 * kS);
    s.Tick(At("ForestTemple", Forest().arrive, false), 44 * kS);  // up and calm 6 s
    s.Tick(Local{}, 45 * kS);                                      // a menu
    auto a = s.Tick(At("ForestTemple", Forest().arrive), 46 * kS);
    CHECK_FALSE(Has(a, proto::BossState::left));
    CHECK(s.phase() == Sync::Phase::fight);
}

// The whole local chain over fake memory: Link's life and death read from
// memory (coop::host::read_vitals), the downed state machine, and the boss
// room's sync, in the order the hook calls them each frame.
namespace {

using coop::host::test::FakeMemory;

constexpr std::uint64_t kActor = 0x90000000, kTable = 0x90010000, kLife = 0x90020000, kLifeWord = 0x90030000,
                        kMaxWord = 0x90040000, kDeath = 0x90050000;
constexpr std::uint32_t kAliveWord = 0x43a0, kDeadWord = 0x43af;

void PutLink(FakeMemory& m, std::uint32_t life, std::uint32_t death) {
    m.put_u64(kActor + 0x270, kTable);
    m.put_u64(kTable + 0x2b8, kLife);
    m.put_u64(kTable + 0x1c0, kDeath);
    m.put_u64(kLife + 0x1c10, kLifeWord);
    m.put_u64(kLife + 0x1c18, kMaxWord);
    m.put_u32(kLifeWord + 8, life);
    m.put_u32(kMaxWord + 8, 12);
    m.put_u32(kDeath + 0x20, death);
}

struct Game {
    FakeMemory memory;
    coop::host::Downed downed;
    Sync boss;

    // One frame: Link's state from memory (or nothing: a load), the event.
    Actions Frame(std::uint64_t now_us, bool readable, bool event, const char* name = "",
                  bool fairy_in_a = false) {
        coop::host::DownedInput in;
        in.now_us = now_us;
        if (readable) {
            in.vitals = coop::host::read_vitals(memory, kActor);
        }
        in.event_running = readable ? std::optional<bool>(event) : std::nullopt;
        for (auto& b : in.bottles) {
            b = coop::host::Bottle{coop::host::bottle_value("Empty"), false, 0};
        }
        if (fairy_in_a) {
            in.bottles[0] = coop::host::Bottle{coop::host::bottle_value("Fairy"), true, 1};
        }
        downed.tick(in);
        Local l;
        if (readable) {
            l = At("ForestTemple", Forest().arrive, event);
        }
        l.event_name = name;
        l.downed = downed.state() != coop::host::DownedState::alive;
        l.game_over = downed.state() == coop::host::DownedState::game_over;
        return boss.Tick(l, now_us);
    }
};

} // namespace

TEST_CASE("boss: fake memory, a mate revives us in the fight: no defeat, the real death still is") {
    Game g;
    PutLink(g.memory, 12, kAliveWord);
    g.Frame(1 * kS, true, false);
    g.Frame(2 * kS, true, true, "Dm_ForestTemple_BossIn");
    g.Frame(20 * kS, true, false);
    REQUIRE(g.boss.phase() == Sync::Phase::fight);
    // Killed: down, the gate shut.
    PutLink(g.memory, 0, kDeadWord);
    auto a = g.Frame(30 * kS, true, false);
    REQUIRE(g.downed.state() == coop::host::DownedState::downed);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    // P2 holds A beside us: the fairy goes in, the gate opens, the game over
    // event's fairy branch runs (its name not read the first frame).
    g.downed.request_revive(2);
    g.Frame(33 * kS, true, false);
    REQUIRE(g.downed.state() == coop::host::DownedState::reviving);
    g.Frame(33 * kS + 16'000, true, false, "", true);
    a = g.Frame(33 * kS + 100'000, true, true, "", true);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    a = g.Frame(34 * kS, true, true, "DmF_SY_GameOver", true);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    PutLink(g.memory, 12, kAliveWord);
    a = g.Frame(39 * kS, true, true, "DmF_SY_GameOver");
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    a = g.Frame(40 * kS, true, false);
    REQUIRE(g.downed.state() == coop::host::DownedState::alive);
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    CHECK(g.boss.phase() == Sync::Phase::fight);
    // The boss dies.
    a = g.Frame(70 * kS, true, true, "Dm_ForestTemple_BossOut");
    CHECK(Has(a, proto::BossState::defeated));
}

TEST_CASE("boss: fake memory, group defeat in the fight, Continue: the room starts over") {
    Game g;
    PutLink(g.memory, 12, kAliveWord);
    g.Frame(1 * kS, true, false);
    g.Frame(11 * kS, true, false);  // no intro: the fight
    REQUIRE(g.boss.phase() == Sync::Phase::fight);
    PutLink(g.memory, 0, kDeadWord);
    g.Frame(20 * kS, true, false);
    REQUIRE(g.downed.group_defeat(2));
    g.Frame(22 * kS, true, false);
    REQUIRE(g.downed.state() == coop::host::DownedState::game_over);
    auto a = g.Frame(23 * kS, true, true, "DmF_SY_GameOver");
    CHECK_FALSE(Has(a, proto::BossState::defeated));
    // Continue: the load (nothing readable), then Link up in the room.
    g.Frame(30 * kS, false, false);
    g.Frame(36 * kS, false, false);
    PutLink(g.memory, 12, kAliveWord);
    a = g.Frame(40 * kS, true, false);
    CHECK(g.downed.state() == coop::host::DownedState::alive);
    CHECK(States(a) == std::vector{proto::BossState::left, proto::BossState::in_room});
    CHECK(g.boss.phase() == Sync::Phase::waiting);
    a = g.Frame(50 * kS, true, false);
    CHECK(Logged(a, "no intro"));  // test8: the fight starts here, nobody is moved in (issue 148)
    CHECK(g.boss.phase() == Sync::Phase::fight);
}
