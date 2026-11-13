#include "coop/host/wallets.hpp"

#include <algorithm>

namespace coop::host {

std::optional<std::uint32_t> wallet_value(std::optional<std::int32_t> raw) {
  if (!raw || *raw < 0) return std::nullopt;
  return std::min(static_cast<std::uint32_t>(*raw), protocol::kMaxRupees);
}

std::vector<std::string> WalletLog::update(const std::vector<WalletSeen>& players) {
  std::vector<std::string> lines;
  std::array<bool, protocol::kMaxPlayers + 1> here{};
  for (const WalletSeen& p : players) {
    if (p.number == 0 || p.number > protocol::kMaxPlayers) continue;
    here[p.number] = true;
    std::optional<std::uint32_t>& last = last_[p.number];
    if (!p.rupees) {
      last.reset();  // not known now: its next count is new
      continue;
    }
    if (last == p.rupees) continue;
    std::string line = "P" + std::to_string(p.number) + " '" + p.name + "'" + (p.self ? " (you)" : "") +
                       " has " + std::to_string(*p.rupees) + " rupees";
    if (last) line += " (was " + std::to_string(*last) + ")";
    lines.push_back(std::move(line));
    last = p.rupees;
  }
  for (std::size_t n = 1; n < last_.size(); ++n) {
    if (!here[n]) last_[n].reset();  // left (or not in the room): forgotten
  }
  return lines;
}

const char* to_string(WalletPath path) {
  switch (path) {
    case WalletPath::own_game: return "from our game's PlayerStatus.CurrentRupee";
    case WalletPath::sent: return "from the count we last sent (our game is not read now)";
    case WalletPath::peer: return "from their PLAYER_EQUIP";
    case WalletPath::stale: return "quiet (no sample for 1 s): their last count shown with a '?'";
    case WalletPath::absent:
      return "their PLAYER_EQUIP carries no rupees (an older game, or a room server that drops component 29): "
             "shown '-'";
    case WalletPath::waiting: return "no PLAYER_EQUIP from them yet: shown '-'";
  }
  return "?";
}

WalletShown wallet_shown(const WalletRow& row) {
  WalletShown out;
  if (row.self) {
    if (row.game) {
      out.rupees = std::min(*row.game, protocol::kMaxRupees);
      out.path = WalletPath::own_game;
    } else if (row.sent) {
      out.rupees = row.sent;
      out.path = WalletPath::sent;
    }
    return out;
  }
  if (row.sent) {
    out.rupees = std::min(*row.sent, protocol::kMaxRupees);
    out.stale = !row.live;
    out.path = row.live ? WalletPath::peer : WalletPath::stale;
    return out;
  }
  out.path = row.has_equipment ? WalletPath::absent : WalletPath::waiting;
  return out;
}

std::vector<std::string> WalletPaths::update(const std::vector<WalletRow>& rows, std::uint64_t now_us) {
  std::vector<std::string> lines;
  std::array<bool, protocol::kMaxPlayers + 1> here{};
  for (const WalletRow& r : rows) {
    if (r.number == 0 || r.number > protocol::kMaxPlayers) continue;
    here[r.number] = true;
    Known& k = known_[r.number];
    const WalletPath path = wallet_shown(r).path;
    if (path == WalletPath::waiting) {
      if (k.waiting_since_us == 0) k.waiting_since_us = now_us != 0 ? now_us : 1;
      if (now_us - k.waiting_since_us < kWaitingUs) continue;  // the first PLAYER_EQUIP is on its way
    } else {
      k.waiting_since_us = 0;
    }
    if (k.path == path) continue;
    k.path = path;
    lines.push_back("HUD: P" + std::to_string(r.number) + " '" + r.name + "'" + (r.self ? " (you)" : "") + " " +
                    to_string(path));
  }
  for (std::size_t n = 1; n < known_.size(); ++n) {
    if (!here[n]) known_[n] = Known{};
  }
  return lines;
}

}  // namespace coop::host
