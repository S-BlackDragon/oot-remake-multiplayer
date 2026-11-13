// SPDX-License-Identifier: GPL-3.0-or-later
// The townsperson pose copy (coop_npc_pose.h). No emulator header, so the unit
// tests build it.
#include "coop_npc_pose.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace CoopNpcPose {

namespace proto = coop::protocol;

namespace {

// Whether `actor` is still a live actor, as the Mover's Alive checks it
// (coop_objects.cpp): state 4, and its actor-list node points back at it. The
// source and the targets come from the last walk; by this calc frame the game
// may have deleted that townsperson, and its freed memory still holds the
// skeleton chain (review 2026-11-11, oot-remake docs/REVISION_OBJETOS.md).
constexpr std::uint64_t kActorNode = 0x538;
constexpr std::uint64_t kNodeOwner = 0x10;

bool Live(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint32_t state = 0;
    std::uint64_t owner = 0;
    return actor != 0 && guest.Read(actor + CoopPuppet::addr::kActorState, &state, sizeof(state)) &&
           state == CoopPuppet::addr::kStatePresent &&
           guest.Read(actor + kActorNode + kNodeOwner, &owner, sizeof(owner)) && owner == actor;
}

constexpr std::size_t kPartBones = proto::kObjectPosePartBones;

// The parts a skeleton of `count` bones is sent in.
std::size_t PartsFor(std::size_t count) {
    return (count + kPartBones - 1) / kPartBones;
}

template <std::size_t N>
bool Close(const std::optional<std::array<std::int16_t, N>>& a,
           const std::optional<std::array<std::int16_t, N>>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    for (std::size_t i = 0; i < N; ++i) {
        if (std::abs(int{(*a)[i]} - int{(*b)[i]}) > kSameUnits) return false;
    }
    return true;
}

// Whether `now` is the bone last sent as far as a copy can see: the same
// parts and flags, each packed value within kSameUnits.
bool Same(const proto::PoseBone& sent, const proto::PoseBone& now) {
    return sent.bone == now.bone && sent.flags == now.flags && Close(sent.rotation, now.rotation) &&
           Close(sent.translation, now.translation) && Close(sent.scale, now.scale);
}

}  // namespace

void NpcPose::ResetSent() {
    outbox_.reset();
    sent_ms_ = 0;
    next_ms_ = 0;
    sent_skeleton_ = 0;
    sent_bones_.clear();
    repeats_.clear();
    refreshed_ms_.fill(0);
}

void NpcPose::SetSource(std::optional<Source> source) {
    std::lock_guard lock(mutex_);
    if (source == source_) return;
    // Another townsperson (or none): its first read starts over, and its
    // first pose goes out at once.
    source_ = std::move(source);
    reader_.Reset();
    ResetSent();
}

std::optional<Source> NpcPose::source() const {
    std::lock_guard lock(mutex_);
    return source_;
}

void NpcPose::SetTargets(std::vector<Target> targets) {
    std::lock_guard lock(mutex_);
    targets_ = std::move(targets);
    std::erase_if(probes_, [&](const auto& kv) {
        return std::none_of(targets_.begin(), targets_.end(),
                            [&](const Target& t) { return t.actor == kv.first; });
    });
}

std::vector<Heard> NpcPose::Fresh(std::uint64_t now_ms) const {
    std::lock_guard lock(mutex_);
    std::vector<Heard> out;
    for (const auto& [id, t] : tracks_) {
        if (t.shown_ms != 0 && now_ms - std::min(now_ms, t.shown_ms) < kFreshMs) {
            out.push_back({id, t.player, t.epoch});
        }
    }
    return out;
}

void NpcPose::OnCalcFrame(CoopPuppet::Guest& guest, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (source_) {
        std::string why;
        if (!Live(guest, source_->actor)) {
            ++counts_.not_read;
            ++counts_.gone;
        } else if (auto pose = reader_.Read(guest, source_->actor, kMaxBones, &why)) {
            counts_.bones = static_cast<std::uint32_t>(pose->bones.size());
            outbox_ = std::move(pose);
            ++outbox_serial_;
            ++counts_.read;
        } else {
            ++counts_.not_read;
        }
    }
    for (const Target& t : targets_) {
        const auto track = tracks_.find(t.id);
        if (track == tracks_.end()) continue;
        const auto pose = track->second.track.At(now_ms);
        if (!pose) continue;
        if (!Live(guest, t.actor)) {
            ++counts_.gone;  // deleted since the walk: not written into freed memory
            continue;
        }
        std::optional<bool> kept;
        switch (CoopPose::WriteActorPose(guest, t.actor, *pose, &probes_[t.actor], &kept,
                                         t.weight)) {
        case CoopPose::WriteResult::written:
            ++counts_.written;
            if (kept) ++(*kept ? counts_.kept : counts_.overwritten);
            break;
        case CoopPose::WriteResult::other_skeleton:
            ++counts_.other_skeleton;
            break;
        case CoopPose::WriteResult::no_skeleton:
        case CoopPose::WriteResult::failed:
            ++counts_.no_skeleton;
            break;
        }
    }
}

