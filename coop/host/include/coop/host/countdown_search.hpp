#pragma once

// ORM, the escape's countdown found in a running game (oot-remake docs/FINAL.md, "Dónde vive la
// cuenta atrás"; docs/RIESGOS_RESUELTOS.md, "Mundo e historia"). The escape from the collapsing
// castle runs on the minigame timer (Defeat_Ganon's flow: EventTriggerMiniGameTimerStart and
// EventTriggerShowTimerUI, the timer GameUI draws); no GameData flag holds its live value
// (MiniGame_TimerValue and MiniGame_TimerMinute/Second/MilliSecond are a minigame's result,
// written at its end). So the word is looked for while the countdown runs: every aligned word of
// the GameUI module (main + 0x4e7cfa8, the object that shows the timer) and of the objects it
// points to that reads as seconds left (a float, whole seconds or milliseconds, up to the
// countdown's length) is followed once a second; the one that drops as the wall clock does
// (never up, about 1 s a second, over a few samples) is the countdown. Found, it is said in the
// format LICHII_ORM_ESCAPE_CLOCK_WORD takes (CountdownWord, final_rules.hpp in orm/final2):
// "main+0x4e7cfa8,0x1a0:f32" (the object's pointer in main, the word's offset) or
// "main+0x4e7cfa8,0x88,0x1c:ms" (one more pointer step).
//
// Not found (no word drops right, or an event or a pause stopped it each time): the group's
// clock goes on without the game's timer (nothing is set in our game; each game's own timer
// runs out on its own, group_exit still takes everyone out), said once.
//
// Game-free: a GameMemory in, log lines and the word out. The hook glue is
// lichii/hook/coop_escape_timer.{h,cpp}.
// Switch: LICHII_ORM_ESCAPE_CLOCK_SEARCH (on with LICHII_ORM_ESCAPE_CLOCK when no _WORD is given;
//   0 turns it off; a root list adds places to look: see parse_countdown_roots).
// Test: tests/host/countdown_search_test.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/game_memory.hpp"

namespace coop::host {

// One place to look: the object whose pointer is at main + `pointer`, `bytes` of it, and
// `child_bytes` of each object it points to (0: only the object itself).
struct CountdownRoot {
  std::string name;  // for the log ("GameUI")
  std::uint64_t pointer = 0;
  std::uint32_t bytes = 0x2000;
  std::uint32_t child_bytes = 0x400;
  bool operator==(const CountdownRoot&) const = default;
};

// The GameUI module (addresses.hpp kGameUiModulePtr): the object that draws the timer.
std::vector<CountdownRoot> default_countdown_roots();

// "main+0x<hex>[:<bytes>[:<child bytes>]][,...]" (bytes decimal or 0x hex, 4 to 0x10000; child
// bytes 0 to 0x4000). Nothing (and `error`) on a bad list.
std::optional<std::vector<CountdownRoot>> parse_countdown_roots(std::string_view text,
                                                                std::string* error = nullptr);

// u32_mmss: two whole words, minutes then seconds (0-59), as the game names its minigame timer
// (MiniGame_TimerMinute/Second/MilliSecond). The probe of 2026-11-12 (docs/SONDAS.md row 1) found a
// u32 that read 1 s and, a minute later, 4 s: the seconds of such a pair, not the countdown.
enum class CountdownUnit : std::uint8_t { f32_seconds, u32_seconds, u32_ms, u32_mmss };
const char* countdown_unit_text(CountdownUnit unit);  // "f32", "u32", "ms", "mmss"

struct CountdownFound {
  std::uint64_t base = 0;              // from main: the root's pointer
  std::vector<std::uint64_t> offsets;  // pointer steps, as CountdownWord
  CountdownUnit unit = CountdownUnit::f32_seconds;
  std::uint64_t address = 0;  // where it was when found
  double seconds = 0.0;       // as last read
  // LICHII_ORM_ESCAPE_CLOCK_WORD's text: "main+0x4e7cfa8,0x1a0:f32".
  std::string text() const;
};

struct CountdownSearchSettings {
  std::vector<CountdownRoot> roots = default_countdown_roots();
  std::uint32_t length_s = 180;  // the most the countdown shows (LICHII_ORM_ESCAPE_CLOCK_LENGTH_S)
  std::uint32_t slack_s = 5;     // a word may read this much more
  std::uint64_t every_us = 1'000'000;  // one sample a second
  std::uint32_t samples = 6;           // that agree, the first one included (5 s)
  std::uint32_t max_children = 256;    // objects followed from each root
  std::size_t max_words = 200'000;     // candidates kept at most
  int tries = 3;  // every candidate gone: looked for again this many times in all
};

class CountdownSearch {
 public:
  enum class State : std::uint8_t { idle, searching, found, failed };

  explicit CountdownSearch(CountdownSearchSettings settings = {});

  // Once a frame while the countdown should run (our Link up in the escape's scene, no event).
  // Returns log lines ("escape clock: ..."; the hook adds "coop: ").
  std::vector<std::string> step(GameMemory& memory, std::uint64_t now_us);
  // An event or a pause stopped the clock: the samples so far mean nothing; the next step starts
  // them again (not counted as a try).
  void interrupt();
  // Our Link left the scene before it was found: the next step starts afresh (tries too). A word
  // found stays found.
  void reset();

  State state() const { return state_; }
  const std::optional<CountdownFound>& found() const { return found_; }
  std::size_t candidates() const { return words_.size(); }
  int tries_used() const { return tries_used_; }

 private:
  struct Region {
    std::size_t root = 0;
    std::optional<std::uint64_t> child_offset;  // in the root object; nothing: the root itself
    std::uint64_t address = 0;
    std::uint32_t bytes = 0;
  };
  struct Word {
    std::size_t region = 0;
    std::uint32_t offset = 0;
    CountdownUnit unit = CountdownUnit::f32_seconds;
    double first = 0.0;
    double last = 0.0;
  };

  bool snapshot(GameMemory& memory, std::uint64_t now_us);
  std::optional<double> seconds_of(std::uint32_t raw, CountdownUnit unit) const;
  // The word (or the pair, for u32_mmss) at `offset` of a block, as seconds; nothing when it does not read so.
  std::optional<double> seconds_at(const std::vector<std::uint8_t>& block, std::uint32_t offset,
                                   CountdownUnit unit) const;
  std::vector<std::string> sample(GameMemory& memory, std::uint64_t now_us);
  std::vector<std::string> give_up_try(std::string why);

  CountdownSearchSettings settings_;
  State state_ = State::idle;
  std::vector<Region> regions_;
  std::vector<Word> words_;
  std::uint64_t first_us_ = 0;
  std::uint64_t last_us_ = 0;
  std::uint32_t taken_ = 0;  // samples taken this try
  int tries_used_ = 0;
  bool restart_ = false;
  std::optional<CountdownFound> found_;
};

}  // namespace coop::host
