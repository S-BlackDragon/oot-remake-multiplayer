// ORM story cutscenes for everyone (coop/host/story.hpp): the list, the
// HavePlayedEvent reading over fake memory, and the rules of who owns a
// listed event and who follows it.

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/story.hpp"
#include "coop/protocol/codec.hpp"
#include "fake_memory.hpp"

using namespace coop::host;
using coop::host::test::FakeMemory;
namespace proto = coop::protocol;

namespace {

constexpr const char* kMeet = "KokiriForest_Meet_DekuTree_MeetDekuTree_01";
constexpr const char* kForest = "KokiriForest_Child";
const proto::Vec3 kSpot{-82.0f, 122.26f, 33.65f};

bool logged(const StoryOutput& out, const std::string& part) {
  for (const auto& line : out.log) {
    if (line.find(part) != std::string::npos) return true;
  }
  return false;
}

// A clock and a game standing still in the Kokiri Forest, no event running.
struct Game {
  Story story{StoryList::defaults()};
  StoryInput in;
  Game() {
    in.now_us = 1'000'000;
    in.server_now_us = 50'000'000;
    in.scene = kForest;
    in.position = kSpot;
    in.event_running = false;
    in.played.assign(story.list().size(), false);
  }
  StoryOutput tick(std::uint64_t ms = 16) {
    in.now_us += ms * 1000;
    in.server_now_us += ms * 1000;
    return story.tick(in);
  }
  void event(const std::optional<std::string>& name) {
    in.event_running = true;
    in.event_name = name;
  }
  void no_event() {
    in.event_running = false;
    in.event_name.reset();
  }
  void set_played(const std::string& name, bool on) {
    in.played[*story.list().find(name)] = on;
  }
};

proto::StoryEvent start_from(std::uint8_t player, const std::string& scene = kForest,
                             const std::string& name = kMeet) {
  proto::StoryEvent m;
  m.player_number = player;
  m.kind = proto::StoryKind::start;
  m.scene = scene;
  m.name = name;
  m.position = {52.3f, 119.1f, -20.0f};
  m.server_time_ms = 1234;
  return m;
}

// The flag manager's Bool table (M + 0x10), entries of 0x18 with the value
// at +0x16, offsets written out again (docs/flags.md).
class Readable final : public GameMemory {
 public:
  std::uint64_t main_base() const override { return mem.main_base(); }
  bool read(std::uint64_t a, std::span<std::uint8_t> out) override { return mem.read(a, out); }
  FakeMemory mem;
};

constexpr std::uint64_t kMgr = 0x8000000000ull, kEntries = 0x8100000000ull,
                        kSlots = 0x8110000000ull;
constexpr std::uint32_t kCap = 31;

void bool_table(FakeMemory& m, const std::vector<std::pair<std::uint32_t, bool>>& flags) {
  m.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
  m.put_u32(kMgr + 0x10, static_cast<std::uint32_t>(flags.size()));
  m.put_u64(kMgr + 0x18, kEntries);
  m.put_u64(kMgr + 0x20, kSlots);
  m.put_u32(kMgr + 0x2c, kCap);
  for (std::uint32_t i = 0; i < kCap; ++i) m.put_u64(kSlots + 8 * i, 0);
  for (std::size_t i = 0; i < 0x18 * flags.size(); ++i) m.put_u8(kEntries + i, 0);
  std::uint32_t index = 0;
  for (const auto& [h, value] : flags) {
    std::uint32_t s = h % kCap;
    while (*read_u32(m, kSlots + 8 * s) != 0) s = (s + 1) % kCap;
    m.put_u32(kSlots + 8 * s, h);
    m.put_u32(kSlots + 8 * s + 4, index);
    m.put_u8(kEntries + 0x18 * index + 0x16, value ? 1 : 0);
    ++index;
  }
}

// The flag manager's Enum table (M + 0x250), entries of 0x38 with the value
// at +0x1c (the value name's hash), as the game's getter 0x7e73d0 reads it.
constexpr std::uint64_t kEnumEntries = 0x8200000000ull, kEnumSlots = 0x8210000000ull;

void enum_table(FakeMemory& m, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& flags) {
  m.put_u32(kMgr + 0x250, static_cast<std::uint32_t>(flags.size()));
  m.put_u64(kMgr + 0x258, kEnumEntries);
  m.put_u64(kMgr + 0x260, kEnumSlots);
  m.put_u32(kMgr + 0x26c, kCap);
  m.put_u32(kMgr + 0x270, 0);  // an empty queue
  for (std::uint32_t i = 0; i < kCap; ++i) m.put_u64(kEnumSlots + 8 * i, 0);
  for (std::size_t i = 0; i < 0x38 * flags.size(); ++i) m.put_u8(kEnumEntries + i, 0);
  std::uint32_t index = 0;
  for (const auto& [h, value] : flags) {
    std::uint32_t s = h % kCap;
    while (*read_u32(m, kEnumSlots + 8 * s) != 0) s = (s + 1) % kCap;
    m.put_u32(kEnumSlots + 8 * s, h);
    m.put_u32(kEnumSlots + 8 * s + 4, index);
    m.put_u32(kEnumEntries + 0x38 * index + 0x1c, value);
    ++index;
  }
}

}  // namespace

// ---- The list ----------------------------------------------------------------

TEST_CASE("story list: the built-in list is the whole game's story, with flags") {
  const StoryList list = StoryList::defaults();
  REQUIRE(list.size() == 125);
  // Child and adult, the sages, Sheik's songs, the Temple of Time, Ganon, the end.
  for (const char* on :
       {kMeet, "KokiriForest_Get_StoneOfForest_Completed_Event", "KokiriForest_ArriveLostWood",
        "InsideTheDekuTree_Arrive", "HyruleField_Goto_HyruleCastle_Kaepora", "Dm_SY_BancChangeArrive_Market",
        "Dm_HrlCastle_0000", "HyruleCastle_Goto_HyruleCastle_MeetZelda", "PlayStormSong_fromBancLogic",
        "RoyalFamilyTomb_BlowOcarina", "Meet_Darunia_PlayOcarina", "Get_StoneOfWater_JabuJabuEat",
        "Market_Child_DoorOfTime_OpenDoor", "Market_Child_DoorOfTime_Draw", "SageRoom_MeetRauru",
        "Market_Adult_AllSage_Start", "ForestTemple_SageOfForest_MelodyGet", "SageOfFire_MelodyGetFire",
        "SageOfWater_MelodyGet_Water", "KakarikoVillage_Arrive", "KakarikoVillage_MelodyGet_Shadow",
        "Dm_MelodyGet_Spilit", "Market_Adult_Field_GetLightPrelude", "Meet_FourCarpenter_ClearEvent",
        "Market_Adult_Field_GetArrowLight", "GanonCastle_Adult_Field_Rainbow",
        "InsideGanonCastle_Dungeon_Boss_GetMasterSword", "InsideGanonCastle_Dungeon_BossDie_2"}) {
    INFO(on);
    CHECK(list.contains(on));
  }
  // A chest, a talk, a sign, the game over, the timelines a flow plays, Navi,
  // the eight boss rooms' intros and deaths (the boss-room sync plays them),
  // a Gerudo guard taking one Link to the cell, the horse, a minigame, the
  // ocarina's own cutscene, a water level of the Water Temple: none.
  for (const char* off :
       {"Get_StoneOfForest_GetSword", "NPC_Kokiri_DekuScrub_Talk", "Obj_Kokiri_Signboard_GuideAction_A_03",
        "DmF_SY_GameOver", "Dm_Kokiri_0070", "Dm_HrlCastle_0045", "DefaultBootEvent",
        "Goto_HyruleCastle_EntryMarket", "InsideTheDekuTree_BossArea_ShutIn", "Dm_Deku_BossIn",
        "InsideTheDekuTree_BossAppear", "InsideTheDekuTree_BossDie", "Get_StoneOfFire_AppearBoss",
        "Get_StoneOfFire_DefeatBoss", "Get_StoneOfWater_BossAppear", "Get_StoneOfWater_DestroyBoss",
        "ForestTemple_SageOfForest_PhantomGanonAppearance", "ForestTemple_SageOfForest_PhantomGanonDefeat",
        "SageOfFire_BossAppear", "SageOfFire_DefeatBoss", "Demo_SageOfWater_BossAppears",
        "Demo_SageOfWater_BossDefeat", "ShadowTemple_BossAppear", "ShadowTemple_BossWin",
        "SageOfSpirit_BossStart", "SageOfSpirit_BossChange", "SageOfSpirit_BossEnd", "GerudoFortress_GotoPrison",
        "Get_Epona_EscapeRanch_EscapeFromA", "Sub_Get_Epona_GambleRace_End", "Dm_SY_Ocarina",
        "WaterTemple_WaterLevelChange01",
        // Ganondorf and Ganon have boss rooms now (oot-remake docs/FINAL.md): their intros and
        // Ganondorf's death are the boss-room sync's. Ganon's death is the ending: listed.
        "InsideGanonCastle_Dungeon_BossAppear_1", "InsideGanonCastle_Dungeon_BossDie_1",
        "InsideGanonCastle_Dungeon_BossAppear_2"}) {
    INFO(off);
    CHECK_FALSE(list.contains(off));
  }
  const auto& meet = list.entries()[*list.find(kMeet)];
  CHECK(meet.played_hash == 0xef98d829u);  // GameDataList
  CHECK_FALSE(meet.solo);
  const auto keys = list.shared_flags();
  auto has = [&](std::uint32_t h) {
    return std::find(keys.begin(), keys.end(), proto::FlagKey{h, std::uint16_t{0}}) != keys.end();
  };
  CHECK(has(0xef98d829u));  // HavePlayedEvent.KokiriForest_Meet_DekuTree_MeetDekuTree_01
  CHECK(has(0x97137664u));  // HavePlayedEvent.Dm_Kokiri_0070
  CHECK(has(0x5ab867ffu));  // HavePlayedEvent.Dm_Kokiri_0075
  CHECK(has(proto::flag_hash("HavePlayedEvent.Dm_Deku_0000")));
  CHECK(has(0x9a60a460u));  // KokiriForest_Meet_DekuTree_OpenDekuTreeMouth (Bool)
  CHECK(has(0x3372689bu));  // Step_Meet_DekuTree (Enum)
  CHECK(has(0x7d776b61u));  // Step_Get_StoneOfForest (Enum)
  CHECK(has(0xdb89c1a9u));  // Step_Goto_HyruleCastle (Enum)
  CHECK(has(0x378913cau));  // Step_DoorOfTime (Enum)
  // World state that must be one world (D-R12): the drained well, the Royal
  // Tomb, Kakariko burning, the Door of Time, Jabu-Jabu fed, the trials.
  for (const char* world :
       {"KakarikoVillage_Npc_Hylia_009_Child_DeleteWaterPlane", "GraveYard_RoyalFamilyTomb_Open",
        "KakarikoVillage_Burning_Start", "Market_Child_TimeTempleDoor_Opened", "GaveFishToJabJab",
        "Zorariver_TheWaterfallsplits", "GraveYard_Field_ShadowTemple_ShutterOpen",
        "InsideGanonCastle_Dungeon_BarrierRemove_Light", "IsHyruleFieldBridgeClose"}) {
    INFO(world);
    CHECK(has(proto::flag_hash(world)));
  }
  // 125 HavePlayedEvent of the events, 176 timelines' and world Bools, 17 steps.
  CHECK(keys.size() == 125 + 176 + 17);
  CHECK(std::is_sorted(keys.begin(), keys.end()));
}

