// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM (oot-remake docs/NPC_COMPARTIDOS.md, steps 5 to 7; LICHII_COOP_NPC_POSE=1,
// off by default): the townsperson a player talks to looks right on the other
// screens. Its owner's game reads its skeleton each frame (the head turned to
// the talker, the face) and sends it as OBJECT_POSE, at most kSendHz a second,
// for that one townsperson only. The other games write it onto their copy,
// bone by bone, the way the puppets get Link's pose (coop_pose.h). No
// emulator code: coop_objects.cpp hands over the actors and the calls.
//
// Threads: SetSource, SetTargets, Fresh and TakeOutgoing from the present
// thread; Receive from the network thread; OnCalcFrame from the game's calc
// thread (the Mover's turn at the svc cave). One mutex guards it all.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "coop/protocol/messages.hpp"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace CoopNpcPose {

// 20 a second: the copies show the last two blended, one interval behind, so
// a head turn plays smooth at 60 frames. An unchanged pose is not sent again
// before kRefreshMs (a talker standing still costs 2 a second).
inline constexpr int kSendHz = 20;
inline constexpr std::uint64_t kRefreshMs = 500;
// Townspeople with many bones (oot-remake docs/NPC_COMPARTIDOS.md, "PNJ con
// muchos huesos": Ruto 300, Zelda 292, the Kokiri 250 to 264). A pose goes in
// parts of coop::protocol::kObjectPosePartBones bones, and each part lists
// only the bones that changed since they were last sent (by more than
// kSameUnits in a packed value), each again in the kRepeats poses after its
// last change (one lost pose does not leave it wrong), and all its bones
// every kRefreshMs. The receiver keeps the bones of earlier poses.
// The bones sent are held to kBonesPerSecond (what the old whole-pose limit,
// 160 bones at kSendHz, could send): a pose of more bones than that allows at
// kSendHz waits longer before the next (300 bones that all move: 10 a second).
// Any skeleton the skeleton id can name (kMaxBones bones) is sent.
inline constexpr std::uint32_t kMaxBones = 511;  // CoopPose::SkeletonId's 9 count bits
static_assert(kMaxBones < coop::protocol::kObjectPosePartBones * coop::protocol::kMaxObjectPoseParts);
inline constexpr std::uint32_t kBonesPerSecond = 160 * kSendHz;
inline constexpr std::uint8_t kRepeats = 2;
inline constexpr int kSameUnits = 1;
// A copy follows the poses of an object heard from this recently; a track not
// heard from for kForgetMs is dropped. At most kMaxTracks objects are tracked
// (one per other player is the most a room sends).
inline constexpr std::uint64_t kFreshMs = 1000;
inline constexpr std::uint64_t kForgetMs = 5000;
inline constexpr std::size_t kMaxTracks = 12;

// The one townsperson we own that our player talks to.
struct Source {
    coop::protocol::ObjectId id;
    std::uint16_t epoch = 0;
    std::uint64_t actor = 0;
    bool operator==(const Source&) const = default;
};

// One of our copies to write, and the object whose poses it takes.
struct Target {
    coop::protocol::ObjectId id;
    std::uint64_t actor = 0;
    // Below 1: the copy is blending back to its owner's pose after our own
    // talk let it go (Sync::RejoinWeight); the pose is blended with its bones.
    float weight = 1.0f;
    bool operator==(const Target&) const = default;
};

// An object whose poses came within kFreshMs: from whom, at which epoch.
struct Heard {
    coop::protocol::ObjectId id;
    std::uint8_t player = 0;
    std::uint16_t epoch = 0;
};

// Since the last TakeCounts.
struct Counts {
    unsigned read = 0;            // our source's skeleton read (calc frames)
    unsigned not_read = 0;        // no skeleton, or over kMaxBones
    unsigned sent = 0;            // poses out (each in one OBJECT_POSE per part)
    unsigned sent_parts = 0;      // OBJECT_POSE messages out
    unsigned sent_bones = 0;      // bones in them
    unsigned received = 0;        // poses in, whole (every part of the mask), shown
    unsigned stale = 0;           // in, older than the newest of that object, or a part again
    unsigned incomplete = 0;      // poses a newer one replaced before all their parts came
    unsigned bad = 0;             // parts or bones that do not fit the pose's skeleton
    unsigned written = 0;         // copy-frames written
    unsigned other_skeleton = 0;  // a copy with another skeleton: not written
    unsigned no_skeleton = 0;     // a copy whose skeleton was not found
    unsigned gone = 0;            // a copy or source no longer a live actor: not touched
    unsigned kept = 0;            // the last write was still there a frame later
    unsigned overwritten = 0;     // the game wrote over it (its own animation)
    std::uint32_t bones = 0;      // bones of the last source read
};

