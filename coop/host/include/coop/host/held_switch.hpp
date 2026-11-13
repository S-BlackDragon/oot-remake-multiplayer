#pragma once

// ORM held floor switches (oot-remake docs/SOSTENIDOS.md, PLAN_100 F3/E13, the
// case of Caleb: "one player stands on a switch, another goes through the door
// it holds open"). A held step switch (Obj_Stepswitch_A_03 and its kin) is on
// only while something stands on it, and the game keeps that in the switch
// actor, not in a flag (daurosk docs/dungeon.md, "Live dungeon actions"). The
// other players are puppets with no physics, so in game B nothing stands on
// B's switch while A's Link stands on A's.
//
// The rule (oot-64's: every Link presses a switch in every game; here, by
// place): a switch is "held by others" in our game while any other player in
// our scene has its feet inside the switch's area, as their samples put them
// (presence: the same positions the puppets are drawn at). While it is held by
// others the hook presses it in our game; when the last one steps off (or
// leaves, or disconnects) it lets it go, unless our own Link stands on it, in
// which case the game keeps it pressed by itself. No new message: the
// positions every game already has are enough, and every game decides the
// same from them.
//
// Here, with no game and no emulator:
// - HeldSwitchList: the switches (placement hash, place, actor, area) and the
//   words that press each kind of switch actor, from a text list (built in,
//   plus LICHII_ORM_HELD_SWITCH_LIST);
// - HeldSwitchRule: who stands on what, frame by frame, with the changes;
// - HeldFlagApply: the switch's own "Link on it" flag (the held: lines of
//   shared_dungeon_adult.txt) written 1 while held by others;
// - HeldWordApply: the actor words written while held by others.
// The hook glue is lichii/hook/coop_held_switch.{h,cpp}.
//
// Doc: oot-remake docs/SOSTENIDOS.md.
// Switch: LICHII_ORM_HELD_SWITCHES (unset or "log": watch and log only; "1":
//   also press; "0": off), LICHII_ORM_HELD_SWITCH_LIST, LICHII_ORM_HELD_SWITCH_PROBE.
// Test: tests/host/held_switch_test.cpp.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// Where feet count as "on the switch", in the switch's own frame (turned by
// its yaw about the vertical axis, origin at its placement). A box is
// |x| <= half_x, |z| <= half_z; a cylinder is x^2 + z^2 <= radius^2. Both
// y_min <= y <= y_max. From the actor packs' CheckObjectOn sensor, widened
// down to the pressed button: feet, not the body, are tested.
struct HeldArea {
  enum class Shape : std::uint8_t { box, cylinder };
  Shape shape = Shape::box;
  float half_x = 0.5f;  // a cylinder's radius
  float half_z = 0.5f;
  float y_min = -0.45f;
  float y_max = 0.3f;
  bool operator==(const HeldArea&) const = default;
};

enum class HeldKind : std::uint8_t {
  held,   // on only while something stands on it (Obj_Stepswitch_A_03 and kin)
  timed,  // stays on for its CountTime after it is left (Obj_StepSwitch_A_02)
};

struct HeldSwitchSpec {
  std::string place;           // scene place (place_of): "FireTemple"
  std::uint64_t hash = 0;      // placement hash (Banc "Hash")
  std::string actor;           // Gyaml: "Obj_Stepswitch_A_03"
  protocol::Vec3 position{};   // Banc "Translate"
  float yaw = 0.0f;            // radians about +y
  HeldKind kind = HeldKind::held;
  HeldArea area;
  std::optional<std::uint32_t> flag;  // "Link on it" flag the room writes, if any
  std::string flag_name;              // as written in the list
  std::string label;                  // for logs: "room10"

  // Feet at `p` inside the area grown by `margin` metres on every side.
  bool contains(const protocol::Vec3& p, float margin = 0.0f) const;
};

