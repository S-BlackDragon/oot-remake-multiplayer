// ORM: tests for the boss room rules oot-64 (ZMP) had and the remake's shared
// boss room did not (coop/host/boss_rules.hpp, oot-remake docs/JEFES.md,
// D-R17), and for ZMP's late entry cases on the boss sync as it is
// (lichii/hook/coop_boss.cpp): one joins in the intro, in the fight, after the
// boss's death, after a reconnection, and the blue warp takes only who enters.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../host/fake_memory.hpp"
#include "coop/host/boss_rules.hpp"
#include "coop/host/downed.hpp"
#include "coop/host/final_rules.hpp"
#include "coop_boss.h"
#include "coop_objects_sync.h"

namespace proto = coop::protocol;
using coop::host::BossPull;
using coop::host::BossRespawn;
using coop::host::BossRespawnAction;
using coop::host::BossRespawnInput;
using coop::host::DownedState;
using CoopBoss::Actions;
using CoopBoss::Local;
using CoopBoss::Sync;
using CoopBoss::Vec;

namespace {

constexpr std::uint64_t kS = 1'000'000;
constexpr Vec kForestEntrance{0.0f, 101.5f, 78.4f};  // the Forest Temple's entrance, outside the boss room

const CoopBoss::Room& Forest() {
    return *CoopBoss::RoomOfScene("ForestTemple");
}

Local At(const char* scene, Vec p, bool event = false, std::uint8_t self = 2) {
    Local l;
    l.scene = scene;
    l.position = p;
    l.event = event;
    l.self = self;
    return l;
}

Local Lying(Local l, bool pull_while_down) {
    l.downed = true;
    l.pull_while_down = pull_while_down;
    return l;
}

proto::BossRoom From(std::uint8_t player, const CoopBoss::Room& room, proto::BossState state) {
    return {player, std::string(room.scene), room.index, state};
}

bool Has(const Actions& a, proto::BossState s) {
    return std::any_of(a.send.begin(), a.send.end(), [&](const proto::BossRoom& m) { return m.state == s; });
}

template <typename A>
bool Logged(const A& a, const std::string& part) {
    return std::any_of(a.log.begin(), a.log.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

BossRespawnInput Read(std::uint64_t now_us, const char* scene, std::optional<std::uint8_t> in_room, bool up,
                      bool game_over = false, bool event = false) {
    BossRespawnInput in;
    in.now_us = now_us;
    in.readable = true;
    in.scene = scene;
    in.in_room = in_room;
    in.up = up;
    in.game_over = game_over;
    in.event = event;
    return in;
}

BossRespawnInput Loading(std::uint64_t now_us, bool game_over = false) {
    BossRespawnInput in;
    in.now_us = now_us;
    in.game_over = game_over;
    return in;
}

} // namespace

// What the server relays: our messages with our player number.
static void Relay(const Actions& out, std::uint8_t from, Sync& to) {
    for (auto m : out.send) {
        m.player_number = from;
        to.Receive(proto::Message{m});
    }
}

// ---- Pulled in while down (oot-64 D-117 c) -----------------------------------

TEST_CASE("boss rules: how a Link may be moved into the boss room, by his downed state") {
    CHECK(coop::host::boss_pull(DownedState::alive, true) == BossPull::normal);
    CHECK(coop::host::boss_pull(DownedState::alive, false) == BossPull::normal);
    CHECK(coop::host::boss_pull(DownedState::downed, true) == BossPull::matrix_only);
    CHECK(coop::host::boss_pull(DownedState::downed, false) == BossPull::none);
    CHECK(coop::host::boss_pull(DownedState::reviving, true) == BossPull::none);
    CHECK(coop::host::boss_pull(DownedState::fairy, true) == BossPull::none);  // ORM integration
    CHECK(coop::host::boss_pull(DownedState::game_over, true) == BossPull::none);
    CHECK(std::string(coop::host::to_string(BossPull::matrix_only)) == "matrix only");
}

TEST_CASE("boss rules: a downed Link outside the room is moved in with the group, as he lies") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_start));  // P1 is past the door
    auto a = s.Tick(Lying(At("ForestTemple", kForestEntrance), true), 1 * kS);
    REQUIRE(a.pull);
    CHECK(a.pull->room == &Forest());
    CHECK_FALSE(a.pull->by_event);  // never the scene change: a load would get him up
    CHECK(Logged(a, "down as we lie"));
    // In the room he is still down: no intro of his, the heartbeat says in_room.
    a = s.Tick(Lying(At("ForestTemple", CoopBoss::ArrivalFor(Forest(), 2)), true), 2 * kS);
    CHECK(Has(a, proto::BossState::in_room));
    CHECK_FALSE(Has(a, proto::BossState::intro_start));
    CHECK(s.phase() == Sync::Phase::waiting);
}

TEST_CASE("boss rules: without the switch a downed Link stays where he fell (the sync before)") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    auto a = s.Tick(Lying(At("ForestTemple", kForestEntrance), false), 1 * kS);
    CHECK_FALSE(a.pull);
}

TEST_CASE("boss rules: a Link being revived, or in his game over, is never moved") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    // Reviving: the hook gives no pull_while_down (boss_pull says none), and
    // the revive's event runs.
    Local l = Lying(At("ForestTemple", kForestEntrance, true), false);
    l.event_name = "DmF_SY_GameOver";
    auto a = s.Tick(l, 1 * kS);
    CHECK_FALSE(a.pull);
    // The game over: even with the flag set by mistake, nothing.
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    Local over = Lying(At("ForestTemple", kForestEntrance), true);
    over.game_over = true;
    a = s.Tick(over, 2 * kS);
    CHECK_FALSE(a.pull);
}

