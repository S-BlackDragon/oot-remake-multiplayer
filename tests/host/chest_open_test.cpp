// ORM cofre (oot-remake docs/DETALLES_MARIONETA.md, "Cofres"): coop/host/chest_open.hpp.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/chest_open.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"

namespace host = coop::host;
namespace proto = coop::protocol;
using host::ChestItemKind;
using proto::Vec3;

namespace {

// A Forest Temple small key chest (Banc ForestTemple, Obj_TBox_A_01).
constexpr std::uint64_t kKeyChest = 0x1041dd4d83575dda;
const std::string kScene = "ForestTemple";
const Vec3 kChestAt{10.0f, 100.0f, -20.0f};
const Vec3 kBeside{10.0f, 100.0f, -18.8f};  // 1.2 m in front
const Vec3 kFar{40.0f, 100.0f, -20.0f};

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  REQUIRE_MESSAGE(f.good(), path.string());
  return std::string(std::istreambuf_iterator<char>(f), {});
}

host::DoorActor Chest(std::uint64_t id, std::string command, float frame = 0.0f,
                      std::uint64_t hash = kKeyChest, Vec3 at = kChestAt,
                      std::string name = "Obj_TBox_A_01") {
  host::DoorActor a;
  a.id = id;
  a.name = std::move(name);
  a.position = at;
  a.command = std::move(command);
  a.hash = hash;
  a.frame = frame;
  return a;
}

host::ChestLidInput Ours(std::string command, Vec3 at = kBeside, std::uint64_t server_us = 50'000'000) {
  host::ChestLidInput in;
  in.scene = kScene;
  in.own = at;
  in.own_command = std::move(command);
  in.server_now_us = server_us;
  return in;
}

proto::ChestOpen From(std::uint8_t player, std::uint64_t chest = kKeyChest, std::string scene = kScene,
                      std::string command = "FrontOpenSmall", float frame = 4.0f,
                      std::uint64_t server_us = 49'000'000) {
  proto::ChestOpen m;
  m.player_number = player;
  m.chest = chest;
  m.scene = std::move(scene);
  m.animation = proto::Animation{std::move(command), frame, 1.0f};
  m.server_time_us = server_us;
  return m;
}

