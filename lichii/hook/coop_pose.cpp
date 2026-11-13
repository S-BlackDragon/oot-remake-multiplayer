// SPDX-License-Identifier: GPL-3.0-or-later
// The pose copy (coop_pose.h). No emulator header, so the unit tests build it.
#include "coop_pose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include "coop/net/client.hpp"
#include "coop/protocol/messages.hpp"
#include "coop_puppet.h"

namespace CoopPose {

namespace proto = coop::protocol;

// ------------------------------------------------------------------ math

namespace {

Quat Normalised(Quat q) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        return {0, 0, 0, 1};
    }
    for (float& v : q) {
        v /= n;
    }
    return q;
}

std::int16_t ToS16(float v, float scale) {
    return static_cast<std::int16_t>(std::lround(std::clamp(v * scale, -32767.0f, 32767.0f)));
}

}  // namespace

// The column-vector convention (v' = M v). Which way the game means its rows
// does not matter: QuatToRows gives back exactly what RowsToQuat was given.
Quat RowsToQuat(const Rows& m) {
    const float m00 = m[0], m01 = m[1], m02 = m[2];
    const float m10 = m[3], m11 = m[4], m12 = m[5];
    const float m20 = m[6], m21 = m[7], m22 = m[8];
    Quat q{};
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    q = Normalised(q);
    if (q[3] < 0.0f) {
        for (float& v : q) {
            v = -v;
        }
    }
    return q;
}

Rows QuatToRows(const Quat& in) {
    const Quat q = Normalised(in);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}

std::array<std::int16_t, 4> Pack(const Quat& in) {
    const Quat q = Normalised(in);
    return {ToS16(q[0], 32767.0f), ToS16(q[1], 32767.0f), ToS16(q[2], 32767.0f),
            ToS16(q[3], 32767.0f)};
}

Quat Unpack(const std::array<std::int16_t, 4>& in) {
    return Normalised({in[0] / 32767.0f, in[1] / 32767.0f, in[2] / 32767.0f, in[3] / 32767.0f});
}

std::array<std::int16_t, 3> PackUnit(const Vec3& v) {
    return {ToS16(v[0], proto::kPoseUnit), ToS16(v[1], proto::kPoseUnit),
            ToS16(v[2], proto::kPoseUnit)};
}

Vec3 UnpackUnit(const std::array<std::int16_t, 3>& v) {
    return {v[0] / proto::kPoseUnit, v[1] / proto::kPoseUnit, v[2] / proto::kPoseUnit};
}

Quat Nlerp(const Quat& a, const Quat& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    Quat q{};
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = a[i] * (1.0f - t) + sign * b[i] * t;
    }
    return Normalised(q);
}

