// ORM: see coop/host/doors.hpp and oot-remake docs/PUERTAS.md.
#include "coop/host/doors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace coop::host {

namespace {

// Read from the romfs (Pack/Actor/<name>.pack.zs, its ASRef), 2026-11-11.
// AS "Door" (Push_L_ByPlayer, Close_L_ByPlayer, ...) or "DoorKokiri" (the same
// names). Obj_Gerudo_SwitchShutter_A_08 is a hinged door despite its name.
constexpr auto kHinge = std::to_array<std::string_view>({
    "Obj_Base_Door_A_01",       "Obj_Door_A_02",           "Obj_Door_A_05",
    "Obj_Door_A_06",            "Obj_Door_NPCHouseA",      "Obj_Door_NPCHouseB",
    "Obj_Door_PodsRoom",        "Obj_Door_ShopAnything",   "Obj_Door_ShopBombBoring",
    "Obj_Door_ShopBombChu",     "Obj_Door_ShopHitTarget",  "Obj_Door_ShopMask",
    "Obj_Door_ShopMedicine",    "Obj_Door_ShopTreasureBox", "Obj_Graveyard_Door_A_01",
    "Obj_Kakariko_Door_A_01",   "Obj_Kakariko_Door_A_02",  "Obj_Kakariko_Door_A_03",
    "Obj_Kakariko_Door_A_04",   "Obj_Kakariko_Door_A_05",  "Obj_Kakariko_Door_A_06",
    "Obj_Kakariko_Door_A_07",   "Obj_Kakariko_Door_A_08",  "Obj_Kokiri_Door_A_01",
    "Obj_Kokiri_Door_A_03",     "Obj_LakeHrl_Door_A_01",   "Obj_LakeHrl_Door_B_01",
    "Obj_LonLon_Door_A_01",     "Obj_LonLon_Door_A_03",    "Obj_Gerudo_SwitchShutter_A_08",
});
// AS "DoorDungeon" (Push_L, Close_L, ...); the Fire Temple's also "DoorDungeonBig".
constexpr auto kDungeon = std::to_array<std::string_view>({
    "Obj_Base_Door_A_02", "Obj_FireTemple_Door_A_01", "Obj_ForestTemple_Door_A_01",
    "Obj_ShadowTemple_Door_A_01", "Obj_HrlCity_Shop_Door_A_01",
});
// The SwitchShutters whose AS has ToOpen (Obj_SwitchShutter_A_01/_A_02/_A_03,
// Obj_Base_SwitchShutter_A_02 or their own). Those with no AS of their own
// (Obj_InsideTheDekuTree_SwitchShutter_A_01, Obj_SwitchShutter_A_05, ...) are
// left out: a name their AS does not have is never asked for.
constexpr auto kShutter = std::to_array<std::string_view>({
    "Obj_Base_SwitchShutter_A_01",          "Obj_Base_SwitchShutter_A_02",
    "Obj_DodongoCave_SwitchShutter_A_01",   "Obj_FireTemple_SwitchShutter_A_01",
    "Obj_FireTemple_SwitchShutter_A_02",    "Obj_FireTemple_SwitchShutter_A_03",
    "Obj_ForestTemple_SwitchShutter_A_03",  "Obj_ForestTemple_SwitchShutter_A_04",
    "Obj_GanonCastle_SwitchShutter_A_01",   "Obj_GanonCastle_SwitchShutter_A_02",
    "Obj_GanonCastle_SwitchShutter_A_03",   "Obj_Gerudo_SwitchShutter_A_01",
    "Obj_Gerudo_SwitchShutter_A_02",        "Obj_Gerudo_SwitchShutter_A_03",
    "Obj_Gerudo_SwitchShutter_A_04",        "Obj_Gerudo_SwitchShutter_A_05",
    "Obj_Gerudo_SwitchShutter_A_06",        "Obj_Gerudo_SwitchShutter_A_07",
    "Obj_Goron_SwitchShutter_A_01",         "Obj_Goron_SwitchShutter_A_02",
    "Obj_Graveyard_SwitchShutter_A_01",     "Obj_HrlCastle_SwitchShutter_A_01",
    "Obj_IceCave_SwitchShutter_A_02",       "Obj_InsideGanonCastle_SwitchShutter_A_01",
    "Obj_InsideGanonCastle_SwitchShutter_A_02", "Obj_InsideGanonCastle_SwitchShutter_A_03",
    "Obj_InsideGanonCastle_SwitchShutter_A_04", "Obj_InsideGanonCastle_SwitchShutter_A_05",
    "Obj_InsideJabu_Jabu_SwitchShutter_A_01", "Obj_InsideJabu_Jabu_SwitchShutter_A_02",
    "Obj_InsideJabu_Jabu_SwitchShutter_A_03", "Obj_LakeHrl_SwitchShutter_A_01",
    "Obj_ShadowTemple_SwitchShutter_A_01",  "Obj_ShadowTemple_SwitchShutter_A_03",
    "Obj_ShadowTemple_SwitchShutter_A_04",  "Obj_SpiritTemple_SwitchShutter_A_01",
    "Obj_SpiritTemple_SwitchShutter_A_03",  "Obj_SpiritTemple_SwitchShutter_A_04",
    "Obj_SpiritTemple_SwitchShutter_A_05",  "Obj_SwitchShutter_A_01",
    "Obj_SwitchShutter_A_02",               "Obj_SwitchShutter_A_04",
    "Obj_SwitchShutter_A_06",               "Obj_UnderTheGrave_SwitchShutter_A_01",
    "Obj_WaterTemple_SwitchShutter_A_01",   "Obj_WaterTemple_SwitchShutter_A_04",
    "Obj_Well_SwitchShutter_A_01",
});
// AS Obj_TBox_A_01 (commands CloseWait, FrontOpenSmall, OpenWait, Rattle, ...)
// and Obj_TBox_A_02 (CloseWait, FrontOpen, FrontOpen_Adult, OpenWait, ...);
// Open_Front_Small and Open_Front_Dungeon are their animations, not commands
// (ORM cofre, read again 2026-11-11: the .root.asb lists the commands first).
// The Lens of Truth's chest has Obj_TBox_A_01's AS. Not the Treasure Chest
// Shop's (Obj_HrlCity_Shop_TBox_*).
constexpr auto kChestSmall = std::to_array<std::string_view>({
    "Obj_TBox_A_01", "Obj_TBox_A_01_NoBake", "Obj_TBox_A_01_memo11",
    "Obj_TBox_A_04", "Obj_TBox_A_06",        "Obj_TBox_A_06_NoBake",
    "Obj_TBox_GlassTruth_A_01",
});
constexpr auto kChestBig = std::to_array<std::string_view>({
    "Obj_TBox_A_02", "Obj_TBox_A_02_NoBake", "Obj_TBox_A_03", "Obj_TBox_A_03_NoBake",
});

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view name) {
  return std::find(list.begin(), list.end(), name) != list.end();
}

