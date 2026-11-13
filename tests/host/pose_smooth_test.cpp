// Tests for the puppet smoothing (coop/host/pose_smooth.hpp; oot-remake docs/SUAVIZADO.md,
// PLAN_100 E7): jitter, loss, reordering, stop, teleport, the sender's animation and gait.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "coop/host/pose_smooth.hpp"

using namespace coop::host;
using coop::protocol::Animation;
using coop::protocol::Vec3;

namespace {

constexpr std::uint64_t kMs = 1000;
constexpr std::uint64_t kT0 = 10'000'000;  // server time of the first sample
constexpr float kSpeed = 6.0f;             // units a second along x

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A player running along x at kSpeed from x = 0 at kT0, sampled every `every_ms`.
SmoothSample running(std::uint32_t seq, std::uint64_t every_ms = 16) {
  SmoothSample s;
  s.time = {kT0 + seq * every_ms * kMs, seq};
  const float t = static_cast<float>(seq * every_ms) / 1000.0f;
  s.position = {kSpeed * t, 0.0f, 0.0f};
  s.velocity = Vec3{kSpeed, 0.0f, 0.0f};
  s.rotation = coop::protocol::Quat{0, 0, 0, 1};
  s.scene = "Kokiri";
  s.animation = Animation{"Move", static_cast<float>(seq), 1.0f};
  return s;
}

// Where the running player really was at server time `us`.
float truth(std::uint64_t us) { return kSpeed * static_cast<float>(us - kT0) / 1e6f; }

// The shown position each frame (60 a second) from `from_us` to `to_us` of the server clock.
std::vector<SmoothedPose> frames(PoseSmoother& s, std::uint64_t from_us, std::uint64_t to_us,
                                 std::uint64_t step_us = 16'667) {
  std::vector<SmoothedPose> out;
  for (std::uint64_t now = from_us; now <= to_us; now += step_us) {
    if (auto p = s.at(now)) out.push_back(*p);
  }
  return out;
}

// The largest step between two shown frames, as a multiple of what kSpeed covers in one.
float worst_step(const std::vector<SmoothedPose>& f) {
  float worst = 0.0f;
  for (std::size_t i = 1; i < f.size(); ++i) {
    const float dt = static_cast<float>(f[i].render_us - f[i - 1].render_us) / 1e6f;
    if (dt <= 0.0f) continue;
    worst = std::max(worst, std::fabs(f[i].position[0] - f[i - 1].position[0]) / (kSpeed * dt));
  }
  return worst;
}

}  // namespace

TEST_CASE("smooth: a steady stream is drawn 100 ms behind, on the line") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i < 40; ++i) REQUIRE(s.push(running(i)) == SmoothPush::buffered);
  const std::uint64_t now = kT0 + 300 * kMs + 5 * kMs;
  const auto p = s.at(now);
  REQUIRE(p);
  CHECK(p->how == SmoothHow::interpolated);
  CHECK(p->render_us == now - 100 * kMs);
  CHECK(near(p->position[0], truth(now - 100 * kMs)));
  CHECK(p->snapped);  // the first pose
  CHECK_FALSE(s.at(now + 16 * kMs)->snapped);
}

TEST_CASE("smooth: jitter and bursts on the wire do not show") {
  PoseSmoother s;
  std::mt19937 rng(7);
  std::uniform_int_distribution<int> late(10, 90);  // ms on the wire
  struct Arrival {
    std::uint64_t at;
    SmoothSample sample;
  };
  std::vector<Arrival> wire;
  for (std::uint32_t i = 0; i < 200; ++i) {
    const SmoothSample m = running(i);
    std::uint64_t at = m.time.server_time_us + late(rng) * kMs;
    if (i % 7 == 0) at += 40 * kMs;  // now and then a burst behind a stall
    wire.push_back({at, m});
  }
  std::sort(wire.begin(), wire.end(), [](const Arrival& a, const Arrival& b) { return a.at < b.at; });
  std::vector<SmoothedPose> shown;
  std::size_t next = 0;
  for (std::uint64_t now = kT0 + 150 * kMs; now < kT0 + 3000 * kMs; now += 16'667) {
    while (next < wire.size() && wire[next].at <= now) s.push(wire[next++].sample);
    if (auto p = s.at(now)) shown.push_back(*p);
  }
  REQUIRE(shown.size() > 100);
  for (std::size_t i = 1; i < shown.size(); ++i) {
    // Never more than 5 cm off where the player was at the shown time.
    CHECK(std::fabs(shown[i].position[0] - truth(shown[i].render_us)) < 0.05f);
  }
  CHECK(worst_step(shown) < 1.5f);  // no frame jumps ahead or stalls hard
  const auto st = s.take_stats();
  CHECK(st.reordered > 0);
  CHECK(st.snaps == 1);  // only the first pose
}

