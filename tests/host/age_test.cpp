// ORM "the room's age" (coop/host/age.hpp, D-R17): a mate's age change is
// followed by a reload of our own scene at the other age.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "coop/host/age.hpp"
#include "coop/protocol/orm_status.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

bool logged(const AgeOutput& o, const std::string& part) {
  for (const auto& l : o.log) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

AgeMate mate(Age age, std::string scene, std::uint8_t status = 0) {
  return AgeMate{2, age, std::move(scene), status};
}

// Our game standing in Kokiri as a child, a mate in the room.
AgeInput calm(std::uint64_t ms, Age ours, AgeMate m, std::string scene = "KokiriForest_Child") {
  AgeInput in;
  in.now_us = ms * 1000;
  in.age = ours;
  in.scene = std::move(scene);
  in.position = proto::Vec3{-81.9f, 122.3f, 33.7f};
  in.event_running = false;
  in.mates = {std::move(m)};
  return in;
}

// An event named (LICHII_ORM_AGE_EVENT): the in-place reload. With none, the
// game's own era change (the tests at the end of this file).
constexpr const char* kInPlace = "OurCarrierEvent";

}  // namespace

TEST_CASE("age: the scene names say the age, and the other age's scene") {
  CHECK(age_of_scene("KokiriForest_Child") == Age::child);
  CHECK(age_of_scene("Market_Adult") == Age::adult);
  CHECK_FALSE(age_of_scene("WaterTemple"));
  CHECK_FALSE(age_of_scene("_Adult"));  // nothing before it
  CHECK(scene_for_age("KokiriForest_Child", Age::adult) == "KokiriForest_Adult");
  CHECK(scene_for_age("Market_Adult", Age::child) == "Market_Child");
  CHECK(scene_for_age("Market_Adult", Age::adult) == "Market_Adult");
  CHECK(scene_for_age("InsideTheDekuTree", Age::adult) == "InsideTheDekuTree");
}

TEST_CASE("age: a mate turning adult is followed: our event, the scene change, Link put back") {
  AgeFollow f({}, kInPlace);
  auto o = f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  o = f.tick(calm(2500, Age::child, mate(Age::child, "Market_Child")));
  CHECK(o.log.empty());  // first seen: no change
  CHECK_FALSE(f.pending());

  // He pulls the sword: his own cutscene, his load (adult for a moment while
  // loading is not trusted), then adult in Market_Adult.
  f.tick(calm(3000, Age::child, mate(Age::adult, "Market_Child")));  // scene disagrees: ignored
  f.tick(calm(3100, Age::child, mate(Age::adult, "Market_Adult", proto::status::loading)));
  o = f.tick(calm(4000, Age::child, mate(Age::adult, "Market_Adult")));
  CHECK_FALSE(f.pending());  // not settled yet
  o = f.tick(calm(6100, Age::child, mate(Age::adult, "Market_Adult")));
  CHECK(logged(o, "P2 is adult now (was child)"));
  CHECK(logged(o, "we follow: reload as adult"));
  REQUIRE(f.pending() == Age::adult);
  CHECK_FALSE(o.ask_event);  // calm must last a second first

  // A talk of ours: wait.
  auto busy = calm(6500, Age::child, mate(Age::adult, "Market_Adult"));
  busy.event_running = true;
  CHECK_FALSE(f.tick(busy).ask_event);
  CHECK_FALSE(f.tick(calm(7000, Age::child, mate(Age::adult, "Market_Adult"))).ask_event);
  o = f.tick(calm(8100, Age::child, mate(Age::adult, "Market_Adult")));
  REQUIRE(o.ask_event);
  CHECK(*o.ask_event == kInPlace);
  CHECK_FALSE(o.set_era);  // not before an event of ours runs: a refused request leaves the era alone
  CHECK(logged(o, "reloading as adult: KokiriForest_Child -> KokiriForest_Adult, Link at -81.90 122.30 33.70"));
  CHECK(f.reloading());

  auto ev = calm(8300, Age::child, mate(Age::adult, "Market_Adult"));
  ev.event_running = true;
  o = f.tick(ev);
  REQUIRE(o.ask_scene);
  CHECK(*o.ask_scene == "KokiriForest_Adult");
  CHECK(o.set_era == Age::adult);  // now: our event runs
  CHECK(logged(o, "event running 0.20 s after it was asked"));
  ev.now_us = 8500'000;
  CHECK_FALSE(f.tick(ev).ask_scene);
  ev.now_us = 8800'000;
  CHECK(f.tick(ev).ask_scene);  // again every 0.5 s while the event runs

  AgeInput load = ev;
  load.now_us = 9000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  AgeInput arrive = calm(20000, Age::adult, mate(Age::adult, "Market_Adult"), "KokiriForest_Adult");
  arrive.event_running = true;  // the arrival's own event
  arrive.position = proto::Vec3{0.0f, 120.0f, 0.0f};  // the scene's start
  o = f.tick(arrive);
  CHECK(logged(o, "loading KokiriForest_Adult"));
  arrive.now_us = 20100'000;
  o = f.tick(arrive);
  CHECK_FALSE(o.move_to);
  arrive.now_us = 22000'000;
  arrive.event_running = false;
  o = f.tick(arrive);
  CHECK(logged(o, "now adult in KokiriForest_Adult, 13.9 s after the reload began, Link put back"));
  REQUIRE(o.move_to);
  CHECK((*o.move_to)[0] == doctest::Approx(-81.9f));
  CHECK(o.cancel_scene);
  CHECK_FALSE(f.pending());
  CHECK_FALSE(f.reloading());

  // Our own change seen from him is not his: nothing more.
  o = f.tick(calm(30000, Age::adult, mate(Age::adult, "Market_Adult"), "KokiriForest_Adult"));
  CHECK_FALSE(o.ask_event);
}

