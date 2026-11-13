// SPDX-License-Identifier: GPL-3.0-or-later
//
// /teleport <player>, the hook's side (coop_teleport.h has the rules).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/player_reader.hpp"
#include "coop_teleport.h"

namespace Core {
class System;
}
namespace coop::host {
class Session;
}
namespace coop::net {
class Client;
struct Event;
}

namespace CoopTeleport {

// Any thread (the chat box): true when `text` is a command, which is then
// done here and not sent as chat. Takes the session's network lock.
bool OnChat(std::string_view text, coop::host::Session& session);

// Network thread, from the session's on_event (the network lock is held):
// answers the others' requests, and takes the answers to ours.
void OnEvent(const coop::net::Event& event, coop::net::Client& client);

// Present thread, once per frame. `main_base` 0 when the game is not up;
// `session` null when not connected.
void Frame(Core::System& system, std::uint64_t main_base, const std::optional<coop::host::PlayerReading>& reading,
           coop::host::Session* session);

// The chat notices since the last call (for the chat box).
std::vector<std::string> TakeLines();

// ORM (coop/host songs.hpp, the warp-song invitation): start a trip to that player, as
// "/teleport <name>" does. Any thread.
void Follow(std::uint8_t number, std::string name);

// ORM (the test warp in free play, coop_testinput.h WarpStartsEvent): starts the trip's short
// scene event (Trip::kSceneEvent) so the game takes a reserved scene change with no event of its
// own. False when /teleport is off (LICHII_COOP_TELEPORT=0: no caller starts it). Any thread.
bool StartSceneEvent();

} // namespace CoopTeleport
