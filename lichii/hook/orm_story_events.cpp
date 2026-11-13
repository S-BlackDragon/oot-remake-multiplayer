// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM story cutscenes, asking for an event by name (orm_story_events.h). No
// emulator code.

#include "orm_story_events.h"

#include "coop_switch.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace OrmStoryEvents {
namespace {

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace

bool SwitchOn(const std::optional<std::string>& events, const std::optional<std::string>& old_story) {
    // coop_switch.h's words; an unknown value of the new name is as unset.
    if (events) {
        if (const auto r = CoopSwitch::Parse(*events); r == CoopSwitch::Reading::on || r == CoopSwitch::Reading::off) {
            return r == CoopSwitch::Reading::on;
        }
    }
    return old_story && CoopSwitch::Parse(*old_story) == CoopSwitch::Reading::on;
}

bool ValidName(std::string_view name) {
    if (name.empty() || name.size() > kMaxName) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool Requester::Ask(std::uint64_t main, std::uint64_t actor, std::string name) {
    std::lock_guard lock(mutex_);
    if (!ValidName(name) || main == 0 || actor == 0) {
        log_.push_back("refused '" + name + "': " +
                       (!ValidName(name) ? "not an event name" : main == 0 ? "no game" : "no Link"));
        return false;
    }
    if (!name_.empty()) {
        log_.push_back("'" + name_ + "' was not asked yet; replaced");
    }
    main_ = main;
    actor_ = actor;
    name_ = std::move(name);
    return true;
}

std::optional<CoopPuppet::Call> Requester::Step(CoopPuppet::Guest& guest, bool after_call,
                                                std::uint64_t result) {
    std::lock_guard lock(mutex_);
    if (after_call) {
        if (phase_ == Phase::allocating) {
            phase_ = Phase::idle;
            if (result == 0) {
                alloc_failed_ = true;
                log_.push_back("the game's malloc gave nothing; '" + name_ + "' not asked");
                name_.clear();
                return std::nullopt;
            }
            block_ = result;
            return Prepare(guest);
        }
        if (phase_ == Phase::requesting) {
            phase_ = Phase::idle;
            last_answer_ = (result & 1) != 0;
            std::uint32_t after = 0;
            const bool read = guest.Read(component_ + kComponentPhase, &after, 4);
            log_.push_back("the game " + std::string(*last_answer_ ? "took" : "refused") + " '" +
                           asked_ + "' (returned " + Hex(result) + ", component phase " +
                           (read ? std::to_string(after) : std::string("unreadable")) + ")");
        }
        return std::nullopt;
    }
    if (phase_ != Phase::idle || name_.empty() || main_ == 0) {
        return std::nullopt;
    }
    if (block_ == 0) {
        if (alloc_failed_) {
            log_.push_back("'" + name_ + "' not asked: no block (the malloc failed before)");
            name_.clear();
            return std::nullopt;
        }
        phase_ = Phase::allocating;
        CoopPuppet::Call c;
        c.function = main_ + kMalloc;
        c.x[0] = kBlockSize;
        return c;
    }
    return Prepare(guest);
}

std::optional<CoopPuppet::Call> Requester::Prepare(CoopPuppet::Guest& guest) {
    const std::string name = std::exchange(name_, {});
    std::uint32_t state = 0;
    std::uint64_t table = 0;
    std::uint64_t component = 0;
    if (!guest.Read(actor_ + kActorState, &state, 4) || state != kActorStatePresent) {
        log_.push_back("'" + name + "' not asked: Link's actor is gone");
        return std::nullopt;
    }
    if (!guest.Read(actor_ + kActorComponents, &table, 8) || table == 0 ||
        !guest.Read(table + kComponentEvent, &component, 8) || component == 0) {
        log_.push_back("'" + name + "' not asked: Link's event component is not readable");
        return std::nullopt;
    }
    std::array<char, kMaxName + 1> text{};
    std::memcpy(text.data(), name.data(), name.size());
    const std::uint64_t box = block_ + kBlockName;
    std::uint32_t phase = 0;
    const bool phase_read = guest.Read(component + kComponentPhase, &phase, 4);
    if (!guest.Write(block_ + kBlockName, text.data(), name.size() + 1) ||
        !guest.Write(block_ + kBlockBox, &box, 8)) {
        log_.push_back("'" + name + "' not asked: the block at " + Hex(block_) + " is not writable");
        return std::nullopt;
    }
    phase_ = Phase::requesting;
    asked_ = name;
    component_ = component;
    log_.push_back("asking '" + name + "' with Link's event component " + Hex(component) +
                   " (phase " + (phase_read ? std::to_string(phase) : std::string("unreadable")) +
                   "), name at " + Hex(box));
    CoopPuppet::Call c;
    c.function = main_ + kRequestEvent;
    c.x[0] = component;
    c.x[1] = block_ + kBlockBox;
    return c;
}

std::vector<std::string> Requester::TakeLog() {
    std::lock_guard lock(mutex_);
    return std::exchange(log_, {});
}

std::optional<bool> Requester::LastAnswer() {
    std::lock_guard lock(mutex_);
    return last_answer_;
}

bool Skipper::Want(std::uint64_t main, std::string why) {
    std::lock_guard lock(mutex_);
    if (main == 0) {
        log_.push_back("no skip (" + why + "): no game");
        return false;
    }
    main_ = main;
    why_ = why.empty() ? std::string("asked") : std::move(why);
    return true;
}

std::optional<CoopPuppet::Call> Skipper::Step(CoopPuppet::Guest& guest, bool after_call) {
    std::lock_guard lock(mutex_);
    if (after_call || why_.empty() || main_ == 0) {
        return std::nullopt;
    }
    const std::string why = std::exchange(why_, {});
    std::uint64_t manager = 0;
    std::uint64_t session = 0;
    std::uint64_t events = 0;
    std::uint32_t count = 0;
    std::uint32_t current = 0;
    std::uint64_t event = 0;
    if (!guest.Read(main_ + kEventManagerPtr, &manager, 8) || manager == 0 ||
        !guest.Read(manager + kEventSession, &session, 8) || session == 0) {
        log_.push_back("no event to skip (" + why + "): none runs");
        return std::nullopt;
    }
    if (!guest.Read(session + kSessionEvents, &events, 8) || events == 0 ||
        !guest.Read(session + kSessionCount, &count, 4) || !guest.Read(session + kSessionCurrent, &current, 4) ||
        current >= count || !guest.Read(events + 8ull * current, &event, 8) || event == 0) {
        log_.push_back("no event to skip (" + why + "): the current event is not readable");
        return std::nullopt;
    }
    log_.push_back("force skip of the event " + Hex(event) + " (" + why + ")");
    CoopPuppet::Call c;
    c.function = main_ + kForceSkip;
    c.x[0] = event;
    return c;
}

std::vector<std::string> Skipper::TakeLog() {
    std::lock_guard lock(mutex_);
    return std::exchange(log_, {});
}

} // namespace OrmStoryEvents
