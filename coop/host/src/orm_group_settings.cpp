#include "coop/host/orm_group_settings.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_messages.hpp"

namespace coop::host::orm {

namespace {

using protocol::SettingEntry;
using K = SettingKind;
using R = SettingRule;
using S = SwitchRead;

// GameDataList (romfs GameData/GameDataList.Product.110), save file 1 (the
// player's option.sav): every OptionData flag of version 1.1.0, with the value
// names of its Enum ones in the game's order. Checked with tools/romfs.
constexpr std::string_view kZeldaNote[] = {"NotSet", "Use", "NotUse"};

constexpr SettingInfo kTable[] = {
    // World: they bring things into the group's world (items, bonuses).
    {"OptionData.IsUseAmiibo", K::game_bool, R::host_wins, S::none, "amiibo"},
    {"OptionData.ZeldaNoteState", K::game_enum, R::host_wins, S::none, "ZELDA NOTES"},
    // Local: presentation, or the controls of the player's own Link (in this
    // co-op each game runs its own Link: lock-on and jump buttons change no
    // one else's world).
    {"OptionData.IsCameraUpDownReverse", K::game_bool, R::local, S::none, "inverted vertical camera"},
    {"OptionData.IsCameraRightLeftReverse", K::game_bool, R::local, S::none, "inverted horizontal camera"},
    {"OptionData.CameraRotateSpeed", K::game_enum, R::local, S::none, "camera speed"},
    {"OptionData.IsUseGyro", K::game_bool, R::local, S::none, "motion aiming"},
    {"OptionData.IsChangeJumpButton", K::game_bool, R::local, S::none, "swapped jump controls"},
    {"OptionData.DisplayMode", K::game_enum, R::local, S::none, "in-game interface"},
    {"OptionData.IsDisplayTextRuby", K::game_bool, R::local, S::none, "ruby text"},
    {"OptionData.VoiceLanguage", K::game_enum, R::local, S::none, "voice language"},
    {"OptionData.LockOnType", K::game_enum, R::local, S::none, "targeting type"},
    {"OptionData.OcarinaSongEstimateMode", K::game_enum, R::local, S::none, "ocarina mic input"},
    {"OptionData.OcarinaKeyAssign", K::game_enum, R::local, S::none, "ocarina controls"},
    {"OptionData.VoiceSubtitle", K::game_bool, R::local, S::none, "subtitles"},
    {"OptionData.VoiceSubtitleSpeakerName", K::game_bool, R::local, S::none, "speaker names"},
    {"OptionData.ControllerRumble", K::game_enum, R::local, S::none, "vibration"},

    // World: co-op switches (coop-options.ini), each read as its code reads it.
    {"LICHII_COOP_ITEMS", K::coop_switch, R::must_match, S::on_unless_zero, "shared items"},
    {"LICHII_COOP_FLAGS", K::coop_switch, R::must_match, S::list_files, "the shared flags file"},
    {"LICHII_COOP_DUNGEON", K::coop_switch, R::must_match, S::on_unless_zero, "shared dungeons"},
    {"LICHII_COOP_DUNGEON_LISTS", K::coop_switch, R::must_match, S::dungeon_lists, "the world lists"},
    {"LICHII_COOP_STORY", K::coop_switch, R::must_match, S::on_unless_zero, "shared story flags"},
    {"LICHII_ORM_STORY_EVENTS", K::coop_switch, R::must_match, S::story_events, "story cutscenes for all",
     "LICHII_COOP_STORY"},
    {"LICHII_ORM_STORY_LIST", K::coop_switch, R::must_match, S::list_files, "the story cutscene list",
     "LICHII_COOP_STORY_LIST"},
    {"LICHII_COOP_TALK", K::coop_switch, R::must_match, S::list_files, "the talk flag list"},
    {"LICHII_COOP_FLAGS_DEFER", K::coop_switch, R::must_match, S::off_unless_set, "talk flag rules"},
    {"LICHII_COOP_FLAGS_DEFER_LIST", K::coop_switch, R::must_match, S::list_files, "the talk flag rules file"},
    {"LICHII_COOP_PICKUPS", K::coop_switch, R::must_match, S::on_unless_zero, "one-time pickups"},
    {"LICHII_COOP_TIME", K::coop_switch, R::must_match, S::on_unless_zero, "the host's time of day"},
    {"LICHII_COOP_OBJECTS", K::coop_switch, R::must_match, S::off_unless_set, "shared enemies and objects"},
    {"LICHII_COOP_OBJECTS_HURT", K::coop_switch, R::must_match, S::off_unless_set, "enemy hits by life writes"},
    {"LICHII_COOP_OBJECTS_HIT_REPLAY", K::coop_switch, R::must_match, S::on_unless_zero, "enemy hit replay"},
    {"LICHII_COOP_HEARTS_TO_LINK", K::coop_switch, R::must_match, S::on_unless_zero, "heart containers to Link"},
    {"LICHII_COOP_SPAWN", K::coop_switch, R::must_match, S::off_unless_set, "shared seeds, nuts and bombs"},
    {"LICHII_COOP_DROPS", K::coop_switch, R::must_match, S::on_unless_zero, "shared drops"},
    {"LICHII_COOP_NPC", K::coop_switch, R::must_match, S::off_unless_set, "townspeople you talk to"},
    {"LICHII_COOP_LIVE_EVENTS", K::coop_switch, R::must_match, S::on_unless_zero, "live dungeon events"},
    {"LICHII_COOP_LIVE_EVENT_NAMES", K::coop_switch, R::must_match, S::text, "the live event names"},
    {"LICHII_COOP_DOWNED", K::coop_switch, R::must_match, S::off_unless_set, "downed, not dead"},
    {"LICHII_COOP_BOSS", K::coop_switch, R::must_match, S::on_unless_zero, "boss rooms together"},
    {"LICHII_COOP_BOSS_PULL_DOWNED", K::coop_switch, R::must_match, S::downed_auto, "the boss room pull"},
    {"LICHII_COOP_BOSS_RESPAWN", K::coop_switch, R::must_match, S::downed_auto, "the boss room respawn"},
    {"LICHII_COOP_CHEATS", K::coop_switch, R::must_match, S::off_unless_set, "the item cheat window"},
    {"LICHII_COOP_OBJECTS_CHEAT", K::coop_switch, R::must_match, S::off_unless_set, "the objects cheat"},

    // Local co-op switches: what this player sees, or this PC's role.
    {"LICHII_COOP_PUPPET", K::coop_switch, R::local, S::none, "the others as Link"},
    {"LICHII_COOP_FAIRY", K::coop_switch, R::local, S::none, "Navi on the others"},
    {"LICHII_COOP_SPECTATOR", K::coop_switch, R::local, S::none, "spectator camera"},
    {"LICHII_COOP_HUD", K::coop_switch, R::local, S::none, "the co-op HUD (upstream: hearts, game look)"},
    {"LICHII_ORM_HUD", K::coop_switch, R::local, S::none, "the HUD drawn in the picture"},
    {"LICHII_COOP_PANEL", K::coop_switch, R::local, S::none, "the co-op panel"},
    // ORM each player's own HUD (oot-remake docs/HUD_AJUSTES.md, lichii/hook/coop_hud_settings.h).
    {"LICHII_ORM_HUD_NAMES", K::coop_switch, R::local, S::none, "the names over the others"},
    {"LICHII_ORM_HUD_RUPEES", K::coop_switch, R::local, S::none, "everyone's rupees on screen"},
    {"LICHII_ORM_HUD_RUPEES_AT", K::coop_switch, R::local, S::none, "where the rupees go"},
    {"LICHII_ORM_HUD_PANEL_AT", K::coop_switch, R::local, S::none, "where the panel goes"},
    {"LICHII_ORM_HUD_SIZE", K::coop_switch, R::local, S::none, "the HUD's text size"},
    {"LICHII_ORM_HUD_CHAT_LINES", K::coop_switch, R::local, S::none, "the chat lines shown"},
    {"LICHII_COOP_NPC_POSE", K::coop_switch, R::local, S::none, "townsperson pose"},
    {"LICHII_COOP_TELEPORT", K::coop_switch, R::local, S::none, "/teleport"},
    {"LICHII_COOP_HOST_SAVE", K::coop_switch, R::local, S::none, "the co-op save folder"},
    {"LICHII_ORM_GROUP_SETTINGS", K::coop_switch, R::local, S::none, "group settings"},
};

const std::string* find_value(const std::vector<SettingEntry>& entries, std::string_view key) {
  for (const SettingEntry& e : entries) {
    if (e.key == key) return &e.value;
  }
  return nullptr;
}

void sort_entries(std::vector<SettingEntry>& entries) {
  std::sort(entries.begin(), entries.end(),
            [](const SettingEntry& a, const SettingEntry& b) { return a.key < b.key; });
}

std::uint32_t fnv1a(std::uint32_t h, std::string_view bytes) {
  for (const char c : bytes) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

std::string hex8(std::uint32_t v) {
  char buf[9];
  std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(v));
  return std::string(buf, 8);
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

// How a value reads in the chat: a list's fingerprint as "a list (...)".
std::string shown(const std::string& value) {
  if (value.rfind("on:", 0) == 0) return "a list (" + value.substr(3) + ")";
  if (value.empty()) return "(none)";
  return value;
}

}  // namespace

std::span<const SettingInfo> setting_table() { return kTable; }

const SettingInfo* find_setting(std::string_view key) {
  for (const SettingInfo& info : kTable) {
    if (info.key == key) return &info;
  }
  return nullptr;
}

std::span<const std::string_view> enum_value_names(std::string_view key) {
  if (key == "OptionData.ZeldaNoteState") return kZeldaNote;
  return {};
}

bool enum_value_not_chosen(std::string_view key, std::string_view value) {
  return key == "OptionData.ZeldaNoteState" && value == kZeldaNote[0];
}

std::string list_fingerprint(std::string_view paths, const std::filesystem::path& base,
                             bool dungeon_folders) {
  std::uint32_t h = 2166136261u;
  bool any = false;
  const auto add = [&](std::string_view name, std::string_view text) {
    h = fnv1a(h, name);
    h = fnv1a(h, std::string_view("\0", 1));
    h = fnv1a(h, text);
    h = fnv1a(h, std::string_view("\0", 1));
  };
  std::size_t at = 0;
  while (at <= paths.size()) {
    std::size_t comma = paths.find(',', at);
    if (comma == std::string_view::npos) comma = paths.size();
    const std::string_view entry = trim(paths.substr(at, comma - at));
    at = comma + 1;
    if (entry.empty()) continue;
    any = true;
    std::filesystem::path p{std::string(entry)};
    if (p.is_relative()) p = base / p;
    std::error_code ec;
    if (dungeon_folders && std::filesystem::is_directory(p, ec)) {
      std::vector<std::filesystem::path> files;
      for (std::filesystem::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("shared_dungeon_", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".txt") == 0) {
          files.push_back(it->path());
        }
      }
      std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.filename().string() < b.filename().string();
      });
      for (const auto& f : files) {
        const auto text = read_text(f);
        add(f.filename().string(), text ? *text : std::string());
      }
      continue;
    }
    if (const auto text = read_text(p)) {
      add(p.filename().string(), *text);
    } else {
      add("missing:" + p.filename().string(), {});
    }
  }
  if (!any) return "off";
  return "on:" + hex8(h);
}