bool Says(const std::vector<std::string>& lines, std::string_view part) {
  return std::any_of(lines.begin(), lines.end(),
                     [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

bool PlaysSay(const host::ChestLidOut& out, std::string_view part) {
  return std::any_of(out.plays.begin(), out.plays.end(),
                     [&](const host::ChestLidPlay& p) { return p.line.find(part) != std::string::npos; });
}

host::FindMate Mate(std::uint8_t n, std::string command, std::string scene = kScene, bool adult = false,
                    Vec3 at = kBeside) {
  host::FindMate m;
  m.number = n;
  m.scene = std::move(scene);
  m.position = at;
  m.adult = adult;
  m.command = std::move(command);
  return m;
}

std::string Name(std::uint8_t n) { return n == 2 ? "Ana" : n == 3 ? "Tobi" : ""; }

}  // namespace

// ---- The table ----

TEST_CASE("chests: the table has every chest once, sorted, and knows what each gives") {
  const auto chests = host::chest_placements();
  CHECK(chests.size() == 200);
  for (std::size_t i = 1; i < chests.size(); ++i) {
    CHECK(chests[i - 1].hash < chests[i].hash);
  }
  for (const auto& c : chests) {
    CHECK(std::string_view(c.actor).substr(0, 9) == "Obj_TBox_");
    CHECK(host::find_chest(c.hash) == &c);
    const std::string_view drop = host::chest_drop(c);
    if (!drop.empty()) {
      CHECK_MESSAGE(host::chest_item(drop) != nullptr, drop);
    }
    if (c.substitute[0] != '\0') {
      CHECK_MESSAGE(host::chest_item(c.substitute) != nullptr, c.substitute);
    }
  }
  CHECK(host::find_chest(0x1234) == nullptr);
  const auto* key = host::find_chest(kKeyChest);
  REQUIRE(key != nullptr);
  CHECK(std::string(key->area) == "ForestTemple");
  CHECK(host::chest_drop(*key) == "Item_Dungeon_SmallKey");
  // A Gerudo Training Ground chest with no drop: its substitute (arrows, with the bow).
  const auto* arrows = host::find_chest(0x44db1a332e1181a1);
  REQUIRE(arrows != nullptr);
  CHECK(host::chest_drop(*arrows) == "TBox_ArrowBundle_Medium");
  // The Deku Tree's slingshot chest.
  const auto* sling = host::find_chest(0x0b66198d17d6584e);
  REQUIRE(sling != nullptr);
  CHECK(host::chest_drop(*sling) == "Item_Range_SlingShot");
}

TEST_CASE("chests: every find has its words, its stand-in and the puppet pack lists it") {
  const auto items = host::chest_items();
  CHECK(items.size() == 37);
  const std::string dumpcheck =
      ReadFile(std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "puppet_pack" / "dumpcheck.py");
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    if (i > 0) CHECK(items[i - 1].drop < it.drop);
    CHECK(!it.words.empty());
    CHECK(it.stand_in == "CoopGet_" + std::string(it.drop));
    CHECK(it.stand_in.size() < 0x40);  // the spawn director's name cell
    CHECK(host::chest_get_stand_in(it.drop) == it.stand_in);
    CHECK(host::is_chest_find(it.drop));
    CHECK_MESSAGE(dumpcheck.find("\"" + std::string(it.drop) + "\"") != std::string::npos, it.drop);
  }
  CHECK_FALSE(host::is_chest_find("Item_Bomb"));  // a bomb in the hands is a shot, not a find
  CHECK(host::chest_get_stand_in("Obj_Pot_A_08").empty());
}

TEST_CASE("chests: every kind of find reaches every game (small keys, maps, compasses, boss keys, items)") {
  // The kit's list makes the wallet shared (oot-remake docs/INVENTARIO.md).
  const std::string kit = ReadFile(std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" /
                                   "kit" / "shared_items.txt");
  CHECK(kit.find("count PlayerStatus.CurrentRupee") != std::string::npos);
  std::set<ChestItemKind> kinds;
  for (const auto& c : host::chest_placements()) {
    const std::string_view drop = host::chest_drop(c);
    if (drop.empty()) continue;
    const host::ChestItem* item = host::chest_item(drop);
    REQUIRE(item != nullptr);
    kinds.insert(item->kind);
    const std::string flag = host::chest_item_flag(*item, c.area);
    const std::string where = std::string(drop) + " in " + c.area + ": " + flag;
    INFO(where);
    if (item->kind == ChestItemKind::ammo || item->kind == ChestItemKind::magic) {
      CHECK(flag.empty());  // each player's own, as oot-64: the opener's
      continue;
    }
    REQUIRE(!flag.empty());
    const host::ItemFlag* f = host::find_item(host::flag_hash(flag));
    REQUIRE(f != nullptr);
    CHECK(f->on);
    const auto built_in = host::built_in_item_rule(f->hash);
    REQUIRE(built_in);
    switch (item->kind) {
      case ChestItemKind::own_shield:
        // Shared again since test10 (upstream e2132ffc, issue 130): the chest's shield reaches all.
        CHECK(*built_in == host::ItemRule::any);
        break;
      case ChestItemKind::rupees:
        CHECK(*built_in == host::ItemRule::per_player);  // upstream; the kit's line above shares it
        break;
      case ChestItemKind::small_key:
      case ChestItemKind::heart_piece:
        CHECK(*built_in == host::ItemRule::count);
        break;
      default:
        CHECK(*built_in != host::ItemRule::per_player);
        CHECK(host::item_shared_flags().contains(host::FlagKey(f->hash, std::uint16_t{0})));
        break;
    }
  }
  for (const auto k : {ChestItemKind::item, ChestItemKind::small_key, ChestItemKind::map, ChestItemKind::compass,
                       ChestItemKind::boss_key, ChestItemKind::heart_piece, ChestItemKind::rupees,
                       ChestItemKind::ammo, ChestItemKind::own_shield, ChestItemKind::magic}) {
    CHECK_MESSAGE(kinds.contains(k), host::chest_item_kind_text(k));
  }
}

TEST_CASE("chests: songs, which no chest gives, are the group's too") {
  int songs = 0;
  for (const auto& f : host::item_flags()) {
    if (f.name.substr(0, 18) == "IsGet.Item_Melody_") {
      ++songs;
      CHECK_MESSAGE(f.rule == host::ItemRule::any, f.name);
      CHECK(f.on);
    }
  }
  CHECK(songs >= 12);
}

TEST_CASE("chests: the notice and the item-get commands") {
  CHECK(host::chest_found_text("Ana", "Item_Dungeon_SmallKey") == "Ana encontró una llave pequeña");
  CHECK(host::chest_found_text("Tobi", "Item_Dungeon_BossKey") == "Tobi encontró la gran llave");
  CHECK(host::chest_found_text("Ana", "Item_Hookshot") == "Ana encontró el gancho");
  CHECK(host::chest_found_text("Ana", "") == "Ana abrió un cofre");
  CHECK(host::chest_found_text("", "Item_Dungeon_Map") == "Alguien encontró el mapa");
  for (const char* get : {"DemoGet", "DemoGetLoop", "DemoGet_Smile", "DemoGetLoop_Smile", "EvtGet",
                          "Demo_Get_Child", "Demo_Get_Adult_Big_Loop", "Demo_Get_Child_InWater"}) {
    CHECK_MESSAGE(host::is_item_get_command(get), get);
  }
  for (const char* not_get : {"", "Wait", "DemoOpenTreasureBoxFront", "Demo_Get_Kick_TreasureBox_Child",
                              "DemoGetBottleContent", "DemoOcarinaMelodyGet"}) {
    CHECK_FALSE_MESSAGE(host::is_item_get_command(not_get), not_get);
  }
}

// ---- The lid: ours ----

TEST_CASE("chest lids: the chest our Link opens is told once, with its command and frame") {
  host::ChestLids lids;
  lids.update(Ours("Wait"), 1000);
  CHECK_FALSE(lids.wants_actors(1000));
  lids.update(Ours("DemoOpenTreasureBoxFront"), 1100);
  CHECK(lids.wants_actors(1100));
  CHECK(lids.worth_reading(kChestAt));
  CHECK_FALSE(lids.worth_reading(kFar));
  // First sight: closed. Its idle has nothing to tell.
  auto out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1100);
  CHECK(out.send.empty());
  // Its lid starts opening.
  out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall", 3.0f)}, {}, 1500);
  REQUIRE(out.send.size() == 1);
  const auto& m = out.send[0];
  CHECK(m.player_number == proto::kPlayerSelf);
  CHECK(m.chest == kKeyChest);
  CHECK(m.scene == kScene);
  CHECK(m.animation.name == "FrontOpenSmall");
  CHECK(m.animation.frame == doctest::Approx(3.0f));
  CHECK(m.animation.rate == doctest::Approx(1.0f));
  CHECK(m.server_time_us == 50'000'000);
  CHECK(Says(out.lines, "our Link opens Obj_TBox_A_01 0x1041dd4d83575dda (Item_Dungeon_SmallKey): CHEST_OPEN"));
  CHECK_FALSE(lids.wants_actors(1600));  // told: nothing more to read for it
  out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "OpenWait")}, {}, 1700);
  CHECK(out.send.empty());
  // The same chest again (a reload, a second try) within a minute: not told again.
  lids.update(Ours("Wait"), 5000);
  lids.update(Ours("DemoOpenBoxFront_Mini"), 6000);
  lids.decide(Ours("DemoOpenBoxFront_Mini"), {Chest(8, "CloseWait")}, {}, 6000);
  out = lids.decide(Ours("DemoOpenBoxFront_Mini"), {Chest(8, "FrontOpenSmall")}, {}, 6200);
  CHECK(out.send.empty());
  CHECK(Says(out.lines, "told already"));
}

