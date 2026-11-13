// ORM: hazards with several players: the flags and actors each player keeps, and the reach point of a
//   Link downed in lava, quicksand, deep water, a whirlpool or the void. The rules are described in
//   coop/host/hazards.hpp.
// Doc: oot-remake docs/PELIGROS.md.
// Switch: LICHII_COOP_HAZARDS (on unless "0"; the reach point needs LICHII_COOP_DOWNED; the Poe rule always).
// Test: tests/host/hazards_test.cpp.

#include "coop/host/hazards.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "coop/protocol/codec.hpp"

namespace coop::host {
namespace {

// Read from the romfs (GameDataList has each one as a Bool; the flows that read and write them are
// named in oot-remake docs/PELIGROS.md, section 2). hazards_test checks every hash against its name.
constexpr std::array kHazardFlags = std::to_array<HazardFlag>({
    {0x0187aaa2, "PlayerStatus.IsActiveGlassTruth", "the Lens of Truth on"},
    {0xc5c8d274, "GuidePoe_IsGuiding", "the guide Poe leads this Link"},
    {0xe7af21e9, "GuidePoe_IsComplete", "the guide Poe brought this Link to the Colossus"},
    {0x72bcbfe0, "GuidePoe_HaveSeen", "this Link has seen the guide Poe"},
    {0xf611492d, "GuidePoe_IsGoalandRestart", "this Link reached the goal and was set back"},
    {0x1930bb98, "GuidePoe_Disappear", "the guide Poe left this Link"},
    {0xc0b180e0, "GuidePoe_IsTookGoalAction", "the guide Poe's goal action, for this Link"},
    {0x69a8dcf4, "HountedWasteland_GuidePoe_Adult_IsMissing", "this Link lost the guide Poe"},
    {0xfc049376, "HountedWasteland_RestartedOnce", "this Link was set back by the storm once"},
    {0xbe7d3ec7, "HountedWasteland_ArrivedFromColossus", "this Link came from the Colossus side"},
    {0xfe1c1830, "HountedWasteland_ApproachedGoal", "this Link is near the end of the storm"},
    {0xc04e0d93, "HountedWasteland_IsRespawnAtGuidePoeRuins", "this Link's storm checkpoint"},
    {0x0e542378, "HountedWasteland_IsRespawnBeyondQuickSand", "this Link's storm checkpoint"},
    {0x3cd8135b, "HountedWasteland_isFirstRespawn_01", "this Link's first storm set-back"},
    {0x0b651ad9, "HountedWasteland_HaveTalkedNaviMessage", "Navi's storm hint, for this Link"},
    {0x6b7b1515, "DesertColossus_PlayerPosCheck", "where this Link stands (the guide Poe's check)"},
    {0xece4e747, "ForestTemple_IsInFallingCeilingRoom", "this Link is in the falling ceiling room (the void-out reads it)"},
    {0xcb2e8559, "DmF_SY_FallDown_Elevator_Reset", "this Link's void-out resets the lifts"},
    {0x7dc0d2a4, "HavePlayedNaviBalloonMessage.type015", "Navi's hint after this Link's fall"},
    {0x493701cd, "HavePlayedNaviBalloonMessage.type036", "Navi's hint after this Link's fall"},
});

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

template <std::size_t N>
bool contains_any(std::string_view s, const std::array<std::string_view, N>& parts) {
  return std::any_of(parts.begin(), parts.end(), [&](std::string_view p) { return contains(s, p); });
}

std::string text(const char* format, auto... args) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), format, args...);
  return buf;
}

double distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    sum += d * d;
  }
  return std::sqrt(sum);
}

bool finite(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

const std::string& command_of(const PlayerReading& r) {
  static const std::string none;
  return r.animation ? r.animation->name : none;
}

bool up(DownedState s) { return s == DownedState::alive; }
bool down(DownedState s) { return s == DownedState::downed || s == DownedState::reviving; }

}  // namespace

std::span<const HazardFlag> hazard_flags() { return kHazardFlags; }

