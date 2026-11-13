#pragma once
// ORM water levels that change for everyone (oot-remake docs/AGUA.md, PLAN_100 F6/E13):
// the Water Temple's three levels, the Deku Tree basement, the Bottom of the Well's
// drain, Jabu-Jabu's room 14, two Water Temple rooms and the Forest Temple well.
//
// What the romfs says (AGUA.md, section 1): every one of these is a saved GameData
// flag (an Int for the Water Temple, a Bool for the rest) bound to a placement group
// (the Banc's PlgGdTable) or read by a GroundControlActor_ResidentMoveTo node; the
// mover polls the flag (HoldGameData...) and moves the water to the transform of its
// value, so a value written from outside should move a loaded room's water at once.
// The level is ONE step value: the Water Temple's level events (Dm_WaterTemple_0020,
// 0021, 0022) write the Int (0 low, 1 middle, 2 high, the default) and three one-hot
// Bools (WaterTemple_WaterLevelChange01..03) that gate the side water planes'
// presence. Shared flag by flag, two players changing the level at once could leave
// the Int from one and the Bools from the other; so the Int is shared and the Bools
// are derived from it in each game (WaterGuard::derive), never sent.
//
// The no-void-out rule: the game never moves the water under a Link that swims,
// falls or climbs (each level event first warps Link onto the triforce tile). A level
// from another player waits while our Link, in that water's scene, is in an event,
// swims, dives, falls, jumps, climbs or moves up and down, and is written as soon as
// he stands (at most max_wait later: then written anyway, with a log line). Out of
// that scene, or loading, it is written at once: the room loads with it. A late
// joiner gets the level in the host's snapshot like every shared flag; the same
// rule holds it while the joiner swims.
//
// Doc: oot-remake docs/AGUA.md.
// Switch: LICHII_ORM_WATER=1 (off by default: not run in a game yet);
//   LICHII_ORM_WATER_MAX_WAIT_S (default 20; 0 waits for ever);
//   LICHII_ORM_WATER_JABU=1 also shares Jabu-Jabu's room 14 water (daurosk left it
//   out: its step switches set it both ways).
// Test: tests/host/water_test.cpp. No emulator code, no game data but names and hashes.

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// One body of water whose level the group shares.
struct WaterBody {
  std::string_view id;     // "water-temple"
  std::string_view scene;  // the Banc it lives in (place_of: either era)
  std::string_view what;   // for the log
  std::string_view flag;   // the level flag's name
  std::uint32_t hash = 0;  // flag_hash(flag)
  bool is_int = false;     // an Int level (else a Bool: 0 or 1)
  std::int32_t lowest = 0;   // Int only: the values the game's events write
  std::int32_t highest = 1;
  // Int only: the Bool each value raises (one-hot; value lowest + i -> one_hot[i]),
  // derived here from the level, never shared. Empty names: none.
  std::array<std::string_view, 3> one_hot{};
  std::array<std::uint32_t, 3> one_hot_hash{};
  bool opt_in = false;  // shared only when asked (Jabu-Jabu)
};

// Every body, in the order of AGUA.md.
std::span<const WaterBody> water_bodies();
// The body whose level flag is `hash`, or null.
const WaterBody* water_body_of(std::uint32_t hash);

// Flags taken off the shared list when water is on: the Water Temple's one-hot
// Bools (derived from the level here) and WaterTemple_SkipWaterChange (one game's
// choice to skip its own level cutscene: set 1, then 0 three frames later).
struct WaterFlag {
  std::string_view name;
  std::uint32_t hash = 0;
};
std::span<const WaterFlag> water_unshared();

struct WaterMergeReport {
  std::size_t added = 0;    // level flags that were not shared before
  std::size_t removed = 0;  // water_unshared() flags taken off
};
// Adds the level flags (Jabu-Jabu's too when `jabu`) and takes water_unshared() off.
// Nothing (no flag sharing) stays nothing. Keeps the list's Enum flags.
WaterMergeReport merge_water(std::optional<SharedFlags>& shared, bool jabu);

// True for an AS command (the player's main slot) of a Link in the water or off his
// feet: Swim*, WaterDive*, Dive*, Fall*, Jump*, Climb*, Ladder*, Hang*, Float*.
bool water_unsafe_animation(std::string_view as_command);

// Our game this frame.
struct WaterView {
  bool link = false;                       // our Link is read (false: loading, title)
  std::optional<std::string> scene;        // the Banc name
  std::optional<protocol::Vec3> position;  // metres; [1] is the height
  std::optional<std::string> animation;    // the main slot's AS command
  bool event_running = false;
  std::uint64_t now_us = 0;  // a steady clock
  // The heights (metres) of the loaded water planes of our scene: the actors whose
  // name has "WaterPlane" in the objects walk. Nothing: the walk was not read
  // (LICHII_COOP_OBJECTS off, another scene); empty: read, none loaded.
  std::optional<std::vector<float>> planes;
};

// Whether a body's mover re-reads its level while the room is loaded, as this
// game has seen it (docs/RIESGOS_RESUELTOS.md, "Mundo e historia").
enum class WaterLive : std::uint8_t {
  unknown,  // not seen yet, or no water plane read
  live,     // a water plane moved after another player's level was written
  at_load,  // no plane moved: the level shows when the scene loads again
};
const char* to_string(WaterLive live);

