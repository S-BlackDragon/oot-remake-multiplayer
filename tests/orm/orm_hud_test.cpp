// Tests for the in-game HUD's portable part (lichii/hook/orm_hud.cpp): the
// model from the hook's markers and player list, and its layout as rectangles.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "orm_hud.h"
#include "coop_hud_font.h"
#include "coop_notices.h"  // ORM "Guardado" (Corner)

namespace hud = OrmHud;

namespace {

constexpr std::uint32_t kNone = 0xff000000;  // a pixel nothing was drawn on

// The rectangles painted in order onto a w x h picture.
std::vector<std::uint32_t> Paint(const std::vector<hud::Rect>& rects, int w, int h) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, kNone);
    for (const auto& r : rects) {
        for (int y = r.y; y < r.y + r.height; ++y) {
            for (int x = r.x; x < r.x + r.width; ++x) {
                REQUIRE(x >= 0);
                REQUIRE(y >= 0);
                REQUIRE(x < w);
                REQUIRE(y < h);
                px[static_cast<std::size_t>(y) * w + x] = r.rgb;
            }
        }
    }
    return px;
}

bool GlyphBit(char32_t cp, int x, int y) {
    const auto& g = CoopHudFont::kGlyphs[cp - CoopHudFont::kFirst];
    return (g[y] >> (CoopHudFont::kWidth - 1 - x)) & 1;
}

hud::Tag OneTag(std::string name, float x, float y) {
    hud::Tag t;
    t.number = 2;
    t.name = std::move(name);
    t.x = x;
    t.y = y;
    t.marker_x = x;
    t.marker_y = y;
    t.head = true;  // a name tag over the head: no dot
    return t;
}

hud::Model OneTagModel(std::string name, float x, float y, float w = 200, float h = 100) {
    hud::Model m;
    m.width = w;
    m.height = h;
    m.tags.push_back(OneTag(std::move(name), x, y));
    return m;
}

CoopHook::Marker Marker(std::uint8_t number, std::string tag, float x, float y) {
    CoopHook::Marker m;
    m.number = number;
    m.tag = std::move(tag);
    m.x = x;
    m.y = y;
    m.tag_x = x;
    m.tag_y = y - 10;
    m.tag_head = true;
    m.size = 12;
    return m;
}

int Count(const std::vector<std::uint32_t>& px, std::uint32_t rgb) {
    int n = 0;
    for (const auto p : px) {
        n += p == rgb;
    }
    return n;
}

} // namespace

TEST_CASE("hud: a switch is on unless empty, missing or 0") {
    CHECK(hud::SwitchOn("1"));
    CHECK(hud::SwitchOn("yes"));
    CHECK_FALSE(hud::SwitchOn("0"));
    CHECK_FALSE(hud::SwitchOn(""));
    CHECK_FALSE(hud::SwitchOn(nullptr));
}

TEST_CASE("hud: F9 toggles the panel") {
    hud::SetPanelShown(false);
    hud::TogglePanel();
    CHECK(hud::PanelShown());
    hud::TogglePanel();
    CHECK_FALSE(hud::PanelShown());
}

TEST_CASE("hud: UTF-8 names are decoded, broken bytes become U+FFFD") {
    CHECK(hud::Decode("Link") == U"Link");
    CHECK(hud::Decode("\xc3\x91" "and\xc3\xba") == U"Ñandú");  // Nandu, with tilde and accent
    CHECK(hud::Decode("a\xff" "b") == U"a�b");
    CHECK(hud::Decode("a\xc3") == U"a�");             // cut off
    CHECK(hud::Decode("\xc0\x80") == U"��");     // overlong
    CHECK(hud::Decode("\xe4\xb8\xad") == U"中");
    CHECK(hud::Decode("\xf0\x9f\x98\x80") == U"\U0001f600");
}

TEST_CASE("hud: the text grows with the picture in whole font pixels") {
    CHECK(hud::TagScale(720) == 2);
    CHECK(hud::TagScale(1080) == 3);
    CHECK(hud::TagScale(1440) == 4);
    CHECK(hud::TagScale(2160) == 6);
    CHECK(hud::TagScale(100) == 1);
    CHECK(hud::TagScale(0) == 1);
    CHECK(hud::TagScale(100000) == 8);
    CHECK(hud::PanelScale(720) == 1);
    CHECK(hud::PanelScale(1080) == 2);
    CHECK(hud::TextWidth("\xc3\x91" "and\xc3\xba", 2) == 5 * CoopHudFont::kWidth * 2);
}

