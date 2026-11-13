// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: where the hook's own time goes (oot-remake docs/RENDIMIENTO.md). With
// LICHII_ORM_PERF=1, one log line every 5 s:
//
//   coop: perf: 5.0 s, 300 presents (16.61 ms each), 300 calc frames, 5 puppets;
//   calc 0.212 ms/frame (puppet frame 0.020, pose 0.120, heads 0.031, belt 0.004,
//   calls 0.018; callers 10: 0.004, 60: 0.011); present 0.305 ms/frame (objects
//   0.110, input 0.001, read 0.020, flags 0.031, session 0.040, publish 0.012,
//   share 0.060, features 0.020, place 0.011); guest 412 reads 96 writes
//   181 KB per calc frame
//
// "calc" is what runs inside the game's calc thread (the svc cave: the game
// waits for it), "present" what runs on the emulator's compositor thread each
// presented frame. What the game itself spends on the puppets (five more
// Links to animate and draw) is NOT in these numbers: it is the frame time
// that is left. Off (one relaxed load per scope) when the variable is unset.
//
// Portable: tests/lichii/perf_test.cpp. Any thread may add; the present
// thread closes the windows.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace CoopPerf {

enum class Part : std::uint8_t {
    // The calc thread (svc 0xf4), inside the game's frame.
    calc,           // the whole svc, every call of the frame
    puppet_frame,   // Director::NewFrame (the Link puppets' director)
    puppet_pose,    // the pose copy: our skeleton read, each puppet's written
    puppet_heads,   // masks and lens on the puppets' heads
    puppet_belt,    // the Ocarina on the belt or in the hand(the tunic tint is upstream's since test10)
    puppet_calls,   // Director::Next for every slot (moves, animations, gear)
    // The present thread, once per presented frame.
    present,        // the whole CoopHook::OnFrame
    present_objects,  // CoopObjects::OnFrame (the actor walk)
    present_input,  // CoopTestInput::OnFrame
    present_read,   // our Link read, the event name
    present_flags,  // the flag stores and the deferral before the session
    present_session,  // Session::frame and what it reports
    present_publish,  // the puppets', fairies' and horse's plans
    present_share,  // CoopObjects::Share, doors, chests, spawns, drops
    present_features,  // time, bosses, songs, teleport, story, age, final...
    present_place,  // the markers and name tags
    kCount,
};
inline constexpr std::size_t kParts = static_cast<std::size_t>(Part::kCount);
const char* Name(Part part);

inline constexpr std::uint64_t kWindowUs = 5'000'000;
inline constexpr std::size_t kMaxCallers = 24;

class Meter {
public:
    void Enable(bool on) { on_.store(on, std::memory_order_relaxed); }
    bool On() const { return on_.load(std::memory_order_relaxed); }

    // Any thread; nothing when off.
    void Add(Part part, std::uint64_t ns);
    // The calc thread: one svc caller after the puppets, by its order
    // (CoopSvc::AddCaller). More than kMaxCallers orders: the rest are dropped.
    void AddCaller(int order, std::uint64_t ns);
    // The calc thread: one guest read or write of `bytes`.
    void Guest(std::size_t bytes, bool write);
    // The calc thread, once per game frame (the svc's first call of a frame).
    void CalcFrame();

    // The present thread, once per presented frame at `now_us` (steady, never
    // going back): the line of a window that closed with this frame. The
    // first frame after Enable only starts the window.
    std::optional<std::string> Present(std::uint64_t now_us, std::uint32_t puppets);

private:
    struct Caller {
        std::atomic<int> order{-1};
        std::atomic<std::uint64_t> ns{0};
    };
    std::atomic<bool> on_{false};
    std::array<std::atomic<std::uint64_t>, kParts> ns_{};
    std::array<Caller, kMaxCallers> callers_{};
    std::atomic<std::uint64_t> calc_frames_{0};
    std::atomic<std::uint64_t> reads_{0}, writes_{0}, bytes_{0};
    // Present thread only.
    std::uint64_t window_start_us_ = 0;
    std::uint64_t presents_ = 0;
    bool started_ = false;
};

// The one meter of the process.
Meter& The();

inline std::uint64_t NowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

// The time from here to the end of the block, added to `part`.
class Scope {
public:
    explicit Scope(Part part) : part_(part), start_(The().On() ? NowNs() : 0) {}
    ~Scope() {
        if (start_ != 0) {
            The().Add(part_, NowNs() - start_);
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Part part_;
    std::uint64_t start_;
};

// For a long function: Mark(part) adds the time since the Lap was made or
// last marked to `part`.
class Lap {
public:
    Lap() : last_(The().On() ? NowNs() : 0) {}
    void Mark(Part part) {
        if (last_ == 0) {
            return;
        }
        const std::uint64_t now = NowNs();
        The().Add(part, now - last_);
        last_ = now;
    }

private:
    std::uint64_t last_;
};

} // namespace CoopPerf
