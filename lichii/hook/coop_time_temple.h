// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "the Temple of Time with several players" (oot-remake docs/TEMPLO_TIEMPO.md),
// the emulator side of coop/host/time_temple.hpp. On with the story cutscenes
// (LICHII_ORM_STORY_EVENTS, orm_story_events.h) unless LICHII_ORM_TIME=0. The
// list is the built-in one, or the file LICHII_ORM_TIME_LIST names (the kit's
// shared_time_temple.txt; a bad file turns this off). Log lines start with
// "coop: temple: ".
//
// What it does:
// - a mate's STORY_EVENT start of a Temple of Time cutscene is taken only when
//   our Link is in the temple (the temple is a part of the market's scenes);
//   a start of a later part of the sword's story (the Sacred Realm, Rauru) is
//   never taken; each becomes `played` for the story layer (Filter);
// - while our game runs the sword's story (or asks for it, or is in the
//   Chamber of Sages) the room's age follow of D-R17 waits (HoldsAge, read by
//   CoopAge::Frame as AgeInput::wait);
// - the list's `flag` lines are shared flags (AddShared).
#pragma once

#include <optional>
#include <string>

#include "coop/host/flags.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/host/story.hpp"
#include "coop/protocol/messages.hpp"

namespace CoopTimeTemple {

bool On();

// At the session's start, after OrmStoryEvents::AddShared: when on, adds the
// list's flags to the shared flags (making the list when there is none).
void AddShared(std::optional<coop::host::SharedFlags>& shared);

// Present thread: a STORY_EVENT from the server, before the story layer gets
// it. `reading`: our Link now (nothing: no game).
void Filter(coop::protocol::StoryEvent& m, const std::optional<coop::host::PlayerReading>& reading);

// Present thread, once a frame, after the story layer's tick, with what it was
// given and what it asks.
void Frame(const coop::host::Story& story, const coop::host::StoryInput& in);

// Why the room's age follow waits now, or nothing (also when off). Any thread.
// CoopAge::Frame (coop_age_game.cpp, D-R17) gives it to AgeFollow as
// AgeInput::wait ("the Temple of Time: <why>"): no reload begins, a mate's
// change is still seen and a pending reload is kept for afterwards (dropped
// then as "already <age>" when our own cutscene made us that age), one under
// way runs to its end. The answer is the one of the story layer's last frame
// (OrmStoryEvents::Frame runs before CoopAge::Frame in the hook's frame).
std::optional<std::string> HoldsAge();

// Our story layer asks our game, on a mate's word, for an event of the sword's
// story that changes Link's age (rule travel: the draw, putting it back), as of
// its last frame. Any thread. CoopAge::Frame gives it as AgeInput::story_follow
// (2026-11-13): our own age change soon after is the mate's, so this game
// does not tell "Moviendo al grupo contigo...". False when off.
bool AskedOnMatesWord();

} // namespace CoopTimeTemple