bool starts(std::string_view s, std::string_view prefix) {
  return s.substr(0, prefix.size()) == prefix;
}

bool has(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string metres(float m) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(m));
  return buf;
}

std::string who(std::uint8_t number) { return "P" + std::to_string(number); }

}  // namespace

DoorMove door_move_of(std::string_view c) {
  if (c.empty()) return DoorMove::none;
  if (starts(c, "BigDoorPushOpenL")) return DoorMove::big_left;
  if (starts(c, "BigDoorPushOpenR")) return DoorMove::big_right;
  if (starts(c, "DoorPushOpenL")) return DoorMove::push_left;
  if (starts(c, "DoorPushOpenR")) return DoorMove::push_right;
  for (const std::string_view s :
       {std::string_view("EvtDoorShutter"), std::string_view("Evt_Door_Shutter"),
        std::string_view("EvtDoorKey"), std::string_view("Evt_Door_Key"),
        std::string_view("EvtDoorBoss"), std::string_view("Evt_Door_Boss"),
        std::string_view("EvtDoorJabu"), std::string_view("Evt_Door_Jabu")}) {
    if (starts(c, s)) return DoorMove::shutter;
  }
  if (starts(c, "DemoOpenBoxWithKick_Mini") || starts(c, "DemoOpenBoxFront_Mini")) {
    return DoorMove::chest_small;
  }
  if (starts(c, "DemoOpenTreasureBoxWithKick") || starts(c, "Demo_Get_Kick_TreasureBox")) {
    return DoorMove::chest_kick;
  }
  if (starts(c, "DemoOpenTreasureBoxFront") || starts(c, "Demo_Front_OpenTreasureBox") ||
      c == "DemoOpenTreasureBox") {
    return DoorMove::chest_front;
  }
  return DoorMove::none;
}