TEST_CASE("hud: the font has ASCII and the Latin-1 letters") {
    for (char32_t cp = 33; cp <= 126; ++cp) {
        const auto& g = CoopHudFont::kGlyphs[cp - CoopHudFont::kFirst];
        bool any = false;
        for (const auto row : g) {
            any = any || row != 0;
            CHECK(row < (1u << CoopHudFont::kWidth));
        }
        CHECK_MESSAGE(any, "codepoint ", static_cast<int>(cp));
    }
    for (const char32_t cp : {U'Ñ', U'ñ', U'á', U'é', U'ü', U'Ç'}) {
        const auto& g = CoopHudFont::kGlyphs[cp - CoopHudFont::kFirst];
        bool any = false;
        for (const auto row : g) {
            any = any || row != 0;
        }
        CHECK_MESSAGE(any, "codepoint ", static_cast<int>(cp));
    }
}

TEST_CASE("hud: the model takes each marker's name, colour and ping") {
    CoopHook::Markers markers;
    markers.width = 1920;
    markers.height = 1080;
    markers.items.push_back(Marker(3, "Saria", 100, 200));
    markers.items.push_back(Marker(2, "Darunia", 900, 500));
    markers.items[1].color = {0xff, 0x80, 0x00};
    markers.items[1].alpha = 0.7f;
    std::vector<CoopHook::PlayerRow> rows;
    rows.push_back({1, "Link", true, "connected", 112, std::string("Kokiri")});
    rows.push_back({2, "Darunia", false, "connected", 45, std::string("Kokiri")});
    rows.push_back({3, "Saria", false, "no samples", std::nullopt, std::string("Kokiri")});
    const std::vector<CoopHook::OverlayLine> lines{{"Co-op: player 1, ping 112 ms", false},
                                                   {"P3 Saria: other scene", true}};

    const auto m = hud::Build(markers, rows, lines, false);
    CHECK(m.width == 1920);
    CHECK(m.height == 1080);
    REQUIRE(m.tags.size() == 2);
    CHECK(m.tags[0].number == 3);
    CHECK(m.tags[0].name == "Saria");
    CHECK(m.tags[0].rgb == hud::kWhite);  // the hook gave no colour (all zero)
    CHECK_FALSE(m.tags[0].ping_ms);
    CHECK(m.tags[0].x == 100);
    CHECK(m.tags[0].y == 190);
    CHECK(m.tags[0].head);
    CHECK(m.tags[1].name == "Darunia");
    CHECK(m.tags[1].rgb == 0xff8000);
    CHECK(m.tags[1].ping_ms == 45u);
    CHECK(m.tags[1].alpha == doctest::Approx(0.7f));
    CHECK(hud::PingText(m.tags[1]) == "~45 ms");
    CHECK(hud::PingText(m.tags[0]).empty());
    CHECK(m.panel.empty());  // the panel is hidden

    const auto shown = hud::Build(markers, rows, lines, true);
    REQUIRE(shown.panel.size() == 2);
    CHECK(shown.panel[0].text == "Co-op: player 1, ping 112 ms");
    CHECK(shown.panel[1].dim);
}

TEST_CASE("hud: yourself (debug camera) gets our round trip, without ~") {
    CoopHook::Markers markers;
    markers.width = 1280;
    markers.height = 720;
    markers.items.push_back(Marker(0, "you", 10, 10));
    markers.items[0].self = true;
    std::vector<CoopHook::PlayerRow> rows{{1, "Link", true, "connected", 30, std::nullopt}};
    const auto m = hud::Build(markers, rows, {}, false);
    REQUIRE(m.tags.size() == 1);
    CHECK(m.tags[0].self);
    CHECK(m.tags[0].rgb == hud::kSelf);
    CHECK(hud::PingText(m.tags[0]) == "30 ms");
}

