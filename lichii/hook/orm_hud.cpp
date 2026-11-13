// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_hud.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "coop_hud_font.h"
#include "coop_switch.h"

namespace OrmHud {
namespace {

std::atomic<bool> g_panel{CoopSwitch::On("LICHII_COOP_PANEL", false)};
std::atomic<bool> g_typing{false};  // ORM: the Qt chat box is open

constexpr char32_t kReplacement = 0xfffd;

bool Finite(float v) {
    return std::isfinite(v);
}

// The hook's (red, green, blue) as 0xRRGGBB; none for all zero (no colour).
std::optional<std::uint32_t> Rgb(const std::array<std::uint8_t, 3>& c) {
    if (c[0] == 0 && c[1] == 0 && c[2] == 0) {
        return std::nullopt;
    }
    return (std::uint32_t{c[0]} << 16) | (std::uint32_t{c[1]} << 8) | c[2];
}

// A dim panel line in a player's colour: two thirds of it, as the Qt panel's
// 170/255 over its dark background.
std::uint32_t Dimmed(std::uint32_t rgb) {
    const auto ch = [&](int shift) { return ((rgb >> shift) & 0xffu) * 170u / 255u; };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// The glyph for a codepoint, '?' for one the font does not have.
const std::array<std::uint8_t, CoopHudFont::kHeight>& Glyph(char32_t cp) {
    const auto& question = CoopHudFont::kGlyphs['?' - CoopHudFont::kFirst];
    if (cp == 0xa0) {
        cp = ' ';  // no-break space
    }
    if (cp < CoopHudFont::kFirst || cp > CoopHudFont::kLast) {
        return question;
    }
    const auto& g = CoopHudFont::kGlyphs[cp - CoopHudFont::kFirst];
    if (cp != ' ' && std::all_of(g.begin(), g.end(), [](std::uint8_t row) { return row == 0; })) {
        return question;
    }
    return g;
}

// Collects rectangles clipped to one box, up to kMaxRects.
class Canvas {
public:
    Canvas(const Box& clip, std::vector<Rect>& out) : clip_(clip), out_(out) {}

    void Fill(std::int64_t x, std::int64_t y, std::int64_t w, std::int64_t h, std::uint32_t rgb) {
        const std::int64_t x0 = std::max<std::int64_t>(x, clip_.x);
        const std::int64_t y0 = std::max<std::int64_t>(y, clip_.y);
        const std::int64_t x1 = std::min<std::int64_t>(x + w, std::int64_t{clip_.x} + clip_.width);
        const std::int64_t y1 = std::min<std::int64_t>(y + h, std::int64_t{clip_.y} + clip_.height);
        if (x1 <= x0 || y1 <= y0 || out_.size() >= kMaxRects) {
            return;
        }
        out_.push_back({static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                        static_cast<std::int32_t>(x1 - x0), static_cast<std::int32_t>(y1 - y0), rgb});
    }

    // A filled convex polygon, one rectangle per pixel row (spans sampled at
    // the row's centre).
    void Polygon(float (*pts)[2], int n, std::uint32_t rgb) {
        float top = pts[0][1], bottom = pts[0][1];
        for (int i = 1; i < n; ++i) {
            top = std::min(top, pts[i][1]);
            bottom = std::max(bottom, pts[i][1]);
        }
        for (auto y = static_cast<std::int64_t>(std::floor(top));
             y < static_cast<std::int64_t>(std::ceil(bottom)); ++y) {
            const float cy = static_cast<float>(y) + 0.5f;
            float left = 0.0f, right = 0.0f;
            bool any = false;
            for (int i = 0; i < n; ++i) {
                const float* a = pts[i];
                const float* b = pts[(i + 1) % n];
                if ((cy < a[1]) == (cy < b[1])) {
                    continue;  // the edge does not cross this row
                }
                const float x = a[0] + (cy - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
                left = any ? std::min(left, x) : x;
                right = any ? std::max(right, x) : x;
                any = true;
            }
            if (any) {
                const auto x0 = static_cast<std::int64_t>(std::lround(left));
                const auto x1 = static_cast<std::int64_t>(std::lround(right));
                Fill(x0, y, std::max<std::int64_t>(1, x1 - x0), 1, rgb);
            }
        }
    }

    void Disc(float cx, float cy, float r, std::uint32_t rgb) {
        for (auto y = static_cast<std::int64_t>(std::floor(cy - r));
             y < static_cast<std::int64_t>(std::ceil(cy + r)); ++y) {
            const float dy = static_cast<float>(y) + 0.5f - cy;
            if (dy * dy > r * r) {
                continue;
            }
            const float half = std::sqrt(r * r - dy * dy);
            const auto x0 = static_cast<std::int64_t>(std::lround(cx - half));
            const auto x1 = static_cast<std::int64_t>(std::lround(cx + half));
            Fill(x0, y, std::max<std::int64_t>(1, x1 - x0), 1, rgb);
        }
    }

private:
    Box clip_;
    std::vector<Rect>& out_;
};

struct Run {
    std::u32string text;
    std::uint32_t rgb = kWhite;
};

// The filled pixels of a line of text as rectangles, unclipped: per glyph,
// each row's runs of set bits, a run carried down while the next row has the
// same one.
void GlyphRects(const std::vector<Run>& runs, std::int64_t left, std::int64_t top, int scale,
                std::vector<Rect>& out) {
    struct Open {
        int x0, x1, row0, rows;
    };
    std::int64_t pen = left;
    for (const auto& run : runs) {
        for (const char32_t cp : run.text) {
            const auto& g = Glyph(cp);
            std::vector<Open> open, next;
            auto close = [&](const Open& o) {
                out.push_back({static_cast<std::int32_t>(pen + std::int64_t{o.x0} * scale),
                               static_cast<std::int32_t>(top + std::int64_t{o.row0} * scale),
                               (o.x1 - o.x0) * scale, o.rows * scale, run.rgb});
            };
            for (int row = 0; row <= CoopHudFont::kHeight; ++row) {
                const std::uint8_t bits = row < CoopHudFont::kHeight ? g[row] : 0;
                next.clear();
                int x = 0;
                while (x < CoopHudFont::kWidth) {
                    if (!(bits & (1u << (CoopHudFont::kWidth - 1 - x)))) {
                        ++x;
                        continue;
                    }
                    int end = x;
                    while (end < CoopHudFont::kWidth &&
                           (bits & (1u << (CoopHudFont::kWidth - 1 - end)))) {
                        ++end;
                    }
                    const auto it = std::find_if(open.begin(), open.end(), [&](const Open& o) {
                        return o.x0 == x && o.x1 == end;
                    });
                    if (it != open.end()) {
                        next.push_back({x, end, it->row0, it->rows + 1});
                        open.erase(it);
                    } else {
                        next.push_back({x, end, row, 1});
                    }
                    x = end;
                }
                for (const auto& o : open) {
                    close(o);  // not carried into this row
                }
                open.swap(next);
            }
            pen += std::int64_t{CoopHudFont::kWidth} * scale;
        }
    }
}

// One line: the outline of every run first (each pixel rectangle grown by
// `outline`), then the fills, so neighbouring glyphs never cover each other.
void Text(Canvas& canvas, const std::vector<Run>& runs, std::int64_t left, std::int64_t top,
          int scale, int outline) {
    std::vector<Rect> fills;
    GlyphRects(runs, left, top, scale, fills);
    if (outline > 0) {
        for (const auto& r : fills) {
            canvas.Fill(std::int64_t{r.x} - outline, std::int64_t{r.y} - outline,
                        std::int64_t{r.width} + 2 * outline, std::int64_t{r.height} + 2 * outline,
                        kOutline);
        }
    }
    for (const auto& r : fills) {
        canvas.Fill(r.x, r.y, r.width, r.height, r.rgb);
    }
}

// ORM wallet list: a rupee (an upright hexagon) in the glyph cell whose top
// left is (left, top), outlined in black as the text is.
void RupeeIcon(Canvas& canvas, std::int64_t left, std::int64_t top, int scale, int outline) {
    const float s = static_cast<float>(scale);
    const float cx = static_cast<float>(left) + CoopHudFont::kWidth * s / 2.0f;
    const float y0 = static_cast<float>(top) + 1.5f * s;
    const float y1 = static_cast<float>(top) + (CoopHudFont::kHeight - 1.5f) * s;
    const float half = 2.5f * s;
    const float shoulder = 2.5f * s;
    auto hexagon = [&](float grow, float (*p)[2]) {
        p[0][0] = cx;
        p[0][1] = y0 - grow;
        p[1][0] = cx + half + grow;
        p[1][1] = y0 + shoulder;
        p[2][0] = cx + half + grow;
        p[2][1] = y1 - shoulder;
        p[3][0] = cx;
        p[3][1] = y1 + grow;
        p[4][0] = cx - half - grow;
        p[4][1] = y1 - shoulder;
        p[5][0] = cx - half - grow;
        p[5][1] = y0 + shoulder;
    };
    float pts[6][2];
    hexagon(static_cast<float>(outline), pts);
    canvas.Polygon(pts, 6, kOutline);
    hexagon(0.0f, pts);
    canvas.Polygon(pts, 6, kRupee);
}

// ORM: a line cut to `max` codepoints, the last three of them "...".
void Fit(std::vector<Run>& runs, std::size_t max) {
    std::size_t total = 0;
    for (const auto& r : runs) {
        total += r.text.size();
    }
    if (total <= max) {
        return;
    }
    const std::size_t dots = std::min<std::size_t>(3, max);
    std::size_t left = max - dots;
    std::vector<Run> out;
    for (auto& r : runs) {
        if (left == 0) {
            break;
        }
        if (r.text.size() > left) {
            r.text.resize(left);
        }
        left -= r.text.size();
        out.push_back(std::move(r));
    }
    const std::uint32_t rgb = out.empty() ? kWhite : out.back().rgb;
    out.push_back({std::u32string(dots, U'.'), rgb});
    runs = std::move(out);
}

int ScaleFor(int picture_height, double share) {
    if (picture_height <= 0) {
        return 1;
    }
    const auto s = static_cast<int>(std::lround(picture_height * share / CoopHudFont::kHeight));
    return std::clamp(s, 1, 8);
}

// ORM (Settings::size): the size in percent as a factor, kept in its range.
double SizeFactor(int size_percent) {
    return std::clamp(size_percent, kMinSize, kMaxSize) / 100.0;
}

int OutlineFor(int scale) {
    return std::max(1, scale * 2 / 3);
}

// A triangle of "radius" r at (mx, my) pointing at `angle`, outlined, as the
// Qt layer's edge arrow.
void Arrow(Canvas& canvas, float mx, float my, float r, float angle, int outline, std::uint32_t rgb) {
    const float ca = std::cos(angle), sa = std::sin(angle);
    auto tri = [&](float k, float (*p)[2]) {
        p[0][0] = mx + ca * r * k;
        p[0][1] = my + sa * r * k;
        p[1][0] = mx + (-ca * 0.6f - sa * 0.8f) * r * k;
        p[1][1] = my + (-sa * 0.6f + ca * 0.8f) * r * k;
        p[2][0] = mx + (-ca * 0.6f + sa * 0.8f) * r * k;
        p[2][1] = my + (-sa * 0.6f - ca * 0.8f) * r * k;
    };
    float pts[3][2];
    tri((r + 2.0f * static_cast<float>(outline)) / r, pts);
    canvas.Polygon(pts, 3, kOutline);
    tri(1.0f, pts);
    canvas.Polygon(pts, 3, rgb);
}

// A marker's colour: ours, a downed mate's, or the player's (kMarker for white).
std::uint32_t MarkerColour(bool self, bool urgent, std::uint32_t rgb) {
    return self ? kMarkerSelf : urgent ? kMarkerUrgent : rgb == kWhite ? kMarker : rgb;
}

// ORM (docs/HUD_AJUSTES.md): how many places there are, and which side each is on.
constexpr std::size_t kPlaces = 6;
bool AtBottom(Place p) {
    return p == Place::bottom_left || p == Place::bottom || p == Place::bottom_right;
}
bool AtRight(Place p) {
    return p == Place::top_right || p == Place::bottom_right;
}
bool AtCentre(Place p) {
    return p == Place::top || p == Place::bottom;
}

// ORM: one line of a block (the panel, its wallet list, the rupee line):
// a line of text, or wallets side by side; `gap` pixels before it.
struct Row {
    std::vector<Run> text;
    std::vector<const Wallet*> wallets;
    std::int64_t width = 0;  // pixels
    std::int64_t gap = 0;
};

// ORM: between two wallets on the rupee line, in glyph cells.
constexpr std::int64_t kWalletGapCells = 2;

// ORM: a wallet's width in glyph cells: the name, a space, the rupee, a space, the count.
std::int64_t WalletCells(const Wallet& w) {
    return static_cast<std::int64_t>(Decode(w.name).size() + 3 + Decode(RupeeText(w)).size());
}

// ORM: a wallet drawn with its top left at (left, top): the name in the
// player's colour, the rupee, the count. Returns its width in glyph cells.
std::int64_t WalletEntry(Canvas& canvas, const Wallet& w, std::int64_t left, std::int64_t top, int scale,
                         int outline) {
    const std::int64_t cell = std::int64_t{CoopHudFont::kWidth} * scale;
    const std::u32string name = Decode(w.name);
    Text(canvas, {{name, w.rgb}}, left, top, scale, outline);
    const std::int64_t icon = left + (static_cast<std::int64_t>(name.size()) + 1) * cell;
    RupeeIcon(canvas, icon, top, scale, outline);
    const std::u32string count = Decode(RupeeText(w));
    Text(canvas, {{count, kRupeeCount}}, icon + 2 * cell, top, scale, outline);
    return static_cast<std::int64_t>(name.size() + 3 + count.size());
}

} // namespace

int MapDotRadius(int picture_height) {
    return std::clamp(static_cast<int>(std::lround(picture_height * 0.008)), 3, 16);
}

bool SwitchOn(const char* value) {
    return CoopSwitch::From(value, false);
}

bool On() {
    // ORM rule 6 (test10): upstream's own LICHII_COOP_HUD (its game-look HUD,
    // on unless =0) shares the old name, so ours is LICHII_ORM_HUD now; the old
    // LICHII_COOP_HUD=1 of kits 0.8 to 0.11 still turns ours on.
    static const bool on = CoopSwitch::On("LICHII_ORM_HUD", "LICHII_COOP_HUD", false);
    return on;
}

bool PanelShown() {
    return g_panel.load(std::memory_order_relaxed);
}

void SetPanelShown(bool shown) {
    g_panel.store(shown, std::memory_order_relaxed);
}

void TogglePanel() {
    bool shown = g_panel.load(std::memory_order_relaxed);
    while (!g_panel.compare_exchange_weak(shown, !shown, std::memory_order_relaxed)) {
    }
}

bool Typing() {
    return g_typing.load(std::memory_order_relaxed);
}

void SetTyping(bool typing) {
    g_typing.store(typing, std::memory_order_relaxed);
}

std::vector<ChatRow> BuildChat(const std::vector<CoopHook::ChatLine>& lines) {
    std::vector<ChatRow> rows;
    for (const auto& l : lines) {
        if (l.age_ms >= kChatShowMs) {
            continue;
        }
        ChatRow r;
        if (l.notice) {
            r.text = l.text;
            r.text_rgb = kNotice;
        } else {
            const std::uint32_t rgb = l.color ? Rgb(*l.color).value_or(kWhite) : kWhite;
            const std::string prefix = l.name + ": ";
            if (!l.name.empty() && l.text.compare(0, prefix.size(), prefix) == 0) {
                r.name = l.name;
                r.name_rgb = rgb;
                r.text = l.text.substr(l.name.size());
            } else {
                r.text = l.text;
            }
            r.text_rgb = l.quick ? rgb : kWhite;
        }
        rows.push_back(std::move(r));
    }
    if (rows.size() > kChatLines) {
        rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - kChatLines));
    }
    return rows;
}

std::string PingText(const Tag& tag) {
    if (!tag.ping_ms) {
        return {};
    }
    return (tag.self ? "" : "~") + std::to_string(*tag.ping_ms) + " ms";
}

Model Build(const std::optional<CoopHook::Markers>& markers,
            const std::vector<CoopHook::PlayerRow>& players,
            const std::vector<CoopHook::OverlayLine>& panel_lines, bool panel_shown) {
    Model model;
    if (markers && markers->width > 0.0f && markers->height > 0.0f && Finite(markers->width) &&
        Finite(markers->height)) {
        model.width = markers->width;
        model.height = markers->height;
        for (const auto& m : markers->items) {
            Tag t;
            t.number = m.number;
            t.name = m.tag;
            t.ping = m.ping;  // ORM quick messages: the sender's colour, no ping in ms
            t.self = m.self && !m.ping;
            t.urgent = m.urgent && !m.self && !m.ping;
            t.rgb = t.self ? kSelf : t.urgent ? kUrgent : Rgb(m.color).value_or(kWhite);
            for (const auto& row : players) {
                if (t.ping) {
                    break;
                }
                if (row.self == m.self && (m.self || row.number == m.number)) {
                    t.ping_ms = row.ms;
                    break;
                }
            }
            t.x = m.tag_x;
            t.y = m.tag_y;
            t.below = m.tag_below;
            t.head = m.tag_head;
            t.edge = m.edge;
            t.arrow_angle = m.arrow_angle;
            t.marker_x = m.x;
            t.marker_y = m.y;
            t.marker_size = m.size;
            t.alpha = m.alpha;
            model.tags.push_back(std::move(t));
        }
    }
    if (panel_shown) {
        for (const auto& line : panel_lines) {
            PanelLine p{line.text, line.dim, std::nullopt};
            if (line.color) {
                p.rgb = Rgb(*line.color);
            }
            model.panel.push_back(std::move(p));
        }
        // ORM (INVENTARIO.md, "Cartera de cada uno"): everyone's rupees, once any count is known.
        model.wallets = Wallets(players);
    }
    return model;
}

std::vector<Wallet> Wallets(const std::vector<CoopHook::PlayerRow>& players) {
    std::vector<Wallet> out;
    const bool any_count = std::any_of(players.begin(), players.end(),
                                       [](const CoopHook::PlayerRow& row) { return row.rupees.has_value(); });
    if (!any_count) {
        return out;
    }
    for (const auto& row : players) {
        Wallet w;
        w.number = row.number;
        w.name = row.name.empty() ? "P" + std::to_string(row.number) : row.name;
        w.rgb = row.color ? Rgb(*row.color).value_or(kWhite) : kWhite;
        w.rupees = row.rupees;
        w.self = row.self;
        w.stale = row.rupees_stale && row.rupees.has_value();
        out.push_back(std::move(w));
    }
    return out;
}

void Apply(Model& model, const Settings& settings, bool paused, const std::vector<CoopHook::PlayerRow>& players) {
    if (paused) {
        model.tags.clear();  // oot-64 D-072 point 6: nothing over our own pause menu
    } else if (!settings.names) {
        for (auto& t : model.tags) {
            if (!t.ping) {
                t.name.clear();
                t.ping_ms.reset();
            }
        }
    }
    const std::size_t keep = std::min(settings.chat_lines, kChatLines);
    if (model.chat.size() > keep) {
        model.chat.erase(model.chat.begin(),
                         model.chat.begin() + static_cast<std::ptrdiff_t>(model.chat.size() - keep));
    }
    if (settings.rupees) {
        model.rupee_line = Wallets(players);
        if (!model.rupee_line.empty()) {
            model.wallets.clear();  // never twice: the line has them
        }
    } else {
        model.rupee_line.clear();
    }
    model.rupees_at = settings.rupees_at;
    model.panel_at = settings.panel_at;
    model.size = std::clamp(settings.size, kMinSize, kMaxSize);
}

std::string RupeeText(const Wallet& wallet) {
    return wallet.rupees ? std::to_string(*wallet.rupees) + (wallet.stale ? "?" : "") : "-";
}

std::u32string Decode(std::string_view s) {
    std::u32string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        int extra = 0;
        char32_t cp = 0;
        if (c < 0x80) {
            out.push_back(c);
            ++i;
            continue;
        } else if ((c & 0xe0) == 0xc0) {
            extra = 1;
            cp = c & 0x1f;
        } else if ((c & 0xf0) == 0xe0) {
            extra = 2;
            cp = c & 0x0f;
        } else if ((c & 0xf8) == 0xf0) {
            extra = 3;
            cp = c & 0x07;
        } else {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        if (i + extra >= s.size()) {  // cut off at the end
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xc0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (d & 0x3f);
        }
        constexpr char32_t kMin[] = {0, 0x80, 0x800, 0x10000};
        if (!ok || cp < kMin[extra] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

int TagScale(int picture_height, int size_percent) {
    return ScaleFor(picture_height, 0.035 * SizeFactor(size_percent));
}

int PanelScale(int picture_height, int size_percent) {
    return ScaleFor(picture_height, 0.022 * SizeFactor(size_percent));
}

int BannerScale(int picture_height, int size_percent) {
    return ScaleFor(picture_height, 0.05 * SizeFactor(size_percent));
}

int TextWidth(std::string_view utf8, int scale) {
    return static_cast<int>(Decode(utf8).size()) * CoopHudFont::kWidth * scale;
}

std::vector<Rect> Draw(const Model& model, const Box& picture, int frame_width, int frame_height) {
    std::vector<Rect> out;
    Box clip;
    clip.x = std::max(picture.x, 0);
    clip.y = std::max(picture.y, 0);
    clip.width = std::min(picture.x + picture.width, frame_width) - clip.x;
    clip.height = std::min(picture.y + picture.height, frame_height) - clip.y;
    if (clip.width <= 0 || clip.height <= 0 || picture.width <= 0 || picture.height <= 0) {
        return out;
    }
    Canvas canvas(clip, out);

    if (!model.tags.empty() && model.width > 0.0f && model.height > 0.0f) {
        const float sx = static_cast<float>(picture.width) / model.width;
        const float sy = static_cast<float>(picture.height) / model.height;
        const int scale = TagScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        for (const auto& t : model.tags) {
            if (!(t.alpha >= kMinAlpha) || !Finite(t.x) || !Finite(t.y) || !Finite(t.marker_x) ||
                !Finite(t.marker_y) || !Finite(t.marker_size) || !Finite(t.arrow_angle)) {
                continue;
            }
            const float mx = static_cast<float>(picture.x) + t.marker_x * sx;
            const float my = static_cast<float>(picture.y) + t.marker_y * sy;
            const std::uint32_t mark = MarkerColour(t.self, t.urgent, t.rgb);
            if (t.edge) {
                // A triangle at the edge pointing at the player, as the Qt layer.
                const float r = std::max(6.0f, t.marker_size * sx / 2.0f);
                Arrow(canvas, mx, my, r, t.arrow_angle, outline, mark);
            } else if (!t.head) {
                const float r = std::max(3.0f, t.marker_size * sx / 2.0f);
                canvas.Disc(mx, my, r + static_cast<float>(outline), kOutline);
                canvas.Disc(mx, my, r, mark);
            }

            std::vector<Run> runs{{Decode(t.name), t.rgb}};
            const std::string ping = PingText(t);
            if (!ping.empty()) {
                runs.push_back({Decode(" " + ping), kPing});
            }
            std::int64_t width = 0;
            for (const auto& r : runs) {
                width += static_cast<std::int64_t>(r.text.size()) * CoopHudFont::kWidth * scale;
            }
            const float ax = static_cast<float>(picture.x) + t.x * sx;
            const float ay = static_cast<float>(picture.y) + t.y * sy;
            std::int64_t left = std::lround(ax - static_cast<float>(width) / 2.0f);
            // Kept on the picture, as the Qt layer keeps it on the window.
            const std::int64_t lo = std::int64_t{picture.x} + 2 + outline;
            const std::int64_t hi = std::int64_t{picture.x} + picture.width - width - 2 - outline;
            left = std::clamp(left, lo, std::max(lo, hi));
            const std::int64_t line = std::int64_t{CoopHudFont::kHeight} * scale;
            const std::int64_t top = t.below ? std::lround(ay) : std::lround(ay) - line;
            Text(canvas, runs, left, top, scale, outline);
        }
    }

    // ORM partner map (coop_map.h): the mates over the game's map, then the
    // lines about mates elsewhere, both at the panel's text size.
    if (!model.map_dots.empty()) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const float r = static_cast<float>(MapDotRadius(picture.height));
        const std::int64_t line = std::int64_t{CoopHudFont::kHeight} * scale;
        for (const auto& d : model.map_dots) {
            if (!Finite(d.x) || !Finite(d.y) || !Finite(d.angle)) {
                continue;
            }
            const float cx = static_cast<float>(picture.x) + d.x * static_cast<float>(picture.width);
            const float cy = static_cast<float>(picture.y) + d.y * static_cast<float>(picture.height);
            const std::uint32_t fill = MarkerColour(d.self, d.urgent, d.rgb);
            if (d.edge) {
                Arrow(canvas, cx, cy, r * 1.4f, d.angle, outline, fill);
            } else {
                canvas.Disc(cx, cy, r + static_cast<float>(outline), kOutline);
                canvas.Disc(cx, cy, r, fill);
                if (d.other_floor) {
                    canvas.Disc(cx, cy, r * 0.5f, kOutline);  // a ring: another floor
                }
            }
            if (!d.label.empty()) {
                Text(canvas, {{Decode(d.label), fill}},
                     std::lround(cx + r * 1.4f) + outline + 2, std::lround(cy) - line / 2, scale, outline);
            }
        }
    }
    if (!model.notes.empty() && Finite(model.notes_left) && Finite(model.notes_bottom)) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const std::int64_t step = std::int64_t{CoopHudFont::kHeight + 1} * scale;
        const std::int64_t left =
            std::int64_t{picture.x} + std::lround(model.notes_left * static_cast<float>(picture.width));
        std::int64_t bottom =
            std::int64_t{picture.y} + std::lround(model.notes_bottom * static_cast<float>(picture.height));
        if (!model.chat.empty()) {
            // ORM: the chat rows take the bottom left corner (drawn below): the
            // elsewhere lines go up over them, half a line apart.
            const int chat_margin = std::max(4, picture.height / 45);
            const std::int64_t chat_top = std::int64_t{picture.y} + picture.height - chat_margin -
                                          step * static_cast<std::int64_t>(model.chat.size());
            bottom = std::min(bottom, chat_top - step / 2);
        }
        const auto n = static_cast<std::int64_t>(model.notes.size());
        for (std::int64_t i = 0; i < n; ++i) {
            const auto& note = model.notes[static_cast<std::size_t>(i)];
            Text(canvas, {{Decode(note.text), note.rgb}}, left, bottom - (n - i) * step, scale, outline);
        }
    }

    if (!model.notices.empty()) {
        // ORM cofre: centred, the first line's top at 12 % of the picture
        // (under the hearts and the buttons), each kept on the picture.
        const int scale = TagScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const std::int64_t line = std::int64_t{CoopHudFont::kHeight + 2} * scale;
        std::int64_t top = std::int64_t{picture.y} + std::llround(picture.height * 0.12);
        for (const auto& n : model.notices) {
            std::vector<Run> runs;
            if (!n.name.empty()) {
                runs.push_back({Decode(n.name), n.rgb});
            }
            if (!n.rest.empty()) {
                runs.push_back({Decode(n.rest), kWhite});
            }
            std::int64_t width = 0;
            for (const auto& r : runs) {
                width += static_cast<std::int64_t>(r.text.size()) * CoopHudFont::kWidth * scale;
            }
            if (width > 0) {
                const std::int64_t lo = std::int64_t{picture.x} + 2 + outline;
                const std::int64_t hi = std::int64_t{picture.x} + picture.width - width - 2 - outline;
                const std::int64_t left =
                    std::clamp(std::int64_t{picture.x} + (std::int64_t{picture.width} - width) / 2, lo,
                               std::max(lo, hi));
                Text(canvas, runs, left, top, scale, outline);
            }
            top += line;
        }
    }

    // ORM the room's age (coop_notices.h Banner): big, in the middle, over the
    // tags; a line too wide goes in two after ": ", then smaller to fit.
    if (model.banner) {
        const Banner& b = *model.banner;
        const int margin = std::max(4, picture.height / 45);
        const std::int64_t room = std::int64_t{picture.width} - 2 * margin;
        std::vector<std::vector<Run>> lines;
        const std::u32string name = Decode(b.name);
        const std::u32string rest = Decode(b.rest);
        const auto chars = [](const std::vector<Run>& runs) {
            std::int64_t n = 0;
            for (const auto& r : runs) {
                n += static_cast<std::int64_t>(r.text.size());
            }
            return n;
        };
        int scale = BannerScale(picture.height, model.size);
        {
            std::vector<Run> one;
            if (!name.empty()) {
                one.push_back({name, b.rgb});
            }
            if (!rest.empty()) {
                one.push_back({rest, kWhite});
            }
            const std::size_t cut = rest.find(U": ");
            if (chars(one) * CoopHudFont::kWidth * scale > room && cut != std::u32string::npos) {
                std::vector<Run> first;
                if (!name.empty()) {
                    first.push_back({name, b.rgb});
                }
                first.push_back({rest.substr(0, cut + 1), kWhite});
                lines.push_back(std::move(first));
                lines.push_back({{rest.substr(cut + 2), kWhite}});
            } else if (!one.empty()) {
                lines.push_back(std::move(one));
            }
        }
        if (!b.detail.empty()) {
            lines.push_back({{Decode(b.detail), kNotice}});
        }
        std::int64_t widest = 0;
        for (const auto& l : lines) {
            widest = std::max(widest, chars(l));
        }
        while (scale > 1 && widest * CoopHudFont::kWidth * scale > room) {
            --scale;
        }
        const int outline = OutlineFor(scale);
        const std::int64_t step = std::int64_t{CoopHudFont::kHeight + 3} * scale;
        std::int64_t top = std::int64_t{picture.y} + std::llround(picture.height * 0.30);
        for (const auto& l : lines) {
            const std::int64_t width = chars(l) * CoopHudFont::kWidth * scale;
            const std::int64_t lo = std::int64_t{picture.x} + 2 + outline;
            const std::int64_t hi = std::int64_t{picture.x} + picture.width - width - 2 - outline;
            const std::int64_t left = std::clamp(
                std::int64_t{picture.x} + (std::int64_t{picture.width} - width) / 2, lo, std::max(lo, hi));
            Text(canvas, l, left, top, scale, outline);
            top += step;
        }
    }

    // The panel (with its wallet list) and, ORM (D-R72, docs/HUD_AJUSTES.md),
    // the rupee line, each in its place; a block at the bottom left goes over
    // the chat rows, two blocks in one place go one after the other.
    if (!model.panel.empty() || !model.wallets.empty() || !model.rupee_line.empty()) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const int margin = std::max(4, picture.height / 45);
        const std::int64_t line_height = std::int64_t{CoopHudFont::kHeight + 1} * scale;
        const std::int64_t cell = std::int64_t{CoopHudFont::kWidth} * scale;
        std::array<std::int64_t, kPlaces> used{};  // pixels taken from each place's edge
        if (!model.chat.empty()) {
            used[static_cast<std::size_t>(Place::bottom_left)] =
                line_height * static_cast<std::int64_t>(model.chat.size()) + line_height / 2;
        }
        if (!model.corner.empty()) {  // ORM "Guardado": a block at the bottom right goes over it
            used[static_cast<std::size_t>(Place::bottom_right)] = line_height + line_height / 2;
        }
        if (!model.stats.empty()) {  // ORM /fps, /stats...: a block at the top right goes under them
            used[static_cast<std::size_t>(Place::top_right)] =
                line_height * static_cast<std::int64_t>(model.stats.size()) + line_height / 2;
        }
        auto place_rows = [&](Place at, const std::vector<Row>& rows) {
            if (rows.empty()) {
                return;
            }
            std::int64_t height = 0;
            for (const auto& r : rows) {
                height += r.gap + line_height;
            }
            std::int64_t& taken = used[static_cast<std::size_t>(at)];
            std::int64_t top = AtBottom(at) ? std::int64_t{picture.y} + picture.height - margin - taken - height
                                            : std::int64_t{picture.y} + margin + taken;
            taken += height + line_height / 2;
            const std::int64_t lo = std::int64_t{picture.x} + margin;
            for (const auto& r : rows) {
                top += r.gap;
                std::int64_t left = lo;
                if (AtRight(at)) {
                    left = std::max(lo, std::int64_t{picture.x} + picture.width - margin - r.width);
                } else if (AtCentre(at)) {
                    left = std::max(lo, std::int64_t{picture.x} + (std::int64_t{picture.width} - r.width) / 2);
                }
                if (!r.text.empty()) {
                    Text(canvas, r.text, left, top, scale, outline);
                }
                for (const Wallet* w : r.wallets) {
                    left += (WalletEntry(canvas, *w, left, top, scale, outline) + kWalletGapCells) * cell;
                }
                top += line_height;
            }
        };

        std::vector<Row> panel;
        for (const auto& line : model.panel) {
            const std::uint32_t rgb = line.rgb ? (line.dim ? Dimmed(*line.rgb) : *line.rgb)
                                               : (line.dim ? kDim : kWhite);
            Row r;
            r.text = {{Decode(line.text), rgb}};
            r.width = static_cast<std::int64_t>(r.text[0].text.size()) * cell;
            panel.push_back(std::move(r));
        }
        // ORM wallet list (INVENTARIO.md, "Cartera de cada uno"): name, a rupee, the count,
        // half a line under the panel's lines.
        for (const auto& w : model.wallets) {
            Row r;
            r.wallets = {&w};
            r.width = WalletCells(w) * cell;
            r.gap = (&w == &model.wallets.front() && !model.panel.empty()) ? line_height / 2 : 0;
            panel.push_back(std::move(r));
        }
        place_rows(model.panel_at, panel);

        // ORM rupee line: the entries side by side, a new line when the next does not fit.
        std::vector<Row> line;
        const std::int64_t room = std::int64_t{picture.width} - 2 * std::int64_t{margin};
        for (const auto& w : model.rupee_line) {
            const std::int64_t width = WalletCells(w) * cell;
            if (!line.empty() && line.back().width + kWalletGapCells * cell + width <= room) {
                line.back().wallets.push_back(&w);
                line.back().width += kWalletGapCells * cell + width;
                continue;
            }
            Row r;
            r.wallets = {&w};
            r.width = width;
            line.push_back(std::move(r));
        }
        place_rows(model.rupees_at, line);
    }

    // ORM /fps, /ping, /red, /stats (oot-remake docs/COMANDOS.md): top right,
    // right-aligned, the panel's size.
    if (!model.stats.empty()) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const int margin = std::max(4, picture.height / 45);
        const std::int64_t line = std::int64_t{CoopHudFont::kHeight + 1} * scale;
        std::int64_t top = std::int64_t{picture.y} + margin;
        for (const auto& text : model.stats) {
            const std::u32string decoded = Decode(text);
            const std::int64_t width = static_cast<std::int64_t>(decoded.size()) * CoopHudFont::kWidth * scale;
            const std::int64_t left =
                std::max(std::int64_t{picture.x} + margin, std::int64_t{picture.x} + picture.width - margin - width);
            Text(canvas, {{decoded, kWhite}}, left, top, scale, outline);
            top += line;
        }
    }

