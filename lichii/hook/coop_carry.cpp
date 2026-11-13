// SPDX-License-Identifier: GPL-3.0-or-later
// ORM "Levantar": see coop_carry.h.

#include "coop_carry.h"

#include "coop_check_visual.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "coop_objects_sync.h"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace CoopCarry {

namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kActorPosition = 0x31c;
constexpr std::uint64_t kActorRotation = 0x328;

std::atomic<bool>& FlowersFlag() {
    static std::atomic<bool> on{false};
    return on;
}

bool Finite(const Vec3& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float Length(const Vec3& v) {
    return std::sqrt(Dot(v, v));
}

float Distance(const Vec3& a, const Vec3& b) {
    return Length({a[0] - b[0], a[1] - b[1], a[2] - b[2]});
}

Mat3 Multiply(const Mat3& a, const Mat3& b) {
    Mat3 m{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            m[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
        }
    }
    return m;
}

Mat3 Transpose(const Mat3& m) {
    return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

Vec3 Times(const Mat3& m, const Vec3& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Quat Nlerp(const Quat& a, Quat b, float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0f) {
        for (float& v : b) {
            v = -v;
        }
    }
    Quat q{};
    float n = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = a[i] + (b[i] - a[i]) * t;
        n += q[i] * q[i];
    }
    n = std::sqrt(n);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        return b;
    }
    for (float& v : q) {
        v /= n;
    }
    return q;
}

Vec3 LerpV(const Vec3& a, const Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

bool HasPrefix(std::string_view name, std::string_view prefix) {
    return name.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view name, std::string_view suffix) {
    return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
}

template <typename T>
bool ReadValue(CoopPuppet::Guest& guest, std::uint64_t address, T& out) {
    return address >= 0x1000 && guest.Read(address, &out, sizeof(T));
}

// An actor's turn made rigid; no turn when it is not read or not one.
Mat3 TurnOf(const std::optional<Mat3>& m);

// Where `at` is from our Link's feet: sideways and up.
std::pair<float, float> FromFeet(const Frame& link, const Vec3& at) {
    const float dx = at[0] - link.p[0];
    const float dz = at[2] - link.p[2];
    return {std::sqrt(dx * dx + dz * dz), at[1] - link.p[1]};
}

}  // namespace

// ---------------------------------------------------------------- names

bool IsBombFlower(std::string_view name) {
    return name == "Obj_BombFlower_A_01" || name == "Obj_BombFlower_A_04" ||
           HasPrefix(name, "Obj_BombFlower_Fruit_");
}

bool IsLiftable(std::string_view name) {
    if (HasPrefix(name, "Obj_Pot_") || HasPrefix(name, "Obj_LiftRock_") ||
        name == "Obj_BrokenBox_A_03" || HasPrefix(name, "Obj_Plant_Grass_A_") ||
        HasPrefix(name, "Npc_Zora_Ruto_Child_") || IsBombFlower(name)) {
        return true;
    }
    // Cuccos: every Animal_Cucco_* (the plain one, Kakariko's, the market's,
    // the minigames' and the save-spot one) and the Super Cucco; the flock that
    // attacks is never lifted.
    return name == "Animal_Cucco" || HasPrefix(name, "Animal_Cucco_") ||
           name == "Animal_SuperCucco";
}

void SetShareFlowers(bool on) {
    FlowersFlag().store(on, std::memory_order_relaxed);
}

bool ShareFlowers() {
    return FlowersFlag().load(std::memory_order_relaxed);
}

Phase PhaseOf(std::string_view c) {
    // Link's AS (Player_Child and Player_Adult have the same names):
    // Lift_Ready_<kind>, Lift_Up_<kind> (Pod, Rock, RockM, Box, Cucco, Plant,
    // Ruto, Bomb, with _High_ variants), Lift_Wait_RockM, Lift_Throw_<kind>,
    // Lift_Put_[Stand_][High_]<kind>, Lift_Drop, Lift_Fail, Lift_Fail_Stand;
    // the older one-word names LiftReady, LiftUp, LiftWait, LiftStand,
    // LiftThrow, LiftPut, LiftDrop, LiftFail are there too.
    if (HasPrefix(c, "Lift_Throw") || HasPrefix(c, "LiftThrow")) return Phase::thrown;
    if (HasPrefix(c, "Lift_Put") || HasPrefix(c, "LiftPut")) return Phase::put;
    if (HasPrefix(c, "Lift_Drop") || HasPrefix(c, "LiftDrop") || HasPrefix(c, "Lift_Fail") ||
        HasPrefix(c, "LiftFail")) {
        return Phase::dropped;
    }
    if (HasPrefix(c, "Lift_Ready") || HasPrefix(c, "Lift_Up") || HasPrefix(c, "Lift_Wait") ||
        HasPrefix(c, "LiftReady") || HasPrefix(c, "LiftUp") || HasPrefix(c, "LiftWait") ||
        HasPrefix(c, "LiftStand")) {
        return Phase::lift;
    }
    return Phase::none;
}

