// Tests for the ORM quick messages (oot-remake docs/COMUNICACION.md): the
// portable rules (coop/host/quick_chat.hpp), the switches' reading
// (lichii/hook/orm_quick_chat.h) and the chat log's sender (coop_chat.h).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "coop/host/quick_chat.hpp"
#include "coop_chat.h"
#include "orm_quick_chat.h"

namespace qc = coop::host::quick_chat;

namespace {

constexpr std::uint64_t kSecond = 1'000'000;

// The texts as UTF-8 bytes, split after each escape.
const std::string kHere = "\xc2\xa1" "Aqu\xc3\xad" "!";
const std::string kGo = "\xc2\xa1" "Vamos!";
const std::string kWait = "\xc2\xa1" "Espera!";
const std::string kHelp = "\xc2\xa1" "Ayuda!";

}  // namespace

TEST_CASE("quick chat: the four texts, their d-pad direction and which leave a marker") {
  CHECK(qc::entry(qc::Quick::here).text == kHere);
  CHECK(qc::entry(qc::Quick::go).text == kGo);
  CHECK(qc::entry(qc::Quick::wait).text == kWait);
  CHECK(qc::entry(qc::Quick::help).text == kHelp);
  CHECK(qc::entry(qc::Quick::here).dpad == qc::kDUp);
  CHECK(qc::entry(qc::Quick::go).dpad == qc::kDRight);
  CHECK(qc::entry(qc::Quick::wait).dpad == qc::kDDown);
  CHECK(qc::entry(qc::Quick::help).dpad == qc::kDLeft);
  CHECK(qc::entry(qc::Quick::here).marker);
  CHECK(qc::entry(qc::Quick::help).marker);
  CHECK_FALSE(qc::entry(qc::Quick::go).marker);
  CHECK_FALSE(qc::entry(qc::Quick::wait).marker);
  // The test input's names (coop_testinput.h) and its bits.
  CHECK(qc::entry(qc::Quick::here).dpad_name == "DUP");
  CHECK(qc::entry(qc::Quick::help).dpad_name == "DLEFT");
  CHECK(qc::kL == (1u << 6));
  CHECK(qc::kDUp == (1u << 13));
  // Every text fits a CHAT line (200 characters) and has no control character.
  for (const auto& e : qc::kTable) {
    CHECK(e.text.size() < 200);
    for (const char c : e.text) CHECK(static_cast<unsigned char>(c) >= 0x20);
  }
}

TEST_CASE("quick chat: a line is a quick message only by its exact text") {
  CHECK(qc::parse(kHere) == qc::Quick::here);
  CHECK(qc::parse(kGo) == qc::Quick::go);
  CHECK(qc::parse(kWait) == qc::Quick::wait);
  CHECK(qc::parse(kHelp) == qc::Quick::help);
  CHECK_FALSE(qc::parse("Aqui!").has_value());
  CHECK_FALSE(qc::parse(kHere + " ").has_value());
  CHECK_FALSE(qc::parse(" " + kHere).has_value());
  CHECK_FALSE(qc::parse("hola").has_value());
  CHECK_FALSE(qc::parse("").has_value());
  CHECK(qc::ping_label(qc::Quick::here, "Ana") == kHere + " Ana");
  CHECK(qc::ping_label(qc::Quick::help, "") == kHelp);
}

TEST_CASE("quick chat: the combination's button by name, any case, never the d-pad or a face button") {
  CHECK(qc::button_bit("L") == qc::kL);
  CHECK(qc::button_bit("l") == qc::kL);
  CHECK(qc::button_bit("R") == qc::kR);
  CHECK(qc::button_bit("zl") == qc::kZL);
  CHECK(qc::button_bit("ZR") == qc::kZR);
  CHECK(qc::button_bit("Plus") == qc::kPlus);
  CHECK(qc::button_bit("MINUS") == qc::kMinus);
  CHECK(qc::button_bit("LCLICK") == qc::kLClick);
  CHECK(qc::button_bit("rclick") == qc::kRClick);
  CHECK_FALSE(qc::button_bit("A").has_value());
  CHECK_FALSE(qc::button_bit("DUP").has_value());
  CHECK_FALSE(qc::button_bit("").has_value());
  CHECK_FALSE(qc::button_bit("L ").has_value());
}

