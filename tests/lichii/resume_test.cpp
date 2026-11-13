// Tests for the way back to our own last place (lichii/hook/coop_resume.cpp,
// and the resume trip of lichii/hook/coop_teleport.cpp).
#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

#include "coop_resume.h"
#include "coop_teleport.h"

namespace proto = coop::protocol;
using CoopResume::Decision;
using CoopResume::Kind;
using CoopResume::Now;
using CoopResume::Place;
using CoopResume::Recorder;
using CoopResume::Returner;
using CoopTeleport::Local;
using CoopTeleport::Trip;
using CoopTeleport::Vec;

namespace {

constexpr std::uint64_t kS = 1'000'000;

Local At(const char* scene, float x, float y, float z) {
    Local l;
    l.scene = scene;
    l.position = Vec{x, y, z};
    l.age = proto::Age::child;
    l.reserved_banc = std::string{};
    return l;
}

Now Standing(const char* scene, float x, float y, float z) {
    Now n;
    n.local = At(scene, x, y, z);
    n.grounded = Vec{x, y, z};
    n.grounded_scene = scene;
    n.unix_s = 1'800'000'000;
    return n;
}

Place Kept(const char* scene, float x, float y, float z, Kind kind = Kind::ok) {
    Place p;
    p.room = "sala1";
    p.name = "Zelda";
    p.scene = scene;
    p.at = Vec{x, y, z};
    p.age = proto::Age::child;
    p.kind = kind;
    p.unix_s = 1'799'999'000;
    return p;
}

bool Has(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines) {
        if (l.find(part) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("resume: a place is one line and back") {
    const Place p = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f, Kind::boss);
    const std::string line = CoopResume::Format(p);
    CHECK(line == "v1;sala1;Zelda;InsideTheDekuTree;-51.50;112.60;15.00;child;boss;1799999000");
    const auto back = CoopResume::Parse(line + "\r\n");
    REQUIRE(back);
    CHECK(back->room == "sala1");
    CHECK(back->name == "Zelda");
    CHECK(back->scene == "InsideTheDekuTree");
    CHECK(back->at.x == doctest::Approx(-51.5f));
    CHECK(back->at.y == doctest::Approx(112.6f));
    CHECK(back->at.z == doctest::Approx(15.0f));
    CHECK(back->age == proto::Age::child);
    CHECK(back->kind == Kind::boss);
    CHECK(back->unix_s == 1'799'999'000);
    Place none = p;
    none.age.reset();
    none.kind = Kind::event;
    const auto again = CoopResume::Parse(CoopResume::Format(none));
    REQUIRE(again);
    CHECK_FALSE(again->age);
    CHECK(again->kind == Kind::event);
}

TEST_CASE("resume: anything else is not a place") {
    CHECK_FALSE(CoopResume::Parse(""));
    CHECK_FALSE(CoopResume::Parse("# a comment"));
    CHECK_FALSE(CoopResume::Parse("v2;sala1;Zelda;X;1;2;3;child;ok;1"));       // another version
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;child;ok"));         // a field missing
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;child;ok;1;9"));     // one too many
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;;1;2;3;child;ok;1"));        // no scene
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;nan;3;child;ok;1"));     // not finite
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2x;3;child;ok;1"));      // not a number
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;old;ok;1"));         // no such age
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;child;maybe;1"));    // no such kind
    CHECK_FALSE(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;child;ok;soon"));    // no time
    CHECK(CoopResume::Parse("v1;sala1;Zelda;X;1;2;3;-;ok;0"));
}

TEST_CASE("resume: fields are cleaned so a line stays one line") {
    CHECK(CoopResume::Clean("  Ana;B#c\n") == "Ana_B_c");
    CHECK(CoopResume::Clean(std::string(100, 'a')).size() == 64);
    Place p = Kept("KokiriForest_Child", 1, 2, 3);
    p.name = "Ana;1";
    p.room = "wss://host:443/remake/sala1";
    const auto back = CoopResume::Parse(CoopResume::Format(p));
    REQUIRE(back);
    CHECK(back->name == "Ana_1");
    CHECK(back->room == "wss://host:443/remake/sala1");
}

TEST_CASE("resume: the file gives the line of this room and name") {
    Place mine = Kept("KokiriForest_Child", 1, 2, 3);
    const std::string text = CoopResume::FileText(mine);
    CHECK(text.rfind("# ", 0) == 0);
    REQUIRE(CoopResume::FromFile(text, "sala1", "Zelda"));
    CHECK(CoopResume::FromFile(text, "sala1", "Zelda")->scene == "KokiriForest_Child");
    CHECK_FALSE(CoopResume::FromFile(text, "sala2", "Zelda"));   // another room
    CHECK_FALSE(CoopResume::FromFile(text, "sala1", "Saria"));   // another player
    CHECK_FALSE(CoopResume::FromFile("garbage\n# x\n", "sala1", "Zelda"));
    Place other = Kept("LostWoods", 4, 5, 6);
    other.room = "sala2";
    const std::string two = CoopResume::Format(other) + "\r\n" + CoopResume::Format(mine) + "\n";
    REQUIRE(CoopResume::FromFile(two, "sala2", "Zelda"));
    CHECK(CoopResume::FromFile(two, "sala2", "Zelda")->scene == "LostWoods");
}

TEST_CASE("resume: the app gives one line (ours whatever its name), a pair gives one per name") {
    Place a = Kept("InsideTheDekuTree", 6.7f, 47.0f, 67.6f);
    a.name = "A";
    Place b = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f);
    b.name = "B";
    const auto one = CoopResume::FromGiven(CoopResume::Format(a), "Zelda");
    REQUIRE(one);
    CHECK(one->at.x == doctest::Approx(6.7f));
    const std::string pair = CoopResume::Format(a) + "|" + CoopResume::Format(b);
    REQUIRE(CoopResume::FromGiven(pair, "B"));
    CHECK(CoopResume::FromGiven(pair, "B")->at.x == doctest::Approx(-51.5f));
    CHECK(CoopResume::FromGiven(pair, "A")->at.x == doctest::Approx(6.7f));
    CHECK_FALSE(CoopResume::FromGiven(pair, "C"));
    CHECK_FALSE(CoopResume::FromGiven("garbage", "A"));
    CHECK_FALSE(CoopResume::FromGiven("", "A"));
    // A bad part next to a good one: only by name.
    CHECK_FALSE(CoopResume::FromGiven(CoopResume::Format(a) + "|junk", "B"));
    CHECK(CoopResume::FromGiven(CoopResume::Format(a) + "|junk", "A"));
}

TEST_CASE("resume: the recorder keeps the grounded place once started, not too often") {
    Recorder rec("sala1", "Zelda");
    Now n = Standing("KokiriForest_Child", 10, 100, 20);
    CHECK_FALSE(rec.Frame(n, 0));  // not before the way back is decided
    rec.Start();
    auto p = rec.Frame(n, kS);
    REQUIRE(p);
    CHECK(p->room == "sala1");
    CHECK(p->name == "Zelda");
    CHECK(p->scene == "KokiriForest_Child");
    CHECK(p->kind == Kind::ok);
    CHECK(p->unix_s == n.unix_s);
    CHECK_FALSE(rec.Frame(n, 2 * kS));  // the same place
    n.grounded = Vec{10.5f, 100, 20};
    CHECK_FALSE(rec.Frame(n, 10 * kS));  // half a unit: the same place
    n.grounded = Vec{15, 100, 20};
    CHECK_FALSE(rec.Frame(n, kS + Recorder::kEveryUs - 1));  // moved, but too soon
    p = rec.Frame(n, kS + Recorder::kEveryUs);
    REQUIRE(p);
    CHECK(p->at.x == 15.0f);
    // Another scene: within a second.
    Now there = Standing("LostWoods", 1, 2, 3);
    CHECK_FALSE(rec.Frame(there, kS + Recorder::kEveryUs + Recorder::kSoonUs - 1));
    p = rec.Frame(there, kS + Recorder::kEveryUs + Recorder::kSoonUs);
    REQUIRE(p);
    CHECK(p->scene == "LostWoods");
    // Not grounded yet: nothing.
    Now air = there;
    air.grounded.reset();
    CHECK_FALSE(rec.Frame(air, 100 * kS));
}

TEST_CASE("resume: a place kept in a boss room, a fight or an event says so") {
    Recorder rec("sala1", "Zelda");
    rec.Start();
    Now n = Standing("InsideTheDekuTree", 0, 101, 20);
    REQUIRE(rec.Frame(n, 0)->kind == Kind::ok);
    n.local.event = true;  // a talk starts: the place is the one before it
    auto p = rec.Frame(n, 2 * kS);
    REQUIRE(p);
    CHECK(p->kind == Kind::event);
    n.local.event = false;
    n.boss_room = true;
    p = rec.Frame(n, 4 * kS);
    REQUIRE(p);
    CHECK(p->kind == Kind::boss);
    n.boss_room = false;
    n.local.fighting = true;
    CHECK_FALSE(rec.Frame(n, 6 * kS));  // still boss: nothing new
    n.local.fighting = false;
    p = rec.Frame(n, 8 * kS);
    REQUIRE(p);
    CHECK(p->kind == Kind::ok);
}

TEST_CASE("resume: the same scene moves Link back, unless it was no place to go back to") {
    const Now here = Standing("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);
    auto d = CoopResume::Decide(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f), here);
    CHECK(d.what == Decision::What::move);
    CHECK(d.at.x == -51.5f);
    CHECK(d.notice == "De vuelta donde lo dejaste");
    d = CoopResume::Decide(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f, Kind::event), here);
    CHECK(d.what == Decision::What::none);
    CHECK(d.log.find("cutscene or a talk") != std::string::npos);
    d = CoopResume::Decide(Kept("InsideTheDekuTree", 6.7f, 47.0f, 67.6f), here);  // Gohma's room
    CHECK(d.what == Decision::What::none);
    CHECK(d.log.find("boss room") != std::string::npos);
    d = CoopResume::Decide(Kept("InsideTheDekuTree", 1.0f, 101.0f, 14.7f), here);
    CHECK(d.what == Decision::What::none);
    CHECK(d.log.find("already") != std::string::npos);
}

TEST_CASE("resume: another scene is a trip: to the place, or to the entrance") {
    const Now here = Standing("KokiriForest_Child", 90.0f, 115.5f, -27.9f);
    auto d = CoopResume::Decide(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f), here);
    CHECK(d.what == Decision::What::travel);
    CHECK(d.scene == "InsideTheDekuTree");
    CHECK(d.start == "Entrance");
    CHECK(d.place);
    // Gohma's room: the dungeon's entrance, no move (a running fight pulls him in).
    d = CoopResume::Decide(Kept("InsideTheDekuTree", 6.7f, 47.0f, 67.6f), here);
    CHECK(d.what == Decision::What::travel);
    CHECK(d.start == "Entrance");
    CHECK_FALSE(d.place);
    CHECK(d.log.find("entrance") != std::string::npos);
    // Kept as boss although outside a room (our fight ran), and during an event.
    d = CoopResume::Decide(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f, Kind::boss), here);
    CHECK(d.what == Decision::What::travel);
    CHECK_FALSE(d.place);
    d = CoopResume::Decide(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f, Kind::event), here);
    CHECK(d.what == Decision::What::travel);
    CHECK_FALSE(d.place);
}

