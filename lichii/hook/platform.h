// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM platform layer (oot-remake docs/PLATAFORMA_HOOKSHOT.md): everything our
// hook code needs from what runs the game, in one small interface, so moving
// from the emulator (Fritchii + daurosk's patches) to Hookshot (the native
// recomp) is writing one adapter, not touching every feature.
//
// Two halves:
// - per frame, a Platform object the hook's frame hands to each feature's
//   Frame (guest memory, input, the game requests daurosk's glue makes);
// - process-wide, free functions whose definition the adapter's .cpp gives
//   at link time (calls into the game, the environment and the log, which are
//   used before any frame, from static initialisers and other threads).
//
// What the platform calls in our code (the other direction) is listed in
// docs/PLATAFORMA_HOOKSHOT.md, "Lo que la plataforma llama": the frame
// (CoopHook::OnFrame), the start and the end, the game-thread pump of the
// callers below, the camera matrix and the HUD's drawing.
//
// The emulator's adapter is platform_emulator.h (the default, the only one
// built today). No emulator header here: this file builds in the unit tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "coop/host/game_flags.hpp"  // coop::host::WritableGameMemory
#include "coop_puppet.h"             // CoopPuppet::Guest, CoopPuppet::Call
#include "platform_env.h"            // Env, ORM_LOG_*, ORM_PLATFORM_LIVE

namespace CoopPlatform {

// ---- Guest memory ----

// Reads, writes and the 32-bit compare-and-swap the flag queues need, on
// guest addresses (the emulator maps main at 0x7100000000 or so, Hookshot at
// 0x80020000: code adds offsets to main_base() and follows the game's
// pointers, never an absolute address). Unmapped bytes fail quietly, never
// fault.
using GuestMemory = coop::host::WritableGameMemory;

// The byte view the cave callers and the camera walk take (CoopPuppet::Guest)
// on any GuestMemory, with its semantics: size 0 is true, a range that wraps
// or is not mapped is false.
class GuestView final : public CoopPuppet::Guest {
public:
    explicit GuestView(GuestMemory& memory) : memory_(memory) {}
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        if (size == 0) {
            return true;
        }
        return out != nullptr &&
               memory_.read(address, std::span<std::uint8_t>(static_cast<std::uint8_t*>(out), size));
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        if (size == 0) {
            return true;
        }
        return in != nullptr &&
               memory_.write(address, std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(in), size));
    }

private:
    GuestMemory& memory_;
};

// ---- Input ----

// Pad buttons as the Switch's Npad bits (what the game reads): A bit 0, B 1,
// X 2, Y 3, ..., D-pad left 12, up 13, right 14, down 15.
inline constexpr std::uint64_t kButtonA = 1ull << 0;
// A stick axis at its edge (HID_JOYSTICK_MAX).
inline constexpr std::int32_t kStickMax = 0x7fff;
struct Stick {
    std::int32_t x = 0;  // -kStickMax (left) to kStickMax (right)
    std::int32_t y = 0;  // -kStickMax (down) to kStickMax (up)
};

// ---- The per-frame interface ----

class Platform {
public:
    virtual ~Platform() = default;

    // Where Ocarina's main module is in guest memory; 0 while no game runs
    // (or it is not Ocarina 1.1.1, build e0f3ccfa). Memory().main_base() is
    // the same value.
    virtual std::uint64_t MainBase() const = 0;
    virtual GuestMemory& Memory() = 0;
    // One aligned 64-bit store as the guest's CPU makes it (the emulator's
    // Write64, which also tells the GPU's cache; Hookshot marks the page
    // written for the renderer's mirror). False when the 8 bytes are not
    // mapped; nothing is written then.
    virtual bool Write64(std::uint64_t address, std::uint64_t value) = 0;

    // Player 1's pad, or the handheld one, as the game reads it this frame,
    // scripted test input included: the held buttons (Npad bits). Nothing
    // when no pad is connected.
    virtual std::optional<std::uint64_t> PadButtons() = 0;
    // While set, the right stick of that pad reads `right` (the spectator
    // camera's wake nudge); a test script's own right stick wins. Nothing:
    // let go.
    virtual void NudgeRightStick(std::optional<Stick> right) = 0;

    // Game requests whose code is daurosk's glue today (coop_boss_game.cpp,
    // memory and the cave only, so they port with it):
    // - the game's scene change, as the pull into a boss room asks it; an
    //   empty `start` is the scene's own start, an empty `scene` clears an
    //   earlier request;
    virtual bool AskSceneChange(std::string_view scene, std::string_view start) = 0;
    // - our Link put at (x, y, z) in this scene (y up), the boss pull's move.
    virtual bool MoveLink(float x, float y, float z) = 0;
};

// ---- Calls into the game (process-wide) ----

// Something that calls game functions on the game's own thread, once a frame,
// with CoopPuppet::Director::Step's contract: asked with after_call false;
// each call it returns is made and it is asked again with after_call true and
// the call's x0 in `result`; nothing means done for this frame. `now_ms` is a
// steady clock. Callers run by `order` (the puppets first, then CoopSpawn 10,
// the boss rooms 50, the story 55, ...). Keep each step short: the game's
// frame waits for it. The emulator runs them from the code cave in main
// (svc 0xf4, coop_svc.h); Hookshot from an ENTRY_HOOK on the same frame
// function, calling a64_lookup(function).
using Caller = std::function<std::optional<CoopPuppet::Call>(CoopPuppet::Guest& guest, bool after_call,
                                                             std::uint64_t result, std::uint64_t now_ms)>;
// Any thread, best before the game starts.
void AddCaller(int order, Caller caller);

// ---- Folders (process-wide) ----

// The folders of what runs the game that our code keeps files in:
// - mods: the root of the game mod folders, one per title id (the emulator's
//   load folder: the puppet pack is a mod under 0400486024DEC000 there);
// - config: its configuration folder (coop.ini, coop_ride.txt).
enum class Folder { mods, config };
// Any thread.
std::filesystem::path FolderPath(Folder folder);

// A call of the game function at main+`offset` with no arguments yet.
inline CoopPuppet::Call MainCall(std::uint64_t main_base, std::uint64_t offset) {
    CoopPuppet::Call call;
    call.function = main_base + offset;
    return call;
}

} // namespace CoopPlatform