class NpcPose {
public:
    void SetSource(std::optional<Source> source);
    std::optional<Source> source() const;
    void SetTargets(std::vector<Target> targets);
    std::vector<Heard> Fresh(std::uint64_t now_ms) const;

    // Calc thread, once a game frame: read the source's skeleton (for
    // TakeOutgoing) and write the shown pose onto each target.
    void OnCalcFrame(CoopPuppet::Guest& guest, std::uint64_t now_ms);

    // The pose to send now, as one OBJECT_POSE per part that has bones to
    // send (in part order, one time and parts mask for all); empty when
    // nothing is due: no new read, too soon (kSendHz, kBonesPerSecond), or
    // nothing changed and no refresh due. The player field is 0.
    std::vector<coop::protocol::ObjectPose> TakeOutgoing(std::uint64_t server_now_us,
                                                         std::uint64_t now_ms);

    // A received OBJECT_POSE part (player field: the owner). Per object the
    // newest pose wins: an older epoch, or a pose of the same epoch not newer
    // (server time, then sequence) than the last one, is dropped, and so is
    // a part of that pose that came already. Each part's bones replace those
    // kept; once every part in the pose's mask came, the kept bones are
    // shown as the newest pose. A new epoch or owner starts the track over,
    // another skeleton its bones. A part that does not fit its skeleton (a
    // part or bone past its bone count) is dropped as bad.
    void Receive(const coop::protocol::ObjectPose& m, std::uint64_t now_ms);

    Counts TakeCounts();
    void Reset();

private:
    struct TrackOf {
        std::uint8_t player = 0;
        std::uint16_t epoch = 0;
        // The pose being put together: its time, mask, and the parts come.
        std::uint64_t time_us = 0;
        std::uint32_t sequence = 0;
        std::uint8_t parts = 0;
        std::uint8_t got = 0;
        bool shown = false;  // pushed onto the track
        // The newest bones kept, by bone number (the skeleton's bone count).
        std::uint32_t skeleton = 0;
        std::vector<std::optional<coop::protocol::PoseBone>> bones;
        std::uint64_t heard_ms = 0;  // the last part kept
        std::uint64_t shown_ms = 0;  // the last pose pushed onto the track
        CoopPose::Track track;
    };
    void ResetSent();

    mutable std::mutex mutex_;
    std::optional<Source> source_;
    CoopPose::ActorPoseReader reader_;
    std::optional<CoopPose::Pose> outbox_;
    std::uint32_t outbox_serial_ = 0;
    std::uint32_t sent_serial_ = 0;
    std::uint64_t sent_ms_ = 0;    // the last pose sent (0: none since the source was set)
    std::uint64_t next_ms_ = 0;    // the next may go from then on
    std::uint32_t sent_skeleton_ = 0;
    // By bone number: the value last sent, and the poses it goes in again.
    std::vector<std::optional<coop::protocol::PoseBone>> sent_bones_;
    std::vector<std::uint8_t> repeats_;
    std::array<std::uint64_t, coop::protocol::kMaxObjectPoseParts> refreshed_ms_{};
    std::uint32_t sequence_ = 0;
    std::vector<Target> targets_;
    std::map<coop::protocol::ObjectId, TrackOf> tracks_;
    std::map<std::uint64_t, CoopPose::WriteProbe> probes_;  // by copy actor
    Counts counts_;
};

// The one the hook uses.
NpcPose& TheNpcPose();

// Step 6: partial AS slots. Link's upper body and a face are slots of their
// own beside slot 0 (docs/puppet.md, "Animation playback"); playing slot 0's
// command on a copy leaves them as the copy's own AI had them. The game's
// ExecuteASSyncPartialAnimation call (CoopPuppet::addr::kSyncAsSlot,
// 0x3abec10(actor, 0, slot, 0, 0)) plays slot 0's command on another slot.
//
// The actor's AS slot count (controller +0x18), or 0 when it has no AS chain.
std::int32_t AsSlotCount(CoopPuppet::Guest& guest, std::uint64_t actor);
// The calls that put slot 0's command on slots 1 to slot_count - 1 (at most
// CoopPuppet::addr::kMaxAsSlots - 1 of them), in slot order.
std::vector<CoopPuppet::Call> PartialCalls(std::uint64_t main, std::uint64_t actor,
                                           std::int32_t slot_count);

}  // namespace CoopNpcPose
