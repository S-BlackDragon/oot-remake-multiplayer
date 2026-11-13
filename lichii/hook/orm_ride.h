// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM moving floors that carry a partner: the hook glue of coop/host/ride.hpp
// (oot-remake docs/TEMPLOS.md "Plataformas", docs/MONTANA_SOMBRA.md "Barca").
// Lifts, rising, sinking and floating floors and the Shadow Temple's boat run
// in each game on their own (oot-64 D-153: each player rides his own boat);
// this only draws a partner who stands on one on OUR copy of it.
//
// - Our side (Walked, Ours): each objects walk (coop_objects.cpp, Collect;
//   needs LICHII_COOP_OBJECTS=1, as the kit has it) hands over the moving
//   floors loaded here (coop::host::is_ride_platform, their place +0x31c and
//   placement hash) and our Link's place; RideDetector says whether one of
//   them carries our Link, and Ours() is that ride for the sampler
//   (Session::set_ride, before the session's frame): PLAYER_SAMPLE
//   component 30.
// - Their side (OnEvent, Draw): the newest ride of each player's samples
//   (network thread); after the puppet planner, each shown puppet whose
//   player rides a floor that is loaded here gets the step that puts it on
//   our floor at its place on theirs (RideFollower: within the floor's reach
//   of its samples only, faded in and out), added to its position and to
//   Target::offset, so the smoothing director draws it there too.
//   Presentation only: nothing in the game changes.
//
// - ORM boss families (oot-remake docs/JEFES.md section 9): a Link held by a
//   grabbing part (Morpha's tentacle, Bongo Bongo's hand, a Like Like) rides
//   it the same way. Our side: the object sync's FamilyWatch finds the part
//   that holds our Link and its ride ({coop::host::family_key, our Link from
//   it}), which goes out instead of a floor's (Grabs::ours). Their side: the
//   walk's shared grabbing parts (Grabs::parts, by family_key: every game's
//   copy has the same key) are followed as floors are, never by our own ride
//   detector (a Wallmaster's shadow moves with our Link: it carries nothing).
//
// Switch: LICHII_ORM_RIDE (old name LICHII_COOP_RIDE), on; 0 turns it off.
// Log lines (each a change, never a frame):
//   coop: ride: we ride <actor> <key>, at <dx> <dy> <dz> from it
//   coop: ride: we are off <actor> <key>
//   coop: ride: P<n> rides <key>: drawn on our floor
//   coop: ride: P<n> rides <key>: our floor is <d> m from theirs, drawn where they are
//   coop: ride: P<n> rides <key>: not loaded here, drawn where they are
//   coop: ride: P<n> off its floor
//   coop: ride: P<n> is held by <actor> <key>: drawn on our copy of it
//   coop: ride: P<n> is held by <key>: our copy is <d> m from theirs, drawn where they are
//   coop: ride: P<n> is held by <key>: not loaded here, drawn where they are
// Rides is plain logic, tested on its own (tests/lichii/ride_hook_test.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "coop/host/ride.hpp"
#include "coop/protocol/messages.hpp"
#include "coop_puppet.h"

namespace coop::net {
struct Event;
}

namespace OrmRide {

using Vec3 = coop::protocol::Vec3;

// ORM boss families: the walk's grabbing parts and what holds our Link (CoopObjects' Sync,
// FamilyParts and FamilyRide).
struct Grabs {
    std::vector<coop::host::RidePlatform> parts;  // by coop::host::family_key
    std::optional<coop::protocol::Ride> ours;     // our Link on the part that holds it
};

class Rides {
public:
    using Log = std::function<void(const std::string&)>;

    // A remote ride whose sample came longer ago than this is dropped (the
    // sampler sends at least 5 a second, keepalive_us).
    static constexpr std::uint64_t kStaleUs = 1'000'000;
    // Our ride goes out while the walk that found it is at most this old
    // (walks are 30 a second; RideSettings::max_gap_us).
    static constexpr std::uint64_t kWalkFreshUs = 250'000;

