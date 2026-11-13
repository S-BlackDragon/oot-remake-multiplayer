// ORM: downed, not dead: lying down instead of the game over, revived by a mate, group defeat, the
//   screen and connection lines. The rules are described in coop/host/downed.hpp.
// Doc: oot-remake docs/CAIDO_REVIVIR.md (D-R13, D-R14).
// Switch: LICHII_COOP_DOWNED (on in the kit since 0.7).
// Test: tests/host/downed_test.cpp.

#include "coop/host/downed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "coop/protocol/codec.hpp"

namespace coop::host {
namespace {

// Offsets: docs/objects.md, oot-remake docs/CAIDO_REVIVIR.md 2.1, 2.2.
constexpr std::uint64_t kActorComponents = 0x270;
constexpr std::uint64_t kComponentLife = 0x2b8;
constexpr std::uint64_t kComponentDeath = 0x1c0;
constexpr std::uint64_t kLifeValue = 0x1c10;
constexpr std::uint64_t kLifeMax = 0x1c18;
constexpr std::uint64_t kValueAt = 8;
constexpr std::uint64_t kDeathWord = 0x20;
constexpr std::int32_t kMaxLifeSeen = 999;  // quarter hearts; more is not a life word

std::string text(const char* format, auto... args) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), format, args...);
  return buf;
}

char letter(int slot) { return static_cast<char>('A' + slot); }

double distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    sum += d * d;
  }
  return std::sqrt(sum);
}

}  // namespace

bool death_word_dead(std::uint32_t word) {
  return (word & 0xeu) != 0 && (word & 0x6000u) != 0x2000u;
}

Vitals read_vitals(GameMemory& memory, std::uint64_t player_actor) {
  Vitals v;
  const auto table = read_ptr(memory, player_actor + kActorComponents);
  if (!table) return v;
  if (const auto death = read_ptr(memory, *table + kComponentDeath)) {
    v.death_word = read_u32(memory, *death + kDeathWord);
  }
  const auto life = read_ptr(memory, *table + kComponentLife);
  const auto value = life ? read_ptr(memory, *life + kLifeValue) : std::nullopt;
  const auto max = life ? read_ptr(memory, *life + kLifeMax) : std::nullopt;
  const auto l = value ? read_u32(memory, *value + kValueAt) : std::nullopt;
  const auto m = max ? read_u32(memory, *max + kValueAt) : std::nullopt;
  if (m && static_cast<std::int32_t>(*m) > 0 && static_cast<std::int32_t>(*m) <= kMaxLifeSeen) {
    v.max_life = static_cast<std::int32_t>(*m);
    if (l && static_cast<std::int32_t>(*l) >= 0 && static_cast<std::int32_t>(*l) <= *v.max_life) {
      v.life = static_cast<std::int32_t>(*l);
    }
  }
  return v;
}

std::int32_t revive_life(std::int32_t max_life) {
  if (max_life <= 0) return 0;
  // oot-64: MAX(0x30, ((cap / 4) + 0xF) & ~0xF) in sixteenths; here quarters.
  const std::int32_t quarter = (max_life / 4 + 3) / 4 * 4;
  return std::min(std::max<std::int32_t>(12, quarter), max_life);
}

bool write_life(WritableGameMemory& memory, std::uint64_t player_actor, std::int32_t quarters,
                bool direct) {
  const Vitals v = read_vitals(memory, player_actor);
  if (!v.max_life || quarters < 0 || quarters > *v.max_life) return false;
  if (!direct) return GameFlags::set_int(memory, kLifeFlag, quarters);
  const auto table = read_ptr(memory, player_actor + kActorComponents);
  const auto life = table ? read_ptr(memory, *table + kComponentLife) : std::nullopt;
  const auto value = life ? read_ptr(memory, *life + kLifeValue) : std::nullopt;
  if (!value) return false;
  const auto q = static_cast<std::uint32_t>(quarters);
  const std::array<std::uint8_t, 4> bytes{static_cast<std::uint8_t>(q), static_cast<std::uint8_t>(q >> 8),
                                          static_cast<std::uint8_t>(q >> 16),
                                          static_cast<std::uint8_t>(q >> 24)};
  return memory.write(*value + kValueAt, bytes);
}

