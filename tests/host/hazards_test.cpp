// ORM hazards with several players (coop/host/hazards.hpp, oot-remake docs/PELIGROS.md): the flags
// each player keeps (and that nothing the hook builds shares them), the guide Poe as each game's own
// actor, the AS command rules, and the reach point of a Link downed in lava, quicksand, water or the void.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flag_defer.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/hazards.hpp"
#include "coop/host/story.hpp"
#include "coop_chests.h"
#include "coop_dungeon.h"
#include "coop_objects_sync.h"
#include "coop_story.h"

using namespace coop::host;
namespace protocol = coop::protocol;

namespace {

FlagKey hazard_named(std::string_view name) { return FlagKey(flag_hash(name), std::uint16_t{0}); }

// A frame clock and a Link for the reach point: 60 frames a second.
struct Run {
  DownedReach reach;
  std::uint64_t now = 1'000'000;
  PlayerReading link;
  std::vector<std::string> log;

  explicit Run(ReachSettings s = {}) : reach(s) {
    link.scene = "DeathMountainCrater_Adult";
    link.event_running = false;
    play("Wait");
  }
  void play(std::string name) { link.animation = protocol::Animation{std::move(name), 0.0f, 1.0f}; }
  void at(float x, float y, float z) { link.position = {x, y, z}; }
  // One frame; returns what would be sent.
  std::optional<PlayerReading> frame(DownedState state = DownedState::alive) {
    now += 16'667;
    auto out = reach.frame(link, state, now);
    for (auto& l : reach.take_log()) log.push_back(std::move(l));
    return out;
  }
  void frames(int n, DownedState state = DownedState::alive) {
    for (int i = 0; i < n; ++i) frame(state);
  }
  // Moves in a straight line to (x, y, z) over n frames, playing `name`.
  void move(const std::string& name, float x, float y, float z, int n, DownedState state = DownedState::alive) {
    play(name);
    const auto from = link.position;
    for (int i = 1; i <= n; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(n);
      at(from[0] + (x - from[0]) * t, from[1] + (y - from[1]) * t, from[2] + (z - from[2]) * t);
      frame(state);
    }
  }
  bool logged(std::string_view part) const {
    for (const auto& l : log) {
      if (l.find(part) != std::string::npos) return true;
    }
    return false;
  }
};

bool near(const protocol::Vec3& a, const protocol::Vec3& b, float within = 0.05f) {
  for (int i = 0; i < 3; ++i) {
    if (a[i] - b[i] > within || b[i] - a[i] > within) return false;
  }
  return true;
}

}  // namespace

// ---- Flags -------------------------------------------------------------------------------------------

TEST_CASE("hazards: each per-player flag's hash is its name's, once") {
  std::set<std::uint32_t> seen;
  for (const auto& f : hazard_flags()) {
    INFO(f.name);
    CHECK(f.hash == flag_hash(f.name));
    CHECK(seen.insert(f.hash).second);
    CHECK_FALSE(f.why.empty());
    CHECK(is_hazard_flag(FlagKey(f.hash, 0)));
    CHECK(is_hazard_flag(FlagKey(f.hash, 3)));  // any element
  }
  CHECK(hazard_flags().size() == 20);
  CHECK(is_hazard_flag(hazard_named("PlayerStatus.IsActiveGlassTruth")));
  CHECK(is_hazard_flag(hazard_named("GuidePoe_IsGuiding")));
  CHECK(is_hazard_flag(hazard_named("DmF_SY_FallDown_Elevator_Reset")));
  CHECK_FALSE(is_hazard_flag(hazard_named("Gerudo_phantomdesert_gate")));  // the gate is the group's
  CHECK_FALSE(is_hazard_flag(hazard_named("HountedWasteland_TreasureBoxAppear")));
  CHECK_FALSE(is_hazard_flag(FlagKey::key64_flag(0x0187aaa2)));
}

