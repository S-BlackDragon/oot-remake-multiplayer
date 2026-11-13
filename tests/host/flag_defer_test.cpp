#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/host/flag_defer.hpp"
#include "coop/host/flag_watcher.hpp"
#include "coop/protocol/codec.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

const FlagKey kBlocker{flag_hash("KokiriForest_Npc_Kokiri_Mido_Child_Blocker"), 0};
constexpr FlagKey kDoor{0x2000u, 0};    // no rule: progress, written at once
constexpr FlagKey kKeys{0x3000u, 0};    // a count
constexpr FlagKey kAppear{0x5000u, 0};  // a placement appears (scene rule only)

proto::FlagEntry entry(FlagKey key, std::uint32_t value,
                       proto::FlagKind kind = proto::FlagKind::boolean) {
  return {key, kind, value};
}

class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  std::set<FlagKey> unwritable;
  std::set<FlagKey> counts;
  int writes = 0;

  void set(FlagKey key, std::uint32_t value) { values[key] = entry(key, value); }
  std::uint32_t get(FlagKey key) const { return values.at(key).value; }

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
  bool is_count(FlagKey key) override { return counts.contains(key); }
};

FakeStore game() {
  FakeStore store;
  store.set(kBlocker, 1);  // Mido blocks the path
  store.set(kDoor, 0);
  store.set(kAppear, 0);
  store.values[kKeys] = entry(kKeys, 0, proto::FlagKind::int32);
  store.counts.insert(kKeys);
  store.set(FlagKey::key64_flag(0x77u), 0);
  return store;
}

DeferRules rules() {
  std::string error;
  auto r = DeferRules::parse(
      "KokiriForest_Npc_Kokiri_Mido_Child_Blocker scene:KokiriForest actor:Npc_Kokiri_Mido_Child "
      "far:25 after:120\n"
      "0x5000 scene:Market after:0\n",
      &error);
  REQUIRE_MESSAGE(r, error);
  return *r;
}

// Kokiri Forest with Mido 3 m from our Link, owned by this game.
DeferView forest(std::uint64_t now_us = 1'000'000) {
  DeferView v;
  v.scene = "KokiriForest_Child";
  v.actors = std::vector<SeenActor>{{"Npc_Kokiri_Mido_Child", proto::Vec3{3, 0, 0}, false},
                                    {"Npc_Kokiri_Fado_Child", proto::Vec3{40, 0, 0}, false},
                                    {"FldObj_AirWall_Player_GenerateNavMesh", std::nullopt, false}};
  v.link = proto::Vec3{0, 0, 0};
  v.now_us = now_us;
  return v;
}

std::vector<DeferWhy> whys(FlagDeferral& d) {
  std::vector<DeferWhy> out;
  for (const auto& n : d.take_notes()) out.push_back(n.why);
  return out;
}

}  // namespace

