// SPDX-License-Identifier: GPL-3.0-or-later
// ORM efectos2: the emulator side of coop_effects.h. Each plan's status
// stand-ins (CoopFx_Burn, _Freeze, _Shock, _Nayru on the puppets) go to the
// spawn director, which makes them from the puppet pack and places them
// every game frame; one that leaves the plan is deleted.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform_env.h"  // ORM_LOG_*
#include "coop_effects.h"
#include "coop_puppet.h"
#include "coop_spawn.h"

namespace CoopEffects {

void UseGame(const Settings& settings) {
    if (!settings.status) {
        return;
    }
    CoopPuppet::OnPlan([](const CoopPuppet::Input& input) {
        // The present thread only (CoopPuppet::Publish).
        static bool used = false;
        static FxKeys keys;  // ORM review: a new key each time a status shows again
        static std::size_t shown = 0;
        if (input.main_base == 0) {
            return;  // no game: the spawn director keeps its own base
        }
        if (!used) {
            used = true;
            CoopSpawn::UseDirector();  // the cave caller and the pack's stand-ins
        }
        std::vector<const CoopPuppet::Target*> on;
        std::vector<FxKeys::Shown> pairs;
        for (const CoopPuppet::Target& t : input.targets) {
            if (t.number == 0 || t.status == Status::none || t.show != CoopPuppet::Show::show) {
                continue;
            }
            on.push_back(&t);
            pairs.push_back({t.number, t.status});
        }
        const std::vector<std::uint64_t> made = keys.Plan(input.scene, pairs);
        std::vector<CoopSpawn::Copy> copies;
        for (std::size_t i = 0; i < on.size(); ++i) {
            const CoopPuppet::Target& t = *on[i];
            FxPlace f = FxAt(t.number, t.status, t.position, keys.generation());
            f.key = made[i];
            CoopSpawn::Copy c;
            c.key = f.key;
            c.actor = f.actor;
            c.position = f.position;
            c.act = CoopSpawn::Act::show;
            c.remakes = 2;  // an effect that ends by itself comes back while wanted
            copies.push_back(std::move(c));
        }
        if (copies.size() != shown) {
            ORM_LOG_INFO("coop: effects: {} status stand-in(s) on the puppets", copies.size());
            shown = copies.size();
        }
        CoopSpawn::TheDirector().SetEffects(input.main_base, std::move(copies));
    });
}

}  // namespace CoopEffects