TEST_CASE("smooth: lost samples are bridged by the line between the ones that came") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i < 60; ++i) {
    if (i % 4 != 0) continue;  // three of four lost
    s.push(running(i));
  }
  const auto f = frames(s, kT0 + 120 * kMs, kT0 + 900 * kMs);
  for (const auto& p : f) {
    CHECK(p.how == SmoothHow::interpolated);
    CHECK(near(p.position[0], truth(p.render_us), 2e-3f));
  }
}

TEST_CASE("smooth: a sample that arrives out of order is put in its place") {
  PoseSmoother ordered;
  PoseSmoother mixed;
  for (std::uint32_t i = 0; i < 20; ++i) ordered.push(running(i));
  for (std::uint32_t i = 0; i < 20; i += 2) {
    mixed.push(running(i + 1));
    mixed.push(running(i));
  }
  CHECK(mixed.take_stats().reordered == 10);
  const std::uint64_t now = kT0 + 230 * kMs;
  CHECK(near(mixed.at(now)->position[0], ordered.at(now)->position[0], 1e-5f));
  // Duplicates and nonsense are refused.
  CHECK(mixed.push(running(3)) == SmoothPush::duplicate);
  SmoothSample bad = running(50);
  bad.position[1] = std::nanf("");
  CHECK(mixed.push(bad) == SmoothPush::invalid);
  bad = running(51);
  bad.rotation = coop::protocol::Quat{0, 0, 0, 0};
  CHECK(mixed.push(bad) == SmoothPush::invalid);
  // Older than the shown time and than the newest: it would rewrite the past.
  SmoothSample old = running(0);
  old.time.sequence = 900;
  CHECK(mixed.push(old) == SmoothPush::too_old);
}

TEST_CASE("smooth: past the newest sample it is carried on 150 ms, then held") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));  // the last at kT0 + 160 ms
  const std::uint64_t last = kT0 + 160 * kMs;
  auto p = s.at(last + 100 * kMs + 50 * kMs);  // shown 50 ms past it
  REQUIRE(p);
  CHECK(p->how == SmoothHow::extrapolated);
  CHECK(p->ahead_us == 50 * kMs);
  CHECK(near(p->position[0], truth(last + 50 * kMs)));
  p = s.at(last + 100 * kMs + 400 * kMs);
  CHECK(p->how == SmoothHow::held);
  CHECK(near(p->position[0], truth(last + 150 * kMs)));  // capped at 150 ms
  CHECK(near(s.at(last + 100 * kMs + 900 * kMs)->position[0], truth(last + 150 * kMs)));
  CHECK(s.take_stats().held == 2);
}

