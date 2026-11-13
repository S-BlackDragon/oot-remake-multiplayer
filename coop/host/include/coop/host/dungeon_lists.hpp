#pragma once

// ORM per-block flag lists (oot-remake docs/CONTENIDO_NINO.md): one text file
// per block of the game (the kit's shared_dungeon_child.txt for the child
// dungeons and the child overworld; later blocks bring their own), applied on
// top of the built-in lists (lichii/hook/coop_dungeon.cpp, coop_chests.cpp,
// the story list) with LICHII_COOP_DUNGEON_LISTS. A list says, flag by flag:
//
//   # a comment; blank lines are fine
//   SomeFlagName              shared, live (any SharedFlags line, flags.hpp;
//   key64:0x2fa4c1d09b7e6a35  a key64 line is a one-time key, as the dungeon
//                             keys are: a 1 crosses, a 0 never undoes it)
//   once:SomeFlagName         shared by the pickup rule (a named Bool that
//                             says something was done for good: a wall
//                             blown, a puzzle solved; a 1 is never undone by
//                             another game's 0 or by a load)
//   hold:SomeFlagName Place   shared, but held while our Link is in Place
//                             (issue 129's rule out of the dungeons: a played
//                             gimmick event must not reach a game whose area
//                             is loaded, or that game's own trigger stops
//                             working; it is written when we leave or load)
//   unshare:SomeFlagName      taken off the shared list: a flag a built-in
//                             list shares that says where ONE player (or one
//                             game's Ruto) is, not what the group did (the
//                             issue 148 kind; ZMP's lesson that the player of
//                             a room is never "the nearest" but the one the
//                             room is about)
//   toggle64:0x03ecedcb30afe5e4 Place
//                             a 64-bit key that goes both ways (a Song of
//                             Time block that comes and goes, CANCIONES.md):
//                             shared like a plain line, never a one-time
//                             key, and another game's value is held while
//                             our Link is in Place (no block taken from
//                             under him, no bounce: the last writer wins
//                             when he leaves or loads)
//   held:SomeFlagName Dungeon a held switch (on only while something stands
//                             on it; shared, it bounces between the games):
//                             taken off like unshare:, Dungeon names where it
//                             is (shared_dungeon_adult.txt, CONTENIDO_ADULTO.md)
//
// A flag token is a name or 0x and 1 to 8 hex digits. Place is a scene name;
// "KokiriForest" holds in KokiriForest_Child and KokiriForest_Adult
// (place_of, scene.hpp), "KakarikoVillage" in KakarikoVillage_Adult_Alternative
// too (town_place_of, towns.hpp: a town's second version is the town). Each
// flag has one kind: a flag listed under two kinds, in one file or across
// files, is an error; the same line twice is fine. An Enum flag cannot be
// taken off. No game data here: plain text.
//
// Doc: oot-remake docs/CONTENIDO_NINO.md, CONTENIDO_ADULTO.md.
// Switch: LICHII_COOP_DUNGEON_LISTS (on in the kit: shared_dungeon_child.txt,
//   shared_dungeon_adult.txt, _minigames, _montana, _towns: coop-options.ini).
// Test: tests/host/dungeon_lists_test.cpp, tests/lichii/adult_list_test.cpp,
//   tests/lichii/town_list_test.cpp, tests/lichii/montana_list_test.cpp.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/flags.hpp"

namespace coop::host {

struct HoldEntry {
  std::string name;        // as written: a name or 0x hash
  std::uint32_t hash = 0;  // flag_hash(name)
  std::string place;       // the scene or place it is held in
  bool operator==(const HoldEntry&) const = default;
};

// A toggle64: line: a two-way 64-bit key, held while our Link is in `place`.
struct ToggleEntry {
  std::uint64_t key = 0;
  std::string place;
  bool operator==(const ToggleEntry&) const = default;
};

class DungeonList {
 public:
  DungeonList() = default;

