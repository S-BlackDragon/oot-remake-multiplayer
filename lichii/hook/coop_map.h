// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM partner map (oot-remake docs/MAPA_COMPANEROS.md): knowing where the
// other players are, as in oot-64. Three parts, all drawn by the in-game HUD
// (orm_hud.h) and on unless LICHII_COOP_MAP=0 (2026-11-11: "Sí hay
// que hacer que aparezcan en el mapa los jugadores"; oot-remake D-R70):
//
// 1. Pointers: a mate in our place whom the name tags leave out (behind the
//    camera, far off screen, or too far away for a tag) gets an arrow at the
//    edge of the picture, in his colour, with his name and distance; one too
//    far away but in view gets a dot and his name. They are added to the
//    hook's markers (coop_hook.cpp, Place), so the Qt layer shows them too.
// 2. Map dots: the mates over the game's own minimap and pause map. The game
//    draws its map icons from its own data (MapParam, one player icon), so
//    nothing is added to the game: the HUD draws a dot where the mate would be
//    on that map, from a table built on each PC from its own romfs
//    (oot-remake tools/romfs/map_table.py) and checked in game. In a dungeon
//    a mate on another floor gets a ring instead of a dot, and on the pause
//    map his floor after his name ("Ana 2F").
// 3. Elsewhere: one line per mate in another place, "Ana: en Bosque Kokiri".
//
// Everything here is portable (tests/lichii/map_test.cpp); coop_map_game.cpp
// is the glue to the live hook.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/overlay/overlay.hpp"
#include "coop_hook.h"
#include "orm_hud.h"

namespace CoopMap {

using Vec3 = std::array<float, 3>;

// ---- Switches (any thread) ----

// LICHII_COOP_MAP, read once: on unless it is "0" (not set, empty or
// anything else: on).
bool On();
// The rule On() reads the variable with (for the tests).
bool OnFrom(const char* value);
// LICHII_COOP_MAP_DEBUG: our own dot on the maps too (yellow), to line the
// table up with the game's player icon. Read once.
bool Debug();
// The pause map overlay (F10): the game's pause menu is not read yet, so the
// player says when the map is open. Shown: the dots go to the pause map's
// place, the name tags are left out (they would cover the menu).
bool PauseShown();
void SetPauseShown(bool shown);
void TogglePause();

// ---- Place names ----

// What players call a scene, in Spanish: "KokiriForest_Child" -> "Bosque
// Kokiri", "InsideTheDekuTree" -> "Gran Árbol Deku" (UTF-8). A scene not in
// the list is shown as its words: "LakeHylia_Fishing" -> "Lake Hylia Fishing".
std::string PlaceName(std::string_view scene);

// ---- Floors (dungeons) ----

// A dungeon's floors as its map data has them: the lowest floor's number
// (-2: the lowest is B2) and each floor's bottom height, lowest first.
struct Floors {
    int min_floor = 0;
    std::vector<float> bottoms;
};
// The floor a height is on: the highest floor whose bottom is at or below
// `y`; under the lowest floor, the lowest. Nothing with no floors or a
// height that is not finite.
std::optional<int> FloorOf(const Floors& floors, float y);
// The floor as the game's map names it: 0 -> "1F", 2 -> "3F", -1 -> "B1".
std::string FloorLabel(int floor);

// ---- The map table ----

enum class Which : std::uint8_t { mini, pause };

// What part of the world a map shows. Unrotated, +x is to the right and +z
// down (north up); `rotation` turns the content clockwise (radians) and
// `mirror` flips x first. `span` is the world units across the map's height
// on the screen. `follow`: centred on our own Link (a minimap that scrolls),
// else on centre_x, centre_z.
struct View {
    float centre_x = 0.0f, centre_z = 0.0f;
    float span = 0.0f;
    float rotation = 0.0f;
    bool mirror = false;
    bool follow = false;
};

// Where the game draws a map, in fractions of its picture (0 to 1, from the
// top left). `round`: a round map; dots are kept in its inscribed ellipse.
struct Screen {
    float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;
    bool round = false;
};

struct Table {
    std::map<std::string, Floors, std::less<>> floors;
    std::map<std::string, View, std::less<>> mini, pause;
    std::optional<View> mini_any;  // "mini * ...": every scene without its own line
    std::optional<Screen> mini_screen, pause_screen;
    // The bottom left corner of the "elsewhere" lines, fractions of the picture.
    float notes_left = 0.012f, notes_bottom = 0.96f;

