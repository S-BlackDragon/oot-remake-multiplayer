#pragma once

// ORM, the group's heart capacity (oot-remake docs/JEFES.md, "Corazones"; daurosk issue 88,
// docs/items.md "Heart containers"). oot-64 (ZMP): a heart container "comparte capacidad":
// whoever takes it, every Link gets the heart.
//
// The game draws its hearts from our Link's own Life component (its max at
// [Life + 0x1c18] + 8), not from the PlayerStatus.MaxLife flag: the flag is a copy the game
// writes back from the component within a quarter second. So the flag (shared with the max
// rule, game_items.cpp) is only the carrier. This keeps the group's capacity, one way up:
//
// - It is the highest of: our Link's max (our own container, or the pickup that turns the
//   fourth heart piece into a heart), the MaxLife flag as our game holds it, and every
//   MaxLife value another player's change wrote into our game (FlagWatcher::Received,
//   with who sent it). A value read once is kept, even when the game writes its lower copy
//   back over the flag a moment later (the race that lost a container in issue 88).
// - Our Link's max is raised to it, never lowered. Current life is left as it is; only a
//   life above the max (never from a raise) is brought down to it.
// - A save load (ours or the host's) or a new session learns it again: nothing is lowered,
//   but a capacity from before the load no longer raises anyone.
//
// Plain decisions over fake-able memory: no emulator code (coop_hook.cpp feeds and applies it).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/game_memory.hpp"

namespace coop::host {

inline constexpr std::uint32_t kMaxLifeFlag = 0xfbe01da1u;  // PlayerStatus.MaxLife
inline constexpr std::int32_t kMostHeartQuarters = 20 * 4;   // the game's 20 hearts

// The two s32 words our hearts are drawn from (read_vitals' layout): the
// current life and the max, quarter hearts.
struct LifeWords {
  std::uint64_t life = 0;
  std::uint64_t max = 0;
};
std::optional<LifeWords> find_life_words(GameMemory& memory, std::uint64_t player_actor);

struct HeartsInput {
  std::optional<std::int32_t> link_max;   // our Link's (read_vitals), quarters
  std::optional<std::int32_t> link_life;  // our Link's
  std::optional<std::int32_t> flag;       // PlayerStatus.MaxLife in our game
  struct Got {
    std::uint8_t from = 0;  // the player whose change it was (0: not known)
    std::int32_t value = 0;
  };
  std::vector<Got> received;  // MaxLife written by the others' changes this frame
  bool relearn = false;       // a save load or a new session: forget the old capacity first
};

struct HeartsAction {
  std::optional<std::int32_t> max;   // write into our Link's max
  std::optional<std::int32_t> life;  // write into our Link's life (only to bring it down to the max)
  std::vector<std::string> log;      // "hearts: ..." (the hook adds "coop: ")
};

class SharedHearts {
 public:
  HeartsAction update(const HeartsInput& input);
  std::optional<std::int32_t> capacity() const { return capacity_; }
  void reset() { capacity_.reset(); }

 private:
  std::optional<std::int32_t> capacity_;
};

// The MaxLife values among a frame's received flag writes (FrameReport::received).
std::vector<HeartsInput::Got> max_life_received(const std::vector<FlagWatcher::Received>& received);

// Writes the action into our Link's words. False when a write failed (nothing
// to write is true).
bool apply_hearts(WritableGameMemory& memory, const LifeWords& words, const HeartsAction& action);

}  // namespace coop::host
