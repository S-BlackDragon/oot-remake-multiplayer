// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "the room's age", the emulator side (coop_age_game.h). Glue only: the
// rules are in coop/host/age.hpp.

#include "coop_age_game.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/age.hpp"
#include "coop/host/final_rules.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/session.hpp"
#include "coop/presence/presence.hpp"
#include "coop_boss.h"
#include "coop_downed_game.h"
#include "coop_notices.h"
#include "coop_every.h"
#include "coop_switch.h"
#include "coop_time_temple.h"
#include "orm_story_events.h"
#include "platform.h"

namespace CoopAge {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

struct Glue {
    std::once_flag checked;
    bool on = false;
    bool used = false;  // on, or a test change asked: the machine runs
    std::optional<host::AgeFollow> follow;
    std::mutex lines_mutex;
    std::vector<std::string> lines;  // chat lines (TakeLines), under lines_mutex
    bool told_waits = false;         // our PLAYER_STATUS age_waits, as last set
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        // LICHII_ORM_AGE (LICHII_COOP_AGE before test9), off by default.
        glue.on = CoopSwitch::On("LICHII_ORM_AGE", "LICHII_COOP_AGE", false);
        // Empty: the game's own era change (coop/host/age.hpp era_change_event). The old default,
        // ReplaceEquipmentForEraChange, is a sub flow the game never starts by name (no EventSetting
        // row: refused every time), so it is taken as no name.
        std::string event;
        if (const auto e = CoopSwitch::Value("LICHII_ORM_AGE_EVENT", "LICHII_COOP_AGE_EVENT");
            e && OrmStoryEvents::ValidName(*e) && *e != "ReplaceEquipmentForEraChange") {
            event = *e;
        }
        glue.follow.emplace(host::AgeSettings{}, event);
        glue.used = glue.on;
        if (glue.on && event.empty()) {
            ORM_LOG_INFO("coop: age: on (LICHII_ORM_AGE): a mate's age change is followed by the game's own era "
                         "change ('{}' / '{}': the equipment swap, the era, the Temple of Time)",
                         host::era_change_event(host::Age::adult), host::era_change_event(host::Age::child));
        } else if (glue.on) {
            ORM_LOG_INFO("coop: age: on (LICHII_ORM_AGE): a mate's age change is followed (the reload asks the "
                         "event '{}' and reloads our scene in place; the era is written once it runs)",
                         event);
        }
    });
    return glue;
}

} // namespace

bool On() {
    return TheGlue().on;
}

void Change(bool child) {
    Glue& g = TheGlue();
    g.used = true;
    g.follow->change(child ? host::Age::child : host::Age::adult, "the test command");
}

std::vector<std::string> TakeLines() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.lines_mutex);
    std::vector<std::string> out;
    out.swap(g.lines);
    return out;
}