bool IsLiftStart(std::string_view c) {
    return HasPrefix(c, "Lift_Ready") || HasPrefix(c, "Lift_Up") || HasPrefix(c, "LiftReady") ||
           HasPrefix(c, "LiftUp");
}

bool IsBombLift(std::string_view c) {
    return PhaseOf(c) != Phase::none && (EndsWith(c, "_Bomb") || EndsWith(c, "_BombChu"));
}

const char* NameOf(Release why) {
    switch (why) {
    case Release::thrown: return "thrown";
    case Release::put: return "put down";
    case Release::dropped: return "dropped";
    case Release::lost: return "out of Link's hands";
    case Release::gone: return "gone from the walk";
    case Release::scene: return "scene left";
    case Release::no_link: return "our Link not seen";
    }
    return "?";
}

// ---------------------------------------------------------------- frames

Vec3 Apply(const Frame& f, const Vec3& local) {
    const Vec3 v = Times(f.r, local);
    return {v[0] + f.p[0], v[1] + f.p[1], v[2] + f.p[2]};
}

Frame Compose(const Frame& a, const Frame& b) {
    return {Apply(a, b.p), Multiply(a.r, b.r)};
}

Frame Inverse(const Frame& f) {
    const Mat3 t = Transpose(f.r);
    const Vec3 v = Times(t, f.p);
    return {{-v[0], -v[1], -v[2]}, t};
}

Frame Relative(const Frame& anchor, const Frame& object) {
    return Compose(Inverse(anchor), object);
}

Frame Blend(const Frame& a, const Frame& b, float t) {
    t = std::clamp(std::isfinite(t) ? t : 1.0f, 0.0f, 1.0f);
    return {LerpV(a.p, b.p, t), RowsOfQuat(Nlerp(QuatOfRows(a.r), QuatOfRows(b.r), t))};
}

std::optional<Mat3> Rigid(const Mat3& m) {
    for (const float v : m) {
        if (!std::isfinite(v)) {
            return std::nullopt;
        }
    }
    const Vec3 c0{m[0], m[3], m[6]};
    const Vec3 c1{m[1], m[4], m[7]};
    const float l0 = Length(c0);
    if (!(l0 > 1e-4f)) {
        return std::nullopt;
    }
    const Vec3 e0{c0[0] / l0, c0[1] / l0, c0[2] / l0};
    const float d = Dot(c1, e0);
    const Vec3 u1{c1[0] - d * e0[0], c1[1] - d * e0[1], c1[2] - d * e0[2]};
    const float l1 = Length(u1);
    if (!(l1 > 1e-4f) || l1 < 0.1f * Length(c1)) {
        return std::nullopt;  // the first two columns (nearly) in line
    }
    const Vec3 e1{u1[0] / l1, u1[1] / l1, u1[2] / l1};
    const Vec3 e2 = Cross(e0, e1);  // a turn, even where the bone is mirrored
    return Mat3{e0[0], e1[0], e2[0], e0[1], e1[1], e2[1], e0[2], e1[2], e2[2]};
}

Quat QuatOfRows(const Mat3& m) {
    return CoopObjects::QuatOf(m);
}

Mat3 RowsOfQuat(const Quat& q) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!(n > 1e-8f) || !std::isfinite(n)) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    n = std::sqrt(n);
    return CoopObjects::MatOf({q[0] / n, q[1] / n, q[2] / n, q[3] / n});
}

proto::ObjectCarry ToCarry(std::uint8_t holder, const Local& local) {
    proto::ObjectCarry c;
    c.holder = holder;
    c.offset = local.actor.p;
    c.rotation = QuatOfRows(local.actor.r);
    if (local.bone) {
        c.bone = *local.bone;
        c.bone_offset = local.bone_frame.p;
        c.bone_rotation = QuatOfRows(local.bone_frame.r);
    }
    return c;
}

