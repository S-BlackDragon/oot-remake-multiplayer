// Tests for the adult dungeon and overworld list (tools/release/kit/shared_dungeon_adult.txt,
// oot-remake docs/CONTENIDO_ADULTO.md). The list is read with the dungeon list syntax
// (unshare:, held:, once:, shared lines); these checks keep it meaningful against the built-in
// list (lichii/hook/coop_dungeon.cpp) without the game.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "coop_dungeon.h"
#include "coop/host/dungeon_lists.hpp"
#include "coop/host/flags.hpp"

namespace host = coop::host;

namespace {

struct Line {
  enum class Kind { share, once, unshare, held } kind = Kind::share;
  std::string name;
  std::string scene;  // held
  int number = 0;
};

std::vector<Line> ReadList(std::string& text) {
  const auto path = std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" / "kit" /
                    "shared_dungeon_adult.txt";
  std::ifstream file(path);
  REQUIRE_MESSAGE(file.good(), path.string());
  std::stringstream all;
  all << file.rdbuf();
  text = all.str();
  std::vector<Line> out;
  std::istringstream lines(text);
  std::string raw;
  int number = 0;
  while (std::getline(lines, raw)) {
    ++number;
    if (const auto hash = raw.find('#'); hash != std::string::npos) raw.resize(hash);
    std::istringstream words(raw);
    std::vector<std::string> w;
    for (std::string s; words >> s;) w.push_back(s);
    if (w.empty()) continue;
    Line l;
    l.number = number;
    if (w[0].starts_with("unshare:")) {
      CHECK_MESSAGE(w.size() == 1, ("line " + std::to_string(number)));
      l.kind = Line::Kind::unshare;
      l.name = w[0].substr(8);
    } else if (w[0].starts_with("once:")) {
      CHECK_MESSAGE(w.size() == 1, ("line " + std::to_string(number)));
      l.kind = Line::Kind::once;
      l.name = w[0].substr(5);
    } else if (w[0].starts_with("held:")) {
      CHECK_MESSAGE(w.size() == 2, ("line " + std::to_string(number)));
      l.kind = Line::Kind::held;
      l.name = w[0].substr(5);
      l.scene = w.size() > 1 ? w[1] : "";
    } else {
      CHECK_MESSAGE(w.size() == 1, ("line " + std::to_string(number)));  // only plain names here
      l.name = w[0];
    }
    out.push_back(l);
  }
  return out;
}

bool BuiltIn(std::uint32_t hash) {
  for (const auto& f : CoopDungeon::Flags()) {
    if (f.hash == hash) return true;
  }
  for (const auto& f : CoopDungeon::LiveFlags()) {
    if (f.hash == hash) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("adult list: what it takes off is in the built-in list, what it shares is not") {
  std::string text;
  const auto lines = ReadList(text);
  std::set<std::uint32_t> seen;
  std::size_t share = 0, once = 0, unshare = 0, held = 0;
  for (const auto& l : lines) {
    const std::uint32_t hash = host::flag_hash(l.name);
    CHECK_MESSAGE(seen.insert(hash).second, (std::string("listed twice: ") + l.name));
    for (const auto& no : CoopDungeon::NotShared()) {
      CHECK_MESSAGE(l.name != no, (std::string("already never shared: ") + l.name));
    }
    switch (l.kind) {
      case Line::Kind::unshare:
        ++unshare;
        CHECK_MESSAGE(BuiltIn(hash), (std::string("unshare: not in the built-in list: ") + l.name));
        break;
      case Line::Kind::held:
        ++held;
        CHECK_MESSAGE(BuiltIn(hash), (std::string("held: not in the built-in list: ") + l.name));
        CHECK_MESSAGE(CoopDungeon::IsDungeonScene(l.scene), (std::string("held: not a dungeon: ") + l.scene));
        CHECK_MESSAGE(l.name.starts_with(l.scene), (std::string("held: in another dungeon: ") + l.name));
        break;
      case Line::Kind::share:
        ++share;
        CHECK_MESSAGE(!BuiltIn(hash), (std::string("shared already by the built-in list: ") + l.name));
        // One line of the shared flags file syntax (flags.hpp), as the list reader takes it.
        CHECK_MESSAGE(host::SharedFlags::parse(l.name).has_value(), l.name);
        break;
      case Line::Kind::once:
        ++once;
        CHECK_MESSAGE(!BuiltIn(hash), (std::string("once: shared already by the built-in list: ") + l.name));
        CHECK_MESSAGE(host::SharedFlags::parse(l.name).has_value(), l.name);  // a plain name, no index
        break;
    }
  }
  CHECK(unshare == 47);
  CHECK(held == 4);
  CHECK(share == 31);
  CHECK(once == 5);
}

TEST_CASE("adult list: one player's place, fight start or jail is never shared by it") {
  std::string text;
  const auto lines = ReadList(text);
  // oot-64: the player of a room is the one it is about (D-171 jail, D-117 fights).
  const char* one_player[] = {"Prison", "IsGoing", "LinkIsInTheRoom", "RunFrom", "PlayerPos", "AreaCheck",
                              "IsAtCorrectArea", "EnterFrom", "InSecretRoom", "FightBegain", "BattleStart",
                              "AppearGerudoThief", "CloseShutter", "BattleGerudoThief", "StepSwitchOn"};
  for (const auto& l : lines) {
    if (l.kind != Line::Kind::share && l.kind != Line::Kind::once) continue;
    for (const char* word : one_player) {
      CHECK_MESSAGE(l.name.find(word) == std::string::npos, (std::string("shares one player's state: ") + l.name));
    }
  }
}

TEST_CASE("adult list: the flags the scenarios name hash as the scenarios say") {
  // oot-remake/tools/harness/escenarios/adulto-*.txt match these hashes in the logs.
  CHECK(host::flag_hash("ForestTemple_MegBattleStart") == 0x5a52cb64u);
  CHECK(host::flag_hash("ForestTemple_Room08_ClimbIvy") == 0x8f8af32eu);
  CHECK(host::flag_hash("FireTemple_Room10_StepSwitchOnLink") == 0xce689a86u);
  CHECK(host::flag_hash("Npc_Goron_001_ShutterisOpen") == 0x023e190fu);
  CHECK(host::flag_hash("SpiritTemple_Room12_FightBegain") == 0x05fc38ecu);
  CHECK(host::flag_hash("SpiritTemple_Room02_Unlocked") == 0x6f951499u);
  CHECK(host::flag_hash("GerudoFortress_Saved_Npc_Hylia_013_Adult") == 0x0db0556fu);
  CHECK(host::flag_hash("GerudoFortress_IsInPrison") == 0x64740219u);
  CHECK(host::flag_hash("IceCavern_Room02_DungeonIn") == 0x658877c8u);
  CHECK(host::flag_hash("IceCavern_Room06_GimmickClear") == 0xbfc19d6fu);
  CHECK(host::flag_hash("ShadowTemple_Level_Room_13_PlayerPosCheck") == 0x89758470u);
  CHECK(host::flag_hash("ShadowTemple_Level_Room_14_Clear") == 0x146d6db5u);
  CHECK(host::flag_hash("WaterTemple_Room00_WaterLevel") == 0xbeb09982u);
  CHECK(host::flag_hash("WaterTemple_Room05_InRoom") == 0x9067d036u);
  CHECK(host::flag_hash("KakarikoVillage_GatheringTokenGold_GetReward10") == 0x9086a22eu);
  CHECK(host::flag_hash("Market_Child_TimeTempleDoor_Opened") == 0x727c2cbdu);
  CHECK(host::flag_hash("InsideGanonCastle_Dungeon_BarrierRemove_Light") == 0x444d4c74u);
  CHECK(host::flag_hash("BottomOfTheWell_CoffinRoomCheck") == 0x0b5b3b0cu);
  CHECK(host::flag_hash("BottomOfTheWell_Room07_UnLock") == 0xb8daefc7u);
  // gerudo-carpinteros and espiritu-edades (oot-remake docs/GERUDO_ESPIRITU.md).
  CHECK(host::flag_hash("GerudoFortress_Meet_FourCarpenter_NumOfCarpenterHelped") == 0x290a6222u);
  CHECK(host::flag_hash("GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_013_Adult") == 0xf1eb5977u);
  CHECK(host::flag_hash("GerudoFortress_IsAfterHelpCarpenter_Npc_Hylia_014_Adult") == 0x06d7f245u);
  CHECK(host::flag_hash("GerudoFortress_WarpPoint01_ClearEvent") == 0x89c70062u);
  CHECK(host::flag_hash("Enemy_MiddleBoss_GerudoThief_DefeatNum") == 0xaa2d93e3u);
  CHECK(host::flag_hash("Enemy_MiddleBoss_IronKnuckle_Nabooru_BossDefeat") == 0xe9547a14u);
}

TEST_CASE("adult list: the Gerudo and Spirit Temple lines read as the dungeon list reader takes them") {
  // GERUDO_ESPIRITU.md: the guard's posts and Nabooru's Iron Knuckle once for the group, the
  // thieves' count a plain Int, the fight's start each player's own.
  std::string text;
  const auto lines = ReadList(text);
  const auto kind_of = [&](const std::string& name) -> std::optional<Line::Kind> {
    for (const auto& l : lines) {
      if (l.name == name) return l.kind;
    }
    return std::nullopt;
  };
  for (const char* post : {"GerudoFortress_WarpPoint01_ClearEvent", "GerudoFortress_WarpPoint02_ClearEvent",
                           "GerudoFortress_WarpPoint03_ClearEvent", "GerudoFortress_WarpPoint04_ClearEvent"}) {
    CHECK_MESSAGE(kind_of(post) == Line::Kind::once, post);
  }
  CHECK(kind_of("Enemy_MiddleBoss_IronKnuckle_Nabooru_BossDefeat") == Line::Kind::once);
  CHECK(kind_of("Enemy_MiddleBoss_GerudoThief_DefeatNum") == Line::Kind::share);
  CHECK_FALSE(kind_of("Enemy_MiddleBoss_IronKnuckle_Nabooru_BossAppear").has_value());
  // The reader of the hook takes the file as it is (a once: line is a pickup flag).
  std::string why;
  const auto list = host::DungeonList::parse(text, &why);
  REQUIRE_MESSAGE(list.has_value(), why);
  CHECK(list->once().size() == 5);
}
