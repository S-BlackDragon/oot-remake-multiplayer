// ORM Gerudo Fortress, the Haunted Wasteland and the Spirit Temple with several players: the
//   actors each game runs on its own, the story age gate, the age reload's spot and the
//   carpenter count mend. The rules are described in coop/host/gerudo.hpp.
// Doc: oot-remake docs/GERUDO_ESPIRITU.md.
// Switch: LICHII_ORM_GERUDO, or LICHII_COOP_GERUDO (lichii/hook/coop_gerudo.h; on unless 0).
// Test: tests/host/gerudo_test.cpp.

#include "coop/host/gerudo.hpp"

#include <algorithm>
#include <cstdio>

#include "coop/host/scene.hpp"

namespace coop::host {

namespace {

// Read from the romfs (Pack/Actor/*.pack.zs whose strings name
// AI/NpcSecurityGuard.brain.root.ainb), 2026-11-11. Sorted.
const std::vector<std::string_view>& Guards() {
  static const std::vector<std::string_view> names = [] {
    std::vector<std::string_view> v = {
        "Npc_Gerudo_001",
        "Npc_Gerudo_002",
        "Npc_Gerudo_002_Adult",
        "Npc_Gerudo_003_Adult",
        "Npc_Gerudo_004",
        "Npc_Gerudo_005",
        "Npc_Gerudo_006",
        "Npc_Gerudo_007",
        "Npc_Gerudo_008",
        "Npc_Gerudo_009",
        "Npc_Gerudo_010",
        "Npc_Gerudo_011",
        "Npc_Gerudo_012",
        "Npc_Gerudo_013",
        "Npc_Gerudo_014",
        "Npc_Gerudo_015",
        "Npc_Gerudo_016",
        "Npc_Gerudo_017",
        "Npc_Gerudo_018",
        "Npc_Gerudo_020",
        "Npc_Gerudo_FortressGuard",
        "Npc_Gerudo_GEN_SecurityGuard",
        "Npc_Hylia_Soldier_010_Child",
        "Npc_Hylia_Soldier_011_Child",
        "Npc_Hylia_Soldier_012_Child",
        "Npc_Hylia_Soldier_013_Child",
        "Npc_Hylia_Soldier_015_Child",
        "Npc_Hylia_Soldier_016_Child",
        "Npc_Hylia_Soldier_017_Child",
        "Npc_Hylia_Soldier_018_Child",
        "Npc_Hylia_Soldier_019_Child",
        "Npc_Hylia_Soldier_021_Child",
        "Npc_Hylia_Soldier_023_Child",
        "Npc_Hylia_Soldier_024_Child",
        "Npc_Hylia_Soldier_025_Child",
        "Npc_Hylia_Soldier_026_Child",
        "Npc_Hylia_Soldier_SecurityGuard",
    };
    std::sort(v.begin(), v.end());
    return v;
  }();
  return names;
}

std::string Seconds(std::uint64_t us) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f", static_cast<double>(us) / 1e6);
  return text;
}

}  // namespace

const std::vector<std::string_view>& sight_guard_names() { return Guards(); }

bool is_sight_guard(std::string_view actor) {
  const auto& names = Guards();
  return std::binary_search(names.begin(), names.end(), actor);
}

bool runs_in_each_game(std::string_view actor) { return is_sight_guard(actor); }

bool both_ages_scene(std::string_view scene) { return scene == "SpiritTemple"; }

bool story_start_for_age(std::string_view scene, std::optional<protocol::Age> starter,
                         std::optional<protocol::Age> ours) {
  if (!starter || !ours) return true;
  if (place_of(scene) != scene) return true;  // _Child or _Adult: the scene says the age
  return *starter == *ours;
}

bool age_reload_keeps_spot(std::string_view scene) { return !both_ages_scene(scene); }

std::optional<protocol::Vec3> age_reload_start(std::string_view scene) {
  if (scene == "SpiritTemple") return protocol::Vec3{0.0f, 100.25f, 7.66f};  // SpiritTemple_Entrance
  return std::nullopt;
}