std::uint32_t SkeletonId(std::uint32_t bone_count, std::string_view model_name) {
    std::uint32_t hash = 2166136261u;  // FNV-1a
    for (const char c : model_name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return (bone_count & 0x1ffu) | (hash << 9);
}

std::uint32_t BoneCountOf(std::uint32_t skeleton) {
    return skeleton & 0x1ffu;
}

namespace {

bool SameBones(const Pose& a, const Pose& b) {
    if (a.skeleton != b.skeleton || a.bones.size() != b.bones.size()) return false;
    for (std::size_t i = 0; i < a.bones.size(); ++i) {
        const BonePose& x = a.bones[i];
        const BonePose& y = b.bones[i];
        if (x.bone != y.bone || x.rotation.has_value() != y.rotation.has_value() ||
            x.translation.has_value() != y.translation.has_value() ||
            x.scale.has_value() != y.scale.has_value()) {
            return false;
        }
    }
    return true;
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// The pose between two of one bone list, t from older (0) to newer (1).
Pose Blend(const Pose& older, const Pose& newer, float t) {
    Pose out = newer;
    for (std::size_t i = 0; i < out.bones.size(); ++i) {
        BonePose& b = out.bones[i];
        const BonePose& o = older.bones[i];
        if (b.rotation) b.rotation = Nlerp(*o.rotation, *b.rotation, t);
        if (b.translation) b.translation = Lerp(*o.translation, *b.translation, t);
        if (b.scale) b.scale = Lerp(*o.scale, *b.scale, t);
        if (t < 0.5f) b.flags = o.flags;
    }
    return out;
}

}  // namespace

void Track::Push(Pose pose, std::uint64_t now_ms, coop::protocol::SampleTime time) {
    const bool resumed = newest_ && now_ms - std::min(now_ms, newest_ms_) > kResumeGapMs;
    if (time.server_time_us != 0) {
        // Timed (ORM smoothing): a resume starts the timeline over, as it
        // starts the blend over; so does another bone list (not blended).
        if (resumed || (!timeline_.empty() && !SameBones(timeline_.newest(), pose))) {
            timeline_.clear();
        }
        timeline_.push(time, pose);
    }
    if (resumed) {
        // Back after a gap: not blended over the gap's length (which would
        // play it in slow motion), but faded in from what the puppet holds.
        resume_gap_ms_ = now_ms - newest_ms_;
        resumed_ms_ = now_ms;
        older_.reset();
    } else if (newest_ && SameBones(*newest_, pose)) {
        older_ = std::move(newest_);
        older_ms_ = newest_ms_;
    } else {
        older_.reset();
    }
    newest_ = std::move(pose);
    newest_ms_ = now_ms;
}

std::optional<Pose> Track::At(std::uint64_t now_ms, std::uint64_t render_us) const {
    if (!newest_) {
        return std::nullopt;
    }
    if (render_us != 0 && !timeline_.empty()) {
        if (const auto v = timeline_.at(render_us, kResumeGapMs * 1000)) {
            if (v->b != nullptr && SameBones(*v->a, *v->b)) {
                return Blend(*v->a, *v->b, std::clamp(v->t, 0.0f, 1.0f));
            }
            return *v->a;
        }
    }
    if (!older_ || newest_ms_ <= older_ms_) {
        return newest_;
    }
    // older shows at newest_ms, newest one interval later.
    const float span = static_cast<float>(newest_ms_ - older_ms_);
    const float t = static_cast<float>(now_ms - std::min(now_ms, newest_ms_)) / span;
    if (t >= 1.0f) {
        return newest_;
    }
    return Blend(*older_, *newest_, t);
}

std::optional<float> Track::Weight(std::uint64_t now_ms) const {
    if (!newest_) {
        return std::nullopt;
    }
    const std::uint64_t age = now_ms - std::min(now_ms, newest_ms_);
    if (age >= kHoldMs + kBlendMs) {
        return std::nullopt;  // held, then handed over: the game's animation has it
    }
    if (age > kHoldMs) {
        return 1.0f - static_cast<float>(age - kHoldMs) / static_cast<float>(kBlendMs);
    }
    if (resumed_ms_ != 0 && now_ms >= resumed_ms_ && now_ms - resumed_ms_ < kResumeMs) {
        return static_cast<float>(now_ms - resumed_ms_) / static_cast<float>(kResumeMs);
    }
    return 1.0f;
}

// ------------------------------------------------------------------ the game side

namespace {

template <typename T>
bool ReadValue(CoopPuppet::Guest& guest, std::uint64_t at, T& out) {
    return at != 0 && guest.Read(at, &out, sizeof(T));
}

struct LocalMtx {
    std::uint32_t flags;
    std::uint32_t unused[3];
    float scale[4];
    float rows[12];  // three rows of 4, the 4th of each 0
    float translation[4];
};
static_assert(sizeof(LocalMtx) == addr::kLocalStride);

Rows RowsOf(const LocalMtx& b) {
    return {b.rows[0], b.rows[1], b.rows[2], b.rows[4], b.rows[5],
            b.rows[6], b.rows[8], b.rows[9], b.rows[10]};
}

// One bone of a pose onto a skeleton's bone `m`: whole at `weight` 1, below 1
// blended with what `m` holds now (rotation by nlerp, translation and scale by
// lerp; the flags taken from the pose only from 0.5 up). `weight` is already
// clamped to [0, 1]. True when the pose rotated the bone (the write probes
// keep the first rotated one). Shared by the puppets' WritePose and the
// townsfolk's WriteActorPose.
bool BlendBone(LocalMtx& m, const BonePose& b, float weight) {
    const bool blend = weight < 1.0f;
    if (!blend || weight >= 0.5f) {
        m.flags = (m.flags & ~addr::kFlagMask) | (std::uint32_t{b.flags} << addr::kFlagShift);
    }
    if (b.translation) {
        const Vec3 to = blend ? Lerp({m.translation[0], m.translation[1], m.translation[2]},
                                     *b.translation, weight)
                              : *b.translation;
        std::copy_n(to.data(), 3, m.translation);
    }
    if (b.scale) {
        const Vec3 to =
            blend ? Lerp({m.scale[0], m.scale[1], m.scale[2]}, *b.scale, weight) : *b.scale;
        std::copy_n(to.data(), 3, m.scale);
    }
    if (!b.rotation) return false;
    const Rows r =
        QuatToRows(blend ? Nlerp(RowsToQuat(RowsOf(m)), *b.rotation, weight) : *b.rotation);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            m.rows[row * 4 + col] = r[row * 3 + col];
        }
    }
    return true;
}

struct Skeleton {
    std::uint64_t local = 0;
    std::uint32_t count = 0;
    std::string model;  // "Player_Child", "Player_Adult"
};

// Actor -> its skeleton's local transform array, checking the model is one.
std::optional<Skeleton> FindSkeleton(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t at = actor;
    std::uint64_t holder = 0;  // the struct the model object sits in
    for (std::size_t i = 0; i < addr::kSkeletonPath.size(); ++i) {
        if (i + 1 == addr::kSkeletonPath.size()) holder = at;
        if (!ReadValue(guest, at + addr::kSkeletonPath[i], at) || at == 0) {
            return std::nullopt;
        }
    }
    std::uint64_t resource = 0;
    char magic[4]{};
    if (!ReadValue(guest, holder + addr::kModelResource, resource) ||
        !guest.Read(resource, magic, sizeof(magic)) || std::memcmp(magic, "FMDL", 4) != 0) {
        return std::nullopt;
    }
    Skeleton s;
    std::uint16_t count = 0;
    if (!ReadValue(guest, at + addr::kBoneCount, count) ||
        !ReadValue(guest, at + addr::kLocalArray, s.local) || s.local == 0 || count == 0 ||
        count > kMaxBones) {
        return std::nullopt;
    }
    s.count = count;
    std::uint64_t name = 0;
    std::uint16_t length = 0;
    char text[64]{};
    if (ReadValue(guest, resource + addr::kModelName, name) && ReadValue(guest, name, length) &&
        length > 0 && length < sizeof(text) && guest.Read(name + 2, text, length)) {
        s.model.assign(text, length);
    }
    return s;
}

}  // namespace

