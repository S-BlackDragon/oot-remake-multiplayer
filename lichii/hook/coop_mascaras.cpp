// SPDX-License-Identifier: GPL-3.0-or-later
// Masks, the fishing line and the hookshot chain on other players' puppets
// (coop_mascaras.h). No emulator code: tests/lichii/mascaras_test.cpp.
#include "coop_mascaras.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "coop_check_visual.h"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace CoopMascaras {
namespace {

template <typename T>
bool ReadValue(CoopPuppet::Guest& guest, std::uint64_t at, T& out) {
    return at != 0 && guest.Read(at, &out, sizeof(T));
}

template <typename T>
bool WriteValue(CoopPuppet::Guest& guest, std::uint64_t at, const T& value) {
    return at != 0 && guest.Write(at, &value, sizeof(T));
}

constexpr float kEps = 1e-6f;

Vec3 Sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 Scale(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float Dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 Column(const Mat3& m, int c) { return {m[c], m[3 + c], m[6 + c]}; }
bool Finite(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string Num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    return buf;
}

std::atomic<int> g_rows{-1};  // -1: not told yet

}  // namespace

const char* RowsText(Rows rows) { return rows == Rows::axes ? "axes" : "matrix"; }

// ---------------------------------------------------------------- math

Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] +
                             a[r * 3 + 2] * b[2 * 3 + c];
        }
    }
    return out;
}

Vec3 Apply(const Mat3& m, const Vec3& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Mat3 Transposed(const Mat3& m) { return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}; }

float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

Mat3 Orthonormal(const Mat3& m) {
    Vec3 x = Column(m, 0);
    Vec3 y = Column(m, 1);
    const float lx = Length(x);
    if (!(lx > kEps) || !std::isfinite(lx)) {
        return kIdentity;
    }
    x = Scale(x, 1.0f / lx);
    y = Sub(y, Scale(x, Dot(x, y)));
    float ly = Length(y);
    if (!(ly > kEps) || !std::isfinite(ly)) {
        // y lay along x: any axis across x.
        y = std::fabs(x[1]) < 0.9f ? Cross(Vec3{0, 1, 0}, x) : Cross(Vec3{1, 0, 0}, x);
        y = Cross(x, y);
        ly = Length(y);
        if (!(ly > kEps)) {
            return kIdentity;
        }
    }
    y = Scale(y, 1.0f / ly);
    const Vec3 z = Cross(x, y);
    return {x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]};
}

Mat3 ToParent(const Local& local, Rows rows) {
    Mat3 m{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = rows == Rows::matrix ? local.rows[r * 4 + c] : local.rows[c * 4 + r];
        }
    }
    // The child's point is scaled first: column c times scale c.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] *= local.scale[c];
        }
    }
    return m;
}

void SetRotation(Local& local, const Mat3& rotation, Rows rows) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            local.rows[r * 4 + c] = rows == Rows::matrix ? rotation[r * 3 + c] : rotation[c * 3 + r];
        }
        local.rows[r * 4 + 3] = 0.0f;
    }
    local.scale[0] = local.scale[1] = local.scale[2] = 1.0f;
}

Frame BoneFrame(const std::vector<Local>& locals, const std::vector<std::int16_t>& parents,
                std::size_t bone, Rows rows) {
    std::vector<std::size_t> path;  // the bone, then its parents up to the root
    std::size_t at = bone;
    while (at < locals.size() && path.size() <= locals.size()) {
        path.push_back(at);
        if (at >= parents.size() || parents[at] < 0) {
            break;
        }
        at = static_cast<std::size_t>(parents[at]);
    }
    Frame f;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Local& l = locals[*it];
        f.origin = Add(Apply(f.rot, {l.translation[0], l.translation[1], l.translation[2]}), f.origin);
        f.rot = Mul(f.rot, ToParent(l, rows));
    }
    return f;
}

std::optional<Rows> DetectRows(const std::vector<Local>& locals,
                               const std::vector<std::int16_t>& parents, std::size_t head,
                               std::size_t hips, int* fits_out) {
    if (fits_out != nullptr) {
        *fits_out = 0;
    }
    if (head >= locals.size() || hips >= locals.size()) {
        return std::nullopt;
    }
    std::optional<Rows> found;
    int fits = 0;
    for (const Rows rows : {Rows::axes, Rows::matrix}) {
        const Vec3 h = BoneFrame(locals, parents, head, rows).origin;
        const Vec3 p = BoneFrame(locals, parents, hips, rows).origin;
        const float up = h[1] - p[1];
        const float aside = std::hypot(h[0] - p[0], h[2] - p[2]);
        if (std::isfinite(up) && std::isfinite(aside) && up >= kHeadAbove && aside <= kHeadAside) {
            found = rows;
            ++fits;
        }
    }
    if (fits_out != nullptr) {
        *fits_out = fits;
    }
    return fits == 1 ? found : std::nullopt;
}

