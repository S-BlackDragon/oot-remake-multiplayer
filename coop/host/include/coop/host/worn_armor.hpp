#pragma once
// ORM (oot-remake/docs/DETALLES_MARIONETA.md): the armor a player really
// wears, for PLAYER_EQUIP's head, armor, boots and hand, and the rule a
// puppet uses to take off armor its player does not wear.
//
// The PlayerStatus.Equip* flags follow the menu. Masks and the adult boots are
// ZR items (AssignableItem SkullMask, HoverBoot, ...; docs/puppet.md, "Masks
// did not show"), and the Lens of Truth is an armor piece of its own
// (Armor_*_GlassTruth): whether putting them on writes a flag was never seen.
// The piece in the wearer's EquipmentUser is what the game draws, so it wins
// over the flag; the flag stays the answer while no piece is there.
//
// The armor links (read from the dump, not from a running game): 13 links of
// 0x18 bytes at EquipmentUser + 0xe0, shaped like the hand slots (holder +0x8,
// u32 id +0x10, ~0 empty). 0x10aceb0(EquipmentUser, index, 0), the take-off
// the game's GameData path 0x2df6c80 calls, empties link `index`, with index 0
// for slot kind 7 (ArmorHead), 1 for 8 (ArmorUpper), 2 for 9 (ArmorLower), 3
// for 10 (ArmorHand). Hand slot 7 is Helmet (the enum at 0x3ef2033).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "coop/host/game_memory.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host::worn {

inline constexpr std::uint64_t kArmorLinks = 0xe0;
inline constexpr std::uint64_t kLinkStride = 0x18;
inline constexpr std::uint32_t kArmorLinkCount = 13;
inline constexpr std::uint32_t kArmorHead = 0;
inline constexpr std::uint32_t kArmorUpper = 1;
inline constexpr std::uint32_t kArmorLower = 2;
inline constexpr std::uint32_t kArmorHand = 3;
inline constexpr std::uint32_t kHelmetSlot = 7;

// An armor piece another player should see: an `Armor_` actor that is not the
// game's default head (Armor_Child_Default_Head, Armor_Adult_Default_Head:
// every Link has his own, so it is never sent nor taken off).
bool shown_armor(std::string_view name);

// An armor piece a puppet may take off when its player wears nothing of that
// kind: a shown piece that is not the base tunic (Armor_Adult_Normal, which
// every adult wears; an empty tunic from a peer is a peer that could not tell).
bool strayable_armor(std::string_view name);

// The piece worn wins over the flag when it is a shown armor piece; else the
// flag (also when the piece could not be read).
std::optional<std::string> worn_or_flag(const std::optional<std::string>& worn,
                                        const std::optional<std::string>& flag);

// The actor name of the piece a link (hand slot or armor link) holds: "" when
// it is empty, its holder no longer holds it, or the piece is gone (state 7 to
// 11); nothing when the link or the name cannot be read, or the name is not 1
// to protocol::kMaxEquipNameChars printable characters.
std::optional<std::string> link_name(GameMemory& memory, std::uint64_t link);

// An actor's EquipmentUser ([[actor + 0x270] + 0x200]); nothing when unset.
std::optional<std::uint64_t> equipment_user(GameMemory& memory, std::uint64_t actor);

// The name in armor link `index` (kArmorHead ...) of an EquipmentUser.
std::optional<std::string> armor_name(GameMemory& memory, std::uint64_t user, std::uint32_t index);

// Puts what the player actor really wears into head, armor, boots and hand
// (worn_or_flag with each field's flag as it is). The head looks at the
// Helmet hand slot too when armor link 0 holds no shown piece. Nothing changes
// when the actor has no EquipmentUser.
void apply(GameMemory& memory, std::uint64_t actor, protocol::Equipment& equipment);

}  // namespace coop::host::worn