BottleKeys bottle_keys(int slot) {
  const std::string x(1, letter(slot));
  const std::string info = "PlayerStatus.AssignableItem.BottleInfo.Bottle" + x;
  return {protocol::flag_hash(info + ".BottleItem"), protocol::flag_hash(info + ".IsValid"),
          protocol::flag_hash("PouchData.Bottle" + x + ".StockNum")};
}

std::uint32_t bottle_value(std::string_view name) { return protocol::flag_hash(name); }

Bottles read_bottles(GameMemory& memory) {
  Bottles out;
  GameFlags flags(memory);
  for (int slot = 0; slot < kBottleCount; ++slot) {
    const BottleKeys k = bottle_keys(slot);
    const auto item_at = flags.live_address(GameFlags::Live::enum32, k.item);
    const auto valid_at = flags.live_address(GameFlags::Live::bool8, k.valid);
    const auto stock_at = flags.live_address(GameFlags::Live::int32, k.stock);
    const auto item = item_at ? read_u32(memory, *item_at) : std::nullopt;
    const auto valid = valid_at ? read_u8(memory, *valid_at) : std::nullopt;
    const auto stock = stock_at ? read_u32(memory, *stock_at) : std::nullopt;
    if (item && valid && stock) {
      out[static_cast<std::size_t>(slot)] =
          Bottle{*item, *valid != 0, static_cast<std::int32_t>(*stock)};
    }
  }
  return out;
}

bool write_bottle(WritableGameMemory& memory, int slot, const Bottle& bottle) {
  if (slot < 0 || slot >= kBottleCount) return false;
  GameFlags flags(memory);
  const BottleKeys k = bottle_keys(slot);
  const auto item_at = flags.live_address(GameFlags::Live::enum32, k.item);
  const auto valid_at = flags.live_address(GameFlags::Live::bool8, k.valid);
  const auto stock_at = flags.live_address(GameFlags::Live::int32, k.stock);
  if (!item_at || !valid_at || !stock_at) return false;
  const auto u32 = [](std::uint32_t v) {
    return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                       static_cast<std::uint8_t>(v >> 16),
                                       static_cast<std::uint8_t>(v >> 24)};
  };
  const std::array<std::uint8_t, 1> valid{static_cast<std::uint8_t>(bottle.valid ? 1 : 0)};
  bool ok = memory.write(*item_at, u32(bottle.item));
  ok = memory.write(*valid_at, valid) && ok;
  ok = memory.write(*stock_at, u32(static_cast<std::uint32_t>(bottle.stock))) && ok;
  return ok;
}

std::optional<BottlePick> pick_bottle(const Bottles& bottles) {
  const std::uint32_t fairy = bottle_value("Fairy");
  const std::uint32_t empty = bottle_value("Empty");
  for (int slot = 0; slot < kBottleCount; ++slot) {
    const auto& b = bottles[static_cast<std::size_t>(slot)];
    if (b && b->valid && b->stock > 0 && b->item == fairy) return BottlePick{slot, true, false};
  }
  for (int slot = 0; slot < kBottleCount; ++slot) {
    const auto& b = bottles[static_cast<std::size_t>(slot)];
    if (b && b->valid && b->stock > 0 && b->item == empty) return BottlePick{slot, false, false};
  }
  for (int slot = 0; slot < kBottleCount; ++slot) {
    const auto& b = bottles[static_cast<std::size_t>(slot)];
    if (b && !b->owned()) return BottlePick{slot, false, true};
  }
  return std::nullopt;
}

const char* to_string(DownedState state) {
  switch (state) {
    case DownedState::alive: return "alive";
    case DownedState::downed: return "downed";
    case DownedState::reviving: return "reviving";
    case DownedState::game_over: return "game_over";
    case DownedState::fairy: return "fairy";
  }
  return "?";
}

void Downed::request_revive(std::uint8_t by) {
  if (state_ == DownedState::downed && !asked_by_) asked_by_ = by;
}

void Downed::hear_progress(std::uint8_t by, std::uint8_t percent, std::uint64_t now_us) {
  if (state_ != DownedState::downed || percent > protocol::kReviveMaxProgress) return;
  progress_by_ = by;
  progress_ = percent;
  progress_at_ = std::max<std::uint64_t>(now_us, 1);
}

bool Downed::group_defeat(std::uint8_t down) {
  if (state_ != DownedState::downed && state_ != DownedState::reviving) return false;
  defeat_ = std::max<std::uint8_t>(down, 1);
  return true;
}