  // Nothing on a bad line; `error`, when given, says which and why.
  static std::optional<DungeonList> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<DungeonList> load(const std::filesystem::path& path,
                                         std::string* error = nullptr);
  // LICHII_COOP_DUNGEON_LISTS: files or folders, comma-separated, merged in
  // order. A folder stands for every shared_dungeon_*.txt in it, by name.
  // Blanks around an entry and empty entries are skipped. Nothing when an
  // entry is missing or bad, or two lists disagree (`error` says which).
  static std::optional<DungeonList> load_many(std::string_view paths, std::string* error = nullptr);

  // Adds every entry of `other`. False (and `error`) on the first flag the
  // two lists give different kinds (or a held flag two places).
  bool merge(const DungeonList& other, std::string* error = nullptr);

  // Plain shared lines (named and key64), sorted, unique.
  const std::vector<FlagKey>& share() const { return share_; }
  const EnumFlags& share_enums() const { return enums_; }
  // The key64 lines: one-time keys (Session::share_pickup). Not the toggle64 ones.
  std::vector<FlagKey> pickup_keys() const;
  // The toggle64 lines (their keys are in share() too).
  const std::vector<ToggleEntry>& toggles() const { return toggles_; }
  // True when `key` is a toggle64 key and `scene` shows its place, in either era.
  bool toggle_holds_in(std::uint64_t key, std::string_view scene) const;
  // once: flags (Session::share_pickup_named), sorted, unique.
  const std::vector<FlagKey>& once() const { return once_; }
  const std::vector<HoldEntry>& holds() const { return holds_; }
  const std::vector<FlagKey>& unshare() const { return unshare_; }  // sorted, unique

  // The place a hold: flag is held in, or nothing when it is not one.
  std::optional<std::string_view> held_in(std::uint32_t hash) const;
  // True when `hash` is a hold: flag and `scene` (the scene our Link is in)
  // shows its place, in either era or a town's _Alternative version.
  bool holds_in(std::uint32_t hash, std::string_view scene) const;

  bool empty() const {
    return share_.empty() && enums_.empty() && once_.empty() && holds_.empty() && unshare_.empty();
  }

 private:
  enum class Kind { share, once, hold, unshare, toggle };
  std::optional<Kind> kind_of(FlagKey key) const;
  bool add(Kind kind, FlagKey key, std::string* error);
  bool add_hold(const HoldEntry& entry, std::string* error);
  bool add_toggle(const ToggleEntry& entry, std::string* error);
  const ToggleEntry* toggle_of(std::uint64_t key) const;

  std::vector<FlagKey> share_;
  EnumFlags enums_;
  std::vector<FlagKey> once_;
  std::vector<HoldEntry> holds_;
  std::vector<FlagKey> unshare_;
  std::vector<ToggleEntry> toggles_;
};

struct DungeonListReport {
  std::size_t added = 0;    // keys that were not shared before
  std::size_t removed = 0;  // keys taken off the shared list
  std::size_t once = 0;     // once: flags listed
  std::size_t held = 0;     // hold: flags listed
  std::size_t pickups = 0;  // key64 lines listed
  std::size_t toggles = 0;  // toggle64 lines listed (shared both ways, held in their place)
  // Enum flags the list names with other values (or another steps rule) than
  // the built-in lists: the built-in rule is kept, the list's line is not used.
  std::size_t enum_conflicts = 0;
  // unshare: lines naming an Enum flag a built-in list opts in: it stays shared.
  std::size_t enum_kept = 0;
};

// Adds the list's shared, once: and hold: flags (and its Enum flags) to
// `shared` and takes its unshare: flags off. Nothing (flags not shared at
// all) stays nothing: a list never turns flag sharing on by itself.
DungeonListReport apply_dungeon_list(std::optional<SharedFlags>& shared, const DungeonList& list);

}  // namespace coop::host
