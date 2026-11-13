// ORM "each player's own wallet, everyone's rupees on screen" (
// 2026-11-11; oot-remake docs/INVENTARIO.md, "Cartera de cada uno";
// docs/TIPOS_MENSAJE.md): the kit's item list leaves PlayerStatus.CurrentRupee
// per player, each game sends its count in PLAYER_EQUIP's rupees component
// (29), and the HUD panel lists every connected player's wallet.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../host/fake_memory.hpp"
#include "coop/host/flag_watcher.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/item_rules.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/host/wallets.hpp"
#include "coop/protocol/codec.hpp"
#include "orm_hud.h"

namespace {

namespace proto = coop::protocol;
using Bytes = std::vector<std::uint8_t>;
using coop::host::FlagKey;
using coop::host::FlagRole;
using coop::host::FlagWatcher;
using coop::host::ItemFlagStore;
using coop::host::kCurrentRupeeFlag;

constexpr std::uint32_t kRupeeCap = 0xe0010a50;  // 0xf80f8077.Rupee, the wallet's size

struct Reset {
  Reset() { coop::host::reset_item_rules(); }
  ~Reset() { coop::host::reset_item_rules(); }
};

coop::host::ItemRules kit_rules() {
  const auto path = std::filesystem::path(COOP_TEST_SOURCE_DIR) / ".." / "tools" / "release" / "kit" /
                    "shared_items.txt";
  std::string why;
  const auto rules = coop::host::ItemRules::load(path, &why);
  INFO(why);
  REQUIRE(rules.has_value());
  return *rules;
}

// A game's flags as a plain map: unknown flags cannot be read.
class MapStore final : public coop::host::FlagStore {
 public:
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& entry) override {
    values[entry.key] = entry;
    return true;
  }
  void hold(std::uint32_t hash, std::int32_t v) {
    values[FlagKey{hash, 0}] = {hash, 0, proto::FlagKind::int32, static_cast<std::uint32_t>(v)};
  }
  std::int32_t get(std::uint32_t hash) const {
    return static_cast<std::int32_t>(values.at(FlagKey{hash, 0}).value);
  }
  std::map<FlagKey, proto::FlagEntry> values;
};

bool carries_rupees(const std::vector<coop::host::Outgoing>& out) {
  for (const auto& o : out) {
    proto::Message m;
    if (proto::decode(o.bytes, m) != proto::Error::none) continue;
    const std::vector<proto::FlagEntry>* flags = nullptr;
    if (const auto* set = std::get_if<proto::FlagSet>(&m)) flags = &set->flags;
    if (const auto* state = std::get_if<proto::FlagState>(&m)) flags = &state->flags;
    if (flags != nullptr && std::any_of(flags->begin(), flags->end(), [](const proto::FlagEntry& e) {
          return e.key.hash == kCurrentRupeeFlag;
        })) {
      return true;
    }
  }
  return false;
}

Bytes must_encode(const proto::Message& m) {
  Bytes out;
  REQUIRE(proto::encode(m, out) == proto::Error::none);
  return out;
}

proto::Error decode_bytes(const Bytes& b) {
  proto::Message m;
  return proto::decode(b, m);
}

CoopHook::PlayerRow row(std::uint8_t number, std::string name, bool self, std::optional<std::uint32_t> rupees,
                        std::array<std::uint8_t, 3> color) {
  CoopHook::PlayerRow r;
  r.number = number;
  r.name = std::move(name);
  r.self = self;
  r.state = "connected";
  r.rupees = rupees;
  r.color = color;
  return r;
}

}  // namespace

TEST_CASE("wallet: the kit's list keeps each player's rupees their own (2026-11-11)") {
  Reset reset;
  coop::host::apply_item_rules(kit_rules());
  const auto* item = coop::host::find_item(kCurrentRupeeFlag);
  REQUIRE(item != nullptr);
  CHECK(item->name == "PlayerStatus.CurrentRupee");
  CHECK(item->rule == coop::host::ItemRule::per_player);
  CHECK_FALSE(coop::host::item_cap(kCurrentRupeeFlag).has_value());
  const auto keys = coop::host::item_shared_flags().keys();
  CHECK(std::find(keys.begin(), keys.end(), FlagKey{kCurrentRupeeFlag, 0}) == keys.end());
  // Another game's value is never written into ours, as a value or as a change.
  MapStore game;
  game.hold(kCurrentRupeeFlag, 40);
  ItemFlagStore store(game);
  CHECK_FALSE(store.is_count(FlagKey{kCurrentRupeeFlag, 0}));
  CHECK_FALSE(store.write({kCurrentRupeeFlag, 0, proto::FlagKind::int32, 99}));
  CHECK_FALSE(store.write({kCurrentRupeeFlag, 0, proto::FlagKind::count_delta, 5}));
  CHECK(game.get(kCurrentRupeeFlag) == 40);
}