std::optional<std::string> ModelNameOf(CoopPuppet::Guest& guest, std::uint64_t actor) {
    if (actor == 0) {
        return std::nullopt;
    }
    const auto s = FindSkeleton(guest, actor);
    if (!s || s->model.empty()) {
        return std::nullopt;
    }
    return s->model;
}

// The local player's actor, as coop::host::PlayerReader finds it (addresses.hpp).
std::uint64_t FindPlayer(CoopPuppet::Guest& guest, std::uint64_t main_base) {
    std::uint64_t scene = 0, table = 0, array = 0, manager = 0, must = 0;
    std::uint32_t count = 0;
    if (!ReadValue(guest, main_base + 0x4e97648, scene) || !ReadValue(guest, scene + 0x1f0, table) ||
        !ReadValue(guest, table + 0x50, count) || count < 0x10 ||
        !ReadValue(guest, table + 0x58, array) || !ReadValue(guest, array + 0x78, manager) ||
        !ReadValue(guest, manager + 0x30, must) || must == 0) {
        return 0;
    }
    for (const std::uint64_t link_at : {0x40, 0x50}) {
        std::uint64_t link = 0, holder = 0, actor = 0;
        std::uint32_t id = 0, holder_id = 0, state = 0;
        if (ReadValue(guest, manager + link_at, link) && ReadValue(guest, link + 0x10, id) &&
            id != 0xffffffffu && ReadValue(guest, link + 0x8, holder) &&
            ReadValue(guest, holder + 0x48, holder_id) && holder_id == id &&
            ReadValue(guest, holder + 0x40, actor) && ReadValue(guest, actor + 0x2c, state) &&
            state == 4) {
            return actor;
        }
    }
    return 0;
}

bool CopySkeleton(CoopPuppet::Guest& guest, std::uint64_t from, std::uint64_t to,
                  std::string* note) {
    const auto a = FindSkeleton(guest, from);
    const auto b = FindSkeleton(guest, to);
    if (!a || !b) {
        if (note) *note = !a ? "no skeleton on the source" : "no skeleton on the target";
        return false;
    }
    if (note) {
        *note = a->model + " " + std::to_string(a->count) + " bones -> " + b->model + " " +
                std::to_string(b->count) + " bones";
    }
    if (a->count != b->count) return false;
    std::vector<LocalMtx> bones(a->count);
    const std::size_t size = bones.size() * sizeof(LocalMtx);
    if (!guest.Read(a->local, bones.data(), size)) return false;
    for (const LocalMtx& m : bones) {
        if (!std::isfinite(m.rows[0]) || !std::isfinite(m.translation[0])) return false;
    }
    return guest.Write(b->local, bones.data(), size);
}

namespace {

bool Moved(const float* a, const float* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(a[i] - b[i]) > kMoved) return true;
    }
    return false;
}

struct Written {
    std::uint64_t actor = 0;
    std::uint64_t local = 0;
    std::uint16_t bone = 0;
    float rows[12]{};
};

