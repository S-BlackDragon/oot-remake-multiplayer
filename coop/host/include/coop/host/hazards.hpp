#pragma once

// ORM hazards with several players (oot-remake docs/PELIGROS.md): heat, deep
// water, freezing water, lava, the void, quicksand and the sandstorm, the
// Lens of Truth. Each game runs its own Link, so the game's own timers (the
// heat of the crater and the Fire Temple without the Goron Tunic, the breath
// under water without the Zora Tunic, the cold of Zora's Fountain) are each
// player's already: nothing of ours reads or writes them. What this file adds
// is where the co-op layer could get in their way:
//
// - Flags each game keeps for its own player (hazard_flags): the Lens on, the
//   guide Poe's and the Haunted Wasteland's respawn state, the void-out's
//   elevator reset and the Forest Temple flag the void-out reads, Navi's fall
//   hint. unshare_hazard_flags takes them off the shared set, whatever list
//   put them there (today only the built-in dungeon list shares one,
//   ForestTemple_IsInFallingCeilingRoom; the kit's adult list takes it off).
// - The guide Poe of the Haunted Wasteland is each game's own actor
//   (is_own_hazard_actor): shared as a townsperson, a game's copy would
//   follow the owner's Poe on the owner's way through the storm, not lead its
//   own Link (docs/objects.md "Townspeople").
// - The reach point of a downed Link (DownedReach): the game leaves a Link who
//   died in lava, quicksand, deep water, a whirlpool or the void lying where
//   no mate can stand and hold A beside him (oot-remake docs/CAIDO_REVIVIR.md
//   risk 3: the game over event that would bring him back from the void is
//   the one the downed gate holds shut). While he is down there, our samples
//   say he lies at his last firm ground in that scene: the others' puppet of
//   him lies there, and the revive reach (the mates' watch and the server
//   both measure it from our samples) works there. Our own game still has the
//   body where it is; the revive (the game over event's fairy branch, which
//   returns a Link from the void) gets him up.
//
// Plain readings, no game addresses, no sockets: the hook gives our reading
// and the downed state each frame and sends the reading frame() returns.
//
// Doc: oot-remake docs/PELIGROS.md.
// Switch: LICHII_COOP_HAZARDS=0 turns the flag guard and the reach point off (on
//   by default; the reach point only ever acts with LICHII_COOP_DOWNED on). The
//   guide Poe is each game's own always, like Navi.
// Test: tests/host/hazards_test.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/player_reader.hpp"