TEST_CASE("age: a mate changing to our age, or going back before we reload, asks nothing") {
  AgeFollow f({}, kInPlace);
  f.tick(calm(0, Age::adult, mate(Age::child, "Market_Child"), "KokiriForest_Adult"));
  f.tick(calm(2500, Age::adult, mate(Age::child, "Market_Child"), "KokiriForest_Adult"));
  f.tick(calm(3000, Age::adult, mate(Age::adult, "Market_Adult"), "KokiriForest_Adult"));
  auto o = f.tick(calm(5100, Age::adult, mate(Age::adult, "Market_Adult"), "KokiriForest_Adult"));
  CHECK(logged(o, "P2 is adult now"));
  CHECK_FALSE(f.pending());  // we are adult already

  // Down: the reload waits; he goes back to child meanwhile and we are adult.
  AgeFollow g({}, kInPlace);
  g.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  g.tick(calm(2500, Age::child, mate(Age::child, "Market_Child")));
  g.tick(calm(3000, Age::child, mate(Age::adult, "Market_Adult")));
  auto down = calm(5100, Age::child, mate(Age::adult, "Market_Adult"));
  down.downed = true;
  o = g.tick(down);
  REQUIRE(g.pending() == Age::adult);
  for (std::uint64_t t = 5200; t < 9000; t += 100) {
    down.now_us = t * 1000;
    CHECK_FALSE(g.tick(down).ask_event);
  }
  down.mates = {mate(Age::child, "Market_Child")};
  down.now_us = 9100'000;
  g.tick(down);
  down.now_us = 11200'000;
  o = g.tick(down);
  CHECK(logged(o, "P2 is child now (was adult)"));
  CHECK(logged(o, "already child: nothing to reload"));
  CHECK_FALSE(g.pending());
  CHECK_FALSE(g.tick(calm(13000, Age::child, mate(Age::child, "Market_Child"))).ask_event);
}

TEST_CASE("age: no event of ours starts: tried three times, then again 10 s later") {
  AgeFollow f({}, kInPlace);
  f.change(Age::adult, "test");
  auto o = f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  CHECK(logged(o, "change to adult asked (test)"));
  CHECK_FALSE(o.ask_event);
  o = f.tick(calm(1100, Age::child, mate(Age::child, "Market_Child")));
  REQUIRE(o.ask_event);
  o = f.tick(calm(3200, Age::child, mate(Age::child, "Market_Child")));
  CHECK(logged(o, "try 2"));
  CHECK(o.ask_event);
  o = f.tick(calm(5300, Age::child, mate(Age::child, "Market_Child")));
  CHECK(logged(o, "try 3"));
  o = f.tick(calm(7400, Age::child, mate(Age::child, "Market_Child")));
  CHECK(logged(o, "no event of ours started (3 tries"));
  CHECK(o.cancel_scene);
  CHECK_FALSE(o.set_era);  // nothing was written: nothing to put back
  CHECK_FALSE(logged(o, "Sequence_IsChildhood"));
  CHECK_FALSE(f.reloading());
  REQUIRE(f.pending() == Age::adult);  // kept: tried again
  CHECK_FALSE(f.tick(calm(12000, Age::child, mate(Age::child, "Market_Child"))).ask_event);
  CHECK_FALSE(f.tick(calm(17000, Age::child, mate(Age::child, "Market_Child"))).ask_event);
  CHECK(f.tick(calm(18500, Age::child, mate(Age::child, "Market_Child"))).ask_event);
}

TEST_CASE("age: the event ends with no scene change, or the load ends at the old age") {
  AgeFollow f({}, kInPlace);
  f.change(Age::adult, "test");
  f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  REQUIRE(f.tick(calm(1100, Age::child, mate(Age::child, "Market_Child"))).ask_event);
  auto ev = calm(1200, Age::child, mate(Age::child, "Market_Child"));
  ev.event_running = true;
  REQUIRE(f.tick(ev).ask_scene);
  auto o = f.tick(calm(3000, Age::child, mate(Age::child, "Market_Child")));
  CHECK(logged(o, "the event ended with no scene change"));
  CHECK(o.cancel_scene);
  CHECK(o.set_era == Age::child);  // written when the event ran: back to our Link's age
  CHECK(logged(o, "Sequence_IsChildhood put back to child (our Link's age)"));

  AgeFollow g({}, "DmF_SY_FallDown");
  g.change(Age::child, "test");
  g.tick(calm(0, Age::adult, mate(Age::adult, "Market_Adult"), "WaterTemple"));
  o = g.tick(calm(1100, Age::adult, mate(Age::adult, "Market_Adult"), "WaterTemple"));
  REQUIRE(o.ask_event);
  CHECK(*o.ask_event == "DmF_SY_FallDown");
  CHECK_FALSE(o.set_era);
  ev = calm(1200, Age::adult, mate(Age::adult, "Market_Adult"), "WaterTemple");
  ev.event_running = true;
  o = g.tick(ev);
  CHECK(o.set_era == Age::child);
  REQUIRE(o.ask_scene);
  CHECK(*o.ask_scene == "WaterTemple");  // no age in the name: the same scene
  AgeInput load = ev;
  load.now_us = 2000'000;
  load.scene.reset();
  g.tick(load);
  g.tick(calm(10000, Age::adult, mate(Age::adult, "Market_Adult"), "WaterTemple"));  // still adult
  o = g.tick(calm(47000, Age::adult, mate(Age::adult, "Market_Adult"), "WaterTemple"));
  CHECK(logged(o, "did not take: in WaterTemple at adult after the load"));
  CHECK(o.set_era == Age::adult);  // the load kept our Link adult: the era says so again
  CHECK(g.pending() == Age::child);
}

TEST_CASE("age: a mate who leaves and comes back is seen afresh, not as a change") {
  AgeFollow f({}, kInPlace);
  f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  f.tick(calm(2500, Age::child, mate(Age::child, "Market_Child")));
  AgeInput alone = calm(3000, Age::child, mate(Age::child, "Market_Child"));
  alone.mates.clear();
  f.tick(alone);
  f.tick(calm(4000, Age::child, mate(Age::adult, "Market_Adult")));
  auto o = f.tick(calm(6500, Age::child, mate(Age::adult, "Market_Adult")));
  CHECK(o.log.empty());
  CHECK_FALSE(f.pending());
}

