// Every content list of the kit (tools/release/kit/shared_*.txt) through the real readers, together
// (oot-remake docs/LISTAS_KIT.md). Each list has its own test (dungeon_lists_test, adult_list_test,
// minigame_list_test, story_test, flag_defer_test, item_rules_test, final_rules_test); this one checks
// what none of them sees alone:
// - every shared_*.txt of the kit folder has a reader and parses (a new list with no reader fails here);
// - the dungeon lists coop-options.ini turns on load together, in its order (DungeonList::load_many
//   refuses the whole set on one disagreement, and then the hook uses no dungeon list at all);
// - no flag is given two different rules by two lists; the message says which rule the hook keeps
//   (Coop::Begin's order: items and LICHII_COOP_FLAGS, the built-in lists, the story list, the dungeon
//   lists in the ini's order, water, the talk list). Rules that stack (a talk wait on a shared flag,
//   once: over a story Bool, a counted Bool shared) are allowed and reported;
// - no list but shared_items.txt names a flag of the item table (its rule is the item table's);
// - coop-options.ini turns on every list of the folder, by the switch of its reader, except the few
//   off on purpose (kOffOnPurpose, each with its reason), and names no list that is not there.
// The names themselves are checked against the romfs by oot-remake/tools/romfs/test_kit_lists.py
// (C++ tests have no game data).
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/dungeon_lists.hpp"
#include "coop/host/final_rules.hpp"
#include "coop/host/flag_defer.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/item_rules.hpp"
#include "coop/host/story.hpp"
#include "coop/host/time_temple.hpp"

namespace host = coop::host;
using host::FlagKey;
using host::FlagKeyKind;

namespace {

const std::filesystem::path kKit = std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" / "kit";

enum class Reader { dungeon, story, talk, items, counts, time, none };

Reader reader_of(const std::string& file) {
  if (file.starts_with("shared_dungeon_")) return Reader::dungeon;
  if (file == "shared_story.txt") return Reader::story;
  if (file == "shared_talk.txt") return Reader::talk;
  if (file == "shared_items.txt") return Reader::items;
  if (file == "shared_counts.txt") return Reader::counts;
  if (file == "shared_time_temple.txt") return Reader::time;
  return Reader::none;
}

// The switches that take each reader's file (the second name is the one before test9, coop_switch.h).
std::vector<std::string_view> switches_of(Reader r) {
  switch (r) {
    case Reader::dungeon: return {"LICHII_COOP_DUNGEON_LISTS"};
    case Reader::story: return {"LICHII_ORM_STORY_LIST", "LICHII_COOP_STORY_LIST"};
    case Reader::talk: return {"LICHII_COOP_TALK"};
    case Reader::items: return {"LICHII_ORM_ITEM_RULES", "LICHII_COOP_ITEM_RULES"};
    case Reader::counts: return {"LICHII_ORM_COUNTS", "LICHII_COOP_COUNTS"};
    case Reader::time: return {"LICHII_ORM_TIME_LIST"};
    case Reader::none: break;
  }
  return {};
}

// Lists the kit ships but does not turn on, and why. `commented`: the ini still names it on a line
// with '#' in front, so a player can turn it on.
struct OffOnPurpose {
  std::string_view file;
  bool commented;
  std::string_view why;
};
constexpr OffOnPurpose kOffOnPurpose[] = {
    {"shared_story.txt", true,
     "the hook has the same list built in (story_test: 'the kit's shared_story.txt is the built-in list'); "
     "a file the emulator cannot find would turn the story events off instead"},
    {"shared_time_temple.txt", true,
     "the hook has the same Temple of Time rules built in (time_temple.hpp); the file is for changing them "
     "(TEMPLO_TIEMPO.md)"},
};

const OffOnPurpose* off_on_purpose(std::string_view file) {
  for (const auto& o : kOffOnPurpose) {
    if (o.file == file) return &o;
  }
  return nullptr;
}

std::vector<std::string> kit_lists() {
  std::vector<std::string> out;
  for (const auto& e : std::filesystem::directory_iterator(kKit)) {
    const std::string name = e.path().filename().string();
    if (e.is_regular_file() && name.starts_with("shared_") && name.ends_with(".txt")) out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), path.string());
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

std::string trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return std::string(s);
}

