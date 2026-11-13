// SPDX-License-Identifier: GPL-3.0-or-later
// ORM choque: real bodies between Links (coop_bodies.h, oot-remake docs/CHOQUE.md).
#include "coop_bodies.h"

#include <algorithm>
#include <utility>

#include "coop_switch.h"

namespace CoopBodies {

Mode ModeOf(std::optional<std::string_view> value, std::string* complaint) {
    if (!value) {
        return Mode::room;
    }
    std::string_view v = *value;
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
        v.remove_prefix(1);
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) {
        v.remove_suffix(1);
    }
    if (CoopSwitch::detail::SameNoCase(v, "room")) {
        return Mode::room;
    }
    switch (CoopSwitch::Parse(v)) {
    case CoopSwitch::Reading::unset:
        return Mode::room;
    case CoopSwitch::Reading::on:
        return Mode::on;
    case CoopSwitch::Reading::off:
        return Mode::off;
    case CoopSwitch::Reading::unknown:
        break;
    }
    if (complaint != nullptr) {
        *complaint = CoopSwitch::Unknown("LICHII_ORM_BODY_COLLIDER", *value, "room/0/1/true/false/yes/no/on/off",
                                         "room");
    }
    return Mode::room;
}

const char* ModeText(Mode mode) {
    switch (mode) {
    case Mode::room:
        return "the room's /choque";
    case Mode::on:
        return "on (LICHII_ORM_BODY_COLLIDER)";
    case Mode::off:
        return "off (LICHII_ORM_BODY_COLLIDER)";
    }
    return "?";
}

std::string BodyActor(bool adult, bool top) {
    return std::string(adult ? "CoopPuppet_Adult" : "CoopPuppet_Child") + (top ? "_Body" : "_BodySide");
}

PackBodies BodiesIn(std::string_view manifest) {
    const auto has = [&](std::string_view actor) {
        const std::string quoted = "\"" + std::string(actor) + "\"";  // the closing quote: _Body is not _BodySide
        return manifest.find(quoted) != std::string_view::npos;
    };
    PackBodies p;
    p.child = has("CoopPuppet_Child_Body");
    p.adult = has("CoopPuppet_Adult_Body");
    p.child_side = has("CoopPuppet_Child_BodySide");
    p.adult_side = has("CoopPuppet_Adult_BodySide");
    return p;
}

PackBodies Merge(PackBodies a, const PackBodies& b) {
    a.child = a.child || b.child;
    a.adult = a.adult || b.adult;
    a.child_side = a.child_side || b.child_side;
    a.adult_side = a.adult_side || b.adult_side;
    return a;
}

Pick Choose(Mode mode, bool room_on, const PackBodies& pack, bool top_wanted, bool broken) {
    Pick p;
    if (mode == Mode::off) {
        p.fallback = true;
        p.why = "LICHII_ORM_BODY_COLLIDER=0: the mates are drawn aside";
        return p;
    }
    if (mode == Mode::room && !room_on) {
        p.why = "the room's /choque is off: the mates are drawn aside";
        return p;
    }
    if (!pack.Top() && !pack.Side()) {
        p.fallback = true;
        p.why = "the puppet pack has no CoopPuppet_Child_Body and CoopPuppet_Adult_Body (run make-puppet-pack "
                "again): the mates are drawn aside";
        return p;
    }
    if (broken) {
        p.fallback = true;
        p.why = "a body could not be made " + std::to_string(CreateTally::kFailures) +
                " times (see the 'body coop: puppet:' lines): the mates are drawn aside";
        return p;
    }
    p.on = true;
    p.top = top_wanted ? pack.Top() : !pack.Side();  // the kind asked for, else the one the pack has
    return p;
}

CoopPuppet::Input BodiesFrom(const CoopPuppet::Input& puppets, bool top) {
    CoopPuppet::Input in;
    in.main_base = puppets.main_base;
    in.scene = puppets.scene;
    in.camera = puppets.camera;
    in.drawn_since_ms = puppets.drawn_since_ms;
    in.settle_ms = puppets.settle_ms;
    in.server_now_us = puppets.server_now_us;
    in.server_at_ms = puppets.server_at_ms;
    for (const CoopPuppet::Target& t : puppets.targets) {
        CoopPuppet::Target b;
        b.number = t.number;
        b.actor = BodyActor(t.adult, top);
        b.offset = {t.offset[0] - t.aside[0], t.offset[1] - t.aside[1], t.offset[2] - t.aside[2]};
        b.position = {t.position[0] - t.aside[0], t.position[1] - t.aside[1], t.position[2] - t.aside[2]};
        // rotation: identity (upright); no animation, gear, effects or colour.
        b.show = t.solid ? CoopPuppet::Show::show : CoopPuppet::Show::hide;
        b.why = t.solid ? "solid" : "a ghost";
        b.keep_only = t.keep_only;  // a puppet only kept (leaving): no body made for it
        b.sample_age_ms = t.sample_age_ms;
        in.targets.push_back(std::move(b));
    }
    return in;
}

