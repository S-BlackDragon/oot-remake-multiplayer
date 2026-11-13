// Tests for the ORM item rule list (coop/host/item_rules.hpp, the kit's
// shared_items.txt, oot-remake docs/INVENTARIO.md): ZMP's magic meter over the
// built-in item rules, and the rules a list can give (ZMP's common wallet, off
// in the kit since the decision of 2026-11-11: each player's own wallet).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/item_rules.hpp"
#include "coop/protocol/codec.hpp"

namespace {

using coop::host::apply_item_rules;
using coop::host::find_item;
using coop::host::FlagKey;
using coop::host::FlagRole;
using coop::host::FlagWatcher;
using coop::host::item_shared_flags;
using coop::host::ItemFlagStore;
using coop::host::ItemRule;
using coop::host::ItemRules;
using coop::host::LoadMend;
namespace protocol = coop::protocol;
using K = protocol::FlagKind;

constexpr std::uint32_t kRupees = 0xa77921d7;       // PlayerStatus.CurrentRupee
constexpr std::uint32_t kRupeeCap = 0xe0010a50;     // 0xf80f8077.Rupee, the wallet's size
constexpr std::uint32_t kMPMax = 0x12d63a97;        // PlayerStatus.MPMax, max
constexpr std::uint32_t kMP = 0x76b18200;           // PlayerStatus.MP, per player
constexpr std::uint32_t kLife = 0x31ab5580;         // PlayerStatus.Life, per player
constexpr std::uint32_t kBottleAItem = 0x7d48df76;  // BottleInfo.BottleA.BottleItem, per player
constexpr std::uint32_t kBottleAValid = 0xf39ce410; // BottleInfo.BottleA.IsValid, any
constexpr std::uint32_t kBottleA = 0xd53ce3a7;      // PouchData.BottleA.StockNum, any
constexpr std::uint32_t kHookshot = 0x8573d2cd;     // PouchData.Item_Hookshot.StockNum, max
constexpr std::uint32_t kDekuShield = 0x7ca64f23;   // PouchData.Weapon_Shield_Deku.StockNum, shared (test10)
constexpr std::uint32_t kBombs = 0xdb1bcd4f;        // PouchData.Item_Bomb.StockNum, per player
constexpr std::uint32_t kBombBag = 0xb0a1bf51;      // PouchData.Item_BombBag_Small.StockNum, max
constexpr std::uint32_t kHoverBoots = 0xc9220366;   // PouchData.Armor_Adult_HoverBoot.StockNum, max
constexpr std::uint32_t kZoraTunic = 0xb816af94;    // PouchData.Armor_Adult_Zora.StockNum, max
constexpr std::uint32_t kFairyBlessMP = 0x5e671d92; // PouchData.Item_FairyBless_MP.StockNum

// Restores the built-in rules when a test ends (the rules are for the whole build).
struct Reset {
  Reset() { coop::host::reset_item_rules(); }
  ~Reset() { coop::host::reset_item_rules(); }
};

// A game's flags as a plain map: unknown flags cannot be read.
class MapStore final : public coop::host::FlagStore {
 public:
  std::optional<protocol::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const protocol::FlagEntry& entry) override {
    values[entry.key] = entry;
    return true;
  }
  void hold(std::uint32_t hash, std::int32_t v) { values[FlagKey{hash, 0}] = i(hash, v); }
  std::int32_t get(std::uint32_t hash) const {
    return static_cast<std::int32_t>(values.at(FlagKey{hash, 0}).value);
  }
  static protocol::FlagEntry i(std::uint32_t h, std::int32_t v) {
    return {h, 0, K::int32, static_cast<std::uint32_t>(v)};
  }
  std::map<FlagKey, protocol::FlagEntry> values;
};

protocol::FlagEntry i(std::uint32_t h, std::int32_t v) { return MapStore::i(h, v); }
protocol::FlagEntry delta(std::uint32_t h, std::int32_t change) {
  return {h, 0, K::count_delta, static_cast<std::uint32_t>(change)};
}