std::vector<std::string> split_commas(std::string_view value) {
  std::vector<std::string> out;
  while (!value.empty()) {
    const std::size_t comma = value.find(',');
    if (std::string one = trim(value.substr(0, comma)); !one.empty()) out.push_back(std::move(one));
    value = comma == std::string_view::npos ? std::string_view() : value.substr(comma + 1);
  }
  return out;
}

// coop-options.ini's switch lines: NAME=value, on (no '#') or commented ('#' in front), as
// kit-paths.ps1 reads them (LICHII_COOP_, LICHII_ORM_, EDEN_NX2_ names).
struct Ini {
  std::map<std::string, std::string> on;
  std::multimap<std::string, std::string> commented;
};

Ini read_ini() {
  Ini ini;
  std::istringstream lines(read_text(kKit / "coop-options.ini"));
  for (std::string raw; std::getline(lines, raw);) {
    std::string t = trim(raw);
    bool off = false;
    if (t.starts_with('#')) {
      off = true;
      t = trim(std::string_view(t).substr(1));
    }
    if (!(t.starts_with("LICHII_COOP_") || t.starts_with("LICHII_ORM_") || t.starts_with("EDEN_NX2_"))) continue;
    const std::size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string name = trim(std::string_view(t).substr(0, eq));
    std::string value = trim(std::string_view(t).substr(eq + 1));
    if (name.find(' ') != std::string::npos) continue;  // prose that happens to start with a name
    if (const std::size_t space = value.find_first_of(" \t"); space != std::string::npos) {
      value.resize(space);  // "LICHII_COOP_PANEL=1   shows ..."
    }
    if (off) {
      ini.commented.emplace(std::move(name), std::move(value));
    } else {
      ini.on[std::move(name)] = std::move(value);
    }
  }
  return ini;
}

// ---- One flag, every list's rule for it --------------------------------------------------------

struct Rule {
  std::string file;
  std::string kind;    // share, key64, once, hold, off, enum, step, talk, now, count, counted, item
  std::string detail;  // a hold's place, an Enum's values, an item rule
  std::string name;    // as the list writes it (for the messages)
  bool same(const Rule& o) const { return kind == o.kind && detail == o.detail; }
};

std::string hex(std::uint32_t hash) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(hash));
  return text;
}

std::string key_text(FlagKey key) {
  char text[40];
  if (key.kind == FlagKeyKind::key64) {
    std::snprintf(text, sizeof text, "key64:0x%016llx", static_cast<unsigned long long>(key.key));
    return text;
  }
  return hex(key.hash) + (key.index ? " " + std::to_string(key.index) : "");
}

// The names a dungeon list writes, by hash (the reader keeps only hashes for most lines).
std::map<std::uint32_t, std::string> dungeon_names(const std::string& text) {
  std::map<std::uint32_t, std::string> out;
  std::istringstream lines(text);
  for (std::string raw; std::getline(lines, raw);) {
    if (const auto hash = raw.find('#'); hash != std::string::npos) raw.resize(hash);
    std::istringstream words(raw);
    std::string w;
    if (!(words >> w)) continue;
    for (std::string_view p : {"once:", "unshare:", "held:", "hold:", "enum:"}) {
      if (w.starts_with(p)) {
        w = w.substr(p.size());
        break;
      }
    }
    if (!w.starts_with("key64:") && !w.starts_with("0x")) out[host::flag_hash(w)] = w;
  }
  return out;
}

std::string values_text(const std::vector<std::uint32_t>& values, bool steps) {
  std::string out = steps ? "steps" : "values";
  for (const auto v : values) out += " " + hex(v);
  return out;
}

using Rules = std::map<FlagKey, std::vector<Rule>>;

void add(Rules& rules, FlagKey key, Rule rule) { rules[key].push_back(std::move(rule)); }

FlagKey named(std::uint32_t hash) { return FlagKey(hash, std::uint16_t{0}); }

void add_dungeon(Rules& rules, const std::string& file, const host::DungeonList& list, const std::string& text) {
  const auto names = dungeon_names(text);
  auto name_of = [&](FlagKey key) {
    if (key.kind == FlagKeyKind::named) {
      if (const auto it = names.find(key.hash); it != names.end()) return it->second;
    }
    return key_text(key);
  };
  for (const auto& k : list.share()) {
    add(rules, k, {file, k.kind == FlagKeyKind::key64 ? "key64" : "share", "", name_of(k)});
  }
  for (const auto& k : list.share_enums().keys()) {
    add(rules, k,
        {file, "enum", values_text(*list.share_enums().values(k.hash), list.share_enums().steps(k.hash)), name_of(k)});
  }
  for (const auto& k : list.once()) add(rules, k, {file, "once", "", name_of(k)});
  for (const auto& h : list.holds()) add(rules, named(h.hash), {file, "hold", h.place, h.name});
  for (const auto& k : list.unshare()) add(rules, k, {file, "off", "", name_of(k)});
}

