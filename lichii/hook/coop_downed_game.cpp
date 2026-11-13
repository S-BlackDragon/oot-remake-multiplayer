// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "downed, not dead", the emulator side (coop_downed_game.h). Glue only:
// the rules are in coop/host/downed.hpp.

#include "coop_downed_game.h"
#include "coop_switch.h"
#include "coop_continue.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/game_memory.hpp"
#include "coop/host/mate_down_sound.hpp"
#include "coop/host/menus.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop/presence/presence.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_age_game.h"
#include "coop_downed.h"
#include "coop_every.h"
#include "orm_ocarina_sound.h"
#include "platform.h"

namespace CoopDowned {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

struct Glue {
    std::once_flag checked;
    bool on = false;
    bool manual = false;  // present thread only
    host::Downed downed;
    host::ReviveWatch watch;
    host::MateDownSound sound;  // ORM: a mate's fall heard (LICHII_ORM_MATE_DOWN_SOUND)
    bool status_sent = false;  // the dead bit last given to the session
    bool held_logged = false;  // present thread: a mate's hold beside us, as last logged
    std::mutex inbox_mutex;
    std::vector<std::uint8_t> inbox;  // REVIVE senders, network thread to present
    struct Progress {
        std::uint8_t by = 0;
        std::uint8_t percent = 0;
    };
    std::vector<Progress> progress;   // REVIVE progress, same
    std::uint8_t defeat = 0;          // GROUP_DEFEAT players down, same; 0: none
    std::mutex banner_mutex;
    std::string banner;  // the line on the screen (Banner), present thread to GUI
    std::string banner_logged;  // present thread: the last line logged, digits left out
    bool group_defeat = false;  // present thread: our game over is the group's (banner)
    std::vector<std::string> lines;  // present thread: chat notices (TakeLines)
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        glue.on = CoopSwitch::On("LICHII_COOP_DOWNED", false);  // coop_switch.h
#ifdef COOP_HOOKSHOT
        // ORM test10: Hookshot applies no code patch, so our game over gate (the
        // cave on main+0x4ae6bc, coop_downed.h) is not there; a Hookshot built with
        // the gate's ENTRY_HOOK (oot-remake docs/PLATAFORMA_HOOKSHOT.md, "Caído en
        // Hookshot") says so with LICHII_ORM_HOOKSHOT_GATE=1. Without it a dead Link
        // would get the normal game over while we showed him downed: off.
        if (glue.on && !CoopSwitch::On("LICHII_ORM_HOOKSHOT_GATE", false)) {
            glue.on = false;
            ORM_LOG_WARNING("coop: check: downed fallback off on Hookshot: no game over gate in this build "
                            "(needs the ENTRY_HOOK at main+0x4aa890 and LICHII_ORM_HOOKSHOT_GATE=1): "
                            "the normal game over");
        }
#endif
        if (glue.on) {
            ORM_LOG_INFO("coop: downed: on (LICHII_COOP_DOWNED): a dead Link stays down until a "
                         "mate holds A beside him");
        }
    });
    return glue;
}

// Player 1's pad, or the handheld one, as coop_testinput plays into it: the
// buttons include the scripted ones. Nothing when no pad is connected.
std::optional<bool> AHeld(CoopPlatform::Platform& platform) {
    const auto buttons = platform.PadButtons();
    if (!buttons) {
        return std::nullopt;
    }
    return (*buttons & CoopPlatform::kButtonA) != 0;
}

std::string BottleText(const host::Bottle& b) {
    for (const char* name : {"Empty", "Fairy"}) {
        if (host::bottle_value(name) == b.item) {
            return fmt::format("{}, IsValid {}, StockNum {}", name, b.valid ? 1 : 0, b.stock);
        }
    }
    return fmt::format("{:#x}, IsValid {}, StockNum {}", b.item, b.valid ? 1 : 0, b.stock);
}

void WriteGate(CoopPlatform::GuestMemory& memory, std::uint64_t main_base, bool closed) {
    const std::uint64_t at = main_base + kGateOffset;
    const std::uint32_t want = closed ? 1u : 0u;
    const auto now = host::read_u32(memory, at);
    if (now && *now == want) {
        return;
    }
    std::array<std::uint8_t, 4> bytes{};
    std::memcpy(bytes.data(), &want, 4);
    if (!memory.write(at, bytes)) {
        ORM_LOG_ERROR("coop: downed: gate word main+{:#x} NOT written", kGateOffset);
        return;
    }
    ORM_LOG_INFO("coop: downed: gate {}", closed ? "shut" : "open");
}

