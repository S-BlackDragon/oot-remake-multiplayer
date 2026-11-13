// ORM: coop/host/flag_probe.hpp (docs/RIESGOS_RESUELTOS.md "Mundo e historia", SOSTENIDOS.md 3 and 4).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/flag_probe.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

class Store : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;
  void set(FlagKey key, std::uint32_t v) { values[key] = {key, proto::FlagKind::boolean, v}; }
  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    values[key_of(e)] = e;
    return true;
  }
};

bool noted(const std::vector<std::string>& notes, const std::string& part) {
  return std::any_of(notes.begin(), notes.end(), [&](const std::string& n) { return n.find(part) != n.npos; });
}

FlagKey key(std::string_view name) { return FlagKey(flag_hash(name), std::uint16_t{0}); }

FlagWatcher::Received from(std::uint8_t p, FlagKey k, std::uint32_t before, std::uint32_t after) {
  FlagWatcher::Received r;
  r.key = k;
  r.before = {k, proto::FlagKind::boolean, before};
  r.after = {k, proto::FlagKind::boolean, after};
  r.from = p;
  return r;
}

}  // namespace

TEST_CASE("flag probe: the defaults, extra names, no repeats") {
  const auto flags = FlagProbe::flags_with(" Foo_Bar , ,WaterTemple_Room14_SwitchPushed");
  REQUIRE(flags.size() == 5);
  CHECK(flags[0].name == "WaterTemple_Room14_SwitchPushed");
  CHECK(flags[3].name == "InsidejaubjabuBelly_gimic_switchOn_Stone");
  CHECK(flags[4].name == "Foo_Bar");
  CHECK(flags[4].key == key("Foo_Bar"));
}

TEST_CASE("flag probe: each change says who made it, a flag going back and forth is named once") {
  const FlagKey pushed = key("WaterTemple_Room14_SwitchPushed");
  const FlagKey stone = key("InsidejaubjabuBelly_gimic_switchOn_Stone");
  FlagProbe probe(FlagProbe::flags_with(""));
  CHECK(noted(probe.take_notes(), "flag probe: on: every change of WaterTemple_Room14_SwitchPushed"));
  Store s;
  s.set(pushed, 0);
  s.set(stone, 0);
  probe.frame(s, {}, 0);
  CHECK(probe.take_notes().empty());  // the baseline
  s.set(stone, 1);  // Jabu's stone put on its switch here
  probe.frame(s, {}, 100'000);
  CHECK(noted(probe.take_notes(), "flag probe: InsidejaubjabuBelly_gimic_switchOn_Stone 0 -> 1 in our game"));

  s.set(pushed, 1);  // our Link on the switch
  probe.frame(s, {}, 1'000'000);
  s.set(pushed, 0);  // P2's game wrote its 0 here
  probe.frame(s, {from(2, pushed, 1, 0)}, 1'500'000);
  auto notes = probe.take_notes();
  CHECK(noted(notes, "WaterTemple_Room14_SwitchPushed 0 -> 1 in our game"));
  CHECK(noted(notes, "WaterTemple_Room14_SwitchPushed 1 -> 0 from P2"));
  CHECK_FALSE(noted(notes, "bounces"));
  s.set(pushed, 1);  // our room puts it back to 1
  probe.frame(s, {}, 2'000'000);
  notes = probe.take_notes();
  CHECK(noted(notes, "WaterTemple_Room14_SwitchPushed bounces between games (3 changes in 1.0 s, ours and the others')"));
  s.set(pushed, 0);
  probe.frame(s, {from(2, pushed, 1, 0)}, 2'500'000);
  CHECK_FALSE(noted(probe.take_notes(), "bounces"));  // once
}

TEST_CASE("flag probe: changes far apart, or all ours, are no bounce") {
  const FlagKey pushed = key("WaterTemple_Room14_SwitchPushed");
  FlagProbe probe(FlagProbe::flags_with(""));
  probe.take_notes();
  Store s;
  s.set(pushed, 0);
  probe.frame(s, {}, 0);
  for (int i = 1; i <= 4; ++i) {  // ours only
    s.set(pushed, static_cast<std::uint32_t>(i % 2));
    probe.frame(s, {}, static_cast<std::uint64_t>(i) * 1'000'000);
  }
  CHECK_FALSE(noted(probe.take_notes(), "bounces"));
  FlagProbe slow(FlagProbe::flags_with(""));
  slow.take_notes();
  s.set(pushed, 0);
  slow.frame(s, {}, 0);
  s.set(pushed, 1);
  slow.frame(s, {}, 1'000'000);
  s.set(pushed, 0);
  slow.frame(s, {from(3, pushed, 1, 0)}, 7'000'000);
  s.set(pushed, 1);
  slow.frame(s, {}, 13'000'000);
  CHECK_FALSE(noted(slow.take_notes(), "bounces"));
}
