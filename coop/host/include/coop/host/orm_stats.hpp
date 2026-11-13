#pragma once
// ORM on-screen statistics (oot-remake docs/COMANDOS.md, "Estadísticas"):
// /fps, /ping, /red and /stats keep a small line in the HUD's top right
// corner, per player, remembered between sessions in the app folder
// (orm_stats.txt). Portable: the numbers come in, the lines go out.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coop::host::orm::stats {

// What a player keeps on screen, and his local view switches (/hud, /mapa,
// /silencio), saved together.
struct Prefs {
  bool fps = false;
  bool ping = false;
  bool net = false;
  bool all = false;    // /stats on: everything
  bool panel = true;   // /hud: the co-op panel (as F9)
  bool map = true;     // /mapa: mates on the map
  bool mute = false;   // /silencio: mates' chat hidden here
  bool operator==(const Prefs&) const = default;
};
// "fps=1\nping=0\n..." (one key a line); unknown keys and bad lines are
// skipped when read, so an older or newer file still loads.
std::string encode(const Prefs& prefs);
Prefs decode(std::string_view text);

struct Mate {
  std::string name;
  std::optional<std::uint32_t> ping_ms;  // the delay of its newest sample (~)
};

struct Input {
  std::optional<float> fps;               // the game's frames a second
  std::optional<std::uint32_t> ping_ms;   // our round trip to the server
  std::optional<double> up_kbps;
  std::optional<double> down_kbps;
  std::optional<std::uint32_t> loss_percent;
  std::size_t puppets = 0;                // mates drawn here
  std::optional<float> hook_ms;           // our hook's time in a frame
  std::vector<Mate> mates;
};

// The corner lines for `prefs` ("60 fps", "ping 45 ms", "red 12/30 kbps 0%",
// "6 marionetas, gancho 1.2 ms"); "-" for a number not known. Empty: none.
std::vector<std::string> corner_lines(const Prefs& prefs, const Input& in);
// /stats alone and /ping alone: a report in the chat.
std::vector<std::string> report(const Input& in);
std::vector<std::string> ping_report(const Input& in);

// The game's frames a second from the time of each frame (a steady clock),
// over the last second.
class FpsMeter {
 public:
  void frame(std::uint64_t now_us);
  std::optional<float> fps(std::uint64_t now_us) const;

 private:
  std::vector<std::uint64_t> times_;
};

// kbps from a growing byte counter, over the last 2 s or so; a counter that
// goes down (a new connection) starts again.
class Rate {
 public:
  static constexpr std::uint64_t kWindowUs = 2'000'000;
  void add(std::uint64_t total_bytes, std::uint64_t now_us);
  std::optional<double> kbps() const { return kbps_; }

 private:
  std::optional<std::uint64_t> bytes_;
  std::uint64_t at_us_ = 0;
  std::optional<double> kbps_;
};

}  // namespace coop::host::orm::stats