TEST_CASE("hazards: unshare takes only the per-player flags off, enums kept") {
  std::optional<SharedFlags> none;
  CHECK(unshare_hazard_flags(none).empty());
  CHECK_FALSE(none);

  EnumFlags enums;
  REQUIRE(enums.add(flag_hash("Step_SageOfSpirit"), {1u, 2u, 3u}));
  std::optional<SharedFlags> shared = SharedFlags(
      {hazard_named("Gerudo_phantomdesert_gate"), hazard_named("GuidePoe_IsComplete"),
       hazard_named("ForestTemple_IsInFallingCeilingRoom"), FlagKey(flag_hash("PlayerStatus.IsActiveGlassTruth"), 1),
       FlagKey::key64_flag(0x1234)},
      enums);
  const auto taken = unshare_hazard_flags(shared);
  REQUIRE(shared);
  CHECK(taken.size() == 3);
  CHECK(std::find(taken.begin(), taken.end(), "GuidePoe_IsComplete") != taken.end());
  CHECK(shared->size() == 3);  // the gate, the key64 flag and Step_SageOfSpirit's own key
  CHECK(shared->contains(hazard_named("Gerudo_phantomdesert_gate")));
  CHECK(shared->contains(FlagKey::key64_flag(0x1234)));
  CHECK_FALSE(shared->contains(hazard_named("GuidePoe_IsComplete")));
  CHECK(shared->enums().contains(flag_hash("Step_SageOfSpirit")));
  // Again: nothing more to take.
  CHECK(unshare_hazard_flags(shared).empty());
  CHECK(shared->size() == 3);
}

TEST_CASE("hazards: nothing the hook shares is a per-player hazard flag") {
  // What coop_hook builds: the items, upstream's dungeon, story and chest lists, then the kit's
  // dungeon lists, story list and talk list.
  std::optional<SharedFlags> shared = item_shared_flags(true);
  CoopDungeon::Merge(shared, true);
  CoopStory::Merge(shared, true);
  CoopChests::Merge(shared, true);
  REQUIRE(shared);
  // The built-in dungeon list alone shares one (a sensor of one player that the kit takes off).
  {
    std::optional<SharedFlags> builtin = shared;
    const auto taken = unshare_hazard_flags(builtin);
    REQUIRE(taken.size() == 1);
    CHECK(taken[0] == "ForestTemple_IsInFallingCeilingRoom");
  }
  std::string error;
  const auto lists = DungeonList::load_many(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_dungeon_child.txt,"
                                            COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_dungeon_adult.txt",
                                            &error);
  INFO(error);
  REQUIRE(lists);
  apply_dungeon_list(shared, *lists);
  const auto story = StoryList::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_story.txt", &error);
  INFO(error);
  REQUIRE(story);
  const auto talk = DeferRules::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  INFO(error);
  REQUIRE(talk);
  std::vector<FlagKey> keys = shared->keys();
  for (const auto& k : story->shared_flags()) keys.push_back(k);
  for (const auto& k : talk->keys()) keys.push_back(k);
  std::optional<SharedFlags> all = SharedFlags(std::move(keys), shared->enums());
  for (const auto& f : hazard_flags()) {
    INFO(f.name);
    CHECK_FALSE(all->contains(FlagKey(f.hash, 0)));
  }
  CHECK(unshare_hazard_flags(all).empty());
}

// ---- Actors ------------------------------------------------------------------------------------------

TEST_CASE("hazards: the guide Poe is each game's own, other townspeople stay shared") {
  CHECK(is_own_hazard_actor("Npc_GuidePoe_Adult"));
  CHECK(is_own_hazard_actor("Npc_GuidePoe_Child"));
  CHECK_FALSE(is_own_hazard_actor("Npc_Hylia_051"));
  CHECK_FALSE(is_own_hazard_actor("Enemy_PoeSisters_Meg"));
  CHECK_FALSE(CoopObjects::IsShared("Npc_GuidePoe_Adult"));
  CHECK(CoopObjects::IsShared("Npc_Hylia_051"));
  CHECK(CoopObjects::IsShared("Enemy_Freezard"));  // enemies keep their owner (docs/objects.md)
}

// ---- Commands ----------------------------------------------------------------------------------------