// ORM, the endgame (final_rules.hpp ending_holds_age): the credits load Link
// as a child; that change is the ending's, not the room's.
TEST_CASE("age: the ending holds the age: no change followed, a pending reload dropped") {
  AgeFollow f({}, kInPlace);
  const auto adult_here = [](std::uint64_t ms, Age theirs, std::string scene) {
    return calm(ms, Age::adult, mate(theirs, std::move(scene)), "KokiriForest_Adult");
  };
  f.tick(adult_here(0, Age::adult, "Market_Adult"));
  f.tick(adult_here(2500, Age::adult, "Market_Adult"));  // first seen: adult
  // Ganon sealed: his credits make him a child; nothing is followed.
  auto held = adult_here(3000, Age::child, "Market_Child");
  held.held = "Ganon is sealed (Step_Defeat_Ganon Complete)";
  auto o = f.tick(held);
  CHECK(logged(o, "age changes held: Ganon is sealed"));
  held.now_us = 6000'000;
  o = f.tick(held);
  CHECK(o.log.empty());  // said once
  CHECK_FALSE(f.pending());
  // The hold over: what he is now is first seen, not a change.
  o = f.tick(adult_here(7000, Age::child, "Market_Child"));
  CHECK(logged(o, "age changes followed again"));
  o = f.tick(adult_here(9500, Age::child, "Market_Child"));
  CHECK_FALSE(logged(o, "P2 is child now"));
  CHECK_FALSE(f.pending());
  CHECK_FALSE(f.tick(adult_here(12000, Age::child, "Market_Child")).ask_event);

  // A change seen before the hold, its reload not begun: dropped.
  AgeFollow g({}, kInPlace);
  g.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  g.tick(calm(2500, Age::child, mate(Age::child, "Market_Child")));
  g.tick(calm(3000, Age::child, mate(Age::adult, "Market_Adult")));
  g.tick(calm(5100, Age::child, mate(Age::adult, "Market_Adult")));
  REQUIRE(g.pending() == Age::adult);
  auto ending = calm(5200, Age::child, mate(Age::adult, "Market_Adult"));
  ending.held = "the ending runs ('Dm_Ending_0000')";
  o = g.tick(ending);
  CHECK(logged(o, "the reload as adult is dropped"));
  CHECK_FALSE(g.pending());
  ending.now_us = 8000'000;
  CHECK_FALSE(g.tick(ending).ask_event);
  CHECK_FALSE(g.reloading());
}

// ORM, the Temple of Time (time_temple.hpp, oot-remake docs/TEMPLO_TIEMPO.md):
// while our game runs the sword's story no reload begins (our own cutscene
// changes our age), but a mate's change is still seen.
TEST_CASE("age: the Temple of Time's wait: a change is seen, no reload begins until it is over") {
  // We draw the sword with the mate: he is adult before our story is over,
  // and so are we once it is.
  AgeFollow f({}, kInPlace);
  auto in = calm(0, Age::child, mate(Age::child, "Market_Child"), "Market_Child");
  f.tick(in);
  in.now_us = 2500'000;
  f.tick(in);  // first seen: child
  in = calm(3000, Age::child, mate(Age::adult, "Market_Adult"), "Market_Child");
  in.wait = "the Temple of Time: our game runs 'Market_Child_DoorOfTime_Draw' (travel)";
  in.event_running = true;
  auto o = f.tick(in);
  CHECK(logged(o, "reloads wait: the Temple of Time: our game runs"));
  in.now_us = 5100'000;
  o = f.tick(in);
  CHECK(logged(o, "P2 is adult now (was child)"));  // seen while waiting
  CHECK(f.pending() == Age::adult);
  CHECK(o.log.size() == 2);  // the wait is said once
  // Calm, but the story's parts still follow one another: nothing begins.
  in.event_running = false;
  for (std::uint64_t ms = 5200; ms <= 9000; ms += 100) {
    in.now_us = ms * 1000;
    CHECK_FALSE(f.tick(in).ask_event);
  }
  CHECK_FALSE(f.reloading());
  // Over: our own cutscene made us adult.
  auto over = calm(9100, Age::adult, mate(Age::adult, "Market_Adult"), "Market_Adult");
  o = f.tick(over);
  CHECK(logged(o, "reloads may begin again"));
  CHECK(logged(o, "already adult: nothing to reload"));
  CHECK_FALSE(f.pending());
  over.now_us = 12000'000;
  CHECK_FALSE(f.tick(over).ask_event);

  // A blue warp's Chamber of Sages: a mate puts the sword back meanwhile; we
  // follow him once out of it.
  AgeFollow g({}, kInPlace);
  auto sage = calm(0, Age::adult, mate(Age::adult, "Market_Adult"), "SageRoom");
  sage.wait = "the Temple of Time: in SageRoom (no age in its name; its story takes Link out on its own)";
  g.tick(sage);
  sage.now_us = 2500'000;
  g.tick(sage);
  sage.mates = {mate(Age::child, "Market_Child")};
  sage.now_us = 3000'000;
  g.tick(sage);
  sage.now_us = 5100'000;
  o = g.tick(sage);
  CHECK(logged(o, "we follow: reload as child"));
  sage.now_us = 9000'000;
  CHECK_FALSE(g.tick(sage).ask_event);
  CHECK(g.pending() == Age::child);  // kept, not dropped as the ending's hold drops it
  auto meadow = calm(10000, Age::adult, mate(Age::child, "Market_Child"), "SacredForestMeadow_Adult");
  o = g.tick(meadow);
  CHECK(logged(o, "reloads may begin again"));
  CHECK_FALSE(o.ask_event);  // calm must last a second
  meadow.now_us = 11100'000;
  o = g.tick(meadow);
  REQUIRE(o.ask_event);
  CHECK(logged(o, "reloading as child: SacredForestMeadow_Adult -> SacredForestMeadow_Child"));
  // A reload under way runs to its end, a wait or not.
  meadow.now_us = 11300'000;
  meadow.event_running = true;
  meadow.wait = "the Temple of Time: 'Market_Adult_Field_AdultToChild' ran here a moment ago; its story may go on";
  o = g.tick(meadow);
  REQUIRE(o.ask_scene);
  CHECK(*o.ask_scene == "SacredForestMeadow_Child");
  CHECK(g.reloading());
}

// ORM, the Spirit Temple (gerudo.hpp age_reload_keeps_spot, docs/GERUDO_ESPIRITU.md):
// one scene for both ages; Link is not put back in the half he stood in.
TEST_CASE("age: in the Spirit Temple the reload leaves Link at the scene's start") {
  AgeFollow f({}, kInPlace);
  auto in = calm(0, Age::adult, mate(Age::adult, "Market_Adult"), "SpiritTemple");
  in.position = proto::Vec3{12.0f, 140.0f, -60.0f};  // the adult half
  f.tick(in);
  in.now_us = 2500'000;
  f.tick(in);
  in.mates = {mate(Age::child, "Market_Child")};
  in.now_us = 3000'000;
  f.tick(in);
  in.now_us = 5100'000;
  f.tick(in);
  REQUIRE(f.pending() == Age::child);
  in.now_us = 6200'000;
  auto o = f.tick(in);
  REQUIRE(o.ask_event);
  CHECK(logged(o, "reloading as child: SpiritTemple -> SpiritTemple"));
  in.now_us = 6400'000;
  in.event_running = true;
  o = f.tick(in);
  REQUIRE(o.ask_scene);
  CHECK(*o.ask_scene == "SpiritTemple");
  AgeInput load = in;
  load.now_us = 7000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  auto arrive = calm(15000, Age::child, mate(Age::child, "Market_Child"), "SpiritTemple");
  arrive.position = proto::Vec3{0.0f, 100.25f, 8.5f};  // the entrance, far from where he stood
  o = f.tick(arrive);
  CHECK(logged(o, "loading SpiritTemple"));
  arrive.now_us = 15100'000;
  o = f.tick(arrive);
  CHECK(logged(o, "now child in SpiritTemple"));
  CHECK(logged(o, "Link left at the scene's start"));
  CHECK(logged(o, "the entrance from the Desert Colossus (0.8 m from it)"));
  CHECK_FALSE(o.move_to);
  CHECK(o.cancel_scene);
  CHECK_FALSE(f.pending());
  CHECK_FALSE(f.reloading());
}