void add_story(Rules& rules, const std::string& file, const host::StoryList& list) {
  for (const auto& e : list.entries()) {
    add(rules, named(e.played_hash), {file, "share", "", host::played_flag_name(e.name)});
    for (std::size_t i = 0; i < e.flags.size(); ++i) {
      add(rules, e.flags[i], {file, "share", "", i < e.flag_names.size() ? e.flag_names[i] : key_text(e.flags[i])});
    }
    for (const auto& s : e.steps) add(rules, named(s.hash), {file, "step", values_text(s.values, true), s.name});
  }
}

void add_talk(Rules& rules, const std::string& file, const host::DeferRules& list) {
  for (const auto& k : list.keys()) {
    const std::string_view name = list.name(k);
    add(rules, k, {file, list.find(k) ? "talk" : "now", "", name.empty() ? key_text(k) : std::string(name)});
  }
}

void add_items(Rules& rules, const std::string& file, const host::ItemRules& list) {
  for (const auto& r : list.rules()) {
    add(rules, named(r.hash), {file, "item", std::string(host::item_rule_name(r.rule)), r.name});
  }
  for (const auto& c : list.caps()) add(rules, named(c.cap), {file, "item", "cap", hex(c.cap)});
  for (const auto& f : list.fills()) add(rules, named(f.to), {file, "item", "filled", hex(f.to)});
}

void add_counts(Rules& rules, const std::string& file, const host::CountList& list) {
  for (const auto& e : list.entries()) {
    add(rules, named(e.hash), {file, "count", "", e.name});
    for (std::size_t i = 0; i < e.of.size(); ++i) {
      add(rules, named(e.of[i]), {file, "counted", "", i < e.of_names.size() ? e.of_names[i] : hex(e.of[i])});
    }
  }
}

// Every list of the folder, read by its reader (REQUIREs each parses).
Rules all_rules() {
  Rules rules;
  for (const auto& file : kit_lists()) {
    const auto path = kKit / file;
    std::string error;
    INFO(file);
    switch (reader_of(file)) {
      case Reader::dungeon: {
        const auto list = host::DungeonList::load(path, &error);
        INFO(error);
        REQUIRE(list);
        add_dungeon(rules, file, *list, read_text(path));
        break;
      }
      case Reader::story: {
        const auto list = host::StoryList::load(path, &error);
        INFO(error);
        REQUIRE(list);
        add_story(rules, file, *list);
        break;
      }
      case Reader::talk: {
        const auto list = host::DeferRules::load(path, &error);
        INFO(error);
        REQUIRE(list);
        add_talk(rules, file, *list);
        break;
      }
      case Reader::items: {
        const auto list = host::ItemRules::load(path, &error);
        INFO(error);
        REQUIRE(list);
        add_items(rules, file, *list);
        break;
      }
      case Reader::counts: {
        const auto list = host::CountList::load(path, &error);
        INFO(error);
        REQUIRE(list);
        add_counts(rules, file, *list);
        break;
      }
      case Reader::time: {
        // Parsed only: its 'flag' lines are shared Bools of the temple's own glue (TEMPLO_TIEMPO.md).
        const auto list = host::TimeTempleList::load(path, &error);
        INFO(error);
        REQUIRE(list);
        break;
      }
      case Reader::none: FAIL_CHECK("no reader takes this list: " << file); break;
    }
  }
  return rules;
}

bool is(const Rule& r, std::string_view kind) { return r.kind == kind; }
bool dungeon(const Rule& r) { return reader_of(r.file) == Reader::dungeon; }
bool shares(const Rule& r) { return is(r, "share") || is(r, "talk") || is(r, "now"); }

