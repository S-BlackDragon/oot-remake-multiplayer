// ORM: per-block world lists: shared, one-way keys, once:, hold:/held:, unshare:. The rules are
//   described in coop/host/dungeon_lists.hpp.
// Doc: oot-remake docs/CONTENIDO_NINO.md, CONTENIDO_ADULTO.md.
// Switch: LICHII_COOP_DUNGEON_LISTS (on in the kit: shared_dungeon_child.txt,
//   shared_dungeon_adult.txt, _minigames, _montana, _towns: coop-options.ini).
// Test: tests/host/dungeon_lists_test.cpp, tests/lichii/adult_list_test.cpp,
//   tests/lichii/town_list_test.cpp, tests/lichii/montana_list_test.cpp.

#include "coop/host/dungeon_lists.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "coop/host/scene.hpp"
#include "coop/host/towns.hpp"

namespace coop::host {
namespace {

constexpr std::string_view kOnce = "once:";
constexpr std::string_view kHold = "hold:";
constexpr std::string_view kUnshare = "unshare:";
constexpr std::string_view kHeld = "held:";
constexpr std::string_view kToggle = "toggle64:";

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

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

bool has(const std::vector<FlagKey>& keys, FlagKey key) {
  return std::binary_search(keys.begin(), keys.end(), key);
}

void insert_sorted(std::vector<FlagKey>& keys, FlagKey key) {
  const auto at = std::lower_bound(keys.begin(), keys.end(), key);
  if (at == keys.end() || !(*at == key)) keys.insert(at, key);
}

std::string name_of(FlagKey key) {
  char text[32];
  if (key.kind == FlagKeyKind::key64) {
    std::snprintf(text, sizeof text, "key64:0x%016llx", static_cast<unsigned long long>(key.key));
  } else {
    std::snprintf(text, sizeof text, "flag 0x%08x", static_cast<unsigned>(key.hash));
  }
  return text;
}

const char* kind_text(int kind) {
  static constexpr const char* kNames[] = {"shared", "once:", "hold:", "unshare:", "toggle64:"};
  return kNames[kind];
}

FlagKey named(std::uint32_t hash) { return FlagKey(hash, std::uint16_t{0}); }

// 0x and 1 to 16 hex digits, not 0.
std::optional<std::uint64_t> key64_of(std::string_view token) {
  if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) return std::nullopt;
  const std::string_view digits = token.substr(2);
  if (digits.size() > 16) return std::nullopt;
  std::uint64_t key = 0;
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), key, 16);
  if (ec != std::errc() || end != digits.data() + digits.size() || key == 0) return std::nullopt;
  return key;
}

}  // namespace

const ToggleEntry* DungeonList::toggle_of(std::uint64_t key) const {
  for (const auto& t : toggles_) {
    if (t.key == key) return &t;
  }
  return nullptr;
}

std::optional<DungeonList::Kind> DungeonList::kind_of(FlagKey key) const {
  if (key.kind == FlagKeyKind::key64 && key.table == 0 && toggle_of(key.key)) return Kind::toggle;
  if (has(share_, key)) return Kind::share;
  if (key.kind == FlagKeyKind::named && key.index == 0 && enums_.contains(key.hash)) return Kind::share;
  if (has(once_, key)) return Kind::once;
  if (key.kind == FlagKeyKind::named && key.index == 0 && held_in(key.hash)) return Kind::hold;
  if (has(unshare_, key)) return Kind::unshare;
  return std::nullopt;
}

bool DungeonList::add(Kind kind, FlagKey key, std::string* error) {
  if (const auto now = kind_of(key); now && *now != kind) {
    if (error) {
      *error = name_of(key) + " is listed as " + kind_text(static_cast<int>(*now)) + " and as " +
               kind_text(static_cast<int>(kind));
    }
    return false;
  }
  switch (kind) {
    case Kind::share: insert_sorted(share_, key); break;
    case Kind::once: insert_sorted(once_, key); break;
    case Kind::unshare: insert_sorted(unshare_, key); break;
    case Kind::hold: break;    // add_hold
    case Kind::toggle: break;  // add_toggle
  }
  return true;
}

bool DungeonList::add_toggle(const ToggleEntry& entry, std::string* error) {
  const FlagKey key = FlagKey::key64_flag(entry.key);
  if (const ToggleEntry* now = toggle_of(entry.key)) {
    if (now->place == entry.place) return true;
    if (error) *error = name_of(key) + " is held in two places (" + now->place + ", " + entry.place + ")";
    return false;
  }
  if (!add(Kind::toggle, key, error)) return false;
  toggles_.push_back(entry);
  insert_sorted(share_, key);  // shared like a plain line; pickup_keys leaves it out
  return true;
}

bool DungeonList::toggle_holds_in(std::uint64_t key, std::string_view scene) const {
  const ToggleEntry* t = toggle_of(key);
  return t != nullptr && !scene.empty() && (scene == t->place || place_of(scene) == place_of(t->place));
}

