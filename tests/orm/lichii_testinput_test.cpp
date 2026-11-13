// ORM cases moved out of upstream's tests/lichii/testinput_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the test-input script parser and stepper
// (lichii/hook/coop_testinput.cpp). No emulator code.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "coop_testinput.h"

using namespace CoopTestInput;

namespace {

Script Must(const char* text) {
    std::string error;
    auto s = Parse(text, error);
    REQUIRE_MESSAGE(s, error);
    return *s;
}

}  // namespace

TEST_CASE("testinput: a warp's last word adult or child asks that age (ORM)") {
    std::string error;
    CHECK_FALSE(Parse("warp A B C adult\n", error));      // one word too many
    CHECK_FALSE(Parse("warp A B grown\n", error));        // not an age: three words
    Runner r(Must("warp WaterTemple WaterTemple_FromLakeHylia adult\n"
                  "warp ForestTemple CHILD\n"
                  "warp KakarikoVillage_Adult Kakariko_WellSide\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 3);
    CHECK(requests[0].name == "WaterTemple");
    CHECK(requests[0].start == "WaterTemple_FromLakeHylia");
    CHECK(requests[0].child == std::optional<bool>(false));
    CHECK(requests[1].name == "ForestTemple");
    CHECK(requests[1].start.empty());
    CHECK(requests[1].child == std::optional<bool>(true));
    CHECK(requests[2].start == "Kakariko_WellSide");
    CHECK_FALSE(requests[2].child.has_value());  // the scene's name says it
}

TEST_CASE("testinput: life takes a number of quarter hearts") {
    std::string error;
    CHECK_FALSE(Parse("life\n", error));
    CHECK_FALSE(Parse("life -1\n", error));
    CHECK_FALSE(Parse("life 1000\n", error));
    CHECK_FALSE(Parse("life 4 5\n", error));
    CHECK_FALSE(Parse("life x\n", error));
    Runner r(Must("life 0\nmark dead\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::life);
    CHECK(requests[0].name == "0");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "mark dead");
}

TEST_CASE("testinput: gate takes 0 (open) or 1 (closed)") {
    std::string error;
    CHECK_FALSE(Parse("gate\n", error));
    CHECK_FALSE(Parse("gate 2\n", error));
    CHECK_FALSE(Parse("gate 1 1\n", error));
    CHECK_FALSE(Parse("gate on\n", error));
    Runner r(Must("gate 1\nlife 0\ngate 0\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 3);
    CHECK(requests[0].kind == Kind::gate);
    CHECK(requests[0].name == "1");
    CHECK(requests[1].kind == Kind::life);
    CHECK(requests[2].kind == Kind::gate);
    CHECK(requests[2].name == "0");
}

TEST_CASE("testinput: bottle takes a slot A to E and a BottleItem name or restore") {
    std::string error;
    CHECK_FALSE(Parse("bottle\n", error));
    CHECK_FALSE(Parse("bottle A\n", error));
    CHECK_FALSE(Parse("bottle F fairy\n", error));
    CHECK_FALSE(Parse("bottle AB fairy\n", error));
    CHECK_FALSE(Parse("bottle A pixie\n", error));
    CHECK_FALSE(Parse("bottle A fairy 1\n", error));
    CHECK(BottleValue("fairy") == "Fairy");
    CHECK(BottleValue("REDPOTION") == "RedPotion");
    CHECK(BottleValue("Restore") == "restore");
    CHECK_FALSE(BottleValue("Fairies"));
    Runner r(Must("bottle a fairy\ngate 0\nbottle E restore\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 3);
    CHECK(requests[0].kind == Kind::bottle);
    CHECK(requests[0].name == "A");
    CHECK(requests[0].start == "Fairy");
    CHECK(requests[1].kind == Kind::gate);
    CHECK(requests[2].kind == Kind::bottle);
    CHECK(requests[2].name == "E");
    CHECK(requests[2].start == "restore");
}

TEST_CASE("testinput: age takes adult or child (ORM D-R17)") {
    std::string error;
    CHECK_FALSE(Parse("age\n", error));
    CHECK_FALSE(Parse("age teen\n", error));
    CHECK_FALSE(Parse("age adult now\n", error));
    Runner r(Must("age ADULT\nage child\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].kind == Kind::age);
    CHECK(requests[0].name == "adult");
    CHECK(requests[1].name == "child");
}

TEST_CASE("testinput: event takes one event name") {
    std::string error;
    CHECK_FALSE(Parse("event\n", error));
    CHECK_FALSE(Parse("event A B\n", error));
    CHECK_FALSE(Parse("event Bad-Name\n", error));
    CHECK_FALSE(Parse("event " + std::string(128, 'a') + "\n", error));
    CHECK(Parse("event " + std::string(127, 'a') + "\n", error));
    Runner r(Must("mark start\nevent KokiriForest_Meet_DekuTree_MeetDekuTree_01\nmark asked\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::event);
    CHECK(requests[0].name == "KokiriForest_Meet_DekuTree_MeetDekuTree_01");
    CHECK(log == std::vector<std::string>{"mark start", "mark asked"});
}

TEST_CASE("testinput: skip takes no more words and is a request (ORM, ZMP D-048)") {
    std::string error;
    CHECK_FALSE(Parse("skip now\n", error));
    CHECK(Parse("skip\n", error));
    CHECK(Parse("SKIP\n", error));
    Runner r(Must("mark start\nskip\nwait 30\nskip\nmark skipped\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::skip);
    r.Advance(1.0, std::nullopt, log);
    requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::skip);
    CHECK(log == std::vector<std::string>{"mark start", "mark skipped"});
}

TEST_CASE("testinput: int takes a flag name or hash and a decimal value (ORM objetos-uso)") {
    std::string error;
    CHECK_FALSE(Parse("int\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP x\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP 1.5\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP -\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP 2000000\n", error));
    CHECK_FALSE(Parse("int PlayerStatus.MP 48 more\n", error));
    CHECK(Parse("int 0x76b18200 -3\n", error));
    Runner r(Must("int PlayerStatus.MPMax 96\nint PouchData.Item_Bombchu.StockNum 20\nmark set\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].kind == Kind::int_flag);
    CHECK(requests[0].name == "PlayerStatus.MPMax");
    CHECK(requests[0].start == "96");
    CHECK(requests[1].name == "PouchData.Item_Bombchu.StockNum");
    CHECK(requests[1].start == "20");
    CHECK(log == std::vector<std::string>{"mark set"});
}

TEST_CASE("testinput: flag takes a flag name or hash and 0 or 1 (ORM)") {
    std::string error;
    CHECK_FALSE(Parse("flag\n", error));
    CHECK_FALSE(Parse("flag Some.Flag\n", error));
    CHECK_FALSE(Parse("flag Some.Flag 2\n", error));
    CHECK_FALSE(Parse("flag Some.Flag 1 more\n", error));
    CHECK(Parse("flag 0xef98d829 0\n", error));
    Runner r(Must("flag HavePlayedEvent.KokiriForest_Meet_DekuTree_MeetDekuTree_01 1\nmark set\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::flag);
    CHECK(requests[0].name == "HavePlayedEvent.KokiriForest_Meet_DekuTree_MeetDekuTree_01");
    CHECK(requests[0].start == "1");
    CHECK(log == std::vector<std::string>{"mark set"});
}

TEST_CASE("testinput: key takes a 64-bit key in hex and 0 or 1 (ORM)") {
    std::string error;
    CHECK_FALSE(Parse("key\n", error));
    CHECK_FALSE(Parse("key 0x9f7d0ebfa5357dbb\n", error));
    CHECK_FALSE(Parse("key 9f7d0ebfa5357dbb 1\n", error));         // no 0x
    CHECK_FALSE(Parse("key 0x 1\n", error));                       // no digits
    CHECK_FALSE(Parse("key 0x19f7d0ebfa5357dbb 1\n", error));      // 17 digits
    CHECK_FALSE(Parse("key 0x9f7d0ebfa5357dbg 1\n", error));       // not hex
    CHECK_FALSE(Parse("key 0x9f7d0ebfa5357dbb 2\n", error));
    CHECK_FALSE(Parse("key 0x9f7d0ebfa5357dbb 1 more\n", error));
    CHECK(Parse("key 0X1 0\n", error));
    Runner r(Must("key 0x9f7d0ebfa5357dbb 1\nmark set\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].kind == Kind::key64);
    CHECK(requests[0].name == "0x9f7d0ebfa5357dbb");
    CHECK(requests[0].start == "1");
    CHECK(log == std::vector<std::string>{"mark set"});
}

TEST_CASE("testinput: enum takes an Enum flag and an optional value to queue (ORM)") {
    std::string error;
    CHECK_FALSE(Parse("enum\n", error));
    CHECK_FALSE(Parse("enum Step_Meet_DekuTree Complete more\n", error));
    CHECK(Parse("enum 0x3372689b\n", error));
    Runner r(Must("enum Step_Meet_DekuTree\nenum Step_Get_StoneOfForest GoInside\nmark read\n"));
    std::vector<std::string> log;
    r.Advance(0.0, std::nullopt, log);
    const auto requests = r.TakeRequests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].kind == Kind::enum_flag);
    CHECK(requests[0].name == "Step_Meet_DekuTree");
    CHECK(requests[0].start.empty());
    CHECK(requests[1].kind == Kind::enum_flag);
    CHECK(requests[1].name == "Step_Get_StoneOfForest");
    CHECK(requests[1].start == "GoInside");
    CHECK(log == std::vector<std::string>{"mark read"});
}

TEST_CASE("test input: a warp in free play starts a scene event after 2 s, every 6 s, three times at most") {
    using CoopTestInput::WarpStartsEvent;
    // Asked at 10 s: not before 12 s.
    CHECK_FALSE(WarpStartsEvent(11.9, 10.0, -1.0, 0, true, false));
    CHECK(WarpStartsEvent(12.0, 10.0, -1.0, 0, true, false));
    // An event runs: its end takes the warp; nothing of ours.
    CHECK_FALSE(WarpStartsEvent(20.0, 10.0, -1.0, 0, true, true));
    // Loading (Link not read): wait.
    CHECK_FALSE(WarpStartsEvent(20.0, 10.0, -1.0, 0, false, false));
    // Again 6 s after the last one, three in all.
    CHECK_FALSE(WarpStartsEvent(17.9, 10.0, 12.0, 1, true, false));
    CHECK(WarpStartsEvent(18.0, 10.0, 12.0, 1, true, false));
    CHECK(WarpStartsEvent(24.0, 10.0, 18.0, 2, true, false));
    CHECK_FALSE(WarpStartsEvent(60.0, 10.0, 24.0, CoopTestInput::kWarpEvents, true, false));
}
