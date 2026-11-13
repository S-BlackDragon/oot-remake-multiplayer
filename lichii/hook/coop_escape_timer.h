// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM, the escape's countdown looked for in our game (coop/host/countdown_search.hpp; oot-remake
// docs/FINAL.md "Dónde vive la cuenta atrás", docs/RIESGOS_RESUELTOS.md "Mundo e historia").
// With LICHII_ORM_ESCAPE_CLOCK on and no LICHII_ORM_ESCAPE_CLOCK_WORD, while our Link is in
// CrumblingGanonCastle with no event running, the words of the GameUI module (the object that
// draws the minigame timer the escape runs on) and of the objects it points to are followed once
// a second, from the end of the first event in the scene (the collapse's start, whose flow starts
// the timer); the one that drops 1 s a second is our countdown. An event or a short load starts
// the samples again; leaving the scene or a load of 2 s (a Continue's reload) starts afresh.
// Found(): the word in LICHII_ORM_ESCAPE_CLOCK_WORD's format, for the escape clock of orm/final2 (CoopFinal::Final: with no word of its own it takes
// CoopEscapeTimer::Found() and parses it with CountdownWord::parse). Not found after three
// tries: the group's clock goes on without the game's timer (nothing is set here), said once.
//
// Switches: LICHII_ORM_ESCAPE_CLOCK (old LICHII_COOP_ESCAPE_CLOCK), LICHII_ORM_ESCAPE_CLOCK_WORD
// (given: no search), LICHII_ORM_ESCAPE_CLOCK_SEARCH (unset or 1: GameUI; 0: off; a list of
// places, parse_countdown_roots: GameUI and those), LICHII_ORM_ESCAPE_CLOCK_LENGTH_S (180).
// Log: "coop: escape clock: search on: ...", "coop: escape clock: looking for our countdown
// (try 1 of 3): ...", "coop: escape clock: our countdown found at main+0x4e7cfa8,0x1a0:f32 (...)"
// or "coop: escape clock: our countdown was not found (...): the group's clock goes on without
// the game's timer, nothing is set in our game (...)".
// Test: tests/lichii/escape_test.cpp (the Driver; the search in tests/host/countdown_search_test.cpp).
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/countdown_search.hpp"
#include "coop/host/game_memory.hpp"
#include "coop/host/player_reader.hpp"

namespace CoopPlatform {
class Platform;
}

namespace CoopEscapeTimer {

inline constexpr const char* kScene = "CrumblingGanonCastle";

struct Settings {
    bool clock = false;                   // LICHII_ORM_ESCAPE_CLOCK
    bool word_given = false;              // LICHII_ORM_ESCAPE_CLOCK_WORD is set
    std::optional<std::string> search;    // LICHII_ORM_ESCAPE_CLOCK_SEARCH's text
    std::optional<std::string> length_s;  // LICHII_ORM_ESCAPE_CLOCK_LENGTH_S's text
    // LICHII_ORM_ESCAPE_CLOCK_TRIES's text: how many searches of about 5 s before "not found" (3 by
    // default; docs/SONDAS.md 2026-11-12: three ran out before the remake's countdown showed).
    std::optional<std::string> tries;
};

// No emulator code: the tests drive it with a fake memory.
class Driver {
public:
    explicit Driver(const Settings& settings);

    bool On() const { return on_; }
    // The line that says which path runs (once, at start); empty when the clock is off.
    const std::vector<std::string>& Begin() const { return begin_; }

    // Once a frame. `memory` null: no game. `scene` nothing: loading. `event`: an event runs.
    // Returns whole lines ("coop: escape clock: ...").
    // `event_name`: the running event's name when known. The scene's own load events (DefaultBootEvent,
    // DefaultParentEvent: a warp, a Continue) do not start the countdown and do not arm the search (the probe
    // of 2026-11-12 spent its three tries before the collapse; docs/SONDAS.md row 1).
    std::vector<std::string> Frame(coop::host::GameMemory* memory, const std::optional<std::string>& scene,
                                   bool event, std::uint64_t now_us,
                                   const std::optional<std::string>& event_name = std::nullopt);
    static bool IsLoadEvent(std::string_view name);

    // The word found, as LICHII_ORM_ESCAPE_CLOCK_WORD takes it, or nothing.
    std::optional<std::string> Found() const;
    const coop::host::CountdownSearch& Search() const { return search_; }

    // Loading this long is a new visit of the scene (the search starts afresh).
    static constexpr std::uint64_t kNewVisitUs = 2'000'000;

private:
    bool on_ = false;
    std::vector<std::string> begin_;
    coop::host::CountdownSearch search_;
    bool armed_ = false;     // an event ran in the scene since we came: the countdown runs
    bool in_event_ = false;  // one runs now
    bool event_arms_ = false;  // the one running (or that just ended) is not a load event
    std::optional<std::uint64_t> loading_since_;
};

// Present thread, once a frame (after the boss rooms, beside the teleport).
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading);

// Any thread: the word found in this game, or nothing (CoopFinal takes it when it has none).
std::optional<std::string> Found();

} // namespace CoopEscapeTimer
