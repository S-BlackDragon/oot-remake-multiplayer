// SPDX-License-Identifier: GPL-3.0-or-later
//
// The escape goes with the group (coop_escape.h). No emulator code.

#include "coop_escape.h"

#include "coop_switch.h"

#include <string_view>
#include <utility>

namespace CoopEscape {
namespace {

using coop::host::DownedState;
using Phase = CoopTeleport::Trip::Phase;

std::string Who(std::uint8_t number) {
    return "P" + std::to_string(number);
}

// The rule's own lines say "group exit: ..."; the hook's say "escape: ...".
std::string Retag(std::string line) {
    constexpr std::string_view kRule = "group exit: ";
    if (std::string_view(line).starts_with(kRule)) {
        line.erase(0, kRule.size());
    }
    return "escape: " + line;
}

} // namespace

coop::host::GroupExitSettings SettingsFrom(const char* env) {
    return SettingsFrom(CoopSwitch::From(env, true));
}

coop::host::GroupExitSettings SettingsFrom(bool on) {
    coop::host::GroupExitSettings s;
    s.on = on;
    return s;
}

Escape::Escape(coop::host::GroupExitSettings settings, std::vector<coop::host::GroupExit> exits)
    : settings_(settings), exits_(exits), rule_(std::move(exits), settings) {}

void Escape::Reset() {
    rule_.reset();
    ours_.reset();
    escaping_us_ = {};
}

bool Escape::ExitEnd(const std::optional<std::string>& scene) const {
    if (!settings_.on || !scene) {
        return false;
    }
    for (const auto& e : exits_) {
        if (e.to == *scene) {
            return true;
        }
    }
    return false;
}

bool Escape::Escaping(std::uint8_t number, std::uint64_t now_us) const {
    if (!settings_.on || number == 0 || number >= escaping_us_.size() || !escaping_us_[number]) {
        return false;
    }
    return now_us - *escaping_us_[number] <= kEscaperUs;
}

Actions Escape::Frame(const Input& in) {
    Actions out;
    if (!settings_.on) {
        return out;
    }
    for (const auto& m : in.mates) {
        if (m.number == 0 || m.number >= escaping_us_.size() || !m.scene) {
            continue;
        }
        for (const auto& e : exits_) {
            if (e.from == *m.scene) {
                escaping_us_[m.number] = in.now_us;
            }
        }
    }

    const bool up = in.downed == DownedState::alive;
    if (ours_) {
        if (in.trip == Phase::idle || in.trip_player != *ours_) {
            ours_.reset();  // done, given up, or replaced by a /teleport of the player's own
        } else if (!up) {
            out.stop = true;
            out.log.push_back("escape: our Link is down (" + std::string(coop::host::to_string(in.downed)) +
                              "): the trip to " + Who(*ours_) +
                              " stops; a scene change would stand him up. Again once he is up");
            ours_.reset();
        } else {
            return out;  // ours is on its way: the rule waits for it (and keeps its clocks)
        }
    }

    coop::host::GroupExitInput rule_in;
    rule_in.now_us = in.now_us;
    rule_in.scene = in.scene;
    rule_in.up = up;
    rule_in.event = in.event;
    for (const auto& m : in.mates) {
        rule_in.mates.push_back({m.number, m.scene, m.up});
    }
    auto act = rule_.update(rule_in);
    const bool own_trip = in.trip != Phase::idle && !out.stop;
    if (act.follow && own_trip) {
        // The player's own /teleport runs: it is not replaced; the rule asks
        // again after its retry time.
        out.log.push_back("escape: a /teleport to " + Who(in.trip_player) +
                          " runs; the escape does not replace it (asked again later)");
        return out;
    }
    for (auto& line : act.log) {
        out.log.push_back(Retag(std::move(line)));
    }
    if (!act.follow || out.stop) {
        return out;
    }
    out.follow = act.follow;
    ours_ = act.follow;
    return out;
}

CoopTeleport::Answer AnswerTo(const CoopTeleport::Answer& normal, const CoopTeleport::Answer& group, bool escaper,
                              bool at_exit_end) {
    if (normal.accept || !escaper || !at_exit_end || !group.accept) {
        return normal;
    }
    if (normal.reason != coop::protocol::JoinRefuseReason::cannot_warp &&
        normal.reason != coop::protocol::JoinRefuseReason::declined) {
        return normal;  // busy (loading, an event, in the air): busy for the escape too
    }
    return group;
}

} // namespace CoopEscape
