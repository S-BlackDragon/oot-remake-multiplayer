// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera, the emulator side (oot-remake
// docs/ESPECTADOR_Y_JEFES.md, C0 and C1; the portable part is
// coop_camera_aim.h). Off unless one of:
// - LICHII_COOP_DEBUG_CAMERA2=1: the probe. Finds the PlayerCamera actor on
//   the actor list and the player camera object from it (a pointer walk, or a
//   search for its pitch limits), logs the way once, then twice a second
//   "coop: camera2:" with the object's yaw, pitch and state words beside the
//   heading and pitch of the renderer's world-to-clip matrix
//   (CoopCamera::Latest), and where each mate in the scene lands on screen.
// - LICHII_COOP_SPECTATOR=1: the spectator camera (C1, coop_spectator.h):
//   while our Link is down (the downed state machine in `downed`) the view
//   turns at a capped rate to look from his body at the nearest mate standing
//   (the d-pad picks another); after a load, while he stands, a camera still
//   asleep gets a short right-stick nudge (CoopTestInput::Nudge) so it will
//   take the written yaw. LICHII_COOP_SPECTATOR_WAKE=none|bits|stick|both
//   (default stick) chooses how it is woken. C2: the d-pad goes round the
//   standing mates of our room (CoopSpectator::RoomOf), the downed line says
//   whom we look at (Banner) and the in-game HUD marks his tag (Following);
//   the camera object is written only through a CameraLease.
// Frame: present thread, once a frame. Banner and Following: any thread.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/overlay/overlay.hpp"
#include "coop/protocol/messages.hpp"

namespace CoopPlatform {
class Platform;
}

namespace CoopCameraProbe {

// `main_base` 0: no game. `others`: the mates presence shows this frame
// (their scene and feet); `down`: our Link lies downed (no game over, no
// fairy revive); `standing`: he is up and playing (the downed state machine
// says alive).
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<coop::protocol::Vec3>& position,
           const std::vector<coop::overlay::Shown>& others, bool down, bool standing);

// "Mirando a Ana (2 de 3, cruceta para cambiar)" while the spectator follows
// someone (CoopSpectator::FollowLine), else empty; for the downed line.
std::string Banner();
// The number of the mate followed, 0 for none (the HUD marks his tag).
std::uint8_t Following();
// ORM /espectar <nombre> (oot-remake docs/COMANDOS.md): follow that mate
// from the next frame, as the d-pad would pick him (any thread).
void AskFollow(std::uint8_t number);

} // namespace CoopCameraProbe