TEST_CASE("story list: no flag of a player's own state or presentation is ever shared") {
  // The age (D-R17 changes it for everyone its own way), the UI, Navi, the
  // boss rush, the save system, talks: per player, never on the list.
  for (const auto& e : StoryList::defaults().entries()) {
    for (const auto& name : e.flag_names) {
      INFO(e.name << " " << name);
      for (const char* no : {"Sequence_", "UI_", "Navi", "System_", "DreamWorld", "PlayerStatus",
                             "HaveTalked", "IsChildhood"}) {
        CHECK(name.find(no) == std::string::npos);
      }
    }
  }
}

TEST_CASE("story list: the blue warps' cutscenes are solo, as in ZMP (D-067)") {
  const StoryList list = StoryList::defaults();
  std::vector<std::string> solo;
  for (const auto& e : list.entries()) {
    if (e.solo) solo.push_back(e.name);
  }
  CHECK(solo == std::vector<std::string>{
                    "KokiriForest_WarpFromInsideTheDekuTree", "Get_StoneOfFire_Complete",
                    "Get_StoneOfWater_Complete", "ForestTemple_SageOfForest_SageRoom",
                    "ForestTemple_SageOfForest_EscapeFromTempleOutSide", "FireTemple_SageOfFire_SageRoom",
                    "Demo_SageOfFire_Arrive_FireBolero", "FireTemple_SageOfFire_Kakariko", "SageOfWater_SageRoom",
                    "Demo_SageOfWater_Arrive_WaterSerenade", "ShadowTemple_SageOfShadow_SageRoom",
                    "ShadowTemple_Warp_Graveyard", "SpiritTemple_SageOfSpirit_SageRoom",
                    "SpiritTemple_Level_Room_34_Escape"});
  // Their flags travel all the same: the Deku Tree's death moves two steps.
  const auto& tree = list.entries()[*list.find("KokiriForest_WarpFromInsideTheDekuTree")];
  CHECK(tree.steps.size() == 2);
  CHECK(tree.flags.size() == 5);  // the five timelines' HavePlayedEvent
}

namespace {

// Each name of the built-in list with its hash as GameDataList.Product.110
// has it (its "Hash" field: Bool for the HavePlayedEvent flags and the Bool,
// Enum for the steps), written out here and recomputed with the code's own
// hash (protocol::flag_hash). Every flag of the list must be in this table.
struct GameDataName {
  const char* name;
  std::uint32_t hash;
};
// Each step's values as GameDataList has them: the Enum entry's "Values"
// (hashes, in its order, which is the order the story moves forward in) and
// its "RawValues" (their names, same order). The game's own table in memory
// (entry +0x28 count, +0x30 hashes) is in the same order (BANDERAS_ENUM.md).
struct GameDataStep {
  std::uint32_t hash;
  std::vector<std::uint32_t> values;
  std::vector<std::string> names;
};
#include "story_list_data.inc"  // kGameDataNames, game_data_steps()

std::vector<std::string> list_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    if (line.find_first_not_of(" \t\r") != std::string::npos) lines.push_back(line);
    at = end + 1;
  }
  return lines;
}

}  // namespace

