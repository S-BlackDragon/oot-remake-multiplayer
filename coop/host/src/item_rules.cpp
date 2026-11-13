#include "coop/host/item_rules.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace coop::host {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && is_space(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

// A flag token: 0x and 1 to 8 hex digits (not 0), or a printable ASCII name.
std::optional<std::uint32_t> flag_of(std::string_view token) {
  if (token.empty()) return std::nullopt;
  if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    const std::string_view digits = token.substr(2);
    if (digits.empty() || digits.size() > 8) return std::nullopt;
    std::uint32_t hash = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (ec != std::errc() || end != digits.data() + digits.size() || hash == 0) return std::nullopt;
    return hash;
  }
  for (const char c : token) {
    if (c < 0x21 || c > 0x7e) return std::nullopt;
  }
  return flag_hash(token);
}

std::optional<ItemRule> rule_of_word(std::string_view word) {
  if (word == "max") return ItemRule::max;
  if (word == "any") return ItemRule::any;
  if (word == "mirror") return ItemRule::mirror;
  if (word == "count") return ItemRule::count;
  if (word == "player" || word == "per_player") return ItemRule::per_player;
  return std::nullopt;
}

std::string hex(std::uint32_t hash) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(hash));
  return text;
}

// The table's entry as built (docs/items.md), whatever was changed since.
const ItemFlag* built_in(std::uint32_t hash) {
  const ItemFlag* item = find_item(hash);  // the kind and name never change
  return item != nullptr && built_in_item_rule(hash) ? item : nullptr;
}

bool is_bottle_content(const ItemFlag& item) {
  return item.name.starts_with("PlayerStatus.AssignableItem.BottleInfo.") &&
         item.name.ends_with(".BottleItem");
}

}  // namespace

std::string_view item_rule_name(ItemRule rule) {
  switch (rule) {
    case ItemRule::max: return "max";
    case ItemRule::any: return "any";
    case ItemRule::mirror: return "mirror";
    case ItemRule::count: return "count";
    case ItemRule::per_player: return "player";
  }
  return "?";
}

std::optional<ItemRule> ItemRules::rule_of(std::uint32_t hash) const {
  for (const ItemRuleLine& line : rules_) {
    if (line.hash == hash) return line.rule;
  }
  return built_in_item_rule(hash);
}

std::optional<ItemRules> ItemRules::parse(std::string_view text, std::string* error) {
  ItemRules list;
  std::size_t line_number = 0;
  auto fail = [&](std::string_view why) -> std::optional<ItemRules> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + std::string(why);
    return std::nullopt;
  };
  // Caps and fills are checked against the rules once every line is read
  // (a cap may come before its count's rule line).
  struct Pending {
    std::size_t line;
    std::uint32_t a, b;
    bool fill;
  };
  std::vector<Pending> pending;

  while (!text.empty()) {
    ++line_number;
    const std::size_t newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    const auto parts = tokens(line);
    if (parts.empty()) continue;

    if (parts[0] == "cap" || parts[0] == "fill") {
      const bool fill = parts[0] == "fill";
      if (parts.size() != 3) return fail(std::string(parts[0]) + " takes two flags");
      const auto a = flag_of(parts[1]);
      const auto b = flag_of(parts[2]);
      if (!a || !b) return fail("a flag is a name or 0x and 1 to 8 hex digits");
      if (*a == *b) return fail(std::string(parts[0]) + " of a flag by itself");
      if (fill) {
        const auto at = std::find_if(list.fills_.begin(), list.fills_.end(),
                                     [&](const ItemFill& f) { return f.from == *a; });
        if (at != list.fills_.end()) {
          if (at->to != *b) return fail(hex(*a) + " fills two flags");
          continue;
        }
        list.fills_.push_back(ItemFill{*a, *b});
      } else {
        const auto at = std::find_if(list.caps_.begin(), list.caps_.end(),
                                     [&](const ItemCap& c) { return c.count == *a; });
        if (at != list.caps_.end()) {
          if (at->cap != *b) return fail(hex(*a) + " has two caps");
          continue;
        }
        list.caps_.push_back(ItemCap{*a, *b});
      }
      pending.push_back(Pending{line_number, *a, *b, fill});
      continue;
    }

    const auto rule = rule_of_word(parts[0]);
    if (!rule) return fail("unknown rule '" + std::string(parts[0]) +
                           "' (max, any, mirror, count, player, cap or fill)");
    if (parts.size() != 2) return fail(std::string(parts[0]) + " takes one flag");
    const auto hash = flag_of(parts[1]);
    if (!hash) return fail("a flag is a name or 0x and 1 to 8 hex digits");
    const ItemFlag* item = built_in(*hash);
    if (item == nullptr) return fail(std::string(parts[1]) + " is not an item flag (docs/items.md)");
    if (*rule == ItemRule::count && item->kind != protocol::FlagKind::int32) {
      return fail(std::string(item->name) + " is not an Int: it cannot be a count");
    }
    if (*rule != ItemRule::per_player && is_bottle_content(*item)) {
      return fail(std::string(item->name) +
                  ": what is in a bottle stays with each player (the fairy revive writes one)");
    }
    const auto at = std::find_if(list.rules_.begin(), list.rules_.end(),
                                 [&](const ItemRuleLine& l) { return l.hash == *hash; });
    if (at != list.rules_.end()) {
      if (at->rule != *rule) {
        return fail(std::string(item->name) + " is given two rules (" +
                    std::string(item_rule_name(at->rule)) + " and " +
                    std::string(item_rule_name(*rule)) + ")");
      }
      continue;
    }
    list.rules_.push_back(ItemRuleLine{std::string(parts[1]), *hash, *rule});
  }

  for (const Pending& p : pending) {
    line_number = p.line;
    const ItemFlag* a = built_in(p.a);
    if (p.fill) {
      const ItemFlag* b = built_in(p.b);
      if (a == nullptr || b == nullptr) return fail("fill takes two item flags (docs/items.md)");
      if (a->kind != protocol::FlagKind::int32 || b->kind != protocol::FlagKind::int32) {
        return fail("fill takes two Ints");
      }
      if (list.rule_of(p.a) != ItemRule::max) return fail(std::string(a->name) + " is not a max flag");
      if (list.rule_of(p.b) != ItemRule::per_player) {
        return fail(std::string(b->name) + " is shared: only a per-player flag is filled");
      }
    } else {
      if (a == nullptr || list.rule_of(p.a) != ItemRule::count) {
        return fail("cap takes a count (docs/items.md) and the flag of its maximum");
      }
    }
  }
  return list;
}

std::optional<ItemRules> ItemRules::load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  std::string why;
  auto list = parse(text.str(), &why);
  if (!list && error) *error = path.string() + ": " + why;
  return list;
}

ItemRulesApplied apply_item_rules(const ItemRules& rules) {
  reset_item_rules();
  ItemRulesApplied applied;
  for (const ItemRuleLine& line : rules.rules()) {
    if (built_in_item_rule(line.hash) == line.rule) {
      ++applied.same;
      continue;
    }
    if (set_item_rule(line.hash, line.rule)) ++applied.changed;  // parse checked it
  }
  set_item_caps(rules.caps());
  set_item_fills(rules.fills());
  applied.caps = rules.caps().size();
  applied.fills = rules.fills().size();
  return applied;
}

}  // namespace coop::host