TEST_CASE("boss rules: a downed Link's event (the game over's fairy branch) still blocks the move") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    auto a = s.Tick(Lying(At("ForestTemple", kForestEntrance, true), true), 1 * kS);
    CHECK_FALSE(a.pull);
}

// The whole local chain over fake memory: Link dead in memory, the downed
// state machine, boss_pull, the sync; moving him does not touch his life.
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

// One game as the hook runs it each frame: downed state machine, boss sync,
// respawn rule, with the place Link stands (nothing while loading).
struct Game {
    FakeMemory memory;
    coop::host::Downed downed;
    Sync boss;
    BossRespawn respawn;
    bool pull_downed = true;
    std::vector<std::string> log;

    struct Out {
        Actions boss;
        BossRespawnAction respawn;
    };

    Out Frame(std::uint64_t now_us, std::optional<Vec> at, bool event = false, const char* name = "") {
        coop::host::DownedInput in;
        in.now_us = now_us;
        if (at) {
            in.vitals = coop::host::read_vitals(memory, kActor);
        }
        in.event_running = at ? std::optional<bool>(event) : std::nullopt;
        for (auto& b : in.bottles) {
            b = coop::host::Bottle{coop::host::bottle_value("Empty"), false, 0};
        }
        downed.tick(in);
        const DownedState state = downed.state();
        Local l;
        if (at) {
            l = At("ForestTemple", *at, event);
        }
        l.self = 2;
        l.event_name = name;
        l.downed = state != DownedState::alive;
        l.game_over = state == DownedState::game_over;
        l.pull_while_down = coop::host::boss_pull(state, pull_downed) == BossPull::matrix_only;
        Out out;
        out.boss = boss.Tick(l, now_us);
        BossRespawnInput r;
        r.now_us = now_us;
        r.readable = at.has_value();
        if (at) {
            r.scene = "ForestTemple";
            if (const auto* room = CoopBoss::RoomAt("ForestTemple", *at)) r.in_room = room->index;
            r.beaten = boss.Beaten("ForestTemple", Forest().index);
        }
        r.game_over = state == DownedState::game_over || (event && CoopBoss::IsGameOverEvent(name));
        r.up = state == DownedState::alive;
        r.event = event;
        out.respawn = respawn.tick(r);
        log.insert(log.end(), out.boss.log.begin(), out.boss.log.end());
        log.insert(log.end(), out.respawn.log.begin(), out.respawn.log.end());
        return out;
    }
};

} // namespace

TEST_CASE("boss rules: fake memory, a Link killed outside the room is moved in still down, his life still 0") {
    Game g;
    PutLink(g.memory, 12, kAliveWord);
    g.Frame(1 * kS, kForestEntrance);
    PutLink(g.memory, 0, kDeadWord);
    g.Frame(2 * kS, kForestEntrance);
    REQUIRE(g.downed.state() == DownedState::downed);
    g.boss.Receive(From(1, Forest(), proto::BossState::intro_start));
    auto out = g.Frame(3 * kS, kForestEntrance);
    REQUIRE(out.boss.pull);
    CHECK_FALSE(out.boss.pull->by_event);
    // The hook moves him (the matrix): in the room, still down.
    out = g.Frame(4 * kS, CoopBoss::ArrivalFor(Forest(), 2));
    CHECK(g.downed.state() == DownedState::downed);
    const auto v = coop::host::read_vitals(g.memory, kActor);
    CHECK(v.dead());
    CHECK(*v.life == 0);
    CHECK(Has(out.boss, proto::BossState::in_room));
}

// ---- Back into the boss room after a game over in it (oot-64 PLAN 2.8.6) -----

TEST_CASE("boss rules: Continue that leaves us at the dungeon's entrance moves us back into the room") {
    BossRespawn r;
    const std::uint8_t room = Forest().index;
    auto a = r.tick(Read(1 * kS, "ForestTemple", room, false));
    CHECK_FALSE(r.armed());
    a = r.tick(Read(2 * kS, "ForestTemple", room, false, true));  // the game over, in the room
    CHECK(r.armed());
    CHECK(Logged(a, "game over in the boss room of ForestTemple"));
    a = r.tick(Read(5 * kS, "ForestTemple", room, false, true));  // the screen
    CHECK_FALSE(a.move_to);
    a = r.tick(Loading(9 * kS, true));  // Continue: the load
    a = r.tick(Loading(15 * kS));
    // Readable again, at the entrance, still in the game over state a frame.
    a = r.tick(Read(18 * kS, "ForestTemple", std::nullopt, false, true, true));
    CHECK_FALSE(a.move_to);
    a = r.tick(Read(19 * kS, "ForestTemple", std::nullopt, true, false, true));
    REQUIRE(a.move_to);
    CHECK(*a.move_to == room);
    CHECK(a.by_event);  // the load's own wait runs: the scene change can take it
    CHECK(Logged(a, "back into it (try 1)"));
    // Seen in the room: done.
    a = r.tick(Read(20 * kS, "ForestTemple", room, true));
    CHECK_FALSE(a.move_to);
    CHECK(Logged(a, "back in the boss room of ForestTemple"));
    CHECK_FALSE(r.armed());
}

TEST_CASE("boss rules: a move that did not take is tried again every 3 s, three times") {
    BossRespawn r;
    const std::uint8_t room = Forest().index;
    r.tick(Read(1 * kS, "ForestTemple", room, false, true));
    r.tick(Loading(5 * kS));
    auto a = r.tick(Read(10 * kS, "ForestTemple", std::nullopt, true));
    REQUIRE(a.move_to);
    a = r.tick(Read(12 * kS, "ForestTemple", std::nullopt, true));
    CHECK_FALSE(a.move_to);
    a = r.tick(Read(13 * kS, "ForestTemple", std::nullopt, true));
    REQUIRE(a.move_to);
    CHECK(Logged(a, "moved again (try 2)"));
    a = r.tick(Read(16 * kS, "ForestTemple", std::nullopt, true));
    REQUIRE(a.move_to);
    CHECK(Logged(a, "(try 3)"));
    a = r.tick(Read(19 * kS, "ForestTemple", std::nullopt, true));
    CHECK_FALSE(a.move_to);
    CHECK(Logged(a, "after 3 moves: given up"));
    CHECK_FALSE(r.armed());
}

