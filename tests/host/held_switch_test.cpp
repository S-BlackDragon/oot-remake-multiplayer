// Tests for the ORM held floor switches (coop/host/held_switch.hpp,
// oot-remake docs/SOSTENIDOS.md): the list, who stands on what (two and three
// players stepping on and off, a disconnect while holding, a scene reload), and
// what is written into a fake game (the switch's flag, the actor's words).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "coop/host/held_switch.hpp"

namespace {

using coop::host::FlagKey;
using coop::host::HeldChange;
using coop::host::HeldFlagApply;
using coop::host::HeldFrame;
using coop::host::HeldInput;
using coop::host::HeldOther;
using coop::host::HeldSwitchList;
using coop::host::HeldSwitchRule;
using coop::host::HeldWordApply;
using coop::host::flag_hash;
namespace protocol = coop::protocol;
using Kind = HeldChange::Kind;

constexpr std::uint64_t kSwitch = 7865893853239028816ull;  // FireTemple room 10
constexpr protocol::Vec3 kOn{-51.465f, 147.0f, -18.928f};
constexpr protocol::Vec3 kOff{-49.0f, 147.0f, -18.928f};  // 2.5 m away
const std::string kScene = "FireTemple";

HeldSwitchList parsed(std::string_view text) {
  std::string error;
  auto list = HeldSwitchList::parse(text, &error);
  INFO(error);
  REQUIRE(list.has_value());
  return *list;
}

bool parse_fails(std::string_view text) { return !HeldSwitchList::parse(text).has_value(); }

HeldOther other(std::uint8_t n, protocol::Vec3 at, std::string scene = kScene, bool live = true) {
  HeldOther o;
  o.number = n;
  o.scene = std::move(scene);
  o.position = at;
  o.live = live;
  return o;
}

HeldInput input(std::vector<HeldOther> others, std::uint64_t now_us,
                std::optional<protocol::Vec3> link = kOff, std::optional<std::string> scene = kScene) {
  HeldInput in;
  in.scene = std::move(scene);
  in.link = link;
  in.others = std::move(others);
  in.now_us = now_us;
  return in;
}

const coop::host::HeldState* state_of(const HeldFrame& f, std::uint64_t hash) {
  for (const auto& s : f.states) {
    if (s.hash == hash) return &s;
  }
  return nullptr;
}

bool has(const HeldFrame& f, Kind kind, std::uint8_t who) {
  for (const auto& c : f.changes) {
    if (c.hash == kSwitch && c.kind == kind && c.who == who) return true;
  }
  return false;
}

bool has_kind(const HeldFrame& f, Kind kind) {
  for (const auto& c : f.changes) {
    if (c.hash == kSwitch && c.kind == kind) return true;
  }
  return false;
}

// The game's flags as a map; can be made to refuse writes (a load).
class FakeFlags final : public coop::host::FlagStore {
 public:
  std::optional<protocol::FlagEntry> read(FlagKey key) override {
    if (!readable) return std::nullopt;
    const auto it = values.find(key.hash);
    if (it == values.end()) return std::nullopt;
    return protocol::FlagEntry(key, protocol::FlagKind::boolean, it->second);
  }
  bool write(const protocol::FlagEntry& e) override {
    ++writes;
    if (!writable || !values.count(e.key.hash)) return false;
    values[e.key.hash] = e.value;
    return true;
  }
  std::map<std::uint32_t, std::uint32_t> values;
  bool readable = true;
  bool writable = true;
  int writes = 0;
};

const std::uint32_t kFlag = flag_hash("FireTemple_Room10_StepSwitchOnLink");

}  // namespace

TEST_CASE("held switch: the built-in list parses and names every held: flag of the adult list") {
  const auto& list = HeldSwitchList::builtin();
  REQUIRE(HeldSwitchList::parse(HeldSwitchList::builtin_text()).has_value());
  CHECK(list.switches().size() == 14);
  CHECK(list.words().empty());  // the words come from a run of the probe
  std::set<std::uint64_t> hashes;
  for (const auto& s : list.switches()) {
    CHECK(hashes.insert(s.hash).second);
    CHECK_FALSE(s.place.empty());
    CHECK_FALSE(s.label.empty());
  }
  CHECK(list.find(kSwitch) != nullptr);
  CHECK(list.find(kSwitch)->flag == kFlag);
  CHECK(list.in_place("FireTemple").size() == 3);
  CHECK(list.in_place("WaterTemple").size() == 1);
  CHECK(list.in_place("ForestTemple").empty());  // no held step switch there
  CHECK(list.in_place("").empty());

  // Every held: line of the kit's adult list is the flag of a listed switch.
  const auto path = std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" / "kit" /
                    "shared_dungeon_adult.txt";
  std::ifstream in(path);
  REQUIRE(in.good());
  std::set<std::uint32_t> flags;
  for (const auto& s : list.switches()) {
    if (s.flag) flags.insert(*s.flag);
  }
  std::string line;
  int held = 0;
  while (std::getline(in, line)) {
    if (!line.starts_with("held:")) continue;
    std::istringstream words(line.substr(5));
    std::string name;
    words >> name;
    ++held;
    // Room23 has two: on Link and on the Amos statue; the statue presses
    // its own copy in every game, so only the Link one is a switch's flag.
    if (name == "SpiritTemple_Room23_StepSwitchOnAmos") continue;
    INFO(name);
    CHECK(flags.count(flag_hash(name)) == 1);
  }
  CHECK(held == 4);
}

