#include "coop/host/flags.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace coop::host {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

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

bool is_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// A token of 0x and hex digits only; nothing when it is not one.
std::optional<std::string_view> hex_digits(std::string_view token) {
  if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) {
    return std::nullopt;
  }
  const std::string_view digits = token.substr(2);
  if (!std::all_of(digits.begin(), digits.end(), is_hex)) return std::nullopt;
  return digits;
}

std::optional<std::uint16_t> parse_index(std::string_view text) {
  if (text.empty() || text.size() > 5) return std::nullopt;
  unsigned value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size() || value > 0xffff) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(value);
}

// A 64-bit key after "key64:": 0x and 1 to 16 hex digits, or decimal.
std::optional<std::uint64_t> parse_key64(std::string_view text) {
  std::uint64_t value = 0;
  int base = 10;
  if (const auto digits = hex_digits(text)) {
    if (digits->size() > 16) return std::nullopt;
    text = *digits;
    base = 16;
  }
  if (text.empty()) return std::nullopt;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
  return value;
}

constexpr std::string_view kKey64Prefix = "key64:";
constexpr std::string_view kEnumPrefix = "enum:";

// A flag or value token: 0x and 1 to 8 hex digits (a hash), or a name to hash.
std::optional<std::uint32_t> parse_hash(std::string_view token, std::string_view& why) {
  if (const auto digits = hex_digits(token)) {
    if (digits->size() > 8) {
      why = "a hash has at most 8 hex digits";
      return std::nullopt;
    }
    std::uint32_t hash = 0;
    std::from_chars(digits->data(), digits->data() + digits->size(), hash, 16);
    return hash;
  }
  if (token.empty()) {
    why = "a name is printable ASCII";
    return std::nullopt;
  }
  for (const char c : token) {
    if (c < 0x21 || c > 0x7e) {
      why = "a name is printable ASCII";
      return std::nullopt;
    }
  }
  return flag_hash(token);
}

}  // namespace

bool EnumFlags::add(std::uint32_t hash, std::vector<std::uint32_t> values, bool steps) {
  if (hash == 0 || values.empty() || values.size() > kMaxValues || contains(hash)) return false;
  std::vector<std::uint32_t> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return false;
  flags_.emplace(hash, Flag{std::move(values), steps});
  return true;
}

bool EnumFlags::merge(const EnumFlags& other) {
  for (const auto& [hash, flag] : other.flags_) {
    const auto it = flags_.find(hash);
    if (it == flags_.end()) {
      flags_.emplace(hash, flag);
    } else if (!(it->second == flag)) {
      return false;
    }
  }
  return true;
}

bool EnumFlags::allows(std::uint32_t hash, std::uint32_t value) const {
  return rank(hash, value).has_value();
}

const std::vector<std::uint32_t>* EnumFlags::values(std::uint32_t hash) const {
  const auto it = flags_.find(hash);
  return it == flags_.end() ? nullptr : &it->second.values;
}

bool EnumFlags::steps(std::uint32_t hash) const {
  const auto it = flags_.find(hash);
  return it != flags_.end() && it->second.steps;
}

std::optional<std::size_t> EnumFlags::rank(std::uint32_t hash, std::uint32_t value) const {
  const auto* set = values(hash);
  if (set == nullptr) return std::nullopt;
  const auto it = std::find(set->begin(), set->end(), value);
  if (it == set->end()) return std::nullopt;
  return static_cast<std::size_t>(it - set->begin());
}

std::vector<FlagKey> EnumFlags::keys() const {
  std::vector<FlagKey> out;
  out.reserve(flags_.size());
  for (const auto& [hash, flag] : flags_) out.push_back(FlagKey{hash, 0});
  return out;
}

SharedFlags::SharedFlags(std::vector<FlagKey> keys, EnumFlags enums)
    : keys_(std::move(keys)), enums_(std::move(enums)) {
  const auto enum_keys = enums_.keys();
  keys_.insert(keys_.end(), enum_keys.begin(), enum_keys.end());
  std::sort(keys_.begin(), keys_.end());
  keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
}

