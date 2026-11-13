// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM quick messages: the pad and the switches (orm_quick_chat.h). Present
// thread, except Settings (any thread, read once).
#include "orm_quick_chat.h"

#include <mutex>
#include <string>

#include "coop/host/shoulder.hpp"
#include "orm_input_mask.h"
#include "platform.h"  // Platform (the pad), ORM_LOG_*

namespace OrmQuickChat {
namespace {

namespace qc = coop::host::quick_chat;

// ORM (coop/host/shoulder.hpp, docs/RIESGOS_RESUELTOS.md): L shared with the warp-song
// invitation. Read by Poll and by InviteHeld (the songs glue), under its lock.
struct Shared {
    std::mutex mutex;
    coop::host::ShoulderArbiter arbiter{Settings().button};
    Shared() { arbiter.set_quick(Settings().on); }
};
Shared& TheShared() {
    static Shared shared;
    return shared;
}

void LogNotes(coop::host::ShoulderArbiter& a) {
    for (const auto& line : a.take_notes()) {
        ORM_LOG_INFO("coop: {}", line);
    }
}

} // namespace

const Config& Settings() {
    static const Config config = [] {
        const auto on = CoopSwitch::Value("LICHII_ORM_QUICK_CHAT");
        const auto button = CoopSwitch::Value("LICHII_ORM_QUICK_CHAT_BUTTON");
        Config c = Read(on ? on->c_str() : nullptr, button ? button->c_str() : nullptr);
        if (!c.warning.empty()) {
            ORM_LOG_WARNING("{}", c.warning);
        }
        if (c.on) {
            ORM_LOG_INFO(
                     "coop: quick: on (LICHII_ORM_QUICK_CHAT): hold {} and press the d-pad: up '{}', "
                     "right '{}', down '{}', left '{}'; the d-pad is kept from the game while {} is held",
                     c.button_name, qc::entry(qc::Quick::here).text, qc::entry(qc::Quick::go).text,
                     qc::entry(qc::Quick::wait).text, qc::entry(qc::Quick::help).text, c.button_name);
        } else {
            ORM_LOG_INFO("coop: quick: off (LICHII_ORM_QUICK_CHAT=1 turns it on); quick messages "
                         "from others still show");
        }
        return c;
    }();
    return config;
}

// The pad: player 1's, or the handheld one, as coop_testinput plays into it
// (platform.h PadButtons: the scripted buttons included, as the revive's A).
std::optional<qc::Quick> Poll(CoopPlatform::Platform& platform) {
    const Config& config = Settings();
    if (!config.on) {
        OrmInputMask::Set(0);
        return std::nullopt;
    }
    Shared& s = TheShared();
    std::lock_guard lock(s.mutex);
    s.arbiter.step(platform.PadButtons());
    OrmInputMask::Set(s.arbiter.mask());
    LogNotes(s.arbiter);
    return s.arbiter.take_quick();
}

bool InviteHeld(CoopPlatform::Platform& platform, std::uint64_t invite_bits) {
    Shared& s = TheShared();
    std::lock_guard lock(s.mutex);
    s.arbiter.set_invite(invite_bits);
    s.arbiter.step(platform.PadButtons());
    LogNotes(s.arbiter);
    return s.arbiter.invite_held();
}

std::optional<std::uint64_t> ForOthers(std::optional<std::uint64_t> buttons) {
    return Strip(Settings(), buttons);
}

} // namespace OrmQuickChat