bool is_hazard_flag(FlagKey key) {
  if (key.kind != FlagKeyKind::named) return false;
  return std::any_of(kHazardFlags.begin(), kHazardFlags.end(),
                     [&](const HazardFlag& f) { return f.hash == key.hash; });
}

std::vector<std::string_view> unshare_hazard_flags(std::optional<SharedFlags>& shared) {
  std::vector<std::string_view> out;
  if (!shared) return out;
  std::vector<FlagKey> keys;
  keys.reserve(shared->size());
  std::vector<std::uint32_t> taken;
  for (const auto& key : shared->keys()) {
    if (is_hazard_flag(key)) {
      taken.push_back(key.hash);
      continue;
    }
    keys.push_back(key);
  }
  if (taken.empty()) return out;
  for (const auto& f : kHazardFlags) {
    if (std::find(taken.begin(), taken.end(), f.hash) != taken.end()) out.push_back(f.name);
  }
  shared = SharedFlags(std::move(keys), shared->enums());
  return out;
}

bool is_own_hazard_actor(std::string_view name) { return name.starts_with("Npc_GuidePoe"); }

const char* to_string(DownKind kind) {
  switch (kind) {
    case DownKind::ground: return "on the ground";
    case DownKind::lava: return "in lava";
    case DownKind::sand: return "in quicksand";
    case DownKind::water: return "in the water";
    case DownKind::underwater: return "under water";
    case DownKind::whirlpool: return "in a whirlpool";
    case DownKind::abyss: return "in the void";
  }
  return "?";
}

bool is_dead_command(std::string_view c) {
  return c.starts_with("Dead") || c.ends_with("Dead") || c.starts_with("Die") || c.ends_with("Die");
}

DownKind kind_of_command(std::string_view c) {
  if (contains(c, "Lava")) return DownKind::lava;
  if (contains(c, "SandStorm")) return DownKind::ground;  // walking in the storm (an event's)
  if (contains(c, "Sand")) return DownKind::sand;
  if (contains(c, "Whirlpool")) return DownKind::whirlpool;
  static constexpr std::array<std::string_view, 4> kUnder{"Underwater", "UnderWater", "Dive", "Heavy_Swim"};
  if (contains_any(c, kUnder)) return DownKind::underwater;
  if (contains(c, "Swim")) return DownKind::water;
  return DownKind::ground;
}

bool firm_ground_command(std::string_view c) {
  if (c.empty() || is_dead_command(c)) return false;
  static constexpr std::array<std::string_view, 21> kOff{
      "Fall", "Jump", "Swim", "Dive", "Float", "Climb", "Cliff", "Hang", "Ladder", "Hook", "Lava",
      "Sand", "Burn", "Damage", "Freeze", "Frozen", "Whirlpool", "Water", "InAir", "Abyss", "Evt"};
  return !contains_any(c, kOff);
}

// ---- DownedReach -------------------------------------------------------------------------------------

std::optional<PlayerReading> DownedReach::frame(const std::optional<PlayerReading>& reading,
                                                DownedState state, std::uint64_t now_us) {
  if (!settings_.on) return reading;
  if (!reading) {
    // Loading or not readable: nothing to move; a scene change shows on the next reading.
    if (down_ && !down(state)) release(up(state) ? "up again" : "the game over runs");
    return reading;
  }
  const PlayerReading& r = *reading;
  // A reading whose scene was not read (a frame the reader missed it) stays in the last scene: taken
  // as a change, it would drop the firm ground and, while down, the anchor for the rest of the down.
  const std::string scene = r.scene.value_or(last_scene_);
  if (scene != last_scene_) {
    if (firm_ && firm_->scene != scene) firm_.reset();
    firm_since_ = 0;
    last_y_.reset();
    if (down_ && anchor_) {
      // The group's scene change (or the Continue) takes a downed Link along: his real place again.
      anchor_.reset();
      log_.push_back("the scene changed: our real place is sent again");
    }
    last_scene_ = scene;
  }

  if (down(state)) {
    if (!down_) went_down(r, now_us);
    while_down(r, now_us);
  } else {
    if (down_) {
      // docs/RIESGOS_RESUELTOS.md: a revive from where no mate could reach us
      // is watched; does the game bring us back to firm ground?
      if (up(state) && kind_ != DownKind::ground) {
        revived_ = Revived{kind_, now_us};
      } else {
        revived_.reset();
      }
      release(up(state) ? "up again" : "the game over runs");
    }
    if (up(state)) {
      if (const std::string& c = command_of(r); !c.empty() && !is_dead_command(c)) {
        last_live_command_ = c;
        last_live_scene_ = scene;
      }
      track_firm(r, now_us);
      after_revive(r, now_us);
    } else {
      revived_.reset();
    }
  }
  last_y_ = r.position[1];
  last_at_ = now_us;

  if (!anchor_) return reading;
  PlayerReading sent = r;
  sent.position = *anchor_;
  return sent;
}

