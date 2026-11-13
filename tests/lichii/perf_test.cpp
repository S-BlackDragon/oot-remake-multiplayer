// Tests for the hook's own timers (lichii/hook/coop_perf.h, LICHII_ORM_PERF).
#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "coop_perf.h"

using CoopPerf::Meter;
using CoopPerf::Part;

namespace {

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("perf: off adds nothing and says nothing") {
    Meter m;
    m.Add(Part::calc, 1'000'000);
    m.AddCaller(10, 1'000'000);
    m.Guest(64, false);
    m.CalcFrame();
    CHECK_FALSE(m.Present(1'000, 0).has_value());
    CHECK_FALSE(m.Present(1'000 + 2 * CoopPerf::kWindowUs, 0).has_value());
    // Turned on later: what was added while off is not in the first window.
    m.Enable(true);
    CHECK_FALSE(m.Present(20'000'000, 0).has_value());
    const auto line = m.Present(20'000'000 + CoopPerf::kWindowUs, 0);
    REQUIRE(line.has_value());
    CHECK(Has(*line, "calc 0.000 ms/frame"));
    CHECK(Has(*line, "guest 0 reads 0 writes"));
}

TEST_CASE("perf: a window's line is per frame, calc parts by calc frames and present parts by presents") {
    Meter m;
    m.Enable(true);
    std::uint64_t now = 1'000'000;
    CHECK_FALSE(m.Present(now, 5).has_value());  // the first frame starts the window
    // 100 presents in 5 s; 50 calc frames (the game at half the present rate).
    std::optional<std::string> line;
    for (int i = 0; i < 100; ++i) {
        m.Add(Part::present, 300'000);          // 0.3 ms a present
        m.Add(Part::present_objects, 100'000);  // 0.1 ms
        if (i % 2 == 0) {
            m.CalcFrame();
            m.Add(Part::calc, 400'000);         // 0.4 ms a calc frame
            m.Add(Part::puppet_pose, 250'000);
            m.AddCaller(60, 50'000);
            m.AddCaller(10, 20'000);
            m.Guest(1024, false);
            m.Guest(1024, false);
            m.Guest(2048, true);
        }
        now += 50'000;
        line = m.Present(now, 5);
        if (i < 99) {
            CHECK_FALSE(line.has_value());
        }
    }
    REQUIRE(line.has_value());
    CHECK(Has(*line, "5.0 s, 100 presents (50.00 ms each), 50 calc frames, 5 puppets"));
    CHECK(Has(*line, "calc 0.400 ms/frame"));
    CHECK(Has(*line, "pose 0.250"));
    CHECK(Has(*line, "callers 60: 0.050, 10: 0.020"));
    CHECK(Has(*line, "present 0.300 ms/frame (objects 0.100"));
    CHECK(Has(*line, "guest 2 reads 1 writes 4.0 KB per calc frame"));

    // The next window starts from nothing.
    for (int i = 0; i < 100; ++i) {
        now += 50'000;
        line = m.Present(now, 0);
    }
    REQUIRE(line.has_value());
    CHECK(Has(*line, "0 calc frames, 0 puppets; calc 0.000 ms/frame"));
    CHECK(Has(*line, "callers 60: 0.000, 10: 0.000"));
    CHECK(Has(*line, "present 0.000 ms/frame"));
}

TEST_CASE("perf: a scope and a lap add their time to their part, and nothing when off") {
    Meter& m = CoopPerf::The();
    m.Enable(true);
    std::uint64_t now = 1'000'000;
    m.Present(now, 0);
    {
        const CoopPerf::Scope scope(Part::present);
        CoopPerf::Lap lap;
        volatile unsigned spin = 0;
        for (unsigned i = 0; i < 200'000; ++i) {
            spin = spin + i;
        }
        lap.Mark(Part::present_read);
    }
    now += CoopPerf::kWindowUs;
    const auto line = m.Present(now, 0);
    REQUIRE(line.has_value());
    CHECK_FALSE(Has(*line, "present 0.000 ms/frame"));
    CHECK_FALSE(Has(*line, "read 0.000"));
    CHECK(Has(*line, "flags 0.000"));

    m.Enable(false);
    {
        const CoopPerf::Scope scope(Part::present);
        CoopPerf::Lap lap;
        lap.Mark(Part::present_read);
    }
    m.Enable(true);
    m.Present(now, 0);
    now += CoopPerf::kWindowUs;
    const auto after = m.Present(now, 0);
    REQUIRE(after.has_value());
    CHECK(Has(*after, "present 0.000 ms/frame"));
    m.Enable(false);
}

TEST_CASE("perf: more svc callers than places are dropped, not mixed") {
    Meter m;
    m.Enable(true);
    m.Present(1'000, 0);
    for (int order = 0; order < static_cast<int>(CoopPerf::kMaxCallers) + 4; ++order) {
        m.AddCaller(order, 1'000'000);
    }
    m.AddCaller(-1, 1'000'000);
    m.CalcFrame();
    const auto line = m.Present(1'000 + CoopPerf::kWindowUs, 0);
    REQUIRE(line.has_value());
    CHECK(Has(*line, "callers 0: 1.000, 1: 1.000"));
    CHECK(Has(*line, std::to_string(CoopPerf::kMaxCallers - 1) + ": 1.000"));
    CHECK_FALSE(Has(*line, std::to_string(CoopPerf::kMaxCallers) + ": 1.000"));
}
