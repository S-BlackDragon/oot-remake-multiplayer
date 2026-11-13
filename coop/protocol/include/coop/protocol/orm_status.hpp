#pragma once

// ORM (oot-remake docs/MENUS_PAUSA.md): our bits of the PLAYER_STATUS flags
// byte, beside daurosk's bits 0 to 4 (messages.hpp, namespace status). The
// protocol keeps unknown bits (the server relays them, presence keeps them),
// so a game or server without this file reads them as nothing.

#include <cstdint>

#include "coop/protocol/orm_types.hpp"

namespace coop::protocol::orm_status {

// The game is on the title screen or the file select: no world, no Link. Sent
// together with status::loading, so a server without this file already treats
// the player as one who cannot own or be given an object. Bit 7, from the one
// table of ORM numbers (orm_types.hpp, oot-remake docs/TIPOS_MENSAJE.md): bit 5
// is upstream's next one.
inline constexpr std::uint8_t at_title = orm::kStatusAtTitle;
static_assert(at_title == (1u << 7), "at_title is PLAYER_STATUS bit 7");

// The room's age changed (a mate drew or put back the Master Sword) and our
// game's move to the Temple of Time waits until it is safe here: a cutscene,
// a boss room's lock or our Link down (coop/host/age.hpp, oot-remake
// docs/EDAD.md, 2026-11-13). The others show "Esperando a <nombre> para
// cambiar de epoca...". Bit 6, the next of ours down from 7.
inline constexpr std::uint8_t age_waits = orm::kStatusAgeWaits;
static_assert(age_waits == (1u << 6), "age_waits is PLAYER_STATUS bit 6");

}  // namespace coop::protocol::orm_status