TEST_CASE("chest lids: an idle that says Open, a far chest, a made chest are never told") {
  host::ChestLids lids;
  lids.update(Ours("DemoOpenTreasureBoxWithKick"), 1000);
  const std::vector<host::DoorActor> first{
      Chest(1, "OpenWait"),                                         // idle with "Open", unchanged
      Chest(2, "CloseWait", 0.0f, 0x7ff8df2fa8e85da9, kFar),        // 30 m off
      Chest(3, "CloseWait", 0.0f, ~std::uint64_t{0}, kChestAt),     // the game made it: no placement
      Chest(4, "Wait", 0.0f, 0x0b66198d17d6584e, kChestAt, "Obj_Pot_A_08"),  // not a chest
  };
  CHECK(lids.decide(Ours("DemoOpenTreasureBoxWithKick"), first, {}, 1000).send.empty());
  const std::vector<host::DoorActor> later{
      Chest(1, "OpenWait"),
      Chest(2, "FrontOpen", 0.0f, 0x7ff8df2fa8e85da9, kFar),
      Chest(3, "FrontOpenSmall", 0.0f, ~std::uint64_t{0}, kChestAt),
      Chest(4, "Open", 0.0f, 0x0b66198d17d6584e, kChestAt, "Obj_Pot_A_08"),
  };
  CHECK(lids.decide(Ours("DemoOpenTreasureBoxWithKick"), later, {}, 1500).send.empty());
  // The window ends: one line, then nothing more is read for it. Until that line is said the glue still
  // walks (wants_actors), or the line would never come out of a game (efectos-cofre, 2026-11-12).
  CHECK(lids.wants_actors(4000));
  const auto out = lids.decide(Ours("Wait"), later, {}, 4000);
  CHECK(out.send.empty());
  CHECK(Says(out.lines, "our Link's chest move 'DemoOpenTreasureBoxWithKick': no chest within 2.5 m changed"));
  CHECK(Says(out.lines, "'OpenWait' -> 'OpenWait'"));  // what the chests in reach showed
  CHECK_FALSE(lids.wants_actors(4000));
}

