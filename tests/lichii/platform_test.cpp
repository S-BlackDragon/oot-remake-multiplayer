// Tests for the ORM platform layer's portable parts (lichii/hook/platform.h,
// platform_env.h) and the frame log's recorder (lichii/hook/coop_frame_log.h).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>

#include "coop_frame_log.h"
#include "coop_switch.h"
#include "platform.h"

namespace {

// A guest memory of one mapped range [base, base + size).
class FakeMemory final : public CoopPlatform::GuestMemory {
public:
    FakeMemory(std::uint64_t base, std::size_t size) : base_(base), bytes_(size, 0) {}
    std::uint64_t main_base() const override {
        return base_;
    }
    bool read(std::uint64_t address, std::span<std::uint8_t> out) override {
        if (!Mapped(address, out.size())) {
            return false;
        }
        std::memcpy(out.data(), bytes_.data() + (address - base_), out.size());
        return true;
    }
    bool write(std::uint64_t address, std::span<const std::uint8_t> data) override {
        if (!Mapped(address, data.size())) {
            return false;
        }
        std::memcpy(bytes_.data() + (address - base_), data.data(), data.size());
        return true;
    }
    std::optional<std::uint32_t> compare_exchange_u32(std::uint64_t address, std::uint32_t expected,
                                                      std::uint32_t desired) override {
        std::uint32_t now = 0;
        std::array<std::uint8_t, 4> b{};
        if (!read(address, b)) {
            return std::nullopt;
        }
        std::memcpy(&now, b.data(), 4);
        if (now == expected) {
            std::memcpy(b.data(), &desired, 4);
            write(address, b);
        }
        return now;
    }

private:
    bool Mapped(std::uint64_t address, std::size_t size) const {
        return address >= base_ && address + size >= address && address + size <= base_ + bytes_.size();
    }
    std::uint64_t base_;
    std::string bytes_;
};

void SetEnv(const char* name, const char* value) {
#ifdef _MSC_VER
    _putenv_s(name, value);  // "" removes it on Windows
#else
    if (value[0] == 0) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

} // namespace

TEST_CASE("platform: GuestView reads and writes through the guest memory, and refuses what is not mapped") {
    FakeMemory memory(0x1000, 0x100);
    CoopPlatform::GuestView guest(memory);
    const std::uint32_t v = 0xdeadbeef;
    CHECK(guest.Write(0x1010, &v, sizeof v));
    std::uint32_t back = 0;
    CHECK(guest.Read(0x1010, &back, sizeof back));
    CHECK(back == v);
    CHECK_FALSE(guest.Read(0x10fe, &back, sizeof back));      // runs past the end
    CHECK_FALSE(guest.Write(0x0ffc, &v, sizeof v));            // starts before
    CHECK_FALSE(guest.Read(~0ull - 1, &back, sizeof back));    // wraps around
    CHECK(guest.Read(0x5000, &back, 0));                       // nothing asked: true, as before
    CHECK_FALSE(guest.Read(0x1010, nullptr, 4));
}

TEST_CASE("platform: MainCall puts the function at main + offset") {
    const auto call = CoopPlatform::MainCall(0x7100000000ull, 0x372a70);
    CHECK(call.function == 0x7100372a70ull);
    CHECK(call.x[0] == 0);
    CHECK(call.s0 == 0.0f);
}

TEST_CASE("platform: Env tells unset from empty, and the switches still read empty as unset") {
    SetEnv("ORM_PLATFORM_TEST_VAR", "");
    CHECK_FALSE(CoopPlatform::Env("ORM_PLATFORM_TEST_VAR").has_value());
    CHECK_FALSE(CoopPlatform::Env(nullptr).has_value());
    SetEnv("ORM_PLATFORM_TEST_VAR", "on");
    REQUIRE(CoopPlatform::Env("ORM_PLATFORM_TEST_VAR").has_value());
    CHECK(*CoopPlatform::Env("ORM_PLATFORM_TEST_VAR") == "on");
    CHECK(CoopSwitch::On("ORM_PLATFORM_TEST_VAR", false));
    SetEnv("ORM_PLATFORM_TEST_VAR", "0");
    CHECK_FALSE(CoopSwitch::On("ORM_PLATFORM_TEST_VAR", true));
    SetEnv("ORM_PLATFORM_TEST_VAR", "");
    CHECK(CoopSwitch::On("ORM_PLATFORM_TEST_VAR", true));  // unset: the default
    CHECK_FALSE(CoopSwitch::Value("ORM_PLATFORM_TEST_VAR").has_value());
}

TEST_CASE("frame log: one line per frame, the first with no interval") {
    CoopFrameLog::Recorder r(1'000'000);
    auto a = r.Frame(5'000'000);
    CHECK(a.line == "1,0.000000,0.000");
    CHECK_FALSE(a.window);
    auto b = r.Frame(5'016'667);
    CHECK(b.line == "2,0.016667,16.667");
    CHECK(r.frames() == 2);
}

TEST_CASE("frame log: a window gives the median, the worst and the spikes") {
    CoopFrameLog::Recorder r(1'000'000);
    std::uint64_t t = 1'000;
    r.Frame(t);
    std::optional<CoopFrameLog::Window> w;
    // 16.6 ms frames with one 80 ms and one 50 ms frame, until a second has gone.
    int i = 0;
    while (!w) {
        const std::uint64_t step = i == 10 ? 80'000 : i == 20 ? 50'000 : 16'600;
        t += step;
        w = r.Frame(t).window;
        ++i;
    }
    CHECK(w->spikes == 2);  // 50 ms counts (kSpikeUs or more)
    CHECK(w->worst_ms == doctest::Approx(80.0));
    CHECK(w->median_ms == doctest::Approx(16.6));
    CHECK(w->frames == static_cast<std::uint64_t>(i));
    CHECK(w->seconds >= 1.0);
    // The next window starts empty.
    const auto next = r.Frame(t + 16'600);
    CHECK_FALSE(next.window);
}
