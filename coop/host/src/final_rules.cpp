// ORM: the end of the game with up to six players: counted Ints, the escape's clock, the
//   ending's age, Twinrova's owner. The rules are described in coop/host/final_rules.hpp.
// Doc: oot-remake docs/FINAL.md.
// Switches: LICHII_ORM_COUNTS (the kit's shared_counts.txt), LICHII_ORM_ESCAPE_CLOCK (and _WORD,
//   _LENGTH_S), LICHII_ORM_TWINROVA_KEEP; the hook glue is lichii/hook/coop_final.{h,cpp}, the
//   ending's age is in coop_age_game.cpp.
// Test: tests/host/final_rules_test.cpp.

#include "coop/host/final_rules.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "coop/host/game_flags.hpp"

namespace coop::host {
namespace {

constexpr std::string_view kCount = "count:";

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

// A flag token: 0x and 1 to 8 hex digits (not 0), or a printable ASCII name.
std::optional<std::uint32_t> flag_of(std::string_view token) {
  if (token.empty()) return std::nullopt;
  if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    const std::string_view digits = token.substr(2);
    if (digits.empty() || digits.size() > 8) return std::nullopt;
    std::uint32_t hash = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (ec != std::errc() || end != digits.data() + digits.size() || hash == 0) return std::nullopt;
    return hash;
  }
  for (const char c : token) {
    if (c < 0x21 || c > 0x7e) return std::nullopt;
  }
  return flag_hash(token);
}

}  // namespace

// ---- Counted Ints --------------------------------------------------------------------------

std::optional<CountList> CountList::parse(std::string_view text, std::string* error) {
  CountList list;
  std::size_t line_number = 0;
  auto fail = [&](const std::string& why) -> std::optional<CountList> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + why;
    return std::nullopt;
  };
  auto counted = [&](std::uint32_t hash) {
    return std::any_of(list.entries_.begin(), list.entries_.end(),
                       [&](const CountEntry& e) { return e.hash == hash; });
  };
  auto counting = [&](std::uint32_t hash) {
    return std::any_of(list.entries_.begin(), list.entries_.end(), [&](const CountEntry& e) {
      return std::find(e.of.begin(), e.of.end(), hash) != e.of.end();
    });
  };
  while (!text.empty()) {
    ++line_number;
    const std::size_t newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    const auto parts = tokens(line);
    if (parts.empty()) continue;
    if (!parts[0].starts_with(kCount)) return fail("a line is count:IntFlag BoolFlag ...");
    CountEntry entry;
    entry.name = std::string(parts[0].substr(kCount.size()));
    const auto hash = flag_of(entry.name);
    if (!hash) return fail("a flag is a printable name or 0x and 1 to 8 hex digits");
    entry.hash = *hash;
    if (counted(entry.hash)) return fail(entry.name + " is counted twice");
    if (counting(entry.hash)) return fail(entry.name + " is counted and counts");
    if (parts.size() < 2) return fail(entry.name + " counts no Bool");
    if (parts.size() - 1 > kMaxBools) return fail(entry.name + " counts more than 32 Bools");
    for (std::size_t i = 1; i < parts.size(); ++i) {
      const auto bool_hash = flag_of(parts[i]);
      if (!bool_hash) return fail("a flag is a printable name or 0x and 1 to 8 hex digits");
      if (*bool_hash == entry.hash || counted(*bool_hash)) {
        return fail(std::string(parts[i]) + " is counted and counts");
      }
      if (std::find(entry.of.begin(), entry.of.end(), *bool_hash) != entry.of.end()) {
        return fail(std::string(parts[i]) + " is listed twice on " + entry.name + "'s line");
      }
      entry.of.push_back(*bool_hash);
      entry.of_names.emplace_back(parts[i]);
    }
    list.entries_.push_back(std::move(entry));
  }
  return list;
}

std::optional<CountList> CountList::load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  std::string why;
  auto list = parse(text.str(), &why);
  if (!list && error) *error = path.string() + ": " + why;
  return list;
}

