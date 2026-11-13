#pragma once

// ORM test10 (oot-remake docs/PIN_TEST10.md): since protocol 10 every session
// has a join password and every game offers a key share (docs/protocol.md
// "Encrypted traffic"); HELLO alone joins nobody. Our server tests (host
// choice, menus, interest, fuzz) join their players through here, as
// session_test.cpp's Harness does: HELLO with the game's key share, then the
// right JOIN_PROOF for the server's JOIN_CHALLENGE. The session itself still
// takes plain bytes (sealing is the transport's: EnetServer, orm_server).

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <doctest/doctest.h>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/join_password.hpp"
#include "coop/protocol/messages.hpp"
#include "coop/protocol/traffic_crypto.hpp"
#include "session.hpp"

namespace orm_test {

inline constexpr const char* kJoinPassword = "orm-test-pass";

// The config with kJoinPassword when it has none, and test10's owner hold
// after a start (SessionConfig::owner_hold_us) off, as session_test.cpp's
// Harness has it: these tests were written for the first lister owning at once.
inline coop::server::SessionConfig with_password(coop::server::SessionConfig config) {
  if (config.join_password.empty()) config.join_password = kJoinPassword;
  config.owner_hold_us = 0;
  return config;
}

inline const coop::protocol::KeyPair& game_keys() {
  static const coop::protocol::KeyPair keys = coop::protocol::KeyPair::from_secret({7, 7, 7});
  return keys;
}

// `hello` with the game's key share, as a protocol 10 game sends it.
inline coop::protocol::Hello keyed(coop::protocol::Hello hello) {
  hello.key_share = game_keys().share();
  return hello;
}

// HELLO from `id` and the right JOIN_PROOF. What the session asks for after
// the proof (WELCOME, the encryption, the room's state) is left in its
// actions for the caller's own collect(). The challenge itself is taken here.
inline void join(coop::server::Session& session, coop::server::ConnectionId id, const coop::protocol::Hello& hello,
                 std::uint64_t now, const char* password = kJoinPassword) {
  namespace proto = coop::protocol;
  std::vector<std::uint8_t> bytes;
  REQUIRE(proto::encode(proto::Message{keyed(hello)}, bytes) == proto::Error::none);
  session.received(id, proto::kChannelSession, bytes, now);
  std::optional<proto::JoinChallenge> challenge;
  for (coop::server::Action& a : session.take_actions()) {
    const auto* send = std::get_if<coop::server::SendAction>(&a);
    REQUIRE(send != nullptr);  // HELLO gets the challenge only
    REQUIRE(send->to == id);
    proto::Message m;
    REQUIRE(proto::decode(send->bytes, m) == proto::Error::none);
    const auto* c = std::get_if<proto::JoinChallenge>(&m);
    REQUIRE(c != nullptr);
    challenge = *c;
  }
  REQUIRE(challenge);
  REQUIRE(challenge->key_share);
  const proto::JoinProof proof{
      proto::join_proof(password, challenge->nonce, game_keys().share(), *challenge->key_share)};
  bytes.clear();
  REQUIRE(proto::encode(proto::Message{proof}, bytes) == proto::Error::none);
  session.received(id, proto::kChannelSession, bytes, now);
}

}  // namespace orm_test
