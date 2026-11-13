// ORM cases moved out of upstream's tests/protocol/codec_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>  // doctest prints string_view through it
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "../protocol/samples.hpp"
#include "orm_samples.hpp"

using namespace coop::protocol;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes must_encode(const Message& m) {
  Bytes out;
  REQUIRE(encode(m, out) == Error::none);
  return out;
}

Error decode_bytes(const Bytes& b) {
  Message m;
  return decode(b, m);
}

void set_u16(Bytes& b, std::size_t at, std::uint16_t v) {
  b[at] = static_cast<std::uint8_t>(v);
  b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// Rewrites the header length after the body was changed by hand.
void fix_length(Bytes& b) { set_u16(b, 2, static_cast<std::uint16_t>(b.size() - kHeaderSize)); }

}  // namespace

TEST_CASE("an object sample carries a carry component: layout, checks, and an old reader skips it") {
  ObjectSample s{0, test::kPlacedId, 2, {}};
  s.state.time = {10, 1};
  s.state.carry = ObjectCarry{2, 7, Vec3{0.0f, 1.5f, 0.25f}, Quat{0.0f, 0.0f, 0.0f, 1.0f},
                              Vec3{0.0f, 0.1f, 0.0f}, Quat{0.0f, 0.0f, 0.0f, 1.0f}};
  const Bytes b = must_encode(s);
  // header, player, id 10, epoch 2; time (4 + 12); carry (4 + 59)
  REQUIRE(b.size() == kHeaderSize + 1 + 10 + 2 + 16 + 4 + 59);
  const std::size_t carry_at = kHeaderSize + 1 + 10 + 2 + 16;
  CHECK(b[carry_at] == 29);      // component id, little-endian
  CHECK(b[carry_at + 2] == 59);  // its length
  CHECK(b[carry_at + 4] == 2);   // holder
  CHECK(b[carry_at + 5] == 7);   // bone
  Message m;
  REQUIRE(decode(b, m) == Error::none);
  REQUIRE(std::get<ObjectSample>(m).state.carry);
  CHECK(*std::get<ObjectSample>(m).state.carry == *s.state.carry);

  // A reader that does not know id 29 skips it the way it skips id 30: shown
  // with the same bytes under an unknown id.
  Bytes unknown = b;
  unknown[carry_at] = 30;
  REQUIRE(decode(unknown, m) == Error::none);
  CHECK_FALSE(std::get<ObjectSample>(m).state.carry);

  // No holder, a holder past the last player, a turn that is not unit, a
  // NaN place: refused both ways.
  Bytes out;
  for (std::uint8_t holder : {std::uint8_t{0}, static_cast<std::uint8_t>(kMaxPlayers + 1)}) {
    ObjectSample bad = s;
    bad.state.carry->holder = holder;
    CHECK(encode(bad, out) == Error::bad_value);
    Bytes wire = b;
    wire[carry_at + 4] = holder;
    CHECK(decode_bytes(wire) == Error::bad_value);
  }
  ObjectSample bad = s;
  bad.state.carry->rotation = Quat{0.0f, 0.0f, 0.0f, 2.0f};
  CHECK(encode(bad, out) == Error::bad_value);
  bad = s;
  bad.state.carry->bone_offset[1] = std::numeric_limits<float>::quiet_NaN();
  CHECK(encode(bad, out) == Error::bad_value);
  // Cut short: a known component must have its whole length.
  Bytes short_carry = b;
  short_carry[carry_at + 2] = 58;
  short_carry.pop_back();
  fix_length(short_carry);
  CHECK(decode_bytes(short_carry) == Error::bad_length);
}

TEST_CASE("revive messages have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::revive) == 0xF000);
  CHECK(channel_of(MessageType::revive) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::revive, kChannelPlayerSamples));
  CHECK(may_send(Sender::client, MessageType::revive));
  CHECK(may_send(Sender::server, MessageType::revive));
  Message m = Revive{0, 1, kReviveDone};
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);  // player 0
  CHECK_FALSE(set_sender(m, 1));  // reviving yourself
  CHECK(set_sender(m, 2));
  CHECK(sender_of(m) == 2);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  const Bytes b = must_encode(m);
  REQUIRE(b.size() == kHeaderSize + 3);
  CHECK(b[0] == 0x00);
  CHECK(b[1] == 0xF0);
  CHECK(b[kHeaderSize] == 2);
  CHECK(b[kHeaderSize + 1] == 1);
  CHECK(b[kHeaderSize + 2] == 255);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == m);
}

