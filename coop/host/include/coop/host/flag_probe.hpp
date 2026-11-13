#pragma once
// ORM probe (docs/RIESGOS_RESUELTOS.md "Mundo e historia", docs/SOSTENIDOS.md "Qué falta ver en
// partida" 3 and 4): flags whose meaning only a run shows. Each change of a probed flag is logged
// with where it came from (our game, or another player's change written here), and a flag that
// goes back and forth between games is named once as bouncing (a held: candidate). Defaults:
// WaterTemple_Room14_SwitchPushed, ..._SwitchPushedByBlock, ..._SwitchPushedHeavyBoots (shared by
// daurosk's list as plain flags: do they bounce?) and InsidejaubjabuBelly_gimic_switchOn_Stone
// (left out by daurosk: what does Jabu's room 17 stone write, and when?).
// Switch: LICHII_ORM_FLAG_PROBE=1, extra names in LICHII_ORM_FLAG_PROBE_NAMES (comma separated).
// Log: "coop: flag probe: <name> 0 -> 1 in our game" / "... from P2" / "... bounces between games
// (3 changes in 10.0 s, ours and the others'): a held: candidate (docs/SOSTENIDOS.md)".
// Test: tests/host/flag_probe_test.cpp.

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/flags.hpp"

namespace coop::host {

struct ProbedFlag {
  std::string name;
  FlagKey key;
};

class FlagProbe {
 public:
  static constexpr std::uint64_t kBounceWindowUs = 10'000'000;
  static constexpr std::size_t kBounceChanges = 3;

  // The defaults above, plus `extra` names (comma separated; blanks skipped).
  static std::vector<ProbedFlag> flags_with(std::string_view extra);

  explicit FlagProbe(std::vector<ProbedFlag> flags);

  // Once a frame, after the session's frame: `received` its FrameReport::received (another
  // game's change written into ours), then our game's values (a change not received is ours).
  void frame(FlagStore& store, const std::vector<FlagWatcher::Received>& received, std::uint64_t now_us);

  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }
  const std::vector<ProbedFlag>& flags() const { return flags_; }

 private:
  struct Change {
    std::uint64_t at_us = 0;
    std::uint8_t from = 0;  // 0: our game
  };
  struct State {
    bool known = false;
    std::uint32_t value = 0;
    std::deque<Change> changes;
    bool bounce_told = false;
  };
  void changed(const ProbedFlag& f, State& s, std::uint32_t to, std::uint8_t from, std::uint64_t now_us);

  std::vector<ProbedFlag> flags_;
  std::map<FlagKey, State> states_;
  std::vector<std::string> notes_;
};

}  // namespace coop::host
