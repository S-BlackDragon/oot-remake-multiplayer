#pragma once

// ORM one-time prizes won by two players at once (oot-remake docs/MINIJUEGOS.md, section 4
// "Dos ganan a la vez"; RIESGOS_RESUELTOS.md "Mundo e historia"). A minigame's or a
// townsperson's one-time prize is shared by its "given" flag (once:, the kit's
// shared_dungeon_minigames.txt and the child and collectible lists): the second winner gets
// what the game gives after the prize. But two players who win within one round trip of the
// flag both get it, and a heart piece is a count (PouchData.Obj_Heart_Piece.StockNum, its
// changes add up): the group got two pieces for one prize.
//
// The arbitration, the same in every game with no new message: each game knows when ITS
// game set a prize flag itself (our own win: the flag went 0 -> 1 with no other game's value
// written this frame), and the watcher tells when another game's 1 arrives for a flag our
// game holds at 1 already (FlagWatcher::take_agreed: that game set it itself too). An own
// win and another player's 1 within kWindowUs of it is a double win; of the winners, the
// LOWEST player number keeps the prize. Each other winner gives back what its own win added
// that would count twice: one heart piece (its count lowered by 1 in its game; the watcher
// sends the -1 like any count change, so every game ends with one piece for the prize). A
// bag, a quiver, a scale or a bottle is the group's item once (max / any): nothing doubles,
// nothing is given back, and the line says so.
//
// Fallbacks, each with its line: the piece has not come in kPieceUs after the win (nothing
// to take back); the count went DOWN since the win (four pieces made a heart: a piece can
// no longer be taken back, both are kept); the count cannot be read or written.
//
// Game-free. Doc: oot-remake docs/MINIJUEGOS.md. Switch: LICHII_ORM_PRIZE_ARBITER=0 turns
// it off (on by default). Test: tests/host/prizes_test.cpp.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/flags.hpp"

namespace coop::host::prize {

struct Prize {
  std::string_view name;  // the "given" flag (a Bool)
  std::uint32_t hash = 0;
  bool heart_piece = false;  // the prize is a heart piece (a count: given back on a double win)
};
// Every one-time prize flag of the kit's lists (minigames, child, collectibles).
std::span<const Prize> prizes();
const Prize* prize_of(std::uint32_t hash);
// flag_hash("PouchData.Obj_Heart_Piece.StockNum").
std::uint32_t heart_piece_count();

// This frame.
struct PrizeView {
  std::uint8_t self = 0;  // our player number; 0: not in a room
  bool in_game = false;   // our Link is read (not loading, not the title)
  std::uint64_t now_us = 0;
};

class PrizeArbiter {
 public:
  static constexpr std::uint64_t kWindowUs = 10'000'000;  // an other's 1 this soon after our win
  static constexpr std::uint64_t kPieceUs = 15'000'000;   // the piece must have come by then

  explicit PrizeArbiter(bool on = true) : on_(on) {}
  bool on() const { return on_; }

  // Once a frame, after the session's frame: `received` and `agreed` are that frame's
  // (FrameReport). `game` reads our game and takes the give-back write.
  void frame(const PrizeView& view, FlagStore& game, const std::vector<FlagWatcher::Received>& received,
             const std::vector<FlagWatcher::Received>& agreed);

  // "prizes: ..." lines since the last call (the hook adds "coop: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  std::size_t given_back() const { return given_back_; }

 private:
  struct Win {
    std::uint64_t at_us = 0;
    std::int32_t count_before = 0;  // the piece count the frame before (-1: unknown)
    std::set<std::uint8_t> others;  // others who won it too
    bool decided = false;
    bool give_back = false;         // we lost: one piece to give back
    std::uint8_t keeper = 0;
  };
  std::optional<std::int32_t> read_count(FlagStore& game);
  void settle(const Prize& p, Win& w, FlagStore& game, std::uint64_t now_us);

  bool on_;
  bool primed_ = false;
  std::map<std::uint32_t, bool> was_;  // each prize flag last frame
  std::optional<std::int32_t> count_was_;
  std::map<std::uint32_t, Win> wins_;  // our own wins still in their window
  std::size_t given_back_ = 0;
  std::vector<std::string> notes_;
};

}  // namespace coop::host::prize