// docs/RIESGOS_RESUELTOS.md "Mundo e historia": the game's start after the reload is checked
// against the entrance from the Desert Colossus; elsewhere, Link is put there.
TEST_CASE("age: a Spirit Temple reload that leaves Link away from the Colossus entrance puts him there") {
  AgeFollow f({}, kInPlace);
  auto in = calm(0, Age::adult, mate(Age::adult, "Market_Adult"), "SpiritTemple");
  in.position = proto::Vec3{12.0f, 140.0f, -60.0f};
  f.tick(in);
  in.now_us = 2500'000;
  f.tick(in);
  in.mates = {mate(Age::child, "Market_Child")};
  in.now_us = 3000'000;
  f.tick(in);
  in.now_us = 5100'000;
  f.tick(in);
  REQUIRE(f.pending() == Age::child);
  in.now_us = 6200'000;
  REQUIRE(f.tick(in).ask_event);
  in.now_us = 6400'000;
  in.event_running = true;
  REQUIRE(f.tick(in).ask_scene);
  AgeInput load = in;
  load.now_us = 7000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  auto arrive = calm(15000, Age::child, mate(Age::child, "Market_Child"), "SpiritTemple");
  arrive.position = proto::Vec3{12.0f, 140.0f, -60.0f};  // the game kept him in the adult half
  f.tick(arrive);
  arrive.now_us = 15100'000;
  const auto o = f.tick(arrive);
  CHECK(logged(o, "now child in SpiritTemple"));
  CHECK(logged(o, "m from the entrance from the Desert Colossus (the game's start is another): put at the entrance"));
  REQUIRE(o.move_to);
  CHECK((*o.move_to)[0] == doctest::Approx(0.0f));
  CHECK((*o.move_to)[1] == doctest::Approx(100.25f));
  CHECK((*o.move_to)[2] == doctest::Approx(7.66f));
  CHECK_FALSE(f.pending());
}

TEST_CASE("age: our own reading while we load, or that the scene's era denies, is not our age") {
  // P2 turns adult while our game reads adult for a moment in KokiriForest_Child (a load): the
  // change is followed, never dropped as "already adult".
  AgeFollow f({}, kInPlace);
  f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  f.tick(calm(2500, Age::child, mate(Age::child, "Market_Child")));
  f.tick(calm(3000, Age::child, mate(Age::adult, "Market_Adult")));
  auto o = f.tick(calm(5100, Age::adult, mate(Age::adult, "Market_Adult")));  // the scene says child
  CHECK(logged(o, "we follow: reload as adult once nothing runs here (our own age is not read steadily yet"));
  CHECK(logged(o, "our age reads adult in KokiriForest_Child: not trusted"));
  CHECK_FALSE(logged(o, "already adult"));
  REQUIRE(f.pending() == Age::adult);
  // Loading: no scene, adult read: still not trusted, nothing asked.
  AgeInput load = calm(5300, Age::adult, mate(Age::adult, "Market_Adult"));
  load.scene.reset();
  CHECK_FALSE(f.tick(load).ask_event);
  REQUIRE(f.pending() == Age::adult);
  // Back in the scene as a child: the reload goes on as usual.
  f.tick(calm(5500, Age::child, mate(Age::adult, "Market_Adult")));
  o = f.tick(calm(6600, Age::child, mate(Age::adult, "Market_Adult")));
  REQUIRE(o.ask_event);
  CHECK(logged(o, "reloading as adult: KokiriForest_Child -> KokiriForest_Adult"));
}

TEST_CASE("age: in a scene with no era a flicker to the pending age drops nothing, a steady one does") {
  AgeFollow f({}, kInPlace);
  f.tick(calm(0, Age::child, mate(Age::child, "Market_Child"), "SpiritTemple"));
  f.tick(calm(2500, Age::child, mate(Age::child, "Market_Child"), "SpiritTemple"));
  f.tick(calm(3000, Age::child, mate(Age::adult, "Market_Adult"), "SpiritTemple"));
  auto busy = calm(5100, Age::child, mate(Age::adult, "Market_Adult"), "SpiritTemple");
  busy.event_running = true;  // a talk: the reload waits
  auto o = f.tick(busy);
  REQUIRE(f.pending() == Age::adult);
  // 100 ms of adult (the load's moment): not dropped.
  o = f.tick(calm(5200, Age::adult, mate(Age::adult, "Market_Adult"), "SpiritTemple"));
  CHECK_FALSE(logged(o, "already adult"));
  CHECK_FALSE(o.ask_event);
  REQUIRE(f.pending() == Age::adult);
  o = f.tick(calm(5300, Age::child, mate(Age::adult, "Market_Adult"), "SpiritTemple"));
  REQUIRE(f.pending() == Age::adult);
  // Adult for good (our own sword): dropped once it is steady.
  f.tick(calm(6000, Age::adult, mate(Age::adult, "Market_Adult"), "SpiritTemple"));
  o = f.tick(calm(6400, Age::adult, mate(Age::adult, "Market_Adult"), "SpiritTemple"));
  CHECK(logged(o, "already adult: nothing to reload"));
  CHECK_FALSE(f.pending());
}