TEST_CASE("quick chat: the combination is a d-pad press while the button is held") {
  qc::Combo combo;  // L
  CHECK_FALSE(combo.step(0).has_value());
  CHECK_FALSE(combo.step(qc::kDUp).has_value());  // the d-pad alone: the game's
  CHECK_FALSE(combo.step(0).has_value());
  CHECK_FALSE(combo.step(qc::kL).has_value());      // L alone
  CHECK(combo.step(qc::kL | qc::kDUp) == qc::Quick::here);
  CHECK_FALSE(combo.step(qc::kL | qc::kDUp).has_value());  // held: once
  CHECK_FALSE(combo.step(qc::kL).has_value());
  CHECK(combo.step(qc::kL | qc::kDRight) == qc::Quick::go);
  CHECK(combo.step(qc::kL | qc::kDRight | qc::kDDown) == qc::Quick::wait);  // a second direction
  CHECK(combo.step(qc::kL | qc::kDLeft) == qc::Quick::help);
  // L and the d-pad on the same frame: sent (the game may have seen that one
  // d-pad frame, as the patch masks from the next update on).
  combo.step(0);
  CHECK(combo.step(qc::kL | qc::kDDown) == qc::Quick::wait);
  // The d-pad held first, then L: nothing.
  combo.step(0);
  combo.step(qc::kDUp);
  CHECK_FALSE(combo.step(qc::kDUp | qc::kL).has_value());
  // Two directions pressed at once: the first in the table (up).
  combo.step(0);
  CHECK(combo.step(qc::kL | qc::kDLeft | qc::kDUp) == qc::Quick::here);
  // Other buttons do not matter.
  combo.step(qc::kL);
  CHECK(combo.step(qc::kL | qc::kZR | qc::kDLeft | 1u) == qc::Quick::help);
}

TEST_CASE("quick chat: the d-pad is kept from the game exactly while the button is held") {
  qc::Combo combo(qc::kZL);
  CHECK(combo.button() == qc::kZL);
  CHECK(combo.mask(0) == 0);
  CHECK(combo.mask(qc::kDUp) == 0);
  CHECK(combo.mask(qc::kL | qc::kDUp) == 0);  // L is not this combination's button
  CHECK(combo.mask(qc::kZL) == qc::kDpad);
  CHECK(combo.mask(qc::kZL | qc::kDLeft) == qc::kDpad);
  CHECK(combo.step(qc::kL | qc::kDUp) == std::nullopt);
  CHECK(combo.step(qc::kZL | qc::kDUp) == std::nullopt);  // the d-pad was down before ZL
  combo.step(0);
  CHECK(combo.step(qc::kZL | qc::kDUp) == qc::Quick::here);
  // reset: what was held is forgotten (no pad for a frame).
  combo.reset();
  CHECK(combo.step(qc::kZL | qc::kDUp) == qc::Quick::here);
}

TEST_CASE("quick chat: a rate limit holds a gap and a count in a window") {
  qc::RateLimit limit(kSecond, 3, 10 * kSecond);
  const std::uint64_t t0 = 50 * kSecond;
  CHECK(limit.take(t0));
  CHECK_FALSE(limit.take(t0 + kSecond / 2));  // the gap
  CHECK(limit.take(t0 + kSecond));            // exactly the gap: allowed
  CHECK(limit.take(t0 + 2 * kSecond));
  CHECK_FALSE(limit.allowed(t0 + 3 * kSecond));  // three in the window
  CHECK_FALSE(limit.take(t0 + 9 * kSecond));
  CHECK(limit.take(t0 + 10 * kSecond));  // the first is out of the window
  CHECK_FALSE(limit.take(t0 + 10 * kSecond + kSecond / 2));
  // A time before the last one counts as too soon.
  CHECK_FALSE(limit.allowed(t0));
  // allowed does not count.
  qc::RateLimit free(0, 0, 0);
  for (int i = 0; i < 100; ++i) CHECK(free.take(t0));
  qc::RateLimit counted(0, 2, kSecond);
  CHECK(counted.allowed(t0));
  CHECK(counted.allowed(t0));
  counted.note(t0);
  counted.note(t0);
  CHECK_FALSE(counted.allowed(t0));
  CHECK(counted.allowed(t0 + kSecond));
}

