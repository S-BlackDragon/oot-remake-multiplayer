#pragma once

// ORM (oot-remake docs/INVENTARIO.md, "Cartera de cada uno"; 2026-11-11):
// every player's wallet is their own (PlayerStatus.CurrentRupee stays per
// player, as docs/items.md builds it), and every game shows the rupees of each
// connected player. A game's own count goes out in PLAYER_EQUIP's `rupees`
// component (protocol::EquipComponent::rupees, oot-remake
// docs/TIPOS_MENSAJE.md); the receivers only show it (the HUD's wallet list,
// lichii/hook/orm_hud.h), they never write it into their game.
//
// No game, no sockets: tested in tests/orm/wallet_test.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

// PlayerStatus.CurrentRupee (docs/items.md): an Int flag, per player.
inline constexpr std::uint32_t kCurrentRupeeFlag = 0xa77921d7;

// The count to send for the game's value: nothing for a value no wallet holds
// (below 0: a bad read), at most protocol::kMaxRupees.
std::optional<std::uint32_t> wallet_value(std::optional<std::int32_t> raw);

// What a game knows of one player's wallet: the client's player list
// (coop::net::Player: number, nickname, self, equipment->rupees).
struct WalletSeen {
  std::uint8_t number = 0;  // 1 to kMaxPlayers
  std::string name;
  bool self = false;
  std::optional<std::uint32_t> rupees;
};

// One log line per wallet that changed since the last update ("P2 'Ana' has
// 87 rupees (was 82)"; ours with "(you)"). A player first seen with a count
// is logged ("has 87 rupees"); a count that goes away, or a player who
// leaves, is forgotten, so its next count is logged again. Players outside 1
// to kMaxPlayers are ignored.
class WalletLog {
 public:
  std::vector<std::string> update(const std::vector<WalletSeen>& players);

 private:
  std::array<std::optional<std::uint32_t>, protocol::kMaxPlayers + 1> last_{};
};

// ---- What the HUD's wallet list shows (RIESGOS_RESUELTOS.md "Mundo e historia") ----
//
// The list must show each player's OWN wallet and never a wrong number as if it were
// right. Where each count comes from, best first:
// - ours: our game's PlayerStatus.CurrentRupee this frame (own_game); while it cannot
//   be read (loading), the count we last sent (sent);
// - another player's: their last PLAYER_EQUIP rupees (peer); while their samples are
//   quiet (no sample for 1 s, or not yet), that count marked stale ("87?");
// - nothing to show: "-", either because their PLAYER_EQUIP came without the rupees
//   component (absent: an older game, or a room server that drops component 29,
//   TIPOS_MENSAJE.md) or because none came yet (waiting).
enum class WalletPath : std::uint8_t { own_game, sent, peer, stale, absent, waiting };
const char* to_string(WalletPath path);

struct WalletRow {
  std::uint8_t number = 0;
  std::string name;
  bool self = false;
  bool live = false;           // others: a sample newer than 1 s; ours: the room is connected
  bool has_equipment = false;  // a PLAYER_EQUIP of theirs (ours: sent) is known
  std::optional<std::uint32_t> sent;  // its rupees (ours: the count we sent)
  std::optional<std::uint32_t> game;  // ours only: our game's count now
};

struct WalletShown {
  std::optional<std::uint32_t> rupees;  // none: "-"
  bool stale = false;                   // drawn with a '?' after it
  WalletPath path = WalletPath::waiting;
};
WalletShown wallet_shown(const WalletRow& row);

// One line per player when the path of its count changes ("wallet: HUD: P2 'Ana'
// from their PLAYER_EQUIP"); waiting is told only after kWaitingUs, once. A player
// who leaves is forgotten.
class WalletPaths {
 public:
  static constexpr std::uint64_t kWaitingUs = 10'000'000;
  std::vector<std::string> update(const std::vector<WalletRow>& rows, std::uint64_t now_us);

 private:
  struct Known {
    std::optional<WalletPath> path;  // the last one told
    std::uint64_t waiting_since_us = 0;
  };
  std::array<Known, protocol::kMaxPlayers + 1> known_{};
};

}  // namespace coop::host