// ---- The lid: theirs ----

TEST_CASE("chest lids: another player's chest opens here at his frame, once") {
  host::ChestLids lids;
  lids.receive(From(proto::kPlayerSelf), 1000);  // ours echoed: never
  CHECK(lids.waiting() == 0);
  lids.receive(From(2), 1000);
  lids.update(Ours("Wait", kFar), 1000);
  CHECK(lids.wants_actors(1000));
  CHECK(lids.worth_reading(kFar));  // any chest: found by its hash
  // 1 s since he read it on the server clock: 30 frames on.
  auto out = lids.decide(Ours("Wait", kFar), {Chest(7, "CloseWait")}, {}, 1000);
  REQUIRE(out.plays.size() == 1);
  CHECK(out.plays[0].actor == 7);
  CHECK(out.plays[0].command == "FrontOpenSmall");
  REQUIRE(out.plays[0].frame);
  CHECK(*out.plays[0].frame == doctest::Approx(34.0f));
  CHECK(PlaysSay(out, "P2 opened 0x1041dd4d83575dda: Obj_TBox_A_01 opens here too ('FrontOpenSmall', frame 34.0)"));
  CHECK(lids.lid_opened(7));
  CHECK(lids.lids_opened() == std::vector<std::uint64_t>{7});
  CHECK(lids.waiting() == 0);
  CHECK(lids.decide(Ours("Wait", kFar), {Chest(7, "FrontOpenSmall")}, {}, 1100).plays.empty());
  // Remembered a minute, for the door mirror and the objects sync.
  lids.update(Ours("Wait", kFar), 61000);
  CHECK(lids.lid_opened(7));
  lids.update(Ours("Wait", kFar), 61001);
  CHECK_FALSE(lids.lid_opened(7));
}

TEST_CASE("chest lids: just told, the frame plays as it came, beside our Link it opens too") {
  host::ChestLids lids;
  lids.receive(From(3, kKeyChest, kScene, "FrontOpenSmall", 0.5f, 50'000'000), 1000);
  auto out = lids.decide(Ours("Wait", kBeside), {Chest(7, "CloseWait")}, {}, 1000);
  REQUIRE(out.plays.size() == 1);  // our Link only stands there: it sees the lid open
  CHECK_FALSE(out.plays[0].frame);  // 0.5: from where it is
}

