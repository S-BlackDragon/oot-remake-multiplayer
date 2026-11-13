// ORM choque (lichii/hook/coop_bodies.h, oot-remake docs/CHOQUE.md): the plain
// logic of the mates' bodies in our game: the switch, the pack's bodies, the
// pick and its fallbacks, the bodies' plan from the puppets', the same-frame
// pose and the create tally.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>

#include "coop_bodies.h"

using namespace CoopBodies;

TEST_CASE("bodies hook: LICHII_ORM_BODY_COLLIDER: room by default, on/off words, anything else room with a line") {
  CHECK(ModeOf(std::nullopt) == Mode::room);
  CHECK(ModeOf(std::string_view("")) == Mode::room);
  CHECK(ModeOf(std::string_view("room")) == Mode::room);
  CHECK(ModeOf(std::string_view(" ROOM ")) == Mode::room);
  CHECK(ModeOf(std::string_view("1")) == Mode::on);
  CHECK(ModeOf(std::string_view("on")) == Mode::on);
  CHECK(ModeOf(std::string_view("0")) == Mode::off);
  CHECK(ModeOf(std::string_view("No")) == Mode::off);
  std::string complaint;
  CHECK(ModeOf(std::string_view("sometimes"), &complaint) == Mode::room);
  CHECK(complaint == "coop: switch LICHII_ORM_BODY_COLLIDER=sometimes is not room/0/1/true/false/yes/no/on/off: "
                     "room (its default)");
  complaint.clear();
  ModeOf(std::string_view("off"), &complaint);
  CHECK(complaint.empty());
}

TEST_CASE("bodies hook: the actors by age and kind, and which the pack's coop_puppet.json lists") {
  CHECK(BodyActor(false, true) == "CoopPuppet_Child_Body");
  CHECK(BodyActor(true, true) == "CoopPuppet_Adult_Body");
  CHECK(BodyActor(false, false) == "CoopPuppet_Child_BodySide");
  CHECK(BodyActor(true, false) == "CoopPuppet_Adult_BodySide");
  // A pack with the sides alone: "_BodySide" is not "_Body" (the closing quote).
  const PackBodies side = BodiesIn(R"({"bodies": [{"actor": "CoopPuppet_Child_BodySide"},
                                                  {"actor": "CoopPuppet_Adult_BodySide"}]})");
  CHECK_FALSE(side.Top());
  CHECK(side.Side());
  const PackBodies all = BodiesIn(R"({"bodies": [{"actor": "CoopPuppet_Child_Body"}, {"actor": "CoopPuppet_Adult_Body"},
    {"actor": "CoopPuppet_Child_BodySide"}, {"actor": "CoopPuppet_Adult_BodySide"}]})");
  CHECK(all.Top());
  CHECK(all.Side());
  CHECK_FALSE(BodiesIn(R"({"actors": [{"actor": "CoopPuppet_Child"}]})").Side());  // an older pack
  const PackBodies half = BodiesIn(R"("CoopPuppet_Child_Body")");
  CHECK_FALSE(half.Top());  // both ages or none
  CHECK(Merge(half, BodiesIn(R"("CoopPuppet_Adult_Body")")).Top());
}

TEST_CASE("bodies hook: the pick: the room's switch, the forced modes, the pack and a broken create") {
  PackBodies all;
  all.child = all.adult = all.child_side = all.adult_side = true;
  Pick p = Choose(Mode::room, false, all, true, false);
  CHECK_FALSE(p.on);
  CHECK_FALSE(p.fallback);  // the room's choice, not our game's
  CHECK(p.why.find("the room's /choque is off") == 0);
  p = Choose(Mode::room, true, all, true, false);
  CHECK(p.on);
  CHECK(p.top);
  CHECK(p.why.empty());
  CHECK(Choose(Mode::room, true, all, false, false).top == false);  // LICHII_ORM_BODY_TOP=0
  CHECK(Choose(Mode::on, false, all, true, false).on);              // the probes: whatever the room says
  p = Choose(Mode::off, true, all, true, false);
  CHECK_FALSE(p.on);
  CHECK(p.fallback);
  CHECK(p.why.find("LICHII_ORM_BODY_COLLIDER=0") == 0);
  p = Choose(Mode::room, true, PackBodies{}, true, false);
  CHECK_FALSE(p.on);
  CHECK(p.fallback);
  CHECK(p.why.find("the puppet pack has no CoopPuppet_Child_Body") == 0);
  p = Choose(Mode::room, true, all, true, true);
  CHECK_FALSE(p.on);
  CHECK(p.fallback);
  CHECK(p.why.find("a body could not be made 2 times") == 0);
  // The kind asked for, else the one the pack has.
  PackBodies side;
  side.child_side = side.adult_side = true;
  CHECK(Choose(Mode::room, true, side, true, false).top == false);
  PackBodies top;
  top.child = top.adult = true;
  CHECK(Choose(Mode::room, true, top, false, false).top == true);
}

