#pragma once

// ORM, boss rooms with up to six players (oot-remake docs/JEFES.md, D-R17).
// The rules oot-64 (ZMP) had for its boss rooms that the remake's shared boss
// room (lichii/hook/coop_boss.h, daurosk's) did not, as plain decisions over
// plain readings: no game addresses, no sockets. The hook reads the game and
// carries the answers out (lichii/hook/coop_boss_game.cpp).
//
// - Pulled in while down (oot-64 D-117 c, "a downed player at the boss door
//   goes in with the group, still down: the door is no free revive"). The
//   boss sync moves a player who is in the dungeon into the room once a mate
//   is past the door. A Link lying down (DownedState::downed) is moved as he
//   lies, never by the game's scene change: a load would bring him back up.
//   One being revived, or in his game over, is not moved at all.
// - Back into the boss room after a game over in it (oot-64 PLAN 2.8.6, "in a
//   boss room the respawn after a group defeat is the room's entrance, as in
//   the original"). A game over that starts while Link stands in a boss room
//   is remembered; after its reload (Continue), if Link is up in the same
//   dungeon scene but outside that room, and its boss is not beaten, he is
//   moved back in (again every 3 s, 3 tries, until he is seen in the room). If
//   Continue already put him in the room (the remake's own boss retry), or in
//   another scene, nothing is done. Each game does it for its own Link, so the
//   whole group is back in, even though a room entered with no intro moves no
//   one in (docs/dungeons-boss.md, issue 148).
//
// - The families that break with several players (grab, tennis, mirror): see
//   below, "The families".
//
// Doc: oot-remake docs/JEFES.md (D-R17; section 9 for the families).
// Switch: LICHII_COOP_BOSS_PULL_DOWNED, LICHII_COOP_BOSS_RESPAWN (both 1 when
//   LICHII_COOP_DOWNED=1); LICHII_ORM_BOSS_FAMILIES (on).
// Test: tests/lichii/boss_rules_test.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/ride.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// ---- Pulled in while down ----------------------------------------------------

enum class BossPull : std::uint8_t {
  normal,       // up: the scene change while our own event runs, else the matrix move
  matrix_only,  // lying down: moved as he lies, never by a scene change
  none,         // not moved now
};
const char* to_string(BossPull pull);

// How the boss sync may move our Link into the room. `pull_downed` is the
// switch (LICHII_COOP_BOSS_PULL_DOWNED, on by default; =0 turns it off): off,
// a downed Link stays where he fell (the sync before this rule) and the next
// try after he is up moves him.
BossPull boss_pull(DownedState state, bool pull_downed);

// ---- Back into the boss room after a game over in it -------------------------

struct BossRespawnSettings {
  // A game over seen and no load for this long: it was a fairy (alone, the
  // game's own), not Continue.
  std::uint64_t load_wait_us = 60'000'000;
  // After the load, Link must be up within this, or the rule gives up.
  std::uint64_t up_wait_us = 30'000'000;
  // Moved and not seen in the room this long (readable): moved again...
  std::uint64_t retry_us = 3'000'000;
  // ...this many moves at most.
  unsigned tries = 3;
};

struct BossRespawnInput {
  std::uint64_t now_us = 0;
  // The scene and Link's place were read this frame (false while loading).
  bool readable = false;
  std::optional<std::string> scene;
  // The boss room (BOSS_ROOM's room index) Link stands in, if any.
  std::optional<std::uint8_t> in_room;
  // That scene's boss room is beaten (as the boss sync knows it).
  bool beaten = false;
  // Our game over runs (the downed state machine's game_over, or the game
  // over event by name).
  bool game_over = false;
  // Link is up (the downed state machine says alive).
  bool up = false;
  // An event runs (the load's own wait, for one).
  bool event = false;
};

struct BossRespawnAction {
  std::optional<std::uint8_t> move_to;  // move Link into this boss room (its arrival point)
  bool by_event = false;                // an event runs: the game's scene change can take it
  std::vector<std::string> log;
};

class BossRespawn {
 public:
  explicit BossRespawn(BossRespawnSettings settings = {}) : settings_(settings) {}

