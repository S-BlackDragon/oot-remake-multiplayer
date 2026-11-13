// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM platform layer (platform.h), the emulator's adapter: the default, and
// the only one built today. Each call is what our hook files did themselves
// before the layer, moved here unchanged (oot-remake docs/PLATAFORMA_HOOKSHOT.md
// has the list). Emulator build only (core.cmake).
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "coop_flags.h"  // CoopHook::EmulatorMemory
#include "platform.h"

namespace Core {
class System;
}

namespace CoopPlatform {

// One frame's platform, made by the hook's frame (coop_hook.cpp) on the
// present thread and handed to the features' Frame. Holds references only:
// does not outlive the frame.
class Emulator final : public Platform {
public:
    // `main_base`: 0 while no game runs (as the hook's frame passes it to the
    // features).
    Emulator(Core::System& system, std::uint64_t main_base);

    std::uint64_t MainBase() const override {
        return main_base_;
    }
    GuestMemory& Memory() override {
        return memory_;
    }
    bool Write64(std::uint64_t address, std::uint64_t value) override;
    std::optional<std::uint64_t> PadButtons() override;
    void NudgeRightStick(std::optional<Stick> right) override;
    bool AskSceneChange(std::string_view scene, std::string_view start) override;
    bool MoveLink(float x, float y, float z) override;

private:
    Core::System& system_;
    std::uint64_t main_base_;
    CoopHook::EmulatorMemory memory_;
};

} // namespace CoopPlatform