std::optional<Downed::Reviver> Downed::reviver(std::uint64_t now_us) const {
  if (state_ == DownedState::reviving && by_ != 0) return Reviver{by_, 100};
  if (held(now_us)) return Reviver{progress_by_, progress_};
  return std::nullopt;
}

std::optional<std::uint64_t> Downed::alone_until() const {
  if (state_ != DownedState::downed || lonely_since_ == 0) return std::nullopt;
  return lonely_since_ + lonely_grace_us_;
}

void Downed::set(DownedState next, DownedActions& out, const std::string& why) {
  out.log.push_back(std::string(to_string(state_)) + " -> " + to_string(next) +
                    (why.empty() ? "" : ": " + why));
  state_ = next;
  asked_by_.reset();
  lonely_since_ = 0;
  progress_at_ = 0;
  if (next == DownedState::alive || next == DownedState::game_over) defeat_ = 0;
  heal_target_ = 0;  // a revive's heal is set after this, on its own transition
  if (next != DownedState::downed) holding_ = redown_ = false;
}

void Downed::start_heal(const DownedInput& in, DownedActions& out) {
  // Only a mate's revive: our own fairy heals as the game's fairy does.
  if (by_ == 0 || !in.vitals.life || !in.vitals.max_life) return;
  const std::int32_t target = revive_life(*in.vitals.max_life);
  if (*in.vitals.life >= target) return;  // the fairy left enough
  heal_target_ = target;
  heal_tries_ = 1;
  heal_at_ = std::max<std::uint64_t>(in.now_us, 1);
  out.life = LifeWrite{target, false};
  out.log.push_back(text("life %d of %d after the revive, less than max(3 hearts, 25 %%) = %d: written "
                         "through the game's queue (PlayerStatus.Life)",
                         *in.vitals.life, *in.vitals.max_life, target));
}

void Downed::heal(const DownedInput& in, DownedActions& out) {
  if (!in.vitals.life || in.vitals.dead()) return;  // not read now; a new death is the caller's
  if (*in.vitals.life >= heal_target_) {
    out.log.push_back(text("life %d of %d after the revive (max(3 hearts, 25 %%), %d write%s)",
                           *in.vitals.life, in.vitals.max_life.value_or(-1), heal_tries_,
                           heal_tries_ == 1 ? "" : "s"));
    heal_target_ = 0;
    return;
  }
  if (in.now_us - heal_at_ < settings_.heal_check_us) return;
  if (heal_tries_ >= settings_.heal_tries) {
    out.log.push_back(text("the life after the revive stays %d of %d (wanted %d): given up after %d writes",
                           *in.vitals.life, in.vitals.max_life.value_or(-1), heal_target_, heal_tries_));
    heal_target_ = 0;
    return;
  }
  ++heal_tries_;
  heal_at_ = std::max<std::uint64_t>(in.now_us, 1);
  out.life = LifeWrite{heal_target_, true};
  out.log.push_back(text("life still %d of %d %.1f s after the write: written straight into the Life "
                         "component (try %d)",
                         *in.vitals.life, in.vitals.max_life.value_or(-1),
                         static_cast<double>(settings_.heal_check_us) / 1e6, heal_tries_));
}

bool Downed::hold(const DownedInput& in, DownedActions& out) {
  if (in.hold_down) {
    if (!holding_) {
      holding_ = true;
      redown_ = false;
      out.log.push_back("our scene reloads at the other age as we lie here: we stay down through it "
                        "(D-R62, oot-64 D-178)");
    }
    return true;
  }
  if (holding_) {
    holding_ = false;
    redown_ = true;
    redown_since_ = std::max<std::uint64_t>(in.now_us, 1);
    redown_at_ = 0;
    redown_tries_ = 0;
  }
  if (!redown_) return false;
  if (in.vitals.dead()) {
    redown_ = false;
    out.log.push_back(redown_tries_ == 0 ? "the reload is over: still down"
                                         : text("down again after the reload (%d write%s of life 0)",
                                                redown_tries_, redown_tries_ == 1 ? "" : "s"));
    return false;
  }
  const bool timed_out = in.now_us - redown_since_ > settings_.redown_wait_us;
  if (in.vitals.up() && (redown_at_ == 0 || in.now_us - redown_at_ >= settings_.redown_check_us)) {
    if (redown_tries_ >= settings_.redown_tries || timed_out) {
      redown_ = false;
      out.log.push_back(text("the reload got Link up and %d writes of life 0 did not put him down again",
                             redown_tries_));
      return false;  // up: the caller takes it as up without a revive
    }
    ++redown_tries_;
    redown_at_ = std::max<std::uint64_t>(in.now_us, 1);
    out.life = LifeWrite{0, true};
    out.log.push_back(text("the reload got Link up (life %d of %d): put down again where he stands, life "
                           "0 (try %d)",
                           *in.vitals.life, in.vitals.max_life.value_or(-1), redown_tries_));
    return true;
  }
  if (timed_out && !in.vitals.up()) {
    // Neither dead nor up all this time (not read): down as before, as we were.
    redown_ = false;
    out.log.push_back("the reload is over: Link not read up, still down");
    return false;
  }
  return true;
}