Local FromCarry(const proto::ObjectCarry& c) {
    Local l;
    l.actor = {c.offset, RowsOfQuat(c.rotation)};
    if (c.bone != proto::kCarryNoBone) {
        l.bone = c.bone;
        l.bone_frame = {c.bone_offset, RowsOfQuat(c.bone_rotation)};
    }
    return l;
}

proto::ObjectCarry BlendCarry(const proto::ObjectCarry& a, const proto::ObjectCarry& b, float t) {
    t = std::clamp(std::isfinite(t) ? t : 1.0f, 0.0f, 1.0f);
    proto::ObjectCarry c = a;
    c.offset = LerpV(a.offset, b.offset, t);
    c.rotation = Nlerp(a.rotation, b.rotation, t);
    if (a.bone == b.bone && a.bone != proto::kCarryNoBone) {
        c.bone_offset = LerpV(a.bone_offset, b.bone_offset, t);
        c.bone_rotation = Nlerp(a.bone_rotation, b.bone_rotation, t);
    } else {
        c.bone = proto::kCarryNoBone;
        c.bone_offset = {};
        c.bone_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return c;
}

// ---------------------------------------------------------------- memory

std::optional<Skeleton> FindSkeleton(CoopPuppet::Guest& guest, std::uint64_t actor) {
    // The same walk as CoopPose's: actor, +0x270, +0x8, +0x18, +0x18, then
    // the skeleton object at +0x140 of the model unit.
    std::uint64_t at = actor;
    for (const std::uint64_t step : CoopPose::addr::kSkeletonPath) {
        if (!ReadValue(guest, at + step, at) || at == 0) {
            return std::nullopt;
        }
    }
    std::uint16_t count = 0;
    Skeleton s;
    if (!ReadValue(guest, at + CoopPose::addr::kBoneCount, count) || count == 0 ||
        count > CoopPose::kMaxBones || !ReadValue(guest, at + kWorldArray, s.world) ||
        s.world < 0x1000) {
        return std::nullopt;
    }
    s.count = count;
    // The layout, from bone 0: a 4x4 whose 4th column is (0 0 0 1) and whose
    // place reads near the actor (or the origin); else the old 3x4 if that
    // reads; else 4x4 (BoneWorld refuses what does not read).
    std::array<float, 16> m{};
    const auto actor_frame = ReadActor(guest, actor);
    if (ReadValue(guest, s.world, m) && actor_frame) {
        const bool w_row = std::fabs(m[3]) < 1e-3f && std::fabs(m[7]) < 1e-3f && std::fabs(m[11]) < 1e-3f &&
                           std::fabs(m[15] - 1.0f) < 1e-3f;
        Skeleton t = s;
        t.layout = Layout::m44;
        const bool m44 = w_row && BoneWorld(*actor_frame, RowsOf(t, m.data()));
        t.layout = Layout::m34;
        const bool m34 = BoneWorld(*actor_frame, RowsOf(t, m.data())).has_value();
        s.layout = m44 || !m34 ? Layout::m44 : Layout::m34;
        if (s.layout == Layout::m44) {
            // By rows or by columns: the one nearer the actor's own turn (a
            // tie, a model turned apart from its actor: by rows).
            const Mat3& a = actor_frame->r;
            float rows = 0.0f, cols = 0.0f;
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 3; ++c) {
                    rows += std::fabs(m[4 * r + c] - a[3 * r + c]);
                    cols += std::fabs(m[4 * c + r] - a[3 * r + c]);
                }
            }
            s.columns = cols + 1e-3f < rows;
        }
    }
    return s;
}

const char* NameOf(const Skeleton& s) {
    return s.layout == Layout::m34 ? "3x4 (old)" : s.columns ? "4x4 by columns" : "4x4 by rows";
}

std::array<float, 12> RowsOf(const Skeleton& s, const float* m) {
    if (s.layout == Layout::m34) {
        std::array<float, 12> rows{};
        std::memcpy(rows.data(), m, sizeof rows);
        return rows;
    }
    const auto at = [&](std::size_t r, std::size_t c) { return s.columns ? m[4 * c + r] : m[4 * r + c]; };
    return {at(0, 0), at(0, 1), at(0, 2), m[12], at(1, 0), at(1, 1), at(1, 2), m[13],
            at(2, 0), at(2, 1), at(2, 2), m[14]};
}