ItemRules parsed(std::string_view text) {
  std::string why;
  const auto rules = ItemRules::parse(text, &why);
  INFO(why);
  REQUIRE(rules.has_value());
  return *rules;
}

std::string error_of(std::string_view text) {
  std::string why;
  CHECK_FALSE(ItemRules::parse(text, &why).has_value());
  return why;
}

ItemRules kit_rules() {
  const auto path = std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" / "kit" /
                    "shared_items.txt";
  std::string why;
  const auto rules = ItemRules::load(path, &why);
  INFO(why);
  REQUIRE(rules.has_value());
  return *rules;
}

bool shared(std::uint32_t hash) {
  const auto keys = item_shared_flags().keys();
  return std::find(keys.begin(), keys.end(), FlagKey{hash, 0}) != keys.end();
}

void deliver(FlagWatcher& to, const std::vector<coop::host::Outgoing>& out) {
  for (const auto& o : out) CHECK(to.receive(o.bytes));
}

}  // namespace

TEST_CASE("item rules: a list parses rules, caps and fills") {
  Reset reset;
  const auto rules = parsed(
      "# ZMP\n"
      "count PlayerStatus.CurrentRupee   # the wallet\n"
      "\n"
      "cap PlayerStatus.CurrentRupee 0xe0010a50\r\n"
      "fill PlayerStatus.MPMax PlayerStatus.MP\n"
      "count PlayerStatus.CurrentRupee\n"  // the same line twice is fine
      "max 0x5e671d92\n");
  REQUIRE(rules.rules().size() == 2);
  CHECK(rules.rules()[0].hash == kRupees);
  CHECK(rules.rules()[0].rule == ItemRule::count);
  CHECK(rules.rules()[1].hash == kFairyBlessMP);
  CHECK(rules.rules()[1].rule == ItemRule::max);
  REQUIRE(rules.caps().size() == 1);
  CHECK(rules.caps()[0].count == kRupees);
  CHECK(rules.caps()[0].cap == kRupeeCap);
  REQUIRE(rules.fills().size() == 1);
  CHECK(rules.fills()[0].from == kMPMax);
  CHECK(rules.fills()[0].to == kMP);
  CHECK(rules.rule_of(kRupees) == ItemRule::count);
  CHECK(rules.rule_of(kHookshot) == ItemRule::max);  // not listed: built in
  CHECK_FALSE(rules.rule_of(0x11).has_value());
  CHECK(parsed("").empty());
  CHECK(parsed("# nothing\n\n").empty());
}

TEST_CASE("item rules: bad lines are refused with their line") {
  Reset reset;
  CHECK(error_of("share PlayerStatus.CurrentRupee").starts_with("line 1: unknown rule"));
  CHECK(error_of("count").starts_with("line 1: count takes one flag"));
  CHECK(error_of("count A B").starts_with("line 1: count takes one flag"));
  CHECK(error_of("\nmax NotAnItem").starts_with("line 2: NotAnItem is not an item flag"));
  CHECK(error_of("max 0x").starts_with("line 1: a flag is"));
  CHECK(error_of("count IsGet.Item_Hookshot").find("not an Int") != std::string::npos);
  // Two rules for one flag.
  CHECK(error_of("count PlayerStatus.CurrentRupee\nmax PlayerStatus.CurrentRupee")
            .starts_with("line 2: PlayerStatus.CurrentRupee is given two rules"));
  // A cap needs a count, a fill a max from and a per-player to.
  CHECK(error_of("cap PlayerStatus.CurrentRupee 0xe0010a50").starts_with("line 1: cap takes a count"));
  CHECK(error_of("cap PouchData.Item_Hookshot.StockNum 0x1234").starts_with("line 1: cap takes a count"));
  CHECK(error_of("cap PlayerStatus.CurrentRupee").starts_with("line 1: cap takes two flags"));
  CHECK(error_of("fill PlayerStatus.MP PlayerStatus.MPMax").find("is not a max flag") != std::string::npos);
  CHECK(error_of("fill PlayerStatus.MPMax PouchData.Item_Hookshot.StockNum").find("is shared") !=
        std::string::npos);
  CHECK(error_of("fill PlayerStatus.MPMax IsGet.Item_Hookshot").find("two Ints") != std::string::npos);
  CHECK(error_of("fill PlayerStatus.MPMax PlayerStatus.MP\nfill PlayerStatus.MPMax PlayerStatus.Life")
            .starts_with("line 2:"));
  // A cap before its count's rule line is fine.
  CHECK(parsed("cap PlayerStatus.CurrentRupee 0xe0010a50\ncount PlayerStatus.CurrentRupee").caps().size() ==
        1);
}

