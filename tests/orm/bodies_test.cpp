// ORM orm/cuerpos-continuar: a mate's puppet on our Link drawn aside, our
// Link never moved (coop/host/orm_bodies.hpp; oot-remake docs/CUERPOS.md).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <string>
#include <vector>

#include "coop/host/orm_bodies.hpp"
#include "coop_puppet.h"

using namespace coop::host::orm::bodies;

namespace {

float Flat(const Vec3& a, const Vec3& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
float Length(const Vec3& v) { return std::hypot(v[0], v[2]); }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

bool Has(const std::vector<std::string>& lines, const char* part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

AsideIn In(std::uint8_t number, const Vec3& sample, const Vec3& ours, std::uint64_t now_ms) {
  AsideIn in;
  in.number = number;
  in.sample = sample;
  in.ours = ours;
  in.now_ms = now_ms;
  return in;
}

}  // namespace

TEST_CASE("bodies: the round 7 Continue spot: the puppet is drawn out at once, our Link is never moved") {
  // A (us) and B's puppet at one save spot, 0.17 m apart (SONDAS.md round 7, cartel).
  PuppetAside aside;
  const Vec3 ours{-82.00f, 122.26f, 33.65f};
  const Vec3 sample{-81.93f, 122.26f, 33.81f};
  const Vec3 step = aside.step(In(1, sample, ours, 1000));
  // Its first plan: at once, out to two radii, away from us (toward +z here, the
  // open side: our Link stays on the spot the game put him).
  CHECK(Flat(Add(sample, step), ours) == doctest::Approx(2 * kBodyRadius).epsilon(1e-3));
  CHECK(step[2] > 0);
  CHECK(step[1] == 0.0f);
  CHECK(aside.aside(1));
  const auto lines = aside.take_lines();
  CHECK(Has(lines, "bodies: P1's puppet on our Link (0.17 m): drawn 0.53 m aside at once, our Link untouched"));
  // The API has no way to move our Link: only an offset for the puppet comes out.
}

TEST_CASE("bodies: the drawn step follows at its speed, then goes back on the sample when apart") {
  PuppetAside aside;
  const Vec3 ours{0, 0, 0};
  CHECK(Length(aside.step(In(2, {1.0f, 0, 0}, ours, 0))) == 0.0f);  // apart: on its sample
  // The mate walks onto us: the step grows at kSpeedMps, no pop.
  Vec3 step = aside.step(In(2, {0.3f, 0, 0}, ours, 100));
  CHECK(Length(step) == doctest::Approx(kSpeedMps * 0.1f));
  step = aside.step(In(2, {0.3f, 0, 0}, ours, 200));
  CHECK(Length(step) == doctest::Approx(0.4f));  // the whole gap: 0.7 - 0.3
  CHECK(step[0] > 0);                            // away from us
  CHECK(Has(aside.take_lines(), "P2's puppet on our Link (0.30 m): drawn 0.40 m aside, our Link untouched"));
  // Apart again: back onto the sample at the same speed, then the line.
  step = aside.step(In(2, {2.0f, 0, 0}, ours, 300));
  CHECK(Length(step) == doctest::Approx(0.2f));
  CHECK(aside.take_lines().empty());
  step = aside.step(In(2, {2.0f, 0, 0}, ours, 400));
  CHECK(Length(step) == 0.0f);
  CHECK(Has(aside.take_lines(), "P2's puppet drawn on its sample again"));
  CHECK_FALSE(aside.aside(2));
}

TEST_CASE("bodies: on top of each other the way comes from the mate's number, held through jitter") {
  const Vec3 w1 = way_of_number(1);
  CHECK(Length(w1) == doctest::Approx(1.0f));
  CHECK(Length(Add(w1, way_of_number(2))) > 0.1f);  // two mates on us part different ways

  PuppetAside aside;
  const Vec3 ours{5, 0, 5};
  const Vec3 first = aside.step(In(1, ours, ours, 0));
  CHECK(Length(first) == doctest::Approx(2 * kBodyRadius));
  CHECK(first[0] / Length(first) == doctest::Approx(w1[0]));
  CHECK(first[2] / Length(first) == doctest::Approx(w1[2]));
  CHECK(Has(aside.take_lines(), "on top: its way by its number"));
  // The samples jitter around our Link, both sides: the way is kept.
  for (int i = 1; i <= 10; ++i) {
    const float j = (i % 2 == 0 ? 0.02f : -0.02f);
    const Vec3 s = aside.step(In(1, {ours[0] + j, 0, ours[2] - j}, ours, std::uint64_t(i) * 33));
    CHECK(s[0] * w1[0] + s[2] * w1[2] > 0.6f);
  }
}

TEST_CASE("bodies: downed, another floor, not shown or our Link not read: on its sample") {
  const Vec3 ours{0, 0, 0};
  PuppetAside aside;
  AsideIn in = In(1, {0.2f, 0, 0}, ours, 0);
  in.down = true;  // a reviver kneels by him
  CHECK(Length(aside.step(in)) == 0.0f);
  in = In(1, {0.2f, 2.0f, 0}, ours, 33);  // another floor
  CHECK(Length(aside.step(in)) == 0.0f);
  in = In(1, {0.2f, 0, 0}, ours, 66);
  in.shown = false;  // held, hidden or parked
  CHECK(Length(aside.step(in)) == 0.0f);
  in = In(1, {0.2f, 0, 0}, ours, 99);
  in.ours.reset();
  CHECK(Length(aside.step(in)) == 0.0f);
  CHECK(aside.take_lines().empty());
  // Forgotten when no longer planned: a later plan is a first one (at once).
  aside.prune(99 + kForgetMs + 1);
  CHECK(Length(aside.step(In(1, {0.2f, 0, 0}, ours, 99 + kForgetMs + 2))) == doctest::Approx(0.5f));
}

TEST_CASE("bodies: the read back of a puppet drawn aside, once each way per contact") {
  ReadBack rb;
  const Vec3 drawn{1, 2, 3};
  std::string line = rb.judge(1, drawn, {1.01f, 2, 3});
  CHECK(line.find("bodies: P1's puppet read back where we drew it aside") == 0);
  CHECK(rb.judge(1, drawn, drawn).empty());  // said once
  line = rb.judge(1, drawn, {1.3f, 2, 3});
  CHECK(line.find("check: bodies: P1's puppet read back at (1.30, 2.00, 3.00), 0.30 m from where we drew it") == 0);
  CHECK(rb.judge(1, drawn, {1.3f, 2, 3}).empty());
  rb.end(1);
  CHECK_FALSE(rb.judge(1, drawn, drawn).empty());  // a new contact says it again
}

TEST_CASE("bodies: the planner draws a puppet on our Link aside (LICHII_ORM_BODIES), off draws it on its sample") {
  std::vector<CoopPuppet::Remote> remotes(1);
  remotes[0].number = 1;
  remotes[0].scene = "KokiriForest";
  remotes[0].position = {10.6f, 0.0f, 10.0f};  // 0.6 m from us: no first-sight step (that is under 0.5 m)
  const CoopPuppet::Vec3 ours{10.0f, 0.0f, 10.0f};

  CoopPuppet::Planner off;
  auto in = off.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1000);
  REQUIRE(in.targets.size() == 1);
  CHECK(in.targets[0].aside == CoopPuppet::Vec3{});
  CHECK(in.targets[0].position[0] == doctest::Approx(10.6f));

  CoopPuppet::PlannerSettings s;
  s.bodies = true;
  CoopPuppet::Planner on(s);
  in = on.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1000);
  REQUIRE(in.targets.size() == 1);
  const auto& t = in.targets[0];
  CHECK(t.aside[0] == doctest::Approx(0.1f));
  CHECK(t.offset[0] == doctest::Approx(0.1f));  // the director adds it to the smoothed place too
  CHECK(Flat(t.position, ours) == doctest::Approx(2 * kBodyRadius));
  const auto lines = on.TakeEffectLines();
  CHECK(Has(lines, "bodies: P1's puppet on our Link (0.60 m): drawn 0.10 m aside at once"));
}