std::vector<FlagKey> CountList::keys() const {
  std::vector<FlagKey> keys;
  for (const auto& e : entries_) {
    keys.push_back(FlagKey{e.hash, std::uint16_t{0}});
    for (const auto h : e.of) keys.push_back(FlagKey{h, std::uint16_t{0}});
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

std::vector<CountFix> count_fixes(GameMemory& memory, const CountList& list) {
  std::vector<CountFix> out;
  GameFlags flags(memory);
  for (std::size_t i = 0; i < list.entries().size(); ++i) {
    const auto& e = list.entries()[i];
    const auto now = flags.get_int(e.hash);
    if (!now) continue;
    std::int32_t on = 0;
    bool readable = true;
    for (const auto h : e.of) {
      const auto b = flags.get_bool(h);
      if (!b) {
        readable = false;
        break;
      }
      if (*b) ++on;
    }
    if (!readable || *now >= on) continue;  // never lowered: the game may count more ways
    out.push_back(CountFix{i, *now, on});
  }
  return out;
}

std::vector<std::string> apply_counts(WritableGameMemory& memory, const CountList& list) {
  std::vector<std::string> log;
  for (const auto& fix : count_fixes(memory, list)) {
    const auto& e = list.entries()[fix.entry];
    const std::string what = e.name + " " + std::to_string(fix.from) + " -> " + std::to_string(fix.to) + " (" +
                             std::to_string(fix.to) + " of " + std::to_string(e.of.size()) + " set)";
    if (GameFlags::set_int(memory, e.hash, fix.to)) {
      log.push_back("count: " + what);
    } else {
      log.push_back("count: " + what + ": the game's queue refused it; again next time");
    }
  }
  return log;
}

// ---- The escape's clock ----------------------------------------------------------------------

namespace {

// 0x and 1 to 16 hex digits.
std::optional<std::uint64_t> hex_of(std::string_view token) {
  if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) return std::nullopt;
  const std::string_view digits = token.substr(2);
  if (digits.size() > 16) return std::nullopt;
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
  if (ec != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
  return value;
}

}  // namespace

std::optional<CountdownWord> CountdownWord::parse(std::string_view text, std::string* error) {
  auto fail = [&](const char* why) -> std::optional<CountdownWord> {
    if (error) *error = why;
    return std::nullopt;
  };
  while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
  while (!text.empty() && (is_space(text.back()) || text.back() == '\n')) text.remove_suffix(1);
  const std::size_t colon = text.rfind(':');
  if (colon == std::string_view::npos) return fail("a countdown word is main+0x<hex>[,0x<hex>...]:f32|u32|ms|mmss");
  CountdownWord word;
  const std::string_view unit = text.substr(colon + 1);
  if (unit == "f32") {
    word.unit = Unit::f32_seconds;
  } else if (unit == "u32") {
    word.unit = Unit::u32_seconds;
  } else if (unit == "ms") {
    word.unit = Unit::u32_ms;
  } else if (unit == "mmss") {
    word.unit = Unit::u32_mmss;
  } else {
    return fail("the unit is f32 (seconds), u32 (seconds), ms (milliseconds) or mmss (minutes and seconds)");
  }
  std::string_view path = text.substr(0, colon);
  constexpr std::string_view kMain = "main+";
  if (!path.starts_with(kMain)) return fail("a countdown word starts with main+0x<hex>");
  path.remove_prefix(kMain.size());
  bool first = true;
  while (true) {
    const std::size_t comma = path.find(',');
    const std::string_view token = path.substr(0, comma);
    const auto value = hex_of(token);
    if (!value) return fail("an offset is 0x and 1 to 16 hex digits");
    if (first) {
      word.base = *value;
      first = false;
    } else {
      if (word.offsets.size() == kMaxSteps) return fail("at most 8 pointer steps");
      word.offsets.push_back(*value);
    }
    if (comma == std::string_view::npos) break;
    path.remove_prefix(comma + 1);
  }
  return word;
}

std::optional<std::uint64_t> CountdownWord::address(GameMemory& memory) const {
  std::uint64_t at = memory.main_base() + base;
  for (const std::uint64_t offset : offsets) {
    const auto pointer = read_ptr(memory, at);
    if (!pointer) return std::nullopt;
    at = *pointer + offset;
  }
  return at;
}

std::optional<std::uint32_t> CountdownWord::read_seconds(GameMemory& memory) const {
  const auto at = address(memory);
  if (!at) return std::nullopt;
  if (unit == Unit::f32_seconds) {
    const auto value = read_f32(memory, *at);
    if (!value || !std::isfinite(*value) || *value < 0.0f || *value > static_cast<float>(kMaxSeconds)) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);  // rounded down
  }
  if (unit == Unit::u32_mmss) {
    const auto minutes = read_u32(memory, *at);
    const auto secs = read_u32(memory, *at + 4);
    if (!minutes || !secs || *secs > 59 || *minutes > kMaxSeconds / 60) return std::nullopt;
    return *minutes * 60 + *secs;
  }
  const auto value = read_u32(memory, *at);
  if (!value) return std::nullopt;
  const std::uint32_t seconds = unit == Unit::u32_ms ? *value / 1000 : *value;
  if (seconds > kMaxSeconds) return std::nullopt;
  return seconds;
}

bool CountdownWord::write_seconds(WritableGameMemory& memory, std::uint32_t seconds) const {
  const auto at = address(memory);
  if (!at || seconds > kMaxSeconds) return false;
  if (unit == Unit::u32_mmss) {
    const std::uint32_t m = seconds / 60, sec = seconds % 60;
    const std::uint8_t bytes[8] = {static_cast<std::uint8_t>(m),       static_cast<std::uint8_t>(m >> 8),
                                   static_cast<std::uint8_t>(m >> 16), static_cast<std::uint8_t>(m >> 24),
                                   static_cast<std::uint8_t>(sec),     0, 0, 0};
    return memory.write(*at, bytes);
  }
  std::uint32_t raw = seconds;
  if (unit == Unit::f32_seconds) {
    const float value = static_cast<float>(seconds);
    std::memcpy(&raw, &value, sizeof raw);
  } else if (unit == Unit::u32_ms) {
    raw = seconds * 1000;
  }
  const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(raw), static_cast<std::uint8_t>(raw >> 8),
                                 static_cast<std::uint8_t>(raw >> 16), static_cast<std::uint8_t>(raw >> 24)};
  return memory.write(*at, bytes);
}