bool DungeonList::add_hold(const HoldEntry& entry, std::string* error) {
  const FlagKey key = named(entry.hash);
  if (const auto place = held_in(entry.hash)) {
    if (*place == entry.place) return true;
    if (error) *error = name_of(key) + " is held in two places (" + std::string(*place) + ", " + entry.place + ")";
    return false;
  }
  if (!add(Kind::hold, key, error)) return false;
  holds_.push_back(entry);
  return true;
}

std::vector<FlagKey> DungeonList::pickup_keys() const {
  std::vector<FlagKey> keys;
  for (const auto& k : share_) {
    if (k.kind == FlagKeyKind::key64 && !(k.table == 0 && toggle_of(k.key))) keys.push_back(k);
  }
  return keys;
}

std::optional<std::string_view> DungeonList::held_in(std::uint32_t hash) const {
  for (const auto& h : holds_) {
    if (h.hash == hash) return std::string_view(h.place);
  }
  return std::nullopt;
}

bool DungeonList::holds_in(std::uint32_t hash, std::string_view scene) const {
  const auto place = held_in(hash);
  // ORM (PUEBLOS.md): a town's _Alternative scene is the town (town_place_of).
  return place && !scene.empty() && (scene == *place || town_place_of(scene) == town_place_of(*place));
}

std::optional<DungeonList> DungeonList::parse(std::string_view text, std::string* error) {
  DungeonList list;
  std::size_t line_number = 0;
  auto fail = [&](std::string_view why) -> std::optional<DungeonList> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + std::string(why);
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

    std::string why;
    if (parts[0].starts_with(kUnshare) || parts[0].starts_with(kOnce)) {
      const bool unshare = parts[0].starts_with(kUnshare);
      const std::string_view prefix = unshare ? kUnshare : kOnce;
      if (parts.size() != 1) return fail(std::string(prefix) + " takes one flag");
      const auto hash = flag_of(parts[0].substr(prefix.size()));
      if (!hash) return fail(std::string(prefix) + " takes a name or 0x and 1 to 8 hex digits");
      if (unshare && list.enums_.contains(*hash)) {
        return fail(name_of(named(*hash)) + " is an Enum flag a list opts in: it cannot be taken off");
      }
      if (!list.add(unshare ? Kind::unshare : Kind::once, named(*hash), &why)) return fail(why);
      continue;
    }
    if (parts[0].starts_with(kHeld)) {
      // A held switch (shared_dungeon_adult.txt): on only while something
      // stands on it, so taken off the shared list like unshare:; the place
      // says whose dungeon it is (for the reader, and checked in tests).
      if (parts.size() != 2) return fail("held: takes a flag and the dungeon it is in");
      const auto hash = flag_of(parts[0].substr(kHeld.size()));
      if (!hash) return fail("held: takes a name or 0x and 1 to 8 hex digits");
      if (list.enums_.contains(*hash)) {
        return fail(name_of(named(*hash)) + " is an Enum flag a list opts in: it cannot be taken off");
      }
      if (!list.add(Kind::unshare, named(*hash), &why)) return fail(why);
      continue;
    }
    if (parts[0].starts_with(kToggle)) {
      if (parts.size() != 2) return fail("toggle64: takes a key and the place it is held in");
      const auto key = key64_of(parts[0].substr(kToggle.size()));
      if (!key) return fail("toggle64: takes 0x and 1 to 16 hex digits (not 0)");
      if (!list.add_toggle(ToggleEntry{*key, std::string(parts[1])}, &why)) return fail(why);
      continue;
    }
    if (parts[0].starts_with(kHold)) {
      if (parts.size() != 2) return fail("hold: takes a flag and the place it is held in");
      HoldEntry entry;
      entry.name = std::string(parts[0].substr(kHold.size()));
      const auto hash = flag_of(entry.name);
      if (!hash) return fail("hold: takes a name or 0x and 1 to 8 hex digits");
      entry.hash = *hash;
      entry.place = std::string(parts[1]);
      if (!list.add_hold(entry, &why)) return fail(why);
      continue;
    }

    // Any other line is a shared flags file line (SharedFlags syntax).
    const auto one = SharedFlags::parse(line, &why);
    if (!one) {
      // SharedFlags says "line 1: ..." for its one line: keep only the why.
      const std::size_t colon = why.find(": ");
      return fail(colon == std::string::npos ? why : why.substr(colon + 2));
    }
    for (const auto& key : one->enums().keys()) {
      if (const auto now = list.kind_of(key); now && *now != Kind::share) {
        return fail(name_of(key) + " is listed as " + kind_text(static_cast<int>(*now)) + " and as shared");
      }
    }
    if (!list.enums_.merge(one->enums())) return fail("an Enum flag is listed twice with other values");
    for (const auto& key : one->keys()) {
      if (key.kind == FlagKeyKind::named && list.enums_.contains(key.hash)) continue;
      if (!list.add(Kind::share, key, &why)) return fail(why);
    }
  }
  return list;
}