TEST_CASE("boss rules: a move by the scene change waits for its load") {
    BossRespawn r;
    const std::uint8_t room = Forest().index;
    r.tick(Read(1 * kS, "ForestTemple", room, false, true));
    r.tick(Loading(5 * kS));
    REQUIRE(r.tick(Read(10 * kS, "ForestTemple", std::nullopt, true, false, true)).move_to);
    // The scene change reloads the dungeon: 10 s of nothing readable, no new move.
    for (std::uint64_t t = 11; t <= 21; ++t) {
        CHECK_FALSE(r.tick(Loading(t * kS)).move_to);
    }
    auto a = r.tick(Read(22 * kS, "ForestTemple", room, true));
    CHECK(Logged(a, "back in the boss room"));
}

TEST_CASE("boss rules: nothing to do when Continue leaves us in the room, elsewhere, or the boss is beaten") {
    const std::uint8_t room = Forest().index;
    SUBCASE("in the room (the remake's own boss retry)") {
        BossRespawn r;
        r.tick(Read(1 * kS, "ForestTemple", room, false, true));
        r.tick(Loading(5 * kS));
        auto a = r.tick(Read(10 * kS, "ForestTemple", room, true));
        CHECK_FALSE(a.move_to);
        CHECK(Logged(a, "left us in the boss room"));
    }
    SUBCASE("another scene") {
        BossRespawn r;
        r.tick(Read(1 * kS, "ForestTemple", room, false, true));
        r.tick(Loading(5 * kS));
        auto a = r.tick(Read(10 * kS, "SacredForestMeadow", std::nullopt, true));
        CHECK_FALSE(a.move_to);
        CHECK(Logged(a, "not ForestTemple"));
    }
    SUBCASE("the boss beaten") {
        BossRespawn r;
        r.tick(Read(1 * kS, "ForestTemple", room, false, true));
        r.tick(Loading(5 * kS));
        auto in = Read(10 * kS, "ForestTemple", std::nullopt, true);
        in.beaten = true;
        auto a = r.tick(in);
        CHECK_FALSE(a.move_to);
        CHECK(Logged(a, "is beaten"));
    }
    SUBCASE("not up in time") {
        BossRespawn r;
        r.tick(Read(1 * kS, "ForestTemple", room, false, true));
        r.tick(Loading(5 * kS));
        r.tick(Read(10 * kS, "ForestTemple", std::nullopt, false));
        auto a = r.tick(Read(41 * kS, "ForestTemple", std::nullopt, false));
        CHECK_FALSE(a.move_to);
        CHECK(Logged(a, "given up"));
    }
}

TEST_CASE("boss rules: a fairy's revive (no load) or a game over outside a boss room never moves anyone") {
    const std::uint8_t room = Forest().index;
    BossRespawn r;
    r.tick(Read(1 * kS, "ForestTemple", room, false, true));
    auto a = r.tick(Read(7 * kS, "ForestTemple", room, true));  // up, no load: a fairy
    CHECK(Logged(a, "a fairy"));
    CHECK_FALSE(r.armed());
    BossRespawn o;
    o.tick(Read(1 * kS, "ForestTemple", std::nullopt, false, true));
    CHECK_FALSE(o.armed());
    // No load for a minute: forgotten.
    BossRespawn w;
    w.tick(Read(1 * kS, "ForestTemple", room, false, true));
    a = w.tick(Read(62 * kS, "ForestTemple", room, false, true));
    CHECK(Logged(a, "no reload"));
    CHECK_FALSE(w.armed());
}

TEST_CASE("boss rules: fake memory, group defeat in the room, Continue at the entrance: back in, the room anew") {
    Game g;
    PutLink(g.memory, 12, kAliveWord);
    const Vec inside = CoopBoss::ArrivalFor(Forest(), 2);
    g.Frame(1 * kS, inside);
    g.Frame(11 * kS, inside);  // no intro: the fight
    REQUIRE(g.boss.phase() == Sync::Phase::fight);
    PutLink(g.memory, 0, kDeadWord);
    g.Frame(20 * kS, inside);
    REQUIRE(g.downed.state() == DownedState::downed);
    REQUIRE(g.downed.group_defeat(2));
    g.Frame(22 * kS, inside);
    REQUIRE(g.downed.state() == DownedState::game_over);
    CHECK(g.respawn.armed());
    auto out = g.Frame(23 * kS, inside, true, "DmF_SY_GameOver");
    CHECK_FALSE(Has(out.boss, proto::BossState::defeated));
    // Continue: the load, then Link up at the dungeon's entrance.
    g.Frame(30 * kS, std::nullopt);
    g.Frame(36 * kS, std::nullopt);
    PutLink(g.memory, 12, kAliveWord);
    out = g.Frame(40 * kS, kForestEntrance);
    REQUIRE(g.downed.state() == DownedState::alive);
    CHECK(Logged(out.boss, "the boss room starts over"));
    REQUIRE(out.respawn.move_to);
    CHECK(*out.respawn.move_to == Forest().index);
    CHECK_FALSE(out.respawn.by_event);
    // Moved: in the room, a new entry for the sync.
    out = g.Frame(41 * kS, inside);
    CHECK(Has(out.boss, proto::BossState::in_room));
    CHECK(g.boss.phase() == Sync::Phase::waiting);
    CHECK(Logged(out.respawn, "back in the boss room"));
}

