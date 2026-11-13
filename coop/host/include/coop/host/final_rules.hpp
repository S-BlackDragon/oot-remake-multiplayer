#pragma once

// ORM, the end of the game with up to six players (oot-remake docs/FINAL.md): the rules oot-64
// (ZMP) had for Ganon's Castle, the escape, Ganon and the ending that the remake's other layers
// (the story list, the dungeon lists, the boss rooms, group_exit) do not cover, as plain
// decisions over plain readings: no sockets, no game addresses (a GameMemory, as the flag
// readers take it). The hook reads the game and carries the answers out.
//
// - Counted Ints (shared_counts.txt, LICHII_ORM_COUNTS): an Int the game counts up one by one
//   as the group does things that are each a shared Bool (Ganon's Castle's barriers down,
//   InsideGanonCastle_Dungeon_Barrier_Count) is a plain shared Int, "last write wins": two games
//   that each add one at the same moment both write the same number and one is lost, and the
//   tower's barrier never sees six. The rule: the Int is never below the number of its Bools that
//   are set; when it is, it is raised to that (never lowered).
// - The escape's clock (ZMP D-164: one countdown for the group, the same value on every screen;
//   a player who joins with the countdown running gets the group's): the group's countdown
//   started when the collapse was first seen in the room (the server time of the first start of
//   CrumblingGanonCastle_Arrive, the story list's). A game whose own countdown shows more time
//   than the group's is set to the group's; never more time.
// - The ending holds the age (ZMP INVENTARIO G7: the credits load Link as a child; that change
//   must not reach a mate still in the escape or the fight): while a game is in the ending, its
//   age change is not the room's (D-R17 must not send it nor take it).
// - Twinrova's owner holds while it matters (ZMP D-231 and Twinrova's row 3: the ice remembers
//   whom it froze, the mirror shield's charge is the one who absorbs): the boss's owner is not
//   handed over while its Link is frozen by Kotake (the freeze is run by its game's Twinrova:
//   handed over, it would never thaw) nor, for "nearest", while it holds a part charge.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/boss_rules.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/game_memory.hpp"

namespace coop::host {

// ---- Counted Ints --------------------------------------------------------------------------

struct CountEntry {
  std::string name;                  // the Int, as written
  std::uint32_t hash = 0;            // flag_hash(name)
  std::vector<std::string> of_names;  // its Bools, as written
  std::vector<std::uint32_t> of;      // their hashes
  bool operator==(const CountEntry&) const = default;
};

// One counted Int a line:
//
//   # a comment; blank lines are fine
//   count:IntFlag BoolFlag BoolFlag ...
//
// Each flag is a name or 0x and 1 to 8 hex digits; 1 to 32 Bools, none twice; an Int counted on
// two lines, or counted and counting, is an error.
class CountList {
 public:
  static constexpr std::size_t kMaxBools = 32;

  CountList() = default;
  static std::optional<CountList> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<CountList> load(const std::filesystem::path& path, std::string* error = nullptr);

  const std::vector<CountEntry>& entries() const { return entries_; }
  bool empty() const { return entries_.empty(); }
  // The Ints and their Bools as shared-flag keys (index 0), each once, sorted: a count only
  // works when all of them are shared.
  std::vector<FlagKey> keys() const;