TEST_CASE("smooth: a stop is not overshot, and a stop learned late is eased back") {
  SUBCASE("in time: the curve to the still sample ends there") {
    PoseSmoother s;
    for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
    SmoothSample still = running(11);
    still.position = running(10).position;  // stopped dead
    still.position[0] += 0.02f;
    still.velocity = Vec3{0, 0, 0};
    s.push(still);
    const float end = still.position[0];
    for (const auto& p : frames(s, kT0 + 200 * kMs, kT0 + 800 * kMs, 4'000)) {
      CHECK(p.position[0] <= end + 1e-4f);
    }
    CHECK(near(s.at(kT0 + 900 * kMs)->position[0], end));
  }
  SUBCASE("late: shown past it, then eased back over 100 ms, not snapped") {
    PoseSmoother s;
    for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
    std::vector<SmoothedPose> f = frames(s, kT0 + 260 * kMs, kT0 + 360 * kMs);  // 100 ms past
    REQUIRE_FALSE(f.empty());
    const float shown_past = f.back().position[0];
    SmoothSample still = running(11);
    still.position = running(10).position;
    still.velocity = Vec3{0, 0, 0};
    CHECK(s.push(still) == SmoothPush::buffered);
    const float end = still.position[0];
    CHECK(shown_past > end + 0.3f);
    auto after = frames(s, kT0 + 377 * kMs, kT0 + 600 * kMs);
    REQUIRE(after.size() > 6);
    CHECK_FALSE(after.front().snapped);
    CHECK(after.front().position[0] > end + 0.2f);  // not one big jump back
    for (std::size_t i = 1; i < after.size(); ++i) {
      CHECK(after[i].position[0] <= after[i - 1].position[0] + 1e-5f);  // only back, never forth
    }
    CHECK(near(after.back().position[0], end));
    CHECK(s.take_stats().corrections >= 1);
  }
}

TEST_CASE("smooth: a teleport snaps at its time, never slides") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
  SmoothSample far = running(11);
  far.position = {500.0f, 20.0f, -300.0f};
  far.velocity.reset();  // the sampler sends none for a warp
  CHECK(s.push(far) == SmoothPush::cut);
  SmoothSample on = far;
  on.time = {far.time.server_time_us + 16 * kMs, 12};
  s.push(on);
  bool snapped = false;
  for (const auto& p : frames(s, kT0 + 200 * kMs, kT0 + 400 * kMs, 4'000)) {
    const bool before = p.position[0] < 100.0f;
    if (!before) {
      CHECK(near(p.position[0], 500.0f));  // all or nothing: no point on the way
      if (!snapped) {
        CHECK(p.snapped);
      }
      snapped = true;
    } else {
      CHECK(p.position[0] <= truth(kT0 + 176 * kMs) + 1e-3f);
    }
  }
  CHECK(snapped);
}

TEST_CASE("smooth: a scene change is a cut even close by") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
  SmoothSample in = running(11);
  in.scene = "KokiriHouse";
  in.position = {0.5f, 0.0f, 0.0f};
  CHECK(s.push(in) == SmoothPush::cut);
  s.at(kT0 + 260 * kMs);
  const auto p = s.at(kT0 + 280 * kMs);  // the shown time passes 176 ms
  REQUIRE(p);
  CHECK(p->snapped);
  CHECK(near(p->position[0], 0.5f + kSpeed * 0.004f));
}

TEST_CASE("smooth: a gap is not drawn as one slow slide, the step after it is eased or snapped") {
  // Samples to 160 ms, then nothing until 960 ms (over max_gap_us: not joined). Meanwhile the
  // puppet is carried 150 ms on (to x = 1.86) and held, not crawling towards the next sample.
  const auto run = [](float back_x) {
    PoseSmoother s;
    for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
    SmoothSample back = running(60);
    back.position[0] = back_x;
    SmoothSample next = running(61);
    next.position[0] = back_x + kSpeed * 0.016f;
    CHECK(s.push(back) == SmoothPush::buffered);  // 800 ms apart: no teleport by speed
    s.push(next);
    return frames(s, kT0 + 300 * kMs, kT0 + 1300 * kMs, 8'000);
  };
  const float held = truth(kT0 + 310 * kMs);
  SUBCASE("a step under snap_distance is eased over 100 ms") {
    const auto f = run(held + 1.0f);
    bool reached = false;
    for (std::size_t i = 0; i < f.size(); ++i) {
      const auto& p = f[i];
      if (p.render_us < kT0 + 960 * kMs) {
        CHECK(p.position[0] <= held + 1e-3f);
      } else if (!reached) {
        reached = true;
        CHECK_FALSE(p.snapped);
        CHECK(p.position[0] < held + 0.3f);  // not one jump of a metre
      }
    }
    CHECK(reached);
  }
  SUBCASE("a step over snap_distance snaps") {
    const auto f = run(held + 3.9f);
    bool snapped = false;
    for (const auto& p : f) {
      if (p.render_us >= kT0 + 960 * kMs && !snapped) {
        CHECK(p.snapped);
        CHECK(p.position[0] > held + 3.8f);
        snapped = true;
      }
    }
    CHECK(snapped);
  }
}

TEST_CASE("smooth: the render clock does not go back for a small step, follows a big one") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i < 60; ++i) s.push(running(i));
  const auto a = s.at(kT0 + 400 * kMs);
  const auto b = s.at(kT0 + 390 * kMs);  // the estimate stepped back 10 ms
  CHECK(b->render_us == a->render_us);
  CHECK_FALSE(b->snapped);
  const auto c = s.at(kT0 + 400 * kMs - 3'000 * kMs);  // a new clock
  REQUIRE(c);
  CHECK(c->snapped);
  CHECK(c->how == SmoothHow::early);
  s.reset();
  CHECK_FALSE(s.at(kT0 + 400 * kMs));
}