// ---- ORM choque (docs/CHOQUE.md): the gate of a mate's real body -------------

namespace {

GateIn Gate(std::uint8_t number, const Vec3& sample, const Vec3& ours, std::uint64_t now_ms) {
  GateIn g;
  g.number = number;
  g.sample = sample;
  g.ours = ours;
  g.now_ms = now_ms;
  return g;
}

}  // namespace

TEST_CASE("choque: a body is a ghost until clear, then solid, and the Continue spot never wedges anyone") {
  BodyGate gate;
  const Vec3 ours{-82.00f, 122.26f, 33.65f};
  // The round 7 Continue spot: B 0.17 m from us. A ghost, said once, however long it stays.
  const Vec3 on_us{-81.93f, 122.26f, 33.81f};
  CHECK_FALSE(gate.solid(Gate(1, on_us, ours, 1000)));
  auto lines = gate.take_lines();
  CHECK(Has(lines, "bodies: P1's body a ghost until clear: on our Link (0.17 m)"));
  CHECK_FALSE(gate.solid(Gate(1, on_us, ours, 5000)));
  CHECK(gate.take_lines().empty());
  // He walks off: clear (0.8 m) for kClearForMs before the body comes.
  const Vec3 off{-81.00f, 122.26f, 33.65f};  // 1.0 m
  CHECK_FALSE(gate.solid(Gate(1, off, ours, 6000)));
  CHECK_FALSE(gate.solid(Gate(1, off, ours, 6000 + kClearForMs - 1)));
  CHECK(gate.solid(Gate(1, off, ours, 6000 + kClearForMs)));
  lines = gate.take_lines();
  CHECK(Has(lines, "bodies: P1's body solid (1.00 m from our Link)"));
  CHECK(gate.is_solid(1));
  // Solid, he comes back to touching (0.46 m, two children): it stays solid (the bodies part them).
  CHECK(gate.solid(Gate(1, {-81.54f, 122.26f, 33.65f}, ours, 7000)));
  // Deep in our Link (a lag spike, a push): a ghost again until clear.
  CHECK_FALSE(gate.solid(Gate(1, {-81.85f, 122.26f, 33.65f}, ours, 7100)));
  CHECK(Has(gate.take_lines(), "bodies: P1's body a ghost until clear: on our Link (0.15 m)"));
  CHECK_FALSE(gate.solid(Gate(1, off, ours, 7200)));  // clear again, the wait begins again
  CHECK(gate.solid(Gate(1, off, ours, 7200 + kClearForMs)));
}