void Downed::start(std::uint8_t by, DownedActions& out) {
  by_ = by;
  phase_ = Phase::write;
  tries_ = 0;
  opened_at_ = first_open_at_ = 0;
  set(DownedState::reviving, out,
      by ? text("revive requested by P%u", static_cast<unsigned>(by)) : "a fairy in our own bottle");
}

void Downed::restore(DownedActions& out) {
  if (!pick_.own_fairy) out.bottles.push_back({pick_.slot, before_});
  if (quiet_on_) {
    out.quiet = std::set<FlagKey>{};
    quiet_on_ = false;
  }
}

void Downed::own_fairy(const DownedInput& in, DownedActions& out) {
  if (in.event_running.value_or(false) && !fairy_event_) {
    fairy_event_ = true;
    out.log.push_back(text("game over event running %.2f s after the death (our own fairy)",
                           static_cast<double>(in.now_us - fairy_since_) / 1e6));
  }
  const bool over = in.event_running.has_value() && !*in.event_running;
  if (over && in.vitals.up()) {
    set(DownedState::alive, out,
        text("up by our own fairy (life %d of %d, %.2f s after the death)", *in.vitals.life,
             in.vitals.max_life.value_or(-1),
             static_cast<double>(in.now_us - fairy_since_) / 1e6));
    return;
  }
  if (in.now_us > fairy_since_ && in.now_us - fairy_since_ > settings_.revive_wait_us) {
    // The fairy branch never got us up: the event is on its game over (the
    // gate stays open), as it would be with no co-op.
    set(DownedState::game_over, out, "our own fairy did not get us up: the game over runs");
  }
}

