#include "coop/host/flag_burst.hpp"

#include <utility>

namespace coop::host {

std::optional<std::vector<protocol::FlagEntry>> FlagBurst::push(
    std::span<const protocol::FlagEntry> changes, std::uint64_t now_us) {
  held_.insert(held_.end(), changes.begin(), changes.end());
  if (held_.empty()) return std::nullopt;
  if (window_us_ == 0 || !last_sent_us_ || now_us < *last_sent_us_ ||
      now_us - *last_sent_us_ >= window_us_) {
    return take(now_us);
  }
  if (!changes.empty()) ++held_frames_;  // within the window of the last message: held
  return std::nullopt;
}

std::optional<std::vector<protocol::FlagEntry>> FlagBurst::flush(std::uint64_t now_us) {
  return take(now_us);
}

std::optional<std::vector<protocol::FlagEntry>> FlagBurst::take(std::uint64_t now_us) {
  if (held_.empty()) return std::nullopt;
  ++sent_;
  last_sent_us_ = now_us;
  return std::exchange(held_, {});
}

}  // namespace coop::host
