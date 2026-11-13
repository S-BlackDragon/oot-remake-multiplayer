// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM Gerudo Fortress and the Spirit Temple with several players, the hook's
// side (coop_gerudo.h; the rules: coop/host/gerudo.hpp).

#include "coop_gerudo.h"

#include <cstddef>
#include <string_view>

#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/gerudo.hpp"
#include "coop/host/scene.hpp"
#include "coop/host/session.hpp"
#include "coop/presence/presence.hpp"
#include "coop_every.h"
#include "coop_switch.h"
#include "platform.h"  // Platform, ORM_LOG_*

namespace CoopGerudo {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

constexpr std::string_view kFortress = "GerudoFortress";

// Present thread only.
struct Glue {
    host::CarpenterCounts counts;
    CoopEvery::Every read{CoopEvery::kQuarterUs};
    bool in_fortress = false;
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

const char* AgeName(std::optional<proto::Age> age) {
    if (!age) {
        return "unknown";
    }
    return *age == proto::Age::child ? "child" : "adult";
}

} // namespace

bool On() {
    static const bool on = [] {
        // LICHII_ORM_GERUDO (LICHII_COOP_GERUDO before test9), on by default.
        const bool off = !CoopSwitch::On("LICHII_ORM_GERUDO", "LICHII_COOP_GERUDO", true);
        if (off) {
            ORM_LOG_INFO("coop: gerudo: off (LICHII_ORM_GERUDO=0): the carpenters' counts are not mended and "
                         "a story start in the Spirit Temple is followed at any age; the guards and the "
                         "desert's guide Poe still run in each game");
        } else {
            ORM_LOG_INFO("coop: gerudo: on (LICHII_ORM_GERUDO): the carpenters' counts are mended in the "
                         "Gerudo Fortress, a story start in the Spirit Temple is followed only at its "
                         "starter's age; the guards and the desert's guide Poe run in each game");
        }
        return !off;
    }();
    return on;
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<host::PlayerReading>& reading) {
    if (!On()) {
        return;
    }
    Glue& g = TheGlue();
    const bool here = main_base != 0 && reading && reading->scene && host::place_of(*reading->scene) == kFortress;
    if (here != g.in_fortress) {
        g.in_fortress = here;
        g.counts.reset();
        if (here) {
            ORM_LOG_INFO("coop: gerudo: in {}: each game runs its own guards (each catches only its own Link); "
                         "the carpenters' counts are watched",
                         *reading->scene);
        } else {
            ORM_LOG_INFO("coop: gerudo: out of the Gerudo Fortress");
        }
    }
    const std::uint64_t now_us = CoopEvery::NowUs();
    if (!here || !g.read.Due(now_us)) {
        return;
    }

    CoopPlatform::GuestMemory& memory = platform.Memory();
    host::GameFlags flags(memory);
    host::CarpenterReading in;
    in.now_us = now_us;
    in.event_running = reading->event_running;
    for (std::size_t i = 0; i < in.helped.size(); ++i) {
        in.helped[i] = flags.get_bool(host::flag_hash(host::kCarpenterHelpedFlags[i]));
        in.thief_beaten[i] = flags.get_bool(host::flag_hash(host::kThiefBeatenFlags[i]));
    }
    in.helped_count = flags.get_int(host::flag_hash(host::kCarpentersHelpedCount));
    in.found_count = flags.get_int(host::flag_hash(host::kCarpentersFoundCount));
    in.thieves_count = flags.get_int(host::flag_hash(host::kThievesBeatenCount));
    in.card = flags.get_bool(host::flag_hash(host::kGerudoCard));

    const host::CarpenterOutput out = g.counts.tick(in);
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: gerudo: {}", line);
    }
    for (const auto& w : out.writes) {
        const bool queued = host::GameFlags::set_int(memory, host::flag_hash(w.name), w.value);
        ORM_LOG_INFO("coop: gerudo: set_int {} = {}: {}", w.name, w.value,
                     queued ? "queued" : "NOT queued (tried again in a few seconds)");
    }
}

bool StoryStartForUs(host::Session* session, const std::optional<host::PlayerReading>& reading,
                     const proto::StoryEvent& start) {
    if (start.kind != proto::StoryKind::start || !On()) {
        return true;
    }
    const std::optional<proto::Age> ours = reading ? reading->age : std::nullopt;
    std::optional<proto::Age> theirs;
    if (session != nullptr) {
        theirs = session->with_presence([&](coop::presence::Presence& p) -> std::optional<proto::Age> {
            const auto* player = p.player(start.player_number);
            return player != nullptr ? player->age() : std::nullopt;
        });
    }
    if (host::story_start_for_age(start.scene, theirs, ours)) {
        return true;
    }
    ORM_LOG_INFO(
             "coop: story: STORY_EVENT start from P{} ('{}' in {}) not followed: it is the {}'s and we are {} "
             "(two ages in one scene, GERUDO_ESPIRITU.md); its flags and steps come all the same",
             start.player_number, start.name, start.scene, AgeName(theirs), AgeName(ours));
    return false;
}

} // namespace CoopGerudo