// D-R62 (oot-64 D-178): a Link lying down reloads with the room and stays down
// (the downed machine holds him, DownedInput::hold_down); being revived or in
// the game over still waits.
TEST_CASE("age: lying down the reload begins all the same, and says it holds us down") {
  AgeFollow f({}, kInPlace);
  f.change(Age::adult, "test");
  auto down = calm(0, Age::child, mate(Age::child, "Market_Child"));
  down.downed = true;
  down.lying = true;
  f.tick(down);
  down.now_us = 1100'000;
  auto o = f.tick(down);
  REQUIRE(o.ask_event);
  CHECK(logged(o, "we lie down: the reload as the room's age all the same, down we stay (D-R62"));
  CHECK(f.reloading_downed());

  auto ev = down;
  ev.now_us = 1200'000;
  ev.event_running = true;
  REQUIRE(f.tick(ev).ask_scene);
  CHECK(f.reloading_downed());
  AgeInput load = ev;
  load.now_us = 2000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  auto arrive = calm(9000, Age::adult, mate(Age::child, "Market_Child"), "KokiriForest_Adult");
  arrive.downed = true;
  arrive.lying = true;
  arrive.position = proto::Vec3{0.0f, 120.0f, 0.0f};
  o = f.tick(arrive);
  CHECK(logged(o, "loading KokiriForest_Adult"));
  arrive.now_us = 9100'000;
  o = f.tick(arrive);
  CHECK(logged(o, "now adult in KokiriForest_Adult"));
  REQUIRE(o.move_to);  // the body back where he lay
  CHECK_FALSE(f.reloading());
  CHECK_FALSE(f.reloading_downed());

  // A reload begun standing is not one that holds anyone down.
  AgeFollow g({}, kInPlace);
  g.change(Age::adult, "test");
  g.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  REQUIRE(g.tick(calm(1100, Age::child, mate(Age::child, "Market_Child"))).ask_event);
  CHECK(g.reloading());
  CHECK_FALSE(g.reloading_downed());

  // Being revived (down, not lying): it waits, as before.
  AgeFollow h({}, kInPlace);
  h.change(Age::adult, "test");
  auto reviving = calm(0, Age::child, mate(Age::child, "Market_Child"));
  reviving.downed = true;
  for (std::uint64_t t = 0; t < 5000; t += 100) {
    reviving.now_us = t * 1000;
    CHECK_FALSE(h.tick(reviving).ask_event);
  }
}

TEST_CASE("age: two reloads that did not take while down: the revive first, then the reload") {
  AgeFollow f({}, kInPlace);
  f.change(Age::adult, "test");
  auto down = calm(0, Age::child, mate(Age::child, "Market_Child"));
  down.downed = true;
  down.lying = true;
  int asks = 0;
  bool waits = false;
  // No event of ours ever starts for a dead Link (the case to be ready for).
  for (std::uint64_t t = 0; t <= 60000; t += 100) {
    down.now_us = t * 1000;
    const auto o = f.tick(down);
    if (o.ask_event && logged(o, "reloading as adult")) ++asks;
    waits = waits || logged(o, "2 reloads as adult while we lay down did not take: it waits until a mate revives us");
  }
  CHECK(asks == 2);
  CHECK(waits);
  CHECK_FALSE(f.reloading());
  REQUIRE(f.pending() == Age::adult);
  // Revived: on our feet, the reload begins after its second of calm.
  auto up = calm(60100, Age::child, mate(Age::child, "Market_Child"));
  CHECK_FALSE(f.tick(up).ask_event);
  up.now_us = 61200'000;
  const auto o = f.tick(up);
  REQUIRE(o.ask_event);
  CHECK_FALSE(f.reloading_downed());
}

// ---- The game's own era change (no event named; oot-remake docs/EDAD.md) ----
// ReplaceEquipmentForEraChange has no RSDB EventSetting row (a sub flow): the
// game refused it by name in every edad run. The pedestal's own events have one.

namespace {

// From the request to the game's era change running, for a follower in Kokiri.
AgeOutput begin_era_change(AgeFollow& f, Age to) {
  const Age from = to == Age::adult ? Age::child : Age::adult;
  const std::string scene = to == Age::adult ? "KokiriForest_Child" : "KokiriForest_Adult";
  f.change(to, "test");
  // Calm from 0.1 s: the countdown 3, 2, 1 (2026-11-13), the move 3 s later.
  f.tick(calm(100, from, mate(from, "Market_Child"), scene));
  f.tick(calm(1100, from, mate(from, "Market_Child"), scene));
  f.tick(calm(2100, from, mate(from, "Market_Child"), scene));
  return f.tick(calm(3100, from, mate(from, "Market_Child"), scene));
}

}  // namespace

TEST_CASE("age: the game's era change by default: the pedestal's event, nothing written by us") {
  CHECK(std::string(era_change_event(Age::adult)) == "Market_Child_Field_ChildToAdult");
  CHECK(std::string(era_change_event(Age::child)) == "Market_Adult_Field_AdultToChild");
  CHECK(std::string(era_change_scene(Age::adult)) == "Market_Adult");
  CHECK(std::string(era_change_scene(Age::child)) == "Market_Child");

  AgeFollow f;
  CHECK(f.game_era_event());
  auto o = begin_era_change(f, Age::adult);
  REQUIRE(o.ask_event);
  CHECK(*o.ask_event == "Market_Child_Field_ChildToAdult");
  CHECK_FALSE(o.set_era);    // the game's flow sets the era, after its equipment swap
  CHECK_FALSE(o.ask_scene);  // and reserves its own scene
  CHECK(logged(o, "reloading as adult: KokiriForest_Child -> Market_Adult"));
  CHECK(logged(o, "the game's own era change, as at the pedestal"));

  auto ev = calm(3300, Age::child, mate(Age::child, "Market_Child"));
  ev.event_running = true;
  o = f.tick(ev);
  CHECK(logged(o, "the game's era change to Market_Adult runs"));
  CHECK_FALSE(o.set_era);
  CHECK_FALSE(o.ask_scene);
  // Its hand-over to the cutscene: a moment with no event is not its end.
  auto gap = calm(7000, Age::child, mate(Age::child, "Market_Child"));
  o = f.tick(gap);
  CHECK_FALSE(logged(o, "did not take"));
  ev.now_us = 7500'000;
  f.tick(ev);
  for (std::uint64_t t = 8000; t < 32000; t += 500) {  // its cutscene, Dm_TimeTemple_0080
    ev.now_us = t * 1000;
    o = f.tick(ev);
    CHECK_FALSE(o.ask_scene);
    CHECK_FALSE(logged(o, "did not take"));  // 45 s is not the limit here
  }
  AgeInput load = ev;
  load.now_us = 52000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  // The Temple of Time, adult, the arrival's own cutscene first.
  auto arrive = calm(62000, Age::adult, mate(Age::child, "Market_Child"), "Market_Adult");
  arrive.position = proto::Vec3{137.2f, 106.1f, -34.0f};
  arrive.event_running = true;
  o = f.tick(arrive);
  CHECK(logged(o, "loading Market_Adult"));
  arrive.now_us = 72000'000;
  arrive.event_running = false;
  o = f.tick(arrive);
  CHECK(logged(o, "now adult in Market_Adult, 68.9 s after the reload began: the game's own era change"));
  CHECK_FALSE(o.move_to);       // Kokiri's place means nothing in the temple
  CHECK_FALSE(o.cancel_scene);  // nothing of ours to clear
  CHECK_FALSE(o.set_era);
  CHECK_FALSE(f.pending());
  CHECK_FALSE(f.reloading());
}