struct State {
    std::mutex mutex;
    std::function<void(const std::string&)> log;
    bool debug = false;
    bool no_as = false;
    std::string local_model;  // our skeleton's model, for the debug line
    // The local skeleton: the first read and which parts have moved since.
    std::vector<LocalMtx> first;
    std::vector<std::uint8_t> moving;  // pose_part bits, sticky
    // Local pose, from the calc thread, for the network thread.
    std::optional<Pose> outbox;
    // The pieces in our hands (kPieceSlots), every bone each: the slingshot's
    // band and seed, the bow's string.
    std::vector<Pose> pieces_outbox;
    std::uint32_t outbox_serial = 0;
    std::uint64_t outbox_ms = 0;  // when the calc thread read it
    std::uint32_t sent_serial = 0;
    std::uint64_t last_send_ms = 0;
    std::uint32_t sequence = 0;
    std::size_t last_size = 0;
    // What each piece skeleton was last sent as, and when: an unchanged
    // piece is sent again only after kPieceRefreshMs.
    struct PieceSent {
        std::vector<proto::PoseBone> bones;
        std::uint64_t ms = 0;
    };
    std::map<std::uint32_t, PieceSent> pieces_sent;
    // Received, per player 1 to kMaxPlayers, one track per skeleton: Link's body and
    // each held piece arrive as poses of their own skeleton.
    std::array<std::map<std::uint32_t, Track>, proto::kMaxPlayers + 1> tracks;
    // What was last written to each puppet, to see whether the game keeps it.
    std::vector<Written> written_last;
    // WritePose's skeleton buffer, kept between writes.
    std::vector<LocalMtx> write_bones;
    // Per player: its poses stopped and the hold ran out (the blend into its
    // AS command began), for one log line each way.
    std::array<bool, proto::kMaxPlayers + 1> dropped{};
    // Counters for the debug line.
    std::uint64_t log_ms = 0;
    unsigned sent = 0, received = 0, written = 0, refused = 0, kept = 0, overwritten = 0;
    unsigned pieces_sent_n = 0, pieces_written = 0;
};

// What was last written: kept for this many actors (puppets and pieces).
constexpr std::size_t kWrittenKeep = 32;

State& S() {
    static State state;
    return state;
}

void Say(State& s, const std::string& text) {
    if (s.log) s.log("pose: " + text);
}

// Calc thread, under the lock: the local skeleton as a pose of its moving bones.
std::optional<Pose> ReadLocal(State& s, CoopPuppet::Guest& guest, std::uint64_t actor) {
    const auto skel = FindSkeleton(guest, actor);
    if (!skel) return std::nullopt;
    std::vector<LocalMtx> bones(skel->count);
    if (!guest.Read(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) {
        return std::nullopt;
    }
    for (const LocalMtx& b : bones) {
        if (!std::isfinite(b.rows[0]) || !std::isfinite(b.translation[0])) return std::nullopt;
    }
    if (s.first.size() != bones.size() || s.local_model != skel->model) {  // a new skeleton (age)
        s.first = bones;
        s.moving.assign(bones.size(), 0);
    }
    Pose pose;
    pose.skeleton = SkeletonId(skel->count, skel->model);
    s.local_model = skel->model;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const LocalMtx& b = bones[i];
        const LocalMtx& f = s.first[i];
        std::uint8_t& m = s.moving[i];
        if (Moved(b.rows, f.rows, 12) || (b.flags & addr::kFlagMask) != (f.flags & addr::kFlagMask)) {
            m |= proto::pose_part::rotation;
        }
        if (Moved(b.translation, f.translation, 3)) m |= proto::pose_part::translation;
        if (Moved(b.scale, f.scale, 3)) m |= proto::pose_part::scale;
        if (m == 0) continue;
        BonePose out;
        out.bone = static_cast<std::uint16_t>(i);
        out.flags = static_cast<std::uint8_t>((b.flags & addr::kFlagMask) >> addr::kFlagShift);
        if (m & proto::pose_part::rotation) out.rotation = RowsToQuat(RowsOf(b));
        if (m & proto::pose_part::translation) {
            out.translation = Vec3{b.translation[0], b.translation[1], b.translation[2]};
        }
        if (m & proto::pose_part::scale) out.scale = Vec3{b.scale[0], b.scale[1], b.scale[2]};
        pose.bones.push_back(out);
    }
    return pose;
}

// Calc thread, under the lock: every bone of a held piece, as a pose. A
// piece is small, so all its bones go, not only the ones that moved since a
// first read: the puppet's copy of the piece may start in another state
// (pulled back) than ours did.
std::optional<Pose> ReadPiece(CoopPuppet::Guest& guest, const Skeleton& skel) {
    if (skel.count > kMaxPieceBones) return std::nullopt;
    std::vector<LocalMtx> bones(skel.count);
    if (!guest.Read(skel.local, bones.data(), bones.size() * sizeof(LocalMtx))) {
        return std::nullopt;
    }
    Pose pose;
    pose.skeleton = SkeletonId(skel.count, skel.model);
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const LocalMtx& b = bones[i];
        if (!std::isfinite(b.rows[0]) || !std::isfinite(b.translation[0])) return std::nullopt;
        BonePose out;
        out.bone = static_cast<std::uint16_t>(i);
        out.flags = static_cast<std::uint8_t>((b.flags & addr::kFlagMask) >> addr::kFlagShift);
        out.rotation = RowsToQuat(RowsOf(b));
        out.translation = Vec3{b.translation[0], b.translation[1], b.translation[2]};
        out.scale = Vec3{b.scale[0], b.scale[1], b.scale[2]};
        pose.bones.push_back(out);
    }
    return pose;
}

