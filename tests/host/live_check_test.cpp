// Tests for the live check (coop/host/live_check.hpp; docs/RIESGOS_RESUELTOS.md "Mundo e
// historia", docs/CANCIONES.md "Por ver" 1 and 2): a key that moves its actor at once is
// "live", one that does not is "not live" (it changes on the next load), a key changed
// elsewhere is noted for the next load, and actors that go with no listed key are
// "unexplained" (the list misses that key).
// Mutants these catch: no answer window (every change "not live" at once): "live within the
// window"; the cause ignored: "no Ocarina, nothing unexplained"; the steady count dropped
// (a walk mid-load taken as a change): "a count that flickers is no change".
#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "coop/host/live_check.hpp"
#include "coop/host/songs.hpp"

namespace {

using coop::host::FlagKey;
using coop::host::LiveCheck;
using coop::host::LiveView;
using coop::host::LiveWatch;
using coop::host::SeenActor;
namespace protocol = coop::protocol;

// Our game's flags: a map (as the game's table would answer).
struct MapStore final : coop::host::FlagStore {
  std::map<FlagKey, std::uint32_t> values;
  std::optional<protocol::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return protocol::FlagEntry{key, protocol::FlagKind::boolean, it->second};
  }
  bool write(const protocol::FlagEntry& e) override {
    values[e.key] = e.value;
    return true;
  }
};

constexpr std::uint64_t kS = 1'000'000;
const FlagKey kKey = FlagKey::key64_flag(0x0dbe35edd0ba3c92ull);

LiveWatch forest() {
  LiveWatch w;
  w.id = "song of time ForestTemple";
  w.place = "ForestTemple";
  w.actor = "SongOfTimeBlock";
  w.keys = {kKey};
  w.cause = "Ocarina";
  return w;
}

SeenActor actor(std::string name) {
  SeenActor a;
  a.name = std::move(name);
  return a;
}

std::vector<SeenActor> blocks(int n) {
  std::vector<SeenActor> out;
  out.push_back(actor("Player_Adult"));
  for (int i = 0; i < n; ++i) out.push_back(actor("Obj_SongOfTimeBlock_A_01"));
  out.push_back(actor("Obj_Stepswitch_A_03"));
  return out;
}

LiveView at(std::uint64_t t, std::optional<std::vector<SeenActor>> actors, std::string scene = "ForestTemple",
            std::string anim = "Wait") {
  LiveView v;
  v.scene = std::move(scene);
  v.actors = std::move(actors);
  v.animation = std::move(anim);
  v.now_us = t;
  return v;
}

bool has(const std::vector<std::string>& lines, const std::string& part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

// Runs from t0 to t1 in quarter seconds with the same view, gathering the notes.
void run(LiveCheck& c, MapStore& s, std::uint64_t t0, std::uint64_t t1, std::vector<std::string>& notes,
         const std::function<LiveView(std::uint64_t)>& view) {
  for (std::uint64_t t = t0; t <= t1; t += 250'000) {
    c.tick(view(t), s);
    for (auto& n : c.take_notes()) notes.push_back(std::move(n));
  }
}

}  // namespace

TEST_CASE("live check: a key that moves the block at once is live") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
  CHECK(notes.empty());
  s.values[kKey] = 1;  // another player's song: the watcher wrote it here
  run(c, s, 3 * kS + 250'000, 4 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
  run(c, s, 4 * kS + 250'000, 6 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
  CAPTURE(notes);
  CHECK(has(notes, "song of time ForestTemple: live: 2 -> 1 SongOfTimeBlock actor(s) in ForestTemple"));
  CHECK(has(notes, "after key64:0x0dbe35edd0ba3c92"));
  CHECK(c.verdict(0) == "live");
  CHECK_FALSE(has(notes, "not live"));
}

TEST_CASE("live check: a key the block does not answer is not live, and the next load is noted") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
  s.values[kKey] = 1;
  run(c, s, 3 * kS + 250'000, 9 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
  CAPTURE(notes);
  CHECK(has(notes, "not live: still 2 SongOfTimeBlock actor(s) in ForestTemple 4.0 s after key64:0x0dbe35edd0ba3c92; "
                   "it changes when ForestTemple loads"));
  CHECK(c.verdict(0) == "not live");
  CHECK_FALSE(has(notes, "loaded:"));  // no load yet: still in the same room
  // The room loads again (a door out and back): its count is noted once.
  run(c, s, 9 * kS + 250'000, 10 * kS, notes, [](std::uint64_t t) { return at(t, std::nullopt, ""); });
  run(c, s, 10 * kS + 250'000, 12 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
  CHECK(has(notes, "loaded: ForestTemple came in with 1 SongOfTimeBlock actor(s)"));
}

TEST_CASE("live check: a key changed while we are elsewhere loads with the room") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 2 * kS, notes, [](std::uint64_t t) { return at(t, blocks(0), "SacredForestMeadow_Adult"); });
  s.values[kKey] = 1;
  run(c, s, 2 * kS + 250'000, 3 * kS, notes,
      [](std::uint64_t t) { return at(t, blocks(0), "SacredForestMeadow_Adult"); });
  CHECK(has(notes, "key64:0x0dbe35edd0ba3c92 0 -> 1: ForestTemple loads with it (we are in SacredForestMeadow_Adult)"));
  run(c, s, 3 * kS + 250'000, 5 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
  CHECK(has(notes, "loaded: ForestTemple came in with 1 SongOfTimeBlock actor(s)"));
}

TEST_CASE("live check: no actor walk says unknown, once for the scene and once for the key") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 2 * kS, notes, [](std::uint64_t t) { return at(t, std::nullopt); });
  s.values[kKey] = 1;
  run(c, s, 2 * kS + 250'000, 4 * kS, notes, [](std::uint64_t t) { return at(t, std::nullopt); });
  CHECK(has(notes, "no actor walk in ForestTemple"));
  CHECK(has(notes, "unknown, no actor walk to see it"));
  CHECK(c.verdict(0) == "unknown");
  int walks = 0;
  for (const auto& n : notes) walks += n.find("no actor walk in") != std::string::npos;
  CHECK(walks == 1);
}

TEST_CASE("live check: a block that goes after our Ocarina with no listed key is unexplained") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2), "ForestTemple", "OcarinaPlay"); });
  run(c, s, 3 * kS + 250'000, 10 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
  CAPTURE(notes);
  CHECK(has(notes, "unexplained: 2 -> 1 SongOfTimeBlock actor(s) in ForestTemple with no listed key changing after "
                   "our Ocarina: the list misses its key"));
}

