// The server's GROUP_SETTINGS rules (server/orm_session.cpp, oot-remake docs/AJUSTES_GRUPO.md):
// relayed with the sender's number and kept for joiners, as PLAYER_LOOK.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "../server/orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;

namespace {

struct Sent {
  ConnectionId to;
  std::uint8_t channel;
  Message message;
};

// As tests/server/session_test.cpp's harness, cut to what these cases need.
struct Room {
  Session session{orm_test::with_password(SessionConfig{})};
  std::vector<Sent> sent;
  std::uint64_t now = 1'000'000;

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        CHECK(check_received(m, Sender::server, s->channel) == Error::none);
        sent.push_back({s->to, s->channel, m});
      }
    }
  }
  void receive(ConnectionId id, const Message& m, std::uint8_t channel) {
    std::vector<std::uint8_t> bytes;
    REQUIRE(encode(m, bytes) == Error::none);
    session.received(id, channel, bytes, now);
    collect();
  }
  void join(ConnectionId id, const std::string& nickname) {
    REQUIRE(session.connected(id, now, std::nullopt));
    collect();
    Hello h;
    h.build_id = "test 0.0";
    h.game_build_id = "e0f3ccfaf50065ae";
    h.game_version = "1.1.1";
    h.nickname = nickname;
    orm_test::join(session, id, h, now);
    collect();
  }
  std::vector<Sent> to(ConnectionId id) const {
    std::vector<Sent> out;
    for (const Sent& s : sent) {
      if (s.to == id) out.push_back(s);
    }
    return out;
  }
  std::vector<GroupSettings> settings_to(ConnectionId id) const {
    std::vector<GroupSettings> out;
    for (const Sent& s : to(id)) {
      if (const auto* g = std::get_if<GroupSettings>(&s.message)) out.push_back(*g);
    }
    return out;
  }
};

GroupSettings of(std::string downed) {
  return GroupSettings{0, {{"LICHII_COOP_DOWNED", std::move(downed)}, {"OptionData.IsUseAmiibo", "on"}}};
}

}  // namespace

TEST_CASE("group settings: relayed to the others with the sender's number, a repeat is not (ORM)") {
  Room r;
  r.join(1, "Link");
  r.join(2, "Zelda");
  r.join(3, "Saria");
  r.sent.clear();

  r.receive(1, of("on"), kChannelSession);
  CHECK(r.settings_to(1).empty());
  for (ConnectionId id : {2, 3}) {
    const auto got = r.settings_to(id);
    REQUIRE(got.size() == 1);
    CHECK(got[0].player_number == 1);
    CHECK(got[0].entries == of("on").entries);
  }
  CHECK(r.session.stats().settings_relayed == 1);

  r.sent.clear();
  r.receive(1, of("on"), kChannelSession);  // after a WELCOME, the same again
  CHECK(r.sent.empty());
  CHECK(r.session.stats().settings_repeats == 1);
  r.receive(1, of("off"), kChannelSession);
  CHECK(r.settings_to(2).size() == 1);
  CHECK(r.settings_to(3).size() == 1);
  CHECK(r.session.stats().settings_relayed == 2);
}

TEST_CASE("group settings: a joiner gets each player's last ones after its WELCOME (ORM)") {
  Room r;
  r.join(1, "Link");
  r.join(2, "Zelda");
  r.join(3, "Saria");
  r.receive(3, of("off"), kChannelSession);
  r.receive(1, of("off"), kChannelSession);
  r.receive(1, of("on"), kChannelSession);  // the last one counts
  r.sent.clear();

  r.join(4, "Darunia");
  const auto all = r.to(4);
  std::size_t welcome_at = all.size();
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (std::holds_alternative<Welcome>(all[i].message)) welcome_at = i;
  }
  REQUIRE(welcome_at < all.size());
  std::vector<GroupSettings> got;
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (const auto* g = std::get_if<GroupSettings>(&all[i].message)) {
      CHECK(i > welcome_at);
      CHECK(all[i].channel == kChannelSession);
      got.push_back(*g);
    }
  }
  REQUIRE(got.size() == 2);  // player 2 sent none
  CHECK(got[0].player_number == 1);
  CHECK(got[0].entries == of("on").entries);
  CHECK(got[1].player_number == 3);
  for (ConnectionId id : {1, 2, 3}) CHECK(r.settings_to(id).empty());
}

TEST_CASE("group settings: a leaver's are not kept, a posing or misplaced one is refused (ORM)") {
  Room r;
  r.join(1, "Link");
  r.join(2, "Zelda");
  r.receive(2, of("on"), kChannelSession);
  r.session.disconnected(2, r.now);
  r.collect();
  r.sent.clear();
  r.join(3, "Saria");
  CHECK(r.settings_to(3).empty());

  r.sent.clear();
  GroupSettings posing = of("on");
  posing.player_number = 3;  // as player 3
  r.receive(1, posing, kChannelSession);
  r.receive(1, of("on"), kChannelProgress);  // wrong channel
  CHECK(r.sent.empty());
  CHECK(r.session.stats().settings_relayed == 1);  // only Zelda's, before she left
}
