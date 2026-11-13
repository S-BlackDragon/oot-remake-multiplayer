// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM songs with several players, the hook's side (coop_songs_game.h).

#include "coop_songs_game.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string_view>
#include <utility>

#include "coop/host/downed.hpp"
#include "coop/host/live_check.hpp"
#include "coop/host/session.hpp"
#include "coop/host/songs.hpp"
#include "coop/presence/presence.hpp"
#include "coop_downed_game.h"
#include "coop_objects.h"
#include "coop_puppet.h"
#include "coop_teleport_game.h"
#include "coop_switch.h"
#include "orm_quick_chat.h"
#include "coop_testinput.h"  // the pad bits (kL, kR, ...)
#include "platform.h"        // Platform, Env, ORM_LOG_*

namespace CoopSongs {
namespace {

namespace host = coop::host;

constexpr std::uint64_t kEveryUs = 100'000;  // the invitation needs no more than 10 a second

std::uint64_t SteadyUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

struct Glue {
    std::once_flag checked;
    bool on = false;
    std::string button_name = "L";
    std::uint64_t button = CoopTestInput::kL;
    std::optional<host::WarpInvite> invite;
    std::optional<host::InviteButton> button_check;  // is the button the game's too?
    std::uint64_t last_us = 0;
    bool arriving = false;  // our own warp song's arrival event runs
    std::mutex mutex;
    std::vector<std::string> lines;  // present thread -> the chat box
};

// The button that makes the gesture with `name` when the game uses `name` itself: its twin
// on the other side of the pad (play never holds both shoulders for 0.75 s).
std::string_view Partner(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kTwins{{
        {"L", "R"},
        {"R", "L"},
        {"ZL", "ZR"},
        {"ZR", "ZL"},
        {"PLUS", "MINUS"},
        {"MINUS", "PLUS"},
        {"LCLICK", "RCLICK"},
        {"RCLICK", "LCLICK"},
    }};
    for (const auto& [n, twin] : kTwins) {
        if (n == name) {
            return twin;
        }
    }
    return "R";
}

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        glue.on = CoopSwitch::On("LICHII_COOP_WARP_INVITE", false);  // coop_switch.h: 1/true/yes/on
        if (const auto b = CoopPlatform::Env("LICHII_COOP_WARP_INVITE_BUTTON"); b && !b->empty()) {
            if (const auto bit = ButtonBit(*b)) {
                glue.button = *bit;
                glue.button_name = *b;
            } else {
                ORM_LOG_WARNING("coop: songs: LICHII_COOP_WARP_INVITE_BUTTON={} is not a button; L is used", *b);
            }
        }
        glue.invite.emplace(glue.button_name);
        // The fallback when the game uses the button (InviteButton's check line):
        // LICHII_COOP_WARP_INVITE_FALLBACK, else its twin on the other side of the pad.
        std::string partner(Partner(glue.button_name));
        if (const auto f = CoopPlatform::Env("LICHII_COOP_WARP_INVITE_FALLBACK"); f && !f->empty()) {
            if (ButtonBit(*f) && *f != glue.button_name) {
                partner = *f;
            } else {
                ORM_LOG_WARNING("coop: songs: LICHII_COOP_WARP_INVITE_FALLBACK={} is not another button; {} is used",
                                *f, partner);
            }
        }
        glue.button_check.emplace(glue.button_name, glue.button, partner,
                                  ButtonBit(partner).value_or(CoopTestInput::kR));
        ORM_LOG_INFO("coop: songs: warp-song invitation {}",
                     glue.on ? "on (LICHII_COOP_WARP_INVITE=1): hold " + glue.button_name +
                                   " to follow a mate who leaves by a warp song"
                             : std::string("off (LICHII_COOP_WARP_INVITE=1 turns it on)"));
    });
    return glue;
}

} // namespace

