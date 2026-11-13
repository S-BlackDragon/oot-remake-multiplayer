// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM story cutscenes, the emulator side (orm_story_events.h): the run's requester
// and its svc cave caller, and (P2, LICHII_ORM_STORY_EVENTS=1) the glue of the
// shared story events: the rules are coop/host/story.hpp.

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/session.hpp"
#include "coop/host/story.hpp"
#include "coop/overlay/overlay.hpp"
#include "coop/net/client.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_downed_game.h"
#include "coop_every.h"
#include "coop_gerudo.h"
#include "orm_story_events.h"
#include "coop_switch.h"
#include "coop_time_temple.h"
#include "platform.h"

namespace OrmStoryEvents {
namespace {

// After the boss rooms (50): docs/CINEMATICAS.md 3.5. The skipper right after.
constexpr int kCallerOrder = 55;
constexpr int kSkipCallerOrder = 56;

template <typename T>
void Flush(T& r) {
    for (const auto& line : r.TakeLog()) {
        ORM_LOG_INFO("coop: story: {}", line);
    }
}

Skipper& TheSkipper() {
    static Skipper skipper;  // kept for the whole run: the cave holds it
    static std::once_flag added;
    std::call_once(added, [] {
        CoopPlatform::AddCaller(kSkipCallerOrder, [](CoopPuppet::Guest& guest, bool after_call,
                                                std::uint64_t, std::uint64_t) {
            auto call = skipper.Step(guest, after_call);
            Flush(skipper);
            return call;
        });
    });
    return skipper;
}

Requester& TheRequester() {
    static Requester requester;  // kept for the whole run: the cave holds it
    static std::once_flag added;
    std::call_once(added, [] {
        CoopPlatform::AddCaller(kCallerOrder, [](CoopPuppet::Guest& guest, bool after_call,
                                            std::uint64_t result, std::uint64_t) {
            auto call = requester.Step(guest, after_call, result);
            Flush(requester);
            return call;
        });
    });
    return requester;
}

namespace host = coop::host;
namespace proto = coop::protocol;

const char* KindName(proto::StoryKind kind) {
    switch (kind) {
    case proto::StoryKind::start:
        return "start";
    case proto::StoryKind::end:
        return "end";
    case proto::StoryKind::played:
        return "played";
    case proto::StoryKind::skipped:
        return "skipped";
    }
    return "?";
}

std::optional<std::string> Env(const char* name) {
    return CoopSwitch::Value(name);  // coop_switch.h: unset or empty is nothing
}

struct Glue {
    std::once_flag checked;
    bool on = false;
    std::optional<host::Story> story;  // present thread only
    std::mutex inbox_mutex;
    std::vector<proto::StoryEvent> inbox;  // network thread to present
    std::optional<std::string> noted_skip;  // NoteSkip, any thread to present (under inbox_mutex)
    // The list's flags, read every kReadEveryUs (the whole game's list has a few
    // hundred: not every frame); kept between readings.
    std::uint64_t read_at_us = 0;
    std::vector<std::optional<bool>> played;
    std::vector<std::optional<std::uint32_t>> flags;
};

constexpr std::uint64_t kReadEveryUs = 100'000;

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        const auto events = Env("LICHII_ORM_STORY_EVENTS");
        if (events) {
            if (const auto line = CoopSwitch::Complaint("LICHII_ORM_STORY_EVENTS", *events, false)) {
                ORM_LOG_WARNING("{}", *line);
            }
        }
        if (!SwitchOn(events, Env("LICHII_COOP_STORY"))) {
            return;
        }
        host::StoryList list = host::StoryList::defaults();
        std::string from = "built in";
        auto path = CoopSwitch::Value("LICHII_ORM_STORY_LIST", "LICHII_COOP_STORY_LIST");  // the name before test9
        if (path) {
            std::string why;
            auto file = host::StoryList::load(*path, &why);
            if (!file) {
                ORM_LOG_ERROR("coop: story: LICHII_ORM_STORY_LIST {}: {}; story events are not shared",
                              *path, why);
                return;
            }
            list = std::move(*file);
            from = *path;
        }
        std::string names;
        for (const auto& e : list.entries()) {
            names += (names.empty() ? "" : ", ") + e.name;
        }
        ORM_LOG_INFO("coop: story: on (LICHII_ORM_STORY_EVENTS): {} shared story events ({}): {}", list.size(),
                     from, names);
        glue.story.emplace(std::move(list));
        glue.on = true;
    });
    return glue;
}

} // namespace

void Ask(std::uint64_t main, std::uint64_t actor, const std::string& name) {
    Requester& r = TheRequester();
    r.Ask(main, actor, name);
    Flush(r);
}

void Skip(std::uint64_t main, const std::string& why) {
    Skipper& s = TheSkipper();
    s.Want(main, why);
    Flush(s);
}