void Downed::reviving(const DownedInput& in, DownedActions& out) {
  const std::uint32_t fairy = bottle_value("Fairy");
  switch (phase_) {
    case Phase::write: {
      const auto pick = pick_bottle(in.bottles);
      if (!pick) {
        set(DownedState::downed, out, "no bottle free for the fairy, stays down");
        return;
      }
      pick_ = *pick;
      before_ = *in.bottles[static_cast<std::size_t>(pick_.slot)];
      started_at_ = std::max<std::uint64_t>(in.now_us, 1);
      event_wait_logged_ = false;
      if (!pick_.own_fairy) {
        if (pick_.synthetic) {
          const BottleKeys k = bottle_keys(pick_.slot);
          out.quiet = std::set<FlagKey>{FlagKey{k.valid, 0}, FlagKey{k.stock, 0}};
          quiet_on_ = true;
        }
        out.bottles.push_back(
            {pick_.slot, Bottle{fairy, true, std::max<std::int32_t>(before_.stock, 1)}});
      }
      out.log.push_back(text("fairy in bottle %c (%s)", letter(pick_.slot),
                             pick_.own_fairy    ? "our own"
                             : pick_.synthetic ? "a bottle not owned, faked whole"
                                                : "an empty bottle of ours"));
      phase_ = Phase::open;
      return;
    }
    case Phase::open: {
      if (opened_at_ == 0) {
        // Open once the fairy is there (written last frame).
        const auto& b = in.bottles[static_cast<std::size_t>(pick_.slot)];
        if (!b || b->item != fairy) {
          if (++tries_ >= settings_.tries) {
            restore(out);
            set(DownedState::downed, out, "the fairy did not stay in the bottle, stays down");
            return;
          }
          out.bottles.push_back(
              {pick_.slot, Bottle{fairy, true, std::max<std::int32_t>(before_.stock, 1)}});
          return;
        }
        // An event already running before the gate first opens (a cutscene,
        // a boss intro) is not the game over event: taken for it, the revive
        // would wait on the wrong event and, past revive_wait_us, give up
        // into the game over. The gate opens once it is over.
        if (first_open_at_ == 0 && in.event_running.value_or(false)) {
          if (in.now_us - started_at_ > settings_.revive_wait_us) {
            restore(out);
            set(DownedState::downed, out, "another event ran all this time, stays down");
            return;
          }
          if (!event_wait_logged_) {
            event_wait_logged_ = true;
            out.log.push_back("another event runs: the gate opens once it is over");
          }
          return;
        }
        opened_at_ = std::max<std::uint64_t>(in.now_us, 1);
        if (first_open_at_ == 0) first_open_at_ = opened_at_;
        out.log.push_back("gate open");
        return;
      }
      if (in.event_running.value_or(false)) {
        phase_ = Phase::event;
        out.log.push_back(text("game over event running %.2f s after the gate opened",
                               static_cast<double>(in.now_us - opened_at_) / 1e6));
        return;
      }
      if (in.now_us - opened_at_ > settings_.start_wait_us) {
        if (++tries_ >= settings_.tries) {
          restore(out);
          set(DownedState::downed, out, "the game over event did not start, stays down");
          return;
        }
        opened_at_ = 0;  // shut one frame, check the fairy, open again
        out.log.push_back(text("no event %.1f s after the gate opened, try %d",
                               static_cast<double>(settings_.start_wait_us) / 1e6, tries_ + 1));
      }
      return;
    }
    case Phase::event: {
      const bool over = in.event_running.has_value() && !*in.event_running;
      if (over && in.vitals.up()) {
        restore(out);
        set(DownedState::alive, out,
            text("revived (%s, life %d of %d, %.2f s after the gate opened)",
                 by_ ? text("by P%u", unsigned{by_}).c_str() : "our own fairy", *in.vitals.life,
                 in.vitals.max_life.value_or(-1),
                 static_cast<double>(in.now_us - first_open_at_) / 1e6));
        start_heal(in, out);  // a mate's revive: at least max(3 hearts, 25 %)
        return;
      }
      if (over && in.vitals.dead()) {
        restore(out);
        set(DownedState::downed, out, "the event ended with Link still down");
        return;
      }
      if (in.now_us - first_open_at_ > settings_.revive_wait_us) {
        restore(out);
        set(DownedState::game_over, out, "no fairy branch: the game over runs");
      }
      return;
    }
  }
}

