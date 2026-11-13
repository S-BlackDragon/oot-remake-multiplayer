#pragma once

// ORM "Continue with the group's game" (oot-remake docs/CONTINUAR.md, E11 of
// docs/PLAN_100.md). After a game over that the downed state machine lets run
// (the group defeat, or a player alone), the game's own Continue reloads the
// save and puts Link back at the area's start. Each game would then play from
// its own save; the group must go on from one state instead.
//
// So, from the game over until the reload's load window is over, the flag
// watcher takes that window as a save load (FlagWatcher::set_continue_load,
// docs/flags.md "Saves"), in one of two modes:
// - keep (the default; the N64 game over, as in ZMP D-036/D-058): the group
//   goes on with what it had when it fell. Every game, host or player, writes
//   back what its reload moved, back or forward, to the group's values from
//   just before (the snapshot), at once and at the window's end; nothing is
//   sent. Per player values kept too (the wallet) are given by the hook.
// - save: the group follows the host's reload: every shared flag it moved,
//   against the table it had just before (the host's flag snapshot), goes
//   out as FLAG_RESET, and every game writes it exactly; a player's own
//   save's values go back to the group's and nothing is sent.
// A reload that moved no shared flag changes nothing in either mode.
//
// When it ends (REVISION_NUEVAS, continuar): not on Link's life. The Continue
// may fill the hearts before the reload's load window opens, so life back
// with no window yet keeps it on: it waits while the game over event runs,
// then up to kNoWindowUs for a load window, and ends only once a window that
// opened after the game over started is over, with Link up and the game over
// event done. Before that window opens, the watcher sends none of our own
// changes (FlagWatcher::set_continue_load): the window decides them.
//
// Portable: the hook gives the downed state, our flag role and the watcher's
// counters each frame, and passes mode to Session::set_continue_load.
// Switch: LICHII_ORM_CONTINUE = keep (default) | save | 0 (off), with
// LICHII_COOP_DOWNED=1.
// Test: tests/host/continue_load_test.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/flag_watcher.hpp"

namespace coop::host {

using ContinueMode = FlagWatcher::ContinueMode;

// LICHII_ORM_CONTINUE's value: unset or empty or "keep" keep, "save" save,
// "0" or "off" off; anything else nothing (the hook then keeps the default).
std::optional<ContinueMode> parse_continue_mode(std::string_view text);
const char* to_string(ContinueMode mode);

// The game's game over event ("DmF_SY_GameOver", "Delayed_GameOver"; the
// same test as CoopBoss::IsGameOverEvent): its name has "GameOver" in it.
// ContinueLoad also takes the first named event that runs during the game
// over as that event, whatever its name (logged: which of the two it was).
bool is_game_over_event(std::string_view event_name);

// Per player values a kept Continue keeps too (not shared, so not in the
// group's flags): the wallet (PlayerStatus.CurrentRupee) and the ammo
// (PouchData.<item>.StockNum of sticks, nuts, seeds, bombs, bombchus and
// arrows). Bottle contents and magic are not in it (CONTINUAR.md).
struct KeepExtra {
  std::string_view name;
  std::uint32_t hash;  // flag_hash(name), checked by the test
};
std::span<const KeepExtra> continue_keep_extra();
std::vector<FlagKey> continue_keep_extra_keys();

struct ContinueInput {
  std::uint64_t now_us = 0;                // any steady clock
  DownedState state = DownedState::alive;  // Downed::state() this frame
  // An event of ours runs, and its name ("" when not known). A game over
  // event, or a running event with no name, keeps the Continue waiting.
  bool event_running = false;
  std::string_view event_name;
  bool group_defeat = false;  // the game over is the group's (GROUP_DEFEAT), for the log
  bool hosting = false;       // our flag watcher is the host's (FlagWatcher::role)
  bool settling = false;      // FlagWatcher::settling(): a load window is open
  // FlagStats as of this frame: what the window did, for the log.
  std::uint64_t host_resets = 0;
  std::uint64_t load_reverts = 0;
  std::uint64_t reset_applied = 0;
  std::uint64_t continue_kept = 0;
  std::uint64_t continue_counts = 0;  // FlagStats::continue_counts (host: deltas on the group's value)
};

struct ContinueOutput {
  ContinueMode mode = ContinueMode::off;  // Session::set_continue_load
  std::vector<std::string> log;           // one line per step: "coop: continue: ..."
};

class ContinueLoad {
 public:
  enum class Phase : std::uint8_t {
    idle,       // nothing: the watcher's own rules
    game_over,  // our game over runs: the next load window is a save load
    reloading,  // Link is back: the reload's window is still settling
  };

  // Life back, the game over event over and no load window yet: this long
  // for one before the Continue gives up (a fairy's revive, no reload).
  static constexpr std::uint64_t kNoWindowUs = 10'000'000;

  explicit ContinueLoad(ContinueMode mode = ContinueMode::keep) : mode_(mode) {}

  ContinueOutput tick(const ContinueInput& in);

  Phase phase() const { return phase_; }
  ContinueMode mode() const { return mode_; }

 private:
  ContinueMode mode_;
  Phase phase_ = Phase::idle;
  bool group_ = false;  // the game over was the group's
  std::uint64_t resets_ = 0;   // the counters when the game over started
  std::uint64_t reverts_ = 0;
  std::uint64_t applied_ = 0;
  std::uint64_t kept_ = 0;
  std::uint64_t counts_ = 0;  // continue_counts at the game over
  // The reload's window (REVISION_NUEVAS): one that opened after the game
  // over started. A window already open then (a death on a door) is not it.
  bool stale_window_ = false;
  bool window_seen_ = false;
  std::optional<std::uint64_t> waiting_since_;  // Link up, no event, no window yet
  // The first named event that ran during this game over: the game over's, whatever its name.
  std::string game_over_event_;
  bool summary_ = false;  // the window is over: its summary goes out on the next tick
  void finish(const ContinueInput& in, ContinueOutput& out);
  void report(const ContinueInput& in, ContinueOutput& out) const;
};

const char* to_string(ContinueLoad::Phase phase);

}  // namespace coop::host