TEST_CASE("story list: every line of the built-in list and the kit's file parses on its own") {
  std::ifstream file(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_story.txt",
                     std::ios::binary);
  REQUIRE(file);
  const std::string kit((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  CHECK(kit == kDefaultStoryList);  // the same text, comments too
  const auto lines = list_lines(kDefaultStoryList);
  REQUIRE(lines.size() == StoryList::defaults().size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    INFO(lines[i]);
    std::string error;
    const auto one = StoryList::parse(lines[i], &error);
    REQUIRE_MESSAGE(one.has_value(), error);
    REQUIRE(one->size() == 1);
    const auto& e = one->entries()[0];
    CHECK(valid_event_name(e.name));
    CHECK(e.name == StoryList::defaults().entries()[i].name);  // in the list's order
    CHECK(e.played_hash == proto::flag_hash(played_flag_name(e.name)));
    for (const auto& name : e.flag_names) {
      // Only names: a 0x hash would hide what the flag is.
      CHECK(name.rfind("0x", 0) == std::string::npos);
    }
  }
}

TEST_CASE("story list: every flag's hash is GameDataList's, recomputed with the code's hash") {
  auto known = [](std::uint32_t hash) {
    for (const auto& g : kGameDataNames) {
      if (g.hash == hash) return true;
    }
    return false;
  };
  for (const auto& g : kGameDataNames) {
    INFO(g.name);
    CHECK(proto::flag_hash(g.name) == g.hash);
  }
  const StoryList list = StoryList::defaults();
  for (const auto& e : list.entries()) {
    INFO(e.name);
    CHECK(known(e.played_hash));
    for (std::size_t i = 0; i < e.flags.size(); ++i) {
      INFO(e.flag_names[i]);
      CHECK(known(e.flags[i].hash));
      CHECK(e.flags[i].hash == proto::flag_hash(e.flag_names[i]));
    }
    for (const auto& step : e.steps) {
      INFO(step.name);
      CHECK(known(step.hash));
      CHECK(step.hash == proto::flag_hash(step.name));
    }
  }
  // Nothing in the table the list does not use.
  const auto keys = list.shared_flags();
  CHECK(keys.size() == std::size(kGameDataNames));
}

TEST_CASE("story list: each step's values are GameDataList's, first the earliest") {
  const StoryList list = StoryList::defaults();
  const EnumFlags steps = list.step_flags();
  CHECK(steps.size() == game_data_steps().size());
  std::size_t lines_with_steps = 0;
  for (const auto& e : list.entries()) {
    if (!e.steps.empty()) ++lines_with_steps;
    for (const auto& step : e.steps) {
      INFO(e.name << " " << step.name);
      const auto it = std::find_if(game_data_steps().begin(), game_data_steps().end(),
                                   [&](const GameDataStep& g) { return g.hash == step.hash; });
      REQUIRE(it != game_data_steps().end());
      CHECK(step.values == it->values);
      CHECK(step.value_names == it->names);
      for (std::size_t i = 0; i < step.values.size(); ++i) {
        CHECK(step.values[i] == proto::flag_hash(step.value_names[i]));
        // The order it moves forward in.
        CHECK(steps.rank(step.hash, step.values[i]) == std::optional<std::size_t>(i));
      }
      CHECK(step.value_names.front() == "NotReady");  // a new game's value (DefaultValue)
      CHECK(step.value_names.back() == "Complete");
    }
  }
  CHECK(lines_with_steps == 59);
  // A step on two lines has the same values on both (the parser refuses others).
  const auto& meet = list.entries()[*list.find(kMeet)];
  const auto& inside = list.entries()[*list.find("InsideTheDekuTree_Arrive")];
  REQUIRE(inside.steps.size() == 1);
  CHECK(inside.steps[0] == meet.steps[1]);
  const auto& kaepora = list.entries()[*list.find("HyruleField_Goto_HyruleCastle_Kaepora")];
  const auto& zelda = list.entries()[*list.find("HyruleCastle_Goto_HyruleCastle_MeetZelda")];
  REQUIRE(zelda.steps.size() == 2);
  CHECK(zelda.steps[0] == kaepora.steps.at(0));
  CHECK(zelda.steps[1].name == "Step_DoorOfTime");
  CHECK(list.value_name(0x378913cau, proto::flag_hash("Srep2_1_1")) == "Srep2_1_1");
}

TEST_CASE("story list: the kit's shared_story.txt is the built-in list") {
  std::string error;
  const auto kit = StoryList::load(COOP_TEST_SOURCE_DIR "/../tools/release/kit/shared_story.txt", &error);
  REQUIRE_MESSAGE(kit.has_value(), error);
  const StoryList built_in = StoryList::defaults();
  REQUIRE(kit->size() == built_in.size());
  for (std::size_t i = 0; i < kit->size(); ++i) {
    CHECK(kit->entries()[i].name == built_in.entries()[i].name);
    CHECK(kit->entries()[i].flags == built_in.entries()[i].flags);
    CHECK(kit->entries()[i].steps == built_in.entries()[i].steps);
  }
}

TEST_CASE("story list: parsed from text, bad lines are refused with their number") {
  std::string error;
  const auto list = StoryList::parse("# comment\n\n  Ev_One  0x1234 Some.Flag  # tail\r\nEv_Two\n", &error);
  REQUIRE(list.has_value());
  REQUIRE(list->size() == 2);
  CHECK(list->entries()[0].flags ==
        std::vector<proto::FlagKey>{{0x1234u, std::uint16_t{0}}, {proto::flag_hash("Some.Flag"), std::uint16_t{0}}});
  CHECK(list->entries()[1].flags.empty());
  CHECK(list->entries()[1].played_hash == proto::flag_hash("HavePlayedEvent.Ev_Two"));
  CHECK(list->shared_flags().size() == 4);

  CHECK_FALSE(StoryList::parse("Ev_One\nEv.Two\n", &error));
  CHECK(error == "line 2: an event name is 1 to 127 of A-Z a-z 0-9 _");
  CHECK_FALSE(StoryList::parse("Ev_One\nEv_One\n", &error));
  CHECK(error == "line 2: Ev_One is listed twice");
  CHECK_FALSE(StoryList::parse("Ev_One 0x123456789\n", &error));
  CHECK_FALSE(StoryList::parse("Ev_One 0xzz\n", &error));
  CHECK(StoryList::parse("", &error)->size() == 0);
  CHECK_FALSE(StoryList::load("no/such/shared_story.txt", &error));
}

TEST_CASE("story list: solo after the name, once") {
  std::string error;
  const auto list = StoryList::parse("Ev_One solo Some.Flag step:Step_A=X,Y\nEv_Two Other.Flag\n", &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  CHECK(list->entries()[0].solo);
  CHECK(list->entries()[0].flags ==
        std::vector<proto::FlagKey>{{proto::flag_hash("Some.Flag"), std::uint16_t{0}}});  // not "solo"
  CHECK(list->entries()[0].steps.size() == 1);
  CHECK_FALSE(list->entries()[1].solo);
  CHECK(list->shared_flags().size() == 2 + 2 + 1);  // two HavePlayedEvent, two Bools, a step
  CHECK_FALSE(StoryList::parse("Ev_One solo solo\n", &error));
  CHECK(error == "line 1: solo is written twice");
}

TEST_CASE("story list: near=M after the name, metres 1 to 1000, once (ORM, ZMP D-188)") {
  std::string error;
  const auto list = StoryList::parse("Ev_One near=30 Some.Flag\nEv_Two solo near=2.5\nEv_Three\n", &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  CHECK(list->entries()[0].near_m == doctest::Approx(30.0f));
  CHECK(list->entries()[0].flags ==
        std::vector<proto::FlagKey>{{proto::flag_hash("Some.Flag"), std::uint16_t{0}}});  // not "near=30"
  CHECK(list->entries()[1].near_m == doctest::Approx(2.5f));
  CHECK(list->entries()[1].solo);
  CHECK(list->entries()[2].near_m == 0.0f);  // the whole scene
  for (const char* bad : {"Ev near=\n", "Ev near=0\n", "Ev near=0.5\n", "Ev near=1001\n", "Ev near=-3\n",
                          "Ev near=3m\n", "Ev near=1e3\n", "Ev near=123456789\n"}) {
    INFO(bad);
    CHECK_FALSE(StoryList::parse(bad, &error));
    CHECK(error == "line 1: near=M is metres, 1 to 1000");
  }
  CHECK_FALSE(StoryList::parse("Ev near=30 near=40\n", &error));
  CHECK(error == "line 1: near= is written twice");
}

TEST_CASE("story list: the six trials' barriers are their rooms' (near), the escape and the end are not") {
  const StoryList list = StoryList::defaults();
  for (const char* trial : {"Forest", "Fire", "Water", "Shadow", "Spirit", "Light"}) {
    const std::string name = std::string("InsideGanonCastle_Dungeon_BarrierRemove_") + trial;
    INFO(name);
    REQUIRE(list.contains(name));
    const auto& e = list.entries()[*list.find(name)];
    CHECK(e.near_m == doctest::Approx(30.0f));
    CHECK_FALSE(e.solo);
    // The barrier itself and the count of barriers down still travel to everyone.
    CHECK(std::find(e.flags.begin(), e.flags.end(), proto::FlagKey{proto::flag_hash(name), std::uint16_t{0}}) !=
          e.flags.end());
    CHECK(std::find(e.flags.begin(), e.flags.end(),
                    proto::FlagKey{proto::flag_hash("InsideGanonCastle_IsAllBarrierClear"), std::uint16_t{0}}) !=
          e.flags.end());
  }
  std::size_t near_lines = 0;
  for (const auto& e : list.entries()) {
    if (e.near_m > 0.0f) ++near_lines;
  }
  CHECK(near_lines == 6);
  // The escape's floors, the Master Sword, the sages' bind and the ending: the whole scene.
  for (const char* all : {"CrumblingGanonCastle_Arrive", "EscapeFromCrumblingGanonCastle_2F_FallDown",
                          "EscapeFromCrumblingGanonCastle_EscapeComplete",
                          "InsideGanonCastle_Dungeon_Boss_GetMasterSword", "InsideGanonCastle_Dungeon_BossBind0",
                          "InsideGanonCastle_Dungeon_BossDie_2", "InsideGanonCastle_Dungeon_StaffRoll_HyruleCastle"}) {
    INFO(all);
    REQUIRE(list.contains(all));
    const auto& e = list.entries()[*list.find(all)];
    CHECK(e.near_m == 0.0f);
    CHECK_FALSE(e.solo);
  }
}

TEST_CASE("story list: HavePlayedEvent of each listed event read from the game's Bool table") {
  Readable game;
  const StoryList list = StoryList::defaults();
  const auto meet = list.entries()[*list.find(kMeet)].played_hash;
  const auto lost = list.entries()[*list.find("KokiriForest_ArriveLostWood")].played_hash;
  const auto mido =
      list.entries()[*list.find("KokiriForest_Get_StoneOfForest_Completed_Event")].played_hash;
  bool_table(game.mem, {{meet, true}, {lost, false}, {mido, false}});
  const auto played = read_story_played(game, list);
  REQUIRE(played.size() == list.size());
  CHECK(played[*list.find(kMeet)] == std::optional<bool>(true));
  CHECK(played[*list.find("KokiriForest_ArriveLostWood")] == std::optional<bool>(false));
  CHECK_FALSE(played[*list.find("InsideTheDekuTree_Arrive")].has_value());  // not in the table
  Readable none;  // no flag manager: nothing known
  for (const auto& p : read_story_played(none, list)) CHECK_FALSE(p.has_value());
}

// ---- The owner -----------------------------------------------------------------

TEST_CASE("story: our own listed event makes us the owner: start once, end after it is over") {
  Game g;
  g.tick();
  g.event(std::nullopt);  // the edge; the name is not readable on the first frame
  auto out = g.tick();
  CHECK(out.send.empty());
  g.event(std::string(kMeet));
  out = g.tick();
  REQUIRE(out.send.size() == 1);
  const auto& s = out.send[0];
  CHECK(s.kind == proto::StoryKind::start);
  CHECK(s.player_number == proto::kPlayerSelf);
  CHECK(s.scene == kForest);
  CHECK(s.name == kMeet);
  CHECK(s.position == kSpot);
  CHECK(s.server_time_ms == static_cast<std::uint32_t>(g.in.server_now_us / 1000));
  CHECK(logged(out, "owner: our game started"));
  CHECK(g.story.owned() == std::optional<std::string>(kMeet));
  CHECK(proto::encode(proto::Message{s}).has_value());  // the codec takes it

  // The timelines it plays change the name: nothing more.
  g.event(std::string("Dm_Kokiri_0070"));
  CHECK(g.tick().send.empty());
  g.event(std::string(kMeet));
  CHECK(g.tick().send.empty());
  // A short gap between two of its parts is not its end.
  g.no_event();
  CHECK(g.tick(1000).send.empty());
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "runs here again"));
  // Over for 1.5 s: end, once.
  g.no_event();
  CHECK(g.tick().send.empty());
  CHECK(g.tick(1400).send.empty());
  out = g.tick(200);
  REQUIRE(out.send.size() == 1);
  CHECK(out.send[0].kind == proto::StoryKind::end);
  CHECK(out.send[0].name == kMeet);
  CHECK(out.send[0].scene == kForest);
  CHECK(g.tick(5000).send.empty());
  CHECK_FALSE(g.story.owned());

  // The same event again later (a replay): not shared again.
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "not shared again"));
}