DownedActions Downed::tick(const DownedInput& in) {
  DownedActions out;
  if (in.manual) {
    // The test command `gate` has the gate: follow Link for the status bit.
    out.write_gate = false;
    if (quiet_on_) restore(out);
    if (state_ != DownedState::downed && in.vitals.dead()) {
      set(DownedState::downed, out, "dead (the gate is in the test's hands)");
    } else if (state_ != DownedState::alive && in.vitals.up()) {
      set(DownedState::alive, out, "up (the gate is in the test's hands)");
    }
    return out;
  }
  // The room: alone (or no co-op), the game's own death and game over.
  const bool solo = !in.coop || (in.connected && in.mates <= 0);
  const int room = !in.coop ? 0 : !in.connected ? 1 : solo ? 2 : 3;
  if (room_ != room) {
    static constexpr const char* kRoom[] = {
        "no co-op session: the game's own death and game over",
        "not connected: a death leaves us down (a mate may be back after the reconnect)",
        "alone in the room: the game's own death and game over",
        "a mate in the room: a death leaves us down"};
    out.log.push_back(kRoom[room]);
    room_ = room;
  }
  if (state_ == DownedState::downed) {
    // Down with nobody to get us up: a grace, then our own game over.
    const bool lonely = solo || !in.connected;
    const std::uint64_t grace = solo ? settings_.alone_grace_us : settings_.offline_grace_us;
    if (lonely && (lonely_since_ == 0 || grace != lonely_grace_us_)) {
      if (lonely_since_ == 0) lonely_since_ = std::max<std::uint64_t>(in.now_us, 1);
      lonely_grace_us_ = grace;
      out.log.push_back(text("%s: the game over %.0f s after it unless a mate comes",
                             solo ? "no mate left in the room" : "our connection is lost",
                             static_cast<double>(grace) / 1e6));
    } else if (!lonely && lonely_since_ != 0) {
      lonely_since_ = 0;
      out.log.push_back(text("%d mate%s in the room again: stays down", in.mates,
                             in.mates == 1 ? "" : "s"));
    }
  }
  switch (state_) {
    case DownedState::alive:
      if (heal_target_ != 0) heal(in, out);
      if (in.vitals.dead()) {
        const auto pick = pick_bottle(in.bottles);
        if (solo) {
          out.log.push_back("solo, gate open");
          set(DownedState::game_over, out, "dead alone: the game's own game over");
        } else if (pick && pick->own_fairy) {
          fairy_since_ = std::max<std::uint64_t>(in.now_us, 1);
          fairy_event_ = false;
          set(DownedState::fairy, out,
              text("a fairy in our own bottle %c: the gate open, the game's fairy branch",
                   letter(pick->slot)));
          own_fairy(in, out);
        } else {
          set(DownedState::downed, out, "");
        }
      }
      break;
    case DownedState::downed: {
      if (hold(in, out)) break;
      if (in.vitals.up()) {
        set(DownedState::alive, out, "up without a revive");
        break;
      }
      if (defeat_ != 0) {
        set(DownedState::game_over, out,
            text("group defeat (%u down): the game over runs", unsigned{defeat_}));
        break;
      }
      if (lonely_since_ != 0 && in.now_us - lonely_since_ >= lonely_grace_us_) {
        set(DownedState::game_over, out,
            solo ? "nobody came back: group defeat of one, the game over runs"
                 : "no connection all this time: group defeat of one, the game over runs");
        break;
      }
      const auto pick = pick_bottle(in.bottles);
      if (pick && pick->own_fairy) {
        start(0, out);
      } else if (asked_by_) {
        start(*asked_by_, out);
      } else {
        break;
      }
      reviving(in, out);
      break;
    }
    case DownedState::reviving:
      if (defeat_ != 0) {
        // D-R66: a GROUP_DEFEAT that comes anyway is everyone's, ours too
        // while getting up: the bottle back as it was, the game over runs.
        if (phase_ != Phase::write) restore(out);
        set(DownedState::game_over, out,
            text("group defeat (%u down) while getting up: the game over runs for everyone (D-R66)",
                 unsigned{defeat_}));
        break;
      }
      reviving(in, out);
      break;
    case DownedState::fairy:
      own_fairy(in, out);
      break;
    case DownedState::game_over:
      if (in.vitals.up()) set(DownedState::alive, out, "back after the game over");
      break;
  }
  out.gate_closed = !(state_ == DownedState::game_over || state_ == DownedState::fairy ||
                      (state_ == DownedState::alive && solo) ||
                      (state_ == DownedState::reviving && opened_at_ != 0));
  return out;
}

// ---- ReviveWatch -------------------------------------------------------------

ReviveOutput ReviveWatch::tick(const ReviveInput& in) {
  ReviveOutput out;
  std::array<bool, protocol::kMaxPlayers + 1> listed{};
  for (const Mate& m : in.mates) {
    if (m.number == 0 || m.number > protocol::kMaxPlayers) continue;
    listed[m.number] = true;
    const bool down = (m.status & protocol::status::dead) != 0;
    if (down != down_[m.number]) {
      out.log.push_back(down ? text("P%u is down (PLAYER_STATUS dead)", unsigned{m.number})
                             : text("P%u is up again", unsigned{m.number}));
      (down ? out.went_down : out.got_up).push_back(m.number);
      down_[m.number] = down;
    }
  }
  for (std::size_t n = 1; n < listed.size(); ++n) {
    if (!listed[n]) down_[n] = false;  // gone: no line, it may come back
  }

  // The nearest downed mate in our scene within reach.
  std::uint8_t best = 0;
  double best_d = 0.0;
  if (in.connected && !in.self_down && in.position && in.scene) {
    for (const Mate& m : in.mates) {
      if (m.number == 0 || m.number > protocol::kMaxPlayers || !down_[m.number] ||
          !m.position || !m.scene || *m.scene != *in.scene) {
        continue;
      }
      const double d = distance(*in.position, *m.position);
      if (d <= settings_.reach && (best == 0 || d < best_d)) {
        best = m.number;
        best_d = d;
      }
    }
  }
  out.in_reach = best;
  if (best == 0 || !in.a_held.value_or(false)) {
    if (target_ != 0 && !sent_) {
      out.log.push_back(text("let go of P%u after %.1f s", unsigned{target_},
                             static_cast<double>(in.now_us - since_) / 1e6));
    }
    target_ = 0;
    sent_ = false;
    return out;
  }
  if (best != target_) {
    target_ = best;
    since_ = in.now_us;
    sent_ = false;
    progress_at_ = 0;
    out.log.push_back(text("holding A beside P%u (%.2f away)", unsigned{best}, best_d));
  }
  const std::uint64_t held = in.now_us - since_;
  const int percent = static_cast<int>(std::min<std::uint64_t>(
      settings_.hold_us == 0 ? 100 : held * 100 / settings_.hold_us, 100));
  out.holding_percent = sent_ ? 100 : percent;
  if (!sent_ && held < settings_.hold_us &&
      (progress_at_ == 0 || in.now_us - progress_at_ >= settings_.progress_us)) {
    out.progress_to = target_;
    out.progress = static_cast<std::uint8_t>(percent);
    progress_at_ = std::max<std::uint64_t>(in.now_us, 1);
  }
  if ((!sent_ && in.now_us - since_ >= settings_.hold_us) ||
      (sent_ && in.now_us - sent_at_ >= settings_.again_us)) {
    out.request = target_;
    out.log.push_back(text("A held %.1f s beside P%u (%.2f away): revive request%s",
                           static_cast<double>(in.now_us - since_) / 1e6, unsigned{target_}, best_d,
                           sent_ ? " (again)" : ""));
    sent_ = true;
    sent_at_ = in.now_us;
  }
  return out;
}

