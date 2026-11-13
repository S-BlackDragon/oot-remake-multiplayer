#pragma once

// ORM D-R19 (2026-11-11), as oot-64 D-233/D-234: Epona is the group's
// (one player wins her at Lon Lon and every game has her), each player calls
// and rides his own, and the others see him riding a horse.
//
// This file is the portable side (no sockets, no emulator):
// - The shared step: Step_Get_Epona is shared forward only, and only its last
//   value (Complete): "carrera en curso" or "the escape is on" never reaches
//   another game (D-R20, matiz). A game whose own race or escape runs holds a
//   Complete from elsewhere until it is over (race_running), so its own race
//   flow does not run on top of a step it never reached.
// - Each game's own: every horse (Epona and the ranch, Ingo's, the archery
//   and the cutscene horses), Ingo (his race) and the Gerudo horseback
//   archery (its host, pots and targets): never shared as objects, so one
//   player's Epona is never copied onto another's and each player runs his
//   own race and his own archery (own_per_game).
// - The rider: what a remote player's AS command says about his horse
//   (ride_of) and which gait a horse under him plays (gait_for).
//
// Doc: oot-remake docs/EPONA.md. Switch: LICHII_ORM_EPONA=0 (no shared step,
// no race hold), LICHII_ORM_HORSE=0 (riders not drawn on a horse; on by
// default, lichii/hook/coop_epona.h). Test: tests/host/epona_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/game_memory.hpp"

namespace coop::host::epona {

// flag_hash("Step_Get_Epona") (GameDataList: Enum, SaveFileIndex 0).
inline constexpr std::uint32_t kStep = 0x92431c55u;
// Its values' hashes (flag_hash of the names).
inline constexpr std::uint32_t kReadyForGambleRace = 0x4a78580eu;
inline constexpr std::uint32_t kGambleRacePlaying = 0x085d18f0u;
inline constexpr std::uint32_t kAfterWonGambleRace = 0xdac3768fu;
inline constexpr std::uint32_t kComplete = 0x4c0a63f4u;

// The step's values in the game's order (GameDataList RawValues, read
// 2026-11-11): NotReady ... AfterWonGambleRace, Complete.
const std::vector<std::pair<std::string_view, std::uint32_t>>& step_values();
// The name of one of them; nothing for any other hash.
std::optional<std::string_view> value_name(std::uint32_t value);

// The shared step: kStep opted in as a story step whose only value is
// Complete. Any other value reads as unknown (GameFlagStore::read), so it is
// never sent; Complete from elsewhere is written over any other value.
EnumFlags shared_step();
bool is_step(FlagKey key);

// What adding the step to a session's shared flags did.
enum class Added : std::uint8_t {
  added,         // the key and the rule are in
  no_sharing,    // the session shares no flags at all: nothing to add to
  already,       // another list opts Step_Get_Epona in already: its rule stays
};
const char* to_string(Added added);
// Adds kStep and its rule to `shared` (sorted, each once). A list that opts
// the step in with other values keeps its rule (a story list line with the
// whole race would share the race too; docs/EPONA.md says why not).
Added add_shared_step(std::optional<SharedFlags>& shared);

// True while our own race with Ingo or the escape after it runs: the step is
// GambleRacePlaying or AfterWonGambleRace. A Complete from elsewhere waits
// then (the hook's hold: FlagStore::holds).
bool race_running(std::optional<std::uint32_t> value);
// Step_Get_Epona as our game holds it (GameFlags::get_enum); nothing when the
// manager is not up.
std::optional<std::uint32_t> read_step(GameMemory& memory);

// True for an actor each game keeps to itself (never shared as an object,
// never a world break): every horse (Animal_Epona_*, Animal_Horse*), adult
// Ingo (Npc_Hylia_Ingo_00*_Adult, who runs the race), and the Gerudo
// horseback archery: its host Npc_Gerudo_008, its pots and targets
// (Obj_Gerudo_HorsebackArchery*). The prizes are items (game_items), shared
// by their own rules.
bool own_per_game(std::string_view actor);

// ---- The horse under a rider ------------------------------------------------

// True for one of the game's horses a player can be on: Animal_Epona_* and
// Animal_Horse* (the ranch's, Ingo's, the archery's...). Our own puppet horses
// (CoopPuppet_Horse*) are not.
bool is_horse(std::string_view actor);

// The puppet horses tools/puppet_pack builds (HORSES there), one per look,
// and the game's plain event horse.
inline constexpr std::string_view kEponaLook = "CoopPuppet_Horse";        // Animal_Epona_Adult's model
inline constexpr std::string_view kRanchLook = "CoopPuppet_Horse_Ranch";  // Animal_Horse's (Lon Lon)
inline constexpr std::string_view kIngoLook = "CoopPuppet_Horse_Ingo";    // Animal_Horse_For_Ingo's
inline constexpr std::string_view kPlainLook = "Animal_Horse_For_Event_Base";

// The puppet horse for the horse a rider is on (PLAYER_EQUIP's mount): Epona's
// look only for Epona (request: "Epona solo debe verse como Epona"), Ingo's for
// Ingo's horse, the ranch look for any other of the game's horses, and the
// plain event horse when the rider's game did not say (an older game, or it
// found no horse under him): never Epona's.
std::string_view horse_look(std::optional<std::string_view> mount);

// What a remote player's AS command (PLAYER_SAMPLE field 7) says.
enum class Ride : std::uint8_t {
  none,      // on foot, or no command known ("DismountHorse" ends on foot)
  mounting,  // getting on: "HorseRideOn..." (1 to 2 s by the horse's side)
  riding,    // in the saddle: every other horse command (scene.hpp activity_of)
};
Ride ride_of(std::optional<std::string_view> anim);
const char* to_string(Ride ride);

// The horse's gait under a rider, as AS commands of Animal_Horse.root.asb
// (the event horse's AS; read 2026-11-11): Wait, Move_Gear_1 (walk), _2
// (trot), _3 (canter), _Top (gallop).
enum class Gait : std::uint8_t { wait, walk, trot, canter, gallop };
std::string_view gait_command(Gait gait);

// The speeds (units a second, along the ground) where a gait starts. A gait
// changes only when the speed is `margin` past the line, so a speed on a
// line does not flip between two.
struct GaitSpeeds {
  float walk = 0.5f;
  float trot = 3.5f;
  float canter = 7.0f;
  float gallop = 11.0f;
  float margin = 0.4f;
};
Gait gait_for(float speed, Gait now, const GaitSpeeds& speeds = {});

}  // namespace coop::host::epona
