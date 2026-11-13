// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM frame times (coop_frame_log.h).

#include "coop_frame_log.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include "platform_env.h"

namespace CoopFrameLog {

Recorder::Out Recorder::Frame(std::uint64_t now_us) {
    Out out;
    std::uint64_t interval = 0;
    if (frame_ == 0) {
        first_us_ = now_us;
        window_start_us_ = now_us;
    } else {
        interval = now_us >= last_us_ ? now_us - last_us_ : 0;
        intervals_.push_back(interval);
    }
    last_us_ = now_us;
    ++frame_;
    char buf[96];
    const double seconds = static_cast<double>(now_us - first_us_) / 1e6;
    std::snprintf(buf, sizeof buf, "%llu,%.6f,%.3f", static_cast<unsigned long long>(frame_), seconds,
                  static_cast<double>(interval) / 1e3);
    out.line = buf;
    if (now_us - window_start_us_ >= window_us_ && !intervals_.empty()) {
        Window w;
        w.frames = intervals_.size();
        w.seconds = static_cast<double>(now_us - window_start_us_) / 1e6;
        w.worst_ms = static_cast<double>(*std::max_element(intervals_.begin(), intervals_.end())) / 1e3;
        w.spikes = static_cast<std::uint64_t>(
            std::count_if(intervals_.begin(), intervals_.end(), [](std::uint64_t i) { return i >= kSpikeUs; }));
        const auto mid = intervals_.begin() + static_cast<std::ptrdiff_t>(intervals_.size() / 2);
        std::nth_element(intervals_.begin(), mid, intervals_.end());
        w.median_ms = static_cast<double>(*mid) / 1e3;
        out.window = w;
        intervals_.clear();
        window_start_us_ = now_us;
    }
    return out;
}

} // namespace CoopFrameLog

#if ORM_PLATFORM_LIVE

#include <chrono>
#include <string>

namespace CoopFrameLog {
namespace {

struct State {
    bool looked = false;
    std::FILE* file = nullptr;  // null: off
    Recorder recorder;
};

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

void Tick() {
    static State s;  // present thread only
    if (!s.looked) {
        s.looked = true;
        const auto path = CoopPlatform::Env("LICHII_ORM_FRAME_LOG");
        if (!path || path->empty()) {
            return;
        }
        s.file = std::fopen(path->c_str(), "w");
        if (s.file == nullptr) {
            ORM_LOG_ERROR("coop: frames: LICHII_ORM_FRAME_LOG {} cannot be written: no frame log", *path);
            return;
        }
        std::fprintf(s.file, "%s\n", kHeader);
        ORM_LOG_INFO("coop: frames: one line per presented frame to {} (LICHII_ORM_FRAME_LOG); a summary every "
                     "{} s, spikes from {} ms",
                     *path, kWindowUs / 1'000'000, kSpikeUs / 1'000);
    }
    if (s.file == nullptr) {
        return;
    }
    const auto out = s.recorder.Frame(NowUs());
    std::fputs(out.line.c_str(), s.file);
    std::fputc('\n', s.file);
    if (s.recorder.frames() % 60 == 0) {
        std::fflush(s.file);  // a crash loses one second at most
    }
    if (out.window) {
        const Window& w = *out.window;
        ORM_LOG_INFO("coop: frames: {} in {:.1f} s ({:.1f} fps), median {:.1f} ms, worst {:.1f} ms, {} of {} ms or more",
                     w.frames, w.seconds, w.seconds > 0 ? static_cast<double>(w.frames) / w.seconds : 0.0, w.median_ms,
                     w.worst_ms, w.spikes, kSpikeUs / 1'000);
    }
}

} // namespace CoopFrameLog

#endif  // ORM_PLATFORM_LIVE
