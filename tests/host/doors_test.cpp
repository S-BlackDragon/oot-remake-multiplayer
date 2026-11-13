// ORM (oot-remake docs/PUERTAS.md): coop/host/doors.hpp.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <optional>
#include <string>
#include <vector>

#include "coop/host/doors.hpp"

using coop::host::DoorActor;
using coop::host::DoorKind;
using coop::host::DoorMirror;
using coop::host::DoorMove;
using coop::host::DoorMover;
using coop::host::DoorWish;
using coop::host::door_kind_of;
using coop::host::door_move_of;
using coop::host::door_play;
using coop::host::door_shows_open;
using coop::protocol::Vec3;

namespace {

DoorMover Mover(std::uint8_t number, Vec3 at, std::string command, bool adult = false) {
  DoorMover m;
  m.number = number;
  m.position = at;
  m.command = std::move(command);
  m.adult = adult;
  return m;
}

DoorActor Actor(std::uint64_t id, std::string name, Vec3 at, std::string command = "Wait") {
  DoorActor a;
  a.id = id;
  a.name = std::move(name);
  a.position = at;
  a.command = std::move(command);
  return a;
}

std::vector<DoorWish> Plays(const std::vector<DoorWish>& wishes) {
  std::vector<DoorWish> out;
  for (const auto& w : wishes) {
    if (w.actor != 0) out.push_back(w);
  }
  return out;
}

bool Says(const std::vector<DoorWish>& wishes, const std::string& part) {
  for (const auto& w : wishes) {
    if (w.line.find(part) != std::string::npos) return true;
  }
  return false;
}

// A Kakariko house door (KakarikoVillage_Child_Static) and P2 in front of it.
const Vec3 kDoor{-52.6f, 97.9f, 34.5f};
const Vec3 kInFront{-52.6f, 97.9f, 35.4f};
const Vec3 kFar{-80.0f, 97.0f, 50.0f};

}  // namespace

TEST_CASE("doors: the player's door and chest commands, from Player_Child/_Adult's AS") {
  CHECK(door_move_of("DoorPushOpenL") == DoorMove::push_left);
  CHECK(door_move_of("DoorPushOpenL_30cm_R45_Ang90_FootL_Cba") == DoorMove::push_left);
  CHECK(door_move_of("DoorPushOpenR_0cm_F_Ang0_FootR_Cba") == DoorMove::push_right);
  CHECK(door_move_of("BigDoorPushOpenL") == DoorMove::big_left);
  CHECK(door_move_of("BigDoorPushOpenR") == DoorMove::big_right);
  CHECK(door_move_of("EvtDoorShutter") == DoorMove::shutter);
  CHECK(door_move_of("Evt_Door_Key") == DoorMove::shutter);
  CHECK(door_move_of("EvtDoorBoss") == DoorMove::shutter);
  CHECK(door_move_of("EvtDoorJabu") == DoorMove::shutter);
  CHECK(door_move_of("EvtDoorFailure") == DoorMove::none);  // a locked door: nothing opens
  CHECK(door_move_of("DoorPushTrick") == DoorMove::none);   // the trick door enemy
  CHECK(door_move_of("Door_Trick_L") == DoorMove::none);
  CHECK(door_move_of("DoorPush") == DoorMove::none);        // no side
  CHECK(door_move_of("Demo_Get_Kick_TreasureBox_Child_StepFront") == DoorMove::chest_kick);
  CHECK(door_move_of("DemoOpenTreasureBoxWithKick") == DoorMove::chest_kick);
  CHECK(door_move_of("Demo_Front_OpenTreasureBox_Adult_Ang90") == DoorMove::chest_front);
  CHECK(door_move_of("DemoOpenTreasureBoxFront") == DoorMove::chest_front);
  CHECK(door_move_of("DemoOpenTreasureBox") == DoorMove::chest_front);
  CHECK(door_move_of("DemoOpenBoxWithKick_Mini") == DoorMove::chest_small);
  CHECK(door_move_of("DemoOpenBoxFront_Mini") == DoorMove::chest_small);
  CHECK(door_move_of("Wait") == DoorMove::none);
  CHECK(door_move_of("") == DoorMove::none);
}

