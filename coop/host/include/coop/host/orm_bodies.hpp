#pragma once
// ORM bodies (oot-remake docs/COMANDOS.md, "Cuerpos"; docs/CUERPOS.md): two
// Links never drawn inside each other. The portable side: no emulator, no
// clock of its own (times in), no network.
//
// Our own Link is never moved. Moving him meant writing his actor's position
// (CoopBoss::MoveLink), which skips the game's collision: the friend who
// pressed Continue on the host's save spot was carried through the railing
// by the Kokiri Forest start and walked in place there for a minute
// (SONDAS.md round 7, cartel and charla; again with the checked push of
// f8a9dcc, cuerpos1_*: a written position always reads back as taken). No
// movement input of Link's that the game integrates with its collision is
// known to the hook, so the parting is drawn: while a mate's puppet stands
// within two body radii of our Link, PuppetAside draws it out to that
// distance, away from us (presentation only: its sample, the hits and
// everything else keep the sample). Nobody can be wedged.
// - The way: from our Link to the puppet; on top of each other (closer than
//   kFullOverlapM) from the mate's number (fixed, never from jittering
//   samples), held while closer than kHoldWayM.
// - The drawn step follows its target at kSpeedMps (no pop), and a puppet
//   first planned already on us is drawn aside at once.
// - Downed mates (a reviver kneels by them), another floor, a puppet not
//   shown or our Link not read: drawn on the sample.
// - ReadBack: the puppet's position read from its actor on the next pass,
//   against where we drew it: the game moving it (its own collision) is
//   logged once per contact, as is the first read that matches.
//
// ORM choque (oot-remake docs/CHOQUE.md): with the room's /choque on, each
// mate's puppet also has a real body in our game (the puppet pack's
// CoopPuppet_*_Body, placed with it by lichii/hook/coop_bodies.h): BodyGate
// says, plan by plan, whether that body is solid or a ghost (parked out of the
// world). A ghost until the two are clear of each other (kClearM apart, or one
// over the other, for kClearForMs), so a body never appears on anyone (the
// Continue spot); a ghost again while downed, riding, not shown, after a warp
// of its samples, or when it is deep in our Link (kDeepM). While solid, the
// puppet is drawn on its sample (AsideIn::solid): the bodies part the Links.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host::orm::bodies {

using protocol::Vec3;

inline constexpr float kBodyRadius = 0.35f;     // a Link, metres (two: 0.7 apart)
inline constexpr float kBodyHeight = 1.5f;      // feet this close in height touch
inline constexpr float kFullOverlapM = 0.05f;   // closer: "on top", the way by number
inline constexpr float kHoldWayM = 0.15f;       // closer: the way found first is kept
inline constexpr float kSpeedMps = 2.0f;        // the drawn step's speed
inline constexpr std::uint64_t kForgetMs = 5000;  // a mate not planned this long is forgotten
inline constexpr float kReadBackM = 0.05f;      // read back further than this from where drawn: moved

// The way a puppet on top of our Link is drawn out, from its number alone
// (60 degrees a number): fixed for that mate, whatever the samples do.
Vec3 way_of_number(std::uint8_t number);

struct AsideIn {
  std::uint8_t number = 0;
  Vec3 sample{};                 // where the puppet would be drawn (its sample, any other step in)
  std::optional<Vec3> ours;      // our Link's feet; none: not read
  bool shown = true;             // drawn this plan (not held, hidden or parked)
  bool down = false;             // the mate is downed
  bool solid = false;            // ORM choque: its body is solid (BodyGate): drawn on its sample
  std::uint64_t now_ms = 0;
};

class PuppetAside {
 public:
  // What to add to the puppet's drawn place this plan (zero: on its sample).
  Vec3 step(const AsideIn& in);
  // Mates not planned for kForgetMs are dropped.
  void prune(std::uint64_t now_ms);
  void reset();
  // One line per change ("P1's puppet on our Link ..."), for the log.
  std::vector<std::string> take_lines();
  bool aside(std::uint8_t number) const;  // a step is drawn now