std::optional<coop::host::SmoothedPose> SameFrame(const std::optional<CoopPuppet::ShownPose>& shown,
                                                  std::uint64_t server_now_us) {
    if (!shown || server_now_us == 0) {
        return std::nullopt;
    }
    const std::uint64_t a = shown->server_us, b = server_now_us;
    if ((a > b ? a - b : b - a) > kSameFrameUs) {
        return std::nullopt;  // the puppet was not drawn smoothed this frame: the plan's place
    }
    coop::host::SmoothedPose p = shown->pose;
    p.rotation.reset();   // upright
    p.animation.reset();  // nothing to play
    return p;
}

void CreateTally::Count(const std::string& line) {
    const std::string_view prefix = "coop: puppet: P";
    const auto at0 = line.find(prefix);
    if (at0 == std::string::npos) {
        return;
    }
    std::size_t at = at0 + prefix.size();
    int n = 0;
    bool digits = false;
    while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
        n = n * 10 + (line[at++] - '0');
        digits = true;
    }
    if (!digits) {
        return;
    }
    const std::string rest = line.substr(at);
    std::lock_guard lock(mutex_);
    if (rest.starts_with(": create ")) {
        if (const auto end = rest.find(" requested"); end != std::string::npos && end > 9) {
            requested_[n] = rest.substr(9, end - 9);
        }
    } else if (rest.find(" not spawned after ") != std::string::npos ||
               rest.find(" dropped (state") != std::string::npos) {
        if (const auto it = requested_.find(n); it != requested_.end()) {
            ++failed_[it->second];
        }
    } else if (const auto s = rest.find(" spawned after "); s != std::string::npos && s > 1) {
        ++made_[rest.substr(1, s - 1)];
    }
}

std::optional<std::string> CreateTally::Broken() const {
    std::lock_guard lock(mutex_);
    for (const auto& [actor, count] : failed_) {
        if (count >= kFailures && made_.find(actor) == made_.end()) {
            return actor;
        }
    }
    return std::nullopt;
}

std::optional<std::string> CreateTally::TakeFirstMade() {
    std::lock_guard lock(mutex_);
    if (first_said_ || made_.empty()) {
        return std::nullopt;
    }
    first_said_ = true;
    return made_.begin()->first;
}

} // namespace CoopBodies

#include "platform_env.h"  // ORM_PLATFORM_LIVE

#if ORM_PLATFORM_LIVE

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

#include "coop_check_visual.h"
#include "orm_commands_game.h"
#include "platform.h"  // FolderPath (the puppet pack), AddCaller

namespace CoopBodies {

namespace {

bool EnvOn(const char* name) {
    const auto v = CoopPlatform::Env(name);
    return v && !v->empty() && *v != "0";
}

// Every mod folder's coop_puppet.json under load/<title id> (as the horses' PackHorseLooks).
PackBodies PackBodiesNow() {
    PackBodies pack;
    std::error_code ec;
    const auto title = CoopPlatform::FolderPath(CoopPlatform::Folder::mods) / "0400486024DEC000";
    for (const auto& mod : std::filesystem::directory_iterator(title, ec)) {
        std::ifstream in(mod.path() / "coop_puppet.json", std::ios::binary);
        if (!in) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        pack = Merge(pack, BodiesIn(text));
    }
    return pack;
}

constexpr std::uint64_t kParkBeforeDeleteMs = CoopPuppet::Director::kLeaveGraceMs + 1000;

struct Glue {
    Mode mode = Mode::room;
    bool top_wanted = true;
    PackBodies pack;
    CreateTally tally;
    std::unique_ptr<CoopPuppet::Director> director;
    // The present thread only.
    std::optional<Pick> last;
    bool was_on = false;
    bool was_top = true;             // the kind made while on (parked as it is)
    std::uint64_t off_since_ms = 0;  // the colliders went off then: the bodies parked, then deleted