// Two different rules that stack: both apply, and the message says how.
std::optional<std::string> stacked(const Rule& a, const Rule& b) {
  if (dungeon(a) && dungeon(b)) return std::nullopt;  // the dungeon reader takes one rule a flag
  for (const auto& [x, y] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
    if (is(*x, "share") && is(*y, "talk")) {
      return "both: shared live, and " + y->file + "'s wait holds a value while its townsperson is seen";
    }
    if (is(*x, "share") && is(*y, "now")) {
      return "both: shared live, and " + y->file + "'s 'now' line keeps the built-in talk rules from holding it";
    }
    if (is(*x, "once") && shares(*y) && !dungeon(*y)) {
      return x->file + "'s once: (shared, and the pickup rule never undoes a 1)";
    }
    if (is(*x, "share") && is(*y, "hold") && dungeon(*y)) {
      return "both: shared, and " + y->file + "'s hold keeps a mate's value from being written while we are " +
             "in " + y->detail;
    }
    if (is(*x, "counted") && (is(*y, "share") || is(*y, "once"))) {
      return "both: " + y->file + " shares the Bool the count needs shared";
    }
  }
  return std::nullopt;
}

// Which rule the hook keeps when two clash.
std::string winner(const Rule& a, const Rule& b) {
  if (dungeon(a) && dungeon(b)) {
    return "neither: DungeonList::load_many refuses the set, so the hook uses no dungeon list at all";
  }
  for (const auto& [x, y] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
    if (is(*x, "item")) return y->file + " (it changes the shared list the item table builds; the item rule is lost)";
    if (is(*x, "off") && (is(*y, "talk") || is(*y, "now"))) {
      return y->file + " (the talk list comes after the dungeon lists: shared again)";
    }
    if (is(*x, "off") && is(*y, "step")) return y->file + " (an Enum a list opts in is never taken off)";
    if (is(*x, "off") && reader_of(y->file) == Reader::story) {
      return x->file + " (taken off after the story list added it)";
    }
    if (is(*x, "off") && (is(*y, "count") || is(*y, "counted"))) {
      return x->file + " (taken off: the count would not see it)";
    }
    if (is(*x, "step") && is(*y, "enum")) {
      return x->file + " (apply_dungeon_list keeps the story's Enum rule and counts an Enum conflict)";
    }
  }
  return "both are applied: check the pair by hand";
}

}  // namespace

TEST_CASE("kit lists: every list of the kit folder has a reader and parses") {
  const auto files = kit_lists();
  REQUIRE_FALSE(files.empty());
  const auto rules = all_rules();
  std::set<std::string> with_lines;
  for (const auto& entry : rules) {
    for (const auto& r : entry.second) with_lines.insert(r.file);
  }
  for (const auto& file : files) {
    if (reader_of(file) == Reader::time) continue;  // parsed only (all_rules): no flag rule of its own
    CHECK_MESSAGE(with_lines.contains(file), "no flag in " << file);
  }
}

TEST_CASE("kit lists: no flag has two different rules in two lists") {
  const auto rules = all_rules();
  std::size_t clashes = 0, stacks = 0;
  for (const auto& entry : rules) {  // no structured bindings: doctest's macros are lambdas
    const FlagKey key = entry.first;
    const std::vector<Rule>& list = entry.second;
    for (std::size_t i = 0; i < list.size(); ++i) {
      for (std::size_t j = i + 1; j < list.size(); ++j) {
        const Rule& a = list[i];
        const Rule& b = list[j];
        if (a.file == b.file || a.same(b)) continue;  // one list's reader decides; the same rule twice is fine
        const std::string pair = a.name + " (" + key_text(key) + "): " + a.file + " says " + a.kind +
                                 (a.detail.empty() ? "" : " " + a.detail) + ", " + b.file + " says " + b.kind +
                                 (b.detail.empty() ? "" : " " + b.detail);
        if (const auto how = stacked(a, b)) {
          ++stacks;
          MESSAGE(pair << "; the hook keeps " << *how);
          continue;
        }
        ++clashes;
        FAIL_CHECK(pair << "; the hook keeps " << winner(a, b));
      }
    }
  }
  CHECK(clashes == 0);
  MESSAGE(stacks << " flags with rules that stack");
}

