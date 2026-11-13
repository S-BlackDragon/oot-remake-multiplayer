// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM held floor switches (oot-remake docs/SOSTENIDOS.md; the rules are
// coop/host/held_switch.hpp): while another player's feet are on a held step
// switch of our scene (as presence draws them), the switch is pressed in our
// game too, so the door or fire wall it holds is open here as well; when the
// last one steps off, leaves or disconnects, it is let go (unless our own Link
// stands on it: then the game holds it itself).
//
// Pressing it here: the switch's "Link on it" flag (the held: lines of
// shared_dungeon_adult.txt) and the actor words of its kind (word lines of
// LICHII_ORM_HELD_SWITCH_LIST, found with the probe below). The actor is the
// one of its placement hash in the objects walk (LICHII_COOP_OBJECTS).
//
// Switches (each also read under its name before test9, LICHII_COOP_*;
// on/off values as coop_switch.h reads them):
//   LICHII_ORM_HELD_SWITCHES      unset or "flag" (the default): press with
//                                 the switch's "Link on it" flag only, and
//                                 watch the door near it to log whether the
//                                 flag alone opens it (docs/RIESGOS_RESUELTOS.md);
//                                 "log": watch and log only (reads);
//                                 1/true/yes/on or "press": flag and words;
//                                 0/false/no/off: off.
//   LICHII_ORM_HELD_SWITCH_LIST   a list file on top of the built-in one
//                                 (switch, word, off lines).
//   LICHII_ORM_HELD_SWITCH_PROBE  on: when our own Link steps on or off a
//                                 listed switch, log the switch actor's words
//                                 that changed, as word lines ready to copy.
// Logs: "coop: held switch: ..." (docs/SOSTENIDOS.md).
// Test: tests/lichii/held_switch_test.cpp (the probe and the mode), the rules
//   in tests/host/held_switch_test.cpp.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/held_switch.hpp"
#include "coop/overlay/overlay.hpp"

namespace CoopPlatform {
class Platform;
}

namespace CoopHeldSwitch {

enum class Mode : std::uint8_t { off, log, flag, press };

// LICHII_ORM_HELD_SWITCHES: 0/false/no/off off; 1/true/yes/on or "press"
// press; "log" log (any case); anything else (unset, "flag") flag.
Mode ParseMode(std::string_view text);
// Whether `text` is one of the values ParseMode knows (empty too); the glue
// logs one line for the others (coop_switch.h).
bool KnownMode(std::string_view text);

// The others as the hook has them (overlay::collect): live while presence
// still shows them (fade above 0).
std::vector<coop::host::HeldOther> OthersOf(const std::vector<coop::overlay::Shown>& shown);

// LICHII_ORM_HELD_SWITCH_PROBE: the words of a switch actor that change when
// our own Link steps on or off it. A quiet copy of the actor (as the sign
// probe reads it: the actor, its component table, the first 0x200 bytes of
// each component) is taken every kBaseEveryUs while our Link is off it;
// words that changed between two quiet copies are noisy and left out. kAfterUs
// after a step, the words that differ from the last quiet copy are logged as
// "word <actor> <where> on=<on> off=<off>" lines (on: the value with our Link
// on it). Pure over ReadWord.
class StepProbe {
public:
    using ReadWord = std::function<std::optional<std::uint64_t>(std::uint64_t)>;
    static constexpr std::uint64_t kBaseEveryUs = 1'000'000;
    static constexpr std::uint64_t kAfterUs = 500'000;
    static constexpr std::size_t kMaxShown = 24;
    static constexpr int kMaxLines = 200;

    // Once a frame, with what the rule saw and the actors of our scene.
    std::vector<std::string> Look(const coop::host::HeldFrame& frame, const coop::host::HeldSwitchList& list,
                                  const std::function<std::optional<std::uint64_t>(std::uint64_t)>& find,
                                  const ReadWord& read, std::uint64_t now_us);
    void Reset();

private:
    using Snapshot = std::map<std::uint64_t, std::uint64_t>;
    struct State {
        std::uint64_t actor = 0;
        Snapshot base;          // the last quiet copy (our Link off it)
        std::uint64_t base_us = 0;
        std::set<std::uint64_t> noisy;
        std::optional<std::uint64_t> report_at_us;
        bool on = false;  // our Link on it at the step being reported
        Snapshot on_copy; // the copy when our Link was on it (for the off step)
    };
    std::map<std::uint64_t, State> states_;  // by placement hash
    int lines_ = 0;
};

// Present thread, once a frame after CoopObjects::Share. `store`: the game's
// flags (null: not readable this frame).
void Frame(CoopPlatform::Platform& platform, const std::optional<std::string>& scene,
           const std::optional<coop::protocol::Vec3>& link, const std::vector<coop::overlay::Shown>& others,
           coop::host::FlagStore* store, std::uint64_t now_us);

} // namespace CoopHeldSwitch