TEST_CASE("age: the game's era change back to child asks the sword's way out") {
  AgeFollow f;
  const auto o = begin_era_change(f, Age::child);
  REQUIRE(o.ask_event);
  CHECK(*o.ask_event == "Market_Adult_Field_AdultToChild");
  CHECK(logged(o, "reloading as child: KokiriForest_Adult -> Market_Child"));
}

TEST_CASE("age: the game's era change refused: tried again, the era never touched") {
  AgeFollow f;
  auto o = begin_era_change(f, Age::adult);
  REQUIRE(o.ask_event);
  bool gave_up = false;
  for (std::uint64_t t = 3200; t <= 10000; t += 100) {
    o = f.tick(calm(t, Age::child, mate(Age::child, "Market_Child")));
    CHECK_FALSE(o.set_era);
    CHECK_FALSE(o.cancel_scene);  // the game's own reserve is never cleared by us
    if (o.ask_event) CHECK(*o.ask_event == "Market_Child_Field_ChildToAdult");
    gave_up = gave_up || logged(o, "no event of ours started (3 tries of 'Market_Child_Field_ChildToAdult')");
  }
  CHECK(gave_up);
  CHECK(f.pending() == Age::adult);
}

TEST_CASE("age: the game's era change ends with no load: the era goes back to our Link's") {
  AgeFollow f;
  REQUIRE(begin_era_change(f, Age::adult).ask_event);
  auto ev = calm(3300, Age::child, mate(Age::child, "Market_Child"));
  ev.event_running = true;
  f.tick(ev);
  auto off = calm(3500, Age::child, mate(Age::child, "Market_Child"));
  auto o = f.tick(off);
  CHECK_FALSE(logged(o, "did not take"));
  off.now_us = 4600'000;
  o = f.tick(off);
  CHECK(logged(o, "the event ended with no scene change"));
  CHECK(o.set_era == Age::child);  // the flow may have set it adult before its load
  CHECK(logged(o, "Sequence_IsChildhood put back to child"));
  CHECK_FALSE(o.cancel_scene);

  // Still running at the limit: the era is left to the game's event.
  AgeFollow g;
  REQUIRE(begin_era_change(g, Age::adult).ask_event);
  ev.now_us = 3300'000;
  g.tick(ev);
  ev.now_us = 124000'000;
  o = g.tick(ev);
  CHECK(logged(o, "no scene change in time"));
  CHECK_FALSE(o.set_era);
  CHECK(logged(o, "Sequence_IsChildhood left to 'Market_Child_Field_ChildToAdult', still running"));
}

// ---- What the players see (2026-11-13, oot-remake docs/EDAD.md) ----
// One player changes age at the Temple of Time; everyone else follows through
// the game's own era change, told before the move with a countdown, never
// moved in a cutscene, a boss room's lock or down; the group gathered, or the
// move given up with the era left as Link's.

namespace {

AgeMate named(std::uint8_t n, Age age, std::string scene, std::string name, std::uint8_t status = 0) {
  AgeMate m{n, age, std::move(scene), status};
  m.name = std::move(name);
  return m;
}

// Our game in Kokiri as a child with these mates.
AgeInput group(std::uint64_t ms, Age ours, std::vector<AgeMate> mates, std::string scene = "KokiriForest_Child") {
  AgeInput in;
  in.now_us = ms * 1000;
  in.age = ours;
  in.scene = std::move(scene);
  in.position = proto::Vec3{-81.9f, 122.3f, 33.7f};
  in.event_running = false;
  in.mates = std::move(mates);
  return in;
}

using Kind = AgeNotice::Kind;

std::vector<AgeNotice> notices_of(const AgeOutput& o, Kind kind) {
  std::vector<AgeNotice> out;
  for (const auto& n : o.notices) {
    if (n.kind == kind) out.push_back(n);
  }
  return out;
}

const std::string kAnaMoves = "Ana ha cambiado de época: moviendo al grupo al Templo del Tiempo...";

// Ana (P2) and Bo (P3) children with us; Ana's change to adult counts at 5100.
AgeFollow& ana_turns_adult(AgeFollow& f, AgeOutput* at_5100 = nullptr, bool busy = false) {
  f.tick(group(0, Age::child, {named(2, Age::child, "Market_Child", "Ana"), named(3, Age::child, "LakeHylia_Child", "Bo")}));
  f.tick(group(2500, Age::child, {named(2, Age::child, "Market_Child", "Ana"), named(3, Age::child, "LakeHylia_Child", "Bo")}));
  f.tick(group(3000, Age::child, {named(2, Age::adult, "Market_Adult", "Ana"), named(3, Age::child, "LakeHylia_Child", "Bo")}));
  auto in = group(5100, Age::child, {named(2, Age::adult, "Market_Adult", "Ana"), named(3, Age::child, "LakeHylia_Child", "Bo")});
  in.event_running = busy;
  auto o = f.tick(in);
  if (at_5100) *at_5100 = o;
  return f;
}

std::vector<AgeMate> ana_adult_bo(Age bo, std::uint8_t ana_status = 0, std::uint8_t bo_status = 0) {
  return {named(2, Age::adult, "Market_Adult", "Ana", ana_status),
          named(3, bo, bo == Age::adult ? "Market_Adult" : "LakeHylia_Child", "Bo", bo_status)};
}

}  // namespace