std::vector<proto::ObjectPose> NpcPose::TakeOutgoing(std::uint64_t server_now_us,
                                                     std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    std::vector<proto::ObjectPose> out;
    if (!source_ || !outbox_ || outbox_serial_ == sent_serial_) return out;
    if (sent_ms_ != 0 && now_ms < next_ms_) return out;
    sent_serial_ = outbox_serial_;
    const std::vector<proto::PoseBone> bones = CoopPose::EncodeBones(*outbox_);
    // The reader lists bone i at index i, every bone of the skeleton.
    const std::size_t count = std::min<std::size_t>(bones.size(), kMaxBones);
    if (outbox_->skeleton != sent_skeleton_ || sent_bones_.size() != count) {
        const std::uint32_t skeleton = outbox_->skeleton;
        auto keep = std::move(outbox_);
        ResetSent();  // another skeleton: every bone goes again, at once
        outbox_ = std::move(keep);
        sent_skeleton_ = skeleton;
        sent_bones_.assign(count, std::nullopt);
        repeats_.assign(count, 0);
    }
    const std::size_t part_count = PartsFor(count);
    std::array<bool, proto::kMaxObjectPoseParts> refresh{};
    for (std::size_t p = 0; p < part_count; ++p) {
        refresh[p] = refreshed_ms_[p] == 0 || now_ms - std::min(now_ms, refreshed_ms_[p]) >= kRefreshMs;
    }
    std::array<std::vector<proto::PoseBone>, proto::kMaxObjectPoseParts> by_part;
    std::size_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const proto::PoseBone& b = bones[i];
        if (b.bone != i) continue;  // not as the reader lists them: never sent
        const std::size_t p = i / kPartBones;
        std::optional<proto::PoseBone>& last = sent_bones_[i];
        bool go = refresh[p];
        if (!last) {
            go = true;  // its first: the refresh covers a loss
        } else if (!Same(*last, b)) {
            go = true;
            repeats_[i] = kRepeats;  // and in the next kRepeats poses
        } else if (repeats_[i] > 0) {
            go = true;
            --repeats_[i];
        }
        if (!go) continue;
        last = b;
        by_part[p].push_back(b);
        ++total;
    }
    if (total == 0) return out;  // it stands still: again only at the refresh
    std::uint8_t mask = 0;
    for (std::size_t p = 0; p < part_count; ++p) {
        if (!by_part[p].empty()) mask = static_cast<std::uint8_t>(mask | (1u << p));
    }
    const proto::SampleTime time{server_now_us, ++sequence_};
    for (std::size_t p = 0; p < part_count; ++p) {
        if (refresh[p]) refreshed_ms_[p] = std::max<std::uint64_t>(now_ms, 1);
        if (by_part[p].empty()) continue;
        proto::ObjectPose m;
        m.id = source_->id;
        m.epoch = source_->epoch;
        m.time = time;
        m.skeleton = outbox_->skeleton;
        m.bones = std::move(by_part[p]);
        m.part = static_cast<std::uint8_t>(p);
        m.parts = mask;
        out.push_back(std::move(m));
    }
    sent_ms_ = std::max<std::uint64_t>(now_ms, 1);
    const std::uint64_t gap = std::max<std::uint64_t>(1000 / kSendHz, total * 1000 / kBonesPerSecond);
    next_ms_ = sent_ms_ + gap;
    ++counts_.sent;
    counts_.sent_parts += static_cast<unsigned>(out.size());
    counts_.sent_bones += static_cast<unsigned>(total);
    return out;
}

