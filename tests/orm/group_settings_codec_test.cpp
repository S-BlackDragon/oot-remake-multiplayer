// GROUP_SETTINGS on the wire (coop/protocol/orm_messages.hpp, oot-remake docs/AJUSTES_GRUPO.md).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_messages.hpp"

using namespace coop::protocol;
using Bytes = std::vector<std::uint8_t>;

namespace {

GroupSettings sample() {
  return GroupSettings{0, {{"LICHII_COOP_DOWNED", "on"}, {"OptionData.IsUseAmiibo", "off"}}};
}

Bytes must_encode(const Message& m) {
  Bytes out;
  REQUIRE(encode(m, out) == Error::none);
  return out;
}

}  // namespace

TEST_CASE("group settings have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::group_settings) == 0xF006);
  CHECK(channel_of(MessageType::group_settings) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::group_settings, kChannelProgress));
  CHECK(may_send(Sender::client, MessageType::group_settings));
  CHECK(may_send(Sender::server, MessageType::group_settings));
  Message m = sample();
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);  // player 0
  CHECK(set_sender(m, 4));
  CHECK(sender_of(m) == 4);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::wrong_sender);

  const Bytes b = must_encode(sample());
  Bytes expected{0x06, 0xF0, 0, 0, 0, 2};  // type, length (filled below), player 0, two entries
  const std::string k1 = "LICHII_COOP_DOWNED", v1 = "on", k2 = "OptionData.IsUseAmiibo", v2 = "off";
  for (const std::string* s : {&k1, &v1, &k2, &v2}) {
    expected.push_back(static_cast<std::uint8_t>(s->size()));
    expected.insert(expected.end(), s->begin(), s->end());
  }
  expected[2] = static_cast<std::uint8_t>(expected.size() - kHeaderSize);
  CHECK(b == expected);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == Message(sample()));

  // No entries at all is a valid message (a game with every switch unknown).
  Message empty = GroupSettings{3, {}};
  REQUIRE(decode(must_encode(empty), back) == Error::none);
  CHECK(back == empty);
}

TEST_CASE("bad group settings are refused (ORM)") {
  Bytes out;
  CHECK(encode(GroupSettings{7, {}}, out) == Error::bad_value);  // no player 7
  CHECK(encode(GroupSettings{0, {{"has space", "on"}}}, out) == Error::bad_value);
  CHECK(encode(GroupSettings{0, {{"", "on"}}}, out) == Error::bad_value);
  CHECK(encode(GroupSettings{0, {{"A", "two words"}}}, out) == Error::bad_value);
  CHECK(encode(GroupSettings{0, {{"B", "on"}, {"A", "on"}}}, out) == Error::bad_value);  // not sorted
  CHECK(encode(GroupSettings{0, {{"A", "on"}, {"A", "off"}}}, out) == Error::bad_value);  // twice
  CHECK(encode(GroupSettings{0, {{std::string(kMaxSettingKeyChars + 1, 'K'), "on"}}}, out) == Error::bad_value);
  CHECK(encode(GroupSettings{0, {{"A", std::string(kMaxSettingValueChars + 1, 'v')}}}, out) == Error::bad_value);
  GroupSettings many;
  for (std::size_t i = 0; i <= kMaxGroupSettings; ++i) {
    many.entries.push_back({"K" + std::to_string(100 + i), "on"});
  }
  CHECK(encode(many, out) == Error::bad_value);
  many.entries.pop_back();
  CHECK(encode(many, out) == Error::none);  // 48 is the most
  out.clear();
  CHECK(encode(GroupSettings{0, {{"A", ""}}}, out) == Error::none);  // an empty value is fine

  const Bytes b = must_encode(sample());
  Message m;
  Bytes bad = b;
  bad[kHeaderSize] = 7;
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad.pop_back();
  bad[2] = static_cast<std::uint8_t>(bad.size() - kHeaderSize);
  CHECK(decode(bad, m) == Error::truncated);
  bad = b;
  bad.push_back(0);
  bad[2] = static_cast<std::uint8_t>(bad.size() - kHeaderSize);
  CHECK(decode(bad, m) == Error::trailing_bytes);
  bad = b;
  bad[kHeaderSize + 1] = 1;  // says one entry, two follow
  CHECK(decode(bad, m) == Error::trailing_bytes);
  bad = b;
  bad[kHeaderSize + 3] = ' ';  // "L" of the first key becomes a space
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 1] = static_cast<std::uint8_t>(kMaxGroupSettings + 1);
  CHECK(decode(bad, m) == Error::bad_value);
}

TEST_CASE("setting keys and values: what is allowed (ORM)") {
  CHECK(valid_setting_key("OptionData.ZeldaNoteState"));
  CHECK(valid_setting_key("LICHII_COOP_DUNGEON_LISTS"));
  CHECK_FALSE(valid_setting_key("a-b"));
  CHECK_FALSE(valid_setting_key("a/b"));
  CHECK(valid_setting_value("on:0123abcd"));
  CHECK(valid_setting_value("NotUse"));
  CHECK(valid_setting_value(""));
  CHECK_FALSE(valid_setting_value("a b"));
  CHECK_FALSE(valid_setting_value("\x7f"));
  CHECK_FALSE(valid_setting_value("caf\xc3\xa9"));  // ASCII only
}