void CarpenterCounts::reset() {
  low_since_.reset();
  not_before_ = 0;
  all_free_since_.reset();
  card_said_ = false;
  low_said_.reset();
  thieves_said_.reset();
}

CarpenterOutput CarpenterCounts::tick(const CarpenterReading& in) {
  CarpenterOutput out;
  const std::uint64_t now = in.now_us;
  const bool calm = in.event_running.has_value() && !*in.event_running;

  int helped = 0;
  bool helped_known = true;
  for (const auto& h : in.helped) {
    if (!h) {
      helped_known = false;
    } else if (*h) {
      ++helped;
    }
  }
  int beaten = 0;
  bool beaten_known = true;
  for (const auto& b : in.thief_beaten) {
    if (!b) {
      beaten_known = false;
    } else if (*b) {
      ++beaten;
    }
  }

  // The thieves' count: only said (what it picks in the game is not known).
  if (beaten_known && in.thieves_count) {
    if (*in.thieves_count >= beaten) {
      thieves_said_.reset();
    } else if (calm && thieves_said_ != *in.thieves_count) {
      thieves_said_ = *in.thieves_count;
      out.log.push_back("thieves: " + std::to_string(beaten) + " beaten here, the count (" +
                        std::string(kThievesBeatenCount) + ") says " + std::to_string(*in.thieves_count) +
                        ": one was lost to two at once; not mended (GERUDO_ESPIRITU.md)");
    }
  }

  // The carpenters' counts: mended up to what our game shows.
  const bool helped_low = helped_known && in.helped_count && *in.helped_count < helped;
  const bool found_low = helped_known && in.found_count && *in.found_count < helped;
  if (!helped_low && !found_low) {
    low_since_.reset();
    if (helped_known && in.helped_count) low_said_.reset();
  } else if (!calm) {
    low_since_.reset();  // an event may be adding it now: start again once it is over
  } else {
    const std::int32_t count = in.helped_count.value_or(helped);
    if (!low_since_) {
      low_since_ = now;
      if (low_said_ != count) {
        low_said_ = count;
        out.log.push_back("carpenters: " + std::to_string(helped) + " free here, the count says " +
                          std::to_string(count) +
                          (found_low ? " (found " + std::to_string(*in.found_count) + ")" : std::string()) +
                          ": mended in " + Seconds(settings_.settle_us) + " s if nothing runs here");
      }
    }
    if (now - *low_since_ >= settings_.settle_us && now >= not_before_) {
      std::string what;
      if (helped_low) {
        out.writes.push_back(CarpenterWrite{kCarpentersHelpedCount, helped});
        what = "helped " + std::to_string(*in.helped_count) + " -> " + std::to_string(helped);
      }
      if (found_low) {
        out.writes.push_back(CarpenterWrite{kCarpentersFoundCount, helped});
        what += (what.empty() ? "" : ", ") + std::string("found ") + std::to_string(*in.found_count) + " -> " +
                std::to_string(helped);
      }
      out.log.push_back("carpenters: count mended: " + what +
                        " (a rescue in another game was lost to one at the same moment)");
      not_before_ = now + settings_.retry_us;
      low_since_.reset();
    }
  }

  // All four free and no card: said once.
  if (!(helped_known && helped == 4 && in.card.has_value() && !*in.card)) {
    all_free_since_.reset();
    if (in.card.value_or(false)) card_said_ = false;
  } else if (!calm) {
    all_free_since_.reset();
  } else {
    if (!all_free_since_) all_free_since_ = now;
    if (!card_said_ && now - *all_free_since_ >= settings_.card_wait_us) {
      card_said_ = true;
      out.log.push_back(
          "carpenters: all four are free and this game has no Gerudo card (" + std::string(kGerudoCard) +
          "): the card's event (Meet_FourCarpenter_ClearEvent) has not run in any game, the card being "
          "a shared item; the way out is in GERUDO_ESPIRITU.md");
    }
  }
  return out;
}

}  // namespace coop::host
