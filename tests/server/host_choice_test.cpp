// ORM one host for the room (server/host_choice.hpp, server/orm_host.cpp,
// oot-remake docs/PARTIDA_SALA.md "Un solo anfitrion"): the relay's choice by
// measures with hysteresis, by hand, by wishes, and the hand-over when the
// host leaves, with the shared state (flags, story events, objects) kept.
// Driven without a network, as menus_test.cpp.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_status.hpp"
#include "host_choice.hpp"
#include "orm_test_join.hpp"  // ORM test10: password and key share
#include "session.hpp"

using namespace coop::protocol;
using namespace coop::server;

namespace {

constexpr std::uint64_t kSec = 1'000'000;

// ------------------------------------------------------------ the pure choice

HostCandidate cand(std::uint8_t number, std::optional<double> rtt, bool fit = true,
                   HostWish wish = HostWish::automatic) {
  HostCandidate c;
  c.number = number;
  c.nickname = "P" + std::to_string(number);
  c.in_world = fit;
  c.steady = fit;
  c.rtt_ms = rtt;
  c.wish = wish;
  return c;
}

HostRules fast_rules() {
  HostRules r;
  r.better_hold_us = 10 * kSec;
  r.cooldown_us = 30 * kSec;
  r.unfit_hold_us = 5 * kSec;
  return r;
}

// Calls check every second from `from` for `seconds`; the first move, if any.
std::optional<std::pair<std::uint64_t, HostMove>> run_checks(HostElection& e, std::uint8_t host,
                                                            const std::vector<HostCandidate>& all,
                                                            std::uint64_t from, int seconds) {
  for (int s = 0; s <= seconds; ++s) {
    const std::uint64_t t = from + static_cast<std::uint64_t>(s) * kSec;
    if (auto m = e.check(host, all, t)) return std::make_pair(t, *m);
  }
  return std::nullopt;
}

// ------------------------------------------------------------ the session

struct Sent {
  ConnectionId to;
  std::uint8_t channel;
  Message message;
};

SessionConfig room_config(HostRules rules = fast_rules()) {
  SessionConfig config;
  config.silence_timeout_us = 1'000'000 * kSec;
  config.position_stale_us = 1'000'000 * kSec;
  config.host = rules;
  return config;
}

// Players 1..n ("P1".."Pn"), each joined with its number. run() ticks every
// 250 ms and answers every PING of a player with a round trip set in rtt_ms
// (none: it stays silent), so the relay measures each link.
struct Room {
  Session session;
  std::vector<Sent> sent;
  std::uint64_t now = 1'000'000;
  std::map<ConnectionId, double> rtt_ms;

  explicit Room(int players, SessionConfig config = room_config())
      : session(orm_test::with_password(std::move(config))) {
    for (int i = 1; i <= players; ++i) join(static_cast<ConnectionId>(i));
  }

  void collect() {
    for (Action& a : session.take_actions()) {
      if (auto* s = std::get_if<SendAction>(&a)) {
        Message m;
        REQUIRE(decode(s->bytes, m) == Error::none);
        sent.push_back({s->to, s->channel, m});
      }
    }
  }

  void receive(ConnectionId id, const Message& m, std::uint8_t channel, std::uint64_t at = 0) {
    std::vector<std::uint8_t> bytes;
    REQUIRE(encode(m, bytes) == Error::none);
    session.received(id, channel, bytes, at != 0 ? at : now);
    collect();
  }

  void join(ConnectionId id, std::string nickname = {}) {
    REQUIRE(session.connected(id, now, std::nullopt));
    Hello h;
    h.build_id = "test 0.0";
    h.game_build_id = "e0f3ccfaf50065ae";
    h.game_version = "1.1.1";
    h.nickname = nickname.empty() ? "P" + std::to_string(id) : std::move(nickname);
    orm_test::join(session, id, h, now);
    collect();
  }

  // Its game is in the world: a scene, no loading bit.
  void in_world(ConnectionId id, const std::string& scene = "Field") {
    ObjectPresent p;
    p.scene = scene;
    receive(id, p, kChannelOwnership);
    receive(id, PlayerStatus{0, 0}, kChannelSession);
  }

  void choice(ConnectionId id, HostChoice m) { receive(id, m, kChannelSession); }

