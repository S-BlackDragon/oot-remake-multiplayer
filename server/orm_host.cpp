// ORM (oot-remake) one host for the room: HOST_CHOICE (type 254) and the
// relay's choice of host (host_choice.hpp, oot-remake docs/PARTIDA_SALA.md,
// "Un solo anfitrion"). Members of Session, declared in session.hpp; kept out
// of session.cpp (oot-remake docs/ADOPTAR_UPSTREAM.md, section 3).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "coop/protocol/orm_status.hpp"
#include "session.hpp"

namespace coop::server {

using namespace coop::protocol;

namespace {

// At most one pick a second from a player (a pick is answered to everyone).
constexpr std::uint64_t kPickGapUs = 1'000'000;

std::uint16_t rounded_ms(std::optional<double> ms) {
  if (!ms || *ms < 0.0) return 0;
  const double r = std::round(*ms);
  if (r >= 65535.0) return 65535;
  return static_cast<std::uint16_t>(r < 1.0 ? 1.0 : r);  // 0 means "not measured"
}

const char* wish_text(HostWish wish) {
  switch (wish) {
    case HostWish::automatic: return "automatic";
    case HostWish::me: return "wants to host";
    case HostWish::never: return "never hosts";
  }
  return "unknown";
}

}  // namespace

std::optional<double> Session::round_trip_ms(std::uint8_t number) const {
  const auto id = connection_of(number);
  if (!id) return std::nullopt;
  return connections_.at(*id).link.rtt_ms();
}

std::vector<HostCandidate> Session::host_candidates(std::uint64_t now_us) const {
  std::vector<HostCandidate> all;
  for (const auto& [id, c] : connections_) {
    if (c.state != State::joined) continue;
    HostCandidate h;
    h.number = c.number;
    h.nickname = c.nickname;
    // In the world: a scene known, not loading (a scene change, the title
    // screen sends loading too: orm_status::at_title).
    h.in_world = !c.scene.empty() && (c.status & status::loading) == 0 &&
                 (c.status & orm_status::at_title) == 0;
    h.steady = c.link.steady(config_.host, c.joined_at, now_us);
    h.rtt_ms = c.link.rtt_ms();
    h.wish = c.host_wish;
    all.push_back(std::move(h));
  }
  return all;
}

HostChoice Session::host_told() const {
  HostChoice t;
  t.kind = HostChoiceKind::told;
  t.why = host_why_;
  t.by = host_by_;
  t.pinned = election_.pinned();
  if (host_) {
    const Connection& h = connections_.at(*host_);
    t.host = h.number;
    t.name = h.nickname;
  }
  t.rtt_ms = rounded_ms(host_score_ms_);
  return t;
}

void Session::host_on_join(ConnectionId id, std::uint64_t now_us) {
  if (!host_) {
    host_ = id;
    host_why_ = HostWhy::first;
    host_by_ = 0;
    host_score_ms_.reset();
    election_.moved(now_us);  // the cooldown counts from the room's start
  }
  send(id, kChannelProgress, Host{connections_.at(*host_).number});
  send(id, kChannelSession, host_told());
}

void Session::host_on_leave(std::uint64_t now_us) {
  const std::vector<HostCandidate> remaining = host_candidates(now_us);
  if (remaining.empty()) return;  // remove_player empties the room instead
  set_host(election_.after_leave(remaining), now_us);
}

void Session::set_host(const HostMove& move, std::uint64_t now_us) {
  const auto next = connection_of(move.to);
  if (!next) return;
  host_ = *next;
  host_why_ = move.why;
  host_by_ = move.by;
  host_score_ms_ = move.score_ms;
  election_.moved(now_us);
  switch (move.why) {
    case HostWhy::left: ++stats_.host_left; break;
    case HostWhy::measured: ++stats_.host_measured; break;
    case HostWhy::unfit: ++stats_.host_unfit; break;
    case HostWhy::picked: ++stats_.host_picked; break;
    case HostWhy::wished: ++stats_.host_wished; break;
    default: break;
  }
  // HOST first, on the progress channel, as upstream: the new host learns it
  // hosts before any FLAG_SET relayed to it, and the old one that it no
  // longer does (its client sends what is unanswered to the new host). Every
  // shared value stays in the server's table; the new host's client sends its
  // whole table (FLAG_SNAPSHOT) and the server's value wins on a clash.
  const Message host{Host{move.to}};
  const Message told{host_told()};
  for (const auto& [to, c] : connections_) {
    if (c.state != State::joined) continue;
    send(to, kChannelProgress, host);
    send(to, kChannelSession, told);
  }
  const Connection& h = connections_.at(*host_);
  std::string line = "player " + std::to_string(h.number) + " hosts now: " + h.nickname + " (" +
                     to_string(move.why);
  if (move.why == HostWhy::picked && move.by != 0) line += " by player " + std::to_string(move.by);
  if (move.score_ms) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "; %.0f ms to the others on average", *move.score_ms);
    line += buf;
  }
  if (!move.note.empty()) line += "; " + move.note;
  log(line + ")");
}

