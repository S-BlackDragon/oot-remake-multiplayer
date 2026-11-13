// Tests for the Gerudo Fortress and Spirit Temple rules (coop/host/gerudo.hpp; oot-remake
// docs/GERUDO_ESPIRITU.md): the actors each game runs on its own, the story age gate, the
// age reload's spot and the carpenter count mend.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include "coop/host/gerudo.hpp"

using namespace coop::host;
using coop::protocol::Age;

namespace {

constexpr std::uint64_t kS = 1'000'000;

CarpenterReading Calm(std::uint64_t now_s, int helped, std::int32_t count) {
  CarpenterReading in;
  in.now_us = now_s * kS;
  in.event_running = false;
  for (int i = 0; i < 4; ++i) {
    in.helped[i] = i < helped;
    in.thief_beaten[i] = i < helped;
  }
  in.helped_count = count;
  in.found_count = count;
  in.thieves_count = helped;
  in.card = false;
  return in;
}

}  // namespace

TEST_CASE("gerudo: the guards run in each game, Nabooru and others do not") {
  CHECK(is_sight_guard("Npc_Gerudo_001"));
  CHECK(is_sight_guard("Npc_Gerudo_FortressGuard"));
  CHECK(is_sight_guard("Npc_Hylia_Soldier_SecurityGuard"));
  CHECK_FALSE(is_sight_guard("Npc_Gerudo_Nabooru_Child"));
  CHECK_FALSE(is_sight_guard("Npc_Gerudo_003"));       // the child valley's, no guard brain
  CHECK_FALSE(is_sight_guard("Npc_Gerudo_0011"));      // exact names, not a prefix
  CHECK_FALSE(is_sight_guard("Npc_Hylia_Soldier_003_Child"));
  // The guide Poe is hazards.hpp's alone (is_own_hazard_actor, D-R67).
  CHECK_FALSE(runs_in_each_game("Npc_GuidePoe_Adult"));
  CHECK(runs_in_each_game("Npc_Gerudo_018"));
  CHECK_FALSE(runs_in_each_game("Npc_Hylia_013_Adult"));  // a carpenter: shared
  CHECK_FALSE(runs_in_each_game("Enemy_MiddleBoss_GerudoThief"));
  const auto& names = sight_guard_names();
  CHECK(names.size() == 37);
  for (std::size_t i = 1; i < names.size(); ++i) CHECK(names[i - 1] < names[i]);
}

TEST_CASE("gerudo: a story start in the Spirit Temple is the starter's age's") {
  CHECK(both_ages_scene("SpiritTemple"));
  CHECK_FALSE(both_ages_scene("DesertColossus_Child"));
  CHECK(story_start_for_age("SpiritTemple", Age::adult, Age::adult));
  CHECK_FALSE(story_start_for_age("SpiritTemple", Age::adult, Age::child));
  CHECK_FALSE(story_start_for_age("SpiritTemple", Age::child, Age::adult));
  // An era in the name: the scene says the age already.
  CHECK(story_start_for_age("DesertColossus_Child", Age::adult, Age::child));
  // Unknown: followed, as before.
  CHECK(story_start_for_age("SpiritTemple", std::nullopt, Age::child));
  CHECK(story_start_for_age("SpiritTemple", Age::adult, std::nullopt));
}

TEST_CASE("gerudo: the age reload keeps the spot except in the Spirit Temple") {
  CHECK_FALSE(age_reload_keeps_spot("SpiritTemple"));
  CHECK(age_reload_keeps_spot("ForestTemple"));
  CHECK(age_reload_keeps_spot("KokiriForest_Child"));
}

TEST_CASE("gerudo: a lost carpenter count is mended once calm, never lowered") {
  CarpenterCounts rule;
  // Agreeing counts: nothing.
  auto out = rule.tick(Calm(0, 2, 2));
  CHECK(out.writes.empty());
  CHECK(out.log.empty());
  // Three free, the count says 2: said, then mended after 3 s of calm.
  out = rule.tick(Calm(10, 3, 2));
  CHECK(out.writes.empty());
  CHECK(out.log.size() == 1);
  out = rule.tick(Calm(12, 3, 2));
  CHECK(out.writes.empty());
  out = rule.tick(Calm(13, 3, 2));
  REQUIRE(out.writes.size() == 2);
  CHECK(out.writes[0] == CarpenterWrite{kCarpentersHelpedCount, 3});
  CHECK(out.writes[1] == CarpenterWrite{kCarpentersFoundCount, 3});
  // A higher count is never lowered.
  CarpenterCounts high;
  CHECK(high.tick(Calm(0, 2, 4)).writes.empty());
  CHECK(high.tick(Calm(10, 2, 4)).writes.empty());
}

TEST_CASE("gerudo: nothing is mended while an event runs or a reading is missing") {
  CarpenterCounts rule;
  auto in = Calm(0, 3, 2);
  in.event_running = true;  // the rescue event adds the count itself
  CHECK(rule.tick(in).writes.empty());
  in.now_us = 10 * kS;
  CHECK(rule.tick(in).writes.empty());
  in.event_running = false;  // the timer starts again once it is over
  in.now_us = 11 * kS;
  CHECK(rule.tick(in).writes.empty());
  in.now_us = 13 * kS;
  CHECK(rule.tick(in).writes.empty());
  in.now_us = 14 * kS;
  CHECK(rule.tick(in).writes.size() == 2);

  CarpenterCounts unknown;
  auto u = Calm(0, 3, 2);
  u.helped[3].reset();
  CHECK(unknown.tick(u).writes.empty());
  u.now_us = 20 * kS;
  CHECK(unknown.tick(u).writes.empty());
  u.event_running.reset();
  u.helped[3] = false;
  u.now_us = 40 * kS;
  CHECK(unknown.tick(u).writes.empty());
}

TEST_CASE("gerudo: four free and no card is said once, a lost thief count is only said") {
  CarpenterCounts rule;
  auto in = Calm(0, 4, 4);
  CHECK(rule.tick(in).log.empty());
  in.now_us = 10 * kS;
  auto out = rule.tick(in);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("no Gerudo card") != std::string::npos);
  in.now_us = 30 * kS;
  CHECK(rule.tick(in).log.empty());

  CarpenterCounts thieves;
  auto t = Calm(0, 2, 2);
  t.thieves_count = 1;
  out = thieves.tick(t);
  CHECK(out.writes.empty());
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("thieves: 2 beaten") != std::string::npos);
  t.now_us = 10 * kS;
  CHECK(thieves.tick(t).log.empty());
}
