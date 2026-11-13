// Tests for each player's own HUD settings (lichii/hook/coop_hud_settings.cpp;
// oot-remake docs/HUD_AJUSTES.md) and what the HUD does with them and with
// our pause (OrmHud::Apply, the rupee line and the places in OrmHud::Draw).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "orm_hud.h"
#include "coop_hud_font.h"
#include "coop_hud_settings.h"

namespace hud = OrmHud;

namespace {

// An environment from a table, as CoopSwitch::Value reads one.
hud::EnvReader EnvOf(std::map<std::string, std::string> values) {
    return [values = std::move(values)](const char* name) -> std::optional<std::string> {
        const auto it = values.find(name);
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

CoopHook::PlayerRow Row(std::uint8_t number, std::string name, std::optional<std::uint32_t> rupees,
                        std::array<std::uint8_t, 3> color) {
    CoopHook::PlayerRow r;
    r.number = number;
    r.name = std::move(name);
    r.self = number == 1;
    r.rupees = rupees;
    r.color = color;
    return r;
}

std::vector<CoopHook::PlayerRow> Two() {
    return {Row(1, "Ana", 87, {0x10, 0x20, 0x30}), Row(2, "Saria", 43, {0x40, 0x50, 0x60})};
}

hud::Wallet W(std::string name, std::optional<std::uint32_t> rupees, std::uint32_t rgb = 0x102030) {
    hud::Wallet w;
    w.name = std::move(name);
    w.rupees = rupees;
    w.rgb = rgb;
    return w;
}

struct Bounds {
    std::int32_t x0 = std::numeric_limits<std::int32_t>::max(), y0 = std::numeric_limits<std::int32_t>::max();
    std::int32_t x1 = std::numeric_limits<std::int32_t>::min(), y1 = std::numeric_limits<std::int32_t>::min();
    int n = 0;
};

// Where the rectangles of one colour are.
Bounds Of(const std::vector<hud::Rect>& rects, std::uint32_t rgb) {
    Bounds b;
    for (const auto& r : rects) {
        if (r.rgb != rgb) {
            continue;
        }
        b.x0 = std::min(b.x0, r.x);
        b.y0 = std::min(b.y0, r.y);
        b.x1 = std::max(b.x1, r.x + r.width);
        b.y1 = std::max(b.y1, r.y + r.height);
        ++b.n;
    }
    return b;
}

// The glyph cells (counted from `left`, `cell` pixels wide) a colour's rectangles start in.
std::vector<std::int32_t> Cells(const std::vector<hud::Rect>& rects, std::uint32_t rgb, std::int32_t left,
                                std::int32_t cell) {
    std::vector<std::int32_t> out;
    for (const auto& r : rects) {
        if (r.rgb == rgb) {
            const std::int32_t c = (r.x - left) / cell;
            if (std::find(out.begin(), out.end(), c) == out.end()) {
                out.push_back(c);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

constexpr hud::Box kPicture{0, 0, 1920, 1080};
// At 1080p the panel's text is 2 font pixels: a cell 14 wide, a line 28 high;
// the margin 1080 / 45 = 24.
constexpr int kCell = CoopHudFont::kWidth * 2;
constexpr int kLine = (CoopHudFont::kHeight + 1) * 2;
constexpr int kMargin = 24;

} // namespace

TEST_CASE("hud settings: a place by its Spanish or English name, any case and separator") {
    CHECK(hud::ParsePlace("arriba-izquierda") == hud::Place::top_left);
    CHECK(hud::ParsePlace("Arriba Derecha") == hud::Place::top_right);
    CHECK(hud::ParsePlace("ABAJO_IZQUIERDA") == hud::Place::bottom_left);
    CHECK(hud::ParsePlace(" abajo ") == hud::Place::bottom);
    CHECK(hud::ParsePlace("top") == hud::Place::top);
    CHECK(hud::ParsePlace("bottom-right") == hud::Place::bottom_right);
    CHECK_FALSE(hud::ParsePlace("centro").has_value());
    CHECK_FALSE(hud::ParsePlace("").has_value());
    for (const auto p : {hud::Place::top_left, hud::Place::top, hud::Place::top_right, hud::Place::bottom_left,
                         hud::Place::bottom, hud::Place::bottom_right}) {
        CHECK(hud::ParsePlace(hud::PlaceName(p)) == p);
    }
    CHECK(hud::PlaceName(hud::Place::top_right) == "arriba-derecha");
}

TEST_CASE("hud settings: a size is a whole percent from 50 to 300, a number of chat lines 0 to 6") {
    CHECK(hud::ParseSize("120") == 120);
    CHECK(hud::ParseSize(" 80 % ") == 80);
    CHECK(hud::ParseSize("50") == 50);
    CHECK(hud::ParseSize("300%") == 300);
    CHECK_FALSE(hud::ParseSize("49").has_value());
    CHECK_FALSE(hud::ParseSize("301").has_value());
    CHECK_FALSE(hud::ParseSize("-100").has_value());
    CHECK_FALSE(hud::ParseSize("12x").has_value());
    CHECK_FALSE(hud::ParseSize("").has_value());
    CHECK_FALSE(hud::ParseSize("%").has_value());
    CHECK(hud::ParseChatLines("0") == 0u);
    CHECK(hud::ParseChatLines("6") == 6u);
    CHECK_FALSE(hud::ParseChatLines("7").has_value());
    CHECK_FALSE(hud::ParseChatLines("-1").has_value());
    CHECK_FALSE(hud::ParseChatLines("dos").has_value());
}

TEST_CASE("hud settings: on and off take the switches' words and si") {
    CHECK(hud::ParseOnOff("on") == true);
    CHECK(hud::ParseOnOff("1") == true);
    CHECK(hud::ParseOnOff("Si") == true);
    CHECK(hud::ParseOnOff("s\xc3\xad") == true);
    CHECK(hud::ParseOnOff("S\xc3\x8d") == true);
    CHECK(hud::ParseOnOff("OFF") == false);
    CHECK(hud::ParseOnOff("no") == false);
    CHECK_FALSE(hud::ParseOnOff("quizas").has_value());
    CHECK_FALSE(hud::ParseOnOff("").has_value());
}

TEST_CASE("hud settings: from coop-options.ini, the defaults when unset or empty") {
    std::vector<std::string> log;
    CHECK(hud::ReadSettings(EnvOf({}), &log) == hud::Settings{});
    CHECK(hud::ReadSettings(EnvOf({{"LICHII_ORM_HUD_SIZE", ""}, {"LICHII_ORM_HUD_NAMES", "  "}}), &log) ==
          hud::Settings{});
    CHECK(hud::ReadSettings(nullptr, &log) == hud::Settings{});
    CHECK(log.empty());
    const hud::Settings d;
    CHECK(d.names);
    CHECK(d.rupees);
    CHECK(d.rupees_at == hud::Place::top);
    CHECK(d.panel_at == hud::Place::top_left);
    CHECK(d.size == 100);
    CHECK(d.chat_lines == 6u);
}

TEST_CASE("hud settings: every line of coop-options.ini is read") {
    std::vector<std::string> log;
    const hud::Settings s = hud::ReadSettings(EnvOf({{"LICHII_ORM_HUD_NAMES", "0"},
                                                     {"LICHII_ORM_HUD_RUPEES", "no"},
                                                     {"LICHII_ORM_HUD_RUPEES_AT", "abajo-derecha"},
                                                     {"LICHII_ORM_HUD_PANEL_AT", "top-right"},
                                                     {"LICHII_ORM_HUD_SIZE", "150"},
                                                     {"LICHII_ORM_HUD_CHAT_LINES", "3"}}),
                                              &log);
    CHECK(log.empty());
    CHECK_FALSE(s.names);
    CHECK_FALSE(s.rupees);
    CHECK(s.rupees_at == hud::Place::bottom_right);
    CHECK(s.panel_at == hud::Place::top_right);
    CHECK(s.size == 150);
    CHECK(s.chat_lines == 3u);
}

TEST_CASE("hud settings: a value not taken keeps the default and says so in the log") {
    std::vector<std::string> log;
    const hud::Settings s = hud::ReadSettings(EnvOf({{"LICHII_ORM_HUD_NAMES", "maybe"},
                                                     {"LICHII_ORM_HUD_RUPEES_AT", "middle"},
                                                     {"LICHII_ORM_HUD_PANEL_AT", "x"},
                                                     {"LICHII_ORM_HUD_SIZE", "1000"},
                                                     {"LICHII_ORM_HUD_CHAT_LINES", "9"}}),
                                              &log);
    CHECK(s == hud::Settings{});
    REQUIRE(log.size() == 5);
    CHECK(log[0] == "coop: switch LICHII_ORM_HUD_NAMES=maybe is not 0/1/true/false/yes/no/on/off: on (its default)");
    CHECK(log[1].find("LICHII_ORM_HUD_RUPEES_AT=middle") != std::string::npos);
    CHECK(log[1].find("arriba-derecha") != std::string::npos);  // what it takes
    CHECK(log[1].find(": arriba (its default)") != std::string::npos);
    CHECK(log[2].find(": arriba-izquierda (its default)") != std::string::npos);
    CHECK(log[3] == "coop: switch LICHII_ORM_HUD_SIZE=1000 is not a percent from 50 to 300: 100 (its default)");
    CHECK(log[4] == "coop: switch LICHII_ORM_HUD_CHAT_LINES=9 is not 0 to 6: 6 (its default)");
    // No log asked for: the same settings, nothing else.
    CHECK(hud::ReadSettings(EnvOf({{"LICHII_ORM_HUD_SIZE", "1000"}}), nullptr) == hud::Settings{});
}

TEST_CASE("hud settings: the line /hud answers with") {
    CHECK(hud::Describe(hud::Settings{}) ==
          "HUD: nombres s\xc3\xad, rupias s\xc3\xad (arriba), panel arriba-izquierda, tama\xc3\xb1o 100 %, chat 6 "
          "l\xc3\xadneas");
    hud::Settings s;
    s.names = false;
    s.rupees = false;
    s.panel_at = hud::Place::bottom;
    s.size = 75;
    s.chat_lines = 1;
    CHECK(hud::Describe(s) ==
          "HUD: nombres no, rupias no, panel abajo, tama\xc3\xb1o 75 %, chat 1 l\xc3\xadnea");
}

TEST_CASE("hud settings: which chat lines are our commands") {
    CHECK(hud::IsCommand("/rupias"));
    CHECK(hud::IsCommand("  /RUPIAS off"));
    CHECK(hud::IsCommand("/nombres"));
    CHECK(hud::IsCommand("/hud tamano 120"));
    CHECK_FALSE(hud::IsCommand("rupias off"));     // chat
    CHECK_FALSE(hud::IsCommand("/rupiasx"));
    CHECK_FALSE(hud::IsCommand("/teleport Ana"));  // another command
    CHECK_FALSE(hud::IsCommand("/"));
    CHECK_FALSE(hud::IsCommand(""));
    CHECK(hud::kCommandNames.size() == 3);
    hud::Settings s;
    CHECK_FALSE(hud::Command("hola", s, {}).has_value());
    CHECK_FALSE(hud::Command("/teleport Ana", s, {}).has_value());
    CHECK(s == hud::Settings{});
}

TEST_CASE("hud settings: /rupias turns the rupee line on, off, or moves it") {
    hud::Settings s;
    const hud::Settings file;
    CHECK(hud::Command("/rupias off", s, file) == "HUD: rupias de todos ocultas (el panel F9 las sigue mostrando)");
    CHECK_FALSE(s.rupees);
    CHECK(hud::Command("/rupias", s, file) == "HUD: rupias de todos a la vista (arriba)");  // toggles
    CHECK(s.rupees);
    CHECK(hud::Command("/rupias", s, file).has_value());
    CHECK_FALSE(s.rupees);
    CHECK(hud::Command("/RUPIAS On", s, file).has_value());
    CHECK(s.rupees);
    CHECK(hud::Command("/rupias abajo derecha", s, file) == "HUD: rupias de todos a la vista (abajo-derecha)");
    CHECK(s.rupees_at == hud::Place::bottom_right);
    s.rupees = false;
    CHECK(hud::Command("/hud rupias arriba-izquierda", s, file).has_value());  // a place turns it on too
    CHECK(s.rupees);
    CHECK(s.rupees_at == hud::Place::top_left);
    const hud::Settings before = s;
    const auto bad = hud::Command("/rupias quizas", s, file);
    REQUIRE(bad.has_value());
    CHECK(bad->find("nada cambia") != std::string::npos);
    CHECK(s == before);
}

TEST_CASE("hud settings: /nombres and the /hud subcommands") {
    hud::Settings s;
    hud::Settings file;
    file.size = 130;
    CHECK(hud::Command("/nombres", s, file) == "HUD: nombres ocultos (quedan las flechas y los puntos)");
    CHECK_FALSE(s.names);
    CHECK(hud::Command("/hud nombres si", s, file) == "HUD: nombres sobre los compa\xc3\xb1" "eros a la vista");
    CHECK(s.names);
    CHECK(hud::Command("/hud tamano 150", s, file) == "HUD: tama\xc3\xb1o 150 %");
    CHECK(s.size == 150);
    CHECK(hud::Command("/hud TAMA\xc3\x91O 80%", s, file).has_value());
    CHECK(s.size == 80);
    CHECK(hud::Command("/hud size 40", s, file)->find("nada cambia") != std::string::npos);
    CHECK(s.size == 80);
    CHECK(hud::Command("/hud chat 2", s, file) == "HUD: chat 2 l\xc3\xadneas");
    CHECK(s.chat_lines == 2u);
    CHECK(hud::Command("/hud chat 1", s, file) == "HUD: chat 1 l\xc3\xadnea");
    CHECK(hud::Command("/hud chat 7", s, file)->find("nada cambia") != std::string::npos);
    CHECK(s.chat_lines == 1u);
    CHECK(hud::Command("/hud panel abajo izquierda", s, file) == "HUD: panel (F9) abajo-izquierda");
    CHECK(s.panel_at == hud::Place::bottom_left);
    CHECK(hud::Command("/hud panel", s, file)->find("nada cambia") != std::string::npos);
    CHECK(s.panel_at == hud::Place::bottom_left);
    // /hud alone tells, and changes nothing.
    const hud::Settings before = s;
    CHECK(hud::Command("/hud", s, file) == hud::Describe(s));
    CHECK(s == before);
    // An unknown one: what there is.
    const auto usage = hud::Command("/hud colores", s, file);
    REQUIRE(usage.has_value());
    CHECK(usage->find("/rupias on|off|<sitio>") != std::string::npos);
    CHECK(s == before);
    // Back to coop-options.ini.
    CHECK(hud::Command("/hud reset", s, file) == hud::Describe(file) + " (coop-options.ini)");
    CHECK(s == file);
}

TEST_CASE("hud: a wallet's count as drawn") {
    CHECK(hud::RupeeText(W("Ana", 87)) == "87");
    CHECK(hud::RupeeText(W("Ana", 0)) == "0");
    CHECK(hud::RupeeText(W("Ana", std::nullopt)) == "-");
    hud::Wallet quiet = W("Ana", 87);
    quiet.stale = true;
    CHECK(hud::RupeeText(quiet) == "87?");
    hud::Wallet unknown = W("Ana", std::nullopt);
    unknown.stale = true;  // never "-?"
    CHECK(hud::RupeeText(unknown) == "-");
}

TEST_CASE("hud: over our own pause menu no tag is drawn, the map dots and the rest stay") {
    hud::Model m;
    m.width = 1920;
    m.height = 1080;
    hud::Tag name;
    name.number = 2;
    name.name = "Saria";
    hud::Tag quick;
    quick.number = 3;
    quick.ping = true;
    m.tags = {name, quick};
    m.map_dots.push_back({0.5f, 0.5f});
    m.panel.push_back({"Co-op", false});
    m.chat.push_back({"", "hola"});
    hud::Model paused = m;
    hud::Apply(paused, {}, true, {});
    CHECK(paused.tags.empty());
    CHECK(paused.map_dots.size() == 1);
    CHECK(paused.panel.size() == 1);
    CHECK(paused.chat.size() == 1);
    hud::Model playing = m;
    hud::Apply(playing, {}, false, {});
    CHECK(playing.tags.size() == 2);
    // And Draw has nothing of a tag left: no name glyph over the picture's middle.
    paused.panel.clear();
    paused.chat.clear();
    paused.map_dots.clear();
    CHECK(paused.Empty());
}

TEST_CASE("hud: names off keep the markers, a quick message's marker as it is") {
    hud::Model m;
    hud::Tag t;
    t.number = 2;
    t.name = "Saria";
    t.ping_ms = 45;
    t.edge = true;
    hud::Tag quick;
    quick.number = 3;
    quick.name = "\xc2\xa1" "Aqu\xc3\xad!";
    quick.ping = true;
    m.tags = {t, quick};
    hud::Settings s;
    s.names = false;
    hud::Apply(m, s, false, {});
    REQUIRE(m.tags.size() == 2);
    CHECK(m.tags[0].name.empty());
    CHECK_FALSE(m.tags[0].ping_ms.has_value());
    CHECK(m.tags[0].edge);  // its arrow stays
    CHECK(m.tags[1].name == "\xc2\xa1" "Aqu\xc3\xad!");
    // Drawn: the arrow, no text.
    m.width = 200;
    m.height = 100;
    m.tags.pop_back();
    m.tags[0].marker_x = 190;
    m.tags[0].marker_y = 50;
    m.tags[0].marker_size = 12;
    m.tags[0].rgb = 0x40a040;
    const auto rects = hud::Draw(m, {0, 0, 200, 100}, 200, 100);
    CHECK(Of(rects, 0x40a040).n > 0);
    for (const auto& r : rects) {
        CHECK(r.x >= 150);  // only the arrow at the right edge
    }
}

TEST_CASE("hud: the chat keeps its newest rows") {
    hud::Model m;
    for (int i = 0; i < 6; ++i) {
        m.chat.push_back({"", "line " + std::to_string(i)});
    }
    hud::Settings s;
    s.chat_lines = 2;
    hud::Apply(m, s, false, {});
    REQUIRE(m.chat.size() == 2);
    CHECK(m.chat[0].text == "line 4");
    CHECK(m.chat[1].text == "line 5");
    s.chat_lines = 0;
    hud::Apply(m, s, false, {});
    CHECK(m.chat.empty());
}

TEST_CASE("hud: the rupee line takes the wallets from the panel, never twice") {
    const auto players = Two();
    hud::Model m = hud::Build(std::nullopt, players, {{"Co-op", false}}, true);
    REQUIRE(m.wallets.size() == 2);
    hud::Model on = m;
    hud::Apply(on, {}, false, players);
    REQUIRE(on.rupee_line.size() == 2);
    CHECK(on.wallets.empty());
    CHECK(on.rupee_line[0].name == "Ana");
    CHECK(on.rupee_line[0].rgb == 0x102030u);
    CHECK(on.rupee_line[1].rupees == 43u);
    CHECK(on.rupees_at == hud::Place::top);
    // Off: the panel's list as before, no line.
    hud::Settings off;
    off.rupees = false;
    hud::Model shown = m;
    hud::Apply(shown, off, false, players);
    CHECK(shown.rupee_line.empty());
    CHECK(shown.wallets.size() == 2);
    // Without the panel (F9 off) the line is there all the same.
    hud::Model alone = hud::Build(std::nullopt, players, {}, false);
    CHECK(alone.wallets.empty());
    hud::Apply(alone, {}, false, players);
    CHECK(alone.rupee_line.size() == 2);
    CHECK_FALSE(alone.Empty());
    // No count known yet: no line.
    hud::Model none;
    hud::Apply(none, {}, false, {Row(1, "Ana", std::nullopt, {1, 2, 3})});
    CHECK(none.rupee_line.empty());
    CHECK(none.Empty());
}

TEST_CASE("hud: the size setting scales the text, kept between 50 and 300 percent") {
    CHECK(hud::PanelScale(1080) == 2);
    CHECK(hud::PanelScale(1080, 100) == 2);
    CHECK(hud::PanelScale(1080, 200) == 4);
    CHECK(hud::PanelScale(1080, 50) == 1);
    CHECK(hud::PanelScale(1080, 1000) == hud::PanelScale(1080, 300));
    CHECK(hud::PanelScale(1080, 10) == hud::PanelScale(1080, 50));
    CHECK(hud::TagScale(1080) == 3);
    CHECK(hud::TagScale(1080, 200) == 6);
    // Draw follows it: the line twice as tall.
    hud::Model m;
    m.rupee_line = {W("Ana", 87)};
    const auto normal = Of(hud::Draw(m, kPicture, 1920, 1080), hud::kRupee);
    m.size = 200;
    const auto big = Of(hud::Draw(m, kPicture, 1920, 1080), hud::kRupee);
    CHECK(big.y1 - big.y0 > (normal.y1 - normal.y0) * 3 / 2);
}

TEST_CASE("hud: the rupee line at the top, centred: name, rupee, count, two cells between players") {
    hud::Model m;
    m.rupee_line = {W("Ana", 87, 0x102030), W("Saria", 43, 0x405060)};
    const auto rects = hud::Draw(m, kPicture, 1920, 1080);
    // Ana: 3 + 3 + 2 = 8 cells, Saria 5 + 3 + 2 = 10, 2 between: 20 cells, 280 px.
    const std::int32_t left = (1920 - 20 * kCell) / 2;
    CHECK(Cells(rects, hud::kRupee, left, kCell) == std::vector<std::int32_t>{4, 16});
    CHECK(Cells(rects, 0x102030, left, kCell) == std::vector<std::int32_t>{0, 1, 2});
    CHECK(Cells(rects, 0x405060, left, kCell) == std::vector<std::int32_t>{10, 11, 12, 13, 14});
    CHECK(Cells(rects, hud::kRupeeCount, left, kCell) == std::vector<std::int32_t>{6, 7, 18, 19});
    const auto rupees = Of(rects, hud::kRupee);
    CHECK(rupees.y0 >= kMargin);
    CHECK(rupees.y1 <= kMargin + kLine);
}

TEST_CASE("hud: the rupee line on the right ends at the margin, at the bottom left it goes over the chat") {
    hud::Model m;
    m.rupee_line = {W("Ana", 87, 0x102030)};
    m.rupees_at = hud::Place::top_right;
    auto rects = hud::Draw(m, kPicture, 1920, 1080);
    const std::int32_t left = 1920 - kMargin - 8 * kCell;
    CHECK(Cells(rects, hud::kRupee, left, kCell) == std::vector<std::int32_t>{4});
    CHECK(Of(rects, hud::kRupeeCount).x1 <= 1920 - kMargin);

    m.rupees_at = hud::Place::bottom_left;
    m.chat = {{"", "uno"}, {"", "dos"}};
    rects = hud::Draw(m, kPicture, 1920, 1080);
    // Two chat rows and half a line over them: the line's top at 1080 - 24 - 70 - 28.
    const std::int32_t top = 1080 - kMargin - (2 * kLine + kLine / 2) - kLine;
    const auto rupees = Of(rects, hud::kRupee);
    CHECK(rupees.y0 >= top);
    CHECK(rupees.y1 <= top + kLine);
    CHECK(Cells(rects, hud::kRupee, kMargin, kCell) == std::vector<std::int32_t>{4});
}

TEST_CASE("hud: a rupee line too long for the picture goes on to a second line") {
    hud::Model m;
    m.rupee_line = {W("Ana", 87), W("Saria", 43), W("Darunia", 999)};
    m.rupees_at = hud::Place::top_left;
    // 400 wide: room 352; Ana + Saria 280 fit, Darunia (7 + 3 + 3 = 13 cells, 182) does not.
    const hud::Box narrow{0, 0, 400, 1080};
    const auto rects = hud::Draw(m, narrow, 400, 1080);
    int first = 0, second = 0;
    for (const auto& r : rects) {
        if (r.rgb != hud::kRupee) {
            continue;
        }
        CHECK(r.x + r.width <= 400);
        if (r.y < kMargin + kLine) {
            ++first;
        } else {
            CHECK(r.y + r.height <= kMargin + 2 * kLine);
            ++second;
        }
    }
    CHECK(first > 0);
    CHECK(second > 0);
    // Darunia starts the second line at the margin: his rupee in cell 8.
    std::vector<hud::Rect> low;
    std::copy_if(rects.begin(), rects.end(), std::back_inserter(low),
                 [](const hud::Rect& r) { return r.y >= kMargin + kLine; });
    CHECK(Cells(low, hud::kRupee, kMargin, kCell) == std::vector<std::int32_t>{8});
}

TEST_CASE("hud: the panel goes where it is set, the rupee line after it in the same place") {
    hud::Model m;
    m.panel.push_back({"abc", false, 0x123456});
    m.panel_at = hud::Place::top_right;
    auto rects = hud::Draw(m, kPicture, 1920, 1080);
    auto text = Of(rects, 0x123456);
    CHECK(text.x0 >= 1920 - kMargin - 3 * kCell);
    CHECK(text.x1 <= 1920 - kMargin);
    CHECK(text.y0 >= kMargin);

    // Both at the top right: the rupee line under the panel, half a line apart.
    m.rupee_line = {W("Ana", 87)};
    m.rupees_at = hud::Place::top_right;
    rects = hud::Draw(m, kPicture, 1920, 1080);
    const auto rupees = Of(rects, hud::kRupee);
    CHECK(rupees.y0 >= kMargin + kLine + kLine / 2);
    CHECK(rupees.y1 <= kMargin + 2 * kLine + kLine / 2);

    // At the bottom: the panel's last line just over the margin.
    m.rupee_line.clear();
    m.panel_at = hud::Place::bottom;
    rects = hud::Draw(m, kPicture, 1920, 1080);
    text = Of(rects, 0x123456);
    CHECK(text.y0 >= 1080 - kMargin - kLine);
    CHECK(text.y1 <= 1080 - kMargin);
    CHECK(text.x0 >= (1920 - 3 * kCell) / 2);
    CHECK(text.x1 <= (1920 + 3 * kCell) / 2);
}