TEST_CASE("age group: a mate's change is told first, then 3, 2, 1, then the move, gathered on arrival") {
  AgeFollow f;
  AgeOutput o;
  ana_turns_adult(f, &o);
  REQUIRE(f.pending() == Age::adult);
  // Told at once, in order: the change, then the countdown's 3.
  REQUIRE(o.notices.size() == 2);
  CHECK(o.notices[0].kind == Kind::announce);
  CHECK(o.notices[0].text == kAnaMoves);
  CHECK(o.notices[0].name == "Ana");
  CHECK(o.notices[0].number == 2);
  CHECK(o.notices[0].detail.empty());
  CHECK(o.notices[0].chat);  // the chat keeps a line of it
  CHECK(o.notices[1].kind == Kind::countdown);
  CHECK(o.notices[1].text == kAnaMoves);
  CHECK(o.notices[1].detail == "3");
  CHECK_FALSE(o.notices[1].chat);
  CHECK_FALSE(o.ask_event);
  CHECK(logged(o, "countdown 3 before the move to the Temple of Time"));

  std::vector<std::string> counted;
  std::uint64_t moved_at = 0;
  for (std::uint64_t t = 5200; t <= 9000 && moved_at == 0; t += 100) {
    o = f.tick(group(t, Age::child, ana_adult_bo(Age::child)));
    for (const auto& n : notices_of(o, Kind::countdown)) counted.push_back(n.detail);
    CHECK(notices_of(o, Kind::announce).empty());  // told once
    if (o.ask_event) {
      moved_at = t;
      CHECK(*o.ask_event == "Market_Child_Field_ChildToAdult");
    }
  }
  CHECK(counted == std::vector<std::string>{"2", "1"});
  CHECK(moved_at == 8100);  // 3 s after the calm began, never before
  CHECK_FALSE(f.waits_unsafe());

  // The game's era change runs, loads the Temple of Time; Bo is still a child far away.
  auto ev = group(8300, Age::child, ana_adult_bo(Age::child));
  ev.event_running = true;
  f.tick(ev);
  AgeInput load = ev;
  load.now_us = 30000'000;
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  auto arrive = group(40000, Age::adult, ana_adult_bo(Age::child), "Market_Adult");
  arrive.event_running = true;
  f.tick(arrive);
  arrive.now_us = 45000'000;
  arrive.event_running = false;
  o = f.tick(arrive);
  CHECK(logged(o, "now adult in Market_Adult"));
  auto here = notices_of(o, Kind::arrived);
  REQUIRE(here.size() == 1);
  CHECK(here[0].text == "En el Templo del Tiempo");
  CHECK(here[0].detail == "Esperando al resto del grupo...");
  CHECK(notices_of(o, Kind::gathered).empty());
  CHECK(notices_of(o, Kind::moving_you).empty());  // our own reload is no change of ours to announce

  // Bo arrives: the group is gathered.
  f.tick(group(50000, Age::adult, ana_adult_bo(Age::adult), "Market_Adult"));
  o = f.tick(group(52100, Age::adult, ana_adult_bo(Age::adult), "Market_Adult"));
  auto all = notices_of(o, Kind::gathered);
  REQUIRE(all.size() == 1);
  CHECK(all[0].text == "Grupo reunido en el Templo del Tiempo");
  CHECK(all[0].chat);
  o = f.tick(group(60000, Age::adult, ana_adult_bo(Age::adult), "Market_Adult"));
  CHECK(o.notices.empty());  // once
}

TEST_CASE("age group: the move waits in a cutscene, a boss room or down, says why, then counts again") {
  AgeFollow f;
  AgeOutput o;
  ana_turns_adult(f, &o, /*busy=*/true);  // a cutscene of ours as the change counts
  REQUIRE(f.pending() == Age::adult);
  CHECK(o.notices.empty());  // a moment of event is not yet a reason (calm_us)
  CHECK_FALSE(f.waits_unsafe());

  auto in = group(6200, Age::child, ana_adult_bo(Age::child));
  in.event_running = true;
  o = f.tick(in);
  CHECK(f.waits_unsafe());
  CHECK(logged(o, "the move as adult to the Temple of Time waits: an event or cutscene runs here"));
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].kind == Kind::announce);
  CHECK(o.notices[0].text == kAnaMoves);  // told before anything, with when
  CHECK(o.notices[0].detail == "Te moverás cuando termine la escena");
  CHECK(o.notices[0].chat);
  for (std::uint64_t t = 6300; t < 20000; t += 100) {
    in.now_us = t * 1000;
    o = f.tick(in);
    CHECK_FALSE(o.ask_event);
    CHECK(o.notices.empty());  // said once while the reason lasts
    CHECK(f.waits_unsafe());
  }

  // A boss room's lock: another reason, said again (no second chat line).
  in.event_running = false;
  in.boss_fight = true;
  for (std::uint64_t t = 20000; t < 30000; t += 100) {
    in.now_us = t * 1000;
    o = f.tick(in);
    CHECK_FALSE(o.ask_event);
    if (t == 20000) {
      CHECK(logged(o, "waits: a boss room shuts us in"));
      REQUIRE(o.notices.size() == 1);
      CHECK(o.notices[0].detail == "Te moverás cuando acabe el combate");
      CHECK_FALSE(o.notices[0].chat);
    }
  }

  // Down, even lying (unlike the in-place reload of D-R62): never moved.
  in.boss_fight = false;
  in.downed = true;
  in.lying = true;
  for (std::uint64_t t = 30000; t < 40000; t += 100) {
    in.now_us = t * 1000;
    o = f.tick(in);
    CHECK_FALSE(o.ask_event);
    CHECK_FALSE(f.reloading_downed());
    if (t == 30000) {
      CHECK(logged(o, "waits: our Link is down"));
      REQUIRE(o.notices.size() == 1);
      CHECK(o.notices[0].detail == "Te moverás cuando te reanimen");
    }
  }
  CHECK(f.waits_unsafe());

  // Revived: the countdown again from 3; a flicker of event starts it over.
  in.downed = in.lying = false;
  in.now_us = 40000'000;
  o = f.tick(in);
  CHECK_FALSE(f.waits_unsafe());
  CHECK(logged(o, "the move as adult may go on: 'our Link is down' is over"));
  REQUIRE(notices_of(o, Kind::countdown).size() == 1);
  CHECK(notices_of(o, Kind::countdown)[0].detail == "3");
  in.now_us = 41000'000;
  CHECK(notices_of(f.tick(in), Kind::countdown)[0].detail == "2");
  in.now_us = 41500'000;
  in.event_running = true;  // half a second: no reason said, the count stops
  o = f.tick(in);
  CHECK(o.notices.empty());
  CHECK_FALSE(o.ask_event);
  in.now_us = 41900'000;
  in.event_running = false;
  o = f.tick(in);
  REQUIRE(notices_of(o, Kind::countdown).size() == 1);
  CHECK(notices_of(o, Kind::countdown)[0].detail == "3");  // from the top
  std::uint64_t moved_at = 0;
  for (std::uint64_t t = 42000; t <= 46000 && moved_at == 0; t += 100) {
    in.now_us = t * 1000;
    if (f.tick(in).ask_event) moved_at = t;
  }
  CHECK(moved_at == 44900);
}

