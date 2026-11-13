#include "coop/host/time_temple.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "coop/host/game_items.hpp"

namespace coop::host {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && is_space(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

// A scene's Banc name: 1 to 64 of A-Z a-z 0-9 _ (the STORY_EVENT scene's limit).
bool valid_scene(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

// A flag token: 0x and 1 to 8 hex digits, or a printable ASCII name (as the
// story list's).
std::optional<std::uint32_t> flag_of(std::string_view token) {
  if (token.size() >= 3 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    const std::string_view digits = token.substr(2);
    if (digits.size() > 8) return std::nullopt;
    std::uint32_t hash = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (ec != std::errc() || end != digits.data() + digits.size() || hash == 0) return std::nullopt;
    return hash;
  }
  for (const char c : token) {
    if (c < 0x21 || c > 0x7e) return std::nullopt;
  }
  return protocol::flag_hash(token);
}

std::optional<float> number_of(std::string_view token) {
  const std::string s(token);
  if (s.empty()) return std::nullopt;
  char* end = nullptr;
  const float v = std::strtof(s.c_str(), &end);
  if (end != s.c_str() + s.size() || !std::isfinite(v) || std::fabs(v) > 1e6f) return std::nullopt;
  return v;
}

std::optional<TimeRule> rule_of(std::string_view word) {
  if (word == "temple") return TimeRule::temple;
  if (word == "travel") return TimeRule::travel;
  if (word == "chain") return TimeRule::chain;
  if (word == "sage") return TimeRule::sage;
  if (word == "scene") return TimeRule::scene;
  return std::nullopt;
}

std::string at(const protocol::Vec3& p) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", p[0], p[1], p[2]);
  return buf;
}

// Saved Bools the time temple's list must never share (as the story list's
// rule, oot-remake docs/HISTORIA_COMPLETA.md 2): the age is D-R17's, the rest
// is each player's own.
bool never_shared(std::string_view name) {
  constexpr std::string_view kNever[] = {"Sequence_IsChildhood", "UI_",        "System_",
                                         "PlayerStatus.",        "HaveTalked", "DreamWorld"};
  for (const auto prefix : kNever) {
    if (name.starts_with(prefix)) return true;
  }
  return name.find("Navi") != std::string_view::npos;
}

}  // namespace

const char* time_rule_name(TimeRule rule) {
  switch (rule) {
    case TimeRule::temple: return "temple";
    case TimeRule::travel: return "travel";
    case TimeRule::chain: return "chain";
    case TimeRule::sage: return "sage";
    case TimeRule::scene: return "scene";
  }
  return "?";
}

bool TimeArea::contains(const protocol::Vec3& p) const {
  for (std::size_t i = 0; i < 3; ++i) {
    if (!(p[i] >= min[i] && p[i] <= max[i])) return false;  // NaN is outside
  }
  return true;
}

// The areas: each scene's LocationArea "TimeTemple" placement (Market_Child:
// centre 108.45 103.5 -26.87, half sizes 48 3 18; Market_Adult: 111.61 103.5
// -34.0, 45 3 18), joined and widened to take the entrance's first-time
// trigger (x 71), the pedestal (x 137.6) and the Draw's request (x 147.1,
// y 110.5), read from Banc/Market/*_Static.bcett.byml.zs.
const char* const kDefaultTimeTempleList =
    "# ORM: the Temple of Time with several players (LICHII_ORM_STORY_EVENTS=1; LICHII_ORM_TIME=0\n"
    "# turns this off). The temple is a part of Market_Child and Market_Adult, the market square's\n"
    "# scenes: a story cutscene of the temple is taken only by the games whose Link is in the\n"
    "# temple, as in oot-64, where the temple is a scene of its own (D-068, D-187).\n"
    "#\n"
    "#   area SCENE MINX MINY MINZ MAXX MAXY MAXZ   the temple's box in that scene (metres)\n"
    "#   noage SCENE    the room's age is never followed there (D-R17)\n"
    "#   flag NAME      a saved Bool shared with the rest (the story list does not carry it)\n"
    "#   temple EVENT   a mate's start is taken only in the temple\n"
    "#   travel EVENT   the same, and it changes the age: the others follow by the reload\n"
    "#   chain EVENT [ITEM]  a later part of the sword's story: a mate's start is never taken\n"
    "#   sage EVENT ITEM     a sage's awakening, only for who warps (solo); ITEM is shared\n"
    "#   scene EVENT    every game in the scene follows (Sheik elsewhere)\n"
    "\n"
    "# The temple, read from the scenes' LocationArea TimeTemple, with a margin.\n"
    "area Market_Child 58 95 -56 160 125 -4\n"
    "area Market_Adult 58 95 -56 160 125 -4\n"
    "# The Chamber of Sages: no age in its name; its story takes Link out.\n"
    "noage SageRoom\n"
    "\n"
    "# ---- The Door of Time and the pedestal (child)\n"
    "# The temple, the first time (asked at the entrance).\n"
    "temple Dm_TimeTemple_0000\n"
    "# The Song of Time at the altar: the Door of Time opens (its flag is shared).\n"
    "temple Market_Child_DoorOfTime_OpenDoor\n"
    "# The Master Sword's pedestal, the first time; it sets the light shaft below.\n"
    "temple DoorOfTime_DiscoverySword\n"
    "flag Market_Child_TimeTemple_DeleteLightShaft_FirstTime_00\n"
    "# The pedestal room's Area is there while this is 0.\n"
    "flag Market_Child_FoundMasterSword\n"
    "\n"
    "# ---- Seven years: the sword out and back in\n"
    "# Drawing the sword the first time: the Sacred Realm, Rauru, back adult.\n"
    "travel Market_Child_DoorOfTime_Draw\n"
    "chain SageRoom_MeetGanondorf\n"
    "chain SageRoom_MeetRauru Item_Medal_Light\n"
    "# The later trips (the sword back in: child to adult; out: adult to child).\n"
    "travel Market_Child_Field_ChildToAdult\n"
    "travel Market_Adult_Field_AdultToChild\n"
    "flag Market_Adult_Firsttime_GobacktoChild\n"
    "\n"
    "# ---- Sheik in the temple (adult)\n"
    "# Sheik meets adult Link (after Rauru); it sets the first adult times.\n"
    "temple Market_Adult_AllSage_Start\n"
    "flag Market_Child_Firsttime_GotoAdult\n"
    "flag HrlField_HasEnteredFirstTime_Adult\n"
    "temple Dm_TimeTemple_0065\n"
    "# The Prelude of Light; Sheik is Zelda, the Light Arrows.\n"
    "temple Market_Adult_Field_GetLightPrelude\n"
    "temple Market_Adult_Field_GetArrowLight\n"
    "\n"
    "# ---- Sheik elsewhere: everyone in the scene (the story list's own rule)\n"
    "scene ForestTemple_SageOfForest_MelodyGet\n"
    "scene SageOfFire_MelodyGetFire\n"
    "scene SageOfWater_MelodyGet_Water\n"
    "scene KakarikoVillage_Arrive\n"
    "scene KakarikoVillage_FlashbackScenes\n"
    "scene KakarikoVillage_MelodyGet_Shadow\n"
    "scene Dm_MelodyGet_Spilit\n"
    "\n"
    "# ---- The sages after each adult temple: only who takes the blue warp; the medallion is shared\n"
    "sage ForestTemple_SageOfForest_SageRoom Item_Medal_Forest\n"
    "sage FireTemple_SageOfFire_SageRoom Item_Medal_Fire\n"
    "# Saria's or Darunia's awakening sets this (the first of the two).\n"
    "flag AllSage_AwakeSage_Fire_Or_Forest\n"
    "sage SageOfWater_SageRoom Item_Medal_Water\n"
    "sage ShadowTemple_SageOfShadow_SageRoom Item_Medal_Shadow\n"
    "sage SpiritTemple_SageOfSpirit_SageRoom Item_Medal_Spirit\n"
    "# After the last of Shadow and Spirit: Rauru and the sages (Dm_SageRoom_0010).\n"
    "flag HavePlayedEvent.SageRoom_Allsage_LastSage\n";

TimeTempleList TimeTempleList::defaults() {
  auto list = parse(kDefaultTimeTempleList);
  return list ? std::move(*list) : TimeTempleList{};
}

std::optional<TimeTempleList> TimeTempleList::parse(std::string_view text, std::string* error) {
  TimeTempleList out;
  std::size_t line_number = 0;
  auto fail = [&](const std::string& why) -> std::optional<TimeTempleList> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + why;
    return std::nullopt;
  };
  while (!text.empty()) {
    ++line_number;
    const std::size_t newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    const auto parts = tokens(line);
    if (parts.empty()) continue;
    const std::string_view word = parts[0];
    if (word == "area") {
      if (parts.size() != 8) return fail("area SCENE MINX MINY MINZ MAXX MAXY MAXZ");
      if (!valid_scene(parts[1])) return fail("a scene is 1 to 64 of A-Z a-z 0-9 _");
      if (out.area(parts[1])) return fail(std::string(parts[1]) + " has an area already");
      if (out.areas_.size() >= kMaxAreas) return fail("too many areas");
      TimeArea a;
      a.scene = std::string(parts[1]);
      for (std::size_t i = 0; i < 3; ++i) {
        const auto lo = number_of(parts[2 + i]);
        const auto hi = number_of(parts[5 + i]);
        if (!lo || !hi) return fail("an area's corner is six numbers");
        if (*lo > *hi) return fail("an area's first corner is its smallest");
        a.min[i] = *lo;
        a.max[i] = *hi;
      }
      out.areas_.push_back(std::move(a));
      continue;
    }
    if (word == "noage") {
      if (parts.size() != 2 || !valid_scene(parts[1])) return fail("noage SCENE");
      if (out.noage(parts[1])) return fail(std::string(parts[1]) + " is listed twice");
      if (out.noage_.size() >= kMaxAreas) return fail("too many noage scenes");
      out.noage_.emplace_back(parts[1]);
      continue;
    }
    if (word == "flag") {
      if (parts.size() != 2) return fail("flag NAME");
      const auto hash = flag_of(parts[1]);
      if (!hash) return fail("a flag is a printable name or 0x and 1 to 8 hex digits");
      if (std::find(out.flag_hashes_.begin(), out.flag_hashes_.end(), *hash) != out.flag_hashes_.end()) {
        return fail(std::string(parts[1]) + " is listed twice");
      }
      if (out.flag_hashes_.size() >= kMaxFlags) return fail("too many flags");
      out.flag_hashes_.push_back(*hash);
      out.flag_names_.emplace_back(parts[1]);
      continue;
    }
    const auto rule = rule_of(word);
    if (!rule) return fail("a line starts with area, noage, flag, temple, travel, chain, sage or scene");
    const bool takes_item = *rule == TimeRule::chain || *rule == TimeRule::sage;
    if (parts.size() < 2 || parts.size() > (takes_item ? 3u : 2u)) {
      return fail(std::string(word) + (takes_item ? " EVENT [ITEM]" : " EVENT"));
    }
    if (!valid_event_name(parts[1])) return fail("an event name is 1 to 127 of A-Z a-z 0-9 _");
    if (out.find(parts[1])) return fail(std::string(parts[1]) + " is listed twice");
    if (*rule == TimeRule::sage && parts.size() != 3) return fail("a sage line names its item");
    if (parts.size() == 3 && !valid_event_name(parts[2])) {
      return fail("an item is 1 to 127 of A-Z a-z 0-9 _ (Item_Medal_Forest)");
    }
    if (out.entries_.size() >= kMaxEntries) return fail("too many events");
    TimeEntry e;
    e.event = std::string(parts[1]);
    e.rule = *rule;
    if (parts.size() == 3) e.item = std::string(parts[2]);
    out.entries_.push_back(std::move(e));
  }
  return out;
}

