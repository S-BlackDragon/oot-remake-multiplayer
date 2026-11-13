// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM songs with several players, the hook's side (coop/host songs.hpp has the rules,
// oot-remake docs/CANCIONES.md): the warp-song invitation of oot-64 D-067. A mate who leaves
// our scene by a warp song (seen landing on the song's pad) can be followed for 10 s by
// holding a button; the trip is /teleport's (coop_teleport_game.h, Follow).
// Switch: LICHII_COOP_WARP_INVITE=1 (off by default: not run in a game yet);
//   LICHII_COOP_WARP_INVITE_BUTTON=L (or R, ZL, ZR, PLUS, MINUS, LCLICK, RCLICK).
// Log: "coop: songs: ..." (the invitation, the trip, our own arrival by a warp song, whether
//   the button is the game's too: "invitation button L: free" / "...: the game uses it ...: the
//   invitation takes L+R held together"); "coop: live: song of time <place>: ..." (Blocks).
// Switch: LICHII_ORM_LIVE_CHECK=0 turns Blocks off (on by default: it only reads and logs).
// Test: the rules in tests/host/songs_test.cpp and tests/host/live_check_test.cpp; this file
//   only reads the game and the pad.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/player_reader.hpp"

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}

namespace CoopSongs {

// "L" -> the pad bit (coop_testinput.h's Button); nothing for an unknown name.
std::optional<std::uint64_t> ButtonBit(std::string_view name);

// Present thread, once per frame, before CoopTeleport::Frame (so a trip it starts is
// taken the same frame). `main_base` 0 when the game is not up; `session` null when not
// connected; `event_name` the running event's name, if any.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading, const std::optional<std::string>& event_name,
           coop::host::Session* session, std::uint64_t server_now_us);

// The chat notices since the last call (for the chat box).
std::vector<std::string> TakeLines();

// Present thread, after the session's frame, with our game's flags (docs/CANCIONES.md "Por ver"
// 1 and 2, docs/RIESGOS_RESUELTOS.md): whether a Song of Time block answers its key live, on
// the next load, or goes with no listed key (coop/host live_check.hpp). Reads only.
void Blocks(coop::host::FlagStore& store, const std::optional<std::string>& scene,
            const std::optional<coop::host::PlayerReading>& reading);

} // namespace CoopSongs
