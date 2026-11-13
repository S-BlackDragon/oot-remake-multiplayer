#pragma once
// ORM one host for the room (oot-remake docs/PARTIDA_SALA.md, "Un solo
// anfitrion"; the relay's side: server/host_choice.hpp). The game's side, with
// no emulator code (lichii/hook/coop_host_choice.cpp is the glue):
// - the chat command /anfitrion (alias /host): with no word it says who hosts
//   and why; "/anfitrion <name>" asks the relay to make that player the host
//   (HOST_CHOICE pick, the name matched as /teleport matches it); "/anfitrion
//   auto" gives the choice back to the measures;
// - this player's wish (LICHII_ORM_HOST = auto | me | never, set by the app's
//   "Quiero ser el anfitrion" toggle and by room_save_host = false), sent after
//   each WELCOME;
// - the Spanish lines for the chat when the relay says who hosts;
// - the host file the launcher reads (LICHII_ORM_HOST_FILE), so the room's
//   save follows the same host as the relay (orm-launcher roomsave.go).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "coop/protocol/messages.hpp"

namespace coop::host::host_choice {

struct Command {
  enum class Kind : std::uint8_t {
    show,       // /anfitrion: who hosts and why
    pick,       // /anfitrion <name>
    automatic,  // /anfitrion auto (automatico)
  };
  Kind kind = Kind::show;
  std::string name;  // pick: as typed, trimmed
};
// "/anfitrion ..." or "/host ..." (ASCII case ignored, spaces trimmed);
// nothing for any other line.
std::optional<Command> parse(std::string_view line);

// LICHII_ORM_HOST: "me", "yo", "1", "on", "si" -> me; "never", "nunca",
// "0", "off", "no" -> never; anything else (unset, "auto") -> automatic.
protocol::HostWish wish_from(std::string_view value);
// The wish's word for LICHII_ORM_HOST and the log ("auto", "me", "never").
std::string_view wish_word(protocol::HostWish wish);

struct Player {
  std::uint8_t number = 0;
  std::string name;
};
struct Found {
  std::string name;   // the player's nickname as the room has it; empty: see error
  std::string error;  // Spanish, for the chat
};
// The whole name (ASCII case ignored), then a unique start of a name, then
// "2" or "P2". Ourselves may be picked.
Found find_player(std::string_view wanted, std::span<const Player> players);

// The chat line for a HOST_CHOICE told: "Ahora el anfitrion es Ana (mejor
// conexion: 38 ms de media)", "Eres el anfitrion (lo eligio Saria)", and for
// a refused pick (to the picker) why. `names`: the room, for who picked.
std::string told_line(const protocol::HostChoice& told, std::uint8_t self, std::span<const Player> names);
// /anfitrion with no word: who hosts, why and how to change it. `told`: the
// relay's last word (none: an older relay that sends none).
std::string status_line(const std::optional<protocol::HostChoice>& told, std::uint8_t host,
                        std::uint8_t self, std::span<const Player> names);
// The line for a pick sent ("Pedido: Ana como anfitrion").
std::string asked_line(const Command& command, std::string_view name);

// The host file for the launcher, key = value lines:
//   self = <our nickname>
//   host = <the host's nickname>
//   hosting = 1 | 0
//   why = first | left | measured | unfit | picked | wished
//   at = <unix seconds of the write>
std::string host_file(std::string_view self, std::string_view host, bool hosting,
                      protocol::HostWhy why, std::int64_t unix_seconds);

}  // namespace coop::host::host_choice
