#include "coop/host/prizes.hpp"

#include <algorithm>
#include <array>

namespace coop::host::prize {

namespace {

struct Named {
  std::string_view name;
  bool heart_piece;
};

// The once: prize lines of the kit (shared_dungeon_minigames.txt; GoronLargePot from
// shared_dungeon_child.txt; the three given pieces of shared_dungeon_collectibles.txt; the
// fishing pond's from the items table), with what each gives (MINIJUEGOS.md, section 1).
constexpr std::array<Named, 33> kNames{{
    {"MiniGame_HitTarget_HrlCity_Get_BulletBag", false},
    {"MiniGame_HitTarget_Kakariko_Get_Quiver", false},
    {"LostWood_HitTargetOfDekuScrub_FirstClear", false},
    {"MiniGame_BombchuBowling_ClearFirstTime", false},
    {"MiniGame_BombchuBowling_ClearSecondTime", true},
    {"MiniGame_BombchuBowling_ClearThirdTime", false},
    {"MiniGame_ChoiceTreasure_IsClear", true},
    {"UnderTheGrave_DampeRace_HeartGeted", true},
    {"GraveYard_DiggingHoleTour_Drop_HeartPiece", true},
    {"MiniGame_HorsebackArchery_GotaHeart", true},
    {"MiniGame_HorseBackArchery_Get_Quiver", false},
    {"MiniGame_FrogTenors_ZeldaLullaby", false},
    {"MiniGame_FrogTenors_EponaSong", false},
    {"MiniGame_FrogTenors_SariaSong", false},
    {"MiniGame_FrogTenors_SunSong", false},
    {"MiniGame_FrogTenors_TimeSong", false},
    {"MiniGame_FrogTenors_StormSong", false},
    {"MiniGame_FrogTenors_SongClear", true},
    {"MiniGame_FrogTenors_InsectRepellent_ClearAfter", false},
    {"MiniGame_Diving_Cleared", false},
    {"MiniGame_CatchCucco_IsCompleted", false},
    {"IsClear_Minigame_FindOutCucco", false},
    {"SubEvent_FindRichard_GetHeartPiece", true},
    {"Npc_SkullKid_Child_IsGetHeartPiece", true},
    {"Minigame_SkullKidSession_IsGetHeartPiece", true},
    {"Npc_Hylia_001_IsGetHeartPiece", true},
    {"HyruleField_Enemy_Scrub_Business_HeartPiece_01", true},
    {"DeathMountain_Enemy_Scrub_Business_HeartPiece_01", true},
    {"GoronLargePot_IsGetHeartPiece", true},
    {"MiniGame_Fishing_GetHeartPiece", true},
    {"Npc_Hylia_045_HasDived", true},
    {"KakarikoVillage_IsAfterSave_Npc_Hylia_025", true},
    {"MiniGame_FrogTenors_InsectRepellent", true},
}};

const std::vector<Prize>& table() {
  static const auto t = [] {
    std::vector<Prize> v;
    v.reserve(kNames.size());
    for (const Named& n : kNames) v.push_back(Prize{n.name, flag_hash(n.name), n.heart_piece});
    return v;
  }();
  return t;
}

bool named(const FlagKey& k) { return k.kind == protocol::FlagKeyKind::named && k.index == 0; }

}  // namespace

std::span<const Prize> prizes() { return table(); }

const Prize* prize_of(std::uint32_t hash) {
  for (const Prize& p : table()) {
    if (p.hash == hash) return &p;
  }
  return nullptr;
}

std::uint32_t heart_piece_count() {
  static const std::uint32_t hash = flag_hash("PouchData.Obj_Heart_Piece.StockNum");
  return hash;
}

std::optional<std::int32_t> PrizeArbiter::read_count(FlagStore& game) {
  const auto c = game.read(FlagKey(heart_piece_count(), std::uint16_t{0}));
  if (!c || c->kind != protocol::FlagKind::int32) return std::nullopt;
  return static_cast<std::int32_t>(c->value);
}

void PrizeArbiter::settle(const Prize& p, Win& w, FlagStore& game, std::uint64_t now_us) {
  const std::string head = "prizes: " + std::string(p.name) + ": ";
  const auto count = read_count(game);
  if (!count || w.count_before < 0) {
    notes_.push_back(head + "the heart piece count cannot be read here: nothing given back, the group keeps two");
    w.give_back = false;
    return;
  }
  if (*count < w.count_before) {
    notes_.push_back(head + "the heart piece count went down since our win (" + std::to_string(w.count_before) +
                     " -> " + std::to_string(*count) +
                     ": four pieces made a heart?): a piece can no longer be given back, the group keeps two");
    w.give_back = false;
    return;
  }
  if (*count == w.count_before) {
    if (now_us - w.at_us >= kPieceUs) {
      notes_.push_back(head + "no heart piece came in our game in " + std::to_string(kPieceUs / 1'000'000) +
                       " s after the win: nothing to give back");
      w.give_back = false;
    }
    return;  // the piece comes with the prize's text box: wait for it
  }
  const protocol::FlagEntry back{FlagKey(heart_piece_count(), std::uint16_t{0}), protocol::FlagKind::int32,
                                 static_cast<std::uint32_t>(*count - 1)};
  if (game.write(back)) {
    ++given_back_;
    notes_.push_back(head + "our heart piece given back (Obj_Heart_Piece " + std::to_string(*count) + " -> " +
                     std::to_string(*count - 1) + "): one piece for the prize in every game, P" +
                     std::to_string(w.keeper) + " keeps it");
  } else {
    notes_.push_back(head + "our heart piece could not be given back (the count did not take the write): the "
                            "group keeps two");
  }
  w.give_back = false;
}

void PrizeArbiter::frame(const PrizeView& view, FlagStore& game, const std::vector<FlagWatcher::Received>& received,
                         const std::vector<FlagWatcher::Received>& agreed) {
  if (!on_) return;
  if (!view.in_game || view.self == 0) {
    primed_ = false;  // a load sets flags: not wins
    return;
  }
  std::set<std::uint32_t> came;  // another game's 1 written this frame
  for (const auto& r : received) {
    if (named(r.key) && r.after.value != 0) came.insert(r.key.hash);
  }
  const auto count = read_count(game);
  for (const Prize& p : table()) {
    const auto v = game.read(FlagKey(p.hash, std::uint16_t{0}));
    if (!v || v->kind != protocol::FlagKind::boolean) {
      was_.erase(p.hash);
      continue;
    }
    const bool now = v->value != 0;
    const auto it = was_.find(p.hash);
    if (primed_ && it != was_.end() && !it->second && now && !came.contains(p.hash)) {
      Win w;
      w.at_us = view.now_us;
      w.count_before = count_was_ ? *count_was_ : -1;
      wins_[p.hash] = w;
      notes_.push_back("prizes: " + std::string(p.name) + ": won in our game (P" + std::to_string(view.self) +
                       "); another winner within " + std::to_string(kWindowUs / 1'000'000) + " s is arbitrated");
    }
    was_[p.hash] = now;
  }
  for (const auto& a : agreed) {
    if (!named(a.key) || a.after.value == 0 || a.kind != FlagWatcher::ReceivedKind::live) continue;
    if (a.from == 0 || a.from == view.self) continue;  // our own value's echo, or not known
    const auto w = wins_.find(a.key.hash);
    // Every winner within the window counts, also after a first one decided: with three or more
    // winners the one who keeps it must not depend on whose 1 came first.
    if (w == wins_.end() || view.now_us - w->second.at_us > kWindowUs) continue;
    w->second.others.insert(a.from);
  }
  for (auto it = wins_.begin(); it != wins_.end();) {
    Win& w = it->second;
    const Prize* p = prize_of(it->first);
    if (p == nullptr) {
      it = wins_.erase(it);
      continue;
    }
    // The keeper is the lowest number of all the winners seen so far; a lower winner seen later
    // (a third player) decides again, so every game ends with the same keeper whatever the order.
    const std::uint8_t keeper = w.others.empty() ? w.keeper : std::min(view.self, *w.others.begin());
    if (!w.others.empty() && (!w.decided || keeper != w.keeper)) {
      // We lose it now: first decided, or we kept it until a lower winner came. Lost already
      // (given back or its fallback said), a lower keeper changes nothing for us.
      const bool lost_now = keeper != view.self && (!w.decided || w.keeper == view.self);
      w.decided = true;
      w.keeper = keeper;
      std::string who;
      for (const std::uint8_t o : w.others) who += (who.empty() ? "P" : ", P") + std::to_string(o);
      std::string line = "prizes: " + std::string(p->name) + ": won here and by " + who + " at once: P" +
                         std::to_string(w.keeper) + " keeps it (the lowest number)";
      if (w.keeper == view.self) {
        line += "; we keep ours";
      } else if (!p->heart_piece) {
        line += "; the item is the group's once (bag, quiver, scale or bottle): nothing doubled, nothing given back";
      } else if (lost_now) {
        line += "; our heart piece goes back";
        w.give_back = true;
      } else {
        line += "; ours goes back already";
      }
      notes_.push_back(std::move(line));
    }
    if (w.give_back) settle(*p, w, game, view.now_us);
    if (!w.give_back && view.now_us - w.at_us > kWindowUs) {
      it = wins_.erase(it);
      continue;
    }
    ++it;
  }
  count_was_ = count;
  primed_ = true;
}

}  // namespace coop::host::prize
