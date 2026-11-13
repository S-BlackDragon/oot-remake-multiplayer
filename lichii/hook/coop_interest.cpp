// SPDX-License-Identifier: GPL-3.0-or-later
// Whether our Link's pose goes, from where the others stand (coop_interest.h).
// No emulator header, so the unit tests build it.

#include "coop_interest.h"

#include <cmath>
#include <variant>

#include "coop/host/scene.hpp"
#include "coop/net/client.hpp"

namespace CoopInterest {

namespace proto = coop::protocol;

std::string_view ToString(Tier tier) {
    switch (tier) {
    case Tier::here:
        return "here";
    case Tier::alone:
        return "alone";
    }
    return "?";
}

Rates RatesOf(Tier tier) {
    if (tier == Tier::alone) return {std::nullopt};
    return {kPoseGapMs};
}

void Interest::SetEnabled(bool on) {
    std::lock_guard lock(mutex_);
    enabled_ = on;
}

bool Interest::enabled() const {
    std::lock_guard lock(mutex_);
    return enabled_;
}

void Interest::SetLocal(std::optional<std::string_view> scene, std::optional<Vec3> position) {
    std::lock_guard lock(mutex_);
    if (scene && !scene->empty()) {
        if (!scene_ || *scene_ != *scene) position_.reset();  // a new place: the old spot is not ours
        scene_ = std::string(*scene);
    }
    if (position) position_ = position;
}

void Interest::SetSelf(std::uint8_t number) {
    std::lock_guard lock(mutex_);
    self_ = number;
    if (number < remotes_.size()) remotes_[number] = Remote{};
}

void Interest::Joined(std::uint8_t number) {
    std::lock_guard lock(mutex_);
    if (number == 0 || number >= remotes_.size() || number == self_) return;
    remotes_[number].present = true;
}

void Interest::OnSample(std::uint8_t number, const std::optional<std::string>& scene,
                        const std::optional<Vec3>& position, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (number == 0 || number >= remotes_.size() || number == self_) return;
    Remote& r = remotes_[number];
    r.present = true;
    if (scene && !scene->empty()) {
        if (r.scene && *r.scene != *scene) r.position.reset();
        r.scene = *scene;
    }
    if (position && std::isfinite((*position)[0]) && std::isfinite((*position)[1]) &&
        std::isfinite((*position)[2])) {
        r.position = position;
    }
    r.heard_ms = now_ms == 0 ? 1 : now_ms;
}

void Interest::Forget(std::uint8_t number) {
    std::lock_guard lock(mutex_);
    if (number < remotes_.size()) remotes_[number] = Remote{};
}

void Interest::Clear() {
    std::lock_guard lock(mutex_);
    remotes_.fill(Remote{});
}

std::optional<float> Interest::NearestLocked(std::uint64_t now_ms) const {
    std::optional<float> nearest;
    for (std::size_t n = 1; n < remotes_.size(); ++n) {
        const Remote& r = remotes_[n];
        if (!r.present || n == self_) continue;
        const bool stale = r.heard_ms == 0 || now_ms < r.heard_ms || now_ms - r.heard_ms > kStaleMs;
        if (stale || !r.scene || !scene_) return -1.0f;  // not known: as here
        if (!coop::host::same_place(*r.scene, *scene_)) continue;
        if (!r.position || !position_) return -1.0f;
        const float dx = (*r.position)[0] - (*position_)[0];
        const float dy = (*r.position)[1] - (*position_)[1];
        const float dz = (*r.position)[2] - (*position_)[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!nearest || d < *nearest) nearest = d;
    }
    return nearest;
}

Tier Interest::TierAt(std::uint64_t now_ms) const {
    std::lock_guard lock(mutex_);
    if (!enabled_) return Tier::here;
    return NearestLocked(now_ms) ? Tier::here : Tier::alone;
}

bool Interest::AnyoneHere(std::uint64_t now_ms) const {
    std::lock_guard lock(mutex_);
    if (!enabled_) return true;
    return NearestLocked(now_ms).has_value();
}

std::optional<float> Interest::Nearest(std::uint64_t now_ms) const {
    std::lock_guard lock(mutex_);
    const std::optional<float> d = NearestLocked(now_ms);
    if (!d || *d < 0.0f) return std::nullopt;
    return d;
}

Interest& The() {
    static Interest interest;
    return interest;
}

void OnEvent(const coop::net::Event& event, std::uint64_t now_ms) {
    using coop::net::EventKind;
    Interest& in = The();
    switch (event.kind) {
    case EventKind::welcomed:
    case EventKind::reconnected:
        in.Clear();
        in.SetSelf(event.welcome.player_number);
        for (const proto::PlayerInfo& p : event.welcome.players) in.Joined(p.number);
        break;
    case EventKind::disconnected:
        in.Clear();
        break;
    case EventKind::player_joined:
        in.Joined(event.player);
        break;
    case EventKind::player_left:
        in.Forget(event.player);
        break;
    case EventKind::message:
        if (const auto* s = std::get_if<proto::PlayerSample>(&event.message)) {
            in.OnSample(s->player_number, s->scene, s->position, now_ms);
        }
        break;
    default:
        break;
    }
}

}  // namespace CoopInterest
