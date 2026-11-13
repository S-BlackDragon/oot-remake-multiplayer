#pragma once
// ORM (oot-remake): the group's settings (oot-remake docs/AJUSTES_GRUPO.md,
// after oot-64 D-030: what changes the shared world is the group's, never one
// player's). No emulator code: the game's options come through GameOptions
// (GameFlags in the emulator, a fake in tests), the switches through a getter.
//
// Three kinds of setting:
// - host_wins: a game option that brings things into the shared world
//   (amiibo, ZELDA NOTES). While a game plays as a guest, its value is the
//   host's: written into the game at the start and again whenever either side
//   changes it, and the guest's own value is written back when it stops being
//   a guest (disconnected, or it hosts now).
// - must_match: a co-op switch, read once when the emulator starts, so it
//   cannot be changed live. A difference with the host is told in the chat
//   and the log, on both sides, once per value.
// - local: presentation or the player's own controls (camera, lock-on type,
//   voice language, the HUD...). Each player's own; never sent.
//
// GROUP_SETTINGS (type 246) carries each player's host_wins and must_match
// values; the server relays and keeps them as PLAYER_LOOK.
//
// Switch: LICHII_ORM_GROUP_SETTINGS, on unless "0" (the glue reads it).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host::orm {

enum class SettingRule : std::uint8_t { host_wins, must_match, local };
enum class SettingKind : std::uint8_t { game_bool, game_enum, coop_switch };

// How a co-op switch is read, as the code that uses it reads it.
enum class SwitchRead : std::uint8_t {
  none,            // a game option
  on_unless_zero,  // on unless the variable is "0"
  off_unless_set,  // on when set, not empty and not "0"
  list_files,      // a file (or comma-separated files): "off" or "on:<fingerprint>"
  dungeon_lists,   // LICHII_COOP_DUNGEON_LISTS: files or folders (shared_dungeon_*.txt)
  text,            // the text itself ("all" when unset), a fingerprint when long
  story_events,    // LICHII_ORM_STORY_EVENTS, else the old LICHII_COOP_STORY
  downed_auto,     // unset: as LICHII_COOP_DOWNED == "1"; set: on unless "0"
};

struct SettingInfo {
  std::string_view key;   // the game's flag name or the variable's name
  SettingKind kind;
  SettingRule rule;
  SwitchRead read;
  std::string_view what;  // a few English words for the chat ("amiibo")
  std::string_view old_name = {};  // a variable's name before test9, read when the key is unset
};

// Every setting this build knows, the local ones too (for the doc and tests).
std::span<const SettingInfo> setting_table();
const SettingInfo* find_setting(std::string_view key);

// A game Enum option's value names in the game's order (GameDataList);
// empty for anything else. The first one of ZeldaNoteState ("NotSet") means
// "not chosen yet" and is never written into another game.
std::span<const std::string_view> enum_value_names(std::string_view key);
bool enum_value_not_chosen(std::string_view key, std::string_view value);

// A variable's value, nothing when it is unset or empty.
using EnvGet = std::function<std::optional<std::string>(const char*)>;

// The fingerprint of list files: FNV-1a over each file's name and its text
// (with '\r' taken out, so a copy with Windows line ends matches), in order.
// A missing file counts as its name alone. A folder entry stands for its
// shared_dungeon_*.txt files by name when `dungeon_folders`. "on:" + 8 hex
// digits; "off" for empty `paths`. Relative paths are taken from `base`.
std::string list_fingerprint(std::string_view paths, const std::filesystem::path& base,
                             bool dungeon_folders);

// Our must_match switches as GROUP_SETTINGS entries, by key.
std::vector<protocol::SettingEntry> switch_entries(const EnvGet& get, const std::filesystem::path& base);

// The game's options as the game's flag store gives them.
class GameOptions {
 public:
  virtual ~GameOptions() = default;
  virtual std::optional<bool> get_bool(std::uint32_t hash) = 0;
  virtual std::optional<std::uint32_t> get_enum(std::uint32_t hash) = 0;
  // Queue a write as the game's own setter does; false when it did not go in.
  virtual bool set_bool(std::uint32_t hash, bool value) = 0;
  virtual bool set_enum(std::uint32_t hash, std::uint32_t value) = 0;
};

// Who is in the room, from the client.
struct RoomView {
  bool connected = false;
  std::uint8_t self = 0;  // our number
  std::uint8_t host = 0;  // the host's number
  std::map<std::uint8_t, std::string> names;  // nicknames, for the chat
};

struct GroupStep {
  std::optional<protocol::GroupSettings> send;  // ours, to send now
  std::vector<std::string> log;                 // lines for "coop: settings: ..."
  std::vector<std::string> chat;                // notices for the chat
};

class GroupSettings {
 public:
  static constexpr std::uint64_t kStepUs = 500'000;              // works twice a second
  static constexpr std::uint64_t kNoHostNoteUs = 15'000'000;     // "no settings from the host" after this

  // `switches`: switch_entries() of this run (they never change in a run).
  explicit GroupSettings(std::vector<protocol::SettingEntry> switches);

  // A GROUP_SETTINGS relayed by the server; `player_number` is the sender.
  void received(const protocol::GroupSettings& m);
  void player_left(std::uint8_t number);
  // WELCOME or a reconnection: ours go again, the others' come again.
  void connection_new();
  // Our last GROUP_SETTINGS did not go out: send it again on the next step.
  void send_failed();

  // Once a frame. `game` null while no game runs. Works at most every
  // kStepUs, sooner after connection_new().
  GroupStep step(GameOptions* game, const RoomView& room, std::uint64_t now_us);

  // Our entries as last built (switches and game options), by key.
  const std::vector<protocol::SettingEntry>& ours() const { return ours_; }
  // The game options we hold at the host's value: key -> our own value.
  const std::map<std::string, std::string>& own_values() const { return own_; }

 private:
  void read_game(GameOptions* game);
  bool write_game(GameOptions& game, const SettingInfo& info, const std::string& value);
  void restore(GameOptions* game, GroupStep& out);
  void as_guest(GameOptions* game, const RoomView& room, std::uint64_t now_us, GroupStep& out);
  void as_host(const RoomView& room, GroupStep& out);
  std::string name_of(const RoomView& room, std::uint8_t number) const;

  std::vector<protocol::SettingEntry> switches_;
  std::vector<protocol::SettingEntry> ours_;
  std::optional<std::vector<protocol::SettingEntry>> sent_;
  std::map<std::uint8_t, std::vector<protocol::SettingEntry>> peers_;
  std::map<std::string, std::string> own_;      // host_wins key -> our value before the host's
  std::map<std::string, std::string> applied_;  // host_wins key -> the host's value we last wrote
  std::set<std::string> told_;                  // differences already told
  bool send_due_ = true;
  bool force_step_ = true;
  bool stepped_ = false;
  std::uint64_t last_step_us_ = 0;
  std::uint64_t guest_since_us_ = 0;
  bool no_host_told_ = false;
};

}  // namespace coop::host::orm
