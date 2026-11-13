// The group's settings (coop/host/orm_group_settings.hpp, oot-remake docs/AJUSTES_GRUPO.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/orm_group_settings.hpp"
#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_messages.hpp"

using namespace coop::host::orm;
using coop::protocol::flag_hash;
using coop::protocol::SettingEntry;

namespace {

constexpr std::uint32_t kAmiibo = 0xb84dd626;     // GameDataList: OptionData.IsUseAmiibo, Bool, save file 1
constexpr std::uint32_t kZeldaNote = 0x45158091;  // OptionData.ZeldaNoteState, Enum, save file 1

class FakeOptions final : public GameOptions {
 public:
  std::map<std::uint32_t, bool> bools;
  std::map<std::uint32_t, std::uint32_t> enums;
  std::vector<std::string> writes;  // "b <hash> <0|1>", "e <hash> <value>"
  bool fail = false;

  std::optional<bool> get_bool(std::uint32_t hash) override {
    const auto it = bools.find(hash);
    if (it == bools.end()) return std::nullopt;
    return it->second;
  }
  std::optional<std::uint32_t> get_enum(std::uint32_t hash) override {
    const auto it = enums.find(hash);
    if (it == enums.end()) return std::nullopt;
    return it->second;
  }
  bool set_bool(std::uint32_t hash, bool value) override {
    if (fail || !bools.count(hash)) return false;
    bools[hash] = value;
    writes.push_back("b " + std::to_string(hash) + " " + (value ? "1" : "0"));
    return true;
  }
  bool set_enum(std::uint32_t hash, std::uint32_t value) override {
    if (fail || !enums.count(hash)) return false;
    enums[hash] = value;
    writes.push_back("e " + std::to_string(hash) + " " + std::to_string(value));
    return true;
  }
};

FakeOptions game_with(bool amiibo, std::string_view note) {
  FakeOptions g;
  g.bools[kAmiibo] = amiibo;
  g.enums[kZeldaNote] = flag_hash(note);
  return g;
}

EnvGet env_of(std::map<std::string, std::string> vars) {
  return [vars](const char* name) -> std::optional<std::string> {
    const auto it = vars.find(name);
    if (it == vars.end() || it->second.empty()) return std::nullopt;
    return it->second;
  };
}

std::string value_of(const std::vector<SettingEntry>& entries, std::string_view key) {
  for (const auto& e : entries) {
    if (e.key == key) return e.value;
  }
  return "<none>";
}

RoomView room(std::uint8_t self, std::uint8_t host) {
  RoomView r;
  r.connected = true;
  r.self = self;
  r.host = host;
  r.names = {{1, "Link"}, {2, "Zelda"}, {3, "Saria"}};
  return r;
}

// A fresh temporary folder for list files.
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    static int made = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("orm_group_settings_" + std::to_string(stamp) + "_" + std::to_string(++made));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  void write(const std::string& name, const std::string& text) const {
    std::ofstream out(path / name, std::ios::binary);
    out << text;
  }
};

std::vector<SettingEntry> kit_switches() {
  // The kit's coop-options.ini "on by default" lines.
  return switch_entries(env_of({{"LICHII_COOP_PUPPET", "1"},
                                {"LICHII_COOP_OBJECTS", "1"},
                                {"LICHII_COOP_SPAWN", "1"},
                                {"LICHII_COOP_NPC", "1"},
                                {"LICHII_COOP_DOWNED", "1"},
                                {"LICHII_COOP_SPECTATOR", "1"},
                                {"LICHII_ORM_STORY_EVENTS", "1"}}),
                        std::filesystem::temp_directory_path());
}

// `entries` and one more, by key.
std::vector<SettingEntry> plus(std::vector<SettingEntry> entries, std::string key, std::string value) {
  entries.push_back({std::move(key), std::move(value)});
  std::sort(entries.begin(), entries.end(), [](const auto& x, const auto& y) { return x.key < y.key; });
  return entries;
}

}  // namespace

