// ORM: boss rooms with up to six: a downed Link brought in lying down, the group back in the boss
//   room after a group defeat. The rules are described in coop/host/boss_rules.hpp.
//   And the families that break with several players (grab, tennis, mirror): a table and a
//   watcher over each objects walk.
// Doc: oot-remake docs/JEFES.md (D-R17; section 9 for the families).
// Switch: LICHII_COOP_BOSS_PULL_DOWNED, LICHII_COOP_BOSS_RESPAWN (both 1 when
//   LICHII_COOP_DOWNED=1); LICHII_ORM_BOSS_FAMILIES (on).
// Test: tests/lichii/boss_rules_test.cpp.

#include "coop/host/boss_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace coop::host {

const char* to_string(BossPull pull) {
  switch (pull) {
    case BossPull::normal: return "normal";
    case BossPull::matrix_only: return "matrix only";
    case BossPull::none: return "none";
  }
  return "?";
}

BossPull boss_pull(DownedState state, bool pull_downed) {
  switch (state) {
    case DownedState::alive: return BossPull::normal;
    case DownedState::downed: return pull_downed ? BossPull::matrix_only : BossPull::none;
    case DownedState::reviving:
    case DownedState::game_over:
    // Our own bottled fairy (D-R17): the game over event's fairy branch runs;
    // a scene change now would stand him up outside it, as for a revive.
    case DownedState::fairy: return BossPull::none;
  }
  return BossPull::none;
}

void BossRespawn::reset() {
  phase_ = Phase::idle;
  over_last_ = false;
  scene_.clear();
  room_ = 0;
  since_ = 0;
  moves_ = 0;
}

void BossRespawn::idle(BossRespawnAction& out, const std::string& why) {
  out.log.push_back(why);
  phase_ = Phase::idle;
  scene_.clear();
  moves_ = 0;
}

BossRespawnAction BossRespawn::tick(const BossRespawnInput& in) {
  BossRespawnAction out;
  const bool rising = in.game_over && !over_last_;
  over_last_ = in.game_over;
  const bool readable = in.readable && in.scene.has_value();

  if (phase_ == Phase::idle) {
    if (rising && readable && in.in_room) {
      phase_ = Phase::over;
      scene_ = *in.scene;
      room_ = *in.in_room;
      since_ = in.now_us;
      out.log.push_back("game over in the boss room of " + scene_ +
                        ": after Continue, back into it if the reload leaves us outside");
    }
    return out;
  }

  if (phase_ == Phase::over) {
    if (!readable) {
      phase_ = Phase::loading;  // Continue: the scene loads again
      return out;
    }
    if (in.up && !in.game_over) {
      idle(out, "up again with no reload (a fairy): nothing to do");
    } else if (in.now_us - since_ >= settings_.load_wait_us) {
      idle(out, "no reload after the game over: nothing to do");
    }
    return out;
  }

  if (phase_ == Phase::loading) {
    if (!readable) return out;
    phase_ = Phase::back;
    since_ = in.now_us;  // the first frame read after the load
  }

  if (phase_ == Phase::moved) {
    if (!readable) {  // a scene change took the move: its load
      since_ = in.now_us;
      return out;
    }
    if (*in.scene != scene_) {
      idle(out, "now in " + *in.scene + ", not " + scene_ + ": nothing more to do");
    } else if (in.in_room && *in.in_room == room_) {
      idle(out, "back in the boss room of " + scene_);
    } else if (in.beaten) {
      idle(out, "the boss of " + scene_ + " is beaten: nothing more to do");
    } else if (in.now_us - since_ >= settings_.retry_us) {
      if (moves_ >= settings_.tries) {
        idle(out, "not in the boss room of " + scene_ + " after " + std::to_string(moves_) +
                      " moves: given up");
      } else if (in.up) {
        ++moves_;
        since_ = in.now_us;
        out.move_to = room_;
        out.by_event = in.event;
        out.log.push_back("not in the boss room yet: moved again (try " + std::to_string(moves_) + ")");
      }
    }
    return out;
  }

  // Phase::back: the reload is over.
  if (!readable) return out;  // another load: wait for it
  if (*in.scene != scene_) {
    idle(out, "Continue took us to " + *in.scene + ", not " + scene_ + ": nothing to do");
  } else if (in.in_room && *in.in_room == room_) {
    idle(out, "Continue left us in the boss room: nothing to do");
  } else if (in.beaten) {
    idle(out, "the boss of " + scene_ + " is beaten: nothing to do");
  } else if (in.up) {
    out.move_to = room_;
    out.by_event = in.event;
    out.log.push_back("Continue left us outside the boss room of " + scene_ + ": back into it (try 1)");
    phase_ = Phase::moved;
    moves_ = 1;
    since_ = in.now_us;
  } else if (in.now_us - since_ >= settings_.up_wait_us) {
    idle(out, "not up in time after the reload: given up");
  }
  return out;
}

