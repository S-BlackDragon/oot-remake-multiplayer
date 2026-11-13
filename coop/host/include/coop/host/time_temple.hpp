#pragma once

// ORM "the Temple of Time with several players" (oot-remake docs/TEMPLO_TIEMPO.md,
// after oot-64 D-068, D-122, D-178, D-183, D-187 and oot-remake D-R17, D-R20).
//
// In oot-64 the Temple of Time is a scene of its own: a cutscene there (the
// Door of Time, the pedestal, Sheik) is the group's, the whole group in the
// temple draws the sword with whoever draws it, goes with him through the
// Sacred Realm and Rauru's Chamber of Sages and comes back adult; the groups
// elsewhere reload their zone at the other age (D-068). In the remake the
// temple is a part of Market_Child / Market_Adult, the same scenes as the
// market square, so a story start "for the scene" (coop/host/story.hpp) would
// pull a mate shopping in the square into the pedestal's cutscene. This file
// gives the story layer what oot-64's scene gave:
//
// - the temple's area in each of the two scenes (a box, read from the scenes'
//   own LocationArea "TimeTemple" placements, with a margin);
// - for each Temple of Time event a rule:
//     temple  a mate's start is taken only by a game whose Link is in the
//             temple (the Door of Time, the pedestal, Sheik in the temple);
//             the others get its flags, as ever;
//     travel  the same, and the event changes the age (drawing or putting
//             back the sword): a mate in the temple travels with his own
//             cutscene, one elsewhere follows the room's age by the reload of
//             D-R17; the room's age follow is held while it runs here;
//     chain   a later part of the sword's story (the Sacred Realm, Rauru):
//             every traveller's own game plays it after its own draw, so a
//             mate's start of it is never taken (it would start the Chamber of
//             Sages for a latecomer, out of its story); the age follow is held
//             while it runs here;
//     sage    a sage's awakening after an adult temple's blue warp: only who
//             warps sees it (the story list's `solo`, ZMP D-067/D-122); the
//             item after it (the medallion) is a shared item;
//     scene   Sheik elsewhere (the lessons, Kakariko): every game in the scene
//             follows, the story layer's own rule; listed so the check below
//             can say it is there;
// - the scenes where the age is never followed (SageRoom: no age in its name,
//   and its story takes Link out on its own);
// - more saved Bools the time travel and the sages set that the story list
//   does not carry (first times, the light shaft, the sages' count): shared.
//
// The rules are over readings the hook gives; no memory, no emulator, no
// sockets. The glue is lichii/hook/coop_time_temple_game.cpp.
//
// Doc: oot-remake docs/TEMPLO_TIEMPO.md.
// Switch: on with LICHII_ORM_STORY_EVENTS unless LICHII_ORM_TIME=0; list
// LICHII_ORM_TIME_LIST (the kit's shared_time_temple.txt; else built in).
// Test: tests/host/time_temple_test.cpp.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/story.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

enum class TimeRule : std::uint8_t { temple, travel, chain, sage, scene };

const char* time_rule_name(TimeRule rule);  // "temple", "travel", ...

// A box in one scene, world coordinates (metres), min <= max on each axis.
struct TimeArea {
  std::string scene;
  protocol::Vec3 min{};
  protocol::Vec3 max{};
  bool contains(const protocol::Vec3& p) const;
  bool operator==(const TimeArea&) const = default;
};

struct TimeEntry {
  std::string event;
  TimeRule rule = TimeRule::temple;
  // The item the event gives (a medallion: Item_Medal_Forest), or empty; it
  // must be a shared item (check_time_list).
  std::string item;
  bool operator==(const TimeEntry&) const = default;
};