    Glue() {
        std::string complaint;
        mode = ModeOf(CoopSwitch::Value("LICHII_ORM_BODY_COLLIDER"), &complaint);
        if (!complaint.empty()) {
            ORM_LOG_INFO("{}", complaint);
        }
        top_wanted = CoopSwitch::On("LICHII_ORM_BODY_TOP", true);
        pack = PackBodiesNow();
        ORM_LOG_INFO("coop: bodies: colliders by {}; the head disc {}; the puppet pack has {}{}{}{}", ModeText(mode),
                     top_wanted ? "on" : "off (LICHII_ORM_BODY_TOP=0)",
                     pack.Top() ? "CoopPuppet_*_Body" : "no CoopPuppet_*_Body",
                     pack.Side() ? ", CoopPuppet_*_BodySide" : ", no CoopPuppet_*_BodySide",
                     top_wanted && !pack.Top() && pack.Side() ? " (the sides alone then)" : "",
                     !top_wanted && !pack.Side() && pack.Top() ? " (the disc then)" : "");
        director = std::make_unique<CoopPuppet::Director>(
            [this](const std::string& line) {
                tally.Count(line);  // a create the game refuses: the bodies fall back
                ORM_LOG_INFO("body {}", line);
            },
            EnvOn("LICHII_COOP_DEBUG_PUPPET"), false);
        // A body has physics: a plain write of its place is undone (as the fairies and the horses).
        director->SetMatrixCall(true);
        // Made under the floor, then placed: never made on anyone (a ghost stays parked there).
        director->SetCreateParked(true);
        // Exactly where its puppet was drawn this calc frame (the puppets' director goes first).
        director->SetSmoothing(
            [](std::uint8_t number, std::uint64_t server_now_us) {
                return SameFrame(CoopPuppet::LastSmoothed(number), server_now_us);
            },
            coop::host::SmoothSettings{}.delay_us);
        CoopPlatform::AddCaller(kCallerOrder, [this](CoopPuppet::Guest& guest, bool after_call,
                                                      std::uint64_t result, std::uint64_t now_ms) {
            return director->Step(guest, after_call, result, now_ms);
        });
        CoopPuppet::AddPlanObserver([this](const CoopPuppet::Input& puppets) { Plan(puppets); });
    }

    // The present thread, with each plan of the puppets (CoopPuppet::Publish).
    void Plan(const CoopPuppet::Input& puppets) {
        const auto broken = tally.Broken();
        const Pick pick = Choose(mode, OrmCommands::BodiesOn(), pack, top_wanted, broken.has_value());
        if (!last || last->on != pick.on || last->top != pick.top || last->why != pick.why) {
            if (pick.on) {
                ORM_LOG_INFO("coop: bodies: colliders on ({}, {}): each mate's body waits until clear of our Link",
                             ModeText(mode), pick.top ? "CoopPuppet_*_Body, with the head disc"
                                                      : "CoopPuppet_*_BodySide, no head disc");
            } else {
                ORM_LOG_INFO("coop: bodies: colliders off: {}", pick.why);
            }
            if (pick.fallback) {
                CoopVCheck::Fallback("bodies: collider", pick.why + (broken ? " (" + *broken + ")" : ""));
            }
            OrmCommands::SetBodiesFallback(pick.fallback);
            last = pick;
        }
        CoopPuppet::SetColliders(pick.on);
        const std::uint64_t now_ms = puppets.server_at_ms;  // the steady time of this plan
        if (!pick.on) {
            if (was_on) {
                was_on = false;
                off_since_ms = std::max<std::uint64_t>(now_ms, 1);
            }
            if (off_since_ms != 0 && now_ms - std::min(now_ms, off_since_ms) < kParkBeforeDeleteMs) {
                // Parked first: a body left out of the plan stays where it was for the director's
                // grace (Director::kLeaveGraceMs) before it goes; parked, nobody meets it meanwhile.
                CoopPuppet::Input parked = BodiesFrom(puppets, was_top);
                for (auto& t : parked.targets) {
                    t.show = CoopPuppet::Show::hide;
                    t.why = "colliders off";
                }
                director->SetInput(std::move(parked));
                return;
            }
            off_since_ms = 0;
            CoopPuppet::Input none;  // every body goes (the director deletes them)
            none.main_base = puppets.main_base;
            none.scene = puppets.scene;
            director->SetInput(std::move(none));
            return;
        }
        was_on = true;
        was_top = pick.top;
        off_since_ms = 0;
        director->SetInput(BodiesFrom(puppets, pick.top));
        if (const auto made = tally.TakeFirstMade()) {
            CoopVCheck::Ok("bodies: collider",
                           *made + " made: kinematic, placed with its puppet each frame, hitting only our Link");
        }
    }
};

} // namespace

void Use() {
    if (!EnvOn("LICHII_COOP_PUPPET") && !EnvOn("LICHII_COOP_DEBUG_PUPPET")) {
        return;  // the cave is there only with the puppets
    }
    static Glue glue;  // kept for the whole run: the cave holds its director
    (void)glue;
}

} // namespace CoopBodies

#else

namespace CoopBodies {
void Use() {}
} // namespace CoopBodies

#endif // ORM_PLATFORM_LIVE
