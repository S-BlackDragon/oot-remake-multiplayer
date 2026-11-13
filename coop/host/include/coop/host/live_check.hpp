#pragma once

// ORM: does a world flag move what it should, live, in our game? (oot-remake
// docs/RIESGOS_RESUELTOS.md, "Mundo e historia"; first user: the Song of Time blocks of
// docs/CANCIONES.md, "Por ver" 1 and 2.) A flag that another player's game set is written
// into ours, but whether the actor it gates goes (or comes) at once or only when its room
// loads again is up to the game, and which key a block really answers to is not known
// before a run. This watches both, from what every game already has, and says in one log
// line which way it went:
//
// - A watched key changes while our Link is in its place: the watched actors of our scene
//   are counted then and for kAnswerUs. A count that changes is "live"; one that does not
//   is "not live: it changes when <place> loads" (the flag is written all the same: the
//   room takes it on its next load). No actor list (objects not shared): "unknown".
// - A watched key changes while we are elsewhere: "<place> loads with it"; the next time we
//   are in that place the count is noted once ("loaded: ...").
// - The watched actors' count changes in our scene, with no scene change and no watched key
//   changing within kAnswerUs either side, after our Link played the Ocarina (cause): the
//   list misses that actor's key ("unexplained: ..."). That is the fallback for a key the
//   romfs reading got wrong: the run says so, the list line is fixed from it.
//
// Nothing is written: it reads our flags (any FlagStore) and the objects walk's names.
// Doc: oot-remake docs/RIESGOS_RESUELTOS.md, docs/CANCIONES.md.
// Switch: LICHII_ORM_LIVE_CHECK=0 turns it off (on by default: it only reads and logs).
// Test: tests/host/live_check_test.cpp.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flag_defer.hpp"
#include "coop/host/flags.hpp"

namespace coop::host {

// One thing a flag should move.
struct LiveWatch {
  std::string id;              // for the log: "song-of-time ForestTemple"
  std::string place;           // place_of(scene) it lives in: "ForestTemple", "SacredForestMeadow"
  std::string actor;           // a part of the actors' names: "SongOfTimeBlock"
  std::vector<FlagKey> keys;   // the flags that should move it
  std::string cause;           // our Link's AS command prefix that may move it ("Ocarina"); "" none
};

struct LiveView {
  std::optional<std::string> scene;              // nothing or empty: loading, the title
  std::optional<std::vector<SeenActor>> actors;  // nothing: no objects walk in this scene
  std::optional<std::string> animation;          // our Link's AS command
  std::uint64_t now_us = 0;                      // any steady clock
};

class LiveCheck {
 public:
  static constexpr std::uint64_t kAnswerUs = 4'000'000;   // a live answer comes within this
  static constexpr std::uint64_t kCauseUs = 30'000'000;   // our Ocarina this long before
  // Counts that flicker (the walk mid-load) are taken only when they held this long.
  static constexpr std::uint64_t kSteadyUs = 500'000;

  explicit LiveCheck(std::vector<LiveWatch> watches) : watches_(std::move(watches)) {
    states_.resize(watches_.size());
  }

  // A few times a second. `store` is our game's (read only).
  void tick(const LiveView& view, FlagStore& store);

  // Log lines since the last call (the hook adds "coop: live: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  const std::vector<LiveWatch>& watches() const { return watches_; }

  // The verdict so far for a watch: "live", "not live", "unknown" or "" (none yet).
  std::string_view verdict(std::size_t watch) const;

 private:
  struct State {
    std::map<FlagKey, std::uint32_t> values;  // last read, by key
    std::optional<std::size_t> count;         // watched actors in our scene (steady)
    std::optional<std::size_t> raw;           // the last count read
    std::uint64_t raw_since_us = 0;
    bool in_place = false;
    std::string scene;  // the scene the count is of
    // A key changed here: is the count answering?
    struct Pending {
      std::uint64_t since_us = 0;
      std::size_t before = 0;
      std::string key;  // for the log
    };
    std::optional<Pending> pending;
    std::uint64_t key_changed_us = 0;  // the last watched key change (any place)
    bool key_changed_once = false;
    // A count change with no key: waits kAnswerUs for a key before it is unexplained.
    struct Odd {
      std::uint64_t since_us = 0;
      std::size_t before = 0;
      std::size_t after = 0;
    };
    std::optional<Odd> odd;
    bool load_check = false;  // a key changed elsewhere (or not live): note the count when we are back
    bool fresh = false;       // we came into the place: its first steady count is yet to come
    bool no_walk_noted = false;
    std::string verdict;
  };

  void note_key(std::size_t i, State& st, const FlagKey& key, std::uint32_t before, std::uint32_t after,
                const LiveView& view);

  std::vector<LiveWatch> watches_;
  std::vector<State> states_;
  std::optional<std::uint64_t> cause_us_;  // our Link last played the cause
  std::vector<std::string> notes_;
};

// "key64:0x..." or "flag 0x...".
std::string live_key_text(const FlagKey& key);

}  // namespace coop::host