TEST_CASE("chest lids: not on a chest open here already, nor one the door mirror or our Link opens") {
  {
    host::ChestLids lids;
    lids.receive(From(2), 1000);
    const auto out = lids.decide(Ours("Wait", kFar), {Chest(7, "OpenWait")}, {}, 1000);
    CHECK(out.plays.empty());
    CHECK(Says(out.lines, "shows open here already ('OpenWait')"));
  }
  {
    host::ChestLids lids;
    lids.receive(From(2), 1000);
    const auto out = lids.decide(Ours("Wait", kFar), {Chest(7, "CloseWait")}, {7}, 1000);
    CHECK(out.plays.empty());
    CHECK(Says(out.lines, "is opening here already"));
  }
  {
    host::ChestLids lids;
    lids.update(Ours("DemoOpenTreasureBoxFront"), 1000);
    lids.receive(From(2), 1000);
    // Ours may still be told (its lid has not moved yet): his waits, nothing decided.
    auto out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1000);
    CHECK(out.plays.empty());
    CHECK(out.second.empty());
    CHECK(lids.waiting() == 1);
    // Our wait is over and ours was never told: his came first.
    lids.update(Ours("DemoOpenTreasureBoxFront"), 3600);
    out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 3600);
    CHECK(out.plays.empty());
    CHECK(Says(out.lines, "our Link is opening it too, after him: our find of it is not counted again"));
    CHECK(out.second == std::vector<std::uint64_t>{kKeyChest});  // ORM riesgos-visual
    CHECK(lids.waiting() == 0);
  }
}

TEST_CASE("chest lids: elsewhere or not loaded, a CHEST_OPEN waits a moment, then goes") {
  host::ChestLids lids;
  lids.receive(From(2, kKeyChest, "KokiriForest"), 1000);
  lids.receive(From(3, 0x0b66198d17d6584e, kScene), 1000);
  CHECK(lids.waiting() == 2);
  auto out = lids.decide(Ours("Wait", kFar), {Chest(7, "CloseWait")}, {}, 2000);
  CHECK(out.plays.empty());
  CHECK(lids.waiting() == 2);
  // Loading: kept (it may be for where we come to).
  out = lids.decide(host::ChestLidInput{}, {}, {}, 4500);
  CHECK(lids.waiting() == 2);
  out = lids.decide(Ours("Wait", kFar), {Chest(7, "CloseWait")}, {}, 5001);
  CHECK(out.plays.empty());
  CHECK(Says(out.lines, "P2 opened 0x1041dd4d83575dda in KokiriForest: not our place"));
  CHECK(Says(out.lines, "P3 opened 0x0b66198d17d6584e: that chest is not loaded here"));
  CHECK(lids.waiting() == 0);
  // A resend of the same chest keeps one.
  lids.receive(From(2), 6000);
  lids.receive(From(2), 6100);
  CHECK(lids.waiting() == 1);
  // A bad one is not kept.
  lids.receive(From(2, 0), 6200);
  lids.receive(From(2, kKeyChest, ""), 6200);
  CHECK(lids.waiting() == 1);
}

// ---- The find ----

TEST_CASE("chest finds: the find over his head while he holds it up, and the notice once") {
  host::ChestFinds finds;
  finds.opened(From(2), 1000);
  CHECK(finds.pending() == 1);
  // Opening: nothing yet.
  auto out = finds.tick(kScene, {Mate(2, "DemoOpenTreasureBoxFront")}, Name, 1500);
  CHECK(out.held.empty());
  CHECK(out.notices.empty());
  // He holds it up.
  out = finds.tick(kScene, {Mate(2, "DemoGet")}, Name, 2500);
  REQUIRE(out.held.size() == 1);
  CHECK(out.held[0].number == 2);
  CHECK(out.held[0].drop == "Item_Dungeon_SmallKey");
  CHECK(out.held[0].serial == 1);
  CHECK(out.held[0].position[0] == doctest::Approx(kBeside[0]));
  CHECK(out.held[0].position[1] == doctest::Approx(kBeside[1] + 1.25f));
  REQUIRE(out.notices.size() == 1);
  CHECK(out.notices[0].number == 2);
  CHECK(out.notices[0].name == "Ana");
  CHECK(out.notices[0].text == "Ana encontró una llave pequeña");
  CHECK(Says(out.lines, "P2 found Item_Dungeon_SmallKey (held up): 'Ana encontró una llave pequeña'"));
  out = finds.tick(kScene, {Mate(2, "DemoGetLoop")}, Name, 3000);
  CHECK(out.held.size() == 1);
  CHECK(out.notices.empty());  // once
  // He puts it away: still shown a moment (a command between two item-gets), then gone.
  out = finds.tick(kScene, {Mate(2, "Wait")}, Name, 3200);
  CHECK(out.held.size() == 1);
  CHECK(finds.pending() == 1);
  out = finds.tick(kScene, {Mate(2, "Wait")}, Name, 3400);
  CHECK(out.held.empty());
  CHECK(finds.pending() == 0);
  // An adult holds it higher; a new find is a new copy.
  finds.opened(From(3, 0x0b66198d17d6584e), 5000);
  out = finds.tick(kScene, {Mate(3, "Demo_Get_Adult_Big", kScene, true)}, Name, 6000);
  REQUIRE(out.held.size() == 1);
  CHECK(out.held[0].serial == 2);
  CHECK(out.held[0].drop == "Item_Range_SlingShot");
  CHECK(out.held[0].position[1] == doctest::Approx(kBeside[1] + 1.85f));
  CHECK(out.notices.at(0).text == "Tobi encontró el tirachinas de las hadas");
  // Held at most 8 s.
  out = finds.tick(kScene, {Mate(3, "DemoGetLoop", kScene, true)}, Name, 14001);
  CHECK(out.held.empty());
  CHECK(finds.pending() == 0);
}