    explicit Rides(Log log = {}) : log_(std::move(log)) {}

    // Present thread, after each objects walk of `scene` ("" none): our
    // Link's place (nothing: not read) and the moving floors loaded.
    void Walked(const std::string& scene, const std::optional<Vec3>& link,
                const std::vector<coop::host::RidePlatform>& platforms, std::uint64_t now_us,
                const Grabs& grabs = {});
    // Present thread: the ride for our next samples: what holds our Link, else the floor.
    std::optional<coop::protocol::Ride> Ours(std::uint64_t now_us) const;

    // Network thread: a relayed PLAYER_SAMPLE (a late, older one is skipped).
    void Received(const coop::protocol::PlayerSample& sample, std::uint64_t now_us);
    // Network thread: the player left; the connection was lost or made again.
    void Left(std::uint8_t player);
    void ForgetRemotes();
    // Any thread: player n's ride now (nothing: none, or too old).
    std::optional<coop::protocol::Ride> Theirs(std::uint8_t player, std::uint64_t now_us) const;

    // Present thread, after the puppet planner, before the director gets the
    // plan: puts each shown puppet whose player rides a floor of ours on it.
    void Draw(std::vector<CoopPuppet::Target>& targets, const std::string& scene, std::uint64_t now_us);

    // For tests and the log: player n's follower (null: none).
    const coop::host::RideFollower* FollowerOf(std::uint8_t player) const;

private:
    enum class Seen : std::uint8_t { none, on_floor, away, not_loaded };
    struct Remote {
        bool have = false;
        std::uint32_t sequence = 0;
        std::optional<coop::protocol::Ride> ride;
        std::uint64_t at_us = 0;
    };
    struct Follow {
        coop::host::RideFollower follower;
        Seen logged = Seen::none;
        std::uint64_t key = 0;  // the floor of the last line
        bool held = false;      // ...a grabbing part (ORM boss families)
    };
    void Say(const std::string& line) const {
        if (log_) log_(line);
    }
    void Note(std::uint8_t player, Follow& f, Seen now, std::uint64_t key, float apart);
    // The walk's actor of `key` ("" none) and whether it is a grabbing part.
    std::string NameOf(std::uint64_t key) const;
    bool Grabber(std::uint64_t key) const;

    Log log_;
    // The present thread only.
    coop::host::RideDetector detector_;
    coop::host::RidePlatforms platforms_;
    std::unordered_map<std::uint64_t, std::string> names_;  // the last walk's floors, by key
    std::unordered_map<std::uint64_t, std::string> grab_names_;  // its grabbing parts, by key
    std::optional<coop::protocol::Ride> held_;  // what holds our Link (Grabs::ours), this walk
    std::string walk_scene_;
    std::uint64_t walk_us_ = 0;
    std::uint64_t riding_key_ = 0;  // for the "off" line
    std::string riding_name_;
    std::map<std::uint8_t, Follow> follows_;
    // Guarded: the network thread writes them.
    mutable std::mutex mutex_;
    std::array<Remote, coop::protocol::kMaxPlayers + 1> remotes_{};
};

// LICHII_ORM_RIDE (or LICHII_COOP_RIDE) not 0. Read once.
bool On();

// The hook's one Rides (logs through the emulator's log). Every call below
// does nothing while On() is false.
Rides& TheRides();

// CoopObjects::Share, after each walk (present thread).
void Walked(const std::string& scene, const std::optional<Vec3>& link,
            const std::vector<coop::host::RidePlatform>& platforms, const Grabs& grabs = {});
// CoopHook, before Session::frame: Session::set_ride(OrmRide::Ours()).
std::optional<coop::protocol::Ride> Ours();
// Session::on_event (network thread).
void OnEvent(const coop::net::Event& event);
// CoopPuppet::Publish, right after the planner.
void Draw(std::vector<CoopPuppet::Target>& targets, const std::string& scene);

} // namespace OrmRide