TEST_CASE("smooth: the animation is the sender's at the shown time, gait from its speed") {
  PoseSmoother s;
  for (std::uint32_t i = 0; i <= 10; ++i) s.push(running(i));
  auto p = s.at(kT0 + 100 * kMs + 50 * kMs);  // shown at 50 ms: sample 3 (48 ms)
  REQUIRE(p);
  REQUIRE(p->animation);
  CHECK(p->animation->name == "Move");
  CHECK(p->animation_sequence == 3);
  CHECK(p->animation->frame == 3.0f);
  CHECK(p->gait == Gait::run);  // Move at 6 u/s as sent
  // Held at the end of the samples: still the sender's run, though the puppet stands.
  p = s.at(kT0 + 100 * kMs + 900 * kMs);
  CHECK(p->how == SmoothHow::held);
  CHECK(p->gait == Gait::run);
  REQUIRE(p->sender_speed);
  CHECK(near(*p->sender_speed, kSpeed));
}

TEST_CASE("smooth: gait words and speeds") {
  CHECK(gait_of("Move", 6.0f) == Gait::run);
  CHECK(gait_of("Move", 1.5f) == Gait::walk);
  CHECK(gait_of("Move", 0.0f) == Gait::still);
  CHECK(gait_of("Move", std::nullopt) == Gait::unknown);
  CHECK(gait_of("RunStart_L", 0.0f) == Gait::run);  // the name wins over the speed
  CHECK(gait_of("Walk", 9.0f) == Gait::walk);
  CHECK(gait_of("Wait_Normal", 0.5f) == Gait::still);
  CHECK(gait_of("SwordAttack_1", 3.0f) == Gait::other);
  CHECK(gait_of("Return", 0.0f) == Gait::other);  // no "run" word in it
  CHECK(gait_of("", 5.0f) == Gait::run);
  CHECK(horizontal_speed({3.0f, -50.0f, 4.0f}) == 5.0f);  // a fall is not running
  CHECK(std::string(to_string(Gait::walk)) == "walk");
}

TEST_CASE("smooth: the AS fallback plays again when the rate changes") {
  const Animation run{"Move", 10.0f, 1.0f};
  CHECK(next_anim_step(std::nullopt, run, std::nullopt, true) == AnimStep::play);
  CHECK(next_anim_step(AnimPlayed{"Wait", 1.0f}, run, 10.0f, false) == AnimStep::play);
  CHECK(next_anim_step(AnimPlayed{"Move", 1.0f}, run, 10.5f, true) == AnimStep::none);
  CHECK(next_anim_step(AnimPlayed{"Move", 1.0f}, run, 20.0f, true) == AnimStep::set_frame);
  CHECK(next_anim_step(AnimPlayed{"Move", 1.0f}, run, 20.0f, false) == AnimStep::none);
  CHECK(next_anim_step(AnimPlayed{"Move", 0.6f}, run, 10.0f, false) == AnimStep::play);
  const Animation nan_rate{"Move", 10.0f, std::nanf("")};
  CHECK(next_anim_step(AnimPlayed{"Move", 1.0f}, nan_rate, 10.0f, false) == AnimStep::none);
}