TEST_CASE("bad revive messages are refused") {
  Bytes out;
  CHECK(encode(Revive{0, 0, kReviveDone}, out) == Error::bad_value);
  CHECK(encode(Revive{0, 7, kReviveDone}, out) == Error::bad_value);
  CHECK(encode(Revive{3, 3, kReviveDone}, out) == Error::bad_value);
  CHECK(encode(Revive{7, 1, kReviveDone}, out) == Error::bad_value);
  Message m;
  Bytes b = must_encode(Revive{0, 2, 10});
  b[kHeaderSize + 1] = 0;  // nobody
  CHECK(decode(b, m) == Error::bad_value);
  b = must_encode(Revive{2, 4, 10});
  b[kHeaderSize + 1] = 2;  // the sender itself
  CHECK(decode(b, m) == Error::bad_value);
  b = must_encode(Revive{0, 2, 10});
  b.push_back(0);
  CHECK(decode(b, m) != Error::none);
  b = must_encode(Revive{0, 2, 10});
  b.pop_back();
  CHECK(decode(b, m) != Error::none);
  // Progress: 0 to 100 percent, or 255 the request.
  CHECK(encode(Revive{0, 2, 100}, out) == Error::none);
  CHECK(encode(Revive{0, 2, 101}, out) == Error::bad_value);
  CHECK(encode(Revive{0, 2, 254}, out) == Error::bad_value);
  b = must_encode(Revive{0, 2, 100});
  b[kHeaderSize + 2] = 150;
  CHECK(decode(b, m) == Error::bad_value);
}

TEST_CASE("group defeat messages are the server's, on channel 0, with their layout (ORM)") {
  CHECK(static_cast<int>(MessageType::group_defeat) == 0xF002);
  CHECK(channel_of(MessageType::group_defeat) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::group_defeat, kChannelProgress));
  CHECK_FALSE(may_send(Sender::client, MessageType::group_defeat));
  CHECK(may_send(Sender::server, MessageType::group_defeat));
  Message m = GroupDefeat{2, 0x01020304u};
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::wrong_sender);
  CHECK_FALSE(sender_of(m));  // no player field
  const Bytes b = must_encode(m);
  REQUIRE(b.size() == kHeaderSize + 5);
  CHECK(b[0] == 0x02);
  CHECK(b[1] == 0xF0);
  CHECK(b[kHeaderSize] == 2);
  CHECK(b[kHeaderSize + 1] == 0x04);  // little-endian, as every u32
  CHECK(b[kHeaderSize + 4] == 0x01);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == m);

  Bytes out;
  CHECK(encode(GroupDefeat{0, 1}, out) == Error::bad_value);
  CHECK(encode(GroupDefeat{7, 1}, out) == Error::bad_value);
  Bytes bad = b;
  bad[kHeaderSize] = 0;
  CHECK(decode(bad, back) == Error::bad_value);
  bad = b;
  bad.push_back(0);
  CHECK(decode(bad, back) != Error::none);
  bad = b;
  bad.pop_back();
  CHECK(decode(bad, back) != Error::none);
}

TEST_CASE("story events have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::story_event) == 0xF003);
  CHECK(channel_of(MessageType::story_event) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::story_event, kChannelProgress));
  CHECK(may_send(Sender::client, MessageType::story_event));
  CHECK(may_send(Sender::server, MessageType::story_event));
  Message m = StoryEvent{0, StoryKind::start, "KF", "Ab_9", {1.0f, 2.0f, -3.0f}, 0x01020304u};
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);  // player 0
  CHECK(set_sender(m, 3));
  CHECK(sender_of(m) == 3);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  const Bytes b = must_encode(m);
  // u8 player, u8 kind, u16 + "KF", u16 + "Ab_9", 3 x f32, u32.
  REQUIRE(b.size() == kHeaderSize + 1 + 1 + 4 + 6 + 12 + 4);
  CHECK(b[0] == 0x03);
  CHECK(b[1] == 0xF0);
  CHECK(b[kHeaderSize] == 3);
  CHECK(b[kHeaderSize + 1] == 1);
  CHECK(b[kHeaderSize + 2] == 2);  // the scene's length, little-endian
  CHECK(b[kHeaderSize + 4] == 'K');
  CHECK(b[kHeaderSize + 6] == 4);
  CHECK(b[kHeaderSize + 8] == 'A');
  CHECK(b[kHeaderSize + 12 + 3] == 0x3f);  // 1.0f = 0x3f800000
  CHECK(b[b.size() - 4] == 0x04);
  CHECK(b[b.size() - 1] == 0x01);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == m);
}