TEST_CASE("hud: no camera, no tags, and nothing at all is empty") {
    const auto m = hud::Build(std::nullopt, {}, {}, true);
    CHECK(m.tags.empty());
    CHECK(m.Empty());
    CoopHook::Markers bad;
    bad.width = std::numeric_limits<float>::quiet_NaN();
    bad.height = 720;
    bad.items.push_back(Marker(2, "x", 1, 1));
    CHECK(hud::Build(bad, {}, {}, false).tags.empty());
    CHECK(hud::Draw(hud::Model{}, {0, 0, 640, 360}, 640, 360).empty());
}

TEST_CASE("hud: a tag is the font's glyphs, outlined, with its bottom centre at the anchor") {
    // 100 px tall: scale 1, outline 1.
    const auto rects = hud::Draw(OneTagModel("A", 100, 50), {0, 0, 200, 100}, 200, 100);
    const auto px = Paint(rects, 200, 100);
    const int left = 97;  // 100 - 3.5, rounded away from zero
    const int top = 50 - CoopHudFont::kHeight;
    int white = 0;
    for (int y = 0; y < CoopHudFont::kHeight; ++y) {
        for (int x = 0; x < CoopHudFont::kWidth; ++x) {
            const auto p = px[static_cast<std::size_t>(top + y) * 200 + left + x];
            if (GlyphBit('A', x, y)) {
                CHECK(p == hud::kWhite);
                ++white;
            } else {
                CHECK(p != hud::kWhite);
            }
        }
    }
    CHECK(white == Count(px, hud::kWhite));
    // Every white pixel's neighbours are white or outline.
    for (int y = 1; y < 99; ++y) {
        for (int x = 1; x < 199; ++x) {
            if (px[static_cast<std::size_t>(y) * 200 + x] != hud::kWhite) {
                continue;
            }
            for (const auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const auto q = px[static_cast<std::size_t>(y + dy) * 200 + x + dx];
                CHECK((q == hud::kWhite || q == hud::kOutline));
            }
        }
    }
    // The outline comes first.
    bool fill_seen = false;
    for (const auto& r : rects) {
        if (r.rgb == hud::kWhite) {
            fill_seen = true;
        } else {
            CHECK_FALSE(fill_seen);
        }
    }
}

TEST_CASE("hud: the ping follows the name in grey") {
    auto m = OneTagModel("Link", 100, 50);
    m.tags[0].ping_ms = 7;
    const auto px = Paint(hud::Draw(m, {0, 0, 200, 100}, 200, 100), 200, 100);
    CHECK(Count(px, hud::kWhite) > 0);
    CHECK(Count(px, hud::kPing) > 0);
    // "Link ~7 ms": the grey is right of the white.
    int white_right = 0, grey_left = 200;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            const auto p = px[static_cast<std::size_t>(y) * 200 + x];
            if (p == hud::kWhite) white_right = std::max(white_right, x);
            if (p == hud::kPing) grey_left = std::min(grey_left, x);
        }
    }
    CHECK(grey_left > white_right);
}

TEST_CASE("hud: tags follow the scene target into the picture's place in the frame") {
    // A 1600x900 picture in a 1920x900 frame (bars left and right), from a
    // 1920x1080 scene target: the middle of the scene is the middle of the frame.
    const auto m = OneTagModel("I", 960, 540, 1920, 1080);
    const auto rects = hud::Draw(m, {160, 0, 1600, 900}, 1920, 900);
    REQUIRE_FALSE(rects.empty());
    int x0 = 1920, x1 = 0, y1 = 0;
    for (const auto& r : rects) {
        CHECK(r.x >= 160);
        CHECK(r.x + r.width <= 1760);
        if (r.rgb == hud::kWhite) {
            x0 = std::min(x0, r.x);
            x1 = std::max(x1, r.x + r.width);
            y1 = std::max(y1, r.y + r.height);
        }
    }
    CHECK(std::abs((x0 + x1) / 2 - 960) <= 2 * 3);  // scale 2 (900 px tall)
    CHECK(y1 <= 450);
    CHECK(y1 >= 450 - 3 * CoopHudFont::kHeight);
}

TEST_CASE("hud: a tag stays on the picture and nothing is drawn outside it") {
    auto m = OneTagModel("Ganondorf", 199, 50);
    m.tags.push_back(OneTag("Zelda", -50, 3));  // off the left and the top
    const auto rects = hud::Draw(m, {0, 0, 200, 100}, 200, 100);
    int right = 0;
    for (const auto& r : rects) {
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
        CHECK(r.x + r.width <= 200);
        CHECK(r.y + r.height <= 100);
        if (r.rgb == hud::kWhite) right = std::max(right, r.x + r.width);
    }
    CHECK(right <= 200 - 2);
    // A frame smaller than the picture clips to the frame.
    for (const auto& r : hud::Draw(m, {0, 0, 200, 100}, 120, 60)) {
        CHECK(r.x + r.width <= 120);
        CHECK(r.y + r.height <= 60);
    }
}

