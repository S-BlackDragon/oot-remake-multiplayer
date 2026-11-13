// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM cofre: the emulator side of coop_chest_open.h. The network thread hands
// over the others' CHEST_OPENs; the present thread passes them to the door
// mirror (the lid) and to ChestFinds (the find over the head, the notice),
// sends the mirror's own CHEST_OPENs, and gives the spawn director the finds
// held up now.
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "platform_env.h"  // ORM_LOG_*
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop/host/chest_open.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_check_visual.h"
#include "coop_chest_open.h"
#include "coop_doors.h"
#include "coop_notices.h"
#include "coop_puppet.h"
#include "coop_spawn.h"

namespace CoopChestOpen {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

std::string Where(const proto::ChestOpen& m) {
    const host::ChestPlacement* c = host::find_chest(m.chest);
    const std::string drop = c != nullptr ? std::string(host::chest_drop(*c)) : std::string();
    return fmt::format("{:#018x} in {} ({})", m.chest, m.scene,
                       c == nullptr ? std::string("not in the chest list")
                                    : drop.empty() ? std::string(c->actor) : drop);
}

struct Glue {
    std::mutex inbox_mutex;
    std::vector<proto::ChestOpen> inbox;  // network thread to present
    host::ChestFinds finds;               // present thread
    bool connected = false;               // present thread: the last frame had a session
    bool held_any = false;                // present thread: finds handed to the director
    bool said = false;
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

}  // namespace

void OnEvent(const coop::net::Event& event) {
    if (!On() || event.kind != coop::net::EventKind::message) {
        return;
    }
    const auto* m = std::get_if<proto::ChestOpen>(&event.message);
    if (m == nullptr) {
        return;
    }
    Glue& g = TheGlue();
    std::lock_guard lock(g.inbox_mutex);
    if (g.inbox.size() < 64) {
        g.inbox.push_back(*m);
    }
}

void Frame(std::uint64_t main_base, const std::optional<std::string>& scene, host::Session* session,
           std::uint64_t server_now_us) {
    if (!On()) {
        return;
    }
    Glue& g = TheGlue();
    if (!g.said) {
        g.said = true;
        ORM_LOG_INFO("coop: chests: what a player finds in a chest is shown over his Link and told to everyone "
                     "(LICHII_ORM_CHEST_OPEN=0 turns it off)");
    }
    const std::uint64_t now_ms = NowMs();
    std::vector<proto::ChestOpen> inbox;
    {
        std::lock_guard lock(g.inbox_mutex);
        inbox.swap(g.inbox);
    }
    CoopDoors::Mirror* mirror = CoopDoors::TheMirror();
    if (session == nullptr || main_base == 0) {
        if (g.connected || g.held_any) {
            g.finds.reset();
            CoopSpawn::TheDirector().SetFinds(0, {});
            g.held_any = false;
        }
        g.connected = false;
        if (mirror != nullptr) {
            mirror->TakeChestOpens();  // not connected: nobody to tell
            // Nor any find to leave out: kept, they would arm expect_double_find
            // at the next connection for a chest of long ago (and undo a real find).
            mirror->TakeSecondFinds();
        }
        return;
    }
    g.connected = true;
    for (const auto& m : inbox) {
        ORM_LOG_INFO("coop: chests: CHEST_OPEN from P{}: {}, '{}' at frame {:.1f}", m.player_number, Where(m),
                     m.animation.name, m.animation.frame);
        if (mirror != nullptr && mirror->ChestLidsOn()) {
            mirror->ReceiveChestOpen(m);
        }
        g.finds.opened(m, now_ms);
    }
    if (mirror != nullptr) {
        for (const auto& m : mirror->TakeChestOpens()) {
            const bool sent = session->with_client([&](coop::net::Client& c) { return c.send(m); });
            ORM_LOG_INFO("coop: chests: CHEST_OPEN {}{}", sent ? "sent: " : "NOT sent (not connected): ", Where(m));
        }
        // ORM riesgos-visual: a chest we opened second (his CHEST_OPEN first):
        // its counted find is his; ours is put back, not sent (expect_double_find).
        for (const std::uint64_t hash : mirror->TakeSecondFinds()) {
            const std::string flag = coop::host::chest_count_flag(hash);
            if (flag.empty()) {
                ORM_LOG_INFO("coop: chests: {:#018x} opened here second: a unique item (or each one's own), "
                             "nothing to count twice", hash);
                continue;
            }
            const bool armed =
                session->expect_double_find(coop::protocol::FlagKey(coop::protocol::flag_hash(flag), 0));
            ORM_LOG_INFO("coop: chests: {:#018x} opened here second: our {} {}", hash, flag,
                         armed ? "gain is left out (the first opener's counts)"
                               : "is not shared (each player's own): nothing to leave out");
        }
    }
    for (const auto& u : session->take_doubles_undone()) {
        CoopVCheck::Ok("chest double find",
                       fmt::format("{:#010x} +{} from a chest opened first in another game left out{}: counted once",
                                   u.count.hash, u.amount, u.after_sent ? " (it had gone out: taken back)" : ""));
        ORM_LOG_INFO("coop: chests: flag {:#010x}: our +{} of a chest opened first elsewhere left out{}",
                     u.count.hash, u.amount, u.after_sent ? ", sent back as a negative delta" : "");
    }

    std::map<std::uint8_t, std::string> names;
    session->with_client([&](coop::net::Client& c) {
        for (const auto& p : c.players()) {
            names[p.number] = p.nickname;
        }
        return 0;
    });
    const auto name = [&](std::uint8_t n) {
        const auto it = names.find(n);
        return it != names.end() && !it->second.empty() ? it->second : "P" + std::to_string(n);
    };
    const auto out = g.finds.tick(scene, Mates(CoopPuppet::Collect(session, server_now_us)), name, now_ms);
    for (const auto& line : out.lines) {
        ORM_LOG_INFO("coop: chests: {}", line);
    }
    for (const auto& n : out.notices) {
        ORM_LOG_INFO("coop: notice: {}", n.text);
        CoopNotices::TheBoard().Add(n.number, n.name, n.text, now_ms);
    }
    if (!out.held.empty() || g.held_any) {
        if (!out.held.empty()) {
            CoopSpawn::UseDirector();  // once: the director is the svc cave's caller
        }
        CoopSpawn::TheDirector().SetFinds(main_base, Copies(out.held));
        g.held_any = !out.held.empty();
    }
}

}  // namespace CoopChestOpen