Mat3 TurnXTo(const Mat3& from, const Vec3& dir) {
    const float ld = Length(dir);
    if (!(ld > kEps) || !std::isfinite(ld)) {
        return from;
    }
    const Vec3 d = Scale(dir, 1.0f / ld);
    Vec3 x = Column(from, 0);
    const float lx = Length(x);
    if (!(lx > kEps)) {
        return from;
    }
    x = Scale(x, 1.0f / lx);
    const Vec3 axis = Cross(x, d);
    const float s = Length(axis);
    const float c = std::clamp(Dot(x, d), -1.0f, 1.0f);
    Mat3 turn = kIdentity;
    if (!(s > kEps)) {
        if (c > 0.0f) {
            return from;
        }
        // Straight back: half a turn about the frame's own y axis.
        const Vec3 y = Column(Orthonormal(from), 1);
        // R = 2 y y^T - I
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k < 3; ++k) {
                turn[r * 3 + k] = 2.0f * y[r] * y[k] - (r == k ? 1.0f : 0.0f);
            }
        }
    } else {
        const Vec3 k = Scale(axis, 1.0f / s);
        // Rodrigues: I + sin K + (1 - cos) K^2, K the cross product with k.
        const Mat3 K{0, -k[2], k[1], k[2], 0, -k[0], -k[1], k[0], 0};
        const Mat3 K2 = Mul(K, K);
        for (int i = 0; i < 9; ++i) {
            turn[i] = kIdentity[i] + s * K[i] + (1.0f - c) * K2[i];
        }
    }
    return Orthonormal(Mul(turn, from));
}

std::vector<ChainBone> ChainLocals(const Frame& parent, const std::vector<Vec3>& points) {
    std::vector<ChainBone> out;
    out.reserve(points.size());
    Mat3 prev = Orthonormal(parent.rot);
    Vec3 prev_at = parent.origin;
    Vec3 dir = Column(prev, 0);
    for (std::size_t k = 0; k < points.size(); ++k) {
        Vec3 next{};
        if (k + 1 < points.size()) {
            next = Sub(points[k + 1], points[k]);
        } else {
            next = Sub(points[k], prev_at);  // the last one goes on the way it came
        }
        if (Length(next) > kEps) {
            dir = next;
        }
        const Mat3 frame = TurnXTo(prev, dir);
        const Mat3 back = Transposed(prev);
        ChainBone b;
        b.rot = Mul(back, frame);
        b.t = Apply(back, Sub(points[k], prev_at));
        out.push_back(b);
        prev = frame;
        prev_at = points[k];
    }
    return out;
}

std::vector<Vec3> HookPoints(const Vec3& start, const Vec3& end, float link, std::size_t count) {
    std::vector<Vec3> out;
    out.reserve(count);
    const Vec3 v = Sub(end, start);
    const float d = Length(v);
    const Vec3 u = d > kEps ? Scale(v, 1.0f / d) : Vec3{};
    for (std::size_t k = 0; k < count; ++k) {
        const float along = std::min(link * static_cast<float>(k + 1), d);
        out.push_back(Add(start, Scale(u, along)));
    }
    return out;
}

std::vector<Vec3> LinePoints(const Vec3& start, const Vec3& end, const Vec3& down,
                             std::size_t count, float sag) {
    std::vector<Vec3> out;
    if (count == 0) {
        return out;
    }
    out.reserve(count);
    const Vec3 v = Sub(end, start);
    const Vec3 across = Sub(v, Scale(down, Dot(v, down)));
    const float span = Length(across);
    for (std::size_t k = 0; k < count; ++k) {
        const float t = count == 1 ? 1.0f : static_cast<float>(k) / static_cast<float>(count - 1);
        const float dip = sag * span * 4.0f * t * (1.0f - t);
        out.push_back(Add(Add(start, Scale(v, t)), Scale(down, dip)));
    }
    return out;
}

