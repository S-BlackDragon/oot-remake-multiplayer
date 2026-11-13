#include "coop/host/quick_chat.hpp"

#include <utility>

namespace coop::host::quick_chat {

const Entry& entry(Quick quick) {
  for (const Entry& e : kTable) {
    if (e.quick == quick) return e;
  }
  return kTable[0];
}

std::optional<Quick> parse(std::string_view text) {
  for (const Entry& e : kTable) {
    if (e.text == text) return e.quick;
  }
  return std::nullopt;
}

std::string ping_label(Quick quick, std::string_view name) {
  std::string out(entry(quick).text);
  if (!name.empty()) {
    out += ' ';
    out += name;
  }
  return out;
}

std::optional<std::uint64_t> button_bit(std::string_view name) {
  std::string upper(name);
  for (char& c : upper) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  struct Named {
    std::string_view name;
    std::uint64_t bit;
  };
  static constexpr Named kButtons[] = {
      {"L", kL},         {"R", kR},         {"ZL", kZL},         {"ZR", kZR},
      {"PLUS", kPlus},   {"MINUS", kMinus}, {"LCLICK", kLClick}, {"RCLICK", kRClick},
  };
  for (const Named& b : kButtons) {
    if (b.name == upper) return b.bit;
  }
  return std::nullopt;
}

std::optional<Quick> Combo::step(std::uint64_t buttons) {
  const std::uint64_t pressed = buttons & ~last_;
  last_ = buttons;
  if ((buttons & button_) == 0) return std::nullopt;
  for (const Entry& e : kTable) {
    if ((pressed & e.dpad) != 0) return e.quick;
  }
  return std::nullopt;
}

bool RateLimit::allowed(std::uint64_t now_us) const {
  if (!times_.empty()) {
    const std::uint64_t last = times_.back();
    if (now_us < last || (gap_us_ != 0 && now_us - last < gap_us_)) return false;
  }
  if (count_ == 0 || window_us_ == 0) return true;
  std::size_t in_window = 0;
  for (const std::uint64_t t : times_) {
    if (t > now_us || now_us - t < window_us_) ++in_window;
  }
  return in_window < count_;
}

void RateLimit::note(std::uint64_t now_us) {
  times_.push_back(now_us);
  // Keep only what can still count: the window's times, and the last one
  // for the gap.
  while (times_.size() > 1 && times_.front() <= now_us &&
         (window_us_ == 0 || now_us - times_.front() >= window_us_)) {
    times_.pop_front();
  }
}

bool RateLimit::take(std::uint64_t now_us) {
  if (!allowed(now_us)) return false;
  note(now_us);
  return true;
}

bool Sender::may_quick(std::uint64_t now_us) const {
  return quick_.allowed(now_us) && chat_.allowed(now_us);
}

bool Sender::may_chat(std::uint64_t now_us) const { return chat_.allowed(now_us); }

void Sender::sent_quick(std::uint64_t now_us) {
  quick_.note(now_us);
  chat_.note(now_us);
}

void Sender::sent_chat(std::uint64_t now_us) { chat_.note(now_us); }

bool Receiver::accept(std::uint8_t player, std::uint64_t now_us) {
  auto it = by_player_.find(player);
  if (it == by_player_.end()) {
    it = by_player_.emplace(player, RateLimit(0, kReceiveCount, kReceiveWindowUs)).first;
  }
  return it->second.take(now_us);
}

void Receiver::forget(std::uint8_t player) { by_player_.erase(player); }

void Pings::place(Ping ping, std::uint64_t now_us) {
  ping.until_us = now_us + kPingUs;
  const std::uint8_t player = ping.player;
  by_player_.insert_or_assign(player, std::move(ping));
}

std::vector<Ping> Pings::live(std::uint64_t now_us) {
  std::vector<Ping> out;
  for (auto it = by_player_.begin(); it != by_player_.end();) {
    if (now_us >= it->second.until_us) {
      it = by_player_.erase(it);
    } else {
      out.push_back(it->second);
      ++it;
    }
  }
  return out;
}

void Pings::forget(std::uint8_t player) { by_player_.erase(player); }

}  // namespace coop::host::quick_chat
