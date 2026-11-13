#include "coop/host/trades.hpp"

#include <array>
#include <cstdio>

namespace coop::host::trade {

namespace {

constexpr std::array<std::string_view, 27> kStepNames{
    "NotReady", "Ready",    "StartSubEvent", "WaitEggHatch", "EggHatched", "Step01",    "Step01_01",
    "Step02",   "Step03",   "Step03_01",     "Step03_02",    "Step03_03",  "Step04",    "Step05",
    "Step05_01", "Step06",  "Step06_01",     "Step06_02",    "Step06_03",  "Step07",    "Step08",
    "Step08_01", "Step09",  "Step09_01",     "Step10",       "Step11",     "Complete"};

const std::vector<std::pair<std::string_view, std::uint32_t>>& values_table() {
  static const auto table = [] {
    std::vector<std::pair<std::string_view, std::uint32_t>> v;
    v.reserve(kStepNames.size());
    for (const std::string_view name : kStepNames) v.emplace_back(name, flag_hash(name));
    return v;
  }();
  return table;
}

const std::vector<TimedItem>& items_table() {
  static const auto table = [] {
    std::vector<TimedItem> v{
        {"the Odd Mushroom", "PouchData.Item_OddMushroom.StockNum", 0, "Step04",
         "GetSwordBigGoron_Step04_GiveUp", 0},
        {"the Eyeball Frog", "PouchData.Item_EyeballFrog.StockNum", 0, "Step09",
         "GetSwordBigGoron_Step09_GiveUp", 0},
        {"the Eye Drops", "PouchData.Item_EyeDrop.StockNum", 0, "Step10", "GetSwordBigGoron_Step10_GiveUp", 0},
    };
    for (TimedItem& t : v) {
      t.stock = flag_hash(t.stock_name);
      t.give_up = flag_hash(t.give_up_name);
    }
    return v;
  }();
  return table;
}

bool named(const protocol::FlagEntry& e) {
  return e.key.kind == protocol::FlagKeyKind::named && e.key.index == 0;
}

}  // namespace

std::uint32_t step_flag() {
  static const std::uint32_t hash = flag_hash("Step_SubEvent_GetSwordBigGoron");
  return hash;
}

std::span<const std::pair<std::string_view, std::uint32_t>> step_values() { return values_table(); }

std::optional<std::size_t> step_rank(std::uint32_t value) {
  const auto& v = values_table();
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (v[i].second == value) return i;
  }
  return std::nullopt;
}

std::string step_name(std::uint32_t value) {
  if (const auto r = step_rank(value)) return std::string(values_table()[*r].first);
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(value));
  return text;
}

std::span<const TimedItem> timed_items() { return items_table(); }

const TimedItem* TradeGuard::held_item(FlagStore& game, bool& known) {
  known = true;
  for (const TimedItem& t : items_table()) {
    const auto stock = game.read(FlagKey(t.stock, std::uint16_t{0}));
    if (!stock || stock->kind != protocol::FlagKind::int32) {
      known = false;
      continue;
    }
    if (static_cast<std::int32_t>(stock->value) > 0) return &t;
  }
  return nullptr;
}