std::vector<SettingEntry> switch_entries(const EnvGet& get, const std::filesystem::path& base) {
  const auto read = [&](const SettingInfo& info) -> std::optional<std::string> {
    std::optional<std::string> v = get(std::string(info.key).c_str());
    if (!v && !info.old_name.empty() && info.read != SwitchRead::story_events) {
      v = get(std::string(info.old_name).c_str());
    }
    if (v && v->empty()) v.reset();
    return v;
  };
  std::vector<SettingEntry> out;
  for (const SettingInfo& info : kTable) {
    if (info.kind != SettingKind::coop_switch || info.rule != SettingRule::must_match) continue;
    const std::optional<std::string> v = read(info);
    std::string value;
    switch (info.read) {
      case SwitchRead::none:
        continue;
      case SwitchRead::on_unless_zero:
        value = v && *v == "0" ? "off" : "on";
        break;
      case SwitchRead::off_unless_set:
        value = v && *v != "0" ? "on" : "off";
        break;
      case SwitchRead::list_files:
        value = v ? list_fingerprint(*v, base, false) : "off";
        break;
      case SwitchRead::dungeon_lists:
        value = v ? list_fingerprint(*v, base, true) : "off";
        break;
      case SwitchRead::text:
        if (!v) {
          value = "all";
        } else if (protocol::valid_setting_value(*v)) {
          value = *v;
        } else {
          value = "h:" + hex8(fnv1a(2166136261u, *v));
        }
        break;
      case SwitchRead::story_events: {
        // As OrmStoryEvents::SwitchOn: the new name when set, else the old.
        std::optional<std::string> old = get(std::string(info.old_name).c_str());
        if (old && old->empty()) old.reset();
        const bool on = v ? *v != "0" : (old && *old != "0");
        value = on ? "on" : "off";
        break;
      }
      case SwitchRead::downed_auto: {
        // As coop_boss_game.cpp: unset follows LICHII_COOP_DOWNED == "1".
        const std::optional<std::string> downed = get("LICHII_COOP_DOWNED");
        const bool on = v ? *v != "0" : (downed && *downed == "1");
        value = on ? "on" : "off";
        break;
      }
    }
    out.push_back({std::string(info.key), std::move(value)});
  }
  sort_entries(out);
  return out;
}