TEST_CASE("hazards: the dead commands and what a command says about the place") {
  for (const char* c : {"Dead", "DeadRagdoll", "LandDead", "LavaDead", "SandDead", "SwimDead", "UnderwaterDead",
                        "WhirlpoolDead", "DeadSelf", "Die", "Die_Lava", "Die_Air", "HookMoveDie"}) {
    INFO(c);
    CHECK(is_dead_command(c));
    CHECK_FALSE(firm_ground_command(c));
  }
  for (const char* c : {"Wait", "Move", "Move_Run_Ice", "Wait_Service_Hot", "Wait_Service_Cold", "Swim_Wait",
                        "Fall", "DamageLStand"}) {
    INFO(c);
    CHECK_FALSE(is_dead_command(c));
  }
  CHECK(kind_of_command("LavaDead") == DownKind::lava);
  CHECK(kind_of_command("Move_Run_Lava") == DownKind::lava);
  CHECK(kind_of_command("SandDead") == DownKind::sand);
  CHECK(kind_of_command("JumpSand") == DownKind::sand);
  CHECK(kind_of_command("Evt_SandStorm_Sword") == DownKind::ground);  // walking in the storm
  CHECK(kind_of_command("WhirlpoolDead") == DownKind::whirlpool);
  CHECK(kind_of_command("UnderwaterDead") == DownKind::underwater);
  CHECK(kind_of_command("Swim_Dive") == DownKind::underwater);
  CHECK(kind_of_command("Heavy_Swim_Move") == DownKind::underwater);  // iron boots on the bottom
  CHECK(kind_of_command("SwimDead") == DownKind::water);
  CHECK(kind_of_command("Swim_Wait") == DownKind::water);
  CHECK(kind_of_command("Dead") == DownKind::ground);
  CHECK(kind_of_command("") == DownKind::ground);

  for (const char* c : {"Wait", "Move", "Move_Run_Ice", "Wait_Service_Hot", "Wait_Service_Cold", "Guard",
                        "Horse_Move", "Rolling"}) {
    INFO(c);
    CHECK(firm_ground_command(c));
  }
  for (const char* c : {"", "Fall", "Fall_Danger", "Jump", "Swim_Wait", "Heavy_Swim_Wait_Down", "WaterDive_Fall",
                        "Cliff_Hold_Fall", "Climb", "Ladder", "HookshotMove", "Move_Run_Lava", "JumpSand",
                        "Damage_Burning", "Freeze", "EvtWait", "Float_N"}) {
    INFO(c);
    CHECK_FALSE(firm_ground_command(c));
  }
}

// ---- The reach point ---------------------------------------------------------------------------------

TEST_CASE("hazards: down in lava: sent at the last firm ground until up") {
  Run run;
  run.at(0, 0, 0);
  run.frames(30);
  run.move("Move", 2, 0, 0, 30);
  REQUIRE(run.reach.firm());
  CHECK(near(run.reach.firm()->position, {2, 0, 0}));
  run.move("Move_Run_Lava", 3, -0.3f, 0, 20);  // into the lava: no longer firm
  CHECK(near(run.reach.firm()->position, {2, 0, 0}));

  run.play("LavaDead");
  auto sent = run.frame(DownedState::downed);
  REQUIRE(sent);
  CHECK(run.reach.kind() == DownKind::lava);
  REQUIRE(run.reach.anchor());
  CHECK(near(sent->position, {2, 0, 0}));
  CHECK(sent->animation->name == "LavaDead");  // the rest is ours
  CHECK(run.logged("down in lava ('LavaDead'"));
  CHECK(run.logged("where a mate can stand and hold A"));
  run.frames(120, DownedState::downed);
  CHECK(near(run.frame(DownedState::reviving)->position, {2, 0, 0}));  // still there while revived

  run.play("DamageLStandStartPose");
  sent = run.frame(DownedState::alive);
  CHECK(near(sent->position, {3, -0.3f, 0}));
  CHECK_FALSE(run.reach.anchor());
  CHECK_FALSE(run.reach.kind());
  CHECK(run.logged("up again: our real place is sent again"));
}

TEST_CASE("hazards: down on the ground: the real place, nothing moved") {
  Run run;
  run.at(5, 1, 5);
  run.frames(40);
  run.play("Dead");
  const auto sent = run.frame(DownedState::downed);
  CHECK(near(sent->position, {5, 1, 5}));
  CHECK(run.reach.kind() == DownKind::ground);
  CHECK_FALSE(run.reach.anchor());
  CHECK(run.logged("down on the ground ('Dead'): our real place is sent (a mate can reach us)"));
  run.frames(300, DownedState::downed);
  CHECK(run.log.size() == 1);  // one line, not one a frame
  CHECK_FALSE(run.reach.anchor());
}

