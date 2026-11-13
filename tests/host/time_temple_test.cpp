// ORM the Temple of Time with several players (coop/host/time_temple.hpp): the
// list, its agreement with the story list and the shared items, which mate's
// start a game takes, and when the room's age follow waits.

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/story.hpp"
#include "coop/host/time_temple.hpp"
#include "coop/protocol/codec.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr const char* kChild = "Market_Child";
constexpr const char* kAdult = "Market_Adult";
constexpr const char* kDraw = "Market_Child_DoorOfTime_Draw";
constexpr const char* kOpenDoor = "Market_Child_DoorOfTime_OpenDoor";
constexpr const char* kRauru = "SageRoom_MeetRauru";
constexpr const char* kSheik = "Market_Adult_AllSage_Start";
constexpr const char* kToChild = "Market_Adult_Field_AdultToChild";

// From the scenes' Banc (Market_Child_Static / Market_Adult_Static).
const proto::Vec3 kPedestal{137.63f, 104.86f, -34.0f};       // Obj_TimeTemple_MasterSwordBase_A_01
const proto::Vec3 kDrawAsk{147.10f, 110.49f, -30.64f};       // the Draw's EventRequestActor
const proto::Vec3 kAltar{112.73f, 113.29f, -34.34f};         // the Door of Time's proxy request
const proto::Vec3 kFirstTime{71.11f, 110.63f, -33.74f};      // Dm_TimeTemple_0000's request
const proto::Vec3 kToChildAsk{140.43f, 116.26f, -27.65f};    // AdultToChild's request (adult)
const proto::Vec3 kSquare{-2.04f, 100.09f, -16.10f};         // Market_FrontOfHyruleCastle
const proto::Vec3 kSquareAdult{-1.84f, 99.99f, -16.10f};     // Market_Entrance_FromCastle

proto::StoryEvent start(const std::string& name, const std::string& scene, std::uint8_t from = 2) {
  proto::StoryEvent m;
  m.player_number = from;
  m.kind = proto::StoryKind::start;
  m.scene = scene;
  m.name = name;
  m.position = kPedestal;
  m.server_time_ms = 1000;
  return m;
}

TimeWhere where(const std::optional<std::string>& scene, const std::optional<proto::Vec3>& at) {
  TimeWhere w;
  w.scene = scene;
  w.position = at;
  return w;
}

bool has(const std::vector<std::string>& lines, const std::string& part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

bool logged(const StoryOutput& out, const std::string& part) { return has(out.log, part); }

}  // namespace