// ---- oot-64's late entry cases, on the boss sync as it is --------------------

TEST_CASE("boss late: one who comes in during the others' intro is pulled and leaves the intro with them") {
    Sync s;
    s.Tick(At("ForestTemple", kForestEntrance), 1 * kS);
    s.Receive(From(1, Forest(), proto::BossState::intro_start));
    auto a = s.Tick(At("ForestTemple", kForestEntrance), 2 * kS);
    REQUIRE(a.pull);
    // Arrives in his own intro (the room starts it on entry).
    a = s.Tick(At("ForestTemple", Forest().arrive, true), 3 * kS);
    CHECK(Has(a, proto::BossState::intro_start));
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    a = s.Tick(At("ForestTemple", Forest().arrive, true), 4 * kS);
    CHECK(a.skip_intro);
    a = s.Tick(At("ForestTemple", Forest().arrive, false), 5 * kS);
    CHECK(Has(a, proto::BossState::intro_end));
    CHECK(s.phase() == Sync::Phase::fight);
}

TEST_CASE("boss late: one who joins during the fight is pulled, his own intro is skipped at once") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));  // the heartbeat of one fighting
    auto a = s.Tick(At("ForestTemple", kForestEntrance), 1 * kS);
    REQUIRE(a.pull);
    a = s.Tick(At("ForestTemple", Forest().arrive, true), 2 * kS);
    CHECK(Has(a, proto::BossState::intro_start));
    CHECK(a.skip_intro);
}

TEST_CASE("boss late: after the boss's death nobody is pulled, and the room is entered beaten") {
    Sync s;
    s.Tick(At("ForestTemple", kForestEntrance), 1 * kS);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    s.Receive(From(1, Forest(), proto::BossState::defeated));
    auto a = s.Tick(At("ForestTemple", kForestEntrance), 2 * kS);
    CHECK_FALSE(a.pull);
    CHECK(s.Beaten("ForestTemple", Forest().index));
    a = s.Tick(At("ForestTemple", Forest().arrive), 3 * kS);
    CHECK(Logged(a, "(boss beaten)"));
    CHECK(s.phase() == Sync::Phase::done);
    a = s.Tick(At("ForestTemple", Forest().arrive), 10 * kS);
    CHECK(a.send.empty());  // no heartbeat for a beaten room
}

TEST_CASE("boss late: a reconnection forgets everything, the next heartbeat pulls again") {
    Sync s;
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    REQUIRE(s.Tick(At("ForestTemple", kForestEntrance), 1 * kS).pull);
    s.Reset();  // the hook: our connection lost
    CHECK_FALSE(s.Tick(At("ForestTemple", kForestEntrance), 2 * kS).pull);
    s.Receive(From(1, Forest(), proto::BossState::intro_end));
    CHECK(s.Tick(At("ForestTemple", kForestEntrance), 3 * kS).pull);
}

TEST_CASE("boss late: the blue warp takes only who steps in, the others stay and are not pulled anywhere") {
    Sync a;  // P1 beat the boss and takes the warp
    Sync b;  // P2 stays in the room
    a.Tick(At("ForestTemple", Forest().arrive, false, 1), 1 * kS);
    a.Tick(At("ForestTemple", Forest().arrive, true, 1), 2 * kS);   // intro
    a.Tick(At("ForestTemple", Forest().arrive, false, 1), 10 * kS);  // fight
    auto out = a.Tick(At("ForestTemple", Forest().arrive, true, 1), 30 * kS);  // the death
    REQUIRE(Has(out, proto::BossState::defeated));
    b.Tick(At("ForestTemple", CoopBoss::ArrivalFor(Forest(), 2)), 1 * kS);
    Relay(out, 1, b);
    b.Tick(At("ForestTemple", CoopBoss::ArrivalFor(Forest(), 2)), 31 * kS);
    // P1's warp: a scene change in P1's game only.
    out = a.Tick(At("SacredForestMeadow", Vec{0.0f, 0.0f, 0.0f}, false, 1), 40 * kS);
    CHECK(Has(out, proto::BossState::left));
    CHECK_FALSE(out.pull);
    Relay(out, 1, b);
    auto bo = b.Tick(At("ForestTemple", CoopBoss::ArrivalFor(Forest(), 2)), 41 * kS);
    CHECK_FALSE(bo.pull);
    CHECK(b.phase() == Sync::Phase::done);
    // A third player still in the dungeon is not pulled into the beaten room.
    Sync c;
    c.Tick(At("ForestTemple", kForestEntrance, false, 3), 1 * kS);
    c.Receive(From(1, Forest(), proto::BossState::defeated));
    c.Receive(From(2, Forest(), proto::BossState::intro_end));  // P2's last heartbeat before the death
    CHECK_FALSE(c.Tick(At("ForestTemple", kForestEntrance, false, 3), 42 * kS).pull);
}

// ---- The families that break with several players (coop/host/boss_rules.hpp, docs/JEFES.md 9) ----

namespace {

using coop::host::BossFamily;
using coop::host::FamilyActor;
using coop::host::FamilyHold;
using coop::host::FamilyRole;
using coop::host::FamilySeen;
using coop::host::FamilyWalkIn;
using coop::host::FamilyWalkOut;
using coop::host::FamilyWatch;

proto::ObjectId Placed(std::uint64_t hash) { return proto::ObjectId{proto::ObjectKind::placed, 0, hash}; }
proto::ObjectId Made(std::uint8_t maker, std::uint64_t value) {
    return proto::ObjectId{proto::ObjectKind::made, maker, value};
}

bool Said(const std::vector<std::string>& lines, const std::string& start) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.starts_with(start); });
}