    // The view of `which` for `scene`, or nothing (no dots there).
    const View* ViewFor(Which which, std::string_view scene) const;
    const Screen* ScreenFor(Which which) const;
    const Floors* FloorsFor(std::string_view scene) const;
};

// The table's text, one entry a line; '#' starts a comment. Numbers are
// world units, fractions of the picture, or degrees (rotation):
//   floors <scene> <min_floor> <bottom> [<bottom> ...]
//   mini <scene|*> follow <span> [<rotation> [mirror]]
//   mini <scene|*> <centre_x> <centre_z> <span> [<rotation> [mirror]]
//   pause <scene> <centre_x> <centre_z> <span> [<rotation> [mirror]]
//   screen <mini|pause> <left> <top> <width> <height> [round]
//   notes <left> <bottom>
// A bad line is left out and named in `errors` ("line 4: ...").
struct Parsed {
    Table table;
    std::vector<std::string> errors;
};
Parsed Parse(std::string_view text);

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md, MAPA_COMPANEROS.md
// "Calibrar en partida"): Parse already leaves out what cannot be right (a
// box off the picture, a view with no span, floors that do not rise).
// The path the dots take in `scene` for `which` map, for the self-check line
// (CoopVCheck "map <mini|pause> <scene>"): ok with the view and the box the
// table gives, or fallback with why there are no dots (edge arrows and the
// "elsewhere" lines still show).
struct MapPath {
    bool ok = false;
    std::string detail;
};
MapPath PathFor(const Table& table, Which which, std::string_view scene);

// ---- World to map ----

// A point on a map, in fractions of the picture. `edge`: it was outside the
// map and is kept on the map's rim; `angle` (radians, 0 right, pi/2 down)
// points from the map's centre towards it.
struct OnMap {
    float x = 0.0f, y = 0.0f;
    bool edge = false;
    float angle = 0.0f;
};
// Where `world` is on the map. `picture_aspect` is the picture's width over
// its height; `own` our Link's position (for a following view). Nothing for a
// view with no span, a screen with no size, a following view without `own`,
// or numbers that are not finite.
std::optional<OnMap> ToMap(const View& view, const Screen& screen, float picture_aspect,
                           const Vec3& world, const std::optional<Vec3>& own);

// The point where a ray from (cx, cy) along (dx, dy) leaves a box of half
// sizes hx, hy around it, or an ellipse with those half axes (`round`). The
// centre itself for a ray of length 0 or a box of size 0.
std::array<float, 2> Rim(float cx, float cy, float hx, float hy, float dx, float dy, bool round);

// ---- Pointers (the name tags' edge arrows, for everyone) ----

struct PointerSettings {
    // How far in from the picture's edge the arrow sits, pixels of the scene
    // target (the overlay's own arrows sit at 16).
    float edge_margin = 30.0f;
    // The arrow across, as a share of the scene target's height.
    float size_share = 0.024f;
    // A mate nearer than this (world units, to our Link or to the camera) is
    // not pointed at: he is right there.
    float near = 0.75f;
    // Gap between the arrow and its tag, pixels.
    float tag_gap = 4.0f;
};

// Markers for the mates in our place that `drawn` (the overlay's markers)
// leaves out: an arrow at the edge for one off screen or behind the camera,
// a dot for one in view but too far for a tag. Tag: "Ana 52 m" (the distance
// to our Link when known), or "Ana (<note>)" when the mate has a note (a
// downed mate's "caído, 23 m"). Nothing for a bad camera or a mate in another
// place, with no scene, faded out or not finite. In the overlay's order
// (lowest number first); the caller puts them under the name tags.
std::vector<CoopHook::Marker> Pointers(const coop::overlay::Camera& camera,
                                       std::string_view local_scene,
                                       const std::optional<Vec3>& own,
                                       const std::vector<coop::overlay::Shown>& mates,
                                       const std::vector<std::uint8_t>& drawn,
                                       const PointerSettings& settings = {});

// Takes out of the overlay's markers the name tags in view that the in-game
// HUD does not draw (alpha under OrmHud::kMinAlpha: a tag fading out with
// distance, about 25 to 40 m with the hook's settings), so that Pointers
// gives those mates their dot; edge arrows and ourselves (number 0) stay.
// Returns the numbers of the markers left: Pointers' `drawn`.
std::vector<std::uint8_t> KeepSolid(std::vector<CoopHook::Marker>& items);

// ---- Map dots and the elsewhere lines ----

struct Input {
    std::optional<std::string> scene;  // ours
    std::optional<Vec3> own;           // our Link's position
    std::vector<CoopHook::Mate> mates;
    float picture_aspect = 16.0f / 9.0f;
    Which which = Which::mini;
    bool self = false;  // our own dot too (Debug)
};

// The dots for the map `input.which` shows, in our place only: mates there
// (and us, with `self`), each in its colour; rings for another floor; names
// only on the pause map. Nothing without a view and a screen for our scene.
std::vector<OrmHud::MapDot> Dots(const Table& table, const Input& input);

// "Ana: en Bosque Kokiri" for each mate in another place (or with our own
// place not known), lowest number first, in the mate's colour.
std::vector<OrmHud::Note> Elsewhere(const Input& input);

// Adds the map dots and the elsewhere lines to the HUD's model for this
// frame; with the pause map shown (F10), the tags are left out. Live hook
// (coop_map_game.cpp, emulator only); does nothing when the map is off.
void AddTo(OrmHud::Model& model);

} // namespace CoopMap