// A word of a switch actor that holds it pressed, as the gimmick probe and the
// held-switch probe log it: "actor+0x1a8" or "c+0x50+0x40" (the component at
// byte 0x50 of the actor's component table, actor+0x270, then its word at
// +0x40). 64-bit words; only `mask` bits are written.
struct HeldWord {
  std::string actor;          // Gyaml it belongs to
  bool in_component = false;  // false: actor+offset
  std::uint64_t entry = 0;    // byte offset in the component table
  std::uint64_t offset = 0;
  std::uint64_t on = 0;
  std::uint64_t off = 0;
  std::uint64_t mask = ~std::uint64_t{0};
  std::string where;  // as written
  bool operator==(const HeldWord&) const = default;
};

// "actor+0x1a8" or "c+0x50+0x40" into `out` (actor, on, off, mask untouched).
bool parse_held_where(std::string_view text, HeldWord& out);

// The list. One entry a line; '#' starts a comment:
//
//   switch <Place> <hash> <Actor> <x> <y> <z> [yaw=<rad>] [kind=held|timed]
//          [box=<hx>,<hz>,<ymin>,<ymax> | cyl=<r>,<ymin>,<ymax>]
//          [flag=<Name or 0x hash>] [label=<word>]
//   word <Actor> <where> on=<hex> off=<hex> [mask=<hex>]
//   off <hash>                 drops a switch an earlier list had
//
// A later switch line for the same hash replaces the earlier; a word line for
// the same actor and where replaces the earlier; an off line also takes the
// switch off the lists it is merged on top of (merge: its offs first, then its
// switch lines, so "off X" then "switch X" keeps the new X). Without
// box=/cyl= the area is the kind's default (held: box 0.5, 0.5, -0.45, 0.3;
// timed: box 0.44, 0.44, -0.4, 0.25).
class HeldSwitchList {
 public:
  static std::optional<HeldSwitchList> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<HeldSwitchList> load(const std::string& path, std::string* error = nullptr);
  // The held and timed step switches of the 13 dungeons, read from the romfs
  // (Banc *_Static, the actor packs' sensors; docs/SOSTENIDOS.md). No words:
  // those come from a run of the probe.
  static const HeldSwitchList& builtin();
  static std::string_view builtin_text();

  // `other`'s lines on top of these, in order.
  void merge(const HeldSwitchList& other);

  const std::vector<HeldSwitchSpec>& switches() const { return switches_; }
  const std::vector<HeldWord>& words() const { return words_; }
  std::vector<const HeldSwitchSpec*> in_place(std::string_view scene) const;
  const HeldSwitchSpec* find(std::uint64_t hash) const;
  std::vector<const HeldWord*> words_for(std::string_view actor) const;

 private:
  void put(HeldSwitchSpec spec);
  void put(HeldWord word);
  void drop(std::uint64_t hash);
  std::vector<HeldSwitchSpec> switches_;
  std::vector<HeldWord> words_;
  std::vector<std::uint64_t> drops_;  // off lines, for merge
};

// Another player as presence shows it this frame.
struct HeldOther {
  std::uint8_t number = 0;
  std::optional<std::string> scene;
  protocol::Vec3 position{};  // feet
  bool live = true;           // false: fading out (no samples): not holding
};

struct HeldInput {
  std::optional<std::string> scene;  // none or empty: loading, the title
  std::optional<protocol::Vec3> link;  // our Link's feet; none: not read
  std::vector<HeldOther> others;
  std::uint64_t now_us = 0;  // any steady clock
};

// One switch of our scene this frame.
struct HeldState {
  std::uint64_t hash = 0;
  bool ours = false;                  // our Link stands on it
  std::vector<std::uint8_t> others;   // the others on it, lowest first
  bool held_by_others() const { return !others.empty(); }
  bool operator==(const HeldState&) const = default;
};