TEST_CASE("story: events off the list, nameless ones and ones while loading are never shared") {
  Game g;
  for (const char* name : {"Get_StoneOfForest_GetSword", "Obj_Kokiri_Signboard_GuideAction_A_03",
                           "DmF_SY_GameOver", "Dm_Kokiri_0070", "DefaultBootEvent"}) {
    g.event(std::string(name));
    CHECK(g.tick().send.empty());
    g.no_event();
    CHECK(g.tick(3000).send.empty());
  }
  // The first name read decides: a listed name later in the same event is a
  // part of it, not a new start.
  g.event(std::string("NPC_Kokiri_DekuScrub_Talk"));
  g.tick();
  g.event(std::string(kMeet));
  CHECK(g.tick().send.empty());
  g.no_event();
  g.tick(3000);
  // No name within 1 s of the edge.
  g.event(std::nullopt);
  g.tick();
  auto out = g.tick(1100);
  CHECK(logged(out, "no name could be read"));
  g.event(std::string(kMeet));
  CHECK(g.tick().send.empty());
  g.no_event();
  g.tick(3000);
  // Scene unknown.
  g.in.scene.reset();
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "scene is unknown"));
}

TEST_CASE("story: an event the room played before we came is not shared again") {
  Game g;
  proto::StoryEvent played = start_from(2);
  played.kind = proto::StoryKind::played;
  g.story.receive(played);
  auto out = g.tick();
  CHECK(out.send.empty());
  CHECK_FALSE(out.ask);  // nothing asked of our game either
  CHECK(logged(out, "before we came"));
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "played in this room already"));
  // The same name in another scene is another event.
  Game h;
  played.scene = "KokiriForest_Adult";
  h.story.receive(played);
  h.tick();
  h.event(std::string(kMeet));
  CHECK(h.tick().send.size() == 1);
}

// ---- Following someone's start ---------------------------------------------------

TEST_CASE("story: a start for our scene asks our game for the same event, then it runs") {
  Game g;
  g.tick();
  g.story.receive(start_from(2));
  auto out = g.tick();
  CHECK(logged(out, "STORY_EVENT start received from P2 ('" + std::string(kMeet) +
                        "' in KokiriForest_Child): starting"));
  CHECK(out.ask == std::optional<std::string>(kMeet));
  CHECK(g.story.asking() == std::optional<std::string>(kMeet));
  CHECK(out.send.empty());
  // The game takes it: running with that name. Not ours to share.
  g.event(std::nullopt);
  out = g.tick(34);
  CHECK_FALSE(out.ask);  // an event runs: not asked again
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "event running '" + std::string(kMeet) + "' as P2 asked"));
  CHECK_FALSE(g.story.asking());
  CHECK_FALSE(g.story.owned());
  // Its end is not ours to send.
  g.no_event();
  g.tick();
  out = g.tick(2000);
  CHECK(out.send.empty());
  CHECK(logged(out, "(P2's) is over here"));
  // A second start of it (a repeat): already ran here.
  g.story.receive(start_from(3));
  out = g.tick();
  CHECK_FALSE(out.ask);
  CHECK(logged(out, "ignored, it already ran here"));
}

TEST_CASE("story: the host's save load forgets what ran: the cutscene is everyone's again (REVISION_HISTORIA H4)") {
  // The save's HavePlayedEvent 0s come back in every game (FLAG_RESET): the
  // cutscene plays again, and the first game to play it owns it again.
  Game g;
  proto::StoryEvent played = start_from(2);
  played.kind = proto::StoryKind::played;
  g.story.receive(played);
  g.tick();
  g.event(std::string(kMeet));
  auto out = g.tick();
  CHECK(out.send.empty());  // the room played it
  g.no_event();
  g.tick(3000);

  g.story.host_reset();
  out = g.tick();
  CHECK(logged(out, "the host loaded a save"));
  g.event(std::string(kMeet));
  out = g.tick();
  REQUIRE(out.send.size() == 1);
  CHECK(out.send[0].kind == proto::StoryKind::start);
  g.no_event();
  g.tick();
  out = g.tick(2000);
  REQUIRE(out.send.size() == 1);
  CHECK(out.send[0].kind == proto::StoryKind::end);

  // It ran here: another start is not followed, until the host loads again.
  g.story.receive(start_from(3));
  out = g.tick();
  CHECK(logged(out, "ignored, it already ran here"));
  g.story.host_reset();
  g.story.receive(start_from(3));
  out = g.tick();
  CHECK(out.ask == std::optional<std::string>(kMeet));
}

TEST_CASE("story: our game is asked every 0.5 s and given up on after 3 s") {
  Game g;
  g.story.receive(start_from(2));
  int asks = 0;
  StoryOutput out;
  for (int i = 0; i < 200 && (i == 0 || g.story.asking()); ++i) {
    out = g.tick(50);
    if (out.ask) ++asks;
  }
  CHECK(asks == 6);  // at 0, 0.5, 1, 1.5, 2, 2.5 s
  CHECK(logged(out, "rejected, the game did not start it in 3000 ms (6 asks)"));
  CHECK_FALSE(g.story.asking());
  // Another event running in between holds the asks back.
  Game h;
  h.story.receive(start_from(2));
  CHECK(h.tick().ask);
  h.event(std::string("DefaultBootEvent"));
  for (int i = 0; i < 10; ++i) CHECK_FALSE(h.tick(100).ask);
  h.no_event();
  CHECK(h.tick().ask);
  // The asked event after another one's name: still confirmed.
  h.event(std::string("DefaultBootEvent"));
  h.tick();
  h.event(std::string(kMeet));
  CHECK(logged(h.tick(), "as P2 asked"));
}