void NoteSkip(const std::string& how) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    std::lock_guard lock(g.inbox_mutex);
    g.noted_skip = how;
}

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
    auto story = g.story->list().shared_flags();
    // The story steps are Enum flags: opted in, as steps (only forward).
    const host::EnumFlags steps = g.story->list().step_flags();
    host::EnumFlags merged = enums;
    if (merged.merge(steps)) {
        enums = std::move(merged);
    } else {
        ORM_LOG_ERROR("coop: story: a story step is also an enum: line of LICHII_COOP_FLAGS with other "
                      "values; the story steps are not shared");
        const auto step_keys = steps.keys();
        std::erase_if(story, [&](const host::FlagKey& k) {
            return std::find(step_keys.begin(), step_keys.end(), k) != step_keys.end() &&
                   !(shared && std::binary_search(shared->keys().begin(), shared->keys().end(), k));
        });
    }
    keys.insert(keys.end(), story.begin(), story.end());
    shared = host::SharedFlags(std::move(keys), std::move(enums));  // sorts, drops repeats
    ORM_LOG_INFO(
                 "coop: story: sharing {} flags of the story events ({} new): HavePlayedEvent, end-state Bools and "
                 "{} story steps",
                 story.size(), shared->size() - before, steps.size());
    CoopTimeTemple::AddShared(shared);  // ORM Temple of Time: its more flags (docs/TEMPLO_TIEMPO.md)
}

void HostReset() {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    g.story->host_reset();
    // The flags read last (up to kReadEveryUs ago) are the old save's: read
    // the loaded one before the next tick looks at them (REVISION_GANCHO F1).
    g.played.clear();
    g.flags.clear();
    g.read_at_us = 0;
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
    ORM_LOG_INFO("coop: story: STORY_EVENT {} from P{} ('{}' in {}, at {:.2f} {:.2f} {:.2f}, server time {} ms){}",
                 KindName(m->kind), m->player_number, m->name, m->scene, m->position[0], m->position[1],
                 m->position[2], m->server_time_ms, g.on ? "" : ", ignored: LICHII_ORM_STORY_EVENTS is off");
    if (!g.on) {
        return;
    }
    std::lock_guard lock(g.inbox_mutex);
    if (g.inbox.size() < 64) {
        g.inbox.push_back(*m);
    }
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading,
           const std::optional<std::string>& event_name, std::uint64_t server_now_us,
           const std::vector<coop::overlay::Shown>* others) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    std::vector<proto::StoryEvent> inbox;
    std::optional<std::string> noted_skip;
    {
        std::lock_guard lock(g.inbox_mutex);
        inbox.swap(g.inbox);
        noted_skip.swap(g.noted_skip);
    }
    if (noted_skip) {
        g.story->note_skip(*noted_skip);
    }
    for (auto& m : inbox) {
        // ORM: a start in a scene for both ages (the Spirit Temple) is the
        // starter's age's (coop_gerudo.h, GERUDO_ESPIRITU.md).
        if (!CoopGerudo::StoryStartForUs(session, reading, m)) {
            continue;
        }
        // ORM Temple of Time (docs/TEMPLO_TIEMPO.md): a temple start out of the temple becomes `played`.
        CoopTimeTemple::Filter(m, main_base != 0 ? reading : std::nullopt);
        g.story->receive(m);
    }

    host::StoryInput in;
    in.now_us = CoopEvery::NowUs();
    in.server_now_us = server_now_us;
    if (main_base != 0 && reading) {
        in.scene = reading->scene;
        in.position = reading->position;
        in.event_running = reading->event_running;
        in.event_name = event_name;
        in.pad = platform.PadButtons();  // the owner's skip press (PLUS, MINUS), D-048
    }
    in.downed = CoopDowned::Down();
    if (others != nullptr) {  // near= events: where each starter stands now
        for (const auto& o : *others) {
            if (o.fade <= 0.0f) {
                continue;  // gone: no samples
            }
            in.mates.push_back(host::StoryInput::Mate{o.number, o.scene, o.position});
        }
    }
    if (main_base != 0) {
        if (g.played.empty() || in.now_us - g.read_at_us >= kReadEveryUs) {
            CoopPlatform::GuestMemory& memory = platform.Memory();  // main_base() == main_base
            g.played = host::read_story_played(memory, g.story->list());
            g.flags = host::read_story_flags(memory, g.story->list());
            g.read_at_us = in.now_us;
        }
        in.played = g.played;
        in.flags = g.flags;
    } else {
        g.played.clear();
        g.flags.clear();
    }
    host::StoryOutput out = g.story->tick(in);
    CoopTimeTemple::Frame(*g.story, in);  // ORM Temple of Time: the room's age waits during the sword's story
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: story: {}", line);
    }
    for (const auto& m : out.send) {
        const bool sent = session != nullptr &&
                          session->with_client([&](coop::net::Client& c) { return c.send(m); });
        ORM_LOG_INFO("coop: story: STORY_EVENT {} {}('{}' in {}, Link at {:.2f} {:.2f} {:.2f})",
                     KindName(m.kind), sent ? "sent " : "NOT sent (not connected) ", m.name, m.scene,
                     m.position[0], m.position[1], m.position[2]);
    }
    if (out.skip) {  // our copy of a cutscene its owner skipped (D-048)
        Skip(main_base, "its owner skipped '" + g.story->skipping().value_or("?") + "'");
    }
    if (out.ask) {
        std::uint64_t actor = 0;
        if (main_base != 0) {
            actor = host::PlayerReader(platform.Memory()).find_player_actor().value_or(0);
        }
        Ask(main_base, actor, *out.ask);
    }
}

} // namespace OrmStoryEvents