TEST_CASE("item rules: what is in a bottle can never be shared (the fairy revive writes one)") {
  Reset reset;
  for (const char* rule : {"max", "any", "mirror"}) {
    const std::string line = std::string(rule) + " PlayerStatus.AssignableItem.BottleInfo.BottleA.BottleItem";
    CHECK(error_of(line).find("stays with each player") != std::string::npos);
  }
  CHECK(error_of("any 0x7d48df76").find("stays with each player") != std::string::npos);
  // Saying it stays per player is fine; the bottle itself is shared anyway.
  CHECK(parsed("player PlayerStatus.AssignableItem.BottleInfo.BottleA.BottleItem").rules().size() == 1);
}

TEST_CASE("item rules: applied, the table, the shared list and the counts follow them") {
  Reset reset;
  REQUIRE(find_item(kRupees)->rule == ItemRule::per_player);
  CHECK_FALSE(shared(kRupees));
  const auto applied = apply_item_rules(parsed(
      "count PlayerStatus.CurrentRupee\n"
      "max PouchData.Item_Hookshot.StockNum\n"  // as built
      "cap PlayerStatus.CurrentRupee 0xe0010a50\n"
      "fill PlayerStatus.MPMax PlayerStatus.MP\n"));
  CHECK(applied.changed == 1);
  CHECK(applied.same == 1);
  CHECK(applied.caps == 1);
  CHECK(applied.fills == 1);
  CHECK(find_item(kRupees)->rule == ItemRule::count);
  CHECK(coop::host::built_in_item_rule(kRupees) == ItemRule::per_player);
  CHECK(shared(kRupees));
  CHECK(coop::host::item_cap(kRupees) == kRupeeCap);
  CHECK(coop::host::item_fill(kMPMax) == kMP);
  MapStore game;
  ItemFlagStore store(game);
  CHECK(store.is_count(FlagKey{kRupees, 0}));

  // Applying again starts from the built-in rules; so does a reset.
  apply_item_rules(ItemRules{});
  CHECK(find_item(kRupees)->rule == ItemRule::per_player);
  CHECK_FALSE(shared(kRupees));
  CHECK_FALSE(coop::host::item_cap(kRupees).has_value());
  CHECK_FALSE(coop::host::item_fill(kMPMax).has_value());
  CHECK_FALSE(store.is_count(FlagKey{kRupees, 0}));
  apply_item_rules(parsed("count PlayerStatus.CurrentRupee"));
  coop::host::reset_item_rules();
  CHECK(find_item(kRupees)->rule == ItemRule::per_player);
  CHECK_FALSE(coop::host::set_item_rule(0x11, ItemRule::max));  // not an item
  CHECK_FALSE(coop::host::set_item_rule(0xff98ed58, ItemRule::count));  // IsGet: a Bool
}

// The common wallet some tests still try: the rules ZMP had (INVENTARIO.md).
static ItemRules common_wallet() {
  return parsed(
      "count PlayerStatus.CurrentRupee\n"
      "cap PlayerStatus.CurrentRupee 0xe0010a50\n"
      "fill PlayerStatus.MPMax PlayerStatus.MP\n");
}