// ---------------------------------------------------------------- GroupSettings

GroupSettings::GroupSettings(std::vector<SettingEntry> switches) : switches_(std::move(switches)) {
  sort_entries(switches_);
  ours_ = switches_;
}

void GroupSettings::received(const protocol::GroupSettings& m) {
  if (m.player_number < 1 || m.player_number > protocol::kMaxPlayers) return;
  peers_[m.player_number] = m.entries;
  force_step_ = true;
}

void GroupSettings::player_left(std::uint8_t number) {
  peers_.erase(number);
  const std::string tag = "p|" + std::to_string(number) + "|";
  for (auto it = told_.begin(); it != told_.end();) {
    it = it->rfind(tag, 0) == 0 ? told_.erase(it) : std::next(it);
  }
}

void GroupSettings::connection_new() {
  peers_.clear();
  told_.clear();
  sent_.reset();
  send_due_ = true;
  force_step_ = true;
  guest_since_us_ = 0;
  no_host_told_ = false;
}

void GroupSettings::send_failed() {
  send_due_ = true;
  force_step_ = true;
}

void GroupSettings::read_game(GameOptions* game) {
  ours_ = switches_;
  if (game != nullptr) {
    for (const SettingInfo& info : kTable) {
      if (info.rule != SettingRule::host_wins) continue;
      const std::uint32_t hash = protocol::flag_hash(info.key);
      if (info.kind == SettingKind::game_bool) {
        if (const auto v = game->get_bool(hash)) ours_.push_back({std::string(info.key), *v ? "on" : "off"});
      } else if (info.kind == SettingKind::game_enum) {
        const auto v = game->get_enum(hash);
        if (!v) continue;
        for (const std::string_view name : enum_value_names(info.key)) {
          if (protocol::flag_hash(name) == *v) {
            ours_.push_back({std::string(info.key), std::string(name)});
            break;
          }
        }
      }
    }
  }
  sort_entries(ours_);
}

