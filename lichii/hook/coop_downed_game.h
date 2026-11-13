// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "downed, not dead", the emulator side (oot-remake docs/CAIDO_REVIVIR.md;
// the logic is coop/host/downed.hpp). Only with LICHII_COOP_DOWNED=1, which
// also patches the gate in (coop_downed.h). Each frame, on the present thread:
// - reads Link's life and death and, while he is down, the bottles;
// - ticks the local state machine and carries out what it asks: the gate
//   word, the bottle values, the flag watcher's quiet set, PLAYER_STATUS bit 4;
// - watches the other players: A held 3 s within 1.5 of one who is down sends
//   REVIVE to that player (A is read from player 1's emulated pad, scripted
//   input included), and its progress every 250 ms before;
// - gives the state machine the room (connected, mates in), and the
//   server's GROUP_DEFEAT;
// - keeps the line for the co-op window (Banner);
// - after a mate's revive, writes the life up to max(3 hearts, 25 %) when the
//   fairy left less; through a reload at the room's age begun as we lay down
//   (coop_age_game.h HoldsDown) keeps us down, and puts down again a Link the
//   load got up (D-R62, oot-64 D-178);
// - plays a game sound once when a mate goes down (LICHII_ORM_MATE_DOWN_SOUND,
//   coop/host/mate_down_sound.hpp, OrmOcarinaSound's cue).
// A REVIVE or GROUP_DEFEAT for us (network thread) is queued for the next frame.
// One line per transition: "coop: downed: alive -> downed", "... revive
// requested by P2", "... revived".
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/presence/presence.hpp"

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace CoopDowned {

// LICHII_COOP_DOWNED is on.
bool On();

// Present thread: our Link is down, being revived, or in his game over
// (PLAYER_STATUS bit 4). False when LICHII_COOP_DOWNED is off.
bool Down();

// The test command `gate` (coop_testinput) took the gate: from now on the
// state machine only follows Link and never writes the gate or a bottle.
void SetManual();

// Network thread: a REVIVE for us (the request, or a mate's progress) and
// GROUP_DEFEAT are queued for the next frame.
void OnEvent(const coop::net::Event& event);

// Present thread: the local state machine's state as of this frame's Frame
// (alive when LICHII_COOP_DOWNED is off). For the boss rooms (coop_boss.h).
coop::host::DownedState State();

// A mate is down: his PLAYER_STATUS dead bit as the session's presence holds
// it (the one store of the others' statuses: this glue's Frame, the NPC
// claim, the HUD tags, the spectator and the escape all read it there);
// false for none or when LICHII_COOP_DOWNED is off. Call it inside
// Session::with_presence. The HUD sets overlay::Shown::down from it, which the
// spectator camera (coop_spectator.h) reads to follow the ones standing.
bool MateDown(const coop::presence::Player* player);

// Any thread: the line the co-op window shows (coop::host::banner_text: "hold
// A to revive Saria", "Link is reviving you 40 %", in Spanish), UTF-8; empty
// for nothing.
std::string Banner();

// ORM: chat notices since the last call ("Ana ha caido...", "Ana vuelve a estar
// en pie"), for the co-op chat lines. Present thread, like Frame.
std::vector<std::string> TakeLines();

// Present thread, once a frame, before Session::frame (so the status bit goes
// out the same frame). `main_base` 0: no game.
// `event_name`: our running event's name, "" when not read (the Continue's
// wait for its reload, coop_continue.h).
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading, std::uint64_t server_now_us,
           std::string_view event_name = {});

} // namespace CoopDowned
