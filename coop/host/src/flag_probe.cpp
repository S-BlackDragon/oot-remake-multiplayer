#include "coop/host/flag_probe.hpp"

#include <algorithm>
#include <cstdio>

namespace coop::host {

std::vector<ProbedFlag> FlagProbe::flags_with(std::string_view extra) {
  std::vector<ProbedFlag> out;
  const auto add = [&out](std::string_view name) {
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
    if (name.empty()) return;
    const FlagKey key(flag_hash(name), std::uint16_t{0});
    if (std::any_of(out.begin(), out.end(), [&](const ProbedFlag& f) { return f.key == key; })) return;
    out.push_back({std::string(name), key});
  };
  for (const std::string_view name :
       {std::string_view("WaterTemple_Room14_SwitchPushed"), std::string_view("WaterTemple_Room14_SwitchPushedByBlock"),
        std::string_view("WaterTemple_Room14_SwitchPushedHeavyBoots"),
        std::string_view("InsidejaubjabuBelly_gimic_switchOn_Stone")}) {
    add(name);
  }
  while (!extra.empty()) {
    const auto comma = extra.find(',');
    add(extra.substr(0, comma));
    if (comma == std::string_view::npos) break;
    extra.remove_prefix(comma + 1);
  }
  return out;
}

FlagProbe::FlagProbe(std::vector<ProbedFlag> flags) : flags_(std::move(flags)) {
  std::string names;
  for (const auto& f : flags_) names += (names.empty() ? "" : ", ") + f.name;
  notes_.push_back("flag probe: on: every change of " + names + " is logged, with who made it");
}

void FlagProbe::changed(const ProbedFlag& f, State& s, std::uint32_t to, std::uint8_t from, std::uint64_t now_us) {
  notes_.push_back("flag probe: " + f.name + " " + std::to_string(s.value) + " -> " + std::to_string(to) +
                   (from == 0     ? std::string(" in our game")
                    : from == 255 ? std::string(" from another game")
                                  : " from P" + std::to_string(from)));
  s.value = to;
  s.changes.push_back({now_us, from});
  while (!s.changes.empty() && now_us - s.changes.front().at_us > kBounceWindowUs) s.changes.pop_front();
  if (s.bounce_told || s.changes.size() < kBounceChanges) return;
  const bool ours = std::any_of(s.changes.begin(), s.changes.end(), [](const Change& c) { return c.from == 0; });
  const bool theirs = std::any_of(s.changes.begin(), s.changes.end(), [](const Change& c) { return c.from != 0; });
  if (!ours || !theirs) return;
  s.bounce_told = true;
  char window[32];
  std::snprintf(window, sizeof window, "%.1f", static_cast<double>(now_us - s.changes.front().at_us) / 1e6);
  notes_.push_back("flag probe: " + f.name + " bounces between games (" + std::to_string(s.changes.size()) +
                   " changes in " + window + " s, ours and the others'): a held: candidate (docs/SOSTENIDOS.md)");
}

void FlagProbe::frame(FlagStore& store, const std::vector<FlagWatcher::Received>& received, std::uint64_t now_us) {
  for (const auto& r : received) {
    for (const auto& f : flags_) {
      if (!(r.key == f.key)) continue;
      State& s = states_[f.key];
      if (!s.known) {
        s.known = true;
        s.value = r.before.value;
      }
      if (r.after.value != s.value) changed(f, s, r.after.value, r.from == 0 ? 255 : r.from, now_us);
    }
  }
  for (const auto& f : flags_) {
    const auto now = store.read(f.key);
    if (!now) continue;
    State& s = states_[f.key];
    if (!s.known) {  // the first reading is the baseline
      s.known = true;
      s.value = now->value;
      continue;
    }
    if (now->value != s.value) changed(f, s, now->value, 0, now_us);
  }
}

}  // namespace coop::host