  void run(std::uint64_t us) {
    for (std::uint64_t t = 0; t < us; t += 250'000) {
      now += 250'000;
      const std::size_t before = sent.size();
      session.tick(now);
      collect();
      // Answer this tick's PINGs.
      std::vector<std::pair<ConnectionId, std::uint64_t>> pings;
      for (std::size_t i = before; i < sent.size(); ++i) {
        if (const auto* p = std::get_if<Ping>(&sent[i].message)) pings.emplace_back(sent[i].to, p->sender_time_us);
      }
      for (const auto& [id, at] : pings) {
        const auto r = rtt_ms.find(id);
        if (r == rtt_ms.end()) continue;
        receive(id, Pong{at, 0}, kChannelSession, at + static_cast<std::uint64_t>(r->second * 1000.0));
      }
    }
  }

  template <typename T>
  std::vector<T> to(ConnectionId id) const {
    std::vector<T> out;
    for (const Sent& s : sent) {
      if (s.to != id) continue;
      if (const auto* m = std::get_if<T>(&s.message)) out.push_back(*m);
    }
    return out;
  }

  std::optional<HostChoice> last_told(ConnectionId id) const {
    const auto all = to<HostChoice>(id);
    if (all.empty()) return std::nullopt;
    return all.back();
  }
};

HostChoice wish(HostWish w) {
  HostChoice m;
  m.kind = HostChoiceKind::wish;
  m.wish = w;
  return m;
}

HostChoice pick(std::string name) {
  HostChoice m;
  m.kind = HostChoiceKind::pick;
  m.name = std::move(name);
  return m;
}

}  // namespace

// ============================================================ LinkMeasure