std::optional<TimeTempleList> TimeTempleList::load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error);
}

const TimeEntry* TimeTempleList::find(std::string_view event) const {
  for (const auto& e : entries_) {
    if (e.event == event) return &e;
  }
  return nullptr;
}

const TimeArea* TimeTempleList::area(std::string_view scene) const {
  for (const auto& a : areas_) {
    if (a.scene == scene) return &a;
  }
  return nullptr;
}

bool TimeTempleList::noage(std::string_view scene) const {
  return std::find(noage_.begin(), noage_.end(), scene) != noage_.end();
}

std::vector<FlagKey> TimeTempleList::shared_flags() const {
  std::vector<FlagKey> keys;
  for (const auto hash : flag_hashes_) keys.push_back(FlagKey{hash, std::uint16_t{0}});
  std::sort(keys.begin(), keys.end());
  return keys;  // parse refused a flag twice
}

std::vector<std::string> check_time_list(const TimeTempleList& list, const StoryList& story) {
  std::vector<std::string> out;
  for (const auto& e : list.entries()) {
    const auto index = story.find(e.event);
    const bool solo = index && story.entries()[*index].solo;
    if (e.rule == TimeRule::sage) {
      if (!index) {
        out.push_back("sage " + e.event + " is not on the story list (its flags are not shared)");
      } else if (!solo) {
        out.push_back("sage " + e.event + " is not solo on the story list: every game in its scene would see it");
      }
    } else if (!index) {
      out.push_back(std::string(time_rule_name(e.rule)) + " " + e.event +
                    " is not on the story list: no start of it ever comes");
    } else if (solo) {
      out.push_back(std::string(time_rule_name(e.rule)) + " " + e.event +
                    " is solo on the story list: no start of it ever comes");
    }
    if (!e.item.empty()) {
      const ItemFlag* item = find_item(protocol::flag_hash("IsGet." + e.item));
      if (item == nullptr || item->rule == ItemRule::per_player || !item->on) {
        out.push_back(e.event + "'s item IsGet." + e.item + " is not a shared item");
      }
    }
  }
  const auto story_flags = story.shared_flags();
  for (std::size_t i = 0; i < list.flag_names().size(); ++i) {
    const std::string& name = list.flag_names()[i];
    if (never_shared(name)) out.push_back("flag " + name + " is never shared (each player's own)");
    const auto hash = flag_of(name);
    if (hash && std::binary_search(story_flags.begin(), story_flags.end(), FlagKey{*hash, std::uint16_t{0}})) {
      out.push_back("flag " + name + " is the story list's already");
    }
  }
  return out;
}

