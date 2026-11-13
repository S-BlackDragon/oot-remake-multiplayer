// ORM (oot-remake docs/PUERTAS.md, D-R12 one world): what another player's
// puppet does while that player goes out of our place or comes into it
// (a loading zone, a dungeon entrance, a warp), as the other Links do in
// oot-64 (D-062, D-107: they go where they went and appear at the entrance).
//
// Without this the planner knew only "same place or not" (scene.hpp):
// - leaving: the newest sample's scene flips at the start of their load,
//   while the shown pose is still 100 ms behind on their walk into the exit;
//   the puppet was then left out of the plan and stood frozen where it was
//   drawn for the director's 2 s grace before it was deleted;
// - arriving: the first samples with our scene may still carry where they
//   stood in the scene they left (the scene is readable as soon as a load
//   starts, docs/puppet.md), and the shown pose then slides from there to
//   the entrance; the puppet showed up mid-room or slid across it.
// Now: a player who leaves is drawn on its shown pose for `leave_show_ms`
// (the walk into the exit plays out), then kept out of sight where it left
// for `leave_park_ms`, then let go as before. A player who comes in is kept
// out of sight until its shown pose has moved more than `moved_m` from where
// it was while elsewhere and `arrive_settle_ms` have passed since (the slide
// is over: it stands at the entrance and walks out of it), or at most
// `arrive_timeout_ms`.
//
// Game-free; the puppet and fairy planners call it (lichii/hook/coop_puppet.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "coop/protocol/messages.hpp"

namespace coop::host {

enum class Passage : std::uint8_t {
  none,      // nothing in passing: the usual rules (scene.hpp)
  leaving,   // just left our place: still drawn on its shown pose a moment
  gone,      // left our place: kept out of sight where it left, then let go
  arriving,  // just came into our place: kept out of sight until it stands there
};

// "none", "leaving", "gone", "arriving".
const char* passage_text(Passage passage);

struct PassageSettings {
  // A player whose shown pose was planned this recently before its scene
  // flipped was really here and walking out; an older one was gone already
  // (stalled, or its load sent nothing): no leaving passage then.
  std::uint64_t fresh_ms = 300;
  // How long a player who left is still drawn on its shown pose: more than
  // presence's show delay (100 ms), so the last of its walk into the exit plays.
  std::uint64_t leave_show_ms = 150;
  // Then how long it is kept out of sight where it left before it is let go.
  std::uint64_t leave_park_ms = 1000;
  // A player who came in has stood up at the entrance once its shown pose is
  // this far (game units, about metres) from where it was while elsewhere...
  float moved_m = 1.0f;
  // ...and this long has passed since that (the slide of the shown pose
  // between the two places is over: one sample gap, 250 ms at most, plus
  // the show delay).
  std::uint64_t arrive_settle_ms = 400;
  // At most this long kept out of sight after it came in.
  std::uint64_t arrive_timeout_ms = 3000;
};

class Passages {
 public:
  explicit Passages(PassageSettings settings = {}) : settings_(settings) {}

  // Once per plan, for each remote player shown (number 1 to kMaxPlayers;
  // others give none). `local`: our scene, `remote`: theirs as presence has
  // it (nothing: not known), `shown`: where presence draws them now. Our own
  // place changing (or not known) starts every track again: whoever is in
  // the new place at the first note was there before us, no passage.
  Passage note(std::uint8_t number, std::optional<std::string_view> local,
               std::optional<std::string_view> remote, const protocol::Vec3& shown,
               std::uint64_t now_ms);

  // Why the last note of `number` gave what it gave, for the director's log
  // line ("left the scene", "arriving", ...). String literals only.
  const char* why(std::uint8_t number) const;

  void forget(std::uint8_t number);  // a player who left the session
  void reset();

  const PassageSettings& settings() const { return settings_; }

 private:
  struct Track {
    bool known = false;  // noted since our place last changed
    bool here = false;   // in our place at the last note
    std::uint64_t last_ms = 0;               // the last note
    std::optional<protocol::Vec3> elsewhere; // shown pose at the last note while elsewhere
    std::uint64_t left_ms = 0;               // when it left our place (fresh); 0 none
    bool arriving = false;
    std::uint64_t arrived_ms = 0;
    bool moved = false;
    std::uint64_t moved_ms = 0;
    const char* why = "";
  };

  PassageSettings settings_;
  std::array<Track, protocol::kMaxPlayers + 1> tracks_{};
  std::string local_place_;  // our place (scene without its era) at the last note
  bool local_known_ = false;
};

}  // namespace coop::host