std::uint32_t TransformBits(const Local& local) {
    const float* s = local.scale;
    std::uint32_t bits = 0;
    const bool one = std::fabs(s[0] - 1.0f) < kEps && std::fabs(s[1] - 1.0f) < kEps &&
                     std::fabs(s[2] - 1.0f) < kEps;
    if (one) {
        bits |= addr::kScaleOne;
    }
    if (std::fabs(s[0] - s[1]) < kEps && std::fabs(s[1] - s[2]) < kEps) {
        bits |= addr::kScaleUniform;
    }
    if (std::fabs(s[0] * s[1] * s[2] - 1.0f) < kEps) {
        bits |= addr::kScaleVolumeOne;
    }
    bool identity = true;
    for (int r = 0; r < 3 && identity; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (std::fabs(local.rows[r * 4 + c] - (r == c ? 1.0f : 0.0f)) >= kEps) {
                identity = false;
                break;
            }
        }
    }
    if (identity) {
        bits |= addr::kRotZero;
    }
    if (std::fabs(local.translation[0]) < kEps && std::fabs(local.translation[1]) < kEps &&
        std::fabs(local.translation[2]) < kEps) {
        bits |= addr::kTransZero;
    }
    return bits;
}

void Store(Local& local, const ChainBone& bone, Rows rows) {
    SetRotation(local, bone.rot, rows);
    local.translation[0] = bone.t[0];
    local.translation[1] = bone.t[1];
    local.translation[2] = bone.t[2];
    local.translation[3] = 0.0f;
    local.flags = (local.flags & ~addr::kTransformBits) | TransformBits(local);
}

// ---------------------------------------------------------------- guest

int Skeleton::Find(std::string_view name) const {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::optional<Skeleton> ReadSkeleton(CoopPuppet::Guest& guest, std::uint64_t actor, bool names) {
    namespace pa = CoopPose::addr;
    std::uint64_t at = actor;
    std::uint64_t holder = 0;  // the struct the model object sits in
    for (std::size_t i = 0; i < pa::kSkeletonPath.size(); ++i) {
        if (i + 1 == pa::kSkeletonPath.size()) {
            holder = at;
        }
        if (!ReadValue(guest, at + pa::kSkeletonPath[i], at) || at == 0) {
            return std::nullopt;
        }
    }
    const std::uint64_t object = at;
    std::uint64_t resource = 0;
    char magic[4]{};
    if (!ReadValue(guest, holder + pa::kModelResource, resource) ||
        !guest.Read(resource, magic, sizeof(magic)) || std::memcmp(magic, "FMDL", 4) != 0) {
        return std::nullopt;
    }
    Skeleton s;
    std::uint16_t count = 0;
    if (!ReadValue(guest, object + pa::kBoneCount, count) ||
        !ReadValue(guest, object + pa::kLocalArray, s.local) || s.local == 0 || count == 0 ||
        count > CoopPose::kMaxBones) {
        return std::nullopt;
    }
    s.count = count;
    std::uint64_t name = 0;
    std::uint16_t length = 0;
    char text[64]{};
    if (ReadValue(guest, resource + pa::kModelName, name) && ReadValue(guest, name, length) &&
        length > 0 && length < sizeof(text) && guest.Read(name + 2, text, length)) {
        s.model.assign(text, length);
    }
    std::uint64_t fskl = 0;
    ReadValue(guest, object + addr::kSkeletonResource, fskl);
    s.fskl = fskl;
    if (!names) {
        return s;
    }
    std::uint64_t bones = 0;
    std::uint16_t res_count = 0;
    if (fskl == 0 || !guest.Read(fskl, magic, sizeof(magic)) || std::memcmp(magic, "FSKL", 4) != 0 ||
        !ReadValue(guest, fskl + addr::kFsklBones, bones) || bones == 0 ||
        !ReadValue(guest, fskl + addr::kFsklBoneCount, res_count) || res_count != count) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> raw(std::size_t(count) * addr::kBoneStride);
    if (!guest.Read(bones, raw.data(), raw.size())) {
        return std::nullopt;
    }
    s.names.resize(count);
    s.parents.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* b = raw.data() + i * addr::kBoneStride;
        std::uint64_t name_at = 0;
        std::int16_t parent = -1;
        std::memcpy(&name_at, b + addr::kBoneName, sizeof(name_at));
        std::memcpy(&parent, b + addr::kBoneParent, sizeof(parent));
        s.parents[i] = parent >= 0 && parent < static_cast<int>(count) ? parent : std::int16_t{-1};
        std::uint16_t len = 0;
        if (ReadValue(guest, name_at, len) && len > 0 && len < sizeof(text) &&
            guest.Read(name_at + 2, text, len)) {
            s.names[i].assign(text, len);
        }
    }
    return s;
}