 private:
  struct State {
    Vec3 offset{};
    std::optional<Vec3> held_way;
    std::uint64_t last_ms = 0;
    bool contact = false;  // touching our Link (target not zero)
  };
  std::map<std::uint8_t, State> states_;
  std::vector<std::string> lines_;
};

// The read back of a puppet drawn aside: `drawn` is what we wrote last pass,
// `read` what its actor holds now. One line when a contact's first read
// matches and one when the game moved it; end() when it is drawn on its
// sample again.
class ReadBack {
 public:
  std::string judge(std::uint8_t number, const Vec3& drawn, const Vec3& read);
  void end(std::uint8_t number);

 private:
  struct Said {
    bool ok = false;
    bool moved = false;
  };
  std::map<std::uint8_t, Said> said_;
};

// ---- ORM choque: the gate of a mate's real body ------------------------------

inline constexpr float kClearM = 2 * kBodyRadius + 0.1f;  // flat apart this far: clear (0.8 m)
inline constexpr std::uint64_t kClearForMs = 300;          // clear this long: solid
inline constexpr float kDeepM = 0.25f;                     // solid and flat this close: a ghost again
inline constexpr float kJumpM = 3.0f;                      // its sample this far from the last plan's: a warp
inline constexpr float kChildHead = 1.17f;                 // the player's cct prisms (Player_cct)
inline constexpr float kAdultHead = 1.68f;
inline constexpr float kHeadSlack = 0.15f;                 // feet this far under a head are on it
inline constexpr float kChildRadius = 0.23f;
inline constexpr float kAdultRadius = 0.35f;
// The self-check lines of a solid body (the probes, docs/CHOQUE.md): our Link
// against it this long, or standing on its head this long.
inline constexpr std::uint64_t kTouchForMs = 500;
inline constexpr std::uint64_t kStandForMs = 1000;

struct GateIn {
  std::uint8_t number = 0;
  Vec3 sample{};               // where its body would stand (its puppet's place, no drawn aside)
  std::optional<Vec3> ours;    // our Link's feet; none: not read
  bool shown = true;           // the puppet is drawn walking (shown, not leaving or arriving)
  bool down = false;           // downed: a reviver kneels by him
  bool riding = false;         // on a horse (or getting on): the horse is drawn under him
  bool adult = false;          // the mate's head height
  bool our_adult = false;      // ours
  std::uint64_t now_ms = 0;
};

// How a mate's feet stand from ours: flat distance, and whether the two
// bodies share a height (neither stands above the other's head).
struct Apart {
  float flat = 0.0f;
  bool level = false;
};
Apart apart_of(const Vec3& sample, const Vec3& ours, bool adult, bool our_adult);

class BodyGate {
 public:
  // Solid this plan? One line per change ("bodies: P2's body solid ..."),
  // and once per contact while solid: "bodies: our Link against P2's body
  // (0.47 m apart ...)" after kTouchForMs, "bodies: our Link stands on P2's
  // head (1.17 m over his feet)" after kStandForMs.
  bool solid(const GateIn& in);
  bool is_solid(std::uint8_t number) const;
  void prune(std::uint64_t now_ms);  // mates not planned for kForgetMs
  void reset();                      // a new scene, the colliders off: every body a ghost again
  std::vector<std::string> take_lines();

 private:
  struct State {
    bool solid = false;
    std::optional<std::uint64_t> clear_since_ms;
    std::optional<Vec3> last;  // its sample at the last shown plan
    std::uint64_t last_ms = 0;
    bool said_on_us = false;   // "a ghost until clear" said for this overlap
    std::optional<std::uint64_t> touch_since_ms, stand_since_ms;
    bool said_touch = false, said_stand = false;
  };
  void contact(State& s, const GateIn& in, const Apart& a);
  std::map<std::uint8_t, State> states_;
  std::vector<std::string> lines_;
};

}  // namespace coop::host::orm::bodies
