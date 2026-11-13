// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM golpes (oot-remake docs/GOLPES_LATENCIA.md, "Posición del enemigo"): where an enemy copy
// we do not own is drawn, from its owner's OBJECT_SAMPLEs, with the puppets' smoother
// (coop/host/pose_smooth.hpp, the same 100 ms render delay and 150 ms of bounded extrapolation
// Sync::PlaceAt had). What it adds over PlaceAt for an enemy: a late sample after an
// extrapolation, or the stream coming back after a gap (an enemy that stood still for a second
// and starts to move), is eased over 100 ms instead of drawn as a jump or a slow early slide; a
// teleport (a Poe vanishing, a Wallmaster) snaps instead of sliding across the room; the render
// clock never goes back; samples are ordered by their server time and sequence. One smoother per
// copy, keyed by its actor address; reset when its owner changes (the new owner's samples alone
// place it, as before).
//
// Plain data, present thread (Sync's Tick and Poses). Switch: LICHII_COOP_COPY_SMOOTH (on;
// =0 copies are placed by Sync::PlaceAt as before). Test: tests/lichii/hits_test.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

#include "coop/host/pose_smooth.hpp"
#include "coop/protocol/messages.hpp"

namespace CoopCopySmooth {

// The puppets' settings with Sync's numbers for copies: kBehindUs, kExtrapolateUs,
// kKeepSamples. An owner sends a still enemy once a second, so samples further apart than
// max_gap_us are not joined: a still one stays still until it moves.
coop::host::SmoothSettings Defaults();

class Streams {
public:
    explicit Streams(coop::host::SmoothSettings settings = Defaults()) : settings_(settings) {}

    // One of the owner's samples for the copy at `address`. False when the smoother did not
    // keep it (a duplicate, too old, not finite).
    bool Push(std::uint64_t address, const coop::protocol::SampleTime& time,
              const coop::protocol::Vec3& position, std::optional<coop::protocol::Vec3> velocity,
              std::optional<coop::protocol::Quat> rotation);
    // Where to draw it at server time `now_us` (the delay is taken off inside). Nothing before
    // its first sample.
    std::optional<coop::host::SmoothedPose> At(std::uint64_t address, std::uint64_t now_us);
    // Another owner: its old samples go, the render clock starts again.
    void Reset(std::uint64_t address);
    void Forget(std::uint64_t address) { streams_.erase(address); }
    void Clear() { streams_.clear(); }
    // Copies no longer loaded go.
    void Expire(const std::function<bool(std::uint64_t)>& loaded);
    std::size_t Size() const { return streams_.size(); }

    // Summed over every copy since the last ask, for the 5 s log line.
    coop::host::PoseSmoother::Stats TakeStats();

private:
    coop::host::SmoothSettings settings_;
    std::map<std::uint64_t, coop::host::PoseSmoother> streams_;
    coop::host::PoseSmoother::Stats gone_{};  // of streams forgotten since the last ask
};

}  // namespace CoopCopySmooth