const char* door_move_text(DoorMove move) {
  switch (move) {
    case DoorMove::none: return "none";
    case DoorMove::push_left: return "door (left)";
    case DoorMove::push_right: return "door (right)";
    case DoorMove::big_left: return "big door (left)";
    case DoorMove::big_right: return "big door (right)";
    case DoorMove::shutter: return "shutter";
    case DoorMove::chest_kick: return "big chest";
    case DoorMove::chest_front: return "chest";
    case DoorMove::chest_small: return "small chest";
  }
  return "?";
}

DoorKind door_kind_of(std::string_view actor) {
  if (listed(kHinge, actor)) return DoorKind::hinge;
  if (listed(kDungeon, actor)) return DoorKind::dungeon;
  if (listed(kShutter, actor)) return DoorKind::shutter;
  if (listed(kChestSmall, actor)) return DoorKind::chest_small;
  if (listed(kChestBig, actor)) return DoorKind::chest_big;
  return DoorKind::none;
}

const char* door_kind_text(DoorKind kind) {
  switch (kind) {
    case DoorKind::none: return "none";
    case DoorKind::hinge: return "hinge";
    case DoorKind::dungeon: return "dungeon";
    case DoorKind::shutter: return "shutter";
    case DoorKind::chest_small: return "small chest";
    case DoorKind::chest_big: return "big chest";
  }
  return "?";
}

std::optional<DoorPlay> door_play(DoorKind kind, DoorMove move, bool adult) {
  const bool left = move == DoorMove::push_left || move == DoorMove::big_left;
  const bool right = move == DoorMove::push_right || move == DoorMove::big_right;
  switch (kind) {
    case DoorKind::hinge:
      if (left) return DoorPlay{"Push_L_ByPlayer", "Close_L_ByPlayer"};
      if (right) return DoorPlay{"Push_R_ByPlayer", "Close_R_ByPlayer"};
      return std::nullopt;
    case DoorKind::dungeon:
      if (left) return DoorPlay{"Push_L", "Close_L"};
      if (right) return DoorPlay{"Push_R", "Close_R"};
      return std::nullopt;
    case DoorKind::shutter:
      if (move == DoorMove::shutter) return DoorPlay{"ToOpen", "ToClose"};
      return std::nullopt;
    case DoorKind::chest_big:
      // The big chest's AS has one opening command (and its adult one) for
      // the kick and the front opening alike.
      if (move == DoorMove::chest_kick || move == DoorMove::chest_front) {
        return DoorPlay{adult ? "FrontOpen_Adult" : "FrontOpen", ""};
      }
      return std::nullopt;
    case DoorKind::chest_small:
      if (move == DoorMove::chest_kick || move == DoorMove::chest_front ||
          move == DoorMove::chest_small) {
        return DoorPlay{"FrontOpenSmall", ""};
      }
      return std::nullopt;
    case DoorKind::none:
      return std::nullopt;
  }
  return std::nullopt;
}

