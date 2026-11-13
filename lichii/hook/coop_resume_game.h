// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM resume, the hook's side (coop_resume.h has the rules). Called from the
// /teleport glue (coop_teleport_game.cpp), whose trip takes us back.
// LICHII_ORM_RESUME=0 turns it off (nothing kept, no way back).
// LICHII_ORM_RESUME_PLACE: the place to go back to, one line of
// coop-resume.txt (the app's helper gives the newest of this PC's and the
// server's); "none" for none. Unset: this PC's coop-resume.txt, the line of
// this room (LICHII_ORM_ROOM, else the server's address) and name.
#pragma once

#include <cstdint>
#include <optional>

#include "coop_resume.h"
#include "coop_teleport.h"

namespace CoopResume {

// Present thread, once per frame: keeps our last place in coop-resume.txt and
// gives the way back once after the load (logged here). `resuming`: our
// resume trip still runs (CoopTeleport::Trip::Resuming); `teleport_on`: the
// trip can change scene (LICHII_COOP_TELEPORT is not 0).
std::optional<Decision> Frame(const CoopTeleport::Local& local, const CoopTeleport::Grounded& grounded,
                              bool resuming, bool teleport_on, std::uint64_t now_us);

} // namespace CoopResume