std::optional<std::uint64_t> ButtonBit(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::uint64_t>, 8> kNames{{
        {"L", CoopTestInput::kL},
        {"R", CoopTestInput::kR},
        {"ZL", CoopTestInput::kZL},
        {"ZR", CoopTestInput::kZR},
        {"PLUS", CoopTestInput::kPlus},
        {"MINUS", CoopTestInput::kMinus},
        {"LCLICK", CoopTestInput::kLClick},
        {"RCLICK", CoopTestInput::kRClick},
    }};
    for (const auto& [n, bit] : kNames) {
        if (n == name) {
            return bit;
        }
    }
    return std::nullopt;
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<host::PlayerReading>& reading, const std::optional<std::string>& event_name,
           host::Session* session, std::uint64_t server_now_us) {
    Glue& g = TheGlue();
    if (!g.on) {
        return;
    }
    // Our own arrival by a warp song: the line the warper's half of a test looks for. The boot
    // event Dm_SY_Ocarina's OcarinaWarp was read to set, or the event the remake runs on arrival
    // (seen in a game, docs/SONDAS.md round 4: Dm_SY_Ocarina_move, then Dm_SY_Ocarina_move_Ed
    // playing DemoOcarinamove_ed in the new scene). The line names the one that ran.
    const bool arriving = main_base != 0 && event_name &&
                          (*event_name == "OcarinaWarp_Arrive" || *event_name == "Dm_SY_Ocarina_move_Ed");
    if (arriving && !g.arriving) {
        ORM_LOG_INFO("coop: songs: our Link arrives by a warp song in {} at {:.1f} {:.1f} {:.1f} (event {})",
                     reading && reading->scene ? *reading->scene : std::string("?"),
                     reading ? reading->position[0] : 0.0f, reading ? reading->position[1] : 0.0f,
                     reading ? reading->position[2] : 0.0f, *event_name);
    }
    g.arriving = arriving;

    const std::uint64_t now = SteadyUs();
    // Every frame: is the button the game's too? (CANCIONES.md "Por ver" 4; a press is 0.3 s.)
    const bool standing = main_base != 0 && reading && !reading->event_running.value_or(true) &&
                          CoopDowned::State() == host::DownedState::alive;
    {
        host::InviteButton::Input bi;
        bi.now_us = now;
        if (main_base != 0) {
            bi.buttons = platform.PadButtons();  // player 1's pad or the handheld, scripted input included
        }
        if (reading && reading->animation) {
            bi.animation = reading->animation->name;
        }
        bi.standing = standing;
        bi.invitation_open = g.invite->open() != 0;
        for (const auto& line : g.button_check->frame(bi)) {
            ORM_LOG_INFO("coop: songs: {}", line);
        }
        if (g.button_check->decided() && g.invite->button() != g.button_check->label()) {
            g.invite->set_button(g.button_check->label());
        }
    }
    if (now - g.last_us < kEveryUs) {
        return;
    }
    g.last_us = now;

    host::InviteInput in;
    in.now_us = now;
    if (main_base != 0 && reading) {
        in.scene = reading->scene;
        in.standing = standing;
    }
    if (main_base != 0) {
        // The button, or with its fallback when the game uses it; never across a quick message
        // (L + d-pad) when the quick messages share it (coop/host/shoulder.hpp).
        in.held = OrmQuickChat::InviteHeld(platform, g.button_check->bits());
    }
    if (session != nullptr && server_now_us != 0) {
        for (const auto& r : CoopPuppet::Collect(session, server_now_us)) {
            host::InviteMate m;
            m.number = r.number;
            m.scene = r.scene;
            m.position = r.position;
            if (r.anim) {
                m.anim = r.anim->name;  // with the puppets on (LICHII_COOP_PUPPET)
            }
            in.mates.push_back(std::move(m));
        }
        session->with_presence([&](coop::presence::Presence& p) {
            in.room = p.players();  // a mate with no pose for a moment is still in (REVISION_NUEVAS)
            for (auto& m : in.mates) {
                if (const auto* player = p.player(m.number)) {
                    m.name = player->nickname();
                }
            }
        });
    }

    host::InviteOutput out = g.invite->tick(in);
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: songs: {}", line);
    }
    if (out.follow) {
        CoopTeleport::Follow(*out.follow, out.follow_name);
    }
    if (!out.notices.empty()) {
        std::lock_guard lock(g.mutex);
        for (auto& n : out.notices) {
            ORM_LOG_INFO("coop: notice: {}", n);
            g.lines.push_back(std::move(n));
        }
    }
}

void Blocks(host::FlagStore& store, const std::optional<std::string>& scene,
            const std::optional<host::PlayerReading>& reading) {
    struct Live {
        bool on = CoopSwitch::On("LICHII_ORM_LIVE_CHECK", true);
        host::LiveCheck check{host::song_block_watches()};
        std::uint64_t last_us = 0;
        Live() {
            ORM_LOG_INFO("coop: live: {}",
                         on ? "on: whether the Song of Time blocks answer their keys live is logged "
                              "(LICHII_ORM_LIVE_CHECK=0 off)"
                            : std::string("off (LICHII_ORM_LIVE_CHECK=0)"));
        }
    };
    static Live live;
    if (!live.on) {
        return;
    }
    const std::uint64_t now = SteadyUs();
    if (now - live.last_us < 250'000) {
        return;
    }
    live.last_us = now;
    host::LiveView view;
    view.scene = scene;
    view.actors = CoopObjects::WalkActors(scene);
    if (reading && reading->animation) {
        view.animation = reading->animation->name;
    }
    view.now_us = now;
    live.check.tick(view, store);
    for (const auto& line : live.check.take_notes()) {
        ORM_LOG_INFO("coop: live: {}", line);
    }
}

std::vector<std::string> TakeLines() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    return std::exchange(g.lines, {});
}

} // namespace CoopSongs