// The pieces in an actor's hands (kPieceSlots) that are out and drawn now.
std::vector<std::uint64_t> HeldPieces(CoopPuppet::Guest& guest, std::uint64_t actor) {
    namespace pa = CoopPuppet::addr;
    std::vector<std::uint64_t> out;
    std::uint64_t parts = 0, eu = 0;
    if (!ReadValue(guest, actor + pa::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + pa::kPartsEquipmentUser, eu) || eu == 0) {
        return out;
    }
    for (const std::uint32_t slot : kPieceSlots) {
        const std::uint64_t link = eu + pa::kEquipSlots + pa::kEquipSlotStride * slot;
        std::uint32_t id = 0xFFFFFFFFu, state = 0;
        std::uint64_t holder = 0, piece = 0;
        if (ReadValue(guest, link + pa::kEquipSlotId, id) && id != 0xFFFFFFFFu &&
            ReadValue(guest, link + pa::kEquipSlotHolder, holder) && holder != 0 &&
            ReadValue(guest, holder + pa::kHolderActor, piece) && piece != 0 && piece != actor &&
            ReadValue(guest, piece + pa::kActorState, state) && state == pa::kStatePresent) {
            out.push_back(piece);
        }
    }
    return out;
}

// Calc thread, under the lock: the pose onto a puppet's skeleton.
bool WritePose(State& s, CoopPuppet::Guest& guest, std::uint64_t actor, const Pose& pose) {
    const auto skel = FindSkeleton(guest, actor);
    if (!skel || SkeletonId(skel->count, skel->model) != pose.skeleton) return false;
    std::vector<LocalMtx> bones(skel->count);
    if (!guest.Read(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) return false;

    // Did the game keep what we wrote last frame?
    auto last = std::find_if(s.written_last.begin(), s.written_last.end(),
                             [actor](const Written& w) { return w.actor == actor; });
    if (last != s.written_last.end() && last->local == skel->local &&
        last->bone < bones.size()) {
        if (Moved(bones[last->bone].rows, last->rows, 12)) {
            ++s.overwritten;
        } else {
            ++s.kept;
        }
    }

    std::optional<std::uint16_t> probe;
    for (const BonePose& b : pose.bones) {
        if (b.bone >= bones.size()) continue;
        if (BlendBone(bones[b.bone], b, weight) && !probe) probe = b.bone;
    }
    if (!guest.Write(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) return false;
    if (probe) {
        if (last == s.written_last.end()) {
            s.written_last.push_back({});
            last = s.written_last.end() - 1;
        }
        last->actor = actor;
        last->local = skel->local;
        last->bone = *probe;
        std::copy_n(bones[*probe].rows, 12, last->rows);
    }
    return true;
}

// The actor's facing in degrees: its forward axis (rotation column 2) on the
// ground plane, atan2(x, z). "-" when it cannot be read.
std::string YawText(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::array<float, 9> m{};
    if (actor == 0 || !guest.Read(actor + 0x328, m.data(), sizeof(m))) return "-";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f", std::atan2(m[2], m[8]) * 57.29578f);
    return buf;
}

}  // namespace

void SetLog(std::function<void(const std::string&)> log, bool debug, bool as_too) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    s.log = std::move(log);
    s.debug = debug;
    s.no_as = !as_too;
}

bool Drives(std::uint8_t number, std::uint64_t now_ms) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (!s.no_as || number == 0 || number >= s.tracks.size()) return false;
    std::uint64_t last = 0;
    for (const auto& [skeleton, track] : s.tracks[number]) last = std::max(last, track.last_ms());
    return last != 0 && now_ms - std::min(now_ms, last) < kHoldMs;
}

namespace {

// Calc thread, under the lock: the pose for `actor` (a puppet or a piece in
// its hands) from `number`'s poses of that skeleton, written. Nothing when
// no pose of its skeleton has come, or the last was held and blended out
// (Track::Weight). `whole`: written at weight 1 from a fresh track (or the
// debug puppet's own pose); `used`: the track it came from.
enum class Wrote { none, written, refused };
Wrote WriteFor(State& s, CoopPuppet::Guest& guest, std::uint8_t number, std::uint64_t actor,
               std::uint64_t now_ms, std::uint64_t render_us, bool* whole = nullptr,
               const Track** used = nullptr) {
    const auto skel = FindSkeleton(guest, actor);
    if (!skel) return Wrote::none;
    const std::uint32_t id = SkeletonId(skel->count, skel->model);
    std::optional<Pose> pose;
    float weight = 1.0f;
    bool fresh = true;
    if (number == 0) {
        if (!s.debug) return Wrote::none;
        if (s.outbox && s.outbox->skeleton == id) {
            pose = s.outbox;  // the debug puppet mirrors us
        } else {
            for (const Pose& p : s.pieces_outbox) {
                if (p.skeleton == id) pose = p;
            }
        }
    } else {
        const auto& tracks = s.tracks[number];
        const auto it = tracks.find(id);
        if (it == tracks.end()) return Wrote::none;
        if (used) *used = &it->second;
        const auto w = it->second.Weight(now_ms);
        if (!w) return Wrote::none;
        weight = *w;
        fresh = now_ms - std::min(now_ms, it->second.last_ms()) < kFreshPoseMs;
        pose = it->second.At(now_ms, render_us);
    }
    if (!pose) return Wrote::none;
    if (!WritePose(s, guest, actor, *skel, *pose, weight)) return Wrote::refused;
    if (whole) *whole = fresh && weight >= 1.0f;
    return Wrote::written;
}

}  // namespace

