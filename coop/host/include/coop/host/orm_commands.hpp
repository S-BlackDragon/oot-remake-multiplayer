#pragma once
// ORM chat commands (oot-remake docs/COMANDOS.md): a small registry of the
// commands a player types in the chat box ("/pvp on", "/ir Saria", ...), its
// parser, the permission rule of each, the /ayuda text and the completion
// engine the chat box's suggestion list is drawn from. Portable: no emulator,
// no widgets, no network (lichii/hook/orm_commands_game.cpp runs them).
//
// A line that starts with '/' is a command and is never sent as chat; an
// unknown one answers "Comando desconocido, escribe /ayuda".
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace coop::host::orm::commands {

enum class Id : std::uint8_t {
  help,        // /ayuda [comando]
  pvp,         // /pvp [on|off]: weapons between players, room-wide
  lock_mates,  // /fijar [on|off]: Z-targeting locks on mates, room-wide
  players,     // /jugadores
  where,       // /donde <nombre>
  go,          // /ir <nombre> (daurosk's /teleport)
  bring,       // /traer <nombre>
  gather,      // /reunir (host)
  accept,      // /aceptar
  ping,        // /ping [on|off]
  fps,         // /fps [on|off]
  net,         // /red [on|off]
  stats,       // /stats [on|off]
  hud,         // /hud [on|off|<ajuste>]: the panel (F9), or this player's HUD settings (orm/hud2)
  map,         // /mapa [on|off]
  color,       // /color <#RRGGBB|nombre>
  spectate,    // /espectar <nombre> (downed)
  logs,        // /registros
  version,     // /version
  time,        // /hora
  retry,       // /reintentar
  mute,        // /silencio [on|off]
  name,        // /nombre <nuevo>
  rupees,      // /rupias [on|off|<sitio>] (orm/hud2, docs/HUD_AJUSTES.md)
  names,       // /nombres [on|off] (orm/hud2)
  host,        // /anfitrion [nombre|auto] (orm/anfitrion, docs/PARTIDA_SALA.md)
  bodies,      // /choque [on|off]: real bodies between Links, room-wide (ORM choque, docs/CHOQUE.md)
};

// What a command takes after its name.
enum class Arg : std::uint8_t {
  none,
  toggle_opt,   // on|off (and its other words), or nothing
  player,       // a player of the room, required
  player_opt,   // a player, or nothing
  command_opt,  // a command's name, or nothing
  color,        // #RRGGBB or a colour's name
  text,         // free text, required (/nombre)
  // on|off (toggle set), another word the command's own code checks and
  // answers (/hud panel arriba, /rupias abajo-derecha), or nothing.
  words_opt,
  host_opt,     // a player of the room (ourselves too), "auto", or nothing (/anfitrion)
};

// Who may run it.
enum class Who : std::uint8_t {
  anyone,
  host,    // the room's host
  downed,  // a player lying downed
};

struct Spec {
  Id id;
  std::string_view name;  // without the '/'
  std::span<const std::string_view> aliases;
  Arg arg;
  Who who;
  // It changes what the whole room has (a switch, a name): its effect is
  // announced to everyone. Local commands answer only the player.
  bool room;
  std::string_view usage;  // "/pvp [on|off]"
  std::string_view help;   // one line, Spanish
};

// Every command, in the order /ayuda lists them.
std::span<const Spec> all();
const Spec& spec(Id id);
// By name or alias, ignoring ASCII case and a leading '/'; nothing for an
// unknown one.
const Spec* find(std::string_view name);

// on, off, true, false, si, sí, no, 1, 0, activar, desactivar, encender,
// apagar, activado, desactivado (any ASCII case; "SÍ" too). Nothing for
// anything else.
std::optional<bool> parse_toggle(std::string_view word);

struct Parsed {
  enum class Kind : std::uint8_t {
    not_command,  // chat: send it as it is
    unknown,      // "/xyz": error holds the answer
    bad_args,     // a known command with a wrong argument: error holds the answer
    ok,
  };
  Kind kind = Kind::not_command;
  const Spec* spec = nullptr;    // unknown and not_command: null
  std::string arg;               // the text after the name, trimmed (may be empty)
  std::optional<bool> toggle;    // toggle_opt with a word given
  std::string error;             // the chat answer for unknown and bad_args
};
// The text the player typed in the chat box (leading and trailing spaces are
// ignored). "/" alone and "//x" are unknown commands, never chat.
Parsed parse(std::string_view line);

struct Context {
  bool connected = true;
  bool host = false;
  bool downed = false;
};
// Nothing when `spec` may run now; else the answer for the chat.
std::optional<std::string> refuse(const Spec& spec, const Context& context);

// "/pvp [on|off] - armas entre jugadores (toda la sala)".
std::string help_line(const Spec& spec);
// /ayuda: a heading, then one help_line per command. With a command's name:
// that command's line and its aliases; an unknown name: the unknown answer.
std::vector<std::string> help_lines(std::string_view about = {});

// ---- Players by name -------------------------------------------------------

struct PlayerName {
  std::uint8_t number = 0;
  std::string name;
};
struct Match {
  std::uint8_t number = 0;  // 0: none, see `error`
  std::string name;
  std::string error;        // Spanish, for the chat
};
// Whole name (ASCII case ignored) first, then a unique start of a name, then
// "2" or "P2". `allow_self` false: ourselves is an error.
Match find_player(std::string_view wanted, std::span<const PlayerName> players, std::uint8_t self,
                  bool allow_self = false);

// ---- /color ----------------------------------------------------------------

// "#RRGGBB" (either case) or a Spanish or English colour name (rojo, azul,
// verde, amarillo, naranja, morado, rosa, blanco, negro, cian, gris; red,
// blue, ...): its red, green, blue. Nothing for anything else.
std::optional<std::array<std::uint8_t, 3>> parse_color(std::string_view text);
// The Spanish names parse_color knows, for the suggestions.
std::span<const std::string_view> color_names();

// ---- /hud, /rupias (orm/hud2) ------------------------------------------------

// The HUD's places as OrmHud::PlaceName writes them, and the words /hud
// takes after its name besides on and off (lichii/hook/coop_hud_settings.h
// checks the values and answers them), for the suggestions.
std::span<const std::string_view> hud_place_names();
std::span<const std::string_view> hud_setting_words();

// ---- /nombre ---------------------------------------------------------------

// Nothing when `name` is a valid new name for player `self` (the app's rule,
// protocol::valid_new_nickname, and no other player has it, ASCII case
// ignored); else the Spanish answer ("Ese nombre ya lo usa otro jugador").
std::optional<std::string> check_new_name(std::string_view name, std::span<const PlayerName> players,
                                          std::uint8_t self);
// `text` (an ini: `key = value` lines, comments, sections) with `key` set to
// `value`: its first line for the key rewritten in place (comments and every
// other line kept), or a line added at the end. Keys compare in ASCII case.
std::string set_ini_value(std::string_view text, std::string_view key, std::string_view value);

// ---- Suggestions while typing ----------------------------------------------

struct Suggestion {
  std::string insert;  // the whole line once chosen ("/fps ", "/fps on", "/ir Saria")
  std::string label;   // what the list shows ("/fps [on|off]", "on", "Saria")
  std::string help;    // one line, may be empty
};
// The list for `text` as typed: nothing for chat; "/" every command (the
// player's own context first: one he may not run is left out); "/fp" those
// whose name or alias starts so; "/fps " the arguments (on, off; the players'
// names; commands for /ayuda; colours), narrowed by what follows the space.
std::vector<Suggestion> complete(std::string_view text, std::span<const PlayerName> players,
                                 std::uint8_t self, const Context& context);

// The chat box's state between keys (Minecraft's): typing shows a new list;
// Up and Down move the selection (wrapping); Tab puts the selected (or the
// first) suggestion in the box, and Tab again goes on to the next of the same
// list; Enter runs the selected suggestion when the player moved to one, or
// the typed text otherwise.
class Completer {
 public:
  // The player typed (not a Tab): the list for the new text.
  void typed(std::string text, std::vector<Suggestion> list);
  const std::vector<Suggestion>& list() const { return list_; }
  // -1: none selected.
  int selected() const { return selected_; }
  void move(int delta);
  // The new text for the box, or nothing when there is no suggestion.
  std::optional<std::string> tab();
  // The line Enter runs instead of the typed text, or nothing (the typed one).
  std::optional<std::string> enter() const;
  void clear();

 private:
  std::string text_;
  std::vector<Suggestion> list_;
  int selected_ = -1;
  bool moved_ = false;
};

}  // namespace coop::host::orm::commands
