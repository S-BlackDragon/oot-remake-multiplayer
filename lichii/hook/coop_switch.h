// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: one reading for every on/off switch of our hook code (oot-remake
// docs/REVISION_GANCHO.md "Interruptores", docs/T9_INTEGRADO.md). A switch is
// an environment variable (the kit's coop-options.ini through play.ps1, or
// nx2_switches.txt):
//   unset or empty                  -> the switch's default;
//   0, false, no, off (any case)    -> off;
//   1, true, yes, on (any case)     -> on;
//   anything else                   -> the default, and one log line
//                                      "coop: switch NAME=VALUE is not ...".
// Our switches renamed for test9 (ADOPTAR_UPSTREAM.md rule 6) are read under
// their LICHII_ORM_* name first and, when that is unset, under the old
// LICHII_COOP_* name, so kits and harness scenarios of before keep working.
// Header only: the hook files the tests build without the emulator use it
// too (the log line goes to stderr there). Upstream's own switches keep their
// own readings.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "platform_env.h"  // CoopPlatform::Env, ORM_LOG_* (oot-remake docs/PLATAFORMA_HOOKSHOT.md)

#if !ORM_PLATFORM_LIVE
#include <cstdio>
#endif

namespace CoopSwitch {

enum class Reading : std::uint8_t { unset, off, on, unknown };

namespace detail {
constexpr bool SameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != b[i]) {
            return false;
        }
    }
    return true;
}
} // namespace detail

// What a value says. Surrounding spaces do not count.
constexpr Reading Parse(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    if (value.empty()) {
        return Reading::unset;
    }
    for (const std::string_view off : {"0", "false", "no", "off"}) {
        if (detail::SameNoCase(value, off)) {
            return Reading::off;
        }
    }
    for (const std::string_view on : {"1", "true", "yes", "on"}) {
        if (detail::SameNoCase(value, on)) {
            return Reading::on;
        }
    }
    return Reading::unknown;
}

// The switch's state for a value (nullptr: unset).
constexpr bool From(const char* value, bool default_on) {
    if (value == nullptr) {
        return default_on;
    }
    switch (Parse(value)) {
    case Reading::off:
        return false;
    case Reading::on:
        return true;
    case Reading::unset:
    case Reading::unknown:
        break;
    }
    return default_on;
}

// The variable's value; nothing when unset or empty.
inline std::optional<std::string> Value(const char* name) {
    auto value = CoopPlatform::Env(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return value;
}

// The renamed switch's value: `name` (LICHII_ORM_*) when set, else `old_name`
// (LICHII_COOP_*). `old_name` may be nullptr.
inline std::optional<std::string> Value(const char* name, const char* old_name) {
    if (auto value = Value(name)) {
        return value;
    }
    return old_name != nullptr ? Value(old_name) : std::nullopt;
}

// The renamed switch that is set, `name` first, and its value; nothing when
// neither is. For the log lines, which name the variable the player set.
struct Found {
    const char* name = nullptr;
    std::string value;
};
inline std::optional<Found> Find(const char* name, const char* old_name) {
    if (auto value = Value(name)) {
        return Found{name, std::move(*value)};
    }
    if (old_name != nullptr) {
        if (auto value = Value(old_name)) {
            return Found{old_name, std::move(*value)};
        }
    }
    return std::nullopt;
}

// The log line for a value a switch does not take: "coop: switch NAME=VALUE
// is not <accepted>: <default> (its default)". Selectors (a switch with more
// than on and off) say what they take in `accepted`.
inline std::string Unknown(std::string_view name, std::string_view value, std::string_view accepted,
                           std::string_view default_text) {
    return "coop: switch " + std::string(name) + "=" + std::string(value) + " is not " + std::string(accepted) +
           ": " + std::string(default_text) + " (its default)";
}

// The log line for a value that is no switch value, or nothing.
inline std::optional<std::string> Complaint(std::string_view name, std::string_view value, bool default_on) {
    if (Parse(value) != Reading::unknown) {
        return std::nullopt;
    }
    return Unknown(name, value, "0/1/true/false/yes/no/on/off", default_on ? "on" : "off");
}

inline void Log(const std::string& line) {
#if ORM_PLATFORM_LIVE
    ORM_LOG_WARNING("{}", line);
#else
    std::fprintf(stderr, "%s\n", line.c_str());
#endif
}

// Reads the switch `name` (and its old name, when given). Unknown values log
// one line each time they are read; read a switch once (at start) where it
// can be.
inline bool On(const char* name, const char* old_name, bool default_on) {
    const auto found = Find(name, old_name);
    if (!found) {
        return default_on;
    }
    if (const auto line = Complaint(found->name, found->value, default_on)) {
        Log(*line);
    }
    return From(found->value.c_str(), default_on);
}

inline bool On(const char* name, bool default_on) {
    return On(name, nullptr, default_on);
}

} // namespace CoopSwitch