TEST_CASE("story: a start is ignored in another scene, loading, down, busy, or played here") {
  auto ignored = [](Game& g, const proto::StoryEvent& m, const std::string& why) {
    g.story.receive(m);
    const auto out = g.tick();
    CHECK_FALSE(out.ask);
    CHECK(logged(out, "ignored, " + why));
  };
  {
    Game g;
    ignored(g, start_from(2, "KokiriForest_Adult"), "we are in KokiriForest_Child");
    CHECK_FALSE(g.story.asking());
  }
  {
    Game g;
    g.in.scene.reset();
    ignored(g, start_from(2), "we are loading");
  }
  {
    Game g;
    g.in.downed = true;
    ignored(g, start_from(2), "we are down");
  }
  {
    Game g;
    g.event(std::string("Obj_Kokiri_Signboard_GuideAction_A_03"));
    g.tick();
    ignored(g, start_from(2), "our own event runs ('Obj_Kokiri_Signboard_GuideAction_A_03')");
  }
  {
    Game g;
    ignored(g, start_from(2, kForest, "Get_StoneOfForest_GetSword"), "not in our list");
  }
  {
    Game g;  // HavePlayedEvent already set when we first read it
    g.set_played(kMeet, true);
    g.tick();
    ignored(g, start_from(2), "HavePlayedEvent." + std::string(kMeet) + " is set here");
  }
  {
    Game g;  // asking for one already
    g.story.receive(start_from(2, kForest, "KokiriForest_ArriveLostWood"));
    CHECK(g.tick().ask);
    ignored(g, start_from(3), "we are asking for 'KokiriForest_ArriveLostWood' already");
  }
}

TEST_CASE("story: HavePlayedEvent set just before the start may be the owner's own: still asked") {
  Game g;
  g.tick();  // read once: not set
  g.set_played(kMeet, true);  // set 1 s before the start comes (shared from the owner)
  g.tick();
  g.tick(1000);
  g.story.receive(start_from(2));
  CHECK(g.tick().ask == std::optional<std::string>(kMeet));

  Game h;
  h.tick();
  h.set_played(kMeet, true);  // set 4 s before: played here
  h.tick();
  h.tick(4000);
  h.story.receive(start_from(2));
  const auto out = h.tick();
  CHECK_FALSE(out.ask);
  CHECK(logged(out, "is set here"));

  Game k;  // unreadable readings change nothing
  k.tick();
  k.in.played.assign(k.story.list().size(), std::nullopt);
  k.tick(4000);
  k.story.receive(start_from(2));
  CHECK(k.tick().ask);
}

TEST_CASE("story: the asks stop when we leave the scene or go down") {
  Game g;
  g.story.receive(start_from(2));
  CHECK(g.tick().ask);
  g.in.scene = std::string("LostWoods");
  auto out = g.tick(600);
  CHECK_FALSE(out.ask);
  CHECK(logged(out, "rejected, we left KokiriForest_Child"));
  Game h;
  h.story.receive(start_from(2));
  CHECK(h.tick().ask);
  h.in.downed = true;
  out = h.tick(600);
  CHECK_FALSE(out.ask);
  CHECK(logged(out, "rejected, we went down"));
}

TEST_CASE("story: someone's end is only logged") {
  Game g;
  proto::StoryEvent end = start_from(2);
  end.kind = proto::StoryKind::end;
  g.story.receive(end);
  const auto out = g.tick();
  CHECK_FALSE(out.ask);
  CHECK(out.send.empty());
  CHECK(logged(out, "P2's '" + std::string(kMeet) + "' in KokiriForest_Child is over there"));
  // A start that came after: still followed.
  g.story.receive(start_from(2));
  CHECK(g.tick().ask);
}

// ---- Skipping with the owner (ZMP D-048) -------------------------------------------

namespace {

constexpr std::uint64_t kPlus = 1ull << 10;
constexpr std::uint64_t kButtonA = 1ull << 0;

proto::StoryEvent end_from(std::uint8_t player, const std::string& name = kMeet,
                           proto::StoryKind kind = proto::StoryKind::end) {
  proto::StoryEvent m = start_from(player, kForest, name);
  m.kind = kind;
  return m;
}

proto::StoryEvent skipped_from(std::uint8_t player, const std::string& name = kMeet) {
  return end_from(player, name, proto::StoryKind::skipped);
}

// A game following P2's start, its copy running.
void following(Game& g) {
  g.tick();
  g.story.receive(start_from(2));
  REQUIRE(g.tick().ask);
  g.event(std::string(kMeet));
  REQUIRE(logged(g.tick(), "as P2 asked"));
  g.event(std::string("Dm_Kokiri_0070"));
  g.tick();
}

// A game that owns the meeting, its copy running, the pad read with nothing held.
void owning(Game& g) {
  g.in.pad = 0;
  g.tick();
  g.event(std::string(kMeet));
  REQUIRE(g.tick().send.size() == 1);
  REQUIRE(g.story.owned() == std::optional<std::string>(kMeet));
}

// Our owned event ends: its last word, after end_quiet_us of quiet.
proto::StoryEvent owner_end(Game& g, StoryOutput* last = nullptr) {
  g.no_event();
  g.tick();
  StoryOutput out = g.tick(1600);
  REQUIRE(out.send.size() == 1);
  if (last != nullptr) *last = out;
  return out.send[0];
}

int skips(const std::vector<StoryOutput>& outs) {
  return static_cast<int>(std::count_if(outs.begin(), outs.end(), [](const StoryOutput& o) { return o.skip; }));
}

}  // namespace