TEST_CASE("bad story events are refused") {
  Bytes out;
  const Vec3 p{1.0f, 2.0f, 3.0f};
  CHECK(encode(StoryEvent{0, StoryKind::start, "", "Ab", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", "", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", "with space", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", "Dot.Name", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", std::string(128, 'a'), p, 0}, out) ==
        Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", std::string(127, 'a'), p, 0}, out) == Error::none);
  CHECK(encode(StoryEvent{0, StoryKind::start, std::string(65, 's'), "Ab", p, 0}, out) != Error::none);
  CHECK(encode(StoryEvent{0, static_cast<StoryKind>(0), "KF", "Ab", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, static_cast<StoryKind>(5), "KF", "Ab", p, 0}, out) == Error::bad_value);
  {  // ORM D-048: an end whose owner skipped it
    const Message skipped = StoryEvent{2, StoryKind::skipped, "KF", "Ab", p, 7};
    Bytes sb;
    REQUIRE(encode(skipped, sb) == Error::none);
    Message back;
    REQUIRE(decode(sb, back) == Error::none);
    CHECK(back == skipped);
  }
  CHECK(encode(StoryEvent{7, StoryKind::start, "KF", "Ab", p, 0}, out) == Error::bad_value);
  CHECK(encode(StoryEvent{0, StoryKind::start, "KF", "Ab",
                          {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, 0},
               out) == Error::bad_value);
  Message m;
  Bytes b = must_encode(StoryEvent{0, StoryKind::end, "KF", "Ab", p, 9});
  Bytes bad = b;
  bad[kHeaderSize + 1] = 5;  // an unknown kind
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 9] = '-';  // a name character the game never uses
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 11] = 0x00;  // x (body bytes 10 to 13) becomes a NaN, 0x7fc000..
  bad[kHeaderSize + 12] = 0xc0;
  bad[kHeaderSize + 13] = 0x7f;
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad.push_back(0);
  CHECK(decode(bad, m) != Error::none);
  bad = b;
  bad.pop_back();
  CHECK(decode(bad, m) != Error::none);
}

TEST_CASE("player looks have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::player_look) == 0xF004);
  CHECK(channel_of(MessageType::player_look) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::player_look, kChannelProgress));
  CHECK_FALSE(allowed_on_channel(MessageType::player_look, kChannelPlayerSamples));
  CHECK(may_send(Sender::client, MessageType::player_look));
  CHECK(may_send(Sender::server, MessageType::player_look));
  Message m = PlayerLook{0, {0x12, 0x34, 0x56}, 0};
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);  // player 0
  CHECK(set_sender(m, 5));
  CHECK(sender_of(m) == 5);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::wrong_sender);  // not 0
  const Bytes b = must_encode(m);
  // u16 type 244, u16 length 5, then u8 player, u8 red, green, blue, u8 style.
  const Bytes expected{0x04, 0xF0, 5, 0, 5, 0x12, 0x34, 0x56, 0};
  CHECK(b == expected);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == m);
}

TEST_CASE("bad player looks are refused") {
  Bytes out;
  CHECK(encode(PlayerLook{7, {1, 2, 3}, 0}, out) == Error::bad_value);
  CHECK(out.empty());
  CHECK(encode(PlayerLook{6, {1, 2, 3}, 0}, out) == Error::none);
  Message m;
  Bytes b = must_encode(PlayerLook{0, {1, 2, 3}, 0});
  Bytes bad = b;
  bad[kHeaderSize] = 7;  // no player 7
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad.push_back(0);
  CHECK(decode(bad, m) != Error::none);
  bad = b;
  bad.pop_back();
  CHECK(decode(bad, m) != Error::none);
  // A style this build does not know is kept, so a newer one's look still shows.
  bad = b;
  bad[kHeaderSize + 4] = 9;
  REQUIRE(decode(bad, m) == Error::none);
  CHECK(std::get<PlayerLook>(m).style == 9);
}

