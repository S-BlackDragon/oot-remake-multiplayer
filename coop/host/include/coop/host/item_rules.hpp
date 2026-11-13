#pragma once

// ORM item rule list (oot-remake docs/INVENTARIO.md): changes to the rules
// of the item table (game_items.hpp, docs/items.md) from plain text, applied
// for the whole build with LICHII_ORM_ITEM_RULES=<file> (the kit's
// shared_items.txt: ZMP's inventory rules, where they differ from Caleb's).
//
//   # a comment (to the end of the line); blank lines are fine
//   count PlayerStatus.CurrentRupee     the rule of an item flag: max, any,
//                                       mirror, count or player
//   cap PlayerStatus.CurrentRupee 0xe0010a50
//                                       a count's gain stops at the value of
//                                       another Int (the wallet's capacity)
//   fill PlayerStatus.MPMax PlayerStatus.MP
//                                       a max Int raised by another player
//                                       fills a per-player Int to it
//
// A flag token is a name (hashed with flag_hash) or 0x and 1 to 8 hex
// digits. A rule line must name a flag of the item table, and count only an
// Int. A cap's count must be a count and a fill's from a max Int, its to a
// per-player Int, after the list's own rules. What is in a bottle
// (BottleInfo.Bottle<X>.BottleItem) always stays with each player: the fairy
// revive puts a fairy in one and must never send it (CAIDO_REVIVIR.md). A
// flag given two different rules (or two caps, two fills) is an error; the
// same line twice is fine. No game data here: plain text.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/game_items.hpp"

namespace coop::host {

struct ItemRuleLine {
  std::string name;        // as written: a name or 0x hash
  std::uint32_t hash = 0;  // flag_hash(name)
  ItemRule rule = ItemRule::per_player;
  bool operator==(const ItemRuleLine&) const = default;
};

class ItemRules {
 public:
  ItemRules() = default;

  // Nothing on a bad line; `error`, when given, says which and why.
  static std::optional<ItemRules> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<ItemRules> load(const std::filesystem::path& path,
                                       std::string* error = nullptr);

  const std::vector<ItemRuleLine>& rules() const { return rules_; }
  const std::vector<ItemCap>& caps() const { return caps_; }
  const std::vector<ItemFill>& fills() const { return fills_; }
  bool empty() const { return rules_.empty() && caps_.empty() && fills_.empty(); }

  // The rule `hash` has with this list over the built-in table; nothing when
  // it is not an item flag.
  std::optional<ItemRule> rule_of(std::uint32_t hash) const;

 private:
  std::vector<ItemRuleLine> rules_;  // in file order, each flag once
  std::vector<ItemCap> caps_;
  std::vector<ItemFill> fills_;
};

struct ItemRulesApplied {
  std::size_t changed = 0;  // rules that differ from docs/items.md
  std::size_t same = 0;     // rule lines that say what the table already says
  std::size_t caps = 0;
  std::size_t fills = 0;
};

// reset_item_rules(), then the list's rules, caps and fills over the table.
// Call before the session starts (not thread safe).
ItemRulesApplied apply_item_rules(const ItemRules& rules);

// "max", "any", "mirror", "count", "player".
std::string_view item_rule_name(ItemRule rule);

}  // namespace coop::host
