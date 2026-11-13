// The group's heart capacity (coop/host/hearts.hpp; daurosk issue 88, oot-64 "comparte
// capacidad"): one way up, into our Link's Life component, over fake memory.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/hearts.hpp"
#include "coop/protocol/codec.hpp"
#include "fake_memory.hpp"

using namespace coop::host;
using coop::host::test::FakeMemory;
namespace proto = coop::protocol;

namespace {

class Writable final : public WritableGameMemory {
 public:
  std::uint64_t main_base() const override { return mem.main_base(); }
  bool read(std::uint64_t a, std::span<std::uint8_t> out) override { return mem.read(a, out); }
  bool write(std::uint64_t a, std::span<const std::uint8_t> data) override {
    std::vector<std::uint8_t> probe(data.size());
    if (!mem.read(a, probe)) return false;
    mem.put(a, data.data(), data.size());
    ++writes;
    return true;
  }
  std::optional<std::uint32_t> compare_exchange_u32(std::uint64_t, std::uint32_t, std::uint32_t) override {
    return std::nullopt;
  }
  FakeMemory mem;
  int writes = 0;
};

// Link's components, written out again here (docs/objects.md; downed_test.cpp has the same).
constexpr std::uint64_t kActor = 0x90000000, kTable = 0x90010000, kLife = 0x90020000, kLifeWord = 0x90030000,
                        kMaxWord = 0x90040000, kDeath = 0x90050000;

void put_link(FakeMemory& m, std::uint32_t life, std::uint32_t max) {
  m.put_u64(kActor + 0x270, kTable);
  m.put_u64(kTable + 0x2b8, kLife);
  m.put_u64(kTable + 0x1c0, kDeath);
  m.put_u64(kLife + 0x1c10, kLifeWord);
  m.put_u64(kLife + 0x1c18, kMaxWord);
  m.put_u32(kLifeWord + 8, life);
  m.put_u32(kMaxWord + 8, max);
  m.put_u32(kDeath + 0x20, 0x43a0);  // alive
}

// One frame of the hook's glue: read Link, decide, write back.
HeartsAction frame(SharedHearts& hearts, Writable& game, std::optional<std::int32_t> flag,
                   std::vector<HeartsInput::Got> received = {}, bool relearn = false) {
  HeartsInput in;
  const Vitals v = read_vitals(game.mem, kActor);
  in.link_max = v.max_life;
  in.link_life = v.life;
  in.flag = flag;
  in.received = std::move(received);
  in.relearn = relearn;
  auto act = hearts.update(in);
  const auto words = find_life_words(game.mem, kActor);
  REQUIRE(words);
  CHECK(apply_hearts(game, *words, act));
  return act;
}

std::int32_t max_of(Writable& game) { return *read_vitals(game.mem, kActor).max_life; }
std::int32_t life_of(Writable& game) { return *read_vitals(game.mem, kActor).life; }

}  // namespace

TEST_CASE("hearts: the MaxLife key is the shared flag's") {
  CHECK(kMaxLifeFlag == proto::flag_hash("PlayerStatus.MaxLife"));
}

TEST_CASE("hearts: the words are found through Link's components") {
  Writable game;
  put_link(game.mem, 10, 12);
  const auto words = find_life_words(game.mem, kActor);
  REQUIRE(words);
  CHECK(words->life == kLifeWord + 8);
  CHECK(words->max == kMaxWord + 8);
  CHECK_FALSE(find_life_words(game.mem, 0x1234));
}

TEST_CASE("hearts: a container another player took raises our max, life kept") {
  Writable game;
  put_link(game.mem, 10, 12);
  SharedHearts hearts;
  auto a = frame(hearts, game, 12);
  CHECK_FALSE(a.max);
  CHECK(hearts.capacity() == 12);
  // P2's container: the flag write from P2 (FlagWatcher::Received) says 16.
  a = frame(hearts, game, 16, {{2, 16}});
  CHECK(a.max == 16);
  CHECK_FALSE(a.life);
  REQUIRE(a.log.size() == 2);
  CHECK(a.log[0] == "hearts: max life 12 -> 16 (from P2)");
  CHECK(a.log[1] == "hearts: our Link's max 12 -> 16 (life 10 kept)");
  CHECK(max_of(game) == 16);
  CHECK(life_of(game) == 10);
  // Done: nothing more to write.
  game.writes = 0;
  a = frame(hearts, game, 16);
  CHECK(a.log.empty());
  CHECK(game.writes == 0);
}

TEST_CASE("hearts: the game's lower copy over the flag does not lose the container (issue 88)") {
  Writable game;
  put_link(game.mem, 12, 12);
  SharedHearts hearts;
  frame(hearts, game, 12);
  // P3's 16 arrives, but before our Link is raised the game writes its 12 back
  // over the flag: the received value is kept all the same.
  auto a = frame(hearts, game, 12, {{3, 16}});
  CHECK(a.max == 16);
  CHECK(a.log[0] == "hearts: max life 12 -> 16 (from P3)");
  // Our Link loses it again (a scene load rebuilds it from an old copy): put back.
  put_link(game.mem, 12, 12);
  a = frame(hearts, game, 12);
  CHECK(a.max == 16);
  CHECK(max_of(game) == 16);
}

