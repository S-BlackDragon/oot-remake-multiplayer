// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM cofre: the portable part of coop_chest_open.h (no emulator code).
#include "coop_chest_open.h"

#include "coop_switch.h"

namespace CoopChestOpen {

std::vector<coop::host::FindMate> Mates(const std::vector<CoopPuppet::Remote>& remotes) {
    std::vector<coop::host::FindMate> out;
    out.reserve(remotes.size());
    for (const auto& r : remotes) {
        if (r.number == 0) {
            continue;
        }
        coop::host::FindMate m;
        m.number = r.number;
        m.scene = r.scene;
        m.position = r.position;
        m.adult = r.adult;
        m.command = r.anim ? r.anim->name : std::string();
        m.frame = r.anim ? r.anim->frame : 0.0f;
        m.shown = r.fade > 0.0f;
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<CoopSpawn::Copy> Copies(const std::vector<coop::host::HeldFind>& held) {
    std::vector<CoopSpawn::Copy> out;
    out.reserve(held.size());
    for (const auto& h : held) {
        if (h.number == 0 || h.drop.empty()) {
            continue;
        }
        CoopSpawn::Copy c;
        c.key = CoopSpawn::Director::kFindKeyBit | (std::uint64_t{h.serial & 0xffffffu} << 8) | h.number;
        c.actor = h.drop;
        c.position = h.position;
        c.act = CoopSpawn::Act::show;
        c.place = true;  // written every frame, as the puppet is placed (a stand-in)
        out.push_back(std::move(c));
    }
    return out;
}

bool On() {
    static const bool on = CoopSwitch::On("LICHII_ORM_CHEST_OPEN", true);
    return on;
}

}  // namespace CoopChestOpen