TEST_CASE("doors: the door and chest actors, from the romfs packs' AS") {
  CHECK(door_kind_of("Obj_Kakariko_Door_A_01") == DoorKind::hinge);
  CHECK(door_kind_of("Obj_Kokiri_Door_A_03") == DoorKind::hinge);
  CHECK(door_kind_of("Obj_Gerudo_SwitchShutter_A_08") == DoorKind::hinge);
  CHECK(door_kind_of("Obj_Base_Door_A_02") == DoorKind::dungeon);
  CHECK(door_kind_of("Obj_FireTemple_Door_A_01") == DoorKind::dungeon);
  CHECK(door_kind_of("Obj_SwitchShutter_A_01") == DoorKind::shutter);
  CHECK(door_kind_of("Obj_ForestTemple_SwitchShutter_A_04") == DoorKind::shutter);
  CHECK(door_kind_of("Obj_InsideTheDekuTree_SwitchShutter_A_01") == DoorKind::none);  // no AS
  CHECK(door_kind_of("Obj_Door_Hinge_A_01") == DoorKind::none);  // the hinge beside a door
  CHECK(door_kind_of("Obj_TBox_A_01") == DoorKind::chest_small);
  CHECK(door_kind_of("Obj_TBox_A_02_NoBake") == DoorKind::chest_big);
  CHECK(door_kind_of("Obj_HrlCity_Shop_TBox_A_01") == DoorKind::none);  // the shop's minigame
  CHECK(door_kind_of("Obj_Pot_A_08") == DoorKind::none);
}

TEST_CASE("doors: what each kind plays for each move") {
  auto p = door_play(DoorKind::hinge, DoorMove::push_left, false);
  REQUIRE(p);
  CHECK(p->open == "Push_L_ByPlayer");
  CHECK(p->close == "Close_L_ByPlayer");
  p = door_play(DoorKind::hinge, DoorMove::big_right, false);
  REQUIRE(p);
  CHECK(p->open == "Push_R_ByPlayer");
  p = door_play(DoorKind::dungeon, DoorMove::push_right, false);
  REQUIRE(p);
  CHECK(p->open == "Push_R");
  CHECK(p->close == "Close_R");
  p = door_play(DoorKind::shutter, DoorMove::shutter, false);
  REQUIRE(p);
  CHECK(p->open == "ToOpen");
  CHECK(p->close == "ToClose");
  CHECK_FALSE(door_play(DoorKind::shutter, DoorMove::push_left, false));
  CHECK_FALSE(door_play(DoorKind::hinge, DoorMove::shutter, false));
  CHECK_FALSE(door_play(DoorKind::hinge, DoorMove::chest_kick, false));
  p = door_play(DoorKind::chest_big, DoorMove::chest_kick, true);
  REQUIRE(p);
  CHECK(p->open == "FrontOpen_Adult");  // ORM cofre: the AS command, not its animation
  CHECK(p->close.empty());
  p = door_play(DoorKind::chest_big, DoorMove::chest_front, false);
  REQUIRE(p);
  CHECK(p->open == "FrontOpen");
  CHECK_FALSE(door_play(DoorKind::chest_big, DoorMove::chest_small, false));
  p = door_play(DoorKind::chest_small, DoorMove::chest_front, true);
  REQUIRE(p);
  CHECK(p->open == "FrontOpenSmall");
  CHECK(door_kind_of("Obj_TBox_GlassTruth_A_01") == DoorKind::chest_small);  // Obj_TBox_A_01's AS
  CHECK_FALSE(door_play(DoorKind::none, DoorMove::push_left, false));

  CHECK(door_shows_open(DoorKind::hinge, "Push_L_ByPlayer"));
  CHECK_FALSE(door_shows_open(DoorKind::hinge, "Close_L_ByPlayer"));
  CHECK_FALSE(door_shows_open(DoorKind::hinge, "Wait"));
  CHECK(door_shows_open(DoorKind::shutter, "Open"));
  CHECK(door_shows_open(DoorKind::shutter, "ToOpenWithChain"));
  CHECK_FALSE(door_shows_open(DoorKind::shutter, "Close"));
  CHECK(door_shows_open(DoorKind::chest_big, "OpenWait"));
  CHECK_FALSE(door_shows_open(DoorKind::chest_big, "CloseWait"));
  CHECK_FALSE(door_shows_open(DoorKind::chest_small, ""));
}

