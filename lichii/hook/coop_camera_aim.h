// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera (oot-remake docs/ESPECTADOR_Y_JEFES.md, A.3-A.5, C0):
// the game's player camera object, how to reach it from the PlayerCamera
// actor, and the angles to look from one point at another. No emulator code
// (tested in tests/lichii/camera_aim_test.cpp); the glue is
// coop_camera_probe.cpp.
//
// The player camera (Hookshot, live, build e0f3ccfaf50065ae): its update is
// main+0x624c80 with the camera in x0; pitch limits {-85, 89} at +0x178,
// pitch at +0x18c, yaw at +0x1a4 (degrees, wrapped to +-180, falls when the
// view turns right), state at +0x204 (3 follows Link, 4 turned by the player).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop_puppet.h"

namespace CoopCameraAim {

using Vec3 = std::array<float, 3>;
using Mat4 = std::array<float, 16>;  // world to clip, row-major (coop_camera.h)

inline constexpr std::uint64_t kPitchLimits = 0x178;  // {-85.0f, 89.0f}
inline constexpr std::uint64_t kPitch = 0x18c;
inline constexpr std::uint64_t kYaw = 0x1a4;
inline constexpr std::uint64_t kState = 0x204;
inline constexpr float kPitchMin = -85.0f;
inline constexpr float kPitchMax = 89.0f;
// The update's own way to its actor: [[cam + 8] + 8] + 0x1f0 (main+0x624c80,
// as in every AI node).
inline constexpr std::uint64_t kContext = 0x08;
inline constexpr std::uint64_t kContextInner = 0x08;
inline constexpr std::uint64_t kContextActor = 0x1f0;

// Where the camera looks, from the clip matrix: the w row is the view's
// forward axis (w grows with depth in front of the camera). Heading in
// degrees, atan2(forward x, forward z) (0 looks down +z, 90 down +x); pitch
// in degrees, up positive. Nothing for a matrix with no forward axis.
struct View {
    float heading = 0.0f;
    float pitch = 0.0f;
};
std::optional<View> ViewOf(const Mat4& clip_from_world);

// `p` in normalised device coordinates (x right, y as the matrix has it,
// -1..1 on screen) and its w; nothing behind the camera (w <= 0).
struct Ndc {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};
std::optional<Ndc> Project(const Mat4& clip_from_world, const Vec3& p);

// The heading (ViewOf's convention) from `from` to `to`; nothing when they
// are less than 1 cm apart on the ground.
std::optional<float> HeadingTo(const Vec3& from, const Vec3& to);

// An angle in degrees wrapped to (-180, 180].
float Wrap(float degrees);

// The game's yaw word for a view heading: yaw = sign * heading + offset (the
// relation measured in C0, Kokiri, reports R0.3 camara.A).
struct YawRule {
    float sign = 1.0f;
    float offset = 0.0f;
};
float YawFor(float heading, const YawRule& rule);

// The nearest of `mates` to `self` on the ground (index), nothing when empty.
std::optional<std::size_t> Nearest(const Vec3& self, const std::vector<Vec3>& mates);

// The camera object is plausible: its pitch limits are {-85, 89} and its yaw
// a finite angle in +-180.
bool LooksLikeCamera(CoopPuppet::Guest& guest, std::uint64_t cam);

// The actor the camera's update works for ([[cam+8]+8]+0x1f0), 0 unreadable.
std::uint64_t ActorOfCamera(CoopPuppet::Guest& guest, std::uint64_t cam);

// A way from an actor to the camera object, as offsets: read a pointer at
// object + offsets[0], then at that + offsets[1], ...; the last one read is
// the camera.
struct Path {
    std::vector<std::uint32_t> offsets;
    std::string Text() const;  // "+0x270 -> +0x360 -> +0x10"
};

// Breadth first from `actor`: every 8-byte pointer in the first `span` bytes
// of each object reached, `depth` pointers deep at most, `budget` objects at
// most. The first object that LooksLikeCamera (and whose ActorOfCamera is
// `actor`, when `same_actor`) is returned with its path.
struct Found {
    std::uint64_t cam = 0;
    Path path;
};
std::optional<Found> Walk(CoopPuppet::Guest& guest, std::uint64_t actor, std::uint32_t span,
                          int depth, std::size_t budget, bool same_actor);

// Follows `path` from `actor`; 0 when a step is unreadable or null.
std::uint64_t Follow(CoopPuppet::Guest& guest, std::uint64_t actor, const Path& path);

// Every address in [from, from + bytes) at which the pitch limits {-85, 89}
// sit (8-byte aligned camera, limits at +0x178), as camera addresses. Reads
// in 64 KiB blocks and skips the unreadable ones; `max` hits at most.
std::vector<std::uint64_t> SearchLimits(CoopPuppet::Guest& guest, std::uint64_t from,
                                        std::uint64_t bytes, std::size_t max);

} // namespace CoopCameraAim