bool TradeGuard::refuses(const protocol::FlagEntry& entry, FlagStore& game) {
  if (!on_ || !named(entry)) return false;
  const bool is_step = entry.key.hash == step_flag();
  const TimedItem* give_up_of = nullptr;
  if (!is_step) {
    for (const TimedItem& t : items_table()) {
      if (entry.key.hash == t.give_up) give_up_of = &t;
    }
    if (give_up_of == nullptr) return false;
  }
  bool known = true;
  const TimedItem* held = held_item(game, known);
  if (held == nullptr) {
    // The item just went (our hand-over sets the count a frame before the step): the
    // value refused under it keeps waiting kGraceUs, so our own step comes first.
    if (const auto w = waits_.find(entry.key.hash); w != waits_.end() && w->second.value == entry.value) {
      if (w->second.gone_us == 0) w->second.gone_us = now_us_ != 0 ? now_us_ : 1;
      if (now_us_ - w->second.gone_us < kGraceUs) {
        ++refused_;
        ++w->second.refused;
        w->second.idle_frames = 0;
        return true;
      }
    }
    if (!known && !noted_unreadable_) {
      noted_unreadable_ = true;
      notes_.push_back("trades: a timed trade item's count cannot be read here: another game's step is "
                       "written as it came (the guard needs the PouchData counts)");
    }
    return false;  // we hold no timed item: the holder's step back is everyone's
  }
  std::uint32_t ours = 0;
  if (is_step) {
    if (entry.kind != protocol::FlagKind::uint32) return false;
    const auto now = game.read(FlagKey(step_flag(), std::uint16_t{0}));
    const auto now_rank = now && now->kind == protocol::FlagKind::uint32 ? step_rank(now->value) : std::nullopt;
    const auto new_rank = step_rank(entry.value);
    if (!now_rank || !new_rank) {
      if (!noted_unreadable_) {
        noted_unreadable_ = true;
        notes_.push_back("trades: Biggoron's step cannot be read here (or is no step of the chain): "
                         "another game's step is written as it came");
      }
      return false;
    }
    if (*new_rank >= *now_rank) return false;  // forward (or the same): fine
    ours = now->value;
  } else {
    // A GiveUp of the item's step is refused only while we hold THAT item.
    if (entry.value == 0 || give_up_of != held) return false;
  }
  ++refused_;
  auto [it, fresh] = waits_.try_emplace(entry.key.hash);
  Wait& w = it->second;
  if (fresh || w.value != entry.value) {
    w.value = entry.value;
    w.ours = ours;
    w.held = std::string(held->what);
    w.refused = 0;
    if (is_step) {
      notes_.push_back("trades: Step_SubEvent_GetSwordBigGoron " + step_name(ours) + " -> " +
                       step_name(entry.value) + " from another game refused: our game holds " + w.held +
                       " (a step back would take it from our hands); kept until our game moves the step itself");
    } else {
      notes_.push_back("trades: " + std::string(give_up_of->give_up_name) + " = 1 from another game refused: our "
                       "game holds " + w.held + "; kept until our game moves the step itself");
    }
  }
  ++w.refused;
  w.idle_frames = 0;
  w.gone_us = 0;  // held again
  return true;
}

void TradeGuard::frame(FlagStore& game, std::uint64_t now_us) {
  now_us_ = now_us;
  if (!on_ || waits_.empty()) return;
  bool known = true;
  const TimedItem* held = held_item(game, known);
  for (auto it = waits_.begin(); it != waits_.end();) {
    const auto now = game.read(FlagKey(it->first, std::uint16_t{0}));
    const bool step = it->first == step_flag();
    if (now && now->value == it->second.value) {
      notes_.push_back("trades: " + std::string(step ? "Step_SubEvent_GetSwordBigGoron " + step_name(now->value)
                                                     : "a GiveUp flag") +
                       " from another game written after " + std::to_string(it->second.refused) +
                       " refusals: our game holds " + (held ? std::string(held->what) : "no timed trade item") +
                       " now");
      it = waits_.erase(it);
      continue;
    }
    if (step && now && now->value != it->second.ours) {
      notes_.push_back("trades: our game moved Biggoron's step itself (" + step_name(it->second.ours) + " -> " +
                       step_name(now->value) + "): the other game's " + step_name(it->second.value) +
                       " is dropped, ours goes to everyone");
      it = waits_.erase(it);
      continue;
    }
    // Not asked for again for a while: the watcher dropped it (our game changed the
    // flag itself, or a newer value came).
    if (++it->second.idle_frames > kIdleFrames) {
      notes_.push_back(std::string("trades: ") + (step ? "Biggoron's step" : "a GiveUp flag") +
                       " from another game no longer waits (the watcher dropped it: our game's value is newer)");
      it = waits_.erase(it);
      continue;
    }
    ++it;
  }
}

}  // namespace coop::host::trade
