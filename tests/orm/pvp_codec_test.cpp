// ORM orm/pvp message formats (coop/protocol/orm_messages.hpp): ROOM_SWITCH 248,
// PLAYER_HIT 250, PLAYER_CALL 251, PLAYER_RENAME 252: numbers, channel,
// senders, bytes, round trips and refusals.
#include <doctest/doctest.h>

#include <ostream>

#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/messages.hpp"

using namespace coop::protocol;

namespace {

std::vector<std::uint8_t> Bytes(const Message& m) {
  std::vector<std::uint8_t> out;
  REQUIRE(encode(m, out) == Error::none);
  return out;
}

Message Back(const std::vector<std::uint8_t>& bytes) {
  Message m;
  REQUIRE(decode(bytes, m) == Error::none);
  return m;
}

PlayerHit AHit() {
  PlayerHit h;
  h.victim = 3;
  h.hit_id = 0x01020304;
  h.weapon = HitWeapon::arrow;
  h.damage = 4;
  h.flags = player_hit::pull;
  h.from = {1.0f, 2.0f, 3.0f};
  h.push = {0.0f, 0.0f, -2.5f};
  return h;
}

}  // namespace

TEST_CASE("pvp codec: the four numbers, the session channel, both senders") {
  CHECK(static_cast<int>(MessageType::room_switch) == 0xF008);
  CHECK(static_cast<int>(MessageType::player_hit) == 0xF00A);
  CHECK(static_cast<int>(MessageType::player_call) == 0xF00B);
  CHECK(static_cast<int>(MessageType::player_rename) == 0xF00C);
  for (const MessageType t : {MessageType::room_switch, MessageType::player_hit, MessageType::player_call,
                              MessageType::player_rename}) {
    CHECK(channel_of(t) == kChannelSession);
    CHECK_FALSE(allowed_on_channel(t, kChannelProgress));
    CHECK(may_send(Sender::client, t));
    CHECK(may_send(Sender::server, t));
  }
}

TEST_CASE("pvp codec: ROOM_SWITCH bytes and round trip") {
  RoomSwitch m;
  m.player_number = 2;
  m.server_time_us = 0x1122334455667788ull;
  m.entries = {{1, true}, {2, false}};
  const auto b = Bytes(m);
  // type 248 (u16), length 14, player, time (8), count, key, on, key, on
  const std::vector<std::uint8_t> want{0x08, 0xF0, 14, 0, 2, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 2, 1, 1, 2, 0};
  CHECK(b == want);
  CHECK(std::get<RoomSwitch>(Back(b)) == m);
  std::vector<std::uint8_t> out;
  RoomSwitch empty;
  CHECK(encode(empty, out) == Error::bad_value);  // at least one
  RoomSwitch twice;
  twice.entries = {{2, true}, {1, true}};
  CHECK(encode(twice, out) == Error::bad_value);  // keys increasing
  RoomSwitch high;
  high.entries = {{32, true}};
  CHECK(encode(high, out) == Error::bad_value);
  auto bad_on = b;
  bad_on.back() = 2;
  Message m2;
  CHECK(decode(bad_on, m2) == Error::bad_value);
  // A client sends 0 as the player; the server's has a player.
  CHECK(check_received(RoomSwitch{0, 0, {{1, true}}}, Sender::client, kChannelSession) == Error::none);
  CHECK(check_received(RoomSwitch{2, 0, {{1, true}}}, Sender::client, kChannelSession) == Error::wrong_sender);
}

TEST_CASE("pvp codec: PLAYER_HIT bytes, round trip and refusals") {
  const PlayerHit h = AHit();
  const auto b = Bytes(h);
  CHECK(b.size() == 4 + 9 + 24);
  CHECK(b[4] == 0);   // player: 0 from a client
  CHECK(b[5] == 3);   // victim
  CHECK(b[6] == 4);   // hit id, little-endian
  CHECK(b[10] == 2);  // weapon arrow
  CHECK(b[11] == 4);  // damage
  CHECK(b[12] == 1);  // flags
  CHECK(std::get<PlayerHit>(Back(b)) == h);
  std::vector<std::uint8_t> out;
  PlayerHit x = h;
  x.hit_id = 0;
  CHECK(encode(x, out) == Error::bad_value);
  x = h;
  x.victim = 0;
  CHECK(encode(x, out) == Error::bad_value);
  x = h;
  x.weapon = static_cast<HitWeapon>(11);
  CHECK(encode(x, out) == Error::bad_value);
  x = h;
  x.damage = 81;
  CHECK(encode(x, out) == Error::bad_value);
  x = h;
  x.push = {3.0f, 0.0f, 1.0f};  // over 3 m
  CHECK(encode(x, out) == Error::bad_value);
  x = h;
  x.from[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(encode(x, out) == Error::bad_value);
  // The server never relays a player's hit on himself.
  Message self = h;
  CHECK_FALSE(set_sender(self, 3));
  CHECK(set_sender(self, 1));
  CHECK(std::get<PlayerHit>(self).player_number == 1);
}

TEST_CASE("pvp codec: PLAYER_CALL come and gather") {
  PlayerCall come{0, 4, CallKind::come, 9};
  CHECK(std::get<PlayerCall>(Back(Bytes(come))) == come);
  PlayerCall gather{0, 0, CallKind::gather, 10};
  CHECK(std::get<PlayerCall>(Back(Bytes(gather))) == gather);
  std::vector<std::uint8_t> out;
  CHECK(encode(PlayerCall{0, 0, CallKind::come, 1}, out) == Error::bad_value);    // come needs a mate
  CHECK(encode(PlayerCall{0, 2, CallKind::gather, 1}, out) == Error::bad_value);  // gather is to all
  CHECK(encode(PlayerCall{0, 7, CallKind::come, 1}, out) == Error::bad_value);
  Message to_self = come;
  CHECK_FALSE(set_sender(to_self, 4));
}

TEST_CASE("pvp codec: PLAYER_RENAME follows the app's name rule") {
  PlayerRename r{0, RenameResult::ok, "Anju-PC"};
  const auto b = Bytes(r);
  const std::vector<std::uint8_t> want{0x0C, 0xF0, 10, 0, 0, 0, 7, 'A', 'n', 'j', 'u', '-', 'P', 'C'};
  CHECK(b == want);
  CHECK(std::get<PlayerRename>(Back(b)) == r);
  PlayerRename taken{2, RenameResult::name_taken, "Saria"};
  CHECK(std::get<PlayerRename>(Back(Bytes(taken))) == taken);
  std::vector<std::uint8_t> out;
  CHECK(encode(PlayerRename{0, RenameResult::ok, ""}, out) == Error::bad_value);
  CHECK(encode(PlayerRename{0, RenameResult::ok, "dos palabras"}, out) == Error::bad_value);
  CHECK(encode(PlayerRename{0, RenameResult::ok, "abcdefghijklmnopq"}, out) == Error::bad_value);
  CHECK(encode(PlayerRename{0, static_cast<RenameResult>(3), "Ana"}, out) == Error::bad_value);
  CHECK(valid_new_nickname("a.b_c-9"));
  CHECK_FALSE(valid_new_nickname("Mart\xc3\xadn"));
}
