// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: the shared clock's game side (coop_clock.h; rules in coop/host/clock.hpp; oot-remake
// docs/HORA.md). The clock's place in memory is upstream's (docs/time.md, coop_time.cpp; build
// e0f3ccfaf50065ae):
//   WorldMgr = [main + 0x4e7eab0], T = [WorldMgr + 0x510]
//   the current record is T + 0x20 (T + 0x50 while bit 9 of the u32 at T + 8 is set: then a
//   time change runs and we leave the clock alone); seconds since midnight are its u32 at +8.
// Test switch: LICHII_ORM_CLOCK_TEST_PUT_BACK=1 undoes each checked nudge (the game putting its
// counter back), so the "nudges do not hold" fallback runs in a scenario (hora-sin-empujon).
// A nudge adds a few seconds to that u32 on the calc thread. Whether the game keeps it (or
// counts from a value of its own and puts it back) is checked on the next frame; nudges that
// do not hold turn nudging off (coop/host/clock.hpp).

#include "coop_clock.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <variant>

#include "coop/host/clock.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop_every.h"
#include "coop_puppet.h"
#include "coop_switch.h"
#include "platform.h"

namespace CoopClock {
namespace {

namespace proto = coop::protocol;
using coop::host::clock_text;

// The same places as coop_time.cpp (kept there for upstream's code).
constexpr std::uint64_t kWorldMgrPtr = 0x4e7eab0;
constexpr std::uint64_t kTimeObject = 0x510;
constexpr std::uint64_t kTimeFlags = 0x8;
constexpr std::uint32_t kTimeOverride = 0x200;
constexpr std::uint64_t kTimeNow = 0x20;
constexpr std::uint64_t kRecordSeconds = 0x8;
constexpr int kCallerOrder = 41;             // right after CoopTime's setter (40)
constexpr std::int32_t kCheckSeconds = 10;   // a nudge this big is checked on the next frame
constexpr std::uint64_t kDebugEveryUs = 2'000'000;

bool Flag(const char* name) {
    return CoopSwitch::On(name, false);  // coop_switch.h
}

// "<value>[@<delay s>]": the value and the delay in microseconds (default 5 s).
// `name` (LICHII_ORM_*) when set, else `old_name` (the name before test9).
bool ParseTest(const char* name, const char* old_name, long& value, std::uint64_t& delay_us) {
    const auto t = CoopSwitch::Value(name, old_name).value_or(std::string());
    if (t.empty()) {
        return false;
    }
    char* rest = nullptr;
    value = std::strtol(t.c_str(), &rest, 10);
    if (rest == t.c_str()) {
        return false;
    }
    delay_us = 5'000'000;
    if (rest != nullptr && *rest == '@') {
        delay_us = std::strtoull(rest + 1, nullptr, 10) * 1'000'000;
    }
    return true;
}

// The calc thread side: adds the asked nudge to the game's seconds and checks the last one.
class Nudger {
public:
    struct Taken {
        std::int32_t applied = 0;  // seconds added since the last Take
        int held = 0;
        int lost = 0;
    };

    void Want(std::uint64_t main, std::int32_t delta) {
        std::lock_guard lock(mutex_);
        main_ = main;
        want_ = delta;  // the newest replaces one not written yet: nudges never pile up
    }

    Taken Take() {
        std::lock_guard lock(mutex_);
        const Taken t = taken_;
        taken_ = {};
        return t;
    }

    void PutBack(bool on) {
        std::lock_guard lock(mutex_);
        put_back_ = on;
    }

