#include "coop/protocol/orm_messages.hpp"

#include <bit>
#include <cmath>
#include <string>

namespace coop::protocol {

bool valid_setting_key(std::string_view key) {
  if (key.empty() || key.size() > kMaxSettingKeyChars) return false;
  for (const char c : key) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.';
    if (!ok) return false;
  }
  return true;
}

bool valid_setting_value(std::string_view value) {
  if (value.size() > kMaxSettingValueChars) return false;
  for (const char c : value) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x21 || u > 0x7e) return false;
  }
  return true;
}

namespace {

bool valid_entries(const std::vector<SettingEntry>& entries) {
  if (entries.size() > kMaxGroupSettings) return false;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (!valid_setting_key(entries[i].key) || !valid_setting_value(entries[i].value)) return false;
    if (i > 0 && !(entries[i - 1].key < entries[i].key)) return false;  // sorted, each key once
  }
  return true;
}

}  // namespace

Error orm_encode_group_settings(const GroupSettings& m, std::vector<std::uint8_t>& out) {
  if (m.player_number > kMaxPlayers || !valid_entries(m.entries)) return Error::bad_value;
  out.push_back(m.player_number);
  out.push_back(static_cast<std::uint8_t>(m.entries.size()));
  for (const SettingEntry& e : m.entries) {
    out.push_back(static_cast<std::uint8_t>(e.key.size()));
    out.insert(out.end(), e.key.begin(), e.key.end());
    out.push_back(static_cast<std::uint8_t>(e.value.size()));
    out.insert(out.end(), e.value.begin(), e.value.end());
  }
  return Error::none;
}

Error orm_decode_group_settings(std::span<const std::uint8_t> body, GroupSettings& m) {
  m = GroupSettings{};
  std::size_t at = 0;
  const auto u8 = [&](std::uint8_t& v) {
    if (at >= body.size()) return false;
    v = body[at++];
    return true;
  };
  const auto text = [&](std::string& s, std::size_t max) -> Error {
    std::uint8_t n = 0;
    if (!u8(n)) return Error::truncated;
    if (n > max) return Error::string_too_long;
    if (body.size() - at < n) return Error::truncated;
    s.assign(reinterpret_cast<const char*>(body.data() + at), n);
    at += n;
    return Error::none;
  };
  std::uint8_t count = 0;
  if (!u8(m.player_number) || !u8(count)) return Error::truncated;
  if (m.player_number > kMaxPlayers || count > kMaxGroupSettings) return Error::bad_value;
  m.entries.resize(count);
  for (SettingEntry& e : m.entries) {
    if (const Error err = text(e.key, kMaxSettingKeyChars); err != Error::none) return err;
    if (const Error err = text(e.value, kMaxSettingValueChars); err != Error::none) return err;
  }
  if (at != body.size()) return Error::trailing_bytes;
  if (!valid_entries(m.entries)) return Error::bad_value;
  return Error::none;
}

// ---- ORM orm/pvp (oot-remake docs/COMANDOS.md) ----