std::vector<std::string> DownedReach::take_log() {
  std::vector<std::string> out;
  out.swap(log_);
  return out;
}

void DownedReach::track_firm(const PlayerReading& r, std::uint64_t now_us) {
  bool firm = finite(r.position) && r.scene && !r.event_running.value_or(false) &&
              firm_ground_command(command_of(r));
  if (firm && last_y_ && now_us > last_at_) {
    const double dt = static_cast<double>(now_us - last_at_) / 1e6;
    const double climb = std::fabs(static_cast<double>(r.position[1]) - *last_y_) / dt;
    if (climb > settings_.firm_climb) firm = false;
  }
  if (!firm) {
    firm_since_ = 0;
    return;
  }
  if (firm_since_ == 0) firm_since_ = std::max<std::uint64_t>(now_us, 1);
  if (now_us - firm_since_ >= settings_.firm_for_us) {
    firm_ = Firm{r.position, *r.scene, now_us};
  }
}

void DownedReach::after_revive(const PlayerReading& r, std::uint64_t now_us) {
  if (!revived_) return;
  const double after = static_cast<double>(now_us - revived_->at_us) / 1e6;
  if (firm_ && firm_->at_us > revived_->at_us) {
    log_.push_back(text("after the revive %s: on firm ground again %.1f s later (the game brought us back)",
                        to_string(revived_->kind), after));
    revived_.reset();
    return;
  }
  if (now_us - revived_->at_us < settings_.after_revive_us) return;
  if (firm_ && firm_->scene == last_scene_ && finite(r.position)) {
    log_.push_back(text("after the revive %s: not on firm ground %.1f s later (%.1f m below and %.1f m from where "
                        "we last stood): the game did not bring us back",
                        to_string(revived_->kind), after, static_cast<double>(firm_->position[1] - r.position[1]),
                        distance(firm_->position, r.position)));
  } else {
    log_.push_back(text("after the revive %s: not on firm ground %.1f s later (no firm ground known in this scene)",
                        to_string(revived_->kind), after));
  }
  revived_.reset();
}

void DownedReach::went_down(const PlayerReading& /*r*/, std::uint64_t now_us) {
  if (revived_) {
    log_.push_back(text("after the revive %s: down again %.1f s later (the game did not take us out in time)",
                        to_string(revived_->kind), static_cast<double>(now_us - revived_->at_us) / 1e6));
    revived_.reset();
  }
  down_ = true;
  down_at_ = now_us;
  before_ = last_live_command_;
  // Down on the first reading of a scene: what he played before is not known.
  if (last_scene_ != last_live_scene_) before_.clear();  // last_scene_: this reading's, or the last read
  dead_command_.clear();
  kind_ = DownKind::ground;
  settled_ = false;
  anchor_.reset();
  firm_since_ = 0;
}

