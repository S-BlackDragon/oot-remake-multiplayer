#pragma once

// Townspeople one talks to (ORM D-R12, docs/objects.md "Townspeople"). The
// hook already shares every Npc_ actor as an object with one owner: the
// owner's game moves it, the others copy its place and animation. When a
// player talks to a townsperson its own game turns it, plays the talk and may
// walk it away; while another game owns it, that copy is pulled back to the
// owner's pose every frame and the talk shows nowhere else.
//
// NpcTalk finds the townsperson our player is talking to, from what the hook
// already reads: whether an event runs (PlayerReading::event_running, minus
// the Ocarina: scene.hpp local_event_hides), our Link's place and the shared
// actors of the walk with their AS slot 0 command. The hook then
// - claims it: OBJECT_PRESENT flags it targeting our player, so the server
//   hands it to us (a copy that chases its player, server/session.cpp
//   chased_by) or keeps it with us (an owner's own targeting is a fight);
// - frees our copy of it until it is ours: no pose or animation of the old
//   owner is written into it, so our own game's talk plays undisturbed.
// ORM charla-radio: big or far townspeople (Biggoron, Kaepora Gaebora on his
// perch, King Zora on his throne) are talked to from farther than kReachM, so
// the partner is first taken from the game itself: the townsperson whose event
// component (read_event_phase) left its rest value when our event started, or
// whose invisible stand-in's did (Npc_Invisible_<X> talks for Npc_<X>:
// stand_in_body). When the game says nothing, the reach of each name
// (talk_reach_m, up to kMaxReachM) and a stand-in within kReachM stand in.
// No emulator code: the one memory read is read_event_phase on GameMemory;
// the rule is tested on plain lists and fake memory.
//
// Doc: oot-remake docs/NPC_COMPARTIDOS.md (D-R12), docs/CHARLAS.md.
// Switch: LICHII_COOP_NPC (on in the kit since 0.6).
// Test: tests/host/npc_talk_test.cpp.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/game_memory.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// A townsperson one may talk to: an Npc_ actor that is not each player's own
// (Navi, held equipment, invisible helpers, as CoopObjects::IsShared) and not
// a fairy (Npc_Fairy*: the Kokiri's fairies hover beside their Kokiri, so
// they would often be the nearest) or a voice (Npc_*_Voice, no body).
bool is_talker(std::string_view name);

// ORM charla-radio: the townsperson an invisible talk stand-in speaks for.
// Npc_Invisible_Goron_Biggoron_Adult is what Link targets on the summit's
// ledge, 10 m from Biggoron; Npc_Invisible_Kaepora, Npc_Invisible_Zora_005 and
// the others in Pack/Actor likewise. "Npc_Invisible_<X>" gives "Npc_<X>";
// nothing for any other name, and for Npc_Invisible_Base. A body is any
// talker whose name starts with it (Npc_Kaepora_LakeHylia for Npc_Kaepora).
std::optional<std::string> stand_in_body(std::string_view name);

// ORM charla-radio: how far from our Link a townsperson of this name may be
// when its talk starts, for the fallback (the game did not say who it is):
// NpcTalk::kReachM, or more for the big and the high ones (Biggoron,
// Jabu-Jabu: kMaxReachM; Kaepora Gaebora, King Zora, the Great Fairies: 15 m;
// Medigoron 10 m; Darunia, the Deku Tree sprout: 8 m). Names from Pack/Actor.
float talk_reach_m(std::string_view name);

// ORM c3 (oot-remake docs/TRIAGE_ROJOS.md, docs/SONDAS.md round 7): whether our own event, by its name,
// can be a talk for the fallback by reach. A talk's event is the townsperson's own
// ("Npc_Kokiri_Mido_Child_DC0_TT", "Npc_Kokiri_004_Child_DC0_TT"): a name that starts with "Npc_". A fall-down
// respawn ("DmF_SY_FallDown"), a scene's boot ("DefaultBootEvent"), the test harness's place or warp, a
// cutscene: not a talk, though a townsperson stands within reach (charla-saria, round 7: Saria claimed during
// a fall-down; charla-biggoron, round 1: a place's event within Biggoron's 25 m).
bool talk_event_name(std::string_view event_name);

// ORM charla-radio: u32 [[[actor + 0x270] + 0x90] + 0x28], the phase of the
// actor's event component (addresses.hpp kComponentsEvent); nothing when the
// actor has no table or no event component, or a read fails.
std::optional<std::uint32_t> read_event_phase(GameMemory& memory, std::uint64_t actor);

