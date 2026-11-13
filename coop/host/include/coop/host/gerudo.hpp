#pragma once

// ORM Gerudo Fortress, the Haunted Wasteland and the Spirit Temple with
// several players, after oot-64's rules: the guard takes only the Link she
// catches to the cell (D-171, D-211), the group frees the carpenters and has
// one membership card, each player crosses the desert on his own, and the
// age is the room's (D-068, oot-remake D-R17) in a temple built for both
// ages. The portable side: actor names and small rules over readings the
// hook gives. No memory, no emulator, no sockets.
//
// Doc: oot-remake docs/GERUDO_ESPIRITU.md.
// Switch: LICHII_ORM_GERUDO (LICHII_COOP_GERUDO before test9;
//   lichii/hook/coop_gerudo.h; on unless 0) turns off the carpenter count
//   mend and the story age gate. The actors each game
//   runs on its own (runs_in_each_game) are a rule without a switch, as the
//   order puzzles are (CoopObjects::IsOwnPuzzle).
// Test: tests/host/gerudo_test.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

// ---- Actors each game runs on its own ---------------------------------------
//
// The actors with the security guard brain (AI/NpcSecurityGuard.brain, read
// from the romfs's actor packs on 2026-11-11): the Gerudo of the fortress and
// the valley, and the Hyrule Castle soldiers of the child's sneak. A guard
// sees only her own game's Link (the puppets are not Links) and catches him.
// Shared as townspeople, a copy's AI is held, so a guard another game owns
// could never catch our Link and her owner's catch played on our copy. Each
// game runs its own: every guard catches whoever she sees, and only that one
// goes to the cell (oot-64 D-171, D-211). Exact names, not a prefix: Nabooru
// (Npc_Gerudo_Nabooru_*) and the Gerudo without the brain stay shared.
bool is_sight_guard(std::string_view actor);
// The actors these rules keep in each game (the sight guards):
// CoopObjects::IsShared leaves them out. The Haunted Wasteland's guide Poe is
// each game's own too, but by hazards.hpp's is_own_hazard_actor alone
// (oot-remake D-R67: one place for it).
bool runs_in_each_game(std::string_view actor);
// The names is_sight_guard takes, for the tests and the log.
const std::vector<std::string_view>& sight_guard_names();

// ---- Two ages in one scene ----------------------------------------------------
//
// The Spirit Temple is one scene for both ages ("SpiritTemple", no _Child or
// _Adult in its name): its child half (the crawl hole, the Silver Gauntlets)
// and its adult half (silver blocks, the mirror shield) are rooms of the same
// scene. Every other place a child and an adult can both be is two scenes.
bool both_ages_scene(std::string_view scene);

// Whether a mate's STORY_EVENT start is ours to follow, by age. In a scene
// whose name has no era a story event belongs to the age of whoever starts
// it (Nabooru meeting the child, her Iron Knuckle fighting the adult): a game
// at the other age does not play it and gets its flags and steps only. With
// an era in the name the scene already says the age (the server sends a start
// only to the same scene). An unknown age (ours or the starter's): followed,
// as before this rule.
bool story_start_for_age(std::string_view scene, std::optional<protocol::Age> starter,
                         std::optional<protocol::Age> ours);

// The age reload (D-R17): whether a game that follows a mate's age change
// puts Link back where he stood (true) or leaves him at the scene's own start
// (false). In the Spirit Temple the start: a child in the adult half (behind
// the silver blocks) or an adult in the child half (behind the crawl hole)
// could not walk out. The start is the entrance from the Desert Colossus,
// which both ages reach.
bool age_reload_keeps_spot(std::string_view scene);

// Where that start is, for a scene age_reload_keeps_spot says no to: the Spirit
// Temple's SpiritTemple_Entrance (the entrance from the Desert Colossus, from the
// scene's start points, lichii/hook/coop_start_points.inc). Nothing for others.
// docs/RIESGOS_RESUELTOS.md "Mundo e historia": a reload that leaves Link more
// than kReloadStartNearM from it (the game's default start is another one) puts
// him there, with a line.
std::optional<protocol::Vec3> age_reload_start(std::string_view scene);
inline constexpr float kReloadStartNearM = 4.0f;