// ORM (oot-remake docs/SONDAS.md, 2026-11-12): the remake's finder plays DemoOpenTreasureBox through
// the whole find; no item-get command comes.
TEST_CASE("chest finds: the remake's chest command held past chest_hold_after_ms holds the find up") {
  host::ChestFinds finds;
  finds.opened(From(2), 1000);
  auto out = finds.tick(kScene, {Mate(2, "DemoOpenTreasureBox")}, Name, 2000);
  CHECK(out.held.empty());
  out = finds.tick(kScene, {Mate(2, "DemoOpenTreasureBox")}, Name, 3500);
  REQUIRE(out.held.size() == 1);
  CHECK(out.held[0].drop == "Item_Dungeon_SmallKey");
  CHECK(Says(out.lines, "P2 holds up Item_Dungeon_SmallKey ('DemoOpenTreasureBox' at frame"));
  CHECK(Says(out.lines, "the chest's own command, no item-get command seen"));
  REQUIRE(out.notices.size() == 1);
  // Still the chest command: still held; then Wait: over after the grace.
  out = finds.tick(kScene, {Mate(2, "DemoOpenTreasureBox")}, Name, 5000);
  CHECK(out.held.size() == 1);
  out = finds.tick(kScene, {Mate(2, "Wait")}, Name, 5400);
  CHECK(out.held.empty());
  // Off (0): only an item-get command counts.
  host::ChestFindRules rules;
  rules.chest_hold_after_ms = 0;
  host::ChestFinds plain(rules);
  plain.opened(From(2), 1000);
  out = plain.tick(kScene, {Mate(2, "DemoOpenTreasureBox")}, Name, 3500);
  CHECK(out.held.empty());
  CHECK(host::is_chest_open_command("DemoOpenTreasureBox"));
  CHECK_FALSE(host::is_chest_open_command("Wait"));
}

