// ORM: the escape's countdown found in a running game: the words of the GameUI module and its
//   objects that drop as the wall clock does. Described in coop/host/countdown_search.hpp.
// Doc: oot-remake docs/FINAL.md, docs/RIESGOS_RESUELTOS.md ("Mundo e historia").
// Switch: LICHII_ORM_ESCAPE_CLOCK_SEARCH (lichii/hook/coop_escape_timer.cpp).
// Test: tests/host/countdown_search_test.cpp.

#include "coop/host/countdown_search.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <system_error>
#include <utility>

#include "addresses.hpp"

namespace coop::host {
namespace {

std::optional<std::uint64_t> number_of(std::string_view token) {
  int base = 10;
  if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    token.remove_prefix(2);
    base = 16;
  }
  if (token.empty() || token.size() > 16) return std::nullopt;
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value, base);
  if (ec != std::errc() || end != token.data() + token.size()) return std::nullopt;
  return value;
}

std::string hex(std::uint64_t v) {
  char text[24];
  std::snprintf(text, sizeof text, "0x%llx", static_cast<unsigned long long>(v));
  return text;
}

std::string secs(double s) {
  char text[24];
  std::snprintf(text, sizeof text, "%.1f", s);
  return text;
}

int unit_rank(CountdownUnit u) {
  switch (u) {
    case CountdownUnit::f32_seconds: return 0;
    case CountdownUnit::u32_ms: return 1;
    case CountdownUnit::u32_mmss: return 2;  // before the seconds word inside it
    case CountdownUnit::u32_seconds: return 3;
  }
  return 4;
}

constexpr std::uint64_t kLowestPointer = 0x1000000;  // below this a word is no heap pointer

}  // namespace

std::vector<CountdownRoot> default_countdown_roots() {
  return {CountdownRoot{"GameUI", addr::kGameUiModulePtr, 0x2000, 0x400}};
}

std::optional<std::vector<CountdownRoot>> parse_countdown_roots(std::string_view text, std::string* error) {
  auto fail = [&](const char* why) -> std::optional<std::vector<CountdownRoot>> {
    if (error) *error = why;
    return std::nullopt;
  };
  std::vector<CountdownRoot> roots;
  while (!text.empty()) {
    const std::size_t comma = text.find(',');
    std::string_view item = text.substr(0, comma);
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
    while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
    if (item.empty()) continue;
    constexpr std::string_view kMain = "main+";
    if (!item.starts_with(kMain)) return fail("a place to look is main+0x<hex>[:<bytes>[:<child bytes>]]");
    item.remove_prefix(kMain.size());
    std::vector<std::string_view> parts;
    while (true) {
      const std::size_t colon = item.find(':');
      parts.push_back(item.substr(0, colon));
      if (colon == std::string_view::npos) break;
      item.remove_prefix(colon + 1);
    }
    if (parts.size() > 3) return fail("a place to look has at most an offset, bytes and child bytes");
    if (!parts[0].starts_with("0x") && !parts[0].starts_with("0X")) return fail("the offset in main is 0x<hex>");
    CountdownRoot root;
    const auto pointer = number_of(parts[0]);
    if (!pointer) return fail("the offset in main is 0x<hex>");
    root.pointer = *pointer;
    root.name = "main+" + hex(*pointer);
    if (parts.size() > 1) {
      const auto bytes = number_of(parts[1]);
      if (!bytes || *bytes < 4 || *bytes > 0x10000) return fail("bytes are 4 to 0x10000");
      root.bytes = static_cast<std::uint32_t>(*bytes);
    }
    if (parts.size() > 2) {
      const auto child = number_of(parts[2]);
      if (!child || *child > 0x4000) return fail("child bytes are 0 to 0x4000");
      root.child_bytes = static_cast<std::uint32_t>(*child);
    }
    roots.push_back(std::move(root));
  }
  if (roots.empty()) return fail("no place to look");
  return roots;
}

const char* countdown_unit_text(CountdownUnit unit) {
  switch (unit) {
    case CountdownUnit::f32_seconds: return "f32";
    case CountdownUnit::u32_seconds: return "u32";
    case CountdownUnit::u32_ms: return "ms";
    case CountdownUnit::u32_mmss: return "mmss";
  }
  return "?";
}

std::string CountdownFound::text() const {
  std::string out = "main+" + hex(base);
  for (const std::uint64_t o : offsets) out += "," + hex(o);
  out += ":";
  out += countdown_unit_text(unit);
  return out;
}

CountdownSearch::CountdownSearch(CountdownSearchSettings settings) : settings_(std::move(settings)) {
  if (settings_.samples < 3) settings_.samples = 3;
  if (settings_.tries < 1) settings_.tries = 1;
  if (settings_.every_us == 0) settings_.every_us = 1'000'000;
}

