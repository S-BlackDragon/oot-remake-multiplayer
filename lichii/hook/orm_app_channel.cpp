// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: the in-process channel between the hook and the Cooperativo screen
// (orm_app_channel.h).

#include "orm_app_channel.h"

#include <mutex>
#include <utility>

namespace OrmAppChannel {
namespace {

struct State {
    std::mutex mutex;
    Listener listener;
    SwitchSetter setter;
    std::vector<std::string> answers;
    Switches switches;
    std::uint64_t version = 0;
};

State& The() {
    static State state;
    return state;
}

constexpr std::size_t kMaxAnswers = 32;

} // namespace

void Listen(Listener listener) {
    State& s = The();
    std::lock_guard lock(s.mutex);
    s.listener = std::move(listener);
}

bool Listening() {
    State& s = The();
    std::lock_guard lock(s.mutex);
    return static_cast<bool>(s.listener);
}

bool Send(const Request& request) {
    Listener listener;
    {
        State& s = The();
        std::lock_guard lock(s.mutex);
        listener = s.listener;
    }
    if (!listener) {
        return false;
    }
    listener(request);  // outside the lock: it may answer at once
    return true;
}

void Answer(std::string line) {
    State& s = The();
    std::lock_guard lock(s.mutex);
    if (s.answers.size() < kMaxAnswers) {
        s.answers.push_back(std::move(line));
    }
}

std::vector<std::string> TakeAnswers() {
    State& s = The();
    std::lock_guard lock(s.mutex);
    return std::exchange(s.answers, {});
}

void PublishSwitches(const Switches& switches) {
    State& s = The();
    std::lock_guard lock(s.mutex);
    if (!(s.switches == switches)) {
        s.switches = switches;
        ++s.version;
    }
}

Switches CurrentSwitches() {
    State& s = The();
    std::lock_guard lock(s.mutex);
    return s.switches;
}

std::uint64_t SwitchesVersion() {
    State& s = The();
    std::lock_guard lock(s.mutex);
    return s.version;
}

void SetSwitchSetter(SwitchSetter setter) {
    State& s = The();
    std::lock_guard lock(s.mutex);
    s.setter = std::move(setter);
}

bool SetSwitch(std::uint8_t key, bool on) {
    SwitchSetter setter;
    {
        State& s = The();
        std::lock_guard lock(s.mutex);
        setter = s.setter;
    }
    return setter && setter(key, on);
}

void Reset() {
    State& s = The();
    std::lock_guard lock(s.mutex);
    s.listener = nullptr;
    s.setter = nullptr;
    s.answers.clear();
    s.switches = {};
    s.version = 0;
}

} // namespace OrmAppChannel