void EscapeClock::started(std::string_view event, std::uint32_t server_time_ms) {
  if (event != settings_.start_event) return;
  if (start_ms_) {
    // Signed: the server's clock wraps. An earlier start (relayed after ours) is the room's
    // first; one after the group's countdown ran out is the escape again.
    const auto since_ms = static_cast<std::int32_t>(server_time_ms - *start_ms_);
    if (since_ms >= 0 && static_cast<std::uint32_t>(since_ms) / 1000 < settings_.length_s) return;
  }
  start_ms_ = server_time_ms;
  set_at_us_.reset();
  set_to_.reset();
  unheeded_ = 0;
  gave_up_ = false;
}

void EscapeClock::reset() {
  start_ms_.reset();
  set_at_us_.reset();
  set_to_.reset();
  told_long_ = false;
  unheeded_ = 0;
  gave_up_ = false;
}

std::optional<std::uint32_t> EscapeClock::group_seconds(std::uint32_t server_now_ms) const {
  if (!start_ms_) return std::nullopt;
  const std::uint32_t gone_ms = server_now_ms - *start_ms_;  // the server's clock wraps: unsigned
  const std::uint32_t gone_s = gone_ms / 1000;
  return gone_s >= settings_.length_s ? 0u : settings_.length_s - gone_s;
}

