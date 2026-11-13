// SPDX-License-Identifier: GPL-3.0-or-later
// ORM golpes: enemy copies drawn with the puppets' smoother (coop_copy_smooth.h).
#include "coop_copy_smooth.h"

#include <algorithm>
#include <utility>

namespace CoopCopySmooth {

namespace {

void Add(coop::host::PoseSmoother::Stats& to, const coop::host::PoseSmoother::Stats& s) {
    to.buffered += s.buffered;
    to.reordered += s.reordered;
    to.late += s.late;
    to.duplicate += s.duplicate;
    to.too_old += s.too_old;
    to.invalid += s.invalid;
    to.cuts += s.cuts;
    to.frames += s.frames;
    to.extrapolated += s.extrapolated;
    to.held += s.held;
    to.corrections += s.corrections;
    to.snaps += s.snaps;
    to.max_ahead_us = std::max(to.max_ahead_us, s.max_ahead_us);
}

}  // namespace

coop::host::SmoothSettings Defaults() {
    coop::host::SmoothSettings s;
    s.delay_us = 100'000;        // Sync::kBehindUs
    s.extrapolate_us = 150'000;  // Sync::kExtrapolateUs
    s.max_gap_us = 500'000;      // an owner sends 20 a second while it moves, 1 while still
    s.snap_distance = 2.0f;
    s.max_speed = 40.0f;
    s.correct_us = 100'000;
    s.clock_back_us = 1'000'000;
    s.hermite = true;
    s.capacity = 16;  // Sync::kKeepSamples
    return s;
}

bool Streams::Push(std::uint64_t address, const coop::protocol::SampleTime& time,
                   const coop::protocol::Vec3& position, std::optional<coop::protocol::Vec3> velocity,
                   std::optional<coop::protocol::Quat> rotation) {
    auto it = streams_.find(address);
    if (it == streams_.end()) {
        it = streams_.emplace(address, coop::host::PoseSmoother(settings_)).first;
    }
    coop::host::SmoothSample s;
    s.time = time;
    s.position = position;
    s.velocity = velocity;
    s.rotation = rotation;
    const coop::host::SmoothPush r = it->second.push(s);
    return r == coop::host::SmoothPush::buffered || r == coop::host::SmoothPush::cut;
}

std::optional<coop::host::SmoothedPose> Streams::At(std::uint64_t address, std::uint64_t now_us) {
    const auto it = streams_.find(address);
    if (it == streams_.end() || it->second.size() == 0) {
        return std::nullopt;
    }
    return it->second.at(now_us);
}

void Streams::Reset(std::uint64_t address) {
    if (const auto it = streams_.find(address); it != streams_.end()) {
        Add(gone_, it->second.take_stats());
        it->second.reset();
    }
}

void Streams::Expire(const std::function<bool(std::uint64_t)>& loaded) {
    for (auto it = streams_.begin(); it != streams_.end();) {
        if (loaded && !loaded(it->first)) {
            Add(gone_, it->second.take_stats());
            it = streams_.erase(it);
        } else {
            ++it;
        }
    }
}

coop::host::PoseSmoother::Stats Streams::TakeStats() {
    coop::host::PoseSmoother::Stats out = std::exchange(gone_, {});
    for (auto& [address, smoother] : streams_) {
        Add(out, smoother.take_stats());
    }
    return out;
}

}  // namespace CoopCopySmooth