TEST_CASE("choque: a new mate starts a ghost even far away, and a warp, downed, riding, hidden or no Link make a ghost") {
  BodyGate gate;
  const Vec3 ours{0, 0, 0};
  const Vec3 far{5, 0, 0};
  CHECK_FALSE(gate.solid(Gate(2, far, ours, 0)));  // never solid on its first plan
  CHECK(gate.solid(Gate(2, far, ours, kClearForMs)));
  gate.take_lines();
  // A warp of its samples (Farore's Wind, a teleport): a ghost, then the wait.
  CHECK_FALSE(gate.solid(Gate(2, {5, 0, 9}, ours, 400)));
  CHECK(Has(gate.take_lines(), "bodies: P2's body a ghost: its samples jumped (a warp)"));
  CHECK_FALSE(gate.solid(Gate(2, {5, 0, 9}, ours, 500)));
  CHECK(gate.solid(Gate(2, {5, 0, 9}, ours, 500 + kClearForMs)));
  for (const char* why : {"downed", "riding", "not shown", "our Link not read"}) {
    GateIn g = Gate(2, {5, 0, 9}, ours, 1000);
    const std::string w = why;
    if (w == "downed") g.down = true;
    if (w == "riding") g.riding = true;
    if (w == "not shown") g.shown = false;
    if (w == "our Link not read") g.ours.reset();
    REQUIRE(gate.solid(Gate(2, {5, 0, 9}, ours, 1000)));  // solid before (clear held from 500)
    CHECK_FALSE(gate.solid(g));
    CHECK(Has(gate.take_lines(), (std::string("bodies: P2's body a ghost: ") + why).c_str()));
    CHECK_FALSE(gate.solid(Gate(2, {5, 0, 9}, ours, 1000)));  // back: the wait first
    CHECK(gate.solid(Gate(2, {5, 0, 9}, ours, 1000 + kClearForMs)));
    gate.take_lines();
  }
}

TEST_CASE("choque: one over the other is clear: our Link can stand on a mate's head, a mate on ours") {
  BodyGate gate;
  const Vec3 mate{0, 0, 0};
  // Our feet on a child mate's head (1.17), right over him: not "on our Link".
  Apart a = apart_of(mate, {0.05f, kChildHead, 0}, false, false);
  CHECK_FALSE(a.level);
  // His feet on our adult head.
  a = apart_of({0, kAdultHead, 0}, {0, 0, 0}, false, true);
  CHECK_FALSE(a.level);
  // Side by side, or his feet at our knees: level.
  CHECK(apart_of({0.3f, 0.4f, 0}, {0, 0, 0}, true, true).level);
  // The gate: solid stays solid while we stand on him (the stand-on probe).
  CHECK_FALSE(gate.solid(Gate(3, {2, 0, 0}, {0, 0, 0}, 0)));
  CHECK(gate.solid(Gate(3, {2, 0, 0}, {0, 0, 0}, kClearForMs)));
  GateIn g = Gate(3, {0.05f, 0, 0}, {0, kChildHead, 0}, kClearForMs + 500);
  CHECK(gate.solid(g));
  // And a mate first seen standing on our head turns solid after the wait.
  CHECK_FALSE(gate.solid(Gate(4, {0, kChildHead, 0}, {0, 0, 0}, 0)));
  CHECK(gate.solid(Gate(4, {0, kChildHead, 0}, {0, 0, 0}, kClearForMs)));
  CHECK(Has(gate.take_lines(), "bodies: P4's body solid (0.00 m from our Link, one over the other)"));
}