FamilyWalkIn HeldBy(std::uint64_t now, bool held, std::vector<FamilySeen> seen,
                    proto::Vec3 link = {0.5f, 0.0f, 0.0f}) {
    FamilyWalkIn in;
    in.now_us = now;
    in.self = 1;
    in.held = held;
    in.link = link;
    in.seen = std::move(seen);
    return in;
}

} // namespace

TEST_CASE("boss families: the table names every grab, tennis and mirror actor once, with a reach for a grabbing part") {
    std::vector<std::string_view> names;
    for (const FamilyActor& a : coop::host::family_actors()) {
        CHECK_FALSE(a.name.empty());
        CHECK_FALSE(a.group.empty());
        CHECK(std::find(names.begin(), names.end(), a.name) == names.end());
        names.push_back(a.name);
        if (a.family == BossFamily::grab && a.role == FamilyRole::part) {
            CHECK(a.reach > 0.0f);
        }
        CHECK(coop::host::family_actor(a.name) == &a);
    }
    // ZMP's grabbers (D-134, D-168) and its tennis and mirror bosses.
    for (const char* grab : {"Enemy_DeadHand_Hand", "Enemy_LikeLike", "Enemy_DungeonBoss_Morpha_Tentacle",
                             "Enemy_DungeonBoss_BongoBongo_LeftHand", "Enemy_DungeonBoss_BongoBongo_RightHand",
                             "Enemy_Wallmaster", "Enemy_Floormaster_Little", "Enemy_ReDead"}) {
        REQUIRE(coop::host::family_actor(grab) != nullptr);
        CHECK(coop::host::family_actor(grab)->family == BossFamily::grab);
        CHECK(coop::host::family_actor(grab)->role == FamilyRole::part);
    }
    CHECK(coop::host::family_actor("Enemy_DungeonBoss_Morpha")->role == FamilyRole::body);
    CHECK(coop::host::family_actor("Enemy_DungeonBoss_Morpha")->whole);
    CHECK_FALSE(coop::host::family_actor("Enemy_LikeLike")->whole);
    CHECK(coop::host::family_actor("DungeonBossPhantomGanon_Bullet_LightBall")->family == BossFamily::tennis);
    CHECK(coop::host::family_actor("DungeonBossGanondrof_Bullet_ChargeLightBall")->group == "Ganondorf");
    CHECK(coop::host::family_actor("Enemy_KotakeKoumeBattleManager")->family == BossFamily::mirror);
    CHECK(coop::host::family_actor("Enemy_DungeonBoss_Gohma") == nullptr);
    CHECK(coop::host::family_actor("Enemy_LikeLike_Dictionary") == nullptr);  // the picture book's
    CHECK(std::string(coop::host::to_string(BossFamily::tennis)) == "tennis");
}

TEST_CASE("boss families: Link's held commands, not a block's grab nor a ledge") {
    for (const char* held : {"Grab", "GrabA", "GrabY", "GrabWait", "GrabEd", "GrabAway", "Grab_Air_St",
                             "Grab_Head_Reaction_B", "Grab_Bongo_St", "Grab_L", "Eaten", "Eaten_F", "FaceGrab",
                             "Face_Grab"}) {
        CHECK_MESSAGE(coop::host::held_animation(held), held);
    }
    for (const char* not_held : {"", "GrabMovableObj", "GrabMoveActorType", "Cliff_Hold", "CliffHold",
                                 "Movable_Pull_Fail_SpinWall", "Wait", "Freeze", "GuardBeam", "Grabbed_by_none",
                                 "Face_Root"}) {
        CHECK_FALSE_MESSAGE(coop::host::held_animation(not_held), not_held);
    }
}

TEST_CASE("boss families: a ride key is the placement hash, or the made id with the top bit") {
    CHECK(coop::host::family_key(Placed(0x7d1f39d42478c1f6)) == 0x7d1f39d42478c1f6);
    const std::uint64_t made = coop::host::family_key(Made(2, 0x15));
    CHECK((made >> 63) == 1u);
    CHECK(made != coop::host::family_key(Made(3, 0x15)));  // another maker's 0x15
    CHECK(made != coop::host::family_key(Made(2, 0x16)));
    CHECK(made == coop::host::family_key(Made(2, 0x15)));  // the same in every game
}

