// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "the Temple of Time with several players", the glue (coop_time_temple.h);
// the rules are coop/host/time_temple.hpp.

#include "coop_time_temple.h"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "platform_env.h"  // Env, ORM_LOG_*
#include "coop/host/time_temple.hpp"
#include "coop_switch.h"
#include "orm_story_events.h"

namespace CoopTimeTemple {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

std::optional<std::string> Env(const char* name) {
    auto v = CoopPlatform::Env(name);
    if (!v || v->empty()) {
        return std::nullopt;
    }
    return v;
}

struct Glue {
    std::once_flag checked;
    bool on = false;
    std::optional<host::TimeTemple> rules;  // present thread only
    bool list_checked = false;              // against the story list, at the first frame
    std::optional<std::string> held;        // the reason last logged (present thread)
    std::mutex hold_mutex;
    std::optional<std::string> hold;        // HoldsAge's answer, under hold_mutex
    std::atomic<bool> asked_travel{false};  // AskedOnMatesWord's answer
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        if (!OrmStoryEvents::On()) {
            return;  // no story cutscenes: no starts to filter, nothing of ours runs on a mate's word
        }
        if (!CoopSwitch::On("LICHII_ORM_TIME", true)) {
            ORM_LOG_INFO("coop: temple: off (LICHII_ORM_TIME=0)");
            return;
        }
        host::TimeTempleList list = host::TimeTempleList::defaults();
        std::string from = "built in";
        if (auto path = Env("LICHII_ORM_TIME_LIST")) {
            std::string why;
            auto file = host::TimeTempleList::load(*path, &why);
            if (!file) {
                ORM_LOG_ERROR("coop: temple: LICHII_ORM_TIME_LIST {}: {}; the Temple of Time rules are off",
                              *path, why);
                return;
            }
            list = std::move(*file);
            from = *path;
        }
        ORM_LOG_INFO(
                 "coop: temple: on ({}): {} Temple of Time events, {} temple areas, {} scenes with no age "
                 "follow, {} more shared flags",
                 from, list.entries().size(), list.areas().size(), list.noage_scenes().size(),
                 list.flag_names().size());
        glue.rules.emplace(std::move(list));
        glue.on = true;
    });
    return glue;
}

} // namespace

bool On() {
    return TheGlue().on;
}

void AddShared(std::optional<host::SharedFlags>& shared) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    std::vector<host::FlagKey> keys;
    host::EnumFlags enums;
    if (shared) {
        keys = shared->keys();
        enums = shared->enums();
    }
    const std::size_t before = shared ? shared->size() : 0;
    const auto ours = g.rules->list().shared_flags();
    keys.insert(keys.end(), ours.begin(), ours.end());
    shared = host::SharedFlags(std::move(keys), std::move(enums));  // sorts, drops repeats
    std::string names;
    for (const auto& n : g.rules->list().flag_names()) {
        names += (names.empty() ? "" : ", ") + n;
    }
    ORM_LOG_INFO("coop: temple: sharing {} flags ({} new): {}", ours.size(), shared->size() - before, names);
}

void Filter(proto::StoryEvent& m, const std::optional<host::PlayerReading>& reading) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    host::TimeWhere where;
    if (reading) {
        where.scene = reading->scene;
        where.position = reading->position;
    }
    if (auto line = g.rules->filter(m, where)) {
        ORM_LOG_INFO("coop: temple: {}", *line);
    }
}

void Frame(const host::Story& story, const host::StoryInput& in) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    if (!g.list_checked) {
        g.list_checked = true;
        const auto problems = host::check_time_list(g.rules->list(), story.list());
        for (const auto& line : problems) {
            ORM_LOG_WARNING("coop: temple: list: {}", line);
        }
        if (problems.empty()) {
            ORM_LOG_INFO("coop: temple: list: agrees with the story list and the shared items");
        }
    }
    host::TimeInput t;
    t.now_us = in.now_us;
    t.scene = in.scene;
    t.event_running = in.event_running;
    t.event_name = in.event_name;
    t.story_asking = story.asking();
    {
        const host::TimeEntry* asked = t.story_asking ? g.rules->list().find(*t.story_asking) : nullptr;
        g.asked_travel.store(asked != nullptr && asked->rule == host::TimeRule::travel, std::memory_order_relaxed);
    }
    auto hold = g.rules->tick(t);
    if (hold != g.held) {
        if (hold) {
            ORM_LOG_INFO("coop: temple: the room's age waits: {}", *hold);
        } else {
            ORM_LOG_INFO("coop: temple: the room's age is followed again");
        }
        g.held = hold;
    }
    std::lock_guard lock(g.hold_mutex);
    g.hold = std::move(hold);
}

bool AskedOnMatesWord() {
    Glue& g = TheGlue();
    return g.on && g.asked_travel.load(std::memory_order_relaxed);
}

std::optional<std::string> HoldsAge() {
    Glue& g = TheGlue();
    if (!g.on) {
        return std::nullopt;
    }
    std::lock_guard lock(g.hold_mutex);
    return g.hold;
}

} // namespace CoopTimeTemple