TEST_CASE("live check: no Ocarina, nothing unexplained, a key just after the blocks is live") {
  SUBCASE("no cause") {
    LiveCheck c({forest()});
    MapStore s;
    s.values[kKey] = 0;
    std::vector<std::string> notes;
    run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
    run(c, s, 3 * kS + 250'000, 10 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
    CHECK_FALSE(has(notes, "unexplained"));
  }
  SUBCASE("the key comes 1 s after the blocks went") {
    LiveCheck c({forest()});
    MapStore s;
    s.values[kKey] = 0;
    std::vector<std::string> notes;
    run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2), "ForestTemple", "OcarinaPlay"); });
    run(c, s, 3 * kS + 250'000, 4 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
    s.values[kKey] = 1;
    run(c, s, 4 * kS + 250'000, 10 * kS, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
    CAPTURE(notes);
    CHECK(has(notes, "live: 2 -> 1 SongOfTimeBlock actor(s) in ForestTemple (the actors went first)"));
    CHECK_FALSE(has(notes, "unexplained"));
    CHECK_FALSE(has(notes, "not live"));
  }
}

TEST_CASE("live check: a count that flickers is no change") {
  LiveCheck c({forest()});
  MapStore s;
  s.values[kKey] = 0;
  std::vector<std::string> notes;
  run(c, s, 1 * kS, 3 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2), "ForestTemple", "OcarinaPlay"); });
  // One quarter second with one block (a walk mid-change), then two again.
  run(c, s, 3 * kS + 250'000, 3 * kS + 250'000, notes, [](std::uint64_t t) { return at(t, blocks(1)); });
  run(c, s, 3 * kS + 500'000, 10 * kS, notes, [](std::uint64_t t) { return at(t, blocks(2)); });
  CHECK(notes.empty());
}

TEST_CASE("live check: the Song of Time watches cover every listed block key") {
  const auto watches = coop::host::song_block_watches();
  std::size_t keys64 = 0;
  for (const auto& w : watches) {
    CAPTURE(w.place);
    CHECK(w.actor == "SongOfTimeBlock");
    CHECK(w.cause == "Ocarina");
    CHECK_FALSE(w.keys.empty());
    for (const auto& k : w.keys) keys64 += k.kind == protocol::FlagKeyKind::key64;
  }
  CHECK(keys64 == 4 + 8);  // the four that go for good and the eight that come and go
  const auto forest_watch = std::find_if(watches.begin(), watches.end(),
                                         [](const LiveWatch& w) { return w.place == "ForestTemple"; });
  REQUIRE(forest_watch != watches.end());
  CHECK(std::find(forest_watch->keys.begin(), forest_watch->keys.end(), kKey) != forest_watch->keys.end());
  CHECK(std::find(forest_watch->keys.begin(), forest_watch->keys.end(),
                  FlagKey(coop::host::flag_hash("Foresttemple_SongOfTimeBlock_A_01_Disappearance"),
                          std::uint16_t{0})) != forest_watch->keys.end());
  const auto meadow = std::find_if(watches.begin(), watches.end(),
                                   [](const LiveWatch& w) { return w.place == "SacredForestMeadow"; });
  REQUIRE(meadow != watches.end());  // both eras: place_of
}