TEST_CASE("story: the owner's skip button just before its end sends skipped, saying how (D-048)") {
  Game g;
  owning(g);
  g.tick(10'000);
  g.in.pad = kPlus;  // the skip prompt
  auto out = g.tick();
  CHECK(logged(out, "skip pressed (PLUS) during our '" + std::string(kMeet) +
                        "': its end is `skipped` if it is over within 5000 ms"));
  g.in.pad = 0;
  g.tick(500);
  g.in.pad = kPlus;  // its confirm: the last press counts
  g.tick();
  g.in.pad = 0;
  g.tick(784);  // the game ends it 0.8 s after
  StoryOutput last;
  const auto m = owner_end(g, &last);
  CHECK(m.kind == proto::StoryKind::skipped);
  CHECK(m.name == kMeet);
  CHECK(logged(last, "'" + std::string(kMeet) + "' is over here, skipped by us (PLUS, 800 ms before its end); "
                     "STORY_EVENT skipped to the others"));
}

TEST_CASE("story: a natural end is a plain end: text presses, an old skip press, or none") {
  SUBCASE("no skip press, only A for the texts") {
    Game g;
    owning(g);
    for (int i = 0; i < 20; ++i) {
      g.in.pad = kButtonA;
      g.tick(100);
      g.in.pad = 0;
      g.tick(1900);
    }
    StoryOutput last;
    CHECK(owner_end(g, &last).kind == proto::StoryKind::end);
    CHECK(logged(last, "is over here, not skipped (no skip press); STORY_EVENT end to the others"));
  }
  SUBCASE("PLUS pressed long before the end (a prompt let go)") {
    Game g;
    owning(g);
    g.in.pad = kPlus;
    g.tick();
    g.in.pad = 0;
    g.tick(5984);
    StoryOutput last;
    CHECK(owner_end(g, &last).kind == proto::StoryKind::end);
    CHECK(logged(last, "not skipped (the last skip press 6000 ms before its end)"));
  }
  SUBCASE("PLUS held from before the event: no press") {
    Game g;
    g.in.pad = kPlus;
    g.tick();
    g.event(std::string(kMeet));
    REQUIRE(g.tick().send.size() == 1);
    g.tick(500);
    CHECK(owner_end(g).kind == proto::StoryKind::end);
  }
  SUBCASE("the pad not read: never skipped") {
    Game g;
    g.tick();
    g.event(std::string(kMeet));
    REQUIRE(g.tick().send.size() == 1);
    CHECK(owner_end(g).kind == proto::StoryKind::end);
  }
}

TEST_CASE("story: the test script's skip counts as the owner's press, a mate's press never sends") {
  Game g;
  owning(g);
  g.story.note_skip("test skip");
  CHECK(logged(g.tick(), "skip pressed (test skip)"));
  StoryOutput last;
  CHECK(owner_end(g, &last).kind == proto::StoryKind::skipped);
  CHECK(logged(last, "skipped by us (test skip, "));

  // A follower's press: his copy is not his to share; nothing is sent.
  Game f;
  following(f);
  f.in.pad = kPlus;
  f.story.note_skip("test skip");
  f.tick();
  f.in.pad = 0;
  f.no_event();
  f.tick();
  const auto out = f.tick(2000);
  CHECK(out.send.empty());
  CHECK(logged(out, "(P2's) is over here"));
}

TEST_CASE("story: the owner's skipped skips what is left of our copy at once, a shot every 0.5 s") {
  Game g;
  following(g);
  g.story.receive(skipped_from(2));
  auto out = g.tick();
  CHECK(logged(out, "P2 skipped '" + std::string(kMeet) + "' in KokiriForest_Child; ours is skipped too"));
  CHECK(out.skip);
  CHECK(logged(out, "force skip of our 'Dm_Kokiri_0070' (P2's '" + std::string(kMeet) + "' was skipped; skip 1)"));
  CHECK(out.send.empty());  // never ours to send
  CHECK(g.story.skipping() == std::optional<std::string>(kMeet));
  CHECK_FALSE(g.tick(400).skip);
  // The skip cut a shot: the next timeline runs, skipped 0.5 s after the first.
  g.event(std::string("Dm_Kokiri_0075"));
  out = g.tick(100);
  CHECK(out.skip);
  CHECK(logged(out, "force skip of our 'Dm_Kokiri_0075'"));
  g.no_event();
  out = g.tick();
  CHECK_FALSE(out.skip);
  CHECK(logged(out, "'" + std::string(kMeet) + "' skipped here after P2's: over after 2 force skips"));
  CHECK_FALSE(g.story.skipping());
  // An event after it is never skipped.
  g.event(std::string("NPC_Kokiri_Talk"));
  for (int i = 0; i < 20; ++i) CHECK_FALSE(g.tick(500).skip);
}

TEST_CASE("story: a plain end never cuts a mate behind, however far behind") {
  Game g;
  following(g);
  g.story.receive(end_from(2));
  std::vector<StoryOutput> outs;
  outs.push_back(g.tick());
  CHECK(logged(outs.back(), "P2's '" + std::string(kMeet) + "' in KokiriForest_Child is over there (not skipped)"));
  for (int i = 0; i < 120; ++i) outs.push_back(g.tick(500));  // a minute more of his own texts
  CHECK(skips(outs) == 0);
  CHECK_FALSE(g.story.skipping());
  g.no_event();
  g.tick();
  CHECK(logged(g.tick(2000), "(P2's) is over here"));
}

TEST_CASE("story: only the skip of the copy we follow skips ours, and our own event is never skipped") {
  Game g;
  following(g);
  g.story.receive(skipped_from(3));  // not the owner we follow
  g.story.receive(skipped_from(2, "KokiriForest_ArriveLostWood"));  // another event
  std::vector<StoryOutput> outs;
  for (int i = 0; i < 40; ++i) outs.push_back(g.tick(250));
  CHECK(skips(outs) == 0);
  CHECK_FALSE(g.story.skipping());

  // Ours: a skipped for it (a stray, the server drops them) changes nothing.
  Game o;
  owning(o);
  o.story.receive(skipped_from(2));
  outs.clear();
  for (int i = 0; i < 40; ++i) outs.push_back(o.tick(250));
  CHECK(skips(outs) == 0);
}

TEST_CASE("story: a start still asked for when its owner skips it is not asked any more") {
  Game g;
  g.tick();
  g.story.receive(start_from(2));
  REQUIRE(g.tick().ask);
  g.story.receive(skipped_from(2));
  auto out = g.tick(100);
  CHECK(logged(out, "skipped '" + std::string(kMeet) +
                        "' in KokiriForest_Child: not asked any more, skipped before it started here (1 ask)"));
  CHECK_FALSE(out.ask);
  CHECK_FALSE(g.story.asking());
  for (int i = 0; i < 10; ++i) CHECK_FALSE(g.tick(500).ask);
  // The game took the last ask anyway: not played, skipped at once.
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.skip);
  CHECK(out.send.empty());
  CHECK(logged(out, "played in this room already (P2's) and skipped there: not shared again, ours is skipped"));

  // A plain end: the asks go on, and it plays.
  Game h;
  h.tick();
  h.story.receive(start_from(2));
  REQUIRE(h.tick().ask);
  h.story.receive(end_from(2));
  h.tick(100);
  CHECK(h.story.asking() == std::optional<std::string>(kMeet));
}

TEST_CASE("story: a latecomer's own start of an event the room skipped is skipped at once, of one it ended plays") {
  // The server sends a joiner `played` and then the owner's last word.
  proto::StoryEvent played = start_from(2);
  played.kind = proto::StoryKind::played;
  Game g;
  g.story.receive(played);
  g.story.receive(skipped_from(2));
  auto out = g.tick();
  CHECK(logged(out, "before we came"));
  CHECK(logged(out, "P2 skipped"));
  CHECK_FALSE(out.skip);  // nothing runs here
  g.event(std::string(kMeet));
  out = g.tick();
  CHECK(out.skip);
  CHECK(out.send.empty());
  g.no_event();
  CHECK(logged(g.tick(), "skipped here after P2's: over after 1 force skip"));

  // Ended there, not skipped: ours plays to its end.
  Game e;
  e.story.receive(played);
  e.story.receive(end_from(2));
  e.tick();
  e.event(std::string(kMeet));
  std::vector<StoryOutput> outs;
  outs.push_back(e.tick());
  CHECK(logged(outs.back(), "played in this room already (P2's); not shared again, ours is skipped only if its "
                            "owner skips theirs"));
  for (int i = 0; i < 40; ++i) outs.push_back(e.tick(500));
  CHECK(skips(outs) == 0);

  // Still playing there: ours plays, and is skipped if its owner skips.
  Game h;
  h.story.receive(played);
  h.tick();
  h.event(std::string(kMeet));
  CHECK_FALSE(h.tick().skip);
  h.story.receive(skipped_from(2));
  CHECK(h.tick().skip);
}

TEST_CASE("story: the skips stop on a downed Link and give up after 8 s, and a save load forgets the skips") {
  Game g;
  following(g);
  g.story.receive(skipped_from(2));
  std::vector<StoryOutput> outs;
  for (int i = 0; i < 100; ++i) outs.push_back(g.tick(100));  // 10 s, still running
  CHECK(skips(outs) == 16);  // at once, then every 0.5 s up to 7.5 s
  CHECK(std::any_of(outs.begin(), outs.end(), [](const StoryOutput& o) {
    return logged(o, "still runs 8000 ms after its skips began (16 force skips); given up, it plays on");
  }));
  CHECK_FALSE(g.story.skipping());

  Game d;
  following(d);
  d.in.downed = true;
  d.story.receive(skipped_from(2));
  const auto out = d.tick();
  CHECK_FALSE(out.skip);
  CHECK(logged(out, "not skipped: we are down"));
  CHECK_FALSE(d.story.skipping());

  // The host loads a save: the room's skips are forgotten with what it played.
  Game r;
  proto::StoryEvent played = start_from(2);
  played.kind = proto::StoryKind::played;
  r.story.receive(played);
  r.story.receive(skipped_from(2));
  r.tick();
  r.story.host_reset();
  r.tick();
  r.event(std::string(kMeet));
  const auto again = r.tick();
  CHECK_FALSE(again.skip);
  CHECK(again.send.size() == 1);  // ours again, shared again
}

TEST_CASE("story: each HavePlayedEvent flip is logged once") {
  Game g;
  const std::string flag = "HavePlayedEvent." + std::string(kMeet);
  auto out = g.tick();
  CHECK_FALSE(logged(out, flag));  // 0 at the first reading: nothing to say
  g.set_played(kMeet, true);
  out = g.tick();
  CHECK(logged(out, flag + " 0 -> 1"));
  CHECK_FALSE(logged(g.tick(), flag));  // still 1: once
  g.in.played[*g.story.list().find(kMeet)] = std::nullopt;  // unreadable: as it was
  CHECK_FALSE(logged(g.tick(), flag));
  g.set_played(kMeet, false);
  CHECK(logged(g.tick(), flag + " 1 -> 0"));
  Game h;
  h.set_played(kMeet, true);
  CHECK(logged(h.tick(), flag + " is 1 (set before we came)"));
}

// ---- End state: the Bools and story steps a cutscene sets -------------------