TEST_CASE("object poses have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::object_pose) == 0xF005);
  CHECK(channel_of(MessageType::object_pose) == kChannelObjectSamples);
  CHECK_FALSE(allowed_on_channel(MessageType::object_pose, kChannelSession));
  CHECK_FALSE(allowed_on_channel(MessageType::object_pose, kChannelOwnership));
  CHECK_FALSE(allowed_on_channel(MessageType::object_pose, kChannelPlayerSamples));
  CHECK(may_send(Sender::client, MessageType::object_pose));
  CHECK(may_send(Sender::server, MessageType::object_pose));
  const ObjectId id{ObjectKind::placed, 0, 0x0102030405060708ull};
  Message m = ObjectPose{0, id, 0x0a0b, {5, 6}, 9, {}};
  CHECK(check_received(m, Sender::client, kChannelObjectSamples) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelObjectSamples) == Error::wrong_sender);
  CHECK(set_sender(m, 6));
  CHECK(sender_of(m) == 6);
  CHECK(check_received(m, Sender::server, kChannelObjectSamples) == Error::none);
  CHECK(check_received(m, Sender::client, kChannelObjectSamples) == Error::wrong_sender);
  PoseBone bone{4, 0x10, std::array<std::int16_t, 4>{1, 2, 3, -1}, std::nullopt,
                std::array<std::int16_t, 3>{5, 6, 7}};
  const Bytes b = must_encode(ObjectPose{0, id, 0x0a0b, {5, 6}, 9, {bone}});
  // header; player 1, id 10 (kind, maker, u64), epoch 2, time 12, skeleton 4,
  // part 1, parts 1, count 2; bone 2, parts 1, flags 1, rotation 8, scale 6.
  REQUIRE(b.size() == kHeaderSize + 1 + 10 + 2 + 12 + 4 + 2 + 2 + 4 + 8 + 6);
  CHECK(b[0] == 0x05);
  CHECK(b[1] == 0xF0);
  CHECK(b[kHeaderSize + 1] == 0);     // kind placed
  CHECK(b[kHeaderSize + 3] == 0x08);  // the hash, little-endian
  CHECK(b[kHeaderSize + 11] == 0x0b);  // epoch
  CHECK(b[kHeaderSize + 12] == 0x0a);
  CHECK(b[kHeaderSize + 29] == 0);     // part 0
  CHECK(b[kHeaderSize + 30] == 0x01);  // of a pose in one part
  CHECK(b[kHeaderSize + 31] == 1);     // one bone
  CHECK(b[kHeaderSize + 33] == 4);     // the bone
  CHECK(b[kHeaderSize + 35] == 0x05);  // rotation and scale
  CHECK(b[kHeaderSize + 36] == 0x10);  // flags
  CHECK(b[kHeaderSize + 43] == 0xff);  // w = -1
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(std::get<ObjectPose>(back) == ObjectPose{0, id, 0x0a0b, {5, 6}, 9, {bone}});
  // Part 2 of a pose in parts 0 and 2 (ORM "PNJ con muchos huesos"): bones
  // 256 to 383.
  PoseBone high = bone;
  high.bone = 300;
  const ObjectPose two{0, id, 0x0a0b, {5, 6}, 9, {high}, 2, 0x05};
  const Bytes c = must_encode(two);
  CHECK(c[kHeaderSize + 29] == 2);
  CHECK(c[kHeaderSize + 30] == 0x05);
  CHECK(c[kHeaderSize + 33] == 0x2c);  // 300, little-endian
  CHECK(c[kHeaderSize + 34] == 0x01);
  REQUIRE(decode(c, back) == Error::none);
  CHECK(std::get<ObjectPose>(back) == two);
}

