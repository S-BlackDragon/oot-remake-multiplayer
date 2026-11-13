// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM (oot-remake docs/ANCHO_DE_BANDA.md, "Interés"; LICHII_COOP_INTEREST=0
// turns it off): whether this game sends its Link's PLAYER_POSE at all.
// (2026-11-11, DECISIONES.md): no pose compression and no lower rate by
// distance, "ocupamos el movimiento tal cual es nativo". So there are two
// tiers only:
//
//   here   another player in our place:  poses at the native 30 a second
//   alone  nobody in our place:           no pose
//
// The place is the scene without its era, as the puppets
// (coop::host::same_place); a pose only feeds a puppet in that place, and the
// relay drops the rest anyway (server/orm_interest.hpp). PLAYER_SAMPLE always
// goes at the native rate, to everyone: the partners' map and the jump to a
// partner use it.
//
// Anything not known counts as here, so a gap in what we know never takes a
// pose away: a player whose scene or place is not known yet, one not heard
// from for kStaleMs (loading), our own scene not known.
//
// OBJECT_POSE (coop_npc_pose.h): the townsperson a player talks to goes,
// at its own native rate, while another player is in our place (AnyoneHere).
//
// Threads: OnEvent on the network thread, SetLocal on the emulator thread,
// the queries on either. One mutex guards it all. No emulator header, so the
// unit tests build it.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "coop/protocol/messages.hpp"

namespace coop::net {
struct Event;
}  // namespace coop::net

namespace CoopInterest {

using Vec3 = coop::protocol::Vec3;

// A player whose last sample is older than this is loading (or gone):
// counted as here until it is heard again. Samples come at least 5 a second.
inline constexpr std::uint64_t kStaleMs = 3000;

enum class Tier : std::uint8_t { here, alone };
std::string_view ToString(Tier tier);

struct Rates {
    std::optional<std::uint64_t> pose_gap_ms;  // nothing: send no pose
    bool operator==(const Rates&) const = default;
};

inline constexpr std::uint64_t kPoseGapMs = 1000 / 30;  // coop_pose.h kSendHz, the native rate

Rates RatesOf(Tier tier);

class Interest {
public:
    void SetEnabled(bool on);
    bool enabled() const;

    // Emulator thread, each frame with a reading: where our Link is. A
    // frame without a reading (a load) keeps the last place.
    void SetLocal(std::optional<std::string_view> scene, std::optional<Vec3> position);
    // Network thread.
    void SetSelf(std::uint8_t number);
    void Joined(std::uint8_t number);  // in the room, nothing heard yet
    void OnSample(std::uint8_t number, const std::optional<std::string>& scene,
                  const std::optional<Vec3>& position, std::uint64_t now_ms);
    void Forget(std::uint8_t number);
    void Clear();  // a drop: nobody is known

    Tier TierAt(std::uint64_t now_ms) const;
    Rates RatesAt(std::uint64_t now_ms) const { return RatesOf(TierAt(now_ms)); }
    // Whether another player is in our place (or might be: anything not
    // known counts as yes).
    bool AnyoneHere(std::uint64_t now_ms) const;
    // The nearest other player in our place, in metres, when every one is
    // known (for the log).
    std::optional<float> Nearest(std::uint64_t now_ms) const;

private:
    struct Remote {
        bool present = false;
        std::optional<std::string> scene;
        std::optional<Vec3> position;
        std::uint64_t heard_ms = 0;  // 0: never
    };
    // The nearest distance in our place; nothing when nobody is here; a
    // negative distance when someone might be here (not known).
    std::optional<float> NearestLocked(std::uint64_t now_ms) const;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    std::uint8_t self_ = 0;
    std::optional<std::string> scene_;
    std::optional<Vec3> position_;
    std::array<Remote, coop::protocol::kMaxPlayers + 1> remotes_{};
};

// The one the hook uses.
Interest& The();

// Network thread: the other players' PLAYER_SAMPLE, who is in the room, a drop.
void OnEvent(const coop::net::Event& event, std::uint64_t now_ms);

}  // namespace CoopInterest
