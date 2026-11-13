// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM frame times (oot-remake docs/PLATAFORMA_HOOKSHOT.md, "Prueba"): with
// LICHII_ORM_FRAME_LOG=<file>, one CSV line per frame the game presents,
// "frame,seconds,ms" (the frame from 1, seconds since the first one, the time
// since the frame before), the same columns as Hookshot's RX_VK_FRAME_LOG, so
// oot-remake/tools/plataforma/comparar.py reads both the same way; and every
// 10 s one log line "coop: frames: ..." with the window's median, worst and
// frames of 50 ms or more. Off (nothing at all) when the variable is unset.
//
// The emulator calls Tick first thing in CoopHook::OnFrame, so it measures
// the guest's presents (what the player sees, high refresh included), not
// the host's display. The Recorder is portable (tests/lichii/frame_log_test.cpp).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CoopFrameLog {

// A frame this long or longer is a spike (Hookshot's RX_VK_SLOW_LOG=50 logs
// the same ones on its side).
inline constexpr std::uint64_t kSpikeUs = 50'000;
inline constexpr std::uint64_t kWindowUs = 10'000'000;

struct Window {
    std::uint64_t frames = 0;
    double median_ms = 0.0;
    double worst_ms = 0.0;
    std::uint64_t spikes = 0;  // frames of kSpikeUs or more
    double seconds = 0.0;      // the window's length
};

class Recorder {
public:
    explicit Recorder(std::uint64_t window_us = kWindowUs) : window_us_(window_us) {}

    struct Out {
        std::string line;              // "frame,seconds,ms", no newline
        std::optional<Window> window;  // a window closed with this frame
    };
    // A frame presented at `now_us` (a steady clock, never going back).
    Out Frame(std::uint64_t now_us);

    std::uint64_t frames() const {
        return frame_;
    }

private:
    std::uint64_t window_us_;
    std::uint64_t frame_ = 0;
    std::uint64_t first_us_ = 0;
    std::uint64_t last_us_ = 0;
    std::uint64_t window_start_us_ = 0;
    std::vector<std::uint64_t> intervals_;  // this window's, microseconds
};

// The CSV's first line.
inline constexpr const char* kHeader = "frame,seconds,ms";

// Present thread, once per presented frame. Emulator build only (it opens the
// file and logs); nothing unless LICHII_ORM_FRAME_LOG is set.
void Tick();

} // namespace CoopFrameLog
