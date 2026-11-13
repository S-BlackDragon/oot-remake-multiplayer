#include "coop/host/age.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "coop/host/gerudo.hpp"  // age_reload_keeps_spot
#include "coop/protocol/orm_status.hpp"

namespace coop::host {
namespace {

constexpr std::string_view kChild = "_Child";
constexpr std::string_view kAdult = "_Adult";

bool ends_with(std::string_view s, std::string_view tail) {
  return s.size() > tail.size() && s.substr(s.size() - tail.size()) == tail;
}

std::string text(const char* format, auto... args) {
  char buf[256];
  const int n = std::snprintf(buf, sizeof(buf), format, args...);
  if (n < 0) return std::string();
  if (static_cast<std::size_t>(n) < sizeof(buf)) return std::string(buf, static_cast<std::size_t>(n));
  std::string out(static_cast<std::size_t>(n), '\0');  // a long line: never cut
  std::snprintf(out.data(), out.size() + 1, format, args...);
  return out;
}

double seconds(std::uint64_t us) { return static_cast<double>(us) / 1e6; }

// Time from `since` to `now`; 0 when the clock reads before it (a start
// stamped as 1 because 0 means "none").
std::uint64_t elapsed(std::uint64_t now, std::uint64_t since) { return now > since ? now - since : 0; }

double distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    sum += d * d;
  }
  return std::sqrt(sum);
}

// The name a notice gives a mate.
std::string shown_name(std::uint8_t number, const std::string& name) {
  return name.empty() ? "Jugador " + std::to_string(number) : name;
}

// ORM (2026-11-13) the notices' words. The HUD's font has no "…": three dots.
constexpr const char* kMovingYou = "Moviendo al grupo contigo...";
constexpr const char* kAnnounceTail = " ha cambiado de época: moviendo al grupo al Templo del Tiempo...";
constexpr const char* kAnnounceNoName = "Cambio de época: moviendo al grupo al Templo del Tiempo...";
constexpr const char* kGathered = "Grupo reunido en el Templo del Tiempo";
constexpr const char* kArrived = "En el Templo del Tiempo";
constexpr const char* kArrivedDetail = "Esperando al resto del grupo...";
constexpr const char* kFailed = "No se pudo cambiar de época: inténtalo en el pedestal";

}  // namespace

const char* notice_kind_name(AgeNotice::Kind kind) {
  switch (kind) {
    case AgeNotice::Kind::moving_you: return "moving_you";
    case AgeNotice::Kind::announce: return "announce";
    case AgeNotice::Kind::countdown: return "countdown";
    case AgeNotice::Kind::waiting_mate: return "waiting_mate";
    case AgeNotice::Kind::gathered: return "gathered";
    case AgeNotice::Kind::arrived: return "arrived";
    case AgeNotice::Kind::failed: return "failed";
  }
  return "?";
}

const char* age_name(Age age) { return age == Age::adult ? "adult" : "child"; }

std::optional<Age> age_of_scene(std::string_view scene) {
  if (ends_with(scene, kChild)) return Age::child;
  if (ends_with(scene, kAdult)) return Age::adult;
  return std::nullopt;
}

std::string scene_for_age(std::string_view scene, Age age) {
  const auto now = age_of_scene(scene);
  if (!now || *now == age) return std::string(scene);
  std::string out(scene.substr(0, scene.size() - kChild.size()));  // both tails are 6 long
  out += age == Age::adult ? kAdult : kChild;
  return out;
}

const char* era_change_event(Age to) {
  return to == Age::adult ? "Market_Child_Field_ChildToAdult" : "Market_Adult_Field_AdultToChild";
}

const char* era_change_scene(Age to) { return to == Age::adult ? "Market_Adult" : "Market_Child"; }

void AgeFollow::change(Age age, const std::string& why) {
  forget_pending();
  pending_ = age;
  not_before_ = 0;
  announce_due_ = true;  // no mate named: "Cambio de época: ..."
  queued_.push_back(text("change to %s asked (%s)", age_name(age), why.c_str()));
}