TEST_CASE("quick chat: the sender sends a quick message at most once a second and 5 in 10 s") {
  qc::Sender sender;
  const std::uint64_t t0 = 100 * kSecond;
  // Ten presses in three seconds (one every 300 ms): one a second goes.
  int sent = 0;
  for (int i = 0; i < 10; ++i) {
    const std::uint64_t now = t0 + static_cast<std::uint64_t>(i) * 300'000;
    if (sender.may_quick(now)) {
      sender.sent_quick(now);
      ++sent;
    }
  }
  CHECK(sent == 3);  // at 0, 1.2 s and 2.4 s
  // Two more a second apart, then the sixth in 10 s is refused.
  CHECK(sender.may_quick(t0 + 3'600'000));
  sender.sent_quick(t0 + 3'600'000);
  CHECK(sender.may_quick(t0 + 4'800'000));
  sender.sent_quick(t0 + 4'800'000);
  CHECK_FALSE(sender.may_quick(t0 + 6 * kSecond));
  CHECK_FALSE(sender.may_quick(t0 + 9'900'000));
  CHECK(sender.may_quick(t0 + 10 * kSecond));  // the first is out
  // Not counted until sent: asking twice changes nothing.
  CHECK(sender.may_quick(t0 + 10 * kSecond));
}

