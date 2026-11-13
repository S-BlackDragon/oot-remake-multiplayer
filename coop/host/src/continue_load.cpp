// ORM: Continue with the group's game after a game over: the reload's load window is a save load, so the group
//   keeps what it had when it fell (keep, the default) or follows the host's reload (save), and never a player's
//   own save. The rules are described in coop/host/continue_load.hpp.
// Doc: oot-remake docs/CONTINUAR.md (E11).
// Switch: LICHII_ORM_CONTINUE = keep (default) | save | 0, with LICHII_COOP_DOWNED=1.
// Test: tests/host/continue_load_test.cpp.

#include "coop/host/continue_load.hpp"

namespace coop::host {
namespace {

std::uint64_t since(std::uint64_t now, std::uint64_t then) { return now >= then ? now - then : 0; }

}  // namespace

std::optional<ContinueMode> parse_continue_mode(std::string_view text) {
  if (text.empty() || text == "keep" || text == "1") return ContinueMode::keep;
  if (text == "save") return ContinueMode::save;
  if (text == "0" || text == "off") return ContinueMode::off;
  return std::nullopt;
}

const char* to_string(ContinueMode mode) {
  switch (mode) {
    case ContinueMode::off: return "off";
    case ContinueMode::keep: return "keep";
    case ContinueMode::save: return "save";
  }
  return "?";
}

const char* to_string(ContinueLoad::Phase phase) {
  switch (phase) {
    case ContinueLoad::Phase::idle: return "idle";
    case ContinueLoad::Phase::game_over: return "game_over";
    case ContinueLoad::Phase::reloading: return "reloading";
  }
  return "?";
}

bool is_game_over_event(std::string_view event_name) {
  return event_name.find("GameOver") != std::string_view::npos;
}

namespace {

// docs/items.md: each of these is per player in game_items (checked by the test).
constexpr KeepExtra kKeepExtra[] = {
    {"PlayerStatus.CurrentRupee", 0xa77921d7},
    {"PouchData.Item_DekuStick.StockNum", 0xb9494411},
    {"PouchData.Item_DekuNut.StockNum", 0xf30d9f54},
    {"PouchData.Item_DekuSeed.StockNum", 0x5fadf7fc},
    {"PouchData.Item_Bomb.StockNum", 0xdb1bcd4f},
    {"PouchData.Item_Bombchu.StockNum", 0xdf7c517a},
    {"PouchData.Item_Arrow_Normal.StockNum", 0x518ec74b},
};

}  // namespace

std::span<const KeepExtra> continue_keep_extra() { return kKeepExtra; }

std::vector<FlagKey> continue_keep_extra_keys() {
  std::vector<FlagKey> keys;
  for (const KeepExtra& k : kKeepExtra) keys.emplace_back(k.hash, std::uint16_t{0});
  return keys;
}

ContinueOutput ContinueLoad::tick(const ContinueInput& in) {
  ContinueOutput out;
  if (mode_ == ContinueMode::off) return out;
  const bool keep = mode_ == ContinueMode::keep;
  if (summary_) {
    // The watcher closed the window in the frame after the one that saw it settle: its last
    // writes (and a host's FLAG_RESET) are in this frame's counters.
    summary_ = false;
    report(in, out);
  }

  if (phase_ == Phase::idle && in.state == DownedState::game_over) {
    phase_ = Phase::game_over;
    group_ = in.group_defeat;
    resets_ = in.host_resets;
    reverts_ = in.load_reverts;
    applied_ = in.reset_applied;
    kept_ = in.continue_kept;
    counts_ = in.continue_counts;
    stale_window_ = in.settling;
    window_seen_ = false;
    waiting_since_.reset();
    game_over_event_.clear();
    std::string how;
    if (keep) {
      how = "the group goes on with what it has now: what the reload moves is written back";
    } else if (in.hosting) {
      how = "we host, so the group follows our reload (FLAG_RESET)";
    } else {
      how = "our own save's values go back to the group's, and the host's reload decides (its FLAG_RESET)";
    }
    out.log.push_back(std::string(group_ ? "group defeat: our game over runs"
                                         : "our game over runs (nobody else up in the room)") +
                      "; its Continue reloads the save and that load counts as a save load (" + to_string(mode_) +
                      "): " + how);
  }
  if (phase_ == Phase::idle) {
    out.mode = ContinueMode::off;
    return out;
  }

  // The reload's window: one that opens after the game over started.
  if (!in.settling) stale_window_ = false;
  if (in.settling && !stale_window_ && !window_seen_) {
    window_seen_ = true;
    if (phase_ == Phase::reloading) out.log.push_back("the reload's load window opens");
  }
  // The game over's event, by name (docs/RIESGOS_RESUELTOS.md "Mundo e historia"): the first named
  // event that runs while our game over does is it, whatever it is called. A name with GameOver in
  // it is the expected path; another name is taken all the same (else the Continue would end 10 s
  // into a game over menu whose event is called otherwise), and the line says so.
  if (in.state == DownedState::game_over && in.event_running && !in.event_name.empty() &&
      game_over_event_.empty()) {
    game_over_event_ = std::string(in.event_name);
    out.log.push_back("the game over event is '" + game_over_event_ + "' (" +
                      (is_game_over_event(in.event_name)
                           ? std::string("its name has GameOver")
                           : std::string("its name has no GameOver: taken as the game over's, it runs while "
                                         "our game over does")) +
                      "): the Continue waits while it runs");
  }
  // The game over event (or an event whose name is not known yet) still runs.
  const bool event_on =
      in.event_running && (in.event_name.empty() || is_game_over_event(in.event_name) ||
                           (!game_over_event_.empty() && in.event_name == game_over_event_));

  if (phase_ == Phase::game_over) {
    if (in.group_defeat) group_ = true;
    if (in.state == DownedState::alive) {
      phase_ = Phase::reloading;
      out.log.push_back(in.settling && window_seen_
                            ? "Link is back after the game over: the reload's window settles"
                        : window_seen_ ? "Link is back after the game over, after the reload's window"
                                       : "Link is back after the game over before the reload's load window: it "
                                         "waits for one (while the game over event runs, then up to 10 s)");
    } else if (in.state != DownedState::game_over && !in.settling) {
      // Down again or being revived with no reload in between: not a
      // Continue (a fairy, or the game over never ran).
      phase_ = Phase::idle;
      out.log.push_back(std::string("left the game over without a reload (now ") +
                        to_string(in.state) + "): nothing to follow");
    }
  }

  if (phase_ == Phase::reloading && !in.settling) {
    if (in.state != DownedState::alive) {
      finish(in, out);  // down again after it: a new game over starts from idle
    } else if (window_seen_) {
      if (!event_on) finish(in, out);
    } else if (event_on) {
      waiting_since_.reset();
    } else if (!waiting_since_) {
      waiting_since_ = in.now_us;
    } else if (in.now_us >= *waiting_since_ && in.now_us - *waiting_since_ >= kNoWindowUs) {
      phase_ = Phase::idle;
      out.log.push_back("no load window came in 10 s after the game over: no reload to follow, the usual "
                        "rules again");
    }
  }

  out.mode = phase_ != Phase::idle ? mode_ : ContinueMode::off;
  return out;
}

void ContinueLoad::finish(const ContinueInput& in, ContinueOutput& out) {
  phase_ = Phase::idle;
  waiting_since_.reset();
  if (!window_seen_) {
    out.log.push_back(std::string("left the game over without a reload (now ") + to_string(in.state) +
                      "): nothing to follow");
    return;
  }
  summary_ = true;  // told on the next tick (report)
}

void ContinueLoad::report(const ContinueInput& in, ContinueOutput& out) const {
  if (mode_ == ContinueMode::keep) {
    const std::uint64_t counts = since(in.continue_counts, counts_);
    out.log.push_back("our Continue's reload is done: " + std::to_string(since(in.continue_kept, kept_)) +
                      " write(s) put the group's values from before it back (nothing sent)" +
                      (counts != 0 ? "; " + std::to_string(counts) +
                                         " count change(s) from others added on the group's value, not on "
                                         "the reload's (sent as totals)"
                                   : std::string()));
  } else if (in.hosting) {
    const std::uint64_t sent = since(in.host_resets, resets_);
    out.log.push_back(sent != 0
                          ? "our Continue's reload is the group's state now: FLAG_RESET sent, every "
                            "game writes it exactly"
                          : "our Continue's reload moved no shared flag: nothing sent, the group "
                            "keeps its state");
  } else {
    const std::uint64_t back = since(in.load_reverts, reverts_);
    const std::uint64_t taken = since(in.reset_applied, applied_);
    out.log.push_back("our Continue's reload is done: " + std::to_string(back) +
                      " write(s) put our own save's values back to the group's, " + std::to_string(taken) +
                      " value(s) taken from the host's FLAG_RESET" +
                      (group_ ? "" : " (alone: nobody else's reload to follow)"));
  }
}

}  // namespace coop::host
