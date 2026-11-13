#pragma once
// ORM PvP shield (oot-remake docs/PVP.md "Escudo"): the AS commands of every
// slot of an actor, not only the main one PlayerReader reads. A guard can
// play on the upper body's slot while the main slot keeps the legs' command
// (Player_*.root.asb has Pose_Sword_ShieldGuard_* and LockOn_Wait_ShieldGuard
// beside LockOnWait), so the victim's shield rule looks at all of them
// (orm_pvp.hpp guard_of).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coop/host/game_memory.hpp"

namespace coop::host::orm {

// Slots read at most (the puppets sync as many: lichii/hook coop_puppet.h).
inline constexpr std::size_t kMaxAsSlots = 8;

// The command now playing in each slot of `actor`'s AS controller, slot 0
// first, up to the controller's slot count and `max_slots`: "" for an empty
// slot or one whose name does not read as 1 to protocol::kMaxAnimationChars
// printable characters. Empty when the controller itself cannot be read (no
// components, no AS, a count of 0 or less).
std::vector<std::string> read_as_commands(GameMemory& memory, std::uint64_t actor,
                                          std::size_t max_slots = kMaxAsSlots);

}  // namespace coop::host::orm