TEST_CASE("group settings: the game options are the romfs's, with their save file and values (ORM)") {
  // Hashes from GameDataList.Product.110 (save file 1, option.sav), read with tools/romfs.
  const std::map<std::string, std::uint32_t> gdl = {
      {"OptionData.IsUseAmiibo", 0xb84dd626},          {"OptionData.ZeldaNoteState", 0x45158091},
      {"OptionData.IsCameraUpDownReverse", 0x520ddc44}, {"OptionData.IsCameraRightLeftReverse", 0xd7459e39},
      {"OptionData.CameraRotateSpeed", 0x128d0120},     {"OptionData.IsUseGyro", 0xbb11d937},
      {"OptionData.IsChangeJumpButton", 0xef35e764},    {"OptionData.DisplayMode", 0x4e767f10},
      {"OptionData.IsDisplayTextRuby", 0x634748c5},     {"OptionData.VoiceLanguage", 0xa2dcdc9d},
      {"OptionData.LockOnType", 0x4219063f},            {"OptionData.OcarinaSongEstimateMode", 0xda87a986},
      {"OptionData.OcarinaKeyAssign", 0xe2ba0c02},      {"OptionData.VoiceSubtitle", 0xff5fe801},
      {"OptionData.VoiceSubtitleSpeakerName", 0xa451159b}, {"OptionData.ControllerRumble", 0x769d96a5},
  };
  std::size_t options = 0;
  for (const SettingInfo& info : setting_table()) {
    CHECK(coop::protocol::valid_setting_key(info.key));
    CHECK(find_setting(info.key) == &info);  // each key once
    if (info.kind == SettingKind::coop_switch) continue;
    ++options;
    const auto it = gdl.find(std::string(info.key));
    REQUIRE(it != gdl.end());
    CHECK(flag_hash(info.key) == it->second);
    // Only amiibo and ZELDA NOTES bring things into the world; the camera, the
    // lock-on type, the voice language, the HUD and the controls stay each player's.
    const bool world = info.key == "OptionData.IsUseAmiibo" || info.key == "OptionData.ZeldaNoteState";
    CHECK((info.rule == SettingRule::host_wins) == world);
    CHECK((info.rule == SettingRule::local) == !world);
  }
  CHECK(options == gdl.size());
  // ZeldaNoteState's values in the game's order; its default (GameDataList) is NotSet's hash.
  const auto names = enum_value_names("OptionData.ZeldaNoteState");
  REQUIRE(names.size() == 3);
  CHECK(names[0] == "NotSet");
  CHECK(names[1] == "Use");
  CHECK(names[2] == "NotUse");
  CHECK(flag_hash("NotSet") == 3606122082u);
  CHECK(enum_value_not_chosen("OptionData.ZeldaNoteState", "NotSet"));
  CHECK_FALSE(enum_value_not_chosen("OptionData.ZeldaNoteState", "Use"));
  // No co-op switch is a host_wins one (they are read once at start).
  for (const SettingInfo& info : setting_table()) {
    if (info.kind == SettingKind::coop_switch) CHECK(info.rule != SettingRule::host_wins);
  }
}