// One actor of the walk, as the hook read it: a shared townsperson, or a
// stand-in (stand_in_body), which is never a partner itself.
struct NpcSeen {
  std::uint64_t address = 0;
  std::string name;
  protocol::Vec3 position{};
  std::optional<std::string> animation;  // AS slot 0 command, when read
  std::optional<std::uint32_t> event_phase;  // read_event_phase, when read
};

class NpcTalk {
 public:
  // A partner is at most this far from our Link when the event starts (most
  // townspeople; talk_reach_m gives each name's).
  static constexpr float kReachM = 5.0f;
  // The farthest reach of a name (Biggoron, Jabu-Jabu).
  static constexpr float kMaxReachM = 25.0f;
  // A partner the game names (its event component) is at most this far from
  // our Link: the walk's loaded actors only, a sanity bound.
  static constexpr float kGameReachM = 40.0f;
  // A stand-in's body is at most this far from the stand-in.
  static constexpr float kStandInBodyM = 25.0f;

  // How the partner was found (for the log line).
  enum class Found : std::uint8_t {
    none,
    game,      // its own event component left its rest value
    stand_in,  // the event component of its stand-in did
    by_reach,  // within reach (talk_reach_m), its stand-in within kReachM
  };
  // In the first part of an event a townsperson whose animation changed
  // since before the event (it turned to us, started its talk) wins over a
  // nearer one that did not; after that the partner stays.
  static constexpr std::uint64_t kSettleUs = 1'000'000;
  // After the event ends the claim and the freed copy are kept this long, so
  // a hand-over in flight lands before the old owner's pose comes back.
  static constexpr std::uint64_t kAfterUs = 1'500'000;

  // Present thread, once per walk. `event` is our own event (a talk or a
  // cutscene, not the Ocarina); `link` our Link's place (nothing: no Link on
  // the walk); `seen` the shared actors of the walk (others are skipped).
  void update(bool event, const std::optional<protocol::Vec3>& link,
              const std::vector<NpcSeen>& seen, std::uint64_t now_us);

  // Our event's name before each update while one runs (nothing: not readable, as before). With a name that
  // is no talk's (talk_event_name), the fallback by reach takes nobody; the game's own word (an event
  // component) still does.
  void set_event_name(std::optional<std::string> name) { event_name_ = std::move(name); }
  // The name of an event whose fallback was refused, once per event (for one log line), else nothing.
  std::optional<std::string> take_refused() {
    if (!refused_ || refused_told_) return std::nullopt;
    refused_told_ = true;
    return refused_name_;
  }

  // The townsperson we talk to now (also during kAfterUs), or nothing.
  std::optional<std::uint64_t> partner() const { return partner_; }
  // True while it is claimed: its OBJECT_PRESENT entry flags targeting, and
  // a copy of it is left to our own game.
  bool claims(std::uint64_t address) const { return partner_ && *partner_ == address; }
  // True between the end of the event and the end of kAfterUs.
  bool lingering() const { return partner_ && ended_at_ != 0; }
  // How the partner was found (none: no partner).
  Found found() const { return partner_ ? found_ : Found::none; }
  // The stand-in whose component moved (Found::stand_in), and how far from
  // our Link the pick was (the stand-in's distance for one by its stand-in).
  const std::string& stand_in() const { return stand_in_; }
  float distance_m() const { return distance_m_; }

  // Not connected, a new scene: forget everything.
  void reset();

 private:
  bool event_ = false;
  std::uint64_t event_since_ = 0;
  std::uint64_t ended_at_ = 0;  // 0: the event still runs (or none)
  std::optional<std::uint64_t> partner_;
  bool partner_changed_ = false;
  Found found_ = Found::none;
  std::string stand_in_;
  float distance_m_ = 0.0f;
  std::optional<std::string> event_name_;
  bool refused_ = false;       // this event's fallback by reach was refused (its name is no talk's)
  bool refused_told_ = false;  // take_refused gave it
  std::string refused_name_;
  // Each townsperson's animation before the event started (its rest).
  std::map<std::uint64_t, std::string> rest_;
  // Each townsperson's and stand-in's event component phase at rest.
  std::map<std::uint64_t, std::uint32_t> rest_phase_;
};

const char* to_string(NpcTalk::Found f);