std::optional<Frame> ReadActor(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::array<float, 3> p{};
    std::array<float, 9> m{};
    if (!ReadValue(guest, actor + kActorPosition, p) || !ReadValue(guest, actor + kActorRotation, m) ||
        !Finite(p)) {
        return std::nullopt;
    }
    const auto r = Rigid(m);
    if (!r) {
        return std::nullopt;
    }
    return Frame{p, *r};
}

std::optional<Frame> BoneWorld(const Frame& actor, const std::array<float, 12>& rows) {
    const Mat3 m{rows[0], rows[1], rows[2], rows[4], rows[5], rows[6], rows[8], rows[9], rows[10]};
    const Vec3 t{rows[3], rows[7], rows[11]};
    const auto r = Rigid(m);
    if (!r || !Finite(t)) {
        return std::nullopt;
    }
    const float from_actor = Distance(t, actor.p);
    const float from_origin = Length(t);
    const bool world = from_actor <= kBoneReachM;
    const bool model = from_origin <= kBoneReachM;
    if (world && (!model || from_actor <= from_origin)) {
        return Frame{t, *r};
    }
    if (model) {
        return Compose(actor, Frame{t, *r});
    }
    return std::nullopt;
}

std::optional<Frame> ReadBone(CoopPuppet::Guest& guest, const Skeleton& skeleton,
                              const Frame& actor, std::uint16_t bone) {
    if (bone >= skeleton.count) {
        return std::nullopt;
    }
    std::array<float, 16> m{};
    if (!guest.Read(skeleton.world + skeleton.Stride() * bone, m.data(), skeleton.Stride())) {
        return std::nullopt;
    }
    return BoneWorld(actor, RowsOf(skeleton, m.data()));
}

std::optional<std::pair<std::uint16_t, float>> NearestBone(CoopPuppet::Guest& guest,
                                                           const Skeleton& skeleton,
                                                           const Frame& actor, const Vec3& at) {
    if (skeleton.count == 0 || skeleton.count > CoopPose::kMaxBones || !Finite(at)) {
        return std::nullopt;
    }
    const std::size_t per = skeleton.Stride() / 4;
    std::vector<float> all(static_cast<std::size_t>(skeleton.count) * per);
    if (!guest.Read(skeleton.world, all.data(), all.size() * sizeof(float))) {
        return std::nullopt;
    }
    std::optional<std::pair<std::uint16_t, float>> best;
    for (std::uint32_t i = 0; i < skeleton.count; ++i) {
        const auto f = BoneWorld(actor, RowsOf(skeleton, all.data() + i * per));
        if (!f) {
            continue;
        }
        const float d = Distance(f->p, at);
        if (!best || d < best->second) {
            best = std::pair{static_cast<std::uint16_t>(i), d};
        }
    }
    return best;
}

std::optional<Frame> Attached(CoopPuppet::Guest& guest, std::uint64_t puppet,
                              const proto::ObjectCarry& carry, bool use_bone, Anchor* used) {
    if (used != nullptr) {
        *used = Anchor::none;
    }
    const auto actor = ReadActor(guest, puppet);
    if (!actor) {
        return std::nullopt;
    }
    const Local local = FromCarry(carry);
    if (use_bone && local.bone) {
        const auto skeleton = FindSkeleton(guest, puppet);
        if (skeleton) {
            if (const auto bone = ReadBone(guest, *skeleton, *actor, *local.bone)) {
                if (used != nullptr) {
                    *used = Anchor::bone;
                }
                // ORM riesgos-visual: the bone matrices, read from the romfs
                // and memory but never used before, are what this expects.
                CoopVCheck::Ok("carry bone", "the puppet's bone " + std::to_string(*local.bone) +
                                                 " reads as a rigid frame near it: lifted things follow its hands (" +
                                                 NameOf(*skeleton) + ")");
                return Compose(*bone, local.bone_frame);
            }
        }
        CoopVCheck::Fallback("carry bone", "the puppet's bone " + std::to_string(*local.bone) +
                                               (skeleton ? " does not read as a rigid frame near it"
                                                         : ": its skeleton was not found") +
                                               ": lifted things follow its actor frame instead");
    }
    if (used != nullptr) {
        *used = Anchor::actor;
    }
    return Compose(*actor, local.actor);
}