  // A few times a second, on the present thread.
  BossRespawnAction tick(const BossRespawnInput& in);
  void reset();
  // A game over in a boss room is remembered, waiting for its reload.
  bool armed() const { return phase_ != Phase::idle; }

 private:
  enum class Phase : std::uint8_t { idle, over, loading, back, moved };
  void idle(BossRespawnAction& out, const std::string& why);

  BossRespawnSettings settings_;
  Phase phase_ = Phase::idle;
  bool over_last_ = false;
  std::string scene_;
  std::uint8_t room_ = 0;
  std::uint64_t since_ = 0;
  unsigned moves_ = 0;
};

// ---- The families that break with several players (oot-remake docs/JEFES.md, section 9) ----
//
// ZMP's family 3 (PLAN_100 section 8, risk 5; oot-64 D-124, D-134 to D-137, D-168, D-231): a boss
// or an enemy that acts on "the player", one Link at a time. In the remake its AI knows only its
// own game's Link, so each such rule turns into "it acts on its owner's Link, and its owner must
// not change while it does" (the Twinrova rule of docs/FINAL.md is the model: the owner's game
// says it in its samples, object_state::owner_keeps, and the server waits):
//
// - grab (D-134, D-135, D-168: Dead Hand, Like Like, Morpha's tentacles, Bongo Bongo's hands,
//   Wallmaster, Floormaster, ReDead, Gibdo). Our Link is held when its main AS command is one of
//   Link's held ones (held_animation) and a grabbing part of the table is within its reach. The
//   held player is his game's focus: the part's AI holds him there, so while it does, our samples
//   of its group carry owner_keeps (neither a hitter nor a nearer player takes it; handed over,
//   its AI would stop with our Link still held). A boss's or mini-boss's bodies are kept wherever
//   they are; an enemy's only near the hold (another Like Like across the room is not). A mate's
//   hit still goes to the owner as DAMAGE: a mate frees the held one as in the original (D-135:
//   Din's Fire on the tentacle, a sword on the Like Like). The others see him on their copy of
//   the part: our samples' ride (PLAYER_SAMPLE component 30) names it, as a lift does.
//   Released when the game lets go (the AS command changes), our Link is downed, we load, leave
//   or disconnect, or after the server's 30 s (owner_keep_max_us).
// - tennis (INVENTARIO F5 and G4, D-124: Phantom Ganon, Ganondorf). The ball is the boss's
//   own, in its owner's game: it flies at the owner's Link and only the owner's game returns it
//   (no copy's swing counts, so there is never a double return). While a ball of the group is
//   in our game, our samples of the boss carry owner_keeps: neither a hitter's arrow nor a nearer
//   player takes the boss mid-volley (its ball would stop in the old owner's game). Between
//   volleys the boss goes to a hitter as before (issue 150).
// - mirror (Twinrova, docs/FINAL.md): Kotake's ice and the mirror shield's charge, the rule in
//   final_rules.hpp (hand_over_holds); the table lists its bodies.
//
// Switch: LICHII_ORM_BOSS_FAMILIES (on; 0 turns grab and tennis off, Twinrova has its own).

enum class BossFamily : std::uint8_t { grab, tennis, mirror };
const char* to_string(BossFamily family);

enum class FamilyRole : std::uint8_t {
  body,  // kept for its group
  part,  // grab: holds a Link; tennis: the ball
};

struct FamilyActor {
  std::string_view name;  // the actor (Pack/Actor), exactly
  BossFamily family = BossFamily::grab;
  std::string_view group;  // who, for the rule and the log ("Morpha", "Like Like")
  FamilyRole role = FamilyRole::body;
  // The group's actors are kept wherever they are in the room (a boss, a mini-boss with its
  // hands); false: only near the hold (an enemy that is its own grabber).
  bool whole = false;
  float reach = 0.0f;  // grab part: our Link held within this of its origin (m)
};

// The table (data, from the game's Pack/Actor list and Player_Adult/Child.root.asb, 2026-11-12).
std::span<const FamilyActor> family_actors();
// Its entry for an actor name, or null.
const FamilyActor* family_actor(std::string_view actor);