// Who else is in the session, with their last sample's place, and their
// names by number (for the screen).
std::vector<host::Mate> Mates(host::Session& session,
                              std::array<std::string, proto::kMaxPlayers + 1>& names) {
    std::vector<host::Mate> mates;
    session.with_presence([&](coop::presence::Presence& p) {
        for (const std::uint8_t n : p.players()) {
            const auto* player = p.player(n);
            if (player == nullptr || n > proto::kMaxPlayers) {
                continue;
            }
            // ORM menus (MENUS_PAUSA.md M6): a mate on the title screen is not
            // in the world: no reviver, not one the group defeat waits for.
            if (host::menus::at_title(player->flags())) {
                continue;
            }
            host::Mate m;
            m.number = n;
            m.scene = player->scene();
            m.status = player->flags();
            if (!player->samples().empty()) {
                m.position = player->samples().back().position;
            }
            names[n] = player->nickname();
            mates.push_back(std::move(m));
        }
    });
    return mates;
}

std::string NameOf(const std::array<std::string, proto::kMaxPlayers + 1>& names, std::uint8_t n) {
    if (n <= proto::kMaxPlayers && !names[n].empty()) {
        return names[n];
    }
    return fmt::format("P{}", n);
}

} // namespace

bool On() {
    return TheGlue().on;
}

bool Down() {
    Glue& g = TheGlue();
    return g.on && g.downed.dead_status();
}

void SetManual() {
    Glue& g = TheGlue();
    if (g.on && !g.manual) {
        ORM_LOG_INFO("coop: downed: the test script took the gate; it is no longer written");
    }
    g.manual = true;
}

void OnEvent(const coop::net::Event& event) {
    if (event.kind != coop::net::EventKind::message) {
        return;
    }
    Glue& g = TheGlue();
    if (const auto* d = std::get_if<proto::GroupDefeat>(&event.message)) {
        ORM_LOG_INFO("coop: downed: group defeat: GROUP_DEFEAT from the server ({} down, server "
                     "time {} ms){}",
                     d->down, d->server_time_ms, g.on ? "" : ", ignored: LICHII_COOP_DOWNED is off");
        if (g.on) {
            std::lock_guard lock(g.inbox_mutex);
            g.defeat = d->down;
        }
        return;
    }
    const auto* r = std::get_if<proto::Revive>(&event.message);
    if (r == nullptr) {
        return;
    }
    if (r->progress == proto::kReviveDone) {
        ORM_LOG_INFO("coop: downed: REVIVE from P{} (the request){}", r->player_number,
                     g.on ? "" : ", ignored: LICHII_COOP_DOWNED is off");
    } else {
        ORM_LOG_INFO("coop: downed: REVIVE from P{}: holding A beside us, {} %", r->player_number,
                     r->progress);
    }
    if (!g.on) {
        return;
    }
    std::lock_guard lock(g.inbox_mutex);
    if (r->progress == proto::kReviveDone) {
        if (g.inbox.size() < 16) {
            g.inbox.push_back(r->player_number);
        }
    } else if (g.progress.size() < 64) {
        g.progress.push_back({r->player_number, r->progress});
    }
}

host::DownedState State() {
    Glue& g = TheGlue();
    return g.on ? g.downed.state() : host::DownedState::alive;
}

std::vector<std::string> TakeLines() {
    Glue& g = TheGlue();
    return std::exchange(g.lines, {});
}

bool MateDown(const coop::presence::Player* player) {
    return player != nullptr && TheGlue().on && player->has_flag(proto::status::dead);
}