// ---------------------------------------------------------------- HoldWatch

void HoldWatch::Note(std::uint64_t actor, const Vec3& read_now, const Vec3& placed) {
    auto& w = watch_[actor];
    if (w.last) {
        const float drop = (*w.last)[1] - read_now[1];
        const float dx = (*w.last)[0] - read_now[0], dz = (*w.last)[2] - read_now[2];
        const float side = std::sqrt(dx * dx + dz * dz);
        if (std::isfinite(drop) && std::isfinite(side) && side < kFarM) {
            ++w.frames;
            if (drop > kFallM) {
                ++w.falls;
            }
            w.largest = std::max(w.largest, drop);
        }
    }
    w.last = placed;
    if (w.frames < kJudgeFrames || (judged_ && (!falling_ || fix_judged_))) {
        return;
    }
    char buf[200];
    if (judged_) {
        // The second judgement: with the pending place written too (Falling).
        fix_judged_ = true;
        if (w.falls >= kFallFrames) {
            std::snprintf(buf, sizeof(buf),
                          "the pending place did not stop it: still fell in %d of %d frames (up to %.3f); a "
                          "stand-in with no physics while carried (CoopLift_*, as the bomb's) is the next step",
                          w.falls, w.frames, static_cast<double>(w.largest));
            CoopVCheck::Fallback("carry hold fix", buf);
        } else {
            std::snprintf(buf, sizeof(buf), "with the pending place written the carried copy held (largest drop %.3f in %d frames)",
                          static_cast<double>(std::max(0.0f, w.largest)), w.frames);
            CoopVCheck::Ok("carry hold fix", buf);
        }
        return;
    }
    judged_ = true;
    if (w.falls >= kFallFrames) {
        falling_ = true;
        std::snprintf(buf, sizeof(buf),
                      "a carried copy fell between our writes in %d of %d frames (up to %.3f a frame): its "
                      "physics body keeps its speed (issue 169); written with the setter and its pending place",
                      w.falls, w.frames, static_cast<double>(w.largest));
        CoopVCheck::Fallback("carry hold", buf);
        for (auto& [address, one] : watch_) {
            one = One{one.last, 0, 0, 0.0f};  // judged again with the fix
        }
    } else {
        std::snprintf(buf, sizeof(buf), "carried copies held still between our writes (largest drop %.3f in %d frames)",
                      static_cast<double>(std::max(0.0f, w.largest)), w.frames);
        CoopVCheck::Ok("carry hold", buf);
    }
}

void HoldWatch::Forget(std::uint64_t actor) {
    watch_.erase(actor);
}

// ---------------------------------------------------------------- Still

std::optional<Body> ReadBody(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t at = actor;
    for (const std::uint64_t offset : still::kRootPath) {
        std::uint64_t next = 0;
        if (at == 0 || !guest.Read(at + offset, &next, 8) || next == 0) {
            return std::nullopt;
        }
        at = next;
    }
    Body b;
    if (!guest.Read(at + still::kRootBody, &b.address, 8) || b.address == 0 ||
        !guest.Read(b.address + still::kBodyFlags, &b.flags, 8) ||
        !guest.Read(b.address + still::kBodyLinear, b.linear.data(), 12) ||
        !guest.Read(b.address + still::kBodyAngular, b.angular.data(), 12) || !Finite(b.linear) ||
        !Finite(b.angular)) {
        return std::nullopt;
    }
    return b;
}

void Still::Frame() {
    for (auto it = held_.begin(); it != held_.end();) {
        One& one = it->second;
        if (one.placed) {
            one.placed = false;
            one.missed = 0;
            ++it;
            continue;
        }
        if (++one.missed < kLetGoFrames) {
            ++it;
            continue;
        }
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "coop: carry: copy %#llx out of the puppet's hands: its own physics again "
                      "(held still %llu frames)",
                      static_cast<unsigned long long>(it->first),
                      static_cast<unsigned long long>(one.frames));
        lines_.emplace_back(buf);
        it = held_.erase(it);
    }
}