 private:
  std::vector<CountEntry> entries_;
};

struct CountFix {
  std::size_t entry = 0;  // in CountList::entries()
  std::int32_t from = 0;  // the Int now
  std::int32_t to = 0;    // the number of its Bools set
};

// What should be raised now. An entry is skipped while its Int or any of its Bools cannot be
// read (the manager not up, a load): nothing is guessed.
std::vector<CountFix> count_fixes(GameMemory& memory, const CountList& list);

// count_fixes, then each through the game's own queue (GameFlags::set_int). One log line a write
// ("count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)"), or a refusal.
std::vector<std::string> apply_counts(WritableGameMemory& memory, const CountList& list);

// ---- The escape's clock ----------------------------------------------------------------------

// Where our game keeps the escape's countdown (LICHII_ORM_ESCAPE_CLOCK_WORD). The remake runs it
// with the minigame timer (Defeat_Ganon's flow: EventTriggerMiniGameTimerStart and
// EventTriggerShowTimerUI); no GameData flag holds the live value (MiniGame_TimerValue and the
// MiniGame_Timer* Ints are a minigame's result, written by a query at its end), so the place is
// to be read in a game (docs/FINAL.md) and given as text:
//
//   main+0x<hex>[,0x<hex>...]:f32|u32|ms
//
// main+0x<hex> is an address in the game's main module; each further 0x<hex> follows the
// pointer read at the address so far (8 bytes, not null) and adds that offset. f32: seconds left
// as a float; u32: whole seconds; ms: milliseconds, u32. At most 8 pointer steps.
struct CountdownWord {
  // u32_mmss: minutes then seconds, two u32 words from the address (countdown_search.hpp).
  enum class Unit : std::uint8_t { f32_seconds, u32_seconds, u32_ms, u32_mmss };
  static constexpr std::size_t kMaxSteps = 8;
  static constexpr std::uint32_t kMaxSeconds = 3600;  // a read above this is not a countdown

  std::uint64_t base = 0;              // from main
  std::vector<std::uint64_t> offsets;  // one pointer step each
  Unit unit = Unit::f32_seconds;

  static std::optional<CountdownWord> parse(std::string_view text, std::string* error = nullptr);
  // The word's address now; nothing when a pointer on the way is unreadable or null.
  std::optional<std::uint64_t> address(GameMemory& memory) const;
  // Seconds left (a float rounded down); nothing when unreadable, not finite, below 0 or above
  // kMaxSeconds.
  std::optional<std::uint32_t> read_seconds(GameMemory& memory) const;
  // Straight into the live value (no queue: it is no flag). False when it cannot be written.
  bool write_seconds(WritableGameMemory& memory, std::uint32_t seconds) const;
};

struct EscapeClockSettings {
  std::string scene = "CrumblingGanonCastle";         // where the countdown runs
  std::string start_event = "CrumblingGanonCastle_Arrive";  // the story event it starts with
  std::uint32_t length_s = 180;  // the countdown's full length (ZMP's: 3 min; the remake's: to be read)
  std::uint32_t slack_s = 2;     // our clock may show this much more than the group's
  std::uint64_t again_us = 1'000'000;  // a set not seen yet: set again after this
  // ORM (oot-remake docs/SONDAS.md, 2026-11-12): a word the search took that drops 1 s a second for
  // a while but is not the countdown (it read 26-30 s while set to 25..12 each second): after this
  // many sets in a row that our game did not take, nothing more is set this escape (one line).
  int max_unheeded = 3;
};

struct EscapeClockInput {
  std::uint64_t now_us = 0;
  std::optional<std::string> scene;       // ours; nothing while loading
  std::optional<std::uint32_t> seconds;   // our countdown's seconds left; nothing: none runs or unreadable
  std::uint32_t server_now_ms = 0;        // the server's clock (ms, wraps); 0: not connected
};

struct EscapeClockAction {
  std::optional<std::uint32_t> set_seconds;  // write this into our countdown
  std::vector<std::string> log;
};

class EscapeClock {
 public:
  explicit EscapeClock(EscapeClockSettings settings = {}) : settings_(std::move(settings)) {}

  // A start of the story event (ours or relayed, StoryEvent::server_time_ms): the first one is
  // the group's start. Another scene's or another event's: ignored. A start after the group's
  // countdown ran out (the escape again after a game over) is the group's new start.
  void started(std::string_view event, std::uint32_t server_time_ms);
  EscapeClockAction update(const EscapeClockInput& in);
  void reset();  // a session ends

  // The group's seconds left now, or nothing before a start.
  std::optional<std::uint32_t> group_seconds(std::uint32_t server_now_ms) const;