TEST_CASE("flag defer: rules from text: names, hashes, options and overrides") {
  std::string error;
  const auto r = DeferRules::parse(
      "# talk flags\n"
      "\n"
      "KokiriForest_Npc_Kokiri_Mido_Child_Blocker scene:KokiriForest actor:Npc_Kokiri_Mido_Child "
      "actor:FldObj_AirWall far:25 after:90  # Mido\n"
      "0x5000 scene:Market scene:HyruleCastle after:0\n"
      "0x6000\n"
      "0x7000 now\n",
      &error);
  REQUIRE_MESSAGE(r, error);
  CHECK(r->size() == 4);
  const DeferRule* mido = r->find(kBlocker);
  REQUIRE(mido);
  CHECK(mido->scenes == std::vector<std::string>{"KokiriForest"});
  CHECK(mido->actors == std::vector<std::string>{"Npc_Kokiri_Mido_Child", "FldObj_AirWall"});
  CHECK(mido->far_m == 25.0f);
  CHECK(mido->after_us == 90'000'000u);
  const DeferRule* appear = r->find(kAppear);
  REQUIRE(appear);
  CHECK(appear->scenes.size() == 2);
  CHECK(appear->after_us == 0u);
  const DeferRule* bare = r->find({0x6000u, 0});
  REQUIRE(bare);
  CHECK(bare->after_us == DeferRule::kDefaultAfterUs);
  CHECK(r->find({0x7000u, 0}) == nullptr);  // "now": never held
  CHECK(r->find({0x8000u, 0}) == nullptr);

  for (const char* bad : {"0x123456789 after:1", "Flag what:1", "Flag far:5", "Flag after:x",
                          "Flag after:1 after:2", "Flag now scene:A", "Flag\x01"}) {
    CAPTURE(bad);
    CHECK_FALSE(DeferRules::parse(bad, &error));
    CHECK(error.starts_with("line 1: "));
  }

  // A file's lines replace the built-in ones; "now" takes one away.
  DeferRules base = default_defer_rules();
  REQUIRE(base.find(kBlocker));
  base.merge(*DeferRules::parse("KokiriForest_Npc_Kokiri_Mido_Child_Blocker now\n"));
  CHECK(base.find(kBlocker) == nullptr);
  CHECK_FALSE(base.set(FlagKey::key64_flag(5), DeferRule{}));  // a 64-bit key is never held
}

TEST_CASE("flag defer: the built-in rules are the Kokiri talk flags") {
  const DeferRules r = default_defer_rules();
  CHECK(r.size() == 101);
  CHECK(flag_hash("KokiriForest_Npc_Kokiri_Mido_Child_Blocker") == 0x502f6ceau);
  const DeferRule* mido = r.find({0x502f6ceau, 0});
  REQUIRE(mido);
  CHECK(mido->actors == std::vector<std::string>{"Npc_Kokiri_Mido_Child"});
  CHECK(mido->scenes == std::vector<std::string>{"KokiriForest"});
  CHECK(mido->far_m == 25.0f);
  CHECK(mido->after_us == 120'000'000u);
  const DeferRule* malon = r.find({flag_hash("IsMalonAppear_Market"), 0});
  REQUIRE(malon);
  CHECK(malon->actors.empty());
  CHECK(malon->after_us == 0u);
  // Items, songs and dungeon flags are not talk flags.
  CHECK(r.find({0xd3f8bfccu, 0}) == nullptr);  // IsGet.Item_Melody_SariaSong
  CHECK(r.find({0xeeb495b9u, 0}) == nullptr);  // PouchData.Weapon_Sword_Kokiri.StockNum
}

TEST_CASE("flag defer: flags with no rule, 64-bit keys and counts go at once") {
  FakeStore store = game();
  FlagDeferral d(rules());
  const DeferView view = forest();
  DeferringFlagStore s(d, store, view);
  CHECK(s.write(entry(kDoor, 1)));
  CHECK(store.get(kDoor) == 1);
  CHECK(s.write(entry(FlagKey::key64_flag(0x77u), 1)));
  CHECK(store.get(FlagKey::key64_flag(0x77u)) == 1);
  CHECK(s.write(entry(kKeys, 2, proto::FlagKind::int32)));
  CHECK(store.get(kKeys) == 2);
  CHECK(s.is_count(kKeys));
  CHECK(d.held() == 0);
  CHECK(d.stats().passed == 3);
  CHECK(d.take_notes().empty());
}