TEST_CASE("wallet: three players gain and spend at once, each wallet moves alone") {
  Reset reset;
  coop::host::apply_item_rules(kit_rules());
  MapStore hw, aw, bw;
  for (auto* w : {&hw, &aw, &bw}) {
    w->hold(kCurrentRupeeFlag, 100);
    w->hold(kRupeeCap, 500);
  }
  ItemFlagStore hs(hw), as(aw), bs(bw);
  FlagWatcher host(coop::host::item_shared_flags(), FlagRole::host);
  FlagWatcher a(coop::host::item_shared_flags(), FlagRole::player);
  FlagWatcher b(coop::host::item_shared_flags(), FlagRole::player);
  CHECK(host.frame(hs).empty());  // the baseline
  CHECK(a.frame(as).empty());
  CHECK(b.frame(bs).empty());
  // The same frame: A picks up 5, B buys a 40-rupee shield, the host cuts grass for 1.
  aw.hold(kCurrentRupeeFlag, 105);
  bw.hold(kCurrentRupeeFlag, 60);
  hw.hold(kCurrentRupeeFlag, 101);
  for (int round = 0; round < 3; ++round) {
    const auto from_a = a.frame(as);
    const auto from_b = b.frame(bs);
    const auto from_host = host.frame(hs);
    CHECK_FALSE(carries_rupees(from_a));
    CHECK_FALSE(carries_rupees(from_b));
    CHECK_FALSE(carries_rupees(from_host));
    for (const auto& o : from_a) host.receive(o.bytes);
    for (const auto& o : from_b) host.receive(o.bytes);
    for (const auto& o : from_host) {
      a.receive(o.bytes);
      b.receive(o.bytes);
    }
  }
  CHECK(hw.get(kCurrentRupeeFlag) == 101);
  CHECK(aw.get(kCurrentRupeeFlag) == 105);
  CHECK(bw.get(kCurrentRupeeFlag) == 60);
}

TEST_CASE("wallet: the value sent is the game's, never below 0, at most kMaxRupees") {
  CHECK_FALSE(coop::host::wallet_value(std::nullopt).has_value());
  CHECK_FALSE(coop::host::wallet_value(-1).has_value());
  CHECK(coop::host::wallet_value(0) == 0u);
  CHECK(coop::host::wallet_value(87) == 87u);
  CHECK(coop::host::wallet_value(500) == 500u);
  CHECK(coop::host::wallet_value(1'000'000) == proto::kMaxRupees);
}

TEST_CASE("wallet: the reader reads PlayerStatus.CurrentRupee into the equipment") {
  using coop::host::test::FakeMemory;
  CHECK(kCurrentRupeeFlag == proto::flag_hash("PlayerStatus.CurrentRupee"));
  FakeMemory m;
  coop::host::PlayerReader reader(m);
  CHECK_FALSE(reader.read_equipment().has_value());  // no game data: nothing
  // A GameDataMgr with only an Int table (docs/flags.md: M + 0xd0 count,
  // +0xd8 entries, +0xe0 slots, +0xec slot count; entries of 0x28, the value
  // at +0x1c), one slot holding the wallet.
  constexpr std::uint64_t kMgr = 0x88000000, kEntries = 0x88100000, kSlots = 0x88200000;
  m.put_u64(FakeMemory::kBase + 0x4e842a8, kMgr);
  m.put_u32(kMgr + 0xd0, 1);
  m.put_u64(kMgr + 0xd8, kEntries);
  m.put_u64(kMgr + 0xe0, kSlots);
  m.put_u32(kMgr + 0xec, 1);
  m.put_u32(kSlots, kCurrentRupeeFlag);
  m.put_u32(kSlots + 4, 0);
  m.put_u32(kEntries + 0x1c, 87);
  auto e = reader.read_equipment();
  REQUIRE(e.has_value());
  CHECK(e->rupees == 87u);
  CHECK_FALSE(e->weapon);  // no String64 table: the names are not read
  m.put_u32(kEntries + 0x1c, static_cast<std::uint32_t>(-3));  // a bad read: no count
  CHECK_FALSE(reader.read_equipment().has_value());
}