void AgeFollow::notice(AgeOutput& out, AgeNotice::Kind kind, std::uint8_t number, const std::string& name,
                       std::string words, std::string detail, std::uint64_t show_us, bool chat) const {
  AgeNotice n;
  n.kind = kind;
  n.number = number;
  n.name = name;
  n.text = std::move(words);
  n.detail = std::move(detail);
  n.show_us = show_us;
  n.chat = chat;
  out.notices.push_back(std::move(n));
}

bool AgeFollow::all_mates_at(Age age) const {
  bool any = false;
  for (std::size_t n = 1; n < listed_.size(); ++n) {
    if (!listed_[n]) continue;
    any = true;
    if (mates_[n].settled != age) return false;
  }
  return any;
}

void AgeFollow::arrived(AgeOutput& out, Age age, std::uint64_t now) {
  if (all_mates_at(age)) {
    notice(out, AgeNotice::Kind::gathered, 0, {}, kGathered, {}, settings_.notice_us, true);
    out.log.push_back("every mate is our age: the group is gathered (told 'Grupo reunido en el Templo del Tiempo')");
    gather_age_.reset();
    gather_since_ = 0;
    return;
  }
  notice(out, AgeNotice::Kind::arrived, 0, {}, kArrived, kArrivedDetail, settings_.notice_us, false);
  gather_age_ = age;
  gather_since_ = std::max<std::uint64_t>(now, 1);
}

std::string AgeFollow::announce_text() const {
  return leader_ != 0 ? leader_name_ + kAnnounceTail : std::string(kAnnounceNoName);
}

void AgeFollow::forget_pending() {
  pending_.reset();
  leader_ = 0;
  leader_name_.clear();
  announce_due_ = false;
  wait_reason_.clear();
  unsafe_since_ = 0;
  countdown_shown_ = 0;
  era_fails_ = 0;
}

void AgeFollow::give_up(AgeOutput& out, const std::string& why, const AgeInput& in, std::optional<Age> ours) {
  // ORM (2026-11-13): the game's era change that did not take era_fail_limit
  // times in a row is given up: no move, the era our Link's, a notice.
  const bool last = game_era_event() && ++era_fails_ >= settings_.era_fail_limit;
  if (last) {
    out.log.push_back(text("the reload as %s did not take: %s; %d in a row: given up, no move (the era stays "
                           "our Link's; told 'No se pudo cambiar de época')",
                           pending_ ? age_name(*pending_) : "?", why.c_str(), era_fails_));
  } else {
    out.log.push_back(text("the reload as %s did not take: %s; tried again in %.0f s",
                           pending_ ? age_name(*pending_) : "?", why.c_str(), seconds(settings_.retry_us)));
  }
  // The game's era change reserves its own scene: none of ours to clear.
  if (!game_era_event()) out.cancel_scene = true;
  // The era follows our Link's body unless a load is under way: put back
  // whatever this reload (ours, or the game's era change) may have written.
  if (wrote_era_ || event_ran_) {
    if (in.event_running.value_or(false)) {
      out.log.push_back(text("Sequence_IsChildhood left to '%s', still running", asked_event_.c_str()));
    } else if (ours) {
      out.set_era = ours;
      out.log.push_back(text("Sequence_IsChildhood put back to %s (our Link's age): the era stays his",
                             age_name(*ours)));
    } else {
      out.log.push_back("Sequence_IsChildhood not put back: our age is not read now");
    }
  }
  if (reload_downed_) ++down_fails_;
  phase_ = Phase::idle;
  not_before_ = in.now_us + settings_.retry_us;
  calm_since_ = 0;
  countdown_shown_ = 0;
  wrote_era_ = event_ran_ = false;
  off_since_ = 0;
  if (last) {
    notice(out, AgeNotice::Kind::failed, 0, {}, kFailed, {}, settings_.notice_us, true);
    forget_pending();
  }
}