bool ReadLocals(CoopPuppet::Guest& guest, const Skeleton& skel, std::vector<Local>& out) {
    out.resize(skel.count);
    if (skel.count == 0 || !guest.Read(skel.local, out.data(), out.size() * sizeof(Local))) {
        return false;
    }
    for (const Local& l : out) {
        if (!std::isfinite(l.rows[0]) || !std::isfinite(l.translation[0])) {
            return false;
        }
    }
    return true;
}

bool WriteLocals(CoopPuppet::Guest& guest, const Skeleton& skel, const std::vector<Local>& locals) {
    return locals.size() == skel.count && skel.count != 0 &&
           guest.Write(skel.local, locals.data(), locals.size() * sizeof(Local));
}

Rows CurrentRows() {
    const int v = g_rows.load(std::memory_order_relaxed);
    return v < 0 ? kDefaultRows : static_cast<Rows>(v);
}

bool NoteRows(Rows rows) {
    const int was = g_rows.exchange(static_cast<int>(rows), std::memory_order_relaxed);
    return was != static_cast<int>(rows);
}

void RowsCheck(int fits, std::optional<Rows> rows, float up) {
    if (rows) {
        CoopVCheck::Ok("bone rows", std::string(RowsText(*rows)) + " (a puppet's head sits " + Num(up) +
                                        " over its hips only that way)");
    } else if (g_rows.load(std::memory_order_relaxed) < 0) {
        CoopVCheck::Fallback("bone rows", std::string(RowsText(kDefaultRows)) + " until a puppet's spine decides (" +
                                              (fits == 0 ? "neither way puts its head over its hips"
                                                         : "both ways put its head over its hips") +
                                              ")");
    }
}

namespace {

// Calc thread: skeletons with names, by actor, while their arrays stay the
// same (reading 309 names each frame would be most of a frame's reads).
std::map<std::uint64_t, Skeleton>& Cache() {
    static std::map<std::uint64_t, Skeleton> cache;
    return cache;
}

// Only between two uses: a pointer Cached gave stays good until then.
void TrimCache() {
    if (Cache().size() >= 64) {
        Cache().clear();
    }
}

const Skeleton* Cached(CoopPuppet::Guest& guest, std::uint64_t actor) {
    auto& cache = Cache();
    const auto quick = ReadSkeleton(guest, actor, false);
    auto it = cache.find(actor);
    if (!quick) {
        return nullptr;  // the entry, if any, is checked again next time
    }
    if (it != cache.end() && it->second.local == quick->local && it->second.count == quick->count &&
        it->second.model == quick->model && it->second.fskl == quick->fskl) {
        return &it->second;
    }
    auto full = ReadSkeleton(guest, actor, true);
    if (!full) {
        return nullptr;
    }
    return &(cache[actor] = std::move(*full));
}

}  // namespace

void Reset() {
    g_rows.store(-1, std::memory_order_relaxed);
    Cache().clear();
}

namespace {

// An actor's place and turn (+0x31c, +0x328).
bool Place(CoopPuppet::Guest& guest, std::uint64_t actor, Vec3& at, Mat3& rot) {
    return ReadValue(guest, actor + CoopPuppet::addr::kActorPosition, at) &&
           ReadValue(guest, actor + CoopPuppet::addr::kActorRotation, rot) && Finite(at);
}

std::uint64_t EquipmentUser(CoopPuppet::Guest& guest, std::uint64_t actor) {
    namespace pa = CoopPuppet::addr;
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    if (!ReadValue(guest, actor + pa::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + pa::kPartsEquipmentUser, eu)) {
        return 0;
    }
    return eu;
}

// The present actor in the equipment link at `link`, other than `wearer`; 0 none.
std::uint64_t LinkedActor(CoopPuppet::Guest& guest, std::uint64_t link, std::uint64_t wearer) {
    namespace pa = CoopPuppet::addr;
    std::uint32_t id = 0xFFFFFFFFu;
    std::uint64_t holder = 0;
    std::uint64_t piece = 0;
    std::uint32_t state = 0;
    if (!ReadValue(guest, link + pa::kEquipSlotId, id) || id == 0xFFFFFFFFu ||
        !ReadValue(guest, link + pa::kEquipSlotHolder, holder) || holder == 0 ||
        !ReadValue(guest, holder + pa::kHolderActor, piece) || piece == 0 || piece == wearer ||
        !ReadValue(guest, piece + pa::kActorState, state) || state != pa::kStatePresent) {
        return 0;
    }
    return piece;
}

}  // namespace