TEST_CASE("hud: nearer tags cover farther ones") {
    hud::Model m;
    m.width = 200;
    m.height = 100;
    m.tags.push_back(OneTag("MMMM", 100, 50));  // far
    m.tags.push_back(OneTag("MMMM", 102, 52));  // near, over it
    m.tags[0].rgb = 0xff0000;
    m.tags[1].rgb = 0x00ff00;
    const auto px = Paint(hud::Draw(m, {0, 0, 200, 100}, 200, 100), 200, 100);
    // The near tag is drawn whole.
    const auto near_only = Paint(hud::Draw([&] {
        auto n = m;
        n.tags.erase(n.tags.begin());
        return n;
    }(), {0, 0, 200, 100}, 200, 100), 200, 100);
    for (std::size_t i = 0; i < px.size(); ++i) {
        if (near_only[i] == 0x00ff00) {
            CHECK(px[i] == 0x00ff00);
        }
    }
}

TEST_CASE("hud: faded or broken tags are left out") {
    auto m = OneTagModel("Link", 100, 50);
    m.tags[0].alpha = 0.3f;
    CHECK(hud::Draw(m, {0, 0, 200, 100}, 200, 100).empty());
    m.tags[0].alpha = 1.0f;
    m.tags[0].x = std::numeric_limits<float>::infinity();
    CHECK(hud::Draw(m, {0, 0, 200, 100}, 200, 100).empty());
}

TEST_CASE("hud: an edge arrow and a dot are drawn at the marker") {
    auto m = OneTagModel("Ruto", 100, 40);
    m.tags[0].edge = true;
    m.tags[0].arrow_angle = 0.0f;  // pointing right
    m.tags[0].marker_x = 180;
    m.tags[0].marker_y = 50;
    m.tags[0].marker_size = 20;
    auto px = Paint(hud::Draw(m, {0, 0, 200, 100}, 200, 100), 200, 100);
    CHECK(px[50 * 200 + 180] == hud::kMarker);
    CHECK(px[50 * 200 + 187] == hud::kMarker);   // towards the tip
    CHECK(px[50 * 200 + 195] != hud::kMarker);   // past it

    m.tags[0].edge = false;
    m.tags[0].head = false;
    m.tags[0].rgb = 0x2040ff;  // a player colour: the dot takes it
    px = Paint(hud::Draw(m, {0, 0, 200, 100}, 200, 100), 200, 100);
    // r = max(3, 20 / 2) = 10, its outline 1 px more.
    CHECK(px[50 * 200 + 185] == 0x2040ff);
    CHECK(px[50 * 200 + 190] == hud::kOutline);
    CHECK(px[50 * 200 + 192] == kNone);
}

TEST_CASE("hud: a downed mate is drawn in orange-red, name and marker") {
    CoopHook::Markers markers;
    markers.width = 200;
    markers.height = 100;
    markers.items.push_back(Marker(2, "Ruto", 100, 50));
    markers.items[0].color = {0x20, 0x40, 0xff};
    markers.items[0].urgent = true;
    markers.items[0].tag_head = false;  // with its dot
    markers.items[0].size = 20;
    const auto m = hud::Build(markers, {}, {}, false);
    REQUIRE(m.tags.size() == 1);
    CHECK(m.tags[0].urgent);
    CHECK(m.tags[0].rgb == hud::kUrgent);
    const auto px = Paint(hud::Draw(m, {0, 0, 200, 100}, 200, 100), 200, 100);
    CHECK(px[50 * 200 + 105] == hud::kMarkerUrgent);
    CHECK(Count(px, hud::kUrgent) > 0);
    CHECK(Count(px, 0x2040ff) == 0);

    markers.items[0].urgent = false;
    const auto again = hud::Build(markers, {}, {}, false);
    CHECK(again.tags[0].rgb == 0x2040ff);  // its own colour once revived
}