TEST_CASE("host choice: a PONG counts only for one of the relay's own PINGs, once") {
  LinkMeasure l;
  CHECK_FALSE(l.rtt_ms());
  l.ping_sent(1'000'000);
  CHECK_FALSE(l.pong(999'000, 1'040'000, kSec));     // never sent: a game making its link look faster
  CHECK(l.pong(1'000'000, 1'040'000, kSec));
  CHECK(*l.rtt_ms() == doctest::Approx(40.0));
  CHECK_FALSE(l.pong(1'000'000, 1'050'000, kSec));   // the same one again
  CHECK_FALSE(l.pong(2'000'000, 1'050'000, kSec));   // from the future
  CHECK(l.samples() == 1);
}

TEST_CASE("host choice: a link is steady after enough round trips, small jitter and no long silence") {
  HostRules rules;
  LinkMeasure l;
  std::uint64_t t = kSec;
  const std::uint64_t joined = t;
  for (int i = 0; i < 100; ++i) {  // 25 s of PINGs every 250 ms, 30 ms each
    l.ping_sent(t);
    REQUIRE(l.pong(t, t + 30'000, rules.max_gap_us));
    t += 250'000;
  }
  CHECK(l.steady(rules, joined, t));
  CHECK(*l.rtt_ms() == doctest::Approx(30.0));
  CHECK_FALSE(l.steady(rules, t - kSec, t));  // joined 1 s ago: still warming up
  // 2 s of silence: unsteady now, and for stable_window_us after it.
  t += 2 * kSec;
  CHECK_FALSE(l.steady(rules, joined, t));
  l.ping_sent(t);
  REQUIRE(l.pong(t, t + 30'000, rules.max_gap_us));
  CHECK(l.long_gap_at_us() == t + 30'000);
  CHECK_FALSE(l.steady(rules, joined, t + 31'000));
  for (int i = 0; i < 130; ++i) {  // 32 s more of good round trips
    t += 250'000;
    l.ping_sent(t);
    REQUIRE(l.pong(t, t + 30'000, rules.max_gap_us));
  }
  CHECK(l.steady(rules, joined, t + 31'000));
}

TEST_CASE("host choice: a jittery link is not steady") {
  HostRules rules;
  LinkMeasure l;
  std::uint64_t t = kSec;
  for (int i = 0; i < 100; ++i) {
    l.ping_sent(t);
    REQUIRE(l.pong(t, t + (i % 2 == 0 ? 20'000 : 180'000), rules.max_gap_us));
    t += 250'000;
  }
  CHECK(l.jitter_ms() > rules.max_jitter_ms);
  CHECK_FALSE(l.steady(rules, kSec, t));
}

// ============================================================ the score

TEST_CASE("host choice: the score is the average round trip to the others through the relay") {
  const std::vector<HostCandidate> all{cand(1, 100.0), cand(2, 20.0), cand(3, 40.0)};
  CHECK(*HostElection::score(all[0], all) == doctest::Approx(130.0));  // 100 + (20 + 40) / 2
  CHECK(*HostElection::score(all[1], all) == doctest::Approx(90.0));   // 20 + (100 + 40) / 2
  CHECK(*HostElection::score(all[2], all) == doctest::Approx(100.0));
  const std::vector<HostCandidate> one{cand(1, 15.0)};
  CHECK(*HostElection::score(one[0], one) == doctest::Approx(15.0));
  const std::vector<HostCandidate> unknown{cand(1, 15.0), cand(2, std::nullopt)};
  CHECK_FALSE(HostElection::score(unknown[0], unknown));
  CHECK_FALSE(HostElection::score(unknown[1], unknown));
}

// ============================================================ measured, with hysteresis

TEST_CASE("host choice: a clearly better link takes over after the hold, not before, not in the cooldown") {
  HostElection e(fast_rules());
  e.moved(kSec);  // the room started at 1 s: cooldown 30 s
  const std::vector<HostCandidate> all{cand(1, 150.0), cand(2, 20.0), cand(3, 30.0)};
  // Better from the first check, but nothing moves before cooldown + hold.
  const auto m = run_checks(e, 1, all, kSec, 60);
  REQUIRE(m);
  CHECK(m->second.to == 2);
  CHECK(m->second.why == HostWhy::measured);
  CHECK(m->first == 41 * kSec);  // cooldown ends at 31 s, then 10 s better in a row
  REQUIRE(m->second.score_ms);
  CHECK(*m->second.score_ms == doctest::Approx(110.0));  // 20 + (150 + 30) / 2
}

TEST_CASE("host choice: a slightly better link never takes over (the margin)") {
  HostElection e(fast_rules());
  // 2 is 15 ms better on average: under max(20 ms, 25 %).
  const std::vector<HostCandidate> all{cand(1, 40.0), cand(2, 25.0)};
  CHECK(*HostElection::score(all[0], all) - *HostElection::score(all[1], all) == doctest::Approx(0.0));
  // With two players both averages are the same: the host stays.
  CHECK_FALSE(run_checks(e, 1, all, kSec, 120));
  const std::vector<HostCandidate> three{cand(1, 60.0), cand(2, 45.0), cand(3, 50.0)};
  // 1: 60 + 47.5 = 107.5; 2: 45 + 55 = 100: 7.5 ms better, under the margin.
  CHECK_FALSE(run_checks(e, 1, three, kSec, 120));
}

TEST_CASE("host choice: a better link that comes and goes restarts the hold") {
  HostElection e(fast_rules());
  const std::vector<HostCandidate> better{cand(1, 150.0), cand(2, 20.0), cand(3, 30.0)};
  const std::vector<HostCandidate> same{cand(1, 30.0), cand(2, 20.0), cand(3, 30.0)};
  std::uint64_t t = kSec;
  for (int round = 0; round < 10; ++round) {  // 8 s better, 1 s not: never 10 s in a row
    for (int s = 0; s < 8; ++s, t += kSec) CHECK_FALSE(e.check(1, better, t));
    CHECK_FALSE(e.check(1, same, t));
    t += kSec;
  }
}

TEST_CASE("host choice: an unmeasured or unsteady player is never picked by measures") {
  HostElection e(fast_rules());
  const std::vector<HostCandidate> all{cand(1, 150.0), cand(2, 5.0, false), cand(3, 140.0)};
  CHECK_FALSE(run_checks(e, 1, all, kSec, 120));
  HostRules off = fast_rules();
  off.automatic = false;
  HostElection manual(off);
  const std::vector<HostCandidate> clear{cand(1, 150.0), cand(2, 5.0), cand(3, 10.0)};
  CHECK_FALSE(run_checks(manual, 1, clear, kSec, 120));
}

// ============================================================ unfit host

TEST_CASE("host choice: a host out of the world is replaced after the unfit hold") {
  HostElection e(fast_rules());
  e.moved(kSec);
  std::vector<HostCandidate> all{cand(1, 30.0), cand(2, 40.0), cand(3, 35.0)};
  all[0].in_world = false;  // on the title screen
  const auto m = run_checks(e, 1, all, 2 * kSec, 30);
  REQUIRE(m);
  CHECK(m->second.why == HostWhy::unfit);
  CHECK(m->second.to == 3);  // 35 + 35 = 70, better than 2's 40 + 32.5
  CHECK(m->first == 7 * kSec);  // 5 s after the first unfit check, cooldown or not
  // A short load (3 s) moves nothing.
  HostElection e2(fast_rules());
  std::vector<HostCandidate> loading = all;
  CHECK_FALSE(run_checks(e2, 1, loading, kSec, 3));
  loading[0].in_world = true;
  CHECK_FALSE(e2.check(1, loading, 5 * kSec));
  loading[0].in_world = false;
  CHECK_FALSE(run_checks(e2, 1, loading, 6 * kSec, 3));
}

TEST_CASE("host choice: with nobody fit the unfit host stays") {
  HostElection e(fast_rules());
  const std::vector<HostCandidate> all{cand(1, 30.0, false), cand(2, 40.0, false)};
  CHECK_FALSE(run_checks(e, 1, all, kSec, 60));
}

// ============================================================ wishes

TEST_CASE("host choice: a fit player who wishes to host takes over at once, the measures keep it") {
  HostElection e(fast_rules());
  e.moved(kSec);
  const std::vector<HostCandidate> all{cand(1, 20.0), cand(2, 200.0, true, HostWish::me), cand(3, 25.0)};
  const auto m = e.check(1, all, 2 * kSec);
  REQUIRE(m);
  CHECK(m->to == 2);
  CHECK(m->why == HostWhy::wished);
  e.moved(2 * kSec);
  // 1 and 3 are far better, but they do not wish it: 2 stays.
  CHECK_FALSE(run_checks(e, 2, all, 3 * kSec, 200));
  // Not fit yet: no move.
  HostElection e2(fast_rules());
  const std::vector<HostCandidate> warming{cand(1, 20.0), cand(2, 30.0, false, HostWish::me)};
  CHECK_FALSE(e2.check(1, warming, kSec));
}

TEST_CASE("host choice: a host who wishes never to host gives it to a fit player") {
  HostElection e(fast_rules());
  const std::vector<HostCandidate> all{cand(1, 20.0, true, HostWish::never), cand(2, 90.0), cand(3, 60.0)};
  const auto m = e.check(1, all, kSec);
  REQUIRE(m);
  CHECK(m->to == 3);
  CHECK(m->why == HostWhy::wished);
  // Never picked by the measures either.
  const std::vector<HostCandidate> fast_never{cand(1, 150.0), cand(2, 5.0, true, HostWish::never),
                                              cand(3, 140.0)};
  HostElection e2(fast_rules());
  CHECK_FALSE(run_checks(e2, 1, fast_never, kSec, 120));
}

// ============================================================ by hand

TEST_CASE("host choice: a pick is kept by name, waits for the game, and beats measures and wishes") {
  HostElection e(fast_rules());
  std::vector<HostCandidate> all{cand(1, 20.0), cand(2, 300.0), cand(3, 25.0, true, HostWish::me)};
  all[1].in_world = false;
  CHECK(e.pick("p2", 3, all) == HostElection::PickResult::picked);
  CHECK(e.pinned() == "P2");  // the player's own spelling
  CHECK(e.pinned_by() == 3);
  CHECK_FALSE(e.check(1, all, kSec));  // P2's game is not in the world: wait (and 3's wish waits too)
  all[1].in_world = true;
  const auto m = e.check(1, all, 2 * kSec);
  REQUIRE(m);
  CHECK(m->to == 2);
  CHECK(m->why == HostWhy::picked);
  CHECK(m->by == 3);
  e.moved(2 * kSec);
  CHECK_FALSE(run_checks(e, 2, all, 3 * kSec, 300));  // worse link, unsteady or not: it stays
  // Back to the measures.
  CHECK(e.pick("", 1, all) == HostElection::PickResult::cleared);
  CHECK(e.pinned().empty());
  const auto back = e.check(2, all, 400 * kSec);
  REQUIRE(back);
  CHECK(back->to == 3);  // 3's wish
}

TEST_CASE("host choice: a pick of nobody here or of a player who never hosts is refused") {
  HostElection e;
  const std::vector<HostCandidate> all{cand(1, 20.0), cand(2, 30.0, true, HostWish::never)};
  CHECK(e.pick("Saria", 1, all) == HostElection::PickResult::not_here);
  CHECK(e.pick("P2", 1, all) == HostElection::PickResult::never);
  CHECK(e.pinned().empty());
  CHECK(e.pick("  P1 ", 2, all) == HostElection::PickResult::picked);
  CHECK(e.pinned() == "P1");
}

// ============================================================ the host leaves

TEST_CASE("host choice: when the host leaves the best of the others takes over, not the lowest number") {
  HostElection e;
  const std::vector<HostCandidate> rest{cand(2, 80.0), cand(3, 20.0), cand(4, 30.0)};
  const HostMove m = e.after_leave(rest);
  CHECK(m.to == 3);
  CHECK(m.why == HostWhy::left);
  // Nothing measured: the lowest number, as upstream.
  const std::vector<HostCandidate> unmeasured{cand(4, std::nullopt, false), cand(2, std::nullopt, false)};
  CHECK(e.after_leave(unmeasured).to == 2);
  // A fit player before an unfit one; a wish before both; never last.
  const std::vector<HostCandidate> mixed{cand(2, 10.0, false), cand(3, 90.0, true),
                                         cand(4, 5.0, true, HostWish::never)};
  CHECK(e.after_leave(mixed).to == 3);
  const std::vector<HostCandidate> wished{cand(2, 10.0), cand(3, 90.0, false, HostWish::me)};
  CHECK(e.after_leave(wished).to == 3);
  const std::vector<HostCandidate> only_never{cand(5, 10.0, true, HostWish::never)};
  CHECK(e.after_leave(only_never).to == 5);  // someone always hosts
}

TEST_CASE("host choice: the picked player takes over when the host leaves, even before its game is in") {
  HostElection e;
  const std::vector<HostCandidate> all{cand(1, 20.0), cand(2, 30.0), cand(3, 99.0, false)};
  REQUIRE(e.pick("P3", 2, all) == HostElection::PickResult::picked);
  const std::vector<HostCandidate> rest{cand(2, 30.0), cand(3, 99.0, false)};
  const HostMove m = e.after_leave(rest);
  CHECK(m.to == 3);
  CHECK(m.why == HostWhy::picked);
  CHECK(m.by == 2);
}

// ============================================================ in the session

TEST_CASE("host session: the first player hosts and every joiner learns why") {
  Room r(1);
  REQUIRE(r.session.host() == 1);
  auto told = r.last_told(1);
  REQUIRE(told);
  CHECK(told->kind == HostChoiceKind::told);
  CHECK(told->why == HostWhy::first);
  CHECK(told->host == 1);
  CHECK(told->name == "P1");
  r.join(2);
  CHECK(r.to<Host>(2).size() == 1);
  CHECK(r.to<Host>(2)[0].player_number == 1);
  told = r.last_told(2);
  REQUIRE(told);
  CHECK(told->host == 1);
  CHECK(told->why == HostWhy::first);
}

TEST_CASE("host session: the relay moves the host to a clearly better link, and tells everyone") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.rtt_ms = {{1, 180.0}, {2, 15.0}, {3, 25.0}};
  r.sent.clear();
  r.run(35 * kSec);  // the cooldown (30 s from the start) is not over plus the hold: no move yet
  CHECK(r.session.host() == 1);
  r.run(20 * kSec);
  REQUIRE(r.session.host() == 2);
  CHECK(r.session.stats().host_measured == 1);
  for (ConnectionId id = 1; id <= 3; ++id) {
    const auto hosts = r.to<Host>(id);
    REQUIRE(hosts.size() == 1);
    CHECK(hosts[0].player_number == 2);
    const auto told = r.last_told(id);
    REQUIRE(told);
    CHECK(told->why == HostWhy::measured);
    CHECK(told->host == 2);
    CHECK(told->rtt_ms >= 110);  // 15 + (180 + 25) / 2 = 117.5, smoothed
    CHECK(told->rtt_ms <= 125);
  }
  REQUIRE(r.session.round_trip_ms(1));
  CHECK(*r.session.round_trip_ms(1) == doctest::Approx(180.0).epsilon(0.05));
  // Steady from then on: no more moves.
  r.sent.clear();
  r.run(300 * kSec);
  CHECK(r.session.host() == 2);
  CHECK(r.to<Host>(1).empty());
}

TEST_CASE("host session: the old host's FLAG_STATE is refused after a move, the new one answers FLAG_SETs") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.receive(1, FlagState{0, {FlagEntry{0x1234, 0, FlagKind::boolean, 1}}}, kChannelProgress);
  REQUIRE(r.session.flag_count() == 1);
  r.rtt_ms = {{1, 180.0}, {2, 15.0}, {3, 25.0}};
  r.run(60 * kSec);
  REQUIRE(r.session.host() == 2);
  const std::uint64_t refused = r.session.stats().flag_refused;
  r.receive(1, FlagState{0, {FlagEntry{0x1234, 0, FlagKind::boolean, 0}}}, kChannelProgress);
  CHECK(r.session.stats().flag_refused == refused + 1);
  r.sent.clear();
  r.receive(3, FlagSet{0, {FlagEntry{0x5678, 0, FlagKind::boolean, 1}}}, kChannelProgress);
  CHECK(r.to<FlagSet>(2).size() == 1);  // to the new host
  CHECK(r.to<FlagSet>(1).empty());
  // The new host's table: the server keeps its own value on a clash.
  r.sent.clear();
  r.receive(2, FlagSnapshot{0, 1, {FlagEntry{0x1234, 0, FlagKind::boolean, 0}}}, kChannelProgress);
  const auto kept = r.to<FlagState>(2);
  REQUIRE(kept.size() == 1);
  CHECK(kept[0].flags[0].value == 1);
  // A joiner gets the table as it was.
  r.sent.clear();
  r.join(4);
  bool has = false;
  for (const auto& part : r.to<FlagSnapshot>(4)) {
    for (const auto& e : part.flags) {
      if (e.key == FlagKey(0x1234, std::uint16_t{0}) && e.value == 1) has = true;
    }
  }
  CHECK(has);
}

TEST_CASE("host session: story events and objects stay as they were across a change of host") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  StoryEvent s;
  s.kind = StoryKind::start;
  s.scene = "KokiriForest_Child";
  s.name = "Meet_Saria";
  r.receive(1, s, kChannelSession);
  // An enemy player 1 owns (it listed it first).
  const ObjectId enemy{ObjectKind::placed, 0, 7};
  for (ConnectionId id = 1; id <= 3; ++id) {
    ObjectPresent p;
    p.scene = "Field";
    p.objects.push_back({enemy, 0});
    r.receive(id, p, kChannelOwnership);
  }
  REQUIRE(r.session.object_owner(enemy));
  const std::uint8_t owner = r.session.object_owner(enemy)->first;
  const std::uint16_t epoch = r.session.object_owner(enemy)->second;
  r.rtt_ms = {{1, 180.0}, {2, 15.0}, {3, 25.0}};
  r.run(60 * kSec);
  REQUIRE(r.session.host() == 2);
  CHECK(r.session.object_owner(enemy)->first == owner);  // the host does not own the enemies
  CHECK(r.session.object_owner(enemy)->second == epoch);
  r.sent.clear();
  r.join(4);
  const auto played = r.to<StoryEvent>(4);
  REQUIRE(played.size() == 1);
  CHECK(played[0].kind == StoryKind::played);
  CHECK(played[0].player_number == 1);
}

TEST_CASE("host session: a pick by hand moves the host at once, a bad pick is answered to the picker only") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.sent.clear();
  r.choice(3, pick("p2"));
  REQUIRE(r.session.host() == 2);
  CHECK(r.session.host_pinned() == "P2");
  CHECK(r.session.stats().host_picked == 1);
  for (ConnectionId id = 1; id <= 3; ++id) {
    const auto told = r.last_told(id);
    REQUIRE(told);
    CHECK(told->why == HostWhy::picked);
    CHECK(told->by == 3);
    CHECK(told->pinned == "P2");
  }
  // A clearly better link changes nothing while the pick holds.
  r.rtt_ms = {{1, 15.0}, {2, 200.0}, {3, 20.0}};
  r.run(200 * kSec);
  CHECK(r.session.host() == 2);
  // Nobody of that name: only the picker hears it.
  r.sent.clear();
  r.choice(1, pick("Saria"));
  CHECK(r.session.host() == 2);
  const auto refused = r.last_told(1);
  REQUIRE(refused);
  CHECK(refused->why == HostWhy::refused);
  CHECK(refused->name == "Saria");
  CHECK(r.to<HostChoice>(2).empty());
  CHECK(r.to<HostChoice>(3).empty());
  // At most one pick a second from a player.
  r.choice(1, pick("P3"));
  CHECK(r.session.host() == 2);
  CHECK(r.session.stats().host_choice_dropped == 1);
  r.now += kSec;
  r.choice(1, pick(""));  // back to the measures
  CHECK(r.session.host_pinned().empty());
  r.run(60 * kSec);
  CHECK(r.session.host() == 1);  // 15 + 110 against 2's 200 + 17.5
}

TEST_CASE("host session: the picked player gets the host back when it comes back in") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.choice(1, pick("P3"));
  REQUIRE(r.session.host() == 3);
  r.session.disconnected(3, r.now);
  r.collect();
  CHECK(r.session.host() == 1);  // the best of the others (nothing measured: the lowest number)
  CHECK(r.session.stats().host_left == 1);
  r.join(7, "P3");  // the same name again, on a new connection
  r.run(2 * kSec);
  CHECK(r.session.host() == 1);  // its game is not in the world yet
  r.in_world(7);
  r.run(2 * kSec);
  const auto number = r.session.player_number(7);
  REQUIRE(number);
  CHECK(r.session.host() == *number);
}

TEST_CASE("host session: wishes come by HOST_CHOICE, never-hosts is passed over when the host leaves") {
  Room r(3);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.choice(2, wish(HostWish::never));
  r.rtt_ms = {{1, 50.0}, {2, 5.0}, {3, 90.0}};
  r.run(25 * kSec);  // everyone measured and steady
  r.session.disconnected(1, r.now);
  r.collect();
  CHECK(r.session.host() == 3);  // 2 is much nearer but never hosts
  // A told from a client, or an unknown kind, is dropped.
  HostChoice told;
  told.kind = HostChoiceKind::told;
  r.choice(2, told);
  HostChoice odd;
  odd.kind = static_cast<HostChoiceKind>(9);
  r.choice(2, odd);
  CHECK(r.session.stats().host_choice_dropped == 2);
}

TEST_CASE("host session: a player who wishes to host takes over once its link is steady and its game in") {
  Room r(2);
  r.in_world(1);
  r.in_world(2);
  r.rtt_ms = {{1, 20.0}, {2, 60.0}};
  r.choice(2, wish(HostWish::me));
  r.run(5 * kSec);
  CHECK(r.session.host() == 1);  // still warming up (20 s, 40 round trips)
  r.run(20 * kSec);
  CHECK(r.session.host() == 2);
  CHECK(r.session.stats().host_wished == 1);
}

TEST_CASE("host session: a host on the title screen hands over after the unfit hold") {
  Room r(2);
  r.in_world(1);
  r.in_world(2);
  r.rtt_ms = {{1, 20.0}, {2, 60.0}};
  r.run(25 * kSec);
  REQUIRE(r.session.host() == 1);
  r.receive(1, PlayerStatus{0, static_cast<std::uint8_t>(status::loading | orm_status::at_title)}, kChannelSession);
  r.run(4 * kSec);
  CHECK(r.session.host() == 1);
  r.run(3 * kSec);
  CHECK(r.session.host() == 2);
  CHECK(r.session.stats().host_unfit == 1);
}

TEST_CASE("host session: with host_check_us 0 the host changes only when it leaves (upstream)") {
  SessionConfig config = room_config();
  config.host_check_us = 0;
  Room r(3, config);
  for (ConnectionId id = 1; id <= 3; ++id) r.in_world(id);
  r.rtt_ms = {{1, 180.0}, {2, 15.0}, {3, 25.0}};
  r.run(200 * kSec);
  CHECK(r.session.host() == 1);
  r.session.disconnected(1, r.now);
  r.collect();
  CHECK(r.session.host() == 2);  // measured: 2 is the best of 2 and 3
}