class WaterGuard {
 public:
  // Steady: the height changed no faster than kMaxClimbMps for kSteadyUs (as
  // CoopTeleport::Grounded: walking, also on a slope or stairs).
  static constexpr std::uint64_t kSteadyUs = 300'000;
  static constexpr double kMaxClimbMps = 2.5;
  static constexpr std::uint64_t kDefaultMaxWaitUs = 20'000'000;
  // A wait not asked about for this long is over (the watcher dropped the value:
  // our game changed the flag itself).
  static constexpr std::uint64_t kForgetUs = 1'000'000;
  // An opt-in water (Jabu-Jabu, set both ways by step switches) whose value comes
  // from the others kBounceFlips times within kBounceUs bounces between games:
  // from then on, until our Link leaves that scene, the others' values wait (our
  // own still go out), so one game settles it (docs/RIESGOS_RESUELTOS.md).
  static constexpr int kBounceFlips = 3;
  static constexpr std::uint64_t kBounceUs = 10'000'000;
  // Live or at load: after another player's level is written in our scene, a water
  // plane that moves more than kPlaneMoveM within kLiveCheckUs says live; none
  // that moves (planes read) says at load; no plane read says unknown.
  static constexpr std::uint64_t kLiveCheckUs = 10'000'000;
  static constexpr float kPlaneMoveM = 0.05f;

  // max_wait_us 0: wait for as long as our Link is in the water or in the air.
  explicit WaterGuard(std::uint64_t max_wait_us = kDefaultMaxWaitUs, bool jabu = false)
      : max_wait_us_(max_wait_us), jabu_(jabu) {}

  // Once a frame, before the session's frame.
  void observe(const WaterView& view);

  // The HoldFlagStore rule (flags.hpp): true while another player's value of a
  // level flag must not be written into our game yet. Notes when a wait starts and
  // ends. Any other flag: false.
  bool holds(FlagKey key);

  // Why a level may not be written now, in our Link's scene ("our Link plays
  // 'SwimWait'"), or nothing.
  std::optional<std::string> unsafe_reason() const;
  bool steady() const;

  // A value another game's change wrote into ours (FlagWatcher::take_received):
  // notes it when it is a level flag. `join`: from the snapshot of a join.
  void received(const protocol::FlagEntry& before, const protocol::FlagEntry& after, std::uint8_t from,
                bool join);

  // Writes the one-hot Bools each Int level asks for, when they differ (not while
  // an event runs here: our own level event writes them itself, after the Int).
  // Returns how many it wrote.
  std::size_t derive(FlagStore& store);

  // Log lines since the last call ("water: ..."; the hook adds "coop: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  // What this game has seen of the body of level flag `hash` (unknown when none).
  WaterLive live(std::uint32_t hash) const;
  // Bodies whose level came while their scene could not show it (out of the scene,
  // loading, or a mover that reads at load): each gets an "applied on load" line
  // when our Link is next read in that scene after a load.
  std::size_t waiting_for_load() const { return on_load_.size(); }
  // An opt-in water that bounced between games in this visit of its scene.
  bool bounced(std::uint32_t hash) const { return bounced_.count(hash) != 0; }

  std::uint64_t max_wait_us() const { return max_wait_us_; }
  bool jabu() const { return jabu_; }

 private:
  struct Wait {
    std::uint64_t since_us = 0;
    std::uint64_t last_us = 0;  // last asked
    bool forced = false;        // max_wait passed: written anyway
  };
  struct LiveCheck {
    std::uint64_t since_us = 0;
    std::int32_t level = 0;                  // the value written (Bool: 0 or 1)
    std::optional<std::vector<float>> base;  // sorted plane heights before it took
    bool planes_seen = false;                // any plane read while watching
  };
  bool in_scene(const WaterBody& body) const;
  void note_swim();
  void watch_live();
  void release(std::uint32_t hash, const WaterBody& body, std::string_view why);
  // A level that waited is written now, in its scene: the store's write does not come back
  // through received() (FlagWatcher records only the writes made as a message arrives), so the
  // live check is armed here, its level read in derive() (docs/SONDAS.md, 2026-11-12).
  void arm_after_wait(std::uint32_t hash, const WaterBody& body);
  static constexpr std::int32_t kLevelUnread = INT32_MIN;

  std::uint64_t max_wait_us_;
  bool jabu_;
  WaterView view_;
  std::optional<protocol::Vec3> prev_;
  std::uint64_t prev_us_ = 0;
  std::uint64_t steady_since_us_ = 0;
  std::string prev_scene_;
  // Probe (RIESGOS_RESUELTOS.md): the first place our Link swims in a water scene, logged once a
  // visit, so a scenario (meca-agua-nado) can 'place' a Link in the water.
  bool swim_noted_ = false;
  std::map<std::uint32_t, Wait> waits_;           // by level flag hash
  std::map<std::uint32_t, std::int32_t> bad_level_;  // Int levels out of range, noted once
  std::map<std::uint32_t, bool> derive_failed_;      // noted once per body
  std::map<std::uint32_t, LiveCheck> checks_;        // by level flag hash
  std::map<std::uint32_t, WaterLive> live_;
  std::map<std::uint32_t, std::int32_t> on_load_;    // level flag hash -> the level it loads with
  std::string last_place_;  // place_of our Link's last scene
  std::map<std::uint32_t, std::vector<std::uint64_t>> flips_;  // opt-in bodies: when values came
  std::map<std::uint32_t, bool> bounced_;                      // ... held for the rest of the visit
  bool was_loading_ = false;  // a load (no scene) since our Link was last read
  std::vector<std::string> notes_;
};

}  // namespace coop::host