std::optional<SharedFlags> SharedFlags::parse(std::string_view text, std::string* error) {
  std::vector<FlagKey> keys;
  EnumFlags enums;
  std::size_t line_number = 0;
  auto fail = [&](std::string_view why) -> std::optional<SharedFlags> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + std::string(why);
    return std::nullopt;
  };

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

    if (parts[0].starts_with(kEnumPrefix)) {
      std::string_view why;
      const auto hash = parse_hash(parts[0].substr(kEnumPrefix.size()), why);
      if (!hash) return fail(why);
      if (*hash == 0) return fail("an Enum flag's hash is not 0");
      if (parts.size() < 2) return fail("an Enum flag lists its values");
      if (parts.size() - 1 > EnumFlags::kMaxValues) return fail("too many Enum values");
      std::vector<std::uint32_t> values;
      for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto value = parse_hash(parts[i], why);
        if (!value) return fail(why);
        values.push_back(*value);
      }
      if (enums.contains(*hash)) return fail("an Enum flag is listed once");
      if (!enums.add(*hash, std::move(values))) return fail("an Enum value is listed once");
      if (keys.size() + enums.size() > kMaxFlags) return fail("too many flags");
      continue;  // its key comes from `enums`
    }

    if (parts.size() > 2) return fail("more than a flag and an index");

    if (parts[0].starts_with(kKey64Prefix)) {
      if (parts.size() != 1) return fail("a 64-bit key has no index");
      const auto key = parse_key64(parts[0].substr(kKey64Prefix.size()));
      if (!key) return fail("a 64-bit key is 0x and 1 to 16 hex digits, or decimal");
      if (keys.size() + 1 > kMaxFlags) return fail("too many flags");
      keys.push_back(FlagKey::key64_flag(*key));
      continue;
    }

    std::uint32_t hash = 0;
    if (const auto digits = hex_digits(parts[0])) {
      if (digits->size() > 8) return fail("a hash has at most 8 hex digits");
      std::from_chars(digits->data(), digits->data() + digits->size(), hash, 16);
    } else {
      for (const char c : parts[0]) {
        if (c < 0x21 || c > 0x7e) return fail("a name is printable ASCII");
      }
      hash = flag_hash(parts[0]);
    }

    std::uint16_t first = 0;
    std::uint16_t last = 0;
    if (parts.size() == 2) {
      const std::string_view index = parts[1];
      const std::size_t dash = index.find('-');
      const auto a = parse_index(index.substr(0, dash));
      const auto b = dash == std::string_view::npos ? a : parse_index(index.substr(dash + 1));
      if (!a || !b) return fail("an index is 0 to 65535, or a range such as 0-7");
      if (*b < *a) return fail("a range goes up");
      first = *a;
      last = *b;
    }
    if (keys.size() + (last - first) + 1u > kMaxFlags) return fail("too many flags");
    for (std::uint32_t i = first; i <= last; ++i) {
      keys.push_back(FlagKey{hash, static_cast<std::uint16_t>(i)});
    }
  }
  if (keys.size() + enums.size() > kMaxFlags) return fail("too many flags");
  return SharedFlags(std::move(keys), std::move(enums));
}

std::optional<SharedFlags> SharedFlags::load(const std::filesystem::path& path,
                                             std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error);
}

bool SharedFlags::add(FlagKey key) {
  const auto at = std::lower_bound(keys_.begin(), keys_.end(), key);
  if ((at != keys_.end() && *at == key) || keys_.size() >= kMaxFlags) return false;
  keys_.insert(at, key);
  return true;
}

bool SharedFlags::contains(FlagKey key) const {
  return std::binary_search(keys_.begin(), keys_.end(), key);
}

}  // namespace coop::host
