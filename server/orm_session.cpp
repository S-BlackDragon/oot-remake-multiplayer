// ORM (oot-remake) server rules kept out of session.cpp (oot-remake
// docs/ADOPTAR_UPSTREAM.md, section 3): GROUP_SETTINGS (type 246, oot-remake
// docs/AJUSTES_GRUPO.md); ROOM_SWITCH, PLAYER_HIT, PLAYER_CALL and
// PLAYER_RENAME (248, 250 to 252, orm/pvp, oot-remake docs/COMANDOS.md).
// Members of Session, declared in session.hpp.
#include <algorithm>
#include <string>
#include <vector>

#include "session.hpp"

namespace coop::server {

using namespace coop::protocol;

// GROUP_SETTINGS: the sender's world settings, kept for joiners and relayed
// to the others with its number, exactly as PLAYER_LOOK. The same settings
// again (a game sends them after each WELCOME) are not relayed. The server
// does not judge them: each game compares the host's with its own.
void Session::on_group_settings(ConnectionId id, Connection& c, const GroupSettings& settings) {
  GroupSettings kept = settings;
  kept.player_number = c.number;
  if (c.settings && *c.settings == kept) {
    ++stats_.settings_repeats;
    return;
  }
  c.settings = kept;
  send_to_others(id, kChannelSession, kept);
  ++stats_.settings_relayed;
  ++stats_.relayed;
}

// To a joiner: the settings of each other player who has sent them, lowest
// number first (so the host's are there before the joiner's game compares).
void Session::send_group_settings(ConnectionId to) {
  std::vector<GroupSettings> all;
  for (const auto& [other, oc] : connections_) {
    if (other != to && oc.state == State::joined && oc.settings) all.push_back(*oc.settings);
  }
  std::sort(all.begin(), all.end(), [](const GroupSettings& a, const GroupSettings& b) {
    return a.player_number < b.player_number;
  });
  for (const GroupSettings& s : all) send(to, kChannelSession, s);
}

// ---- ORM orm/pvp (oot-remake docs/COMANDOS.md) ----

bool Session::switch_on(RoomSwitchKey key) const {
  const auto it = switches_.find(static_cast<std::uint8_t>(key));
  return it != switches_.end() && it->second.on;
}

// ROOM_SWITCH: any player may set any switch of the room (the decision in
// COMANDOS.md: no vote, no host-only; everyone sees who did it). A change is
// stamped with the setter and the server's clock and sent to everyone, the
// setter too (its answer); the same value again is not relayed.
void Session::on_room_switch(Connection& c, const RoomSwitch& m, std::uint64_t now_us) {
  RoomSwitch out;
  out.player_number = c.number;
  out.server_time_us = now_us;
  for (const RoomSwitchEntry& e : m.entries) {
    auto it = switches_.find(e.key);
    if (it != switches_.end() && it->second.on == e.on) {
      ++stats_.switch_repeats;
      continue;
    }
    switches_[e.key] = SwitchRecord{e.on, c.number, now_us};
    out.entries.push_back(e);
  }
  if (out.entries.empty()) return;
  for (const auto& [other, oc] : connections_) {
    if (oc.state == State::joined) send(other, kChannelSession, out);
  }
  ++stats_.switch_relayed;
  ++stats_.relayed;
  std::string line = "player " + std::to_string(c.number) + " set";
  for (const RoomSwitchEntry& e : out.entries) {
    line += " switch " + std::to_string(e.key) + (e.on ? " on" : " off");
  }
  log(line);
}

// To a joiner: each switch the room has set, with who set it and when.
void Session::send_room_switches(ConnectionId to) {
  for (const auto& [key, record] : switches_) {
    RoomSwitch m;
    m.player_number = record.by;
    m.server_time_us = record.at_us;
    m.entries.push_back({key, record.on});
    send(to, kChannelSession, m);
  }
}

// PLAYER_HIT: only while the room's PvP switch is on, to a victim in the
// attacker's scene (when both are known), at most hits_per_second per
// attacker. The victim's game judges the rest.
void Session::on_player_hit(ConnectionId id, Connection& c, PlayerHit& m, std::uint64_t now_us) {
  const auto victim = connection_of(m.victim);
  bool drop = !switch_on(RoomSwitchKey::pvp) || !victim || *victim == id;
  if (!drop) {
    const Connection& v = connections_.at(*victim);
    drop = !c.scene.empty() && !v.scene.empty() && c.scene != v.scene;
  }
  if (!drop && config_.hits_per_second != 0) {
    const double rate = static_cast<double>(config_.hits_per_second);
    if (c.hit_at == 0) {
      c.hit_budget = rate;
    } else {
      const double seconds = static_cast<double>(now_us - std::min(now_us, c.hit_at)) / 1e6;
      c.hit_budget = std::min(c.hit_budget + seconds * rate, rate);
    }
    c.hit_at = std::max(now_us, c.hit_at);
    if (c.hit_budget < 1.0) {
      drop = true;
    } else {
      c.hit_budget -= 1.0;
    }
  }
  if (drop) {
    ++stats_.hit_dropped;
    return;
  }
  m.player_number = c.number;
  Connection& v = connections_.at(*victim);
  v.hit_taken_at = std::max<std::uint64_t>(now_us, 1);
  send(*victim, kChannelSession, m);
  ++stats_.hit_relayed;
  ++stats_.relayed;
}

void Session::note_pvp_down(Connection& c, std::uint64_t now_us) {
  if (config_.pvp_down_window_us == 0 || config_.pvp_down_hold_us == 0 || c.hit_taken_at == 0 ||
      now_us - std::min(now_us, c.hit_taken_at) > config_.pvp_down_window_us) {
    return;
  }
  c.pvp_down_at = std::max<std::uint64_t>(now_us, 1);
  log("player " + std::to_string(c.number) + " fell to PvP: up for the group defeat for " +
      std::to_string(config_.pvp_down_hold_us / 1'000'000) + " s");
}

// PLAYER_CALL: `come` to that mate, `gather` from the host to everyone else;
// one per call_gap_us per player.
void Session::on_player_call(ConnectionId id, Connection& c, PlayerCall& m, std::uint64_t now_us) {
  const bool soon = c.call_at != 0 && now_us - std::min(now_us, c.call_at) < config_.call_gap_us;
  std::optional<ConnectionId> to;
  bool drop = soon;
  if (m.kind == CallKind::gather) {
    drop = drop || !host_ || *host_ != id;
  } else {
    to = connection_of(m.to);
    drop = drop || !to || *to == id;
  }
  if (drop) {
    ++stats_.call_dropped;
    return;
  }
  c.call_at = std::max<std::uint64_t>(now_us, 1);
  m.player_number = c.number;
  if (to) {
    send(*to, kChannelSession, m);
  } else {
    send_to_others(id, kChannelSession, m);
  }
  ++stats_.call_relayed;
  ++stats_.relayed;
}

// PLAYER_RENAME: the app's rule is the codec's (valid_new_nickname); here, no
// other player of the room may have it (ignoring ASCII case, as at the
// HELLO). Taken: everyone gets the new name with the player's number;
// refused: the asker alone, with the reason. One per call_gap_us.
void Session::on_player_rename(ConnectionId id, Connection& c, const PlayerRename& m,
                               std::uint64_t now_us) {
  PlayerRename answer;
  answer.player_number = c.number;
  answer.nickname = m.nickname;
  const auto holder = holder_of(m.nickname);
  if (c.rename_at != 0 && now_us - std::min(now_us, c.rename_at) < config_.call_gap_us) {
    ++stats_.rename_refused;
    return;  // too soon: nothing (the game says so itself)
  }
  c.rename_at = std::max<std::uint64_t>(now_us, 1);
  if (holder && *holder != id) {
    answer.result = RenameResult::name_taken;
    send(id, kChannelSession, answer);
    ++stats_.rename_refused;
    return;
  }
  const std::string before = c.nickname;
  c.nickname = m.nickname;
  answer.result = RenameResult::ok;
  for (const auto& [other, oc] : connections_) {
    if (oc.state == State::joined) send(other, kChannelSession, answer);
  }
  ++stats_.rename_taken;
  ++stats_.relayed;
  log("player " + std::to_string(c.number) + " renamed: " + before + " -> " + c.nickname);
}

}  // namespace coop::server