TEST_CASE("item rules: the kit's list is ZMP's inventory, with each player's own wallet") {
  Reset reset;
  const auto applied = apply_item_rules(kit_rules());
  CHECK(applied.changed == 0);  // everything listed is as built
  CHECK(applied.caps == 0);
  CHECK(applied.fills == 1);
  // Each player's own rupees (2026-11-11); the magic meter filled when it grows.
  CHECK(find_item(kRupees)->rule == ItemRule::per_player);
  CHECK_FALSE(shared(kRupees));
  CHECK_FALSE(coop::host::item_cap(kRupees).has_value());
  CHECK(coop::host::item_fill(kMPMax) == kMP);
  // The wallet's size stays the group's: the wallet upgrades are shared items.
  for (const auto h : {0xafa4bb52u /* Item_Wallet_Large */, 0xb5031190u /* IsGet.Item_Wallet_Large */}) {
    REQUIRE(find_item(h) != nullptr);
    CHECK(find_item(h)->rule != ItemRule::per_player);
    CHECK(shared(h));
  }
  // Shared: items, owned equipment (tunics, boots), bags, bottles, magic size, and the Deku
  // Shield since test10 (upstream e2132ffc, issue 130: one player's purchase reaches everyone).
  for (const auto h : {kHookshot, kBombBag, kHoverBoots, kZoraTunic, kMPMax, kDekuShield}) {
    CHECK(find_item(h)->rule == ItemRule::max);
    CHECK(shared(h));
  }
  CHECK(find_item(kBottleA)->rule == ItemRule::any);
  CHECK(find_item(kBottleAValid)->rule == ItemRule::any);
  // Each player's own: ammo, bottle contents, life, magic now.
  for (const auto h : {kBombs, kBottleAItem, kLife, kMP, kFairyBlessMP}) {
    CHECK(find_item(h)->rule == ItemRule::per_player);
    CHECK_FALSE(shared(h));
  }
  // Worn gear and item buttons are not item flags at all: never sent.
  for (const auto h : {0xa95a3521u, 0xee463235u, 0x008d7f0cu, 0xd107a85du, 0xa0029837u, 0xbc32ce24u}) {
    CHECK(find_item(h) == nullptr);
  }
}

TEST_CASE("item rules: a rupee gain stops at the wallet's size, a spend never below 0") {
  Reset reset;
  MapStore game;
  ItemFlagStore store(game);
  game.hold(kRupees, 195);
  game.hold(kRupeeCap, 200);
  // Built in, rupees are each player's own: a delta is refused.
  CHECK_FALSE(store.write(delta(kRupees, 10)));
  CHECK(game.get(kRupees) == 195);

  apply_item_rules(common_wallet());
  CHECK(store.write(delta(kRupees, 10)));
  CHECK(game.get(kRupees) == 200);  // a full wallet, as in the game
  CHECK(store.stats().capped == 1);
  CHECK(store.write(delta(kRupees, -150)));
  CHECK(game.get(kRupees) == 50);
  CHECK(store.write(delta(kRupees, -80)));  // two spent at once: never below 0
  CHECK(game.get(kRupees) == 0);
  // A wallet over its size (a save from elsewhere) is never lowered by a gain.
  game.hold(kRupees, 250);
  CHECK(store.write(delta(kRupees, 5)));
  CHECK(game.get(kRupees) == 250);
  CHECK(store.write(delta(kRupees, -5)));
  CHECK(game.get(kRupees) == 245);
  // A size not known yet (0, or not readable) does not hold the gain.
  game.hold(kRupeeCap, 0);
  CHECK(store.write(delta(kRupees, 10)));
  CHECK(game.get(kRupees) == 255);
  game.values.erase(FlagKey{kRupeeCap, 0});
  CHECK(store.write(delta(kRupees, 10)));
  CHECK(game.get(kRupees) == 265);
  CHECK(store.stats().capped == 2);
}

