// ORM D-R19: Epona is the group's, each player rides his own (epona.hpp).
#include "coop/host/epona.hpp"

#include <algorithm>

#include "coop/host/game_flags.hpp"

namespace coop::host::epona {

const std::vector<std::pair<std::string_view, std::uint32_t>>& step_values() {
  static const std::vector<std::pair<std::string_view, std::uint32_t>> values{
      {"NotReady", 0x57b849f0u},
      {"Ready", 0x17bfaa46u},
      {"GuideToLonLonRanch", 0x43b92c8fu},
      {"GuideToLonLonRanchAdult", 0xd512c4e6u},
      {"ReadyForGetEponaSong", 0x87c38b24u},
      {"AfterGetEponaSong", 0xcfbd3bf2u},
      {"ReadyForGambleRace", kReadyForGambleRace},
      {"AfterTalkMalon", 0x9c643d4au},
      {"GambleRacePlaying", kGambleRacePlaying},
      {"AfterWonGambleRace", kAfterWonGambleRace},
      {"Complete", kComplete},
  };
  return values;
}

std::optional<std::string_view> value_name(std::uint32_t value) {
  for (const auto& [name, hash] : step_values()) {
    if (hash == value) return name;
  }
  return std::nullopt;
}

EnumFlags shared_step() {
  EnumFlags e;
  e.add(kStep, {kComplete}, true);
  return e;
}

bool is_step(FlagKey key) {
  return key.kind == FlagKeyKind::named && key.hash == kStep && key.index == 0;
}

const char* to_string(Added added) {
  switch (added) {
    case Added::added: return "added";
    case Added::no_sharing: return "no flags are shared";
    case Added::already: return "another list shares it already";
  }
  return "?";
}

Added add_shared_step(std::optional<SharedFlags>& shared) {
  if (!shared) return Added::no_sharing;
  if (shared->enums().contains(kStep)) return Added::already;
  EnumFlags enums = shared->enums();
  if (!enums.merge(shared_step())) return Added::already;  // not reached: checked above
  std::vector<FlagKey> keys = shared->keys();
  keys.push_back(FlagKey(kStep, std::uint16_t{0}));
  shared = SharedFlags(std::move(keys), std::move(enums));  // sorts, drops repeats
  return Added::added;
}

bool race_running(std::optional<std::uint32_t> value) {
  return value && (*value == kGambleRacePlaying || *value == kAfterWonGambleRace);
}

std::optional<std::uint32_t> read_step(GameMemory& memory) {
  return GameFlags(memory).get_enum(kStep);
}

bool own_per_game(std::string_view actor) {
  // Every horse: each player calls and rides his own Epona (D-233); the
  // ranch's, Ingo's and the archery horse belong to that game's own race,
  // archery or cutscene. The puppet's horse (coop_epona.h) is one too.
  if (actor.starts_with("Animal_Epona") || actor.starts_with("Animal_Horse")) return true;
  // Ingo runs the race in each game for its own player (D-234: each game its
  // own race). The child Ingo is only a townsperson: still shared.
  if (actor.starts_with("Npc_Hylia_Ingo_") && actor.ends_with("_Adult")) return true;
  // The horseback archery: its host (Minigame_HorsebackArchery's talker) and
  // the pots and targets each game's own run breaks.
  if (actor == "Npc_Gerudo_008" || actor.starts_with("Obj_Gerudo_HorsebackArchery")) return true;
  return false;
}

bool is_horse(std::string_view actor) {
  return actor.starts_with("Animal_Epona") || actor.starts_with("Animal_Horse");
}

std::string_view horse_look(std::optional<std::string_view> mount) {
  if (!mount || mount->empty()) return kPlainLook;
  const std::string_view m = *mount;
  if (m.starts_with("Animal_Epona")) return kEponaLook;
  if (m == "Animal_Horse_For_Ingo") return kIngoLook;
  if (m.starts_with("Animal_Horse")) return kRanchLook;
  return kPlainLook;
}

Ride ride_of(std::optional<std::string_view> anim) {
  if (!anim) return Ride::none;
  const std::string_view a = *anim;
  // As coop/host/scene.hpp's activity_of: a horse command, but not getting
  // off ("DismountHorse", and the romfs's own "DismounHorse") nor walking up
  // to the horse ("HorseAccess").
  if (a.find("Horse") == std::string_view::npos || a.starts_with("Dismoun") || a == "HorseAccess") {
    return Ride::none;
  }
  if (a.starts_with("HorseRideOn")) return Ride::mounting;
  return Ride::riding;
}

const char* to_string(Ride ride) {
  switch (ride) {
    case Ride::none: return "on foot";
    case Ride::mounting: return "getting on";
    case Ride::riding: return "riding";
  }
  return "?";
}

std::string_view gait_command(Gait gait) {
  switch (gait) {
    case Gait::wait: return "Wait";
    case Gait::walk: return "Move_Gear_1";
    case Gait::trot: return "Move_Gear_2";
    case Gait::canter: return "Move_Gear_3";
    case Gait::gallop: return "Move_Gear_Top";
  }
  return "Wait";
}

Gait gait_for(float speed, Gait now, const GaitSpeeds& s) {
  if (!(speed >= 0.0f)) speed = 0.0f;  // NaN or below 0: standing
  const float lines[4] = {s.walk, s.trot, s.canter, s.gallop};
  // The gait the speed alone gives.
  int bare = 0;
  while (bare < 4 && speed >= lines[bare]) ++bare;
  const int at = static_cast<int>(now);
  if (bare == at) return now;
  // Up: past the next line by the margin; down: under the line by it.
  if (bare > at) {
    int up = at;
    while (up < 4 && speed >= lines[up] + s.margin) ++up;
    return static_cast<Gait>(std::max(up, at));
  }
  int down = at;
  while (down > 0 && speed < lines[down - 1] - s.margin) --down;
  return static_cast<Gait>(down);
}

}  // namespace coop::host::epona
