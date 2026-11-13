#pragma once

// ORM: is our own game paused, when Sequence_IsPause is not to be trusted? (oot-remake
// docs/MENUS_PAUSA.md "Falta ver en partida", docs/RIESGOS_RESUELTOS.md "Mundo e historia".)
// The menus rule (coop/host/menus.hpp, branch orm/menus2) reads the Bool Sequence_IsPause
// (0x604aebe1) that the romfs graphs set while a menu pauses the game; no run has seen it set
// yet. If it is never set (another flag pauses the remake) or cannot be read, a paused
// partner would never show "(pausa)" and would keep his enemies. So a second detector looks
// at what a pause does for sure: the world stops. Our Link's place, AS command and its frame
// stay exactly the same for kFrozenUs while the command has a rate (in play its frame moves
// every frame), with no event, no scene load and no Ocarina of ours (its staff may pause).
//
// Seen in a game (oot-remake docs/SONDAS.md row 4, menu-pausa, 2026-11-12): Sequence_IsPause is set
// 0.5 s after the pause menu opens, and the frozen frame is wrong in the remake: a Link standing
// idle keeps the same place, AS command and frame for many seconds (the command's frame is not the
// play head), so "flag or frozen" called every idle a pause and then, the flag being 0 in those,
// dropped the flag. The default is now flag first: the flag decides from the start, and the frozen
// frame only takes over while the flag cannot be read. LICHII_ORM_PAUSE_DETECT=either is the old
// start (flag or frozen until the flag is seen), =flag the flag alone.
//
// Which one decides (one log line each time it changes, "pause detector: ..."):
// - flag: Sequence_IsPause was seen set once: it is trusted from then on, the frozen frame is
//   dropped (a hitch of the emulator never reads as a pause).
// - flag or frozen: at first, until the flag is seen set: either says paused.
// - frozen: the flag cannot be read for kUnreadableUs with our Link in the world, or it stayed
//   0 through kFlagMissed frozen pauses of kMissedPauseUs or more: the frozen frame alone.
//
// Plain readings in, the pause and the detector out: the menus glue (lichii/hook/coop_menus.cpp
// on orm/menus2) feeds it once a frame and gives its is_pause to menus::Input::is_pause.
// Switch: LICHII_ORM_MENUS (the menus' own); LICHII_ORM_PAUSE_DETECT=flag keeps the flag alone.
// Test: tests/host/pause_detect_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

enum class PauseSource : std::uint8_t {
  flag_or_frozen,  // until Sequence_IsPause is seen set
  flag,            // Sequence_IsPause, seen set once
  frozen,          // the frozen frame alone (the flag unreadable, or never set in a pause)
};
const char* to_string(PauseSource source);

struct PauseInput {
  std::uint64_t now_us = 0;
  std::optional<bool> is_pause;               // Sequence_IsPause; nothing: not readable
  bool link = false;                          // our Link read this frame
  std::optional<protocol::Vec3> position;
  std::optional<protocol::Animation> animation;  // our Link's AS command, frame and rate
  bool event_running = false;
  bool loading_scene = false;                 // Sequence_IsLoadingBanc
  bool ocarina = false;                       // our Link plays the Ocarina (menus::Input::ocarina)
};

struct PauseOutput {
  std::optional<bool> is_pause;  // for menus::Input::is_pause (nothing: not known this frame)
  PauseSource source = PauseSource::flag_or_frozen;
  bool frozen = false;           // the frozen-frame detector says paused
  std::vector<std::string> log;  // "pause detector: ..." (the glue adds "coop: menus: ")
};

class PauseDetect {
 public:
  static constexpr std::uint64_t kFrozenUs = 500'000;
  static constexpr std::uint64_t kUnreadableUs = 2'000'000;
  static constexpr std::uint64_t kMissedPauseUs = 1'000'000;
  static constexpr int kFlagMissed = 2;

  enum class Mode : std::uint8_t {
    flag_first,  // the default: the flag; the frozen frame only while the flag is unreadable
    either,      // LICHII_ORM_PAUSE_DETECT=either: flag or frozen until the flag is seen set
    flag_only,   // LICHII_ORM_PAUSE_DETECT=flag: the flag alone, as orm/menus2 had it
  };
  // The mode for LICHII_ORM_PAUSE_DETECT's text (unset or anything else: flag_first).
  static Mode mode_of(const std::string& text) {
    return text == "flag" ? Mode::flag_only : text == "either" ? Mode::either : Mode::flag_first;
  }
  explicit PauseDetect(Mode mode = Mode::flag_first)
      : flag_only_(mode == Mode::flag_only), flag_first_(mode == Mode::flag_first) {
    if (flag_first_) source_ = PauseSource::flag;
  }

  PauseOutput frame(const PauseInput& in);
  PauseSource source() const { return source_; }

 private:
  void switch_to(PauseSource s, const std::string& why, PauseOutput& out);

  bool flag_only_;
  bool flag_first_;
  PauseSource source_ = PauseSource::flag_or_frozen;
  bool announced_ = false;
  // The frozen frame.
  std::optional<protocol::Vec3> last_pos_;
  std::optional<protocol::Animation> last_anim_;
  std::uint64_t same_since_us_ = 0;
  bool frozen_ = false;
  std::uint64_t frozen_since_us_ = 0;
  bool flag_seen_in_freeze_ = false;  // the flag read 1 during this frozen pause
  int missed_ = 0;                    // frozen pauses the flag stayed 0 through
  // The flag.
  std::optional<std::uint64_t> unreadable_since_us_;
};

}  // namespace coop::host