TEST_CASE("chest openings have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::chest_open) == 0xF009);
  CHECK(channel_of(MessageType::chest_open) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::chest_open, kChannelProgress));
  CHECK_FALSE(allowed_on_channel(MessageType::chest_open, kChannelObjectSamples));
  CHECK(may_send(Sender::client, MessageType::chest_open));
  CHECK(may_send(Sender::server, MessageType::chest_open));
  const ChestOpen open{0, 0x0102030405060708ull, "Graveyard", {"Open", 3.0f, 1.0f}, 77};
  Message m = open;
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);
  CHECK(set_sender(m, 3));
  CHECK(sender_of(m) == 3);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  const Bytes b = must_encode(open);
  // header; player 1, chest 8, scene 2 + 9, name 2 + 4, frame 4, rate 4, time 8.
  REQUIRE(b.size() == kHeaderSize + 1 + 8 + 11 + 6 + 4 + 4 + 8);
  CHECK(b[0] == 0x09);
  CHECK(b[1] == 0xF0);
  CHECK(b[kHeaderSize + 1] == 0x08);  // the hash, little-endian
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(std::get<ChestOpen>(back) == open);
}

TEST_CASE("bad chest openings are refused (ORM)") {
  Bytes out;
  ChestOpen c{0, 5, "Graveyard", {"Open", 0.0f, 1.0f}, 0};
  CHECK(encode(c, out) == Error::none);
  out.clear();
  ChestOpen bad = c;
  bad.chest = 0;  // no placement
  CHECK(encode(bad, out) == Error::bad_value);
  bad = c;
  bad.chest = ~std::uint64_t{0};  // a made actor's
  CHECK(encode(bad, out) == Error::bad_value);
  bad = c;
  bad.scene.clear();
  CHECK(encode(bad, out) == Error::bad_value);
  bad = c;
  bad.animation.name.clear();
  CHECK(encode(bad, out) == Error::bad_value);
  bad = c;
  bad.animation.frame = std::numeric_limits<float>::infinity();
  CHECK(encode(bad, out) == Error::bad_value);
  bad = c;
  bad.player_number = 7;
  CHECK(encode(bad, out) == Error::bad_value);
  CHECK(out.empty());
  // A good one cut short does not decode.
  Bytes b = must_encode(c);
  b.pop_back();
  Message m;
  CHECK(decode(b, m) != Error::none);
}

TEST_CASE("bad object poses are refused (ORM)") {
  const auto bone = [](std::size_t n) {
    PoseBone b;
    b.bone = static_cast<std::uint16_t>(n);
    b.rotation = std::array<std::int16_t, 4>{0, 0, 0, 32767};
    return b;
  };
  Bytes out;
  ObjectPose p;
  p.id = ObjectId{ObjectKind::placed, 0, 7};
  p.bones.resize(kMaxObjectPoseBones + 1);
  for (std::size_t i = 0; i < p.bones.size(); ++i) p.bones[i] = bone(i);
  CHECK(encode(p, out) == Error::bad_value);  // more than an object pose may carry
  p.bones.resize(kMaxObjectPoseBones);
  CHECK(encode(p, out) == Error::none);
  p.bones = {bone(3), bone(3)};  // not increasing
  CHECK(encode(p, out) == Error::bad_value);
  p.bones = {bone(kMaxPoseBones)};
  CHECK(encode(p, out) == Error::bad_value);
  p.bones = {bone(1)};
  p.bones[0].rotation = std::array<std::int16_t, 4>{std::numeric_limits<std::int16_t>::min(), 0, 0, 0};
  CHECK(encode(p, out) == Error::bad_value);
  p.bones = {bone(1)};
  p.player_number = 7;  // no player 7
  CHECK(encode(p, out) == Error::bad_value);
  p.player_number = 0;
  p.id = ObjectId{ObjectKind::made, 0, 7};  // a made id names its maker
  CHECK(encode(p, out) == Error::bad_value);
  p.id = ObjectId{ObjectKind::placed, 0, 7};
  // Parts (ORM "PNJ con muchos huesos"): part below 4, its own bit in the
  // mask, no mask bit past part 3, every bone in the part's 128.
  p.part = 4;
  p.parts = 0x10;
  CHECK(encode(p, out) == Error::bad_value);
  p.part = 1;
  p.parts = 0x01;  // part 1's bit missing
  CHECK(encode(p, out) == Error::bad_value);
  p.parts = 0x12;  // a fifth part
  CHECK(encode(p, out) == Error::bad_value);
  p.parts = 0x02;  // bone 1 is part 0's
  CHECK(encode(p, out) == Error::bad_value);
  p.bones = {bone(128), bone(255)};
  CHECK(encode(p, out) == Error::none);
  p.bones = {bone(128), bone(256)};  // 256 is part 2's
  CHECK(encode(p, out) == Error::bad_value);
  p.part = 3;
  p.parts = 0x0f;
  p.bones = {bone(511)};
  CHECK(encode(p, out) == Error::none);
  p.bones.clear();
  p.bones.resize(kObjectPosePartBones);
  for (std::size_t i = 0; i < p.bones.size(); ++i) p.bones[i] = bone(3 * kObjectPosePartBones + i);
  CHECK(encode(p, out) == Error::none);  // a whole part
  p.part = 0;
  p.parts = 0x01;
  p.bones = {bone(1)};
  Bytes b = must_encode(p);
  Message m;
  Bytes bad = b;
  bad[kHeaderSize + 35] = 0x08;  // an unknown part bit
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 31] = 0x81;  // count 129
  bad[kHeaderSize + 32] = 0x00;
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 29] = 4;  // part 4
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 30] = 0x02;  // part 0 not in the mask
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 30] = 0x11;  // a fifth part in the mask
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad[kHeaderSize + 29] = 1;  // part 1, mask 0x03: bone 1 is not in its range
  bad[kHeaderSize + 30] = 0x03;
  CHECK(decode(bad, m) == Error::bad_value);
  bad[kHeaderSize + 29] = 0;  // part 0 of parts 0 and 1: fine
  REQUIRE(decode(bad, m) == Error::none);
  CHECK(std::get<ObjectPose>(m).parts == 0x03);
  bad = b;
  bad[kHeaderSize] = 7;
  CHECK(decode(bad, m) == Error::bad_value);
  bad = b;
  bad.pop_back();
  CHECK(decode(bad, m) != Error::none);
  bad = b;
  bad.push_back(0);
  CHECK(decode(bad, m) != Error::none);
}

