#pragma once

// ORM magic bean soils with several players (oot-remake docs/COLECCIONABLES.md, "Qué
// falta ver en partida" 1; RIESGOS_RESUELTOS.md "Mundo e historia"). The kit's
// shared_dungeon_collectibles.txt shares, once:, the Bool GameData struct 0x820c6b5e
// gives each soil, read as "a bean is planted here". That meaning is deduced, not
// seen. Each soil (Obj_SmallFarm_A_01/_02) has a platform placed at the same spot,
// in both eras (Obj_SmallFarm_A_03, linked to its soil by a Plant link; the Kokiri
// Forest soil without bugs has one with no link): what the game shows of a planted
// bean. So the meaning is checked in every game, every visit: once the actor walk
// has been in the soil's scene for kSettleUs, the soil's Bool and whether the
// platform's placement is loaded are compared.
//
// What it tells (one line a soil per visit, and when it changes):
// - Bool 1, platform loaded:     "planted: platform seen" (the meaning holds here);
// - Bool 1, platform not loaded: "platform not seen: Bool meaning unconfirmed";
// - Bool 0, platform loaded:     "platform seen with the Bool at 0: the platform does
//                                 not follow the Bool alone (meaning unconfirmed)";
// - Bool 0, platform not loaded: "not planted: no platform" (agrees).
// A Bool that turns 1 while we are there (another player's bean arrives, or ours)
// says whether the platform came at once ("came live") or not within kLiveUs ("not
// live: it should come on the next load"). When a soil has shown both agreeing cases
// in this run, "confirmed" is said once.
//
// The fallback is today's behaviour: the Bool is shared as it was; this only reads.
// With no actor walk (LICHII_COOP_OBJECTS off) it says so once and checks nothing.
//
// Game-free: the soils' table (from the romfs Banc, tools/romfs/collectible_list.py's
// table), the walk and the flags come in. Doc: oot-remake docs/COLECCIONABLES.md.
// Switch: LICHII_ORM_BEAN_CHECK=0 turns it off (on by default: read only).
// Test: tests/host/beans_test.cpp.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"

namespace coop::host::bean {

struct Soil {
  std::string_view what;      // "Kokiri Forest's soil with bugs"
  std::string_view place;     // place_of the scene: "KokiriForest"
  std::uint32_t flag = 0;     // the soil's Bool (struct 0x820c6b5e)
  std::uint64_t soil = 0;     // the soil's placement hash
  std::uint64_t platform = 0; // Obj_SmallFarm_A_03's placement hash at the same spot
};
// The 11 soils of the struct.
std::span<const Soil> soils();

// This frame.
struct BeanView {
  std::optional<std::string> scene;  // the Banc; none or empty: loading
  // The walk is in `scene` (CoopObjects::WalkActors has a list for it).
  bool walked = false;
  // Whether a placement hash is loaded in the last walk (CoopObjects::LoadedActor).
  std::function<bool(std::uint64_t)> loaded;
  std::uint64_t now_us = 0;  // a steady clock
};

enum class Seen : std::uint8_t {
  planted,          // Bool 1, platform
  not_seen,         // Bool 1, no platform
  platform_alone,   // Bool 0, platform
  bare,             // Bool 0, no platform
};
const char* to_string(Seen seen);

class BeanCheck {
 public:
  static constexpr std::uint64_t kSettleUs = 3'000'000;  // the walk in the scene this long first
  static constexpr std::uint64_t kLiveUs = 10'000'000;   // a new 1's platform must come within this

  explicit BeanCheck(bool on = true) : on_(on) {}
  bool on() const { return on_; }

  // True when `scene` has a soil (and the check is on): only then is the walk worth
  // copying for frame(). Leaving a soil's scene still needs one frame() call.
  bool wants(const std::optional<std::string>& scene) const;

  // Once a frame. `game` reads the soils' Bools.
  void frame(const BeanView& view, FlagStore& game);

  // "beans: ..." lines since the last call (the hook adds "coop: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  // What the last look at `flag`'s soil saw in this visit, or nothing.
  std::optional<Seen> seen(std::uint32_t flag) const;
  bool confirmed(std::uint32_t flag) const;

 private:
  struct State {
    std::optional<Seen> seen;          // this visit
    std::optional<bool> bool_was;      // this visit
    std::uint64_t turned_on_us = 0;    // the Bool turned 1 here (0: not)
    bool saw_planted = false;          // this run
    bool saw_bare = false;
    bool confirmed_noted = false;
  };
  void look(const Soil& soil, bool value, bool platform, std::uint64_t now_us);

  bool on_;
  std::string scene_;
  std::uint64_t in_scene_since_us_ = 0;  // the walk in scene_ since (0: not)
  bool noted_no_walk_ = false;
  std::map<std::uint32_t, State> states_;
  std::vector<std::string> notes_;
};

}  // namespace coop::host::bean
