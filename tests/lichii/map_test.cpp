// Tests for the ORM partner map's portable part (lichii/hook/coop_map.cpp):
// place names, floors, the table, world to map, and the edge pointers.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <string>
#include <vector>

#include "coop_map.h"

namespace map = CoopMap;
namespace ov = coop::overlay;

namespace {

// A camera at (0, 2, 10) looking at the origin, 1600 x 900.
ov::Camera TestCamera() {
    ov::Camera c;
    c.view = ov::look_at({0.0f, 2.0f, 10.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    c.projection = ov::perspective(1.0f, 1600.0f / 900.0f, 0.1f, 1000.0f);
    c.viewport_width = 1600.0f;
    c.viewport_height = 900.0f;
    return c;
}

ov::Shown Mate(std::uint8_t n, std::string name, ov::Vec3 at, std::string scene = "KokiriForest_Child") {
    ov::Shown s;
    s.number = n;
    s.name = std::move(name);
    s.position = at;
    s.scene = std::move(scene);
    return s;
}

} // namespace

TEST_CASE("map: place names in Spanish, unknown ones as words") {
    CHECK(map::PlaceName("KokiriForest_Child") == "Bosque Kokiri");
    CHECK(map::PlaceName("KakarikoVillage_Adult_Alternative") == "Aldea Kakariko");
    CHECK(map::PlaceName("InsideTheDekuTree") == "Gran \xC3\x81rbol Deku");
    CHECK(map::PlaceName("LakeHylia_Fishing2Pond") == "Lake Hylia Fishing2 Pond");
}

TEST_CASE("map: floors from the bottoms, labels as the game's map") {
    const map::Floors deku{-2, {45.0f, 73.0f, 93.5f, 106.5f, 118.0f}};
    CHECK(map::FloorOf(deku, 52.8f) == -2);
    CHECK(map::FloorOf(deku, 10.0f) == -2);  // under the lowest
    CHECK(map::FloorOf(deku, 103.4f) == 0);
    CHECK(map::FloorOf(deku, 120.3f) == 2);
    CHECK_FALSE(map::FloorOf(map::Floors{}, 1.0f));
    CHECK_FALSE(map::FloorOf(deku, std::nanf("")));
    CHECK(map::FloorLabel(0) == "1F");
    CHECK(map::FloorLabel(2) == "3F");
    CHECK(map::FloorLabel(-1) == "B1");
}

TEST_CASE("map: the table's lines, and bad ones named") {
    const auto p = map::Parse(
        "# comment\n"
        "floors InsideTheDekuTree -2 45 73 93.5\r\n"
        "mini * follow 60\n"
        "mini InsideTheDekuTree 0 10 120 90 mirror\n"
        "pause InsideTheDekuTree -29 32 125\n"
        "screen mini 0.8 0.7 0.18 0.28 round\n"
        "screen pause 0.1 0.1 0.8 0.8\n"
        "notes 0.02 0.9\n"
        "floors Bad 0 10 5\n"
        "pause X follow 10\n"
        "screen mini 0.9 0.9 0.2 0.2\n"
        "what 1\n");
    CHECK(p.errors.size() == 4);
    CHECK(p.errors[0].rfind("line 9:", 0) == 0);
    const auto& t = p.table;
    REQUIRE(t.FloorsFor("InsideTheDekuTree") != nullptr);
    CHECK(t.FloorsFor("InsideTheDekuTree")->bottoms.size() == 3);
    REQUIRE(t.ViewFor(map::Which::mini, "HyruleField_Child") != nullptr);
    CHECK(t.ViewFor(map::Which::mini, "HyruleField_Child")->follow);
    const auto* deku = t.ViewFor(map::Which::mini, "InsideTheDekuTree");
    REQUIRE(deku != nullptr);
    CHECK(deku->mirror);
    CHECK(deku->rotation == doctest::Approx(3.14159265f / 2.0f));
    CHECK(t.ViewFor(map::Which::pause, "HyruleField_Child") == nullptr);
    REQUIRE(t.ScreenFor(map::Which::mini) != nullptr);
    CHECK(t.ScreenFor(map::Which::mini)->round);
    CHECK(t.notes_bottom == doctest::Approx(0.9f));
}

TEST_CASE("map: world to map, centred, rotated, mirrored, and kept on the rim") {
    const map::Screen square{0.25f, 0.0f, 0.5f, 1.0f, false};  // on a 2:1 picture: a square
    map::View v;
    v.span = 100.0f;  // 100 units across the map's height
    auto at = map::ToMap(v, square, 2.0f, {0.0f, 0.0f, 0.0f}, std::nullopt);
    REQUIRE(at);
    CHECK(at->x == doctest::Approx(0.5f));
    CHECK(at->y == doctest::Approx(0.5f));
    at = map::ToMap(v, square, 2.0f, {25.0f, 0.0f, -25.0f}, std::nullopt);  // right and north
    REQUIRE(at);
    CHECK(at->x == doctest::Approx(0.625f));
    CHECK(at->y == doctest::Approx(0.25f));
    CHECK_FALSE(at->edge);
    v.rotation = 3.14159265f / 2.0f;  // a quarter turn clockwise: +x goes down
    at = map::ToMap(v, square, 2.0f, {25.0f, 0.0f, 0.0f}, std::nullopt);
    CHECK(at->x == doctest::Approx(0.5f));
    CHECK(at->y == doctest::Approx(0.75f));
    v.rotation = 0.0f;
    v.mirror = true;
    at = map::ToMap(v, square, 2.0f, {25.0f, 0.0f, 0.0f}, std::nullopt);
    CHECK(at->x == doctest::Approx(0.375f));
    v.mirror = false;
    at = map::ToMap(v, square, 2.0f, {500.0f, 0.0f, 0.0f}, std::nullopt);  // far right
    REQUIRE(at);
    CHECK(at->edge);
    CHECK(at->angle == doctest::Approx(0.0f));
    CHECK(at->x == doctest::Approx(0.25f + 0.5f * (0.5f + 0.92f / 2.0f)));
    // Following: centred on us; without us, nothing.
    v.follow = true;
    at = map::ToMap(v, square, 2.0f, {10.0f, 0.0f, 0.0f}, map::Vec3{10.0f, 5.0f, 0.0f});
    CHECK(at->x == doctest::Approx(0.5f));
    CHECK_FALSE(map::ToMap(v, square, 2.0f, {10.0f, 0.0f, 0.0f}, std::nullopt));
    CHECK_FALSE(map::ToMap(map::View{}, square, 2.0f, {0.0f, 0.0f, 0.0f}, std::nullopt));  // no span
}

TEST_CASE("map: the rim of a box and of an ellipse") {
    auto p = map::Rim(0.0f, 0.0f, 2.0f, 1.0f, 1.0f, 1.0f, false);
    CHECK(p[0] == doctest::Approx(1.0f));
    CHECK(p[1] == doctest::Approx(1.0f));
    p = map::Rim(0.0f, 0.0f, 2.0f, 1.0f, 1.0f, 0.0f, true);
    CHECK(p[0] == doctest::Approx(2.0f));
    p = map::Rim(5.0f, 5.0f, 2.0f, 1.0f, 0.0f, 0.0f, false);
    CHECK(p[0] == 5.0f);
}

TEST_CASE("map: pointers for mates the tags leave out: behind, off screen, far") {
    const auto c = TestCamera();
    const map::Vec3 own{0.0f, 0.0f, 5.0f};
    const std::vector<ov::Shown> mates{
        Mate(2, "Ana", {0.0f, 0.0f, 30.0f}),        // behind the camera
        Mate(3, "Luis", {200.0f, 0.0f, 0.0f}),      // far off to the right
        Mate(4, "Eva", {0.0f, 2.0f, -80.0f}),       // in view, far
        Mate(5, "Sol", {1.0f, 0.0f, 0.0f}, "Market_Child"),  // elsewhere
        Mate(6, "Teo", {0.0f, 0.0f, 0.0f}),         // the overlay draws him
    };
    const auto out = map::Pointers(c, "KokiriForest_Adult", own, mates, {6});
    REQUIRE(out.size() == 3);
    // Behind: along the bottom edge, pointing down, tag above it.
    CHECK(out[0].number == 2);
    CHECK(out[0].edge);
    CHECK(out[0].x == doctest::Approx(800.0f));
    CHECK(out[0].y > 800.0f);
    CHECK(out[0].arrow_angle == doctest::Approx(3.14159265f / 2.0f));
    CHECK(out[0].tag == "Ana 25 m");
    CHECK(out[0].tag_y < out[0].y);
    // Off to the right: on the right edge, pointing right-ish.
    CHECK(out[1].number == 3);
    CHECK(out[1].edge);
    CHECK(out[1].x > 1500.0f);
    CHECK(std::abs(out[1].arrow_angle) < 0.2f);
    // In view, far: a dot where he is, no arrow.
    CHECK(out[2].number == 4);
    CHECK_FALSE(out[2].edge);
    CHECK(out[2].x == doctest::Approx(800.0f));
    CHECK(out[2].y == doctest::Approx(450.0f));
    CHECK(out[2].tag == "Eva 85 m");
    // A downed mate's note replaces the distance; a bad camera gives nothing.
    auto down = Mate(2, "Ana", {0.0f, 0.0f, 30.0f});
    down.urgent = true;
    down.note = "caido, 25 m";
    const auto d = map::Pointers(c, "KokiriForest_Child", own, {down}, {});
    REQUIRE(d.size() == 1);
    CHECK(d[0].tag == "Ana (caido, 25 m)");
    CHECK(d[0].urgent);
    auto bad = c;
    bad.viewport_width = 0.0f;
    CHECK(map::Pointers(bad, "KokiriForest_Child", own, mates, {}).empty());
}

TEST_CASE("map: a tag the in-game HUD would not draw (fading far away) gives way to a dot") {
    std::vector<CoopHook::Marker> items(4);
    items[0].number = 2;  // near: a solid tag
    items[0].alpha = 1.0f;
    items[1].number = 3;  // about 31 m: fading, under the HUD's kMinAlpha
    items[1].alpha = 0.3f;
    items[2].number = 4;  // an edge arrow stays as it is
    items[2].alpha = 0.3f;
    items[2].edge = true;
    items[3].number = 0;  // ourselves (debug camera) stay
    items[3].alpha = 0.2f;
    const auto drawn = map::KeepSolid(items);
    CHECK(drawn == std::vector<std::uint8_t>{2, 4, 0});
    REQUIRE(items.size() == 3);
    CHECK(items[1].number == 4);
    // The faded one then gets his dot and tag from the pointers.
    const auto c = TestCamera();
    const auto out = map::Pointers(c, "KokiriForest_Child", map::Vec3{0.0f, 0.0f, 5.0f},
                                   {Mate(3, "Luis", {0.0f, 2.0f, -21.0f})}, drawn);
    REQUIRE(out.size() == 1);
    CHECK(out[0].number == 3);
    CHECK_FALSE(out[0].edge);
    CHECK(out[0].tag == "Luis 26 m");
}

TEST_CASE("map: dots on the pause map with floors, and the elsewhere lines") {
    map::Table t = map::Parse("floors WaterTemple -1 85 97 108.7\n"
                              "pause WaterTemple 0 0 100\n"
                              "screen pause 0 0 1 1\n")
                       .table;
    map::Input in;
    in.scene = "WaterTemple";
    in.own = map::Vec3{0.0f, 100.0f, 0.0f};  // floor 0 (1F)
    in.picture_aspect = 1.0f;
    in.which = map::Which::pause;
    CoopHook::Mate a{2, "Ana", {10.0f, 100.0f, 0.0f}, std::string("WaterTemple"), {255, 0, 0}, false};
    CoopHook::Mate b{3, "Luis", {0.0f, 110.0f, 0.0f}, std::string("WaterTemple"), {0, 255, 0}, false};
    CoopHook::Mate e{4, "", {0.0f, 0.0f, 0.0f}, std::string("ZoraRiver_Adult"), {0, 0, 0}, false};
    in.mates = {a, b, e};
    in.self = true;
    const auto dots = map::Dots(t, in);
    REQUIRE(dots.size() == 3);
    CHECK(dots[0].label == "Ana");
    CHECK(dots[0].x == doctest::Approx(0.6f));
    CHECK(dots[0].rgb == 0xff0000u);
    CHECK_FALSE(dots[0].other_floor);
    CHECK(dots[1].label == "Luis 2F");
    CHECK(dots[1].other_floor);
    CHECK(dots[2].self);
    in.which = map::Which::mini;  // no minimap view or box: nothing
    CHECK(map::Dots(t, in).empty());
    const auto notes = map::Elsewhere(in);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].text == "P4: en R\xC3\xADo Zora");
    CHECK(notes[0].rgb == OrmHud::kWhite);
    in.scene.reset();
    CHECK(map::Elsewhere(in).empty());
}