TEST_CASE("player equip: the horse a player is on (ORM D-R19, component 30)") {
  Message back;
  CHECK(static_cast<std::uint16_t>(coop::protocol::EquipComponent::mount) == 30);
  // Component 30 with "E": the mount; empty is on foot.
  CHECK(decode(Bytes{17, 0, 8, 0, 0, 30, 0, 3, 0, 1, 0, 'E'}, back) == Error::none);
  CHECK(std::get<PlayerEquip>(back).equipment.mount == "E");
  CHECK(decode(Bytes{17, 0, 7, 0, 0, 30, 0, 2, 0, 0, 0}, back) == Error::none);
  CHECK(std::get<PlayerEquip>(back).equipment.mount == "");
  // 16 is upstream's next slot, not ours: skipped as unknown.
  CHECK(decode(Bytes{17, 0, 8, 0, 0, 16, 0, 3, 0, 1, 0, 'E'}, back) == Error::none);
  CHECK_FALSE(std::get<PlayerEquip>(back).equipment.mount);
  PlayerEquip m;
  m.equipment.weapon = "Weapon_Sword_Master";
  m.equipment.mount = "Animal_Epona_Adult";
  REQUIRE(decode(must_encode(m), back) == Error::none);
  CHECK(std::get<PlayerEquip>(back) == m);
  // Kept by a merge that does not carry it, replaced by one that does.
  coop::protocol::Equipment later;
  later.weapon = "Weapon_Sword_Master";
  CHECK(coop::protocol::merge_equipment(m.equipment, later).mount == "Animal_Epona_Adult");
  later.mount = "";
  CHECK(coop::protocol::merge_equipment(m.equipment, later).mount == "");
}