std::vector<GameCall> Still::Hold(CoopPuppet::Guest& guest, std::uint64_t main, std::uint64_t actor,
                                  std::uint64_t zero, Cell cell) {
    char buf[400];
    const auto fallback = [&](const char* why) {
        std::snprintf(buf, sizeof(buf),
                      "copy %#llx in a puppet's hands: %s; placed with the setter and its pending "
                      "place (it may fall between places)",
                      static_cast<unsigned long long>(actor), why);
        CoopVCheck::Fallback("carry still", buf);
        held_.erase(actor);  // not held still now: no "out of the hands" line for it later
        return std::vector<GameCall>{};
    };
    if (!on_) {
        return fallback("the zeroing is off (LICHII_COOP_CARRY_STILL=0)");
    }
    if (main == 0 || actor == 0) {
        return {};
    }
    if (cell == Cell::coming) {
        return {};  // a frame or two: the mover's block is being made
    }
    if (cell == Cell::none || zero == 0) {
        return fallback("no zero cell for the speed calls (the mover's block was not made)");
    }
    const auto body = ReadBody(guest, actor);
    if (!body) {
        return fallback("no physics body read at actor+0x270+0x50+0x20+0x190");
    }
    if ((body->flags & still::kLinearKept) != 0) {
        std::snprintf(buf, sizeof(buf), "its body %#llx has flags %#llx: bit 30 keeps its linear speed",
                      static_cast<unsigned long long>(body->address),
                      static_cast<unsigned long long>(body->flags));
        const std::string why(buf);
        return fallback(why.c_str());
    }
    One& one = held_[actor];
    one.placed = true;
    one.missed = 0;
    if (one.frames++ == 0) {
        std::snprintf(buf, sizeof(buf),
                      "copy %#llx in a puppet's hands: its body %#llx (flags %#llx) had linear speed "
                      "(%.2f, %.2f, %.2f); zeroed with the game's own calls after each place: a still "
                      "double while carried, its own physics again when let go%s",
                      static_cast<unsigned long long>(actor),
                      static_cast<unsigned long long>(body->address),
                      static_cast<unsigned long long>(body->flags), static_cast<double>(body->linear[0]),
                      static_cast<double>(body->linear[1]), static_cast<double>(body->linear[2]),
                      (body->flags & still::kAngularKept) != 0 ? " (its turning speed is kept: bit 31)" : "");
        CoopVCheck::Ok("carry still", buf);
    }
    ++still_frames_;
    std::vector<GameCall> calls;
    calls.push_back(GameCall{main + still::kSetLinearVelocity, {body->address, zero, 0}});
    if ((body->flags & still::kAngularKept) == 0) {
        calls.push_back(GameCall{main + still::kSetAngularVelocity, {body->address, zero, 0}});
    }
    return calls;
}

std::vector<std::string> Still::TakeLines() {
    return std::exchange(lines_, {});
}

std::size_t Still::TakeStillFrames() {
    return std::exchange(still_frames_, 0);
}

void Still::Reset() {
    held_.clear();
    lines_.clear();
    still_frames_ = 0;
}

// ---------------------------------------------------------------- ours

namespace {

Mat3 TurnOf(const std::optional<Mat3>& m) {
    const Mat3 none{1, 0, 0, 0, 1, 0, 0, 0, 1};
    return m ? Rigid(*m).value_or(none) : none;
}

}  // namespace