TEST_CASE("age group: the others name a mate whose move waits, again every 15 s, with why") {
  AgeFollow f;
  // We are adult in the temple already; Ana is a child far away.
  auto in = group(0, Age::adult, {named(2, Age::child, "LakeHylia_Child", "Ana")}, "Market_Adult");
  f.tick(in);
  in.now_us = 2500'000;
  f.tick(in);
  in.now_us = 3000'000;
  in.mates[0].status = proto::orm_status::age_waits | proto::status::in_cutscene;
  auto o = f.tick(in);
  CHECK(logged(o, "P2 Ana says his move to the room's age waits (an event or cutscene runs there)"));
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].kind == Kind::waiting_mate);
  CHECK(o.notices[0].text == "Esperando a Ana para cambiar de época...");
  CHECK(o.notices[0].name == "Ana");
  CHECK(o.notices[0].number == 2);
  CHECK(o.notices[0].detail == "Está en una escena");
  CHECK(o.notices[0].chat);
  in.now_us = 10000'000;
  CHECK(f.tick(in).notices.empty());
  in.now_us = 18000'000;
  in.mates[0].status = proto::orm_status::age_waits | proto::status::dead;  // now down
  o = f.tick(in);
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].detail == "Está caído");
  CHECK_FALSE(o.notices[0].chat);  // the chat has it once
  in.now_us = 20000'000;
  in.mates[0].status = 0;
  o = f.tick(in);
  CHECK(logged(o, "P2 Ana no longer waits to change age"));
  CHECK(o.notices.empty());
  CHECK_FALSE(f.pending());  // her age is not the room's: nothing of ours to do

  // A mate already our age who says he waits is no one to wait for here; a nameless one is "Jugador N".
  AgeFollow g;
  auto same = group(0, Age::adult, {named(4, Age::adult, "Market_Adult", "")}, "Market_Adult");
  g.tick(same);
  same.now_us = 2500'000;
  g.tick(same);
  same.now_us = 3000'000;
  same.mates[0].status = proto::orm_status::age_waits;
  o = g.tick(same);
  CHECK(logged(o, "P4 Jugador 4 says his move"));
  CHECK(o.notices.empty());
}

TEST_CASE("age group: whoever drew the sword is told 'Moviendo al grupo contigo...', then the group gathered") {
  AgeFollow f;
  const auto ana = [](Age a) {
    return std::vector<AgeMate>{named(2, a, a == Age::adult ? "Market_Adult" : "KokiriForest_Child", "Ana")};
  };
  auto o = f.tick(group(0, Age::child, ana(Age::child), "Market_Child"));
  CHECK(o.notices.empty());  // our first age is no change
  f.tick(group(2500, Age::child, ana(Age::child), "Market_Child"));
  // Our own sword: the cutscene, the load, then adult in the temple.
  auto load = group(10000, Age::child, ana(Age::child), "Market_Child");
  load.scene.reset();
  load.age.reset();
  load.event_running.reset();
  f.tick(load);
  auto back = group(20000, Age::adult, ana(Age::child), "Market_Adult");
  back.event_running = true;  // Sheik, the arrival's cutscene
  o = f.tick(back);
  CHECK(logged(o, "we changed age ourselves (child -> adult): the group follows us"));
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].kind == Kind::moving_you);
  CHECK(o.notices[0].text == "Moviendo al grupo contigo...");
  CHECK(o.notices[0].chat);
  CHECK_FALSE(o.ask_event);
  CHECK_FALSE(f.pending());
  back.now_us = 21000'000;
  CHECK(f.tick(back).notices.empty());  // once

  // Ana follows (her move, the game's era change), then reads adult.
  f.tick(group(60000, Age::adult, ana(Age::adult), "Market_Adult"));
  o = f.tick(group(62100, Age::adult, ana(Age::adult), "Market_Adult"));
  REQUIRE(notices_of(o, Kind::gathered).size() == 1);
  CHECK(notices_of(o, Kind::gathered)[0].text == "Grupo reunido en el Templo del Tiempo");

  // In the temple with the one who drew: the sword's story on his word is his change, not ours.
  AgeFollow g;
  g.tick(group(0, Age::child, ana(Age::child), "Market_Child"));
  auto asked = group(5000, Age::child, ana(Age::child), "Market_Child");
  asked.story_follow = true;
  asked.event_running = true;
  g.tick(asked);
  o = g.tick(group(30000, Age::adult, ana(Age::child), "Market_Adult"));
  CHECK(logged(o, "by the sword's story asked on a mate's word"));
  CHECK(notices_of(o, Kind::moving_you).empty());

  // Alone in the room: nobody to move, nothing said.
  AgeFollow h;
  h.tick(group(0, Age::child, {}, "Market_Child"));
  o = h.tick(group(30000, Age::adult, {}, "Market_Adult"));
  CHECK(o.notices.empty());
}

TEST_CASE("age group: the game's era change that does not take twice: no move, Link's era, the notice") {
  AgeFollow f;
  REQUIRE(begin_era_change(f, Age::adult).ask_event);
  std::vector<AgeNotice> failed;
  std::uint64_t first_give_up = 0, asks_after = 0;
  for (std::uint64_t t = 3200; t <= 60000; t += 100) {
    auto o = f.tick(calm(t, Age::child, mate(Age::child, "Market_Child")));
    CHECK_FALSE(o.set_era);  // the event never ran: the era was never touched
    if (first_give_up == 0 && logged(o, "tried again in 10 s")) first_give_up = t;
    for (const auto& n : notices_of(o, Kind::failed)) failed.push_back(n);
    if (!failed.empty() && o.ask_event) ++asks_after;
  }
  CHECK(first_give_up != 0);
  REQUIRE(failed.size() == 1);
  CHECK(failed[0].text == "No se pudo cambiar de época: inténtalo en el pedestal");
  CHECK(failed[0].chat);
  CHECK(asks_after == 0);
  CHECK_FALSE(f.pending());  // no move: we stay a child where we are
  CHECK_FALSE(f.reloading());

  // After an event that ran: the era is put back to our Link's age before the notice.
  AgeSettings one;
  one.era_fail_limit = 1;
  AgeFollow g(one);
  REQUIRE(begin_era_change(g, Age::adult).ask_event);
  auto ev = calm(3300, Age::child, mate(Age::child, "Market_Child"));
  ev.event_running = true;
  g.tick(ev);
  auto o = g.tick(calm(3500, Age::child, mate(Age::child, "Market_Child")));
  o = g.tick(calm(4600, Age::child, mate(Age::child, "Market_Child")));
  CHECK(o.set_era == Age::child);
  CHECK(logged(o, "1 in a row: given up, no move (the era stays our Link's"));
  REQUIRE(notices_of(o, Kind::failed).size() == 1);
  CHECK_FALSE(g.pending());
}

TEST_CASE("age group: the in-place reload (an event named) keeps its old timing and says nothing") {
  AgeFollow f({}, kInPlace);
  f.change(Age::adult, "test");
  f.tick(calm(0, Age::child, mate(Age::child, "Market_Child")));
  const auto o = f.tick(calm(1100, Age::child, mate(Age::child, "Market_Child")));
  REQUIRE(o.ask_event);
  CHECK(o.notices.empty());
  CHECK(std::string(notice_kind_name(Kind::waiting_mate)) == "waiting_mate");
}