TEST_CASE("ocarina notes have their number, channel and layout (ORM)") {
  CHECK(static_cast<int>(MessageType::orm_ocarina_note) == 0xF007);
  CHECK(channel_of(MessageType::orm_ocarina_note) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::orm_ocarina_note, kChannelProgress));
  CHECK_FALSE(allowed_on_channel(MessageType::orm_ocarina_note, kChannelPlayerSamples));
  CHECK_FALSE(allowed_on_channel(MessageType::orm_ocarina_note, kChannelObjectSamples));
  CHECK(may_send(Sender::client, MessageType::orm_ocarina_note));
  CHECK(may_send(Sender::server, MessageType::orm_ocarina_note));
  Message m = OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 0, 7};
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::wrong_sender);  // player 0
  CHECK(check_received(m, Sender::client, kChannelPlayerSamples) == Error::wrong_channel);
  CHECK(set_sender(m, 3));
  CHECK(sender_of(m) == 3);
  CHECK(check_received(m, Sender::server, kChannelSession) == Error::none);
  CHECK(check_received(m, Sender::client, kChannelSession) == Error::wrong_sender);  // not 0
  CHECK_FALSE(set_sender(m, 7));
  // u16 type 247, u16 length 6, then u8 player, kind, instrument, note, song, serial.
  Bytes b = must_encode(m);
  const Bytes expected{0x07, 0xF0, 6, 0, 3, 1, 0, 11, 0, 7};
  CHECK(b == expected);
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(back == m);
  // A release and a song (the Scarecrow's, the last), a serial at the top.
  b = must_encode(OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 0, 255});
  const Bytes release{0x07, 0xF0, 6, 0, 0, 2, 0, 0, 0, 255};
  CHECK(b == release);
  b = must_encode(OrmOcarinaNote{6, OcarinaKind::song, 0, 0, 12, 0});
  const Bytes song{0x07, 0xF0, 6, 0, 6, 3, 0, 0, 12, 0};
  CHECK(b == song);
  REQUIRE(decode(b, back) == Error::none);
  CHECK(std::get<OrmOcarinaNote>(back).kind == OcarinaKind::song);
  CHECK(std::get<OrmOcarinaNote>(back).song == 12);
  // Zelda's Lullaby is song 0: a song message may carry 0.
  b = must_encode(OrmOcarinaNote{0, OcarinaKind::song, 0, 0, 0, 1});
  REQUIRE(decode(b, back) == Error::none);
  CHECK(std::get<OrmOcarinaNote>(back).song == 0);
}

TEST_CASE("bad ocarina notes are refused (ORM)") {
  Bytes out;
  // Encode: the player, the kind, and each field against its kind.
  CHECK(encode(OrmOcarinaNote{7, OcarinaKind::note, 0, 11, 0, 0}, out) == Error::bad_value);
  CHECK(encode(OrmOcarinaNote{0, static_cast<OcarinaKind>(0), 0, 0, 0, 0}, out) == Error::bad_value);
  CHECK(encode(OrmOcarinaNote{0, static_cast<OcarinaKind>(4), 0, 0, 0, 0}, out) == Error::bad_value);
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::note, 0, 0, 0, 0}, out) == Error::bad_value);   // Rest is no note
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::note, 0, 20, 0, 0}, out) == Error::bad_value);  // past HighF
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 2, 0}, out) == Error::bad_value);  // a note and a song
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::note, 4, 11, 0, 0}, out) == Error::bad_value);  // no instrument 4
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::release, 0, 11, 0, 0}, out) == Error::bad_value);
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::release, 0, 0, 1, 0}, out) == Error::bad_value);
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::song, 0, 0, 13, 0}, out) == Error::bad_value);  // 13: no song
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::song, 0, 4, 2, 0}, out) == Error::bad_value);
  CHECK(out.empty());
  CHECK(encode(OrmOcarinaNote{6, OcarinaKind::note, 3, 19, 0, 0}, out) == Error::none);
  CHECK(encode(OrmOcarinaNote{0, OcarinaKind::note, 0, 1, 0, 0}, out) == Error::none);

  // Decode: the same rules on the bytes.
  Message m;
  const Bytes b = must_encode(OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 0, 5});
  auto with = [&b](std::size_t at, std::uint8_t v) {
    Bytes x = b;
    x[kHeaderSize + at] = v;
    return x;
  };
  CHECK(decode(with(0, 7), m) == Error::bad_value);   // no player 7
  CHECK(decode(with(1, 0), m) == Error::bad_value);   // no kind 0
  CHECK(decode(with(1, 4), m) == Error::bad_value);   // nor 4
  CHECK(decode(with(1, 2), m) == Error::bad_value);   // a release with a note
  CHECK(decode(with(1, 3), m) == Error::bad_value);   // a song with a note
  CHECK(decode(with(2, 4), m) == Error::bad_value);   // no instrument 4
  CHECK(decode(with(3, 0), m) == Error::bad_value);   // Rest
  CHECK(decode(with(3, 20), m) == Error::bad_value);  // past HighF
  CHECK(decode(with(4, 1), m) == Error::bad_value);   // a note with a song
  REQUIRE(decode(with(5, 200), m) == Error::none);    // any serial
  CHECK(std::get<OrmOcarinaNote>(m).serial == 200);
  Bytes bad = b;
  bad.push_back(0);
  CHECK(decode(bad, m) != Error::none);
  bad = b;
  bad.pop_back();
  CHECK(decode(bad, m) != Error::none);
  bad = b;
  bad.pop_back();
  fix_length(bad);
  CHECK(decode(bad, m) == Error::truncated);
}