    // ORM chat in the picture (docs/COMUNICACION.md): bottom left, the newest
    // lowest, the panel's size.
    if (!model.chat.empty()) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const int margin = std::max(4, picture.height / 45);
        const std::int64_t line = std::int64_t{CoopHudFont::kHeight + 1} * scale;
        const std::int64_t room = std::int64_t{picture.width} - 2 * std::int64_t{margin};
        const auto max_chars = static_cast<std::size_t>(
            std::max<std::int64_t>(1, room / (std::int64_t{CoopHudFont::kWidth} * scale)));
        std::int64_t top = std::int64_t{picture.y} + picture.height - margin -
                           line * static_cast<std::int64_t>(model.chat.size());
        for (const auto& row : model.chat) {
            std::vector<Run> runs;
            if (!row.name.empty()) {
                runs.push_back({Decode(row.name), row.name_rgb});
            }
            runs.push_back({Decode(row.text), row.text_rgb});
            Fit(runs, max_chars);
            Text(canvas, runs, std::int64_t{picture.x} + margin, top, scale, outline);
            top += line;
        }
    }

    // ORM "Guardado" (coop_notices.h Corner): the bottom right corner.
    if (!model.corner.empty()) {
        const int scale = PanelScale(picture.height, model.size);
        const int outline = OutlineFor(scale);
        const int margin = std::max(4, picture.height / 45);
        const std::u32string text = Decode(model.corner);
        const std::int64_t width = static_cast<std::int64_t>(text.size()) * CoopHudFont::kWidth * scale;
        const std::int64_t line = std::int64_t{CoopHudFont::kHeight} * scale;
        const std::int64_t left = std::max(std::int64_t{picture.x} + margin,
                                           std::int64_t{picture.x} + picture.width - margin - width);
        const std::int64_t top = std::int64_t{picture.y} + picture.height - margin - line;
        Text(canvas, {{text, kWhite}}, left, top, scale, outline);
    }
    return out;
}

} // namespace OrmHud