TEST_CASE("resume: never across ages, nor to a scene with no start point") {
    const Now here = Standing("KokiriForest_Child", 90.0f, 115.5f, -27.9f);
    Place adult = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f);
    adult.age = proto::Age::adult;
    CHECK(CoopResume::Decide(adult, here).what == Decision::What::none);
    CHECK(CoopResume::Decide(Kept("KakarikoVillage_Adult", 0, 0, 0), here).what == Decision::What::none);
    CHECK(CoopResume::Decide(Kept("NoSuchScene", 0, 0, 0), here).what == Decision::What::none);
    Place unknown_age = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f);
    unknown_age.age.reset();
    CHECK(CoopResume::Decide(unknown_age, here).what == Decision::What::travel);
}

TEST_CASE("resume: the decision waits for Link to stand still after the load, once") {
    Returner r(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f));
    Now loading;
    CHECK_FALSE(r.Frame(loading, 0));
    Now n = Standing("KokiriForest_Child", 90.0f, 115.5f, -27.9f);
    CHECK_FALSE(r.Frame(n, kS));
    n.local.event = true;  // the load's own event
    CHECK_FALSE(r.Frame(n, kS + Returner::kSteadyUs));
    n.local.event = false;
    n.local.reserved_banc = "KakarikoVillage_Child";  // someone asked a scene change (a harness warp)
    CHECK_FALSE(r.Frame(n, 2 * kS + Returner::kSteadyUs));
    n.local.reserved_banc = std::string{};
    CHECK_FALSE(r.Frame(n, 10 * kS));
    CHECK_FALSE(r.Frame(n, 10 * kS + Returner::kSteadyUs - 1));
    const auto d = r.Frame(n, 10 * kS + Returner::kSteadyUs);
    REQUIRE(d);
    CHECK(d->what == Decision::What::travel);
    CHECK(r.Done());
    CHECK_FALSE(r.Frame(n, 20 * kS));  // once
}