TEST_CASE("hazards: the dead command comes a few frames after the death") {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  run.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  run.play("Move_Run_Lava");
  // The command before says lava: decided at once.
  run.frame(DownedState::downed);
  CHECK(run.reach.kind() == DownKind::lava);
  CHECK(run.reach.anchor());

  Run quiet;
  quiet.at(0, 0, 0);
  quiet.frames(40);
  quiet.frames(3, DownedState::downed);  // still "Wait": nothing decided yet
  CHECK(quiet.log.empty());
  quiet.play("SandDead");
  quiet.frame(DownedState::downed);
  CHECK(quiet.reach.kind() == DownKind::sand);
  CHECK(quiet.reach.anchor());
  CHECK(quiet.logged("down in quicksand ('SandDead'"));

  Run late;  // no dead command at all: ground after the wait
  late.at(0, 0, 0);
  late.frames(40);
  late.frames(60, DownedState::downed);
  CHECK(late.log.empty());
  late.frames(80, DownedState::downed);
  CHECK(late.logged("down on the ground ('Wait')"));
}

TEST_CASE("hazards: the void: far below the firm ground, back when the game returns him") {
  Run run;
  run.at(0, 10, 0);
  run.frames(40);
  run.move("Fall", 0, -30, 4, 60);  // off the edge into the void
  run.play("DeadRagdoll");
  auto sent = run.frame(DownedState::downed);
  CHECK(run.reach.kind() == DownKind::abyss);
  REQUIRE(sent);
  CHECK(near(sent->position, {0, 10, 0}));
  CHECK(run.logged("down in the void ('DeadRagdoll', 40.0 m below"));
  // Being revived: the game's fairy branch brings him back from the void, near where he stood.
  run.frames(30, DownedState::reviving);
  run.at(0.5f, 10.2f, 1);
  sent = run.frame(DownedState::reviving);
  CHECK(near(sent->position, {0.5f, 10.2f, 1}));
  CHECK_FALSE(run.reach.anchor());
  CHECK(run.logged("back on firm ground ('DeadRagdoll'"));
  run.frame(DownedState::alive);
  CHECK_FALSE(run.reach.kind());
}

TEST_CASE("hazards: a short fall counts as the void only after a Fall command") {
  Run fell;
  fell.at(0, 4, 0);
  fell.frames(40);
  fell.move("Fall", 0, 0, 1, 20);  // 4 m down off a ledge into a pit
  fell.play("Dead");
  fell.frame(DownedState::downed);
  CHECK(fell.reach.kind() == DownKind::abyss);
  CHECK(fell.reach.anchor());

  Run walked;  // walked down a slope 4 m and was killed there: reachable where he lies
  walked.at(0, 4, 0);
  walked.frames(40);
  walked.move("Move", 0, 0, 10, 200);
  walked.play("Dead");
  walked.frame(DownedState::downed);
  CHECK(walked.reach.kind() == DownKind::ground);
  CHECK_FALSE(walked.reach.anchor());
}

TEST_CASE("hazards: a body that slides into the void while down is moved then") {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  run.play("Dead");
  run.frame(DownedState::downed);
  CHECK(run.reach.kind() == DownKind::ground);
  run.play("DeadRagdoll");
  run.move("DeadRagdoll", 3, -15, 0, 60, DownedState::downed);
  CHECK(run.reach.kind() == DownKind::abyss);
  REQUIRE(run.reach.anchor());
  CHECK(near(*run.reach.anchor(), {0, 0, 0}));
}

TEST_CASE("hazards: under water and on the surface: the shore") {
  Run deep;
  deep.link.scene = "WaterTemple";
  deep.at(10, 5, 10);
  deep.frames(40);
  deep.move("Swim_Move", 20, 4.5f, 10, 60);
  deep.move("Swim_Dive", 20, -8, 12, 60);
  deep.play("UnderwaterDead");
  deep.frame(DownedState::downed);
  CHECK(deep.reach.kind() == DownKind::underwater);
  REQUIRE(deep.reach.anchor());
  CHECK(near(*deep.reach.anchor(), {10, 5, 10}));

  Run surface;
  surface.link.scene = "LakeHylia_Adult";
  surface.at(0, 1, 0);
  surface.frames(40);
  surface.move("Swim_Move", 15, 0.4f, 0, 90);
  surface.play("SwimDead");
  surface.frame(DownedState::downed);
  CHECK(surface.reach.kind() == DownKind::water);
  CHECK(surface.reach.anchor());
}