struct HeldChange {
  enum class Kind : std::uint8_t {
    stepped_on,   // `who` came onto it (0: our Link)
    stepped_off,  // `who` left it
    gone,         // `who` left the scene, faded out or disconnected while on it
    pressed,      // others hold it now (it was not held by others)
    released,     // no other holds it any more
  };
  Kind kind = Kind::stepped_on;
  std::uint64_t hash = 0;
  std::uint8_t who = 0;
  bool ours = false;  // pressed / released: our Link on it at that moment
  bool operator==(const HeldChange&) const = default;
};

struct HeldFrame {
  std::vector<HeldState> states;  // every listed switch of our scene, list order
  std::vector<HeldChange> changes;
  // Our scene changed this frame (a door, a load, the title): every hold of
  // the scene before ended with it; `left` names the switches that were held
  // by others then (the actors are gone, nothing to let go of).
  bool scene_changed = false;
  std::vector<std::uint64_t> left;
  std::vector<std::uint64_t> held_by_others() const;
};

class HeldSwitchRule {
 public:
  // A holder that leaves the area is let go once its feet are kLeaveMarginM
  // outside it, or have been outside it for kLeaveUs: the samples are drawn
  // 0.1 s late and interpolated, and a door must not flicker at the edge.
  static constexpr float kLeaveMarginM = 0.15f;
  static constexpr std::uint64_t kLeaveUs = 200'000;

  explicit HeldSwitchRule(const HeldSwitchList& list) : list_(&list) {}

  HeldFrame frame(const HeldInput& in);

  // Forget everything (a reconnect: positions start over).
  void reset();

 private:
  struct Holder {
    std::uint64_t outside_since_us = 0;  // 0: inside
  };
  struct Tracked {
    std::map<std::uint8_t, Holder> holders;  // 0 is our Link
    bool held_by_others = false;
  };
  const HeldSwitchList* list_;
  std::optional<std::string> scene_;
  std::map<std::uint64_t, Tracked> tracked_;
};

// The switch's "Link on it" flag, when it has one: written 1 in our game while
// others hold it and our Link is not on it; written back to 0 when that ends
// (only a 1 we wrote, and only while our Link is not on it: then the game's
// own 1 stands). A 0 the game writes over ours while still held is written
// again (counted in rewrites; only the first of a press is in the notes). A 1
// left over when the scene changes is put
// back to 0 as soon as the flag can be written.
class HeldFlagApply {
 public:
  struct Note {
    std::uint64_t hash = 0;
    std::uint32_t flag = 0;
    std::uint32_t value = 0;  // written
    bool ok = false;          // the write took
    bool again = false;       // a rewrite over the game's 0
  };
  explicit HeldFlagApply(const HeldSwitchList& list) : list_(&list) {}
  std::vector<Note> apply(const HeldFrame& frame, FlagStore& store);
  std::size_t rewrites() const { return rewrites_; }
  bool writing(std::uint64_t hash) const { return written_.count(hash) != 0; }

 private:
  const HeldSwitchList* list_;
  std::map<std::uint64_t, std::uint32_t> written_;  // switch -> flag we hold at 1
  std::map<std::uint32_t, std::uint64_t> pending_clear_;  // flag -> switch
  std::set<std::uint32_t> failed_;                        // last write failed (noted once)
  std::set<std::uint64_t> again_noted_;                   // switch: a rewrite noted this press
  std::size_t rewrites_ = 0;
};

// The actor words of the switch kind (word lines), written each frame while
// others hold it and our Link is not on it (only the masked bits, only when
// they differ); when that ends, each word still at its `on` value is put to
// `off` once, so the actor's own calc takes over again (not for a timed
// switch: its own clock runs from there, as when a Link steps off it). The actor is found by
// its placement hash (the objects walk); none: not loaded, nothing written.
class HeldWordApply {
 public:
  using ReadWord = std::function<std::optional<std::uint64_t>(std::uint64_t)>;
  using WriteWord = std::function<bool(std::uint64_t address, std::uint64_t value)>;
  using FindActor = std::function<std::optional<std::uint64_t>(std::uint64_t hash)>;
  static constexpr std::uint64_t kActorTable = 0x270;  // as coop_objects.cpp

