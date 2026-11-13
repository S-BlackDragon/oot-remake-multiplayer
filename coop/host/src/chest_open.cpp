// ORM cofre: see coop/host/chest_open.hpp and oot-remake docs/DETALLES_MARIONETA.md, "Cofres".
#include "coop/host/chest_open.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

#include "coop/host/scene.hpp"

namespace coop::host {

namespace {

constexpr ChestPlacement kChests[] = {
#include "chest_list.inc"
};

using K = ChestItemKind;

// What each drop of the table is. Words as the Spanish game says them, with
// the article for the notice. Flags: game_items.cpp (the item table) and,
// for the dungeon kinds, DungeonInfo.<area>.<flag>. Sorted by drop.
constexpr ChestItem kItems[] = {
    {"Item_Arrow_Ice", K::item, "las flechas de hielo", "IsGet.Item_Arrow_Ice", "CoopGet_Item_Arrow_Ice"},
    {"Item_BombBag_Small", K::item, "una bolsa de bombas", "IsGet.Item_BombBag_Small",
     "CoopGet_Item_BombBag_Small"},
    {"Item_BombBundle_Small", K::ammo, "unas bombas", "", "CoopGet_Item_BombBundle_Small"},
    {"Item_BombchuBundle_Small", K::ammo, "unos bombchus", "", "CoopGet_Item_BombchuBundle_Small"},
    {"Item_Boomerang", K::item, "el bumerán", "IsGet.Item_Boomerang", "CoopGet_Item_Boomerang"},
    {"Item_DekuStickBundle_Small", K::ammo, "unos palos deku", "", "CoopGet_Item_DekuStickBundle_Small"},
    {"Item_Dungeon_BossKey", K::boss_key, "la gran llave", "IsGetBossKey", "CoopGet_Item_Dungeon_BossKey"},
    {"Item_Dungeon_Compass", K::compass, "la brújula", "IsGetCompass", "CoopGet_Item_Dungeon_Compass"},
    {"Item_Dungeon_Map", K::map, "el mapa", "IsGetMap", "CoopGet_Item_Dungeon_Map"},
    {"Item_Dungeon_SmallKey", K::small_key, "una llave pequeña", "SmallKeyNum", "CoopGet_Item_Dungeon_SmallKey"},
    {"Item_Gauntlet_Gold", K::item, "los guanteletes de oro", "IsGet.Armor_Adult_Gauntlet_Gold",
     "CoopGet_Item_Gauntlet_Gold"},
    {"Item_Gauntlet_Silver", K::item, "los guanteletes de plata", "IsGet.Armor_Adult_Gauntlet_Silver",
     "CoopGet_Item_Gauntlet_Silver"},
    {"Item_GlassTruth", K::item, "la Lente de la Verdad", "IsGet.Armor_GlassTruth", "CoopGet_Item_GlassTruth"},
    {"Item_Hammer", K::item, "el martillo Megatón", "IsGet.Item_Hammer", "CoopGet_Item_Hammer"},
    {"Item_HeavyBoot", K::item, "las botas de hierro", "IsGet.Armor_Adult_HeavyBoot", "CoopGet_Item_HeavyBoot"},
    {"Item_Hookshot", K::item, "el gancho", "IsGet.Item_Hookshot", "CoopGet_Item_Hookshot"},
    {"Item_HoverBoot", K::item, "las botas voladoras", "IsGet.Armor_Adult_HoverBoot", "CoopGet_Item_HoverBoot"},
    {"Item_Longshot", K::item, "el gancho largo", "IsGet.Item_Longshot", "CoopGet_Item_Longshot"},
    {"Item_Range_Bow", K::item, "el arco de las hadas", "IsGet.Weapon_Range_Bow", "CoopGet_Item_Range_Bow"},
    {"Item_Range_SlingShot", K::item, "el tirachinas de las hadas", "IsGet.Weapon_Range_SlingShot",
     "CoopGet_Item_Range_SlingShot"},
    {"Item_ScrubBusinessSeed", K::item, "una semilla de los dekus negociantes", "IsGet.Item_ScrubBusinessSeed",
     "CoopGet_Item_ScrubBusinessSeed"},
    {"Item_Shield_Mirror", K::item, "el escudo espejo", "IsGet.Weapon_Shield_Mirror", "CoopGet_Item_Shield_Mirror"},
    {"Item_Sword_Kokiri", K::item, "la espada kokiri", "IsGet.Weapon_Sword_Kokiri", "CoopGet_Item_Sword_Kokiri"},
    {"Obj_Heart_Piece", K::heart_piece, "una pieza de corazón", "PouchData.Obj_Heart_Piece.StockNum",
     "CoopGet_Obj_Heart_Piece"},
    {"TBox_ArrowBundle_Large", K::ammo, "muchas flechas", "", "CoopGet_TBox_ArrowBundle_Large"},
    {"TBox_ArrowBundle_Medium", K::ammo, "unas flechas", "", "CoopGet_TBox_ArrowBundle_Medium"},
    {"TBox_ArrowBundle_Small", K::ammo, "unas flechas", "", "CoopGet_TBox_ArrowBundle_Small"},
    {"TBox_DekuNutBundle", K::ammo, "unas nueces deku", "", "CoopGet_TBox_DekuNutBundle"},
    {"TBox_DekuSeedBundle", K::ammo, "unas semillas deku", "", "CoopGet_TBox_DekuSeedBundle"},
    {"TBox_Rupee_Blue", K::rupees, "una rupia azul (5)", "PlayerStatus.CurrentRupee", "CoopGet_TBox_Rupee_Blue"},
    {"TBox_Rupee_Gold", K::rupees, "una rupia dorada (200)", "PlayerStatus.CurrentRupee",
     "CoopGet_TBox_Rupee_Gold"},
    {"TBox_Rupee_Green", K::rupees, "una rupia verde (1)", "PlayerStatus.CurrentRupee",
     "CoopGet_TBox_Rupee_Green"},
    {"TBox_Rupee_Purple", K::rupees, "una rupia morada (50)", "PlayerStatus.CurrentRupee",
     "CoopGet_TBox_Rupee_Purple"},
    {"TBox_Rupee_Red", K::rupees, "una rupia roja (20)", "PlayerStatus.CurrentRupee", "CoopGet_TBox_Rupee_Red"},
    {"TBox_Shield_Deku", K::own_shield, "un escudo deku", "IsGet.Weapon_Shield_Deku", "CoopGet_TBox_Shield_Deku"},
    {"TBox_Shield_Hylian", K::item, "el escudo hyliano", "IsGet.Weapon_Shield_Hylian",
     "CoopGet_TBox_Shield_Hylian"},
    {"Tbox_MagicJar_Large", K::magic, "un frasco de magia grande", "", "CoopGet_Tbox_MagicJar_Large"},
};

bool starts(std::string_view s, std::string_view prefix) {
  return s.substr(0, prefix.size()) == prefix;
}

bool valid_hash(std::uint64_t hash) {
  return hash != 0 && hash != ~std::uint64_t{0};
}

float distance(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool finite(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

std::string hex(std::uint64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
  return buf;
}

std::string number(float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
  return buf;
}

std::string who(std::uint8_t n) { return "P" + std::to_string(n); }

bool is_chest_move(DoorMove m) {
  return m == DoorMove::chest_kick || m == DoorMove::chest_front || m == DoorMove::chest_small;
}

}  // namespace

// ---- 1. The chest table ----

std::span<const ChestPlacement> chest_placements() { return kChests; }

const ChestPlacement* find_chest(std::uint64_t hash) {
  const auto* end = std::end(kChests);
  const auto* it = std::lower_bound(std::begin(kChests), end, hash,
                                    [](const ChestPlacement& c, std::uint64_t h) { return c.hash < h; });
  return it != end && it->hash == hash ? it : nullptr;
}

std::string_view chest_drop(const ChestPlacement& chest) {
  if (chest.drop[0] != '\0') return chest.drop;
  return chest.substitute;
}

const char* chest_item_kind_text(ChestItemKind kind) {
  switch (kind) {
    case K::item: return "item";
    case K::small_key: return "small key";
    case K::map: return "map";
    case K::compass: return "compass";
    case K::boss_key: return "boss key";
    case K::heart_piece: return "heart piece";
    case K::rupees: return "rupees";
    case K::ammo: return "ammo";
    case K::own_shield: return "own shield";
    case K::magic: return "magic";
  }
  return "?";
}

std::span<const ChestItem> chest_items() { return kItems; }

const ChestItem* chest_item(std::string_view drop) {
  for (const auto& i : kItems) {
    if (i.drop == drop) return &i;
  }
  return nullptr;
}

std::string chest_item_flag(const ChestItem& item, std::string_view area) {
  if (item.flag.empty()) return {};
  switch (item.kind) {
    case K::small_key:
    case K::map:
    case K::compass:
    case K::boss_key:
      if (area.empty()) return {};
      return "DungeonInfo." + std::string(area) + "." + std::string(item.flag);
    default:
      return std::string(item.flag);
  }
}

std::string chest_count_flag(std::uint64_t hash) {
  const ChestPlacement* c = find_chest(hash);
  if (c == nullptr) return {};
  const ChestItem* i = chest_item(chest_drop(*c));
  if (i == nullptr) return {};
  if (i->kind != K::small_key && i->kind != K::heart_piece && i->kind != K::rupees) return {};
  return chest_item_flag(*i, c->area);
}

std::string_view chest_get_stand_in(std::string_view drop) {
  const ChestItem* i = chest_item(drop);
  return i != nullptr ? i->stand_in : std::string_view{};
}

bool is_chest_find(std::string_view actor) { return chest_item(actor) != nullptr; }

std::string chest_found_text(std::string_view player_name, std::string_view drop) {
  std::string name = player_name.empty() ? std::string("Alguien") : std::string(player_name);
  if (const ChestItem* i = chest_item(drop)) {
    return name + " encontró " + std::string(i->words);
  }
  return name + " abrió un cofre";
}

bool is_item_get_command(std::string_view c) {
  if (c.empty()) return false;
  if (starts(c, "DemoGetBottleContent")) return false;
  if (starts(c, "Demo_Get_Kick")) return false;  // the big chest's kick, before the find
  return starts(c, "DemoGet") || starts(c, "EvtGet") || starts(c, "Evt_Get") || starts(c, "Demo_Get_Child") ||
         starts(c, "Demo_Get_Adult");
}

bool is_chest_open_command(std::string_view c) { return starts(c, "DemoOpenTreasureBox"); }

// ---- 2. The lid on every screen ----

void ChestLids::receive(const protocol::ChestOpen& m, std::uint64_t now_ms) {
  if (m.player_number == protocol::kPlayerSelf || !valid_hash(m.chest) || m.scene.empty() ||
      m.animation.name.empty()) {
    return;
  }
  // The same chest told again (a resend): the newest stands.
  std::erase_if(inbox_, [&](const Waiting& w) { return w.message.chest == m.chest; });
  if (inbox_.size() >= 32) inbox_.erase(inbox_.begin());
  inbox_.push_back(Waiting{m, now_ms});
}

void ChestLids::update(const ChestLidInput& in, std::uint64_t now_ms) {
  own_ = in.own;
  if (in.self != 0) self_ = in.self;
  const DoorMove move = door_move_of(in.own_command);
  if (is_chest_move(move) && !is_chest_move(own_move_)) {
    own_move_ms_ = now_ms;
    own_move_command_ = in.own_command;
    own_waiting_ = true;
    own_told_ = false;
    own_said_ = false;
    before_.clear();
    seen_.clear();
  }
  own_move_ = move;
  std::erase_if(lids_, [&](const Lid& l) { return now_ms - std::min(now_ms, l.at_ms) > rules_.lid_keep_ms; });
  std::erase_if(sent_, [&](const auto& s) { return now_ms - std::min(now_ms, s.second) > rules_.resend_ms; });
  std::erase_if(sent_us_, [&](const auto& s) { return !sent_.contains(s.first); });
}

bool ChestLids::wants_actors(std::uint64_t now_ms) const {
  if (!inbox_.empty()) return true;
  // Past the window too until its line is said: decide() says it (the glue only calls decide() while this
  // is true; before, the "nothing told" line never came out of a game).
  return own_waiting_ && !own_told_ &&
         (now_ms - std::min(now_ms, own_move_ms_) <= rules_.open_wait_ms || !own_said_);
}

bool ChestLids::worth_reading(const protocol::Vec3& at) const {
  if (!inbox_.empty()) return true;
  return own_waiting_ && !own_told_ && own_ && distance(*own_, at) <= rules_.reach_m + 0.5f;
}

bool ChestLids::second_to(const protocol::ChestOpen& m) const {
  const auto ours = sent_us_.find(m.chest);
  if (ours == sent_us_.end() || ours->second == 0) return true;  // ours not told: his came first
  if (m.server_time_us == 0) return m.player_number < self_;    // his time not known: the lower number keeps it
  const std::uint64_t apart = m.server_time_us > ours->second ? m.server_time_us - ours->second
                                                              : ours->second - m.server_time_us;
  if (apart <= rules_.tie_us) {
    // A tie: the lower player number keeps the find; both games decide the
    // same way, so exactly one counts it. Our number not known: his wins.
    return self_ == 0 || m.player_number < self_;
  }
  return m.server_time_us < ours->second;  // the earlier one wins
}

std::string ChestLids::tie_text(const protocol::ChestOpen& m) const {
  const auto ours = sent_us_.find(m.chest);
  if (ours == sent_us_.end() || ours->second == 0 || m.server_time_us == 0) return {};
  const std::uint64_t apart = m.server_time_us > ours->second ? m.server_time_us - ours->second
                                                              : ours->second - m.server_time_us;
  if (apart > rules_.tie_us) return {};
  const bool his = self_ == 0 || m.player_number < self_;
  return hex(m.chest) + ": a tie with " + who(m.player_number) + " (" + std::to_string(apart / 1000) +
         " ms apart): the lower player number keeps the find, " +
         (his ? who(m.player_number) + "'s; ours is left out" : std::string("ours; his game leaves his out"));
}

ChestLidOut ChestLids::decide(const ChestLidInput& in, const std::vector<DoorActor>& actors,
                              const std::vector<std::uint64_t>& busy, std::uint64_t now_ms) {
  ChestLidOut out;
  const auto is_busy = [&](std::uint64_t id) { return std::find(busy.begin(), busy.end(), id) != busy.end(); };

  // Ours: the chest our Link opens.
  const bool waiting_own = own_waiting_ && !own_told_;
  if (waiting_own && !in.scene.empty() && own_ && finite(*own_)) {
    const DoorActor* best = nullptr;
    float best_d = std::numeric_limits<float>::infinity();
    // A lid that opens after the window is not ours to tell (only the window's line is still owed).
    const bool in_window = now_ms - std::min(now_ms, own_move_ms_) <= rules_.open_wait_ms;
    for (const auto& a : actors) {
      if (!is_chest(door_kind_of(a.name)) || !valid_hash(a.hash)) continue;
      const float d = distance(*own_, a.position);
      if (d > rules_.reach_m) continue;
      // The first sight of each chest in this move is what it showed before.
      const auto [it, first] = before_.try_emplace(a.id, a.command);
      const bool changed = !first && it->second != a.command;
      // Seen in a game (efectos-cofre, 2026-11-12): the lid starts its opening command in the same frame as our
      // move, so the first sight already reads 'FrontOpen' (and never changes). An opening command at first
      // sight counts; an idle open one ('OpenWait': opened long ago) does not.
      const bool opening_at_first = first && !a.command.ends_with("Wait");
      seen_[a.id] = a.name + " '" + it->second + "' -> '" + a.command + "'";
      if ((changed || opening_at_first) && in_window && door_shows_open(door_kind_of(a.name), a.command) &&
          d < best_d) {
        best = &a;
        best_d = d;
      }
    }
    if (best != nullptr) {
      own_told_ = true;
      if (sent_.contains(best->hash)) {
        out.lines.push_back("our Link opens " + best->name + " " + hex(best->hash) +
                            ": told already, not again");
      } else {
        protocol::ChestOpen m;
        m.player_number = protocol::kPlayerSelf;
        m.chest = best->hash;
        m.scene = in.scene.substr(0, protocol::kMaxSceneChars);
        m.animation.name = best->command.substr(0, protocol::kMaxAnimationChars);
        m.animation.frame = std::isfinite(best->frame) ? best->frame : 0.0f;
        m.animation.rate = 1.0f;
        m.server_time_us = in.server_now_us;
        sent_[best->hash] = now_ms;
        sent_us_[best->hash] = in.server_now_us;
        const ChestPlacement* c = find_chest(best->hash);
        out.lines.push_back("our Link opens " + best->name + " " + hex(best->hash) + " (" +
                            (c != nullptr && !chest_drop(*c).empty() ? std::string(chest_drop(*c))
                                                                      : std::string("not in the chest list")) +
                            "): CHEST_OPEN '" + m.animation.name + "' at frame " + number(m.animation.frame));
        out.send.push_back(std::move(m));
      }
    } else if (now_ms - std::min(now_ms, own_move_ms_) > rules_.open_wait_ms && !own_said_) {
      own_said_ = true;
      own_waiting_ = false;
      // What the chests in reach showed during the move (docs/SONDAS.md: the commands to look for).
      std::string shown;
      for (const auto& [id, text] : seen_) shown += (shown.empty() ? "" : ", ") + text;
      out.lines.push_back("our Link's chest move '" + own_move_command_ + "': no chest within " +
                          number(rules_.reach_m) + " m changed to an opening command; nothing told" +
                          (shown.empty() ? std::string(" (no chest in reach)") : " (" + shown + ")"));
    }
  }

  // Theirs: a lid to open here.
  for (auto it = inbox_.begin(); it != inbox_.end();) {
    const protocol::ChestOpen& m = it->message;
    const std::string head = who(m.player_number) + " opened " + hex(m.chest);
    if (in.scene.empty()) {
      ++it;  // loading: it may be for the place we come to
      continue;
    }
    if (!same_place(in.scene, m.scene)) {
      if (now_ms - std::min(now_ms, it->got_ms) > rules_.inbox_keep_ms) {
        out.lines.push_back(head + " in " + m.scene + ": not our place, no lid to open here");
        it = inbox_.erase(it);
      } else {
        ++it;
      }
      continue;
    }
    const DoorActor* chest = nullptr;
    for (const auto& a : actors) {
      if (a.hash == m.chest && is_chest(door_kind_of(a.name))) {
        chest = &a;
        break;
      }
    }
    if (chest == nullptr) {
      if (now_ms - std::min(now_ms, it->got_ms) > rules_.inbox_keep_ms) {
        out.lines.push_back(head + ": that chest is not loaded here; no lid to open");
        it = inbox_.erase(it);
      } else {
        ++it;
      }
      continue;
    }
    const DoorKind kind = door_kind_of(chest->name);
    if (door_shows_open(kind, chest->command)) {
      out.lines.push_back(head + ": " + chest->name + " shows open here already ('" + chest->command + "')");
      if (const std::string tie = tie_text(m); sent_.contains(m.chest) && !tie.empty()) {
        out.ties.push_back(tie);
        out.lines.push_back(head + ": " + tie);
      }
      if (sent_.contains(m.chest) && second_to(m)) {
        // ORM riesgos-visual: we opened it too, after him: ours counts no more.
        out.second.push_back(m.chest);
        out.lines.push_back(head + ": " + chest->name + " was opened here too, after him: our find of it is not "
                            "counted again");
      }
    } else if (is_busy(chest->id) || lid_opened(chest->id)) {
      out.lines.push_back(head + ": " + chest->name + " is opening here already");
    } else if (is_chest_move(own_move_) && own_ && distance(*own_, chest->position) <= rules_.reach_m) {
      if (!sent_us_.contains(m.chest) && own_waiting_ && !own_told_ &&
          now_ms - std::min(now_ms, own_move_ms_) <= rules_.open_wait_ms) {
        // Ours is not told yet but may be within the tie window (his came in
        // under 50 ms on a LAN): decided once ours is told (the chest then
        // shows open: the tie rule above) or our wait is over. Deciding now
        // would leave ours out while his game, seeing a tie it gives to our
        // lower number, leaves his out too: the find counted by nobody.
        ++it;
        continue;
      }
      if (const std::string tie = tie_text(m); !tie.empty()) {
        out.ties.push_back(tie);
        out.lines.push_back(head + ": " + tie);
      }
      if (second_to(m)) {
        // ORM riesgos-visual: his CHEST_OPEN came first: our find of it is not counted again.
        out.second.push_back(m.chest);
        out.lines.push_back(head + ": " + chest->name +
                            " left alone, our Link is opening it too, after him: our find of it is not counted "
                            "again");
      } else {
        out.lines.push_back(head + ": " + chest->name +
                            " left alone, our Link is opening it too, before him: his find is his game's to "
                            "leave out");
      }
    } else {
      ChestLidPlay p;
      p.actor = chest->id;
      p.command = m.animation.name;
      std::uint64_t since_us = 0;
      if (in.server_now_us > m.server_time_us && m.server_time_us != 0) {
        since_us = std::min<std::uint64_t>(in.server_now_us - m.server_time_us, rules_.carry_max_us);
      }
      const float rate = std::isfinite(m.animation.rate) && m.animation.rate > 0.0f ? m.animation.rate : 1.0f;
      const float carried = static_cast<float>(static_cast<double>(since_us) / 1e6) * rules_.frames_per_second * rate;
      const float frame = (std::isfinite(m.animation.frame) ? m.animation.frame : 0.0f) + carried;
      if (frame >= rules_.min_carry_frames) p.frame = frame;
      p.line = head + ": " + chest->name + " opens here too ('" + p.command + "'" +
               (p.frame ? ", frame " + number(*p.frame) : std::string()) + ")";
      lids_.push_back(Lid{chest->id, now_ms});
      out.plays.push_back(std::move(p));
    }
    it = inbox_.erase(it);
  }
  return out;
}

void ChestLids::reset() {
  own_.reset();
  own_move_ = DoorMove::none;
  own_move_ms_ = 0;
  own_move_command_.clear();
  own_waiting_ = false;
  own_told_ = false;
  own_said_ = false;
  before_.clear();
  seen_.clear();
  lids_.clear();
}

bool ChestLids::lid_opened(std::uint64_t actor) const {
  return std::any_of(lids_.begin(), lids_.end(), [&](const Lid& l) { return l.actor == actor; });
}

std::vector<std::uint64_t> ChestLids::lids_opened() const {
  std::vector<std::uint64_t> out;
  out.reserve(lids_.size());
  for (const auto& l : lids_) out.push_back(l.actor);
  return out;
}

// ---- 3. The find on every screen ----

void ChestFinds::opened(const protocol::ChestOpen& m, std::uint64_t now_ms) {
  if (m.player_number == protocol::kPlayerSelf) return;
  Find f;
  f.serial = next_serial_++;
  if (next_serial_ == 0) next_serial_ = 1;
  if (const ChestPlacement* c = find_chest(m.chest)) {
    f.drop = std::string(chest_drop(*c));
    f.area = c->area;
  }
  f.scene = m.scene;
  f.got_ms = now_ms;
  finds_[m.player_number] = std::move(f);
}

ChestFindOut ChestFinds::tick(const std::optional<std::string>& our_scene, const std::vector<FindMate>& mates,
                              const std::function<std::string(std::uint8_t)>& name, std::uint64_t now_ms) {
  ChestFindOut out;
  for (auto it = finds_.begin(); it != finds_.end();) {
    const std::uint8_t n = it->first;
    Find& f = it->second;
    const FindMate* mate = nullptr;
    for (const auto& m : mates) {
      if (m.number == n) mate = &m;
    }
    const std::optional<std::string_view> ours =
        our_scene ? std::optional<std::string_view>(*our_scene) : std::nullopt;
    const std::optional<std::string_view> theirs =
        mate && mate->scene ? std::optional<std::string_view>(*mate->scene) : std::nullopt;
    const bool here = mate != nullptr && mate->shown && same_place(ours, theirs);
    const bool item_get = here && is_item_get_command(mate->command);
    // The remake: no item-get command, the chest's own one runs through the find (chest_hold_after_ms).
    const bool by_chest = here && !item_get && rules_.chest_hold_after_ms != 0 &&
                          is_chest_open_command(mate->command) &&
                          now_ms - std::min(now_ms, f.got_ms) >= rules_.chest_hold_after_ms;
    const bool holds = item_get || by_chest;
    const auto say = [&](const char* why) {
      if (f.said) return;
      f.said = true;
      FoundNotice notice;
      notice.number = n;
      notice.name = name ? name(n) : std::string();
      notice.text = chest_found_text(notice.name, f.drop);
      out.lines.push_back(who(n) + " found " + (f.drop.empty() ? std::string("something") : f.drop) + " (" +
                          why + "): '" + notice.text + "'");
      out.notices.push_back(std::move(notice));
    };
    if (holds) {
      if (f.held_since_ms == 0) {
        f.held_since_ms = now_ms;
        f.by_chest_command = by_chest;
        if (!f.drop.empty()) {
          if (by_chest) {
            char frame[32];
            std::snprintf(frame, sizeof frame, "%.1f", static_cast<double>(mate->frame));
            out.lines.push_back(who(n) + " holds up " + f.drop + " ('" + mate->command + "' at frame " + frame +
                                ", " + std::to_string(now_ms - std::min(now_ms, f.got_ms)) +
                                " ms after the lid: the chest's own command, no item-get command seen)");
          } else {
            out.lines.push_back(who(n) + " holds up " + f.drop + " ('" + mate->command + "')");
          }
        }
      }
      f.held_last_ms = now_ms;
      say("held up");
    }
    const bool held_over = f.held_since_ms != 0 &&
                           (now_ms - f.held_last_ms > rules_.held_grace_ms ||
                            now_ms - f.held_since_ms > rules_.held_max_ms);
    if (!f.said && now_ms - std::min(now_ms, f.got_ms) >= rules_.notice_after_ms) {
      say(here ? "no item-get seen" : "elsewhere");
    }
    if (held_over || (f.held_since_ms == 0 && now_ms - std::min(now_ms, f.got_ms) > rules_.forget_ms)) {
      it = finds_.erase(it);
      continue;
    }
    if (f.held_since_ms != 0 && here && !f.drop.empty() && is_chest_find(f.drop) && finite(mate->position)) {
      HeldFind h;
      h.number = n;
      h.serial = f.serial;
      h.drop = f.drop;
      h.position = mate->position;
      h.position[1] += mate->adult ? rules_.above_adult_m : rules_.above_child_m;
      out.held.push_back(std::move(h));
    }
    ++it;
  }
  return out;
}

}  // namespace coop::host