TEST_CASE("flag defer: a count or a count delta is never held, even with a rule for its flag") {
  // A kit list may name a flag that is also a count (keys, tokens): the count
  // goes at once whatever its rule says, as does a count delta.
  FakeStore store = game();
  std::string error;
  auto r = DeferRules::parse(
      "0x3000 scene:KokiriForest after:120\n"
      "KokiriForest_Npc_Kokiri_Mido_Child_Blocker scene:KokiriForest actor:Npc_Kokiri_Mido_Child "
      "far:25 after:120\n",
      &error);
  REQUIRE_MESSAGE(r, error);
  FlagDeferral d(*r);
  const DeferView view = forest();
  DeferringFlagStore s(d, store, view);
  CHECK(s.write(entry(kKeys, 2, proto::FlagKind::int32)));
  CHECK(store.get(kKeys) == 2);
  CHECK(s.write(entry(kBlocker, 1, proto::FlagKind::count_delta)));
  CHECK(store.values.at(kBlocker).kind == proto::FlagKind::count_delta);
  CHECK(d.held() == 0);
  CHECK(d.stats().passed == 2);
  CHECK(d.take_notes().empty());
}

TEST_CASE("flag defer: held while its townsperson is loaded here, written when it goes") {
  FakeStore store = game();
  FlagDeferral d(rules());
  DeferView view = forest();
  {
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));  // another player's talk moved Mido aside
    CHECK(store.get(kBlocker) == 1);     // not in the game yet
    CHECK(s.read(kBlocker)->value == 0);  // but read back as written
  }
  CHECK(d.holds(kBlocker));
  CHECK(whys(d) == std::vector{DeferWhy::held});

  view.now_us += 30'000'000;
  d.tick(store, view);  // still there, 3 m away
  CHECK(store.get(kBlocker) == 1);

  // Mido leaves the actor list (our player walked off, he unloaded).
  view.actors->erase(view.actors->begin());
  d.tick(store, view);
  CHECK(store.get(kBlocker) == 0);
  CHECK_FALSE(d.holds(kBlocker));
  CHECK(whys(d) == std::vector{DeferWhy::actors_gone});
  CHECK(d.stats().released == 1);
}

TEST_CASE("flag defer: a ruled value written at once says why it did not wait (charla-mido)") {
  // Seen in a game (sondas9): B's Mido had left B's actor list, so A's talk
  // flag went in at once and the log said nothing about the rule.
  FakeStore store = game();
  FlagDeferral d(rules());
  DeferView view = forest();
  view.actors->erase(view.actors->begin());  // Mido not loaded here
  {
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));
    CHECK(s.write(entry(kDoor, 1)));  // no rule: not said
  }
  CHECK(store.get(kBlocker) == 0);
  CHECK(d.take_notes().empty());  // nothing held, released or dropped
  const auto passed = d.take_passed();
  REQUIRE(passed.size() == 1);
  CHECK(passed[0].entry.key == kBlocker);
  CHECK(passed[0].entry.value == 0);
  CHECK(passed[0].why == DeferWhy::actors_gone);
  CHECK(d.take_passed().empty());
}

TEST_CASE("flag defer: another scene or no scene writes at once, a scene change releases") {
  SUBCASE("in another scene") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    view.scene = "LostWood_Child";
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));
    CHECK(store.get(kBlocker) == 0);
    CHECK(d.held() == 0);
  }
  SUBCASE("loading") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    view.scene.reset();
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));
    CHECK(store.get(kBlocker) == 0);
  }
  SUBCASE("held, then the scene changes") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
      CHECK(s.write(entry(kAppear, 1)));  // a Market rule: not our scene, so at once
    }
    CHECK(store.get(kAppear) == 1);
    CHECK(d.held() == 1);
    view.scene = "KokiriForest_Child_House";  // same prefix, another scene
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 0);
    CHECK(whys(d) == std::vector{DeferWhy::held, DeferWhy::scene_changed});
  }
  SUBCASE("a placement rule waits in its scene until the scene changes, with no time limit") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    view.scene = "Market_Child";
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kAppear, 1)));
    }
    CHECK(store.get(kAppear) == 0);
    view.now_us += 3600'000'000ull;  // an hour in the Market
    d.tick(store, view);
    CHECK(store.get(kAppear) == 0);
    view.scene = "HyruleField";
    d.tick(store, view);
    CHECK(store.get(kAppear) == 1);
  }
}