std::string Banner() {
    Glue& g = TheGlue();
    if (!g.on) {
        return {};
    }
    std::lock_guard lock(g.banner_mutex);
    return g.banner;
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading, std::uint64_t server_now_us,
           std::string_view event_name) {
    Glue& g = TheGlue();
    if (!g.on || main_base == 0) {
        return;
    }
    const std::uint64_t now = CoopEvery::NowUs();
    CoopPlatform::GuestMemory& memory = platform.Memory();  // main_base() == main_base
    host::PlayerReader reader(memory);

    host::DownedInput in;
    in.now_us = now;
    const std::optional<std::uint64_t> actor = reader.find_player_actor();
    if (actor) {
        in.vitals = host::read_vitals(memory, *actor);
    }
    // ORM D-R62 (oot-64 D-178): a reload at the room's age begun as we lay
    // down (coop_age_game.h, last frame's): we stay down through it.
    in.hold_down = CoopAge::HoldsDown();
    in.event_running = reading ? reading->event_running : std::nullopt;
    in.manual = g.manual;
    if (g.downed.wants_bottles(in.vitals)) {  // ORM D-R17: also the frame he dies, for his own fairy
        in.bottles = host::read_bottles(memory);
    }
    // The room (P4): alone or with no co-op, the game's own death applies.
    std::array<std::string, proto::kMaxPlayers + 1> names;
    std::vector<host::Mate> mates;
    in.coop = session != nullptr;
    in.connected = session != nullptr && server_now_us != 0;
    if (session != nullptr) {
        mates = Mates(*session, names);
    }
    in.mates = static_cast<int>(mates.size());
    std::vector<std::uint8_t> asked;
    std::vector<Glue::Progress> progress;
    std::uint8_t defeat = 0;
    {
        std::lock_guard lock(g.inbox_mutex);
        asked.swap(g.inbox);
        progress.swap(g.progress);
        defeat = std::exchange(g.defeat, std::uint8_t{0});
    }
    for (const std::uint8_t by : asked) {
        g.downed.request_revive(by);
    }
    for (const auto& pr : progress) {
        g.downed.hear_progress(pr.by, pr.percent, now);
    }
    if (defeat != 0) {
        const bool taken = g.downed.group_defeat(defeat);
        g.group_defeat = g.group_defeat || taken;
        ORM_LOG_INFO("coop: downed: group defeat {} (we are {})",
                     taken ? "taken: our game over runs" : "dropped", host::to_string(g.downed.state()));
    }

    const host::DownedActions act = g.downed.tick(in);
    for (const auto& line : act.log) {
        ORM_LOG_INFO("coop: downed: {} (life {} of {}, death word {})", line,
                     in.vitals.life.value_or(-1), in.vitals.max_life.value_or(-1),
                     in.vitals.death_word ? fmt::format("{:#x}", *in.vitals.death_word) : "-");
    }
    if (act.life) {
        // ORM: the life after a mate's revive (max(3 hearts, 25 %)), or 0 to put
        // down again a Link a reload got up (D-R62).
        const bool ok = actor && host::write_life(memory, *actor, act.life->quarters, act.life->direct);
        ORM_LOG_INFO("coop: downed: life <- {} ({}){}", act.life->quarters,
                     act.life->direct ? "straight into the Life component"
                                      : "through the game's queue, PlayerStatus.Life",
                     ok ? "" : " (NOT written)");
    }
    for (const auto& w : act.bottles) {
        const bool ok = host::write_bottle(memory, w.slot, w.value);
        ORM_LOG_INFO("coop: downed: bottle {} <- {}{}", static_cast<char>('A' + w.slot),
                     BottleText(w.value), ok ? "" : " (NOT written)");
    }
    if (act.quiet) {
        if (session != nullptr) {
            session->set_quiet_flags(*act.quiet);
        }
        ORM_LOG_INFO("coop: downed: {} flags kept out of sharing", act.quiet->size());
    }
    if (act.write_gate) {
        WriteGate(memory, main_base, act.gate_closed);
    }
    if (session != nullptr) {
        // ORM D-R66/D-R73: getting up counts as standing for the group
        // defeat (no dead bit); a mate's 3 s hold beside us keeps the bit
        // (REVIVE goes only to a dead player) and the server counts the hold.
        const bool dead = g.downed.status_dead();
        session->set_downed(dead);
        if (dead != g.status_sent) {
            g.status_sent = dead;
            ORM_LOG_INFO("coop: downed: PLAYER_STATUS dead bit {}{}", dead ? "on" : "off",
                         !dead && g.downed.dead_status()
                             ? " (getting up: we count as standing for the group defeat, D-R66)"
                             : "");
        }
        const bool held = g.downed.held(now);
        if (held != g.held_logged) {
            g.held_logged = held;
            const bool down = g.downed.state() == host::DownedState::downed;
            ORM_LOG_INFO("coop: downed: {}",
                         held   ? "a mate holds A beside us: the server counts us as standing for the "
                                  "group defeat while the hold lasts (D-R66)"
                         : down ? "the hold beside us stopped: down again for the group defeat (its 2 s "
                                  "start over)"
                                : "the hold beside us is over");
        }
    }

    // A mate's side: A held beside a downed player. Ticked offline too
    // (connected false: no hold counts), so a hold never spans a drop.
    host::ReviveOutput out;
    if (session != nullptr) {
        host::ReviveInput r;
        r.now_us = now;
        r.connected = server_now_us != 0;
        if (reading) {
            r.position = reading->position;
            r.scene = reading->scene;
        }
        r.self_down = g.downed.dead_status();
        r.a_held = AHeld(platform);
        r.mates = mates;
        out = g.watch.tick(r);
        for (const auto& line : out.log) {
            ORM_LOG_INFO("coop: downed: {}", line);
        }
        // ORM: the others' falls and revives are told in the chat, whoever
        // is near: with the panel hidden (F9) nothing else says it.
        for (const std::uint8_t n : out.went_down) {
            if (g.lines.size() < 16) g.lines.push_back(host::mate_down_notice(NameOf(names, n)));
        }
        for (const std::uint8_t n : out.got_up) {
            if (g.lines.size() < 16) g.lines.push_back(host::mate_up_notice(NameOf(names, n)));
        }
        // ORM: a mate's fall heard once: a game sound at his puppet when near,
        // else on the interface (coop/host/mate_down_sound.hpp; the sound is
        // OrmOcarinaSound's cue, on only with LICHII_ORM_MATE_DOWN_SOUND).
        OrmOcarinaSound::Player& player = OrmOcarinaSound::ThePlayer();
        if (player.CuesEnabled()) {
            host::MateDownSoundInput s;
            s.now_us = now;
            if (reading) {
                s.scene = reading->scene;
                s.position = reading->position;
            }
            s.game_over = g.downed.state() == host::DownedState::game_over;
            s.mates = mates;
            const host::MateDownSoundOutput heard = g.sound.tick(s);
            for (const auto& line : heard.log) {
                ORM_LOG_INFO("coop: downed: sound: {}", line);
            }
            if (heard.cue) {
                player.OnCue(main_base, heard.cue->mate, heard.cue->at_mate, now / 1000);  // the cave's ms
            }
        }
        if (out.progress_to) {
            const proto::Revive m{proto::kPlayerSelf, *out.progress_to, out.progress};
            const bool sent = session->with_client([&](coop::net::Client& c) { return c.send(m); });
            ORM_LOG_INFO("coop: downed: REVIVE progress {} % sent to P{}{}", out.progress,
                         *out.progress_to, sent ? "" : " (NOT sent)");
        }
        if (out.request) {
            const proto::Revive m{proto::kPlayerSelf, *out.request, proto::kReviveDone};
            const bool sent = session->with_client([&](coop::net::Client& c) { return c.send(m); });
            ORM_LOG_INFO("coop: downed: REVIVE sent to P{}{}", *out.request, sent ? "" : " (NOT sent)");
        }
    }

    // ORM Continue (coop_continue.h): after a game over, the reload follows the host's.
    CoopContinue::Frame(session, g.downed.state(), g.group_defeat, now,
                        reading && reading->event_running.value_or(false), event_name);

    // The line on the screen (the co-op window shows it).
    host::BannerView view;
    view.state = g.downed.state();
    if (view.state != host::DownedState::game_over) {
        g.group_defeat = false;  // back up after the reload (or never taken)
    }
    view.group_defeat = g.group_defeat;
    if (const auto rv = g.downed.reviver(now)) {
        view.reviver = NameOf(names, rv->by);
        view.reviver_percent = rv->percent;
    }
    if (out.in_reach != 0) {
        view.target = NameOf(names, out.in_reach);
        view.holding_percent = out.holding_percent;
    }
    if (const auto until = g.downed.alone_until()) {
        view.alone_left_us = *until > now ? *until - now : 0;
    }
    std::string text = host::banner_text(view);
    // One log line per kind of line, not per percent.
    std::string kind = text;
    std::erase_if(kind, [](char c) { return c >= '0' && c <= '9'; });
    if (kind != g.banner_logged) {
        ORM_LOG_INFO("coop: downed: screen: '{}'", text);
        g.banner_logged = std::move(kind);
    }
    std::lock_guard lock(g.banner_mutex);
    g.banner = std::move(text);
}

} // namespace CoopDowned
