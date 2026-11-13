// The co-op entry points the patched emulator calls. Built into the
// emulator's core library only when LICHII_COOP is on.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {
class System;
}

namespace CoopHook {

// Reads the settings (coop.ini in the config folder, then LICHII_COOP_*
// environment variables) and, when a server is set, starts the network
// thread. Safe to call more than once; OnFrame calls it too. Does nothing
// after Shutdown.
void Start();

// Runs once per emulated frame, after the guest's present, on the thread
// that handles the present.
void OnFrame(Core::System& system);

// Says goodbye to the server and joins the network thread. Call once, when
// the program closes (after emulation has stopped).
void Shutdown();

// What the co-op window shows: one line of text each. Empty when co-op is
// off. Any thread.
struct OverlayLine {
    std::string text;
    bool dim = false;  // a player that is not drawn (other scene, no position)
};
std::vector<OverlayLine> OverlayLines();

// A player drawn over the game, in the pixels of `Markers::width` x `height`
// (the game's scene target; the window scales it to the picture).
struct Marker {
    float x = 0.0f, y = 0.0f;  // centre, from the top left, y down
    float size = 0.0f;         // across
    float alpha = 1.0f;
    bool edge = false;         // just off screen: an arrow at the edge
    float arrow_angle = 0.0f;  // radians, 0 right, pi/2 down
    bool self = false;         // the local player (LICHII_COOP_DEBUG_CAMERA only)
    std::string tag;           // the name tag
    float tag_x = 0.0f, tag_y = 0.0f;  // bottom centre of the text, or top when below
    bool tag_below = false;
    bool tag_head = false;     // over the player's head: drawn as a name tag, no dot
    std::uint8_t number = 0;   // the player's number (0: yourself, debug camera)
    std::array<std::uint8_t, 3> color{};  // ORM: the player's colour (red, green, blue)
    bool urgent = false;       // ORM: a downed mate (coop::overlay::Shown::urgent)
    // ORM quick messages (docs/COMUNICACION.md): not a player but the marker
    // a "¡Aquí!" or "¡Ayuda!" left where `number` stood, for 5 s; `tag` is
    // its text ("¡Aquí! Ana") and `color` the sender's. No ping in ms and
    // never drawn as downed.
    bool ping = false;
};
struct Markers {
    float width = 0.0f, height = 0.0f;
    std::vector<Marker> items;  // draw in order: far to near
    // ORM (oot-64 D-072 point 6, coop_menus.h): our own pause menu is open
    // this frame: neither the HUD nor the Qt layer draws a tag or marker.
    bool paused = false;
};
// The markers for the last frame, or nothing when the game's camera is not
// known (the panel is then all there is). Any thread.
std::optional<Markers> CurrentMarkers();

// ORM partner map (coop_map.h): where everyone is, for the HUD's map dots and
// elsewhere lines. Any thread.
struct Mate {
    std::uint8_t number = 0;
    std::string name;                         // empty: "P<number>"
    std::array<float, 3> position{};          // world, as presence shows it
    std::optional<std::string> scene;         // none: not known yet
    std::array<std::uint8_t, 3> color{};      // the player's colour
    bool urgent = false;                      // a downed mate
};
struct Mates {
    std::optional<std::string> scene;              // ours
    std::optional<std::array<float, 3>> position;  // ours
    std::vector<Mate> others;                      // lowest number first
    bool camera = false;   // the game's camera is known (a scene is drawn, not a menu)
    float aspect = 0.0f;   // the scene target's width over its height; 0 without a camera
};
// The last frame's, or nothing when the game is not running or co-op is off.
std::optional<Mates> CurrentMates();

// Chat and the player list, for the co-op window. Any thread.
//
// Sends `text` as chat. False when co-op is off, not connected, or the text
// is refused (empty, too long, control characters); then nothing is added.
// The server does not echo chat, so a sent line is added here as "you: ...".
bool SendChat(const std::string& text);
// The last chat lines and room notices (coop_chat.h), oldest first; text and
// when it came.
struct ChatLine {
    std::string text;
    bool notice = false;  // "Saria joined", "connection lost", not chat
    std::uint64_t age_ms = 0;
    // ORM (docs/COMUNICACION.md), for the in-game HUD: the sender's number
    // (ours for our own line, 0 for a notice), the name `text` starts with
    // ("Ana" of "Ana: hola"; empty for a notice), whether it is a quick
    // message, and the sender's colour (its PLAYER_LOOK, else its number's;
    // none for a notice).
    std::uint8_t player = 0;
    std::string name;
    bool quick = false;
    std::optional<std::array<std::uint8_t, 3>> color;
};
std::vector<ChatLine> ChatLines();

// ORM "downed, not dead" (coop_downed_game.h): the one line about it, shown
// over the game ("hold A to revive ...", "... is reviving you 40 %"), UTF-8;
// empty for nothing. Any thread. ORM: a lost connection comes first on the
// same line ("Sin conexion con el servidor: reconectando (n s)"), so it shows
// with the panel hidden (F9) until the connection is back.
std::string DownedBanner();
// Everyone in the room, this player first, then by number.
struct PlayerRow {
    std::uint8_t number = 0;
    std::string name;
    bool self = false;
    std::string state;                  // connected, reconnecting, quiet, ...
    std::optional<std::uint32_t> ms;    // self: round trip; others: sample delay
    std::optional<std::string> scene;
    // ORM (oot-remake docs/INVENTARIO.md, "Cartera de cada uno"): the rupees
    // in the player's own wallet, from its last PLAYER_EQUIP (ours: the one we
    // sent); nothing until the first. Shown by the HUD's wallet list.
    std::optional<std::uint32_t> rupees;
    // ORM (RIESGOS_RESUELTOS.md): `rupees` is an older count of a quiet player (no
    // sample for 1 s): the HUD draws it with a '?' (coop::host::wallet_shown).
    bool rupees_stale = false;
    // ORM: the player's colour (red, green, blue): its PLAYER_LOOK, else its
    // number's default; nothing when not known (no room).
    std::optional<std::array<std::uint8_t, 3>> color;
};
std::vector<PlayerRow> PlayerList();

} // namespace CoopHook