// ---- The families that break with several players ----------------------------------------------

namespace {

using F = BossFamily;
using R = FamilyRole;

// From the game's Pack/Actor list (romfs, 2026-11-12). The reach of a grabbing part is how far
// its held Link can be from the actor's origin: a tentacle holds him at its tip, a hand in its
// palm, a Like Like in its mouth. To be measured in a run: the "grab: our Link is held by" line
// gives the distance.
constexpr FamilyActor kFamilyActors[] = {
    // grab: bosses and the mini-boss, whose actors are kept wherever they are.
    {"Enemy_DungeonBoss_Morpha", F::grab, "Morpha", R::body, true, 0.0f},
    {"Enemy_DungeonBoss_Morpha_Tentacle", F::grab, "Morpha", R::part, true, 10.0f},
    {"Enemy_DungeonBoss_BongoBongo", F::grab, "Bongo Bongo", R::body, true, 0.0f},
    {"Enemy_DungeonBoss_BongoBongo_LeftHand", F::grab, "Bongo Bongo", R::part, true, 4.0f},
    {"Enemy_DungeonBoss_BongoBongo_RightHand", F::grab, "Bongo Bongo", R::part, true, 4.0f},
    {"Enemy_DeadHand", F::grab, "Dead Hand", R::part, true, 3.0f},  // its bite holds too
    {"Enemy_DeadHand_Hand", F::grab, "Dead Hand", R::part, true, 3.0f},
    // grab: enemies that are their own grabber, kept only near the hold.
    {"Enemy_LikeLike", F::grab, "Like Like", R::part, false, 3.0f},
    {"Enemy_Wallmaster", F::grab, "Wallmaster", R::part, false, 3.0f},
    {"Enemy_Floormaster", F::grab, "Floormaster", R::part, false, 3.0f},
    {"Enemy_Floormaster_Truth", F::grab, "Floormaster", R::part, false, 3.0f},
    {"Enemy_Floormaster_Little", F::grab, "Floormaster", R::part, false, 3.0f},
    {"Enemy_Floormaster_Little_Truth", F::grab, "Floormaster", R::part, false, 3.0f},
    {"Enemy_ReDead", F::grab, "ReDead", R::part, false, 3.0f},
    {"Enemy_ReDead_AlwaysRespawn", F::grab, "ReDead", R::part, false, 3.0f},
    {"Enemy_ReDead_ClimaxOnly", F::grab, "ReDead", R::part, false, 3.0f},
    {"Enemy_Gibdo", F::grab, "Gibdo", R::part, false, 3.0f},
    // tennis: the boss and its balls ("Ganondrof" is the game's own spelling of the shots).
    {"Enemy_DungeonBoss_PhantomGanon", F::tennis, "Phantom Ganon", R::body, true, 0.0f},
    {"DungeonBossPhantomGanon_Bullet_LightBall", F::tennis, "Phantom Ganon", R::part, true, 0.0f},
    {"Enemy_DungeonBoss_Ganondorf", F::tennis, "Ganondorf", R::body, true, 0.0f},
    {"DungeonBossGanondrof_Bullet_LightBall", F::tennis, "Ganondorf", R::part, true, 0.0f},
    {"DungeonBossGanondrof_Bullet_ChargeLightBall", F::tennis, "Ganondorf", R::part, true, 0.0f},
    // mirror: Twinrova's bodies and its battle manager, the group's root while it is loaded.
    {"Enemy_DungeonBoss_Twinrova", F::mirror, "Twinrova", R::body, true, 0.0f},
    {"Enemy_DungeonBoss_Koume", F::mirror, "Twinrova", R::body, true, 0.0f},
    {"Enemy_DungeonBoss_Kotake", F::mirror, "Twinrova", R::body, true, 0.0f},
    {"Enemy_KotakeKoumeBattleManager", F::mirror, "Twinrova", R::body, true, 0.0f},
};

// Link's held commands and animations (Player_Adult/Child.root.asb, the same in both).
constexpr std::string_view kHeldNames[] = {"Grab",     "GrabA",  "GrabB",    "GrabX", "GrabY",
                                           "GrabWait", "GrabEd", "GrabAway", "Eaten", "FaceGrab"};
constexpr std::string_view kHeldPrefixes[] = {"Grab_", "Eaten_", "Face_Grab"};

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float x = a[0] - b[0];
  const float y = a[1] - b[1];
  const float z = a[2] - b[2];
  return std::sqrt(x * x + y * y + z * z);
}