TEST_CASE("doors: a partner's push opens our door, and it closes once he is through") {
  DoorMirror m;
  const std::vector<DoorActor> door{Actor(0x1000, "Obj_Kakariko_Door_A_01", kDoor)};
  m.update(kFar, {Mover(2, kInFront, "Wait")}, 0);
  CHECK_FALSE(m.wants_actors(0));
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL_30cm_F_Ang0_FootL_Cba")}, 100);
  CHECK(m.wants_actors(100));
  CHECK(m.worth_reading(kDoor, 0x1000));
  CHECK_FALSE(m.worth_reading(kFar, 0x2000));
  auto wishes = m.decide(door, 100);
  auto plays = Plays(wishes);
  REQUIRE(plays.size() == 1);
  CHECK(plays[0].actor == 0x1000);
  CHECK(plays[0].command == "Push_L_ByPlayer");
  CHECK(Says(wishes, "P2 'DoorPushOpenL_30cm_F_Ang0_FootL_Cba': Obj_Kakariko_Door_A_01 opens here"));
  CHECK_FALSE(m.wants_actors(100));
  // A variant of the same push is not a new one.
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 200);
  CHECK_FALSE(m.wants_actors(200));
  // Through: his command moves on; 300 ms later the door closes, if it
  // still shows our push.
  m.update(kFar, {Mover(2, {-52.6f, 97.9f, 33.0f}, "Walk")}, 1500);
  CHECK_FALSE(m.wants_actors(1799));
  m.update(kFar, {Mover(2, {-52.6f, 97.9f, 32.0f}, "Walk")}, 1800);
  CHECK(m.wants_actors(1800));
  wishes = m.decide({Actor(0x1000, "Obj_Kakariko_Door_A_01", kDoor, "Push_L_ByPlayer")}, 1800);
  plays = Plays(wishes);
  REQUIRE(plays.size() == 1);
  CHECK(plays[0].command == "Close_L_ByPlayer");
  CHECK(Says(wishes, "P2 is through Obj_Kakariko_Door_A_01: closing it (Close_L_ByPlayer)"));
  CHECK(m.opened() == 0);
}

TEST_CASE("doors: a door the game closed itself is left as it is") {
  DoorMirror m;
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenR")}, 0);
  REQUIRE(Plays(m.decide({Actor(7, "Obj_Kakariko_Door_A_01", kDoor)}, 0)).size() == 1);
  m.update(kFar, {Mover(2, kInFront, "Wait")}, 500);
  m.update(kFar, {Mover(2, kInFront, "Wait")}, 800);
  const auto wishes = m.decide({Actor(7, "Obj_Kakariko_Door_A_01", kDoor, "Close_R_ByPlayer")}, 800);
  CHECK(Plays(wishes).empty());
  CHECK(Says(wishes, "left as the game has it ('Close_R_ByPlayer')"));
}

TEST_CASE("doors: never beside our Link, never one open already, a line when none is near") {
  DoorMirror m;
  // Our Link 1 m from the door: our player may be using it.
  m.update(Vec3{-52.6f, 97.9f, 33.5f}, {Mover(2, kInFront, "DoorPushOpenL")}, 0);
  auto wishes = m.decide({Actor(1, "Obj_Kakariko_Door_A_01", kDoor)}, 0);
  CHECK(Plays(wishes).empty());
  CHECK(Says(wishes, "left alone, our Link is beside it"));
  // Open here already.
  m.update(kFar, {Mover(2, kInFront, "Wait")}, 100);
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 200);
  wishes = m.decide({Actor(1, "Obj_Kakariko_Door_A_01", kDoor, "Push_R_ByPlayer")}, 200);
  CHECK(Plays(wishes).empty());
  CHECK(Says(wishes, "shows open here already"));
  // Nothing within 2 m (a door 3 m off, and a chest that does not answer a push).
  m.update(kFar, {Mover(2, kInFront, "Wait")}, 300);
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 400);
  wishes = m.decide({Actor(1, "Obj_Kakariko_Door_A_01", {-52.6f, 97.9f, 38.5f}),
                     Actor(2, "Obj_TBox_A_02", kDoor)},
                    400);
  CHECK(Plays(wishes).empty());
  CHECK(Says(wishes, "no door (left) within 2.0 m of its puppet here"));
  // No hinge door answers a shutter move; a shutter does, the nearest one.
  m.update(kFar, {Mover(3, kInFront, "EvtDoorShutter")}, 500);
  wishes = m.decide({Actor(1, "Obj_Kakariko_Door_A_01", kDoor),
                     Actor(4, "Obj_SwitchShutter_A_01", {-52.6f, 97.9f, 37.4f}),
                     Actor(5, "Obj_SwitchShutter_A_02", {-52.6f, 97.9f, 34.0f})},
                    500);
  const auto plays = Plays(wishes);
  REQUIRE(plays.size() == 1);
  CHECK(plays[0].actor == 5);
  CHECK(plays[0].command == "ToOpen");
}