TEST_CASE("flag defer: copies another game moves, a far townsperson and the time limit release") {
  SUBCASE("our Mido is a copy of the talker's") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
    }
    CHECK(store.get(kBlocker) == 1);  // ours still: its own AI would jump
    (*view.actors)[0].copy = true;    // the talker claimed it (patch 0011)
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 0);
    CHECK(whys(d).back() == DeferWhy::actors_copied);
  }
  SUBCASE("our Link is far from Mido") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
    }
    view.link = proto::Vec3{0, 0, 24};  // 24.2 m: not yet
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 1);
    view.link = proto::Vec3{0, 0, 30};
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 0);
    CHECK(whys(d).back() == DeferWhy::far);
  }
  SUBCASE("the player stays beside Mido") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
    }
    view.now_us += 119'000'000;
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 1);
    {  // a newer value keeps the first time
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
    }
    view.now_us += 1'000'000;
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 0);
    CHECK(whys(d) == std::vector{DeferWhy::held, DeferWhy::timeout});
  }
  SUBCASE("no actor list: the scene and the time still count") {
    FakeStore store = game();
    FlagDeferral d(rules());
    DeferView view = forest();
    view.actors.reset();
    {
      DeferringFlagStore s(d, store, view);
      CHECK(s.write(entry(kBlocker, 0)));
    }
    CHECK(store.get(kBlocker) == 1);
    view.scene = "LostWood_Child";
    d.tick(store, view);
    CHECK(store.get(kBlocker) == 0);
  }
}

TEST_CASE("flag defer: the game's own change wins over a held value") {
  FakeStore store = game();
  FlagDeferral d(rules());
  DeferView view = forest();
  {
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));
  }
  store.set(kBlocker, 0);  // our own player talked to Mido too
  DeferringFlagStore s(d, store, view);
  CHECK(s.read(kBlocker)->value == 0);
  CHECK(d.held() == 0);
  CHECK(whys(d) == std::vector{DeferWhy::held, DeferWhy::game_changed});
  CHECK(d.stats().dropped == 1);

  // A value equal to the game's is not held at all.
  store.set(kBlocker, 1);
  CHECK(s.write(entry(kBlocker, 1)));
  CHECK(d.held() == 0);
}

TEST_CASE("flag defer: a release the game refuses is tried again") {
  FakeStore store = game();
  FlagDeferral d(rules());
  DeferView view = forest();
  {
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(kBlocker, 0)));
  }
  store.unwritable.insert(kBlocker);
  view.scene = "LostWood_Child";
  d.tick(store, view);
  CHECK(d.holds(kBlocker));
  CHECK(d.stats().write_failures == 1);
  store.unwritable.clear();
  d.tick(store, view);
  CHECK(store.get(kBlocker) == 0);
  CHECK_FALSE(d.holds(kBlocker));
}

TEST_CASE("flag defer: with FlagWatcher, a held talk flag is neither sent back nor tried again") {
  FakeStore store = game();
  FlagDeferral d(rules());
  FlagWatcher w(SharedFlags({kBlocker, kDoor}), FlagRole::player);
  DeferView view = forest();
  auto frame = [&] {
    d.tick(store, view);
    DeferringFlagStore s(d, store, view);
    return w.frame(s);
  };
  CHECK(frame().empty());  // baseline

  // The host's FLAG_STATE: another player talked to Mido and opened a door.
  CHECK(w.receive(proto::FlagState{1, {entry(kBlocker, 0), entry(kDoor, 1)}}));
  CHECK(frame().empty());          // no echo of the held value
  CHECK(store.get(kDoor) == 1);    // progress at once
  CHECK(store.get(kBlocker) == 1);  // Mido stays put in front of us
  const int writes = store.writes;
  CHECK(frame().empty());
  CHECK(store.writes == writes);  // not retried
  CHECK(w.stats().write_failures == 0);

  // We walk away from Mido: the game takes it, and still nothing goes out.
  view.link = proto::Vec3{0, 0, 60};
  CHECK(frame().empty());
  CHECK(store.get(kBlocker) == 0);
  CHECK(frame().empty());

  // And a later change of our own goes out as before.
  store.set(kBlocker, 1);
  const auto out = frame();
  REQUIRE(out.size() == 1);
  proto::Message m;
  REQUIRE(proto::decode(out[0].bytes, m) == proto::Error::none);
  CHECK(std::get<proto::FlagSet>(m).flags == std::vector{entry(kBlocker, 1)});
}