TEST_CASE("item rules: a common wallet list, two players gaining and spending at once lose nothing") {
  Reset reset;
  apply_item_rules(common_wallet());
  MapStore hw, aw, bw;
  for (auto* w : {&hw, &aw, &bw}) {
    w->hold(kRupees, 100);
    w->hold(kRupeeCap, 500);
  }
  ItemFlagStore hs(hw), as(aw), bs(bw);
  FlagWatcher host(item_shared_flags(), FlagRole::host);
  FlagWatcher a(item_shared_flags(), FlagRole::player);
  FlagWatcher b(item_shared_flags(), FlagRole::player);
  CHECK(host.frame(hs).empty());  // the first frames are the baseline
  CHECK(a.frame(as).empty());
  CHECK(b.frame(bs).empty());

  // In the same frame: A picks up 5, B buys a 40-rupee shield, the host cuts grass for 1.
  aw.hold(kRupees, 105);
  bw.hold(kRupees, 60);
  hw.hold(kRupees, 101);
  const auto from_a = a.frame(as);
  const auto from_b = b.frame(bs);
  const auto host_own = host.frame(hs);
  REQUIRE(from_a.size() == 1);
  protocol::Message m;
  REQUIRE(protocol::decode(from_a[0].bytes, m) == protocol::Error::none);
  CHECK(std::get<protocol::FlagSet>(m).flags == std::vector{delta(kRupees, 5)});  // a change, not a value

  deliver(a, host_own);
  deliver(b, host_own);
  deliver(host, from_a);
  deliver(host, from_b);
  const auto answer = host.frame(hs);
  CHECK(hw.get(kRupees) == 66);  // 100 + 1 + 5 - 40
  deliver(a, answer);
  deliver(b, answer);
  for (int round = 0; round < 3; ++round) {
    const auto more_a = a.frame(as);
    const auto more_b = b.frame(bs);
    deliver(host, more_a);
    deliver(host, more_b);
    const auto more = host.frame(hs);
    deliver(a, more);
    deliver(b, more);
  }
  CHECK(hw.get(kRupees) == 66);
  CHECK(aw.get(kRupees) == 66);
  CHECK(bw.get(kRupees) == 66);
}

TEST_CASE("item rules: a magic meter grown by another player fills ours") {
  Reset reset;
  MapStore game;
  game.hold(kMPMax, 24);
  game.hold(kMP, 10);
  {
    ItemFlagStore store(game);  // built in: no fill
    CHECK(store.write(i(kMPMax, 48)));
    CHECK(game.get(kMPMax) == 48);
    CHECK(game.get(kMP) == 10);
  }
  apply_item_rules(kit_rules());
  game.hold(kMPMax, 24);
  ItemFlagStore store(game);
  CHECK(store.write(i(kMPMax, 48)));
  CHECK(game.get(kMPMax) == 48);
  CHECK(game.get(kMP) == 48);
  CHECK(store.stats().filled == 1);
  // A smaller value is no growth (max keeps ours): nothing filled.
  game.hold(kMP, 5);
  CHECK(store.write(i(kMPMax, 24)));
  CHECK(game.get(kMPMax) == 48);
  CHECK(game.get(kMP) == 5);
  // The host's save decides (exact): its value, no fill.
  CHECK(store.write_exact(i(kMPMax, 96)));
  CHECK(game.get(kMPMax) == 96);
  CHECK(game.get(kMP) == 5);
  // Magic above the new size is never lowered.
  game.hold(kMPMax, 24);
  game.hold(kMP, 90);
  CHECK(store.write(i(kMPMax, 48)));
  CHECK(game.get(kMP) == 90);
  CHECK(store.stats().filled == 1);
}

TEST_CASE("item rules: no fill without Link (a load would put the save's magic back)") {
  Reset reset;
  apply_item_rules(kit_rules());
  MapStore game;
  game.hold(kMPMax, 24);
  game.hold(kMP, 10);
  LoadMend load;
  load.frame(false);  // the title screen
  ItemFlagStore store(game, &load);
  CHECK(store.write(i(kMPMax, 48)));
  CHECK(game.get(kMPMax) == 48);
  CHECK(game.get(kMP) == 10);
  load.frame(true);
  ItemFlagStore with_link(game, &load);
  game.hold(kMPMax, 24);
  CHECK(with_link.write(i(kMPMax, 48)));
  CHECK(game.get(kMP) == 48);
}
