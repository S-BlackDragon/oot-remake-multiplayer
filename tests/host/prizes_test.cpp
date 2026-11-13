// ORM one-time prizes won by two at once (coop/host/prizes.hpp, oot-remake docs/MINIJUEGOS.md
// section 4): the lowest player number keeps the prize, the others give their heart piece back.
// Fake game flags; the "agreed" values come from the real watcher.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/host/flag_watcher.hpp"
#include "coop/host/prizes.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  bool refuse_writes = false;
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    if (refuse_writes || !values.contains(e.key)) return false;
    values[e.key] = e;
    return true;
  }
  void set_bool(FlagKey k, std::uint32_t v) { values[k] = {k, proto::FlagKind::boolean, v}; }
  void set_int(FlagKey k, std::int32_t v) { values[k] = {k, proto::FlagKind::int32, static_cast<std::uint32_t>(v)}; }
  std::int32_t count() const {
    return static_cast<std::int32_t>(values.at(FlagKey(prize::heart_piece_count(), 0)).value);
  }
};

FlagKey chests() { return FlagKey(flag_hash("MiniGame_ChoiceTreasure_IsClear"), 0); }
FlagKey gallery() { return FlagKey(flag_hash("MiniGame_HitTarget_HrlCity_Get_BulletBag"), 0); }
FlagKey pieces() { return FlagKey(prize::heart_piece_count(), 0); }

FakeStore game(std::int32_t count = 1) {
  FakeStore g;
  for (const auto& p : prize::prizes()) g.set_bool(FlagKey(p.hash, 0), 0);
  g.set_int(pieces(), count);
  return g;
}

FlagWatcher::Received agreed(FlagKey k, std::uint8_t from) {
  const proto::FlagEntry one{k, proto::FlagKind::boolean, 1};
  return FlagWatcher::Received{k, one, one, from, FlagWatcher::ReceivedKind::live};
}

prize::PrizeView in_game(std::uint8_t self, std::uint64_t now_us) { return {self, true, now_us}; }

