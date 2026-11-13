// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "Continue with the group's game", the emulator side (coop_continue.h).

#include "coop_continue.h"

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/continue_load.hpp"
#include "coop/host/flag_watcher.hpp"
#include "coop/host/session.hpp"
#include "coop_switch.h"
#include "platform_env.h"

namespace CoopContinue {
namespace {

namespace host = coop::host;

struct Glue {
    std::once_flag checked;
    host::ContinueMode mode = host::ContinueMode::keep;
    bool bad_value = false;
    host::ContinueLoad rule;
    bool told = false;  // the switch line was logged
    host::ContinueMode sent = host::ContinueMode::off;  // the last mode logged
    // Per player values a kept Continue keeps too (CONTINUAR.md): the wallet
    // and the ammo (host::continue_keep_extra). Hearts capacity and small
    // keys are shared flags, kept with the others.
    std::vector<coop::protocol::FlagKey> extra = host::continue_keep_extra_keys();
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        // coop_switch.h's on and off values, then the selector's own words.
        const auto v = CoopSwitch::Value("LICHII_ORM_CONTINUE");
        const std::string_view text = v ? std::string_view(*v) : std::string_view{};
        std::optional<host::ContinueMode> mode;
        switch (CoopSwitch::Parse(text)) {
        case CoopSwitch::Reading::off:
            mode = host::ContinueMode::off;
            break;
        case CoopSwitch::Reading::on:
        case CoopSwitch::Reading::unset:
            mode = host::ContinueMode::keep;
            break;
        case CoopSwitch::Reading::unknown:
            mode = host::parse_continue_mode(text);
            break;
        }
        glue.bad_value = !mode;
        glue.mode = mode.value_or(host::ContinueMode::keep);
        glue.rule = host::ContinueLoad(glue.mode);
    });
    return glue;
}

} // namespace

bool On() {
    return TheGlue().mode != host::ContinueMode::off;
}

void Frame(host::Session* session, host::DownedState state, bool group_defeat, std::uint64_t now_us,
           bool event_running, std::string_view event_name) {
    Glue& g = TheGlue();
    if (!g.told) {
        g.told = true;
        switch (g.mode) {
        case host::ContinueMode::keep:
            ORM_LOG_INFO("coop: continue: keep{}: after a game over the group goes on with what it had "
                         "when it fell (LICHII_ORM_CONTINUE=save follows the host's reload, 0 turns it off)",
                         g.bad_value ? " (LICHII_ORM_CONTINUE not keep, save, 0/1/true/false/yes/no/on/off: the default)"
                                     : "");
            break;
        case host::ContinueMode::save:
            ORM_LOG_INFO("coop: continue: save (LICHII_ORM_CONTINUE=save): after a game over the group "
                         "follows the host's Continue");
            break;
        case host::ContinueMode::off:
            ORM_LOG_INFO("coop: continue: off (LICHII_ORM_CONTINUE=0): a Continue's reload follows the "
                         "flag watcher's own rules");
            break;
        }
    }
    if (g.mode == host::ContinueMode::off || session == nullptr) {
        return;
    }
    host::ContinueInput in;
    in.now_us = now_us;
    in.state = state;
    in.event_running = event_running;
    in.event_name = event_name;
    in.group_defeat = group_defeat;
    if (const host::FlagWatcher* w = session->flags()) {
        in.hosting = w->role() == host::FlagRole::host;
        in.settling = w->settling();
        in.host_resets = w->stats().host_resets;
        in.load_reverts = w->stats().load_reverts;
        in.reset_applied = w->stats().reset_applied;
        in.continue_kept = w->stats().continue_kept;
        in.continue_counts = w->stats().continue_counts;
    }
    const host::ContinueOutput out = g.rule.tick(in);
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: continue: {}", line);
    }
    // Each frame: a new watcher (a reconnect) gets it too.
    session->set_continue_keep_extra(g.extra);
    session->set_continue_load(out.mode);
    if (out.mode != g.sent) {
        g.sent = out.mode;
        ORM_LOG_INFO("coop: continue: the next load window {}",
                     out.mode == host::ContinueMode::keep   ? "keeps the group's values (a kept Continue)"
                     : out.mode == host::ContinueMode::save ? "is a save load (the host's reload decides)"
                                                            : "follows the usual rules again");
    }
}

} // namespace CoopContinue