TEST_CASE("smooth: the timeline orders by time and joins neighbours") {
  Timeline<int> t(4);
  CHECK_FALSE(t.at(0, 1000));
  CHECK(t.push({200, 2}, 20) == Timeline<int>::Push::buffered);
  CHECK(t.push({100, 1}, 10) == Timeline<int>::Push::buffered);  // late, put first
  CHECK(t.push({100, 1}, 99) == Timeline<int>::Push::duplicate);
  auto v = t.at(150, 1000);
  REQUIRE(v);
  CHECK(*v->a == 10);
  REQUIRE(v->b);
  CHECK(*v->b == 20);
  CHECK(near(v->t, 0.5f));
  v = t.at(50, 1000);
  CHECK(v->early);
  CHECK(*v->a == 10);
  v = t.at(260, 1000);
  CHECK(*v->a == 20);
  CHECK(v->b == nullptr);
  CHECK(v->ahead_us == 60);
  // Too far apart: not joined.
  t.push({5000, 3}, 30);
  v = t.at(1000, 1000);
  CHECK(*v->a == 20);
  CHECK(v->b == nullptr);
  // Full: the oldest goes, and anything older than it is refused.
  t.push({6000, 4}, 40);
  t.push({7000, 5}, 50);
  CHECK(t.size() == 4);
  CHECK(t.push({10, 6}, 1) == Timeline<int>::Push::too_old);
  CHECK(t.newest() == 50);
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): the delay is
// measured from each player's windows, not fixed at 100 ms.
TEST_CASE("delay tuner: late frames raise the delay for everyone, calm windows lower it, quiet senders say nothing") {
  using coop::host::DelayTuner;
  DelayTuner t;
  CHECK(t.delay_us() == 100'000);
  coop::host::PoseSmoother::Stats w;
  w.frames = 600;
  w.buffered = 600;
  w.extrapolated = 3;
  auto v = t.feed(w);  // 0.5 %: kept, calm
  CHECK(v.change == DelayTuner::Change::kept);
  CHECK(v.delay_us == 100'000);
  CHECK(v.share == doctest::Approx(0.005));
  // 8 % of the frames past the newest sample: up 50 ms.
  w.extrapolated = 30;
  w.held = 18;
  v = t.feed(w);
  CHECK(v.change == DelayTuner::Change::up);
  CHECK(v.delay_us == 150'000);
  v = t.feed(w);
  CHECK(v.delay_us == 200'000);
  v = t.feed(w);  // the top: kept there
  CHECK(v.change == DelayTuner::Change::kept);
  CHECK(v.delay_us == 200'000);
  // A sender who was loading (few samples for the frames): his silence, not the network.
  coop::host::PoseSmoother::Stats quiet;
  quiet.frames = 600;
  quiet.buffered = 100;
  quiet.held = 500;
  CHECK(t.feed(quiet).change == DelayTuner::Change::none);
  CHECK(t.delay_us() == 200'000);
  // Too short a window says nothing either.
  coop::host::PoseSmoother::Stats short_window;
  short_window.frames = 10;
  short_window.buffered = 10;
  short_window.held = 10;
  CHECK(t.feed(short_window).change == DelayTuner::Change::none);
  // Six calm windows in a row: down 50 ms; a late one between starts the count again.
  w.extrapolated = 0;
  w.held = 0;
  for (int k = 0; k < 5; ++k) {
    CHECK(t.feed(w).change == DelayTuner::Change::kept);
  }
  coop::host::PoseSmoother::Stats middling = w;
  middling.extrapolated = 18;  // 3 %: neither late nor calm
  CHECK(t.feed(middling).change == DelayTuner::Change::kept);
  for (int k = 0; k < 5; ++k) {
    CHECK(t.feed(w).change == DelayTuner::Change::kept);
  }
  v = t.feed(w);
  CHECK(v.change == DelayTuner::Change::down);
  CHECK(v.delay_us == 150'000);
  for (int k = 0; k < 12; ++k) {
    t.feed(w);
  }
  CHECK(t.delay_us() == 100'000);  // never under the least
}

TEST_CASE("delay tuner: a smoother told a longer delay draws further behind") {
  coop::host::PoseSmoother s;
  s.set_delay_us(150'000);
  CHECK(s.settings().delay_us == 150'000);
}