TEST_CASE("bodies hook: the bodies' plan follows the puppets': place less the aside, solid shown, ghost parked") {
  CoopPuppet::Input puppets;
  puppets.main_base = 0x80000000;
  puppets.scene = "KokiriForest";
  puppets.camera = true;
  puppets.drawn_since_ms = 5;
  puppets.settle_ms = 1000;
  puppets.server_now_us = 123;
  puppets.server_at_ms = 456;
  CoopPuppet::Target a;
  a.number = 1;
  a.actor = "CoopPuppet_Adult_Lock";
  a.adult = true;
  a.solid = true;
  a.position = {10.0f, 1.0f, 5.0f};
  a.offset = {0.5f, 0.0f, 0.0f};  // a side step, no aside
  a.rotation = {0, 0, 1, 0, 1, 0, -1, 0, 0};
  a.anim = CoopPuppet::Anim{"Run", 3.0f, 1.0f};
  a.gear[0] = "Weapon_Sword_Kokiri";
  a.color = 0x010203u;
  CoopPuppet::Target b;
  b.number = 2;
  b.actor = "CoopPuppet_Child";
  b.solid = false;
  b.position = {2.3f, 0.0f, 0.0f};
  b.aside = {0.3f, 0.0f, 0.0f};  // drawn aside from its sample at 2.0
  b.offset = {0.3f, 0.0f, 0.0f};
  puppets.targets = {a, b};
  const CoopPuppet::Input in = BodiesFrom(puppets, true);
  CHECK(in.main_base == puppets.main_base);
  CHECK(in.scene == "KokiriForest");
  CHECK(in.camera);
  CHECK(in.drawn_since_ms == 5);
  CHECK(in.settle_ms == 1000);
  CHECK(in.server_now_us == 123);
  CHECK(in.server_at_ms == 456);
  REQUIRE(in.targets.size() == 2);
  const auto& ba = in.targets[0];
  CHECK(ba.number == 1);
  CHECK(ba.actor == "CoopPuppet_Adult_Body");  // by age, never the lock-on variant's name
  CHECK(ba.show == CoopPuppet::Show::show);
  CHECK(ba.position == CoopPuppet::Vec3{10.0f, 1.0f, 5.0f});
  CHECK(ba.offset == CoopPuppet::Vec3{0.5f, 0.0f, 0.0f});  // the director adds it to the smoothed place too
  CHECK(ba.rotation == CoopPuppet::Mat3{1, 0, 0, 0, 1, 0, 0, 0, 1});  // upright
  CHECK_FALSE(ba.anim);
  CHECK_FALSE(ba.gear[0]);
  CHECK_FALSE(ba.color);
  CHECK_FALSE(ba.effects);
  const auto& bb = in.targets[1];
  CHECK(bb.actor == "CoopPuppet_Child_Body");
  CHECK(bb.show == CoopPuppet::Show::hide);  // a ghost: parked under the floor
  CHECK(bb.position[0] == doctest::Approx(2.0f));  // its sample, not where its puppet is drawn aside
  CHECK(bb.offset[0] == doctest::Approx(0.0f));
  CHECK(BodiesFrom(puppets, false).targets[0].actor == "CoopPuppet_Adult_BodySide");
}

TEST_CASE("bodies hook: the same frame's pose only, upright and still") {
  CoopPuppet::ShownPose shown;
  shown.server_us = 1'000'000;
  shown.pose.position = {1, 2, 3};
  shown.pose.rotation = coop::protocol::Quat{0, 0.7f, 0, 0.7f};
  shown.pose.animation = coop::protocol::Animation{};
  const auto p = SameFrame(shown, 1'000'000 + kSameFrameUs);
  REQUIRE(p);
  CHECK(p->position == coop::protocol::Vec3{1, 2, 3});
  CHECK_FALSE(p->rotation);
  CHECK_FALSE(p->animation);
  CHECK(SameFrame(shown, 1'000'000 - 10'000));
  CHECK_FALSE(SameFrame(shown, 1'000'000 + kSameFrameUs + 1));  // not drawn this frame: the plan's place
  CHECK_FALSE(SameFrame(std::nullopt, 1'000'000));
  CHECK_FALSE(SameFrame(shown, 0));
}

TEST_CASE("bodies hook: the create tally: broken after two failures with none made, the first made once") {
  CreateTally t;
  CHECK_FALSE(t.Broken());
  CHECK_FALSE(t.TakeFirstMade());
  t.Count("coop: puppet: P2: create CoopPuppet_Child_Body requested (handle slot 0x1)");
  t.Count("coop: puppet: P2 CoopPuppet_Child_Body not spawned after 10000 ms");
  CHECK_FALSE(t.Broken());
  t.Count("coop: puppet: P2: create CoopPuppet_Child_Body requested (handle slot 0x1)");
  t.Count("coop: puppet: P2: request 0x5 dropped (state 4), retry in 5 s");
  REQUIRE(t.Broken());
  CHECK(*t.Broken() == "CoopPuppet_Child_Body");
  // One made: not broken (a slow load failed once or twice before).
  t.Count("coop: puppet: P2 CoopPuppet_Child_Body spawned after 300 ms");
  CHECK_FALSE(t.Broken());
  CHECK(t.TakeFirstMade() == std::string("CoopPuppet_Child_Body"));
  CHECK_FALSE(t.TakeFirstMade());
  t.Count("nothing to do with it");
  t.Count("coop: puppet: Px: create X requested");
}
