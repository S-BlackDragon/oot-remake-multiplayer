// The chat lines the co-op window shows: received chat, our own sent chat
// and room notices ("Saria joined"). The network thread adds, the Qt window
// reads; header only, standard C++ only.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace CoopChat {

using Clock = std::chrono::steady_clock;

// How many lines are kept; older ones are dropped.
inline constexpr std::size_t kKeep = 6;

struct Line {
    std::string text;
    bool notice = false;  // a join, leave or connection line, not chat
    Clock::time_point at{};
    // ORM (docs/COMUNICACION.md): who said it, for the in-game HUD's colours:
    // the sender's number (ours for our own line, 0 for a notice), the name
    // `text` starts with ("Ana" of "Ana: hola", "you"), and whether it is one
    // of the quick messages (coop/host/quick_chat.hpp).
    std::uint8_t player = 0;
    std::string name;
    bool quick = false;
};

class Log {
public:
    void Add(std::string text, bool notice) {
        Push({std::move(text), notice, Clock::now()});
    }

    // ORM: a chat line from `player`, shown as "name: body".
    void AddFrom(std::uint8_t player, std::string name, const std::string& body, bool quick) {
        Line line{name + ": " + body, false, Clock::now()};
        line.player = player;
        line.name = std::move(name);
        line.quick = quick;
        Push(std::move(line));
    }

    // The kept lines, oldest first.
    std::vector<Line> Lines() const {
        std::lock_guard lock(mutex_);
        return {lines_.begin(), lines_.end()};
    }

private:
    void Push(Line line) {
        std::lock_guard lock(mutex_);
        lines_.push_back(std::move(line));
        while (lines_.size() > kKeep) {
            lines_.pop_front();
        }
    }

    mutable std::mutex mutex_;
    std::deque<Line> lines_;
};

} // namespace CoopChat