    void Clear() {
        std::lock_guard lock(mutex_);
        want_ = 0;
        check_.reset();
        taken_ = {};
    }

    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call) {
        if (after_call) {
            return std::nullopt;
        }
        std::lock_guard lock(mutex_);
        if (main_ == 0) {
            return std::nullopt;
        }
        std::uint64_t world = 0;
        std::uint64_t time = 0;
        std::uint32_t flags = 0;
        std::uint32_t seconds = 0;
        if (!guest.Read(main_ + kWorldMgrPtr, &world, 8) || world == 0 ||
            !guest.Read(world + kTimeObject, &time, 8) || time == 0 ||
            !guest.Read(time + kTimeFlags, &flags, 4)) {
            check_.reset();
            want_ = 0;
            return std::nullopt;
        }
        const std::uint64_t record = time + kTimeNow + kRecordSeconds;
        if (!guest.Read(record, &seconds, 4) || seconds >= proto::kSecondsPerDay) {
            check_.reset();
            want_ = 0;
            return std::nullopt;
        }
        // Did the last nudge hold? One frame on, the clock has run a few seconds at most.
        if (check_) {
            if (check_->record == record && put_back_) {
                // Test only (LICHII_ORM_CLOCK_TEST_PUT_BACK=1): act as a game that counts from
                // a value of its own and puts its counter back, so the fallback runs for real.
                const std::int64_t back = static_cast<std::int64_t>(seconds) - check_->delta;
                if (back >= 0 && back < static_cast<std::int64_t>(proto::kSecondsPerDay)) {
                    const auto value = static_cast<std::uint32_t>(back);
                    if (guest.Write(record, &value, 4)) {
                        seconds = value;
                    }
                }
            }
            if (check_->record == record) {
                const std::int32_t d = coop::host::clock_ahead(check_->before, seconds);
                const bool held = check_->delta > 0 ? d >= check_->delta : d < 0;
                ++(held ? taken_.held : taken_.lost);
            }
            check_.reset();
        }
        const std::int32_t delta = want_;
        want_ = 0;
        if (delta == 0 || (flags & kTimeOverride) != 0) {
            return std::nullopt;  // nothing asked, or a time change runs
        }
        const std::int64_t next = static_cast<std::int64_t>(seconds) + delta;
        if (next < 0 || next >= static_cast<std::int64_t>(proto::kSecondsPerDay)) {
            return std::nullopt;  // never across midnight
        }
        const auto value = static_cast<std::uint32_t>(next);
        if (!guest.Write(record, &value, 4)) {
            return std::nullopt;
        }
        taken_.applied += delta;
        if (delta >= kCheckSeconds || delta <= -kCheckSeconds) {
            check_ = Check{record, seconds, delta};
        }
        return std::nullopt;
    }

private:
    struct Check {
        std::uint64_t record = 0;
        std::uint32_t before = 0;  // the seconds before our write
        std::int32_t delta = 0;
    };
    std::mutex mutex_;
    std::uint64_t main_ = 0;
    std::int32_t want_ = 0;
    std::optional<Check> check_;
    Taken taken_;
    bool put_back_ = false;  // test: undo each checked nudge as a game would
};

coop::host::ClockSettings Settings() {
    coop::host::ClockSettings s;
    s.nudge = CoopSwitch::On("LICHII_ORM_CLOCK_NUDGE", "LICHII_COOP_CLOCK_NUDGE", true);
    return s;
}

struct Glue {
    bool on = CoopSwitch::On("LICHII_ORM_CLOCK", "LICHII_COOP_CLOCK", true);
    bool debug = Flag("LICHII_COOP_DEBUG_TIME");
    coop::host::Clock clock{Settings()};
    Nudger nudger;
    bool connected = false;
    std::uint64_t joined_at = 0;
    std::uint64_t debug_at = 0;
    std::optional<std::int32_t> test_off;
    std::uint64_t test_off_delay_us = 0;
    std::optional<std::uint32_t> test_jump;
    std::uint64_t test_jump_delay_us = 0;

    Glue() {
        long v = 0;
        std::uint64_t delay = 0;
        if (ParseTest("LICHII_ORM_CLOCK_TEST_OFF", "LICHII_COOP_CLOCK_TEST_OFF", v, delay) && v != 0 && v > -43'200 &&
            v < 43'200) {
            test_off = static_cast<std::int32_t>(v);
            test_off_delay_us = delay;
        }
        if (ParseTest("LICHII_ORM_CLOCK_TEST_JUMP", "LICHII_COOP_CLOCK_TEST_JUMP", v, delay) && v >= 0 &&
            v < static_cast<long>(proto::kSecondsPerDay)) {
            test_jump = static_cast<std::uint32_t>(v);
            test_jump_delay_us = delay;
        }
        if (CoopSwitch::On("LICHII_ORM_CLOCK_TEST_PUT_BACK", false)) {
            nudger.PutBack(true);
            ORM_LOG_INFO(
                     "coop: clock: test: each checked nudge is put back, as a game that keeps its own count");
        }
        if (on) {
            CoopPlatform::AddCaller(kCallerOrder, [this](CoopPuppet::Guest& guest, bool after_call,
                                                         std::uint64_t, std::uint64_t) {
                return nudger.Step(guest, after_call);
            });
        }
        ORM_LOG_INFO("coop: clock {}",
                     !on ? "off (LICHII_ORM_CLOCK): upstream's time rules"
                     : Settings().nudge
                         ? "on: the host owns the shared clock, the others are nudged to it"
                         : "on, no nudges (LICHII_ORM_CLOCK_NUDGE): sets as upstream");
    }
};

Glue& TheGlue() {
    static Glue glue;  // kept for the whole run: the cave holds its nudger
    return glue;
}

struct Ids {
    bool host = false;
    std::uint8_t host_number = 0;
    std::uint8_t number = 0;
};

} // namespace