bool has(const std::vector<std::string>& lines, std::string_view part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("prizes: the table") {
  std::set<std::uint32_t> seen;
  std::size_t pieces_n = 0;
  for (const auto& p : prize::prizes()) {
    CHECK(p.hash == flag_hash(p.name));
    CHECK(seen.insert(p.hash).second);
    pieces_n += p.heart_piece ? 1 : 0;
  }
  CHECK(prize::prizes().size() == 33);
  CHECK(pieces_n == 17);
  REQUIRE(prize::prize_of(chests().hash) != nullptr);
  CHECK(prize::prize_of(chests().hash)->heart_piece);
  CHECK_FALSE(prize::prize_of(gallery().hash)->heart_piece);
  CHECK(prize::prize_of(0x1234u) == nullptr);
  CHECK(prize::heart_piece_count() == 0x51ec97b3u);  // game_items.cpp's PouchData.Obj_Heart_Piece.StockNum
}

TEST_CASE("prizes: two win the chest game at once, the higher number gives its piece back") {
  // We are P2; P1 won too.
  FakeStore g = game(1);
  prize::PrizeArbiter arb;
  std::uint64_t t = 1'000'000;
  arb.frame(in_game(2, t), g, {}, {});
  g.set_bool(chests(), 1);  // our game: the prize flag ...
  arb.frame(in_game(2, t += 16'000), g, {}, {});
  CHECK(has(arb.take_notes(), "MiniGame_ChoiceTreasure_IsClear: won in our game (P2)"));
  g.set_int(pieces(), 2);  // ... and the piece with the text box
  arb.frame(in_game(2, t += 16'000), g, {}, {});
  // P1's 1 arrives over ours (the watcher's agreed), and P1's +1 delta.
  g.set_int(pieces(), 3);
  arb.frame(in_game(2, t += 300'000), g, {}, {agreed(chests(), 1)});
  const auto notes = arb.take_notes();
  CHECK(has(notes, "won here and by P1 at once: P1 keeps it (the lowest number); our heart piece goes back"));
  CHECK(has(notes, "our heart piece given back (Obj_Heart_Piece 3 -> 2)"));
  CHECK(g.count() == 2);
  CHECK(arb.given_back() == 1);
  // Never twice.
  arb.frame(in_game(2, t += 16'000), g, {}, {agreed(chests(), 1)});
  CHECK(g.count() == 2);
  CHECK(arb.take_notes().empty());
}

TEST_CASE("prizes: the lowest number keeps it, items are never given back") {
  SUBCASE("we are P1") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(1, t), g, {}, {});
    g.set_bool(chests(), 1);
    g.set_int(pieces(), 2);
    arb.frame(in_game(1, t += 16'000), g, {}, {});
    arb.frame(in_game(1, t += 300'000), g, {}, {agreed(chests(), 3), agreed(chests(), 2)});
    const auto notes = arb.take_notes();
    CHECK(has(notes, "won here and by P2, P3 at once: P1 keeps it (the lowest number); we keep ours"));
    CHECK(g.count() == 2);
  }
  SUBCASE("a bag: nothing doubled") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(gallery(), 1);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
    arb.frame(in_game(2, t += 300'000), g, {}, {agreed(gallery(), 1)});
    CHECK(has(arb.take_notes(), "nothing doubled, nothing given back"));
    CHECK(g.count() == 1);
  }
}

TEST_CASE("prizes: three win at once: the lowest keeps it whatever the order the others' 1 come in") {
  SUBCASE("we are P2: P3's 1 first, then P1's: ours goes back") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(chests(), 1);
    g.set_int(pieces(), 2);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
    arb.frame(in_game(2, t += 300'000), g, {}, {agreed(chests(), 3)});
    CHECK(has(arb.take_notes(), "won here and by P3 at once: P2 keeps it (the lowest number); we keep ours"));
    CHECK(g.count() == 2);
    g.set_int(pieces(), 4);  // P3's +1 and P1's +1
    arb.frame(in_game(2, t += 300'000), g, {}, {agreed(chests(), 1)});
    const auto notes = arb.take_notes();
    CHECK(has(notes, "won here and by P1, P3 at once: P1 keeps it (the lowest number); our heart piece goes back"));
    CHECK(has(notes, "our heart piece given back (Obj_Heart_Piece 4 -> 3)"));
    CHECK(arb.given_back() == 1);
  }
  SUBCASE("we are P3: P2's 1 first, then P1's: given back once") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(3, t), g, {}, {});
    g.set_bool(chests(), 1);
    g.set_int(pieces(), 2);
    arb.frame(in_game(3, t += 16'000), g, {}, {});
    arb.frame(in_game(3, t += 300'000), g, {}, {agreed(chests(), 2)});
    CHECK(has(arb.take_notes(), "P2 keeps it (the lowest number); our heart piece goes back"));
    CHECK(g.count() == 1);
    arb.frame(in_game(3, t += 300'000), g, {}, {agreed(chests(), 1)});
    CHECK(has(arb.take_notes(), "won here and by P1, P2 at once: P1 keeps it (the lowest number); ours goes back already"));
    CHECK(g.count() == 1);
    CHECK(arb.given_back() == 1);
  }
}

TEST_CASE("prizes: what is not a double win") {
  SUBCASE("the other's 1 came first (received): not our win") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(chests(), 1);
    const proto::FlagEntry zero{chests(), proto::FlagKind::boolean, 0}, one{chests(), proto::FlagKind::boolean, 1};
    arb.frame(in_game(2, t += 16'000), g, {FlagWatcher::Received{chests(), zero, one, 1}}, {});
    arb.frame(in_game(2, t += 16'000), g, {}, {agreed(chests(), 1)});
    CHECK(arb.take_notes().empty());
  }
  SUBCASE("our own echo") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(chests(), 1);
    g.set_int(pieces(), 2);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
    arb.take_notes();
    arb.frame(in_game(2, t += 16'000), g, {}, {agreed(chests(), 2), agreed(chests(), 0)});
    CHECK(arb.take_notes().empty());
    CHECK(g.count() == 2);
  }
  SUBCASE("too late: past the window") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(chests(), 1);
    g.set_int(pieces(), 2);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
    arb.take_notes();
    arb.frame(in_game(2, t += prize::PrizeArbiter::kWindowUs + 1), g, {}, {agreed(chests(), 1)});
    CHECK(arb.take_notes().empty());
    CHECK(g.count() == 2);
  }
  SUBCASE("a load sets the flag: no win") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    arb.frame(in_game(2, t), g, {}, {});
    arb.frame({2, false, t += 16'000}, g, {}, {});
    g.set_bool(chests(), 1);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
    CHECK(arb.take_notes().empty());
  }
  SUBCASE("off") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb(false);
    arb.frame(in_game(2, 1'000'000), g, {}, {});
    g.set_bool(chests(), 1);
    arb.frame(in_game(2, 1'016'000), g, {}, {});
    CHECK(arb.take_notes().empty());
  }
}

TEST_CASE("prizes: the give-back fallbacks") {
  auto lost = [](FakeStore& g, prize::PrizeArbiter& arb, std::uint64_t& t) {
    arb.frame(in_game(2, t), g, {}, {});
    g.set_bool(chests(), 1);
    arb.frame(in_game(2, t += 16'000), g, {}, {});
  };
  SUBCASE("the piece comes late: given back then") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    lost(g, arb, t);
    arb.frame(in_game(2, t += 100'000), g, {}, {agreed(chests(), 1)});
    CHECK(g.count() == 1);  // no piece yet
    g.set_int(pieces(), 2);
    arb.frame(in_game(2, t += 2'000'000), g, {}, {});
    CHECK(g.count() == 1);
    CHECK(has(arb.take_notes(), "given back (Obj_Heart_Piece 2 -> 1)"));
  }
  SUBCASE("no piece at all") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    lost(g, arb, t);
    arb.frame(in_game(2, t += 100'000), g, {}, {agreed(chests(), 1)});
    arb.frame(in_game(2, t += prize::PrizeArbiter::kPieceUs), g, {}, {});
    CHECK(has(arb.take_notes(), "no heart piece came in our game in 15 s"));
    CHECK(g.count() == 1);
  }
  SUBCASE("four pieces made a heart") {
    FakeStore g = game(3);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    lost(g, arb, t);
    g.set_int(pieces(), 0);
    arb.frame(in_game(2, t += 100'000), g, {}, {agreed(chests(), 1)});
    CHECK(has(arb.take_notes(), "went down since our win (3 -> 0"));
    CHECK(g.count() == 0);
  }
  SUBCASE("the write fails") {
    FakeStore g = game(1);
    prize::PrizeArbiter arb;
    std::uint64_t t = 1'000'000;
    lost(g, arb, t);
    g.set_int(pieces(), 2);
    g.refuse_writes = true;
    arb.frame(in_game(2, t += 100'000), g, {}, {agreed(chests(), 1)});
    CHECK(has(arb.take_notes(), "could not be given back"));
  }
}

TEST_CASE("prizes: the watcher reports another game's 1 over ours as agreed") {
  SharedFlags shared({chests()});
  FlagWatcher w(shared, FlagRole::player);
  w.record_agreed(true);
  FakeStore g = game(1);
  (void)w.frame(g);
  g.set_bool(chests(), 1);  // our own win, sent
  (void)w.frame(g);
  w.receive(proto::Message{proto::FlagState{1, {proto::FlagEntry{chests(), proto::FlagKind::boolean, 1}}}});
  (void)w.frame(g);
  CHECK(w.take_received().empty());  // nothing changed here
  const auto a = w.take_agreed();
  REQUIRE(a.size() == 1);
  CHECK(a[0].key == chests());
  CHECK(a[0].from == 1);
  // Off: nothing kept.
  FlagWatcher off(shared, FlagRole::player);
  FakeStore g2 = game(1);
  (void)off.frame(g2);
  g2.set_bool(chests(), 1);
  (void)off.frame(g2);
  off.receive(proto::Message{proto::FlagState{1, {proto::FlagEntry{chests(), proto::FlagKind::boolean, 1}}}});
  (void)off.frame(g2);
  CHECK(off.take_agreed().empty());
}
