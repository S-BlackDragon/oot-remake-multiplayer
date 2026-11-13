#pragma once

// ORM Biggoron's trade chain with several players (oot-remake docs/BOTELLAS_TRUEQUES.md,
// section 6 point 2; RIESGOS_RESUELTOS.md "Mundo e historia"). The chain's step,
// Step_SubEvent_GetSwordBigGoron, is shared both ways (shared_dungeon_minigames.txt,
// enum:), and the three trade items with a timer (the Odd Mushroom, the Eyeball Frog,
// the Eye Drops) are the receiver's own (shared_dungeon_trades.txt, unshare:). The
// risk: a game WITHOUT the item talks to the old woman, the frog's keeper or King Zora,
// and its game puts the step back (or marks GetSwordBigGoron_StepNN_GiveUp); shared
// both ways, that step back reaches the holder, who still has the item in his hand and
// can no longer hand it over.
//
// The rule: while our game holds one of the three timed items (its StockNum > 0),
// another game's step that is EARLIER in the chain than ours, or a GiveUp Bool of the
// step whose item we hold, is not written into our game. The write is refused
// (FlagStore::write returns false), so FlagWatcher keeps it and tries again each frame,
// and drops it as soon as our game changes the step itself (our hand-over moves it on,
// our own spoiled item moves it back: both newer). A host answers the asker with the
// step it holds, so the asker's game is put forward again.
//
// What passes as before (the game's own resets are legit):
// - every change our own game makes (the watcher sends it; this store only sees
//   incoming writes), including the step back when OUR item spoils;
// - a step back from another game when we hold no timed item (the holder's item
//   spoiled there: everyone follows it);
// - a step forward, and the host's save load (write_exact): a save is the group's;
// - anything when our step or the items cannot be read (today's behaviour; noted once).
//
// Game-free: a FlagStore wrapper over the game's store and a guard that keeps the
// waits for the log. The hook builds TradeStepStore over GameFlagStore each frame.
//
// Doc: oot-remake docs/BOTELLAS_TRUEQUES.md. Switch: LICHII_ORM_TRADE_GUARD=0 turns
// it off (on by default: it only ever refuses a step back under a held timed item).
// Test: tests/host/trades_test.cpp.

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

namespace coop::host::trade {

// flag_hash("Step_SubEvent_GetSwordBigGoron").
std::uint32_t step_flag();
// The step's values in the game's order (GameDataList; the same line as
// shared_dungeon_minigames.txt): {name, flag_hash(name)}.
std::span<const std::pair<std::string_view, std::uint32_t>> step_values();
// Where `value` is in step_values() (0 first), or nothing.
std::optional<std::size_t> step_rank(std::uint32_t value);
// The value's name, or "0x...." for an unknown hash.
std::string step_name(std::uint32_t value);

// One trade item with a timer: SubEvent_GetSwordBigGoron_TimerCountingStep is true
// at `step` with the item (BOTELLAS_TRUEQUES.md section 3).
struct TimedItem {
  std::string_view what;        // for the log: "the Odd Mushroom"
  std::string_view stock_name;  // PouchData.Item_OddMushroom.StockNum (Int)
  std::uint32_t stock = 0;      // its hash
  std::string_view step;        // Step04
  std::string_view give_up_name;  // GetSwordBigGoron_Step04_GiveUp (Bool)
  std::uint32_t give_up = 0;
};
std::span<const TimedItem> timed_items();

// The state kept across frames (the hook owns one).
class TradeGuard {
 public:
  // A refused value still waits this long after our timed item goes (a hand-over
  // sets the count and the step in two writes).
  static constexpr std::uint64_t kGraceUs = 2'000'000;
  // A wait not refused again for this many frames is over (the watcher dropped it).
  static constexpr std::uint32_t kIdleFrames = 120;

  explicit TradeGuard(bool on = true) : on_(on) {}

  // An incoming write: true when it must wait (see the rule above). Notes a wait
  // when it starts. `game` reads our game (the step, the items).
  bool refuses(const protocol::FlagEntry& entry, FlagStore& game);

  // Once a frame, after the session's frame: ends the waits whose value got
  // written, or that our game made moot by changing the step itself. `now_us`: a
  // steady clock (the grace after the item goes, kGraceUs).
  void frame(FlagStore& game, std::uint64_t now_us);

  // "trades: ..." lines since the last call (the hook adds "coop: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  bool on() const { return on_; }
  std::size_t refused() const { return refused_; }
  bool waiting() const { return !waits_.empty(); }

 private:
  struct Wait {
    std::uint32_t value = 0;      // the incoming value that waits
    std::uint32_t ours = 0;       // our game's value when it started
    std::string held;             // the item we held
    std::uint64_t refused = 0;    // times refused
    std::uint32_t idle_frames = 0;  // frame() calls since the last refusal
    std::uint64_t gone_us = 0;      // the timed item went at (0: still held)
  };
  // The timed item our game holds, or nothing; `known` false when a StockNum
  // could not be read.
  const TimedItem* held_item(FlagStore& game, bool& known);

  bool on_;
  std::uint64_t now_us_ = 0;  // the last frame()'s clock
  std::map<std::uint32_t, Wait> waits_;  // by flag hash
  std::size_t refused_ = 0;
  bool noted_unreadable_ = false;
  std::vector<std::string> notes_;
};

// FlagStore over the game's store: incoming writes the guard refuses fail (the
// watcher keeps them); everything else goes to `inner` as it is.
class TradeStepStore final : public FlagStore {
 public:
  TradeStepStore(FlagStore& inner, TradeGuard& guard) : inner_(inner), guard_(guard) {}
  std::optional<protocol::FlagEntry> read(FlagKey key) override { return inner_.read(key); }
  bool write(const protocol::FlagEntry& entry) override {
    if (guard_.refuses(entry, inner_)) return false;
    return inner_.write(entry);
  }
  bool is_count(FlagKey key) override { return inner_.is_count(key); }
  bool holds(FlagKey key) override { return inner_.holds(key); }
  bool is_one_way(FlagKey key) override { return inner_.is_one_way(key); }
  // The host's save load is the group's: never refused.
  bool write_exact(const protocol::FlagEntry& entry) override { return inner_.write_exact(entry); }
  bool write_own(const protocol::FlagEntry& entry) override { return inner_.write_own(entry); }

 private:
  FlagStore& inner_;
  TradeGuard& guard_;
};

}  // namespace coop::host::trade
