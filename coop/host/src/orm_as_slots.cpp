#include "coop/host/orm_as_slots.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

#include "addresses.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host::orm {
namespace {

// The name at `at`, in 16-byte reads that never cross a 16-byte boundary (as
// PlayerReader::read_animation: a name near the end of a mapped page still
// reads). "" when it does not read as a command name.
std::string read_name(GameMemory& memory, std::uint64_t at) {
  std::string name;
  bool terminated = false;
  while (!terminated && name.size() <= protocol::kMaxAnimationChars) {
    std::array<std::uint8_t, 16> chunk{};
    const std::size_t size = 16 - static_cast<std::size_t>(at & 15);
    if (!memory.read(at, std::span(chunk.data(), size))) return {};
    for (std::size_t i = 0; i < size; ++i) {
      if (chunk[i] == 0) {
        terminated = true;
        break;
      }
      name.push_back(static_cast<char>(chunk[i]));
    }
    at += size;
  }
  if (!terminated || name.empty() || name.size() > protocol::kMaxAnimationChars) return {};
  for (const char c : name) {
    if (c < 0x20 || c > 0x7e) return {};
  }
  return name;
}

}  // namespace

std::vector<std::string> read_as_commands(GameMemory& memory, std::uint64_t actor, std::size_t max_slots) {
  std::vector<std::string> out;
  if (actor == 0 || max_slots == 0) return out;
  const auto components = read_ptr(memory, actor + addr::kActorComponents);
  if (!components) return out;
  const auto as = read_ptr(memory, *components + addr::kComponentsAs);
  if (!as) return out;
  const auto inner = read_ptr(memory, *as + addr::kAsComponentInner);
  if (!inner) return out;
  const auto controller = read_ptr(memory, *inner + addr::kAsInnerController);
  if (!controller) return out;
  const auto count = read_u32(memory, *controller + addr::kAsSlotCount);
  // A signed count, as the game compares it.
  if (!count || static_cast<std::int32_t>(*count) <= 0) return out;
  const auto array = read_ptr(memory, *controller + addr::kAsSlotArray);
  if (!array) return out;
  const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(static_cast<std::int32_t>(*count)), max_slots);
  for (std::size_t i = 0; i < n; ++i) {
    std::string name;
    if (const auto slot = read_ptr(memory, *array + i * addr::kAsSlotStride)) {
      if (const auto name_at = read_ptr(memory, *slot + addr::kAsSlotCommandName)) {
        name = read_name(memory, *name_at);
      }
    }
    out.push_back(std::move(name));
  }
  return out;
}

}  // namespace coop::host::orm