bool door_shows_open(DoorKind kind, std::string_view command) {
  if (command.empty()) return false;
  switch (kind) {
    case DoorKind::hinge:
    case DoorKind::dungeon:
      return starts(command, "Push");
    case DoorKind::shutter:
    case DoorKind::chest_small:
    case DoorKind::chest_big:
      return has(command, "Open");
    case DoorKind::none:
      return false;
  }
  return false;
}

float DoorMirror::reach(DoorMove move) const {
  switch (move) {
    case DoorMove::shutter: return rules_.reach_shutter_m;
    case DoorMove::chest_kick:
    case DoorMove::chest_front:
    case DoorMove::chest_small: return rules_.reach_chest_m;
    default: return rules_.reach_door_m;
  }
}

bool DoorMirror::close_due(const Opened& o, std::uint64_t now_ms) const {
  if (o.close.empty() || o.done_ms == 0 || now_ms < o.done_ms) return false;
  const std::uint64_t after =
      o.kind == DoorKind::shutter ? rules_.close_shutter_ms : rules_.close_door_ms;
  return now_ms - o.done_ms >= after;
}

void DoorMirror::update(const std::optional<protocol::Vec3>& own,
                        const std::vector<DoorMover>& others, std::uint64_t now_ms) {
  own_ = own;
  std::vector<Last> next;
  next.reserve(others.size());
  for (const auto& m : others) {
    if (m.number == 0) continue;
    DoorMove move = door_move_of(m.command);
    if (chests_by_message_ &&
        (move == DoorMove::chest_kick || move == DoorMove::chest_front || move == DoorMove::chest_small)) {
      move = DoorMove::none;  // his CHEST_OPEN opens it (set_chests_by_message)
    }
    DoorMove before = DoorMove::none;
    for (const auto& l : last_) {
      if (l.number == m.number) before = l.move;
    }
    if (move != DoorMove::none && move != before) {
      starts_.push_back(Start{m.number, move, m.position, m.adult, m.command, now_ms});
    }
    if (before != DoorMove::none && move != before) {
      for (auto& o : opened_) {
        if (o.number == m.number && o.done_ms == 0) o.done_ms = now_ms;
      }
    }
    next.push_back(Last{m.number, move});
  }
  // A mover gone from our place (another scene, the session): its doors
  // close as for a move that ended, its waiting moves are dropped.
  for (const auto& l : last_) {
    const bool still = std::any_of(next.begin(), next.end(),
                                   [&](const Last& n) { return n.number == l.number; });
    if (still) continue;
    for (auto& o : opened_) {
      if (o.number == l.number && o.done_ms == 0) o.done_ms = now_ms;
    }
    std::erase_if(starts_, [&](const Start& s) { return s.number == l.number; });
  }
  last_ = std::move(next);
  std::erase_if(starts_, [&](const Start& s) {
    return now_ms - std::min(now_ms, s.at_ms) > rules_.start_wait_ms;
  });
  std::erase_if(opened_, [&](const Opened& o) {
    return now_ms - std::min(now_ms, o.at_ms) > rules_.open_keep_ms;
  });
}

bool DoorMirror::wants_actors(std::uint64_t now_ms) const {
  if (!starts_.empty()) return true;
  return std::any_of(opened_.begin(), opened_.end(),
                     [&](const Opened& o) { return close_due(o, now_ms); });
}

bool DoorMirror::worth_reading(const protocol::Vec3& at, std::uint64_t id) const {
  for (const auto& o : opened_) {
    if (o.id == id) return true;
  }
  for (const auto& s : starts_) {
    if (distance(at, s.position) <= reach(s.move) + 0.5f) return true;
  }
  return false;
}

