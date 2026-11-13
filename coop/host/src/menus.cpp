// ORM pause, menus, saving and leaving with partners (coop/host/menus.hpp;
// oot-remake docs/MENUS_PAUSA.md).

#include "coop/host/menus.hpp"

#include <algorithm>

namespace coop::host::menus {
namespace {

// UTF-8, written apart so no hex escape runs into the next letter.
constexpr const char* kOacute = "\xc3\xb3";
constexpr const char* kUacute = "\xc3\xba";

std::string name_or_number(std::string_view name, std::uint8_t number) {
  if (!name.empty()) return std::string(name);
  return "P" + std::to_string(number);
}

}  // namespace

const char* to_string(State state) {
  switch (state) {
    case State::playing: return "playing";
    case State::paused: return "paused";
    case State::title: return "title";
  }
  return "?";
}

std::uint8_t status_bits(State state) {
  switch (state) {
    case State::paused: return protocol::status::paused;
    case State::title:
      return static_cast<std::uint8_t>(protocol::status::loading | protocol::orm_status::at_title);
    case State::playing: break;
  }
  return 0;
}

std::optional<State> Local::frame(const Input& in) {
  const std::uint64_t now = std::max<std::uint64_t>(in.now_us, 1);
  State next = state_;

  // The title clock: no Link and no scene in a row, and no scene load said.
  const bool gone = !in.link && !in.scene && !in.loading_scene.value_or(false);
  if (!gone) {
    gone_since_us_ = 0;
  } else if (gone_since_us_ == 0) {
    gone_since_us_ = now;
  }
  const bool title_now = gone_since_us_ != 0 && now - gone_since_us_ >= settings_.title_after_us;

  if (title_now) {
    next = State::title;
    unknown_since_us_ = 0;
  } else if (state_ == State::title) {
    // Back only once our Link is there (a scene alone is the save loading).
    next = in.link ? State::playing : State::title;
    unknown_since_us_ = 0;
  } else {
    bool pause_now = false;
    if (in.is_pause) {
      unknown_since_us_ = 0;
      pause_now = *in.is_pause && !in.ocarina;
    } else if (state_ == State::paused) {
      if (unknown_since_us_ == 0) unknown_since_us_ = now;
      pause_now = !in.ocarina && now - unknown_since_us_ < settings_.unknown_keeps_us;
    }
    next = pause_now ? State::paused : State::playing;
  }

  if (next == state_) return std::nullopt;
  state_ = next;
  return next;
}

State mate_state(std::uint8_t status_flags) {
  if ((status_flags & protocol::orm_status::at_title) != 0) return State::title;
  if ((status_flags & protocol::status::paused) != 0) return State::paused;
  return State::playing;
}

std::string tag_note(std::uint8_t status_flags) {
  return mate_state(status_flags) == State::paused ? std::string("pausa") : std::string();
}

std::vector<MateChange> Mates::frame(std::span<const Seen> seen) {
  std::vector<MateChange> out;
  std::erase_if(known_, [&](const Known& k) {
    return std::none_of(seen.begin(), seen.end(), [&](const Seen& s) { return s.number == k.number; });
  });
  for (const Seen& s : seen) {
    const State now = mate_state(s.status_flags);
    auto it = std::find_if(known_.begin(), known_.end(),
                           [&](const Known& k) { return k.number == s.number; });
    if (it == known_.end()) {
      known_.push_back({s.number, now, s.in_world && now != State::title});
      continue;
    }
    if (it->state != now) {
      MateChange c{s.number, it->state, now, {}};
      const std::string who = name_or_number(s.name, s.number);
      if (now == State::title && it->been_in) {
        c.notice = who + " sali" + kOacute + " al men" + kUacute + " principal";
      } else if (it->state == State::title) {
        c.notice = who + (it->been_in ? std::string(" volvi") + kOacute + " a la partida"
                                      : std::string(" entr") + kOacute + " en la partida");
      }
      out.push_back(std::move(c));
      it->state = now;
    }
    if (s.in_world && now != State::title) it->been_in = true;
  }
  return out;
}

std::string leave_notice(std::string_view name, protocol::LeaveReason reason) {
  const std::string who(name.empty() ? std::string_view("Un jugador") : name);
  switch (reason) {
    case protocol::LeaveReason::timed_out:
      return who + " perdi" + kOacute + " la conexi" + kOacute + "n";
    case protocol::LeaveReason::kicked:
      return who + " sali" + kOacute + " de la sala (expulsado)";
    case protocol::LeaveReason::left:
      break;
  }
  return who + " sali" + kOacute + " de la sesi" + kOacute + "n";
}

bool SaveWatch::frame(std::uint64_t now_us, bool paused, std::optional<std::int64_t> newest_write) {
  const std::uint64_t now = std::max<std::uint64_t>(now_us, 1);
  if (was_paused_ && !paused) pause_end_us_ = now;
  was_paused_ = paused;
  if (!newest_write) return false;
  if (!mark_) {
    mark_ = newest_write;
    return false;
  }
  if (*newest_write <= *mark_) return false;
  mark_ = newest_write;
  return paused ||
         (pause_end_us_ != 0 && now >= pause_end_us_ && now - pause_end_us_ <= kAfterPauseUs);
}

std::string guest_save_notice(std::string_view host_name, bool coop_folder) {
  const std::string host = host_name.empty() ? std::string("el anfitri") + kOacute + "n"
                                             : std::string(host_name) + " (anfitri" + kOacute + "n)";
  return std::string(coop_folder ? "Guardado en tu copia de la partida del grupo"
                                 : "Guardado en tu propia partida") +
         ". La partida del grupo solo la guarda " + host + ".";
}

}  // namespace coop::host::menus
