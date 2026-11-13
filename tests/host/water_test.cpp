// ORM water levels (coop/host/water.hpp, oot-remake docs/AGUA.md): the table against the
// names, the shared list, the no-void-out hold, the one-hot Bools, a late joiner.
// The game is a fake flag store, as in flag_watcher_test.cpp.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/water.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

const FlagKey kLevel{0xbeb09982u, 0};  // WaterTemple_Room00_WaterLevel
const FlagKey kC01{0xa16f6696u, 0};
const FlagKey kC02{0x2a8640b7u, 0};
const FlagKey kC03{0xa4456820u, 0};
const FlagKey kSkip{0xd6991798u, 0};
const FlagKey kDeku{0x392a228au, 0};   // InsideTheDekuTree_RoomG_WaterLevel_timing
const FlagKey kJabu{0xc517f1b5u, 0};   // Gimmick_Insidejabujabu_Updown_MoveWater
constexpr FlagKey kOther{0x1234u, 0};  // not water

proto::FlagEntry boolean(FlagKey key, std::uint32_t v) { return {key, proto::FlagKind::boolean, v}; }
proto::FlagEntry integer(FlagKey key, std::int32_t v) {
  return {key, proto::FlagKind::int32, static_cast<std::uint32_t>(v)};
}

class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  std::set<FlagKey> unwritable;
  int writes = 0;
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    if (unwritable.contains(e.key) || !values.contains(e.key)) return false;
    ++writes;
    values[e.key] = e;
    return true;
  }
  std::int32_t level() const { return static_cast<std::int32_t>(values.at(kLevel).value); }
  std::uint32_t get(FlagKey k) const { return values.at(k).value; }
};

// A Water Temple save as the game starts it: level 2 (high), Change03 on.
FakeStore temple(std::int32_t level = 2) {
  FakeStore s;
  s.values[kLevel] = integer(kLevel, level);
  s.values[kC01] = boolean(kC01, level == 0);
  s.values[kC02] = boolean(kC02, level == 1);
  s.values[kC03] = boolean(kC03, level == 2);
  s.values[kSkip] = boolean(kSkip, 0);
  s.values[kDeku] = boolean(kDeku, 0);
  s.values[kJabu] = boolean(kJabu, 0);
  s.values[kOther] = boolean(kOther, 0);
  return s;
}

// Our Link in `scene` at height `y`, playing `anim`.
WaterView at(std::uint64_t now_us, std::string scene = "WaterTemple", float y = 120.0f,
             std::string anim = "Wait", bool event = false) {
  WaterView v;
  v.link = true;
  v.scene = std::move(scene);
  v.position = proto::Vec3{10.0f, y, -4.0f};
  v.animation = std::move(anim);
  v.event_running = event;
  v.now_us = now_us;
  return v;
}

WaterView loading(std::uint64_t now_us) {
  WaterView v;
  v.now_us = now_us;
  return v;
}

bool has_note(const std::vector<std::string>& notes, std::string_view part) {
  return std::any_of(notes.begin(), notes.end(), [&](const std::string& n) { return n.find(part) != n.npos; });
}

// The guard stands still `frames` frames of 1/60 s from `t`; returns the time after.
std::uint64_t stand(WaterGuard& g, std::uint64_t t, int frames, std::string scene = "WaterTemple",
                    std::string anim = "Wait") {
  for (int i = 0; i < frames; ++i) {
    t += 16'667;
    g.observe(at(t, scene, 120.0f, anim));
  }
  return t;
}

}  // namespace