TEST_CASE("wallet: PLAYER_EQUIP carries the rupees as component 29, a u32") {
  CHECK(static_cast<int>(proto::EquipComponent::rupees) == 29);
  proto::PlayerEquip m;
  m.equipment.weapon = "A";
  m.equipment.rupees = 87;
  const Bytes b = must_encode(m);
  CHECK(b == Bytes{17, 0, 16, 0, 0,
                   1, 0, 3, 0, 1, 0, 'A',
                   29, 0, 4, 0, 87, 0, 0, 0});
  proto::Message back;
  REQUIRE(proto::decode(b, back) == proto::Error::none);
  CHECK(std::get<proto::PlayerEquip>(back) == m);
  // Rupees alone, and the most a message may carry.
  proto::PlayerEquip only;
  only.equipment.rupees = proto::kMaxRupees;
  REQUIRE(proto::decode(must_encode(only), back) == proto::Error::none);
  CHECK(std::get<proto::PlayerEquip>(back).equipment.rupees == proto::kMaxRupees);
  CHECK_FALSE(std::get<proto::PlayerEquip>(back).equipment.weapon);
  // More than that is refused, both ways.
  Bytes out;
  only.equipment.rupees = proto::kMaxRupees + 1;
  CHECK(proto::encode(only, out) == proto::Error::bad_value);
  CHECK(decode_bytes(Bytes{17, 0, 9, 0, 0, 29, 0, 4, 0, 0xa0, 0x86, 0x01, 0x00}) == proto::Error::bad_value);
  // Of exactly four bytes, at most once.
  CHECK(decode_bytes(Bytes{17, 0, 7, 0, 0, 29, 0, 2, 0, 1, 0}) == proto::Error::bad_length);
  CHECK(decode_bytes(Bytes{17, 0, 11, 0, 0, 29, 0, 6, 0, 1, 0, 0, 0, 0, 0}) == proto::Error::bad_length);
  CHECK(decode_bytes(Bytes{17, 0, 17, 0, 0, 29, 0, 4, 0, 1, 0, 0, 0, 29, 0, 4, 0, 2, 0, 0, 0}) ==
        proto::Error::duplicate_component);
  // A message without it (an older build) is the same as before.
  REQUIRE(proto::decode(Bytes{17, 0, 8, 0, 0, 2, 0, 3, 0, 1, 0, 'x'}, back) == proto::Error::none);
  CHECK_FALSE(std::get<proto::PlayerEquip>(back).equipment.rupees);
}

TEST_CASE("wallet: a PLAYER_EQUIP without rupees keeps the last count") {
  proto::Equipment before;
  before.weapon = "Weapon_Sword_Kokiri";
  before.rupees = 42;
  proto::Equipment now;
  now.weapon = "Weapon_Sword_Master";
  CHECK(proto::merge_equipment(before, now).rupees == 42u);
  now.rupees = 0;  // spent it all: 0 is a count
  CHECK(proto::merge_equipment(before, now).rupees == 0u);
}

TEST_CASE("wallet: the log tells each change once, ours too") {
  coop::host::WalletLog wallets;
  using Seen = coop::host::WalletSeen;
  CHECK(wallets.update({}).empty());
  auto lines = wallets.update({Seen{1, "Link", true, 20}, Seen{2, "Ana", false, std::nullopt}});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "P1 'Link' (you) has 20 rupees");
  CHECK(wallets.update({Seen{1, "Link", true, 20}, Seen{2, "Ana", false, std::nullopt}}).empty());
  lines = wallets.update({Seen{1, "Link", true, 25}, Seen{2, "Ana", false, 87}});
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "P1 'Link' (you) has 25 rupees (was 20)");
  CHECK(lines[1] == "P2 'Ana' has 87 rupees");
  // Ana leaves and comes back: her count is new again.
  CHECK(wallets.update({Seen{1, "Link", true, 25}}).empty());
  lines = wallets.update({Seen{1, "Link", true, 25}, Seen{2, "Ana", false, 87}});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "P2 'Ana' has 87 rupees");
  // Numbers outside the room's are ignored.
  CHECK(wallets.update({Seen{0, "?", false, 5}, Seen{7, "?", false, 5}, Seen{1, "Link", true, 25},
                    Seen{2, "Ana", false, 87}})
            .empty());
}