std::uint32_t OnCalcFrame(CoopPuppet::Guest& guest, std::uint64_t main_base,
                          const std::vector<std::pair<std::uint8_t, std::uint64_t>>& puppets,
                          std::uint64_t now_ms, std::uint64_t render_us) {
    std::uint32_t posed = 0;
    State& s = S();
    std::lock_guard lock(s.mutex);
    const std::uint64_t player = main_base != 0 ? FindPlayer(guest, main_base) : 0;
    if (player != 0) {
        if (auto local = ReadLocal(s, guest, player)) {
            s.last_size = local->bones.size();
            s.outbox = std::move(local);
            s.outbox_ms = now_ms;
            s.pieces_outbox.clear();
            for (const std::uint64_t piece : HeldPieces(guest, player)) {
                if (const auto skel = FindSkeleton(guest, piece)) {
                    if (auto pose = ReadPiece(guest, *skel)) {
                        s.pieces_outbox.push_back(std::move(*pose));
                    }
                }
            }
            ++s.outbox_serial;
        }
    }
    for (const auto& [number, actor] : puppets) {
        if (actor == 0 || number >= s.tracks.size()) continue;
        bool whole = false;
        const Track* used = nullptr;
        const Wrote body = WriteFor(s, guest, number, actor, now_ms, render_us, &whole, &used);
        if (body == Wrote::written) {
            ++s.written;
            if (whole && number < 32) posed |= 1u << number;
        } else if (body == Wrote::refused ||
                   (number != 0 && used == nullptr && !s.tracks[number].empty())) {
            ++s.refused;  // poses come, none of this puppet's skeleton (age)
        }
        // A player's poses stopped (held, then blended into the AS command),
        // or came back: one line each way.
        if (number != 0 && used != nullptr) {
            const std::uint64_t age = now_ms - std::min(now_ms, used->last_ms());
            if (!s.dropped[number] && age > kHoldMs) {
                s.dropped[number] = true;
                Say(s, "P" + std::to_string(number) + " no pose for " + std::to_string(age) +
                           " ms: last pose held, blending into its own animation over " +
                           std::to_string(kBlendMs) + " ms");
            } else if (s.dropped[number] && age <= kHoldMs) {
                s.dropped[number] = false;
                Say(s, "P" + std::to_string(number) + " poses back after " +
                           std::to_string(used->resume_gap_ms()) + " ms: blended in over " +
                           std::to_string(kResumeMs) + " ms");
            }
        }
        for (const std::uint64_t piece : HeldPieces(guest, actor)) {
            const Wrote w = WriteFor(s, guest, number, piece, now_ms, render_us);
            if (w == Wrote::written) {
                ++s.pieces_written;
            }
        }
    }
    if (s.written_last.size() > kWrittenKeep) {
        s.written_last.erase(s.written_last.begin(),
                             s.written_last.end() - static_cast<std::ptrdiff_t>(kWrittenKeep));
    }
    if (s.debug && now_ms - s.log_ms >= 1000) {
        s.log_ms = now_ms;
        // Facing, for checking that a puppet turns with its player: ours and
        // each puppet's, from the actor's rotation (+0x328).
        std::string facing = "; yaw ours " + YawText(guest, player);
        for (const auto& [number, actor] : puppets) {
            facing += ", P" + std::to_string(number) + " " + YawText(guest, actor);
        }
        Say(s, (s.local_model.empty() ? std::string("?") : s.local_model) + " " +
                   std::to_string(s.first.size()) + " bones, " + std::to_string(s.last_size) +
                   " moving; " + std::to_string(s.sent) + " sent/s, " +
                   std::to_string(s.received) + " received/s, " + std::to_string(s.written) +
                   " written/s, " + std::to_string(s.refused) + " refused/s; held pieces " +
                   std::to_string(s.pieces_outbox.size()) + " ours, " +
                   std::to_string(s.pieces_sent_n) + " sent/s, " + std::to_string(s.pieces_written) +
                   " written/s; last write kept " +
                   std::to_string(s.kept) + ", overwritten " + std::to_string(s.overwritten) +
                   facing);
        s.sent = s.received = s.written = s.refused = s.kept = s.overwritten = 0;
        s.pieces_sent_n = s.pieces_written = 0;
    }
    return posed;
}