std::optional<DungeonList> DungeonList::load(const std::filesystem::path& path, std::string* error) {
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

std::optional<DungeonList> DungeonList::load_many(std::string_view paths, std::string* error) {
  DungeonList all;
  auto take = [&](const std::filesystem::path& path) -> bool {
    const auto list = load(path, error);
    if (!list) return false;
    std::string why;
    if (!all.merge(*list, &why)) {
      if (error) *error = path.string() + ": " + why;
      return false;
    }
    return true;
  };
  while (!paths.empty()) {
    const std::size_t comma = paths.find(',');
    const std::string_view one = trim(paths.substr(0, comma));
    paths = comma == std::string_view::npos ? std::string_view() : paths.substr(comma + 1);
    if (one.empty()) continue;
    const std::filesystem::path path{std::string(one)};
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      std::vector<std::filesystem::path> files;
      for (const auto& e : std::filesystem::directory_iterator(path, ec)) {
        const std::string name = e.path().filename().string();
        if (e.is_regular_file(ec) && name.starts_with("shared_dungeon_") && name.ends_with(".txt")) {
          files.push_back(e.path());
        }
      }
      std::sort(files.begin(), files.end());
      for (const auto& f : files) {
        if (!take(f)) return std::nullopt;
      }
      continue;
    }
    if (!take(path)) return std::nullopt;
  }
  return all;
}

bool DungeonList::merge(const DungeonList& other, std::string* error) {
  for (const auto& key : other.enums_.keys()) {
    if (const auto now = kind_of(key); now && *now != Kind::share) {
      if (error) *error = name_of(key) + " is listed as " + kind_text(static_cast<int>(*now)) + " and as shared";
      return false;
    }
  }
  if (!enums_.merge(other.enums_)) {
    if (error) *error = "an Enum flag is listed in two lists with other values";
    return false;
  }
  for (const auto& t : other.toggles_) {
    if (!add_toggle(t, error)) return false;
  }
  for (const auto& key : other.share_) {
    if (key.kind == FlagKeyKind::key64 && key.table == 0 && other.toggle_of(key.key)) continue;  // above
    if (!add(Kind::share, key, error)) return false;
  }
  for (const auto& key : other.once_) {
    if (!add(Kind::once, key, error)) return false;
  }
  for (const auto& h : other.holds_) {
    if (!add_hold(h, error)) return false;
  }
  for (const auto& key : other.unshare_) {
    if (key.kind == FlagKeyKind::named && enums_.contains(key.hash)) {
      if (error) *error = name_of(key) + " is an Enum flag a list opts in: it cannot be taken off";
      return false;
    }
    if (!add(Kind::unshare, key, error)) return false;
  }
  return true;
}

DungeonListReport apply_dungeon_list(std::optional<SharedFlags>& shared, const DungeonList& list) {
  DungeonListReport report;
  report.once = list.once().size();
  report.held = list.holds().size();
  report.pickups = list.pickup_keys().size();
  report.toggles = list.toggles().size();
  if (!shared) return report;

  // Flag by flag: a conflict keeps the built-in lists' rule and the list's
  // other Enum flags still go (EnumFlags::merge stops at the first conflict;
  // oot-remake docs/REVISION_HISTORIA.md H2).
  EnumFlags enums = shared->enums();
  const EnumFlags& mine = list.share_enums();
  for (const auto& key : mine.keys()) {
    if (enums.contains(key.hash)) {
      if (*enums.values(key.hash) != *mine.values(key.hash) ||
          enums.steps(key.hash) != mine.steps(key.hash)) {
        ++report.enum_conflicts;
      }
      continue;
    }
    EnumFlags one;
    one.add(key.hash, *mine.values(key.hash), mine.steps(key.hash));
    enums.merge(one);
    if (!shared->contains(key)) ++report.added;
  }

  std::vector<FlagKey> keys;
  keys.reserve(shared->size() + list.share().size() + list.once().size() + list.holds().size());
  for (const auto& key : shared->keys()) {
    if (has(list.unshare(), key) && key.kind == FlagKeyKind::named && key.index == 0 &&
        enums.contains(key.hash)) {
      ++report.enum_kept;  // an Enum flag a built-in list opts in cannot be taken off
    } else if (has(list.unshare(), key)) {
      ++report.removed;
      continue;
    }
    keys.push_back(key);
  }
  auto add = [&](FlagKey key) {
    if (!shared->contains(key)) ++report.added;
    keys.push_back(key);
  };
  for (const auto& key : list.share()) add(key);
  for (const auto& key : list.once()) add(key);
  for (const auto& h : list.holds()) add(named(h.hash));
  shared = SharedFlags(std::move(keys), std::move(enums));
  return report;
}

}  // namespace coop::host