TEST_CASE("time temple list: the built-in list and the kit's file are the same text") {
  std::ifstream file(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_time_temple.txt", std::ios::binary);
  REQUIRE(file);
  const std::string kit((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  CHECK(kit == kDefaultTimeTempleList);
  std::string error;
  const auto parsed = TimeTempleList::parse(kDefaultTimeTempleList, &error);
  REQUIRE_MESSAGE(parsed.has_value(), error);
  const auto loaded =
      TimeTempleList::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_time_temple.txt", &error);
  REQUIRE_MESSAGE(loaded.has_value(), error);
  CHECK(loaded->entries() == parsed->entries());
  CHECK(loaded->areas() == parsed->areas());
  CHECK(TimeTempleList::defaults().entries().size() == parsed->entries().size());
}

TEST_CASE("time temple list: the built-in list's rules, areas and flags") {
  const TimeTempleList list = TimeTempleList::defaults();
  REQUIRE(list.entries().size() == 24);
  CHECK(list.find(kDraw)->rule == TimeRule::travel);
  CHECK(list.find("Market_Child_Field_ChildToAdult")->rule == TimeRule::travel);
  CHECK(list.find(kToChild)->rule == TimeRule::travel);
  CHECK(list.find("SageRoom_MeetGanondorf")->rule == TimeRule::chain);
  CHECK(list.find(kRauru)->rule == TimeRule::chain);
  CHECK(list.find(kRauru)->item == "Item_Medal_Light");
  CHECK(list.find(kOpenDoor)->rule == TimeRule::temple);
  CHECK(list.find(kSheik)->rule == TimeRule::temple);
  CHECK(list.find("Market_Adult_Field_GetArrowLight")->rule == TimeRule::temple);
  CHECK(list.find("KakarikoVillage_MelodyGet_Shadow")->rule == TimeRule::scene);
  CHECK(list.find("ForestTemple_SageOfForest_SageRoom")->rule == TimeRule::sage);
  CHECK(list.find("ForestTemple_SageOfForest_SageRoom")->item == "Item_Medal_Forest");
  CHECK(list.find("SpiritTemple_SageOfSpirit_SageRoom")->item == "Item_Medal_Spirit");
  CHECK(list.find("KokiriForest_Meet_DekuTree_MeetDekuTree_01") == nullptr);

  int sages = 0;
  for (const auto& e : list.entries()) sages += e.rule == TimeRule::sage ? 1 : 0;
  CHECK(sages == 5);  // the five adult temples; Light is Rauru's

  for (const char* scene : {kChild, kAdult}) {
    INFO(scene);
    const TimeArea* area = list.area(scene);
    REQUIRE(area != nullptr);
    CHECK(area->contains(kPedestal));
    CHECK(area->contains(kDrawAsk));
    CHECK(area->contains(kAltar));
    CHECK(area->contains(kFirstTime));
    CHECK(area->contains(kToChildAsk));
    CHECK_FALSE(area->contains(kSquare));
    CHECK_FALSE(area->contains(kSquareAdult));
  }
  CHECK(list.area("HyruleField_Child") == nullptr);
  CHECK(list.noage("SageRoom"));
  CHECK_FALSE(list.noage(kChild));

  const auto keys = list.shared_flags();
  REQUIRE(keys.size() == 7);
  CHECK(std::is_sorted(keys.begin(), keys.end()));
  CHECK(std::find(keys.begin(), keys.end(), proto::FlagKey{proto::flag_hash("AllSage_AwakeSage_Fire_Or_Forest"),
                                                           std::uint16_t{0}}) != keys.end());
  CHECK(std::find(keys.begin(), keys.end(), proto::FlagKey{0x4161c78fu, std::uint16_t{0}}) != keys.end());
  CHECK(std::find(keys.begin(), keys.end(), proto::FlagKey{0xd5ce94f3u, std::uint16_t{0}}) != keys.end());
}

TEST_CASE("time temple list: the built-in list agrees with the story list and the shared items") {
  const auto problems = check_time_list(TimeTempleList::defaults(), StoryList::defaults());
  for (const auto& p : problems) INFO(p);
  CHECK(problems.empty());
}

TEST_CASE("time temple list: a list that does not agree is told line by line") {
  const auto list = TimeTempleList::parse(
      "temple Ev_NotThere\n"
      "temple KokiriForest_WarpFromInsideTheDekuTree\n"  // solo on the story list
      "sage Market_Child_DoorOfTime_Draw Item_Medal_Forest\n"  // not solo there
      "chain SageRoom_MeetRauru Item_Bogus\n"
      "flag Sequence_IsChildhood\n"
      "flag PlayerStatus.IsActiveHeadArmor\n"
      "flag HavePlayedEvent.NaviHint_Shield_Hylian\n"
      "flag HavePlayedEvent.Market_Child_DoorOfTime_Draw\n");
  REQUIRE(list.has_value());
  const auto p = check_time_list(*list, StoryList::defaults());
  CHECK(p.size() == 8);
  CHECK(has(p, "temple Ev_NotThere is not on the story list"));
  CHECK(has(p, "temple KokiriForest_WarpFromInsideTheDekuTree is solo on the story list"));
  CHECK(has(p, "sage Market_Child_DoorOfTime_Draw is not solo on the story list"));
  CHECK(has(p, "SageRoom_MeetRauru's item IsGet.Item_Bogus is not a shared item"));
  CHECK(has(p, "flag Sequence_IsChildhood is never shared"));
  CHECK(has(p, "flag PlayerStatus.IsActiveHeadArmor is never shared"));
  CHECK(has(p, "flag HavePlayedEvent.NaviHint_Shield_Hylian is never shared"));
  CHECK_FALSE(has(p, "HavePlayedEvent.Market_Child_DoorOfTime_Draw is never shared"));
  CHECK(has(p, "flag HavePlayedEvent.Market_Child_DoorOfTime_Draw is the story list's already"));
  const auto p2 = check_time_list(*TimeTempleList::parse("flag HavePlayedEvent.Market_Child_DoorOfTime_Draw\n"),
                                  StoryList::defaults());
  CHECK(p2 == std::vector<std::string>{"flag HavePlayedEvent.Market_Child_DoorOfTime_Draw is the story list's already"});
}

TEST_CASE("time temple list: parsed from text, bad lines are refused with their number") {
  std::string error;
  const auto list = TimeTempleList::parse(
      "# comment\n\n  area  Market_Child 1 2 3  4 5 6  # tail\r\nnoage SageRoom\nflag 0x1234\n"
      "temple Ev_One\nchain Ev_Two\nchain Ev_Three Item_X\nsage Ev_Four Item_Y\nscene Ev_Five\n",
      &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  CHECK(list->entries().size() == 5);
  CHECK(list->find("Ev_Two")->item.empty());
  CHECK(list->find("Ev_Three")->item == "Item_X");
  CHECK(list->area("Market_Child")->min == proto::Vec3{1.0f, 2.0f, 3.0f});
  CHECK(list->area("Market_Child")->max == proto::Vec3{4.0f, 5.0f, 6.0f});
  CHECK(list->shared_flags() == std::vector<proto::FlagKey>{{0x1234u, std::uint16_t{0}}});
  CHECK(list->flag_names() == std::vector<std::string>{"0x1234"});

  CHECK_FALSE(TimeTempleList::parse("temple Ev_One\ntemple Ev_One\n", &error));
  CHECK(error == "line 2: Ev_One is listed twice");
  CHECK_FALSE(TimeTempleList::parse("temple Ev_One\nchain Ev_One\n", &error));
  CHECK(error == "line 2: Ev_One is listed twice");
  CHECK_FALSE(TimeTempleList::parse("bogus Ev_One\n", &error));
  CHECK(error == "line 1: a line starts with area, noage, flag, temple, travel, chain, sage or scene");
  CHECK_FALSE(TimeTempleList::parse("temple Ev.One\n", &error));
  CHECK(error == "line 1: an event name is 1 to 127 of A-Z a-z 0-9 _");
  CHECK_FALSE(TimeTempleList::parse("temple Ev_One Item_X\n", &error));
  CHECK(error == "line 1: temple EVENT");
  CHECK_FALSE(TimeTempleList::parse("sage Ev_One\n", &error));
  CHECK(error == "line 1: a sage line names its item");
  CHECK_FALSE(TimeTempleList::parse("sage Ev_One Item.X\n", &error));
  CHECK_FALSE(TimeTempleList::parse("area Market_Child 1 2 3 4 5\n", &error));
  CHECK(error == "line 1: area SCENE MINX MINY MINZ MAXX MAXY MAXZ");
  CHECK_FALSE(TimeTempleList::parse("area Market_Child 1 2 3 4 5 x\n", &error));
  CHECK(error == "line 1: an area's corner is six numbers");
  CHECK_FALSE(TimeTempleList::parse("area Market_Child 1 2 3 4 5 nan\n", &error));
  CHECK_FALSE(TimeTempleList::parse("area Market_Child 5 2 3 4 5 6\n", &error));
  CHECK(error == "line 1: an area's first corner is its smallest");
  CHECK_FALSE(TimeTempleList::parse("area A 0 0 0 1 1 1\narea A 0 0 0 2 2 2\n", &error));
  CHECK(error == "line 2: A has an area already");
  CHECK_FALSE(TimeTempleList::parse("area Bad-Scene 0 0 0 1 1 1\n", &error));
  CHECK_FALSE(TimeTempleList::parse("noage SageRoom\nnoage SageRoom\n", &error));
  CHECK(error == "line 2: SageRoom is listed twice");
  CHECK_FALSE(TimeTempleList::parse("flag A_Flag\nflag A_Flag\n", &error));
  CHECK(error == "line 2: A_Flag is listed twice");
  CHECK_FALSE(TimeTempleList::parse("flag 0xzz\n", &error));
  CHECK_FALSE(TimeTempleList::parse("flag 0x0\n", &error));
  CHECK(TimeTempleList::parse("", &error)->entries().empty());
  CHECK_FALSE(TimeTempleList::load("no/such/shared_time_temple.txt", &error));
}

TEST_CASE("time temple filter: a temple cutscene's start is taken only in the temple") {
  const TimeTemple rules;
  SUBCASE("our Link in the temple: left as it is") {
    auto m = start(kOpenDoor, kChild);
    const auto line = rules.filter(m, where(std::string(kChild), kAltar));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::start);
    CHECK(line->find("P2's start of 'Market_Child_DoorOfTime_OpenDoor' in Market_Child: our Link is in the "
                     "Temple of Time") == 0);
  }
  SUBCASE("our Link in the market square: played") {
    auto m = start(kOpenDoor, kChild);
    const auto line = rules.filter(m, where(std::string(kChild), kSquare));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::played);
    CHECK(line->find("not taken, our Link is not in the Temple of Time (at -2.0 100.1 -16.1); its flags come") !=
          std::string::npos);
  }
  SUBCASE("Sheik in the adult temple, our Link in the adult square: played") {
    auto m = start(kSheik, kAdult);
    REQUIRE(rules.filter(m, where(std::string(kAdult), kSquareAdult)));
    CHECK(m.kind == proto::StoryKind::played);
  }
  SUBCASE("another scene or loading: the story layer's own answer") {
    auto m = start(kOpenDoor, kChild);
    CHECK_FALSE(rules.filter(m, where(std::string("HyruleField_Child"), kSquare)));
    CHECK(m.kind == proto::StoryKind::start);
    CHECK_FALSE(rules.filter(m, where(std::nullopt, std::nullopt)));
    CHECK(m.kind == proto::StoryKind::start);
  }
  SUBCASE("our place unreadable in the scene: played") {
    auto m = start(kOpenDoor, kChild);
    const auto line = rules.filter(m, where(std::string(kChild), std::nullopt));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::played);
    CHECK(line->find("where our Link is cannot be read") != std::string::npos);
  }
  SUBCASE("an end or a played is never changed") {
    auto m = start(kOpenDoor, kChild);
    m.kind = proto::StoryKind::end;
    CHECK_FALSE(rules.filter(m, where(std::string(kChild), kSquare)));
    CHECK(m.kind == proto::StoryKind::end);
  }
}

TEST_CASE("time temple filter: drawing the sword, the chain after it, Sheik elsewhere and the sages") {
  const TimeTemple rules;
  SUBCASE("the Draw, our Link at the pedestal: taken") {
    auto m = start(kDraw, kChild);
    REQUIRE(rules.filter(m, where(std::string(kChild), kPedestal)));
    CHECK(m.kind == proto::StoryKind::start);
  }
  SUBCASE("the Draw, our Link in the square: played, the age comes by the reload") {
    auto m = start(kDraw, kChild);
    const auto line = rules.filter(m, where(std::string(kChild), kSquare));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::played);
    CHECK(line->find("our age follows the room's by the reload (D-R17)") != std::string::npos);
  }
  SUBCASE("putting the sword back, our Link in the adult temple: taken") {
    auto m = start(kToChild, kAdult);
    REQUIRE(rules.filter(m, where(std::string(kAdult), kToChildAsk)));
    CHECK(m.kind == proto::StoryKind::start);
  }
  SUBCASE("Rauru: never taken, even in the Chamber of Sages") {
    auto m = start(kRauru, "SageRoom");
    const auto line = rules.filter(m, where(std::string("SageRoom"), proto::Vec3{0.0f, 0.0f, 0.0f}));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::played);
    CHECK(line->find("a part of the sword's story") != std::string::npos);
    auto g = start("SageRoom_MeetGanondorf", kChild);
    REQUIRE(rules.filter(g, where(std::string(kChild), kPedestal)));
    CHECK(g.kind == proto::StoryKind::played);
  }
  SUBCASE("Sheik's lesson in Kakariko and a sage's awakening: the story layer's rule") {
    auto m = start("KakarikoVillage_MelodyGet_Shadow", "KakarikoVillage_Adult");
    CHECK_FALSE(rules.filter(m, where(std::string("KakarikoVillage_Adult"), kSquare)));
    CHECK(m.kind == proto::StoryKind::start);
    auto s = start("ForestTemple_SageOfForest_SageRoom", "SageRoom");
    CHECK_FALSE(rules.filter(s, where(std::string("SageRoom"), kSquare)));
    CHECK(s.kind == proto::StoryKind::start);
  }
  SUBCASE("a skip of a part of the sword's story is a plain end here (ORM D-048), of the Draw a skip") {
    auto m = start(kRauru, "SageRoom");
    m.kind = proto::StoryKind::skipped;
    const auto line = rules.filter(m, where(std::string("SageRoom"), proto::Vec3{0.0f, 0.0f, 0.0f}));
    REQUIRE(line);
    CHECK(m.kind == proto::StoryKind::end);
    CHECK(line->find("each traveller's own; not skipped here") != std::string::npos);
    auto d = start(kDraw, kChild);
    d.kind = proto::StoryKind::skipped;
    CHECK_FALSE(rules.filter(d, where(std::string(kChild), kPedestal)));
    CHECK(d.kind == proto::StoryKind::skipped);
  }
  SUBCASE("an event off the list: left alone") {
    auto m = start("KokiriForest_Meet_DekuTree_MeetDekuTree_01", "KokiriForest_Child");
    CHECK_FALSE(rules.filter(m, where(std::string("KokiriForest_Child"), kSquare)));
    CHECK(m.kind == proto::StoryKind::start);
  }
}