TEST_CASE("hazards: no firm ground in this scene, or too far: the real place") {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  run.link.scene = "ZoraFountain_Adult";  // a new scene: the old firm ground is gone
  run.at(50, -1, 50);
  run.play("Swim_Wait");
  run.frames(10);
  CHECK_FALSE(run.reach.firm());
  run.play("SwimDead");
  const auto sent = run.frame(DownedState::downed);
  CHECK(near(sent->position, {50, -1, 50}));
  CHECK_FALSE(run.reach.anchor());
  CHECK(run.logged("down in the water ('SwimDead'), no firm ground known near in this scene"));

  ReachSettings s;
  s.max_distance = 20.0f;
  Run far(s);
  far.at(0, 0, 0);
  far.frames(40);
  far.move("Swim_Move", 30, -0.5f, 0, 120);
  far.play("SwimDead");
  far.frame(DownedState::downed);
  CHECK_FALSE(far.reach.anchor());
}

TEST_CASE("hazards: firm ground needs a firm command, held, without an event or a fast climb") {
  Run run;
  run.at(0, 0, 0);
  run.play("Fall");
  run.frames(30);
  CHECK_FALSE(run.reach.firm());
  run.play("Wait");
  run.frames(10);  // 0.17 s on the ground: not yet
  CHECK_FALSE(run.reach.firm());
  run.frames(10);
  CHECK(run.reach.firm());

  Run event;
  event.link.event_running = true;
  event.at(1, 1, 1);
  event.frames(60);
  CHECK_FALSE(event.reach.firm());

  Run lift;  // carried up fast (a lift, a geyser): not firm while it climbs
  lift.at(0, 0, 0);
  lift.play("Wait");
  for (int i = 0; i < 60; ++i) {
    lift.at(0, static_cast<float>(i) * 0.1f, 0);  // 6 m/s up
    lift.frame();
  }
  CHECK_FALSE(lift.reach.firm());
}

TEST_CASE("hazards: a scene change or the game over gives back the real place") {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  run.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  run.play("LavaDead");
  run.frame(DownedState::downed);
  REQUIRE(run.reach.anchor());
  run.link.scene = "DeathMountainTrail_Adult";  // the group's scene change takes him along, down
  run.at(7, 7, 7);
  auto sent = run.frame(DownedState::downed);
  CHECK(near(sent->position, {7, 7, 7}));
  CHECK(run.logged("the scene changed: our real place is sent again"));

  Run over;
  over.at(0, 0, 0);
  over.frames(40);
  over.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  over.play("LavaDead");
  over.frame(DownedState::downed);
  REQUIRE(over.reach.anchor());
  sent = over.frame(DownedState::game_over);
  CHECK(near(sent->position, over.link.position));
  CHECK(over.logged("the game over runs: our real place is sent again"));
}

TEST_CASE("hazards: a frame whose scene was not read is not a scene change") {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  REQUIRE(run.reach.firm());
  const auto scene = run.link.scene;
  run.link.scene.reset();  // the reader missed the scene for a frame, Link still read
  run.frame();
  run.link.scene = scene;
  run.frame();
  REQUIRE(run.reach.firm());  // the firm ground is still known
  CHECK(run.reach.firm()->scene == "DeathMountainCrater_Adult");

  run.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  run.play("LavaDead");
  run.frame(DownedState::downed);
  REQUIRE(run.reach.anchor());
  run.link.scene.reset();
  auto sent = run.frame(DownedState::downed);
  REQUIRE(sent);
  CHECK(near(sent->position, {0, 0, 0}));  // still the firm ground
  run.link.scene = scene;
  sent = run.frame(DownedState::downed);
  CHECK(near(sent->position, {0, 0, 0}));
  CHECK(run.reach.anchor());
  CHECK_FALSE(run.logged("the scene changed"));

  // Down on a frame whose scene was not read: the firm ground of the scene we are in still counts.
  Run quiet;
  quiet.at(0, 0, 0);
  quiet.frames(40);
  quiet.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  quiet.link.scene.reset();
  quiet.play("LavaDead");
  quiet.frame(DownedState::downed);
  CHECK(quiet.reach.kind() == DownKind::lava);
  REQUIRE(quiet.reach.anchor());
  CHECK(near(*quiet.reach.anchor(), {0, 0, 0}));
}

