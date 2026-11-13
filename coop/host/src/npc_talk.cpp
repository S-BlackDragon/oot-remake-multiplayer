// ORM: the townsperson one talks to is claimed by the talker's game while the talk lasts. The rules
//   are described in coop/host/npc_talk.hpp.
// Doc: oot-remake docs/NPC_COMPARTIDOS.md (D-R12), docs/CHARLAS.md (charla-radio).
// Switch: LICHII_COOP_NPC (on in the kit since 0.6).
// Test: tests/host/npc_talk_test.cpp.

#include "coop/host/npc_talk.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

#include "addresses.hpp"

namespace coop::host {

namespace {

bool ends_with(std::string_view s, std::string_view tail) {
  return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return std::sqrt(x * x + y * y + z * z);
}

bool finite(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

}  // namespace

bool is_talker(std::string_view name) {
  if (!name.starts_with("Npc_")) return false;
  for (std::string_view own : {"Npc_Navi", "Npc_Equipment", "Npc_Invisible", "Npc_Fairy"}) {
    if (name.starts_with(own)) return false;
  }
  return !ends_with(name, "_Voice");
}

std::optional<std::string> stand_in_body(std::string_view name) {
  constexpr std::string_view kPrefix = "Npc_Invisible_";
  if (!name.starts_with(kPrefix) || name.size() == kPrefix.size() ||
      name == "Npc_Invisible_Base") {
    return std::nullopt;
  }
  std::string body = "Npc_";
  body += name.substr(kPrefix.size());
  return body;
}

float talk_reach_m(std::string_view name) {
  // Prefixes of Pack/Actor names (2026-11-11). Darunia's has the '_' so that
  // his son (Npc_Goron_DaruniaJr_Adult, Link's size) keeps kReachM.
  struct Reach {
    std::string_view prefix;
    float m;
  };
  static constexpr std::array<Reach, 8> kReaches{{
      {"Npc_Goron_Biggoron", NpcTalk::kMaxReachM},  // from the ledge, 10 m from him
      {"Npc_JabuJabu", NpcTalk::kMaxReachM},        // the whale-sized fish
      {"Npc_Kaepora", 15.0f},                       // on a perch 5.5 m above Link
      {"Npc_Zora_KingZora", 15.0f},                 // on his throne
      {"Npc_GreatFairy", 15.0f},                    // above her fountain
      {"Npc_Goron_Medigoron", 10.0f},
      {"Npc_Goron_Darunia_", 8.0f},
      {"Npc_DekuTreeChild", 8.0f},
  }};
  for (const Reach& r : kReaches) {
    if (name.starts_with(r.prefix)) return r.m;
  }
  return NpcTalk::kReachM;
}

bool talk_event_name(std::string_view event_name) { return event_name.starts_with("Npc_"); }

std::optional<std::uint32_t> read_event_phase(GameMemory& memory, std::uint64_t actor) {
  if (actor == 0) return std::nullopt;
  const auto table = read_ptr(memory, actor + addr::kActorComponents);
  const auto component = table ? read_ptr(memory, *table + addr::kComponentsEvent) : std::nullopt;
  return component ? read_u32(memory, *component + addr::kEventComponentPhase) : std::nullopt;
}

const char* to_string(NpcTalk::Found f) {
  switch (f) {
    case NpcTalk::Found::none: return "none";
    case NpcTalk::Found::game: return "by its event";
    case NpcTalk::Found::stand_in: return "by its stand-in's event";
    case NpcTalk::Found::by_reach: return "within reach";
  }
  return "?";
}

std::string talk_check_line(std::string_view name, NpcTalk::Found found,
                            std::string_view stand_in, float distance_m) {
  std::string line = "check: talk target ";
  line += name;
  const auto metres = [](float m) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", std::isfinite(m) ? m : 0.0f);
    return std::string(buf);
  };
  switch (found) {
    case NpcTalk::Found::game:
      line += " by event-component";
      break;
    case NpcTalk::Found::stand_in:
      line += " by event-component (stand-in ";
      line += stand_in;
      line += ")";
      break;
    case NpcTalk::Found::by_reach:
    case NpcTalk::Found::none:
      line += " by radius " + metres(talk_reach_m(name)) +
              " m (event component did not change; " + metres(distance_m) + " m away)";
      break;
  }
  return line;
}

void NpcTalk::reset() {
  event_ = false;
  event_since_ = 0;
  ended_at_ = 0;
  partner_.reset();
  partner_changed_ = false;
  found_ = Found::none;
  stand_in_.clear();
  distance_m_ = 0.0f;
  event_name_.reset();
  refused_ = false;
  refused_told_ = false;
  refused_name_.clear();
  rest_.clear();
  rest_phase_.clear();
}

void NpcTalk::update(bool event, const std::optional<protocol::Vec3>& link,
                     const std::vector<NpcSeen>& seen, std::uint64_t now_us) {
  const NpcSeen* partner_seen = nullptr;
  for (const NpcSeen& s : seen) {
    if (partner_ && s.address == *partner_) partner_seen = &s;
  }
  // A partner that left the walk is gone.
  if (partner_ && partner_seen == nullptr) {
    partner_.reset();
    partner_changed_ = false;
    found_ = Found::none;
  }

  if (!event) {
    if (event_) {
      ended_at_ = now_us == 0 ? 1 : now_us;  // the event just ended
    }
    event_ = false;
    if (partner_ && ended_at_ != 0 && now_us - ended_at_ >= kAfterUs) {
      partner_.reset();
      partner_changed_ = false;
      found_ = Found::none;
    }
    if (!partner_) {
      ended_at_ = 0;
      // At rest: what each townsperson plays, and its event component's
      // phase, are the baseline of the next talk.
      std::map<std::uint64_t, std::string> rest;
      std::map<std::uint64_t, std::uint32_t> rest_phase;
      for (const NpcSeen& s : seen) {
        const bool talker = is_talker(s.name);
        if (talker && s.animation) rest.emplace(s.address, *s.animation);
        if ((talker || stand_in_body(s.name)) && s.event_phase) {
          rest_phase.emplace(s.address, *s.event_phase);
        }
      }
      rest_ = std::move(rest);
      rest_phase_ = std::move(rest_phase);
    }
    return;
  }

  if (!event_) {
    event_ = true;
    event_since_ = now_us;
    ended_at_ = 0;
    refused_ = false;
    refused_told_ = false;
    refused_name_.clear();
    if (partner_) {
      // A new event while the last one's claim lingers: start afresh.
      partner_.reset();
      partner_changed_ = false;
      found_ = Found::none;
    }
  }
  const bool settling = now_us - event_since_ < kSettleUs;
  // A partner is chosen only in the event's first part; once one the game
  // named is chosen, or that part is over, it stays. One the game names wins
  // over one picked by reach, even one whose animation changed.
  if (!settling || found_ == Found::game || found_ == Found::stand_in) return;
  if (!link || !finite(*link)) return;

  // The body a stand-in speaks for: the nearest talker of that name to it.
  const auto body_of = [&seen](const NpcSeen& stand_in) -> const NpcSeen* {
    const auto body = stand_in_body(stand_in.name);
    if (!body || !finite(stand_in.position)) return nullptr;
    const NpcSeen* best = nullptr;
    float best_d = 0.0f;
    for (const NpcSeen& s : seen) {
      if (!is_talker(s.name) || !s.name.starts_with(*body) || !finite(s.position)) continue;
      const float d = distance(s.position, stand_in.position);
      if (!(d <= kStandInBodyM)) continue;
      if (best == nullptr || d < best_d || (d == best_d && s.address < best->address)) {
        best = &s;
        best_d = d;
      }
    }
    return best;
  };

  // The game's own word: a townsperson, or a stand-in's body, whose event
  // component left its rest value (one not seen at rest counts when its
  // phase is not 0). The nearest to our Link (a stand-in's distance is its
  // own: it stands where Link talks), the lower address on a tie.
  {
    const NpcSeen* best = nullptr;
    const NpcSeen* best_from = nullptr;  // whose component moved
    Found best_how = Found::none;
    float best_d = 0.0f;
    for (const NpcSeen& s : seen) {
      if (!s.event_phase || !finite(s.position)) continue;
      const auto rest = rest_phase_.find(s.address);
      const bool moved =
          rest != rest_phase_.end() ? *s.event_phase != rest->second : *s.event_phase != 0;
      if (!moved) continue;
      const NpcSeen* who = nullptr;
      Found how = Found::none;
      if (is_talker(s.name)) {
        who = &s;
        how = Found::game;
      } else if (const NpcSeen* body = body_of(s)) {
        who = body;
        how = Found::stand_in;
      }
      if (who == nullptr) continue;
      const float d = distance(s.position, *link);
      if (!(d <= kGameReachM)) continue;
      if (best == nullptr || d < best_d || (d == best_d && who->address < best->address)) {
        best = who;
        best_from = &s;
        best_how = how;
        best_d = d;
      }
    }
    if (best != nullptr) {
      partner_ = best->address;
      partner_changed_ = true;
      found_ = best_how;
      stand_in_ = best_how == Found::stand_in ? best_from->name : std::string();
      distance_m_ = best_d;
      return;
    }
  }
  // Once one whose animation changed is chosen, it stays (unless the game
  // names one, above).
  if (partner_ && partner_changed_) return;
  // An event that is no talk (a fall-down, a boot, the harness's place): nobody by reach (c3).
  if (event_name_ && !talk_event_name(*event_name_)) {
    if (!refused_) {
      refused_ = true;
      refused_name_ = *event_name_;
    }
    return;
  }

  // The fallback: the townspeople within their own reach (talk_reach_m), and
  // the bodies of the stand-ins within kReachM. One whose animation changed
  // since before the event first; then one within kReachM (a big one's longer
  // reach never takes a talk from a townsperson beside us); then the nearest;
  // then the lower address (the choice does not depend on the walk's order).
  struct Pick {
    const NpcSeen* who = nullptr;
    bool changed = false;
    bool outside = false;  // beyond kReachM
    float d = 0.0f;
  };
  const auto better = [](const Pick& a, const Pick& b) {  // a better than b
    if (b.who == nullptr) return true;
    if (a.changed != b.changed) return a.changed;
    if (a.outside != b.outside) return !a.outside;
    if (a.d != b.d) return a.d < b.d;
    return a.who->address < b.who->address;
  };
  Pick best;
  for (const NpcSeen& s : seen) {
    if (!finite(s.position)) continue;
    Pick p;
    p.d = distance(s.position, *link);
    if (is_talker(s.name)) {
      if (!(p.d <= talk_reach_m(s.name))) continue;
      p.who = &s;
    } else if (stand_in_body(s.name)) {
      if (!(p.d <= kReachM)) continue;
      p.who = body_of(s);
      if (p.who == nullptr) continue;
    } else {
      continue;
    }
    const auto rest = rest_.find(p.who->address);
    p.changed = rest != rest_.end() && p.who->animation && *p.who->animation != rest->second;
    p.outside = p.d > kReachM;
    if (better(p, best)) best = p;
  }
  if (best.who == nullptr) return;
  if (!partner_ || best.changed) {
    partner_ = best.who->address;
    partner_changed_ = best.changed;
    found_ = Found::by_reach;
    stand_in_.clear();
    distance_m_ = best.d;
  }
}

bool owner_talks(const NpcOwner& owner, const protocol::Vec3& at, std::string_view name) {
  if (owner.player == 0) return false;
  if (owner.posing) return true;
  const float reach =
      name.empty() ? NpcClaim::kOwnerReachM
                   : talk_reach_m(name) + (NpcClaim::kOwnerReachM - NpcTalk::kReachM);
  return owner.in_event && owner.link && finite(*owner.link) && finite(at) &&
         distance(*owner.link, at) <= reach;
}

std::uint64_t NpcClaim::claim_max_us(std::string_view name) {
  return name.find("_Shop") != std::string_view::npos ? kShopClaimMaxUs : kClaimMaxUs;
}

void NpcClaim::see_status(std::uint8_t player, bool in_event, std::uint64_t now_us) {
  std::uint64_t& since = event_since_[player];
  if (!in_event) {
    since = 0;
  } else if (since == 0) {
    since = now_us == 0 ? 1 : now_us;
  }
}

std::uint64_t NpcClaim::event_since(std::uint8_t player) const {
  const auto it = event_since_.find(player);
  return it != event_since_.end() ? it->second : 0;
}

void NpcClaim::reset() {
  state_ = State::none;
  partner_.reset();
  claimed_since_ = 0;
  owner_stuck_ = false;
  event_since_.clear();
}

NpcClaim::State NpcClaim::update(std::optional<std::uint64_t> partner, std::string_view name,
                                 const protocol::Vec3& at, std::uint8_t self,
                                 const NpcOwner& owner, std::uint64_t now_us) {
  if (!partner) {
    state_ = State::none;
    partner_.reset();
    claimed_since_ = 0;
    owner_stuck_ = false;
    return state_;
  }
  if (partner_ != partner) {
    // A new talk (or another partner): decided afresh.
    partner_ = partner;
    state_ = State::none;
    claimed_since_ = 0;
    owner_stuck_ = false;
  }
  const std::uint64_t max_us = claim_max_us(name);
  if (state_ == State::timed_out) return state_;
  if (state_ == State::claimed && now_us - claimed_since_ >= max_us) {
    state_ = State::timed_out;
    return state_;
  }

  const bool ours_or_none = owner.player == 0 || owner.player == self;
  bool yield = false;
  if (!ours_or_none && owner_talks(owner, at, name)) {
    const std::uint64_t since = event_since(owner.player);
    owner_stuck_ = !owner.posing && since != 0 && now_us - since >= max_us;
    yield = !owner_stuck_;
  }
  // A yield holds for the whole talk, unless the townsperson became ours
  // (a hand-over already on its way when we yielded): then it is ours to keep.
  if (state_ == State::yielded && !(owner.player == self && self != 0)) return state_;
  if (yield) {
    state_ = State::yielded;
    claimed_since_ = 0;
    return state_;
  }
  if (state_ != State::claimed) {
    state_ = State::claimed;
    claimed_since_ = now_us == 0 ? 1 : now_us;
  }
  return state_;
}

const char* to_string(NpcClaim::State s) {
  switch (s) {
    case NpcClaim::State::none: return "none";
    case NpcClaim::State::claimed: return "claimed";
    case NpcClaim::State::yielded: return "yielded";
    case NpcClaim::State::timed_out: return "timed out";
  }
  return "?";
}

}  // namespace coop::host
