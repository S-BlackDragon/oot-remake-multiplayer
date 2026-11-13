#pragma once

// ORM (oot-remake docs/TIPOS_MENSAJE.md): the one table of every number our
// fork adds to daurosk's protocol: message types, component ids, PLAYER_STATUS
// bits and OBJECT_SAMPLE state bits. A branch takes its number from here, never a literal:
//
//   group_settings = orm::type::group_settings,  // in messages.hpp's MessageType
//
// A new number is added here first (branch orm/tipos), in TIPOS_MENSAJE.md and
// in the Go copy (server/internal/remake/orm_types.go on orm/go109, whose test
// parses this file and compares). Never renumber one that has shipped in a kit.
//
// Standalone on purpose (only <cstdint>, <cstddef>): messages.hpp includes it,
// so any branch can include it too. The static_asserts below check the table
// against itself and against the blocks upstream uses; messages.hpp checks
// upstream's own enums against them.

#include <cstddef>
#include <cstdint>

namespace coop::protocol::orm {

// A closed range of numbers.
struct Block {
  std::uint16_t first;
  std::uint16_t last;
};

constexpr bool in_block(std::uint32_t v, Block b) { return v >= b.first && v <= b.last; }

// ------------------------------------------------------------ message types
// Every type block upstream has used or keeps for later (docs/protocol.md at
// v0.1.0-test10, f7ab871, 2026-11-12): 1 to 39 (30, 31 enemy ownership, 39
// drops), 50 to 54 boss rooms, 80 to 84 signs, 135 to 139 saves, 140 to 144
// Ocarina, 150 to 154 "who did it", 156 to 160 projectiles, 171 to 175 world
// objects, 176 to 180 items, 186 to 190 fish (never used), 191 to 195 join
// password, 206 to 215 save transfer, 241 to 250 PvP, 251 to 255 the co-op
// HUD, 320 to 324 fishing, 345 to 349 rejoin, 355 to 359 gimmick events, 370
// to 374 (kept for gohma-sync, not used), 385 to 389 riding, 400 to 404
// moving platforms. Update on each re-pin (oot-remake docs/ADOPTAR_UPSTREAM.md).
inline constexpr Block kUpstreamTypeBlocks[] = {
    {1, 39},    {50, 54},   {80, 84},   {135, 139}, {140, 144}, {150, 154}, {156, 160},
    {171, 175}, {176, 180}, {186, 190}, {191, 195}, {206, 215}, {241, 250}, {251, 255},
    {320, 324}, {345, 349}, {355, 359}, {370, 374}, {385, 389}, {400, 404},
};
// Upstream's highest MessageType today (PLATFORM_COUNTER); messages.hpp checks it.
inline constexpr std::uint16_t kUpstreamLastType = 401;

// Ours: 0xF000 to 0xF0FF (61440 to 61695), far above every number upstream
// has used (types are u16 on the wire). They were 60 to 65 up to protocol 108
// and 240 to 254 in 109; test10 took 241 to 255 (PvP, HUD), so protocol 110
// moves the whole block, keeping the order: new = 0xF000 + (old - 240)
// (oot-remake docs/PIN_TEST10.md).
inline constexpr Block kTypeBlock{0xF000, 0xF0FF};

namespace type {
// Unscoped on purpose, so a value converts to MessageType's std::uint16_t.
enum Value : std::uint16_t {
  revive = 0xF000,          // REVIVE: a mate gets a downed player up (60 in 108, 240 in 109)
  revived = 0xF001,         // kept for REVIVE ("revived"); never sent
  group_defeat = 0xF002,    // GROUP_DEFEAT: every player is down (62 in 108)
  story_event = 0xF003,     // STORY_EVENT: a story cutscene for the scene (63 in 108)
  player_look = 0xF004,     // PLAYER_LOOK: a player's colour (64 in 108)
  object_pose = 0xF005,     // OBJECT_POSE: the bones of a townsperson (65 in 108)
  group_settings = 0xF006,  // GROUP_SETTINGS: settings that change the shared world (orm/ajustes)
  ocarina_note = 0xF007,    // OCARINA_NOTE: a note or song a player plays (orm/ocarina3)
  room_switch = 0xF008,     // ROOM_SWITCH: a room-wide switch (PvP, lock on mates) set live (orm/pvp)
  chest_open = 0xF009,      // CHEST_OPEN: a player opens a shared chest (orm/efectos, orm/cofre2)
  player_hit = 0xF00A,      // PLAYER_HIT: a player's weapon hit a mate's puppet, to that mate (orm/pvp)
  player_call = 0xF00B,     // PLAYER_CALL: "come to me" to one mate or (host) to all (orm/pvp)
  player_rename = 0xF00C,   // PLAYER_RENAME: a player's new name, checked by the server (orm/pvp)
  link_notice = 0xF00D,     // LINK_NOTICE: the relay tells one player about his link (orm/riesgos-red)
  host_choice = 0xF00E,     // HOST_CHOICE: who hosts, by the relay's measures or by hand (orm/anfitrion)
};
}  // namespace type

inline constexpr std::uint16_t kTypes[] = {
    type::revive,       type::revived,        type::group_defeat, type::story_event,
    type::player_look,  type::object_pose,    type::group_settings, type::ocarina_note,
    type::chest_open,   type::link_notice,    type::host_choice,  type::room_switch,
    type::player_hit,   type::player_call,    type::player_rename,
};

constexpr bool is_orm_type(std::uint16_t t) { return in_block(t, kTypeBlock); }

constexpr bool in_upstream_type_block(std::uint32_t t) {
  for (const Block& b : kUpstreamTypeBlocks) {
    if (in_block(t, b)) return true;
  }
  return false;
}

// ------------------------------------------------------------ component ids
// Each message has its own component id space (u16 on the wire), but the
// decoders keep one bit per known id in a 32-bit mask (codec.cpp,
// `seen |= 1u << id`, here and in the Go port), so a known component must be
// 1 to 31. Upstream counts up from 1; ours take the top, 24 to 31, counting
// down from 30 in each space (31 kept as the last one).
inline constexpr Block kComponentBlock{24, 31};
inline constexpr std::uint16_t kMaxKnownComponentId = 31;

// Upstream's highest component id in each space (v0.1.0-test9, origin/main
// 2e19825). messages.hpp checks them; update on each re-pin.
inline constexpr std::uint16_t kUpstreamLastSampleComponent = 7;   // animation
inline constexpr std::uint16_t kUpstreamLastEquipComponent = 16;   // color (test10)
inline constexpr std::uint16_t kUpstreamLastObjectComponent = 19;  // slots (test10)

namespace sample_component {  // PLAYER_SAMPLE (and nothing in OBJECT_SAMPLE)
enum Value : std::uint16_t {
  ride = 30,  // the platform a player rides and its offset on it (orm/plataformas, orm/templos)
};
}  // namespace sample_component

namespace equip_component {  // PLAYER_EQUIP
enum Value : std::uint16_t {
  mount = 30,   // the horse a rider sits on, by actor name (orm/epona, orm/caballo; was 16 there)
  rupees = 29,  // the player's own wallet, u32 0 to kMaxRupees (orm/cartera)
};
}  // namespace equip_component

namespace object_component {  // OBJECT_SAMPLE, OBJECT_SPAWN, HANDOVER_STATE
enum Value : std::uint16_t {
  // 30 is skipped here: upstream's own test ("an object sample needs its time
  // component, and skips unknown ones", tests/protocol/codec_test.cpp) sends 30
  // as the unknown component.
  carry = 29,  // a lifted object in a player's hands: holder, place, bone (orm/levantar; was 19 there)
};
}  // namespace object_component

inline constexpr std::uint16_t kSampleComponents[] = {sample_component::ride};
inline constexpr std::uint16_t kEquipComponents[] = {equip_component::mount, equip_component::rupees};
inline constexpr std::uint16_t kObjectComponents[] = {object_component::carry};

// ------------------------------------------------------- PLAYER_STATUS bits
// Upstream's flag bits are 0 to 4 (status::paused to status::dead) and the
// protocol keeps unknown bits; ours count down from bit 7.
inline constexpr std::uint8_t kUpstreamLastStatusBit = 4;

namespace status_bit {  // bit numbers, not masks
enum Value : std::uint8_t {
  at_title = 7,  // on the title screen or file select, sent with status::loading (orm/menus; was bit 5 there)
  age_waits = 6,  // the room's age changed and our move waits until it is safe (orm/edad-grupo, EDAD.md)
};
}  // namespace status_bit

inline constexpr std::uint8_t kStatusAtTitle = std::uint8_t(1u << status_bit::at_title);
inline constexpr std::uint8_t kStatusAgeWaits = std::uint8_t(1u << status_bit::age_waits);
inline constexpr std::uint8_t kStatusBits[] = {status_bit::at_title, status_bit::age_waits};

// ------------------------------------- OBJECT_SAMPLE state bits (component 18)
// Upstream's object_state bits are 0 to 2 (in_fight, held, asleep); ours count
// down from bit 7, as in PLAYER_STATUS.
inline constexpr std::uint8_t kUpstreamLastObjectStateBit = 2;

namespace object_state_bit {  // bit numbers, not masks
enum Value : std::uint8_t {
  // In the owner's OBJECT_SAMPLE of a boss: its game needs it a while longer
  // (Twinrova's ice, oot-remake docs/FINAL.md, ZMP D-231). orm/final2; were bits 3 and 4 there.
  owner_keeps = 7,       // no hand-over to a hitter nor to a nearer player
  owner_keeps_near = 6,  // no hand-over to a nearer player
};
}  // namespace object_state_bit

inline constexpr std::uint8_t kObjectStateOwnerKeeps = std::uint8_t(1u << object_state_bit::owner_keeps);
inline constexpr std::uint8_t kObjectStateOwnerKeepsNear =
    std::uint8_t(1u << object_state_bit::owner_keeps_near);
inline constexpr std::uint8_t kObjectStateBits[] = {object_state_bit::owner_keeps,
                                                    object_state_bit::owner_keeps_near};

// ------------------------------------------------------------------- checks
template <typename T, std::size_t N>
constexpr bool all_different(const T (&v)[N]) {
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      if (v[i] == v[j]) return false;
    }
  }
  return true;
}

