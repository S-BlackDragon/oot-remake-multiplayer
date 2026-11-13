#include "coop/host/flag_watcher.hpp"

#include <algorithm>
#include <limits>

#include "coop/protocol/codec.hpp"

namespace coop::host {
namespace {

using protocol::FlagEntry;

void push(std::vector<Outgoing>& out, const protocol::Message& message) {
  if (auto bytes = protocol::encode(message)) {
    out.push_back({protocol::channel_of(message), std::move(*bytes)});
  }
}

bool is_delta(const FlagEntry& e) { return e.kind == protocol::FlagKind::count_delta; }

// Saved progress going back: a Bool from set to 0, an Int lower.
bool went_back(const FlagEntry& before, const FlagEntry& now) {
  if (before.kind != now.kind) return false;
  if (now.kind == protocol::FlagKind::boolean) return before.value != 0 && now.value == 0;
  if (now.kind == protocol::FlagKind::int32) {
    return static_cast<std::int32_t>(now.value) < static_cast<std::int32_t>(before.value);
  }
  return false;
}

// The game's value `now` is older than the group's (FlagWatcher::set_loading):
// a Bool back from 1 to 0, or, while there is no Link at all (`loading`),
// an Int (not a count) back to exactly the value it held before the group's
// value came (`before`). Any other Int change is the game's own (a trade
// item handed on in the settle time, more progress in the save) and goes out.
bool backwards(const FlagEntry& group, const FlagEntry* before, const FlagEntry& now,
               bool loading) {
  if (group.kind == protocol::FlagKind::boolean) {
    return group.value != 0 && now.value == 0;
  }
  return loading && group.kind == protocol::FlagKind::int32 && before != nullptr &&
         now == *before && now != group;
}

std::int64_t as_int(std::uint32_t bits) { return static_cast<std::int32_t>(bits); }

// A max-rule value not written yet (held, or refused) that is above the
// game's own new value: the game's change does not replace it (issue 193).
bool held_is_higher(FlagStore& store, const protocol::FlagEntry& held, const protocol::FlagEntry& now) {
  return held.kind == protocol::FlagKind::int32 && now.kind == protocol::FlagKind::int32 &&
         store.is_max(held.key) && as_int(held.value) > as_int(now.value);
}

std::uint32_t as_bits(std::int64_t v) {
  v = std::clamp<std::int64_t>(v, std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max());
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
}

// FLAG_SET or FLAG_STATE messages for `table`, at most kMaxFlagEntries each.
// A message the codec refuses (an entry this kind cannot carry, such as a
// count delta in a FLAG_STATE) is sent again without the entries it refuses,
// so one bad entry never takes the others with it; `unsent` counts those.
template <typename M>
void push_list(std::vector<Outgoing>& out, const std::map<FlagKey, FlagEntry>& table,
               std::uint64_t& unsent) {
  auto it = table.begin();
  while (it != table.end()) {
    M message;
    message.player_number = protocol::kPlayerSelf;
    while (it != table.end() && message.flags.size() < protocol::kMaxFlagEntries) {
      message.flags.push_back(it->second);
      ++it;
    }
    if (protocol::encode(message)) {
      push(out, message);
      continue;
    }
    M good;
    good.player_number = message.player_number;
    for (const FlagEntry& e : message.flags) {
      M one;
      one.player_number = message.player_number;
      one.flags.push_back(e);
      if (protocol::encode(one)) {
        good.flags.push_back(e);
      } else {
        ++unsent;
      }
    }
    if (!good.flags.empty()) push(out, good);
  }
}

}  // namespace

bool FlagWatcher::receive(const protocol::Message& message) {
  if (const auto* set = std::get_if<protocol::FlagSet>(&message)) {
    if (role_ == FlagRole::host) {
      queue_.emplace_back(*set);
      return true;
    }
  } else if (const auto* state = std::get_if<protocol::FlagState>(&message)) {
    // Both roles: a host gets FLAG_STATE for its own game too (its client's
    // answer to a FLAG_SET, the server's value on a clash).
    queue_.emplace_back(*state);
    return true;
  } else if (const auto* snapshot = std::get_if<protocol::FlagSnapshot>(&message)) {
    if (role_ == FlagRole::player) {
      queue_.emplace_back(*snapshot);
      return true;
    }
  } else if (const auto* reset = std::get_if<protocol::FlagReset>(&message)) {
    if (role_ == FlagRole::player) {  // a host sent its own
      queue_.emplace_back(*reset);
      return true;
    }
  } else if (const auto* origin = std::get_if<protocol::FlagOrigin>(&message)) {
    queue_.emplace_back(*origin);  // both roles: who asked for the next answer
    return true;
  }
  ++stats_.ignored_messages;
  return false;
}

bool FlagWatcher::receive(std::span<const std::uint8_t> bytes) {
  protocol::Message message;
  if (protocol::decode(bytes, message) != protocol::Error::none) {
    ++stats_.ignored_messages;
    return false;
  }
  return receive(message);
}

bool FlagWatcher::share_one_way(FlagKey key) {
  if (key.kind != FlagKeyKind::named || !one_way_.insert(key).second) return false;
  shared_.add(key);
  return true;
}

bool FlagWatcher::share_pickup(FlagKey key) {
  if (key.kind != FlagKeyKind::key64 || !pickups_.insert(key).second) return false;
  shared_.add(key);
  return true;
}

void FlagWatcher::guard_count(FlagKey count, std::span<const FlagKey> sources) {
  guards_.try_emplace(count);
  for (const FlagKey source : sources) {
    share_pickup(source);
    guard_source_[source] = count;
  }
}

void FlagWatcher::expect_doubled_gain() {
  if (doubled_ == 0 || frames_ > doubled_until_) doubled_ = 0;
  doubled_ = std::min(doubled_ + 1, kMaxDoubledGains);
  doubled_until_ = frames_ + kDoubledGainFrames;
}

bool FlagWatcher::share_pickup_named(FlagKey key) {
  if (key.kind != FlagKeyKind::named || !pickups_.insert(key).second) return false;
  shared_.add(key);
  return true;
}

void FlagWatcher::note_for_others(FlagStore& store, const FlagEntry& entry) {
  const FlagKey key = key_of(entry);
  const bool noted = key.kind == FlagKeyKind::key64 || pickups_.contains(key);
  if (!noted || entry.kind != protocol::FlagKind::boolean || entry.value == 0) return;
  const auto source = guard_source_.find(key);
  if (source == guard_source_.end() && written_for_others_.size() >= kMaxWrittenForOthers) return;
  const std::optional<FlagEntry> before = store.read(key);
  if (before && before->value != 0) return;  // the game has it already (set here)
  // Another player took it, live (a join snapshot is no take now): our game
  // counting its pickup again soon after is a second count (guard_count).
  if (source != guard_source_.end() && received_kind_ == ReceivedKind::live) {
    guards_[source->second].others_at = frames_ + 1;  // never 0
  }
  if (written_for_others_.size() < kMaxWrittenForOthers) written_for_others_.push_back(key);
}

bool FlagWatcher::takes(const FlagEntry& entry) {
  const FlagKey key = key_of(entry);
  const bool one = entry.kind == protocol::FlagKind::boolean && entry.value != 0;
  if (!shared_.contains(key)) {
    if (!learn_ || !one || key.kind != FlagKeyKind::key64 || learned_ >= kMaxLearned) {
      ++stats_.not_shared;
      return false;
    }
    ++learned_;
    ++stats_.pickups_learned;
    share_pickup(key);
    return true;
  }
  if (!one && (pickups_.contains(key) || one_way_.contains(key))) {
    ++stats_.pickup_resets;
    return false;
  }
  return true;
}

void FlagWatcher::set_role(FlagRole role) {
  role_ = role;
  holding_ = false;
  snapshot_.clear();
  held_states_.clear();
  if (role_ == FlagRole::host) {
    // The host's game is the truth now: nothing waits for an answer.
    pending_.clear();
    resend_pending_ = false;
  } else {
    snapshot_requested_ = false;  // ORM: a table held while hosting is no player's to send
    said_held_ = false;
  }
}

void FlagWatcher::set_loading(bool loading) {
  if (loading) {
    loading_ = true;
    settle_left_ = kSettleFrames;
  } else {
    loading_ = false;
  }
}

void FlagWatcher::note_group(const FlagEntry& value, const FlagEntry* before) {
  const FlagKey key = key_of(value);
  group_[key] = value;
  if (before != nullptr && *before != value) {
    group_before_[key] = *before;
  } else {
    group_before_.erase(key);
  }
}

void FlagWatcher::reset() {
  queue_.clear();
  // Another session, or the same server started again: what the group had
  // comes again with the next snapshot.
  group_.clear();
  group_before_.clear();
  group_known_ = false;  // ORM: SONDAS.md round 6
  holding_ = false;
  snapshot_.clear();
  held_states_.clear();
  origin_.clear();
  received_.clear();
  // Guards: what others took and our unmatched sources belong to the old
  // session. A held gain stays held, so it is still sent or put back.
  for (auto& [key, g] : guards_) {
    g.credit = 0;
    g.others_at = 0;
    g.put_back = 0;
  }
  resend_pending_ = role_ == FlagRole::player && !pending_.empty();
}

std::vector<Outgoing> FlagWatcher::frame(FlagStore& store) {
  std::vector<Outgoing> out;
  const unsigned step = ticks_.step();  // 1/60 s that passed (issue 106)
  frames_ += step;
  if (!loading_) in_save_ = true;  // ORM: a Link is read: our game has a save in

  // Writes that failed before go first, so newer incoming values win. One
  // whose flag the game has changed by itself since is dropped instead: the
  // game's change is newer, and the diff below sends it.
  const Table retry = unwritten_;
  for (const auto& [key, entry] : retry) {
    if (quiet_.contains(key)) continue;  // held until the game has its real value back
    // A count total is written on top of the game's own change (write()),
    // so it is never dropped for one.
    if (const auto known = last_.find(key); known != last_.end() && !store.is_count(key)) {
      std::optional<FlagEntry> now = store.read(key);
      if (now) {
        now->key = key;
        if (*now != known->second && !held_is_higher(store, entry, *now)) {
          unwritten_.erase(key);
          continue;
        }
      }
    }
    write(store, entry);
  }
  const auto deltas = std::move(unadded_);
  unadded_.clear();
  for (const auto& [key, entry] : deltas) add(store, entry);

  Table answers;  // host: FLAG_STATE entries answering a FLAG_SET
  auto queue = std::move(queue_);
  queue_.clear();
  for (auto& message : queue) handle(store, message, answers);

  std::vector<Outgoing> resets;
  if (continue_mode_ == ContinueMode::keep && !window_ && !settling() && !keep_extra_taken_) {
    // ORM: CONTINUAR.md, keep: the per player values to keep, as they are at the game over (once: a
    // Continue may move them before its load window opens, REVISION_NUEVAS).
    keep_extra_taken_ = true;
    keep_extra_.clear();
    for (const FlagKey& key : keep_extra_keys_) {
      if (shared_.contains(key)) continue;  // shared: kept with the others
      if (std::optional<FlagEntry> now = store.read(key)) {
        now->key = key;
        keep_extra_[key] = *now;
      }
    }
  }
  if (settling() && !window_) {
    window_ = true;
    window_from_save_ = in_save_;  // ORM: from a save (a door, a reload) or from the title screen
    moved_ = false;
    keep_ = false;
    back_keys_.clear();
    no_scene_frames_ = 0;
  } else if (!settling() && window_) {
    window_ = false;
    end_window(store, resets);
  }
  if (window_ && loading_ && !scene_readable_) no_scene_frames_ += static_cast<int>(step);
  // ORM: the title screen or file select (no scene for kTitleFrames): the game's flags are no save's now.
  if (window_ && loading_ && no_scene_frames_ >= kTitleFrames) in_save_ = false;
  if (window_ && loading_ && continue_mode_ != ContinueMode::off && !moved_) {  // ORM: CONTINUAR.md
    moved_ = true;  // a Continue's reload: a save load
    keep_ = continue_mode_ == ContinueMode::keep;
    ++stats_.continue_loads;
  }
  if (window_ && keep_) keep_extra(store);
  see_save_load();

  Table changes;
  if (!holding_) {
    if (resend_pending_) {
      changes = pending_;
      resend_pending_ = false;
    }
    diff(store, changes);
  }
  if (!loading_ && settle_left_ > 0) settle_left_ -= (std::min)(settle_left_, static_cast<int>(step));
  restate_.clear();
  added_.clear();

  if (role_ == FlagRole::player) {
    push_list<protocol::FlagSet>(out, changes, stats_.entries_unsent);
  } else {
    // Answers already updated last_, so the two never hold the same flag.
    for (const auto& [key, total] : keep_totals_) answers[key] = total;  // ORM: keep_count_delta
    keep_totals_.clear();
    answers.merge(changes);
    push_list<protocol::FlagState>(out, answers, stats_.entries_unsent);
    out.insert(out.end(), resets.begin(), resets.end());
    if (snapshot_requested_) {
      if (in_save_ && !loading_) {
        send_snapshot(store, out);
        snapshot_requested_ = false;
        if (std::exchange(said_held_, false)) {
          notes_.emplace_back("our table sent: our save is in now (held while our game had none)");
        }
      } else if (!said_held_) {
        // ORM (SONDAS.md round 6): a new game's flags are no group's; sent once a Link is read.
        said_held_ = true;
        ++stats_.snapshots_held;
        notes_.emplace_back(
            "we host with no save in (the title screen): our table is held until our save is in, so no "
            "joiner gets a new game's flags (3 hearts, no magic)");
      }
    }
  }
  return out;
}

void FlagWatcher::handle(FlagStore& store, Incoming& message, Table& answers) {
  if (auto* origin = std::get_if<protocol::FlagOrigin>(&message)) {
    // An asker whose answer never came (a send failed) is not kept past its
    // frames, so it never names a later change to the same flag.
    std::erase_if(origin_, [&](const auto& o) { return frames_ - o.second.second > kOriginFrames; });
    if (origin_.size() > 4 * kMaxReceived) origin_.clear();
    for (const auto& entry : origin->flags) origin_[key_of(entry)] = {origin->player_number, frames_};
    return;
  }
  if (auto* set = std::get_if<protocol::FlagSet>(&message)) {
    if (role_ != FlagRole::host) {
      ++stats_.ignored_messages;
      return;
    }
    from_ = set->player_number;
    received_kind_ = ReceivedKind::live;
    recording_ = true;
    answer_set(store, *set, answers);
    recording_ = false;
    return;
  }
  if (auto* reset = std::get_if<protocol::FlagReset>(&message)) {
    if (role_ != FlagRole::player) {
      ++stats_.ignored_messages;
      return;
    }
    from_ = reset->player_number;
    received_kind_ = ReceivedKind::reset;
    recording_ = true;
    apply_reset(store, reset->flags);
    recording_ = false;
    reset_seen_ = true;
    return;
  }
  if (auto* state = std::get_if<protocol::FlagState>(&message)) {
    if (holding_) {
      held_states_.push_back(std::move(*state));
    } else {
      from_ = state->player_number;
      received_kind_ = ReceivedKind::live;
      recording_ = true;
      apply_state(store, state->flags);
      recording_ = false;
    }
    return;
  }
  if (role_ != FlagRole::player) {
    ++stats_.ignored_messages;
    return;
  }

  auto& part = std::get<protocol::FlagSnapshot>(message);
  if (part.part >= part.part_count) {
    ++stats_.bad_snapshots;
    return;
  }
  if (part.part == 0) {
    if (holding_) ++stats_.bad_snapshots;  // the earlier one never finished
    holding_ = true;
    snapshot_.clear();
    snapshot_next_part_ = 0;
    snapshot_part_count_ = part.part_count;
  } else if (!holding_ || part.part != snapshot_next_part_ ||
             part.part_count != snapshot_part_count_) {
    // A part missing or out of order: drop the snapshot, stop holding the
    // game, and apply what came after it.
    ++stats_.bad_snapshots;
    holding_ = false;
    snapshot_.clear();
    auto held = std::move(held_states_);
    held_states_.clear();
    for (const auto& state : held) apply_state(store, state.flags);
    return;
  }
  for (const auto& entry : part.flags) snapshot_[key_of(entry)] = entry;
  ++snapshot_next_part_;
  if (snapshot_next_part_ == snapshot_part_count_) {
    from_ = 0;
    received_kind_ = ReceivedKind::join;
    recording_ = true;
    apply_snapshot(store);
    recording_ = false;
  }
}

void FlagWatcher::apply_state(FlagStore& store, const std::vector<FlagEntry>& flags) {
  for (const auto& entry : flags) {
    const FlagKey key = key_of(entry);
    if (!takes(entry)) continue;
    if (is_delta(entry)) {
      // A player's count change, handed over by the host's client: the host
      // adds it, and diff() sends the total. A player takes totals only.
      if (role_ == FlagRole::host) {
        const std::uint8_t sender = from_;
        take_origin(key);
        add(store, entry);
        from_ = sender;
      } else {
        ++stats_.ignored_messages;
      }
      continue;
    }
    // This is the host's answer: it wins over the local change.
    pending_.erase(key);
    // Who made it: the asker FLAG_ORIGIN named, else the sender.
    const std::uint8_t sender = from_;
    take_origin(key);
    write(store, entry);
    from_ = sender;
  }
}

void FlagWatcher::take_origin(const FlagKey& key) {
  const auto o = origin_.find(key);
  if (o == origin_.end()) return;
  if (frames_ - o->second.second <= kOriginFrames) from_ = o->second.first;
  origin_.erase(o);
}

void FlagWatcher::apply_reset(FlagStore& store, const std::vector<FlagEntry>& flags) {
  for (const auto& entry : flags) {
    const FlagKey key = key_of(entry);
    if (!shared_.contains(key) || is_delta(entry)) continue;  // no one-way rule here
    pending_.erase(key);  // the host's save wins over a change of ours
    if (quiet_.contains(key)) {  // ORM: faked for now (set_quiet): written once it is not
      ++stats_.quiet_held;
      unwritten_[key] = entry;
      continue;
    }
    note_for_others(store, entry);
    const auto seen = last_.find(key);
    const std::optional<FlagEntry> before =
        seen != last_.end() ? std::optional<FlagEntry>(seen->second) : std::nullopt;
    if (!store.write_exact(entry)) {
      ++stats_.write_failures;
      unwritten_[key] = entry;
      if (!written_for_others_.empty() && written_for_others_.back() == key) {
        written_for_others_.pop_back();
      }
      continue;
    }
    ++stats_.applied;
    ++stats_.reset_applied;
    group_known_ = true;  // ORM: SONDAS.md round 6
    unwritten_.erase(key);
    if (!store.is_count(key)) note_group(entry, nullptr);
    last_[key] = entry;
    if (before) note_received(key, *before, entry);
  }
}

void FlagWatcher::see_save_load() {
  if (!window_ || !loading_ || moved_) return;
  if (static_cast<int>(back_keys_.size()) < kSaveLoadMin || no_scene_frames_ < kTitleFrames) return;
  moved_ = true;
  seen_flags_ = static_cast<int>(back_keys_.size());
  seen_frames_ = no_scene_frames_;
}

// ORM: CONTINUAR.md, keep: one value the Continue's reload moved goes back to the snapshot. True when it is
// written (or held to write once the game takes it).
bool FlagWatcher::keep_back(FlagStore& store, const FlagKey& key, const FlagEntry& want, bool own) {
  if (!own && store.holds(key)) {
    // Our game takes it later (HoldFlagStore: a dungeon's flag while we are in its area), as a player's save
    // load does in diff(): the game's value is the last seen, so the retry does not drop the write and the
    // diff after the window does not send the reload's value as the game's own change (orm/continuar 60f613b).
    unwritten_[key] = want;
    if (shared_.contains(key)) {
      if (std::optional<FlagEntry> now = store.read(key)) {
        now->key = key;
        last_[key] = *now;
      }
    }
    return true;
  }
  if (!(own ? store.write_own(want) : store.write_exact(want))) {
    ++stats_.write_failures;
    return false;
  }
  ++stats_.continue_kept;
  ++stats_.applied;
  unwritten_.erase(key);
  return true;
}

void FlagWatcher::keep_extra(FlagStore& store) {
  for (const auto& [key, want] : keep_extra_) {
    std::optional<FlagEntry> now = store.read(key);
    if (!now) continue;
    now->key = key;
    if (*now != want) keep_back(store, key, want, true);
  }
}

void FlagWatcher::end_window(FlagStore& store, std::vector<Outgoing>& out) {
  if (!moved_) return;  // a door: the next diff sends what changed, as always
  if (keep_) {  // ORM: CONTINUAR.md, keep: the group's values from before the reload, for everyone
    keep_ = false;
    keep_extra(store);
    keep_extra_.clear();
    for (const FlagKey key : shared_.keys()) {
      if (quiet_.contains(key) || one_way_.contains(key)) continue;
      std::optional<FlagEntry> now = store.read(key);
      if (!now) continue;
      now->key = key;
      const auto it = last_.find(key);
      if (it != last_.end() && it->second != *now) keep_back(store, key, it->second);
    }
    return;
  }
  Table changed;
  // ORM (SONDAS.md round 6): a player's save load from the title screen, before any group value came (the
  // host has no save in yet: it holds its table), has no group to go back to: what this game had "before" is
  // a new game's flags (3 hearts, no magic), so the save's own values are kept. With a group value come, or a
  // reload from a save, the values go back as before.
  const bool keep_own = role_ == FlagRole::player && !window_from_save_ && !group_known_;
  std::size_t kept = 0;
  std::size_t reverted = 0;
  for (const FlagKey key : shared_.keys()) {
    if (quiet_.contains(key)) continue;  // ORM: a fake value is never sent nor reverted
    std::optional<FlagEntry> now = store.read(key);
    if (!now) continue;
    now->key = key;
    const auto it = last_.find(key);
    if (it == last_.end() || it->second == *now) continue;
    // A room's own switch state the game resets itself (one way) is the
    // room's: never reset for the group, never put back.
    if (one_way_.contains(key)) continue;
    if (role_ == FlagRole::host) {
      changed[key] = *now;  // the save's value goes to everyone
      last_[key] = *now;
      if (!store.is_count(key)) note_group(*now, nullptr);
    } else if (keep_own) {
      last_[key] = *now;  // ours: not sent (a guest's load never changes the group), not put back
      ++stats_.load_kept_own;
      ++kept;
    } else if (store.write_exact(it->second)) {
      ++stats_.load_reverts;  // back to the group's value from before the load
      ++stats_.applied;
      ++reverted;
    }
  }
  if (kept != 0) {
    notes_.emplace_back("our save load from the title screen: " + std::to_string(kept) +
                        " values of our save kept (no group value had come: the host has no save in yet)");
  } else if (reverted != 0) {
    notes_.emplace_back("our save load: " + std::to_string(reverted) + " values back to the group's (" +
                        (window_from_save_ ? "a reload from a save" : "from the title screen, the group known") +
                        ")");
  }
  if (role_ == FlagRole::host && !changed.empty()) {
    ++stats_.host_resets;
    reset_seen_ = true;
    push_list<protocol::FlagReset>(out, changed, stats_.entries_unsent);
  }
}

void FlagWatcher::apply_snapshot(FlagStore& store) {
  holding_ = false;
  for (const auto& [key, entry] : snapshot_) {
    if (!takes(entry)) {
      // not shared here, or a pickup's 0
    } else if (pending_.contains(key)) {
      ++stats_.kept_local;  // the host's answer to it is still to come
    } else {
      write(store, entry);
    }
  }
  snapshot_.clear();
  auto held = std::move(held_states_);
  held_states_.clear();
  for (const auto& state : held) apply_state(store, state.flags);
}

void FlagWatcher::answer_set(FlagStore& store, const protocol::FlagSet& set, Table& answers) {
  for (const auto& entry : set.flags) {
    const FlagKey key = key_of(entry);
    if (!takes(entry)) continue;
    if (const auto total = keep_count_delta(store, entry)) {
      // ORM: our Continue reloads; the delta goes on the group's value (keep_count_delta).
      ++stats_.applied;
      ++stats_.deltas_added;
      keep_totals_.erase(key);
      answers[key] = *total;
      ++stats_.answers_sent;
      continue;
    }
    note_for_others(store, entry);
    const bool written = store.write(entry);
    if (!written && is_delta(entry) && (store.holds(key) || store.busy(key))) {
      // A count change (rupees, keys) refused for now: summed with any other
      // for the same count and added on a later frame (add()), which then
      // states the new total. No answer now: a FLAG_STATE carries no delta.
      ++stats_.write_failures;
      if (!written_for_others_.empty() && written_for_others_.back() == key) {
        written_for_others_.pop_back();
      }
      auto [it, fresh] = unadded_.try_emplace(key, entry);
      if (!fresh) it->second.value = as_bits(as_int(it->second.value) + as_int(entry.value));
      continue;
    }
    // Held, or refused only for room in the game's queue (issue 175): kept and
    // written on a later frame, and answered with the value asked. Answering
    // with the game's old value would turn the player's change back.
    const bool held = !written && (store.holds(key) || store.busy(key));
    if (written) {
      ++stats_.applied;
    } else {
      ++stats_.write_failures;
      if (!written_for_others_.empty() && written_for_others_.back() == key) {
        written_for_others_.pop_back();
      }
      if (held) unwritten_[key] = entry;  // our game takes it later (HoldFlagStore)
    }
    // Answer with what the game holds now, which may not be what was asked;
    // a held value is the group's already, only not in our game yet.
    std::optional<FlagEntry> now = held ? std::optional<FlagEntry>(entry) : store.read(key);
    if (now) {
      now->key = key;
    } else {
      ++stats_.read_failures;
      if (written && !is_delta(entry)) now = entry;
    }
    if (!now) continue;  // no answer: neither written nor readable
    if (const auto g = guards_.find(key); g != guards_.end() && g->second.held != 0 &&
                                          now->kind == protocol::FlagKind::int32) {
      // Our own gain still held (guard_count): not the group's yet. last_
      // keeps the game's value, so diff() does not see it again.
      const FlagEntry game = *now;
      now->value = as_bits(as_int(now->value) - g->second.held);
      if (!held) {
        const auto seen = last_.find(key);
        if (seen != last_.end()) note_received(key, seen->second, *now);
        last_[key] = game;
      }
      answers[key] = *now;
      ++stats_.answers_sent;
      continue;
    }
    if (!is_delta(*now) && !store.is_count(key)) {
      const auto known = last_.find(key);
      note_group(*now, known != last_.end() ? &known->second : nullptr);
    }
    if (!held) {
      const auto seen = last_.find(key);
      if (seen != last_.end() && !is_delta(*now)) note_received(key, seen->second, *now);
      last_[key] = *now;  // held: the game still has its own value
    }
    answers[key] = *now;
    ++stats_.answers_sent;
  }
}

bool FlagWatcher::write(FlagStore& store, const FlagEntry& entry) {
  const FlagKey key = key_of(entry);
  if (quiet_.contains(key)) {  // faked for now (set_quiet): written once it is not
    ++stats_.quiet_held;
    unwritten_[key] = entry;
    return false;
  }
  const auto seen = last_.find(key);
  const std::optional<FlagEntry> before =
      seen != last_.end() ? std::optional<FlagEntry>(seen->second) : std::nullopt;
  FlagEntry to_write = entry;
  if (entry.kind == protocol::FlagKind::int32 && store.is_count(key)) {
    // A count total from the host. A change the game made since last frame
    // is not in it (diff() has not sent it yet): write the total plus that
    // change, and keep the total as last seen, so diff() still sends it.
    const auto known = last_.find(key);
    const std::optional<FlagEntry> now = store.read(key);
    if (known != last_.end() && now && known->second.kind == protocol::FlagKind::int32 &&
        now->kind == protocol::FlagKind::int32 && !(window_ && keep_)) {
      to_write.value = as_bits(as_int(entry.value) + as_int(now->value) -
                               as_int(known->second.value));
    }
  }
  // A gain guard_count holds is in the game but not in the total yet.
  const auto guard = guards_.find(key);
  const std::int32_t held = guard != guards_.end() && entry.kind == protocol::FlagKind::int32
                                ? guard->second.held
                                : 0;
  to_write.value = as_bits(as_int(to_write.value) + held);
  note_for_others(store, to_write);
  if (!store.write(to_write)) {
    ++stats_.write_failures;
    unwritten_[key] = entry;
    if (!written_for_others_.empty() && written_for_others_.back() == key) {
      written_for_others_.pop_back();  // not written: tried again later
    }
    return false;
  }
  ++stats_.applied;
  unwritten_.erase(key);
  group_known_ = true;  // ORM: SONDAS.md round 6
  if (!is_delta(entry) && !store.is_count(key)) {
    const auto known = last_.find(key);
    note_group(entry, known != last_.end() ? &known->second : nullptr);
  }
  last_[key] = entry;  // so the next diff does not send it back
  if (held != 0) last_[key].value = as_bits(as_int(entry.value) + held);
  if (before) note_received(key, *before, entry);
  return true;
}

void FlagWatcher::note_received(const FlagKey& key, const FlagEntry& before, const FlagEntry& after) {
  if (!recording_) return;
  if (before.kind == after.kind && before.value == after.value) {
    // ORM prizes: the same value from another game (take_agreed).
    if (record_agreed_ && received_kind_ == ReceivedKind::live && agreed_.size() < kMaxReceived) {
      agreed_.push_back(Received{key, before, after, from_, received_kind_});
    }
    return;
  }
  if (received_.size() >= kMaxReceived) return;
  received_.push_back(Received{key, before, after, from_, received_kind_});
}

std::optional<FlagEntry> FlagWatcher::keep_count_delta(FlagStore& store, const FlagEntry& delta) {
  const FlagKey key = key_of(delta);
  if (role_ != FlagRole::host || continue_mode_ != ContinueMode::keep || !is_delta(delta) ||
      !store.is_count(key) || guards_.contains(key) || quiet_.contains(key)) {
    return std::nullopt;
  }
  if (window_ && !keep_) return std::nullopt;  // a window that is no Continue's reload (a door)
  const auto known = last_.find(key);
  if (known == last_.end() || known->second.kind != protocol::FlagKind::int32) return std::nullopt;
  FlagEntry total = known->second;
  total.key = key;
  total.value = as_bits(as_int(known->second.value) + as_int(delta.value));
  if (!keep_back(store, key, total)) return std::nullopt;  // the usual path tries it again
  note_received(key, known->second, total);
  last_[key] = total;
  keep_totals_[key] = total;
  ++stats_.continue_counts;
  return total;
}

void FlagWatcher::add(FlagStore& store, const FlagEntry& delta) {
  const FlagKey key = key_of(delta);
  if (!store.is_count(key)) {
    ++stats_.not_shared;
    return;
  }
  if (keep_count_delta(store, delta)) {
    ++stats_.applied;
    ++stats_.deltas_added;
    return;  // answered with keep_totals_; the keep rule holds the total in our game
  }
  // The count before, for the chat notices ("the group has N").
  std::optional<FlagEntry> was = store.read(key);
  if (!store.write(delta)) {
    ++stats_.write_failures;
    // Tried again next frame, with any other change to the same count.
    auto [it, fresh] = unadded_.try_emplace(key, delta);
    if (!fresh) it->second.value = as_bits(as_int(it->second.value) + as_int(delta.value));
    return;
  }
  ++stats_.applied;
  ++stats_.deltas_added;
  if (guards_.contains(key)) {
    auto [a, fresh] = added_.try_emplace(key, delta);
    if (!fresh) a->second.value = as_bits(as_int(a->second.value) + as_int(delta.value));
  }
  if (was && was->kind == protocol::FlagKind::int32) {
    was->key = key;
    FlagEntry now = *was;
    now.value = as_bits(as_int(was->value) + as_int(delta.value));
    note_received(key, *was, now);
  }
  // last_ is left as it was, so diff() sees the new total and sends it; and
  // sends it even when it did not change (the host spent what the player
  // got), as the player waits for it.
  restate_.insert(key);
}

bool FlagWatcher::expect_double_find(FlagKey count, std::uint64_t frames) {
  if (!shared_.contains(count)) return false;
  if (const auto g = own_gains_.find(count); g != own_gains_.end() && frames_ - std::min(frames_, g->second.frame) <= frames) {
    take_back_[count] += g->second.amount;  // already out: taken back at the next diff
    own_gains_.erase(g);
    return true;
  }
  doubles_[count] = frames_ + frames;
  return true;
}

void FlagWatcher::undo_doubles(FlagStore& store, Table& changes) {
  std::erase_if(doubles_, [&](const auto& d) { return d.second < frames_; });
  for (const auto& [key, amount] : take_back_) {
    std::optional<FlagEntry> now = store.read(key);
    if (!now || now->kind != protocol::FlagKind::int32 || amount <= 0) continue;
    now->key = key;
    now->value = as_bits(std::max<std::int64_t>(as_int(now->value) - amount, 0));
    if (!store.write(*now)) {
      ++stats_.write_failures;
      continue;
    }
    last_[key] = *now;
    if (role_ == FlagRole::player) {
      changes[key] = FlagEntry{key, protocol::FlagKind::count_delta, as_bits(-std::int64_t{amount})};
      ++stats_.changes_sent;
    } else {
      restate_.insert(key);  // the host's total, without it
    }
    doubles_undone_.push_back(DoubleUndone{key, amount, true});
  }
  take_back_.clear();
}

void FlagWatcher::diff(FlagStore& store, Table& changes) {
  undo_doubles(store, changes);  // ORM riesgos-visual (expect_double_find)
  for (const FlagKey key : shared_.keys()) {
    if (quiet_.contains(key)) continue;  // a fake value is never sent
    std::optional<FlagEntry> now = store.read(key);
    if (!now) {
      ++stats_.read_failures;
      continue;
    }
    now->key = key;
    const auto [it, first] = last_.try_emplace(key, *now);
    if (first) continue;  // the first read is the baseline
    if (it->second == *now && !restate_.contains(key)) continue;
    if (continue_mode_ != ContinueMode::off && !window_ && !restate_.contains(key) && !one_way_.contains(key)) {
      // ORM: CONTINUAR.md (REVISION_NUEVAS): the game over runs and its reload's window is not open yet. A
      // change of our own now is the Continue's (Link cannot play): held, not sent, last_ kept; the window
      // writes it back (keep) or sends it as FLAG_RESET (save, host), or it goes out once the mode is off.
      ++stats_.continue_held;
      continue;
    }
    if (window_ && loading_ && !moved_ && key.kind == FlagKeyKind::named && !one_way_.contains(key) &&
        went_back(it->second, *now)) {
      // The title screen (no scene) and saved progress going back: a save
      // was loaded. A door, warp, cutscene, time travel or death keeps a
      // scene, and its flags going back are the room's or the story's.
      // Distinct flags: one held at 0 for many frames counts once.
      back_keys_.insert(key);
      see_save_load();
    }
    if (window_ && moved_ && keep_) {
      // ORM: CONTINUAR.md, keep: back to the snapshot at once, either way, host or player; nothing sent.
      if (!one_way_.contains(key)) keep_back(store, key, it->second);
      continue;
    }
    if (window_ && moved_) {
      if (role_ == FlagRole::player && !store.is_count(key) && !one_way_.contains(key)) {
        // The group's value back at once, so a room loading now has it.
        const auto group = group_.find(key);
        if (group != group_.end() && group->second != *now) {
          if (store.holds(key)) {
            unwritten_[key] = group->second;
            it->second = *now;
            continue;
          }
          if (store.write_exact(group->second)) {
            ++stats_.load_reverts;
            ++stats_.applied;
            it->second = group->second;
            continue;
          }
        }
      }
      continue;  // a save load sends nothing; end_window decides
    }
    if (window_ && loading_) {
      // No Link, no sign of a save load yet: a guest puts the group's value
      // back at once as before (backwards(), issue 129); nothing goes out
      // until Link is back, when the rules below take what is left.
      if (role_ == FlagRole::player && !store.is_count(key) && !one_way_.contains(key)) {
        const auto group = group_.find(key);
        const auto older = group_before_.find(key);
        if (group != group_.end() &&
            backwards(group->second, older != group_before_.end() ? &older->second : nullptr, *now, true)) {
          if (store.holds(key)) {
            ++stats_.load_reverts;
            unwritten_[key] = group->second;
            continue;
          }
          if (store.write(group->second)) {
            ++stats_.load_reverts;
            ++stats_.applied;
            it->second = group->second;
            unwritten_.erase(key);
            continue;
          }
        }
      }
      continue;
    }
    const FlagEntry before = it->second;
    it->second = *now;
    // Not while loading: a load putting the save's 0 back is the load
    // revert's (below, the group's value), not a drop of the game's own.
    if (!loading_ && before.kind == protocol::FlagKind::boolean && before.value != 0 &&
        now->value == 0 && store.is_one_way(key)) {
      // A map or compass the game took back (issue 146: both players opened
      // the Deku Tree's map chest and neither kept the map). Never right for
      // a one-way Bool: put it back, send nothing.
      if (store.write(before)) {
        ++stats_.one_way_restores;
        ++stats_.applied;
        it->second = before;
        if (one_way_told_.insert(key).second) one_way_restored_.push_back(key);
        continue;
      }
      ++stats_.write_failures;
    }
    if (settling() && !store.is_count(key) && !restate_.contains(key) && !one_way_.contains(key)) {
      // A load putting back an older value than the group's: not sent, and
      // the group's value goes back in (set_loading).
      const auto group = group_.find(key);
      const auto older = group_before_.find(key);
      if (group != group_.end() &&
          backwards(group->second, older != group_before_.end() ? &older->second : nullptr,
                    *now, loading_)) {
        if (store.holds(key)) {
          // Our game takes the group's value later (HoldFlagStore): kept to
          // write then; the load's older value is ours alone, never sent.
          ++stats_.load_reverts;
          unwritten_[key] = group->second;
          continue;
        }
        if (store.write(group->second)) {
          ++stats_.load_reverts;
          ++stats_.applied;
          it->second = group->second;
          unwritten_.erase(key);
          continue;
        }
        // The game takes no write of it (a player's own item): it is not
        // the group's to put back.
        ++stats_.write_failures;
        group_.erase(group);
        group_before_.erase(key);
      }
    }
    if (now->value == 0 && (pickups_.contains(key) || one_way_.contains(key)) && !restate_.contains(key)) {
      ++stats_.pickup_resets;  // the game reset it (a scene change): ours only
      continue;
    }
    if (!restate_.contains(key) && before.kind == protocol::FlagKind::int32 &&
        now->kind == protocol::FlagKind::int32 && as_int(now->value) < as_int(before.value) &&
        store.is_max(key)) {
      // A max-rule value our game lowered (MaxLife: the game writes its own
      // hearts back when nothing raised Link's). Every store keeps the higher
      // value, so sending it only brings the higher one back, the game lowers
      // it again, and so on for ever, with a notice and the heal sound each
      // time. Ours alone: not sent, not put back.
      ++stats_.max_drops_kept;
      pending_.erase(key);
      continue;
    }
    if (doubled_ > 0 && frames_ > doubled_until_) doubled_ = 0;  // lapsed
    if (doubled_ > 0 && !restate_.contains(key) && store.is_count(key) && !guards_.contains(key) &&
        before.kind == protocol::FlagKind::int32 && now->kind == protocol::FlagKind::int32 &&
        as_int(now->value) > as_int(before.value)) {
      // A chest another player had opened, opened again here (issue 195):
      // its content counts once for the group. Put back, not sent.
      FlagEntry back = before;
      back.key = key;
      if (store.write(back)) {
        --doubled_;
        ++stats_.doubled_gains;
        ++stats_.applied;
        counts_guarded_.push_back(
            CountGuarded{key, static_cast<std::int32_t>(as_int(now->value) - as_int(before.value))});
        it->second = back;
        continue;
      }
      ++stats_.write_failures;  // sent as any gain
    }
    if (const auto d = doubles_.find(key);
        d != doubles_.end() && store.is_count(key) && before.kind == protocol::FlagKind::int32 &&
        now->kind == protocol::FlagKind::int32) {
      // ORM riesgos-visual: the find of a chest another player opened first,
      // counted again by our game (expect_double_find): put back, not sent.
      std::int64_t own = as_int(now->value) - as_int(before.value);
      if (const auto a = added_.find(key); a != added_.end()) own -= as_int(a->second.value);
      if (own > 0) {
        FlagEntry back = *now;
        back.value = as_bits(as_int(now->value) - own);
        if (store.write(back)) {
          it->second = back;
          doubles_undone_.push_back(DoubleUndone{key, static_cast<std::int32_t>(own), false});
          doubles_.erase(d);
          continue;
        }
        ++stats_.write_failures;
      }
    }
    if (const auto g = guards_.find(key); g != guards_.end() &&
                                          before.kind == protocol::FlagKind::int32 &&
                                          now->kind == protocol::FlagKind::int32) {
      // settle_guards() decides what is sent, after every source was seen.
      std::int64_t own = std::int64_t{as_int(now->value)} - as_int(before.value);
      if (const auto a = added_.find(key); a != added_.end()) own -= as_int(a->second.value);
      g->second.own += static_cast<std::int32_t>(own);
      continue;
    }
    if (const auto s = guard_source_.find(key);
        s != guard_source_.end() && before.value == 0 && now->value != 0) {
      Guard& guard = guards_[s->second];
      if (guard.credit == 0) guard.credit_since = frames_;
      ++guard.credit;  // taken in our own game: a gain of its count is real
    }
    ++stats_.changes_sent;
    if (store.is_count(key) && before.kind == protocol::FlagKind::int32 &&
        now->kind == protocol::FlagKind::int32 && as_int(now->value) < as_int(before.value) &&
        own_count_drops_.size() < kMaxOwnCountDrops) {
      own_count_drops_.push_back(key);  // our game spent one (a key on a door)
    }
    if (role_ == FlagRole::player && store.is_count(key) &&
        before.kind == protocol::FlagKind::int32 && now->kind == protocol::FlagKind::int32) {
      // A count: the change, so a gain here and one elsewhere both count.
      // Not pending: the host's total answers it, and a total written later
      // keeps any change not sent yet (write()).
      changes[key] = FlagEntry{key, protocol::FlagKind::count_delta,
                               as_bits(as_int(now->value) - as_int(before.value))};
      if (as_int(now->value) > as_int(before.value)) {
        // ORM riesgos-visual: kept for expect_double_find (a CHEST_OPEN that came first, late).
        own_gains_[key] = OwnGain{static_cast<std::int32_t>(as_int(now->value) - as_int(before.value)), frames_};
      }
      continue;
    }
    changes[key] = *now;
    if (!store.is_count(key)) note_group(*now, &before);  // sent: the group's value now
    // The game's own change is newer, except a max-rule value it is still
    // below (issue 193: a quest step held in a dungeon while our game takes
    // a lower step of its own): that one is still written later.
    if (const auto u = unwritten_.find(key); u != unwritten_.end() && !held_is_higher(store, u->second, *now)) {
      unwritten_.erase(u);
    }
    if (role_ == FlagRole::player) pending_[key] = *now;
  }
  settle_guards(store, changes);
}

void FlagWatcher::settle_guards(FlagStore& store, Table& changes) {
  for (auto& [key, g] : guards_) {
    const std::int32_t own = std::exchange(g.own, 0);
    std::int64_t send = 0;  // our own change that goes out
    const auto shift = [&](std::int32_t by) {  // our game's count, not sent
      std::optional<FlagEntry> now = store.read(key);
      if (!now || now->kind != protocol::FlagKind::int32) return false;
      now->key = key;
      now->value = as_bits(std::max<std::int64_t>(std::int64_t{as_int(now->value)} + by, 0));
      if (!store.write(*now)) {
        ++stats_.write_failures;
        return false;
      }
      last_[key] = *now;
      return true;
    };
    if (own > 0) {
      if (g.held == 0) g.held_since = frames_;
      g.held += own;
    } else if (own < 0) {
      std::int32_t drop = -own;
      const std::int32_t back = std::min(drop, g.held);  // a held gain given back
      g.held -= back;
      drop -= back;
      if (drop > 0 && g.put_back > 0 && frames_ - g.put_back_at <= kGuardHoldFrames) {
        // Given back after we had put it back already: up again, once.
        const std::int32_t up = std::min(drop, g.put_back);
        if (shift(up)) {
          g.put_back -= up;
          drop -= up;
        }
      }
      send -= drop;  // a drop of our own
    }
    if (g.credit > 0 && g.held == 0 && frames_ - g.credit_since > kGuardHoldFrames) g.credit = 0;
    const std::int32_t matched = std::min(g.held, g.credit);
    g.held -= matched;
    g.credit -= matched;
    send += matched;
    if (g.held > 0 && frames_ - g.held_since >= kGuardHoldFrames) {
      const bool taken_elsewhere = g.others_at != 0 && frames_ + 1 - g.others_at <= kGuardRecentFrames;
      const std::int32_t gain = g.held;
      g.held = 0;  // before shift(): the write is ours, not a held gain
      if (taken_elsewhere && shift(-gain)) {
        ++stats_.counts_guarded;
        counts_guarded_.push_back(CountGuarded{key, gain});
        g.put_back = gain;
        g.put_back_at = frames_;
      } else {
        send += gain;  // no take we know of: as any count
      }
    }
    if (role_ == FlagRole::player) {
      if (send != 0) {
        changes[key] = FlagEntry{key, protocol::FlagKind::count_delta, as_bits(send)};
        ++stats_.changes_sent;
      }
      continue;
    }
    // The host sends its total, without a gain still held, when the group's
    // changed: our own change sent, or a player's delta added (restate_).
    if (send == 0 && !restate_.contains(key)) continue;
    std::optional<FlagEntry> now = store.read(key);
    if (!now || now->kind != protocol::FlagKind::int32) continue;
    now->key = key;
    now->value = as_bits(as_int(now->value) - g.held);
    changes[key] = *now;
    ++stats_.changes_sent;
  }
}

void FlagWatcher::send_snapshot(FlagStore& store, std::vector<Outgoing>& out) {
  std::vector<FlagEntry> table;
  table.reserve(shared_.size());
  for (const FlagKey key : shared_.keys()) {
    if (quiet_.contains(key)) {  // its last real value, not the fake
      if (const auto known = last_.find(key); known != last_.end()) table.push_back(known->second);
      continue;
    }
    std::optional<FlagEntry> now = store.read(key);
    if (!now) {
      ++stats_.read_failures;
      continue;
    }
    now->key = key;
    // A gain still held (guard_count) is not the group's yet, as in
    // answer_set: a joiner must not get a count the host may put back.
    if (const auto g = guards_.find(key); g != guards_.end() && g->second.held != 0 &&
        now->kind == protocol::FlagKind::int32) {
      now->value = as_bits(as_int(now->value) - g->second.held);
    }
    table.push_back(*now);
  }
  if (auto parts = protocol::split_snapshot(table)) {
    for (const auto& part : *parts) push(out, part);
  }
}

}  // namespace coop::host