TEST_CASE("boss families: what is kept for a grab and for a volley") {
    const FamilyActor& morpha = *coop::host::family_actor("Enemy_DungeonBoss_Morpha");
    const FamilyActor& tentacle = *coop::host::family_actor("Enemy_DungeonBoss_Morpha_Tentacle");
    const FamilyActor& like = *coop::host::family_actor("Enemy_LikeLike");
    const FamilyActor& phantom = *coop::host::family_actor("Enemy_DungeonBoss_PhantomGanon");
    const FamilyActor& twinrova = *coop::host::family_actor("Enemy_DungeonBoss_Twinrova");
    FamilyHold none;
    CHECK_FALSE(coop::host::family_holds(morpha, none, proto::Vec3{0, 0, 0}));
    // A tentacle holds us: the whole of Morpha is kept, the core wherever it swims.
    FamilyHold by_tentacle{"Morpha", proto::Vec3{10.0f, 0.0f, 0.0f}, {}};
    REQUIRE(coop::host::family_holds(morpha, by_tentacle, proto::Vec3{-20.0f, 0.0f, 0.0f}));
    CHECK(*coop::host::family_holds(morpha, by_tentacle, std::nullopt) ==
          "Morpha holds its owner's Link: its game lets him go (ZMP D-134)");
    CHECK(coop::host::family_holds(tentacle, by_tentacle, std::nullopt));
    CHECK_FALSE(coop::host::family_holds(like, by_tentacle, proto::Vec3{10.0f, 0.0f, 0.0f}));  // another group
    // A Like Like holds us: that one, not the one across the room, nor one whose place is not known.
    FamilyHold by_like{"Like Like", proto::Vec3{0.0f, 0.0f, 0.0f}, {}};
    CHECK(coop::host::family_holds(like, by_like, proto::Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(coop::host::family_holds(like, by_like, proto::Vec3{2.9f, 0.0f, 0.0f}));
    CHECK_FALSE(coop::host::family_holds(like, by_like, proto::Vec3{12.0f, 0.0f, 0.0f}));
    CHECK_FALSE(coop::host::family_holds(like, by_like, std::nullopt));
    // A volley: Phantom Ganon, not Ganondorf.
    FamilyHold volley{{}, std::nullopt, "Phantom Ganon"};
    REQUIRE(coop::host::family_holds(phantom, volley, std::nullopt));
    CHECK(*coop::host::family_holds(phantom, volley, std::nullopt) ==
          "Phantom Ganon's ball is in its owner's game: the volley is theirs (no hand-over mid-volley)");
    CHECK_FALSE(coop::host::family_holds(*coop::host::family_actor("Enemy_DungeonBoss_Ganondorf"), volley,
                                         std::nullopt));
    CHECK_FALSE(coop::host::family_holds(phantom, by_tentacle, std::nullopt));
    // Mirror is final_rules' (hand_over_holds): nothing here.
    CHECK_FALSE(coop::host::family_holds(twinrova, by_tentacle, std::nullopt));

    // Through hand_over_holds and owner_keep (the Twinrova rule's entry): both reasons, never
    // for a downed owner.
    coop::host::HandOverInput in;
    in.body = "Enemy_DungeonBoss_Morpha";
    in.family = by_tentacle;
    const coop::host::OwnerKeep keep = coop::host::owner_keep(in);
    CHECK(keep.hit);
    CHECK(keep.nearest);
    CHECK(keep.why == "Morpha holds its owner's Link: its game lets him go (ZMP D-134)");
    in.owner_up = false;
    CHECK(coop::host::owner_keep(in) == coop::host::OwnerKeep{});
    in.owner_up = true;
    in.body = "Enemy_LikeLike";
    in.family = by_like;
    in.body_at = proto::Vec3{1.0f, 0.0f, 0.0f};
    CHECK(coop::host::owner_keep(in).hit);
    in.body_at = proto::Vec3{30.0f, 0.0f, 0.0f};
    CHECK(coop::host::owner_keep(in) == coop::host::OwnerKeep{});
    // Twinrova still by its own rule: the ice, not a grab.
    in.body = "Enemy_DungeonBoss_Kotake";
    in.family = by_tentacle;
    CHECK_FALSE(coop::host::hand_over_holds(in));
    in.owner_frozen = true;
    CHECK(coop::host::hand_over_holds(in));
}

TEST_CASE("boss families: the watcher finds the Like Like that holds us, says it once, and its ride") {
    FamilyWatch w;
    const std::vector<FamilySeen> room{{"Enemy_LikeLike", Placed(0x11), {0.0f, 0.0f, 0.0f}, 1},
                                       {"Enemy_LikeLike", Placed(0x22), {20.0f, 0.0f, 0.0f}, 1},
                                       {"Enemy_Stalfos", Placed(0x33), {0.4f, 0.0f, 0.0f}, 1}};
    FamilyWalkOut out = w.walk(HeldBy(1'000'000, false, room));
    CHECK(out.hold == FamilyHold{});
    CHECK_FALSE(out.ride);
    CHECK(out.parts.size() == 2);  // both Like Likes, for the others' rides; not the Stalfos
    CHECK(out.log.empty());
    out = w.walk(HeldBy(1'033'000, true, room));
    CHECK(out.hold.held_group == "Like Like");
    REQUIRE(out.hold.held_at);
    CHECK((*out.hold.held_at)[0] == doctest::Approx(0.0f));
    REQUIRE(out.ride);
    CHECK(out.ride->platform == 0x11);
    CHECK(out.ride->offset[0] == doctest::Approx(0.5f));
    REQUIRE(out.log.size() == 2);
    CHECK(out.log[0] ==
          "grab: our Link is held by Enemy_LikeLike 0000000000000011 (Like Like), 0.5 m from it; ours: our samples "
          "of Like Like carry owner_keeps while it holds us (ZMP D-134); the others draw us on their copy of it");
    CHECK(out.log[1] == "check: grab ok our Link held by Enemy_LikeLike 0.5 m from its origin (Like Like)");
    CHECK(w.hold() == out.hold);
    out = w.walk(HeldBy(1'066'000, true, room));
    CHECK(out.log.empty());  // a change is said once
    out = w.walk(HeldBy(1'100'000, false, room));
    CHECK(out.hold == FamilyHold{});
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0] == "grab: our Link is let go by Enemy_LikeLike 0000000000000011 (Like Like): it may be "
                        "handed over again (issue 150)");
    // Held again: no second ok line.
    out = w.walk(HeldBy(1'133'000, true, room));
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0].starts_with("grab: our Link is held by Enemy_LikeLike"));
    // Downed while held: nothing holds for the group (the owner gives it up).
    FamilyWalkIn down = HeldBy(1'166'000, true, room);
    down.up = false;
    out = w.walk(down);
    CHECK(out.hold == FamilyHold{});
    CHECK(Said(out.log, "grab: our Link is let go by"));
}

