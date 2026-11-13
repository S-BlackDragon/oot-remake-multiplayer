// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM held floor switches: the parts with no emulator (coop_held_switch.h).
// The frame glue is coop_held_switch_game.cpp.

#include "coop_held_switch.h"

#include <algorithm>
#include <cstdio>

#include "coop_switch.h"
#include "coop_world.h"

namespace CoopHeldSwitch {

namespace {
// The value without the spaces around it (as CoopSwitch::Parse reads it).
std::string_view Trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}
} // namespace

Mode ParseMode(std::string_view text) {
    text = Trimmed(text);
    switch (CoopSwitch::Parse(text)) {
    case CoopSwitch::Reading::off:
        return Mode::off;
    case CoopSwitch::Reading::on:
        return Mode::press;
    case CoopSwitch::Reading::unset:
    case CoopSwitch::Reading::unknown:
        break;
    }
    if (CoopSwitch::detail::SameNoCase(text, "press")) {
        return Mode::press;
    }
    if (CoopSwitch::detail::SameNoCase(text, "log")) {
        return Mode::log;
    }
    return Mode::flag;  // unset, "flag", or a value it does not know (the default)
}

bool KnownMode(std::string_view text) {
    text = Trimmed(text);
    return CoopSwitch::Parse(text) != CoopSwitch::Reading::unknown || CoopSwitch::detail::SameNoCase(text, "press") ||
           CoopSwitch::detail::SameNoCase(text, "log") || CoopSwitch::detail::SameNoCase(text, "flag");
}

std::vector<coop::host::HeldOther> OthersOf(const std::vector<coop::overlay::Shown>& shown) {
    std::vector<coop::host::HeldOther> out;
    out.reserve(shown.size());
    for (const auto& s : shown) {
        coop::host::HeldOther o;
        o.number = s.number;
        o.scene = s.scene;
        o.position = s.position;
        o.live = s.fade > 0.0f;
        out.push_back(std::move(o));
    }
    return out;
}

void StepProbe::Reset() {
    states_.clear();
}

std::vector<std::string> StepProbe::Look(const coop::host::HeldFrame& frame,
                                         const coop::host::HeldSwitchList& list,
                                         const std::function<std::optional<std::uint64_t>(std::uint64_t)>& find,
                                         const ReadWord& read, std::uint64_t now_us) {
    std::vector<std::string> lines;
    if (frame.scene_changed) {
        states_.clear();
    }
    auto words_line = [&](const coop::host::HeldSwitchSpec& spec, const Snapshot& off, const Snapshot& on,
                          const std::set<std::uint64_t>& noisy, bool stepped_on) {
        std::vector<std::string> words;
        std::size_t changed = 0;
        for (const auto& [key, off_value] : off) {
            const auto it = on.find(key);
            if (it == on.end() || it->second == off_value || noisy.count(key)) {
                continue;
            }
            ++changed;
            if (words.size() < kMaxShown) {
                char text[160];
                std::snprintf(text, sizeof text, "word %s %s on=0x%llx off=0x%llx", spec.actor.c_str(),
                              CoopWorld::SignProbe::Where(key).c_str(),
                              static_cast<unsigned long long>(it->second),
                              static_cast<unsigned long long>(off_value));
                words.emplace_back(text);
            }
        }
        char head[200];
        std::snprintf(head, sizeof head, "held switch: probe: %s %s (%016llx) our Link stepped %s: %zu words changed%s",
                      spec.place.c_str(), spec.label.empty() ? spec.actor.c_str() : spec.label.c_str(),
                      static_cast<unsigned long long>(spec.hash), stepped_on ? "on" : "off", changed,
                      changed > words.size() ? " (first ones shown)" : "");
        if (lines_ < kMaxLines) {
            ++lines_;
            lines.emplace_back(head);
            for (auto& w : words) {
                lines.push_back("held switch: probe:   " + w);
            }
        }
    };
    std::set<std::uint64_t> stepped_on;
    std::set<std::uint64_t> stepped_off;
    for (const auto& c : frame.changes) {
        if (c.who != 0) {
            continue;
        }
        if (c.kind == coop::host::HeldChange::Kind::stepped_on) {
            stepped_on.insert(c.hash);
        } else if (c.kind == coop::host::HeldChange::Kind::stepped_off) {
            stepped_off.insert(c.hash);
        }
    }
    for (const auto& state : frame.states) {
        const coop::host::HeldSwitchSpec* spec = list.find(state.hash);
        if (!spec) {
            continue;
        }
        const auto actor = find(state.hash);
        if (!actor) {
            states_.erase(state.hash);
            continue;
        }
        State& s = states_[state.hash];
        if (s.actor != *actor) {  // a new actor (the room loaded again)
            s = State{};
            s.actor = *actor;
        }
        if (stepped_on.count(state.hash) && !s.base.empty()) {
            s.report_at_us = now_us + kAfterUs;
            s.on = true;
        } else if (stepped_off.count(state.hash) && !s.on_copy.empty()) {
            s.report_at_us = now_us + kAfterUs;
            s.on = false;
        }
        if (s.report_at_us && now_us >= *s.report_at_us) {
            const Snapshot now = CoopWorld::SignProbe::Read(read, *actor);
            if (s.on && state.ours) {
                words_line(*spec, s.base, now, s.noisy, true);
                s.on_copy = now;
            } else if (!s.on && !state.ours) {
                words_line(*spec, now, s.on_copy, s.noisy, false);
                s.on_copy.clear();
                s.base = now;  // quiet again
                s.base_us = now_us;
            }
            s.report_at_us.reset();
            continue;
        }
        // Quiet copies only while nobody stands on it here or elsewhere and
        // no report waits (a held-by-others press changes words too).
        if (!state.ours && !state.held_by_others() && !s.report_at_us &&
            (s.base.empty() || now_us >= s.base_us + kBaseEveryUs)) {
            Snapshot copy = CoopWorld::SignProbe::Read(read, *actor);
            if (copy.empty()) {
                continue;
            }
            for (const auto& [key, value] : copy) {
                const auto it = s.base.find(key);
                if (it != s.base.end() && it->second != value) {
                    s.noisy.insert(key);
                }
            }
            s.base = std::move(copy);
            s.base_us = now_us;
        }
    }
    return lines;
}

} // namespace CoopHeldSwitch
