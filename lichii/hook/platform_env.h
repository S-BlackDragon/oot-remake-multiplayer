// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM platform layer (platform.h, oot-remake docs/PLATAFORMA_HOOKSHOT.md),
// the two services every file of our hook code uses and that need no game:
// the environment (every switch is a variable) and the log. Header only and
// free of the rest of platform.h, so coop_switch.h and the files the tests
// build without the emulator can include it.
//
// Which platform is chosen at compile time:
//   LICHII_COOP            the emulator (Fritchii with daurosk's patches): the
//                          log is the emulator's own (LOG_INFO(Core, ...)), so
//                          every line keeps its source file and line;
//   ORM_PLATFORM_HOOKSHOT  a Hookshot build: the adapter defines
//                          CoopPlatform::Log (hos_log into hookshot_log.txt);
//   neither                the unit tests: the log macros do nothing (code
//                          that logs there keeps its own #if, as before).
// ORM_PLATFORM_LIVE is 1 in the first two (a game runs), 0 in the tests.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#if defined(LICHII_COOP) && defined(ORM_PLATFORM_HOOKSHOT)
#error "LICHII_COOP (the emulator) and ORM_PLATFORM_HOOKSHOT are two platforms: pick one"
#endif

#if defined(LICHII_COOP) || defined(ORM_PLATFORM_HOOKSHOT)
#define ORM_PLATFORM_LIVE 1
#else
#define ORM_PLATFORM_LIVE 0
#endif

namespace CoopPlatform {

// The process environment's variable `name`: its value, empty included when
// it is set to nothing; nothing when it is unset. Any thread. On both
// platforms the switches files (the kit's coop-options.ini through play.ps1,
// nx2_switches.txt in the emulator, hookshot_env.txt in Hookshot) end up in
// the environment before the game starts, so this is the one reading.
inline std::optional<std::string> Env(const char* name) {
    if (name == nullptr) {
        return std::nullopt;
    }
#ifdef _MSC_VER
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }
    std::string value(raw);
    std::free(raw);
    return value;
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    return std::string(raw);
#endif
}

enum class Level { info, warning, error };

#ifdef ORM_PLATFORM_HOOKSHOT
// The adapter's (Hookshot: hos_log("%s", line) with the level in front for
// warnings and errors). Any thread; `line` has no newline.
void Log(Level level, std::string_view line);
#endif

} // namespace CoopPlatform

// ORM_LOG_INFO("coop: x: {}", value): one log line, fmt syntax, as the
// emulator's LOG_INFO(Core, ...) takes it.
#if defined(LICHII_COOP)
#include "common/logging.h"
#define ORM_LOG_INFO(...) LOG_INFO(Core, __VA_ARGS__)
#define ORM_LOG_WARNING(...) LOG_WARNING(Core, __VA_ARGS__)
#define ORM_LOG_ERROR(...) LOG_ERROR(Core, __VA_ARGS__)
#define ORM_LOG_DEBUG(...) LOG_DEBUG(Core, __VA_ARGS__)
#elif defined(ORM_PLATFORM_HOOKSHOT)
#include <fmt/format.h>
#define ORM_LOG_INFO(...) ::CoopPlatform::Log(::CoopPlatform::Level::info, ::fmt::format(__VA_ARGS__))
#define ORM_LOG_WARNING(...) ::CoopPlatform::Log(::CoopPlatform::Level::warning, ::fmt::format(__VA_ARGS__))
#define ORM_LOG_ERROR(...) ::CoopPlatform::Log(::CoopPlatform::Level::error, ::fmt::format(__VA_ARGS__))
#define ORM_LOG_DEBUG(...) ((void)0)  // the adapter keeps no debug lines
#else
#define ORM_LOG_INFO(...) ((void)0)
#define ORM_LOG_WARNING(...) ((void)0)
#define ORM_LOG_ERROR(...) ((void)0)
#define ORM_LOG_DEBUG(...) ((void)0)
#endif
