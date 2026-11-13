// Tests for the escape's countdown found in a running game (coop/host/countdown_search.hpp,
// oot-remake docs/FINAL.md, docs/RIESGOS_RESUELTOS.md "Mundo e historia"): a fake GameUI module
// and an object it points to, with words that count down right, wrong or not at all.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "coop/host/countdown_search.hpp"
#include "fake_memory.hpp"

using namespace coop::host;
using coop::host::test::FakeMemory;

namespace {

constexpr std::uint64_t kUiPtr = 0x4e7cfa8;  // addresses.hpp kGameUiModulePtr
constexpr std::uint64_t kUi = 0x80000000;
constexpr std::uint64_t kChild = 0x80100000;
constexpr std::uint64_t kSecond = 1'000'000;

CountdownSearchSettings small() {
  CountdownSearchSettings s;
  s.roots = {CountdownRoot{"GameUI", kUiPtr, 0x40, 0x20}};
  return s;
}

// GameUI's 0x40 bytes (zeros), the pointer to it, and a child object at +0x20.
FakeMemory game() {
  FakeMemory m;
  m.put_u64(FakeMemory::kBase + kUiPtr, kUi);
  for (std::uint64_t a = 0; a < 0x40; a += 8) m.put_u64(kUi + a, 0);
  m.put_u64(kUi + 0x20, kChild);
  for (std::uint64_t a = 0; a < 0x20; a += 8) m.put_u64(kChild + a, 0);
  return m;
}

bool has(const std::vector<std::string>& lines, const std::string& part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

// Runs `seconds` + 1 steps a second apart; `set` writes the words for second t first.
std::vector<std::string> run(CountdownSearch& s, FakeMemory& m, int seconds,
                             const std::function<void(int)>& set, std::uint64_t from_us = 0) {
  std::vector<std::string> all;
  for (int t = 0; t <= seconds; ++t) {
    set(t);
    for (auto& l : s.step(m, from_us + static_cast<std::uint64_t>(t) * kSecond)) all.push_back(l);
  }
  return all;
}

}  // namespace

TEST_CASE("countdown search: a float in GameUI that drops 1 s a second is found") {
  FakeMemory m = game();
  CountdownSearch s(small());
  const auto lines = run(s, m, 5, [&](int t) {
    m.put_f32(kUi + 0x10, 172.5f - static_cast<float>(t));
    m.put_u32(kUi + 0x14, 150);  // a constant that reads as seconds: never taken
  });
  REQUIRE(s.state() == CountdownSearch::State::found);
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x10:f32");
  CHECK(s.found()->address == kUi + 0x10);
  CHECK(has(lines, "escape clock: looking for our countdown (try 1 of 3)"));
  CHECK(has(lines, "escape clock: our countdown found at main+0x4e7cfa8,0x10:f32 (reads 167.5 s; 1 word"));
  // Found stays found: nothing more is read or said.
  const auto reads = m.reads;
  CHECK(s.step(m, 9 * kSecond).empty());
  s.reset();
  CHECK(s.state() == CountdownSearch::State::found);
  CHECK(m.reads == reads);
}

TEST_CASE("countdown search: milliseconds in an object GameUI points to: two pointer steps") {
  FakeMemory m = game();
  CountdownSearch s(small());
  run(s, m, 5, [&](int t) { m.put_u32(kChild + 0x0c, 120'000u - static_cast<std::uint32_t>(t) * 1000u); });
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x20,0xc:ms");
  CHECK(s.found()->seconds == doctest::Approx(115.0));
}

TEST_CASE("countdown search: whole seconds read down still count, an emulator a little slow too") {
  FakeMemory m = game();
  CountdownSearch s(small());
  // 0.85 s of game a wall second, read down to whole seconds.
  run(s, m, 5, [&](int t) { m.put_u32(kUi + 0x08, static_cast<std::uint32_t>(170.9 - 0.85 * t)); });
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x8:u32");
}

TEST_CASE("countdown search: decoys that go up, drop too fast or stand still are left out") {
  FakeMemory m = game();
  CountdownSearch s(small());
  run(s, m, 5, [&](int t) {
    m.put_f32(kUi + 0x00, 10.0f + static_cast<float>(t));         // counts up
    m.put_f32(kUi + 0x04, 100.0f - 3.0f * static_cast<float>(t));  // 3 s a second
    m.put_f32(kUi + 0x08, 42.0f);                                  // still
    m.put_u32(kUi + 0x30, 90u - static_cast<std::uint32_t>(t));    // the real one, whole seconds
  });
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x30:u32");
}

TEST_CASE("countdown search: two that drop right: the float is taken, the other said") {
  FakeMemory m = game();
  CountdownSearch s(small());
  const auto lines = run(s, m, 5, [&](int t) {
    m.put_u32(kUi + 0x08, 90u - static_cast<std::uint32_t>(t));
    m.put_f32(kChild + 0x04, 90.5f - static_cast<float>(t));
  });
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x20,0x4:f32");
  CHECK(has(lines, "2 words dropped 1 s a second"));
  CHECK(has(lines, "also main+0x4e7cfa8,0x8:u32"));
}

TEST_CASE("countdown search: nothing counts down: three tries, then the group's clock alone") {
  FakeMemory m = game();
  CountdownSearch s(small());
  m.put_f32(kUi + 0x10, 60.0f);  // still
  std::vector<std::string> lines;
  for (int t = 0; t <= 12; ++t) {
    for (auto& l : s.step(m, static_cast<std::uint64_t>(t) * kSecond)) lines.push_back(l);
  }
  CHECK(s.state() == CountdownSearch::State::failed);
  CHECK(!s.found());
  CHECK(s.tries_used() == 3);
  CHECK(has(lines, "try 1 of 3): looking again"));
  CHECK(has(lines, "try 2 of 3): looking again"));
  CHECK(has(lines, "escape clock: our countdown was not found (no word dropped 1 s a second"));
  CHECK(has(lines, "the group's clock goes on without the game's timer, nothing is set in our game"));
  CHECK(s.step(m, 20 * kSecond).empty());  // failed stays failed in this scene
  s.reset();                                // a new visit looks again
  CHECK(s.state() == CountdownSearch::State::idle);
}

TEST_CASE("countdown search: no word reads as seconds left at all is a try too") {
  FakeMemory m = game();
  m.put_u64(kUi + 0x20, 0);  // no child
  CountdownSearch s(small());
  const auto lines = s.step(m, 0);
  CHECK(has(lines, "0 words of GameUI"));
  CHECK(has(lines, "no countdown yet (no word reads as seconds left, try 1 of 3)"));
}

TEST_CASE("countdown search: an event stops the clock: the samples start again, no try lost") {
  FakeMemory m = game();
  CountdownSearch s(small());
  float v = 150.0f;
  m.put_f32(kUi + 0x10, v);
  (void)s.step(m, 0);
  m.put_f32(kUi + 0x10, v - 1.0f);
  (void)s.step(m, kSecond);
  s.interrupt();  // a floor cutscene: the timer stands
  v = 149.0f;
  const auto lines = run(s, m, 5, [&](int t) { m.put_f32(kUi + 0x10, v - static_cast<float>(t)); }, 30 * kSecond);
  REQUIRE(s.found());
  CHECK(s.tries_used() == 0);
  CHECK(has(lines, "(try 1 of 3)"));
}

TEST_CASE("countdown search: the GameUI pointer not readable yet waits, says nothing") {
  FakeMemory m;
  CountdownSearch s(small());
  CHECK(s.step(m, 0).empty());
  CHECK(s.state() == CountdownSearch::State::idle);
  CHECK(s.tries_used() == 0);
}

TEST_CASE("countdown search: an object made again elsewhere is not where its pointer leads") {
  FakeMemory m = game();
  CountdownSearch s(small());
  const auto lines = run(s, m, 5, [&](int t) {
    m.put_u32(kChild + 0x0c, 100'000u - static_cast<std::uint32_t>(t) * 1000u);
    if (t == 5) m.put_u64(kUi + 0x20, 0x80200000);  // GameUI points to another object now
  });
  CHECK(!s.found());
  CHECK(has(lines, "no longer where their pointers lead"));
}

TEST_CASE("countdown search: places to look and units as text") {
  std::string why;
  const auto roots = parse_countdown_roots("main+0x4e7cfa8:0x1000:0x200, main+0x4e94840", &why);
  REQUIRE(roots);
  REQUIRE(roots->size() == 2);
  CHECK((*roots)[0].pointer == 0x4e7cfa8);
  CHECK((*roots)[0].bytes == 0x1000);
  CHECK((*roots)[0].child_bytes == 0x200);
  CHECK((*roots)[1].pointer == 0x4e94840);
  CHECK((*roots)[1].bytes == 0x2000);
  CHECK((*roots)[1].name == "main+0x4e94840");
  CHECK(!parse_countdown_roots("", &why));
  CHECK(!parse_countdown_roots("0x4e7cfa8", &why));
  CHECK(!parse_countdown_roots("main+4e7cfa8", &why));
  CHECK(!parse_countdown_roots("main+0x10:2", &why));
  CHECK(!parse_countdown_roots("main+0x10:0x100:0x8000", &why));
  CHECK(!parse_countdown_roots("main+0x10:1:2:3", &why));
  CHECK(std::string(countdown_unit_text(CountdownUnit::u32_ms)) == "ms");
  CHECK(default_countdown_roots().front().pointer == kUiPtr);
  CountdownFound f;
  f.base = 0x10;
  f.offsets = {0x8, 0x1c};
  f.unit = CountdownUnit::u32_seconds;
  CHECK(f.text() == "main+0x10,0x8,0x1c:u32");
}

// The game keeps its minigame timer as minutes and seconds (MiniGame_TimerMinute/Second): the pair is
// the countdown, not its seconds word, which jumps from 0 to 59 each minute (docs/SONDAS.md row 1).
TEST_CASE("countdown search: minutes and seconds are found as one countdown, across a minute") {
  FakeMemory m = game();
  CountdownSearch s(small());
  const auto lines = run(s, m, 5, [&](int t) {
    const int left = 122 - t;  // 2:02 down to 1:57
    m.put_u32(kChild + 0x10, static_cast<std::uint32_t>(left / 60));
    m.put_u32(kChild + 0x14, static_cast<std::uint32_t>(left % 60));
  });
  REQUIRE(s.state() == CountdownSearch::State::found);
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x20,0x10:mmss");
  CHECK(s.found()->seconds == 117.0);
  CHECK(has(lines, "our countdown found at main+0x4e7cfa8,0x20,0x10:mmss (reads 117.0 s"));
  CHECK(std::string(countdown_unit_text(CountdownUnit::u32_mmss)) == "mmss");
}

TEST_CASE("countdown search: with no minute to cross, the pair still goes before its seconds word") {
  FakeMemory m = game();
  CountdownSearch s(small());
  run(s, m, 5, [&](int t) {
    m.put_u32(kChild + 0x10, 1);                                    // 1:50 down to 1:45
    m.put_u32(kChild + 0x14, static_cast<std::uint32_t>(50 - t));
  });
  REQUIRE(s.found());
  CHECK(s.found()->text() == "main+0x4e7cfa8,0x20,0x10:mmss");
}