namespace coop::host {

// ---- Flags each player keeps -----------------------------------------------

struct HazardFlag {
  std::uint32_t hash = 0;  // flag_hash(name)
  std::string_view name;
  std::string_view why;    // what it says about one player
};

// Bools of the game (GameDataList, checked by name), never shared.
std::span<const HazardFlag> hazard_flags();
bool is_hazard_flag(FlagKey key);

// Takes every hazard flag off `shared` (named, any element). Returns the
// names it took off, in table order; nothing when `shared` is nothing.
std::vector<std::string_view> unshare_hazard_flags(std::optional<SharedFlags>& shared);

// ---- Actors each game runs for its own player --------------------------------

// The Haunted Wasteland's guide Poe (Npc_GuidePoe_Adult, and any other
// Npc_GuidePoe*): it leads the Link of its own game through the storm and
// its flags are that player's (hazard_flags).
bool is_own_hazard_actor(std::string_view name);

// ---- How Link went down ------------------------------------------------------

enum class DownKind : std::uint8_t {
  ground,     // on firm ground: a mate can reach him where he lies
  lava,
  sand,       // quicksand (the Haunted Wasteland, the Spirit Temple)
  water,      // on the surface: a swimming mate cannot hold A (A dives)
  underwater,
  whirlpool,
  abyss,      // fallen into the void, or far below his last firm ground
};
const char* to_string(DownKind kind);  // "on the ground", "in lava", ...

// A command of the dead Link's AS (Player_Adult/Player_Child, read from the
// romfs): Dead, DeadRagdoll, LandDead, LavaDead, SandDead, SwimDead,
// UnderwaterDead, WhirlpoolDead, DeadSelf, Die, Die_*, HookMoveDie...
bool is_dead_command(std::string_view command);
// What a command says about where Link is: lava ("LavaDead", "Move_Run_Lava",
// "Die_Lava"), sand ("SandDead", "JumpSand"), whirlpool, under water ("Dive",
// "Underwater", "Heavy_Swim": iron boots on the bottom), water ("Swim"), and
// ground for the rest.
DownKind kind_of_command(std::string_view command);
// Link has his feet on firm, safe ground playing this: not falling, jumping,
// swimming, diving, floating, climbing, hanging, on a hook, in lava or sand,
// hurt, frozen, dead. Walking on ice, on a horse or in a hot room is firm.
bool firm_ground_command(std::string_view command);

// ---- The reach point of a downed Link ---------------------------------------

struct ReachSettings {
  bool on = true;
  // Down this far below the last firm ground: in the void, whatever the
  // command says (a body that slid off a ledge while down too).
  float abyss_drop = 10.0f;
  // ... or this far, when the last command before the death was a fall.
  float fall_drop = 3.0f;
  // A body in the void that comes back this close to the firm ground's height
  // (the game's own void-out return ran first): its real place is sent again.
  float back_within = 2.0f;
  // A firm ground further than this from the body is not used (a bad reading).
  float max_distance = 150.0f;
  // Firm: a firm command and at most `firm_climb` m/s up or down, this long.
  std::uint64_t firm_for_us = 300'000;
  float firm_climb = 2.0f;
  // Down with no dead command yet: the command before the death decides the
  // kind until one comes, at most this long after going down.
  std::uint64_t kind_wait_us = 2'000'000;
  // After a revive from where no mate could reach us (lava, sand, water, the
  // void): this long to say how it went (docs/RIESGOS_RESUELTOS.md, "Mundo e
  // historia"): on firm ground again, down again, or still not on firm ground.
  std::uint64_t after_revive_us = 5'000'000;
};

class DownedReach {
 public:
  explicit DownedReach(ReachSettings settings = {}) : settings_(settings) {}

  // Once a frame on the game's thread, after the downed state machine's tick
  // and before the session's frame: the reading to send. It is `reading`
  // itself, but while our Link is down (downed or being revived) where no
  // mate can reach him, its position is his last firm ground.
  std::optional<PlayerReading> frame(const std::optional<PlayerReading>& reading, DownedState state,
                                     std::uint64_t now_us);

  // One line each since the last call: "down in lava ('LavaDead') ...".
  std::vector<std::string> take_log();

  struct Firm {
    protocol::Vec3 position{};
    std::string scene;
    std::uint64_t at_us = 0;
  };
  // Where our Link last stood firm in this scene (nothing yet, or since the
  // scene changed).
  const std::optional<Firm>& firm() const { return firm_; }
  // While down: how (nothing while up).
  std::optional<DownKind> kind() const { return down_ ? std::optional<DownKind>(kind_) : std::nullopt; }
  // The place our samples say now instead of the real one (nothing: the real one).
  std::optional<protocol::Vec3> anchor() const { return anchor_; }

 private:
  void track_firm(const PlayerReading& r, std::uint64_t now_us);
  void went_down(const PlayerReading& r, std::uint64_t now_us);
  void while_down(const PlayerReading& r, std::uint64_t now_us);
  void decide(const PlayerReading& r, const std::string& by);
  void release(const char* why);
  void after_revive(const PlayerReading& r, std::uint64_t now_us);

  ReachSettings settings_;
  std::optional<Firm> firm_;
  // Firm conditions held since (0: not holding); the last frame's height and time.
  std::uint64_t firm_since_ = 0;
  std::optional<float> last_y_;
  std::uint64_t last_at_ = 0;
  std::string last_scene_;
  std::string last_live_command_;  // the newest command while up
  std::string last_live_scene_;    // ... and its scene
  // Down.
  bool down_ = false;
  std::uint64_t down_at_ = 0;
  std::string before_;        // the command before the death
  std::string dead_command_;  // the dead command seen, empty: none yet
  DownKind kind_ = DownKind::ground;
  bool settled_ = false;  // the kind decided (and logged) at least once
  // A revive from a kind no mate could reach, being watched (after_revive_us).
  struct Revived {
    DownKind kind = DownKind::ground;
    std::uint64_t at_us = 0;
  };
  std::optional<Revived> revived_;
  std::optional<protocol::Vec3> anchor_;
  std::vector<std::string> log_;
};

}  // namespace coop::host
