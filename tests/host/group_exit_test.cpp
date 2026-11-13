// Tests for the scene exits the group takes together (coop/host/group_exit.hpp; oot-64
// D-165, the escape from Ganon's castle).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include "coop/host/group_exit.hpp"

using namespace coop::host;

namespace {

constexpr std::uint64_t kS = 1'000'000;

GroupExitInput in_escape(std::uint64_t now_s, std::vector<MateScene> mates) {
  GroupExitInput in;
  in.now_us = now_s * kS;
  in.scene = "CrumblingGanonCastle";
  in.mates = std::move(mates);
  return in;
}

}  // namespace

TEST_CASE("group exit: the default is the escape to Ganon's field") {
  const auto exits = default_group_exits();
  REQUIRE(exits.size() == 1);
  CHECK(exits[0] == GroupExit{"CrumblingGanonCastle", "FinalBattleField"});
}

TEST_CASE("group exit: a game still in the escape follows the first mate who got out") {
  GroupExitRule rule;
  // P3 and P2 reach the field; P4 is still running with us.
  auto a = rule.update(in_escape(0, {{4, "CrumblingGanonCastle"}, {3, "FinalBattleField"},
                                     {2, "FinalBattleField"}}));
  CHECK_FALSE(a.follow);  // just seen there: a load may flicker
  a = rule.update(in_escape(2, {{4, "CrumblingGanonCastle"}, {3, "FinalBattleField"},
                                {2, "FinalBattleField"}}));
  REQUIRE(a.follow);
  CHECK(*a.follow == 2);  // the lowest number of those out
  CHECK(a.to == "FinalBattleField");
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0] ==
        "group exit: P2 is in FinalBattleField, we are still in CrumblingGanonCastle: following "
        "(first ask)");
  // Not again at once; again after the retry time while we are still there.
  CHECK_FALSE(rule.update(in_escape(5, {{2, "FinalBattleField"}})).follow);
  a = rule.update(in_escape(13, {{2, "FinalBattleField"}}));
  REQUIRE(a.follow);
  CHECK(a.log[0].ends_with("(asked again)"));
  // We got there: nothing more.
  GroupExitInput there = in_escape(14, {{2, "FinalBattleField"}});
  there.scene = "FinalBattleField";
  CHECK_FALSE(rule.update(there).follow);
}

TEST_CASE("group exit: nothing while all are still in the escape, or out of it") {
  GroupExitRule rule;
  CHECK_FALSE(rule.update(in_escape(0, {{2, "CrumblingGanonCastle"}})).follow);
  CHECK_FALSE(rule.update(in_escape(5, {{2, "CrumblingGanonCastle"}})).follow);
  // In the castle's trials, not the escape: a mate at the last fight moves nobody.
  GroupExitInput trials = in_escape(0, {{2, "FinalBattleField"}});
  trials.scene = "InsideGanonCastle";
  CHECK_FALSE(rule.update(trials).follow);
  trials.now_us = 5 * kS;
  CHECK_FALSE(rule.update(trials).follow);
  // Loading (no scene read): nothing.
  GroupExitInput loading = in_escape(9, {{2, "FinalBattleField"}});
  loading.scene.reset();
  CHECK_FALSE(rule.update(loading).follow);
}

TEST_CASE("group exit: a downed mate does not lead, a downed Link waits until he is up") {
  GroupExitRule rule;
  MateScene down{2, "FinalBattleField", false};
  rule.update(in_escape(0, {down}));
  CHECK_FALSE(rule.update(in_escape(3, {down})).follow);

  GroupExitRule ours;
  auto mates = std::vector<MateScene>{{2, "FinalBattleField"}};
  ours.update(in_escape(0, mates));
  GroupExitInput downed = in_escape(2, mates);
  downed.up = false;
  auto a = ours.update(downed);
  CHECK_FALSE(a.follow);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0].find("we follow when our Link is up") != std::string::npos);
  downed.now_us = 3 * kS;
  CHECK(ours.update(downed).log.empty());  // said once
  // Revived (or back after his own Continue): he follows at once.
  a = ours.update(in_escape(4, mates));
  REQUIRE(a.follow);
  CHECK(*a.follow == 2);
}

TEST_CASE("group exit: an event in our game makes it wait") {
  GroupExitRule rule;
  auto mates = std::vector<MateScene>{{5, "FinalBattleField"}};
  rule.update(in_escape(0, mates));
  GroupExitInput ev = in_escape(2, mates);
  ev.event = true;
  auto a = rule.update(ev);
  CHECK_FALSE(a.follow);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0] == "group exit: P5 is in FinalBattleField: we follow when our event ends");
  CHECK(rule.update(in_escape(3, mates)).follow == std::optional<std::uint8_t>(5));
}

TEST_CASE("group exit: a mate must stay in the next scene for the settle time") {
  GroupExitRule rule;
  rule.update(in_escape(0, {{2, "FinalBattleField"}}));
  // He went back (a load seen half way): the clock starts again when he is there again.
  rule.update(in_escape(1, {{2, "CrumblingGanonCastle"}}));
  GroupExitInput again = in_escape(2, {{2, "FinalBattleField"}});
  CHECK_FALSE(rule.update(again).follow);
  again.now_us = 2 * kS + kS / 2;
  CHECK_FALSE(rule.update(again).follow);
  again.now_us = 3 * kS;
  CHECK(rule.update(again).follow);
}

TEST_CASE("group exit: off, nothing, and other exits can be given") {
  GroupExitSettings off;
  off.on = false;
  GroupExitRule none(default_group_exits(), off);
  none.update(in_escape(0, {{2, "FinalBattleField"}}));
  CHECK_FALSE(none.update(in_escape(5, {{2, "FinalBattleField"}})).follow);

  GroupExitRule custom({GroupExit{"A", "B"}});
  GroupExitInput in;
  in.scene = "A";
  in.mates = {{6, "B"}};
  custom.update(in);
  in.now_us = 2 * kS;
  CHECK(custom.update(in).follow == std::optional<std::uint8_t>(6));
  custom.reset();
  in.now_us = 3 * kS;
  CHECK_FALSE(custom.update(in).follow);  // forgotten: seen anew
}