TEST_CASE("hud: a character the font lacks is drawn as ?") {
    const auto a = hud::Draw(OneTagModel("\xe4\xb8\xad", 100, 50), {0, 0, 200, 100}, 200, 100);
    const auto b = hud::Draw(OneTagModel("?", 100, 50), {0, 0, 200, 100}, 200, 100);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].x == b[i].x);
        CHECK(a[i].y == b[i].y);
        CHECK(a[i].width == b[i].width);
        CHECK(a[i].height == b[i].height);
    }
}

TEST_CASE("hud: the panel is in the top left, dim lines grey") {
    hud::Model m;
    m.panel.push_back({"Co-op: player 1", false});
    m.panel.push_back({"P2 Saria: other scene", true});
    const auto rects = hud::Draw(m, {0, 0, 1280, 720}, 1280, 720);
    REQUIRE_FALSE(rects.empty());
    const auto px = Paint(rects, 1280, 720);
    CHECK(Count(px, hud::kWhite) > 0);
    CHECK(Count(px, hud::kDim) > 0);
    for (const auto& r : rects) {
        CHECK(r.x >= 16 - 1);  // margin 720 / 45 = 16, less the outline
        CHECK(r.y >= 16 - 1);
        CHECK(r.y + r.height <= 16 + 2 * (CoopHudFont::kHeight + 1) + 1);
    }
}

TEST_CASE("hud: panel lines take the player's colour, darker when dim") {
    const std::vector<CoopHook::OverlayLine> lines{
        {"Co-op: player 1", false, std::array<std::uint8_t, 3>{0xff, 0x00, 0x00}},
        {"P2 Saria: other scene", true, std::array<std::uint8_t, 3>{0x00, 0xff, 0x00}},
        {"P3 Ruto", false, std::nullopt}};
    const auto m = hud::Build(std::nullopt, {}, lines, true);
    REQUIRE(m.panel.size() == 3);
    CHECK(m.panel[0].rgb == 0xff0000u);
    CHECK(m.panel[1].rgb == 0x00ff00u);
    CHECK_FALSE(m.panel[2].rgb);
    const auto px = Paint(hud::Draw(m, {0, 0, 1280, 720}, 1280, 720), 1280, 720);
    CHECK(Count(px, 0xff0000) > 0);
    CHECK(Count(px, 0x00aa00) > 0);  // 0xff * 170 / 255
    CHECK(Count(px, 0x00ff00) == 0);
    CHECK(Count(px, hud::kWhite) > 0);
}

TEST_CASE("hud: never more than kMaxRects rectangles") {
    hud::Model m;
    m.width = 1920;
    m.height = 1080;
    for (int i = 0; i < 40; ++i) {
        m.tags.push_back(OneTag("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", 960, 540));
    }
    CHECK(hud::Draw(m, {0, 0, 1920, 1080}, 1920, 1080).size() <= hud::kMaxRects);
}

// ORM cofre (coop_notices.h): "<nombre> encontró <objeto>", centred in the
// top middle, the name in the player's colour and the rest white.
TEST_CASE("hud: a find's notice is centred near the top, the name in its colour (cofre)") {
    hud::Model m;
    hud::Notice n;
    n.number = 2;
    n.name = "Ana";
    n.rest = " encontró una llave pequeña";
    n.rgb = 0x40c0ff;
    m.notices.push_back(n);
    CHECK_FALSE(m.Empty());  // drawn with no tag or panel
    const int w = 640, h = 360;
    const auto px = Paint(hud::Draw(m, {0, 0, w, h}, w, h), w, h);
    CHECK(Count(px, 0x40c0ff) > 0);
    CHECK(Count(px, hud::kWhite) > 0);
    int x0 = w, x1 = 0, y0 = h, name_right = 0, white_left = w;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto p = px[static_cast<std::size_t>(y) * w + x];
            if (p == 0x40c0ff || p == hud::kWhite) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
            }
            if (p == 0x40c0ff) name_right = std::max(name_right, x);
            if (p == hud::kWhite) white_left = std::min(white_left, x);
        }
    }
    CHECK(name_right < white_left);  // the name first
    CHECK(std::abs((x0 + x1) / 2 - w / 2) <= 8);  // centred
    CHECK(y0 >= h * 12 / 100 - 2);  // from 12 % down
    CHECK(y0 <= h / 4);
    // Two notices: the second under the first.
    m.notices.push_back(n);
    const auto two = hud::Draw(m, {0, 0, w, h}, w, h);
    int bottom = 0;
    for (const auto& r : two) bottom = std::max(bottom, r.y + r.height);
    CHECK(bottom > y0 + 2 * CoopHudFont::kHeight * hud::TagScale(h) - 4);
}

