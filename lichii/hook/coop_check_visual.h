// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md, "Marionetas y
// visual"): one self-check line per risk, so a run's log says which path the
// code took instead of a guess. Each risk that used to rest on an estimate or
// on a memory layout read from the romfs but never seen in a run now has a
// measured or read path and a fallback; the first time either runs, one line:
//
//   coop: check: <thing> ok <what was measured or read>
//   coop: check: <thing> fallback <why, and what is used instead>
//
// Once per thing and path for the whole run (a fallback that later becomes
// ok says both, in that order). The harness scenarios assert these lines
// (`#! expect ... coop: check: saddle ok`), so a scenario tells right from
// wrong, not only that something ran.
//
// Header only: the hook files the tests build without the emulator use it
// too (the line goes to stderr there, or to the sink a test sets).
#pragma once

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "platform_env.h"  // ORM_LOG_INFO, ORM_PLATFORM_LIVE

#if !ORM_PLATFORM_LIVE
#include <cstdio>
#endif

namespace CoopVCheck {

enum class Path : unsigned char { ok, fallback };

constexpr std::string_view Word(Path path) {
    return path == Path::ok ? "ok" : "fallback";
}

// "coop: check: <thing> ok|fallback <detail>" (no detail: no trailing space).
inline std::string Line(std::string_view thing, Path path, std::string_view detail) {
    std::string out = "coop: check: ";
    out += thing;
    out += ' ';
    out += Word(path);
    if (!detail.empty()) {
        out += ' ';
        out += detail;
    }
    return out;
}

using Sink = std::function<void(const std::string&)>;

namespace detail {
struct State {
    std::mutex mutex;
    std::set<std::string> said;  // "<thing>\n<path>"
    Sink sink;                   // tests; empty: the log
};
inline State& The() {
    static State state;
    return state;
}
inline void Emit(const Sink& sink, const std::string& line) {
    if (sink) {
        sink(line);
        return;
    }
#if ORM_PLATFORM_LIVE
    ORM_LOG_INFO("{}", line);
#else
    std::fprintf(stderr, "%s\n", line.c_str());
#endif
}
} // namespace detail

// Says the line the first time this thing takes this path; true when it did.
// Thread safe (the present and the calc thread both report).
inline bool Once(std::string_view thing, Path path, std::string_view detail) {
    auto& s = detail::The();
    Sink sink;
    {
        std::lock_guard lock(s.mutex);
        std::string key(thing);
        key += '\n';
        key += Word(path);
        if (!s.said.insert(std::move(key)).second) {
            return false;
        }
        sink = s.sink;
    }
    detail::Emit(sink, Line(thing, path, detail));
    return true;
}

inline bool Ok(std::string_view thing, std::string_view detail) {
    return Once(thing, Path::ok, detail);
}
inline bool Fallback(std::string_view thing, std::string_view why) {
    return Once(thing, Path::fallback, why);
}

// Whether this thing already said this path (tests, and callers that word a
// later line after it).
inline bool Said(std::string_view thing, Path path) {
    auto& s = detail::The();
    std::lock_guard lock(s.mutex);
    std::string key(thing);
    key += '\n';
    key += Word(path);
    return s.said.contains(key);
}

// Tests: where the lines go (empty: stderr) and a fresh start.
inline void SetSinkForTest(Sink sink) {
    auto& s = detail::The();
    std::lock_guard lock(s.mutex);
    s.sink = std::move(sink);
}
inline void ResetForTest() {
    auto& s = detail::The();
    std::lock_guard lock(s.mutex);
    s.said.clear();
}

} // namespace CoopVCheck
