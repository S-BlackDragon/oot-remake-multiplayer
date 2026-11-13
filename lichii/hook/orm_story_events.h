// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "story cutscenes for everyone" (oot-remake docs/CINEMATICAS.md), P1:
// asking the game to start an event by its name, from the svc cave.
// Build e0f3ccfaf50065ae; offsets into main.
//
// The game's own way with an actor (the game over's, main+0x4ae6bc):
// main+0x4aa890(component, &box), `component` an actor's event component
// [[actor + 0x270] + 0x90] (for the game over, Link's), `box` a struct whose
// first field is the name's char*. It builds the 0x1b20-byte event request on
// its own stack (main+0xd79f10), puts the component's actor in it, copies the
// name into the request's own buffer (+0x1a90, 128 bytes), and hands it to
// main+0xf61460(component, &request, 0), which returns 1 at once when
// u32 [component + 0x28] is 3, else sends a copy to the event manager
// (main+0xd7a820) and, when taken, sets [component + 0x28] = 1. Returns that
// answer in bit 0 (code_004.c f_804ca890, code_011.c f_80f81460).
//
// No emulator code here: the cave calls go through CoopPuppet::Guest and the
// lines to log are handed back, so tests run it on fake memory. The glue is in
// orm_story_events_game.cpp.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/player_reader.hpp"
#include "coop_puppet.h"

namespace coop::overlay {
struct Shown;
}

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace OrmStoryEvents {

inline constexpr std::uint64_t kRequestEvent = 0x4aa890;  // (component, &box) -> bit 0
inline constexpr std::uint64_t kMalloc = 0x14c6c90;        // the game's malloc(size)
inline constexpr std::uint64_t kActorState = 0x2c;
inline constexpr std::uint32_t kActorStatePresent = 4;
inline constexpr std::uint64_t kActorComponents = 0x270;
inline constexpr std::uint64_t kComponentEvent = 0x90;
inline constexpr std::uint64_t kComponentPhase = 0x28;  // u32; 1 after a taken request
// Our block from the game's malloc, kept for the whole run: the box's char*
// at +0, the name (NUL-terminated) from +0x10.
inline constexpr std::uint64_t kBlockBox = 0x0;
inline constexpr std::uint64_t kBlockName = 0x10;
inline constexpr std::size_t kMaxName = 127;  // the request's buffer holds 128 with the NUL
inline constexpr std::uint64_t kBlockSize = kBlockName + kMaxName + 1;

// 1 to kMaxName characters of A-Z, a-z, 0-9 and _ (every event name in the
// romfs is so).
bool ValidName(std::string_view name);

// One request at a time. Ask on any thread; Step from the cave (CoopSvc
// caller contract: asked with after_call false once a frame, then with each
// call's x0).
class Requester {
public:
    // Ask for `name` with the event component of `actor` (our Link). A
    // request not yet made is replaced. False (and nothing asked) for a bad
    // name, no main or no actor.
    bool Ask(std::uint64_t main, std::uint64_t actor, std::string name);

    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call,
                                         std::uint64_t result);

    // The lines to log since the last call ("asked ...", "the game took ...").
    std::vector<std::string> TakeLog();

    // For tests: the answer of the last request made (bit 0 of x0), if any.
    std::optional<bool> LastAnswer();

private:
    enum class Phase { idle, allocating, requesting };
    std::optional<CoopPuppet::Call> Prepare(CoopPuppet::Guest& guest);

    std::mutex mutex_;
    std::uint64_t main_ = 0;
    std::uint64_t actor_ = 0;
    std::string name_;  // pending when not empty
    std::string asked_;  // the one being requested
    std::uint64_t component_ = 0;
    std::uint64_t block_ = 0;
    bool alloc_failed_ = false;
    Phase phase_ = Phase::idle;
    std::optional<bool> last_answer_;
    std::vector<std::string> log_;
};

// orm_story_events_game.cpp (emulator only): the run's requester, its cave caller
// added on first use. Logs "coop: story: ..." lines as the cave reports.
void Ask(std::uint64_t main, std::uint64_t actor, const std::string& name);

