// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM moving floors that carry a partner (orm_ride.h). No emulator code: the
// tests build this file on its own; the log goes to the emulator's log only
// in the hook's build.

#include "orm_ride.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <set>
#include <variant>

#include "coop/net/client.hpp"
#include "coop_every.h"
#include "coop_switch.h"
#include "platform_env.h"  // ORM_LOG_INFO, ORM_PLATFORM_LIVE

namespace OrmRide {
namespace {

std::string Hex(std::uint64_t key) {
    char text[24];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(key));
    return text;
}

std::string Metres(float v) {
    char text[24];
    std::snprintf(text, sizeof text, "%.2f", static_cast<double>(v));
    return text;
}

// a is after b, allowing for the sequence's wrap.
bool Newer(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::int32_t>(a - b) > 0;
}

} // namespace

void Rides::Walked(const std::string& scene, const std::optional<Vec3>& link,
                   const std::vector<coop::host::RidePlatform>& platforms, std::uint64_t now_us,
                   const Grabs& grabs) {
    const std::optional<coop::protocol::Ride> before = detector_.ride();
    if (scene != walk_scene_) {
        // Another scene: its floors are other floors, the ride is over.
        detector_.reset();
        platforms_.clear();
        names_.clear();
        grab_names_.clear();
        follows_.clear();
        walk_scene_ = scene;
    }
    if (scene.empty()) {
        detector_.reset();  // no scene (a load, the title): nothing carries us
    } else {
        detector_.update(link, platforms, now_us);
    }
    const std::optional<coop::protocol::Ride>& now = detector_.ride();
    // ORM boss families: the grabbing parts are followed as the floors are (never detected as
    // ours: our ride on one is Grabs::ours, from what holds our Link).
    if (grabs.parts.empty()) {
        platforms_.walked(platforms, now_us);
    } else {
        std::vector<coop::host::RidePlatform> all = platforms;
        all.insert(all.end(), grabs.parts.begin(), grabs.parts.end());
        platforms_.walked(all, now_us);
    }
    names_.clear();
    for (const auto& p : platforms) {
        names_.emplace(p.key, p.name);
    }
    grab_names_.clear();
    for (const auto& p : grabs.parts) {
        grab_names_.emplace(p.key, p.name);
    }
    held_ = scene.empty() ? std::nullopt : grabs.ours;
    walk_us_ = now_us;
    const bool changed = now.has_value() != before.has_value() ||
                         (now && before && now->platform != before->platform);
    if (!changed) {
        return;
    }
    if (before && riding_key_ != 0) {
        Say("ride: we are off " + riding_name_ + " " + Hex(riding_key_));
    }
    riding_key_ = 0;
    riding_name_.clear();
    if (now) {
        riding_key_ = now->platform;
        riding_name_ = detector_.ride_name();
        Say("ride: we ride " + riding_name_ + " " + Hex(riding_key_) + ", at " + Metres(now->offset[0]) + " " +
            Metres(now->offset[1]) + " " + Metres(now->offset[2]) + " from it");
    }
}

std::optional<coop::protocol::Ride> Rides::Ours(std::uint64_t now_us) const {
    if (walk_us_ == 0 || now_us < walk_us_ || now_us - walk_us_ > kWalkFreshUs) {
        return std::nullopt;  // no walk lately (objects off, a load): not riding
    }
    if (held_) {
        return held_;  // ORM boss families: held by a grabbing part, drawn on it
    }
    return detector_.ride();
}