void Session::run_host_choice(std::uint64_t now_us) {
  if (!host_) return;
  const std::vector<HostCandidate> all = host_candidates(now_us);
  if (all.size() < 2) return;  // alone: it hosts
  const std::uint8_t number = connections_.at(*host_).number;
  if (auto move = election_.check(number, all, now_us)) set_host(*move, now_us);
}

// HOST_CHOICE from a player: its wish (kept; the choice uses it at the next
// check), or a pick (taken at once when the player picked is in the world,
// else kept until it is). The answer to a pick that cannot be taken goes to
// the picker alone; a taken one is told to everyone (with the next HOST, or
// at once when the host stays the same).
void Session::on_host_choice(ConnectionId id, Connection& c, const HostChoice& m, std::uint64_t now_us) {
  switch (m.kind) {
    case HostChoiceKind::wish: {
      if (m.wish != HostWish::automatic && m.wish != HostWish::me && m.wish != HostWish::never) {
        ++stats_.host_choice_dropped;
        return;
      }
      ++stats_.host_wishes;
      if (c.host_wish != m.wish) {
        c.host_wish = m.wish;
        log("player " + std::to_string(c.number) + " " + c.nickname + ": " + wish_text(m.wish));
      }
      return;
    }
    case HostChoiceKind::pick: {
      if (c.host_pick_at != 0 && now_us - c.host_pick_at < kPickGapUs) {
        ++stats_.host_choice_dropped;
        return;
      }
      c.host_pick_at = now_us == 0 ? 1 : now_us;
      const std::vector<HostCandidate> all = host_candidates(now_us);
      const auto result = election_.pick(m.name, c.number, all);
      if (result == HostElection::PickResult::not_here || result == HostElection::PickResult::never) {
        ++stats_.host_picks_refused;
        HostChoice refused = host_told();
        refused.why = HostWhy::refused;
        refused.by = c.number;
        refused.name = m.name;
        refused.rtt_ms = 0;
        send(id, kChannelSession, refused);
        log("player " + std::to_string(c.number) + " picked " + m.name + " to host: refused (" +
            (result == HostElection::PickResult::never ? "that player never hosts" : "not in the room") + ")");
        return;
      }
      log("player " + std::to_string(c.number) + " " + c.nickname +
          (result == HostElection::PickResult::cleared ? " gave the host back to the measures"
                                                       : " picked " + election_.pinned() + " to host"));
      const std::uint8_t before = host_ ? connections_.at(*host_).number : 0;
      if (auto move = election_.check(before, all, now_us)) {
        set_host(*move, now_us);
        return;
      }
      // The host stays (the picked one hosts already, its game is not in the
      // world yet, or back to automatic): everyone learns the kept name.
      if (result == HostElection::PickResult::picked && host_ &&
          connections_.at(*host_).nickname == election_.pinned()) {
        host_why_ = HostWhy::picked;
        host_by_ = c.number;
      }
      const Message told{host_told()};
      for (const auto& [to, other] : connections_) {
        if (other.state == State::joined) send(to, kChannelSession, told);
      }
      return;
    }
    case HostChoiceKind::told:
      break;
  }
  ++stats_.host_choice_dropped;  // told comes from the server only; an unknown kind
}

}  // namespace coop::server