void DownedReach::while_down(const PlayerReading& r, std::uint64_t now_us) {
  const std::string& c = command_of(r);
  if (dead_command_.empty() && is_dead_command(c)) dead_command_ = c;

  // The kind from the commands: the dead one when it says more than "dead", else the one before.
  // `by` says which (docs/RIESGOS_RESUELTOS.md: the dead commands of each place are not all seen).
  DownKind next = dead_command_.empty() ? DownKind::ground : kind_of_command(dead_command_);
  std::string by = "its dead command";
  if (next == DownKind::ground) {
    next = kind_of_command(before_);
    if (next != DownKind::ground) {
      by = "the command before it ('" + before_ + "')";
    } else if (dead_command_.empty()) {
      by = text("no dead command in %.1f s, nor the command before: on the ground",
                static_cast<double>(settings_.kind_wait_us) / 1e6);
    } else {
      by = "no command said more than dead";
    }
  }
  // The void (when the commands say nothing more): far below the last firm ground, or below it after
  // a fall; a body that slid off a ledge while down too. Once in the void, until the body is back near
  // the firm ground's height (the game's own void-out return ran first).
  bool back = false;
  if (next == DownKind::ground && firm_ && finite(r.position)) {
    const float below = firm_->position[1] - r.position[1];
    if (below >= settings_.abyss_drop || (contains(before_, "Fall") && below >= settings_.fall_drop)) {
      next = DownKind::abyss;
      by = "its height (the commands said nothing more)";
    } else if (kind_ == DownKind::abyss) {
      if (std::fabs(below) <= settings_.back_within) {
        back = true;
        by = "its height (back near our firm ground)";
      } else {
        next = DownKind::abyss;
        by = "its height (the commands said nothing more)";
      }
    }
  }
  // No dead command yet and nothing else to go by: it may still come.
  if (dead_command_.empty() && next == DownKind::ground && now_us - down_at_ < settings_.kind_wait_us) return;
  if (settled_ && next == kind_) return;
  settled_ = true;
  kind_ = next;
  const char* shown = !dead_command_.empty() ? dead_command_.c_str() : (c.empty() ? "?" : c.c_str());
  if (kind_ != DownKind::ground) {
    decide(r, by);  // in lava, sand, water...: the firm ground; back from the void into lava: the same
    return;
  }
  if (anchor_) {
    anchor_.reset();
    log_.push_back((back ? text("back on firm ground ('%s', %.1f m from where we last stood): our real place is "
                                "sent again", shown, distance(firm_->position, r.position))
                         : text("down %s ('%s'): our real place is sent again", to_string(kind_), shown)) +
                   "; kind by " + by);
  } else {
    log_.push_back(text("down %s ('%s'): our real place is sent (a mate can reach us)", to_string(kind_), shown) +
                   "; kind by " + by);
  }
}

void DownedReach::decide(const PlayerReading& r, const std::string& by) {
  const std::string& c = command_of(r);
  const char* shown = !dead_command_.empty() ? dead_command_.c_str() : (c.empty() ? "?" : c.c_str());
  const bool here = firm_ && firm_->scene == last_scene_ && finite(r.position);  // last_scene_: see frame()
  const double away = here ? distance(firm_->position, r.position) : 0.0;
  if (!here || away > settings_.max_distance) {
    anchor_.reset();
    log_.push_back(text("down %s ('%s'), no firm ground known near in this scene: our real place is sent",
                        to_string(kind_), shown) + "; kind by " + by);
    return;
  }
  anchor_ = firm_->position;
  log_.push_back(text("down %s ('%s', %.1f m below and %.1f m from our last firm ground): the others see us "
                      "lying there (%.2f, %.2f, %.2f), where a mate can stand and hold A",
                      to_string(kind_), shown, static_cast<double>(firm_->position[1] - r.position[1]), away,
                      static_cast<double>(firm_->position[0]), static_cast<double>(firm_->position[1]),
                      static_cast<double>(firm_->position[2])) + "; kind by " + by);
}

void DownedReach::release(const char* why) {
  if (anchor_) log_.push_back(text("%s: our real place is sent again", why));
  down_ = false;
  settled_ = false;
  anchor_.reset();
  dead_command_.clear();
  before_.clear();
  kind_ = DownKind::ground;
  firm_since_ = 0;
}

}  // namespace coop::host