// ---------------------------------------------------------------- masks

bool IsBoundHead(std::string_view name) {
    if (name.find("Default_Head") != std::string_view::npos) {
        return false;
    }
    return name.find("_Mask_") != std::string_view::npos ||
           name.find("GlassTruth") != std::string_view::npos;
}

std::uint64_t Heads::HeadPiece(CoopPuppet::Guest& guest, std::uint64_t puppet) {
    namespace pa = CoopPuppet::addr;
    const std::uint64_t eu = EquipmentUser(guest, puppet);
    if (eu == 0) {
        return 0;
    }
    // Armor link 0 (ArmorHead, kind 7), then hand slot 7 ("Helmet"), where
    // orm/detalles found the worn head on the player as well.
    if (const std::uint64_t piece = LinkedActor(guest, eu + pa::kArmorLinks, puppet)) {
        return piece;
    }
    return LinkedActor(guest, eu + pa::kEquipSlots + pa::kEquipSlotStride * 7, puppet);
}

bool Heads::Follow(CoopPuppet::Guest& guest, std::uint8_t number, std::uint64_t puppet,
                   const Log& log) {
    namespace pa = CoopPuppet::addr;
    const std::uint64_t piece = HeadPiece(guest, puppet);
    if (piece == 0) {
        seen_.erase(number);
        return false;
    }
    Seen& s = seen_[number];
    const auto piece_now = ReadSkeleton(guest, piece, false);
    const auto puppet_now = ReadSkeleton(guest, puppet, false);
    if (!piece_now || !puppet_now) {
        s = Seen{};  // made a moment ago, or going: read again next frame
        return false;
    }
    // ORM review (REVISION_FINAL.md "Visual"): the model and the counts too, as
    // Cached does: a piece made again at the same addresses (a mask swapped)
    // must not keep the last one's bound flag and bone map.
    if (s.piece != piece || s.piece_local != piece_now->local || s.puppet_local != puppet_now->local ||
        s.model != piece_now->model || s.piece_count != piece_now->count ||
        s.puppet_count != puppet_now->count) {
        // Without the names (an FSKL not as read from the romfs) the piece is
        // still placed; only its bones are not copied.
        const auto piece_skel = ReadSkeleton(guest, piece, true);
        const auto puppet_skel = ReadSkeleton(guest, puppet, true);
        s = Seen{};
        s.piece = piece;
        s.piece_local = piece_now->local;
        s.puppet_local = puppet_now->local;
        s.piece_count = piece_now->count;
        s.puppet_count = puppet_now->count;
        s.model = piece_now->model;
        s.bound = IsBoundHead(s.model);
        s.names = piece_skel && puppet_skel && piece_skel->count == piece_now->count &&
                  puppet_skel->count == puppet_now->count;
        for (std::size_t i = 0; s.names && i < piece_skel->names.size(); ++i) {
            const int j = puppet_skel->Find(piece_skel->names[i]);
            if (!piece_skel->names[i].empty() && j >= 0) {
                s.map.emplace_back(static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(j));
            }
        }
    }
    if (!s.bound) {
        return false;  // the default head, a hat that is bound by world matrix
    }
    // The place and turn as one block (+0x31c, 48 bytes), and the engine's
    // pending transform as the puppet's (CoopPuppet::Director::Pose).
    std::array<float, 12> transform{};
    if (!guest.Read(puppet + pa::kActorPosition, transform.data(), sizeof(transform))) {
        return false;
    }
    if (!s.said) {
        Vec3 was{};
        ReadValue(guest, piece + pa::kActorPosition, was);
        const float off = Length(Sub(was, {transform[0], transform[1], transform[2]}));
        std::string bones;
        for (std::size_t i = 0; i < s.map.size() && i < 8; ++i) {
            bones += (i == 0 ? "" : ", ") + std::to_string(s.map[i].first) + "->" +
                     std::to_string(s.map[i].second);
        }
        if (s.names) {
            CoopVCheck::Ok("mask follow", "'" + s.model + "' was " + Num(off) + " from its puppet: placed on it, " +
                                              std::to_string(s.map.size()) + " bones of the same name copied");
        } else {
            CoopVCheck::Fallback("mask follow", "'" + s.model + "' placed on its puppet without copying bones: "
                                                "the skeletons' names could not be read");
        }
        if (log) {
            log("P" + std::to_string(number) + " head '" + s.model + "' (actor " + Hex(piece) +
                ") was " + Num(off) + " from its puppet: placed on it every frame, " +
                (s.names ? std::to_string(s.map.size()) + " bones of the same name copied" +
                               (bones.empty() ? std::string()
                                              : " (" + bones + (s.map.size() > 8 ? ", ..." : "") + ")")
                         : std::string("no bone copied (the skeletons' names could not be read)")));
        }
        s.said = true;
    }
    guest.Write(piece + pa::kActorPosition, transform.data(), sizeof(transform));
    guest.Write(piece + pa::kActorPositionRequest, transform.data(), 12);
    guest.Write(piece + pa::kActorRotationRequest, transform.data() + 3, 36);
    for (const auto& [to, from] : s.map) {
        Local bone{};
        if (guest.Read(s.puppet_local + std::uint64_t(from) * sizeof(Local), &bone, sizeof(bone)) &&
            std::isfinite(bone.rows[0]) && std::isfinite(bone.translation[0])) {
            guest.Write(s.piece_local + std::uint64_t(to) * sizeof(Local), &bone, sizeof(bone));
        }
    }
    return true;
}

