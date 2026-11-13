// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM, the end of the game with up to six players, the emulator side
// (coop_final.h has what it does and its switches; the rules are
// coop/host/final_rules.hpp, oot-remake docs/FINAL.md).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "coop/host/downed.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/player_reader.hpp"

namespace CoopPlatform {
class Platform;
}
namespace coop::net {
struct Event;
}

namespace CoopFinal {

// The session starts (Coop::Begin, after the other lists): the counted list's
// flags join `shared`, the escape's clock starts over. Logs what it did.
void AddShared(std::optional<coop::host::SharedFlags>& shared);

// Network thread, from the session's on_event: a STORY_EVENT is queued.
void OnEvent(const coop::net::Event& event);

// Present thread, once a frame, after the session's frame and the story
// cutscenes' frame. `main_base` 0: no game.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading,
           const std::optional<std::string>& event_name, coop::host::DownedState downed,
           std::uint64_t server_now_us);

} // namespace CoopFinal