// ---- Upstream's every-message checks, over our samples (tests/orm/orm_samples.hpp) ----

TEST_CASE("every ORM sample message survives a round trip") {
  for (const Message& m : test::orm_messages()) {
    CAPTURE(m.index());
    const Bytes b = must_encode(m);
    Message back;
    REQUIRE(decode(b, back) == Error::none);
    CHECK(back == m);
    CHECK(type_of(back) == type_of(m));
  }
}

TEST_CASE("every ORM sample message: truncated buffers are refused at every length") {
  for (const Message& m : test::orm_messages()) {
    const Bytes full = must_encode(m);
    for (std::size_t n = 0; n < full.size(); ++n) {
      CAPTURE(m.index());
      CAPTURE(n);
      const Bytes cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
      CHECK(decode_bytes(cut) == Error::truncated);
      // Also cut, with the header length fixed to match: the body is short.
      if (n >= kHeaderSize) {
        Bytes fixed = cut;
        fix_length(fixed);
        Message back;
        const Error e = decode(fixed, back);
        // As upstream's case: only an object sample's optional components may
        // be cut off cleanly; anything else must be refused.
        if (e == Error::none) CHECK(std::holds_alternative<ObjectSample>(m));
      }
    }
  }
}

TEST_CASE("every ORM sample message goes on its own channel") {
  for (const Message& m : test::orm_messages()) {
    CHECK(channel_of(type_of(m)) == channel_of(m));
    CHECK(allowed_on_channel(type_of(m), channel_of(m)));
  }
}

namespace {

// As upstream's tests/protocol/fuzz_test.cpp: decodes from a heap block of
// exactly the input's size; a buffer that decodes encodes and decodes again.
void decode_exact(const Bytes& input) {
  const std::size_t n = input.size();
  auto block = std::make_unique<std::uint8_t[]>(n == 0 ? 1 : n);
  for (std::size_t i = 0; i < n; ++i) block[i] = input[i];
  Message m;
  const Error e = decode(std::span<const std::uint8_t>(block.get(), n), m);
  if (e == Error::none) {
    Bytes again;
    REQUIRE(encode(m, again) == Error::none);
    Message back;
    REQUIRE(decode(again, back) == Error::none);
    CHECK(back == m);
  }
}

}  // namespace

TEST_CASE("mutated ORM sample messages never crash the decoder") {
  std::mt19937 rng(0x0A11CEu);  // fixed seed: a failure is reproducible
  auto pick = [&rng](std::size_t below) {
    return std::uniform_int_distribution<std::size_t>(0, below - 1)(rng);
  };
  auto byte = [&rng] {
    return static_cast<std::uint8_t>(std::uniform_int_distribution<int>(0, 255)(rng));
  };
  std::vector<Bytes> seeds;
  for (const Message& m : test::orm_messages()) seeds.push_back(must_encode(m));
  REQUIRE_FALSE(seeds.empty());
  for (int i = 0; i < 3000; ++i) {
    Bytes b = seeds[pick(seeds.size())];
    const std::size_t edits = 1 + pick(4);
    for (std::size_t k = 0; k < edits; ++k) {
      switch (pick(4)) {
        case 0:  // flip a byte
          if (!b.empty()) b[pick(b.size())] = byte();
          break;
        case 1:  // cut the end off
          if (!b.empty()) b.resize(pick(b.size()));
          break;
        case 2:  // insert a byte
          b.insert(b.begin() + static_cast<std::ptrdiff_t>(pick(b.size() + 1)), byte());
          break;
        case 3:  // keep the header length honest so the body gets parsed
          if (b.size() >= kHeaderSize) {
            const std::size_t len = b.size() - kHeaderSize;
            b[2] = static_cast<std::uint8_t>(len);
            b[3] = static_cast<std::uint8_t>(len >> 8);
          }
          break;
      }
    }
    decode_exact(b);
  }
}