TEST_CASE("story list: the Deku Tree meeting carries its end state, steps in GameDataList order") {
  const StoryList list = StoryList::defaults();
  const auto& meet = list.entries()[*list.find(kMeet)];
  CHECK(meet.flags.back() == proto::FlagKey{0x9a60a460u, std::uint16_t{0}});  // OpenDekuTreeMouth
  REQUIRE(meet.steps.size() == 2);
  // The values as GameDataList lists them (Enum "Values", in its order).
  CHECK(meet.steps[0].hash == 0x3372689bu);
  CHECK(meet.steps[0].values == std::vector<std::uint32_t>{1471695344u, 398436934u, 3949112793u,
                                                            240843480u, 1871805950u, 1275749364u});
  CHECK(meet.steps[1].hash == 0x7d776b61u);
  CHECK(meet.steps[1].values ==
        std::vector<std::uint32_t>{1471695344u, 398436934u, 3592842818u, 3957372343u, 3051307979u,
                                   1466071034u, 3576369172u, 1275749364u});
  CHECK(meet.steps[1].value_names[2] == "GoInside");
  // Mido's and Saria's lines move no step (the step stays where it was).
  CHECK(list.entries()[*list.find("KokiriForest_Get_StoneOfForest_Completed_Event")].steps.empty());
  CHECK(list.entries()[*list.find("KokiriForest_ArriveLostWood")].steps.empty());
  // Opted in as steps (only forward), each once.
  const EnumFlags steps = list.step_flags();
  CHECK(steps.size() == 17);
  CHECK(steps.steps(0x3372689bu));
  CHECK(steps.steps(0x7d776b61u));
  CHECK(steps.steps(0xdb89c1a9u));  // Step_Goto_HyruleCastle
  CHECK(steps.steps(0x378913cau));  // Step_DoorOfTime
  CHECK(steps.steps(proto::flag_hash("Step_Defeat_Ganon")));
  // Not the minigames, the escape's countdown or the trade chains: each game's
  // own (Step_Get_Epona is D-R19's, on the adult branch).
  for (const char* off : {"Step_EscapeFromCrumblingGanonCastle", "Step_MiniGame_Fishing",
                          "Step_SubEvent_GetSwordBigGoron", "Step_SubEvent_MaskSalesman"}) {
    INFO(off);
    CHECK_FALSE(steps.steps(proto::flag_hash(off)));
  }
  CHECK(steps.rank(0x3372689bu, proto::flag_hash("Complete")) == 5u);
  CHECK(steps.rank(0x7d776b61u, proto::flag_hash("GoInside")) == 2u);
  CHECK(list.value_name(0x3372689bu, 0x4c0a63f4u) == "Complete");
  CHECK_FALSE(list.value_name(0x3372689bu, proto::flag_hash("GoInside")));
  CHECK_FALSE(list.value_name(0x1234u, 0x4c0a63f4u));
  // End flags: the Bools first (each once, in list order), then the steps
  // (each once, in the order they first appear).
  const auto ends = list.end_flags();
  REQUIRE(ends.size() == 176 + 17);
  CHECK(ends[0].name == "HavePlayedEvent.Dm_Kokiri_0060");  // Saria at Link's house
  CHECK(ends[3].name == "KokiriForest_Meet_DekuTree_OpenDekuTreeMouth");
  CHECK_FALSE(ends[3].step);
  CHECK_FALSE(ends[175].step);
  CHECK(ends[176].name == "Step_Meet_DekuTree");
  CHECK(ends[176].step);
  CHECK(ends[177].name == "Step_Get_StoneOfForest");  // once, though on many lines
  CHECK(ends[178].name == "Step_Goto_HyruleCastle");
  CHECK(ends[179].name == "Step_DoorOfTime");
  CHECK(ends.back().name == "Step_Defeat_Ganon");
  CHECK(ends.back().step);
}

TEST_CASE("story list: step:Name=Values, bad steps are refused") {
  std::string error;
  const auto list = StoryList::parse(
      "Ev_One Some.Bool step:Step_A=NotReady,Ready,0x10\n"
      "Ev_Two step:Step_A=NotReady,Ready,0x10 step:0x77=X\n",
      &error);
  REQUIRE_MESSAGE(list.has_value(), error);
  const auto& one = list->entries()[0];
  CHECK(one.flags == std::vector<proto::FlagKey>{{proto::flag_hash("Some.Bool"), std::uint16_t{0}}});
  REQUIRE(one.steps.size() == 1);
  CHECK(one.steps[0].name == "Step_A");
  CHECK(one.steps[0].hash == proto::flag_hash("Step_A"));
  CHECK(one.steps[0].values ==
        std::vector<std::uint32_t>{proto::flag_hash("NotReady"), proto::flag_hash("Ready"), 0x10u});
  CHECK(one.steps[0].value_names == std::vector<std::string>{"NotReady", "Ready", "0x10"});
  CHECK(list->step_flags().size() == 2);  // Step_A once, 0x77
  CHECK(list->shared_flags().size() == 2 + 1 + 2);  // two HavePlayedEvent, a Bool, two steps
  CHECK(list->end_flags().size() == 3);

  CHECK_FALSE(StoryList::parse("Ev step:Step_A\n", &error));
  CHECK(error == "line 1: a step is step:Name=Value1,Value2,...");
  CHECK_FALSE(StoryList::parse("Ev step:=A\n", &error));
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=\n", &error));
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=A,,B\n", &error));
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=A,0xzz\n", &error));
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=A,B,A\n", &error));
  CHECK(error == "line 1: a step lists a value once");
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=A step:Step_A=A\n", &error));
  CHECK(error == "line 1: Step_A is listed twice");
  CHECK_FALSE(StoryList::parse("Ev step:Step_A=A,B\nEv2 step:Step_A=B,A\n", &error));
  CHECK(error == "line 2: Step_A is listed with other values on Ev's line");
}

TEST_CASE("story list: the end flags read from the game, a Bool as 0 or 1, a step as its hash") {
  Readable w;
  const StoryList list = StoryList::defaults();
  bool_table(w.mem, {{0x9a60a460u, true}, {0x97137664u, false}});
  enum_table(w.mem, {{0x7d776b61u, proto::flag_hash("GoInside")},
                     {0x3372689bu, proto::flag_hash("Complete")}});
  const auto ends = list.end_flags();
  const auto got = read_story_flags(w, list);
  REQUIRE(got.size() == ends.size());
  for (std::size_t i = 0; i < ends.size(); ++i) {
    INFO(ends[i].name);
    if (ends[i].name == "KokiriForest_Meet_DekuTree_OpenDekuTreeMouth") {
      CHECK(got[i] == 1u);
    } else if (ends[i].name == "HavePlayedEvent.Dm_Kokiri_0070") {
      CHECK(got[i] == 0u);
    } else if (ends[i].name == "Step_Meet_DekuTree") {
      CHECK(got[i] == proto::flag_hash("Complete"));
    } else if (ends[i].name == "Step_Get_StoneOfForest") {
      CHECK(got[i] == proto::flag_hash("GoInside"));
    } else {
      CHECK_FALSE(got[i]);  // not in the fake game
    }
  }
}

TEST_CASE("story: each end-state flag is logged at its first reading and on each change") {
  Game g;
  const auto ends = g.story.list().end_flags();
  auto at = [&](const std::string& name) {
    for (std::size_t i = 0; i < ends.size(); ++i) {
      if (ends[i].name == name) return i;
    }
    FAIL("no such end flag");
    return std::size_t{0};
  };
  g.in.flags.assign(ends.size(), std::nullopt);
  const std::size_t mouth = at("KokiriForest_Meet_DekuTree_OpenDekuTreeMouth");
  const std::size_t meet = at("Step_Meet_DekuTree");
  g.in.flags[mouth] = 0u;
  g.in.flags[meet] = proto::flag_hash("NotReady");
  auto out = g.tick();
  CHECK(logged(out, "KokiriForest_Meet_DekuTree_OpenDekuTreeMouth is 0"));
  CHECK(logged(out, "Step_Meet_DekuTree is NotReady"));
  CHECK_FALSE(logged(out, "Step_Get_StoneOfForest"));  // unknown: nothing said
  CHECK(g.tick().log.empty());                          // no change: nothing
  g.in.flags[mouth] = 1u;
  g.in.flags[meet] = proto::flag_hash("Complete");
  out = g.tick();
  CHECK(logged(out, "KokiriForest_Meet_DekuTree_OpenDekuTreeMouth 0 -> 1"));
  CHECK(logged(out, "Step_Meet_DekuTree NotReady -> Complete"));
  g.in.flags[meet] = std::nullopt;  // unreadable: as it was
  CHECK(g.tick().log.empty());
  g.in.flags[meet] = 0x1234u;  // not a listed value: its hash
  CHECK(logged(g.tick(), "Step_Meet_DekuTree Complete -> 0x00001234 (not a listed value)"));
}

