// ORM bean soils (coop/host/beans.hpp, oot-remake docs/COLECCIONABLES.md): the soil Bool's
// meaning checked against the platform's placement in the actor walk. Fake flags, a fake walk.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop/host/beans.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

class FakeStore : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    values[e.key] = e;
    return true;
  }
  void set(std::uint32_t hash, std::uint32_t v) {
    values[FlagKey(hash, 0)] = {FlagKey(hash, 0), proto::FlagKind::boolean, v};
  }
};

constexpr std::uint32_t kBugs = 0xb55acaddu;    // Kokiri Forest's soil with bugs
constexpr std::uint32_t kNoBugs = 0xe7bb7f2fu;  // without bugs
constexpr std::uint64_t kBugsPlatform = 0x7dafcfc1694ac1d7ull;

bean::BeanView view(std::uint64_t now_us, const std::set<std::uint64_t>* loaded, std::string scene = "KokiriForest_Adult",
                    bool walked = true) {
  bean::BeanView v;
  v.scene = std::move(scene);
  v.walked = walked;
  if (loaded) v.loaded = [loaded](std::uint64_t h) { return loaded->contains(h); };
  v.now_us = now_us;
  return v;
}

bool has(const std::vector<std::string>& lines, std::string_view part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("beans: the soils' table") {
  const auto s = bean::soils();
  REQUIRE(s.size() == 11);
  std::set<std::uint32_t> flags;
  std::set<std::uint64_t> places;
  for (const auto& soil : s) {
    CHECK(flags.insert(soil.flag).second);
    CHECK(places.insert(soil.soil).second);
    CHECK(places.insert(soil.platform).second);
    CHECK_FALSE(soil.place.empty());
    CHECK(soil.place.find('_') == std::string_view::npos);  // a place, no era
  }
  CHECK(flags.contains(kBugs));
  CHECK(flags.contains(0x562fab76u));  // the crater's (the list's first soil line)
}

TEST_CASE("beans: the four cases, after the room settles") {
  std::set<std::uint64_t> loaded{kBugsPlatform};
  FakeStore g;
  g.set(kBugs, 1);
  g.set(kNoBugs, 0);
  bean::BeanCheck check;
  check.frame(view(1'000'000, &loaded), g);
  CHECK(check.take_notes().empty());  // not settled yet
  check.frame(view(1'000'000 + bean::BeanCheck::kSettleUs, &loaded), g);
  const auto notes = check.take_notes();
  CHECK(has(notes, "Kokiri Forest's soil with bugs (Bool 0xb55acadd = 1, KokiriForest_Adult): planted: platform seen"));
  CHECK(has(notes, "without bugs (Bool 0xe7bb7f2f = 0, KokiriForest_Adult): not planted: no platform"));
  CHECK(has(notes, "confirmed") == false);  // two soils, each seen once
  CHECK(*check.seen(kBugs) == bean::Seen::planted);
  // The same again: no new line.
  check.frame(view(5'000'000, &loaded), g);
  CHECK(check.take_notes().empty());
  // The platform goes (it should not): the line says so.
  loaded.clear();
  check.frame(view(6'000'000, &loaded), g);
  CHECK(has(check.take_notes(), "platform not seen: Bool meaning unconfirmed"));
  // A platform with the Bool at 0.
  g.set(kBugs, 0);
  loaded.insert(kBugsPlatform);
  check.frame(view(7'000'000, &loaded), g);
  CHECK(has(check.take_notes(), "platform seen with the Bool at 0"));
}

TEST_CASE("beans: a bean that arrives while we are there, live or not") {
  SUBCASE("live, then confirmed") {
    std::set<std::uint64_t> loaded;
    FakeStore g;
    g.set(kBugs, 0);
    g.set(kNoBugs, 0);
    bean::BeanCheck check;
    std::uint64_t t = 1'000'000;
    check.frame(view(t, &loaded), g);
    check.frame(view(t += bean::BeanCheck::kSettleUs, &loaded), g);
    check.take_notes();
    g.set(kBugs, 1);  // another player's bean
    check.frame(view(t += 16'000, &loaded), g);
    CHECK(check.take_notes().empty());  // waiting for its platform
    loaded.insert(kBugsPlatform);
    check.frame(view(t += 500'000, &loaded), g);
    const auto notes = check.take_notes();
    CHECK(has(notes, "the bean came while we are here and its platform came live"));
    CHECK(has(notes, "planted: platform seen"));
    CHECK(has(notes, "Kokiri Forest's soil with bugs: confirmed in this run"));
    CHECK(check.confirmed(kBugs));
  }
  SUBCASE("not live") {
    std::set<std::uint64_t> loaded;
    FakeStore g;
    g.set(kBugs, 0);
    bean::BeanCheck check;
    std::uint64_t t = 1'000'000;
    check.frame(view(t, &loaded), g);
    check.frame(view(t += bean::BeanCheck::kSettleUs, &loaded), g);
    check.take_notes();
    g.set(kBugs, 1);
    check.frame(view(t += 16'000, &loaded), g);
    check.frame(view(t += bean::BeanCheck::kLiveUs, &loaded), g);
    const auto notes = check.take_notes();
    CHECK(has(notes, "no platform came in 10 s: not live, it should come on the next load"));
    CHECK(has(notes, "platform not seen: Bool meaning unconfirmed"));
    // The next visit (a load) shows it.
    check.frame(view(t += 16'000, &loaded, ""), g);
    loaded.insert(kBugsPlatform);
    check.frame(view(t += 16'000, &loaded), g);
    check.frame(view(t += bean::BeanCheck::kSettleUs, &loaded), g);
    CHECK(has(check.take_notes(), "planted: platform seen"));
  }
}

TEST_CASE("beans: no walk, other scenes, off, unreadable") {
  FakeStore g;
  g.set(kBugs, 1);
  SUBCASE("no walk: said once") {
    bean::BeanCheck check;
    check.frame(view(1'000'000, nullptr, "KokiriForest_Child", false), g);
    check.frame(view(9'000'000, nullptr, "KokiriForest_Child", false), g);
    const auto notes = check.take_notes();
    REQUIRE(notes.size() == 1);
    CHECK(has(notes, "no actor walk in KokiriForest_Child"));
  }
  SUBCASE("a scene with no soil") {
    std::set<std::uint64_t> loaded{kBugsPlatform};
    bean::BeanCheck check;
    check.frame(view(1'000'000, &loaded, "Market_Child"), g);
    check.frame(view(9'000'000, &loaded, "Market_Child"), g);
    CHECK(check.take_notes().empty());
  }
  SUBCASE("off") {
    std::set<std::uint64_t> loaded{kBugsPlatform};
    bean::BeanCheck check(false);
    check.frame(view(1'000'000, &loaded), g);
    check.frame(view(9'000'000, &loaded), g);
    CHECK(check.take_notes().empty());
  }
  SUBCASE("a Bool that cannot be read: no verdict") {
    std::set<std::uint64_t> loaded{kBugsPlatform};
    FakeStore empty;
    bean::BeanCheck check;
    check.frame(view(1'000'000, &loaded), empty);
    check.frame(view(9'000'000, &loaded), empty);
    CHECK(check.take_notes().empty());
  }
}