// ---------------------------------------------------------------- tethers

const char* TetherText(Tether tether) {
    switch (tether) {
    case Tether::none:
        return "none";
    case Tether::hook_chain:
        return "hookshot chain";
    case Tether::fishing_line:
        return "fishing line";
    }
    return "?";
}

Tether TetherOf(std::string_view shared_actor) {
    if (shared_actor == "Shootable_Hook" || shared_actor == "Shootable_HookLong") {
        return Tether::hook_chain;
    }
    if (shared_actor == kLineActor) {
        return Tether::fishing_line;
    }
    return Tether::none;
}

std::vector<std::string_view> AnchorBones(std::string_view shared_actor) {
    // Item_Hookshot_Body: the tip sits at FirePos_* before the shot; Tip_* is
    // the barrel's end. Item_FishingRod: Rod_4 is the last bone, 1.76 out.
    if (shared_actor == "Shootable_Hook") {
        return {"FirePos_HookShot", "Tip_HookShot"};
    }
    if (shared_actor == "Shootable_HookLong") {
        return {"FirePos_LongHook", "Tip_LongHook"};
    }
    if (shared_actor == kLineActor || shared_actor == "Weapon_Lure" ||
        shared_actor == "Weapon_SinkingLure") {
        return {"Rod_4", "Rod_3"};
    }
    return {};
}

std::optional<Anchor> FindAnchor(CoopPuppet::Guest& guest, std::uint64_t puppet,
                                 std::string_view shared_actor) {
    namespace pa = CoopPuppet::addr;
    TrimCache();
    const Skeleton* body = Cached(guest, puppet);
    Vec3 at{};
    Mat3 rot{};
    std::vector<Local> locals;
    if (body == nullptr || !Place(guest, puppet, at, rot) || !ReadLocals(guest, *body, locals)) {
        return std::nullopt;
    }
    if (const int head = body->Find("Head"), hips = body->Find("Skl_Root"); head >= 0 && hips >= 0) {
        int fits = 0;
        const auto rows = DetectRows(locals, body->parents, std::size_t(head), std::size_t(hips), &fits);
        float up = 0.0f;
        if (rows) {
            NoteRows(*rows);
            up = BoneFrame(locals, body->parents, std::size_t(head), *rows).origin[1] -
                 BoneFrame(locals, body->parents, std::size_t(hips), *rows).origin[1];
        }
        RowsCheck(fits, rows, up);
    } else {
        CoopVCheck::Fallback("bone rows", std::string(RowsText(CurrentRows())) +
                                              ": the puppet's skeleton has no Head or Skl_Root by name");
    }
    const Rows rows = CurrentRows();
    struct Hand {
        std::string_view name;
        Frame world;
    };
    std::vector<Hand> hands;
    for (const std::string_view name : kHandBones) {
        if (const int b = body->Find(name); b >= 0) {
            const Frame f = BoneFrame(locals, body->parents, std::size_t(b), rows);
            hands.push_back({name, Frame{Mul(rot, f.rot), Add(Apply(rot, f.origin), at)}});
        }
    }
    const auto wanted = AnchorBones(shared_actor);
    const std::uint64_t eu = EquipmentUser(guest, puppet);
    for (std::uint32_t slot = 0; eu != 0 && !wanted.empty() && slot < 8; ++slot) {
        const std::uint64_t piece =
            LinkedActor(guest, eu + pa::kEquipSlots + pa::kEquipSlotStride * slot, puppet);
        const Skeleton* skel = piece != 0 ? Cached(guest, piece) : nullptr;
        if (skel == nullptr) {
            continue;
        }
        for (const std::string_view name : wanted) {
            const int b = skel->Find(name);
            Vec3 piece_at{};
            Mat3 piece_rot{};
            std::vector<Local> piece_locals;
            if (b < 0 || !Place(guest, piece, piece_at, piece_rot) ||
                !ReadLocals(guest, *skel, piece_locals)) {
                continue;
            }
            const Vec3 local = BoneFrame(piece_locals, skel->parents, std::size_t(b), rows).origin;
            Anchor a;
            a.bone = std::string(name);
            a.at = Add(Apply(piece_rot, local), piece_at);
            a.from = "piece";
            const Hand* nearest = nullptr;
            for (const Hand& h : hands) {
                const float d = Length(Sub(piece_at, h.world.origin));
                if (nearest == nullptr || d < a.off) {
                    nearest = &h;
                    a.off = d;
                }
            }
            if (nearest != nullptr && a.off > kPieceOnHand) {
                // The piece's own place is not in the hand (its actor may
                // stand at the wearer's root): the bone from the hand's frame.
                a.at = Add(Apply(nearest->world.rot, local), nearest->world.origin);
                a.from = "hand frame";
            }
            if (!Finite(a.at)) {
                continue;
            }
            return a;
        }
    }
    if (hands.empty() || !Finite(hands.front().world.origin)) {
        return std::nullopt;
    }
    Anchor a;
    a.at = hands.front().world.origin;
    a.from = "hand";
    a.bone = std::string(hands.front().name);
    return a;
}