TEST_CASE("water: the table's hashes are the names' (Murmur3), one-hot flags in level order") {
  REQUIRE(water_bodies().size() >= 7);
  std::set<std::uint32_t> seen;
  for (const WaterBody& b : water_bodies()) {
    CAPTURE(b.flag);
    CHECK(flag_hash(b.flag) == b.hash);
    CHECK(seen.insert(b.hash).second);
    CHECK(water_body_of(b.hash) == &b);
    CHECK_FALSE(b.scene.empty());
    for (std::size_t i = 0; i < b.one_hot.size(); ++i) {
      if (!b.one_hot[i].empty()) CHECK(flag_hash(b.one_hot[i]) == b.one_hot_hash[i]);
    }
    if (!b.is_int) CHECK(b.one_hot[0].empty());
  }
  for (const WaterFlag& f : water_unshared()) CHECK(flag_hash(f.name) == f.hash);
  const WaterBody* t = water_body_of(kLevel.hash);
  REQUIRE(t != nullptr);
  CHECK(t->is_int);
  CHECK(t->lowest == 0);
  CHECK(t->highest == 2);  // Dm_WaterTemple_0020 writes 0, 0021 1, 0022 2 (AGUA.md)
  CHECK(t->one_hot_hash[0] == kC01.hash);
  CHECK(t->one_hot_hash[2] == kC03.hash);
  CHECK(water_body_of(kOther.hash) == nullptr);
  CHECK(water_body_of(kJabu.hash)->opt_in);
}

TEST_CASE("water: the shared list gets the levels, loses the one-hot flags and the skip") {
  std::optional<SharedFlags> none;
  CHECK(merge_water(none, true).added == 0);
  CHECK_FALSE(none);  // a list never turns sharing on by itself

  EnumFlags enums;
  REQUIRE(enums.add(0x77u, {1u, 2u}));
  std::optional<SharedFlags> shared = SharedFlags({kLevel, kC01, kC02, kC03, kSkip, kOther}, enums);
  const auto r = merge_water(shared, false);
  CHECK(r.removed == 4);
  CHECK(shared->contains(kLevel));
  CHECK(shared->contains(kDeku));
  CHECK(shared->contains(kOther));
  CHECK_FALSE(shared->contains(kC01));
  CHECK_FALSE(shared->contains(kC03));
  CHECK_FALSE(shared->contains(kSkip));
  CHECK_FALSE(shared->contains(kJabu));  // opt-in
  CHECK(shared->enums().contains(0x77u));
  CHECK(r.added == water_bodies().size() - 1 - 2);  // all but the level (there) and Jabu's two

  CHECK(merge_water(shared, true).added == 2);
  CHECK(shared->contains(kJabu));
  CHECK(merge_water(shared, true).added == 0);  // twice is once
}

TEST_CASE("water: in the water or off his feet") {
  for (const char* a : {"SwimWait", "Swim_Dive_Bottom", "SwimDive", "WaterDive_Fall", "Fall_Danger", "Jump",
                        "ClimbWait", "LadderUp", "Float_N", "Hang"}) {
    CAPTURE(a);
    CHECK(water_unsafe_animation(a));
  }
  for (const char* a : {"Wait", "Walk", "Run", "Land", "WaitWater", "", "OcarinaPlay"}) {
    CAPTURE(a);
    CHECK_FALSE(water_unsafe_animation(a));
  }
}