bool HoldsDown() {
    Glue& g = TheGlue();
    return g.used && g.follow->reloading_downed();
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading) {
    Glue& g = TheGlue();
    if (!g.used || main_base == 0) {
        return;
    }
    host::AgeInput in;
    in.now_us = CoopEvery::NowUs();
    if (reading) {
        in.age = reading->age;
        in.scene = reading->scene;
        in.position = reading->position;
        in.event_running = reading->event_running;
    }
    in.downed = CoopDowned::Down();
    // ORM D-R62: lying down (no revive, no game over running) reloads all the same, down.
    in.lying = CoopDowned::State() == host::DownedState::downed;
    CoopPlatform::GuestMemory& memory = platform.Memory();  // main_base() == main_base
    // ORM, the endgame (coop/host/final_rules.hpp, oot-remake docs/FINAL.md):
    // the credits load Link as a child. That change is the ending's, not the
    // room's: none is followed while our own ending event runs or once Ganon
    // is sealed (Step_Defeat_Ganon Complete, a shared story step).
    {
        std::optional<std::string> event;
        if (reading && reading->event_running.value_or(false)) {
            event = host::PlayerReader(memory).read_event_name();
        }
        in.held = host::ending_holds_age(
            event, host::GameFlags(memory).get_enum(host::flag_hash("Step_Defeat_Ganon")));
    }
    // ORM, the Temple of Time (coop_time_temple.h, oot-remake docs/TEMPLO_TIEMPO.md):
    // while our game runs the sword's story (or is asked for it on a mate's
    // word, or stands in the Chamber of Sages, or the story's parts still
    // follow one another) no reload begins: our own cutscene changes our age.
    // A mate's change is still seen, so one that is not ours when the story is
    // over (a story that did not run to its end here, a blue warp's Chamber)
    // is followed then (AgeInput::wait); a reload under way runs to its end.
    if (auto temple = CoopTimeTemple::HoldsAge()) {
        in.wait = "the Temple of Time: " + *temple;
    }
    // ORM (2026-11-13, docs/EDAD.md): never moved to the Temple of Time
    // in a boss room's lock; the sword's story on a mate's word is his change.
    in.boss_fight = CoopBoss::Fighting();
    in.story_follow = CoopTimeTemple::AskedOnMatesWord();
    if (session != nullptr && g.on) {
        session->with_presence([&](coop::presence::Presence& p) {
            for (const std::uint8_t n : p.players()) {
                const auto* player = p.player(n);
                if (player == nullptr || n > proto::kMaxPlayers) {
                    continue;
                }
                in.mates.push_back(
                    host::AgeMate{n, player->age(), player->scene(), player->flags(), player->nickname()});
            }
        });
    }
    const host::AgeOutput out = g.follow->tick(in);
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: age: {}", line);
    }
    // ORM (2026-11-13): the big centred notice (the newest shown), and a
    // chat line for the record when the notice asks for one.
    for (const auto& n : out.notices) {
        ORM_LOG_INFO("coop: age: notice {}: {}{}{}", host::notice_kind_name(n.kind), n.text,
                     n.detail.empty() ? "" : " / ", n.detail);
        CoopNotices::TheBanner().Show(n.number, n.name, n.text, n.detail, in.now_us / 1000, n.show_us / 1000);
        if (n.chat) {
            std::lock_guard lock(g.lines_mutex);
            g.lines.push_back(n.detail.empty() ? n.text : n.text + " (" + n.detail + ")");
            if (g.lines.size() > 16) {
                g.lines.erase(g.lines.begin());
            }
        }
    }
    // Our move waits because it is not safe here: the others name us.
    if (session != nullptr) {
        const bool waits = g.follow->waits_unsafe();
        session->set_age_waits(waits);
        if (waits != g.told_waits) {
            g.told_waits = waits;
            ORM_LOG_INFO("coop: age: our PLAYER_STATUS {} age_waits (bit 6)", waits ? "carries" : "drops");
        }
    }
    if (out.set_era) {
        const bool child = *out.set_era == host::Age::child;
        const bool ok = host::GameFlags::set_bool(memory, host::flag_hash("Sequence_IsChildhood"), child);
        ORM_LOG_INFO("coop: age: Sequence_IsChildhood <- {} {}", child ? 1 : 0, ok ? "(queued)" : "NOT queued");
    }
    if (out.ask_event) {
        std::uint64_t actor = host::PlayerReader(memory).find_player_actor().value_or(0);
        ORM_LOG_INFO("coop: age: asking the event '{}'", *out.ask_event);
        OrmStoryEvents::Ask(main_base, actor, *out.ask_event);
    }
    if (out.cancel_scene) {
        platform.AskSceneChange("", "");
    }
    if (out.ask_scene) {
        const bool ok = platform.AskSceneChange(*out.ask_scene, "");
        ORM_LOG_INFO("coop: age: scene change to {} {}", *out.ask_scene, ok ? "asked" : "NOT asked");
    }
    if (out.move_to) {
        const auto& p = *out.move_to;
        const bool ok = platform.MoveLink(p[0], p[1], p[2]);
        ORM_LOG_INFO("coop: age: Link put back at {:.2f} {:.2f} {:.2f}: {}", p[0], p[1], p[2],
                     ok ? "done" : "failed");
    }
}

} // namespace CoopAge
