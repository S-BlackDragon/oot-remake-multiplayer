// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: where the hook's own time goes (coop_perf.h).

#include "coop_perf.h"

#include <cstdio>

namespace CoopPerf {

const char* Name(Part part) {
    switch (part) {
    case Part::calc:
        return "calc";
    case Part::puppet_frame:
        return "puppet frame";
    case Part::puppet_pose:
        return "pose";
    case Part::puppet_heads:
        return "heads";
    case Part::puppet_belt:
        return "belt";
    case Part::puppet_calls:
        return "calls";
    case Part::present:
        return "present";
    case Part::present_objects:
        return "objects";
    case Part::present_input:
        return "input";
    case Part::present_read:
        return "read";
    case Part::present_flags:
        return "flags";
    case Part::present_session:
        return "session";
    case Part::present_publish:
        return "publish";
    case Part::present_share:
        return "share";
    case Part::present_features:
        return "features";
    case Part::present_place:
        return "place";
    case Part::kCount:
        break;
    }
    return "?";
}

void Meter::Add(Part part, std::uint64_t ns) {
    const auto i = static_cast<std::size_t>(part);
    if (!On() || i >= kParts) {
        return;
    }
    ns_[i].fetch_add(ns, std::memory_order_relaxed);
}

void Meter::AddCaller(int order, std::uint64_t ns) {
    if (!On() || order < 0) {
        return;
    }
    for (Caller& c : callers_) {
        int have = c.order.load(std::memory_order_relaxed);
        if (have == -1) {
            // Only the calc thread takes a free place, so no other writer races.
            c.order.store(order, std::memory_order_relaxed);
            have = order;
        }
        if (have == order) {
            c.ns.fetch_add(ns, std::memory_order_relaxed);
            return;
        }
    }
}

void Meter::Guest(std::size_t bytes, bool write) {
    if (!On()) {
        return;
    }
    (write ? writes_ : reads_).fetch_add(1, std::memory_order_relaxed);
    bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void Meter::CalcFrame() {
    if (On()) {
        calc_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::optional<std::string> Meter::Present(std::uint64_t now_us, std::uint32_t puppets) {
    if (!On()) {
        started_ = false;
        return std::nullopt;
    }
    if (!started_) {
        // The window starts clean: what was added before it is dropped.
        started_ = true;
        window_start_us_ = now_us;
        presents_ = 0;
        for (auto& n : ns_) {
            n.store(0, std::memory_order_relaxed);
        }
        for (Caller& c : callers_) {
            c.ns.store(0, std::memory_order_relaxed);
        }
        calc_frames_.store(0, std::memory_order_relaxed);
        reads_.store(0, std::memory_order_relaxed);
        writes_.store(0, std::memory_order_relaxed);
        bytes_.store(0, std::memory_order_relaxed);
        return std::nullopt;
    }
    ++presents_;
    if (now_us < window_start_us_ || now_us - window_start_us_ < kWindowUs) {
        return std::nullopt;
    }
    const double seconds = static_cast<double>(now_us - window_start_us_) / 1e6;
    const double presents = static_cast<double>(presents_);
    const std::uint64_t calc_frames = calc_frames_.exchange(0, std::memory_order_relaxed);
    // Per calc frame for the calc parts, per present for the present parts;
    // with no calc frame in the window (a load, puppets off) per present.
    const double calc_div = calc_frames != 0 ? static_cast<double>(calc_frames) : presents;
    std::array<double, kParts> ms{};
    for (std::size_t i = 0; i < kParts; ++i) {
        const bool calc_part = i <= static_cast<std::size_t>(Part::puppet_calls);
        ms[i] = static_cast<double>(ns_[i].exchange(0, std::memory_order_relaxed)) / 1e6 /
                (calc_part ? calc_div : presents);
    }
    const auto at = [&ms](Part p) { return ms[static_cast<std::size_t>(p)]; };
    char buf[256];
    std::string line;
    std::snprintf(buf, sizeof buf,
                  "%.1f s, %llu presents (%.2f ms each), %llu calc frames, %u puppets; calc %.3f ms/frame (",
                  seconds, static_cast<unsigned long long>(presents_), seconds * 1e3 / presents,
                  static_cast<unsigned long long>(calc_frames), puppets, at(Part::calc));
    line += buf;
    bool first = true;
    for (const Part p : {Part::puppet_frame, Part::puppet_pose, Part::puppet_heads, Part::puppet_belt,
                         Part::puppet_calls}) {
        std::snprintf(buf, sizeof buf, "%s%s %.3f", first ? "" : ", ", Name(p), at(p));
        line += buf;
        first = false;
    }
    first = true;
    for (Caller& c : callers_) {
        const int order = c.order.load(std::memory_order_relaxed);
        if (order == -1) {
            break;
        }
        const double caller_ms =
            static_cast<double>(c.ns.exchange(0, std::memory_order_relaxed)) / 1e6 / calc_div;
        std::snprintf(buf, sizeof buf, "%s%d: %.3f", first ? "; callers " : ", ", order, caller_ms);
        line += buf;
        first = false;
    }
    std::snprintf(buf, sizeof buf, "); present %.3f ms/frame (", at(Part::present));
    line += buf;
    first = true;
    for (std::size_t i = static_cast<std::size_t>(Part::present_objects); i < kParts; ++i) {
        std::snprintf(buf, sizeof buf, "%s%s %.3f", first ? "" : ", ", Name(static_cast<Part>(i)), ms[i]);
        line += buf;
        first = false;
    }
    const double reads = static_cast<double>(reads_.exchange(0, std::memory_order_relaxed));
    const double writes = static_cast<double>(writes_.exchange(0, std::memory_order_relaxed));
    const double bytes = static_cast<double>(bytes_.exchange(0, std::memory_order_relaxed));
    std::snprintf(buf, sizeof buf, "); guest %.0f reads %.0f writes %.1f KB per calc frame", reads / calc_div,
                  writes / calc_div, bytes / 1024.0 / calc_div);
    line += buf;
    window_start_us_ = now_us;
    presents_ = 0;
    return line;
}

Meter& The() {
    static Meter meter;
    return meter;
}

} // namespace CoopPerf