// Our Link's main AS command (or animation) is one of Link's held ones: "Grab", "GrabA/B/X/Y"
// (the mashing), "GrabWait", "GrabEd", "GrabAway", "Grab_*" (Grab_Air_*, Grab_Head_*,
// Grab_Bongo_St, Grab_L/R/U/D, Grab_Reaction_*), "Eaten", "Eaten_*" (the Like Like), "FaceGrab",
// "Face_Grab*". Not "GrabMovableObj" nor "GrabMoveActorType" (a block), nor a ledge's "Cliff_Hold*".
bool held_animation(std::string_view command);

// The key a shared object goes by in a ride (protocol::Ride::platform): a placed object's
// placement hash (as a lift's), else its id folded with the top bit set. The object sync's id is
// the same in every game, so every game finds its own copy by it.
std::uint64_t family_key(const protocol::ObjectId& id);

// What our game holds now, for the keep rule.
struct FamilyHold {
  std::string_view held_group;            // the group whose part holds our Link ("" none)
  std::optional<protocol::Vec3> held_at;  // that part's place
  std::string_view rally_group;           // the tennis group whose ball is in our game ("" none)
  bool operator==(const FamilyHold&) const = default;
};

// An enemy's body this near the hold is the holder's (a split Floormaster's small ones).
inline constexpr float kFamilyHoldNear = 3.0f;

// Why our game keeps `actor` (at `at`, its place; nothing: not known) for a hit and for a nearer
// player alike, or nothing. Mirror (Twinrova) is not decided here: hand_over_holds.
std::optional<std::string> family_holds(const FamilyActor& actor, const FamilyHold& hold,
                                        const std::optional<protocol::Vec3>& at);

// One object of the walk the table names.
struct FamilySeen {
  std::string_view name;  // outlives the walk call
  protocol::ObjectId id;  // nothing shared (a ball, never shared): id stays {}
  protocol::Vec3 position{};
  std::uint8_t owner = 0;  // its owner's player number; 0: not known or not shared
};

struct FamilyWalkIn {
  std::uint64_t now_us = 0;
  std::uint8_t self = 0;                // our player number
  bool up = true;                       // our Link is not downed
  bool held = false;                    // our Link's main AS command is a held one
  std::optional<protocol::Vec3> link;   // our Link's place this walk
  std::vector<FamilySeen> seen;         // others than the table's are skipped
};

struct FamilyWalkOut {
  FamilyHold hold;
  std::optional<protocol::Ride> ride;  // our Link on the holding part, for our samples
  std::vector<RidePlatform> parts;     // this walk's shared grabbing parts, by family_key
  std::vector<std::string> log;        // whole lines without "coop: "
};

// Once per objects walk: finds the hold and the volley, says them when they change, and says
// once each whether the readings work ("check: grab ok|fallback ...", "check: tennis ...").
class FamilyWatch {
 public:
  // Held this long with no grabbing part within reach: the fallback check line.
  static constexpr std::uint64_t kNoHolderUs = 1'000'000;
  // A tennis boss owned this long with no ball of its group ever seen: the fallback line (Phantom
  // Ganon's first half, the paintings, has no ball: well past it).
  static constexpr std::uint64_t kNoBallUs = 300'000'000;

  FamilyWalkOut walk(const FamilyWalkIn& in);
  void reset();  // a scene change, a reconnection
  const FamilyHold& hold() const { return hold_; }

 private:
  FamilyHold hold_;
  std::string held_by_;            // the holder last said ("<actor> <key>"; "" none)
  std::string_view held_group_;    // its group
  std::string_view rally_said_;    // the volley last said (only while we own its boss)
  std::uint64_t no_holder_since_ = 0;
  bool grab_ok_said_ = false;
  bool grab_fallback_said_ = false;
  std::vector<std::pair<std::string_view, std::uint64_t>> owned_since_;  // tennis groups we own
  std::vector<std::string_view> ball_seen_;       // ...whose ball was seen
  std::vector<std::string_view> tennis_said_;     // ok said
  std::vector<std::string_view> tennis_fallback_;  // fallback said
};

}  // namespace coop::host