// ---- The carpenters' counts ---------------------------------------------------
//
// The four carpenters are shared one by one (shared_dungeon_adult.txt), and so
// are the counts the game keeps of them, as plain Ints: last write wins. Two
// rescues in two games within the same moment send the same count (each game
// added one to the same number), so one is lost, and the fourth rescue would
// then not reach 4: no membership card. This rule mends the count: once
// nothing runs in our game for a while, a count below the carpenters our game
// shows as helped is raised to it (never lowered, never above four). The
// game's own rescue event adds one inside the event, and the flag the rule
// counts (GerudoFortress_IsAfterHelpCarpenter_*) is set when that event ends,
// so outside events the two agree unless an update was lost.

// The flags it reads and writes, by name (the hook hashes them).
inline constexpr std::array<const char*, 4> kCarpenterHelpedFlags = {
    "GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_013_Adult",
    "GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_014_Adult",
    "GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_015_Adult",
    "GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_016_Adult",
};
inline constexpr std::array<const char*, 4> kThiefBeatenFlags = {
    "GerudoFortress_DestroyGerudoThief_Npc_Hylia_013_Adult",
    "GerudoFortress_DestroyGerudoThief_Npc_Hylia_014_Adult",
    "GerudoFortress_DestroyGerudoThief_Npc_Hylia_015_Adult",
    "GerudoFortress_DestroyGerudoThief_Npc_Hylia_016_Adult",
};
inline constexpr const char* kCarpentersHelpedCount = "GerudoFortress_Meet_FourCarpenter_NumOfCarpenterHelped";
inline constexpr const char* kCarpentersFoundCount = "GerudoFortress_Meet_FourCarpenter_NumOfCarpenterFound";
inline constexpr const char* kThievesBeatenCount = "Enemy_MiddleBoss_GerudoThief_DefeatNum";
inline constexpr const char* kGerudoCard = "IsGet.Item_Token_Gerudo";

struct CarpenterSettings {
  // A low count must stay low this long, with nothing running in our game,
  // before it is mended: the rescue event's own add lands within it.
  std::uint64_t settle_us = 3'000'000;
  // After a write, this long before the next one (the game applies its queue
  // on its next GameData update; the reading shows it then).
  std::uint64_t retry_us = 2'000'000;
  // All four free, no card, nothing running this long: said once in the log.
  std::uint64_t card_wait_us = 10'000'000;
};

struct CarpenterReading {
  std::uint64_t now_us = 0;  // any steady clock
  std::optional<bool> event_running;  // nothing: not known (nothing is mended)
  std::array<std::optional<bool>, 4> helped{};        // kCarpenterHelpedFlags
  std::array<std::optional<bool>, 4> thief_beaten{};  // kThiefBeatenFlags
  std::optional<std::int32_t> helped_count;  // kCarpentersHelpedCount
  std::optional<std::int32_t> found_count;   // kCarpentersFoundCount
  std::optional<std::int32_t> thieves_count; // kThievesBeatenCount
  std::optional<bool> card;                  // kGerudoCard
};

struct CarpenterWrite {
  const char* name = nullptr;  // one of the k*Count names
  std::int32_t value = 0;
  bool operator==(const CarpenterWrite&) const = default;
};

struct CarpenterOutput {
  std::vector<CarpenterWrite> writes;  // GameFlags::set_int each, now
  std::vector<std::string> log;        // one line each
};

class CarpenterCounts {
 public:
  explicit CarpenterCounts(CarpenterSettings settings = {}) : settings_(settings) {}

  // Once a frame (or a few times a second) while our Link is in the fortress.
  CarpenterOutput tick(const CarpenterReading& in);
  // Out of the fortress, or a new session: forget the timers and what was said.
  void reset();

 private:
  CarpenterSettings settings_;
  std::optional<std::uint64_t> low_since_;  // the counts low and our game calm since
  std::uint64_t not_before_ = 0;            // a write waits until then
  std::optional<std::uint64_t> all_free_since_;
  bool card_said_ = false;
  std::optional<std::int32_t> low_said_;      // the helped count last said low
  std::optional<std::int32_t> thieves_said_;  // the thieves count last said low
};

}  // namespace coop::host