// ---- The game's force skip (ZMP D-048; coop/host/story.hpp, "Skipping") ----
// What the AI node SeqOneShotEventRequestForceSkip (main+0x32d1890) does, as
// the boss rooms do it (coop_boss_game.cpp, docs/dungeons-boss.md): while an
// event runs, S = [[main + 0x4e94840] + 0x68], event = [[S + 0xeb8] + 8 *
// u32 [S + 0xf18]] (u32 [S + 0xeb0] events), and main+0x3b3af30(event), which
// calls the skip of the event's player ([event + 0x220], vtable slot 0x58; no
// player, nothing). One shot is cut a call (a skipped intro plays its next).
inline constexpr std::uint64_t kEventManagerPtr = 0x4e94840;
inline constexpr std::uint64_t kEventSession = 0x68;
inline constexpr std::uint64_t kSessionCount = 0xeb0;
inline constexpr std::uint64_t kSessionEvents = 0xeb8;
inline constexpr std::uint64_t kSessionCurrent = 0xf18;
inline constexpr std::uint64_t kForceSkip = 0x3b3af30;

// One skip at a time. Want on any thread; Step from the cave (the CoopSvc
// caller contract, as Requester's).
class Skipper {
public:
    // Skip the event that runs when the cave comes next. `why` goes in the log
    // line. False (and nothing asked) with no main.
    bool Want(std::uint64_t main, std::string why);

    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call);

    // The lines to log since the last call ("force skip of the event ...").
    std::vector<std::string> TakeLog();

private:
    std::mutex mutex_;
    std::uint64_t main_ = 0;
    std::string why_;  // pending when not empty
    std::vector<std::string> log_;
};

// orm_story_events_game.cpp (emulator only): the run's skipper, its cave
// caller added on first use (after the requester's). Logs "coop: story: ...".
// Used by the shared cutscenes (StoryOutput::skip) and the test script's
// `skip` (coop_testinput.h: the owner's skip in a scenario).
void Skip(std::uint64_t main, const std::string& why);

// Any thread: our own game's event was skipped by other means than the pad
// (the test script's `skip`): the story layer counts it as the owner's skip
// press (coop::host::Story::note_skip), so the end of an event we own goes out
// as STORY_EVENT `skipped`. The pad's PLUS and MINUS are read by Frame.
void NoteSkip(const std::string& how);

// ---- Story cutscenes for everyone (P2; the rules are coop/host/story.hpp) ----
// Only with LICHII_ORM_STORY_EVENTS=1 (SwitchOn). The list of shared events is
// the built-in one (coop::host::kDefaultStoryList), or the file
// LICHII_ORM_STORY_LIST names (old name LICHII_COOP_STORY_LIST; the kit's
// shared_story.txt; a bad file turns the feature off). Upstream's own
// LICHII_COOP_STORY (coop_story.h, issue 173, test9) shares the story flags and
// is on by default; this layer adds what it does not do: a listed cutscene
// starts in every game in the scene at once, `solo` entries, `step:` flags.

// Our switch from two variables, in coop_switch.h's words (1/true/yes/on,
// 0/false/no/off): LICHII_ORM_STORY_EVENTS when it says one of them; else the
// old name, LICHII_COOP_STORY, when it says on, so kits written before test9
// (LICHII_COOP_STORY=1) keep the cutscenes. Unset (or unknown): off.
bool SwitchOn(const std::optional<std::string>& events, const std::optional<std::string>& old_story);

// The cutscenes are on (and their list was read).
bool On();

// At the session's start: when on, adds the list's HavePlayedEvent flags (and
// each entry's more flags) to the shared flags, making the list when there
// is none.
void AddShared(std::optional<coop::host::SharedFlags>& shared);

// Present thread: the host loaded a save (FrameReport::host_reset): what ran
// and what the room played is forgotten (coop::host::Story::host_reset), and
// the flags read from the old save are read again from the loaded one.
void HostReset();

// Network thread: a STORY_EVENT is queued for the next frame.
void OnEvent(const coop::net::Event& event);

// Present thread, once a frame, after the session's frame. `main_base` 0: no
// game. `event_name`: the current event's name while one runs (read once a
// frame by the hook). Sends STORY_EVENT start/end, asks the game for a
// followed event (Ask above), and logs "coop: story: ..." lines.
// `others`: the other players as presence shows them (near= events, ORM
// docs/RIESGOS_RESUELTOS.md "Mundo e historia"); null when not connected.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading,
           const std::optional<std::string>& event_name, std::uint64_t server_now_us,
           const std::vector<coop::overlay::Shown>* others = nullptr);

} // namespace OrmStoryEvents
