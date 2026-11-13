// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM quick messages and the world ping (oot-remake docs/COMUNICACION.md):
// the pad and the switches. The rules (the table, the combination, the
// rate limits, the markers) are portable, in coop/host/quick_chat.hpp; the
// sending, the receiving and the markers' drawing are in coop_hook.cpp.
//
// Switches (coop_switch.h readings):
//   LICHII_ORM_QUICK_CHAT=1          the combination is read (off by default
//                                    until seen in a real game); received
//                                    quick messages and their markers show
//                                    whatever this says
//   LICHII_ORM_QUICK_CHAT_BUTTON=L   the button held with the d-pad: L, R, ZL,
//                                    ZR, PLUS, MINUS, LCLICK or RCLICK (any
//                                    case); anything else: L, and a log line
//
// Log lines: "coop: quick: on: hold L and press the d-pad ...", "coop: quick:
// sent '¡Aquí!' (L+DUP)", "... not sent: ...", "coop: quick: P2 Ana
// '¡Aquí!'", "coop: quick: marker for P2 at (x, y, z) in <scene> for 5 s".
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "coop/host/quick_chat.hpp"
#include "coop_switch.h"

namespace CoopPlatform {
class Platform;
}

namespace OrmQuickChat {

struct Config {
    bool on = false;
    std::uint64_t button = coop::host::quick_chat::kL;
    std::string button_name = "L";
    // A switch value that is not one ("" when none): logged once.
    std::string warning;
};

// The reading of the two switches' values (nullptr or "": unset). Portable,
// for the tests.
inline Config Read(const char* on, const char* button) {
    Config c;
    c.on = CoopSwitch::From(on, false);
    if (on != nullptr) {
        if (auto complaint = CoopSwitch::Complaint("LICHII_ORM_QUICK_CHAT", on, false)) {
            c.warning = *complaint;
        }
    }
    if (button != nullptr && button[0] != '\0') {
        if (const auto bit = coop::host::quick_chat::button_bit(button)) {
            c.button = *bit;
            c.button_name = button;
            for (char& ch : c.button_name) {
                if (ch >= 'a' && ch <= 'z') {
                    ch = static_cast<char>(ch - 'a' + 'A');
                }
            }
        } else {
            c.warning = std::string("coop: quick: LICHII_ORM_QUICK_CHAT_BUTTON=") + button +
                        " is not L, R, ZL, ZR, PLUS, MINUS, LCLICK or RCLICK: L is used";
        }
    }
    return c;
}

// "L+DUP" for the log.
inline std::string ComboName(const Config& config, coop::host::quick_chat::Quick quick) {
    return config.button_name + "+" + std::string(coop::host::quick_chat::entry(quick).dpad_name);
}

// The pad as another reader of the d-pad should see it (the downed
// spectator's left and right): without the d-pad while quick chat is on and
// its button is held, so one press does not do two things.
inline std::optional<std::uint64_t> Strip(const Config& config, std::optional<std::uint64_t> buttons) {
    if (!config.on || !buttons || (*buttons & config.button) == 0) {
        return buttons;
    }
    return *buttons & ~coop::host::quick_chat::kDpad;
}

// ---- Where a game runs only (orm_quick_chat.cpp, through platform.h) ----

// The switches, read once from the environment (and logged once).
const Config& Settings();

// Present thread, once a frame: reads player 1's pad (or the handheld one;
// scripted input included, as the revive's A), keeps the d-pad from the game
// while the button is held (orm_input_mask.h, patch 0101) and returns the
// message the combination sent this frame. Nothing while off or with no pad.
std::optional<coop::host::quick_chat::Quick> Poll(CoopPlatform::Platform& platform);

// ORM (coop/host/shoulder.hpp): the warp-song invitation's hold, from the same pad reading as
// the quick messages: `invite_bits` all down and no quick message (a d-pad press) since the
// quick button went down, when the two share it. Present thread, once a frame or more.
bool InviteHeld(CoopPlatform::Platform& platform, std::uint64_t invite_bits);

// Strip with Settings().
std::optional<std::uint64_t> ForOthers(std::optional<std::uint64_t> buttons);

} // namespace OrmQuickChat