TEST_CASE("doors: a shutter closes 0.8 s after the boss door move, a mover gone closes his door") {
  DoorMirror m;
  const Vec3 boss{0.0f, 100.0f, -20.0f};
  m.update(kFar, {Mover(2, {0.0f, 100.0f, -18.5f}, "EvtDoorBoss")}, 0);
  REQUIRE(Plays(m.decide({Actor(9, "Obj_ForestTemple_SwitchShutter_A_04", boss, "Close")}, 0)).size() == 1);
  // He goes out of our place (the boss room loads in his game): as if his move ended.
  m.update(kFar, {}, 1000);
  CHECK_FALSE(m.wants_actors(1799));
  CHECK(m.wants_actors(1800));
  const auto plays = Plays(m.decide({Actor(9, "Obj_ForestTemple_SwitchShutter_A_04", boss, "ToOpen")}, 1800));
  REQUIRE(plays.size() == 1);
  CHECK(plays[0].command == "ToClose");
}

TEST_CASE("doors: the big chest opens with his kick and stays open, not one beside our Link") {
  DoorMirror m;
  const Vec3 chest{-54.4f, 112.0f, 12.2f};  // the Deku Tree slingshot chest
  m.update(kFar, {Mover(2, {-53.3f, 112.0f, 12.2f}, "Demo_Get_Kick_TreasureBox_Adult", true)}, 0);
  auto plays = Plays(m.decide({Actor(3, "Obj_TBox_A_02", chest, "CloseWait")}, 0));
  REQUIRE(plays.size() == 1);
  CHECK(plays[0].command == "FrontOpen_Adult");
  m.update(kFar, {Mover(2, {-53.3f, 112.0f, 12.2f}, "Wait", true)}, 3000);
  m.update(kFar, {Mover(2, {-53.3f, 112.0f, 12.2f}, "Wait", true)}, 9000);
  CHECK_FALSE(m.wants_actors(9000));  // a chest has no closing
  // A small chest with our Link 2.5 m from it: the objects sync's to remove.
  DoorMirror n;
  n.update(Vec3{-54.4f, 112.0f, 14.7f}, {Mover(2, {-53.3f, 112.0f, 12.2f}, "DemoOpenBoxFront_Mini")}, 0);
  const auto wishes = n.decide({Actor(3, "Obj_TBox_A_01", chest, "CloseWait")}, 0);
  CHECK(Plays(wishes).empty());
  CHECK(Says(wishes, "left alone, our Link is beside it"));
}

TEST_CASE("doors: a move whose door is never read is dropped after a second, reset forgets all") {
  DoorMirror m;
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 0);
  CHECK(m.waiting() == 1);
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 1000);
  CHECK(m.waiting() == 1);
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL")}, 1001);
  CHECK(m.waiting() == 0);
  CHECK_FALSE(m.wants_actors(1001));
  // A push that ended before its door was found still closes it.
  m.update(kFar, {Mover(3, kInFront, "DoorPushOpenR")}, 2000);
  m.update(kFar, {Mover(3, kInFront, "Walk")}, 2050);
  REQUIRE(Plays(m.decide({Actor(1, "Obj_Kakariko_Door_A_01", kDoor)}, 2050)).size() == 1);
  CHECK(m.wants_actors(2350));
  m.reset();
  CHECK(m.opened() == 0);
  CHECK_FALSE(m.wants_actors(2350));
}

// ORM cofre (efectos-cofre, 2026-11-12): with CHEST_OPEN on, a mate's chest is opened by his message; his move
// alone opened the lid first and the message found it open. Doors still follow the move.
TEST_CASE("doors: with chests by message, a mate's chest move starts nothing, his door push does") {
  DoorMirror m;
  m.set_chests_by_message(true);
  const Vec3 chest{-54.4f, 112.0f, 12.2f};
  m.update(kFar, {Mover(2, {-53.3f, 112.0f, 12.2f}, "DemoOpenTreasureBox")}, 0);
  CHECK(m.waiting() == 0);
  CHECK(Plays(m.decide({Actor(3, "Obj_TBox_A_02", chest, "CloseWait")}, 0)).empty());
  m.update(kFar, {Mover(2, kInFront, "DoorPushOpenL_30cm_F_Ang0_FootL_Cba")}, 100);
  CHECK(m.waiting() == 1);
}