// The list, from plain text, one line each:
//
//   # a comment; blank lines are fine
//   area  SCENE  MINX MINY MINZ  MAXX MAXY MAXZ
//   noage SCENE
//   flag  NAME                    (a saved Bool, by name or 0x hash)
//   temple EVENT
//   travel EVENT
//   chain  EVENT [ITEM]
//   sage   EVENT ITEM
//   scene  EVENT
//
// An event listed twice is an error, so is a scene with two areas or a flag
// twice. A `sage` line needs its item.
class TimeTempleList {
 public:
  static constexpr std::size_t kMaxEntries = 64;
  static constexpr std::size_t kMaxAreas = 16;
  static constexpr std::size_t kMaxFlags = 64;

  TimeTempleList() = default;

  static TimeTempleList defaults();  // the built-in list (the kit's file says the same)
  static std::optional<TimeTempleList> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<TimeTempleList> load(const std::filesystem::path& path,
                                            std::string* error = nullptr);

  const TimeEntry* find(std::string_view event) const;
  const std::vector<TimeEntry>& entries() const { return entries_; }
  const std::vector<TimeArea>& areas() const { return areas_; }
  const std::vector<std::string>& noage_scenes() const { return noage_; }
  const std::vector<std::string>& flag_names() const { return flag_names_; }
  // The `flag` lines as shared-flag keys (index 0), sorted, each once.
  std::vector<FlagKey> shared_flags() const;

  // The area of `scene`, or null when the list has none for it.
  const TimeArea* area(std::string_view scene) const;
  bool noage(std::string_view scene) const;

 private:
  std::vector<TimeEntry> entries_;
  std::vector<TimeArea> areas_;
  std::vector<std::string> noage_;
  std::vector<std::string> flag_names_;
  std::vector<std::uint32_t> flag_hashes_;
};

extern const char* const kDefaultTimeTempleList;

// What in `list` does not agree with the story list and the item list, one
// line each (empty: all agree): an event of rule temple, travel, chain or
// scene that is not on the story list (its start never comes), one that is
// `solo` there; a sage that is not `solo` there; an item that is not a shared
// item; a flag that is the story list's already or one never shared (the age,
// the interface, Navi, the player's own state, talks).
std::vector<std::string> check_time_list(const TimeTempleList& list, const StoryList& story);

// ---- The rules -------------------------------------------------------------

struct TimeSettings {
  // The age follow is still held this long after an event of rule travel or
  // chain was last seen running here: the story's parts follow one another
  // with short gaps and scene loads between them. A load (no scene) inside
  // that time starts it again.
  std::uint64_t hold_after_us = 8'000'000;
};

// Where our Link is, when a mate's start comes.
struct TimeWhere {
  std::optional<std::string> scene;  // nothing: loading
  std::optional<protocol::Vec3> position;
};

struct TimeInput {
  std::uint64_t now_us = 0;  // any steady clock
  std::optional<std::string> scene;
  std::optional<bool> event_running;
  std::optional<std::string> event_name;  // ours, while one runs
  std::optional<std::string> story_asking;  // Story::asking(): the event we ask our game for
};

class TimeTemple {
 public:
  explicit TimeTemple(TimeTempleList list = TimeTempleList::defaults(), TimeSettings settings = {})
      : list_(std::move(list)), settings_(settings) {}

  // A STORY_EVENT from the server, before the story layer gets it. A start
  // this game does not take (rule temple or travel with our Link out of the
  // temple, rule chain) becomes `played` in place: the story layer then notes
  // that the room played it and does not ask our game for it. Returns the
  // line to log when the message is one of the list's starts, else nothing
  // (the message is left as it is).
  std::optional<std::string> filter(protocol::StoryEvent& m, const TimeWhere& where) const;

  // Once a frame: why the room's age follow (coop/host/age.hpp, D-R17) must
  // wait now, or nothing.
  std::optional<std::string> tick(const TimeInput& in);

  const TimeTempleList& list() const { return list_; }

 private:
  bool holds(std::string_view event) const;

  TimeTempleList list_;
  TimeSettings settings_;
  std::string last_event_;      // the travel or chain event seen last
  std::uint64_t last_seen_ = 0;  // when (0: none within hold_after_us)
};

}  // namespace coop::host
