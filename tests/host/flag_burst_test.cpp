// ORM: the flag changes of one burst go out together (coop/host/flag_burst.cpp,
// oot-remake docs/ANCHO_DE_BANDA.md).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <vector>

#include "coop/host/flag_burst.hpp"

using coop::host::FlagBurst;
namespace proto = coop::protocol;

namespace {

std::vector<proto::FlagEntry> one(std::uint32_t hash, std::uint32_t value = 1) {
  return {proto::FlagEntry(hash, 0, proto::FlagKind::boolean, value)};
}

}  // namespace

TEST_CASE("flag burst: no window sends every frame's changes at once") {
  FlagBurst burst;
  CHECK_FALSE(burst.push({}, 0).has_value());
  for (std::uint64_t t = 0; t < 5; ++t) {
    const auto go = burst.push(one(1 + static_cast<std::uint32_t>(t)), t * 1000);
    REQUIRE(go.has_value());
    CHECK(go->size() == 1);
  }
  CHECK(burst.sent() == 5);
  CHECK(burst.held_frames() == 0);
}

TEST_CASE("flag burst: the first change goes at once, the rest of the window together") {
  FlagBurst burst(100'000);
  auto go = burst.push(one(1), 1'000'000);
  REQUIRE(go.has_value());  // a lone chest waits for nothing
  CHECK(go->size() == 1);
  CHECK_FALSE(burst.push(one(2), 1'016'000).has_value());
  CHECK_FALSE(burst.push(one(3), 1'033'000).has_value());
  CHECK_FALSE(burst.push({}, 1'050'000).has_value());  // a quiet frame: still held
  CHECK(burst.holding());
  go = burst.push({}, 1'100'000);  // the window ends
  REQUIRE(go.has_value());
  CHECK(*go == std::vector<proto::FlagEntry>{one(2)[0], one(3)[0]});  // in order
  CHECK_FALSE(burst.holding());
  CHECK(burst.sent() == 2);
  CHECK(burst.held_frames() == 2);
  // A change long after: at once again.
  go = burst.push(one(4), 3'000'000);
  REQUIRE(go.has_value());
  CHECK(go->size() == 1);
}

TEST_CASE("flag burst: one key twice keeps both entries for the client to merge") {
  // Client::set_flags keeps the last value of a key and adds count deltas,
  // so the burst hands over everything in order and merges nothing itself.
  FlagBurst burst(100'000);
  REQUIRE(burst.push(one(9), 0).has_value());
  CHECK_FALSE(burst.push(one(1, 1), 10'000).has_value());
  const proto::FlagEntry delta(proto::FlagKey(7, 0), proto::FlagKind::count_delta, 1);
  CHECK_FALSE(burst.push(std::vector<proto::FlagEntry>{delta}, 20'000).has_value());
  CHECK_FALSE(burst.push(std::vector<proto::FlagEntry>{delta}, 30'000).has_value());
  CHECK_FALSE(burst.push(one(1, 0), 40'000).has_value());
  const auto go = burst.push({}, 100'000);
  REQUIRE(go.has_value());
  REQUIRE(go->size() == 4);
  CHECK((*go)[0].value == 1);
  CHECK((*go)[1] == delta);
  CHECK((*go)[2] == delta);
  CHECK((*go)[3].value == 0);
}

TEST_CASE("flag burst: flush hands over what is held and starts the window again") {
  FlagBurst burst(100'000);
  CHECK_FALSE(burst.flush(0).has_value());  // nothing held
  REQUIRE(burst.push(one(1), 0).has_value());
  CHECK_FALSE(burst.push(one(2), 50'000).has_value());
  const auto held = burst.flush(60'000);  // a FLAG_RESET goes next, in order after it
  REQUIRE(held.has_value());
  CHECK(held->size() == 1);
  CHECK_FALSE(burst.push(one(3), 120'000).has_value());  // within 100 ms of the flush
  CHECK(burst.push({}, 160'000).has_value());
}

TEST_CASE("flag burst: a clock that goes back does not hold for ever") {
  FlagBurst burst(100'000);
  REQUIRE(burst.push(one(1), 500'000).has_value());
  CHECK(burst.push(one(2), 400'000).has_value());  // earlier than the last: sent
}