TEST_CASE("story: a solo event is only ours: no start sent, its played flag logged as any") {
  Game g;
  const std::string warp = "KokiriForest_WarpFromInsideTheDekuTree";
  g.tick();
  g.event(warp);
  auto out = g.tick();
  CHECK(out.send.empty());
  CHECK(logged(out, "'KokiriForest_WarpFromInsideTheDekuTree' is solo"));
  CHECK_FALSE(g.story.owned());
  g.event(std::string("Dm_Kokiri_0130"));
  CHECK(g.tick().send.empty());
  g.no_event();
  CHECK(g.tick(2000).send.empty());  // no end either
  g.set_played(warp, true);
  CHECK(logged(g.tick(), "HavePlayedEvent.KokiriForest_WarpFromInsideTheDekuTree 0 -> 1"));
}

TEST_CASE("story: a start for a solo event from elsewhere is ignored") {
  Game g;
  g.tick();
  g.story.receive(start_from(2, kForest, "KokiriForest_WarpFromInsideTheDekuTree"));
  const auto out = g.tick();
  CHECK(logged(out, "ignored, it is solo here"));
  CHECK_FALSE(out.ask);
}

// ---- near: a cutscene of one room (ORM, oot-remake docs/FINAL.md, ZMP D-188) -------------

namespace {

constexpr const char* kCastle = "InsideGanonCastle";
constexpr const char* kForestTrial = "InsideGanonCastle_Dungeon_BarrierRemove_Forest";
// Two trials' barrier rooms (Banc InsideGanonCastle, rooms 15 and 11: the centres of their
// placements), about 91 m apart. Which trial is which does not matter here.
const proto::Vec3 kForestCore{-94.3f, 100.0f, -21.0f};
const proto::Vec3 kWaterCore{-51.7f, 104.0f, -102.4f};

proto::StoryEvent barrier_from(std::uint8_t player, const proto::Vec3& where) {
  proto::StoryEvent m = start_from(player, kCastle, kForestTrial);
  m.position = where;
  return m;
}

}  // namespace

TEST_CASE("story: a trial's barrier falls for who is in its room, not for the other trials") {
  // Same room: 4 m from the one who broke it. Asked, as any shared event.
  Game same;
  same.in.scene = kCastle;
  same.in.position = proto::Vec3{-90.3f, 100.0f, -21.0f};
  same.tick();
  same.story.receive(barrier_from(3, kForestCore));
  auto out = same.tick();
  CHECK(logged(out, "STORY_EVENT start received from P3 ('" + std::string(kForestTrial) +
                        "' in InsideGanonCastle): starting"));
  CHECK(out.ask == std::optional<std::string>(kForestTrial));

  // Another trial, the same scene: not asked (ZMP D-188: no arc, no trip to the central
  // room, the silver rupees and blocks there stay as they are).
  Game other;
  other.in.scene = kCastle;
  other.in.position = kWaterCore;
  other.tick();
  other.story.receive(barrier_from(3, kForestCore));
  out = other.tick();
  CHECK(logged(out, "ignored, it is for the room of P3: we are 92.0 m from where it started (near=30)"));
  CHECK_FALSE(out.ask);
  CHECK_FALSE(other.story.asking());
  // The end state still comes by the shared flags; a later start is still judged by place.
  other.in.position = proto::Vec3{-92.0f, 100.0f, -25.0f};
  other.story.receive(barrier_from(4, kForestCore));
  out = other.tick();
  CHECK(logged(out, "STORY_EVENT start received from P4 ('" + std::string(kForestTrial) +
                        "' in InsideGanonCastle): starting"));

  // Where we are is not known: not asked.
  Game lost;
  lost.in.scene = kCastle;
  lost.in.position.reset();
  lost.tick();
  lost.story.receive(barrier_from(2, kForestCore));
  out = lost.tick();
  CHECK(logged(out, "ignored, it is for the room of P2 and our place is unknown"));
  CHECK_FALSE(out.ask);
}

TEST_CASE("story: near takes a start by where it started or by where its starter stands now") {
  // At the far side of a 21 m room from where P3 broke it (42 m), P3 walked to us: taken by
  // the second measure, and the log says which.
  Game far_side;
  far_side.in.scene = kCastle;
  far_side.in.position = proto::Vec3{kForestCore[0] + 42.0f, kForestCore[1], kForestCore[2]};
  far_side.in.mates = {StoryInput::Mate{3, std::string(kCastle),
                                        proto::Vec3{kForestCore[0] + 30.0f, kForestCore[1], kForestCore[2]}}};
  far_side.tick();
  far_side.story.receive(barrier_from(3, kForestCore));
  auto out = far_side.tick();
  CHECK(logged(out, "): starting (near: 12.0 m from P3 now (42.0 m from where it started), near=30)"));
  CHECK(out.ask == std::optional<std::string>(kForestTrial));

  // Close to where it started: the first measure, whatever the starter does now.
  Game near_start;
  near_start.in.scene = kCastle;
  near_start.in.position = proto::Vec3{kForestCore[0] + 5.0f, kForestCore[1], kForestCore[2]};
  near_start.in.mates = {StoryInput::Mate{3, std::string(kCastle), kWaterCore}};
  near_start.tick();
  near_start.story.receive(barrier_from(3, kForestCore));
  out = near_start.tick();
  CHECK(logged(out, "): starting (near: 5.0 m from where it started, near=30)"));

  // The starter seen in another scene does not count; neither does one far now.
  Game other;
  other.in.scene = kCastle;
  other.in.position = kWaterCore;
  other.in.mates = {StoryInput::Mate{3, std::string("GanonCastleTower"), kWaterCore}};
  other.tick();
  other.story.receive(barrier_from(3, kForestCore));
  out = other.tick();
  CHECK(logged(out, "ignored, it is for the room of P3: we are 92.0 m from where it started (near=30)"));
  CHECK_FALSE(out.ask);
  Game both_far;
  both_far.in.scene = kCastle;
  both_far.in.position = kWaterCore;
  both_far.in.mates = {StoryInput::Mate{3, std::string(kCastle), kForestCore}};
  both_far.tick();
  both_far.story.receive(barrier_from(3, kForestCore));
  out = both_far.tick();
  CHECK(logged(out, "ignored, it is for the room of P3: we are 92.0 m from where it started and 92.0 m from P3 "
                    "now (near=30)"));
}

TEST_CASE("story: near with a start place the starter did not know (0 0 0) judges by the starter now") {
  // The start place is 0 0 0 (the starter's Link was not read): a mate 3 m from P2 now takes it.
  Game g;
  g.in.scene = kCastle;
  g.in.position = proto::Vec3{kForestCore[0] + 3.0f, kForestCore[1], kForestCore[2]};
  g.in.mates = {StoryInput::Mate{2, std::string(kCastle), kForestCore}};
  g.tick();
  g.story.receive(barrier_from(2, proto::Vec3{0.0f, 0.0f, 0.0f}));
  auto out = g.tick();
  CHECK(logged(out, "): starting (near: 3.0 m from P2 now (its start place not known), near=30)"));
  // The origin is far from every trial room: without the starter seen, never taken by it.
  Game h;
  h.in.scene = kCastle;
  h.in.position = proto::Vec3{1.0f, 0.0f, 1.0f};  // 1.4 m from 0 0 0
  h.tick();
  h.story.receive(barrier_from(2, proto::Vec3{0.0f, 0.0f, 0.0f}));
  out = h.tick();
  CHECK(logged(out, "ignored, it is for the room of P2: its start place is not known and P2 is not seen in our "
                    "scene (near=30)"));
  CHECK_FALSE(out.ask);
  // Seen, but far.
  h.in.mates = {StoryInput::Mate{2, std::string(kCastle), kWaterCore}};
  h.story.receive(barrier_from(2, proto::Vec3{0.0f, 0.0f, 0.0f}));
  out = h.tick();
  CHECK(logged(out, "from P2 now, its start place not known (near=30)"));
}

TEST_CASE("story: height counts for near (the tower's floors stand over each other)") {
  Game g;
  g.in.scene = kCastle;
  g.in.position = proto::Vec3{kForestCore[0], kForestCore[1] + 31.0f, kForestCore[2]};
  g.tick();
  g.story.receive(barrier_from(2, kForestCore));
  const auto out = g.tick();
  CHECK(logged(out, "ignored, it is for the room of P2: we are 31.0 m from where it started (near=30)"));
}

TEST_CASE("story: a near event our own game starts is shared as any (the others judge)") {
  Game g;
  g.in.scene = kCastle;
  g.in.position = kForestCore;
  g.tick();
  g.event(std::nullopt);  // the edge; the name comes a frame later
  CHECK(g.tick().send.empty());
  g.event(std::string(kForestTrial));
  const auto out = g.tick();
  REQUIRE(out.send.size() == 1);
  CHECK(out.send[0].name == kForestTrial);
  CHECK(out.send[0].position == kForestCore);
}