bool valid_new_nickname(std::string_view name) {
  if (name.empty() || name.size() > kMaxNicknameChars) return false;
  for (const char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

namespace {

// Little-endian, as codec.cpp's Writer and Reader.
struct Out {
  std::vector<std::uint8_t> bytes;
  void u8(std::uint8_t v) { bytes.push_back(v); }
  void u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
  void u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
  void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
  Error done(std::vector<std::uint8_t>& out) const {
    out.insert(out.end(), bytes.begin(), bytes.end());
    return Error::none;
  }
};

struct In {
  std::span<const std::uint8_t> body;
  std::size_t at = 0;
  bool u8(std::uint8_t& v) {
    if (at >= body.size()) return false;
    v = body[at++];
    return true;
  }
  bool u32(std::uint32_t& v) {
    if (body.size() - at < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(body[at + i]) << (8 * i);
    at += 4;
    return true;
  }
  bool u64(std::uint64_t& v) {
    if (body.size() - at < 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(body[at + i]) << (8 * i);
    at += 8;
    return true;
  }
  bool f32(float& v) {
    std::uint32_t bits = 0;
    if (!u32(bits)) return false;
    v = std::bit_cast<float>(bits);
    return true;
  }
  bool end() const { return at == body.size(); }
};

bool finite3(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool valid_switch_entries(const std::vector<RoomSwitchEntry>& entries) {
  if (entries.empty() || entries.size() > kMaxRoomSwitches) return false;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].key < 1 || entries[i].key > kMaxRoomSwitchKey) return false;
    if (i > 0 && entries[i - 1].key >= entries[i].key) return false;
  }
  return true;
}

bool valid_hit(const PlayerHit& m) {
  const auto weapon = static_cast<std::uint8_t>(m.weapon);
  if (m.player_number > kMaxPlayers || m.victim < 1 || m.victim > kMaxPlayers || m.hit_id == 0) return false;
  if (weapon < 1 || weapon > kMaxHitWeapon || m.damage > kMaxHitDamage) return false;
  if (!finite3(m.from) || !finite3(m.push)) return false;
  const float length2 = m.push[0] * m.push[0] + m.push[1] * m.push[1] + m.push[2] * m.push[2];
  return length2 <= kMaxHitPush * kMaxHitPush * 1.0001f;
}

bool valid_call(std::uint8_t player, std::uint8_t to, std::uint8_t kind) {
  if (player > kMaxPlayers || to > kMaxPlayers) return false;
  if (kind == static_cast<std::uint8_t>(CallKind::come)) return to >= 1;
  if (kind == static_cast<std::uint8_t>(CallKind::gather)) return to == 0;
  return false;
}

}  // namespace

Error orm_encode_room_switch(const RoomSwitch& m, std::vector<std::uint8_t>& out) {
  if (m.player_number > kMaxPlayers || !valid_switch_entries(m.entries)) return Error::bad_value;
  Out w;
  w.u8(m.player_number);
  w.u64(m.server_time_us);
  w.u8(static_cast<std::uint8_t>(m.entries.size()));
  for (const RoomSwitchEntry& e : m.entries) {
    w.u8(e.key);
    w.u8(e.on ? 1 : 0);
  }
  return w.done(out);
}

Error orm_decode_room_switch(std::span<const std::uint8_t> body, RoomSwitch& m) {
  m = RoomSwitch{};
  In r{body};
  std::uint8_t count = 0;
  if (!r.u8(m.player_number) || !r.u64(m.server_time_us) || !r.u8(count)) return Error::truncated;
  if (m.player_number > kMaxPlayers || count == 0 || count > kMaxRoomSwitches) return Error::bad_value;
  m.entries.resize(count);
  for (RoomSwitchEntry& e : m.entries) {
    std::uint8_t on = 0;
    if (!r.u8(e.key) || !r.u8(on)) return Error::truncated;
    if (on > 1) return Error::bad_value;
    e.on = on == 1;
  }
  if (!r.end()) return Error::trailing_bytes;
  if (!valid_switch_entries(m.entries)) return Error::bad_value;
  return Error::none;
}

Error orm_encode_player_hit(const PlayerHit& m, std::vector<std::uint8_t>& out) {
  if (!valid_hit(m)) return Error::bad_value;
  Out w;
  w.u8(m.player_number);
  w.u8(m.victim);
  w.u32(m.hit_id);
  w.u8(static_cast<std::uint8_t>(m.weapon));
  w.u8(m.damage);
  w.u8(m.flags);
  for (const float f : m.from) w.f32(f);
  for (const float f : m.push) w.f32(f);
  return w.done(out);
}

Error orm_decode_player_hit(std::span<const std::uint8_t> body, PlayerHit& m) {
  m = PlayerHit{};
  In r{body};
  std::uint8_t weapon = 0;
  if (!r.u8(m.player_number) || !r.u8(m.victim) || !r.u32(m.hit_id) || !r.u8(weapon) || !r.u8(m.damage) ||
      !r.u8(m.flags)) {
    return Error::truncated;
  }
  for (float& f : m.from) {
    if (!r.f32(f)) return Error::truncated;
  }
  for (float& f : m.push) {
    if (!r.f32(f)) return Error::truncated;
  }
  if (!r.end()) return Error::trailing_bytes;
  m.weapon = static_cast<HitWeapon>(weapon);
  if (!valid_hit(m)) return Error::bad_value;
  return Error::none;
}

Error orm_encode_player_call(const PlayerCall& m, std::vector<std::uint8_t>& out) {
  const auto kind = static_cast<std::uint8_t>(m.kind);
  if (!valid_call(m.player_number, m.to, kind)) return Error::bad_value;
  Out w;
  w.u8(m.player_number);
  w.u8(m.to);
  w.u8(kind);
  w.u32(m.call_id);
  return w.done(out);
}

Error orm_decode_player_call(std::span<const std::uint8_t> body, PlayerCall& m) {
  m = PlayerCall{};
  In r{body};
  std::uint8_t kind = 0;
  if (!r.u8(m.player_number) || !r.u8(m.to) || !r.u8(kind) || !r.u32(m.call_id)) return Error::truncated;
  if (!r.end()) return Error::trailing_bytes;
  if (!valid_call(m.player_number, m.to, kind)) return Error::bad_value;
  m.kind = static_cast<CallKind>(kind);
  return Error::none;
}

Error orm_encode_player_rename(const PlayerRename& m, std::vector<std::uint8_t>& out) {
  const auto result = static_cast<std::uint8_t>(m.result);
  if (m.player_number > kMaxPlayers || result > kMaxRenameResult || !valid_new_nickname(m.nickname)) {
    return Error::bad_value;
  }
  Out w;
  w.u8(m.player_number);
  w.u8(result);
  w.u8(static_cast<std::uint8_t>(m.nickname.size()));
  w.bytes.insert(w.bytes.end(), m.nickname.begin(), m.nickname.end());
  return w.done(out);
}

Error orm_decode_player_rename(std::span<const std::uint8_t> body, PlayerRename& m) {
  m = PlayerRename{};
  In r{body};
  std::uint8_t result = 0;
  std::uint8_t length = 0;
  if (!r.u8(m.player_number) || !r.u8(result) || !r.u8(length)) return Error::truncated;
  if (length > kMaxNicknameChars) return Error::string_too_long;
  if (body.size() - r.at < length) return Error::truncated;
  m.nickname.assign(reinterpret_cast<const char*>(body.data() + r.at), length);
  r.at += length;
  if (!r.end()) return Error::trailing_bytes;
  if (m.player_number > kMaxPlayers || result > kMaxRenameResult || !valid_new_nickname(m.nickname)) {
    return Error::bad_value;
  }
  m.result = static_cast<RenameResult>(result);
  return Error::none;
}

}  // namespace coop::protocol