bool GroupSettings::write_game(GameOptions& game, const SettingInfo& info, const std::string& value) {
  const std::uint32_t hash = protocol::flag_hash(info.key);
  if (info.kind == SettingKind::game_bool) {
    if (value != "on" && value != "off") return false;
    return game.set_bool(hash, value == "on");
  }
  if (info.kind == SettingKind::game_enum) {
    const auto names = enum_value_names(info.key);
    if (std::find(names.begin(), names.end(), value) == names.end()) return false;
    return game.set_enum(hash, protocol::flag_hash(value));
  }
  return false;
}

std::string GroupSettings::name_of(const RoomView& room, std::uint8_t number) const {
  const auto it = room.names.find(number);
  const std::string n = std::to_string(number);
  if (it == room.names.end() || it->second.empty()) return "player " + n;
  return it->second + " (player " + n + ")";
}

void GroupSettings::restore(GameOptions* game, GroupStep& out) {
  if (game == nullptr) return;  // no game: written back once one runs
  applied_.clear();
  for (auto it = own_.begin(); it != own_.end();) {
    const SettingInfo* info = find_setting(it->first);
    const std::string* now = find_value(ours_, it->first);
    if (info == nullptr || (now != nullptr && *now == it->second)) {
      it = own_.erase(it);
      continue;
    }
    if (write_game(*game, *info, it->second)) {
      out.log.push_back(it->first + " back to this player's own: " + it->second);
      it = own_.erase(it);
    } else {
      ++it;  // the game's queue was busy: next step
    }
  }
}