TEST_CASE("chest finds: a finder elsewhere, or whose pose is not seen, is told all the same") {
  host::ChestFinds finds;
  finds.opened(From(2, kKeyChest, "ForestTemple"), 1000);
  // He is in the Forest Temple, we are in Kokiri Forest: no puppet, no find over a head.
  auto out = finds.tick(std::string("KokiriForest"), {Mate(2, "DemoGet")}, Name, 2000);
  CHECK(out.held.empty());
  CHECK(out.notices.empty());
  out = finds.tick(std::string("KokiriForest"), {Mate(2, "DemoGet")}, Name, 5000);
  REQUIRE(out.notices.size() == 1);
  CHECK(out.notices[0].text == "Ana encontró una llave pequeña");
  CHECK(Says(out.lines, "(elsewhere)"));
  // Forgotten after 20 s.
  out = finds.tick(std::string("KokiriForest"), {}, Name, 21001);
  CHECK(finds.pending() == 0);
  // A chest the table does not know: the notice says he opened a chest; nothing over his head.
  finds.opened(From(3, 0x1234), 30000);
  out = finds.tick(kScene, {Mate(3, "DemoGet")}, Name, 30100);
  CHECK(out.held.empty());
  REQUIRE(out.notices.size() == 1);
  CHECK(out.notices[0].text == "Tobi abrió un cofre");
  // Our own echo is not a find.
  finds.opened(From(proto::kPlayerSelf), 40000);
  CHECK(finds.pending() == 1);
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): the first
// CHEST_OPEN wins; the second opener's game leaves its counted find out.
TEST_CASE("chest lids (riesgos-visual): the first CHEST_OPEN wins by its server time, a tie by the lower player number") {
  {
    // We told ours at 50 s; his says 49 s: his came first, ours is second.
    host::ChestLids lids;
    lids.update(Ours("Wait"), 900);
    lids.update(Ours("DemoOpenTreasureBoxFront"), 1000);
    lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1000);
    const auto told = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1100);
    REQUIRE(told.send.size() == 1);
    lids.receive(From(2, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 49'000'000), 1200);
    const auto out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1200);
    CHECK(out.second == std::vector<std::uint64_t>{kKeyChest});
    CHECK(Says(out.lines, "was opened here too, after him: our find of it is not counted again"));
  }
  {
    // His says 51 s, after ours: we were first, nothing left out here.
    host::ChestLids lids;
    lids.update(Ours("Wait"), 900);
    lids.update(Ours("DemoOpenTreasureBoxFront"), 1000);
    lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1000);
    REQUIRE(lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1100).send.size() == 1);
    lids.receive(From(2, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 51'000'000), 1200);
    const auto out = lids.decide(Ours("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1200);
    CHECK(out.second.empty());
  }
  for (const std::uint8_t self : {std::uint8_t{1}, std::uint8_t{3}}) {
    // A tie (20 ms apart, under tie_us): the lower player number keeps the find.
    // We are P1 against P2 (ours counts) or P3 against P2 (ours is left out);
    // P2's game decides the other way round, so exactly one counts it.
    host::ChestLids lids;
    auto in = [&](const char* command) {
      auto i = Ours(command);
      i.self = self;
      return i;
    };
    lids.update(in("Wait"), 900);
    lids.update(in("DemoOpenTreasureBoxFront"), 1000);
    lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1000);
    REQUIRE(lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1100).send.size() == 1);
    lids.receive(From(2, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 50'020'000), 1200);
    const auto out = lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1200);
    REQUIRE(out.ties.size() == 1);
    CHECK(out.ties[0].find("a tie with P2 (20 ms apart): the lower player number keeps the find") != std::string::npos);
    if (self == 1) {
      CHECK(out.second.empty());
      CHECK(out.ties[0].find("ours; his game leaves his out") != std::string::npos);
    } else {
      CHECK(out.second == std::vector<std::uint64_t>{kKeyChest});
      CHECK(out.ties[0].find("P2's; ours is left out") != std::string::npos);
    }
  }
  {
    // His game as P2 against our P1's CHEST_OPEN: it leaves its own out (the mirror image).
    host::ChestLids lids;
    auto in = [&](const char* command) {
      auto i = Ours(command, kBeside, 50'020'000);
      i.self = 2;
      return i;
    };
    lids.update(in("Wait"), 900);
    lids.update(in("DemoOpenTreasureBoxFront"), 1000);
    lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "CloseWait")}, {}, 1000);
    REQUIRE(lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1100).send.size() == 1);
    lids.receive(From(1, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 50'000'000), 1200);
    CHECK(lids.decide(in("DemoOpenTreasureBoxFront"), {Chest(7, "FrontOpenSmall")}, {}, 1200).second ==
          std::vector<std::uint64_t>{kKeyChest});
  }
}