TEST_CASE("map: the HUD draws map dots and notes inside the picture") {
    OrmHud::Model m;
    m.map_dots.push_back({0.5f, 0.5f, 0xff0000u});
    m.notes.push_back({"Ana: en Lago Hylia", 0x00ff00u});
    const auto rects = OrmHud::Draw(m, {0, 0, 400, 300}, 400, 300);
    bool red = false, green = false;
    for (const auto& r : rects) {
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
        CHECK(r.x + r.width <= 400);
        CHECK(r.y + r.height <= 300);
        red = red || r.rgb == 0xff0000u;
        green = green || r.rgb == 0x00ff00u;
    }
    CHECK(red);
    CHECK(green);
    CHECK_FALSE(m.Empty());
}

// (2026-11-11, D-R70): the partners show on the map unless the player
// turns it off with LICHII_COOP_MAP=0.
TEST_CASE("map: on by default, off only with 0") {
    CHECK(map::OnFrom(nullptr));
    CHECK(map::OnFrom(""));
    CHECK(map::OnFrom("1"));
    CHECK(map::OnFrom("yes"));
    CHECK_FALSE(map::OnFrom("0"));
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): the dots' path
// in a scene, for its check line.
TEST_CASE("map (riesgos-visual): the path the dots take in a scene, or why there are none") {
    auto parsed = CoopMap::Parse("screen mini 0.8 0.1 0.4 0.3\n"  // past the right edge: left out by Parse
                                 "screen pause 0.1 0.1 0.8 0.8\n"
                                 "mini HyruleField 10 20 300\n"
                                 "pause HyruleField 10 20 900\n");
    CHECK(parsed.errors.size() == 1);
    const auto& t = parsed.table;
    const auto pause = CoopMap::PathFor(t, CoopMap::Which::pause, "HyruleField");
    CHECK(pause.ok);
    CHECK(pause.detail.rfind("pause map dots by the table: centre 10, 20, span 900, box 0.1 0.1 0.8 0.8", 0) == 0);
    const auto mini = CoopMap::PathFor(t, CoopMap::Which::mini, "HyruleField");
    CHECK_FALSE(mini.ok);
    CHECK(mini.detail == "the map table has no minimap box: edge arrows and lines only");
    const auto none = CoopMap::PathFor(t, CoopMap::Which::pause, "KokiriForest_Child");
    CHECK_FALSE(none.ok);
    CHECK(none.detail == "the map table has no pause map view for KokiriForest_Child: edge arrows and lines only");
    CoopMap::Table empty;
    CHECK(CoopMap::PathFor(empty, CoopMap::Which::mini, "HyruleField").detail ==
          "no minimap view or box in the map table (coop_map.txt): edge arrows and lines only");
}
