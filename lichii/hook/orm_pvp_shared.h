// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM PvP (oot-remake docs/COMANDOS.md): what the spawn sync (coop_spawn.cpp)
// hands to the PvP frame (orm_commands_game.cpp): our own shots in flight,
// where they are, and the mates' bombs that went off with no copy of ours
// (no explosion in our game, so the blast would never reach our Link). Any
// thread. Header only, no emulator code.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace OrmPvpShared {

struct Shot {
    std::uint64_t id = 0;  // the spawn sync's id of it (stable while it flies)
    std::string actor;
    std::array<float, 3> at{};
};

class Board {
public:
    void SetShots(std::vector<Shot> shots) {
        std::lock_guard lock(mutex_);
        shots_ = std::move(shots);
    }
    std::vector<Shot> Shots() const {
        std::lock_guard lock(mutex_);
        return shots_;
    }
    void AddUnseenBlast(const std::array<float, 3>& at) {
        std::lock_guard lock(mutex_);
        if (blasts_.size() < 32) {
            blasts_.push_back(at);
        }
    }
    std::vector<std::array<float, 3>> TakeUnseenBlasts() {
        std::lock_guard lock(mutex_);
        return std::exchange(blasts_, {});
    }

private:
    mutable std::mutex mutex_;
    std::vector<Shot> shots_;
    std::vector<std::array<float, 3>> blasts_;
};

inline Board& TheBoard() {
    static Board board;
    return board;
}

} // namespace OrmPvpShared
