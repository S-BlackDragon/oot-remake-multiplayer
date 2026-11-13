// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera, the portable part (coop_camera_aim.h).

#include "coop_camera_aim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_set>

namespace CoopCameraAim {
namespace {

constexpr double kDeg = 180.0 / 3.14159265358979323846;

template <typename T>
bool Get(CoopPuppet::Guest& guest, std::uint64_t at, T& out) {
    return at != 0 && guest.Read(at, &out, sizeof(T));
}

// A guest heap pointer: 8-byte aligned and in the 39-bit user range the
// game's heap uses (never a small number, a float pair or a tagged value).
bool Plausible(std::uint64_t p) {
    return p >= 0x10000 && p < (1ull << 39) && (p & 7) == 0;
}

} // namespace

std::optional<View> ViewOf(const Mat4& m) {
    const double fx = m[12], fy = m[13], fz = m[14];
    const double len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (!std::isfinite(len) || len < 1e-6) {
        return std::nullopt;
    }
    View v;
    v.heading = static_cast<float>(std::atan2(fx, fz) * kDeg);
    v.pitch = static_cast<float>(std::asin(fy / len) * kDeg);
    return v;
}

std::optional<Ndc> Project(const Mat4& m, const Vec3& p) {
    float clip[4];
    for (int row = 0; row < 4; ++row) {
        clip[row] = m[row * 4 + 0] * p[0] + m[row * 4 + 1] * p[1] + m[row * 4 + 2] * p[2] + m[row * 4 + 3];
    }
    if (!std::isfinite(clip[3]) || clip[3] <= 1e-6f) {
        return std::nullopt;
    }
    return Ndc{clip[0] / clip[3], clip[1] / clip[3], clip[3]};
}

std::optional<float> HeadingTo(const Vec3& from, const Vec3& to) {
    const double dx = to[0] - from[0], dz = to[2] - from[2];
    if (std::hypot(dx, dz) < 0.01) {
        return std::nullopt;
    }
    return static_cast<float>(std::atan2(dx, dz) * kDeg);
}

float Wrap(float d) {
    double a = std::fmod(static_cast<double>(d), 360.0);
    if (a <= -180.0) {
        a += 360.0;
    } else if (a > 180.0) {
        a -= 360.0;
    }
    return static_cast<float>(a);
}

float YawFor(float heading, const YawRule& rule) {
    return Wrap(rule.sign * heading + rule.offset);
}

std::optional<std::size_t> Nearest(const Vec3& self, const std::vector<Vec3>& mates) {
    std::optional<std::size_t> best;
    double best_d = 0;
    for (std::size_t i = 0; i < mates.size(); ++i) {
        const double d = std::hypot(mates[i][0] - self[0], mates[i][2] - self[2]);
        if (!best || d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

bool LooksLikeCamera(CoopPuppet::Guest& guest, std::uint64_t cam) {
    float limits[2]{};
    float yaw = 0.0f;
    if (!Plausible(cam) || !guest.Read(cam + kPitchLimits, limits, sizeof(limits)) ||
        limits[0] != kPitchMin || limits[1] != kPitchMax || !Get(guest, cam + kYaw, yaw)) {
        return false;
    }
    return std::isfinite(yaw) && yaw >= -180.5f && yaw <= 180.5f;
}

std::uint64_t ActorOfCamera(CoopPuppet::Guest& guest, std::uint64_t cam) {
    std::uint64_t a = 0, b = 0, actor = 0;
    if (!Get(guest, cam + kContext, a) || !Plausible(a) || !Get(guest, a + kContextInner, b) ||
        !Plausible(b) || !Get(guest, b + kContextActor, actor)) {
        return 0;
    }
    return actor;
}

std::string Path::Text() const {
    std::string out;
    char buf[24];
    for (const auto o : offsets) {
        std::snprintf(buf, sizeof(buf), "%s+%#x", out.empty() ? "" : " -> ", o);
        out += buf;
    }
    return out.empty() ? "(the actor itself)" : out;
}

std::optional<Found> Walk(CoopPuppet::Guest& guest, std::uint64_t actor, std::uint32_t span, int depth,
                          std::size_t budget, bool same_actor) {
    struct Node {
        std::uint64_t at;
        std::vector<std::uint32_t> path;
    };
    std::deque<Node> queue;
    std::unordered_set<std::uint64_t> seen;
    queue.push_back({actor, {}});
    seen.insert(actor);
    std::vector<std::uint64_t> words(span / 8);
    while (!queue.empty() && seen.size() <= budget) {
        Node node = std::move(queue.front());
        queue.pop_front();
        if (static_cast<int>(node.path.size()) >= depth ||
            !guest.Read(node.at, words.data(), words.size() * 8)) {
            continue;
        }
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::uint64_t p = words[i];
            if (!Plausible(p) || !seen.insert(p).second) {
                continue;
            }
            std::vector<std::uint32_t> path = node.path;
            path.push_back(static_cast<std::uint32_t>(i * 8));
            if (LooksLikeCamera(guest, p) && (!same_actor || ActorOfCamera(guest, p) == actor)) {
                return Found{p, Path{std::move(path)}};
            }
            queue.push_back({p, std::move(path)});
        }
    }
    return std::nullopt;
}

std::uint64_t Follow(CoopPuppet::Guest& guest, std::uint64_t actor, const Path& path) {
    std::uint64_t at = actor;
    for (const auto o : path.offsets) {
        std::uint64_t next = 0;
        if (!Get(guest, at + o, next) || !Plausible(next)) {
            return 0;
        }
        at = next;
    }
    return at;
}

std::vector<std::uint64_t> SearchLimits(CoopPuppet::Guest& guest, std::uint64_t from, std::uint64_t bytes,
                                        std::size_t max) {
    constexpr std::uint64_t kBlock = 0x10000;
    std::uint32_t want[2];
    const float limits[2] = {kPitchMin, kPitchMax};
    std::memcpy(want, limits, sizeof(want));
    std::vector<std::uint32_t> block(kBlock / 4 + 2);
    std::vector<std::uint64_t> hits;
    from &= ~(kBlock - 1);
    for (std::uint64_t at = from; at < from + bytes && hits.size() < max; at += kBlock) {
        // One extra word so a pair across two blocks is still seen.
        if (!guest.Read(at, block.data(), kBlock + 4)) {
            if (!guest.Read(at, block.data(), kBlock)) {
                continue;
            }
            block[kBlock / 4] = 0;
        }
        for (std::size_t i = 0; i < kBlock / 4 && hits.size() < max; ++i) {
            if (block[i] != want[0] || block[i + 1] != want[1]) {
                continue;
            }
            const std::uint64_t cam = at + i * 4 - kPitchLimits;
            if ((cam & 7) == 0) {
                hits.push_back(cam);
            }
        }
    }
    return hits;
}

} // namespace CoopCameraAim
