// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM choque (oot-remake docs/CHOQUE.md): real bodies between Links. Request
// (2026-11-12): you can body-block a partner and stand on him; drawing the
// mate's puppet aside (docs/CUERPOS.md) looks crude.
//
// - Each mate's puppet gets a body actor of ours in our game, made from the
//   puppet pack (tools/puppet_pack, BODY_KINDS): CoopPuppet_Child_Body /
//   CoopPuppet_Adult_Body, two kinematic rigid bodies that track the actor and
//   hit only the Player layer: the sides (the player's own cct prism, layer
//   Character / NPC, unridable as a townsperson) and a disc on the head (layer
//   GroundObject, ridable); _BodySide is the sides alone. No model, AS, life
//   or AI.
// - A fourth CoopPuppet::Director (svc caller kCallerOrder, after the horses)
//   makes and places them: each calc frame exactly where the puppets' director
//   drew that mate (CoopPuppet::LastSmoothed, the same frame), upright, with
//   the game's own setter after the write (Director::SetMatrixCall) so its
//   physics takes the place. We only ever move that actor; the game's physics
//   makes our Link collide with it, so walls and railings are kept and our
//   Link is never written.
// - The gate (coop::host::orm::bodies::BodyGate, in the puppets' planner,
//   Target::solid): a ghost (parked Director::kHideDrop under its spot) until
//   the two are clear of each other, and again while downed, riding, not
//   shown, after a warp of its samples or deep in our Link. While solid the
//   puppet is drawn on its sample (no drawn aside).
// - Switches: the room's /choque (ROOM_SWITCH key bodies, its default
//   coop::host::orm::pvp::kBodiesDefault: on since the test10 game run of 2026-11-13);
//   LICHII_ORM_BODY_COLLIDER: room (unset, the default) | on (whatever the
//   room says: the probes) | off (never: drawn aside, as before);
//   LICHII_ORM_BODY_TOP (on unless 0: the head disc; 0: _BodySide).
// - The path taken, once: "coop: check: bodies: collider ok <what was made>"
//   or "coop: check: bodies: collider fallback <why>" (the pack has no body,
//   a body could not be made twice, the switch says off), and then the mates
//   are drawn aside as before. "coop: bodies: ..." lines on each change.
//
// BodiesFrom, BodiesIn, Choose, SameFrame and CreateTally are plain logic,
// tested on their own (tests/lichii/bodies_hook_test.cpp); Use is the game
// glue (ORM_PLATFORM_LIVE).
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "coop/host/pose_smooth.hpp"
#include "coop_puppet.h"

namespace CoopBodies {

// The svc cave's order for the bodies' director: after the puppets (they set
// LastSmoothed for the frame), spawn 10, the fairies 20 and the horses 25;
// before the object mover 30.
inline constexpr int kCallerOrder = 26;

// LICHII_ORM_BODY_COLLIDER.
enum class Mode { room, on, off };
// Unset or empty: room; "room": room; on/off words (coop_switch.h): on, off;
// anything else: room, and `complaint` (when given) gets the log line.
Mode ModeOf(std::optional<std::string_view> value, std::string* complaint = nullptr);
const char* ModeText(Mode mode);

// The body actor for a puppet of that age.
std::string BodyActor(bool adult, bool top);

// The bodies a puppet pack has: the actors its coop_puppet.json lists
// (build.py writes them under "bodies"; an older pack lists none).
struct PackBodies {
    bool child = false;       // CoopPuppet_Child_Body
    bool adult = false;       // CoopPuppet_Adult_Body
    bool child_side = false;  // CoopPuppet_Child_BodySide
    bool adult_side = false;  // CoopPuppet_Adult_BodySide
    bool Top() const { return child && adult; }
    bool Side() const { return child_side && adult_side; }
};
PackBodies BodiesIn(std::string_view manifest);
PackBodies Merge(PackBodies a, const PackBodies& b);

// Whether the colliders run this frame, with which kind, and why not.
struct Pick {
    bool on = false;
    bool top = true;        // the _Body kind (with the head disc); false _BodySide
    bool fallback = false;  // off for a reason our game has (not the room's switch)
    std::string why;        // "" when on; the reason otherwise (for the log)
};
// `broken`: a body actor could not be made twice (CreateTally::Broken).
Pick Choose(Mode mode, bool room_on, const PackBodies& pack, bool top_wanted, bool broken);

// The bodies' plan from the puppets' (CoopPuppet::Publish's, OnPlan): one
// target per puppet, at its place less the drawn aside, upright, with nothing
// to wear, play or show but the place; shown while Target::solid, else hidden
// (parked under the floor: a ghost). The input's game, scene, drawing and
// server times are the puppets', so both directors run on the same clock.
CoopPuppet::Input BodiesFrom(const CoopPuppet::Input& puppets, bool top);

// The bodies' smoothing source: player n's pose as the puppets' director drew
// it, when it was drawn for this very frame (within kSameFrameUs of
// `server_now_us`), upright and without its animation; nothing otherwise (the
// body keeps the plan's place, as the puppet does).
inline constexpr std::uint64_t kSameFrameUs = 50'000;
std::optional<coop::host::SmoothedPose> SameFrame(const std::optional<CoopPuppet::ShownPose>& shown,
                                                  std::uint64_t server_now_us);

// The director's lines, counted per actor ("coop: puppet: P<n>: create <actor>
// requested ...", "... P<n> <actor> spawned after ...", "... not spawned after
// ...", "P<n>: request ... dropped (state ...)"). Any thread.
class CreateTally {
public:
    void Count(const std::string& line);
    // An actor with two failed creates and none made: the bodies fall back.
    std::optional<std::string> Broken() const;
    // The first actor made, once (for the "ok" line).
    std::optional<std::string> TakeFirstMade();
    static constexpr int kFailures = 2;

private:
    mutable std::mutex mutex_;
    std::map<int, std::string> requested_;  // by player: the last create
    std::map<std::string, int> failed_;
    std::map<std::string, int> made_;
    bool first_said_ = false;
};

// The game glue (ORM_PLATFORM_LIVE): the director, its caller, the plan
// observer, the switches and the pack's bodies, once. CoopSvc's
// ConfigurePuppets calls it after CoopPuppet::Configure. Nothing without the
// puppets.
void Use();

} // namespace CoopBodies