bool finite(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

std::string hex(std::uint64_t key) {
  char text[24];
  std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(key));
  return text;
}

std::string metres(float v) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f", static_cast<double>(v));
  return text;
}

bool contains(const std::vector<std::string_view>& list, std::string_view v) {
  return std::find(list.begin(), list.end(), v) != list.end();
}

}  // namespace

const char* to_string(BossFamily family) {
  switch (family) {
    case BossFamily::grab: return "grab";
    case BossFamily::tennis: return "tennis";
    case BossFamily::mirror: return "mirror";
  }
  return "?";
}

std::span<const FamilyActor> family_actors() { return kFamilyActors; }

const FamilyActor* family_actor(std::string_view actor) {
  for (const FamilyActor& a : kFamilyActors) {
    if (a.name == actor) return &a;
  }
  return nullptr;
}

bool held_animation(std::string_view command) {
  if (command.empty()) return false;
  for (const auto name : kHeldNames) {
    if (command == name) return true;
  }
  for (const auto prefix : kHeldPrefixes) {
    if (command.starts_with(prefix)) return true;
  }
  return false;
}

std::uint64_t family_key(const protocol::ObjectId& id) {
  if (id.kind == protocol::ObjectKind::placed) return id.value;
  return (std::uint64_t{1} << 63) | ((std::uint64_t{static_cast<std::uint8_t>(id.kind)} & 0x7fu) << 56) |
         (std::uint64_t{id.maker} << 48) | (id.value & 0xffff'ffff'ffffull);
}

std::optional<std::string> family_holds(const FamilyActor& actor, const FamilyHold& hold,
                                        const std::optional<protocol::Vec3>& at) {
  switch (actor.family) {
    case BossFamily::grab: {
      if (hold.held_group.empty() || hold.held_group != actor.group) return std::nullopt;
      if (!actor.whole) {
        // An enemy: the one that holds us (and what stands with it), not its kin elsewhere.
        if (!at || !hold.held_at || !finite(*at) || !finite(*hold.held_at) ||
            distance(*at, *hold.held_at) > kFamilyHoldNear) {
          return std::nullopt;
        }
      }
      return std::string(actor.group) + " holds its owner's Link: its game lets him go (ZMP D-134)";
    }
    case BossFamily::tennis:
      if (hold.rally_group.empty() || hold.rally_group != actor.group) return std::nullopt;
      return std::string(actor.group) + "'s ball is in its owner's game: the volley is theirs (no hand-over "
                                        "mid-volley)";
    case BossFamily::mirror:
      return std::nullopt;  // hand_over_holds (final_rules.hpp)
  }
  return std::nullopt;
}

void FamilyWatch::reset() { *this = FamilyWatch{}; }