std::optional<double> CountdownSearch::seconds_of(std::uint32_t raw, CountdownUnit unit) const {
  const double most = static_cast<double>(settings_.length_s) + settings_.slack_s;
  switch (unit) {
    case CountdownUnit::f32_seconds: {
      const float f = std::bit_cast<float>(raw);
      if (!std::isfinite(f) || f <= 0.0f || f > most) return std::nullopt;
      return static_cast<double>(f);
    }
    case CountdownUnit::u32_seconds:
      if (raw == 0 || raw > most) return std::nullopt;
      return static_cast<double>(raw);
    case CountdownUnit::u32_ms:
      if (raw == 0 || raw > most * 1000.0) return std::nullopt;
      return raw / 1000.0;
    case CountdownUnit::u32_mmss:
      return std::nullopt;  // two words: seconds_at
  }
  return std::nullopt;
}

std::optional<double> CountdownSearch::seconds_at(const std::vector<std::uint8_t>& b, std::uint32_t o,
                                                  CountdownUnit unit) const {
  auto u32 = [&](std::uint32_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
  };
  if (static_cast<std::size_t>(o) + 4 > b.size()) return std::nullopt;
  if (unit != CountdownUnit::u32_mmss) return seconds_of(u32(o), unit);
  if (static_cast<std::size_t>(o) + 8 > b.size()) return std::nullopt;
  const std::uint32_t minutes = u32(o), seconds = u32(o + 4);
  const double most = static_cast<double>(settings_.length_s) + settings_.slack_s;
  if (seconds > 59 || minutes > 99) return std::nullopt;
  const double total = minutes * 60.0 + seconds;
  if (total <= 0.0 || total > most) return std::nullopt;
  return total;
}

bool CountdownSearch::snapshot(GameMemory& memory, std::uint64_t now_us) {
  regions_.clear();
  words_.clear();
  std::vector<std::vector<std::uint8_t>> blocks;
  for (std::size_t r = 0; r < settings_.roots.size(); ++r) {
    const CountdownRoot& root = settings_.roots[r];
    const auto object = read_ptr(memory, memory.main_base() + root.pointer);
    if (!object) continue;
    std::vector<std::uint8_t> bytes(root.bytes & ~3u);
    if (bytes.empty() || !memory.read(*object, bytes)) continue;
    regions_.push_back(Region{r, std::nullopt, *object, static_cast<std::uint32_t>(bytes.size())});
    blocks.push_back(bytes);
    if (root.child_bytes < 4) continue;
    std::set<std::uint64_t> seen;
    std::uint32_t children = 0;
    for (std::uint64_t at = 0; at + 8 <= bytes.size() && children < settings_.max_children; at += 8) {
      std::uint64_t p = 0;
      for (int i = 7; i >= 0; --i) p = (p << 8) | bytes[at + static_cast<std::uint64_t>(i)];
      if (p < kLowestPointer || (p & 3) != 0 || p == *object || !seen.insert(p).second) continue;
      std::vector<std::uint8_t> child(root.child_bytes & ~3u);
      if (!memory.read(p, child)) continue;
      regions_.push_back(Region{r, at, p, static_cast<std::uint32_t>(child.size())});
      blocks.push_back(std::move(child));
      ++children;
    }
  }
  if (regions_.empty()) return false;
  for (std::size_t g = 0; g < regions_.size() && words_.size() < settings_.max_words; ++g) {
    const auto& b = blocks[g];
    for (std::uint32_t o = 0; o + 4 <= b.size() && words_.size() < settings_.max_words; o += 4) {
      for (const CountdownUnit u : {CountdownUnit::f32_seconds, CountdownUnit::u32_ms, CountdownUnit::u32_mmss,
                                    CountdownUnit::u32_seconds}) {
        const auto s = seconds_at(b, o, u);
        if (!s || *s < 1.0) continue;  // a running countdown shows a second at least
        words_.push_back(Word{g, o, u, *s, *s});
      }
    }
  }
  first_us_ = last_us_ = now_us;
  taken_ = 1;
  return true;
}

std::vector<std::string> CountdownSearch::give_up_try(std::string why) {
  std::vector<std::string> out;
  ++tries_used_;
  words_.clear();
  regions_.clear();
  if (tries_used_ >= settings_.tries) {
    state_ = State::failed;
    out.push_back("escape clock: our countdown was not found (" + why + "; " + std::to_string(tries_used_) +
                  " tries): the group's clock goes on without the game's timer, nothing is set in our game "
                  "(LICHII_ORM_ESCAPE_CLOCK_WORD gives the word by hand)");
    return out;
  }
  restart_ = true;
  out.push_back("escape clock: no countdown yet (" + why + ", try " + std::to_string(tries_used_) + " of " +
                std::to_string(settings_.tries) + "): looking again");
  return out;
}