TEST_CASE("time temple filter: with the story layer, a start out of the temple is never asked for") {
  Story story{StoryList::defaults()};
  const TimeTemple rules;
  StoryInput in;
  in.now_us = 10'000'000;
  in.server_now_us = 50'000'000;
  in.scene = kChild;
  in.event_running = false;
  in.played.assign(story.list().size(), false);

  SUBCASE("in the square: noted as played, not asked, and not shared again from here") {
    in.position = kSquare;
    auto m = start(kDraw, kChild);
    rules.filter(m, where(in.scene, in.position));
    story.receive(m);
    in.now_us += 16'000;
    const auto out = story.tick(in);
    CHECK_FALSE(out.ask);
    CHECK(logged(out, "P2 played 'Market_Child_DoorOfTime_Draw' in Market_Child before we came"));
    CHECK_FALSE(story.asking());
    // Later our own game plays it (its own pull): the room has it already.
    in.now_us += 16'000;
    in.event_running = true;
    in.event_name = std::string(kDraw);
    const auto own = story.tick(in);
    CHECK(own.send.empty());
    CHECK(logged(own, "was played in this room already"));
  }
  SUBCASE("at the pedestal: asked for") {
    in.position = kPedestal;
    auto m = start(kDraw, kChild);
    rules.filter(m, where(in.scene, in.position));
    story.receive(m);
    in.now_us += 16'000;
    const auto out = story.tick(in);
    REQUIRE(out.ask);
    CHECK(*out.ask == kDraw);
    CHECK(story.asking() == std::optional<std::string>(kDraw));
  }
}

