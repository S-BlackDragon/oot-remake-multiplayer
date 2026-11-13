#include "coop/host/orm_stats.hpp"

#include <algorithm>
#include <cstdio>

namespace coop::host::orm::stats {

namespace {

std::string number(std::optional<double> v, const char* format) {
  if (!v) return "-";
  char buf[32];
  std::snprintf(buf, sizeof(buf), format, *v);
  return buf;
}

std::string whole(std::optional<std::uint32_t> v) { return v ? std::to_string(*v) : "-"; }

struct Key {
  std::string_view name;
  bool Prefs::*field;
};
constexpr Key kKeys[] = {
    {"fps", &Prefs::fps},     {"ping", &Prefs::ping}, {"red", &Prefs::net},       {"stats", &Prefs::all},
    {"hud", &Prefs::panel},   {"mapa", &Prefs::map},  {"silencio", &Prefs::mute},
};

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

}  // namespace

std::string encode(const Prefs& prefs) {
  std::string out = "# ORM: lo que cada jugador ve en pantalla (/fps, /ping, /red, /stats, /hud, /mapa, /silencio)\n";
  for (const Key& k : kKeys) out += std::string(k.name) + "=" + (prefs.*(k.field) ? "1" : "0") + "\n";
  return out;
}

Prefs decode(std::string_view text) {
  Prefs out;
  std::size_t at = 0;
  while (at <= text.size()) {
    std::size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view line = trim(text.substr(at, end - at));
    at = end + 1;
    const std::size_t eq = line.find('=');
    if (line.empty() || line.front() == '#' || eq == std::string_view::npos) continue;
    const std::string_view key = trim(line.substr(0, eq));
    const std::string_view value = trim(line.substr(eq + 1));
    if (value != "0" && value != "1") continue;
    for (const Key& k : kKeys) {
      if (k.name == key) out.*(k.field) = value == "1";
    }
  }
  return out;
}

std::vector<std::string> corner_lines(const Prefs& prefs, const Input& in) {
  std::vector<std::string> out;
  if (prefs.fps || prefs.all) out.push_back(number(in.fps ? std::optional<double>(*in.fps) : std::nullopt, "%.0f") + " fps");
  if (prefs.ping || prefs.all) out.push_back("ping " + whole(in.ping_ms) + " ms");
  if (prefs.net || prefs.all) {
    out.push_back("red " + number(in.up_kbps, "%.0f") + "/" + number(in.down_kbps, "%.0f") + " kbps, " +
                  whole(in.loss_percent) + "% perdido");
  }
  if (prefs.all) {
    out.push_back(std::to_string(in.puppets) + " marionetas, gancho " +
                  number(in.hook_ms ? std::optional<double>(*in.hook_ms) : std::nullopt, "%.1f") + " ms");
  }
  return out;
}

std::vector<std::string> ping_report(const Input& in) {
  std::vector<std::string> out;
  out.push_back("Ping al servidor: " + whole(in.ping_ms) + " ms");
  for (const Mate& m : in.mates) {
    out.push_back(m.name + ": ~" + whole(m.ping_ms) + " ms (su última muestra)");
  }
  return out;
}

std::vector<std::string> report(const Input& in) {
  Prefs every;
  every.all = true;
  std::vector<std::string> out = corner_lines(every, in);
  for (const Mate& m : in.mates) out.push_back(m.name + ": ~" + whole(m.ping_ms) + " ms");
  return out;
}

void FpsMeter::frame(std::uint64_t now_us) {
  times_.push_back(now_us);
  const std::uint64_t from = now_us > 1'000'000 ? now_us - 1'000'000 : 0;
  std::erase_if(times_, [&](std::uint64_t t) { return t < from; });
  if (times_.size() > 400) times_.erase(times_.begin(), times_.begin() + static_cast<std::ptrdiff_t>(times_.size() - 400));
}

std::optional<float> FpsMeter::fps(std::uint64_t now_us) const {
  if (times_.size() < 2) return std::nullopt;
  if (now_us - std::min(now_us, times_.back()) > 1'000'000) return 0.0f;  // stalled
  const std::uint64_t span = times_.back() - times_.front();
  if (span == 0) return std::nullopt;
  return float(times_.size() - 1) * 1e6f / float(span);
}

void Rate::add(std::uint64_t total_bytes, std::uint64_t now_us) {
  if (!bytes_ || total_bytes < *bytes_ || now_us < at_us_) {
    bytes_ = total_bytes;
    at_us_ = now_us;
    kbps_.reset();
    return;
  }
  const std::uint64_t span = now_us - at_us_;
  if (span < kWindowUs) return;
  kbps_ = double(total_bytes - *bytes_) * 8.0 / 1000.0 / (double(span) / 1e6);
  bytes_ = total_bytes;
  at_us_ = now_us;
}

}  // namespace coop::host::orm::stats