TEST_CASE("flag defer: a host's answer and snapshot carry the held value") {
  FakeStore store = game();
  FlagDeferral d(rules());
  FlagWatcher w(SharedFlags({kBlocker, kDoor}), FlagRole::host);
  const DeferView view = forest();
  auto frame = [&] {
    d.tick(store, view);
    DeferringFlagStore s(d, store, view);
    return w.frame(s);
  };
  frame();
  CHECK(w.receive(proto::FlagSet{2, {entry(kBlocker, 0)}}));
  const auto out = frame();
  REQUIRE(out.size() == 1);
  proto::Message m;
  REQUIRE(proto::decode(out[0].bytes, m) == proto::Error::none);
  CHECK(std::get<proto::FlagState>(m).flags == std::vector{entry(kBlocker, 0)});
  CHECK(store.get(kBlocker) == 1);  // the host's own Mido waits too

  w.request_snapshot();
  const auto snap = frame();
  REQUIRE(snap.size() == 1);
  REQUIRE(proto::decode(snap[0].bytes, m) == proto::Error::none);
  const auto& flags = std::get<proto::FlagSnapshot>(m).flags;
  CHECK(std::find(flags.begin(), flags.end(), entry(kBlocker, 0)) != flags.end());
}

// ORM shared talk list (tools/release/kit/shared_talk.txt, LICHII_COOP_TALK).
namespace {

std::vector<std::string> kit_talk_lines() {
  std::ifstream file(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", std::ios::binary);
  REQUIRE(file);
  std::vector<std::string> out;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') continue;
    out.push_back(line);
  }
  return out;
}

}  // namespace

TEST_CASE("talk list: every line of the kit's file parses on its own, with its hash recomputed") {
  const auto lines = kit_talk_lines();
  REQUIRE(lines.size() >= 10);
  std::set<FlagKey> seen;
  for (const std::string& line : lines) {
    INFO(line);
    std::string error;
    const auto one = DeferRules::parse(line, &error);
    REQUIRE_MESSAGE(one, error);
    REQUIRE(one->size() == 1);
    const FlagKey key = one->keys()[0];
    CHECK(seen.insert(key).second);  // no flag twice
    // A name, not a bare hash, and the comment's hash is flag_hash(name).
    const std::string name = line.substr(0, line.find(' '));
    CHECK(one->name(key) == name);
    const auto at = line.find("# 0x");
    REQUIRE(at != std::string::npos);
    CHECK(std::stoul(line.substr(at + 2), nullptr, 16) == flag_hash(name));
    CHECK(key.hash == flag_hash(name));
    // Shared now, or held within named scenes (never "any scene for 120 s").
    const bool now = line.find(" now") != std::string::npos;
    CHECK((now || line.find("scene:") != std::string::npos));
    if (!now) CHECK(one->find(key) != nullptr);
    // What stays each player's own is never listed (docs: BANDERAS_CHARLA, "Decision").
    for (const char* own : {"HaveTalked", "_Talk_0", "FirstTalk", "IsAfterFirstTalk", "NaviHint", "Shop_Select",
                            "GlassTruth", "NumberOfTalkedNpc", "Shop_Talking", "_Shop_Child", "DekuShield", "_Tutorial", "LinkPosition"}) {
      CHECK(name.find(own) == std::string::npos);
    }
  }
  std::string error;
  const auto all = DeferRules::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  REQUIRE_MESSAGE(all, error);
  CHECK(all->size() == lines.size());
  CHECK(all->keys() == std::vector<FlagKey>(seen.begin(), seen.end()));
}