TEST_CASE("boss families: a held Link with no part near says the fallback once, and a copy's hold is not kept") {
    FamilyWatch w;
    const std::vector<FamilySeen> out_of_reach{{"Enemy_LikeLike", Placed(0x11), {9.0f, 0.0f, 0.0f}, 1}};
    FamilyWalkOut out = w.walk(HeldBy(1'000'000, true, out_of_reach));
    CHECK(out.hold == FamilyHold{});
    CHECK(out.log.empty());
    out = w.walk(HeldBy(1'999'000, true, out_of_reach));
    CHECK(out.log.empty());
    out = w.walk(HeldBy(2'000'000, true, out_of_reach));
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0] == "check: grab fallback our Link's AS command is a held one but no grabbing part of the "
                        "table is within reach: nothing is kept, the others draw us on our samples (docs/JEFES.md "
                        "section 9)");
    CHECK(w.walk(HeldBy(5'000'000, true, out_of_reach)).log.empty());  // once

    // A copy P2 owns holds us (its own AI ran here, LICHII_COOP_OBJECTS_AI_HOLD unset): said, and the
    // hold's group is known (our samples of it are none: we do not own it), the ride goes out.
    FamilyWatch c;
    const std::vector<FamilySeen> copy{{"Enemy_DungeonBoss_BongoBongo_LeftHand", Made(2, 7), {0.0f, 1.0f, 0.0f}, 2},
                                       {"Enemy_DungeonBoss_BongoBongo", Placed(0x44), {0.0f, 8.0f, 0.0f}, 2}};
    out = c.walk(HeldBy(1'000'000, true, copy, {0.0f, 1.5f, 0.0f}));
    CHECK(out.hold.held_group == "Bongo Bongo");
    REQUIRE(out.ride);
    CHECK(out.ride->platform == coop::host::family_key(Made(2, 7)));
    CHECK(out.ride->offset[1] == doctest::Approx(0.5f));
    CHECK(Said(out.log, "grab: our Link is held by Enemy_DungeonBoss_BongoBongo_LeftHand "));
    CHECK(out.log[0].find("a copy P2 owns: it is not its owner's AI that holds us, nothing is kept") !=
          std::string::npos);
    // Our place not read: the fallback says so.
    FamilyWatch n;
    FamilyWalkIn no_link = HeldBy(1'000'000, true, copy);
    no_link.link.reset();
    n.walk(no_link);
    no_link.now_us = 2'500'000;
    out = n.walk(no_link);
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0].find("(our Link's place is not read)") != std::string::npos);
}

TEST_CASE("boss families: a ball in our game while we own its boss is a volley, and the fallback without one") {
    FamilyWatch w;
    const FamilySeen boss{"Enemy_DungeonBoss_PhantomGanon", Placed(0x55), {0.0f, 75.0f, -62.0f}, 1};
    const FamilySeen ball{"DungeonBossPhantomGanon_Bullet_LightBall", {}, {1.0f, 75.0f, -60.0f}, 0};
    FamilyWalkOut out = w.walk(HeldBy(1'000'000, false, {boss}));
    CHECK(out.hold.rally_group.empty());
    CHECK(out.log.empty());
    out = w.walk(HeldBy(2'000'000, false, {boss, ball}));
    CHECK(out.hold.rally_group == "Phantom Ganon");
    REQUIRE(out.log.size() == 2);
    CHECK(out.log[0] == "tennis: Phantom Ganon's ball is in flight in our game: the volley is ours, our samples of "
                        "it carry owner_keeps (no hand-over mid-volley)");
    CHECK(out.log[1] == "check: tennis ok DungeonBossPhantomGanon_Bullet_LightBall in our game while we own Phantom "
                        "Ganon: our samples of it carry owner_keeps until the volley ends");
    CHECK(w.walk(HeldBy(2'033'000, false, {boss, ball})).log.empty());
    out = w.walk(HeldBy(3'000'000, false, {boss}));
    CHECK(out.hold.rally_group.empty());
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0] == "tennis: Phantom Ganon's volley is over: it may be handed over again (issue 150)");
    // Not ours: the hold still names the volley (only our own samples carry bits), but nothing is said.
    FamilyWatch theirs;
    FamilySeen their_boss = boss;
    their_boss.owner = 2;
    out = theirs.walk(HeldBy(1'000'000, false, {their_boss, ball}));
    CHECK(out.hold.rally_group == "Phantom Ganon");
    CHECK(out.log.empty());
    // Owned for kNoBallUs without a ball: the fallback, once.
    FamilyWatch none;
    none.walk(HeldBy(1'000'000, false, {boss}));
    CHECK(none.walk(HeldBy(1'000'000 + FamilyWatch::kNoBallUs - 1, false, {boss})).log.empty());
    out = none.walk(HeldBy(1'000'000 + FamilyWatch::kNoBallUs, false, {boss}));
    REQUIRE(out.log.size() == 1);
    CHECK(out.log[0] == "check: tennis fallback we have owned Phantom Ganon for 300 s and seen no ball of the table: "
                        "its name may differ (docs/JEFES.md section 9), a volley is not kept");
    CHECK(none.walk(HeldBy(1'000'000 + 2 * FamilyWatch::kNoBallUs, false, {boss})).log.empty());
    // Lost and owned again: the wait starts over, and a reset forgets everything.
    none.reset();
    CHECK(none.hold() == FamilyHold{});
}

namespace {

CoopObjects::Seen Walked(std::uint64_t address, std::uint64_t hash, std::string name, float x) {
    CoopObjects::Seen s;
    s.address = address;
    s.name = std::move(name);
    s.hash = hash;
    s.life = proto::ObjectLife{8, 8};
    s.position = {x, 0.0f, 0.0f};
    return s;
}

// The owner's samples of this step, by id.
std::vector<proto::ObjectSample> SamplesOf(const std::vector<proto::Message>& send, const proto::ObjectId& id) {
    std::vector<proto::ObjectSample> out;
    for (const auto& m : send) {
        if (const auto* s = std::get_if<proto::ObjectSample>(&m); s != nullptr && s->id == id) {
            out.push_back(*s);
        }
    }
    return out;
}

std::vector<proto::Message> Step(CoopObjects::Sync& sync, std::uint64_t now, const std::vector<CoopObjects::Seen>& seen,
                                 const std::string& scene = "WaterTemple") {
    std::vector<proto::Message> send;
    std::vector<CoopObjects::LifeWrite> writes;
    sync.Tick(scene, 1, now, seen, send, writes);
    return send;
}

std::uint8_t BitsOf(const std::vector<proto::ObjectSample>& samples) {
    REQUIRE(samples.size() == 1);
    return samples[0].state.state.value_or(0);
}

} // namespace

