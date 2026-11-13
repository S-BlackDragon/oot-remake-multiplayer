// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_detalles.h"

#include "coop/host/worn_armor.hpp"

namespace CoopDetalles {

std::optional<std::string> StrayArmor(CoopPuppet::Guest& guest, std::uint64_t main_base,
                                      std::uint64_t eu, std::uint32_t index) {
    GuestMemory memory(guest, main_base);
    auto name = coop::host::worn::armor_name(memory, eu, index);
    if (!name) {
        return std::nullopt;
    }
    if (!coop::host::worn::strayable_armor(*name)) {
        return std::string{};
    }
    return name;
}

}  // namespace CoopDetalles