void NpcPose::Receive(const proto::ObjectPose& m, std::uint64_t now_ms) {
    if (m.player_number == 0) return;  // only a relayed one names its owner
    std::lock_guard lock(mutex_);
    // The part against its skeleton: the codec checked the part's range and
    // mask; the bone count is the skeleton id's (CoopPose::SkeletonId).
    const std::size_t count = CoopPose::BoneCountOf(m.skeleton);
    const std::size_t part_count = PartsFor(count);
    if (count == 0 || m.part >= part_count || (m.parts >> part_count) != 0 ||
        std::any_of(m.bones.begin(), m.bones.end(),
                    [&](const proto::PoseBone& b) { return b.bone >= count; })) {
        ++counts_.bad;
        return;
    }
    // Tracks not heard from for kForgetMs go first: one left at an old epoch
    // (a restarted server hands out epochs from 0 again) would otherwise
    // refuse every new pose of its object as stale, and never be dropped.
    const auto forget = [&] {
        std::erase_if(tracks_, [&](const auto& kv) {
            return now_ms - std::min(now_ms, kv.second.heard_ms) > kForgetMs;
        });
    };
    forget();
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << m.part);
    bool new_pose = true;
    auto it = tracks_.find(m.id);
    if (it != tracks_.end()) {
        TrackOf& t = it->second;
        const auto ahead = static_cast<std::int16_t>(static_cast<std::uint16_t>(m.epoch - t.epoch));
        const bool same = m.time.server_time_us == t.time_us && m.time.sequence == t.sequence;
        const bool newer = m.time.server_time_us > t.time_us ||
                           (m.time.server_time_us == t.time_us && m.time.sequence > t.sequence);
        if (ahead < 0) {
            ++counts_.stale;
            return;
        }
        if (ahead == 0 && m.player_number == t.player) {
            if (same && (m.parts != t.parts || (t.got & bit) != 0)) {
                ++counts_.stale;  // that part again (or a mask that does not match)
                return;
            }
            if (!same && !newer) {
                ++counts_.stale;
                return;
            }
            new_pose = !same;
        } else {
            t = TrackOf{};  // a new owner: its poses do not blend with the old one's
        }
    } else {
        it = tracks_.emplace(m.id, TrackOf{}).first;
    }
    TrackOf& t = it->second;
    if (new_pose) {
        if (t.parts != 0 && !t.shown) ++counts_.incomplete;
        t.time_us = m.time.server_time_us;
        t.sequence = m.time.sequence;
        t.parts = m.parts;
        t.got = 0;
        t.shown = false;
    }
    if (m.skeleton != t.skeleton || t.bones.size() != count) {
        t.skeleton = m.skeleton;  // another skeleton: none of its old bones
        t.bones.assign(count, std::nullopt);
    }
    t.player = m.player_number;
    t.epoch = m.epoch;
    t.heard_ms = std::max<std::uint64_t>(now_ms, 1);
    t.got = static_cast<std::uint8_t>(t.got | bit);
    for (const proto::PoseBone& b : m.bones) t.bones[b.bone] = b;
    if (t.got == t.parts) {
        std::vector<proto::PoseBone> kept;
        kept.reserve(t.bones.size());
        for (const auto& b : t.bones) {
            if (b) kept.push_back(*b);
        }
        t.track.Push(CoopPose::DecodeBones(t.skeleton, kept), now_ms);
        t.shown = true;
        t.shown_ms = std::max<std::uint64_t>(now_ms, 1);
        ++counts_.received;
    }
    forget();
    while (tracks_.size() > kMaxTracks) {
        auto oldest = tracks_.begin();
        for (auto i = tracks_.begin(); i != tracks_.end(); ++i) {
            if (i->second.heard_ms < oldest->second.heard_ms) oldest = i;
        }
        tracks_.erase(oldest);
    }
}

Counts NpcPose::TakeCounts() {
    std::lock_guard lock(mutex_);
    Counts out = counts_;
    const std::uint32_t bones = counts_.bones;
    counts_ = Counts{};
    counts_.bones = bones;
    return out;
}

void NpcPose::Reset() {
    std::lock_guard lock(mutex_);
    source_.reset();
    reader_.Reset();
    ResetSent();
    targets_.clear();
    tracks_.clear();
    probes_.clear();
}

NpcPose& TheNpcPose() {
    static NpcPose pose;
    return pose;
}

std::int32_t AsSlotCount(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t p = actor;
    for (const std::uint64_t offset : CoopPuppet::addr::kControllerPath) {
        if (p == 0 || !guest.Read(p + offset, &p, sizeof(p))) return 0;
    }
    std::int32_t count = 0;
    if (p == 0 || !guest.Read(p + CoopPuppet::addr::kSlotCount, &count, sizeof(count))) return 0;
    return std::max(count, 0);
}

std::vector<CoopPuppet::Call> PartialCalls(std::uint64_t main, std::uint64_t actor,
                                           std::int32_t slot_count) {
    std::vector<CoopPuppet::Call> out;
    if (main == 0 || actor == 0) return out;
    const std::int32_t last = std::min(slot_count, CoopPuppet::addr::kMaxAsSlots);
    for (std::int32_t slot = 1; slot < last; ++slot) {
        CoopPuppet::Call c;
        c.function = main + CoopPuppet::addr::kSyncAsSlot;
        c.x[0] = actor;
        c.x[1] = 0;  // from slot 0
        c.x[2] = static_cast<std::uint64_t>(slot);
        c.x[3] = 0;  // node 0
        c.x[4] = 0;
        out.push_back(c);
    }
    return out;
}

}  // namespace CoopNpcPose