TEST_CASE("kit lists: only shared_items.txt names a flag of the item table") {
  const auto rules = all_rules();
  for (const auto& entry : rules) {
    const FlagKey key = entry.first;
    if (key.kind != FlagKeyKind::named || !host::built_in_item_rule(key.hash)) continue;
    for (const auto& r : entry.second) {
      if (reader_of(r.file) == Reader::items) continue;
      // The three trade items with a timer: the trades list takes them off the item table on
      // purpose, so each stays in the game that got it (BOTELLAS_TRUEQUES.md, 3).
      if (r.file == "shared_dungeon_trades.txt" && is(r, "off") &&
          (r.name == "PouchData.Item_OddMushroom.StockNum" || r.name == "PouchData.Item_EyeballFrog.StockNum" ||
           r.name == "PouchData.Item_EyeDrop.StockNum")) {
        MESSAGE(r.name << ": " << r.file << "'s unshare on purpose (a timed trade item stays with its receiver)");
        continue;
      }
      FAIL_CHECK(r.name << " is an item flag (docs/items.md) and " << r.file << " says " << r.kind
                        << "; the hook keeps " << winner(r, Rule{"shared_items.txt", "item", "", r.name}));
    }
  }
}

TEST_CASE("kit lists: coop-options.ini turns every list on, by its reader's switch") {
  const Ini ini = read_ini();
  const auto files = kit_lists();
  const std::set<std::string> present(files.begin(), files.end());

  // What each switch that is on names, entry by entry ('.' is every shared_dungeon_*.txt here).
  std::map<std::string, std::string> on_by;  // file -> switch
  for (const auto& sw : ini.on) {
    const std::string& name = sw.first;
    for (const auto& entry : split_commas(sw.second)) {
      if (entry == "." && name == "LICHII_COOP_DUNGEON_LISTS") {
        for (const auto& f : files) {
          if (reader_of(f) == Reader::dungeon) on_by[f] = name;
        }
        continue;
      }
      if (!entry.starts_with("shared_")) continue;
      CHECK_MESSAGE(present.contains(entry), name << " names " << entry << ", which is not in the kit folder");
      on_by[entry] = name;
    }
  }
  std::set<std::string> commented;
  for (const auto& sw : ini.commented) {
    for (const auto& entry : split_commas(sw.second)) commented.insert(entry);
  }

  for (const auto& file : files) {
    INFO(file);
    const Reader r = reader_of(file);
    const auto sw = switches_of(r);
    if (const auto* off = off_on_purpose(file)) {
      CHECK_MESSAGE(!on_by.contains(file), file << " is on now: take it off kOffOnPurpose (" << off->why << ")");
      if (off->commented) {
        CHECK_MESSAGE(commented.contains(file), file << ": no '# SWITCH=" << file << "' line");
      }
      MESSAGE(file << " is off on purpose: " << off->why);
      continue;
    }
    if (!on_by.contains(file)) {
      FAIL_CHECK(file << " is in the kit but no switch that is on names it");
      continue;
    }
    const std::string& by = on_by.at(file);
    CHECK_MESSAGE(std::find(sw.begin(), sw.end(), by) != sw.end(),
                  file << " is named by " << by << ", which is not its reader's switch");
  }
}

TEST_CASE("kit lists: the dungeon lists the ini turns on load together, in its order") {
  const Ini ini = read_ini();
  const auto it = ini.on.find("LICHII_COOP_DUNGEON_LISTS");
  REQUIRE(it != ini.on.end());
  // As kit-paths.ps1 hands them to the emulator: each entry as a path of the kit folder.
  std::string paths;
  for (const auto& entry : split_commas(it->second)) {
    paths += (paths.empty() ? "" : ",") + (kKit / entry).string();
  }
  std::string error;
  const auto all = host::DungeonList::load_many(paths, &error);
  INFO(error);
  REQUIRE(all);

  // The same with the built-in lists under them: nothing an earlier list opts in as an Enum is taken
  // off, and no Enum line disagrees with an earlier one (the hook logs both counts).
  std::optional<host::SharedFlags> shared = host::item_shared_flags();
  const auto story = host::StoryList::load(kKit / "shared_story.txt", &error);
  REQUIRE(story);
  {
    std::vector<FlagKey> keys = shared->keys();
    const auto mine = story->shared_flags();
    keys.insert(keys.end(), mine.begin(), mine.end());
    shared = host::SharedFlags(std::move(keys), story->step_flags());
  }
  const auto report = host::apply_dungeon_list(shared, *all);
  CHECK(report.enum_conflicts == 0);
  CHECK(report.enum_kept == 0);
  MESSAGE("dungeon lists: " << report.added << " added, " << report.removed << " taken off, " << report.once
                            << " once, " << report.held << " held, " << report.pickups << " one-time keys");
}