template <typename T, std::size_t N>
constexpr bool all_in(const T (&v)[N], std::uint32_t first, std::uint32_t last) {
  for (std::size_t i = 0; i < N; ++i) {
    const std::uint32_t x = v[i];
    if (x < first || x > last) return false;
  }
  return true;
}

template <std::size_t N>
constexpr bool none_upstream(const std::uint16_t (&v)[N]) {
  for (std::size_t i = 0; i < N; ++i) {
    if (in_upstream_type_block(v[i])) return false;
  }
  return true;
}

constexpr bool type_block_clear_of_upstream() {
  for (const Block& b : kUpstreamTypeBlocks) {
    if (b.first > b.last) return false;
    if (b.last >= kTypeBlock.first && b.first <= kTypeBlock.last) return false;
  }
  return true;
}

static_assert(type_block_clear_of_upstream(), "an upstream type block overlaps ours (0xF000 to 0xF0FF)");
static_assert(all_different(kTypes), "two ORM message types share a number");
static_assert(all_in(kTypes, kTypeBlock.first, kTypeBlock.last), "an ORM message type is outside 0xF000 to 0xF0FF");
static_assert(none_upstream(kTypes), "an ORM message type is in an upstream block");

static_assert(kComponentBlock.last == kMaxKnownComponentId, "the seen masks hold ids 1 to 31 only");
static_assert(kUpstreamLastSampleComponent < kComponentBlock.first &&
                  kUpstreamLastEquipComponent < kComponentBlock.first &&
                  kUpstreamLastObjectComponent < kComponentBlock.first,
              "upstream's components reached ours (24 to 31): move ours before the re-pin");
static_assert(all_different(kSampleComponents) &&
                  all_in(kSampleComponents, kComponentBlock.first, kComponentBlock.last),
              "PLAYER_SAMPLE: ORM components must be different and 24 to 31");
static_assert(all_different(kEquipComponents) &&
                  all_in(kEquipComponents, kComponentBlock.first, kComponentBlock.last),
              "PLAYER_EQUIP: ORM components must be different and 24 to 31");
static_assert(all_different(kObjectComponents) &&
                  all_in(kObjectComponents, kComponentBlock.first, kComponentBlock.last),
              "OBJECT_SAMPLE: ORM components must be different and 24 to 31");

static_assert(all_different(kStatusBits) && all_in(kStatusBits, kUpstreamLastStatusBit + 1u, 7u),
              "PLAYER_STATUS: ORM bits must be different and 5 to 7");
static_assert(all_different(kObjectStateBits) &&
                  all_in(kObjectStateBits, kUpstreamLastObjectStateBit + 1u, 7u),
              "OBJECT_SAMPLE state: ORM bits must be different and 3 to 7");

}  // namespace coop::protocol::orm
