// Tests for the held floor switch glue (lichii/hook/coop_held_switch.cpp):
// the switch, the others as presence shows them, and the probe that finds the
// words of a switch actor our own Link presses (docs/SOSTENIDOS.md).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop_held_switch.h"

namespace {

using coop::host::HeldInput;
using coop::host::HeldSwitchList;
using coop::host::HeldSwitchRule;
namespace protocol = coop::protocol;

constexpr std::uint64_t kSwitch = 7865893853239028816ull;  // FireTemple room 10
constexpr protocol::Vec3 kOn{-51.465f, 147.0f, -18.928f};
constexpr protocol::Vec3 kOff{-49.0f, 147.0f, -18.928f};
constexpr std::uint64_t kActor = 0x80000000;
constexpr std::uint64_t kTable = 0x81000000;
constexpr std::uint64_t kComponent = 0x82000000;

struct Memory {
  std::map<std::uint64_t, std::uint64_t> words;
  Memory() {
    for (std::uint64_t i = 0; i < 0x600; i += 8) words[kActor + i] = 0;
    for (std::uint64_t i = 0; i < 0x300; i += 8) words[kTable + i] = 0;
    for (std::uint64_t i = 0; i < 0x200; i += 8) words[kComponent + i] = 0;
    words[kActor + 0x270] = kTable;
    words[kTable] = kComponent;
  }
  CoopHeldSwitch::StepProbe::ReadWord reader() {
    return [this](std::uint64_t a) -> std::optional<std::uint64_t> {
      const auto it = words.find(a);
      if (it == words.end()) return std::nullopt;
      return it->second;
    };
  }
};

HeldInput at(std::optional<protocol::Vec3> link, std::uint64_t now_us) {
  HeldInput in;
  in.scene = "FireTemple";
  in.link = link;
  in.now_us = now_us;
  return in;
}

bool has_line(const std::vector<std::string>& lines, const std::string& part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("held switch glue: the switch's values") {
  // docs/RIESGOS_RESUELTOS.md: the flag path presses by default; "log" only watches.
  CHECK(CoopHeldSwitch::ParseMode("") == CoopHeldSwitch::Mode::flag);
  CHECK(CoopHeldSwitch::ParseMode("flag") == CoopHeldSwitch::Mode::flag);
  CHECK(CoopHeldSwitch::ParseMode("Flag ") == CoopHeldSwitch::Mode::flag);
  CHECK(CoopHeldSwitch::KnownMode("flag"));
  CHECK_FALSE(CoopHeldSwitch::KnownMode("sometimes"));
  CHECK(CoopHeldSwitch::ParseMode("sometimes") == CoopHeldSwitch::Mode::flag);
  CHECK(CoopHeldSwitch::ParseMode("log") == CoopHeldSwitch::Mode::log);
  CHECK(CoopHeldSwitch::ParseMode("0") == CoopHeldSwitch::Mode::off);
  CHECK(CoopHeldSwitch::ParseMode("1") == CoopHeldSwitch::Mode::press);
  CHECK(CoopHeldSwitch::ParseMode("press") == CoopHeldSwitch::Mode::press);
}

TEST_CASE("held switch glue: the others as presence shows them, a fading one does not hold") {
  coop::overlay::Shown a;
  a.number = 2;
  a.scene = "FireTemple";
  a.position = kOn;
  a.fade = 1.0f;
  coop::overlay::Shown b = a;
  b.number = 3;
  b.fade = 0.5f;
  coop::overlay::Shown c = a;
  c.number = 4;
  c.fade = 0.0f;
  const auto others = CoopHeldSwitch::OthersOf({a, b, c});
  REQUIRE(others.size() == 3);
  CHECK(others[0].live);
  CHECK(others[1].live);  // still drawn: its last samples hold
  CHECK_FALSE(others[2].live);
  CHECK(others[0].scene == std::optional<std::string>("FireTemple"));
}

TEST_CASE("held switch glue: the probe logs the words our Link's step changes, as word lines") {
  const HeldSwitchList& list = HeldSwitchList::builtin();
  HeldSwitchRule rule(list);
  CoopHeldSwitch::StepProbe probe;
  Memory mem;
  const auto find = [](std::uint64_t hash) -> std::optional<std::uint64_t> {
    return hash == kSwitch ? std::optional<std::uint64_t>(kActor) : std::nullopt;
  };
  std::vector<std::string> lines;
  auto look = [&](std::optional<protocol::Vec3> link, std::uint64_t now_us) {
    const auto frame = rule.frame(at(link, now_us));
    for (auto& l : probe.Look(frame, list, find, mem.reader(), now_us)) lines.push_back(std::move(l));
  };
  look(kOff, 0);  // the first quiet copy
  mem.words[kActor + 0x10] = 1;  // a counter
  look(kOff, 500'000);           // too soon for another copy
  look(kOff, 1'000'000);         // a second copy: the counter is noisy
  CHECK(lines.empty());

  look(kOn, 1'100'000);  // our Link steps on
  mem.words[kComponent + 0x40] = 1;
  mem.words[kActor + 0x10] = 2;
  look(kOn, 1'300'000);
  CHECK(lines.empty());  // reported 0.5 s after the step
  look(kOn, 1'600'000);
  CHECK(has_line(lines, "FireTemple room10 (6d2944c8c33dac50) our Link stepped on: 1 words changed"));
  CHECK(has_line(lines, "word Obj_Stepswitch_A_03 c+0x0+0x40 on=0x1 off=0x0"));
  CHECK_FALSE(has_line(lines, "actor+0x10 "));  // the counter

  lines.clear();
  look(kOff, 2'000'000);  // off: far, at once
  mem.words[kComponent + 0x40] = 0;
  look(kOff, 2'500'000);
  CHECK(has_line(lines, "our Link stepped off: 1 words changed"));
  CHECK(has_line(lines, "word Obj_Stepswitch_A_03 c+0x0+0x40 on=0x1 off=0x0"));

  // Not loaded: nothing read, nothing logged.
  lines.clear();
  probe.Reset();
  const auto none = [](std::uint64_t) -> std::optional<std::uint64_t> { return std::nullopt; };
  const auto frame = rule.frame(at(kOn, 3'000'000));
  CHECK(probe.Look(frame, list, none, mem.reader(), 3'000'000).empty());
}