std::vector<std::string> CountdownSearch::sample(GameMemory& memory, std::uint64_t now_us) {
  std::vector<std::string> out;
  std::vector<std::optional<std::vector<std::uint8_t>>> blocks(regions_.size());
  std::vector<bool> wanted(regions_.size(), false);
  for (const Word& w : words_) wanted[w.region] = true;
  for (std::size_t g = 0; g < regions_.size(); ++g) {
    if (!wanted[g]) continue;
    std::vector<std::uint8_t> b(regions_[g].bytes);
    if (memory.read(regions_[g].address, b)) blocks[g] = std::move(b);
  }
  const double elapsed = static_cast<double>(now_us - first_us_) / 1e6;
  const double slack = 1.0 + 0.25 * elapsed;  // whole seconds read down; an emulator a little slow
  std::vector<Word> kept;
  kept.reserve(words_.size());
  for (Word w : words_) {
    const auto& b = blocks[w.region];
    if (!b) continue;
    const auto s = seconds_at(*b, w.offset, w.unit);
    if (!s) continue;
    const double up = w.unit == CountdownUnit::f32_seconds ? 0.05 : 0.0;
    if (*s > w.last + up) continue;  // a countdown never goes up
    if (std::fabs((w.first - *s) - elapsed) > slack) continue;  // not 1 s a second
    w.last = *s;
    kept.push_back(w);
  }
  words_ = std::move(kept);
  last_us_ = now_us;
  ++taken_;
  if (words_.empty()) {
    return give_up_try("no word dropped 1 s a second over " + secs(elapsed) + " s");
  }
  if (taken_ < settings_.samples) return out;

  // The best: a float before milliseconds before whole seconds, the object before what it
  // points to, then in memory order.
  std::vector<const Word*> order;
  for (const Word& w : words_) order.push_back(&w);
  std::stable_sort(order.begin(), order.end(), [&](const Word* a, const Word* b) {
    const bool ca = regions_[a->region].child_offset.has_value();
    const bool cb = regions_[b->region].child_offset.has_value();
    if (unit_rank(a->unit) != unit_rank(b->unit)) return unit_rank(a->unit) < unit_rank(b->unit);
    if (ca != cb) return !ca;
    if (a->region != b->region) return a->region < b->region;
    return a->offset < b->offset;
  });
  auto found_of = [&](const Word& w) {
    const Region& g = regions_[w.region];
    CountdownFound f;
    f.base = settings_.roots[g.root].pointer;
    if (g.child_offset) f.offsets.push_back(*g.child_offset);
    f.offsets.push_back(w.offset);
    f.unit = w.unit;
    f.address = g.address + w.offset;
    f.seconds = w.last;
    return f;
  };
  // The pointer path must still lead there (a word in an object freed and made again is not it).
  auto leads = [&](const CountdownFound& f) {
    std::uint64_t at = memory.main_base() + f.base;
    for (const std::uint64_t o : f.offsets) {
      const auto p = read_ptr(memory, at);
      if (!p) return false;
      at = *p + o;
    }
    return at == f.address;
  };
  for (const Word* w : order) {
    CountdownFound f = found_of(*w);
    if (!leads(f)) continue;
    std::string line = "escape clock: our countdown found at " + f.text() + " (reads " + secs(f.seconds) + " s; " +
                       std::to_string(words_.size()) + (words_.size() == 1 ? " word" : " words") +
                       " dropped 1 s a second over " + secs(elapsed) + " s";
    std::size_t others = 0;
    for (const Word* o : order) {
      if (o == w || others == 3) continue;
      line += others == 0 ? "; also " : ", ";
      line += found_of(*o).text();
      ++others;
    }
    line += ")";
    out.push_back(std::move(line));
    found_ = std::move(f);
    state_ = State::found;
    words_.clear();
    regions_.clear();
    return out;
  }
  return give_up_try("the words that dropped right are no longer where their pointers lead");
}

std::vector<std::string> CountdownSearch::step(GameMemory& memory, std::uint64_t now_us) {
  std::vector<std::string> out;
  if (state_ == State::found || state_ == State::failed) return out;
  if (state_ == State::idle || restart_) {
    if (!snapshot(memory, now_us)) {
      state_ = State::idle;  // the objects are not up yet: the next frame tries again
      return out;
    }
    restart_ = false;
    state_ = State::searching;
    std::size_t children = 0;
    for (const Region& g : regions_) children += g.child_offset ? 1 : 0;
    std::string where;
    for (const CountdownRoot& r : settings_.roots) where += (where.empty() ? "" : ", ") + r.name;
    out.push_back("escape clock: looking for our countdown (try " + std::to_string(tries_used_ + 1) + " of " +
                  std::to_string(settings_.tries) + "): " + std::to_string(words_.size()) +
                  " words of " + where + " and " + std::to_string(children) +
                  " objects they point to read as 1 to " + std::to_string(settings_.length_s + settings_.slack_s) +
                  " s; the one that drops 1 s a second is it");
    if (words_.empty()) {
      for (auto& line : give_up_try("no word reads as seconds left")) out.push_back(std::move(line));
    }
    return out;
  }
  if (now_us < last_us_ || now_us - last_us_ < settings_.every_us) return out;
  return sample(memory, now_us);
}

void CountdownSearch::interrupt() {
  if (state_ == State::searching) restart_ = true;
}

void CountdownSearch::reset() {
  if (state_ == State::found) return;
  state_ = State::idle;
  tries_used_ = 0;
  restart_ = false;
  words_.clear();
  regions_.clear();
}

}  // namespace coop::host