TEST_CASE("held switch: the area is the switch's own, turned by its yaw") {
  auto list = parsed("switch FireTemple 1 Obj_Stepswitch_A_03 10 0 10 yaw=0.785398\n"
                     "switch InsideJabuJabuBelly 2 Obj_X 0 0 0 cyl=1.0,-0.6,0.5\n");
  const auto& box = list.switches()[0];
  CHECK(box.contains({10.0f, 0.0f, 10.0f}));
  CHECK(box.contains({10.0f, 0.25f, 10.0f}));
  CHECK_FALSE(box.contains({10.0f, 0.5f, 10.0f}));   // above: jumping over it
  CHECK_FALSE(box.contains({10.0f, -0.6f, 10.0f}));  // below
  // Turned 45 degrees: the square's corner (0.5, 0.5) is now 0.707 along x.
  CHECK(box.contains({10.65f, 0.0f, 10.0f}));
  CHECK_FALSE(box.contains({10.5f, 0.0f, 10.5f}));
  CHECK(box.contains({10.5f, 0.0f, 10.5f}, 0.3f));  // within the margin
  const auto& cyl = list.switches()[1];
  CHECK(cyl.area.shape == coop::host::HeldArea::Shape::cylinder);
  CHECK(cyl.contains({0.7f, 0.0f, 0.7f}));
  CHECK_FALSE(cyl.contains({0.75f, 0.0f, 0.75f}));
}

TEST_CASE("held switch: list lines") {
  auto list = parsed(
      "# a comment\n"
      "switch FireTemple 0x10 Obj_Stepswitch_A_03 1 2 3 kind=timed flag=0xce689a86 label=a  # trailing\n"
      "switch KokiriForest_Child 0x11 Obj_A 1 2 3 box=0.4,0.3,-0.2,0.2\n"
      "word Obj_Stepswitch_A_03 c+0x50+0x40 on=0x1 off=0x0 mask=0xff\n"
      "word Obj_Stepswitch_A_03 actor+0x1a8 on=0x3f800000 off=0x0\n");
  REQUIRE(list.switches().size() == 2);
  CHECK(list.switches()[0].kind == coop::host::HeldKind::timed);
  CHECK(list.switches()[0].area.half_x == doctest::Approx(0.44f));  // the timed default
  CHECK(list.switches()[0].flag == 0xce689a86u);
  CHECK(list.switches()[1].place == "KokiriForest");  // place_of: either era
  CHECK(list.in_place("KokiriForest_Adult").size() == 1);
  CHECK(list.switches()[1].area.half_z == doctest::Approx(0.3f));
  const auto words = list.words_for("Obj_Stepswitch_A_03");
  REQUIRE(words.size() == 2);
  CHECK(words[0]->in_component);
  CHECK(words[0]->entry == 0x50);
  CHECK(words[0]->offset == 0x40);
  CHECK(words[0]->mask == 0xff);
  CHECK_FALSE(words[1]->in_component);
  CHECK(words[1]->offset == 0x1a8);

  // A later list replaces a switch and a word, and takes one off.
  auto over = parsed("switch FireTemple 0x10 Obj_Stepswitch_A_03 5 5 5\n"
                     "word Obj_Stepswitch_A_03 c+0x50+0x40 on=0x2 off=0x0\n"
                     "off 0x11\n");
  list.merge(over);
  CHECK(list.find(0x10)->position[0] == doctest::Approx(5.0f));
  CHECK(list.find(0x10)->kind == coop::host::HeldKind::held);
  CHECK(list.find(0x11) == nullptr);  // off takes a switch of the list below off
  CHECK(list.switches().size() == 1);
  CHECK(list.words_for("Obj_Stepswitch_A_03")[0]->on == 0x2);
  auto dropped = parsed("switch FireTemple 0x10 Obj_A 1 2 3\noff 0x10\n");
  CHECK(dropped.switches().empty());
  // Off and then listed again in the same file: the new one stays.
  list.merge(parsed("off 0x11\nswitch FireTemple 0x11 Obj_B 1 2 3\n"));
  REQUIRE(list.find(0x11) != nullptr);
  CHECK(list.find(0x11)->actor == "Obj_B");
  // A built-in switch taken off by LICHII_COOP_HELD_SWITCH_LIST (as the hook merges it).
  HeldSwitchList with_off = HeldSwitchList::builtin();
  with_off.merge(parsed("off 7865893853239028816\n"));
  CHECK(with_off.find(kSwitch) == nullptr);
  CHECK(with_off.switches().size() == 13);
  CHECK(HeldSwitchList::builtin().find(kSwitch) != nullptr);  // the built-in list is untouched

  CHECK(parse_fails("switch FireTemple 0 Obj_A 1 2 3\n"));          // no hash
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2\n"));            // no z
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 x\n"));          // bad z
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 3 kind=what\n"));
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 3 box=1,1,1\n"));
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 3 box=1,1,0.5,0.1\n"));  // ymin over ymax
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 3 nope=1\n"));
  CHECK(parse_fails("switch FireTemple 1 Obj_A 1 2 3 flag=0x123456789\n"));
  CHECK(parse_fails("word Obj_A c+0x50+0x41 on=1 off=0\n"));  // not a word
  CHECK(parse_fails("word Obj_A c+0x51+0x40 on=1 off=0\n"));  // not a table entry
  CHECK(parse_fails("word Obj_A table+0x40 on=1 off=0\n"));
  CHECK(parse_fails("word Obj_A actor+0x40 on=1\n"));            // no off
  CHECK(parse_fails("word Obj_A actor+0x40 on=1 off=1\n"));      // same
  CHECK(parse_fails("word Obj_A actor+0x40 on=1 off=0 mask=0\n"));
  CHECK(parse_fails("what is this\n"));
}

