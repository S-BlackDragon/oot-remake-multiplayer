// SPDX-License-Identifier: GPL-3.0-or-later
//
// The end of the game with up to six players, the emulator side
// (coop_final_game.h). Glue only: what it does is coop_final.cpp.

#include "coop_final_game.h"

#include <mutex>
#include <utility>
#include <vector>

#include "coop/net/client.hpp"
#include "coop_escape_timer.h"
#include "coop_final.h"
#include "coop_objects.h"
#include "coop_switch.h"
#include "platform.h"  // Platform, ORM_LOG_*

namespace CoopFinal {
namespace {

namespace proto = coop::protocol;

struct Glue {
    std::once_flag checked;
    std::optional<Final> final;
    std::mutex inbox_mutex;
    std::vector<proto::StoryEvent> inbox;  // network thread to present
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        Settings s;
        // Renamed switches read under LICHII_ORM_* first, then the old name
        // the scenarios of docs/FINAL.md use (coop_switch.h).
        s.counts = CoopSwitch::Value("LICHII_ORM_COUNTS", "LICHII_COOP_COUNTS");
        s.escape_clock = CoopSwitch::On("LICHII_ORM_ESCAPE_CLOCK", "LICHII_COOP_ESCAPE_CLOCK", false);
        s.clock_word = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_WORD");
        s.clock_length = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_LENGTH_S");
        s.twinrova = CoopSwitch::On("LICHII_ORM_TWINROVA_KEEP", true);
        glue.final.emplace(std::move(s));
    });
    return glue;
}

void Log(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        ORM_LOG_INFO("{}", line);
    }
}

} // namespace

void AddShared(std::optional<coop::host::SharedFlags>& shared) {
    Glue& g = TheGlue();
    {
        std::lock_guard lock(g.inbox_mutex);
        g.inbox.clear();  // a session before this one's
    }
    Log(g.final->Begin(shared));
}

void OnEvent(const coop::net::Event& event) {
    if (event.kind != coop::net::EventKind::message) {
        return;
    }
    const auto* m = std::get_if<proto::StoryEvent>(&event.message);
    if (m == nullptr) {
        return;
    }
    Glue& g = TheGlue();
    if (!g.final->ClockOn()) {
        return;
    }
    std::lock_guard lock(g.inbox_mutex);
    if (g.inbox.size() < 64) {
        g.inbox.push_back(*m);
    }
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading,
           const std::optional<std::string>& event_name, coop::host::DownedState downed,
           std::uint64_t server_now_us) {
    Glue& g = TheGlue();
    std::vector<proto::StoryEvent> inbox;
    {
        std::lock_guard lock(g.inbox_mutex);
        inbox.swap(g.inbox);
    }
    for (const auto& m : inbox) {
        g.final->Story(m);
    }
    FrameIn in;
    in.now_us = CoopEvery::NowUs();
    in.link = main_base != 0 && reading.has_value();
    if (in.link) {
        in.scene = reading->scene;
        if (reading->animation) {
            in.animation = reading->animation->name;
        }
        if (reading->event_running.value_or(false)) {
            in.event_name = event_name;
        }
    }
    in.downed = downed;
    in.server_now_us = server_now_us;
    in.found_word = CoopEscapeTimer::Found();  // ORM: the countdown the search found (no word given)
    coop::host::WritableGameMemory* memory = main_base != 0 ? &platform.Memory() : nullptr;
    const FrameOut out = g.final->Frame(memory, in);
    Log(out.log);
    CoopObjects::SetKeep(out.keep.up, out.keep.frozen, out.keep.charging, out.keep.charge, out.keep.held);
}

} // namespace CoopFinal