void Rides::Received(const coop::protocol::PlayerSample& sample, std::uint64_t now_us) {
    const std::uint8_t n = sample.player_number;
    if (n == 0 || n > coop::protocol::kMaxPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    Remote& r = remotes_[n];
    if (r.have && !Newer(sample.time.sequence, r.sequence)) {
        return;  // late or repeated: the newer one already said
    }
    r.have = true;
    r.sequence = sample.time.sequence;
    r.ride = sample.ride;  // none: not riding
    r.at_us = now_us;
}

void Rides::Left(std::uint8_t player) {
    if (player == 0 || player > coop::protocol::kMaxPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    remotes_[player] = Remote{};
}

void Rides::ForgetRemotes() {
    std::lock_guard lock(mutex_);
    remotes_.fill(Remote{});
}

std::optional<coop::protocol::Ride> Rides::Theirs(std::uint8_t player, std::uint64_t now_us) const {
    if (player == 0 || player > coop::protocol::kMaxPlayers) {
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);
    const Remote& r = remotes_[player];
    if (!r.have || !r.ride || (now_us > r.at_us && now_us - r.at_us > kStaleUs)) {
        return std::nullopt;
    }
    return r.ride;
}

std::string Rides::NameOf(std::uint64_t key) const {
    if (const auto it = names_.find(key); it != names_.end()) {
        return it->second;
    }
    if (const auto it = grab_names_.find(key); it != grab_names_.end()) {
        return it->second;
    }
    return {};
}

bool Rides::Grabber(std::uint64_t key) const {
    return key != 0 && grab_names_.contains(key);
}

void Rides::Note(std::uint8_t player, Follow& f, Seen now, std::uint64_t key, float apart) {
    if (now == f.logged && (now == Seen::none || key == f.key)) {
        return;
    }
    const std::string who = "ride: P" + std::to_string(player);
    // ORM boss families: a key that is one of our walk's grabbing parts (one not loaded here
    // cannot be told from a floor: said as a ride).
    const bool held = now != Seen::none && Grabber(key);
    switch (now) {
    case Seen::none:
        Say(who + (f.held ? " is let go" : " off its floor"));
        break;
    case Seen::on_floor:
        if (held) {
            Say(who + " is held by " + NameOf(key) + " " + Hex(key) + ": drawn on our copy of it");
        } else {
            Say(who + " rides " + Hex(key) + ": drawn on our floor");
        }
        break;
    case Seen::away:
        Say(who + (held ? " is held by " : " rides ") + Hex(key) + ": our " + (held ? "copy" : "floor") +
            " is " + Metres(apart) + " m from theirs, drawn where they are");
        break;
    case Seen::not_loaded:
        Say(who + (held ? " is held by " : " rides ") + Hex(key) + ": not loaded here, drawn where they are");
        break;
    }
    f.logged = now;
    f.key = key;
    f.held = held;
}

void Rides::Draw(std::vector<CoopPuppet::Target>& targets, const std::string& scene, std::uint64_t now_us) {
    std::set<std::uint8_t> drawn;
    const bool our_floors = !scene.empty() && scene == walk_scene_;
    for (auto& t : targets) {
        if (t.number == 0) {
            continue;  // the debug puppet has no player
        }
        drawn.insert(t.number);
        Follow& f = follows_[t.number];
        if (!our_floors || t.show != CoopPuppet::Show::show) {
            // Parked, frozen in its cutscene, or our floors are of another
            // scene: drawn on its samples (a step starts again from nothing).
            f.follower.reset();
            Note(t.number, f, Seen::none, 0, 0.0f);
            continue;
        }
        const auto ride = Theirs(t.number, now_us);
        std::optional<Vec3> ours;
        float reach = coop::host::RideFollower::kReach;
        if (ride) {
            ours = platforms_.at(ride->platform, t.position, now_us);
            if (const auto name = names_.find(ride->platform); name != names_.end()) {
                if (const auto shape = coop::host::ride_shape(name->second)) {
                    reach = shape->reach;
                }
            }
        }
        const Vec3 sampled = t.position;  // where it is drawn without the step
        const Vec3 step = f.follower.step(ride, ours, sampled, now_us, reach);
        for (std::size_t i = 0; i < 3; ++i) {
            t.position[i] += step[i];
            t.offset[i] += step[i];  // the smoothing director adds it to its own place
        }
        Seen now = Seen::none;
        float apart = 0.0f;
        if (ride && !ours) {
            now = Seen::not_loaded;
        } else if (ride) {
            now = f.follower.on() ? Seen::on_floor : Seen::away;
            const Vec3 target{(*ours)[0] + ride->offset[0], (*ours)[1] + ride->offset[1],
                              (*ours)[2] + ride->offset[2]};
            apart = std::hypot(target[0] - sampled[0], target[1] - sampled[1], target[2] - sampled[2]);
        }
        Note(t.number, f, now, ride ? ride->platform : 0, apart);
    }
    for (auto it = follows_.begin(); it != follows_.end();) {
        it = drawn.contains(it->first) ? std::next(it) : follows_.erase(it);
    }
}

const coop::host::RideFollower* Rides::FollowerOf(std::uint8_t player) const {
    const auto it = follows_.find(player);
    return it == follows_.end() ? nullptr : &it->second.follower;
}

bool On() {
    static const bool on = CoopSwitch::On("LICHII_ORM_RIDE", "LICHII_COOP_RIDE", true);
    return on;
}

Rides& TheRides() {
    static Rides rides([](const std::string& line) {
#if ORM_PLATFORM_LIVE
        ORM_LOG_INFO("coop: {}", line);
#else
        (void)line;
#endif
    });
    return rides;
}

void Walked(const std::string& scene, const std::optional<Vec3>& link,
            const std::vector<coop::host::RidePlatform>& platforms, const Grabs& grabs) {
    if (On()) {
        TheRides().Walked(scene, link, platforms, CoopEvery::NowUs(), grabs);
    }
}

std::optional<coop::protocol::Ride> Ours() {
    if (!On()) {
        return std::nullopt;
    }
    return TheRides().Ours(CoopEvery::NowUs());
}

void OnEvent(const coop::net::Event& event) {
    if (!On()) {
        return;
    }
    using coop::net::EventKind;
    switch (event.kind) {
    case EventKind::message:
        if (const auto* sample = std::get_if<coop::protocol::PlayerSample>(&event.message)) {
            TheRides().Received(*sample, CoopEvery::NowUs());
        }
        break;
    case EventKind::player_left:
        TheRides().Left(event.player);
        break;
    case EventKind::disconnected:
    case EventKind::reconnected:
        TheRides().ForgetRemotes();  // the sequences start again
        break;
    default:
        break;
    }
}

void Draw(std::vector<CoopPuppet::Target>& targets, const std::string& scene) {
    if (On()) {
        TheRides().Draw(targets, scene, CoopEvery::NowUs());
    }
}

} // namespace OrmRide
