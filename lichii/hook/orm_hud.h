// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM in-game HUD (oot-remake docs/HUD_EN_JUEGO.md): the name tags (with
// ping) and the information panel drawn INSIDE the game's picture, by the
// emulator's present pass (coop_hud_vk.cpp, patch 0100-orm-hud), instead of the Qt
// layer over the window (coop_window.cpp). Off unless LICHII_ORM_HUD=1 (or the old
// LICHII_COOP_HUD=1; upstream's coop_hud.* is another thing: hearts and the game look); with
// it on the Qt panel and markers are not made, and F9 shows or hides this
// panel instead.
//
// Everything here is portable (tested in tests/orm/orm_hud_test.cpp): the
// model built from what the hook already works out (CoopHook::Markers, the
// player list, the panel lines) and its layout as plain filled rectangles in
// the frame's pixels, text from a 7x13 bitmap font (coop_hud_font.h) at a
// whole-number scale that grows with the picture's height. Rectangles because
// the cheapest in-frame drawing needs no shader, no pipeline and no buffer:
// vkCmdClearAttachments inside a render pass on the finished frame. The cost
// is no blending: a tag is drawn whole or not at all, and the panel has no
// see-through background (outlined text instead).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop_hook.h"
#include "coop_hud_settings.h"  // ORM: each player's HUD settings (docs/HUD_AJUSTES.md)

