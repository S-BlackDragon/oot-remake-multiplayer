// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM held floor switches: the frame glue (coop_held_switch.h). Reads the
// switches, logs what the rule sees and, with LICHII_ORM_HELD_SWITCHES=1,
// presses them in our game while others stand on them.

#include <cstdlib>
#include <string>

#include "coop/host/game_memory.hpp"
#include "coop_held_switch.h"
#include "coop_objects.h"
#include "coop_switch.h"
#include "platform.h"

namespace CoopHeldSwitch {
namespace {

struct Held {
    Held() {
        // coop_switch.h: the LICHII_ORM_* name, or the name before test9.
        if (const auto found = CoopSwitch::Find("LICHII_ORM_HELD_SWITCHES", "LICHII_COOP_HELD_SWITCHES")) {
            mode = ParseMode(found->value);
            if (!KnownMode(found->value)) {
                CoopSwitch::Log(CoopSwitch::Unknown(found->name, found->value,
                                                    "0/1/true/false/yes/no/on/off/log/flag/press", "flag"));
            }
        }
        probe_on = CoopSwitch::On("LICHII_ORM_HELD_SWITCH_PROBE", "LICHII_COOP_HELD_SWITCH_PROBE", false);
        list = coop::host::HeldSwitchList::builtin();
        if (const auto path = CoopSwitch::Value("LICHII_ORM_HELD_SWITCH_LIST", "LICHII_COOP_HELD_SWITCH_LIST");
            path && mode != Mode::off) {
            std::string error;
            if (auto extra = coop::host::HeldSwitchList::load(*path, &error)) {
                list.merge(*extra);
                ORM_LOG_INFO("coop: held switch: list {}: {} switches, {} words in all", *path,
                             list.switches().size(), list.words().size());
            } else {
                ORM_LOG_WARNING("coop: held switch: list not taken ({}): the built-in one only", error);
            }
        }
        if (mode != Mode::off) {
            ORM_LOG_INFO("coop: held switch: {} ({} switches, {} words){}",
                     mode == Mode::press  ? "on: switches others stand on are pressed here"
                     : mode == Mode::flag ? "on: switches others stand on are pressed here with their flag "
                                            "(the door near each tells whether that opens it; "
                                            "LICHII_ORM_HELD_SWITCHES=1 adds the words)"
                                          : "watching only (LICHII_ORM_HELD_SWITCHES=1 presses them)",
                     list.switches().size(), list.words().size(), probe_on ? ", probe on" : "");
        }
    }
    Mode mode = Mode::flag;  // docs/RIESGOS_RESUELTOS.md: the flag path by default
    bool probe_on = false;
    coop::host::HeldSwitchList list;
    coop::host::HeldSwitchRule rule{list};
    coop::host::HeldFlagApply flags{list};
    coop::host::HeldWordApply words{list};
    coop::host::HeldDoorCheck doors{list};
    StepProbe probe;
    bool no_words_said = false;
};

Held& TheHeld() {
    static Held held;
    return held;
}

} // namespace

void Frame(CoopPlatform::Platform& platform, const std::optional<std::string>& scene,
           const std::optional<coop::protocol::Vec3>& link, const std::vector<coop::overlay::Shown>& others,
           coop::host::FlagStore* store, std::uint64_t now_us) {
    Held& h = TheHeld();
    if (h.mode == Mode::off) {
        return;
    }
    coop::host::HeldInput in;
    in.scene = scene;
    in.link = link;
    in.others = OthersOf(others);
    in.now_us = now_us;
    const coop::host::HeldFrame frame = h.rule.frame(in);

    if (frame.scene_changed && !frame.left.empty()) {
        ORM_LOG_INFO("coop: held switch: scene left with {} switches held by others: let go with it",
                     frame.left.size());
    }
    for (const auto& c : frame.changes) {
        const coop::host::HeldSwitchSpec* spec = h.list.find(c.hash);
        if (!spec) {
            continue;
        }
        const std::string name = spec->place + " " + (spec->label.empty() ? spec->actor : spec->label);
        using Kind = coop::host::HeldChange::Kind;
        switch (c.kind) {
        case Kind::stepped_on:
        case Kind::stepped_off:
        case Kind::gone:
            ORM_LOG_INFO("coop: held switch: {} {} {}", name, c.who == 0 ? std::string("our Link")
                                                                               : "P" + std::to_string(c.who),
                         c.kind == Kind::gone ? "left it (gone from the scene)" : coop::host::to_string(c.kind));
            break;
        case Kind::pressed:
            ORM_LOG_INFO("coop: held switch: {} held by another player (P{}): {}{}", name, c.who,
                     h.mode != Mode::log ? "pressed here" : "would be pressed here (watching only)",
                     c.ours ? ", our Link is on it too" : "");
            break;
        case Kind::released:
            ORM_LOG_INFO("coop: held switch: {} no other player on it: {}", name,
                     c.ours ? "our Link holds it" : h.mode != Mode::log ? "let go here" : "would be let go here");
            break;
        }
    }
    if (h.mode == Mode::log && !h.probe_on) {
        return;
    }

    CoopPlatform::GuestMemory& memory = platform.Memory();
    const auto read = [&memory](std::uint64_t address) -> std::optional<std::uint64_t> {
        if (address < 0x1000) {
            return std::nullopt;
        }
        return coop::host::read_u64(memory, address);  // nothing when the 8 bytes are not mapped
    };
    const auto find = [&scene](std::uint64_t hash) { return CoopObjects::LoadedActor(hash, scene); };

    if (h.probe_on) {
        for (const auto& line : h.probe.Look(frame, h.list, find, read, now_us)) {
            ORM_LOG_INFO("coop: {}", line);
        }
    }
    if (h.mode == Mode::log) {
        return;
    }
    if (store) {
        for (const auto& n : h.flags.apply(frame, *store)) {
            ORM_LOG_INFO("coop: held switch: flag {:#010x} = {} {}{}", n.flag, n.value,
                         n.ok ? "written" : "not written (tried again each frame)",
                         n.again ? " again (the game had put it back to 0)" : "");
        }
    }
    const auto write = [&platform](std::uint64_t address, std::uint64_t value) {
        return address >= 0x1000 && platform.Write64(address, value);  // the guest CPU's own 64-bit store
    };
    if (h.mode == Mode::press) {
        for (const auto& n : h.words.apply(frame, find, read, write)) {
            ORM_LOG_INFO("coop: held switch: {:016x} word {} at {:#x} {:#x} -> {:#x}{}{}", n.hash, n.where,
                     n.address, n.before, n.after, n.release ? " (let go)" : "", n.ok ? "" : ": not written");
        }
    }
    // docs/RIESGOS_RESUELTOS.md: did what we wrote open the door near it? The
    // door actors of our scene from the objects walk (nothing: not read).
    std::optional<std::vector<coop::host::HeldDoorSeen>> doors;
    if (const auto actors = CoopObjects::WalkActors(scene)) {
        doors.emplace();
        for (const auto& a : *actors) {
            if (a.position && coop::host::held_door_actor(a.name)) {
                doors->push_back({a.name, *a.position, a.animation});
            }
        }
    }
    const auto path_of = [&h](const coop::host::HeldSwitchSpec& spec) {
        coop::host::HeldDoorCheck::Path p;
        p.flag = spec.flag.has_value() && h.flags.writing(spec.hash);
        p.words = h.mode == Mode::press && !h.list.words_for(spec.actor).empty();
        return p;
    };
    for (const auto& line : h.doors.frame(frame, doors, path_of, now_us)) {
        ORM_LOG_INFO("coop: {}", line);
    }
    if (!h.no_words_said) {
        for (const auto& s : frame.states) {
            const auto* spec = h.list.find(s.hash);
            if (s.held_by_others() && spec && h.list.words_for(spec->actor).empty() && !spec->flag) {
                h.no_words_said = true;
                ORM_LOG_INFO("coop: held switch: {} {} has no word line yet: nothing to press it with "
                             "(run the probe, docs/SOSTENIDOS.md)",
                             spec->place, spec->actor);
                break;
            }
        }
    }
}

} // namespace CoopHeldSwitch