AgeOutput AgeFollow::tick(const AgeInput& in) {
  AgeOutput out;
  out.log.swap(queued_);
  const std::uint64_t now = in.now_us;
  waits_unsafe_ = false;

  // The ending holds the age (AgeInput::held): nothing is followed.
  if (in.held) {
    if (!held_) out.log.push_back("age changes held: " + *in.held);
    held_ = true;
    mates_.fill(Seen{});  // once it ends, each mate's age is first seen again
    own_settled_.reset();  // and ours (the credits' child is no change of ours to announce)
    gather_age_.reset();
    if (phase_ == Phase::idle) {
      if (pending_) {
        out.log.push_back(text("the reload as %s is dropped", age_name(*pending_)));
        forget_pending();
      }
      calm_since_ = 0;
      return out;
    }
  } else if (held_) {
    held_ = false;
    out.log.push_back("age changes followed again");
  }
  // A wait (AgeInput::wait): changes are seen, no reload begins.
  const bool waits = in.wait.has_value() && !in.held;
  if (waits) {
    if (!waiting_) out.log.push_back("reloads wait: " + *in.wait);
    waiting_ = true;
  } else if (waiting_) {
    waiting_ = false;
    out.log.push_back("reloads may begin again");
  }

  // Our own age (EDAD.md, docs/RIESGOS_RESUELTOS.md "Mundo e historia": GameUI's IsChild reads
  // adult for a moment while a game loads, and a first reading then is no change): trusted only
  // with a scene read (not loading) whose era, when its name has one, agrees. Steady: the same
  // for own_steady_us; only a steady reading may drop a pending reload as "already".
  // A scene whose era names that age is proof enough at once (Market_Adult after our own sword).
  std::optional<Age> ours;
  bool era_agrees = false;
  if (in.age && in.scene) {
    const auto era = age_of_scene(*in.scene);
    if (!era || *era == *in.age) ours = in.age;
    era_agrees = era && *era == *in.age;
  }
  if (ours != own_candidate_) {
    own_candidate_ = ours;
    own_since_ = now;
  }
  const bool own_steady = ours.has_value() && (era_agrees || now - own_since_ >= settings_.own_steady_us);
  if (ours) told_untrusted_ = false;

  // ORM (2026-11-13): our own age changed, not by a reload of ours (its
  // arrival sets own_settled_) nor by the sword's story asked on a mate's word:
  // we drew or put back the sword, the group follows us.
  if (in.story_follow) story_follow_at_ = std::max<std::uint64_t>(now, 1);
  if (own_steady && ours != own_settled_) {
    const auto was = own_settled_;
    own_settled_ = ours;
    const bool story = story_follow_at_ != 0 && elapsed(now, story_follow_at_) < settings_.story_follow_us;
    if (was && game_era_event() && !in.held && phase_ == Phase::idle && pending_ != ours && !story &&
        !in.mates.empty()) {
      out.log.push_back(text("we changed age ourselves (%s -> %s): the group follows us to the Temple of Time "
                             "(told 'Moviendo al grupo contigo...')",
                             age_name(*was), age_name(*ours)));
      notice(out, AgeNotice::Kind::moving_you, 0, {}, kMovingYou, {}, settings_.notice_us, true);
      gather_age_ = ours;
      gather_since_ = std::max<std::uint64_t>(now, 1);
    } else if (was && story) {
      out.log.push_back(text("our age is %s now by the sword's story asked on a mate's word: his change, not "
                             "ours to announce",
                             age_name(*ours)));
    }
    if (was && story) story_follow_at_ = 0;
  }

  // On our feet again: the reloads that did not take while down count no more.
  if (!in.downed) {
    down_fails_ = 0;
    told_down_wait_ = false;
  }

  // A mate's age, once it has settled; a change of it is the room's change.
  std::array<bool, protocol::kMaxPlayers + 1> listed{};
  for (const AgeMate& m : in.mates) {
    if (in.held) break;  // a reload under way runs on, nothing new is seen
    if (m.number == 0 || m.number > protocol::kMaxPlayers) continue;
    listed[m.number] = true;
    Seen& s = mates_[m.number];
    s.name = shown_name(m.number, m.name);
    // ORM (2026-11-13): a mate whose move to the room's age waits (an
    // event, a boss room, down: his orm_status::age_waits) is named here once
    // we are the age he goes to, and again every wait_repeat_us.
    const bool mate_waits = (m.status & protocol::orm_status::age_waits) != 0;
    const bool mate_down = (m.status & protocol::status::dead) != 0;
    const bool mate_scene = (m.status & protocol::status::in_cutscene) != 0;
    if (mate_waits != s.waits) {
      out.log.push_back(mate_waits ? text("P%u %s says his move to the room's age waits (%s)", unsigned{m.number},
                                          s.name.c_str(),
                                          mate_down    ? "he is down"
                                          : mate_scene ? "an event or cutscene runs there"
                                                       : "his log says why: a boss room, most likely")
                                   : text("P%u %s no longer waits to change age", unsigned{m.number},
                                          s.name.c_str()));
      s.waits = mate_waits;
      s.told_at = 0;
    }
    if (mate_waits && game_era_event() && own_steady && s.settled && *s.settled != *ours &&
        (s.told_at == 0 || elapsed(now, s.told_at) >= settings_.wait_repeat_us)) {
      const bool first = s.told_at == 0;
      s.told_at = std::max<std::uint64_t>(now, 1);
      notice(out, AgeNotice::Kind::waiting_mate, m.number, s.name,
             "Esperando a " + s.name + " para cambiar de época...",
             mate_down ? "Está caído" : mate_scene ? "Está en una escena" : "", settings_.notice_us, first);
    }
    const bool loading = (m.status & protocol::status::loading) != 0;
    const auto by_scene = m.scene ? age_of_scene(*m.scene) : std::nullopt;
    if (!m.age || loading || (by_scene && *by_scene != *m.age)) continue;  // not a reading to trust
    if (s.candidate != m.age) {
      s.candidate = m.age;
      s.since = now;
    }
    if (now - s.since < settings_.settle_us || s.settled == s.candidate) continue;
    const auto was = s.settled;
    s.settled = s.candidate;
    if (!was) continue;  // first seen: what he is, not a change
    out.log.push_back(text("P%u is %s now (was %s)", unsigned{m.number}, age_name(*s.settled),
                           age_name(*was)));
    if ((!ours || *ours != *s.settled) && pending_ != s.settled) {
      // Our age not trusted now (loading): followed all the same; the idle phase drops it as
      // "already" once a steady reading says we are that age.
      forget_pending();
      pending_ = s.settled;
      not_before_ = 0;
      leader_ = m.number;
      leader_name_ = s.name;
      announce_due_ = true;
      out.log.push_back(text("we follow: reload as %s once nothing runs here%s", age_name(*pending_),
                             ours ? "" : " (our own age is not read steadily yet: checked again before it)"));
    } else if (own_steady && *ours == *s.settled && pending_ && *pending_ != *s.settled &&
               phase_ == Phase::idle) {
      out.log.push_back(text("already %s: nothing to reload", age_name(*s.settled)));
      forget_pending();
    }
  }
  for (std::size_t n = 1; n < listed.size(); ++n) {
    if (!listed[n]) mates_[n] = Seen{};  // gone (or held): a rejoin starts from what he is
  }
  listed_ = listed;

  // ORM (2026-11-13): after our own change or our arrival, "Grupo
  // reunido" once every mate reads our age.
  if (gather_age_) {
    if (ours && *ours != *gather_age_) {
      out.log.push_back("no longer looking for the group at our age: ours changed");
      gather_age_.reset();
    } else if (elapsed(now, gather_since_) > settings_.gather_us) {
      out.log.push_back(text("not every mate reached %s in %.0f s: the group is no longer looked for",
                             age_name(*gather_age_), seconds(settings_.gather_us)));
      gather_age_.reset();
    } else if (all_mates_at(*gather_age_)) {
      notice(out, AgeNotice::Kind::gathered, 0, {}, kGathered, {}, settings_.notice_us, true);
      out.log.push_back("every mate is our age: the group is gathered (told 'Grupo reunido en el Templo del Tiempo')");
      gather_age_.reset();
    }
  }

  switch (phase_) {
    case Phase::idle: {
      if (!pending_) break;
      if (ours && *ours == *pending_) {
        if (own_steady) {
          out.log.push_back(text("already %s: nothing to reload", age_name(*pending_)));
          forget_pending();
        }
        calm_since_ = 0;  // not steady yet: neither dropped nor reloaded to the age we read
        countdown_shown_ = 0;
        break;
      }
      if (!ours && in.age && !told_untrusted_) {
        told_untrusted_ = true;
        out.log.push_back(text("our age reads %s in %s: not trusted (the scene's era says otherwise, or we "
                               "load): the reload as %s waits for a reading that agrees",
                               age_name(*in.age), in.scene ? in.scene->c_str() : "no scene",
                               age_name(*pending_)));
      }
      // D-R62 (oot-64 D-178): lying down is no reason to wait, being revived
      // or a game over is; after downed_tries reloads that did not take while
      // down, the revive first. ORM (2026-11-13): the game's era change
      // moves Link to the Temple of Time: never while down, lying or not.
      const bool era = game_era_event();
      const bool down_waits = in.downed && (era || !in.lying || down_fails_ >= settings_.downed_tries);
      if (!era && down_waits && in.lying && !told_down_wait_) {
        told_down_wait_ = true;
        out.log.push_back(text("%d reloads as %s while we lay down did not take: it waits until a mate "
                               "revives us",
                               down_fails_, age_name(*pending_)));
      }
      // ORM (2026-11-13): the move to the Temple of Time never in an
      // event or cutscene, a boss room's lock or down. A reason that lasts
      // calm_us is said: one log line, the notice with when we will move, and
      // our PLAYER_STATUS (orm_status::age_waits, waits_unsafe).
      if (era && !waits && ours && in.scene) {
        const char* why = nullptr;
        const char* until = nullptr;
        if (in.downed) {
          why = "our Link is down";
          until = "Te moverás cuando te reanimen";
        } else if (in.boss_fight) {
          why = "a boss room shuts us in";
          until = "Te moverás cuando acabe el combate";
        } else if (in.event_running == true) {
          why = "an event or cutscene runs here";
          until = "Te moverás cuando termine la escena";
        }
        if (why == nullptr) {
          unsafe_since_ = 0;
          if (!wait_reason_.empty()) {
            out.log.push_back(text("the move as %s may go on: '%s' is over", age_name(*pending_),
                                   wait_reason_.c_str()));
            wait_reason_.clear();
          }
        } else {
          if (unsafe_since_ == 0) unsafe_since_ = std::max<std::uint64_t>(now, 1);
          if (elapsed(now, unsafe_since_) >= settings_.calm_us) {
            waits_unsafe_ = true;
            if (wait_reason_ != why) {
              wait_reason_ = why;
              out.log.push_back(text("the move as %s to the Temple of Time waits: %s (our PLAYER_STATUS says so: "
                                     "the others show 'Esperando a ...')",
                                     age_name(*pending_), why));
              notice(out, AgeNotice::Kind::announce, leader_, leader_name_, announce_text(), until,
                     settings_.notice_us, announce_due_);
              announce_due_ = false;
            }
          }
          calm_since_ = 0;
          countdown_shown_ = 0;
          break;
        }
      }
      const bool calm = ours && in.scene && in.event_running == false && !down_waits && !waits && !in.boss_fight;
      if (!calm) {
        calm_since_ = 0;
        countdown_shown_ = 0;
        break;
      }
      if (era && now < not_before_) break;  // a retry: its countdown comes after the wait
      if (calm_since_ == 0) calm_since_ = std::max<std::uint64_t>(now, 1);
      if (era) {
        // ORM (2026-11-13): told before the move, then 3, 2, 1.
        if (announce_due_) {
          announce_due_ = false;
          notice(out, AgeNotice::Kind::announce, leader_, leader_name_, announce_text(), {}, settings_.notice_us,
                 true);
          out.log.push_back("told: '" + announce_text() + "'");
        }
        const std::uint64_t calm_for = elapsed(now, calm_since_);
        if (calm_for < settings_.countdown_us) {
          const int left = static_cast<int>((settings_.countdown_us - calm_for + 999'999) / 1'000'000);
          if (left != countdown_shown_) {
            countdown_shown_ = left;
            notice(out, AgeNotice::Kind::countdown, leader_, leader_name_, announce_text(), std::to_string(left),
                   settings_.countdown_step_us, false);
            out.log.push_back(text("countdown %d before the move to the Temple of Time", left));
          }
          break;
        }
      } else if (now < calm_since_ + settings_.calm_us || now < not_before_) {
        break;
      }
      from_ = *in.scene;
      asked_event_ = game_era_event() ? era_change_event(*pending_) : event_;
      target_ = game_era_event() ? std::string(era_change_scene(*pending_)) : scene_for_age(from_, *pending_);
      where_ = in.position;
      phase_ = Phase::event;
      phase_at_ = first_at_ = now;
      tries_ = 1;
      unloaded_ = false;
      wrote_era_ = event_ran_ = false;
      off_since_ = 0;
      reload_downed_ = in.downed;
      if (reload_downed_) {
        out.log.push_back("we lie down: the reload as the room's age all the same, down we stay (D-R62, "
                          "oot-64 D-178)");
      }
      // Nothing written yet: the era waits for an event of ours that runs.
      out.ask_event = asked_event_;
      if (where_) {
        out.log.push_back(text("reloading as %s: %s -> %s, Link at %.2f %.2f %.2f; asking the event '%s'",
                               age_name(*pending_), from_.c_str(), target_.c_str(), (*where_)[0],
                               (*where_)[1], (*where_)[2], asked_event_.c_str()));
      } else {
        out.log.push_back(text("reloading as %s: %s -> %s; asking the event '%s'", age_name(*pending_),
                               from_.c_str(), target_.c_str(), asked_event_.c_str()));
      }
      if (game_era_event()) {
        out.log.push_back(text("the game's own era change, as at the pedestal: it swaps the equipment, sets "
                               "the era and loads %s itself; Link is not put back (another scene)",
                               target_.c_str()));
      }
      break;
    }
    case Phase::event:
      if (in.event_running.value_or(false)) {
        phase_ = Phase::scene;
        asked_at_ = now;
        event_ran_ = true;
        off_since_ = 0;
        if (game_era_event()) {
          out.log.push_back(text("event running %.2f s after it was asked: the game's era change to %s runs",
                                 seconds(now - phase_at_), target_.c_str()));
        } else {
          // Now, not before: a request the game refuses leaves the era alone.
          out.set_era = pending_;
          wrote_era_ = true;
          out.ask_scene = target_;
          out.log.push_back(text("event running %.2f s after it was asked: scene change to %s asked",
                                 seconds(now - phase_at_), target_.c_str()));
        }
      } else if (now - phase_at_ > settings_.event_wait_us) {
        if (++tries_ > settings_.event_tries) {
          give_up(out, text("no event of ours started (%d tries of '%s')", settings_.event_tries,
                            asked_event_.c_str()),
                  in, ours);
          break;
        }
        phase_at_ = now;
        out.ask_event = asked_event_;
        out.log.push_back(text("no event %.1f s after it was asked: try %d", seconds(settings_.event_wait_us),
                               tries_));
      }
      break;
    case Phase::scene: {
      const std::uint64_t wait_us = game_era_event() ? settings_.era_reload_wait_us : settings_.reload_wait_us;
      if (!in.scene) {
        unloaded_ = true;
      } else if (unloaded_) {
        phase_ = Phase::arrive;
        out.log.push_back(text("loading %s", in.scene->c_str()));
      } else if (in.event_running == false) {
        // The game's era change hands over to its cutscene: a moment with no event is not its end.
        if (game_era_event() && off_since_ == 0) off_since_ = std::max<std::uint64_t>(now, 1);
        if (!game_era_event() || now - off_since_ >= settings_.event_gap_us) {
          give_up(out, "the event ended with no scene change", in, ours);
          break;
        }
      } else {
        off_since_ = 0;
        if (!game_era_event() && now - asked_at_ >= settings_.ask_again_us) {
          asked_at_ = now;
          out.ask_scene = target_;
        }
      }
      if (phase_ == Phase::scene && now - first_at_ > wait_us) {
        give_up(out, "no scene change in time", in, ours);
      }
      break;
    }
    case Phase::arrive:
      if (game_era_event() && in.scene && *in.scene == target_ && in.age == pending_ &&
          in.event_running == false) {
        // The game's own destination: no place of ours there, nothing of ours to clear.
        out.log.push_back(text("now %s in %s, %.1f s after the reload began: the game's own era change, its "
                               "equipment swap and era; Link left where the game put him (the Temple of Time)",
                               age_name(*pending_), target_.c_str(), seconds(now - first_at_)));
        const Age reached = *pending_;
        own_settled_ = reached;  // our change, not one to announce
        forget_pending();
        phase_ = Phase::idle;
        calm_since_ = 0;
        down_fails_ = 0;
        wrote_era_ = event_ran_ = false;
        arrived(out, reached, now);  // ORM (2026-11-13): "Grupo reunido ..."
      } else if (!game_era_event() && in.scene && *in.scene == target_ && in.age == pending_ &&
                 in.event_running == false) {
        // A scene for both ages (the Spirit Temple): Link stays at its start,
        // the way out (gerudo.hpp age_reload_keeps_spot).
        const bool keep = age_reload_keeps_spot(target_);
        const bool far =
            keep && where_ && in.position && distance(*where_, *in.position) > settings_.move_back_min;
        if (far) out.move_to = where_;
        // Not kept: is the start the game left him at the way out both ages reach? (gerudo.hpp)
        std::string start_note;
        if (!keep) {
          const auto start = age_reload_start(target_);
          if (!start) {
            start_note = " (no start known to check it by)";
          } else if (!in.position) {
            start_note = " (our place not read: not checked against the entrance)";
          } else if (const double d = distance(*start, *in.position); d <= kReloadStartNearM) {
            start_note = text(", the entrance from the Desert Colossus (%.1f m from it)", d);
          } else {
            out.move_to = start;
            start_note = text(": %.1f m from the entrance from the Desert Colossus (the game's start is another): "
                              "put at the entrance",
                              d);
          }
        }
        out.log.push_back(text("now %s in %s, %.1f s after the reload began%s%s", age_name(*pending_),
                               target_.c_str(), seconds(now - first_at_),
                               far     ? ", Link put back where he stood"
                               : !keep ? ", Link left at the scene's start (a scene for both ages: the other "
                                         "half may have no way out at this age)"
                                       : "",
                               start_note.c_str()));
        out.cancel_scene = true;
        own_settled_ = *pending_;  // our change, not one to announce
        forget_pending();
        phase_ = Phase::idle;
        calm_since_ = 0;
        down_fails_ = 0;
        wrote_era_ = event_ran_ = false;
      } else if (now - first_at_ >
                 (game_era_event() ? settings_.era_reload_wait_us : settings_.reload_wait_us)) {
        give_up(out,
                text("in %s at %s after the load", in.scene ? in.scene->c_str() : "?",
                     in.age ? age_name(*in.age) : "?"),
                in, ours);
      }
      break;
  }
  return out;
}

}  // namespace coop::host
