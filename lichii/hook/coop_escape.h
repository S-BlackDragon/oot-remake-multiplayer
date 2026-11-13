// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM, the escape from Ganon's collapsing castle goes with the group
// (coop/host/group_exit.hpp; oot-64 D-165 "EscapeTakesGroup"; oot-remake
// docs/CONTENIDO_ADULTO.md section 4). No emulator code: coop_teleport_game.cpp
// feeds it once a frame and does what it says with the /teleport trip
// (CoopTeleport::Trip: JOIN_PARTNER, the game's scene change through
// Sequence_ReservedNextBanc, then the mate's grounded place). Tested in
// tests/lichii/escape_test.cpp.
//
// Asker's side: GroupExitRule decides when our game, still in the escape,
// follows a mate (up) who already reached the last fight. Then a trip to that
// mate starts, unless a /teleport of the player's own runs. If our Link goes
// down on the way, the trip stops (a scene change would stand him up: ORM
// D-R17, oot-64 D-178); the rule asks again once he is up.
//
// Answerer's side: our boss fight refuses every JOIN_PARTNER (cannot_warp),
// and Ganon's fight is in FinalBattleField, where the escape goes. A mate
// seen in the escape lately (or just out of it, for the trip's second ask)
// is let in when the only refusal is our fight.
//
// LICHII_ORM_GROUP_EXIT=0 turns both sides off.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/group_exit.hpp"
#include "coop_teleport.h"

namespace CoopEscape {

// GroupExitSettings from LICHII_ORM_GROUP_EXIT's value (null: unset, on; the
// reading of coop_switch.h), or from the switch already read.
coop::host::GroupExitSettings SettingsFrom(const char* env);
coop::host::GroupExitSettings SettingsFrom(bool on);

struct Mate {
    std::uint8_t number = 0;
    std::optional<std::string> scene;  // the presence's; nothing when not known
    bool up = true;                    // not down (CoopDowned::MateDown)
};

struct Input {
    std::uint64_t now_us = 0;
    std::optional<std::string> scene;  // ours; nothing while loading
    coop::host::DownedState downed = coop::host::DownedState::alive;
    bool event = false;  // an event runs in our game
    std::vector<Mate> mates;
    CoopTeleport::Trip::Phase trip = CoopTeleport::Trip::Phase::idle;  // the trip's phase now
    std::uint8_t trip_player = 0;
};

struct Actions {
    std::optional<std::uint8_t> follow;  // start a trip to this player now
    bool stop = false;                   // stop the trip: ours, and our Link went down on it
    std::vector<std::string> log;        // "escape: ..." (the hook adds "coop: ")
};

class Escape {
public:
    // A mate seen in an exit's scene this long ago still counts as escaping:
    // a trip gives up its scene change after a minute and asks again after
    // settling (CoopTeleport::Trip::kGiveUpUs, kSettleUs).
    static constexpr std::uint64_t kEscaperUs = 150'000'000;

    explicit Escape(coop::host::GroupExitSettings settings = {},
                    std::vector<coop::host::GroupExit> exits = coop::host::default_group_exits());

    bool On() const { return settings_.on; }

    // Present thread, once a frame.
    Actions Frame(const Input& input);

    // The answerer's side: whether player `number` was in an exit's scene
    // within kEscaperUs (as of the last Frame).
    bool Escaping(std::uint8_t number, std::uint64_t now_us) const;
    // Whether `scene` is where an exit takes the group (FinalBattleField).
    bool ExitEnd(const std::optional<std::string>& scene) const;

    // The session ended.
    void Reset();

private:
    coop::host::GroupExitSettings settings_;
    std::vector<coop::host::GroupExit> exits_;
    coop::host::GroupExitRule rule_;
    std::optional<std::uint8_t> ours_;  // the trip the escape started, to that player
    std::array<std::optional<std::uint64_t>, coop::protocol::kMaxPlayers + 1> escaping_us_{};  // by player number: last seen in an exit's scene
};

// The answer to a mate's JOIN_PARTNER. `normal` is CoopTeleport::AnswerFor
// (or declined: /teleport off here); `group` is AnswerFor as if no boss fight
// ran, taken only when we are where the exit goes and the asker is escaping,
// and only when our fight (or /teleport off) was the one refusal: busy stays
// busy.
CoopTeleport::Answer AnswerTo(const CoopTeleport::Answer& normal, const CoopTeleport::Answer& group, bool escaper,
                              bool at_exit_end);

} // namespace CoopEscape