TEST_CASE("group settings: co-op switches are read as their code reads them (ORM)") {
  const auto none = switch_entries(env_of({}), std::filesystem::temp_directory_path());
  CHECK(value_of(none, "LICHII_COOP_ITEMS") == "on");        // on unless 0
  CHECK(value_of(none, "LICHII_COOP_STORY") == "on");
  CHECK(value_of(none, "LICHII_COOP_TIME") == "on");
  CHECK(value_of(none, "LICHII_COOP_OBJECTS") == "off");     // off unless set
  CHECK(value_of(none, "LICHII_COOP_DOWNED") == "off");
  CHECK(value_of(none, "LICHII_ORM_STORY_EVENTS") == "off");
  CHECK(value_of(none, "LICHII_COOP_DUNGEON_LISTS") == "off");
  CHECK(value_of(none, "LICHII_COOP_LIVE_EVENT_NAMES") == "all");
  CHECK(value_of(none, "LICHII_COOP_BOSS_PULL_DOWNED") == "off");
  // Local switches are never sent.
  CHECK(value_of(none, "LICHII_COOP_PUPPET") == "<none>");
  CHECK(value_of(none, "LICHII_COOP_SPECTATOR") == "<none>");
  CHECK(value_of(none, "LICHII_COOP_HOST_SAVE") == "<none>");
  CHECK(value_of(none, "LICHII_ORM_GROUP_SETTINGS") == "<none>");
  // Sorted, valid: they go into one GROUP_SETTINGS.
  CHECK(std::is_sorted(none.begin(), none.end(),
                       [](const SettingEntry& a, const SettingEntry& b) { return a.key < b.key; }));
  std::vector<std::uint8_t> out;
  CHECK(coop::protocol::orm_encode_group_settings({0, none}, out) == coop::protocol::Error::none);

  const auto kit = kit_switches();
  CHECK(value_of(kit, "LICHII_COOP_OBJECTS") == "on");
  CHECK(value_of(kit, "LICHII_COOP_DOWNED") == "on");
  CHECK(value_of(kit, "LICHII_ORM_STORY_EVENTS") == "on");
  CHECK(value_of(kit, "LICHII_COOP_BOSS_PULL_DOWNED") == "on");  // unset: follows DOWNED=1
  CHECK(value_of(kit, "LICHII_COOP_BOSS_RESPAWN") == "on");

  const auto base = std::filesystem::temp_directory_path();
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_ITEMS", "0"}}), base), "LICHII_COOP_ITEMS") == "off");
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_DOWNED", "0"}}), base), "LICHII_COOP_DOWNED") == "off");
  // The boss rooms read DOWNED == "1" exactly; any other "on" leaves them off.
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_DOWNED", "yes"}}), base), "LICHII_COOP_BOSS_RESPAWN") ==
        "off");
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_DOWNED", "1"}, {"LICHII_COOP_BOSS_RESPAWN", "0"}}), base),
                 "LICHII_COOP_BOSS_RESPAWN") == "off");
  // The story cutscenes' old name (kits before test9) still counts, the new one wins.
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_STORY", "1"}}), base), "LICHII_ORM_STORY_EVENTS") == "on");
  CHECK(value_of(switch_entries(env_of({{"LICHII_COOP_STORY", "1"}, {"LICHII_ORM_STORY_EVENTS", "0"}}), base),
                 "LICHII_ORM_STORY_EVENTS") == "off");
  // Long or spaced text goes as a fingerprint.
  const auto names = switch_entries(env_of({{"LICHII_COOP_LIVE_EVENT_NAMES", "a, b"}}), base);
  CHECK(value_of(names, "LICHII_COOP_LIVE_EVENT_NAMES").rfind("h:", 0) == 0);
}

TEST_CASE("group settings: a list's fingerprint follows its text, not its line ends or folder (ORM)") {
  TempDir a, b;
  a.write("shared_story.txt", "Dm_One\nDm_Two\n");
  b.write("shared_story.txt", "Dm_One\r\nDm_Two\r\n");  // a copy saved with Windows line ends
  const std::string fa = list_fingerprint("shared_story.txt", a.path, false);
  CHECK(fa.rfind("on:", 0) == 0);
  CHECK(fa.size() == 11);
  CHECK(fa == list_fingerprint("shared_story.txt", b.path, false));
  CHECK(fa == list_fingerprint((a.path / "shared_story.txt").string(), b.path, false));  // absolute path
  b.write("shared_story.txt", "Dm_One\n");
  CHECK(fa != list_fingerprint("shared_story.txt", b.path, false));
  CHECK(fa != list_fingerprint("missing.txt", a.path, false));
  CHECK(list_fingerprint("", a.path, false) == "off");
  CHECK(list_fingerprint(" , ", a.path, false) == "off");

  // A folder stands for its shared_dungeon_*.txt by name, as the lists' loader reads it.
  a.write("shared_dungeon_child.txt", "child\n");
  a.write("shared_dungeon_adult.txt", "adult\n");
  a.write("notes.txt", "not a list\n");
  CHECK(list_fingerprint(".", a.path, true) ==
        list_fingerprint("shared_dungeon_adult.txt, shared_dungeon_child.txt", a.path, true));
  CHECK(list_fingerprint(".", a.path, true) !=
        list_fingerprint("shared_dungeon_child.txt,shared_dungeon_adult.txt", a.path, true));  // order counts
}