namespace OrmHud {

inline constexpr std::uint32_t kWhite = 0xffffff;
inline constexpr std::uint32_t kOutline = 0x000000;
inline constexpr std::uint32_t kSelf = 0xffe178;     // yourself (debug camera), as the Qt layer
inline constexpr std::uint32_t kPing = 0xb4b4b4;     // the ping after a name
inline constexpr std::uint32_t kDim = 0xaaaaaa;      // a panel line for a player not drawn
inline constexpr std::uint32_t kMarker = 0x5ac8ff;   // an edge arrow or a dot, no colour known
inline constexpr std::uint32_t kMarkerSelf = 0xffd23c;
// A downed mate, to be found (CoopHook::Marker::urgent): name and marker in
// orange-red, as the Qt layer.
inline constexpr std::uint32_t kUrgent = 0xff966e;
inline constexpr std::uint32_t kMarkerUrgent = 0xff6e46;
// ORM wallet list (oot-remake docs/INVENTARIO.md, "Cartera de cada uno"):
// the rupee drawn before each player's count, and the count.
inline constexpr std::uint32_t kRupee = 0x32c850;
inline constexpr std::uint32_t kRupeeCount = kWhite;
// ORM chat in the picture (docs/COMUNICACION.md): notices (joins, leaves,
// downed, connection) in yellow, as the Qt chat lines.
inline constexpr std::uint32_t kNotice = 0xffdc78;
// A chat line shows this long, as the Qt layer's (no fade: no blending).
inline constexpr std::uint64_t kChatShowMs = 8000;
// At most this many chat lines, the newest (the chat log keeps 6); fewer
// with LICHII_ORM_HUD_CHAT_LINES or /hud chat (coop_hud_settings.h).
inline constexpr std::size_t kChatLines = kMaxChatRows;
// Below this a tag is not drawn (no blending: drawn whole or not at all).
inline constexpr float kMinAlpha = 0.5f;
// At most this many rectangles a frame (about a dozen tags and a long panel);
// the rest is left out.
inline constexpr std::size_t kMaxRects = 6000;

// ---- Switches (any thread) ----

// The reading of every switch of our hook code (coop_switch.h), off by
// default: 1/true/yes/on turn it on, 0/false/no/off and unknown values not.
bool SwitchOn(const char* value);
// LICHII_ORM_HUD (else the old LICHII_COOP_HUD), read once.
bool On();
// The panel (F9), shown at start when LICHII_COOP_PANEL is on, as the Qt one.
bool PanelShown();
void SetPanelShown(bool shown);
void TogglePanel();
// ORM: the Qt chat box is open (coop_window.cpp). Its Qt lines show every
// kept line then, so the HUD leaves its chat lines out (never twice).
bool Typing();
void SetTyping(bool typing);

// ---- The model ----

struct Tag {
    std::uint8_t number = 0;  // 0: yourself (LICHII_COOP_DEBUG_CAMERA)
    std::string name;         // UTF-8
    // The name's colour, 0xRRGGBB: the player's (CoopHook::Marker::color,
    // PLAYER_LOOK or its number's), kSelf for yourself, kUrgent for a downed
    // mate, white when the hook gives none (all zero). The edge arrow or dot
    // takes the same colour (kMarker for white, kMarkerSelf, kMarkerUrgent).
    std::uint32_t rgb = kWhite;
    std::optional<std::uint32_t> ping_ms;
    bool self = false;
    bool urgent = false;  // a downed mate
    // In the pixels of Model::width x height (the game's scene target), y
    // down. The text's bottom centre, or its top centre when `below`.
    float x = 0.0f, y = 0.0f;
    bool below = false;
    // The marker under the tag: an arrow at the edge (`edge`) or, with no
    // head height known (`!head`), a dot.
    bool head = false;
    bool edge = false;
    float arrow_angle = 0.0f;  // radians, 0 right, pi/2 down
    float marker_x = 0.0f, marker_y = 0.0f, marker_size = 0.0f;
    float alpha = 1.0f;
    // ORM quick messages: a "¡Aquí!" marker (CoopHook::Marker::ping), in the
    // sender's colour, with no ping in ms; `number` is the sender's.
    bool ping = false;
};

struct PanelLine {
    std::string text;
    bool dim = false;
    // The player's colour for this line (CoopHook::OverlayLine::color), 0xRRGGBB;
    // none: white, or kDim when `dim`. A dim coloured line is drawn darker.
    std::optional<std::uint32_t> rgb;
};

// ORM (oot-remake docs/INVENTARIO.md, "Cartera de cada uno";
// 2026-11-11): one connected player's wallet, under the panel: the name in
// the player's colour, a rupee, the count ("-" while not known).
struct Wallet {
    std::uint8_t number = 0;
    std::string name;  // UTF-8; "P<number>" when the room gives none
    std::uint32_t rgb = kWhite;
    std::optional<std::uint32_t> rupees;
    bool self = false;
    // ORM (RIESGOS_RESUELTOS.md): a quiet player's last count: drawn "87?".
    bool stale = false;
};
// ORM partner map (coop_map.h): a mate over the game's minimap or pause map.
struct MapDot {
    float x = 0.0f, y = 0.0f;  // fractions of the picture, from the top left
    std::uint32_t rgb = kWhite;
    bool self = false;         // ourselves (LICHII_COOP_MAP_DEBUG): kMarkerSelf
    bool urgent = false;       // a downed mate: kMarkerUrgent
    bool other_floor = false;  // another floor of the dungeon: a ring, not a dot
    bool edge = false;         // off the map: a small arrow on its rim
    float angle = 0.0f;        // the arrow's direction, radians, 0 right, pi/2 down
    std::string label;         // after the dot (pause map): "Ana", "Ana 2F"
};
// ORM partner map: a line about a mate in another place ("Ana: en Bosque
// Kokiri"), in his colour.
struct Note {
    std::string text;
    std::uint32_t rgb = kWhite;
};
// ORM cofre (coop_notices.h): a short line in the top middle of the picture,
// "<nombre> encontró <objeto>", the name in the player's colour.
struct Notice {
    std::uint8_t number = 0;
    std::string name;            // UTF-8, in `rgb`; "" none
    std::string rest;            // UTF-8, white
    std::uint32_t rgb = kWhite;
};
// ORM the room's age (coop_notices.h Banner, 2026-11-13): the big notice
// in the middle of the picture, the name in the player's colour, the rest
// white, and a second line under it (a countdown, when we will move) yellow.
struct Banner {
    std::uint8_t number = 0;
    std::string name;            // UTF-8, in `rgb`; "" none
    std::string rest;            // UTF-8, white
    std::string detail;          // UTF-8, kNotice; "" none
    std::uint32_t rgb = kWhite;
};
// ORM: one chat line in the picture's bottom left corner: the sender's name
// in his colour, then the rest ("": hola") in white, or in his colour too
// for a quick message; a notice is all `text`, in kNotice.
struct ChatRow {
    std::string name;  // empty: none (a notice, a line with no sender)
    std::string text;
    std::uint32_t name_rgb = kWhite;
    std::uint32_t text_rgb = kWhite;
};

struct Model {
    float width = 0.0f, height = 0.0f;  // the scene target; 0 with no tags
    std::vector<Tag> tags;              // far to near
    std::vector<PanelLine> panel;       // empty: no panel
    std::vector<Wallet> wallets;        // with the panel only; empty: no list
    // ORM partner map (coop_map.h): dots over the game's map, drawn after the
    // tags, and the elsewhere lines, stacked up from their bottom left corner
    // (fractions of the picture).
    std::vector<MapDot> map_dots;
    std::vector<Note> notes;
    float notes_left = 0.012f, notes_bottom = 0.96f;
    std::vector<Notice> notices;        // oldest first; drawn under each other
    std::optional<Banner> banner;       // ORM the room's age: the big centred notice
    std::vector<ChatRow> chat;          // ORM: oldest first; empty: none
    // ORM (D-R72, docs/HUD_AJUSTES.md): everyone's rupees on one fixed line
    // (more when it does not fit), at `rupees_at`, with or without the panel;
    // empty: none. With it the panel does not list them again.
    std::vector<Wallet> rupee_line;
    Place rupees_at = Place::top;
    Place panel_at = Place::top_left;  // where the panel and its wallet list go
    int size = 100;                    // text size, percent (Settings::size)
    // ORM "Guardado" (coop_notices.h Corner): one word in the bottom right
    // corner, at the panel's size; "" none.
    std::string corner;
    // ORM /fps, /ping, /red, /stats (oot-remake docs/COMANDOS.md): small lines
    // in the top right corner, right-aligned, top to bottom (a block placed at
    // the top right goes under them).
    std::vector<std::string> stats;
    bool Empty() const {
        return tags.empty() && panel.empty() && wallets.empty() && map_dots.empty() && notes.empty() &&
               notices.empty() && !banner && chat.empty() && rupee_line.empty() && corner.empty() &&
               stats.empty();
    }
};

// A dot over the game's map: its radius in pixels for a picture this tall.
int MapDotRadius(int picture_height);

// The model from what the hook has: the markers (none when the camera is not
// known), the player list (pings by number: our own round trip for
// ourselves, the newest sample's delay for the others, shown with "~"), and
// the panel lines (only when `panel_shown`). ORM: with the panel, also the
// wallet list, one row per player of the list in its order (ourselves
// first), when at least one of them has a count.
Model Build(const std::optional<CoopHook::Markers>& markers,
            const std::vector<CoopHook::PlayerRow>& players,
            const std::vector<CoopHook::OverlayLine>& panel_lines, bool panel_shown);

// ORM: the chat rows from the hook's chat lines (CoopHook::ChatLines): the
// ones younger than kChatShowMs, at most the kChatLines newest.
std::vector<ChatRow> BuildChat(const std::vector<CoopHook::ChatLine>& lines);

// ORM: one wallet row per player of the list, in its order, when at least
// one of them has a count; none otherwise (Build's list under the panel).
std::vector<Wallet> Wallets(const std::vector<CoopHook::PlayerRow>& players);

// ORM (docs/HUD_AJUSTES.md): this player's settings and our pause applied to
// the model of this frame, after everything else is in it:
// - `paused` (our own pause menu, CoopHook::Markers::paused; oot-64 D-072
//   point 6): no tags at all (names, edge arrows, quick markers, ourselves);
//   the map dots stay (the pause map shows the mates);
// - names off: the tags keep their marker (edge arrow, dot) but lose the name
//   and the ping; a quick message's marker is kept as it is;
// - the chat keeps its `chat_lines` newest rows;
// - rupees on: `rupee_line` from the player list (Wallets) and no wallet list
//   under the panel; off: the panel's list as before;
// - the places and the size, for Draw.
void Apply(Model& model, const Settings& settings, bool paused, const std::vector<CoopHook::PlayerRow>& players);

// ORM: a wallet's count as drawn: "87", "87?" for a quiet player's last
// count, "-" while not known.
std::string RupeeText(const Wallet& wallet);

// The ping text after a tag's name: "~45 ms" (others) or "45 ms" (self).
std::string PingText(const Tag& tag);

// The model for this frame, from the live hook (coop_hud_game.cpp, emulator
// only); nothing when the HUD is off or there is nothing to draw. The player
// list and the panel are refreshed every 200 ms, the tags every call.
std::optional<Model> Current();

// ---- Layout ----

// A rectangle in a frame's pixels, x right and y down.
struct Box {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
};
// A filled rectangle, 0xRRGGBB, opaque.
struct Rect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::uint32_t rgb = 0;
};

