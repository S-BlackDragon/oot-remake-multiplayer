// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM platform layer, the emulator's adapter (platform_emulator.h).

#include "platform_emulator.h"

#include <utility>

#include "coop_boss.h"
#include "coop_svc.h"
#include "common/fs/path_util.h"
#include "coop_testinput.h"
#include "core/core.h"
#include "core/memory.h"
#ifdef COOP_HOOKSHOT
// ORM test10: upstream's Hookshot module (hookshot/CMakeLists.txt) builds the hook files against
// hookshot/compat, which has no hid_core: the pad's word comes from Hookshot's bridge instead.
#include "coop_runtime.h"
#else
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#endif

namespace CoopPlatform {

static_assert(kStickMax == CoopTestInput::kStickMax, "platform.h's stick range is the HID's");
#ifndef COOP_HOOKSHOT
static_assert(kButtonA == static_cast<std::uint64_t>(Core::HID::NpadButton::A), "platform.h's buttons are Npad bits");
#endif

Emulator::Emulator(Core::System& system, std::uint64_t main_base)
    : system_(system), main_base_(main_base), memory_(system.ApplicationMemory(), main_base) {}

bool Emulator::Write64(std::uint64_t address, std::uint64_t value) {
    // As coop_held_switch_game.cpp did: the range checked, then the
    // emulator's own 64-bit write (not the unsafe block copy).
    auto& memory = system_.ApplicationMemory();
    if (!memory.IsValidVirtualAddressRange(address, 8)) {
        return false;
    }
    memory.Write64(address, value);
    return true;
}

std::optional<std::uint64_t> Emulator::PadButtons() {
#ifdef COOP_HOOKSHOT
    // Hookshot: the word its nn::hid last gave the game for player 1 (HosCoopApi::pad_buttons,
    // Npad bits as the emulated pad's), as upstream's coop_ocarina_sound.cpp reads it. An older
    // bridge without it: no buttons (A to revive, the quick messages' L do nothing), said once.
    std::uint64_t buttons = 0;
    if (CoopRuntime::PadButtons(buttons)) {
        return buttons;
    }
    static bool said = false;
    if (!std::exchange(said, true)) {
        ORM_LOG_INFO("coop: platform: Hookshot's bridge gives no pad buttons (an older build): our "
                     "features that read the pad (revive with A, quick messages) do nothing");
    }
    return std::nullopt;
#else
    // Player 1's pad, or the handheld one, as coop_testinput plays into it:
    // the buttons include the scripted ones.
    auto& hid = system_.HIDCore();
    auto* pad = hid.GetEmulatedController(Core::HID::NpadIdType::Player1);
    if (pad == nullptr || !pad->IsConnected()) {
        pad = hid.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    }
    if (pad == nullptr || !pad->IsConnected()) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(pad->GetNpadButtons().raw);
#endif
}

void Emulator::NudgeRightStick(std::optional<Stick> right) {
    CoopTestInput::Nudge(system_, right ? std::optional<CoopTestInput::Stick>(CoopTestInput::Stick{right->x, right->y})
                                        : std::nullopt);
}

bool Emulator::AskSceneChange(std::string_view scene, std::string_view start) {
    return CoopBoss::AskSceneChange(system_, main_base_, scene, start);
}

bool Emulator::MoveLink(float x, float y, float z) {
    return CoopBoss::MoveLink(system_, main_base_, CoopBoss::Vec{x, y, z});
}

void AddCaller(int order, Caller caller) {
    CoopSvc::AddCaller(order, std::move(caller));
}

std::filesystem::path FolderPath(Folder folder) {
    // As coop_epona.cpp read them: the emulator's load and config folders.
    return Common::FS::GetEdenPath(folder == Folder::mods ? Common::FS::EdenPath::LoadDir
                                                          : Common::FS::EdenPath::ConfigDir);
}

} // namespace CoopPlatform
