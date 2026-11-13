// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_enemies.h"

namespace CoopEnemies {

namespace {

// The game's names (Pack/Actor of the 1.1.1 romfs). Exact names: a variant
// the list does not have is a plain enemy until it is added here.
constexpr std::string_view kMiniBosses[] = {
    "Enemy_Stalfos",               // Forest Temple, Shadow, Spirit, Ganon's Castle
    "Enemy_Stalfos_AlwaysRespawn",
    "Enemy_Stalfos_ForChild",
    "Enemy_Stalfos_Weak",
    "Enemy_Lizalfos",              // Dodongo's Cavern (the pair), Fire Temple
    "Enemy_Dinolfos",              // Ganon's Castle, Gerudo Training Ground
    "Enemy_IronKnuckle_Black",     // Spirit Temple, Ganon's Castle
    "Enemy_IronKnuckle_Silver",
    "Enemy_IronKnuckle_White",
    "Enemy_Wolfos",                // Sacred Forest Meadow, Lost Woods, Ice Cavern
    "Enemy_Wolfos_White",
};

}  // namespace

bool IsMiniBoss(std::string_view name) {
    for (std::string_view m : kMiniBosses) {
        if (name == m) {
            return true;
        }
    }
    return false;
}

}  // namespace CoopEnemies