TEST_CASE("resume: no place says so at once, a Link that never stands gives up") {
    Returner none(std::nullopt);
    const auto d = none.Frame(Now{}, 0);
    REQUIRE(d);
    CHECK(d->what == Decision::What::none);
    CHECK(none.Done());
    Returner busy(Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f));
    Now n = Standing("KokiriForest_Child", 0, 0, 0);
    n.local.event = true;
    CHECK_FALSE(busy.Frame(n, kS));
    const auto late = busy.Frame(n, kS + Returner::kWindowUs + 1);
    REQUIRE(late);
    CHECK(late->what == Decision::What::none);
    CHECK(busy.Done());
}

TEST_CASE("resume trip: the scene change, then our own place, nothing in the chat") {
    Trip trip;
    auto a = trip.Resume("InsideTheDekuTree", "Entrance", Vec{-51.5f, 112.6f, 15.0f}, true, 0);
    REQUIRE(a.scene);
    CHECK(*a.scene == "InsideTheDekuTree");
    CHECK(a.start == "Entrance");
    CHECK(a.start_event);
    CHECK(a.lines.empty());
    CHECK(Has(a.log, "resume: scene change to InsideTheDekuTree"));
    CHECK(trip.Resuming());
    CHECK(trip.phase() == Trip::Phase::travelling);
    // A lost connection does not stop it.
    CHECK(trip.Left(0).log.empty());
    CHECK(trip.Resuming());
    Local loading;
    a = trip.Tick(loading, kS);
    CHECK(a.release);
    Local arrived = At("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);
    // test10 (#197): the request is not emptied on arrival (the game's arrival step reads it);
    // here the game has emptied it itself (reserved_banc ""), so nothing is left for us to clear.
    a = trip.Tick(arrived, 2 * kS);
    CHECK_FALSE(a.cancel_scene);
    CHECK(a.game_cleared);
    CHECK(trip.phase() == Trip::Phase::settling);
    CHECK_FALSE(trip.Tick(arrived, 3 * kS).move);
    a = trip.Tick(arrived, 3 * kS + Trip::kSettleUs);
    REQUIRE(a.move);
    CHECK_FALSE(a.cancel_scene);  // the game emptied it on arrival
    CHECK(a.move->x == -51.5f);
    CHECK_FALSE(a.ask);  // no player to ask
    CHECK(a.lines.empty());
    CHECK(Has(a.log, "back at the last place"));
    CHECK(a.notice == "De vuelta donde lo dejaste");
    CHECK(trip.phase() == Trip::Phase::idle);
    CHECK_FALSE(trip.Resuming());
}

TEST_CASE("resume trip: to the entrance only, and ended by a /teleport") {
    Trip trip;
    trip.Resume("InsideTheDekuTree", "Entrance", Vec{6.7f, 47.0f, 67.6f}, false, 0);
    Local arrived = At("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);
    trip.Tick(arrived, kS);
    trip.Tick(arrived, 2 * kS);
    auto a = trip.Tick(arrived, 2 * kS + Trip::kSettleUs);
    CHECK_FALSE(a.move);
    CHECK(Has(a.log, "at the entrance"));
    CHECK(a.notice.find("entrada") != std::string::npos);
    CHECK(trip.phase() == Trip::Phase::idle);

    Trip other;
    other.Resume("InsideTheDekuTree", "Entrance", Vec{0, 0, 0}, true, 0);
    a = other.Start(2, "Saria", At("KokiriForest_Child", 0, 0, 0), kS);
    CHECK(a.cancel_scene);
    CHECK(a.restore_start);
    CHECK_FALSE(other.Resuming());
    CHECK(other.phase() == Trip::Phase::asking);
    // A second resume while a trip runs is dropped.
    a = other.Resume("InsideTheDekuTree", "Entrance", Vec{0, 0, 0}, true, kS);
    CHECK_FALSE(a.scene);
    CHECK_FALSE(other.Resuming());
}

TEST_CASE("resume trip: giving up goes to the log, not the chat") {
    Trip trip;
    trip.Resume("InsideTheDekuTree", "Entrance", Vec{0, 0, 0}, true, 0);
    const Local here = At("KokiriForest_Child", 0, 0, 0);
    const auto a = trip.Tick(here, Trip::kGiveUpUs + 1);
    CHECK(a.cancel_scene);
    CHECK(a.lines.empty());
    CHECK(Has(a.log, "did not change scene in time"));
    CHECK_FALSE(trip.Resuming());
}

// ---- Inside a dungeon (issue 197): never into another room; the entrance room's way out ----

namespace {

// Two rooms of the Deku Tree, as the puppet pack's coop_rooms.txt gives them: the entrance
// room (0) and the slingshot room (2), where upstream's bigrun4 left Link held.
CoopRooms::Table DekuRooms() {
    return CoopRooms::Table({{"InsideTheDekuTree", 0, 0.2f, 101.0f, 14.7f},
                             {"InsideTheDekuTree", 0, -8.0f, 101.0f, 5.0f},
                             {"InsideTheDekuTree", 2, -33.0f, 110.0f, 10.0f},
                             {"InsideTheDekuTree", 2, -51.5f, 112.6f, 15.0f}});
}

CoopResume::Door DoorSpot() {
    CoopResume::Door d;
    d.scene = "InsideTheDekuTree";
    d.start = "Entrance";
    d.at = Vec{-10.0f, 101.0f, 8.0f};  // in the entrance room, by its way to the slingshot room
    d.age = proto::Age::child;
    d.unix_s = 1'799'998'000;
    return d;
}

} // namespace

TEST_CASE("resume dungeon: the door is a spot of the entrance room, nowhere else") {
    const CoopRooms::Table rooms = DekuRooms();
    auto d = CoopResume::DoorAt(rooms, "InsideTheDekuTree", Vec{-10.0f, 101.0f, 8.0f}, proto::Age::child, 5);
    REQUIRE(d);
    CHECK(d->start == "Entrance");
    CHECK(d->at.x == -10.0f);
    CHECK(d->unix_s == 5);
    CHECK_FALSE(CoopResume::DoorAt(rooms, "InsideTheDekuTree", Vec{-51.5f, 112.6f, 15.0f}, proto::Age::child, 5));
    CHECK_FALSE(CoopResume::DoorAt(rooms, "KokiriForest_Child", Vec{90.0f, 115.5f, -27.9f}, proto::Age::child, 5));
    // No room table: only within CoopTeleport::kNearMove of the entrance start.
    const CoopRooms::Table none;
    CHECK(CoopResume::DoorAt(none, "InsideTheDekuTree", Vec{-5.0f, 101.0f, 10.0f}, std::nullopt, 5));
    CHECK_FALSE(CoopResume::DoorAt(none, "InsideTheDekuTree", Vec{-30.0f, 101.0f, 10.0f}, std::nullopt, 5));
    REQUIRE(CoopTeleport::StartPosition("InsideTheDekuTree", "Entrance"));
    CHECK(CoopTeleport::StartPosition("InsideTheDekuTree", "Entrance")->y == doctest::Approx(101.0f));
    CHECK_FALSE(CoopTeleport::StartPosition("InsideTheDekuTree", "NoSuchStart"));
}

TEST_CASE("resume dungeon: the recorder keeps the last spot of the entrance room, and the file its own line") {
    const CoopRooms::Table rooms = DekuRooms();
    Recorder rec("sala1", "Zelda");
    rec.Start();
    Now n = Standing("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);
    n.rooms = &rooms;
    REQUIRE(rec.Frame(n, kS));
    REQUIRE(rec.LastDoor());
    n.grounded = Vec{-10.0f, 101.0f, 8.0f};  // walks to the way out
    rec.Frame(n, 2 * kS);
    n.grounded = Vec{-51.5f, 112.6f, 15.0f};  // the slingshot room: not a door
    auto p = rec.Frame(n, 2 * kS + Recorder::kEveryUs);
    REQUIRE(p);
    REQUIRE(rec.LastDoor());
    CHECK(rec.LastDoor()->at.x == -10.0f);
    // An event or a boss room keeps no door.
    n.local.event = true;
    n.grounded = Vec{-1.0f, 101.0f, 12.0f};
    rec.Frame(n, 20 * kS);
    CHECK(rec.LastDoor()->at.x == -10.0f);

    const std::string text = CoopResume::FileText(*p, rec.LastDoor());
    REQUIRE(CoopResume::FromFile(text, "sala1", "Zelda"));  // the v1 line, the d1 one skipped
    CHECK(CoopResume::FromFile(text, "sala1", "Zelda")->at.x == -51.5f);
    const auto door = CoopResume::DoorFromFile(text, "sala1", "Zelda");
    REQUIRE(door);
    CHECK(door->scene == "InsideTheDekuTree");
    CHECK(door->start == "Entrance");
    CHECK(door->at.z == 8.0f);
    CHECK(door->age == proto::Age::child);
    CHECK_FALSE(CoopResume::DoorFromFile(text, "sala1", "Saria"));
    CHECK_FALSE(CoopResume::Parse(CoopResume::FormatDoor("sala1", "Zelda", *door)));  // never a place line
    CHECK_FALSE(CoopResume::DoorFromFile("d1;sala1;Zelda;InsideTheDekuTree;Entrance;x;1;2;child;5\n", "sala1", "Zelda"));
    CHECK(CoopResume::FileText(*p).find("d1;") == std::string::npos);  // no door, no line
}

TEST_CASE("resume dungeon: a place in another room travels to the door, or the entrance without one") {
    const CoopRooms::Table rooms = DekuRooms();
    Now here = Standing("KokiriForest_Child", 90.0f, 115.5f, -27.9f);
    here.rooms = &rooms;
    const Place slingshot = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f);
    auto d = CoopResume::Decide(slingshot, here, DoorSpot());
    CHECK(d.what == Decision::What::travel);
    CHECK(d.start == "Entrance");
    CHECK(d.place);
    CHECK(d.door);
    CHECK(d.at.x == -10.0f);
    CHECK(d.log.find("another room of this dungeon") != std::string::npos);
    CHECK(d.log.find("issue 197") != std::string::npos);
    // No door kept (another PC, an old file), or one of another dungeon or age: the entrance.
    for (const auto& door : {std::optional<CoopResume::Door>{}, std::optional<CoopResume::Door>{[] {
                                 auto o = DoorSpot();
                                 o.scene = "DodongoCavern";
                                 return o;
                             }()},
                             std::optional<CoopResume::Door>{[] {
                                 auto o = DoorSpot();
                                 o.age = proto::Age::adult;
                                 return o;
                             }()}}) {
        d = CoopResume::Decide(slingshot, here, door);
        CHECK(d.what == Decision::What::travel);
        CHECK_FALSE(d.place);
        CHECK(d.door);
        CHECK(d.log.find("no spot of its entrance room kept, going to its entrance (Entrance)") != std::string::npos);
    }
    // A place in the entrance room itself: straight there, as before.
    d = CoopResume::Decide(Kept("InsideTheDekuTree", -8.0f, 101.0f, 5.0f), here, DoorSpot());
    CHECK(d.place);
    CHECK_FALSE(d.door);
    CHECK(d.at.x == -8.0f);
    // No room rule given (now.rooms unset): the old decision.
    here.rooms = nullptr;
    d = CoopResume::Decide(slingshot, here, DoorSpot());
    CHECK(d.place);
    CHECK_FALSE(d.door);
    CHECK(d.at.x == -51.5f);
}

TEST_CASE("resume dungeon: the same dungeon moves to the door of our room, or stays") {
    const CoopRooms::Table rooms = DekuRooms();
    Now here = Standing("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);  // the room's save put us at the entrance
    here.rooms = &rooms;
    const Place slingshot = Kept("InsideTheDekuTree", -51.5f, 112.6f, 15.0f);
    auto d = CoopResume::Decide(slingshot, here, DoorSpot());
    CHECK(d.what == Decision::What::move);
    CHECK(d.door);
    CHECK(d.at.x == -10.0f);
    CHECK(d.notice.find("otra sala") != std::string::npos);
    d = CoopResume::Decide(slingshot, here, std::nullopt);
    CHECK(d.what == Decision::What::none);
    CHECK(d.door);
    CHECK(d.log.find("no spot of this room kept, staying here") != std::string::npos);
    Now at_door = Standing("InsideTheDekuTree", -10.5f, 101.0f, 8.0f);
    at_door.rooms = &rooms;
    d = CoopResume::Decide(slingshot, at_door, DoorSpot());
    CHECK(d.what == Decision::What::none);
    CHECK(d.log.find("already at the spot") != std::string::npos);
    // The Returner hands its door to the decision.
    Returner r(slingshot, DoorSpot());
    CHECK_FALSE(r.Frame(here, kS));
    const auto back = r.Frame(here, kS + Returner::kSteadyUs);
    REQUIRE(back);
    CHECK(back->what == Decision::What::move);
    CHECK(back->at.x == -10.0f);
}

TEST_CASE("resume dungeon trip: the door after the load, or the entrance, said in the log") {
    Trip trip;
    trip.Resume("InsideTheDekuTree", "Entrance", Vec{-10.0f, 101.0f, 8.0f}, true, 0, true);
    const Local arrived = At("InsideTheDekuTree", 0.2f, 101.0f, 14.7f);
    trip.Tick(arrived, kS);
    trip.Tick(arrived, 2 * kS);
    auto a = trip.Tick(arrived, 2 * kS + Trip::kSettleUs);
    REQUIRE(a.move);
    CHECK(a.move->x == -10.0f);
    CHECK(a.resume);
    CHECK(a.lines.empty());
    CHECK(Has(a.log, "spot of the entrance room of InsideTheDekuTree we last stood on"));
    CHECK(a.notice.find("sala de entrada") != std::string::npos);

    Trip entrance;
    entrance.Resume("InsideTheDekuTree", "Entrance", Vec{-51.5f, 112.6f, 15.0f}, false, 0, true);
    entrance.Tick(arrived, kS);
    entrance.Tick(arrived, 2 * kS);
    a = entrance.Tick(arrived, 2 * kS + Trip::kSettleUs);
    CHECK_FALSE(a.move);
    CHECK(Has(a.log, "another room of this dungeon, no spot of its entrance room kept"));
    CHECK(a.notice == "De vuelta a la entrada de la mazmorra: tu sitio está en otra sala");
}