 private:
  EscapeClockSettings settings_;
  std::optional<std::uint32_t> start_ms_;
  std::optional<std::uint64_t> set_at_us_;
  std::optional<std::uint32_t> set_to_;
  bool told_long_ = false;  // "longer than length_s" logged once
  int unheeded_ = 0;        // sets in a row our game did not take
  bool gave_up_ = false;    // max_unheeded reached: no more sets this escape
};

// ---- The ending holds the age ----------------------------------------------------------------

// Why an age change now is the ending's and not the room's (the credits load Link as a child),
// or nothing. `event` is the event our game runs (PlayerReader::read_event_name), `defeat_ganon`
// the value of Step_Defeat_Ganon (its value name's hash, GameFlags::get_enum).
std::optional<std::string> ending_holds_age(const std::optional<std::string>& event,
                                            std::optional<std::uint32_t> defeat_ganon);

// ---- Twinrova's owner -------------------------------------------------------------------------

enum class HandOver : std::uint8_t { hit, nearest };

struct HandOverInput {
  std::string_view body;               // the boss actor whose owner would change
  HandOver reason = HandOver::nearest;
  bool owner_up = true;                // the owner's Link is not downed
  bool owner_frozen = false;           // Kotake's ice holds the owner's Link
  // The owner's Link absorbs a witch's blast with the mirror shield now (its main AS command
  // GuardBeam*, absorbing_animation): the charge builds in its game, so the boss stays with it
  // for a hit and for "nearest" (owner_keeps).
  bool owner_charging = false;
  std::optional<unsigned> owner_charge;  // the owner's mirror shield charge (0 to 3), if read
  // ORM boss families (boss_rules.hpp, docs/JEFES.md section 9): what the owner's game holds
  // now (a grabbing part holding its Link, a tennis ball in flight) and where the body is.
  FamilyHold family;
  std::optional<coop::protocol::Vec3> body_at;
};

// Why the hand-over waits, or nothing (hand it over as the boss sync would). Twinrova's bodies
// (Twinrova, Koume, Kotake and their battle manager, the group's root while it is loaded,
// lichii/hook/coop_objects_sync.cpp) by the rule above; the grab and tennis families of
// boss_rules.hpp's table by family_holds (`family`, `body_at`).
std::optional<std::string> hand_over_holds(const HandOverInput& in);

// The owner decides nothing alone: the server hands a boss over (issue 150: to a hitter, to a
// nearer player). So the owner's game says it in its samples of the boss (object_state
// owner_keeps, owner_keeps_near) and the server waits. owner_keep is hand_over_holds for both
// reasons at once (`in.reason` is not read).
struct OwnerKeep {
  bool hit = false;      // a hitter does not take it (implies nearest)
  bool nearest = false;  // a nearer player does not take it
  std::string why;       // the reason, for the log; empty when it keeps nothing
  bool operator==(const OwnerKeep&) const = default;
};
OwnerKeep owner_keep(const HandOverInput& in);

// Our Link's main AS command is one of Link's frozen ones ("Freeze", "Freeze_01" to "_16",
// "FreezeBreak": Player_Adult/Child.root.asb; ExecutePlayerReactionFrozen). Kotake's ice is the
// only freeze in Twinrova's room.
bool frozen_animation(std::string_view name);

// Our Link's main AS command is one of the mirror shield's beam guards: "GuardBeam" (holding a
// beam on the shield), "GuardBeamHit" (taking it in), or their animations "Guard_Beam*"
// (Guard_Beam_St, Guard_Beam_Hit_UR, ...; Player_Adult/Child.root.asb). In Twinrova's room the
// only beams are the witches' blasts, which the mirror shield absorbs into its charge
// (Weapon_Shield_Mirror's AbsorbCount, ExecuteMirrorShieldAbsorb).
bool absorbing_animation(std::string_view name);

}  // namespace coop::host