std::vector<DoorWish> DoorMirror::decide(const std::vector<DoorActor>& actors,
                                         std::uint64_t now_ms) {
  std::vector<DoorWish> out;
  const auto find = [&](std::uint64_t id) -> const DoorActor* {
    for (const auto& a : actors) {
      if (a.id == id) return &a;
    }
    return nullptr;
  };

  // Doors due to close: only one that still shows the command we played
  // (the game itself may have closed it, or our Link pushed it since).
  for (auto it = opened_.begin(); it != opened_.end();) {
    if (!close_due(*it, now_ms)) {
      ++it;
      continue;
    }
    const DoorActor* a = find(it->id);
    if (a != nullptr && a->command == it->open) {
      out.push_back(DoorWish{it->id, it->close,
                             who(it->number) + " is through " + it->name + ": closing it (" +
                                 it->close + ")"});
    } else {
      out.push_back(DoorWish{0, {},
                             who(it->number) + " is through " + it->name +
                                 ": left as the game has it ('" +
                                 (a != nullptr ? a->command : std::string("gone")) + "')"});
    }
    it = opened_.erase(it);
  }

  for (const auto& s : starts_) {
    const DoorActor* best = nullptr;
    float best_d = std::numeric_limits<float>::infinity();
    std::optional<DoorPlay> best_play;
    DoorKind best_kind = DoorKind::none;
    for (const auto& a : actors) {
      const DoorKind kind = door_kind_of(a.name);
      const auto play = door_play(kind, s.move, s.adult);
      if (!play) continue;
      const float d = distance(a.position, s.position);
      if (d <= reach(s.move) && d < best_d) {
        best = &a;
        best_d = d;
        best_play = play;
        best_kind = kind;
      }
    }
    const std::string head = who(s.number) + " '" + s.command + "'";
    if (best == nullptr) {
      out.push_back(DoorWish{0, {},
                             head + ": no " + door_move_text(s.move) + " within " +
                                 metres(reach(s.move)) + " m of its puppet here"});
      continue;
    }
    const float own_near = is_chest(best_kind) ? rules_.own_near_chest_m : rules_.own_near_door_m;
    if (own_ && distance(*own_, best->position) <= own_near) {
      out.push_back(DoorWish{0, {}, head + ": " + best->name + " left alone, our Link is beside it"});
      continue;
    }
    if (std::any_of(opened_.begin(), opened_.end(),
                    [&](const Opened& o) { return o.id == best->id && o.done_ms == 0; })) {
      out.push_back(DoorWish{0, {}, head + ": " + best->name + " is opening already"});
      continue;
    }
    if (door_shows_open(best_kind, best->command)) {
      out.push_back(DoorWish{0, {},
                             head + ": " + best->name + " shows open here already ('" +
                                 best->command + "')"});
      continue;
    }
    out.push_back(DoorWish{best->id, std::string(best_play->open),
                           head + ": " + best->name + " opens here (" +
                               std::string(best_play->open) + "), " + metres(best_d) +
                               " m from its puppet"});
    Opened o;
    o.id = best->id;
    o.name = best->name;
    o.number = s.number;
    o.kind = best_kind;
    o.open = std::string(best_play->open);
    o.close = std::string(best_play->close);
    o.at_ms = now_ms;
    // The move may have ended before its door was found.
    DoorMove now_move = DoorMove::none;
    for (const auto& l : last_) {
      if (l.number == s.number) now_move = l.move;
    }
    if (now_move != s.move) o.done_ms = now_ms;
    std::erase_if(opened_, [&](const Opened& e) { return e.id == o.id; });
    opened_.push_back(std::move(o));
  }
  starts_.clear();
  return out;
}

std::vector<std::uint64_t> DoorMirror::opened_ids() const {
  std::vector<std::uint64_t> out;
  out.reserve(opened_.size());
  for (const auto& o : opened_) out.push_back(o.id);
  return out;
}

void DoorMirror::reset() {
  own_.reset();
  last_.clear();
  starts_.clear();
  opened_.clear();
}

}  // namespace coop::host