TEST_CASE("time temple hold: the room's age waits while the sword's story runs here") {
  TimeTemple rules;
  TimeInput in;
  in.now_us = 5'000'000;
  in.scene = kChild;
  in.event_running = false;
  auto step = [&](std::uint64_t ms) {
    in.now_us += ms * 1000;
    return rules.tick(in);
  };

  CHECK_FALSE(step(16));

  SUBCASE("asking our game for a mate's Draw") {
    in.story_asking = std::string(kDraw);
    const auto held = step(16);
    REQUIRE(held);
    CHECK(*held == "asking our game for 'Market_Child_DoorOfTime_Draw' (a mate's travel)");
    in.story_asking.reset();
    CHECK_FALSE(step(16));  // never ran here: nothing after it
  }
  SUBCASE("asking for Sheik in the temple does not hold") {
    in.story_asking = std::string(kSheik);
    CHECK_FALSE(step(16));
  }
  SUBCASE("our Draw, the load, the Chamber of Sages, the load, Sheik, then free") {
    in.event_running = true;
    in.event_name = std::string(kDraw);
    REQUIRE(step(16));
    CHECK(*step(16) == "our game runs 'Market_Child_DoorOfTime_Draw' (travel)");
    // A gap between the story's parts: still held.
    in.event_running = false;
    in.event_name.reset();
    CHECK(*step(1000) == "'Market_Child_DoorOfTime_Draw' ran here a moment ago; its story may go on");
    // The load to the Chamber of Sages, longer than the hold: still held.
    in.scene.reset();
    CHECK(*step(20'000) == "loading after 'Market_Child_DoorOfTime_Draw'");
    in.scene = std::string("SageRoom");
    CHECK(*step(16) == "in SageRoom (no age in its name; its story takes Link out on its own)");
    in.event_running = true;
    in.event_name = std::string(kRauru);
    CHECK(*step(60'000) == "our game runs 'SageRoom_MeetRauru' (chain)");
    in.event_running = false;
    in.event_name.reset();
    in.scene.reset();
    CHECK(*step(15'000) == "loading after 'SageRoom_MeetRauru'");
    in.scene = std::string(kAdult);
    CHECK(step(7'000));
    // Sheik's meeting runs (temple: no hold of its own), within the hold.
    in.event_running = true;
    in.event_name = std::string(kSheik);
    CHECK(*step(500) == "'SageRoom_MeetRauru' ran here a moment ago; its story may go on");
    CHECK_FALSE(step(30'000));
    in.event_running = false;
    in.event_name.reset();
    CHECK_FALSE(step(16));
  }
  SUBCASE("the hold ends hold_after_us after the last part") {
    in.event_running = true;
    in.event_name = std::string(kToChild);
    REQUIRE(step(16));
    in.event_running = false;
    in.event_name.reset();
    CHECK(step(7'900));
    CHECK_FALSE(step(200));
    CHECK_FALSE(step(16));
  }
  SUBCASE("another event of ours, or an unreadable name, holds nothing") {
    in.event_running = true;
    in.event_name = std::string(kOpenDoor);
    CHECK_FALSE(step(16));
    in.event_name.reset();
    CHECK_FALSE(step(16));
  }
}
