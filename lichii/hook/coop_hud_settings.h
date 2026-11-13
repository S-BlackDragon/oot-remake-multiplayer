// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: each player's own settings of the in-game HUD (orm_hud.h; oot-remake
// docs/HUD_AJUSTES.md, oot-64 D-071). They change only what this PC draws,
// never the shared game, so every player may pick his own: hide the names
// over the others, the text size, where the panel (F9) and the fixed line of
// everyone's rupees go, how many chat lines show, and that line on or off.
//
// Read once from the environment (the kit's coop-options.ini through
// play.ps1):
//   LICHII_ORM_HUD_NAMES=1        names over the others (0: markers only)
//   LICHII_ORM_HUD_RUPEES=1       everyone's rupees on a fixed line (D-R72)
//   LICHII_ORM_HUD_RUPEES_AT=top  where that line goes
//   LICHII_ORM_HUD_PANEL_AT=top-left  where the panel (F9) goes
//   LICHII_ORM_HUD_SIZE=100       text size, percent, 50 to 300
//   LICHII_ORM_HUD_CHAT_LINES=6   chat lines shown, 0 to 6
// A place is arriba-izquierda, arriba, arriba-derecha, abajo-izquierda,
// abajo, abajo-derecha (or top-left, top, top-right, bottom-left, bottom,
// bottom-right). A value these do not take keeps the default and writes
// "coop: switch NAME=VALUE is not ..." in the log, as every switch.
//
// And changed while playing by chat commands (until the game closes):
//   /rupias [on|off|<place>]   /nombres [on|off]
//   /hud                       the settings now
//   /hud nombres on|off        /hud rupias on|off|<place>
//   /hud panel <place>         /hud tamano <50-300>
//   /hud chat <0-6>            /hud reset (back to coop-options.ini)
// orm/pvp's command registry (coop/host/orm_commands.hpp, docs/COMANDOS.md)
// has /hud, /rupias and /nombres, with their suggestions and /ayuda lines;
// its handler (orm_commands_game.cpp) hands them to OnChat. With the registry
// off (LICHII_ORM_COMMANDS=0) the chat box hands every line to OnChat after it
// (CoopHook::SendChat). Either way a command is answered with a chat notice and
// never sent.
//
// Everything but the live store is portable (tests/lichii/hud_settings_test.cpp).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OrmHud {

// Where a block of the HUD goes in the picture.
enum class Place : std::uint8_t { top_left, top, top_right, bottom_left, bottom, bottom_right };

inline constexpr int kMinSize = 50;   // percent
inline constexpr int kMaxSize = 300;  // percent
inline constexpr std::size_t kMaxChatRows = 6;  // the chat log keeps 6 (CoopChat::kKeep)

struct Settings {
    bool names = true;                 // the names (and ping) over the others
    bool rupees = true;                // everyone's rupees on a fixed line
    Place rupees_at = Place::top;      // where that line goes
    Place panel_at = Place::top_left;  // where the panel (F9) goes
    int size = 100;                    // text size, percent of the usual
    std::size_t chat_lines = kMaxChatRows;
    bool operator==(const Settings&) const = default;
};

// A place from its name: Spanish or English, any case, '-', '_' or ' '
// between the words ("Arriba Derecha", "top_right"); nothing when unknown.
std::optional<Place> ParsePlace(std::string_view text);
// Its Spanish name, as the commands and ParsePlace take it ("arriba-derecha").
std::string_view PlaceName(Place place);
// A size: a whole number of percent, kMinSize to kMaxSize, a trailing '%'
// allowed ("120", "120%"); nothing otherwise.
std::optional<int> ParseSize(std::string_view text);
// A chat line count, 0 to kMaxChatRows; nothing otherwise.
std::optional<std::size_t> ParseChatLines(std::string_view text);
// On or off: the switches' words (1/0, true/false, yes/no, on/off) and si/sí
// (any case); nothing otherwise.
std::optional<bool> ParseOnOff(std::string_view text);

// The settings from the environment, `env` giving a variable's value (empty
// or nothing: unset). Each value not taken adds one line to `log` (when not
// null) and keeps that setting's default.
using EnvReader = std::function<std::optional<std::string>(const char* name)>;
Settings ReadSettings(const EnvReader& env, std::vector<std::string>* log);

// One line about the settings, for /hud and the log: "HUD: nombres sí,
// rupias sí (arriba), panel arriba-izquierda, tamaño 100 %, chat 6 líneas".
std::string Describe(const Settings& settings);

// The commands' names (without '/'), for a command registry.
inline constexpr std::array<std::string_view, 3> kCommandNames{"hud", "rupias", "nombres"};
// Whether `text` is one of these commands ("/rupias off", "/HUD").
bool IsCommand(std::string_view text);
// One of these commands applied to `settings`; `file` is what /hud reset
// goes back to. The answer for the chat, in Spanish; nothing when `text` is
// not one of them (it is then chat, or another command). A bad value changes
// nothing and says what is taken.
std::optional<std::string> Command(std::string_view text, Settings& settings, const Settings& file);

// ---- Live (any thread) ----

// The settings now: the environment's, read on the first call, as the
// commands left them.
Settings LiveSettings();
// The chat box's line: a command is done on the live settings, logged
// ("coop: hud: /rupias off -> HUD: ...") and answered; nothing otherwise.
std::optional<std::string> OnChat(std::string_view text);

} // namespace OrmHud
