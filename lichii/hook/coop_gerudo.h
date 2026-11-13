// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM Gerudo Fortress and the Spirit Temple with several players, the hook's
// side (the rules: coop/host/gerudo.hpp; doc: oot-remake docs/GERUDO_ESPIRITU.md).
// LICHII_ORM_GERUDO (LICHII_COOP_GERUDO before test9), on unless off (coop_switch.h):
// - in the Gerudo Fortress, the carpenters' counts are mended once nothing
//   runs (coop::host::CarpenterCounts): a rescue lost to two at once in two
//   games would leave the fourth one without the membership card;
// - a mate's story start in a scene for both ages (the Spirit Temple) is
//   followed only at the starter's age (coop::host::story_start_for_age).
// The guards run in each game without a switch (CoopObjects::IsShared,
// coop::host::runs_in_each_game), but those test10's guard sync shares
// (CoopObjects::IsGuard, issue 191); the desert's guide Poe too, by
// coop::host::is_own_hazard_actor (oot-remake D-R67).
// Log: "coop: gerudo: ...", "coop: story: ... not followed: ...".
#pragma once

#include <cstdint>
#include <optional>

#include "coop/host/player_reader.hpp"
#include "coop/protocol/messages.hpp"

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}

namespace CoopGerudo {

// LICHII_ORM_GERUDO (or LICHII_COOP_GERUDO), read once: on unless 0/false/no/off.
bool On();

// Present thread, once per frame. `main_base` 0 when the game is not up.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading);

// Present thread (OrmStoryEvents::Frame, before Story::receive): false when a
// mate's STORY_EVENT start is for the other age (logged once per start). True
// for anything else, when off, or when an age is not known. `session` may be
// null (then the starter's age is not known).
bool StoryStartForUs(coop::host::Session* session, const std::optional<coop::host::PlayerReading>& reading,
                     const coop::protocol::StoryEvent& start);

} // namespace CoopGerudo