// ---- ORM quick messages and chat in the picture (docs/COMUNICACION.md) ----

namespace {

CoopHook::ChatLine ChatFrom(std::uint8_t player, std::string name, std::string body, bool quick,
                            std::uint64_t age_ms, std::optional<std::array<std::uint8_t, 3>> color) {
    CoopHook::ChatLine l;
    l.text = name + ": " + body;
    l.age_ms = age_ms;
    l.player = player;
    l.name = std::move(name);
    l.quick = quick;
    l.color = color;
    return l;
}

CoopHook::ChatLine Notice(std::string text, std::uint64_t age_ms) {
    CoopHook::ChatLine l;
    l.text = std::move(text);
    l.notice = true;
    l.age_ms = age_ms;
    return l;
}

const std::string kHereText = "\xc2\xa1" "Aqu\xc3\xad" "!";
constexpr std::array<std::uint8_t, 3> kPurple{0xa0, 0x5c, 0xe0};
constexpr std::array<std::uint8_t, 3> kGreen{0x2e, 0xb8, 0x3c};

} // namespace

TEST_CASE("hud: chat rows: the name in the sender's colour, a quick message all in it, notices yellow") {
    const std::vector<CoopHook::ChatLine> lines{
        Notice("Ana joined", 9000),  // older than 8 s: gone
        Notice("Ana ha caido", 100),
        ChatFrom(2, "Ana", kHereText, true, 200, kPurple),
        ChatFrom(1, "you", "hola", false, 300, kGreen),
        ChatFrom(3, "Saria", "sin color", false, 400, std::nullopt),
    };
    const auto rows = hud::BuildChat(lines);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].name.empty());
    CHECK(rows[0].text == "Ana ha caido");
    CHECK(rows[0].text_rgb == hud::kNotice);
    CHECK(rows[1].name == "Ana");
    CHECK(rows[1].text == ": " + kHereText);
    CHECK(rows[1].name_rgb == 0xa05ce0u);
    CHECK(rows[1].text_rgb == 0xa05ce0u);  // quick: in his colour
    CHECK(rows[2].name == "you");
    CHECK(rows[2].name_rgb == 0x2eb83cu);
    CHECK(rows[2].text_rgb == hud::kWhite);  // typed: white
    CHECK(rows[3].name_rgb == hud::kWhite);  // no colour known
    // A line that does not start with its name is drawn whole, white.
    auto odd = ChatFrom(2, "Ana", "x", false, 0, kPurple);
    odd.text = "something else";
    const auto r = hud::BuildChat({odd});
    REQUIRE(r.size() == 1);
    CHECK(r[0].name.empty());
    CHECK(r[0].text == "something else");
}

TEST_CASE("hud: at most the six newest chat rows") {
    std::vector<CoopHook::ChatLine> lines;
    for (int i = 0; i < 8; ++i) {
        lines.push_back(ChatFrom(2, "Ana", std::to_string(i), false, 100, kPurple));
    }
    const auto rows = hud::BuildChat(lines);
    REQUIRE(rows.size() == hud::kChatLines);
    CHECK(rows.front().text == ": 2");
    CHECK(rows.back().text == ": 7");
    hud::Model m;
    m.chat = rows;
    CHECK_FALSE(m.Empty());
}