std::optional<Anchor> PuppetAnchor(CoopPuppet::Guest& guest, std::uint8_t owner,
                                   std::string_view shared_actor) {
    // Shown or held, not parked: LivePuppets is 0 while the puppets are off,
    // so TheDirector is never made from here.
    if (owner == 0 || owner >= 32 || ((CoopPuppet::LivePuppets() >> owner) & 1u) == 0) {
        return std::nullopt;
    }
    for (const auto& slot : CoopPuppet::TheDirector().Slots()) {
        if (slot.number == owner && slot.phase == CoopPuppet::Phase::live && slot.actor != 0) {
            return FindAnchor(guest, slot.actor, shared_actor);
        }
    }
    return std::nullopt;
}

bool Tethers::Learn(CoopPuppet::Guest& guest, Known& k, std::uint64_t actor, Tether tether) {
    auto skel = ReadSkeleton(guest, actor, true);
    if (!skel) {
        return false;
    }
    k = Known{};
    k.actor = actor;
    k.local = skel->local;
    k.skel = std::move(*skel);
    char name[32];
    if (tether == Tether::hook_chain) {
        for (int i = 0; i < 128; ++i) {
            std::snprintf(name, sizeof(name), "MainLine_%02d", i);
            const int b = k.skel.Find(name);
            if (b < 0) {
                break;
            }
            k.chain.push_back(static_cast<std::uint16_t>(b));
        }
        if (!k.chain.empty()) {
            k.parent = k.skel.parents[k.chain.front()];
        }
        k.link = 0.383f;  // Item_Hookshot_Chain's rest step
        std::vector<Local> locals;
        if (k.chain.size() >= 2 && ReadLocals(guest, k.skel, locals)) {
            const Local& second = locals[k.chain[1]];
            const float rest = Length({second.translation[0], second.translation[1], second.translation[2]});
            if (rest > 0.05f && rest < 2.0f) {
                k.link = rest;
            }
        }
    } else if (tether == Tether::fishing_line) {
        if (const int root = k.skel.Find("Root"); root >= 0) {
            k.chain.push_back(static_cast<std::uint16_t>(root));
            for (int i = 2; i < 200; ++i) {
                std::snprintf(name, sizeof(name), "Rope_%03d", i);
                const int b = k.skel.Find(name);
                if (b < 0) {
                    break;
                }
                k.chain.push_back(static_cast<std::uint16_t>(b));
            }
            if (const int tip = k.skel.Find("Tip"); tip >= 0) {
                k.chain.push_back(static_cast<std::uint16_t>(tip));
            }
            k.parent = k.skel.parents[root];
        }
    }
    // Each chain bone hangs from the one before (the models' own order).
    k.usable = k.chain.size() >= 2;
    for (std::size_t i = 1; k.usable && i < k.chain.size(); ++i) {
        k.usable = k.skel.parents[k.chain[i]] == static_cast<std::int16_t>(k.chain[i - 1]);
    }
    return true;
}