// ---- TimeTemple ------------------------------------------------------------

bool TimeTemple::holds(std::string_view event) const {
  const TimeEntry* e = list_.find(event);
  return e != nullptr && (e->rule == TimeRule::travel || e->rule == TimeRule::chain);
}

std::optional<std::string> TimeTemple::filter(protocol::StoryEvent& m, const TimeWhere& where) const {
  if (m.kind == protocol::StoryKind::skipped) {
    // ORM D-048: the owner skipped it. A part of the sword's story is each
    // traveller's own: never cut there (a plain end).
    const TimeEntry* c = list_.find(m.name);
    if (c == nullptr || c->rule != TimeRule::chain) return std::nullopt;
    m.kind = protocol::StoryKind::end;
    return "P" + std::to_string(m.player_number) + " skipped '" + m.name +
           "': a part of the sword's story, each traveller's own; not skipped here";
  }
  if (m.kind != protocol::StoryKind::start) return std::nullopt;
  const TimeEntry* e = list_.find(m.name);
  if (e == nullptr) return std::nullopt;
  const std::string who =
      "P" + std::to_string(m.player_number) + "'s start of '" + m.name + "' in " + m.scene;
  switch (e->rule) {
    case TimeRule::sage:
    case TimeRule::scene:
      return std::nullopt;  // the story layer's own rule (solo, the scene)
    case TimeRule::chain:
      m.kind = protocol::StoryKind::played;
      return who + ": not taken, a part of the sword's story (each traveller's own game plays it after "
                   "its own draw)";
    case TimeRule::temple:
    case TimeRule::travel:
      break;
  }
  // Loading or in another scene: the story layer says why it does not take it.
  if (!where.scene || *where.scene != m.scene) return std::nullopt;
  const TimeArea* area = list_.area(m.scene);
  if (area == nullptr) return std::nullopt;  // no temple in that scene: the whole scene, as ever
  const char* then = e->rule == TimeRule::travel ? "; our age follows the room's by the reload (D-R17)"
                                                 : "; its flags come all the same";
  if (!where.position) {
    m.kind = protocol::StoryKind::played;
    return who + ": not taken, where our Link is cannot be read" + then;
  }
  if (!area->contains(*where.position)) {
    m.kind = protocol::StoryKind::played;
    return who + ": not taken, our Link is not in the Temple of Time (at " + at(*where.position) + ")" + then;
  }
  return who + ": our Link is in the Temple of Time (at " + at(*where.position) + "): the story may take it";
}

std::optional<std::string> TimeTemple::tick(const TimeInput& in) {
  const std::uint64_t now = std::max<std::uint64_t>(in.now_us, 1);
  if (in.story_asking && holds(*in.story_asking)) {
    return "asking our game for '" + *in.story_asking + "' (a mate's " +
           time_rule_name(list_.find(*in.story_asking)->rule) + ")";
  }
  if (in.event_running.value_or(false) && in.event_name && holds(*in.event_name)) {
    last_event_ = *in.event_name;
    last_seen_ = now;
    return "our game runs '" + last_event_ + "' (" + time_rule_name(list_.find(last_event_)->rule) + ")";
  }
  if (in.scene && list_.noage(*in.scene)) {
    return "in " + *in.scene + " (no age in its name; its story takes Link out on its own)";
  }
  if (last_seen_ != 0) {
    if (!in.scene) {
      last_seen_ = now;  // a load between the story's parts
      return "loading after '" + last_event_ + "'";
    }
    if (now < last_seen_ || now - last_seen_ < settings_.hold_after_us) {
      return "'" + last_event_ + "' ran here a moment ago; its story may go on";
    }
    last_seen_ = 0;
    last_event_.clear();
  }
  return std::nullopt;
}

}  // namespace coop::host