TEST_CASE("hud: a quick message's marker is in the sender's colour, with no ping and never downed") {
    CoopHook::Markers markers;
    markers.width = 1920;
    markers.height = 1080;
    markers.items.push_back(Marker(2, "Ana", 500, 500));
    markers.items.push_back(Marker(2, kHereText + " Ana", 500, 460));
    markers.items[1].ping = true;
    markers.items[1].urgent = true;  // even if it were set
    markers.items[1].color = kPurple;
    std::vector<CoopHook::PlayerRow> rows{{1, "Link", true, "connected", 30, std::nullopt},
                                          {2, "Ana", false, "connected", 45, std::nullopt}};
    const auto m = hud::Build(markers, rows, {}, false);
    REQUIRE(m.tags.size() == 2);
    CHECK_FALSE(m.tags[0].ping);
    CHECK(m.tags[0].ping_ms == 45u);
    CHECK(m.tags[1].ping);
    CHECK_FALSE(m.tags[1].ping_ms);
    CHECK_FALSE(m.tags[1].urgent);
    CHECK(m.tags[1].rgb == 0xa05ce0u);
    CHECK(m.tags[1].name == kHereText + " Ana");
    CHECK(hud::PingText(m.tags[1]).empty());
    const auto px = Paint(hud::Draw(m, {0, 0, 1920, 1080}, 1920, 1080), 1920, 1080);
    CHECK(Count(px, 0xa05ce0) > 0);
}

TEST_CASE("hud: chat rows are in the bottom left, the newest lowest, in their colours") {
    hud::Model m;
    m.chat.push_back({"Ana", ": hola", 0xa05ce0, hud::kWhite});
    m.chat.push_back({"", "Ana ha caido", hud::kWhite, hud::kNotice});
    const auto rects = hud::Draw(m, {0, 0, 1280, 720}, 1280, 720);
    REQUIRE_FALSE(rects.empty());
    const auto px = Paint(rects, 1280, 720);
    CHECK(Count(px, 0xa05ce0) > 0);
    CHECK(Count(px, hud::kWhite) > 0);
    CHECK(Count(px, hud::kNotice) > 0);
    // Scale 1 at 720 lines, margin 16, a row 14 px: the two rows end 16 px over the bottom.
    const int line = CoopHudFont::kHeight + 1;
    for (const auto& r : rects) {
        CHECK(r.x >= 16 - 1);
        CHECK(r.y >= 720 - 16 - 2 * line - 1);
        CHECK(r.y + r.height <= 720 - 16 + 1);
    }
    // The notice (the newest) is the lower row.
    int notice_top = 720, name_top = 720;
    for (const auto& r : rects) {
        if (r.rgb == hud::kNotice) notice_top = std::min(notice_top, r.y);
        if (r.rgb == 0xa05ce0u) name_top = std::min(name_top, r.y);
    }
    CHECK(notice_top >= name_top + line - 1);
}

TEST_CASE("hud: a chat row too long for the picture is cut with dots, never drawn outside") {
    hud::Model m;
    m.chat.push_back({"Ana", ": " + std::string(300, 'M'), 0xa05ce0, hud::kWhite});
    const auto rects = hud::Draw(m, {0, 0, 200, 100}, 200, 100);
    REQUIRE_FALSE(rects.empty());
    Paint(rects, 200, 100);  // every rectangle inside the frame
    for (const auto& r : rects) {
        CHECK(r.x + r.width <= 200 - 4 + 1);  // margin 4 (100 / 45 is less), outline 1
    }
    // 27 glyphs fit (192 / 7): "Ana", ": " and 19 M, then "..." (in the last run's white).
    const auto same = hud::Draw([] {
        hud::Model c;
        c.chat.push_back({"Ana", ": " + std::string(19, 'M') + "...", 0xa05ce0, hud::kWhite});
        return c;
    }(), {0, 0, 200, 100}, 200, 100);
    CHECK(Paint(rects, 200, 100) == Paint(same, 200, 100));
}

TEST_CASE("hud: the chat box being open is known to the HUD") {
    hud::SetTyping(true);
    CHECK(hud::Typing());
    hud::SetTyping(false);
    CHECK_FALSE(hud::Typing());
}

// ORM "Guardado" (coop_notices.h Corner; oot-64 D-054): one white word in the
// bottom right corner, at the panel's size, only while shown.
TEST_CASE("hud: Guardado is drawn in the bottom right corner (room save sent)") {
    hud::Model m;
    m.corner = "Guardado";
    CHECK_FALSE(m.Empty());
    const int w = 640, h = 360;
    const auto px = Paint(hud::Draw(m, {0, 0, w, h}, w, h), w, h);
    REQUIRE(Count(px, hud::kWhite) > 0);
    int x0 = w, x1 = 0, y0 = h, y1 = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (px[static_cast<std::size_t>(y) * w + x] == hud::kWhite) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    CHECK(x0 > w / 2);       // right half
    CHECK(y0 > h * 3 / 4);   // bottom quarter
    CHECK(x1 < w);
    CHECK(y1 < h);
    CHECK(w - x1 <= std::max(4, h / 45) + CoopHudFont::kWidth);  // against the right margin
    hud::Model none;
    CHECK(none.Empty());
    CHECK(hud::Draw(none, {0, 0, w, h}, w, h).empty());
}

