#pragma once

// Holding back the flags a talk sets (ORM D-R12, docs/flags.md "Talk flags").
//
// A shared flag that another player's talk changed reaches this game at once
// (FlagWatcher). Most shared flags are progress that must show at once:
// items, songs, keys, chests, doors, switches, dungeon rooms. They pass
// straight on. A few change what the scene shows: a Kokiri who blocks a path
// steps aside (KokiriForest_Npc_Kokiri_Mido_Child_Blocker turns his blocker
// AI off and takes the air wall away), a townsperson stands elsewhere, a
// placement with a live Presence flag appears or goes. Written while our
// player looks, the townsperson jumps or is swapped in front of them. Such a
// flag has a DeferRule, and FlagDeferral holds an incoming value of it until
// nobody here sees the change:
// - our scene is not known (loading, the title) or changed since it came;
// - our scene is none of the rule's scenes;
// - none of the rule's actors is on the actor list (unloaded, or never in
//   this scene), or every one of them is a copy another game owns (its place
//   and animation come from the owner, who already shows the change);
// - every one of them is farther from our Link than the rule's distance;
// - or the rule's time ran out (a player who stays in the scene for an hour
//   must not keep an old world for good: one jump then, not never).
//
// DeferringFlagStore goes between FlagWatcher and the game's store (above
// ItemFlagStore). A held write answers true and reads back as written, so the
// watcher neither tries it again nor sends the game's older value back as a
// change of ours. When the game changes the flag itself meanwhile (our player
// had the same talk), the game's value is newer: the held one is dropped and
// the watcher sends the game's. 64-bit keys, counts and flags with no rule
// are never held, so with no rules the store is the game's store.
//
// No emulator code: tested with fake stores and fake memory. Not
// thread-safe: the present thread only, like FlagWatcher::frame.
//
// Doc: oot-remake docs/BANDERAS_CHARLA.md (sections 7 and 10, D-R12).
// Switch: LICHII_COOP_TALK=shared_talk.txt (off in the kit), or LICHII_COOP_FLAGS_DEFER=1 for the
//   rules alone.
// Test: tests/host/flag_defer_test.cpp.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

struct DeferRule {
  // Without a time on the rule's line.
  static constexpr std::uint64_t kDefaultAfterUs = 120'000'000;

  std::vector<std::string> scenes;  // scene name prefixes ("KokiriForest"); empty: any scene
  std::vector<std::string> actors;  // actor name prefixes ("Npc_Kokiri_Mido_Child")
  float far_m = 0.0f;               // 0: no distance rule
  std::uint64_t after_us = kDefaultAfterUs;  // 0: wait for one of the other reasons
  bool now = false;                 // an override line: this flag is never held

  bool operator==(const DeferRule&) const = default;
};

// The rules by flag, from plain text, one flag a line:
//
//   # a comment
//   KokiriForest_Npc_Kokiri_Mido_Child_Blocker scene:KokiriForest actor:Npc_Kokiri_Mido_Child far:25 after:120
//   0x502f6cea now          a hash; "now" takes a rule away (never held)
//
// A flag is a name (hashed with flag_hash) or 0x and 1 to 8 hex digits,
// element 0. Options, each at most once except scene: and actor:, which may
// repeat: scene:<prefix>, actor:<prefix>, far:<metres>, after:<seconds> (0:
// no time limit), now (alone).
class DeferRules {
 public:
  static constexpr std::size_t kMaxRules = 1u << 16;

  // Replaces the flag's rule. False for a 64-bit key (never held) or when full.
  bool set(FlagKey key, DeferRule rule);
  // The rule that holds this flag; nothing for no rule and for a "now" line.
  const DeferRule* find(FlagKey key) const;
  // `over`'s lines replace ours ("now" lines too).
  void merge(const DeferRules& over);
  std::size_t size() const { return rules_.size(); }
  bool empty() const { return rules_.empty(); }
  // Every flag with a line, "now" lines too, sorted (the shared talk list:
  // each of them is shared, LICHII_COOP_TALK).
  std::vector<FlagKey> keys() const;
  // The name a line gave its flag; empty for a 0x hash or no line.
  std::string_view name(FlagKey key) const;

  static std::optional<DeferRules> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<DeferRules> load(const std::filesystem::path& path,
                                        std::string* error = nullptr);

 private:
  friend DeferRules default_defer_rules();
  std::map<FlagKey, DeferRule> rules_;
  std::map<FlagKey, std::string> names_;
};

// The talk flags read from the RomFS (Kokiri Forest so far; docs/flags.md,
// "Talk flags"). None of them is shared by default: they matter once a
// shared-flags file (LICHII_COOP_FLAGS) or the shared talk list
// (LICHII_COOP_TALK, the kit's shared_talk.txt) names them.
DeferRules default_defer_rules();

// One actor of the scene's actor list, as the hook read it.
struct SeenActor {
  std::string name;
  std::optional<protocol::Vec3> position;  // read for shared and movable actors only
  bool copy = false;  // a shared object another game owns: our copy follows its owner
  // Read for the water planes and the door actors only (coop/host/water.hpp,
  // held_switch.hpp HeldDoorCheck): its AS slot 0 command.
  std::optional<std::string> animation;
};

// What this game shows now.
struct DeferView {
  std::optional<std::string> scene;              // nothing or empty: loading, the title
  std::optional<std::vector<SeenActor>> actors;  // nothing: the list was not read
  std::optional<protocol::Vec3> link;            // our Link
  std::uint64_t now_us = 0;                      // any steady clock
};