  struct Note {
    std::uint64_t hash = 0;
    std::string where;
    std::uint64_t address = 0;
    std::uint64_t before = 0;
    std::uint64_t after = 0;
    bool ok = false;
    bool release = false;  // the off write
  };
  explicit HeldWordApply(const HeldSwitchList& list) : list_(&list) {}
  // Only the first write of each word per press is noted (the rest are counted).
  std::vector<Note> apply(const HeldFrame& frame, const FindActor& find, const ReadWord& read,
                          const WriteWord& write);
  std::size_t writes() const { return writes_; }
  // The word's address in an actor, or nothing (a bad table or component pointer).
  static std::optional<std::uint64_t> address_of(const HeldWord& w, std::uint64_t actor,
                                                 const ReadWord& read);

 private:
  const HeldSwitchList* list_;
  std::set<std::uint64_t> pressing_;          // switches we write now
  std::set<std::uint64_t> noted_;             // first write noted this press
  std::map<std::uint64_t, std::uint64_t> actor_of_;  // switch -> actor we wrote into
  std::size_t writes_ = 0;
};

// Does pressing it here open its door? (docs/RIESGOS_RESUELTOS.md, "Mundo e
// historia"). The words of each switch kind come only from a run of the probe;
// until then the switch is pressed with its "Link on it" flag alone, and this
// tells, from the door actors near the switch, whether that was enough: when
// others start holding it, the doors within kNearM of the switch are noted;
// one that then moves more than kMoveM, plays an AS command with "Open" in it,
// or goes (a fire wall) within kWaitUs says it opened; none says it did not.
// A switch with no door actor near, or with the door state not read, says so.
// One line per press; the verdict of each switch is kept.

// A door actor of our scene, from the objects walk.
struct HeldDoorSeen {
  std::string name;
  protocol::Vec3 position{};
  std::optional<std::string> animation;  // its AS slot 0 command, when read
};
// Shutters, bars and fire walls a switch holds open (by actor name).
bool held_door_actor(std::string_view name);

enum class HeldDoorVerdict : std::uint8_t {
  unknown,    // not seen yet (or no door to tell by)
  opened,     // the door moved with what we wrote
  stays_shut, // it did not: the flag alone does not press it here
};

class HeldDoorCheck {
 public:
  static constexpr float kNearM = 20.0f;
  static constexpr float kMoveM = 0.3f;
  static constexpr std::uint64_t kWaitUs = 4'000'000;

  // How the switch is pressed here this time, for the line.
  struct Path {
    bool flag = false;   // its "Link on it" flag is written
    bool words = false;  // its kind's actor words are written
  };
  using PathOf = std::function<Path(const HeldSwitchSpec&)>;

  explicit HeldDoorCheck(const HeldSwitchList& list) : list_(&list) {}
  // Once a frame, after the flag and words are applied. `doors`: the door
  // actors of our scene (nothing: the walk was not read). Returns the lines
  // ("held switch: ..."; the hook adds "coop: ").
  std::vector<std::string> frame(const HeldFrame& frame, const std::optional<std::vector<HeldDoorSeen>>& doors,
                                 const PathOf& path_of, std::uint64_t now_us);
  HeldDoorVerdict verdict(std::uint64_t hash) const;
  void reset() { checks_.clear(); }

 private:
  struct Check {
    std::uint64_t since_us = 0;
    Path path;
    std::optional<std::vector<HeldDoorSeen>> base;  // the near doors at the press
  };
  const HeldSwitchList* list_;
  std::map<std::uint64_t, Check> checks_;
  std::map<std::uint64_t, HeldDoorVerdict> verdicts_;
};

const char* to_string(HeldChange::Kind kind);

}  // namespace coop::host
