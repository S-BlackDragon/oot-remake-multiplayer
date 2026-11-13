// SPDX-License-Identifier: GPL-3.0-or-later
// ORM partner map, the portable part (coop_map.h, docs MAPA_COMPANEROS.md).
#include "coop_map.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "coop/host/scene.hpp"
#include "platform_env.h"  // Env

namespace CoopMap {
namespace {

std::atomic<bool> g_pause{false};

bool Finite(float v) {
    return std::isfinite(v);
}

bool Finite(const Vec3& v) {
    return Finite(v[0]) && Finite(v[1]) && Finite(v[2]);
}

// The scene without its era and without "_Alternative" (Kakariko and the
// Zora's Domain have a second adult scene): what the place tables are keyed by.
std::string_view PlaceKey(std::string_view scene) {
    constexpr std::string_view kAlternative = "_Alternative";
    if (scene.size() > kAlternative.size() &&
        scene.substr(scene.size() - kAlternative.size()) == kAlternative) {
        scene.remove_suffix(kAlternative.size());
    }
    return coop::host::place_of(scene);
}

struct Name {
    std::string_view place;
    const char* spanish;  // UTF-8
};

// The places by what the scene is called without its era (PlaceKey). Our own
// words, from the Spanish names of the 1998 game.
constexpr Name kNames[] = {
    {"KokiriForest", "Bosque Kokiri"},
    {"LostWood", "Bosque Perdido"},
    {"SacredForestMeadow", "Prado Sagrado"},
    {"HyruleField", "Campo de Hyrule"},
    {"Market", "Mercado"},
    {"HyruleCastle", "Castillo de Hyrule"},
    {"GanonCastle", "Castillo de Ganon"},
    {"InsideGanonCastle", "Castillo de Ganon (dentro)"},
    {"CrumblingGanonCastle", "Castillo de Ganon (derrumbe)"},
    {"KakarikoVillage", "Aldea Kakariko"},
    {"Graveyard", "Cementerio"},
    {"DeathMountainTrail", "Monta" "\xC3\xB1" "a de la Muerte"},
    {"DeathMountainCrater", "Cr" "\xC3\xA1" "ter de la Monta" "\xC3\xB1" "a"},
    {"GoronCity", "Ciudad Goron"},
    {"ZoraRiver", "R" "\xC3\xAD" "o Zora"},
    {"ZoraDomain", "Dominio Zora"},
    {"ZoraFountain", "Fuente Zora"},
    {"LakeHylia", "Lago Hylia"},
    {"LonLonRanch", "Rancho Lon Lon"},
    {"GerudoValley", "Valle Gerudo"},
    {"GerudoFortress", "Fortaleza Gerudo"},
    {"HountedWasteland", "Desierto Encantado"},
    {"DesertColossus", "Coloso del Desierto"},
    {"InsideTheDekuTree", "Gran " "\xC3\x81" "rbol Deku"},
    {"DodongoCavern", "Cueva de los Dodongos"},
    {"InsideJabuJabuBelly", "Tripa de Jabu-Jabu"},
    {"ForestTemple", "Templo del Bosque"},
    {"FireTemple", "Templo del Fuego"},
    {"WaterTemple", "Templo del Agua"},
    {"ShadowTemple", "Templo de las Sombras"},
    {"SpiritTemple", "Templo del Esp" "\xC3\xAD" "ritu"},
    {"BottomOfTheWell", "Fondo del Pozo"},
    {"IceCavern", "Caverna de Hielo"},
    {"GerudoTrainingGround", "Centro de Instrucci" "\xC3\xB3" "n Gerudo"},
    {"GohmaStage", "sala de Gohma"},
    {"DodongoStage", "sala del Rey Dodongo"},
    {"BarinadeStage", "sala de Barinade"},
    {"PhantomGanonStage", "sala de Ganon Fantasma"},
    {"VolvagiaStage", "sala de Volvagia"},
    {"MorphaStage", "sala de Morpha"},
    {"BongoBongoStage", "sala de Bongo Bongo"},
    {"TwinrovaStage", "sala de Birova"},
    {"GanondorfStage", "sala de Ganondorf"},
    {"GanonStage", "sala de Ganon"},
    {"FinalBattleField", "batalla final"},
};

// "LakeHylia_Fishing" -> "Lake Hylia Fishing": a space before a capital that
// follows a small letter or a digit, '_' as a space.
std::string Words(std::string_view s) {
    std::string out;
    char prev = '\0';
    for (const char c : s) {
        if (c == '_') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            const auto uc = static_cast<unsigned char>(c);
            const auto up = static_cast<unsigned char>(prev);
            if (std::isupper(uc) && (std::islower(up) || std::isdigit(up)) && !out.empty() &&
                out.back() != ' ') {
                out += ' ';
            }
            out += c;
        }
        prev = c;
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::uint32_t Rgb(const std::array<std::uint8_t, 3>& c) {
    if (c[0] == 0 && c[1] == 0 && c[2] == 0) {
        return OrmHud::kWhite;  // no colour
    }
    return (std::uint32_t{c[0]} << 16) | (std::uint32_t{c[1]} << 8) | c[2];
}

std::string NameOf(std::uint8_t number, const std::string& name) {
    return name.empty() ? "P" + std::to_string(number) : name;
}

// ---- Parsing ----

std::vector<std::string_view> Split(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (i > start) {
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

std::optional<float> Number(std::string_view s) {
    if (s.empty() || s.size() > 64) {
        return std::nullopt;
    }
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::optional<int> Integer(std::string_view s) {
    const auto v = Number(s);
    if (!v || *v != std::floor(*v) || std::abs(*v) > 64.0f) {
        return std::nullopt;
    }
    return static_cast<int>(*v);
}

constexpr float kDegrees = 3.14159265358979f / 180.0f;

// The rest of a view line after its scene: "follow <span> [rot [mirror]]" or
// "<x> <z> <span> [rot [mirror]]". `follow_ok`: the minimap only.
std::optional<View> ParseView(const std::vector<std::string_view>& t, std::size_t at, bool follow_ok,
                              std::string& why) {
    View v;
    std::size_t i = at;
    if (i < t.size() && t[i] == "follow") {
        if (!follow_ok) {
            why = "a pause map does not follow";
            return std::nullopt;
        }
        v.follow = true;
        ++i;
    } else {
        const auto x = i < t.size() ? Number(t[i]) : std::nullopt;
        const auto z = i + 1 < t.size() ? Number(t[i + 1]) : std::nullopt;
        if (!x || !z) {
            why = "the centre is not two numbers";
            return std::nullopt;
        }
        v.centre_x = *x;
        v.centre_z = *z;
        i += 2;
    }
    const auto span = i < t.size() ? Number(t[i]) : std::nullopt;
    if (!span || *span <= 0.0f) {
        why = "the span is not a number above 0";
        return std::nullopt;
    }
    v.span = *span;
    ++i;
    if (i < t.size()) {
        const auto rot = Number(t[i]);
        if (!rot) {
            why = "the rotation is not a number (degrees)";
            return std::nullopt;
        }
        v.rotation = *rot * kDegrees;
        ++i;
    }
    if (i < t.size()) {
        if (t[i] != "mirror") {
            why = "expected 'mirror' or nothing after the rotation";
            return std::nullopt;
        }
        v.mirror = true;
        ++i;
    }
    if (i != t.size()) {
        why = "too many words";
        return std::nullopt;
    }
    return v;
}

} // namespace

// ---- Switches ----

bool OnFrom(const char* value) {
    // On by default (D-R70): only an explicit "0" turns the map off.
    return value == nullptr || value[0] == '\0' || OrmHud::SwitchOn(value);
}

bool On() {
    static const bool on = [] {
        const auto value = CoopPlatform::Env("LICHII_COOP_MAP");
        return OnFrom(value ? value->c_str() : nullptr);
    }();
    return on;
}

bool Debug() {
    static const bool on = [] {
        const auto value = CoopPlatform::Env("LICHII_COOP_MAP_DEBUG");
        return OrmHud::SwitchOn(value ? value->c_str() : nullptr);
    }();
    return on;
}

bool PauseShown() {
    return g_pause.load(std::memory_order_relaxed);
}

void SetPauseShown(bool shown) {
    g_pause.store(shown, std::memory_order_relaxed);
}

void TogglePause() {
    bool shown = g_pause.load(std::memory_order_relaxed);
    while (!g_pause.compare_exchange_weak(shown, !shown, std::memory_order_relaxed)) {
    }
}

// ---- Place names ----

std::string PlaceName(std::string_view scene) {
    const std::string_view key = PlaceKey(scene);
    for (const auto& n : kNames) {
        if (n.place == key) {
            return n.spanish;
        }
    }
    return Words(key);
}

// ---- Floors ----

std::optional<int> FloorOf(const Floors& floors, float y) {
    if (floors.bottoms.empty() || !Finite(y)) {
        return std::nullopt;
    }
    std::size_t index = 0;
    for (std::size_t i = 1; i < floors.bottoms.size(); ++i) {
        if (floors.bottoms[i] <= y) {
            index = i;
        }
    }
    return floors.min_floor + static_cast<int>(index);
}

std::string FloorLabel(int floor) {
    return floor >= 0 ? std::to_string(floor + 1) + "F" : "B" + std::to_string(-floor);
}

// ---- The table ----

const View* Table::ViewFor(Which which, std::string_view scene) const {
    const auto& views = which == Which::mini ? mini : pause;
    if (const auto it = views.find(scene); it != views.end()) {
        return &it->second;
    }
    if (const auto it = views.find(PlaceKey(scene)); it != views.end()) {
        return &it->second;
    }
    if (which == Which::mini && mini_any) {
        return &*mini_any;
    }
    return nullptr;
}

const Screen* Table::ScreenFor(Which which) const {
    const auto& s = which == Which::mini ? mini_screen : pause_screen;
    return s ? &*s : nullptr;
}

const Floors* Table::FloorsFor(std::string_view scene) const {
    if (const auto it = floors.find(scene); it != floors.end()) {
        return &it->second;
    }
    if (const auto it = floors.find(PlaceKey(scene)); it != floors.end()) {
        return &it->second;
    }
    return nullptr;
}

Parsed Parse(std::string_view text) {
    Parsed out;
    std::size_t number = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++number;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto t = Split(line);
        if (t.empty()) {
            continue;
        }
        std::string why;
        const auto bad = [&](const std::string& what) {
            out.errors.push_back("line " + std::to_string(number) + ": " + what);
        };
        if (t[0] == "floors") {
            if (t.size() < 4) {
                bad("floors needs a scene, the lowest floor and at least one height");
                continue;
            }
            Floors f;
            const auto min_floor = Integer(t[2]);
            if (!min_floor) {
                bad("the lowest floor is not a whole number");
                continue;
            }
            f.min_floor = *min_floor;
            bool ok = true;
            for (std::size_t i = 3; i < t.size(); ++i) {
                const auto y = Number(t[i]);
                if (!y || (!f.bottoms.empty() && *y <= f.bottoms.back())) {
                    ok = false;
                    break;
                }
                f.bottoms.push_back(*y);
            }
            if (!ok) {
                bad("the floor heights are not numbers going up");
                continue;
            }
            out.table.floors[std::string(t[1])] = std::move(f);
        } else if (t[0] == "mini" || t[0] == "pause") {
            const bool mini = t[0] == "mini";
            if (t.size() < 3) {
                bad(std::string(t[0]) + " needs a scene and a view");
                continue;
            }
            auto v = ParseView(t, 2, mini, why);
            if (!v) {
                bad(why);
                continue;
            }
            if (t[1] == "*") {
                if (!mini) {
                    bad("pause takes one scene, not *");
                    continue;
                }
                out.table.mini_any = *v;
            } else {
                (mini ? out.table.mini : out.table.pause)[std::string(t[1])] = *v;
            }
        } else if (t[0] == "screen") {
            if ((t.size() != 6 && t.size() != 7) || (t[1] != "mini" && t[1] != "pause")) {
                bad("screen <mini|pause> <left> <top> <width> <height> [round]");
                continue;
            }
            Screen s;
            const auto l = Number(t[2]), top = Number(t[3]), w = Number(t[4]), h = Number(t[5]);
            if (!l || !top || !w || !h || *w <= 0.0f || *h <= 0.0f || *l < 0.0f || *top < 0.0f ||
                *l + *w > 1.0f + 1e-4f || *top + *h > 1.0f + 1e-4f) {
                bad("the screen box is not inside the picture (fractions 0 to 1)");
                continue;
            }
            if (t.size() == 7) {
                if (t[6] != "round") {
                    bad("expected 'round' or nothing after the box");
                    continue;
                }
                s.round = true;
            }
            s.left = *l;
            s.top = *top;
            s.width = *w;
            s.height = *h;
            (t[1] == "mini" ? out.table.mini_screen : out.table.pause_screen) = s;
        } else if (t[0] == "notes") {
            const auto l = t.size() == 3 ? Number(t[1]) : std::nullopt;
            const auto b = t.size() == 3 ? Number(t[2]) : std::nullopt;
            if (!l || !b || *l < 0.0f || *l > 1.0f || *b < 0.0f || *b > 1.0f) {
                bad("notes <left> <bottom>, fractions 0 to 1");
                continue;
            }
            out.table.notes_left = *l;
            out.table.notes_bottom = *b;
        } else {
            bad("unknown entry '" + std::string(t[0]) + "'");
        }
    }
    return out;
}

// ---- World to map ----

std::array<float, 2> Rim(float cx, float cy, float hx, float hy, float dx, float dy, bool round) {
    if ((dx == 0.0f && dy == 0.0f) || hx <= 0.0f || hy <= 0.0f || !Finite(dx) || !Finite(dy)) {
        return {cx, cy};
    }
    double t = 0.0;
    if (round) {
        const double a = double(dx) / hx, b = double(dy) / hy;
        t = 1.0 / std::sqrt(a * a + b * b);
    } else {
        t = std::numeric_limits<double>::infinity();
        if (dx != 0.0f) {
            t = std::min(t, double(hx) / std::abs(dx));
        }
        if (dy != 0.0f) {
            t = std::min(t, double(hy) / std::abs(dy));
        }
    }
    return {static_cast<float>(cx + dx * t), static_cast<float>(cy + dy * t)};
}

std::optional<OnMap> ToMap(const View& view, const Screen& screen, float picture_aspect,
                           const Vec3& world, const std::optional<Vec3>& own) {
    if (!(view.span > 0.0f) || !Finite(view.span) || !Finite(view.rotation) || !Finite(view.centre_x) ||
        !Finite(view.centre_z) || !(screen.width > 0.0f) || !(screen.height > 0.0f) ||
        !Finite(screen.left) || !Finite(screen.top) || !Finite(screen.width) || !Finite(screen.height) ||
        !(picture_aspect > 0.0f) || !Finite(picture_aspect) || !Finite(world)) {
        return std::nullopt;
    }
    double cx = view.centre_x, cz = view.centre_z;
    if (view.follow) {
        if (!own || !Finite(*own)) {
            return std::nullopt;
        }
        cx = (*own)[0];
        cz = (*own)[2];
    }
    double dx = world[0] - cx;
    const double dz = world[2] - cz;
    if (view.mirror) {
        dx = -dx;
    }
    const double c = std::cos(double(view.rotation)), s = std::sin(double(view.rotation));
    // Clockwise on the screen (y down) by `rotation`.
    const double u = dx * c - dz * s;
    const double v = dx * s + dz * c;
    // Map units: the map's half height is 1, its half width `aspect`.
    const double aspect = double(screen.width) * picture_aspect / screen.height;
    const double k = 2.0 / view.span;
    double mx = u * k, my = v * k;
    OnMap out;
    const bool inside = screen.round ? (mx / aspect) * (mx / aspect) + my * my <= 1.0
                                     : std::abs(mx) <= aspect && std::abs(my) <= 1.0;
    if (!inside) {
        // On the rim, a little in so the arrow is on the map.
        constexpr float kIn = 0.92f;
        const auto rim = Rim(0.0f, 0.0f, static_cast<float>(aspect * kIn), kIn, static_cast<float>(mx),
                             static_cast<float>(my), screen.round);
        out.edge = true;
        out.angle = static_cast<float>(std::atan2(my, mx));
        mx = rim[0];
        my = rim[1];
    }
    out.x = static_cast<float>(screen.left + screen.width * (0.5 + mx / (2.0 * aspect)));
    out.y = static_cast<float>(screen.top + screen.height * (0.5 + my / 2.0));
    if (!Finite(out.x) || !Finite(out.y)) {
        return std::nullopt;
    }
    return out;
}

// ---- Pointers ----

std::vector<CoopHook::Marker> Pointers(const coop::overlay::Camera& camera,
                                       std::string_view local_scene,
                                       const std::optional<Vec3>& own,
                                       const std::vector<coop::overlay::Shown>& mates,
                                       const std::vector<std::uint8_t>& drawn,
                                       const PointerSettings& settings) {
    std::vector<CoopHook::Marker> out;
    const float width = camera.viewport_width, height = camera.viewport_height;
    if (!Finite(width) || !Finite(height) || width <= 0.0f || height <= 0.0f) {
        return out;
    }
    const auto vp = coop::overlay::multiply(camera.projection, camera.view);
    if (!std::all_of(vp.begin(), vp.end(), [](float v) { return std::isfinite(v); })) {
        return out;
    }
    const bool own_ok = own && Finite(*own);
    const float cx = width / 2.0f, cy = height / 2.0f;
    const float size = std::max(8.0f, height * settings.size_share);
    const float hx = std::max(0.0f, cx - settings.edge_margin - size / 2.0f);
    const float hy = std::max(0.0f, cy - settings.edge_margin - size / 2.0f);

    for (const auto& s : mates) {
        if (std::find(drawn.begin(), drawn.end(), s.number) != drawn.end()) {
            continue;  // the overlay has a tag or arrow for him
        }
        if (!Finite(s.position) || !Finite(s.fade) || s.fade <= 0.0f) {
            continue;
        }
        const std::optional<std::string_view> theirs =
            s.scene ? std::optional<std::string_view>(*s.scene) : std::nullopt;
        if (!coop::host::same_place(theirs, local_scene)) {
            continue;
        }
        std::optional<float> metres;
        if (own_ok) {
            const double dx = double(s.position[0]) - (*own)[0];
            const double dy = double(s.position[1]) - (*own)[1];
            const double dz = double(s.position[2]) - (*own)[2];
            metres = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
            if (*metres < settings.near) {
                continue;  // on top of us
            }
        }
        // Clip coordinates (column-major matrix, clip y up).
        double clip[4];
        for (int row = 0; row < 4; ++row) {
            clip[row] = double(vp[0 * 4 + row]) * s.position[0] + double(vp[1 * 4 + row]) * s.position[1] +
                        double(vp[2 * 4 + row]) * s.position[2] + double(vp[3 * 4 + row]);
        }
        if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3])) {
            continue;
        }
        const double w = clip[3];
        constexpr double kEps = 1e-6;
        CoopHook::Marker m;
        m.number = s.number;
        m.color = s.color.value_or(coop::overlay::default_color(s.number));
        m.urgent = s.urgent;
        m.alpha = std::min(1.0f, s.fade);  // presence's fade only: no fade with distance
        m.tag = NameOf(s.number, s.name);
        if (!s.note.empty()) {
            m.tag += " (" + s.note + ")";
        } else if (metres) {
            m.tag += " " + std::to_string(static_cast<long long>(std::lround(*metres))) + " m";
        }
        double dx = 0.0, dy = 0.0;  // screen pixels from the centre, y down
        if (w > kEps) {
            const double nx = clip[0] / w, ny = clip[1] / w;
            if (std::abs(nx) <= 1.0 && std::abs(ny) <= 1.0) {
                if (w < settings.near) {
                    continue;  // in the camera's face
                }
                // In view, too far for a tag: a dot where he is.
                m.x = static_cast<float>((nx * 0.5 + 0.5) * width);
                m.y = static_cast<float>((0.5 - ny * 0.5) * height);
                m.size = size * 0.6f;
                m.tag_x = m.x;
                m.tag_y = m.y - m.size / 2.0f - settings.tag_gap;
                if (m.tag_y < 0.0f) {
                    m.tag_below = true;
                    m.tag_y = m.y + m.size / 2.0f + settings.tag_gap;
                }
                out.push_back(std::move(m));
                continue;
            }
            dx = nx * cx;
            dy = -ny * cy;
        } else {
            // Behind the camera: dividing by w would turn him around. His
            // side is clip x's sign; he is put along the bottom edge, further
            // to the side the more he is off to it.
            const double aw = std::max(std::abs(w), kEps);
            dx = std::clamp(clip[0] / aw, -1e6, 1e6) * cx;
            dy = cy;
        }
        if (dx == 0.0 && dy == 0.0) {
            dy = cy;
        }
        const auto rim = Rim(cx, cy, hx, hy, static_cast<float>(dx), static_cast<float>(dy), false);
        m.x = rim[0];
        m.y = rim[1];
        m.edge = true;
        m.arrow_angle = static_cast<float>(std::atan2(dy, dx));
        m.size = size;
        m.tag_x = m.x;
        if (m.y < cy) {
            m.tag_below = true;  // an arrow along the top: the tag under it
            m.tag_y = m.y + size / 2.0f + settings.tag_gap;
        } else {
            m.tag_y = m.y - size / 2.0f - settings.tag_gap;
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<std::uint8_t> KeepSolid(std::vector<CoopHook::Marker>& items) {
    std::erase_if(items, [](const CoopHook::Marker& m) {
        return m.number != 0 && !m.edge && !(m.alpha >= OrmHud::kMinAlpha);
    });
    std::vector<std::uint8_t> drawn;
    drawn.reserve(items.size());
    for (const auto& m : items) {
        drawn.push_back(m.number);
    }
    return drawn;
}

// ---- Map dots and elsewhere ----

std::vector<OrmHud::MapDot> Dots(const Table& table, const Input& input) {
    std::vector<OrmHud::MapDot> out;
    if (!input.scene || input.scene->empty()) {
        return out;
    }
    const View* view = table.ViewFor(input.which, *input.scene);
    const Screen* screen = table.ScreenFor(input.which);
    if (view == nullptr || screen == nullptr) {
        return out;
    }
    const Floors* floors = table.FloorsFor(*input.scene);
    std::optional<int> shown_floor;
    if (floors != nullptr && input.own) {
        shown_floor = FloorOf(*floors, (*input.own)[1]);
    }
    const bool pause = input.which == Which::pause;
    const std::optional<std::string_view> ours(*input.scene);
    for (const auto& mate : input.mates) {
        const std::optional<std::string_view> theirs =
            mate.scene ? std::optional<std::string_view>(*mate.scene) : std::nullopt;
        if (!coop::host::same_place(theirs, ours)) {
            continue;
        }
        const auto at = ToMap(*view, *screen, input.picture_aspect, mate.position, input.own);
        if (!at) {
            continue;
        }
        OrmHud::MapDot d;
        d.x = at->x;
        d.y = at->y;
        d.edge = at->edge;
        d.angle = at->angle;
        d.rgb = Rgb(mate.color);
        d.urgent = mate.urgent;
        std::optional<int> floor;
        if (floors != nullptr) {
            floor = FloorOf(*floors, mate.position[1]);
            d.other_floor = floor && shown_floor && *floor != *shown_floor;
        }
        if (pause) {
            d.label = NameOf(mate.number, mate.name);
            if (d.other_floor) {
                d.label += " " + FloorLabel(*floor);
            }
        }
        out.push_back(std::move(d));
    }
    if (input.self && input.own) {
        if (const auto at = ToMap(*view, *screen, input.picture_aspect, *input.own, input.own)) {
            OrmHud::MapDot d;
            d.x = at->x;
            d.y = at->y;
            d.edge = at->edge;
            d.angle = at->angle;
            d.self = true;
            if (pause) {
                d.label = shown_floor ? "you " + FloorLabel(*shown_floor) : "you";
            }
            out.push_back(std::move(d));  // last: over the others
        }
    }
    return out;
}

std::vector<OrmHud::Note> Elsewhere(const Input& input) {
    std::vector<OrmHud::Note> out;
    if (!input.scene || input.scene->empty()) {
        return out;  // ours not known (a load): nothing to compare with
    }
    std::vector<const CoopHook::Mate*> mates;
    for (const auto& m : input.mates) {
        mates.push_back(&m);
    }
    std::stable_sort(mates.begin(), mates.end(),
                     [](const CoopHook::Mate* a, const CoopHook::Mate* b) { return a->number < b->number; });
    const std::optional<std::string_view> ours(*input.scene);
    for (const auto* m : mates) {
        if (!m->scene || m->scene->empty()) {
            continue;  // his not known yet
        }
        if (coop::host::same_place(std::optional<std::string_view>(*m->scene), ours)) {
            continue;
        }
        out.push_back({NameOf(m->number, m->name) + ": en " + PlaceName(*m->scene), Rgb(m->color)});
    }
    return out;
}

// ---- riesgos-visual: the dots' path

namespace {

std::string Num1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
    return buf;
}

} // namespace

MapPath PathFor(const Table& table, Which which, std::string_view scene) {
    MapPath p;
    const char* name = which == Which::mini ? "minimap" : "pause map";
    const View* view = table.ViewFor(which, scene);
    const Screen* screen = table.ScreenFor(which);
    if (view == nullptr && screen == nullptr) {
        p.detail = std::string("no ") + name + " view or box in the map table (coop_map.txt): edge arrows and "
                   "lines only";
    } else if (view == nullptr) {
        p.detail = std::string("the map table has no ") + name + " view for " + std::string(scene) +
                   ": edge arrows and lines only";
    } else if (screen == nullptr) {
        p.detail = std::string("the map table has no ") + name + " box: edge arrows and lines only";
    } else {
        p.ok = true;
        p.detail = std::string(name) + " dots by the table: " +
                   (view->follow ? std::string("following our Link")
                                 : "centre " + Num1(view->centre_x) + ", " + Num1(view->centre_z)) +
                   ", span " + Num1(view->span) + ", box " + Num1(screen->left) + " " + Num1(screen->top) + " " +
                   Num1(screen->width) + " " + Num1(screen->height) +
                   " (calibrate with LICHII_COOP_MAP_DEBUG=1: our own dot on the game's arrow)";
    }
    return p;
}

} // namespace CoopMap