enum class DeferWhy : std::uint8_t {
  held,           // a value came and waits
  scene_unknown,  // released: loading or no scene
  scene_changed,  // released: the scene is not the one it came in
  other_scene,    // released: our scene is none of the rule's
  actors_gone,    // released: none of the rule's actors is loaded
  actors_copied,  // released: every one of them is a copy another game moves
  far,            // released: every one of them is far from our Link
  timeout,        // released: the rule's time ran out
  game_changed,   // dropped: the game changed the flag itself meanwhile
};

std::string_view to_string(DeferWhy why);

struct DeferNote {
  protocol::FlagEntry entry;  // the value held or released
  DeferWhy why = DeferWhy::held;
};

struct DeferStats {
  std::uint64_t passed = 0;        // writes with no rule, or released at once
  std::uint64_t held = 0;          // writes held
  std::uint64_t released = 0;      // held values written later
  std::uint64_t dropped = 0;       // held values the game's own change replaced
  std::uint64_t write_failures = 0;  // a release the game refused; tried again
};

class FlagDeferral {
 public:
  explicit FlagDeferral(DeferRules rules) : rules_(std::move(rules)) {}

  // Once per frame, before the watcher's frame (DeferringFlagStore over the
  // same store): writes the held values that may go now.
  void tick(FlagStore& game, const DeferView& view);

  // DeferringFlagStore's read and write.
  std::optional<protocol::FlagEntry> read(FlagStore& game, FlagKey key);
  bool write(FlagStore& game, const protocol::FlagEntry& entry, const DeferView& view);

  // DeferringFlagStore's write_exact (the host's save load, docs/flags.md
  // "Saves"): the save's value goes as it is, and a held one is dropped.
  bool write_exact(FlagStore& game, const protocol::FlagEntry& entry);

  // A new game process: what was held was for the old one.
  void forget() { held_.clear(); }

  bool holds(FlagKey key) const { return held_.contains(key); }
  std::size_t held() const { return held_.size(); }
  // What was held, released and dropped since the last call, for the log.
  std::vector<DeferNote> take_notes();
  // ORM (charla-mido): a value with a rule that was written at once, with
  // why it did not wait (Mido not loaded here: actors_gone). Kept apart from
  // take_notes, so the log can say which path ran for every ruled value.
  std::vector<DeferNote> take_passed() { return std::exchange(passed_, {}); }
  const DeferStats& stats() const { return stats_; }
  const DeferRules& rules() const { return rules_; }

 private:
  struct Held {
    protocol::FlagEntry entry;
    std::optional<protocol::FlagEntry> game_before;  // the game's value when it came
    std::string scene;
    std::uint64_t since_us = 0;
  };

  // Why a value held since `since_us` in `scene` may be written now; nothing
  // while it must wait.
  static std::optional<DeferWhy> release(const DeferRule& rule, const DeferView& view,
                                         std::string_view scene, std::uint64_t since_us);
  // The game changed the flag since it was held: drops it and says so.
  bool changed_by_game(FlagStore& game, FlagKey key, const Held& held,
                       std::optional<protocol::FlagEntry>* now);

  DeferRules rules_;
  std::map<FlagKey, Held> held_;
  std::vector<DeferNote> notes_;
  std::vector<DeferNote> passed_;
  DeferStats stats_;
};

// The game's store as FlagWatcher sees it with the deferral in between. One
// per frame, like ItemFlagStore; `view` must outlive it.
class DeferringFlagStore final : public FlagStore {
 public:
  DeferringFlagStore(FlagDeferral& deferral, FlagStore& game, const DeferView& view)
      : deferral_(deferral), game_(game), view_(view) {}

  std::optional<protocol::FlagEntry> read(FlagKey key) override {
    return deferral_.read(game_, key);
  }
  bool write(const protocol::FlagEntry& entry) override {
    return deferral_.write(game_, entry, view_);
  }
  bool is_count(FlagKey key) override { return game_.is_count(key); }
  bool is_one_way(FlagKey key) override { return game_.is_one_way(key); }
  bool holds(FlagKey key) override { return game_.holds(key); }
  bool write_exact(const protocol::FlagEntry& entry) override {
    return deferral_.write_exact(game_, entry);
  }
  // ORM: a per player value is no talk flag: straight to the game's store.
  bool write_own(const protocol::FlagEntry& entry) override { return game_.write_own(entry); }

 private:
  FlagDeferral& deferral_;
  FlagStore& game_;
  const DeferView& view_;
};

// What the game holds of a few flags, read now and then, for the log (ORM
// shared talk list: "coop: talk: <flag> 1 -> 0", in the game that talked and
// in the others once their copy of the flag changes). Reads the game's
// store, not the deferral: a held value shows only once it is written.
struct FlagChange {
  FlagKey key;
  std::optional<protocol::FlagEntry> before;  // nothing: not readable before
  protocol::FlagEntry now;
};

class FlagChanges {
 public:
  explicit FlagChanges(std::vector<FlagKey> keys) : keys_(std::move(keys)) {}

  // Reads every flag; the changes since the last call. The first call (and
  // the first readable value of each flag) only records.
  std::vector<FlagChange> read(FlagStore& game);
  // A new game process or a save load: start again.
  void forget() { last_.clear(); }

 private:
  std::vector<FlagKey> keys_;
  std::map<FlagKey, protocol::FlagEntry> last_;
};

}  // namespace coop::host