TEST_CASE("talk list: the kit's rules for Mido, a deal, Malon and the story cutscene, and no shop") {
  std::string error;
  const auto talk = DeferRules::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  REQUIRE_MESSAGE(talk, error);
  const DeferRule* mido = talk->find({0x502f6ceau, 0});
  REQUIRE(mido);
  CHECK(mido->scenes == std::vector<std::string>{"KokiriForest"});
  CHECK(mido->actors == std::vector<std::string>{"Npc_Kokiri_Mido_Child"});
  CHECK(mido->far_m == 25.0f);
  CHECK(mido->after_us == 120'000'000u);
  const DeferRule* paid = talk->find({flag_hash("KokiriForest_Npc_Kokiri_008_Child_AlreadyPaid"), 0});
  REQUIRE(paid);
  CHECK(paid->actors == std::vector<std::string>{"Npc_Kokiri_008_Child"});
  // Each player buys their own Deku Shield (issue 130): the shop's word on it is not listed.
  const auto listed = talk->keys();
  CHECK(std::find(listed.begin(), listed.end(),
                  FlagKey{flag_hash("KokiriForest_Meet_DekuTree_BoughtDekuShield"), 0}) == listed.end());
  const DeferRule* malon = talk->find({0x291f87b2u, 0});
  REQUIRE(malon);
  CHECK(malon->actors.empty());
  CHECK(malon->after_us == 0u);
  // The Deku Tree meeting plays for everyone (story list): shared, never held,
  // over the built-in rule that would hold it.
  const FlagKey meet{0x97137664u, 0};
  CHECK(talk->find(meet) == nullptr);
  const auto keys = talk->keys();
  CHECK(std::find(keys.begin(), keys.end(), meet) != keys.end());
  auto rules = default_defer_rules();
  CHECK(rules.find(meet) != nullptr);
  rules.merge(*talk);
  CHECK(rules.find(meet) == nullptr);
  CHECK(rules.name(meet) == "HavePlayedEvent.Dm_Kokiri_0070");
  // A flag the list leaves to each player keeps its built-in rule but is not in the list.
  const FlagKey greeting{flag_hash("HaveTalked_Special.Npc_Kokiri_Mido_Child_Talk_Event30"), 0};
  CHECK(rules.find(greeting) != nullptr);
  CHECK(std::find(keys.begin(), keys.end(), greeting) == keys.end());
}