TEST_CASE("hazards: off, the reading goes out as it is, no reading, nothing") {
  ReachSettings s;
  s.on = false;
  Run run(s);
  run.at(0, 0, 0);
  run.frames(40);
  run.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  run.play("LavaDead");
  const auto sent = run.frame(DownedState::downed);
  CHECK(near(sent->position, {1, -0.2f, 0}));
  CHECK(run.log.empty());

  DownedReach reach;
  CHECK_FALSE(reach.frame(std::nullopt, DownedState::downed, 1));
  CHECK(reach.take_log().empty());
}

// ---- docs/RIESGOS_RESUELTOS.md ("Mundo e historia"): which way the kind was told, and the revive's outcome

TEST_CASE("hazards: each down line says what told its kind") {
  Run lava;
  lava.at(0, 0, 0);
  lava.frames(40);
  lava.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  lava.play("LavaDead");
  lava.frame(DownedState::downed);
  CHECK(lava.logged("; kind by its dead command"));

  Run before;
  before.at(0, 0, 0);
  before.frames(40);
  before.move("Move_Run_Lava", 1, -0.2f, 0, 10);
  before.frame(DownedState::downed);  // still Move_Run_Lava: the command before tells
  CHECK(before.logged("; kind by the command before it ('Move_Run_Lava')"));

  Run plain;
  plain.at(0, 0, 0);
  plain.frames(40);
  plain.play("Dead");
  plain.frame(DownedState::downed);
  CHECK(plain.logged("; kind by no command said more than dead"));

  Run late;
  late.at(0, 0, 0);
  late.frames(40);
  late.frames(140, DownedState::downed);
  CHECK(late.logged("; kind by no dead command in 2.0 s, nor the command before: on the ground"));

  Run fell;
  fell.at(0, 10, 0);
  fell.frames(40);
  fell.move("Fall", 0, -30, 4, 60);
  fell.play("DeadRagdoll");
  fell.frame(DownedState::downed);
  CHECK(fell.logged("; kind by its height (the commands said nothing more)"));
}

namespace {

// Down in lava beside firm ground at the origin, revived; returns the run just up again.
Run revived_from_lava() {
  Run run;
  run.at(0, 0, 0);
  run.frames(40);
  run.move("Move_Run_Lava", 1, -0.3f, 0, 10);
  run.play("LavaDead");
  run.frames(60, DownedState::downed);
  run.frames(30, DownedState::reviving);
  run.play("DamageLStandStartPose");
  run.frame(DownedState::alive);
  REQUIRE(run.logged("up again: our real place is sent again"));
  return run;
}

}  // namespace

TEST_CASE("hazards: after a revive from lava: back on firm ground, down again, or stuck") {
  SUBCASE("the game takes us out: firm ground again") {
    Run run = revived_from_lava();
    run.move("Move", 0, 0, 0, 30);
    run.frames(30);
    CHECK(run.logged("after the revive in lava: on firm ground again"));
    CHECK(run.logged("(the game brought us back)"));
  }
  SUBCASE("down again before standing") {
    Run run = revived_from_lava();
    run.play("Move_Run_Lava");
    run.frames(60);
    run.play("LavaDead");
    run.frame(DownedState::downed);
    CHECK(run.logged("after the revive in lava: down again 1.0 s later (the game did not take us out in time)"));
  }
  SUBCASE("still in it 5 s later") {
    Run run = revived_from_lava();
    run.play("Move_Run_Lava");
    run.frames(320);
    CHECK(run.logged("after the revive in lava: not on firm ground 5.0 s later (0.3 m below and 1.0 m from where we "
                     "last stood): the game did not bring us back"));
  }
  SUBCASE("a revive from the ground is not watched") {
    Run run;
    run.at(0, 0, 0);
    run.frames(40);
    run.play("Dead");
    run.frames(10, DownedState::downed);
    run.play("Wait");
    run.frames(400);
    CHECK_FALSE(run.logged("after the revive"));
  }
}
