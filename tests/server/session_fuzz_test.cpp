// The relay session against random and mutated messages from joined players and from connections
// that have not sent HELLO (oot-remake docs/REVISION_RED.md). Fixed seeds: a failure is reproducible.
//
// Besides not crashing, two rules must hold for everything the session asks to send:
// - it decodes and is something a server may send on that channel (no player field 0 or past 6);
// - while handling one player's message, nothing it relays to another player names anyone but
//   that player in its player field (REVIVE, STORY_EVENT, PLAYER_LOOK, OBJECT_POSE and the rest:
//   no spoofing another player's number).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "../protocol/samples.hpp"
#include "../orm/orm_samples.hpp"
#include "coop/protocol/codec.hpp"
#include "orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;
using Bytes = std::vector<std::uint8_t>;

namespace {

struct FuzzHarness {
  Session session;
  std::uint64_t now = 1'000'000;
  std::uint64_t sends = 0;
  std::uint64_t bad_sends = 0;
  std::uint64_t spoofed = 0;

  explicit FuzzHarness(SessionConfig config) : session(orm_test::with_password(std::move(config))) {}

  // Takes the actions and checks every send; `from`/`number`: the player whose message caused
  // them (0: a tick).
  void collect(ConnectionId from = 0, std::uint8_t number = 0) {
    for (Action& a : session.take_actions()) {
      auto* s = std::get_if<SendAction>(&a);
      if (s == nullptr) continue;
      ++sends;
      Message m;
      if (decode(s->bytes, m) != Error::none ||
          check_received(m, Sender::server, s->channel) != Error::none) {
        ++bad_sends;
        continue;
      }
      if (from != 0 && s->to != from) {
        if (const auto field = sender_of(m); field && *field != number) ++spoofed;
      }
    }
  }
};

Hello hello_from(const std::string& nickname) {
  Hello h;
  h.build_id = "test 0.0";
  h.game_build_id = "e0f3ccfaf50065ae";
  h.game_version = "1.1.1";
  h.nickname = nickname;
  return h;
}

// Every sample message as a client would send it: its player field 0.
std::vector<std::pair<Bytes, std::uint8_t>> client_seeds() {
  std::vector<std::pair<Bytes, std::uint8_t>> out;
  std::vector<Message> samples = test::every_message();
  for (Message& m : test::orm_messages()) samples.push_back(std::move(m));  // ORM: ours are not in upstream's list
  for (Message m : samples) {
    std::visit(
        [](auto& v) {
          if constexpr (requires { v.player_number; }) v.player_number = kPlayerSelf;
        },
        m);
    Bytes b;
    if (encode(m, b) == Error::none) out.emplace_back(std::move(b), channel_of(m));
  }
  return out;
}

}  // namespace

TEST_CASE("session fuzz: random and mutated messages never crash it, never send a bad message, never spoof a sender") {
  SessionConfig config;
  config.silence_timeout_us = 3'600'000'000ull;  // nobody times out during the run
  config.reliable_per_second = 1'000'000;        // the rate limits would drop most of the run
  config.unreliable_per_second = 1'000'000;
  config.bytes_per_second = 1u << 30;
  config.object_pose_gap_us = 0;
  config.object_pose_one_us = 0;
  config.ocarina_note_per_second = 0;  // the Ocarina's own bucket off too
  FuzzHarness h(config);
  for (ConnectionId id = 1; id <= 6; ++id) {
    REQUIRE(h.session.connected(id, h.now));
    orm_test::join(h.session, id, hello_from("p" + std::to_string(id)), h.now);
    h.collect();
  }
  REQUIRE(h.session.player_count() == 6);
  REQUIRE(h.session.connected(7, h.now));  // stays waiting for HELLO
  h.collect();

  const auto seeds = client_seeds();
  REQUIRE(seeds.size() > 40);
  std::mt19937 rng(0x5E55u);
  auto pick = [&rng](std::size_t below) { return static_cast<std::size_t>(rng() % below); };

  for (int i = 0; i < 60'000; ++i) {
    Bytes b;
    std::uint8_t channel = 0;
    const std::size_t kind = pick(10);
    if (kind < 5) {  // a seed as it is, or with a few bytes changed (same length)
      const auto& seed = seeds[pick(seeds.size())];
      b = seed.first;
      channel = seed.second;
      if (kind >= 3) {
        for (std::size_t k = 0, n = 1 + pick(3); k < n && b.size() > kHeaderSize; ++k) {
          b[kHeaderSize + pick(b.size() - kHeaderSize)] = static_cast<std::uint8_t>(rng());
        }
      }
    } else if (kind < 8) {  // a seed cut short, grown, or with its length wrong
      b = seeds[pick(seeds.size())].first;
      channel = static_cast<std::uint8_t>(pick(kChannelCount + 2));
      switch (pick(3)) {
        case 0: b.resize(pick(b.size() + 1)); break;
        case 1: b.push_back(static_cast<std::uint8_t>(rng())); break;
        default:
          if (b.size() >= kHeaderSize) b[2] = static_cast<std::uint8_t>(rng());
          break;
      }
    } else {  // a random header (ORM's 0xF000 to 0xF00E half the time, protocol 110; else upstream's
              // numbers, up to 404 since test10) and body, honest length
      const std::uint16_t type = pick(2) == 0 ? static_cast<std::uint16_t>(0xF000 + pick(15))
                                              : static_cast<std::uint16_t>(pick(512));
      const std::size_t n = pick(120);
      b = {static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(type >> 8),
           static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8)};
      for (std::size_t k = 0; k < n; ++k) b.push_back(static_cast<std::uint8_t>(rng()));
      channel = static_cast<std::uint8_t>(pick(kChannelCount));
    }
    const ConnectionId from = static_cast<ConnectionId>(1 + pick(7));
    h.session.received(from, channel, std::span<const std::uint8_t>(b), h.now);
    const auto number = h.session.player_number(from);
    h.collect(from, number.value_or(0));
    h.now += pick(20'000);
    if (i % 50 == 0) {
      h.session.tick(h.now);
      h.collect();
    }
  }

  CHECK(h.sends > 10'000);  // the run reached the relay paths, not only the decoder
  CHECK(h.session.stats().relayed > 5'000);
  CHECK(h.bad_sends == 0);
  CHECK(h.spoofed == 0);
  CHECK(h.session.player_count() == 6);  // every player still in
  MESSAGE("sends " << h.sends << ", relayed " << h.session.stats().relayed << ", revives relayed "
                   << h.session.stats().revive_relayed << ", stories relayed "
                   << h.session.stats().story_relayed << ", looks relayed "
                   << h.session.stats().look_relayed << ", poses relayed "
                   << h.session.stats().object_pose_relayed);
}
