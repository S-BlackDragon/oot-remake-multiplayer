#pragma once

// ORM quick messages and the world ping (oot-remake docs/COMUNICACION.md,
// decision COM-1): hold a button (L by default) and press the d-pad, and one
// of four fixed texts goes out as daurosk's own CHAT (type 9): up "¡Aquí!",
// right "¡Vamos!", down "¡Espera!", left "¡Ayuda!". No message type of our
// own: an older game shows them as plain chat, a newer one knows them by
// their exact text. "¡Aquí!" and "¡Ayuda!" also put a marker in the world
// for 5 s where the sender stands: the sender uses its own position, the
// others the sender's presence when the line comes.
//
// This file is the portable side (no sockets, no emulator, no clock of its
// own: every call takes the time):
// - the table and its texts (kTable, parse);
// - the combination read from the pad's buttons (Combo): the d-pad pressed
//   while the button is held, and the d-pad taken from the game meanwhile
//   (patch 0101-orm-quick-chat-input);
// - the rate limits: the sender's (Sender: a quick message at most once a
//   second and 5 in 10 s, and every chat line at most 8 in 10 s, 250 ms
//   apart, under the relay's own limit of 10 in 10 s, 100 ms apart, in
//   server/session.cpp), and the receiver's (Receiver: at most 5 quick lines
//   from one player in 10 s);
// - the markers (Pings): one per sender, the new one replaces the old, gone
//   after 5 s.
//
// Switch: LICHII_ORM_QUICK_CHAT=1 (off by default), the button
// LICHII_ORM_QUICK_CHAT_BUTTON (lichii/hook/orm_quick_chat.h). Test:
// tests/orm/quick_chat_test.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coop::host::quick_chat {

// The emulator's NpadButton bits (hid_core/hid_types.h), as the test input's.
inline constexpr std::uint64_t kLClick = 1u << 4;
inline constexpr std::uint64_t kRClick = 1u << 5;
inline constexpr std::uint64_t kL = 1u << 6;
inline constexpr std::uint64_t kR = 1u << 7;
inline constexpr std::uint64_t kZL = 1u << 8;
inline constexpr std::uint64_t kZR = 1u << 9;
inline constexpr std::uint64_t kPlus = 1u << 10;
inline constexpr std::uint64_t kMinus = 1u << 11;
inline constexpr std::uint64_t kDLeft = 1u << 12;
inline constexpr std::uint64_t kDUp = 1u << 13;
inline constexpr std::uint64_t kDRight = 1u << 14;
inline constexpr std::uint64_t kDDown = 1u << 15;
inline constexpr std::uint64_t kDpad = kDLeft | kDUp | kDRight | kDDown;

enum class Quick : std::uint8_t { here, go, wait, help };

struct Entry {
  Quick quick = Quick::here;
  std::string_view text;       // UTF-8, sent as it is as CHAT
  std::uint64_t dpad = 0;      // the d-pad direction that sends it
  std::string_view dpad_name;  // as the test input spells it (DUP)
  bool marker = false;         // a marker in the world at the sender
};

// The four messages, in the order a d-pad press is looked at. The strings
// are split after each escape so a letter is never read as a hex digit.
inline constexpr std::array<Entry, 4> kTable{{
    {Quick::here, "\xc2\xa1" "Aqu\xc3\xad" "!", kDUp, "DUP", true},
    {Quick::go, "\xc2\xa1" "Vamos!", kDRight, "DRIGHT", false},
    {Quick::wait, "\xc2\xa1" "Espera!", kDDown, "DDOWN", false},
    {Quick::help, "\xc2\xa1" "Ayuda!", kDLeft, "DLEFT", true},
}};

// The rates and times (docs/COMUNICACION.md "Límite de ritmo").
inline constexpr std::uint64_t kQuickGapUs = 1'000'000;  // one quick message a second
inline constexpr std::size_t kQuickCount = 5;            // and 5 ...
inline constexpr std::uint64_t kQuickWindowUs = 10'000'000;  // ... in 10 s
// Every chat line (typed or quick), kept under the relay's limit (10 in
// 10 s, 100 ms apart): a line the relay would drop is refused here instead,
// so the chat box turns red and nothing is shown as sent that never went.
inline constexpr std::uint64_t kChatGapUs = 250'000;
inline constexpr std::size_t kChatCount = 8;
inline constexpr std::uint64_t kChatWindowUs = 10'000'000;
// The receiver: at most this many quick lines from one player in 10 s.
inline constexpr std::size_t kReceiveCount = 5;
inline constexpr std::uint64_t kReceiveWindowUs = 10'000'000;
// How long a marker stays.
inline constexpr std::uint64_t kPingUs = 5'000'000;
// How far over the sender's head the marker's text floats, in world units
// (about a metre: Ocarina's unit), above his own name tag.
inline constexpr float kPingAbove = 0.6f;