TEST_CASE("held switch: two players, one on the switch: pressed in the other game, let go when he steps off") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  auto f = rule.frame(input({other(2, kOff)}, 1'000));
  REQUIRE(state_of(f, kSwitch));
  CHECK_FALSE(state_of(f, kSwitch)->held_by_others());
  CHECK(f.changes.empty());

  f = rule.frame(input({other(2, kOn)}, 2'000));
  CHECK(has(f, Kind::stepped_on, 2));
  CHECK(has(f, Kind::pressed, 2));
  CHECK(state_of(f, kSwitch)->others == std::vector<std::uint8_t>{2});
  CHECK(f.held_by_others() == std::vector<std::uint64_t>{kSwitch});

  f = rule.frame(input({other(2, kOn)}, 3'000));
  CHECK(f.changes.empty());  // standing still: nothing new

  f = rule.frame(input({other(2, kOff)}, 4'000));  // far off at once
  CHECK(has(f, Kind::stepped_off, 2));
  CHECK(has(f, Kind::released, 0));
  CHECK_FALSE(state_of(f, kSwitch)->held_by_others());
  for (const auto& c : f.changes) {
    if (c.kind == Kind::released) CHECK_FALSE(c.ours);
  }
}

TEST_CASE("held switch: three players: pressed while any stands on it, let go when the last one leaves") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  auto f = rule.frame(input({other(2, kOn), other(3, kOff)}, 1'000));
  CHECK(has(f, Kind::pressed, 2));
  f = rule.frame(input({other(2, kOn), other(3, kOn)}, 2'000));
  CHECK(has(f, Kind::stepped_on, 3));
  CHECK_FALSE(has_kind(f, Kind::pressed));  // already pressed
  CHECK(state_of(f, kSwitch)->others == std::vector<std::uint8_t>{2, 3});
  f = rule.frame(input({other(2, kOff), other(3, kOn)}, 3'000));
  CHECK(has(f, Kind::stepped_off, 2));
  CHECK_FALSE(has_kind(f, Kind::released));  // P3 still on it
  f = rule.frame(input({other(2, kOff), other(3, kOff)}, 4'000));
  CHECK(has(f, Kind::released, 0));
}

TEST_CASE("held switch: our Link on it too: the release says so (the game holds it itself)") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  auto f = rule.frame(input({other(2, kOn)}, 1'000, kOn));
  CHECK(has(f, Kind::stepped_on, 0));
  CHECK(has(f, Kind::pressed, 2));
  CHECK(state_of(f, kSwitch)->ours);
  f = rule.frame(input({other(2, kOff)}, 2'000, kOn));
  bool released_ours = false;
  for (const auto& c : f.changes) {
    if (c.kind == Kind::released) released_ours = c.ours;
  }
  CHECK(released_ours);
  // Our Link alone on it is never "held by others".
  f = rule.frame(input({}, 3'000, kOn));
  CHECK_FALSE(state_of(f, kSwitch)->held_by_others());
  // A frame where our Link is not read keeps it on.
  f = rule.frame(input({}, 4'000, std::nullopt));
  CHECK(state_of(f, kSwitch)->ours);
  CHECK(f.changes.empty());
}

TEST_CASE("held switch: a disconnect or a fade-out while holding lets go at once") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  rule.frame(input({other(2, kOn), other(3, kOn)}, 1'000));
  auto f = rule.frame(input({other(3, kOn)}, 2'000));  // P2 is gone from presence
  CHECK(has(f, Kind::gone, 2));
  CHECK_FALSE(has_kind(f, Kind::released));
  f = rule.frame(input({other(3, kOn, kScene, false)}, 3'000));  // P3 fading: no samples
  CHECK(has(f, Kind::gone, 3));
  CHECK(has(f, Kind::released, 0));
  // Back with samples, still standing there: pressed again.
  f = rule.frame(input({other(3, kOn)}, 4'000));
  CHECK(has(f, Kind::pressed, 3));
  // Another scene at the same coordinates does not count.
  f = rule.frame(input({other(3, kOn, "WaterTemple")}, 5'000));
  CHECK(has(f, Kind::gone, 3));
  CHECK(has(f, Kind::released, 0));
}

TEST_CASE("held switch: stepping just off the edge waits a moment, then lets go") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  rule.frame(input({other(2, kOn)}, 1'000'000));
  // 0.1 m past the edge along the switch's own axis: inside the margin.
  const auto* spec = HeldSwitchList::builtin().find(kSwitch);
  REQUIRE(spec);
  const float c = std::cos(spec->yaw), s = std::sin(spec->yaw);
  const float lx = 0.6f;  // local x: half 0.5 + 0.1
  const protocol::Vec3 edge{kOn[0] + c * lx, kOn[1], kOn[2] - s * lx};
  CHECK_FALSE(spec->contains(edge));
  CHECK(spec->contains(edge, HeldSwitchRule::kLeaveMarginM));
  auto f = rule.frame(input({other(2, edge)}, 1'050'000));
  CHECK(f.changes.empty());
  f = rule.frame(input({other(2, kOn)}, 1'100'000));  // back on: the wait starts over
  CHECK(f.changes.empty());
  rule.frame(input({other(2, edge)}, 1'150'000));
  f = rule.frame(input({other(2, edge)}, 1'300'000));
  CHECK(f.changes.empty());  // 150 ms out
  f = rule.frame(input({other(2, edge)}, 1'360'000));
  CHECK(has(f, Kind::stepped_off, 2));
  CHECK(has(f, Kind::released, 0));
}

TEST_CASE("held switch: a scene reload ends the holds and finds them again") {
  HeldSwitchRule rule(HeldSwitchList::builtin());
  rule.frame(input({other(2, kOn)}, 1'000));
  auto f = rule.frame(input({other(2, kOn)}, 2'000, std::nullopt, std::nullopt));  // loading
  CHECK(f.scene_changed);
  CHECK(f.left == std::vector<std::uint64_t>{kSwitch});
  CHECK(f.states.empty());
  f = rule.frame(input({other(2, kOn)}, 3'000, std::nullopt, std::string()));  // still loading
  CHECK_FALSE(f.scene_changed);
  f = rule.frame(input({other(2, kOn)}, 4'000));  // the temple again, P2 still on it
  CHECK_FALSE(f.scene_changed);  // from nothing: no scene ended
  CHECK(has(f, Kind::stepped_on, 2));
  CHECK(has(f, Kind::pressed, 2));
  // A change straight to another scene.
  f = rule.frame(input({other(2, kOn)}, 5'000, kOff, std::string("SpiritTemple")));
  CHECK(f.scene_changed);
  CHECK(f.left == std::vector<std::uint64_t>{kSwitch});
  CHECK(state_of(f, kSwitch) == nullptr);
}

TEST_CASE("held switch: the switch's flag is written 1 while others hold it, and back to 0") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldFlagApply apply(list);
  FakeFlags flags;
  flags.values[kFlag] = 0;

  apply.apply(rule.frame(input({other(2, kOff)}, 1'000)), flags);
  CHECK(flags.values[kFlag] == 0);
  CHECK(flags.writes == 0);

  auto notes = apply.apply(rule.frame(input({other(2, kOn)}, 2'000)), flags);
  CHECK(flags.values[kFlag] == 1);
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].ok);
  CHECK(notes[0].value == 1);
  CHECK(apply.writing(kSwitch));

  // The room puts it back to 0 (our Link is not in its area): written again.
  flags.values[kFlag] = 0;
  notes = apply.apply(rule.frame(input({other(2, kOn)}, 3'000)), flags);
  CHECK(flags.values[kFlag] == 1);
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].again);
  CHECK(apply.rewrites() == 1);
  // And again (a room that clears it every frame): written and counted, not
  // noted a second time (no log line a frame).
  flags.values[kFlag] = 0;
  notes = apply.apply(rule.frame(input({other(2, kOn)}, 3'500)), flags);
  CHECK(flags.values[kFlag] == 1);
  CHECK(notes.empty());
  CHECK(apply.rewrites() == 2);
  const int writes = flags.writes;
  apply.apply(rule.frame(input({other(2, kOn)}, 4'000)), flags);
  CHECK(flags.writes == writes);  // already 1: nothing written

  notes = apply.apply(rule.frame(input({other(2, kOff)}, 5'000)), flags);
  CHECK(flags.values[kFlag] == 0);
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].value == 0);
  CHECK_FALSE(apply.writing(kSwitch));

  // A new press notes its first rewrite again.
  apply.apply(rule.frame(input({other(2, kOn)}, 6'000)), flags);
  flags.values[kFlag] = 0;
  notes = apply.apply(rule.frame(input({other(2, kOn)}, 7'000)), flags);
  REQUIRE(notes.size() == 1);
  CHECK(notes[0].again);
  CHECK(apply.rewrites() == 3);
}

TEST_CASE("held switch: our Link on it when the other leaves: its flag stays the game's") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldFlagApply apply(list);
  FakeFlags flags;
  flags.values[kFlag] = 0;
  apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), flags);
  CHECK(flags.values[kFlag] == 1);
  // Our Link steps on: the game's own sensor says 1 too; we stop holding it.
  apply.apply(rule.frame(input({other(2, kOn)}, 2'000, kOn)), flags);
  CHECK_FALSE(apply.writing(kSwitch));
  CHECK(flags.values[kFlag] == 1);
  apply.apply(rule.frame(input({other(2, kOff)}, 3'000, kOn)), flags);
  CHECK(flags.values[kFlag] == 1);  // never written 0 under our Link
}

TEST_CASE("held switch: a 1 of ours left when the scene changes goes back to 0 once it can") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldFlagApply apply(list);
  FakeFlags flags;
  flags.values[kFlag] = 0;
  apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), flags);
  CHECK(flags.values[kFlag] == 1);
  flags.readable = false;  // loading: no flags yet
  flags.writable = false;
  apply.apply(rule.frame(input({other(2, kOn)}, 2'000, std::nullopt, std::nullopt)), flags);
  apply.apply(rule.frame(input({other(2, kOn)}, 3'000, std::nullopt, std::nullopt)), flags);
  CHECK(flags.values[kFlag] == 1);
  flags.readable = true;
  flags.writable = true;
  apply.apply(rule.frame(input({}, 4'000, std::nullopt, std::nullopt)), flags);
  CHECK(flags.values[kFlag] == 0);
  // And a reload with P2 still on it holds it again.
  apply.apply(rule.frame(input({other(2, kOn)}, 5'000)), flags);
  CHECK(flags.values[kFlag] == 1);
}

TEST_CASE("held switch: a flag the game cannot take is noted once, tried each frame") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldFlagApply apply(list);
  FakeFlags flags;  // the flag is not in this game's table
  auto notes = apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), flags);
  REQUIRE(notes.size() == 1);
  CHECK_FALSE(notes[0].ok);
  notes = apply.apply(rule.frame(input({other(2, kOn)}, 2'000)), flags);
  CHECK(notes.empty());
  CHECK(flags.writes == 2);
}

namespace {

// Guest memory as 64-bit words.
struct Words {
  std::map<std::uint64_t, std::uint64_t> m;
  int writes = 0;
  HeldWordApply::ReadWord reader() {
    return [this](std::uint64_t a) -> std::optional<std::uint64_t> {
      const auto it = m.find(a);
      if (it == m.end()) return std::nullopt;
      return it->second;
    };
  }
  HeldWordApply::WriteWord writer() {
    return [this](std::uint64_t a, std::uint64_t v) {
      if (!m.count(a)) return false;
      m[a] = v;
      ++writes;
      return true;
    };
  }
};

constexpr std::uint64_t kActor = 0x80000000;
constexpr std::uint64_t kTable = 0x81000000;
constexpr std::uint64_t kComponent = 0x82000000;

HeldSwitchList with_words(std::string_view kind = "") {
  HeldSwitchList list = HeldSwitchList::builtin();
  list.merge(parsed(std::string("switch FireTemple 7865893853239028816 Obj_Stepswitch_A_03 -51.465 147.0 -18.928 "
                                "yaw=-0.7854 ") +
                    std::string(kind) +
                    "\nword Obj_Stepswitch_A_03 c+0x50+0x40 on=0x1 off=0x0 mask=0x1\n"
                    "word Obj_Stepswitch_A_03 actor+0x1a8 on=0x3f800000 off=0x0\n"));
  return list;
}

}  // namespace

TEST_CASE("held switch: the actor's words are written while others hold it, and let go") {
  const HeldSwitchList list = with_words();
  HeldSwitchRule rule(list);
  HeldWordApply apply(list);
  Words mem;
  mem.m[kActor + HeldWordApply::kActorTable] = kTable;
  mem.m[kTable + 0x50] = kComponent;
  mem.m[kComponent + 0x40] = 0x100;  // other bits of the word stay
  mem.m[kActor + 0x1a8] = 0;
  std::optional<std::uint64_t> loaded = kActor;
  const auto find = [&loaded](std::uint64_t hash) -> std::optional<std::uint64_t> {
    return hash == kSwitch ? loaded : std::nullopt;
  };

  auto notes = apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), find, mem.reader(), mem.writer());
  CHECK(mem.m[kComponent + 0x40] == 0x101);
  CHECK(mem.m[kActor + 0x1a8] == 0x3f800000);
  CHECK(notes.size() == 2);
  // Next frame: nothing to write. The actor's calc puts one back: written again, not noted.
  apply.apply(rule.frame(input({other(2, kOn)}, 2'000)), find, mem.reader(), mem.writer());
  CHECK(mem.writes == 2);
  mem.m[kComponent + 0x40] = 0x100;
  notes = apply.apply(rule.frame(input({other(2, kOn)}, 3'000)), find, mem.reader(), mem.writer());
  CHECK(mem.m[kComponent + 0x40] == 0x101);
  CHECK(notes.empty());
  CHECK(apply.writes() == 3);

  notes = apply.apply(rule.frame(input({other(2, kOff)}, 4'000)), find, mem.reader(), mem.writer());
  CHECK(mem.m[kComponent + 0x40] == 0x100);
  CHECK(mem.m[kActor + 0x1a8] == 0);
  REQUIRE(notes.size() == 2);
  CHECK(notes[0].release);

  // Not loaded: nothing written, and nothing breaks.
  loaded.reset();
  apply.apply(rule.frame(input({other(2, kOn)}, 5'000)), find, mem.reader(), mem.writer());
  CHECK(mem.m[kActor + 0x1a8] == 0);
  // Loaded again (the room came back): written into it.
  loaded = kActor;
  apply.apply(rule.frame(input({other(2, kOn)}, 6'000)), find, mem.reader(), mem.writer());
  CHECK(mem.m[kActor + 0x1a8] == 0x3f800000);
}

TEST_CASE("held switch: no word written under our own Link, none let go on a timed switch or a new actor") {
  {
    const HeldSwitchList list = with_words();
    HeldSwitchRule rule(list);
    HeldWordApply apply(list);
    Words mem;
    mem.m[kActor + HeldWordApply::kActorTable] = kTable;
    mem.m[kTable + 0x50] = kComponent;
    mem.m[kComponent + 0x40] = 0;
    mem.m[kActor + 0x1a8] = 0;
    const auto find = [](std::uint64_t) -> std::optional<std::uint64_t> { return kActor; };
    apply.apply(rule.frame(input({other(2, kOn)}, 1'000, kOn)), find, mem.reader(), mem.writer());
    CHECK(mem.writes == 0);  // the game presses it under our Link
  }
  {
    const HeldSwitchList list = with_words("kind=timed");
    HeldSwitchRule rule(list);
    HeldWordApply apply(list);
    Words mem;
    mem.m[kActor + HeldWordApply::kActorTable] = kTable;
    mem.m[kTable + 0x50] = kComponent;
    mem.m[kComponent + 0x40] = 0;
    mem.m[kActor + 0x1a8] = 0;
    const auto find = [](std::uint64_t) -> std::optional<std::uint64_t> { return kActor; };
    apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), find, mem.reader(), mem.writer());
    CHECK(mem.m[kActor + 0x1a8] == 0x3f800000);
    apply.apply(rule.frame(input({other(2, kOff)}, 2'000)), find, mem.reader(), mem.writer());
    CHECK(mem.m[kActor + 0x1a8] == 0x3f800000);  // its own clock lets it go
  }
  {
    const HeldSwitchList list = with_words();
    HeldSwitchRule rule(list);
    HeldWordApply apply(list);
    Words mem;
    for (const std::uint64_t a : {kActor, kActor + 0x10000}) {
      mem.m[a + HeldWordApply::kActorTable] = kTable;
      mem.m[a + 0x1a8] = 0;
    }
    mem.m[kTable + 0x50] = kComponent;
    mem.m[kComponent + 0x40] = 0;
    std::uint64_t actor = kActor;
    const auto find = [&actor](std::uint64_t) -> std::optional<std::uint64_t> { return actor; };
    apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), find, mem.reader(), mem.writer());
    actor = kActor + 0x10000;  // another actor in its place: not ours to let go
    mem.m[actor + 0x1a8] = 0x3f800000;
    apply.apply(rule.frame(input({other(2, kOff)}, 2'000)), find, mem.reader(), mem.writer());
    CHECK(mem.m[actor + 0x1a8] == 0x3f800000);
  }
  // A bad component pointer: no address.
  coop::host::HeldWord w;
  REQUIRE(coop::host::parse_held_where("c+0x50+0x40", w));
  Words mem;
  mem.m[kActor + HeldWordApply::kActorTable] = 0;
  CHECK_FALSE(HeldWordApply::address_of(w, kActor, mem.reader()).has_value());
  CHECK_FALSE(HeldWordApply::address_of(w, 0, mem.reader()).has_value());
}

TEST_CASE("held switch: the words and the flag go with the scene") {
  const HeldSwitchList list = with_words();
  HeldSwitchRule rule(list);
  HeldWordApply apply(list);
  Words mem;
  mem.m[kActor + HeldWordApply::kActorTable] = kTable;
  mem.m[kTable + 0x50] = kComponent;
  mem.m[kComponent + 0x40] = 0;
  mem.m[kActor + 0x1a8] = 0;
  const auto find = [](std::uint64_t) -> std::optional<std::uint64_t> { return kActor; };
  apply.apply(rule.frame(input({other(2, kOn)}, 1'000)), find, mem.reader(), mem.writer());
  const int writes = mem.writes;
  // The scene goes: nothing let go (the actor went with it), nothing written.
  apply.apply(rule.frame(input({other(2, kOn)}, 2'000, std::nullopt, std::nullopt)), find, mem.reader(),
              mem.writer());
  CHECK(mem.writes == writes);
}

// docs/RIESGOS_RESUELTOS.md ("Mundo e historia"): pressed with the flag alone,
// the door near the switch tells whether that was enough.
namespace {

using coop::host::HeldDoorCheck;
using coop::host::HeldDoorSeen;
using coop::host::HeldDoorVerdict;

HeldDoorSeen door(float dy, std::optional<std::string> anim = std::string("Wait"),
                  std::string name = "Obj_FireTemple_SwitchShutter_A_01") {
  return {std::move(name), protocol::Vec3{kOn[0] + 6.0f, kOn[1] + dy, kOn[2]}, std::move(anim)};
}

HeldDoorCheck::PathOf flag_only() {
  return [](const coop::host::HeldSwitchSpec&) { return HeldDoorCheck::Path{true, false}; };
}

bool line_has(const std::vector<std::string>& lines, std::string_view part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("held switch: door actors by name") {
  CHECK(coop::host::held_door_actor("Obj_FireTemple_SwitchShutter_A_01"));
  CHECK(coop::host::held_door_actor("Obj_SwitchShutter_A_05"));
  CHECK(coop::host::held_door_actor("Obj_FireTemple_FlameWall_A_01"));
  CHECK_FALSE(coop::host::held_door_actor("Obj_Stepswitch_A_03"));
  CHECK_FALSE(coop::host::held_door_actor("Enemy_Keese"));
}

TEST_CASE("held switch: the door opened with the flag: the shutter moves up") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldDoorCheck check(list);
  std::optional<std::vector<HeldDoorSeen>> doors = std::vector<HeldDoorSeen>{door(0)};
  CHECK(check.frame(rule.frame(input({other(2, kOff)}, 1'000)), doors, flag_only(), 1'000).empty());
  CHECK(check.frame(rule.frame(input({other(2, kOn)}, 2'000)), doors, flag_only(), 2'000).empty());
  doors = std::vector<HeldDoorSeen>{door(0.1f)};  // starting: under the margin
  CHECK(check.frame(rule.frame(input({other(2, kOn)}, 500'000)), doors, flag_only(), 500'000).empty());
  doors = std::vector<HeldDoorSeen>{door(2.0f)};
  const auto lines = check.frame(rule.frame(input({other(2, kOn)}, 900'000)), doors, flag_only(), 900'000);
  CHECK(line_has(lines, "held switch: FireTemple room10: door opened with the flag "
                        "(Obj_FireTemple_SwitchShutter_A_01 moved 2.00 m, 0.9 s after the press)"));
  CHECK(check.verdict(kSwitch) == HeldDoorVerdict::opened);
}

TEST_CASE("held switch: the door's open command or a fire wall that goes count too") {
  const auto& list = HeldSwitchList::builtin();
  SUBCASE("ToOpen") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    std::optional<std::vector<HeldDoorSeen>> doors = std::vector<HeldDoorSeen>{door(0)};
    check.frame(rule.frame(input({other(2, kOn)}, 1'000)), doors, flag_only(), 1'000);
    doors = std::vector<HeldDoorSeen>{door(0, std::string("ToOpen"))};
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOn)}, 100'000)), doors, flag_only(), 100'000),
                   "plays 'ToOpen'"));
  }
  SUBCASE("a fire wall goes") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    std::optional<std::vector<HeldDoorSeen>> doors =
        std::vector<HeldDoorSeen>{door(0, std::nullopt, "Obj_FireTemple_FlameWall_A_01")};
    check.frame(rule.frame(input({other(2, kOn)}, 1'000)), doors, flag_only(), 1'000);
    doors = std::vector<HeldDoorSeen>{};
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOn)}, 100'000)), doors, flag_only(), 100'000),
                   "Obj_FireTemple_FlameWall_A_01 went"));
  }
}

TEST_CASE("held switch: the door did not move: the flag alone is not enough, the line says so") {
  const auto& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  HeldDoorCheck check(list);
  const std::optional<std::vector<HeldDoorSeen>> doors = std::vector<HeldDoorSeen>{door(0)};
  check.frame(rule.frame(input({other(2, kOn)}, 1'000)), doors, flag_only(), 1'000);
  CHECK(check.frame(rule.frame(input({other(2, kOn)}, 3'000'000)), doors, flag_only(), 3'000'000).empty());
  const auto lines = check.frame(rule.frame(input({other(2, kOn)}, 4'100'000)), doors, flag_only(), 4'100'000);
  CHECK(line_has(lines, "held switch: FireTemple room10: door did not move 4.1 s after the flag: needs the actor "
                        "words (run the probe, docs/SOSTENIDOS.md)"));
  CHECK(check.verdict(kSwitch) == HeldDoorVerdict::stays_shut);
  // With the words too, the line says they do not press it either.
  HeldSwitchRule rule2(list);
  HeldDoorCheck check2(list);
  const auto both = [](const coop::host::HeldSwitchSpec&) { return HeldDoorCheck::Path{true, true}; };
  check2.frame(rule2.frame(input({other(2, kOn)}, 1'000)), doors, both, 1'000);
  CHECK(line_has(check2.frame(rule2.frame(input({other(2, kOn)}, 5'000'000)), doors, both, 5'000'000),
                 "after the flag and the words: its words do not press it either"));
}

TEST_CASE("held switch: no door near, no door state, nothing written, let go early: each says so") {
  const auto& list = HeldSwitchList::builtin();
  SUBCASE("no door within 20 m") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    const std::optional<std::vector<HeldDoorSeen>> doors =
        std::vector<HeldDoorSeen>{{"Obj_FireTemple_SwitchShutter_A_01", protocol::Vec3{kOn[0] + 40.0f, kOn[1], kOn[2]},
                                   std::nullopt}};
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOn)}, 1'000)), doors, flag_only(), 1'000),
                   "no door actor within 20 m to tell by: not told whether the flag opens it"));
    CHECK(check.verdict(kSwitch) == HeldDoorVerdict::unknown);
  }
  SUBCASE("the walk not read (LICHII_COOP_OBJECTS off)") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    check.frame(rule.frame(input({other(2, kOn)}, 1'000)), std::nullopt, flag_only(), 1'000);
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOn)}, 4'500'000)), std::nullopt, flag_only(), 4'500'000),
                   "door state not read (LICHII_COOP_OBJECTS off?)"));
  }
  SUBCASE("nothing to press it with") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    const auto none = [](const coop::host::HeldSwitchSpec&) { return HeldDoorCheck::Path{}; };
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOn)}, 1'000)), std::nullopt, none, 1'000),
                   "nothing to press it with here (no flag written, no word line): its door stays shut"));
  }
  SUBCASE("let go before the door moved") {
    HeldSwitchRule rule(list);
    HeldDoorCheck check(list);
    const std::optional<std::vector<HeldDoorSeen>> doors = std::vector<HeldDoorSeen>{door(0)};
    check.frame(rule.frame(input({other(2, kOn)}, 1'000)), doors, flag_only(), 1'000);
    CHECK(line_has(check.frame(rule.frame(input({other(2, kOff)}, 200'000)), doors, flag_only(), 200'000),
                   "let go before its door moved (not told)"));
  }
}
