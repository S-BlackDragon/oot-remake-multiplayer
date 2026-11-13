// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM, the end of the game with up to six players: the hook glue of the rules
// in coop/host/final_rules.hpp (oot-remake docs/FINAL.md, "Pegamento"). No
// emulator code: coop_final_game.cpp reads the switches, hands the story
// messages over and calls Frame once a frame with the game's memory. Tested in
// tests/lichii/final_test.cpp on fake memory.
//
// - Counted Ints (LICHII_ORM_COUNTS=<file>, old name LICHII_COOP_COUNTS; the
//   kit's shared_counts.txt): the list's Ints and Bools are added to the
//   shared flags at the session's start, and every 0.5 s, while our Link is in
//   the game, apply_counts raises an Int below the number of its Bools set.
//   Log: "coop: count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)".
// - The escape's clock (LICHII_ORM_ESCAPE_CLOCK=1, old LICHII_COOP_ESCAPE_CLOCK;
//   off by default): the group's start is the first CrumblingGanonCastle_Arrive
//   of the room (a relayed STORY_EVENT start or played, with its server time,
//   or our own game running it, at the server's time now). Our countdown is
//   read and written at LICHII_ORM_ESCAPE_CLOCK_WORD (CountdownWord: to be
//   found in a game); without it the start is kept and nothing is set.
//   LICHII_ORM_ESCAPE_CLOCK_LENGTH_S: the countdown's length (180 by default,
//   ZMP's). Log: "coop: escape clock: ours shows 180 s, the group's 150 s (it
//   started 30 s ago): set to 150 (ZMP D-164)".
// - The ending holds the age: in coop_age_game.cpp already (AgeInput::held).
// - Twinrova's owner (LICHII_ORM_TWINROVA_KEEP, on by default): our Link up and
//   frozen (its main AS command Freeze*) in the Spirit Temple goes to the
//   object sync (CoopObjects::SetKeep): our samples of Twinrova then carry
//   object_state::owner_keeps and the server waits with the hand-over. The
//   same while our Link charges the mirror shield with a witch's blast (its
//   main AS command GuardBeam*, host::absorbing_animation): owner_keeps, so
//   neither a hitter nor a nearer player takes the boss mid-charge. The
//   charge count itself is not read (nothing). Log: "coop: twinrova: ..."
//   when it starts and stops; one check line, "coop: check: twinrova charge
//   ok <how>" the first time a charge is seen, or "... fallback <why>" when
//   our Link's AS command is not read in the Spirit Temple (then only the
//   ice keeps the boss).
// - The boss families (oot-remake docs/JEFES.md section 9; LICHII_ORM_BOSS_FAMILIES
//   is read by the objects walk): our Link up with a held main AS command
//   (coop::host::held_animation: Grab*, Eaten*, Face_Grab*) goes to the object
//   sync as KeepOut::held, which then looks for the grabbing part that holds
//   it. Log: "coop: grab: our Link's AS command <c> is a held one" and
//   "coop: grab: our Link's AS command is no held one any more (<c>)".
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/final_rules.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/protocol/messages.hpp"
#include "coop_every.h"

namespace CoopFinal {

struct Settings {
    std::optional<std::string> counts;        // the counted-Int list's file; nothing: off
    bool escape_clock = false;                // LICHII_ORM_ESCAPE_CLOCK
    std::optional<std::string> clock_word;    // LICHII_ORM_ESCAPE_CLOCK_WORD's text
    std::optional<std::string> clock_length;  // LICHII_ORM_ESCAPE_CLOCK_LENGTH_S's text
    bool twinrova = true;                     // LICHII_ORM_TWINROVA_KEEP
};

// What our game shows this frame.
struct FrameIn {
    std::uint64_t now_us = 0;                // steady clock
    bool link = false;                       // our Link was read (a save is loaded)
    std::optional<std::string> scene;        // ours; nothing while loading
    std::optional<std::string> event_name;   // the event our game runs, while one runs
    std::optional<std::string> animation;    // our Link's main AS command
    coop::host::DownedState downed = coop::host::DownedState::alive;
    std::uint64_t server_now_us = 0;         // 0: not connected
    // ORM (coop_escape_timer.h, docs/RIESGOS_RESUELTOS.md): the countdown word the search found
    // in our game, in LICHII_ORM_ESCAPE_CLOCK_WORD's format; taken when no word was given.
    std::optional<std::string> found_word;
};

// Our Link for the object sync (CoopObjects::SetKeep).
struct KeepOut {
    bool up = true;
    bool frozen = false;
    std::optional<unsigned> charge;
    bool charging = false;  // absorbing a witch's blast with the mirror shield
    // ORM boss families (docs/JEFES.md section 9): our Link up and its main AS command a held
    // one (coop::host::held_animation), anywhere: the objects walk looks for what holds it.
    bool held = false;
    bool operator==(const KeepOut&) const = default;
};

struct FrameOut {
    KeepOut keep;
    std::vector<std::string> log;  // whole lines ("coop: ...")
};

class Final {
public:
    static constexpr std::uint64_t kCountsEveryUs = 500'000;
    static constexpr std::string_view kTwinrovaScene = "SpiritTemple";
    // Our Link read in the Spirit Temple with no AS command this long: the
    // charge cannot be seen (the check line's fallback).
    static constexpr std::uint64_t kNoAnimationUs = 2'000'000;

    explicit Final(Settings settings = {});

    // The session starts: the counted list is loaded and its flags added to
    // `shared` (only when flags are shared at all); the clock starts over.
    // Returns the lines to log.
    std::vector<std::string> Begin(std::optional<coop::host::SharedFlags>& shared);

    // A STORY_EVENT from the server (start or played: a start of the room).
    void Story(const coop::protocol::StoryEvent& m);

    // Once a frame, after the session's frame. `memory`: the game's, null when
    // no game runs.
    FrameOut Frame(coop::host::WritableGameMemory* memory, const FrameIn& in);

    bool CountsOn() const { return counts_.has_value(); }
    bool ClockOn() const { return clock_.has_value(); }
    const std::optional<coop::host::CountdownWord>& Word() const { return word_; }

private:
    Settings settings_;
    std::optional<coop::host::CountList> counts_;
    CoopEvery::Every counts_due_{kCountsEveryUs};
    std::optional<coop::host::EscapeClockSettings> clock_settings_;
    std::optional<coop::host::EscapeClock> clock_;
    std::optional<coop::host::CountdownWord> word_;
    std::vector<std::string> pending_;  // lines from Begin's settings and Story, logged by Frame
    bool own_start_seen_ = false;       // our game ran the start event this time in the scene
    bool told_no_word_ = false;
    std::optional<std::string> found_taken_;  // the found word last taken (or refused)
    std::string keep_why_;              // the last Twinrova reason logged
    std::uint64_t no_animation_since_ = 0;  // in the Spirit Temple, no AS command read; 0: not
    bool charge_ok_said_ = false;        // "coop: check: twinrova charge ok" said
    bool charge_fallback_said_ = false;  // "coop: check: twinrova charge fallback" said
    bool held_said_ = false;             // "coop: grab: our Link's AS command ... is a held one" said last
};

} // namespace CoopFinal