bool On() {
    return TheGlue().on;
}

std::optional<std::uint32_t> Tick(std::uint64_t main, std::optional<std::uint32_t> now,
                                  coop::host::Session& session,
                                  std::vector<coop::protocol::Message>& send) {
    Glue& g = TheGlue();
    const std::uint64_t t = CoopEvery::NowUs();
    const Ids ids = session.with_client([](coop::net::Client& c) {
        return Ids{c.is_host(), c.host_number(), c.player_number()};
    });
    if (!g.connected) {
        g.connected = true;
        g.joined_at = t;
    }
    const auto taken = g.nudger.Take();
    coop::host::ClockInput in;
    in.now_us = t;
    in.game = main != 0 ? now : std::nullopt;
    in.host = ids.host;
    in.host_number = ids.host_number;
    in.our_number = ids.number;
    in.nudged = taken.applied;
    in.nudges_held = taken.held;
    in.nudges_lost = taken.lost;
    const auto out = g.clock.tick(in);
    for (const auto& line : out.log) {
        ORM_LOG_INFO("coop: clock: {}", line);
    }
    for (const auto& m : out.send) {
        send.emplace_back(m);
    }
    if (out.nudge != 0 && main != 0) {
        g.nudger.Want(main, out.nudge);
    }
    if (g.debug && t - g.debug_at >= kDebugEveryUs) {
        g.debug_at = t;
        const auto st = g.clock.status();
        ORM_LOG_INFO(
                     "coop: clock: ours {} shared {} off {} s, anchor {}, {} at {:.0f} s/s, {}, "
                     "nudged {} s ({} held, {} put back)",
                     in.game ? clock_text(*in.game) : std::string("-"),
                     st.shared ? clock_text(*st.shared) : std::string("-"), st.off.value_or(0),
                     st.anchor, st.running ? "runs" : "stands", st.rate,
                     st.in_step ? "in step" : "catching up", taken.applied, taken.held, taken.lost);
    }
    if (out.set) {
        return out.set;
    }
    // Test switches (the clock decides first: a set it asked for goes before them).
    if (g.test_off && now && main != 0 && t - g.joined_at >= g.test_off_delay_us) {
        const std::int64_t to = (static_cast<std::int64_t>(*now) + *g.test_off +
                                 proto::kSecondsPerDay) %
                                proto::kSecondsPerDay;
        const auto target = static_cast<std::uint32_t>(to);
        ORM_LOG_INFO("coop: clock: test: our clock quietly to {} ({:+} s)", clock_text(target),
                     *g.test_off);
        g.clock.our_set(target, t);
        g.test_off.reset();
        return target;
    }
    if (g.test_jump && now && main != 0 && t - g.joined_at >= g.test_jump_delay_us) {
        ORM_LOG_INFO("coop: clock: test: our clock jumps to {}, as the Sun's Song would",
                     clock_text(*g.test_jump));
        const auto target = *g.test_jump;
        g.test_jump.reset();
        return target;  // not told to the clock: it sees the jump and sends it
    }
    return std::nullopt;
}

void Reset() {
    Glue& g = TheGlue();
    g.clock.reset();
    g.nudger.Clear();
    g.connected = false;
}

void Receive(const coop::protocol::Message& message) {
    if (const auto* t = std::get_if<coop::protocol::TimeOfDay>(&message)) {
        TheGlue().clock.receive(*t, CoopEvery::NowUs());
    }
}

} // namespace CoopClock
