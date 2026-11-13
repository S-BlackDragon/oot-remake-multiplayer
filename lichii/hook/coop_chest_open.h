// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM cofre (oot-remake docs/DETALLES_MARIONETA.md, "Cofres"; 2026-11-11:
// "si uno descubre un objeto, todos lo descubren, como en oot-64"): the
// present thread's side of a chest another player opens.
// - The lid: CoopDoors' mirror (coop_doors.h) tells our chests (CHEST_OPEN,
//   type 249) and opens the lid of theirs; here its messages go out and in.
// - The find: coop/host/chest_open.hpp ChestFinds. While the finder's puppet
//   holds it up, its model-only stand-in (CoopGet_<find>, tools/puppet_pack
//   GET_STAND_INS) is made over the puppet's head by the spawn director
//   (CoopSpawn::Director::SetFinds); and once, the notice "<nombre>
//   encontró <objeto>" goes to the in-game HUD (coop_notices.h, drawn 5 s,
//   never a text box: oot-64 D-049) and to the log.
// What the find gives each game travels as before (the shared item flags and
// the chest's opened flag); nothing here writes a flag.
//
// LICHII_ORM_CHEST_OPEN=0 turns it all off (the lids with it). The portable
// part (Mates, Copies) is tested in tests/lichii/chest_open_test.cpp; the
// glue (OnEvent, Frame) is coop_chest_open_game.cpp.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/chest_open.hpp"
#include "coop_puppet.h"
#include "coop_spawn.h"

namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace CoopChestOpen {

// The others as ChestFinds wants them, from the puppets' remote list.
std::vector<coop::host::FindMate> Mates(const std::vector<CoopPuppet::Remote>& remotes);

// The spawn director's copies for the finds held up now: the find's name as
// the copy's actor (CoopSpawn::StandIn makes it its CoopGet_ stand-in, and
// StandInOnly never the game's own), placed where ChestFinds says, keyed
// CoopSpawn::Director::kFindKeyBit | serial << 8 | player, so a new find is a
// new copy.
std::vector<CoopSpawn::Copy> Copies(const std::vector<coop::host::HeldFind>& held);

// LICHII_ORM_CHEST_OPEN, read once (on unless 0).
bool On();

// Network thread, from the session's on_event: another player's CHEST_OPEN.
void OnEvent(const coop::net::Event& event);

// The present thread, once a frame after the doors' Frame: our game (0
// none), our scene, the session (or null) and the server time.
void Frame(std::uint64_t main_base, const std::optional<std::string>& scene, coop::host::Session* session,
           std::uint64_t server_now_us);

}  // namespace CoopChestOpen
