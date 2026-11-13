// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM (oot-remake docs/DETALLES_MARIONETA.md): small details of the other
// players' Links. Here: armor a puppet wears that its player does not.
//
// A puppet equips itself about 65 ms after its actor exists, from OUR game's
// PlayerStatus.Equip* flags (docs/puppet.md, "Sword and shield on the
// puppet"). The director then wears its player's pieces over them, but a kind
// its player wears nothing of (no boots, no gauntlets, no mask) kept ours: our
// Iron Boots or our Silver Gauntlets showed on every other adult. The director
// takes such a piece off with the game's own armor take-off, as it does a
// piece its player took off (Director::Equip, CoopPuppet::addr::kUnequipArmor).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "coop/host/game_memory.hpp"
#include "coop_puppet.h"

namespace CoopDetalles {

// coop::host::GameMemory over the director's Guest, so the host's link
// reading (coop/host/worn_armor.hpp) serves the puppet too.
class GuestMemory final : public coop::host::GameMemory {
public:
    GuestMemory(CoopPuppet::Guest& guest, std::uint64_t main_base)
        : guest_(guest), main_base_(main_base) {}
    std::uint64_t main_base() const override { return main_base_; }
    bool read(std::uint64_t address, std::span<std::uint8_t> out) override {
        return guest_.Read(address, out.data(), out.size());
    }

private:
    CoopPuppet::Guest& guest_;
    std::uint64_t main_base_;
};

// The piece in armor link `index` (0 head, 1 tunic, 2 boots, 3 gauntlets) of
// the EquipmentUser `eu` when a puppet may take it off because its player
// wears none of that kind (coop::host::worn::strayable_armor: never the
// default head nor the base tunic); "" when there is none such. Nothing when
// the link cannot be read.
std::optional<std::string> StrayArmor(CoopPuppet::Guest& guest, std::uint64_t main_base,
                                      std::uint64_t eu, std::uint32_t index);

// A stray piece is taken off at most this many times in a puppet's life: if
// the game keeps putting it back, it stays on (one line says so).
inline constexpr std::uint8_t kStrayTries = 3;

}  // namespace CoopDetalles