const Entry& entry(Quick quick);
// The message whose text is exactly `text`; nothing for any other line.
std::optional<Quick> parse(std::string_view text);
// The marker's text: "¡Aquí! Ana" (`name` empty: the text alone).
std::string ping_label(Quick quick, std::string_view name);
// The bit of a button the combination may use, by name, any case: L, R, ZL,
// ZR, PLUS, MINUS, LCLICK, RCLICK. Nothing for any other (the d-pad, the face
// buttons, a typo).
std::optional<std::uint64_t> button_bit(std::string_view name);

// The combination, from the pad's buttons once a frame.
class Combo {
 public:
  explicit Combo(std::uint64_t button = kL) : button_(button) {}

  // The message the buttons send now: a d-pad direction pressed since the
  // last call (not held then) while the button is held now. Pressing the
  // button with the d-pad already down sends nothing. Two directions pressed
  // at once: the first in kTable.
  std::optional<Quick> step(std::uint64_t buttons);
  // The d-pad bits to keep from the game: all four while the button is held.
  std::uint64_t mask(std::uint64_t buttons) const { return (buttons & button_) != 0 ? kDpad : 0; }
  // No pad this frame (none connected): what was held is forgotten.
  void reset() { last_ = 0; }
  std::uint64_t button() const { return button_; }

 private:
  std::uint64_t button_;
  std::uint64_t last_ = 0;
};

// At most `count` events in any `window_us` (count or window 0: no such
// rule), and each at least `gap_us` after the one before (0: no such rule).
// The times only go forward; a time before the last one counts as too soon.
class RateLimit {
 public:
  RateLimit(std::uint64_t gap_us, std::size_t count, std::uint64_t window_us)
      : gap_us_(gap_us), count_(count), window_us_(window_us) {}

  // Whether one more may happen at `now_us`.
  bool allowed(std::uint64_t now_us) const;
  // One happened at `now_us`.
  void note(std::uint64_t now_us);
  // allowed, and when it is, note.
  bool take(std::uint64_t now_us);

 private:
  std::uint64_t gap_us_;
  std::size_t count_;
  std::uint64_t window_us_;
  std::deque<std::uint64_t> times_;
};

// The sender's limits. Not thread safe: the hook guards it with a lock (the
// chat box sends from the Qt thread, the combination from the present one).
class Sender {
 public:
  // Whether a quick message may go now (both its own limit and the chat
  // lines' one).
  bool may_quick(std::uint64_t now_us) const;
  // Whether a typed chat line may go now.
  bool may_chat(std::uint64_t now_us) const;
  // One went (counted only once it was really sent).
  void sent_quick(std::uint64_t now_us);
  void sent_chat(std::uint64_t now_us);

 private:
  RateLimit quick_{kQuickGapUs, kQuickCount, kQuickWindowUs};
  RateLimit chat_{kChatGapUs, kChatCount, kChatWindowUs};
};

// The receiver's limit, per sending player.
class Receiver {
 public:
  // A quick line from `player` at `now_us`: true to show it (and place its
  // marker), false to drop it (the sixth in 10 s from that player).
  bool accept(std::uint8_t player, std::uint64_t now_us);
  // The player left: a new one with that number starts afresh.
  void forget(std::uint8_t player);

 private:
  std::map<std::uint8_t, RateLimit> by_player_;
};

struct Ping {
  std::uint8_t player = 0;
  Quick quick = Quick::here;
  std::array<float, 3> position{};  // the sender's feet, world units
  float head = 0.0f;                // his head's height over them (0: not known)
  std::string scene;                // where (drawn only there)
  std::string name;                 // the sender's name, for the label
  std::array<std::uint8_t, 3> rgb{};  // the sender's colour
  std::uint64_t until_us = 0;       // gone at this time
};

// The live markers: one per sender, the newest.
class Pings {
 public:
  void place(Ping ping, std::uint64_t now_us);
  // The markers still up at `now_us`, lowest player first; the others are
  // dropped.
  std::vector<Ping> live(std::uint64_t now_us);
  void forget(std::uint8_t player);
  void clear() { by_player_.clear(); }
  std::size_t size() const { return by_player_.size(); }

 private:
  std::map<std::uint8_t, Ping> by_player_;
};

}  // namespace coop::host::quick_chat