TEST_CASE("choque: a solid body is drawn on its sample (no drawn aside), a ghost one aside as before") {
  PuppetAside aside;
  AsideIn in = In(1, {0.3f, 0, 0}, {0, 0, 0}, 1000);
  in.solid = true;
  CHECK(Length(aside.step(in)) == 0.0f);
  CHECK(aside.take_lines().empty());
  in.solid = false;
  in.now_ms = 2000;
  CHECK(Length(aside.step(in)) > 0.0f);  // a ghost: drawn aside (at its speed)
}

TEST_CASE("choque: the planner's gate (SetColliders): ghost first, solid when clear, the aside off while solid") {
  std::vector<CoopPuppet::Remote> remotes(1);
  remotes[0].number = 1;
  remotes[0].scene = "KokiriForest";
  remotes[0].position = {12.0f, 0.0f, 10.0f};  // 2 m: no first-sight step
  remotes[0].adult = true;
  const CoopPuppet::Vec3 ours{10.0f, 0.0f, 10.0f};
  CoopPuppet::PlannerSettings s;
  s.bodies = true;
  CoopPuppet::Planner p(s);
  CHECK_FALSE(p.Colliders());
  auto in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1000);
  REQUIRE(in.targets.size() == 1);
  CHECK_FALSE(in.targets[0].solid);  // colliders off: no gate
  CHECK(in.targets[0].adult);
  p.SetColliders(true);
  CHECK(p.Colliders());
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1100);
  CHECK_FALSE(in.targets[0].solid);  // a ghost until clear for kClearForMs
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1100 + kClearForMs);
  CHECK(in.targets[0].solid);
  // He walks up to us: solid, drawn on his sample (no aside), however close the bodies let him.
  remotes[0].position = {10.5f, 0.0f, 10.0f};
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1500);
  CHECK(in.targets[0].solid);
  CHECK(Length(in.targets[0].aside) == 0.0f);
  CHECK(in.targets[0].position[0] == doctest::Approx(10.5f));
  // Deep in us (a lag spike): a ghost, and drawn aside as before.
  remotes[0].position = {10.1f, 0.0f, 10.0f};
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1600);
  CHECK_FALSE(in.targets[0].solid);
  CHECK(Length(in.targets[0].aside) > 0.0f);
  const auto lines = p.TakeEffectLines();
  CHECK(Has(lines, "bodies: P1's body solid (2.00 m from our Link)"));
  CHECK(Has(lines, "bodies: P1's body a ghost until clear: on our Link (0.10 m)"));
  // Off and on again: a ghost until clear once more.
  remotes[0].position = {12.0f, 0.0f, 10.0f};
  p.SetColliders(false);
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1700);
  CHECK_FALSE(in.targets[0].solid);
  p.SetColliders(true);
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1800);
  CHECK_FALSE(in.targets[0].solid);
  in = p.Plan(0x80000000, std::string("KokiriForest"), ours, false, remotes, 1800 + kClearForMs);
  CHECK(in.targets[0].solid);
}

TEST_CASE("choque: the probes' lines: our Link against a solid body, our Link standing on a head") {
  BodyGate gate;
  const Vec3 mate{0, 0, 0};
  CHECK_FALSE(gate.solid(Gate(2, mate, {3, 0, 0}, 0)));
  CHECK(gate.solid(Gate(2, mate, {3, 0, 0}, kClearForMs)));
  gate.take_lines();
  // Our Link walks into him and stops at the two radii (children: 0.46 m).
  CHECK(gate.solid(Gate(2, mate, {0.47f, 0, 0}, 1000)));
  CHECK(gate.take_lines().empty());
  CHECK(gate.solid(Gate(2, mate, {0.47f, 0, 0}, 1000 + kTouchForMs)));
  CHECK(Has(gate.take_lines(), "bodies: our Link against P2's body (0.47 m apart, the bodies 0.46)"));
  CHECK(gate.solid(Gate(2, mate, {0.47f, 0, 0}, 3000)));
  CHECK(gate.take_lines().empty());  // once per contact
  // He steps back and we land on his head (the disc at 1.17).
  CHECK(gate.solid(Gate(2, mate, {0.05f, kChildHead + 0.01f, 0}, 4000)));
  CHECK(gate.solid(Gate(2, mate, {0.05f, kChildHead + 0.01f, 0}, 4000 + kStandForMs)));
  CHECK(Has(gate.take_lines(), "bodies: our Link stands on P2's head (1.18 m over his feet)"));
  // Off his head and back: said again.
  CHECK(gate.solid(Gate(2, mate, {2, 0, 0}, 6000)));
  CHECK(gate.solid(Gate(2, mate, {0.05f, kChildHead, 0}, 6100)));
  CHECK(gate.solid(Gate(2, mate, {0.05f, kChildHead, 0}, 6100 + kStandForMs)));
  CHECK(Has(gate.take_lines(), "bodies: our Link stands on P2's head"));
}
