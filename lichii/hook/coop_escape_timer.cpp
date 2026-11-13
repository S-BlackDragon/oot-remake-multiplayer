// SPDX-License-Identifier: GPL-3.0-or-later
//
// The escape's countdown looked for in our game (coop_escape_timer.h). The Driver has no
// emulator code (the tests build it alone); the glue at the end is only built where a game
// runs (ORM_PLATFORM_LIVE).

#include "coop_escape_timer.h"

#include <charconv>
#include <system_error>
#include <utility>

namespace CoopEscapeTimer {
namespace {

namespace host = coop::host;

// 1 to 3600 whole seconds.
std::optional<std::uint32_t> SecondsOf(const std::string& text) {
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size() || value == 0 || value > 3600) {
        return std::nullopt;
    }
    return value;
}

bool IsOff(const std::string& v) {
    return v == "0" || v == "off" || v == "false" || v == "no";
}
bool IsOn(const std::string& v) {
    return v == "1" || v == "on" || v == "true" || v == "yes";
}

host::CountdownSearchSettings SearchSettingsOf(const Settings& s, std::vector<std::string>& lines) {
    host::CountdownSearchSettings out;
    if (s.length_s) {
        if (const auto length = SecondsOf(*s.length_s)) {
            out.length_s = *length;
        }
    }
    if (s.tries) {
        // 1 to 720 searches (about an hour); anything else keeps the default, with a line.
        if (const auto tries = SecondsOf(*s.tries); tries && *tries <= 720) {
            out.tries = static_cast<int>(*tries);
        } else {
            lines.push_back("coop: escape clock: LICHII_ORM_ESCAPE_CLOCK_TRIES " + *s.tries +
                            ": not 1 to 720; " + std::to_string(out.tries) + " tries");
        }
    }
    if (s.clock && !s.word_given && s.search && !IsOff(*s.search) && !IsOn(*s.search)) {
        std::string why;
        if (auto extra = host::parse_countdown_roots(*s.search, &why)) {
            for (auto& r : *extra) {
                out.roots.push_back(std::move(r));
            }
        } else {
            lines.push_back("coop: escape clock: LICHII_ORM_ESCAPE_CLOCK_SEARCH " + *s.search + ": " + why +
                            "; the GameUI module only");
        }
    }
    return out;
}

} // namespace

Driver::Driver(const Settings& settings) : search_(SearchSettingsOf(settings, begin_)) {
    if (!settings.clock) {
        begin_.clear();
        return;
    }
    if (settings.word_given) {
        begin_.push_back("coop: escape clock: search off: the word is given by hand (LICHII_ORM_ESCAPE_CLOCK_WORD)");
        return;
    }
    if (settings.search && IsOff(*settings.search)) {
        begin_.push_back("coop: escape clock: search off (LICHII_ORM_ESCAPE_CLOCK_SEARCH=0): with no "
                         "LICHII_ORM_ESCAPE_CLOCK_WORD the group's clock goes on without the game's timer");
        return;
    }
    on_ = true;
    std::vector<std::string> again;  // the complaints are in begin_ already
    begin_.push_back(std::string("coop: escape clock: search on: our countdown is looked for in ") + kScene +
                     " while the collapse runs (a word that drops 1 s a second, up to " +
                     std::to_string(SearchSettingsOf(settings, again).length_s) + " s)");
}

bool Driver::IsLoadEvent(std::string_view name) {
    return name == "DefaultBootEvent" || name == "DefaultParentEvent";
}

std::vector<std::string> Driver::Frame(host::GameMemory* memory, const std::optional<std::string>& scene,
                                       bool event, std::uint64_t now_us,
                                       const std::optional<std::string>& event_name) {
    std::vector<std::string> out;
    if (!on_) {
        return out;
    }
    if (memory == nullptr || !scene) {
        // A load: the timer stands. Long enough, it is a new visit (a Continue's reload).
        if (!loading_since_) {
            loading_since_ = now_us;
        }
        search_.interrupt();
        if (now_us - *loading_since_ >= kNewVisitUs) {
            search_.reset();  // a word found stays found
            armed_ = false;
            in_event_ = false;
        }
        return out;
    }
    loading_since_.reset();
    if (*scene != kScene) {
        search_.reset();
        armed_ = false;
        in_event_ = false;
        return out;
    }
    if (event) {
        if (!in_event_) {
            event_arms_ = false;
        }
        in_event_ = true;
        // Unknown name: as before, any event arms. A load event alone does not.
        if (!event_name || !IsLoadEvent(*event_name)) {
            event_arms_ = true;
        }
        search_.interrupt();  // the collapse's start or a floor's cutscene: the timer may stand
        return out;
    }
    if (in_event_) {
        in_event_ = false;
        if (!event_arms_) {
            return out;  // a warp's or a Continue's load event: the collapse has not started
        }
        if (!armed_ && search_.state() != coop::host::CountdownSearch::State::found) {
            out.push_back("coop: escape clock: an event of the collapse ended: our countdown should run now");
        }
        armed_ = true;
    }
    if (!armed_) {
        return out;  // the countdown starts with the collapse's event (Defeat_Ganon's timer start)
    }
    for (auto& line : search_.step(*memory, now_us)) {
        out.push_back("coop: " + std::move(line));
    }
    return out;
}

std::optional<std::string> Driver::Found() const {
    if (!search_.found()) {
        return std::nullopt;
    }
    return search_.found()->text();
}

} // namespace CoopEscapeTimer

#include "platform_env.h"  // ORM_PLATFORM_LIVE

#if ORM_PLATFORM_LIVE

#include <chrono>

#include "coop_switch.h"
#include "platform.h"  // Platform, ORM_LOG_*

namespace CoopEscapeTimer {
namespace {

struct Glue {
    std::once_flag checked;
    std::optional<Driver> driver;
    std::mutex found_mutex;
    std::optional<std::string> found;
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        Settings s;
        s.clock = CoopSwitch::On("LICHII_ORM_ESCAPE_CLOCK", "LICHII_COOP_ESCAPE_CLOCK", false);
        s.word_given = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_WORD").has_value();
        s.search = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_SEARCH");
        s.length_s = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_LENGTH_S");
        s.tries = CoopSwitch::Value("LICHII_ORM_ESCAPE_CLOCK_TRIES");
        glue.driver.emplace(s);
        for (const auto& line : glue.driver->Begin()) {
            ORM_LOG_INFO("{}", line);
        }
    });
    return glue;
}

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base,
           const std::optional<coop::host::PlayerReading>& reading) {
    Glue& g = TheGlue();
    if (!g.driver->On()) {
        return;
    }
    coop::host::GameMemory* memory = nullptr;
    std::optional<std::string> scene;
    bool event = false;
    std::optional<std::string> event_name;
    if (main_base != 0) {
        memory = &platform.Memory();
        if (reading) {
            scene = reading->scene;
            event = reading->event_running.value_or(false);
            if (event && scene && *scene == kScene) {
                coop::host::PlayerReader reader(*memory);
                event_name = reader.read_event_name();
            }
        }
    }
    for (const auto& line : g.driver->Frame(memory, scene, event, NowUs(), event_name)) {
        ORM_LOG_INFO("{}", line);
    }
    if (auto word = g.driver->Found()) {
        std::lock_guard lock(g.found_mutex);
        g.found = std::move(word);
    }
}

std::optional<std::string> Found() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.found_mutex);
    return g.found;
}

} // namespace CoopEscapeTimer

#endif // ORM_PLATFORM_LIVE
