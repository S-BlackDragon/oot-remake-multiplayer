#pragma once
// ORM (oot-remake) message bodies kept out of codec.cpp, so a re-pin on a new
// upstream tag touches only a few lines there (oot-remake
// docs/ADOPTAR_UPSTREAM.md, section 3). codec.cpp calls these from its own
// encode_body and decode_body.
//
// GROUP_SETTINGS (type 246, oot-remake docs/AJUSTES_GRUPO.md), body:
//   u8 player (0 from a client), u8 count (0 to kMaxGroupSettings), then
//   `count` times: u8 key length, key bytes, u8 value length, value bytes.
// Keys are 1 to kMaxSettingKeyChars of A-Z a-z 0-9 _ and '.', in strictly
// increasing byte order (so each key once); values are 0 to
// kMaxSettingValueChars of printable ASCII (0x21 to 0x7e: no space).
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::protocol {

inline constexpr std::size_t kMaxGroupSettings = 48;
inline constexpr std::size_t kMaxSettingKeyChars = 48;
inline constexpr std::size_t kMaxSettingValueChars = 64;

bool valid_setting_key(std::string_view key);
bool valid_setting_value(std::string_view value);

// Appends the body (not the header) to `out`; bad_value for a bad player
// number, key, value, order or count, and then `out` is left as it was.
Error orm_encode_group_settings(const GroupSettings& m, std::vector<std::uint8_t>& out);
// Decodes a whole body; trailing_bytes when bytes are left after it.
Error orm_decode_group_settings(std::span<const std::uint8_t> body, GroupSettings& m);

// ORM orm/pvp (oot-remake docs/COMANDOS.md). Little-endian, as codec.cpp.
//
// ROOM_SWITCH: u8 player, u64 server time, u8 count (1 to kMaxRoomSwitches),
//   then `count` times u8 key (1 to kMaxRoomSwitchKey, increasing), u8 on (0, 1).
// PLAYER_HIT: u8 player, u8 victim (1 to kMaxPlayers), u32 hit id (not 0),
//   u8 weapon (1 to kMaxHitWeapon), u8 damage (0 to kMaxHitDamage), u8 flags,
//   f32 x 3 from, f32 x 3 push (finite, at most kMaxHitPush long).
// PLAYER_CALL: u8 player, u8 to, u8 kind (1 come: to 1 to kMaxPlayers;
//   2 gather: to 0), u32 call id.
// PLAYER_RENAME: u8 player, u8 result (0 to kMaxRenameResult), u8 length,
//   name bytes (valid_new_nickname).
// Encoders append the body to `out` (bad_value, and `out` as it was, for a
// bad field); decoders take a whole body (trailing_bytes for more).
Error orm_encode_room_switch(const RoomSwitch& m, std::vector<std::uint8_t>& out);
Error orm_decode_room_switch(std::span<const std::uint8_t> body, RoomSwitch& m);
Error orm_encode_player_hit(const PlayerHit& m, std::vector<std::uint8_t>& out);
Error orm_decode_player_hit(std::span<const std::uint8_t> body, PlayerHit& m);
Error orm_encode_player_call(const PlayerCall& m, std::vector<std::uint8_t>& out);
Error orm_decode_player_call(std::span<const std::uint8_t> body, PlayerCall& m);
Error orm_encode_player_rename(const PlayerRename& m, std::vector<std::uint8_t>& out);
Error orm_decode_player_rename(std::span<const std::uint8_t> body, PlayerRename& m);

}  // namespace coop::protocol
