#include "coop/host/hearts.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace coop::host {
namespace {

// Offsets: the ones read_vitals uses (downed.cpp; docs/objects.md).
constexpr std::uint64_t kActorComponents = 0x270;
constexpr std::uint64_t kComponentLife = 0x2b8;
constexpr std::uint64_t kLifeValue = 0x1c10;
constexpr std::uint64_t kLifeMax = 0x1c18;
constexpr std::uint64_t kValueAt = 8;

bool usable(std::int32_t quarters) { return quarters > 0 && quarters <= kMostHeartQuarters; }

std::string who(std::uint8_t from) { return from == 0 ? std::string("another game") : "P" + std::to_string(from); }

bool write_s32(WritableGameMemory& memory, std::uint64_t address, std::int32_t value) {
  std::array<std::uint8_t, 4> bytes{};
  const auto bits = static_cast<std::uint32_t>(value);
  for (std::size_t i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(bits >> (8 * i));
  return memory.write(address, bytes);
}

}  // namespace

std::optional<LifeWords> find_life_words(GameMemory& memory, std::uint64_t player_actor) {
  const auto table = read_ptr(memory, player_actor + kActorComponents);
  const auto life = table ? read_ptr(memory, *table + kComponentLife) : std::nullopt;
  const auto value = life ? read_ptr(memory, *life + kLifeValue) : std::nullopt;
  const auto max = life ? read_ptr(memory, *life + kLifeMax) : std::nullopt;
  if (!value || !max) return std::nullopt;
  return LifeWords{*value + kValueAt, *max + kValueAt};
}

HeartsAction SharedHearts::update(const HeartsInput& in) {
  HeartsAction out;
  if (in.relearn && capacity_) {
    out.log.push_back("hearts: a save load or a new session: the group's max life is learnt again (was " +
                      std::to_string(*capacity_) + ")");
    capacity_.reset();
  }
  const auto raise = [&](std::int32_t value, const std::string& source) {
    if (!usable(value) || (capacity_ && value <= *capacity_)) return;
    if (!capacity_) {
      // The first reading of a session: the base, nobody gained anything.
      out.log.push_back("hearts: max life " + std::to_string(value) + " (" + source + ", first reading)");
    } else {
      out.log.push_back("hearts: max life " + std::to_string(*capacity_) + " -> " + std::to_string(value) + " (" +
                        source + ")");
    }
    capacity_ = value;
  };
  // Our own first: a container (or a fourth piece) we just took is ours, even
  // when the game's copy of it reaches the flag in the same frame.
  if (in.link_max) raise(*in.link_max, "ours");
  for (const auto& got : in.received) raise(got.value, "from " + who(got.from));
  if (in.flag) raise(*in.flag, "the MaxLife flag");

  if (!capacity_ || !in.link_max || !usable(*in.link_max)) return out;
  const std::int32_t max = std::max(*in.link_max, *capacity_);
  if (max > *in.link_max) {
    out.max = max;
    out.log.push_back("hearts: our Link's max " + std::to_string(*in.link_max) + " -> " + std::to_string(max) +
                      " (life " + (in.link_life ? std::to_string(*in.link_life) : std::string("?")) + " kept)");
  }
  if (in.link_life && *in.link_life > max) {
    out.life = max;
    out.log.push_back("hearts: our Link's life " + std::to_string(*in.link_life) + " brought down to the max " +
                      std::to_string(max));
  }
  return out;
}

std::vector<HeartsInput::Got> max_life_received(const std::vector<FlagWatcher::Received>& received) {
  std::vector<HeartsInput::Got> out;
  for (const auto& r : received) {
    if (r.key.kind != protocol::FlagKeyKind::named || r.key.hash != kMaxLifeFlag || r.key.index != 0) continue;
    if (r.after.kind != protocol::FlagKind::int32) continue;
    out.push_back({r.from, static_cast<std::int32_t>(r.after.value)});
  }
  return out;
}

bool apply_hearts(WritableGameMemory& memory, const LifeWords& words, const HeartsAction& action) {
  bool ok = true;
  // The max first: a life is never left above the max, not even between the writes.
  if (action.max) ok = write_s32(memory, words.max, *action.max) && ok;
  if (action.life) ok = write_s32(memory, words.life, *action.life) && ok;
  return ok;
}

}  // namespace coop::host
