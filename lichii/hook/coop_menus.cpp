// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM pause, menus, saving and leaving with partners, the emulator side
// (coop_menus.h). Glue only: the rules are in coop/host/menus.hpp.

#include "coop_menus.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform_env.h"  // ORM_LOG_*
#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/pause_detect.hpp"
#include "coop/host/save_transfer.hpp"
#include "coop/host/scene.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop/presence/presence.hpp"
#include "coop_every.h"
#include "coop_save.h"
#include "coop_switch.h"

namespace CoopMenus {
namespace {

namespace host = coop::host;
namespace menus = coop::host::menus;

struct Glue {
    std::once_flag checked;
    bool on = false;
    // Present thread only, below.
    menus::Local local;
    menus::Mates mates;
    menus::SaveWatch save_watch;
    CoopEvery::Every save_due{CoopEvery::kQuarterUs};  // the folder's file times, 4 a second
    std::uint64_t slots_seen = 0;                      // CoopSave::SlotsWritten at the last look
    bool slots_known = false;
    std::vector<std::string> lines;  // TakeLines
    bool pause_flag_said = false;    // "Sequence_IsPause unreadable" logged once
    // ORM (coop/host/pause_detect.hpp, docs/RIESGOS_RESUELTOS.md): the frozen frame when the
    // flag is not to be trusted; LICHII_ORM_PAUSE_DETECT=flag keeps the flag alone.
    std::optional<host::PauseDetect> pause;
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        glue.on = CoopSwitch::On("LICHII_ORM_MENUS", true);  // coop_switch.h
        // Flag first by default (SONDAS.md row 4: the flag is set in a pause, an idle Link looks frozen).
        glue.pause.emplace(host::PauseDetect::mode_of(CoopSwitch::Value("LICHII_ORM_PAUSE_DETECT").value_or("")));
        ORM_LOG_INFO("coop: menus: {} (LICHII_ORM_MENUS){}", glue.on ? "on" : "off",
                     glue.on ? ": our pause and title screen are sent, a paused mate stands frozen with "
                               "'(pausa)' on the tag"
                             : "");
    });
    return glue;
}

std::uint32_t LoadingFlag() {
    static const std::uint32_t hash = host::flag_hash("Sequence_IsLoadingBanc");
    return hash;
}

void AddLine(Glue& g, std::string line) {
    if (line.empty() || g.lines.size() >= 16) {
        return;
    }
    ORM_LOG_INFO("coop: notice: {}", line);
    g.lines.push_back(std::move(line));
}

// The guest's line after its own menu save, or "" for the host (its save is
// the group's) and when it is not known who hosts.
std::string GuestNotice(host::Session& session) {
    return session.with_client([](coop::net::Client& c) -> std::string {
        if (c.state() != coop::net::State::connected || c.host_number() == 0 || c.is_host()) {
            return {};
        }
        std::string host_name;
        for (const auto& p : c.players()) {
            if (p.number == c.host_number()) {
                host_name = p.nickname;
            }
        }
        return menus::guest_save_notice(host_name, CoopSave::UseCoopFolder());
    });
}

// The newest write in our save folder, 4 times a second (nothing between, and
// nothing when the host's save was being written into it: that is the
// host's, not ours, and becomes the new mark).
std::optional<std::int64_t> NewestWrite(Glue& g, std::uint64_t now) {
    if (!g.save_due.Due(now)) {
        return std::nullopt;
    }
    const std::string folder = CoopSave::SaveFolder();
    if (folder.empty()) {
        return std::nullopt;
    }
    const std::uint64_t before = CoopSave::SlotsWritten();
    const auto newest = host::save::newest_write(std::filesystem::path(folder));
    const std::uint64_t after = CoopSave::SlotsWritten();
    std::optional<std::int64_t> ticks;
    if (newest) {
        ticks = static_cast<std::int64_t>(newest->time_since_epoch().count());
    }
    if (g.slots_known && before == g.slots_seen && after == before) {
        return ticks;
    }
    // A host's save write started or ended since the last look (or this is
    // the first look): its time is not a menu save. The count kept is the
    // one before the read, so a write that ended during it is looked at again.
    g.slots_known = true;
    g.slots_seen = before;
    if (ticks) {
        g.save_watch.take_mark(*ticks);
    }
    return std::nullopt;
}

} // namespace

bool On() {
    return TheGlue().on;
}