FamilyWalkOut FamilyWatch::walk(const FamilyWalkIn& in) {
  FamilyWalkOut out;
  const bool link_known = in.link.has_value() && finite(*in.link);

  // ---- grab: the nearest grabbing part within its reach of our held Link ----
  const FamilySeen* holder = nullptr;
  const FamilyActor* holder_actor = nullptr;
  float best = 0.0f;
  for (const FamilySeen& s : in.seen) {
    const FamilyActor* a = family_actor(s.name);
    if (a == nullptr || a->family != BossFamily::grab || a->role != FamilyRole::part) continue;
    const bool shared = s.id != protocol::ObjectId{};
    if (shared && finite(s.position)) {
      out.parts.push_back({family_key(s.id), s.position, std::string(s.name)});
    }
    if (!in.held || !in.up || !link_known || !finite(s.position)) continue;
    const float d = distance(*in.link, s.position);
    if (d <= a->reach && (holder == nullptr || d < best)) {
      holder = &s;
      holder_actor = a;
      best = d;
    }
  }
  if (holder != nullptr) {
    out.hold.held_group = holder_actor->group;
    out.hold.held_at = holder->position;
    if (holder->id != protocol::ObjectId{}) {
      protocol::Ride ride;
      ride.platform = family_key(holder->id);
      for (std::size_t i = 0; i < 3; ++i) ride.offset[i] = (*in.link)[i] - holder->position[i];
      out.ride = ride;
    }
  }
  const std::string by =
      holder != nullptr ? std::string(holder->name) + " " + hex(family_key(holder->id)) : std::string();
  if (by != held_by_) {
    if (!held_by_.empty()) {
      out.log.push_back("grab: our Link is let go by " + held_by_ + " (" + std::string(held_group_) +
                        "): it may be handed over again (issue 150)");
    }
    if (holder != nullptr) {
      std::string whose;
      if (in.self != 0 && holder->owner == in.self) {
        whose = "ours: our samples of " + std::string(holder_actor->group) +
                " carry owner_keeps while it holds us (ZMP D-134)";
      } else if (holder->owner == 0) {
        whose = "its owner is not known yet: nothing is kept";
      } else {
        whose = "a copy P" + std::to_string(holder->owner) +
                " owns: it is not its owner's AI that holds us, nothing is kept";
      }
      out.log.push_back("grab: our Link is held by " + by + " (" + std::string(holder_actor->group) + "), " +
                        metres(best) + " m from it; " + whose + "; the others draw us on their copy of it");
    }
    held_by_ = by;
    held_group_ = holder != nullptr ? holder_actor->group : std::string_view{};
  }
  if (holder != nullptr && !grab_ok_said_) {
    grab_ok_said_ = true;
    out.log.push_back("check: grab ok our Link held by " + std::string(holder->name) + " " + metres(best) +
                      " m from its origin (" + std::string(holder_actor->group) + ")");
  }
  if (in.held && in.up && holder == nullptr) {
    if (no_holder_since_ == 0) no_holder_since_ = std::max<std::uint64_t>(in.now_us, 1);
    if (!grab_fallback_said_ && in.now_us >= no_holder_since_ && in.now_us - no_holder_since_ >= kNoHolderUs) {
      grab_fallback_said_ = true;
      out.log.push_back(std::string("check: grab fallback our Link's AS command is a held one but no grabbing "
                                    "part of the table is within reach") +
                        (link_known ? "" : " (our Link's place is not read)") +
                        ": nothing is kept, the others draw us on our samples (docs/JEFES.md section 9)");
    }
  } else {
    no_holder_since_ = 0;
  }

  // ---- tennis: a ball of a tennis boss in our game ----
  std::vector<std::string_view> owned;                               // tennis groups we own
  std::vector<std::pair<std::string_view, std::string_view>> balls;  // group, ball
  for (const FamilySeen& s : in.seen) {
    const FamilyActor* a = family_actor(s.name);
    if (a == nullptr || a->family != BossFamily::tennis) continue;
    if (a->role == FamilyRole::body) {
      if (in.self != 0 && s.owner == in.self && !contains(owned, a->group)) owned.push_back(a->group);
    } else {
      balls.emplace_back(a->group, s.name);
    }
  }
  if (!balls.empty()) out.hold.rally_group = balls.front().first;
  for (const auto& ball : balls) {
    if (contains(owned, ball.first)) {
      out.hold.rally_group = ball.first;  // one whose boss we own first
      break;
    }
  }
  const std::string_view rally = contains(owned, out.hold.rally_group) ? out.hold.rally_group : std::string_view{};
  if (rally != rally_said_) {
    if (!rally_said_.empty()) {
      out.log.push_back("tennis: " + std::string(rally_said_) +
                        "'s volley is over: it may be handed over again (issue 150)");
    }
    if (!rally.empty()) {
      out.log.push_back("tennis: " + std::string(rally) +
                        "'s ball is in flight in our game: the volley is ours, our samples of it carry "
                        "owner_keeps (no hand-over mid-volley)");
    }
    rally_said_ = rally;
  }
  std::erase_if(owned_since_, [&owned](const auto& g) { return !contains(owned, g.first); });
  for (const std::string_view group : owned) {
    auto since = std::find_if(owned_since_.begin(), owned_since_.end(),
                              [group](const auto& g) { return g.first == group; });
    if (since == owned_since_.end()) {
      owned_since_.emplace_back(group, in.now_us);
      since = std::prev(owned_since_.end());
    }
    const auto ball =
        std::find_if(balls.begin(), balls.end(), [group](const auto& b) { return b.first == group; });
    if (ball != balls.end()) {
      if (!contains(ball_seen_, group)) ball_seen_.push_back(group);
      if (!contains(tennis_said_, group)) {
        tennis_said_.push_back(group);
        out.log.push_back("check: tennis ok " + std::string(ball->second) + " in our game while we own " +
                          std::string(group) + ": our samples of it carry owner_keeps until the volley ends");
      }
    } else if (!contains(ball_seen_, group) && !contains(tennis_fallback_, group) &&
               in.now_us >= since->second && in.now_us - since->second >= kNoBallUs) {
      tennis_fallback_.push_back(group);
      out.log.push_back("check: tennis fallback we have owned " + std::string(group) + " for " +
                        std::to_string(kNoBallUs / 1'000'000) +
                        " s and seen no ball of the table: its name may differ (docs/JEFES.md section 9), "
                        "a volley is not kept");
    }
  }

  hold_ = out.hold;
  return out;
}

}  // namespace coop::host
