// SPDX-License-Identifier: GPL-3.0-or-later
// Full-body pose copy for puppets (docs/puppet.md, "Pose").
//
// Each game frame, on the game's calc thread (the puppet cave's SVC), the
// local Link's skeleton is read: every bone's local transform as the game's
// animation left it. The network thread sends the bones that move as
// PLAYER_POSE at most kSendHz times a second. A received pose is kept per
// player; each frame the newest two are blended and written onto that
// player's puppet skeleton.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/pose_smooth.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::net {
class Client;
struct Event;
}  // namespace coop::net

namespace CoopPuppet {
class Guest;
}

namespace CoopPose {

using Quat = std::array<float, 4>;  // x, y, z, w
using Vec3 = std::array<float, 3>;
using Rows = std::array<float, 9>;  // 3x3 rotation as the game stores it, rows

// Read from the running game with the emulator's debug server (2026-11-09,
// Kakariko, child): see docs/puppet.md, Pose.
namespace addr {
// Actor -> skeleton object: read a pointer at each offset in turn. The model
// object sits inline at +0x120 of the fourth, its skeleton pointer at +0x20.
inline constexpr std::array<std::uint64_t, 5> kSkeletonPath{0x270, 0x8, 0x18, 0x18, 0x140};
inline constexpr std::uint64_t kModelResource = 0x120;  // of the fourth: "FMDL"
inline constexpr std::uint64_t kModelName = 0x8;  // FMDL: pointer to u16 length + chars
inline constexpr std::uint64_t kBoneCount = 0x48;       // u16 in the skeleton object
inline constexpr std::uint64_t kLocalArray = 0x18;      // pointer to the local transforms
inline constexpr std::uint64_t kLocalStride = 0x60;
// One local transform: u32 flags (bits 23 to 30 change with the pose),
// scale (3 floats at +0x10), rotation (3 rows of 4 floats at +0x20, the 4th
// of each 0) and translation (3 floats at +0x50).
inline constexpr std::uint32_t kFlagShift = 23;
inline constexpr std::uint32_t kFlagMask = 0xffu << kFlagShift;
}  // namespace addr

inline constexpr std::size_t kMaxBones = 512;
inline constexpr int kSendHz = 30;
// A part counts as moving once it differs from the first read by more than this.
inline constexpr float kMoved = 1e-4f;

// When a player's poses stop (a lost stretch of packets, a skeleton the
// sender could not read for a moment) the puppet holds the last pose for
// kHoldMs; the director plays no AS command on it meanwhile (Drives). Then
// the director starts the player's AS command, at the frame its newest
// PLAYER_SAMPLE says, and the pose copy fades out over kBlendMs: each frame
// it writes the held pose blended with what the puppet's skeleton holds now,
// so the hand-over to the game's own animation has no jump. If the game's AS
// playback does not write the local transforms before our write, what the
// skeleton holds is our own last write and the blend is a longer hold
// (kHoldMs + kBlendMs). After that nothing is written.
inline constexpr std::uint64_t kHoldMs = 2500;
inline constexpr std::uint64_t kBlendMs = 500;
// Poses that come back after a gap longer than kResumeGapMs blend in from
// what the puppet shows over kResumeMs, instead of snapping to the new pose.
inline constexpr std::uint64_t kResumeGapMs = 300;
inline constexpr std::uint64_t kResumeMs = 200;
// A pose counts as fresh for the director's reveal (OnCalcFrame's mask)
// when the newest one of its track came within this.
inline constexpr std::uint64_t kFreshPoseMs = 250;

// Held pieces (issue 158). The pieces in a Link's hands have skeletons of
// their own that the game animates apart from Link's: the slingshot's band
// pulled back with a seed in it, then let go when it shoots; the bow's string.
// The puppet's copy of the piece is not animated by anyone, so it stayed
// pulled back after the shot. Each piece out in a hand slot below (the
// EquipmentUser's hand slots: 1 SubWeapon, 2 ThirdWeapon, 3 Bow, 5 Tool,
// 6 SubTool; not the sword or shield) is sent as a PLAYER_POSE of its own
// skeleton, every bone, and written onto the piece of that skeleton in the
// puppet's hands. No new message: the skeleton id tells the poses apart.
inline constexpr std::array<std::uint32_t, 5> kPieceSlots{1, 2, 3, 5, 6};
inline constexpr std::uint32_t kMaxPieceBones = 64;      // bigger: not sent
inline constexpr std::uint64_t kPieceRefreshMs = 250;    // an unchanged piece, again
inline constexpr std::uint64_t kTrackForgetMs = 5000;    // a skeleton not heard from
inline constexpr std::size_t kMaxTracks = 8;             // skeletons kept per player

// One bone of a pose: the parts that move.
struct BonePose {
    std::uint16_t bone = 0;
    std::uint8_t flags = 0;
    std::optional<Quat> rotation;
    std::optional<Vec3> translation;
    std::optional<Vec3> scale;
};

struct Pose {
    std::uint32_t skeleton = 0;
    std::vector<BonePose> bones;  // increasing bone order
};

Quat RowsToQuat(const Rows& m);  // normalised, w >= 0
Rows QuatToRows(const Quat& q);  // q normalised first; identity if it is 0
std::array<std::int16_t, 4> Pack(const Quat& q);
Quat Unpack(const std::array<std::int16_t, 4>& q);
std::array<std::int16_t, 3> PackUnit(const Vec3& v);  // scaled by protocol::kPoseUnit
Vec3 UnpackUnit(const std::array<std::int16_t, 3>& v);
// Shortest-path normalised lerp from a to b; t in [0, 1].
Quat Nlerp(const Quat& a, const Quat& b, float t);

// A skeleton's id, sent with every pose: the bone count in the low 9 bits and
// a hash of the model's name ("Player_Child", "Player_Adult") above it, so a
// puppet applies only a pose of its own model.
std::uint32_t SkeletonId(std::uint32_t bone_count, std::string_view model_name);
std::uint32_t BoneCountOf(std::uint32_t skeleton);
// The name of the actor's model ("Player_Child", "Player_Adult") when it has
// a skeleton and an FMDL model resource; nothing when not (yet).
std::optional<std::string> ModelNameOf(CoopPuppet::Guest& guest, std::uint64_t actor);

// The newest poses of one remote player, and the blend shown now.
class Track {
public:
    // `time`: the pose's sender time (PLAYER_POSE's server time and
    // sequence); 0 server time: not known, only the arrival time is used.
    void Push(Pose pose, std::uint64_t now_ms, coop::protocol::SampleTime time = {});
    // The pose to show at now_ms: shown one interval behind, the last two
    // blended, so a 30 Hz stream plays smooth at 60 frames a second.
    // ORM smoothing (oot-remake docs/SUAVIZADO.md, LICHII_ORM_SMOOTH):
    // with `render_us` (the server time the puppet's position is shown at,
    // coop::host::PoseSmoother) and timed poses, the two poses around that
    // time by their sender time instead, so bursts, jitter and reordering on
    // the wire do not show and the body moves with the position.
    std::optional<Pose> At(std::uint64_t now_ms, std::uint64_t render_us = 0) const;
    std::uint64_t last_ms() const { return newest_ms_; }
    // How much of At() is written over what the puppet holds now: 1 while
    // poses come (or within kHoldMs of the last), falling to 0 over kBlendMs
    // after that, rising from 0 over kResumeMs after poses came back from a
    // gap. Nothing: write nothing (none yet, or held and blended out).
    std::optional<float> Weight(std::uint64_t now_ms) const;
    // The gap the newest pose ended (0: it did not end one), and when.
    std::uint64_t resume_gap_ms() const { return resume_gap_ms_; }
    std::uint64_t resumed_ms() const { return resumed_ms_; }

private:
    std::optional<Pose> older_;
    std::optional<Pose> newest_;
    std::uint64_t older_ms_ = 0;
    std::uint64_t newest_ms_ = 0;
    std::uint64_t resumed_ms_ = 0;
    std::uint64_t resume_gap_ms_ = 0;
    coop::host::Timeline<Pose> timeline_{8};  // timed poses (ORM smoothing)
};

// ORM OBJECT_POSE (coop_npc_pose.h): any actor's skeleton, read and written
// the way Link's is. The bones of a pose as the protocol carries them, and back.
std::vector<coop::protocol::PoseBone> EncodeBones(const Pose& pose);
Pose DecodeBones(std::uint32_t skeleton, const std::vector<coop::protocol::PoseBone>& bones);

// The skeleton id of an actor's skeleton (SkeletonId of its bone count and
// model), or nothing when it has none.
std::optional<std::uint32_t> SkeletonOf(CoopPuppet::Guest& guest, std::uint64_t actor);

// Reads one actor's skeleton as a pose: every bone's rotation (and flag
// byte), its translation and scale once they moved from the first read of
// that actor (sticky, as Link's moving bones). Another actor or skeleton
// starts over. A skeleton of more than max_bones is not read (why says so).
class ActorPoseReader {
public:
    std::optional<Pose> Read(CoopPuppet::Guest& guest, std::uint64_t actor,
                             std::uint32_t max_bones, std::string* why = nullptr);
    void Reset();
    std::uint32_t bone_count() const { return count_; }

private:
    std::uint64_t actor_ = 0;
    std::uint32_t skeleton_ = 0;
    std::uint32_t count_ = 0;
    std::vector<std::array<float, 6>> first_;  // translation, then scale
    std::vector<std::uint8_t> moving_;         // pose_part bits
};

// One bone of the last write onto an actor, to see at the next write whether
// the game kept it (as the puppet's "last write kept" count).
struct WriteProbe {
    std::uint64_t actor = 0;
    std::uint64_t local = 0;
    std::uint16_t bone = 0;
    bool set = false;
    std::array<float, 12> rows{};
};

enum class WriteResult { written, no_skeleton, other_skeleton, failed };

// Writes `pose` onto `actor`'s skeleton when its skeleton id is the pose's;
// bones not in the pose are left as they are. probe (may be null) is checked
// first (kept: what it saw, when it can tell) and then set to this write.
// `weight` below 1 blends the pose with what the skeleton holds now (a copy
// blending back to its owner's pose after our own talk, Sync::RejoinWeight).
WriteResult WriteActorPose(CoopPuppet::Guest& guest, std::uint64_t actor, const Pose& pose,
                           WriteProbe* probe = nullptr, std::optional<bool>* kept = nullptr,
                           float weight = 1.0f);

// The local player's actor, as coop::host::PlayerReader finds it; 0 none.
std::uint64_t FindPlayer(CoopPuppet::Guest& guest, std::uint64_t main_base);

// Calc thread: copy every bone's local transform of `from`'s skeleton onto
// `to`'s, when both skeletons are found and have the same bone count. For
// the fairies: the local Navi's wing beat onto each puppet's Navi, whose own
// actor plays no animation. `note` (may be null) gets both models and bone
// counts, or why nothing was copied.
bool CopySkeleton(CoopPuppet::Guest& guest, std::uint64_t from, std::uint64_t to,
                  std::string* note);

// Calc thread, once a frame: read the local player's skeleton and write the
// shown pose onto each live puppet. puppets: (player number, actor); the
// debug puppet (number 0) copies the local player's own pose. Returns the
// players (bit n) whose puppet got a whole, fresh body pose this frame: its
// track's newest pose within kFreshPoseMs, written at weight 1 (the
// director's reveal waits for it, so a new puppet never shows its T-pose).
// Logs once when a player's poses stop for kHoldMs (`pose: P<n> no pose
// for ...`) and when they come back (`pose: P<n> poses back after ...`).
// `render_us` (ORM smoothing): the server time the puppets are shown at
// (Track::At); 0 shows the poses by arrival time, as before.
std::uint32_t OnCalcFrame(CoopPuppet::Guest& guest, std::uint64_t main_base,
                          const std::vector<std::pair<std::uint8_t, std::uint64_t>>& puppets,
                          std::uint64_t now_ms, std::uint64_t render_us = 0);
// Network thread: send the newest local pose when it is time.
// ORM (coop_interest.h): `gap_ms` is the shortest time between two poses,
// the native 1000 / kSendHz; nothing sends none (nobody in our place).
void Send(coop::net::Client& client, std::uint64_t now_ms,
          std::optional<std::uint64_t> gap_ms = 1000 / kSendHz);
// What Send sends now: Link's pose, then each held piece that changed (or
// every kPieceRefreshMs while it holds still). Empty when it is not time.
// Each is stamped with the server time it was read at (server_now_us less
// how long ago the calc thread read it), so the receiver shows it with the
// PLAYER_SAMPLE of the same moment.
std::vector<coop::protocol::PlayerPose> TakeOutgoing(std::uint64_t server_now_us,
                                                     std::uint64_t now_ms,
                                                     std::optional<std::uint64_t> gap_ms = 1000 / kSendHz);
// Network thread: keep a received PLAYER_POSE.
void OnEvent(const coop::net::Event& event, std::uint64_t now_ms);
void Receive(const coop::protocol::PlayerPose& pose, std::uint64_t now_ms);
// LICHII_COOP_DEBUG_POSE=1: a line a second with bone counts and rates.
// The pose copy wins over the AS commands: a puppet whose poses arrive plays
// no AS command of its own (see Drives). LICHII_COOP_POSE_AS=1 (as_too) plays
// them as well, as before the pose copy.
void SetLog(std::function<void(const std::string&)> log, bool debug, bool as_too = false);
// Calc thread: whether the pose copy drives this player's puppet now (poses
// arrived in the last kHoldMs), so the director plays no AS command on it.
bool Drives(std::uint8_t number, std::uint64_t now_ms);

}  // namespace CoopPose