menus::State State() {
    Glue& g = TheGlue();
    return g.on ? g.local.state() : menus::State::playing;
}

bool Paused() {
    return State() == menus::State::paused;
}

std::string TagNote(std::uint8_t status_flags) {
    return TheGlue().on ? menus::tag_note(status_flags) : std::string();
}

std::vector<std::string> TakeLines() {
    return std::exchange(TheGlue().lines, {});
}

void Frame(host::GameMemory* memory, host::Session* session,
           const std::optional<host::PlayerReading>& reading, bool scene_readable) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    if (memory != nullptr) {
        menus::Input in;
        in.now_us = CoopEvery::NowUs();
        in.link = reading.has_value();
        in.scene = scene_readable;
        host::GameFlags flags(*memory);
        in.is_pause = flags.get_bool(menus::kIsPauseFlag);
        in.loading_scene = flags.get_bool(LoadingFlag());
        in.ocarina = reading && reading->animation &&
                     host::activity_of(std::optional<std::string_view>(reading->animation->name)) ==
                         host::Activity::ocarina;
        if (!in.is_pause && in.link && !g.pause_flag_said) {
            g.pause_flag_said = true;
            ORM_LOG_WARNING("coop: menus: Sequence_IsPause ({:#010x}) not readable with our Link in "
                            "the world: the frozen frame tells a pause while it stays so",
                            menus::kIsPauseFlag);
        }
        // The flag, or the frozen frame when the flag is not seen (pause_detect.hpp).
        host::PauseInput pi;
        pi.now_us = in.now_us;
        pi.is_pause = in.is_pause;
        pi.link = in.link;
        if (reading) {
            pi.position = reading->position;
            pi.animation = reading->animation;
            pi.event_running = reading->event_running.value_or(false);
        }
        pi.loading_scene = in.loading_scene.value_or(false);
        pi.ocarina = in.ocarina;
        const bool flag_set = in.is_pause == std::optional<bool>(true);
        auto detected = g.pause->frame(pi);
        for (const auto& line : detected.log) {
            ORM_LOG_INFO("coop: menus: {}", line);
        }
        in.is_pause = detected.is_pause;
        const menus::State before = g.local.state();
        if (const auto now = g.local.frame(in)) {
            // Which detector said so (docs/RIESGOS_RESUELTOS.md): the flag or the frozen frame.
            const std::string by = *now != menus::State::paused ? std::string()
                                   : flag_set                   ? std::string(" (by Sequence_IsPause)")
                                   : detected.frozen            ? std::string(" (by a frozen frame)")
                                                                : std::string();
            ORM_LOG_INFO("coop: menus: {} -> {}{}{}", menus::to_string(before), menus::to_string(*now),
                         *now == menus::State::paused && reading && reading->scene
                             ? " in " + *reading->scene
                             : std::string(),
                         by);
        }
    }
    if (session != nullptr) {
        session->set_menu_status(g.local.status_bits());
    }
}

void After(host::Session* session) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    const std::uint64_t now = CoopEvery::NowUs();
    const bool paused = g.local.state() == menus::State::paused;
    const auto newest = NewestWrite(g, now);
    if (g.save_watch.frame(now, paused, newest)) {
        const std::string notice = session != nullptr ? GuestNotice(*session) : std::string();
        ORM_LOG_INFO("coop: menus: menu save seen in {} ({})", CoopSave::SaveFolder(),
                     notice.empty() ? "we host or play alone: no notice" : "guest: told");
        AddLine(g, notice);
    }

    if (session == nullptr) {
        g.mates.clear();
        return;
    }
    std::vector<menus::Seen> seen;
    session->with_presence([&](coop::presence::Presence& p) {
        for (const std::uint8_t n : p.players()) {
            const auto* player = p.player(n);
            if (player == nullptr) {
                continue;
            }
            menus::Seen s;
            s.number = n;
            s.name = player->nickname();
            s.status_flags = player->flags();
            s.in_world = player->scene().has_value();
            seen.push_back(std::move(s));
        }
    });
    for (auto& c : g.mates.frame(seen)) {
        std::string name;
        for (const auto& s : seen) {
            if (s.number == c.number) {
                name = s.name;
            }
        }
        ORM_LOG_INFO("coop: menus: P{} '{}' {} (was {})", c.number, name, menus::to_string(c.to),
                     menus::to_string(c.from));
        AddLine(g, std::move(c.notice));
    }
}

} // namespace CoopMenus