TEST_CASE("wallet: the HUD panel lists every player's rupees, hidden with the panel") {
  const std::vector<CoopHook::PlayerRow> rows{row(1, "Link", true, 25, {0xff, 0xe1, 0x78}),
                                              row(2, "Ana", false, 87, {0xff, 0x00, 0x00}),
                                              row(3, "", false, std::nullopt, {0x00, 0x00, 0x00})};
  const std::vector<CoopHook::OverlayLine> lines{{"co-op: 3 players", false, std::nullopt}};
  const OrmHud::Model shown = OrmHud::Build(std::nullopt, rows, lines, true);
  REQUIRE(shown.wallets.size() == 3);
  CHECK(shown.wallets[0].name == "Link");
  CHECK(shown.wallets[0].self);
  CHECK(shown.wallets[0].rgb == 0xffe178u);
  CHECK(shown.wallets[0].rupees == 25u);
  CHECK(shown.wallets[1].rgb == 0xff0000u);
  CHECK(shown.wallets[1].rupees == 87u);
  CHECK(shown.wallets[2].name == "P3");             // no name from the room
  CHECK(shown.wallets[2].rgb == OrmHud::kWhite);   // no colour
  CHECK_FALSE(shown.wallets[2].rupees);             // drawn as "-"
  // With the panel hidden (F9), no list; with no count known yet, no list.
  CHECK(OrmHud::Build(std::nullopt, rows, lines, false).wallets.empty());
  std::vector<CoopHook::PlayerRow> unknown = rows;
  for (auto& r : unknown) r.rupees.reset();
  CHECK(OrmHud::Build(std::nullopt, unknown, lines, true).wallets.empty());
  // A list alone is something to draw.
  CHECK_FALSE(OrmHud::Build(std::nullopt, rows, {}, true).Empty());
}

TEST_CASE("wallet: the HUD draws the name in its colour, a rupee, then the count") {
  OrmHud::Model model;
  model.wallets.push_back({2, "Ana", 0xff0000, 87, false});
  const OrmHud::Box picture{0, 0, 1920, 1080};
  const auto rects = OrmHud::Draw(model, picture, 1920, 1080);
  const int scale = OrmHud::PanelScale(1080);
  REQUIRE(scale == 2);
  const int margin = 1080 / 45;            // 24
  const int cell = 7 * scale;              // one glyph
  const int icon = margin + 4 * cell;      // "Ana", a space
  const int count = icon + 2 * cell;       // the rupee, a space
  const auto of = [&](std::uint32_t rgb) {
    std::vector<OrmHud::Rect> v;
    std::copy_if(rects.begin(), rects.end(), std::back_inserter(v),
                 [&](const OrmHud::Rect& r) { return r.rgb == rgb; });
    return v;
  };
  const auto name = of(0xff0000);
  const auto rupee = of(OrmHud::kRupee);
  const auto digits = of(OrmHud::kRupeeCount);
  REQUIRE_FALSE(name.empty());
  REQUIRE_FALSE(rupee.empty());
  REQUIRE_FALSE(digits.empty());
  for (const auto& r : name) {
    CHECK(r.x >= margin);
    CHECK(r.x + r.width <= icon);
  }
  for (const auto& r : rupee) {  // inside its glyph cell
    CHECK(r.x >= icon);
    CHECK(r.x + r.width <= icon + cell);
    CHECK(r.y >= margin);
    CHECK(r.y + r.height <= margin + 13 * scale);
  }
  for (const auto& r : digits) {
    CHECK(r.x >= count);
    CHECK(r.x + r.width <= count + 2 * cell);  // "87"
  }
  // Under a panel line, the list starts half a line lower than a second line would.
  model.panel.push_back({"co-op", false, std::nullopt});
  const auto under = OrmHud::Draw(model, picture, 1920, 1080);
  int top = 1 << 30;
  for (const auto& r : under) {
    if (r.rgb == OrmHud::kRupee) top = std::min(top, r.y);
  }
  CHECK(top >= margin + (13 + 1) * scale + (13 + 1) * scale / 2);
}