TEST_CASE("water: a level waits while our Link swims in that scene, written once he stands") {
  WaterGuard g;
  std::uint64_t t = 1'000'000;
  t = stand(g, t, 30);
  CHECK(g.steady());
  CHECK_FALSE(g.holds(kLevel));  // standing: at once
  CHECK_FALSE(g.holds(kOther));

  t = stand(g, t, 2, "WaterTemple", "SwimWait");
  CHECK(g.holds(kLevel));
  CHECK(g.holds(kDeku) == false);  // the Deku Tree's water: not our scene
  CHECK(*g.unsafe_reason() == "our Link plays 'SwimWait'");
  auto notes = g.take_notes();
  CHECK(has_note(notes, "WaterTemple_Room00_WaterLevel (the Water Temple's level"));
  CHECK(has_note(notes, "waits: our Link plays 'SwimWait'"));

  // Out of the water onto the floor: not yet steady for one frame, then written.
  g.observe(at(t += 16'667, "WaterTemple", 120.0f, "Swim_Climb_0cm"));
  CHECK(g.holds(kLevel));
  t = stand(g, t, 25);
  CHECK_FALSE(g.holds(kLevel));
  notes = g.take_notes();
  CHECK(has_note(notes, "waited"));
  CHECK(has_note(notes, "written, our Link stands"));
  CHECK_FALSE(g.holds(kLevel));
  CHECK(g.take_notes().empty());  // one line per wait
}

TEST_CASE("water: an event, a fall or a climb hold it too, loading and other scenes do not") {
  WaterGuard g;
  std::uint64_t t = stand(g, 0, 30);
  g.observe(at(t += 16'667, "WaterTemple", 120.0f, "Wait", true));
  CHECK(g.holds(kLevel));
  CHECK(*g.unsafe_reason() == "an event runs here");

  // Falling 1 m in a frame: moving up or down, even with a standing pose.
  t = stand(g, t, 30);
  g.observe(at(t += 16'667, "WaterTemple", 119.0f, "Wait"));
  CHECK_FALSE(g.steady());
  CHECK(g.holds(kLevel));
  // Walking down a slope at 1 m/s is steady.
  WaterGuard slope;
  float y = 120.0f;
  std::uint64_t s = 0;
  for (int i = 0; i < 40; ++i) {
    y -= 1.0f / 60.0f;
    slope.observe(at(s += 16'667, "WaterTemple", y, "Walk"));
  }
  CHECK(slope.steady());
  CHECK_FALSE(slope.holds(kLevel));

  // Loading: written, the room loads with it.
  g.observe(loading(t += 16'667));
  CHECK_FALSE(g.holds(kLevel));
  CHECK_FALSE(g.unsafe_reason());
  // In Lake Hylia (another scene), swimming: written, the temple loads with it.
  t = stand(g, t, 2, "HyruleField_Adult", "SwimWait");
  CHECK_FALSE(g.holds(kLevel));
  // The Deku Tree's water while we swim in the Deku Tree: held.
  t = stand(g, t, 2, "InsideTheDekuTree", "SwimWait");
  CHECK(g.holds(kDeku));
}

TEST_CASE("water: at most max_wait, then written anyway with a line, 0 waits for ever") {
  WaterGuard g(5'000'000);
  std::uint64_t t = stand(g, 0, 2, "WaterTemple", "SwimWait");
  CHECK(g.holds(kLevel));
  for (int i = 0; i < 290; ++i) {  // asked each frame, as the watcher's retry does
    t = stand(g, t, 1, "WaterTemple", "SwimWait");
    CHECK(g.holds(kLevel));
  }
  t = stand(g, t, 15, "WaterTemple", "SwimWait");
  CHECK_FALSE(g.holds(kLevel));
  CHECK(has_note(g.take_notes(), "s: written although our Link plays 'SwimWait'"));
  CHECK_FALSE(g.holds(kLevel));
  // Not asked for a second: the next change waits afresh.
  t = stand(g, t, 70, "WaterTemple", "SwimWait");
  CHECK(g.holds(kLevel));

  WaterGuard forever(0);
  std::uint64_t u = stand(forever, 0, 2, "WaterTemple", "SwimWait");
  for (int i = 0; i < 3600; ++i) {
    u = stand(forever, u, 1, "WaterTemple", "SwimWait");
    REQUIRE(forever.holds(kLevel));
  }
}

TEST_CASE("water: Jabu-Jabu's water only when asked") {
  WaterGuard off;
  stand(off, 0, 2, "InsideJabuJabuBelly", "SwimWait");
  CHECK_FALSE(off.holds(kJabu));
  WaterGuard on(WaterGuard::kDefaultMaxWaitUs, true);
  stand(on, 0, 2, "InsideJabuJabuBelly", "SwimWait");
  CHECK(on.holds(kJabu));
}

TEST_CASE("water: the one-hot flags follow the level, not while an event runs") {
  WaterGuard g;
  FakeStore s = temple(2);
  stand(g, 0, 20);
  CHECK(g.derive(s) == 0);  // the save's own state is one level
  CHECK(g.take_notes().empty());

  // Another player lowered it: the Int came, the Bools are ours to set.
  s.values[kLevel] = integer(kLevel, 0);
  CHECK(g.derive(s) == 2);
  CHECK(s.get(kC01) == 1);
  CHECK(s.get(kC02) == 0);
  CHECK(s.get(kC03) == 0);
  CHECK(has_note(g.take_notes(), "WaterTemple_Room00_WaterLevel is 0: WaterTemple_WaterLevelChange01 1, "
                                 "WaterTemple_WaterLevelChange03 0"));
  CHECK(g.derive(s) == 0);

  // Two levels at once (A's Bools, B's Int): one level again.
  s.values[kLevel] = integer(kLevel, 1);
  s.values[kC01] = boolean(kC01, 1);
  s.values[kC02] = boolean(kC02, 1);
  CHECK(g.derive(s) == 1);
  CHECK(s.get(kC01) == 0);
  CHECK(s.get(kC02) == 1);

  // Our own level event: it writes the Bools itself, a few frames after the Int.
  s.values[kLevel] = integer(kLevel, 2);
  g.observe(at(1'000'000, "WaterTemple", 120.0f, "Wait", true));
  CHECK(g.derive(s) == 0);
  CHECK(s.get(kC02) == 1);

  // A value that is no level: left alone, said once.
  g.observe(at(1'100'000));
  s.values[kLevel] = integer(kLevel, 7);
  CHECK(g.derive(s) == 0);
  CHECK(g.derive(s) == 0);
  const auto notes = g.take_notes();
  CHECK(std::count_if(notes.begin(), notes.end(), [](const std::string& n) { return n.find("not a level") != n.npos; }) == 1);

  // A Bool that cannot be written: tried again, said once.
  s.values[kLevel] = integer(kLevel, 0);
  s.unwritable.insert(kC01);
  CHECK(g.derive(s) == 1);  // Change02 down; Change01 refused
  CHECK(g.derive(s) == 0);
  CHECK(s.get(kC01) == 0);
  CHECK(has_note(g.take_notes(), "could not be written"));
  s.unwritable.clear();
  CHECK(g.derive(s) == 1);
}

TEST_CASE("water: the received line says live or on the next load") {
  WaterGuard g;
  stand(g, 0, 20);
  g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
  stand(g, 400'000, 2, "HyruleField_Adult");
  g.received(boolean(kDeku, 0), boolean(kDeku, 1), 3, false);
  g.received(integer(kLevel, 0), integer(kLevel, 1), 0, true);
  g.received(boolean(kOther, 0), boolean(kOther, 1), 2, false);
  const auto notes = g.take_notes();
  REQUIRE(notes.size() == 4);
  CHECK(notes[0] == "water: WaterTemple_Room00_WaterLevel 2 -> 0 (P1): written in WaterTemple, live (its mover reads the "
                   "flag): watching its water planes 10.0 s");
  // Left before its water moved: no live verdict (docs/RIESGOS_RESUELTOS.md).
  CHECK(notes[1] == "water: WaterTemple_Room00_WaterLevel: live not told: our Link left WaterTemple before its water "
                   "moved; WaterTemple loads with it");
  CHECK(notes[2] == "water: InsideTheDekuTree_RoomG_WaterLevel_timing 0 -> 1 (P3): InsideTheDekuTree loads with it (we are in HyruleField_Adult)");
  CHECK(notes[3].find("0 -> 1 joining: WaterTemple loads with it") != std::string::npos);
}

namespace {

// The hook's chain, cut down: watcher -> hold (the water rule) -> the game.
struct Player {
  FakeStore game = temple(2);
  WaterGuard guard;
  FlagWatcher watcher;
  explicit Player(SharedFlags shared) : watcher(std::move(shared), FlagRole::player) {}
  void frame(const WaterView& view) {
    guard.observe(view);
    watcher.set_loading(!view.link);
    HoldFlagStore hold(game, [this](FlagKey k) { return guard.holds(k); });
    watcher.frame(hold);
    for (const auto& r : watcher.take_received()) {
      guard.received(r.before, r.after, r.from, r.kind == FlagWatcher::ReceivedKind::join);
    }
    guard.derive(game);
  }
};

SharedFlags water_list() {
  std::optional<SharedFlags> s = SharedFlags({kLevel, kC01, kC02, kC03, kSkip, kDeku});
  merge_water(s, false);
  return *s;
}

}  // namespace

TEST_CASE("water: another player's level reaches a swimming Link only when he stands (watcher)") {
  Player b(water_list());
  std::uint64_t t = 0;
  for (int i = 0; i < 5; ++i) b.frame(at(t += 16'667, "WaterTemple", 120.0f, "SwimWait"));
  b.watcher.receive(proto::FlagState{1, {integer(kLevel, 0)}});
  for (int i = 0; i < 60; ++i) b.frame(at(t += 16'667, "WaterTemple", 120.0f, "SwimWait"));
  CHECK(b.game.level() == 2);  // still high under the swimmer
  CHECK(b.game.get(kC03) == 1);
  CHECK(has_note(b.guard.take_notes(), "waits: our Link plays 'SwimWait'"));
  // He climbs out and stands: the level and its flags.
  for (int i = 0; i < 30; ++i) b.frame(at(t += 16'667, "WaterTemple", 121.0f, "Wait"));
  CHECK(b.game.level() == 0);
  CHECK(b.game.get(kC01) == 1);
  CHECK(b.game.get(kC03) == 0);
  const auto notes = b.guard.take_notes();
  CHECK(has_note(notes, "written, our Link stands"));
  // (A held value written later is no received line: the wait's own line says it.)
  CHECK(has_note(notes, "WaterTemple_Room00_WaterLevel is 0: WaterTemple_WaterLevelChange01 1"));
  // The derived flags are not shared: nothing goes out for them.
  CHECK_FALSE(b.watcher.shared().contains(kC01));
}

TEST_CASE("water: a standing Link gets it at once, with its line, out of the scene too") {
  Player b(water_list());
  std::uint64_t t = 0;
  for (int i = 0; i < 30; ++i) b.frame(at(t += 16'667));
  b.watcher.receive(proto::FlagState{2, {integer(kLevel, 1)}});
  b.frame(at(t += 16'667));
  CHECK(b.game.level() == 1);
  CHECK(b.game.get(kC02) == 1);
  CHECK(b.game.get(kC03) == 0);
  CHECK(has_note(b.guard.take_notes(), "2 -> 1 (P2): written in WaterTemple, live"));

  Player lake(water_list());
  for (int i = 0; i < 5; ++i) lake.frame(at(t += 16'667, "HyruleField_Adult", 90.0f, "SwimWait"));
  lake.watcher.receive(proto::FlagState{2, {integer(kLevel, 0)}});
  lake.frame(at(t += 16'667, "HyruleField_Adult", 90.0f, "SwimWait"));
  CHECK(lake.game.level() == 0);
  CHECK(has_note(lake.guard.take_notes(), "WaterTemple loads with it (we are in HyruleField_Adult)"));
}

TEST_CASE("water: mutant: without the water rule the swimmer gets it at once") {
  Player b(water_list());
  std::uint64_t t = 0;
  for (int i = 0; i < 5; ++i) b.frame(at(t += 16'667, "WaterTemple", 120.0f, "SwimWait"));
  b.watcher.receive(proto::FlagState{1, {integer(kLevel, 0)}});
  // The plain chain (no water rule): the level moves under the swimming Link.
  b.watcher.frame(b.game);
  CHECK(b.game.level() == 0);
}

TEST_CASE("water: a late joiner gets the current level from the snapshot, even swimming after the load") {
  Player j(water_list());
  std::uint64_t t = 0;
  // Joining: the title screen, then the save loads (level 2 in the save).
  for (int i = 0; i < 10; ++i) j.frame(loading(t += 16'667));
  j.watcher.receive(proto::FlagSnapshot{0, 1, {integer(kLevel, 1), boolean(kDeku, 1)}});
  j.frame(loading(t += 16'667));
  CHECK(j.game.level() == 1);
  CHECK(j.game.get(kDeku) == 1);
  CHECK(j.game.get(kC02) == 1);  // derived while loading: the room's planes load right
  CHECK(j.game.get(kC03) == 0);
  // In the temple, swimming at the entrance: the joined level stays.
  for (int i = 0; i < 60; ++i) j.frame(at(t += 16'667, "WaterTemple", 100.0f, "SwimWait"));
  CHECK(j.game.level() == 1);
  CHECK(j.watcher.pending() == 0);  // nothing of ours to send
}

// docs/RIESGOS_RESUELTOS.md ("Mundo e historia"): does the mover re-read the level live?
namespace {

WaterView with_planes(std::uint64_t now_us, std::vector<float> planes, std::string scene = "WaterTemple") {
  WaterView v = at(now_us, std::move(scene));
  v.planes = std::move(planes);
  return v;
}

}  // namespace

TEST_CASE("water: live: a water plane that moves after the write says so, and the next line knows") {
  WaterGuard g;
  std::uint64_t t = 0;
  for (int i = 0; i < 20; ++i) g.observe(with_planes(t += 16'667, {-20.0f, 4.0f}));
  g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
  (void)g.take_notes();
  // The plane settles over two seconds.
  for (int i = 0; i < 30; ++i) g.observe(with_planes(t += 16'667, {-20.0f, 4.0f}));
  g.observe(with_planes(t += 16'667, {-20.0f, 3.5f}));
  auto notes = g.take_notes();
  CHECK(has_note(notes, "water: WaterTemple_Room00_WaterLevel: live: a water plane moved 0.50 m"));
  CHECK(g.live(kLevel.hash) == WaterLive::live);
  g.received(integer(kLevel, 0), integer(kLevel, 1), 2, false);
  CHECK(has_note(g.take_notes(), "live (its mover reads the flag, seen before)"));
  CHECK(g.waiting_for_load() == 0);
}

TEST_CASE("water: not live: no plane moves in 10 s, applied on the scene's next load") {
  WaterGuard g;
  std::uint64_t t = 0;
  for (int i = 0; i < 20; ++i) g.observe(with_planes(t += 16'667, {-20.0f, 4.0f}));
  g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
  (void)g.take_notes();
  for (int i = 0; i < 11 * 60; ++i) g.observe(with_planes(t += 16'667, {-20.0f, 4.02f}));  // a ripple
  auto notes = g.take_notes();
  CHECK(has_note(notes, "water: WaterTemple_Room00_WaterLevel: not live: no water plane moved in 10.0 s; "
                        "WaterTemple applies it on its next load"));
  CHECK(g.live(kLevel.hash) == WaterLive::at_load);
  CHECK(g.waiting_for_load() == 1);
  // Not before a load: walking on in the same scene says nothing.
  for (int i = 0; i < 60; ++i) g.observe(with_planes(t += 16'667, {-20.0f, 4.0f}));
  CHECK_FALSE(has_note(g.take_notes(), "applied on load"));
  // A load (a door of the temple), then our Link in it again.
  for (int i = 0; i < 30; ++i) g.observe(loading(t += 16'667));
  g.observe(with_planes(t += 16'667, {-28.0f}));
  CHECK(has_note(g.take_notes(), "water: WaterTemple_Room00_WaterLevel: applied on load: WaterTemple loaded with 0"));
  CHECK(g.waiting_for_load() == 0);
  // The next level: it says at once that it shows at the next load.
  g.received(integer(kLevel, 0), integer(kLevel, 2), 1, false);
  CHECK(has_note(g.take_notes(), "its mover reads it at load (seen before): applied when WaterTemple loads again"));
  CHECK(g.waiting_for_load() == 1);
}

// ORM (oot-remake docs/SONDAS.md, 2026-11-12, meca-agua-templo): a level that waited under a swimmer is
// written by the watcher's retry, which records no received line; the wait's release arms the check.
TEST_CASE("water: a level written after a wait is watched live too (the release arms it)") {
  Player b(water_list());
  std::uint64_t t = 0;
  const auto view = [&](std::string anim, std::vector<float> planes, float y = 120.0f) {
    WaterView v = at(t += 16'667, "WaterTemple", y, std::move(anim));
    v.planes = std::move(planes);
    return v;
  };
  for (int i = 0; i < 5; ++i) b.frame(view("SwimWait", {-20.0f, 4.0f}));
  b.watcher.receive(proto::FlagState{1, {integer(kLevel, 0)}});
  for (int i = 0; i < 30; ++i) b.frame(view("SwimWait", {-20.0f, 4.0f}));
  CHECK(b.game.level() == 2);
  (void)b.guard.take_notes();
  for (int i = 0; i < 30; ++i) b.frame(view("Wait", {-20.0f, 4.0f}, 121.0f));
  CHECK(b.game.level() == 0);
  auto notes = b.guard.take_notes();
  CHECK(has_note(notes, "written, our Link stands"));
  CHECK(has_note(notes, "WaterTemple_Room00_WaterLevel: written in WaterTemple after the wait, live (its mover "
                        "reads the flag): watching its water planes 10.0 s"));
  b.frame(view("Wait", {-20.0f, 3.4f}, 121.0f));
  notes = b.guard.take_notes();
  CHECK(has_note(notes, "water: WaterTemple_Room00_WaterLevel: live: a water plane moved 0.60 m"));
  CHECK(b.guard.live(kLevel.hash) == WaterLive::live);
}

TEST_CASE("water: after a wait, no plane moving says not live, and the load says the level read") {
  Player b(water_list());
  std::uint64_t t = 0;
  const auto view = [&](std::string anim, std::vector<float> planes) {
    WaterView v = at(t += 16'667, "WaterTemple", 120.0f, std::move(anim));
    v.planes = std::move(planes);
    return v;
  };
  for (int i = 0; i < 5; ++i) b.frame(view("SwimWait", {-20.0f}));
  b.watcher.receive(proto::FlagState{1, {integer(kLevel, 1)}});
  for (int i = 0; i < 30; ++i) b.frame(view("SwimWait", {-20.0f}));
  for (int i = 0; i < 11 * 60; ++i) b.frame(view("Wait", {-20.0f}));
  auto notes = b.guard.take_notes();
  CHECK(has_note(notes, "water: WaterTemple_Room00_WaterLevel: not live: no water plane moved in 10.0 s"));
  CHECK(b.guard.waiting_for_load() == 1);
  for (int i = 0; i < 30; ++i) b.frame(loading(t += 16'667));
  b.frame(view("Wait", {-24.0f}));
  CHECK(has_note(b.guard.take_notes(), "applied on load: WaterTemple loaded with 1"));
}

TEST_CASE("water: live unknown without planes, a plane appearing counts, leaving ends the watch") {
  SUBCASE("no plane read (objects walk off)") {
    WaterGuard g;
    std::uint64_t t = stand(g, 0, 20);
    g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
    (void)g.take_notes();
    stand(g, t, 11 * 60);
    CHECK(has_note(g.take_notes(), "water: WaterTemple_Room00_WaterLevel: live unknown: no water plane read in 10.0 s"));
    CHECK(g.live(kLevel.hash) == WaterLive::unknown);
  }
  SUBCASE("a side plane shows up (its one-hot flag)") {
    WaterGuard g;
    std::uint64_t t = 0;
    for (int i = 0; i < 20; ++i) g.observe(with_planes(t += 16'667, {4.0f}));
    g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
    (void)g.take_notes();
    g.observe(with_planes(t += 16'667, {-20.0f, 4.0f}));
    CHECK(has_note(g.take_notes(), "live: the water planes changed (1 -> 2)"));
  }
  SUBCASE("our Link leaves before it moves: no verdict, the scene loads with it") {
    WaterGuard g;
    std::uint64_t t = 0;
    for (int i = 0; i < 20; ++i) g.observe(with_planes(t += 16'667, {4.0f}));
    g.received(integer(kLevel, 2), integer(kLevel, 0), 1, false);
    (void)g.take_notes();
    g.observe(loading(t += 16'667));
    CHECK(has_note(g.take_notes(), "live not told: we loaded before its water moved; WaterTemple loads with it"));
    CHECK(g.live(kLevel.hash) == WaterLive::unknown);
    g.observe(with_planes(t += 16'667, {-20.0f}, "LakeHylia_Adult"));
    CHECK_FALSE(has_note(g.take_notes(), "applied on load"));
    g.observe(with_planes(t += 16'667, {-20.0f}));
    CHECK(has_note(g.take_notes(), "applied on load: WaterTemple loaded with 0"));
  }
  SUBCASE("out of the scene: the line on the next entry") {
    WaterGuard g;
    std::uint64_t t = stand(g, 0, 20, "HyruleField_Adult");
    g.received(boolean(kDeku, 0), boolean(kDeku, 1), 3, false);
    (void)g.take_notes();
    stand(g, t, 2, "InsideTheDekuTree");
    CHECK(has_note(g.take_notes(),
                   "water: InsideTheDekuTree_RoomG_WaterLevel_timing: applied on load: InsideTheDekuTree loaded with 1"));
  }
}

TEST_CASE("water: Jabu-Jabu's water that bounces between games is settled by ours until we leave") {
  WaterGuard g(WaterGuard::kDefaultMaxWaitUs, true);
  std::uint64_t t = stand(g, 0, 20, "InsideJabuJabuBelly");
  const FlagKey jabu = kJabu;
  CHECK_FALSE(g.holds(jabu));
  // Its step switches set it both ways: values from the others three times in a few seconds.
  for (int i = 0; i < 3; ++i) {
    g.received(boolean(jabu, i % 2), boolean(jabu, (i + 1) % 2), 2, false);
    t = stand(g, t, 60, "InsideJabuJabuBelly");
  }
  CHECK(has_note(g.take_notes(), "water: Gimmick_Insidejabujabu_Updown_MoveWater bounced: 3 values from the others "
                                 "in 10.0 s: theirs wait until we leave InsideJabuJabuBelly (ours still go out)"));
  CHECK(g.bounced(jabu.hash));
  CHECK(g.holds(jabu));
  // Out of the scene: the hold ends with a line, the value is written (the room loads with it).
  t = stand(g, t, 2, "ZoraFountain");
  CHECK(has_note(g.take_notes(), "the bounce hold ends (we left InsideJabuJabuBelly)"));
  CHECK_FALSE(g.bounced(jabu.hash));
  CHECK_FALSE(g.holds(jabu));
}

TEST_CASE("water: two values far apart are no bounce, the Water Temple is never held so") {
  WaterGuard g(WaterGuard::kDefaultMaxWaitUs, true);
  std::uint64_t t = stand(g, 0, 20, "InsideJabuJabuBelly");
  for (int i = 0; i < 3; ++i) {
    g.received(boolean(kJabu, i % 2), boolean(kJabu, (i + 1) % 2), 2, false);
    t = stand(g, t, 6 * 60, "InsideJabuJabuBelly");  // 6 s apart: two in any 10 s
  }
  CHECK_FALSE(g.bounced(kJabu.hash));
  WaterGuard temple_guard;
  t = stand(temple_guard, 0, 20);
  for (int i = 0; i < 4; ++i) {
    temple_guard.received(integer(kLevel, i % 3), integer(kLevel, (i + 1) % 3), 2, false);
    t = stand(temple_guard, t, 30);
  }
  CHECK_FALSE(temple_guard.bounced(kLevel.hash));
}

TEST_CASE("water: walking on the bottom with the iron boots counts as standing") {
  CHECK_FALSE(water_unsafe_animation("Heavy_Swim_Wait"));
  CHECK_FALSE(water_unsafe_animation("Heavy_Swim_Walk"));
  WaterGuard g;
  stand(g, 0, 30, "WaterTemple", "Heavy_Swim_Walk");
  CHECK_FALSE(g.unsafe_reason().has_value());
  CHECK_FALSE(g.holds(kLevel));
}

// RIESGOS_RESUELTOS.md: the probe that finds a place in the water for meca-agua-nado.
TEST_CASE("water: probe: the first place our Link swims in a water scene is logged once a visit") {
  WaterGuard g;
  std::uint64_t t = stand(g, 0, 5);
  CHECK_FALSE(has_note(g.take_notes(), "probe"));
  t = stand(g, t, 3, "WaterTemple", "SwimWait");
  auto notes = g.take_notes();
  CHECK(has_note(notes, "water: probe: our Link swims ('SwimWait') at 10.00 120.00 -4.00 in WaterTemple"));
  CHECK(has_note(notes, "meca-agua-nado: place 10.00 120.00 -4.00"));
  t = stand(g, t, 3, "WaterTemple", "SwimWait");
  CHECK_FALSE(has_note(g.take_notes(), "probe"));  // once a visit
  t = stand(g, t, 3, "HyruleField_Adult", "SwimWait");  // no water of ours: nothing
  CHECK_FALSE(has_note(g.take_notes(), "probe"));
  stand(g, t, 3, "WaterTemple", "Swim_Front");  // back: a new visit
  CHECK(has_note(g.take_notes(), "probe: our Link swims ('Swim_Front')"));
}