TEST_CASE("boss families, the object sync: the Like Like that holds our Link carries owner_keeps, the other one not") {
    CoopObjects::Sync sync;
    const std::vector<CoopObjects::Seen> room{Walked(0x100, 0x11, "Enemy_LikeLike", 0.0f),
                                              Walked(0x200, 0x22, "Enemy_LikeLike", 20.0f)};
    sync.SetPlayer(proto::Vec3{0.5f, 0.0f, 0.0f});
    Step(sync, 0, room);
    const proto::ObjectId held_one = *sync.IdOf(0x100);
    const proto::ObjectId other_one = *sync.IdOf(0x200);
    sync.Receive(proto::ObjectOwner{held_one, 1, 0, 0});
    sync.Receive(proto::ObjectOwner{other_one, 1, 0, 0});
    auto send = Step(sync, 100'000, room);
    CHECK(BitsOf(SamplesOf(send, held_one)) == 0);
    CHECK(sync.TakeFamilyLines().empty());
    CHECK(sync.FamilyParts().size() == 2);

    CoopObjects::Sync::Keep held;
    held.held = true;
    sync.SetKeep(held);
    send = Step(sync, 200'000, room);  // a change of the bits goes out at once
    CHECK(BitsOf(SamplesOf(send, held_one)) == proto::object_state::owner_keeps);
    CHECK(SamplesOf(send, other_one).empty());  // nothing changed for it: no sample, no bits
    CHECK(sync.KeepBits("Enemy_LikeLike", proto::Vec3{20.0f, 0.0f, 0.0f}) == 0);
    CHECK(sync.FamilyNow().held_group == "Like Like");
    REQUIRE(sync.FamilyRide());
    CHECK(sync.FamilyRide()->platform == coop::host::family_key(held_one));
    CHECK(sync.FamilyScene() == "WaterTemple");
    const auto lines = sync.TakeFamilyLines();
    CHECK(Said(lines, "grab: our Link is held by Enemy_LikeLike"));
    CHECK(Said(lines, "check: grab ok our Link held by Enemy_LikeLike 0.5 m"));

    // Let go: the bits go at once, the ride ends.
    sync.SetKeep(CoopObjects::Sync::Keep{});
    send = Step(sync, 300'000, room);
    CHECK(BitsOf(SamplesOf(send, held_one)) == 0);
    CHECK_FALSE(sync.FamilyRide());
    CHECK(Said(sync.TakeFamilyLines(), "grab: our Link is let go by Enemy_LikeLike"));

    // Off: nothing is kept, the watcher does not run.
    sync.SetFamilies(false);
    sync.SetKeep(held);
    send = Step(sync, 400'000, room);
    CHECK(SamplesOf(send, held_one).empty());
    CHECK(sync.KeepBits("Enemy_LikeLike", proto::Vec3{0.0f, 0.0f, 0.0f}) == 0);
    CHECK(sync.TakeFamilyLines().empty());
    CHECK(sync.FamilyParts().empty());
}

TEST_CASE("boss families, the object sync: a ball in our walk keeps Phantom Ganon while we own it") {
    CoopObjects::Sync sync;
    const std::vector<CoopObjects::Seen> room{Walked(0x300, 0x55, "Enemy_DungeonBoss_PhantomGanon", 0.0f)};
    sync.SetPlayer(proto::Vec3{6.0f, 0.0f, 0.0f});
    Step(sync, 0, room, "ForestTemple");
    const proto::ObjectId boss = *sync.IdOf(0x300);
    sync.Receive(proto::ObjectOwner{boss, 1, 0, 0});
    CHECK(BitsOf(SamplesOf(Step(sync, 100'000, room, "ForestTemple"), boss)) == 0);
    // The ball is no shared object: the walk hands it over by name and place.
    sync.SetFamilyExtras({{"DungeonBossPhantomGanon_Bullet_LightBall", proto::Vec3{3.0f, 0.0f, 0.0f}},
                          {"Obj_Something_Else", proto::Vec3{0.0f, 0.0f, 0.0f}}});
    auto send = Step(sync, 200'000, room, "ForestTemple");
    CHECK(BitsOf(SamplesOf(send, boss)) == proto::object_state::owner_keeps);
    CHECK(sync.FamilyNow().rally_group == "Phantom Ganon");
    CHECK(Said(sync.TakeFamilyLines(), "tennis: Phantom Ganon's ball is in flight in our game"));
    // The next walk has no ball: the volley is over, the bits go.
    send = Step(sync, 300'000, room, "ForestTemple");
    CHECK(BitsOf(SamplesOf(send, boss)) == 0);
    CHECK(Said(sync.TakeFamilyLines(), "tennis: Phantom Ganon's volley is over"));
    // A new scene forgets the volley.
    sync.SetFamilyExtras({{"DungeonBossPhantomGanon_Bullet_LightBall", proto::Vec3{3.0f, 0.0f, 0.0f}}});
    Step(sync, 400'000, room, "ForestTemple");
    REQUIRE(sync.FamilyNow().rally_group == "Phantom Ganon");
    Step(sync, 500'000, {}, "SacredForestMeadow");
    CHECK(sync.FamilyNow() == FamilyHold{});
}
