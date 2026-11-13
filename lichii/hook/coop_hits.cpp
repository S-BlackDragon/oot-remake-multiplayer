// SPDX-License-Identifier: GPL-3.0-or-later
// ORM golpes: what a hit on another game's enemy shows on the hitter's screen while the owner's
// answer is on its way (coop_hits.h, oot-remake docs/GOLPES_LATENCIA.md).
#include "coop_hits.h"

#include <algorithm>
#include <cmath>

namespace CoopHits {

namespace {

bool Finite(const Vec3& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float Distance(const Vec3& a, const Vec3& b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// Normalised linear blend, the short way round (as coop_objects_sync.cpp).
Quat Nlerp(const Quat& a, Quat b, float t) {
    float dot = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        dot += a[i] * b[i];
    }
    if (dot < 0.0f) {
        for (float& v : b) {
            v = -v;
        }
    }
    Quat q{};
    float len = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = a[i] + (b[i] - a[i]) * t;
        len += q[i] * q[i];
    }
    len = std::sqrt(len);
    if (!(len > 1e-6f)) {
        return a;
    }
    for (float& v : q) {
        v /= len;
    }
    return q;
}

// A word of `name` (split at '_') equal to `word`.
bool HasWord(std::string_view name, std::string_view word) {
    for (std::size_t at = name.find(word); at != std::string_view::npos; at = name.find(word, at + 1)) {
        const bool starts = at == 0 || name[at - 1] == '_';
        const std::size_t end = at + word.size();
        const bool ends = end == name.size() || name[end] == '_' || (name[end] >= '0' && name[end] <= '9');
        if (starts && ends) {
            return true;
        }
    }
    return false;
}

std::string_view WithoutLod(std::string_view name) {
    for (const std::string_view suffix : {"_ModelOnlyLOD", "_LOD"}) {
        if (name.size() > suffix.size() && name.ends_with(suffix)) {
            return name.substr(0, name.size() - suffix.size());
        }
    }
    return name;
}

}  // namespace

bool IsHitReaction(std::string_view name) {
    const std::string_view base = WithoutLod(name);
    if (base.empty()) {
        return false;
    }
    for (const std::string_view death : {"Die", "Dead", "Death"}) {
        if (HasWord(base, death)) {
            return false;  // CoopObjects::IsDeathAnimation's words: a death, not a flinch
        }
    }
    for (const std::string_view word : {"Damage", "Dmg", "Hit", "Flinch", "Knockback", "KnockBack",
                                        "Blow", "Stagger", "Recoil", "Hurt"}) {
        if (HasWord(base, word)) {
            return true;
        }
    }
    return false;
}

const char* ToString(Phase phase) {
    switch (phase) {
    case Phase::follow:
        return "follow";
    case Phase::free:
        return "free";
    case Phase::hold:
        return "hold";
    case Phase::blend:
        return "blend";
    case Phase::dead:
        return "dead";
    }
    return "?";
}

void Latency::Note(std::uint64_t sample_us, std::uint64_t now_us) {
    const std::uint64_t d = std::min(now_us > sample_us ? now_us - sample_us : 0, kMostUs);
    const double v = static_cast<double>(d);
    if (!known_) {
        value_ = v;
        known_ = true;
    } else if (v > value_) {
        value_ += (v - value_) / 4.0;  // a spike shows within a few samples
    } else {
        value_ += (v - value_) / 16.0;  // and goes slowly
    }
}

std::uint64_t Latency::OneWayUs() const {
    return known_ ? static_cast<std::uint64_t>(value_) : kDefaultUs;
}

std::uint64_t Feedback::AnswerUs() const {
    return 2 * latency_.OneWayUs() + settings_.owner_react_us + settings_.behind_us;
}

std::uint64_t Feedback::WorstAnswerUs() const {
    return 2 * latency_.OneWayUs() + settings_.owner_apply_us + settings_.behind_us + settings_.margin_us;
}

std::uint64_t Feedback::WindowUs() const {
    return latency_.OneWayUs() + settings_.owner_apply_us + settings_.margin_us;
}

std::uint64_t Feedback::ReactEnd(const Entry& e) const {
    const std::uint64_t span = std::clamp<std::uint64_t>(WorstAnswerUs() + settings_.reaction_us,
                                                         settings_.react_min_us, settings_.react_most_us);
    return e.hit_us + span;
}

void Feedback::OurHit(std::uint64_t address, std::uint64_t now_us, std::int32_t life, const Vec3& at,
                      std::optional<Quat> rotation, std::string over) {
    const auto [it, added] = entries_.try_emplace(address);
    Entry& e = it->second;
    const bool kill = life <= 0;
    ++stats_.hits;
    if (kill) {
        ++stats_.kills;
    }
    if (!added && e.phase == Phase::dead && kill) {
        e.hit_us = now_us;  // hit again while dying here: nothing new to show
        return;
    }
    // The owner's animation from before our first hit stays the one kept off while a reaction
    // of ours plays: a second hit in that time finds the copy in our own flinch already.
    const bool keep_over = !added && Active(address, now_us) && (over.empty() || IsHitReaction(over));
    if (added || !keep_over) {
        e.over = std::move(over);
    }
    e.hit_us = now_us;
    e.life = life;
    e.kill = kill;
    e.rejected = false;
    e.answered_sample_us = 0;
    e.moved_on = false;
    e.phase = Phase::free;
    e.hold = Finite(at) ? at : Vec3{};
    e.hold_rotation = rotation;
    e.hold_since = 0;
    e.blend_since = 0;
}

void Feedback::Sent(std::uint64_t address, std::uint64_t now_us) {
    if (const auto it = entries_.find(address); it != entries_.end()) {
        it->second.sent_us = now_us;
    }
}

void Feedback::OwnerLife(std::uint64_t address, std::uint64_t sample_us, std::int32_t life) {
    const auto it = entries_.find(address);
    if (it == entries_.end()) {
        return;
    }
    Entry& e = it->second;
    if (e.answered_sample_us != 0 || e.rejected || sample_us < e.hit_us || life > e.life) {
        return;
    }
    // Taken after our hit and as low as ours (or lower: another hit too): the owner has it.
    e.answered_sample_us = std::max<std::uint64_t>(sample_us, 1);
    ++stats_.answered;
    stats_.answer_sum_us += sample_us - e.hit_us;
}

void Feedback::Rejected(std::uint64_t address, std::uint64_t now_us) {
    const auto it = entries_.find(address);
    if (it == entries_.end() || it->second.rejected) {
        return;
    }
    (void)now_us;
    it->second.rejected = true;
    ++stats_.rejected;
}

bool Feedback::Before(std::uint64_t address, std::uint64_t sample_us, std::uint64_t now_us) const {
    const auto it = entries_.find(address);
    if (it == entries_.end() || it->second.sent_us == 0) {
        return false;
    }
    const Entry& e = it->second;
    if (now_us >= e.sent_us + settings_.window_most_us) {
        return false;  // an owner that never answers is not waited for longer
    }
    return sample_us < e.sent_us + WindowUs();
}

void Feedback::Advance(Entry& e, std::uint64_t now_us, const Vec3& target, const Vec3& here,
                       std::optional<Quat> here_rotation) {
    const Vec3 at = Finite(here) ? here : e.hold;
    // A few steps at most: one call may pass more than one boundary (a late frame).
    for (int step = 0; step < 4; ++step) {
        if (e.rejected && (e.phase == Phase::free || e.phase == Phase::dead || e.phase == Phase::hold)) {
            if (e.phase != Phase::hold) {
                e.hold = at;
                e.hold_rotation = here_rotation;
            }
            e.phase = Phase::blend;
            e.blend_since = now_us;
            continue;
        }
        switch (e.phase) {
        case Phase::free:
            if (e.kill) {
                if (e.answered_sample_us != 0) {
                    e.phase = Phase::dead;  // the owner's enemy died too: ours dies here
                    continue;
                }
                if (now_us >= e.hit_us + settings_.kill_most_us) {
                    e.hold = at;
                    e.hold_rotation = here_rotation;
                    e.phase = Phase::blend;
                    e.blend_since = now_us;
                    ++stats_.timed_out;
                    continue;
                }
                return;
            }
            if (now_us >= e.hit_us + settings_.free_us) {
                e.hold = at;  // where our game's knockback left it
                e.hold_rotation = here_rotation;
                e.hold_since = now_us;
                e.phase = Phase::hold;
                continue;
            }
            return;
        case Phase::hold: {
            const std::uint64_t shown = now_us > settings_.behind_us ? now_us - settings_.behind_us : 0;
            const bool answered = e.answered_sample_us != 0 && shown >= e.answered_sample_us;
            // The owner's knockback, now in its stream, has come to ours (or had its time).
            const bool met = answered && (!Finite(target) || Distance(target, e.hold) <= settings_.converge_m ||
                                          shown >= e.answered_sample_us + settings_.converge_most_us);
            const bool late = now_us >= e.hit_us + WorstAnswerUs() + (answered ? settings_.converge_most_us : 0) ||
                              now_us >= e.hold_since + settings_.hold_most_us;
            if (met || late) {
                if (!answered) {
                    ++stats_.timed_out;
                }
                e.phase = Phase::blend;
                e.blend_since = now_us;
                continue;
            }
            return;
        }
        case Phase::blend:
            if (now_us >= e.blend_since + settings_.blend_us) {
                e.phase = Phase::follow;
                continue;
            }
            return;
        case Phase::dead:
        case Phase::follow:
            return;
        }
    }
}

PoseOut Feedback::Pose(std::uint64_t address, std::uint64_t now_us, const Vec3& target,
                       std::optional<Quat> target_rotation, const Vec3& here,
                       std::optional<Quat> here_rotation) {
    PoseOut out{true, target, target_rotation};
    const auto it = entries_.find(address);
    if (it == entries_.end()) {
        return out;
    }
    Entry& e = it->second;
    Advance(e, now_us, target, here, here_rotation);
    switch (e.phase) {
    case Phase::free:
    case Phase::dead:
        out.write = false;
        return out;
    case Phase::hold:
        out.position = e.hold;
        out.rotation = e.hold_rotation ? e.hold_rotation : target_rotation;
        return out;
    case Phase::blend: {
        const float f = settings_.blend_us == 0
                            ? 1.0f
                            : std::clamp(static_cast<float>(now_us - e.blend_since) /
                                             static_cast<float>(settings_.blend_us),
                                         0.0f, 1.0f);
        const float k = f * f * (3.0f - 2.0f * f);  // smooth at both ends
        out.position = Lerp(e.hold, target, k);
        if (e.hold_rotation && target_rotation) {
            out.rotation = Nlerp(*e.hold_rotation, *target_rotation, k);
        }
        return out;
    }
    case Phase::follow:
        return out;
    }
    return out;
}

bool Feedback::SkipPlay(std::uint64_t address, std::uint64_t now_us, std::string_view name,
                        std::uint64_t sample_us) {
    const auto it = entries_.find(address);
    if (it == entries_.end()) {
        return false;
    }
    Entry& e = it->second;
    if (e.rejected) {
        return false;  // the owner did not take our hit: its stream rules again
    }
    if (e.phase == Phase::dead || (e.kill && e.phase == Phase::free)) {
        ++stats_.plays_skipped;  // our game plays its death; the owner's walk is not put over it
        return true;
    }
    if (now_us >= ReactEnd(e)) {
        return false;
    }
    // Taken before the owner could have heard of our hit: from before the hit.
    if (sample_us < e.hit_us + latency_.OneWayUs() + settings_.owner_react_us) {
        ++stats_.plays_skipped;
        return true;
    }
    if (e.answered_sample_us != 0 && sample_us >= e.answered_sample_us) {
        // The owner's answer: its own reaction is the one our game already showed.
        if (!e.moved_on && IsHitReaction(name)) {
            ++stats_.plays_skipped;
            return true;
        }
        e.moved_on = true;  // anything after it (a counterattack, a later hit) plays as usual
        return false;
    }
    if (name == e.over || IsHitReaction(name)) {
        ++stats_.plays_skipped;
        return true;
    }
    return false;
}

bool Feedback::Active(std::uint64_t address, std::uint64_t now_us) const {
    const auto it = entries_.find(address);
    if (it == entries_.end()) {
        return false;
    }
    const Entry& e = it->second;
    if (e.rejected) {
        return e.phase != Phase::follow;
    }
    return e.phase != Phase::follow || now_us < ReactEnd(e);
}

std::optional<Phase> Feedback::PhaseOf(std::uint64_t address) const {
    const auto it = entries_.find(address);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second.phase;
}

void Feedback::Expire(std::uint64_t now_us, const std::function<bool(std::uint64_t)>& loaded) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        const Entry& e = it->second;
        const bool done = e.phase == Phase::follow && now_us >= ReactEnd(e) &&
                          now_us >= e.sent_us + settings_.window_most_us;
        if (done || (loaded && !loaded(it->first))) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

Feedback::Stats Feedback::TakeStats() {
    return std::exchange(stats_, Stats{});
}

std::optional<std::uint8_t> OwnerLedger::Near(const Vec3& at, std::optional<Vec3> self_at,
                                              const std::vector<std::pair<std::uint8_t, Vec3>>& others) {
    if (!Finite(at) || !self_at || !Finite(*self_at) || Distance(at, *self_at) <= kReachM) {
        return std::nullopt;  // our Link could have done it (or we cannot tell)
    }
    std::optional<std::uint8_t> found;
    for (const auto& [player, place] : others) {
        if (!Finite(place) || Distance(at, place) > kReachM) {
            continue;
        }
        if (found && *found != player) {
            return std::nullopt;  // two could have: not told apart
        }
        found = player;
    }
    return found;
}

std::int32_t OwnerLedger::Match(std::vector<Part>& parts, std::uint64_t now_us, std::uint8_t player,
                                std::int32_t amount) {
    std::erase_if(parts, [now_us](const Part& p) { return p.until <= now_us || p.amount <= 0; });
    std::int32_t taken = 0;
    for (Part& p : parts) {
        if (p.player != player || taken >= amount) {
            continue;
        }
        const std::int32_t take = std::min(p.amount, amount - taken);
        p.amount -= take;
        taken += take;
    }
    std::erase_if(parts, [](const Part& p) { return p.amount <= 0; });
    return taken;
}

std::int32_t OwnerLedger::Drop(std::uint64_t address, std::uint64_t now_us, std::int32_t amount,
                               std::uint8_t player) {
    if (amount <= 0 || player == 0) {
        return 0;
    }
    Entry& e = entries_[address];
    const std::int32_t matched = Match(e.damages, now_us, player, amount);
    if (matched > 0) {
        ++pairs_;
        if (put_back_) {
            taken_back_ += static_cast<std::uint32_t>(matched);
        }
    }
    if (amount > matched) {
        e.drops.push_back({player, amount - matched, now_us + kWindowUs});
    }
    return put_back_ ? matched : 0;
}

std::int32_t OwnerLedger::Damage(std::uint64_t address, std::uint64_t now_us, std::int32_t amount,
                                 std::uint8_t player) {
    if (amount <= 0 || player == 0) {
        return amount;
    }
    Entry& e = entries_[address];
    const std::int32_t matched = Match(e.drops, now_us, player, amount);
    if (matched > 0) {
        ++pairs_;
        if (put_back_) {
            taken_back_ += static_cast<std::uint32_t>(matched);
        }
    }
    if (amount > matched) {
        e.damages.push_back({player, amount - matched, now_us + kWindowUs});
    }
    return put_back_ ? amount - matched : amount;
}

void OwnerLedger::Expire(std::uint64_t now_us, const std::function<bool(std::uint64_t)>& loaded) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        Entry& e = it->second;
        std::erase_if(e.drops, [now_us](const Part& p) { return p.until <= now_us; });
        std::erase_if(e.damages, [now_us](const Part& p) { return p.until <= now_us; });
        if ((e.drops.empty() && e.damages.empty()) || (loaded && !loaded(it->first))) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace CoopHits