TEST_CASE("group settings: a guest plays with the host's amiibo and gets its own back after (ORM)") {
  GroupSettings guest(kit_switches());
  FakeOptions game = game_with(false, "NotUse");
  std::uint64_t t = 1'000'000;

  // First step: our settings go out, with our game's options.
  auto s = guest.step(&game, room(2, 1), t);
  REQUIRE(s.send.has_value());
  CHECK(s.send->player_number == 0);
  CHECK(value_of(s.send->entries, "OptionData.IsUseAmiibo") == "off");
  CHECK(value_of(s.send->entries, "OptionData.ZeldaNoteState") == "NotUse");
  CHECK(game.writes.empty());  // nothing from the host yet

  // The host's arrive: amiibo on, ZELDA NOTES on.
  auto host = kit_switches();
  host.push_back({"OptionData.IsUseAmiibo", "on"});
  host.push_back({"OptionData.ZeldaNoteState", "Use"});
  std::sort(host.begin(), host.end(), [](const auto& x, const auto& y) { return x.key < y.key; });
  guest.received({1, host});
  s = guest.step(&game, room(2, 1), t += 10'000);  // sooner than kStepUs: a message came
  CHECK(game.bools[kAmiibo]);
  CHECK(game.enums[kZeldaNote] == flag_hash("Use"));
  CHECK(game.writes.size() == 2);
  CHECK(guest.own_values().at("OptionData.IsUseAmiibo") == "off");
  CHECK(guest.own_values().at("OptionData.ZeldaNoteState") == "NotUse");
  REQUIRE(s.chat.size() == 2);
  CHECK(s.chat[0] == "Group setting from the host: amiibo on");
  CHECK(s.log[0] == "OptionData.IsUseAmiibo: off -> on (the host's, Link (player 1))");
  CHECK(s.chat[1] == "Group setting from the host: ZELDA NOTES Use");

  // The next step sends ours again, now with the host's values; nothing more is written.
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  REQUIRE(s.send.has_value());
  CHECK(value_of(s.send->entries, "OptionData.IsUseAmiibo") == "on");
  CHECK(game.writes.size() == 2);
  CHECK(s.chat.empty());
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  CHECK_FALSE(s.send.has_value());

  // The player turns amiibo off in the menu: back to the host's, with a notice.
  game.bools[kAmiibo] = false;
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  CHECK(game.bools[kAmiibo]);
  REQUIRE(s.chat.size() == 1);
  CHECK(s.chat[0] == "amiibo stays on: the host decides it");
  CHECK(guest.own_values().at("OptionData.IsUseAmiibo") == "off");  // still the player's first value

  // Disconnected: the player's own values go back.
  RoomView gone;
  s = guest.step(&game, gone, t += GroupSettings::kStepUs);
  CHECK_FALSE(game.bools[kAmiibo]);
  CHECK(game.enums[kZeldaNote] == flag_hash("NotUse"));
  CHECK(guest.own_values().empty());
  CHECK(s.log.size() == 2);
}

TEST_CASE("group settings: what is not written into a guest's game (ORM)") {
  GroupSettings guest(kit_switches());
  FakeOptions game = game_with(false, "NotUse");
  std::uint64_t t = 1'000'000;
  (void)guest.step(&game, room(2, 1), t);

  // The host never chose ZELDA NOTES: the guest's choice stays.
  guest.received({1, {{"OptionData.ZeldaNoteState", "NotSet"}}});
  auto s = guest.step(&game, room(2, 1), t += 1);
  CHECK(game.enums[kZeldaNote] == flag_hash("NotUse"));
  // The host's game is not up (title screen): it sends no options; nothing changes.
  guest.received({1, kit_switches()});
  s = guest.step(&game, room(2, 1), t += 1);
  CHECK(game.writes.empty());
  // Our game is not up: nothing to write, and nothing taken as ours.
  guest.received({1, {{"OptionData.IsUseAmiibo", "on"}}});
  s = guest.step(nullptr, room(2, 1), t += 1);
  CHECK(guest.own_values().empty());
  // A host value this build does not know is not written.
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  CHECK(game.bools[kAmiibo]);
  guest.received({1, {{"OptionData.IsUseAmiibo", "maybe"}}});
  game.writes.clear();
  s = guest.step(&game, room(2, 1), t += 1);
  CHECK(game.writes.empty());
  // A write the game's queue refuses is tried again, told once.
  GroupSettings other(kit_switches());
  FakeOptions busy = game_with(false, "NotUse");
  busy.fail = true;
  other.received({1, plus(kit_switches(), "OptionData.IsUseAmiibo", "on")});
  s = other.step(&busy, room(2, 1), t);
  CHECK(s.log.size() == 1);
  CHECK(other.own_values().empty());
  s = other.step(&busy, room(2, 1), t += GroupSettings::kStepUs);
  CHECK(s.log.empty());
  busy.fail = false;
  s = other.step(&busy, room(2, 1), t += GroupSettings::kStepUs);
  CHECK(busy.bools[kAmiibo]);
  CHECK(other.own_values().at("OptionData.IsUseAmiibo") == "off");
}

TEST_CASE("group settings: a switch not as the host's is told once, on both sides (ORM)") {
  auto mine = kit_switches();
  auto theirs = switch_entries(env_of({{"LICHII_COOP_OBJECTS", "1"},
                                       {"LICHII_COOP_SPAWN", "1"},
                                       {"LICHII_COOP_NPC", "1"},
                                       {"LICHII_ORM_STORY_EVENTS", "1"}}),  // DOWNED left out
                               std::filesystem::temp_directory_path());
  FakeOptions game = game_with(false, "NotUse");
  std::uint64_t t = 1'000'000;

  // The guest's side: the host (player 1) has downed on, we off.
  GroupSettings guest(theirs);
  (void)guest.step(&game, room(3, 1), t);
  guest.received({1, mine});
  auto s = guest.step(&game, room(3, 1), t += 1);
  REQUIRE(s.chat.size() == 3);  // DOWNED, and the boss room pull and respawn that follow it
  CHECK(s.chat[0] == "Not as the host: downed, not dead is on there, off here. Use the host's coop-options.ini");
  CHECK(s.log[0] == "not as the host (Link (player 1)): LICHII_COOP_DOWNED host on, here off");
  s = guest.step(&game, room(3, 1), t += GroupSettings::kStepUs);
  CHECK(s.chat.empty());  // once
  // A rejoin tells again.
  guest.connection_new();
  guest.received({1, mine});
  s = guest.step(&game, room(3, 1), t += 1);
  CHECK(s.chat.size() == 3);

  // The host's side: player 3 (Saria) has downed off.
  GroupSettings host(mine);
  (void)host.step(&game, room(1, 1), t);
  host.received({3, theirs});
  s = host.step(&game, room(1, 1), t += 1);
  REQUIRE(s.chat.size() == 3);
  CHECK(s.chat[0] == "Saria (player 3) has downed, not dead off (yours on). Same coop-options.ini for all");
  CHECK(game.writes.empty());  // the host never takes a guest's values
  s = host.step(&game, room(1, 1), t += GroupSettings::kStepUs);
  CHECK(s.chat.empty());
  // Saria leaves and comes back with the same file: told again.
  host.player_left(3);
  host.received({3, theirs});
  s = host.step(&game, room(1, 1), t += 1);
  CHECK(s.chat.size() == 3);

  // A list file that differs shows as a list, not as raw text.
  GroupSettings g2({{"LICHII_ORM_STORY_LIST", "on:00000001"}});
  (void)g2.step(nullptr, room(2, 1), t);
  g2.received({1, {{"LICHII_ORM_STORY_LIST", "on:00000002"}}});
  s = g2.step(nullptr, room(2, 1), t += 1);
  REQUIRE(s.chat.size() == 1);
  CHECK(s.chat[0] ==
        "Not as the host: the story cutscene list is a list (00000002) there, a list (00000001) here. Use the "
        "host's coop-options.ini");
}

TEST_CASE("group settings: when ours are sent, and the host that sends none (ORM)") {
  GroupSettings guest(kit_switches());
  FakeOptions game = game_with(false, "NotUse");
  std::uint64_t t = 1'000'000;
  // Not connected: nothing is sent.
  auto s = guest.step(&game, RoomView{}, t);
  CHECK_FALSE(s.send.has_value());
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  CHECK(s.send.has_value());
  // Within kStepUs nothing runs; later, nothing changed: nothing sent.
  s = guest.step(&game, room(2, 1), t += 1);
  CHECK_FALSE(s.send.has_value());
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  CHECK_FALSE(s.send.has_value());
  // Our game's option changes: sent again.
  game.bools[kAmiibo] = true;
  s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
  REQUIRE(s.send.has_value());
  CHECK(value_of(s.send->entries, "OptionData.IsUseAmiibo") == "on");
  // A failed send goes again on the next step.
  guest.send_failed();
  s = guest.step(&game, room(2, 1), t += 1);
  CHECK(s.send.has_value());
  // After a WELCOME (a reconnection), again.
  guest.connection_new();
  s = guest.step(&game, room(2, 1), t += 1);
  CHECK(s.send.has_value());

  // No settings from the host for kNoHostNoteUs: said once in the log (an older server drops type 246).
  std::size_t notes = 0;
  for (int i = 0; i < 40; ++i) {
    s = guest.step(&game, room(2, 1), t += GroupSettings::kStepUs);
    notes += s.log.size();
  }
  CHECK(notes == 1);
}

TEST_CASE("group settings: a guest who becomes the host gets its own values back (ORM)") {
  GroupSettings g(kit_switches());
  FakeOptions game = game_with(false, "NotUse");
  std::uint64_t t = 1'000'000;
  g.received({1, {{"OptionData.IsUseAmiibo", "on"}}});
  (void)g.step(&game, room(2, 1), t);
  CHECK(game.bools[kAmiibo]);
  // The host leaves; we (player 2) host now.
  g.player_left(1);
  auto s = g.step(&game, room(2, 2), t += GroupSettings::kStepUs);
  CHECK_FALSE(game.bools[kAmiibo]);
  CHECK(g.own_values().empty());
  REQUIRE(s.log.size() == 1);
  CHECK(s.log[0] == "OptionData.IsUseAmiibo back to this player's own: off");
  // Another host takes over instead (player 3): its values count, ours stay kept.
  GroupSettings h(kit_switches());
  FakeOptions game2 = game_with(false, "NotUse");
  h.received({1, {{"OptionData.IsUseAmiibo", "on"}}});
  h.received({3, {{"OptionData.IsUseAmiibo", "off"}}});
  (void)h.step(&game2, room(2, 1), t);
  CHECK(game2.bools[kAmiibo]);
  h.player_left(1);
  (void)h.step(&game2, room(2, 3), t += GroupSettings::kStepUs);
  CHECK_FALSE(game2.bools[kAmiibo]);  // player 3's value, which is also ours
  CHECK(h.own_values().empty());
}