EscapeClockAction EscapeClock::update(const EscapeClockInput& in) {
  EscapeClockAction out;
  if (!in.scene || *in.scene != settings_.scene || !in.seconds || in.server_now_ms == 0) return out;
  const auto group = group_seconds(in.server_now_ms);
  if (!group) return out;
  if (*in.seconds > settings_.length_s + settings_.slack_s) {
    // Our countdown is longer than the group's whole length: length_s is not the remake's, and
    // setting it would cut time from everyone. Nothing, said once.
    if (!told_long_) {
      told_long_ = true;
      out.log.push_back("escape clock: ours shows " + std::to_string(*in.seconds) + " s, more than the length " +
                        std::to_string(settings_.length_s) + " s: the length is not the game's; not set");
    }
    return out;
  }
  if (*group == 0) {
    set_to_.reset();  // the group's ran out: the game's own end (RestartPos), never forced from here
    return out;
  }
  // A set of ours lately: an in-step reading right after it may be our own write, read back before the game
  // writes its value again (a display copy: sondas21r7, 'ours shows 25 s' set 56 times, each read back once).
  const bool recent = set_at_us_ && in.now_us - *set_at_us_ < 2 * settings_.again_us;
  if (*in.seconds <= *group + settings_.slack_s) {
    set_to_.reset();  // in step (or behind: never more time)
    if (!recent) unheeded_ = 0;  // in step on its own, or for longer than a read-back: taken
    return out;
  }
  if (gave_up_) return out;
  if (set_at_us_ && set_to_ && in.now_us - *set_at_us_ < settings_.again_us) return out;  // not seen yet
  // Not taken: our set still not shown, or shown and put back by the game (a bounce).
  if ((set_to_ || recent) && ++unheeded_ >= settings_.max_unheeded) {
    // Set again and again, and our game shows its own value each time: not the countdown.
    gave_up_ = true;
    out.log.push_back("escape clock: ours still shows " + std::to_string(*in.seconds) + " s after " +
                      std::to_string(unheeded_) +
                      " sets in a row: that word is not the countdown; nothing more is set this escape "
                      "(the group's clock goes on alone)");
    return out;
  }
  out.set_seconds = *group;
  char line[160];
  std::snprintf(line, sizeof line,
                "escape clock: ours shows %u s, the group's %u s (it started %u s ago): set to %u (ZMP D-164)",
                *in.seconds, *group, (in.server_now_ms - *start_ms_) / 1000, *group);
  out.log.push_back(line);
  set_at_us_ = in.now_us;
  set_to_ = *group;
  return out;
}

// ---- The ending holds the age ----------------------------------------------------------------

std::optional<std::string> ending_holds_age(const std::optional<std::string>& event,
                                            std::optional<std::uint32_t> defeat_ganon) {
  // The story list's ending events and the timelines they play (shared_story.txt).
  static constexpr std::string_view kEnding[] = {"InsideGanonCastle_Dungeon_BossDie_2",
                                                 "InsideGanonCastle_Dungeon_StaffRoll_HyruleCastle"};
  static constexpr std::string_view kTimelines[] = {"Dm_Ending_", "Dm_StaffCredit_", "Dm_FinalBattle_BossOut"};
  if (event) {
    for (const auto e : kEnding) {
      if (*event == e) return "the ending runs ('" + *event + "')";
    }
    for (const auto t : kTimelines) {
      if (event->starts_with(t)) return "the ending runs ('" + *event + "')";
    }
  }
  if (defeat_ganon && *defeat_ganon == flag_hash("Complete")) return "Ganon is sealed (Step_Defeat_Ganon Complete)";
  return std::nullopt;
}

// ---- Twinrova's owner -------------------------------------------------------------------------

std::optional<std::string> hand_over_holds(const HandOverInput& in) {
  // The bodies are the table's (boss_rules.hpp): Twinrova's under mirror, the others' families below.
  const FamilyActor* actor = family_actor(in.body);
  if (actor == nullptr) return std::nullopt;
  if (!in.owner_up) return std::nullopt;  // a downed owner holds nothing for the group
  if (actor->family != BossFamily::mirror) return family_holds(*actor, in.family, in.body_at);
  if (in.owner_frozen) {
    return "Twinrova's owner is frozen by Kotake's ice: its game thaws it (ZMP D-231)";
  }
  if (in.owner_charging) {
    return "Twinrova's owner is charging the mirror shield with a witch's blast: the charge is in its "
           "game, theirs to fire";
  }
  if (in.reason == HandOver::nearest && in.owner_charge && *in.owner_charge >= 1 && *in.owner_charge <= 2) {
    return "Twinrova's owner holds a charge of " + std::to_string(*in.owner_charge) +
           " in the mirror shield: it is theirs to fire";
  }
  return std::nullopt;
}

OwnerKeep owner_keep(const HandOverInput& in) {
  OwnerKeep out;
  HandOverInput ask = in;
  ask.reason = HandOver::hit;
  if (auto why = hand_over_holds(ask)) {
    out.hit = true;
    out.nearest = true;
    out.why = std::move(*why);
    return out;
  }
  ask.reason = HandOver::nearest;
  if (auto why = hand_over_holds(ask)) {
    out.nearest = true;
    out.why = std::move(*why);
  }
  return out;
}

bool frozen_animation(std::string_view name) { return name.starts_with("Freeze"); }

bool absorbing_animation(std::string_view name) {
  return name.starts_with("GuardBeam") || name.starts_with("Guard_Beam");
}

}  // namespace coop::host