bool Tethers::Lay(CoopPuppet::Guest& guest, std::uint64_t key, std::uint64_t actor, Tether tether,
                  const Vec3& at, const Mat3& rot, const std::optional<Anchor>& anchor,
                  const Log& log) {
    if (tether == Tether::none) {
        return false;
    }
    Known& k = known_[key];
    const auto now = ReadSkeleton(guest, actor, false);
    if (!now) {
        k = Known{};
        return false;
    }
    if (k.actor != actor || k.local != now->local) {
        if (!Learn(guest, k, actor, tether)) {
            // The names could not be read: said once, not laid (the copy shows as it is).
            const bool said = k.actor == actor && k.said;
            k = Known{};
            k.actor = actor;
            k.local = now->local;
            k.skel.model = now->model;
            if (!said) {
                CoopVCheck::Fallback(TetherText(tether), now->model + " not laid (shows as the game has it): its "
                                                                      "skeleton's names could not be read");
            }
            if (!said && log) {
                log(now->model + ": copy " + std::to_string(key) + ": " + TetherText(tether) +
                    " not laid: its skeleton's names could not be read");
            }
            k.said = true;
            return false;
        }
    }
    if (!k.said) {
        k.said = true;
        if (log) {
            log(k.skel.model + ": copy " + std::to_string(key) + ": " + TetherText(tether) + " " +
                (k.usable ? "of " + std::to_string(k.chain.size()) + " bones from '" +
                                k.skel.names[k.chain.front()] + "'" +
                                (tether == Tether::hook_chain ? ", link " + Num(k.link) : std::string())
                          : std::string("not laid: no chain of bones in this model")));
        }
    }
    if (!k.usable) {
        return false;
    }
    std::vector<Local> locals;
    if (!ReadLocals(guest, k.skel, locals)) {
        return false;
    }
    const Rows rows = CurrentRows();
    const Frame parent = k.parent >= 0 && std::size_t(k.parent) < locals.size()
                             ? BoneFrame(locals, k.skel.parents, std::size_t(k.parent), rows)
                             : Frame{};
    const Mat3 back = Transposed(Orthonormal(rot));
    Vec3 end = parent.origin;  // folded: no anchor, or a wrong one
    bool folded = true;
    if (anchor && Finite(anchor->at) && Finite(at)) {
        const Vec3 local = Apply(back, Sub(anchor->at, at));
        if (Length(Sub(local, parent.origin)) <= kMaxReach) {
            end = local;
            folded = false;
        }
    }
    std::vector<Vec3> points;
    if (tether == Tether::hook_chain) {
        points = HookPoints(parent.origin, end, k.link, k.chain.size());
    } else {
        points = LinePoints(parent.origin, end, Apply(back, Vec3{0, -1, 0}), k.chain.size(), kLineSag);
    }
    const auto chain = ChainLocals(parent, points);
    for (Local& l : locals) {
        l.flags &= ~addr::kHierarchyBits;
    }
    for (std::size_t i = 0; i < chain.size() && i < k.chain.size(); ++i) {
        Store(locals[k.chain[i]], chain[i], rows);
    }
    if (!WriteLocals(guest, k.skel, locals)) {
        return false;
    }
    if (log) {
        if (folded && !k.folded_said) {
            log(k.skel.model + ": copy " + std::to_string(key) + ": " + TetherText(tether) +
                " folded (" + (anchor ? "its puppet's hand is over " + Num(kMaxReach) + " away"
                                      : std::string("its owner's puppet is not here")) + ")");
        } else if (!folded && !k.laid_said) {
            CoopVCheck::Ok(TetherText(tether), k.skel.model + " laid to the puppet's " + anchor->bone + " (" +
                                                   anchor->from + "), " + Num(Length(Sub(end, parent.origin))) +
                                                   " long, rows " + RowsText(rows));
            log(k.skel.model + ": copy " + std::to_string(key) + ": " + TetherText(tether) +
                " laid to the puppet's " + anchor->bone + " (" + anchor->from + ", the piece " +
                Num(anchor->off) + " from the hand), " + Num(Length(Sub(end, parent.origin))) +
                " long, rows " + RowsText(rows));
        }
    }
    k.folded_said = k.folded_said || folded;
    k.laid_said = k.laid_said || !folded;
    return true;
}

void Tethers::Keep(const std::vector<std::uint64_t>& keys) {
    std::erase_if(known_, [&](const auto& e) {
        return std::find(keys.begin(), keys.end(), e.first) == keys.end();
    });
}

}  // namespace CoopMascaras