// ---- The line on the screen --------------------------------------------------

namespace {
// UTF-8 spelled out, whatever the compiler's source character set.
constexpr const char* kAacute = "\xc3\xa1";
constexpr const char* kEacute = "\xc3\xa9";
constexpr const char* kIacute = "\xc3\xad";
constexpr const char* kNtilde = "\xc3\xb1";
constexpr const char* kOacute = "\xc3\xb3";
}  // namespace

std::string banner_text(const BannerView& v) {
  if (v.state == DownedState::downed || v.state == DownedState::reviving) {
    if (v.reviver) {
      return *v.reviver + " te est" + kAacute + " reviviendo " +
             std::to_string(std::clamp(v.reviver_percent, 0, 100)) + " %";
    }
    if (v.state == DownedState::reviving) return "Te levantas...";
    if (v.alone_left_us) {
      const auto s = (*v.alone_left_us + 999'999) / 1'000'000;
      return std::string("Has ca") + kIacute + "do y no queda nadie: game over en " +
             std::to_string(s) + " s";
    }
    return std::string("Has ca") + kIacute + "do: un compa" + kNtilde +
           "ero puede revivirte (A 3 s a tu lado)";
  }
  if (v.state == DownedState::game_over && v.group_defeat) {
    return std::string("Todos hab") + kEacute + "is ca" + kIacute + "do: game over";
  }
  if (v.state == DownedState::alive && v.target) {
    std::string line = std::string("Mant") + kEacute + "n A para revivir a " + *v.target;
    if (v.holding_percent) {
      line += " " + std::to_string(std::clamp(*v.holding_percent, 0, 100)) + " %";
    }
    return line;
  }
  return {};
}

std::string mate_down_notice(const std::string& name) {
  return name + " ha ca" + kIacute + "do: ve a su lado y mant" + kEacute + "n A";
}

std::string mate_up_notice(const std::string& name) { return name + " vuelve a estar en pie"; }

std::string connection_text(bool welcomed_once, bool connected, std::uint64_t lost_for_ms) {
  if (!welcomed_once || connected || lost_for_ms < 1000) return {};
  return std::string("Sin conexi") + kOacute + "n con el servidor: reconectando (" +
         std::to_string(lost_for_ms / 1000) + " s)";
}

std::string join_screen_lines(const std::string& first, const std::string& second) {
  if (first.empty()) return second;
  if (second.empty()) return first;
  return first + "   |   " + second;
}

std::string downed_tag_note(std::optional<float> metres) {
  std::string note = std::string("ca") + kIacute + "do";
  if (metres && std::isfinite(*metres) && *metres >= 0.0f && *metres < 100'000.0f) {
    note += ", " + std::to_string(static_cast<int>(std::lround(*metres))) + " m";
  }
  return note;
}

}  // namespace coop::host
