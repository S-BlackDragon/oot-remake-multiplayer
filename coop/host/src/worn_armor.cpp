#include "coop/host/worn_armor.hpp"

#include <array>
#include <utility>

#include "addresses.hpp"

namespace coop::host::worn {

bool shown_armor(std::string_view name) {
  return name.starts_with("Armor_") && name.find("_Default_") == std::string_view::npos;
}

bool strayable_armor(std::string_view name) {
  return shown_armor(name) && name.find("_Normal") == std::string_view::npos;
}

std::optional<std::string> worn_or_flag(const std::optional<std::string>& worn,
                                        const std::optional<std::string>& flag) {
  if (worn && shown_armor(*worn)) return worn;
  return flag;
}

std::optional<std::string> link_name(GameMemory& memory, std::uint64_t link) {
  const auto id = read_u32(memory, link + addr::kEquipSlotId);
  if (!id) return std::nullopt;
  if (*id == addr::kEmptyId) return std::string{};
  // As PlayerReader::read_slot_name: the holder must still hold this id and
  // its actor must not be gone.
  const auto holder = read_ptr(memory, link + addr::kEquipSlotHolder);
  if (!holder) return std::string{};
  const auto holder_id = read_u32(memory, *holder + addr::kHolderId);
  if (!holder_id || *holder_id != *id) return std::string{};
  const auto piece = read_ptr(memory, *holder + addr::kHolderActor);
  if (!piece) return std::string{};
  const auto state = read_u32(memory, *piece + addr::kActorState);
  if (!state) return std::nullopt;
  if (*state >= addr::kActorStateGoneFirst && *state <= addr::kActorStateGoneLast) {
    return std::string{};
  }
  const auto text = read_ptr(memory, *piece + addr::kActorName);
  if (!text) return std::nullopt;
  std::array<std::uint8_t, protocol::kMaxEquipNameChars + 1> bytes{};
  if (!memory.read(*text, bytes)) return std::nullopt;
  std::string name;
  for (const std::uint8_t b : bytes) {
    if (b == 0) return name.empty() ? std::nullopt : std::optional<std::string>(name);
    if (b < 0x20 || b > 0x7e) return std::nullopt;
    name.push_back(static_cast<char>(b));
  }
  return std::nullopt;  // longer than a name can be
}

std::optional<std::uint64_t> equipment_user(GameMemory& memory, std::uint64_t actor) {
  const auto components = read_ptr(memory, actor + addr::kActorComponents);
  if (!components) return std::nullopt;
  return read_ptr(memory, *components + addr::kComponentsEquipmentUser);
}

std::optional<std::string> armor_name(GameMemory& memory, std::uint64_t user, std::uint32_t index) {
  if (index >= kArmorLinkCount) return std::nullopt;
  return link_name(memory, user + kArmorLinks + kLinkStride * index);
}

void apply(GameMemory& memory, std::uint64_t actor, protocol::Equipment& equipment) {
  const auto user = equipment_user(memory, actor);
  if (!user) return;
  auto head = armor_name(memory, *user, kArmorHead);
  if (!head || !shown_armor(*head)) {
    if (auto helmet = link_name(memory, *user + addr::kEquipSlots + kLinkStride * kHelmetSlot);
        helmet && shown_armor(*helmet)) {
      head = std::move(helmet);
    }
  }
  equipment.head = worn_or_flag(head, equipment.head);
  equipment.armor = worn_or_flag(armor_name(memory, *user, kArmorUpper), equipment.armor);
  equipment.boots = worn_or_flag(armor_name(memory, *user, kArmorLower), equipment.boots);
  equipment.hand = worn_or_flag(armor_name(memory, *user, kArmorHand), equipment.hand);
}

}  // namespace coop::host::worn