TEST_CASE("hud: the Guardado corner shows for 2 s, the newest word only") {
    CoopNotices::Corner c;
    CHECK(c.Current(0).empty());
    c.Show(CoopNotices::kSavedText, 1000, CoopNotices::kSavedMs);
    CHECK(c.Current(1000) == "Guardado");
    CHECK(c.Current(2999) == "Guardado");
    CHECK(c.Current(3000).empty());
    c.Show(CoopNotices::kSavedText, 5000, CoopNotices::kSavedMs);
    c.Show("Otra", 6000, 500);
    CHECK(c.Current(6100) == "Otra");
    CHECK(c.Current(6500).empty());
}

// ORM the room's age (coop_notices.h Banner, 2026-11-13, oot-remake
// docs/EDAD.md): the big notice in the middle of the picture, larger than the
// other notices, split after ": " when too wide, its detail line under it.
TEST_CASE("hud: the room's age banner is big and centred, split in two, the countdown under it") {
    hud::Model m;
    hud::Banner b;
    b.number = 2;
    b.name = "Ana";
    b.rest = " ha cambiado de época: moviendo al grupo al Templo del Tiempo...";
    b.detail = "3";
    b.rgb = 0x40c0ff;
    m.banner = b;
    CHECK_FALSE(m.Empty());
    const int w = 1280, h = 720;
    CHECK(hud::BannerScale(h) > hud::TagScale(h));
    const auto px = Paint(hud::Draw(m, {0, 0, w, h}, w, h), w, h);
    REQUIRE(Count(px, 0x40c0ff) > 0);
    REQUIRE(Count(px, hud::kWhite) > 0);
    REQUIRE(Count(px, hud::kNotice) > 0);
    int x0 = w, x1 = 0, y0 = h, white_bottom = 0, detail_top = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto p = px[static_cast<std::size_t>(y) * w + x];
            if (p == 0x40c0ff || p == hud::kWhite) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
            }
            if (p == hud::kWhite) white_bottom = std::max(white_bottom, y);
            if (p == hud::kNotice) detail_top = std::min(detail_top, y);
        }
    }
    const int scale = hud::BannerScale(h);
    CHECK(y0 >= h * 30 / 100 - 2);  // from 30 % down: under the find notices
    CHECK(std::abs((x0 + x1) / 2 - w / 2) <= 3 * CoopHudFont::kWidth * scale);  // centred (two lines)
    CHECK(white_bottom - y0 > 2 * CoopHudFont::kHeight * scale - CoopHudFont::kHeight);  // two lines
    CHECK(detail_top > white_bottom - scale);  // the countdown under them
    CHECK(x1 < w);

    // Narrow: smaller to fit, never off the picture.
    const int nw = 320, nh = 720;
    const auto narrow = hud::Draw(m, {0, 0, nw, nh}, nw, nh);
    for (const auto& r : narrow) {
        CHECK(r.x >= 0);
        CHECK(r.x + r.width <= nw);
    }
}

TEST_CASE("hud: the banner shows the newest notice for its time, the name kept apart") {
    CoopNotices::Banner banner;
    CHECK_FALSE(banner.Current(0));
    banner.Show(2, "Ana", "Ana ha cambiado de época", "3", 1000, 1200);
    auto now = banner.Current(1500);
    REQUIRE(now);
    CHECK(now->number == 2);
    CHECK(now->name == "Ana");
    CHECK(now->rest == " ha cambiado de época");
    CHECK(now->detail == "3");
    banner.Show(0, "", "Grupo reunido en el Templo del Tiempo", "", 1600, 5000);
    now = banner.Current(1700);
    REQUIRE(now);
    CHECK(now->name.empty());
    CHECK(now->rest == "Grupo reunido en el Templo del Tiempo");
    CHECK_FALSE(banner.Current(6600));
    banner.Show(3, "Bo", "Esperando a Bo para cambiar de época...", "", 7000, 5000);
    CHECK(banner.Current(7000)->name.empty());  // the name not at the start: all of it is the rest
}