// ORM (RIESGOS_RESUELTOS.md, "Mundo e historia"): where each HUD count comes from.
TEST_CASE("wallet: the HUD count of each player, by path, and its log line") {
  using coop::host::WalletPath;
  using coop::host::WalletRow;
  auto me = [](std::optional<std::uint32_t> sent, std::optional<std::uint32_t> game) {
    WalletRow r;
    r.number = 1;
    r.name = "Link";
    r.self = true;
    r.live = true;
    r.has_equipment = sent.has_value();
    r.sent = sent;
    r.game = game;
    return r;
  };
  auto them = [](bool live, bool equip, std::optional<std::uint32_t> sent) {
    WalletRow r;
    r.number = 2;
    r.name = "Ana";
    r.live = live;
    r.has_equipment = equip;
    r.sent = sent;
    return r;
  };
  // Ours: our game's count wins over the one we sent (a send that failed stays behind).
  auto s = coop::host::wallet_shown(me(20, 25));
  CHECK(s.path == WalletPath::own_game);
  CHECK(s.rupees == 25u);
  CHECK_FALSE(s.stale);
  CHECK(coop::host::wallet_shown(me(20, std::nullopt)).path == WalletPath::sent);
  CHECK(coop::host::wallet_shown(me(std::nullopt, std::nullopt)).path == WalletPath::waiting);
  CHECK(coop::host::wallet_shown(me(std::nullopt, 999999)).rupees == coop::protocol::kMaxRupees);
  // Theirs.
  s = coop::host::wallet_shown(them(true, true, 87));
  CHECK(s.path == WalletPath::peer);
  CHECK(s.rupees == 87u);
  s = coop::host::wallet_shown(them(false, true, 87));
  CHECK(s.path == WalletPath::stale);
  CHECK(s.rupees == 87u);
  CHECK(s.stale);
  s = coop::host::wallet_shown(them(true, true, std::nullopt));
  CHECK(s.path == WalletPath::absent);
  CHECK_FALSE(s.rupees);
  CHECK(coop::host::wallet_shown(them(true, false, std::nullopt)).path == WalletPath::waiting);
  // Never another player's game count as ours: `game` is read for ours only.
  WalletRow odd = them(true, true, 87);
  odd.game = 43;
  CHECK(coop::host::wallet_shown(odd).rupees == 87u);

  coop::host::WalletPaths paths;
  std::uint64_t t = 1'000'000;
  auto lines = paths.update({me(20, 25), them(true, false, std::nullopt)}, t);
  REQUIRE(lines.size() == 1);  // Ana's "waiting" only after kWaitingUs
  CHECK(lines[0] == "HUD: P1 'Link' (you) from our game's PlayerStatus.CurrentRupee");
  CHECK(paths.update({me(20, 25), them(true, false, std::nullopt)}, t += 1'000'000).empty());
  lines = paths.update({me(20, 25), them(true, false, std::nullopt)}, t += coop::host::WalletPaths::kWaitingUs);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "HUD: P2 'Ana' no PLAYER_EQUIP from them yet: shown '-'");
  lines = paths.update({me(20, 25), them(true, true, std::nullopt)}, t += 16'000);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find("HUD: P2 'Ana' their PLAYER_EQUIP carries no rupees") == 0);
  lines = paths.update({me(20, 25), them(true, true, 87)}, t += 16'000);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "HUD: P2 'Ana' from their PLAYER_EQUIP");
  lines = paths.update({me(20, std::nullopt), them(false, true, 87)}, t += 16'000);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "HUD: P1 'Link' (you) from the count we last sent (our game is not read now)");
  CHECK(lines[1] == "HUD: P2 'Ana' quiet (no sample for 1 s): their last count shown with a '?'");
  // Ana leaves: forgotten; back with a count, told again.
  CHECK(paths.update({me(20, std::nullopt)}, t += 16'000).empty());
  lines = paths.update({me(20, std::nullopt), them(true, true, 90)}, t += 16'000);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "HUD: P2 'Ana' from their PLAYER_EQUIP");
}

TEST_CASE("wallet: the HUD draws a quiet player's last count with a '?'") {
  std::vector<CoopHook::PlayerRow> rows{row(1, "Link", true, 25, {0xff, 0xe1, 0x78}),
                                        row(2, "Ana", false, 87, {0xff, 0x00, 0x00})};
  rows[1].rupees_stale = true;
  const std::vector<CoopHook::OverlayLine> lines{{"co-op: 2 players", false, std::nullopt}};
  const OrmHud::Model shown = OrmHud::Build(std::nullopt, rows, lines, true);
  REQUIRE(shown.wallets.size() == 2);
  CHECK_FALSE(shown.wallets[0].stale);
  CHECK(shown.wallets[1].stale);
  // Drawn: "87?" is three glyphs wide, "87" two.
  OrmHud::Model fresh, quiet;
  fresh.wallets.push_back({2, "Ana", 0xff0000, 87, false, false});
  quiet.wallets.push_back({2, "Ana", 0xff0000, 87, false, true});
  const OrmHud::Box picture{0, 0, 1920, 1080};
  auto right = [&](const OrmHud::Model& m) {
    std::int64_t x = 0;
    for (const auto& r : OrmHud::Draw(m, picture, 1920, 1080)) {
      if (r.rgb == OrmHud::kRupeeCount) x = std::max<std::int64_t>(x, r.x + r.width);
    }
    return x;
  };
  CHECK(right(quiet) > right(fresh));
}