TEST_CASE("hearts: our own container (or fourth piece) becomes the group's") {
  Writable game;
  put_link(game.mem, 12, 12);
  SharedHearts hearts;
  frame(hearts, game, 12);
  // The pickup raised our Link's max; the game's copy reaches the flag too.
  put_link(game.mem, 16, 16);
  auto a = frame(hearts, game, 16);
  CHECK_FALSE(a.max);
  REQUIRE(a.log.size() == 1);
  CHECK(a.log[0] == "hearts: max life 12 -> 16 (ours)");
  CHECK(hearts.capacity() == 16);
}

TEST_CASE("hearts: one way up: a lower value never lowers anyone") {
  Writable game;
  put_link(game.mem, 20, 20);
  SharedHearts hearts;
  frame(hearts, game, 20);
  // A mate behind us (a late joiner's 12) and a flag written lower.
  auto a = frame(hearts, game, 12, {{2, 12}});
  CHECK_FALSE(a.max);
  CHECK_FALSE(a.life);
  CHECK(a.log.empty());
  CHECK(hearts.capacity() == 20);
  CHECK(max_of(game) == 20);
  CHECK(life_of(game) == 20);
}

TEST_CASE("hearts: a cheat on our own flag raises our Link too") {
  Writable game;
  put_link(game.mem, 12, 12);
  SharedHearts hearts;
  frame(hearts, game, 12);
  auto a = frame(hearts, game, 16);  // the test give: MaxLife + 4 in our game
  CHECK(a.max == 16);
  CHECK(a.log[0] == "hearts: max life 12 -> 16 (the MaxLife flag)");
}

TEST_CASE("hearts: a life above the max is brought down to it, never raised") {
  Writable game;
  put_link(game.mem, 4, 12);
  SharedHearts hearts;
  HeartsInput in;
  in.link_max = 12;
  in.link_life = 14;  // read raw: above the max
  in.flag = 12;
  auto a = hearts.update(in);
  CHECK_FALSE(a.max);
  CHECK(a.life == 12);
  CHECK(a.log.back() == "hearts: our Link's life 14 brought down to the max 12");
  // A raise leaves the life where it is.
  in.link_life = 4;
  in.received = {{2, 20}};
  a = hearts.update(in);
  CHECK(a.max == 20);
  CHECK_FALSE(a.life);
}

TEST_CASE("hearts: values out of the game's range are not taken") {
  SharedHearts hearts;
  HeartsInput in;
  in.link_max = 12;
  in.flag = 12;
  hearts.update(in);
  in.received = {{2, 0}, {2, -4}, {3, kMostHeartQuarters + 4}};
  in.flag = 999;
  const auto a = hearts.update(in);
  CHECK_FALSE(a.max);
  CHECK(hearts.capacity() == 12);
  in.received = {{4, kMostHeartQuarters}};
  CHECK(hearts.update(in).max == kMostHeartQuarters);
}

TEST_CASE("hearts: a save load or a new session learns the capacity again") {
  Writable game;
  put_link(game.mem, 12, 12);
  SharedHearts hearts;
  frame(hearts, game, 12);
  frame(hearts, game, 12, {{2, 20}});
  CHECK(max_of(game) == 20);
  // Another save of ours with 12: the old 20 raises nobody now.
  put_link(game.mem, 12, 12);
  auto a = frame(hearts, game, 12, {}, true);
  CHECK_FALSE(a.max);
  REQUIRE(a.log.size() == 2);
  CHECK(a.log[0] == "hearts: a save load or a new session: the group's max life is learnt again (was 20)");
  CHECK(a.log[1] == "hearts: max life 12 (ours, first reading)");
  CHECK(max_of(game) == 12);
}

TEST_CASE("hearts: no Link (a load): learnt, nothing written") {
  SharedHearts hearts;
  HeartsInput in;
  in.flag = 12;
  in.received = {{2, 16}};
  const auto a = hearts.update(in);
  CHECK_FALSE(a.max);
  CHECK_FALSE(a.life);
  CHECK(hearts.capacity() == 16);
}

TEST_CASE("hearts: only MaxLife values are picked from a frame's received writes") {
  using K = proto::FlagKind;
  std::vector<FlagWatcher::Received> r;
  const proto::FlagKey max_key{kMaxLifeFlag, 0};
  const proto::FlagKey life_key{proto::flag_hash("PlayerStatus.Life"), 0};
  r.push_back({max_key, {max_key, K::int32, 12}, {max_key, K::int32, 16}, 2});
  r.push_back({life_key, {life_key, K::int32, 8}, {life_key, K::int32, 12}, 2});
  r.push_back({proto::FlagKey{kMaxLifeFlag, 1}, {}, {proto::FlagKey{kMaxLifeFlag, 1}, K::int32, 40}, 3});
  r.push_back({max_key, {max_key, K::int32, 16}, {max_key, K::int32, 20}, 4, FlagWatcher::ReceivedKind::join});
  const auto got = max_life_received(r);
  REQUIRE(got.size() == 2);
  CHECK(got[0].from == 2);
  CHECK(got[0].value == 16);
  CHECK(got[1].from == 4);
  CHECK(got[1].value == 20);
}