TEST_CASE("quick chat: every chat line is kept under the relay's limit, quick ones too") {
  qc::Sender sender;
  const std::uint64_t t0 = 200 * kSecond;
  // 250 ms apart.
  CHECK(sender.may_chat(t0));
  sender.sent_chat(t0);
  CHECK_FALSE(sender.may_chat(t0 + 100'000));
  CHECK(sender.may_chat(t0 + 250'000));
  // Eight in ten seconds, quick messages counted with the typed ones.
  for (int i = 1; i < 7; ++i) sender.sent_chat(t0 + static_cast<std::uint64_t>(i) * kSecond);
  CHECK(sender.may_quick(t0 + 7 * kSecond));
  sender.sent_quick(t0 + 7 * kSecond);  // the eighth line
  CHECK_FALSE(sender.may_chat(t0 + 8 * kSecond));
  CHECK_FALSE(sender.may_quick(t0 + 9 * kSecond));  // its own limit lets it, the chat's not
  CHECK(sender.may_chat(t0 + 10 * kSecond));
  // Under the relay's (server/session.cpp: 10 in 10 s, no gap).
  CHECK(qc::kChatCount < 10);
}

TEST_CASE("quick chat: the receiver shows at most 5 quick lines from one player in 10 s") {
  qc::Receiver receiver;
  const std::uint64_t t0 = 300 * kSecond;
  for (int i = 0; i < 5; ++i) CHECK(receiver.accept(2, t0 + static_cast<std::uint64_t>(i) * 100'000));
  CHECK_FALSE(receiver.accept(2, t0 + kSecond));
  CHECK(receiver.accept(3, t0 + kSecond));  // another player has his own
  CHECK(receiver.accept(2, t0 + 10 * kSecond));
  receiver.forget(3);
  for (int i = 0; i < 5; ++i) CHECK(receiver.accept(3, t0 + 10 * kSecond));  // no gap: five at once
  CHECK_FALSE(receiver.accept(3, t0 + 10 * kSecond));
}

TEST_CASE("quick chat: one marker per sender, the newest, gone after 5 s") {
  qc::Pings pings;
  const std::uint64_t t0 = 400 * kSecond;
  qc::Ping a;
  a.player = 2;
  a.quick = qc::Quick::here;
  a.position = {1.0f, 2.0f, 3.0f};
  a.scene = "KokiriForest_Child";
  a.name = "Ana";
  a.rgb = {0xa0, 0x5c, 0xe0};
  a.until_us = 7;  // set by place
  pings.place(a, t0);
  qc::Ping b = a;
  b.player = 1;
  b.quick = qc::Quick::help;
  pings.place(b, t0 + kSecond);
  auto live = pings.live(t0 + 2 * kSecond);
  REQUIRE(live.size() == 2);
  CHECK(live[0].player == 1);  // lowest first
  CHECK(live[1].player == 2);
  CHECK(live[1].until_us == t0 + qc::kPingUs);
  CHECK(live[1].name == "Ana");
  // A new one from player 2 replaces the old, and its 5 s start again.
  a.quick = qc::Quick::help;
  a.position = {9.0f, 9.0f, 9.0f};
  pings.place(a, t0 + 3 * kSecond);
  live = pings.live(t0 + 4 * kSecond);
  REQUIRE(live.size() == 2);
  CHECK(live[1].quick == qc::Quick::help);
  CHECK(live[1].position[0] == 9.0f);
  // Player 1's is gone at t0 + 6 s, player 2's at t0 + 8 s.
  CHECK(pings.live(t0 + 6 * kSecond - 1).size() == 2);
  live = pings.live(t0 + 6 * kSecond);
  REQUIRE(live.size() == 1);
  CHECK(live[0].player == 2);
  CHECK(pings.live(t0 + 8 * kSecond).empty());
  CHECK(pings.size() == 0);
  pings.place(a, t0);
  pings.forget(2);
  CHECK(pings.live(t0).empty());
}

TEST_CASE("quick chat: the switches' reading (orm_quick_chat.h)") {
  auto c = OrmQuickChat::Read(nullptr, nullptr);
  CHECK_FALSE(c.on);  // off by default
  CHECK(c.button == qc::kL);
  CHECK(c.button_name == "L");
  CHECK(c.warning.empty());
  c = OrmQuickChat::Read("1", "zr");
  CHECK(c.on);
  CHECK(c.button == qc::kZR);
  CHECK(c.button_name == "ZR");
  CHECK(c.warning.empty());
  c = OrmQuickChat::Read("yes", "A");
  CHECK(c.on);
  CHECK(c.button == qc::kL);  // not a button the combination may use: L
  CHECK(c.warning.find("LICHII_ORM_QUICK_CHAT_BUTTON=A") != std::string::npos);
  c = OrmQuickChat::Read("maybe", "");
  CHECK_FALSE(c.on);
  CHECK(c.warning.find("LICHII_ORM_QUICK_CHAT=maybe") != std::string::npos);
  c = OrmQuickChat::Read("0", "R");
  CHECK_FALSE(c.on);
  CHECK(c.button == qc::kR);
  CHECK(OrmQuickChat::ComboName(OrmQuickChat::Read("1", nullptr), qc::Quick::here) == "L+DUP");
  CHECK(OrmQuickChat::ComboName(OrmQuickChat::Read("1", "minus"), qc::Quick::help) == "MINUS+DLEFT");
}

TEST_CASE("quick chat: the spectator's d-pad is not also a quick message (Strip)") {
  const auto on = OrmQuickChat::Read("1", nullptr);
  const auto off = OrmQuickChat::Read(nullptr, nullptr);
  CHECK(OrmQuickChat::Strip(on, std::nullopt) == std::nullopt);
  CHECK(OrmQuickChat::Strip(on, qc::kDLeft) == qc::kDLeft);              // L not held
  CHECK(OrmQuickChat::Strip(on, qc::kL | qc::kDLeft | 1u) == (qc::kL | 1u));  // held: no d-pad
  CHECK(OrmQuickChat::Strip(off, qc::kL | qc::kDLeft) == (qc::kL | qc::kDLeft));  // off: untouched
}

TEST_CASE("quick chat: a chat line keeps its sender for the HUD (coop_chat.h)") {
  CoopChat::Log log;
  log.Add("Ana joined", true);
  log.AddFrom(2, "Ana", kHere, true);
  log.AddFrom(1, "you", "hola", false);
  const auto lines = log.Lines();
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].notice);
  CHECK(lines[0].player == 0);
  CHECK(lines[0].name.empty());
  CHECK(lines[1].text == "Ana: " + kHere);
  CHECK(lines[1].player == 2);
  CHECK(lines[1].name == "Ana");
  CHECK(lines[1].quick);
  CHECK_FALSE(lines[1].notice);
  CHECK(lines[2].text == "you: hola");
  CHECK_FALSE(lines[2].quick);
  // Six kept, oldest dropped.
  for (int i = 0; i < 10; ++i) log.AddFrom(3, "Saria", std::to_string(i), false);
  const auto kept = log.Lines();
  REQUIRE(kept.size() == CoopChat::kKeep);
  CHECK(kept.back().text == "Saria: 9");
}