// ORM charla-radio self-check (request: no open assumption; the run says which
// path held): the line logged once per townsperson when a talk takes it, after
// "coop: ". `name` the townsperson, `found` how, `stand_in` the stand-in whose
// component moved (Found::stand_in), `distance_m` from our Link:
//   "check: talk target <name> by event-component"
//   "check: talk target <name> by event-component (stand-in <stand_in>)"
//   "check: talk target <name> by radius <reach> m (event component did not
//    change; <distance> m away)"
std::string talk_check_line(std::string_view name, NpcTalk::Found found,
                            std::string_view stand_in, float distance_m);

// ORM "Dos jugadores con el mismo PNJ" (oot-remake docs/NPC_COMPARTIDOS.md),
// the rule of oot-64 (D-069): a townsperson talking to one player answers only
// that one until the talk is done. Here every game has its own copy and its
// own text box, so the second talker's game plays its own talk on its own copy
// (NpcTalk frees it from the owner's poses as before); what it must not do is
// take the townsperson away from the owner who is still talking to it, which
// would move every other screen from the first talk to the second and back.

// What we know of the owner of the townsperson we talk to.
struct NpcOwner {
  std::uint8_t player = 0;              // 0: no owner known
  bool in_event = false;                // its PLAYER_STATUS says in_cutscene
  std::optional<protocol::Vec3> link;   // its Link's place (a fresh sample), if known
  bool posing = false;                  // its OBJECT_POSE of this townsperson came lately
};

// True when the owner's game talks to the townsperson standing at `at`: its
// own OBJECT_POSE of it came lately (an owner sends one only for its talk
// partner, LICHII_COOP_NPC_POSE), or its player is in an event with its Link
// within NpcClaim::kOwnerReachM of it (for a named one, its talk_reach_m plus
// the same metre: Biggoron is talked to from 10 m).
bool owner_talks(const NpcOwner& owner, const protocol::Vec3& at, std::string_view name = {});

// Whether our talk claims its townsperson (OBJECT_PRESENT targeting), once a
// walk, after NpcTalk::update.
// - Ours, or no owner known: claimed, as before.
// - Another player's, and that player talks to it: yielded. Not claimed for
//   the rest of this talk, even once the owner's talk is over (so the server
//   never hands it over in the middle of ours, and never back); our copy stays
//   freed (NpcTalk) until our talk is over, then blends back (Sync::SetTalk).
// - Another player's, nobody talks to it: claimed (the server hands it to us).
// - A claim held longer than claim_max_us (an event bit stuck on near a
//   townsperson) is let go for the rest of this talk. An owner whose event has
//   run that long is not yielded to either.
class NpcClaim {
 public:
  enum class State : std::uint8_t { none, claimed, yielded, timed_out };

  // The owner's Link within this of the townsperson counts as talking to it.
  static constexpr float kOwnerReachM = NpcTalk::kReachM + 1.0f;
  // The longest claim, and the longest owner's event yielded to. A talk is
  // a few dozen seconds; a shopkeeper's (browsing the shelf) may be minutes.
  // ORM charla-mido (round 5): Mido's first talk with the sword and shield ran
  // 57 s with A pressed every 0.75 s (six talks, Navi's, his blocker AI's end),
  // and the 60 s cap let the claim go at its end: a player who reads takes
  // twice that. Three minutes; a stuck event bit still ends.
  static constexpr std::uint64_t kClaimMaxUs = 180'000'000;
  static constexpr std::uint64_t kShopClaimMaxUs = 300'000'000;
  static std::uint64_t claim_max_us(std::string_view name);

  // Every walk, for each other player we know the status of (in any order),
  // before update: when each one's event started.
  void see_status(std::uint8_t player, bool in_event, std::uint64_t now_us);
  // Every walk: `partner` from NpcTalk (nothing: no talk), its name and place,
  // our own player number, what we know of its owner.
  State update(std::optional<std::uint64_t> partner, std::string_view name,
               const protocol::Vec3& at, std::uint8_t self, const NpcOwner& owner,
               std::uint64_t now_us);

  State state() const { return state_; }
  bool claims() const { return state_ == State::claimed; }
  // The owner's event counted as stuck (it ran over the claim limit).
  bool owner_stuck() const { return owner_stuck_; }
  // When the given player's event started (0: none seen running).
  std::uint64_t event_since(std::uint8_t player) const;

  void reset();

 private:
  State state_ = State::none;
  std::optional<std::uint64_t> partner_;
  std::uint64_t claimed_since_ = 0;
  bool owner_stuck_ = false;
  std::map<std::uint8_t, std::uint64_t> event_since_;  // by player: 0 none
};

const char* to_string(NpcClaim::State s);

}  // namespace coop::host