// Revision (REVISION_FINAL.md, Visual): his CHEST_OPEN arrives before our own
// lid moves, yet within the tie window of ours. Our P1 keeps the find by the
// tie rule and his game (P2) leaves his out, so ours must not be left out
// too: the find would count for nobody (a small key lost).
TEST_CASE("chest lids (riesgos-visual): a tie whose other CHEST_OPEN comes before our lid moves still counts once") {
  // Our game, P1: his (P2) at 50.000 s comes in while our Link's chest move runs.
  host::ChestLids ours;
  auto in = [](const char* command, std::uint64_t server_us, std::uint8_t self) {
    auto i = Ours(command, kBeside, server_us);
    i.self = self;
    return i;
  };
  ours.update(in("Wait", 50'000'000, 1), 900);
  ours.update(in("DemoOpenTreasureBoxFront", 50'000'000, 1), 1000);
  ours.receive(From(2, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 50'000'000), 1010);
  auto out = ours.decide(in("DemoOpenTreasureBoxFront", 50'010'000, 1), {Chest(7, "CloseWait")}, {}, 1010);
  CHECK(out.second.empty());
  CHECK(ours.waiting() == 1);
  // 30 ms later our lid moves: ours is told at 50.030 s, a tie, and P1 keeps it.
  out = ours.decide(in("DemoOpenTreasureBoxFront", 50'030'000, 1), {Chest(7, "FrontOpenSmall")}, {}, 1040);
  REQUIRE(out.send.size() == 1);
  REQUIRE(out.ties.size() == 1);
  CHECK(out.ties[0].find("ours; his game leaves his out") != std::string::npos);
  CHECK(out.second.empty());

  // His game, P2: told at 50.000 s; our CHEST_OPEN at 50.030 s is a tie it gives to P1.
  host::ChestLids his;
  his.update(in("Wait", 50'000'000, 2), 900);
  his.update(in("DemoOpenTreasureBoxFront", 50'000'000, 2), 1000);
  his.decide(in("DemoOpenTreasureBoxFront", 50'000'000, 2), {Chest(7, "CloseWait")}, {}, 1000);
  REQUIRE(his.decide(in("DemoOpenTreasureBoxFront", 50'000'000, 2), {Chest(7, "FrontOpenSmall")}, {}, 1010)
              .send.size() == 1);
  his.receive(From(1, kKeyChest, kScene, "FrontOpenSmall", 4.0f, 50'030'000), 1060);
  CHECK(his.decide(in("DemoOpenTreasureBoxFront", 50'060'000, 2), {Chest(7, "FrontOpenSmall")}, {}, 1060).second ==
        std::vector<std::uint64_t>{kKeyChest});
}

TEST_CASE("chest count flags (riesgos-visual): small keys, heart pieces and rupees count, a unique item does not") {
  CHECK(host::chest_count_flag(0x1041dd4d83575dda) == "DungeonInfo.ForestTemple.SmallKeyNum");
  CHECK(host::chest_count_flag(0x0258b24f2ac0a365) == "DungeonInfo.ShadowTemple.SmallKeyNum");
  CHECK(host::chest_count_flag(0x292e3ed6acacbe44) == "PouchData.Obj_Heart_Piece.StockNum");
  CHECK(host::chest_count_flag(0x033f5b9324bf56aa) == "PlayerStatus.CurrentRupee");
  CHECK(host::chest_count_flag(0xde9e62394333948e).empty());  // the Hookshot: a second one is the same item
  CHECK(host::chest_count_flag(0x1234).empty());              // not a chest of the list
}

// Seen in a game (efectos-cofre, 2026-11-12, docs/SONDAS.md row 7): the lid's opening command starts in the
// same frame as our move, so the first look already reads it and it never "changes".
TEST_CASE("chest lids: a lid already opening at the first look is ours, one idle open is not") {
  host::ChestLids lids;
  lids.update(Ours("Wait"), 1000);
  lids.update(Ours("DemoOpenTreasureBox"), 1100);
  auto out = lids.decide(Ours("DemoOpenTreasureBox"), {Chest(7, "FrontOpen", 2.0f)}, {}, 1100);
  REQUIRE(out.send.size() == 1);
  CHECK(out.send[0].animation.name == "FrontOpen");
  CHECK(Says(out.lines, "our Link opens Obj_TBox_A_01"));

  host::ChestLids idle;
  idle.update(Ours("Wait"), 1000);
  idle.update(Ours("DemoOpenTreasureBox"), 1100);
  CHECK(idle.decide(Ours("DemoOpenTreasureBox"), {Chest(7, "OpenWait")}, {}, 1100).send.empty());
}