void Lifter::Update(const std::string& scene, const std::optional<LinkNow>& link,
                    const std::vector<Candidate>& candidates, std::uint64_t now_us,
                    const BoneFinder& find, const BoneReader& bone) {
    if (scene != scene_) {
        if (hold_) {
            LetGo(Release::scene, now_us);
        }
        let_go_.clear();  // the objects of the old scene are gone
        scene_ = scene;
    }
    std::erase_if(let_go_, [&](const auto& kv) {
        return now_us < kv.second || now_us - kv.second >= kAfterUs;
    });
    if (!link) {
        starting_ = false;
        if (hold_) {
            LetGo(Release::no_link, now_us);
        }
        return;
    }
    const Phase phase = link->command ? PhaseOf(*link->command) : Phase::none;
    const bool start = link->command && IsLiftStart(*link->command);
    if (start && !starting_) {
        start_since_us_ = now_us;
    }
    starting_ = start;
    if (hold_) {
        const auto it = std::find_if(candidates.begin(), candidates.end(),
                                     [&](const Candidate& c) { return c.address == hold_->address; });
        if (it == candidates.end()) {
            LetGo(Release::gone, now_us);
            return;
        }
        if (phase == Phase::thrown) {
            LetGo(Release::thrown, now_us);
            return;
        }
        if (phase == Phase::put) {
            LetGo(Release::put, now_us);
            return;
        }
        if (phase == Phase::dropped) {
            LetGo(Release::dropped, now_us);
            return;
        }
        const auto [side, up] = FromFeet(link->frame, it->position);
        if (side > kHoldReachM || up < kLowM || up > kHighM || !Finite(it->position)) {
            if (++hold_->out >= kLostWalks) {
                LetGo(Release::lost, now_us);
                return;
            }
        } else {
            hold_->out = 0;
        }
        const Frame object{it->position, TurnOf(it->rotation)};
        Local l;
        l.actor = Relative(link->frame, object);
        if (hold_->bone && bone) {
            if (const auto b = bone(*hold_->bone)) {
                l.bone = hold_->bone;
                l.bone_frame = Relative(*b, object);
            }
        }
        hold_->local = l;
        return;
    }
    if (!start || now_us < start_since_us_ || now_us - start_since_us_ > kStartWindowUs) {
        return;
    }
    const bool bomb = IsBombLift(*link->command);
    const Vec3 forward{link->frame.r[2], 0.0f, link->frame.r[8]};
    const Candidate* best = nullptr;
    float best_score = std::numeric_limits<float>::infinity();
    for (const Candidate& c : candidates) {
        if (c.held_by_other || !IsLiftable(c.name) || !Finite(c.position) ||
            (bomb && !IsBombFlower(c.name)) || let_go_.contains(c.address)) {
            continue;
        }
        const auto [side, up] = FromFeet(link->frame, c.position);
        if (side > kReachM || up < kLowM || up > kHighM) {
            continue;
        }
        const Vec3 to{c.position[0] - link->frame.p[0], 0.0f, c.position[2] - link->frame.p[2]};
        const float score = side + (Dot(forward, to) < 0.0f ? kBehindPenaltyM : 0.0f);
        if (score < best_score) {
            best_score = score;
            best = &c;
        }
    }
    if (best == nullptr) {
        return;
    }
    Hold h;
    h.address = best->address;
    h.name = best->name;
    h.since_us = now_us;
    if (find) {
        if (const auto near = find(best->position); near && near->second <= kBoneNearM) {
            h.bone = near->first;
        }
    }
    const Frame object{best->position, TurnOf(best->rotation)};
    Local l;
    l.actor = Relative(link->frame, object);
    if (h.bone && bone) {
        if (const auto b = bone(*h.bone)) {
            l.bone = h.bone;
            l.bone_frame = Relative(*b, object);
        }
    }
    h.local = l;
    Event e;
    e.kind = Event::Kind::lifted;
    e.address = h.address;
    e.name = h.name;
    e.bone = h.bone;
    events_.push_back(std::move(e));
    hold_ = std::move(h);
}

void Lifter::LetGo(Release why, std::uint64_t now_us) {
    if (!hold_) {
        return;
    }
    Event e;
    e.kind = Event::Kind::released;
    e.address = hold_->address;
    e.name = hold_->name;
    e.why = why;
    e.bone = hold_->bone;
    events_.push_back(std::move(e));
    if (why != Release::scene) {
        let_go_[hold_->address] = now_us;
    }
    hold_.reset();
}

std::optional<std::uint64_t> Lifter::Held() const {
    if (!hold_) {
        return std::nullopt;
    }
    return hold_->address;
}

std::set<std::uint64_t> Lifter::Claimed(std::uint64_t now_us) const {
    std::set<std::uint64_t> out;
    if (hold_) {
        out.insert(hold_->address);
    }
    for (const auto& [address, at] : let_go_) {
        if (now_us >= at && now_us - at < kAfterUs) {
            out.insert(address);
        }
    }
    return out;
}

std::optional<proto::ObjectCarry> Lifter::CarryOf(std::uint64_t address, std::uint8_t self) const {
    if (!hold_ || hold_->address != address || !hold_->local || self == 0 ||
        self > proto::kMaxPlayers) {
        return std::nullopt;
    }
    return ToCarry(self, *hold_->local);
}

std::vector<Event> Lifter::TakeEvents() {
    return std::exchange(events_, {});
}

void Lifter::Reset() {
    hold_.reset();
    let_go_.clear();
    scene_.clear();
    events_.clear();
    starting_ = false;
    start_since_us_ = 0;
}

} // namespace CoopCarry