TEST_CASE("talk list: adult Mido's blocker, Talon back at the ranch and Talon leaving Kakariko (charlas)") {
  std::string error;
  const auto talk = DeferRules::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_talk.txt", &error);
  REQUIRE_MESSAGE(talk, error);
  const DeferRule* mido = talk->find({0x3b320c55u, 0});  // KokiriForest_Npc_Kokiri_Mido_Adult_Blocker
  REQUIRE(mido);
  CHECK(mido->scenes == std::vector<std::string>{"LostWood"});
  CHECK(mido->actors == std::vector<std::string>{"Npc_Kokiri_Mido_Adult"});
  CHECK(mido->far_m == 25.0f);
  const DeferRule* ranch = talk->find({flag_hash("Forbid_MelodyGet_Epona"), 0});
  REQUIRE(ranch);
  CHECK(ranch->actors == std::vector<std::string>{"Npc_Hylia_Malon_Child"});
  CHECK(ranch->after_us == 120'000'000u);
  const DeferRule* woken = talk->find({0x7207ede9u, 0});  // KakarikoVillage_CrowedCucco_Npc_Hylia_Talon_Adult
  REQUIRE(woken);
  CHECK(woken->scenes == std::vector<std::string>{"KakarikoVillage"});
  const FlagKey leave{0x0bf7ce9du, 0};  // KakarikoVillage_LeaveKakariko_Npc_Hylia_Talon_Adult
  REQUIRE(flag_hash("KakarikoVillage_LeaveKakariko_Npc_Hylia_Talon_Adult") == leave.hash);
  const DeferRule* go = talk->find(leave);
  REQUIRE(go);
  CHECK(go->scenes == std::vector<std::string>{"KakarikoVillage", "HyruleField"});
  CHECK(go->actors.empty());
  CHECK(go->after_us == 0u);

  // At the ranch, where adult Talon is not loaded yet: held (an actor rule would let it go at
  // once and pop him and the ranch's props in), and written when the scene changes.
  FakeStore store = game();
  store.set(leave, 0);
  FlagDeferral d(*talk);
  DeferView view;
  view.scene = "HyruleField_Adult";
  view.actors = std::vector<SeenActor>{{"Npc_Hylia_Malon_Adult", proto::Vec3{4, 0, 0}, false}};
  view.link = proto::Vec3{0, 0, 0};
  view.now_us = 1'000'000;
  {
    DeferringFlagStore s(d, store, view);
    CHECK(s.write(entry(leave, 1)));
  }
  CHECK(store.get(leave) == 0);
  CHECK(whys(d) == std::vector{DeferWhy::held});
  view.now_us += 600'000'000ull;  // ten minutes at the ranch
  d.tick(store, view);
  CHECK(store.get(leave) == 0);
  view.scene = "Market_Adult";
  d.tick(store, view);
  CHECK(store.get(leave) == 1);
  CHECK(whys(d) == std::vector{DeferWhy::scene_changed});
  // In a scene of none of its rules (Death Mountain), at once.
  FakeStore other = game();
  other.set(leave, 0);
  FlagDeferral d2(*talk);
  view.scene = "DeathMountainTrail_Adult";
  {
    DeferringFlagStore s(d2, other, view);
    CHECK(s.write(entry(leave, 1)));
  }
  CHECK(other.get(leave) == 1);
  CHECK(d2.held() == 0);
}

TEST_CASE("talk list: rule names and keys, and a 0x line has no name") {
  std::string error;
  auto r = DeferRules::parse("KokiriForest_LinkHouseGoDown scene:KokiriForest after:120\n0x5000 now\n", &error);
  REQUIRE_MESSAGE(r, error);
  CHECK(r->keys() == std::vector<FlagKey>{FlagKey{0x5000u, 0}, FlagKey{0x1afd0ad0u, 0}});
  CHECK(r->name({0x1afd0ad0u, 0}) == "KokiriForest_LinkHouseGoDown");
  CHECK(r->name({0x5000u, 0}).empty());
  CHECK(r->name({0x6000u, 0}).empty());
  // A 0x line has no name; merged, its "now" replaces the named rule.
  auto over = DeferRules::parse("0x1afd0ad0 now\n", &error);
  REQUIRE(over);
  CHECK(over->name({0x1afd0ad0u, 0}).empty());
  r->merge(*over);
  CHECK(r->find({0x1afd0ad0u, 0}) == nullptr);
}

TEST_CASE("talk log: changes of the listed flags only, after a first reading") {
  FakeStore store = game();
  FlagChanges log({kBlocker, kAppear, FlagKey{0x9999u, 0}});  // 0x9999: not readable
  CHECK(log.read(store).empty());
  CHECK(log.read(store).empty());
  store.set(kBlocker, 0);
  store.set(kDoor, 1);  // not listed
  auto c = log.read(store);
  REQUIRE(c.size() == 1);
  CHECK(c[0].key == kBlocker);
  CHECK(c[0].before->value == 1u);
  CHECK(c[0].now.value == 0u);
  CHECK(log.read(store).empty());
  store.set(FlagKey{0x9999u, 0}, 1);  // readable now: recorded, not a change
  CHECK(log.read(store).empty());
  log.forget();
  store.set(kAppear, 1);
  CHECK(log.read(store).empty());  // after forget the first reading only records
}