namespace {

std::vector<proto::PoseBone> Encode(const Pose& pose) {
    std::vector<proto::PoseBone> bones;
    bones.reserve(pose.bones.size());
    for (const BonePose& b : pose.bones) {
        proto::PoseBone out;
        out.bone = b.bone;
        out.flags = b.flags;
        if (b.rotation) out.rotation = Pack(*b.rotation);
        if (b.translation) out.translation = PackUnit(*b.translation);
        if (b.scale) out.scale = PackUnit(*b.scale);
        bones.push_back(out);
    }
    return bones;
}

}  // namespace

std::vector<proto::PlayerPose> TakeOutgoing(std::uint64_t server_now_us, std::uint64_t now_ms,
                                            std::optional<std::uint64_t> gap_ms) {
    std::vector<proto::PlayerPose> out;
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (!gap_ms || !s.outbox || s.outbox_serial == s.sent_serial ||
        now_ms - s.last_send_ms < *gap_ms) {  // ORM: the gap from coop_interest.h
        return out;
    }
    s.sent_serial = s.outbox_serial;
    s.last_send_ms = now_ms;
    // When it was read (ORM smoothing): the receiver shows it with the sample
    // of that moment, not of the network round that sent it.
    const std::uint64_t read_ago_us = (now_ms - std::min(now_ms, s.outbox_ms)) * 1000;
    const std::uint64_t read_us =
        server_now_us > read_ago_us ? server_now_us - read_ago_us : server_now_us;
    proto::PlayerPose body;
    body.time = {read_us, ++s.sequence};
    body.skeleton = s.outbox->skeleton;
    body.bones = Encode(*s.outbox);
    out.push_back(std::move(body));
    ++s.sent;
    // Each piece in our hands as a pose of its own skeleton: when it changed,
    // or now and then while it holds still (a late joiner, a lost packet).
    for (const Pose& piece : s.pieces_outbox) {
        std::vector<proto::PoseBone> bones = Encode(piece);
        auto& last = s.pieces_sent[piece.skeleton];
        if (last.ms != 0 && last.bones == bones && now_ms - last.ms < kPieceRefreshMs) continue;
        last.bones = bones;
        last.ms = now_ms;
        proto::PlayerPose m;
        m.time = {read_us, ++s.sequence};
        m.skeleton = piece.skeleton;
        m.bones = std::move(bones);
        out.push_back(std::move(m));
        ++s.pieces_sent_n;
    }
    std::erase_if(s.pieces_sent, [&](const auto& kv) {
        return now_ms - kv.second.ms > 10 * kPieceRefreshMs;  // put away long ago
    });
    return out;
}

void Send(coop::net::Client& client, std::uint64_t now_ms, std::optional<std::uint64_t> gap_ms) {
    for (const proto::PlayerPose& m : TakeOutgoing(client.server_now_us(), now_ms, gap_ms)) {
        client.send(m);
    }
}

void OnEvent(const coop::net::Event& event, std::uint64_t now_ms) {
    if (event.kind != coop::net::EventKind::message) return;
    const auto* m = std::get_if<proto::PlayerPose>(&event.message);
    if (m == nullptr) return;
    Receive(*m, now_ms);
}

void Receive(const proto::PlayerPose& m, std::uint64_t now_ms) {
    Pose pose;
    pose.skeleton = m.skeleton;
    pose.bones.reserve(m.bones.size());
    for (const proto::PoseBone& b : m.bones) {
        BonePose out;
        out.bone = b.bone;
        out.flags = b.flags;
        if (b.rotation) out.rotation = Unpack(*b.rotation);
        if (b.translation) out.translation = UnpackUnit(*b.translation);
        if (b.scale) out.scale = UnpackUnit(*b.scale);
        pose.bones.push_back(out);
    }
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (m.player_number == 0 || m.player_number >= s.tracks.size()) return;
    auto& tracks = s.tracks[m.player_number];
    tracks[m.skeleton].Push(std::move(pose), now_ms, m.time);
    // Skeletons not heard from for a while (a piece put away, the other age).
    std::erase_if(tracks, [&](const auto& kv) {
        return now_ms - std::min(now_ms, kv.second.last_ms()) > kTrackForgetMs;
    });
    while (tracks.size() > kMaxTracks) {
        auto oldest = tracks.begin();
        for (auto it = tracks.begin(); it != tracks.end(); ++it) {
            if (it->second.last_ms() < oldest->second.last_ms()) oldest = it;
        }
        tracks.erase(oldest);
    }
    ++s.received;
}

// ------------------------------------------------------------------ any actor (ORM OBJECT_POSE)

std::vector<proto::PoseBone> EncodeBones(const Pose& pose) {
    return Encode(pose);
}