// UTF-8 to codepoints; a broken sequence is one U+FFFD each.
std::u32string Decode(std::string_view utf8);
// The font pixel size (whole number) for a picture this tall: tags 3.5 % of
// the height in line height, the panel 2.2 %, both 1 to 8. ORM: times
// `size_percent` (Settings::size) before rounding.
int TagScale(int picture_height, int size_percent = 100);
int PanelScale(int picture_height, int size_percent = 100);
// ORM the banner (Model::banner): 5 % of the height in line height, 1 to 8.
int BannerScale(int picture_height, int size_percent = 100);
// Pixels across a line of text at `scale` (every glyph is the same width).
int TextWidth(std::string_view utf8, int scale);

// Everything to draw this frame, in order (later rectangles cover earlier
// ones): for each tag, far to near, its outline (black) then its fill; then
// the map dots (ORM partner map) with their labels, the elsewhere lines, the
// notices, centred, from 12 % of the picture's height down (tag size); then
// (ORM) the banner, centred with its top at 30 % of the height, BannerScale,
// split after ": " in two lines when it does not fit, smaller still when one
// line does not, and its detail line under it; then the
// panel in the picture's top left corner (ORM: Model::panel_at), and under it
// (half a line apart) the wallet list at the panel's scale: per row the name, a
// space, a rupee one glyph wide (kRupee, outlined), a space, the count; then
// (ORM) the rupee line at Model::rupees_at, the same entries side by side two
// glyphs apart, wrapped when the picture is too narrow, after the panel when
// both are in one place (under it at the top, over it at the bottom); then the
// chat rows in its bottom left corner, the newest lowest, a row too long for
// the picture cut with "..." (a block at the bottom left goes over them); last
// (ORM) the corner word in its bottom right corner, white, the panel's size and
// margin (a block at the bottom right goes over it). Every text size times
// Model::size. `picture` is where the game's
// picture is in the frame (letterbox bars excluded); nothing is drawn outside
// it or outside the frame. At most kMaxRects.
std::vector<Rect> Draw(const Model& model, const Box& picture, int frame_width, int frame_height);

} // namespace OrmHud