void GroupSettings::as_guest(GameOptions* game, const RoomView& room, std::uint64_t now_us,
                             GroupStep& out) {
  if (guest_since_us_ == 0) guest_since_us_ = now_us == 0 ? 1 : now_us;
  const auto peer = peers_.find(room.host);
  if (peer == peers_.end()) {
    if (!no_host_told_ && now_us >= guest_since_us_ && now_us - guest_since_us_ >= kNoHostNoteUs) {
      out.log.push_back("no settings from the host (" + name_of(room, room.host) +
                        ") yet: an older server or game, or LICHII_ORM_GROUP_SETTINGS=0 there? Not checked");
      no_host_told_ = true;
    }
    return;
  }
  const std::vector<SettingEntry>& host = peer->second;
  const std::string host_name = name_of(room, room.host);
  for (const SettingInfo& info : kTable) {
    if (info.rule == SettingRule::local) continue;
    const std::string key(info.key);
    const std::string* hv = find_value(host, key);
    const std::string* ov = find_value(ours_, key);
    if (info.rule == SettingRule::host_wins) {
      // Both games must be up (the host's sends no game option before).
      if (hv == nullptr || ov == nullptr || game == nullptr || *hv == *ov) continue;
      if (enum_value_not_chosen(key, *hv)) continue;
      const bool first = own_.find(key) == own_.end();
      if (!write_game(*game, info, *hv)) {
        if (told_.insert("w|" + key).second) {
          out.log.push_back("could not write " + key + " = " + *hv + " yet; trying again");
        }
        continue;
      }
      // The player changed it back in the menu, or the host's value is new.
      const auto applied = applied_.find(key);
      const bool player_changed = applied != applied_.end() && applied->second == *hv;
      applied_[key] = *hv;
      if (first) own_[key] = *ov;
      if (own_[key] == *hv) own_.erase(key);  // the host's is our own: nothing to give back
      out.log.push_back(key + ": " + *ov + " -> " + *hv + " (the host's, " + host_name + ")");
      out.chat.push_back(player_changed ? std::string(info.what) + " stays " + *hv + ": the host decides it"
                                        : "Group setting from the host: " + std::string(info.what) + " " + *hv);
      continue;
    }
    // must_match
    if (ov == nullptr) continue;  // a setting this build does not send
    if (hv == nullptr) {
      if (told_.insert("h|" + key).second) {
        out.log.push_back("the host does not send " + key + " (an older build?)");
      }
      continue;
    }
    if (*hv == *ov) continue;
    if (told_.insert("g|" + std::to_string(room.host) + "|" + key + "|" + *hv + "|" + *ov).second) {
      out.log.push_back("not as the host (" + host_name + "): " + key + " host " + *hv + ", here " + *ov);
      out.chat.push_back("Not as the host: " + std::string(info.what) + " is " + shown(*hv) + " there, " +
                         shown(*ov) + " here. Use the host's coop-options.ini");
    }
  }
}

void GroupSettings::as_host(const RoomView& room, GroupStep& out) {
  for (const auto& [number, entries] : peers_) {
    if (number == room.self) continue;
    for (const SettingInfo& info : kTable) {
      if (info.rule != SettingRule::must_match) continue;
      const std::string key(info.key);
      const std::string* pv = find_value(entries, key);
      const std::string* ov = find_value(ours_, key);
      if (pv == nullptr || ov == nullptr || *pv == *ov) continue;
      if (told_.insert("p|" + std::to_string(number) + "|" + key + "|" + *pv + "|" + *ov).second) {
        const std::string who = name_of(room, number);
        out.log.push_back(who + " is not as the host: " + key + " there " + *pv + ", host " + *ov);
        out.chat.push_back(who + " has " + std::string(info.what) + " " + shown(*pv) + " (yours " + shown(*ov) +
                           "). Same coop-options.ini for all");
      }
    }
  }
}

GroupStep GroupSettings::step(GameOptions* game, const RoomView& room, std::uint64_t now_us) {
  GroupStep out;
  if (!force_step_ && stepped_ && now_us >= last_step_us_ && now_us - last_step_us_ < kStepUs) return out;
  force_step_ = false;
  stepped_ = true;
  last_step_us_ = now_us;
  read_game(game);
  if (!room.connected) {
    restore(game, out);
    peers_.clear();
    guest_since_us_ = 0;
    return out;
  }
  if (send_due_ || !sent_ || *sent_ != ours_) {
    out.send = protocol::GroupSettings{protocol::kPlayerSelf, ours_};
    sent_ = ours_;
    send_due_ = false;
  }
  if (room.self == 0 || room.host == 0) return out;
  if (room.self == room.host) {
    restore(game, out);
    guest_since_us_ = 0;
    as_host(room, out);
  } else {
    as_guest(game, room, now_us, out);
  }
  return out;
}

}  // namespace coop::host::orm