Pose DecodeBones(std::uint32_t skeleton, const std::vector<proto::PoseBone>& bones) {
    Pose pose;
    pose.skeleton = skeleton;
    pose.bones.reserve(bones.size());
    for (const proto::PoseBone& b : bones) {
        BonePose out;
        out.bone = b.bone;
        out.flags = b.flags;
        if (b.rotation) out.rotation = Unpack(*b.rotation);
        if (b.translation) out.translation = UnpackUnit(*b.translation);
        if (b.scale) out.scale = UnpackUnit(*b.scale);
        pose.bones.push_back(out);
    }
    return pose;
}

std::optional<std::uint32_t> SkeletonOf(CoopPuppet::Guest& guest, std::uint64_t actor) {
    const auto skel = FindSkeleton(guest, actor);
    if (!skel) return std::nullopt;
    return SkeletonId(skel->count, skel->model);
}

void ActorPoseReader::Reset() {
    actor_ = 0;
    skeleton_ = 0;
    count_ = 0;
    first_.clear();
    moving_.clear();
}

std::optional<Pose> ActorPoseReader::Read(CoopPuppet::Guest& guest, std::uint64_t actor,
                                          std::uint32_t max_bones, std::string* why) {
    const auto skel = actor != 0 ? FindSkeleton(guest, actor) : std::nullopt;
    if (!skel) {
        if (why) *why = "no skeleton";
        return std::nullopt;
    }
    if (skel->count > max_bones) {
        if (why) *why = skel->model + " has " + std::to_string(skel->count) + " bones, over " +
                        std::to_string(max_bones);
        return std::nullopt;
    }
    std::vector<LocalMtx> bones(skel->count);
    if (!guest.Read(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) {
        if (why) *why = "skeleton not readable";
        return std::nullopt;
    }
    for (const LocalMtx& b : bones) {
        if (!std::isfinite(b.rows[0]) || !std::isfinite(b.translation[0])) {
            if (why) *why = "not a number in the skeleton";
            return std::nullopt;
        }
    }
    const std::uint32_t id = SkeletonId(skel->count, skel->model);
    if (actor != actor_ || id != skeleton_ || first_.size() != bones.size()) {
        actor_ = actor;
        skeleton_ = id;
        count_ = skel->count;
        first_.resize(bones.size());
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const LocalMtx& b = bones[i];
            first_[i] = {b.translation[0], b.translation[1], b.translation[2],
                         b.scale[0],       b.scale[1],       b.scale[2]};
        }
        moving_.assign(bones.size(), 0);
    }
    Pose pose;
    pose.skeleton = id;
    pose.bones.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const LocalMtx& b = bones[i];
        std::uint8_t& m = moving_[i];
        if (Moved(b.translation, first_[i].data(), 3)) m |= proto::pose_part::translation;
        if (Moved(b.scale, first_[i].data() + 3, 3)) m |= proto::pose_part::scale;
        BonePose out;
        out.bone = static_cast<std::uint16_t>(i);
        out.flags = static_cast<std::uint8_t>((b.flags & addr::kFlagMask) >> addr::kFlagShift);
        out.rotation = RowsToQuat(RowsOf(b));
        if (m & proto::pose_part::translation) {
            out.translation = Vec3{b.translation[0], b.translation[1], b.translation[2]};
        }
        if (m & proto::pose_part::scale) out.scale = Vec3{b.scale[0], b.scale[1], b.scale[2]};
        pose.bones.push_back(out);
    }
    return pose;
}

WriteResult WriteActorPose(CoopPuppet::Guest& guest, std::uint64_t actor, const Pose& pose,
                           WriteProbe* probe, std::optional<bool>* kept, float weight) {
    if (kept) kept->reset();
    weight = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 1.0f;
    const auto skel = actor != 0 ? FindSkeleton(guest, actor) : std::nullopt;
    if (!skel) return WriteResult::no_skeleton;
    if (SkeletonId(skel->count, skel->model) != pose.skeleton) return WriteResult::other_skeleton;
    std::vector<LocalMtx> bones(skel->count);
    if (!guest.Read(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) {
        return WriteResult::failed;
    }
    if (probe && probe->set && probe->actor == actor && probe->local == skel->local &&
        probe->bone < bones.size() && kept) {
        *kept = !Moved(bones[probe->bone].rows, probe->rows.data(), 12);
    }
    std::optional<std::uint16_t> first_rotated;
    for (const BonePose& b : pose.bones) {
        if (b.bone >= bones.size()) continue;
        if (BlendBone(bones[b.bone], b, weight) && !first_rotated) first_rotated = b.bone;
    }
    if (!guest.Write(skel->local, bones.data(), bones.size() * sizeof(LocalMtx))) {
        return WriteResult::failed;
    }
    if (probe) {
        probe->set = first_rotated.has_value();
        if (first_rotated) {
            probe->actor = actor;
            probe->local = skel->local;
            probe->bone = *first_rotated;
            std::copy_n(bones[*first_rotated].rows, 12, probe->rows.data());
        }
    }
    return WriteResult::written;
}

}  // namespace CoopPose
