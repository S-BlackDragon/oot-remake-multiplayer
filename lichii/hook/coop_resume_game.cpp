// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM resume, the emulator side (coop_resume_game.h, coop_resume.h).

#include "coop_resume_game.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "coop_boss.h"
#include "coop_fairy.h"  // the dungeon room table (issue 197)
#include "coop_settings.h"
#include "coop_switch.h"
#include "platform.h"  // FolderPath, ORM_LOG_*

namespace CoopResume {
namespace {

constexpr const char* kFileName = "coop-resume.txt";
// After the way back (or none), before the first place is written: the
// grounded place of the new spot needs a moment (CoopTeleport::Grounded).
constexpr std::uint64_t kRecordAfterUs = 3'000'000;

std::filesystem::path ConfigDir() {
    return CoopPlatform::FolderPath(CoopPlatform::Folder::config);
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// A temporary file, then the rename: a crash never leaves half a line.
bool WriteFile(const std::filesystem::path& path, const std::string& text) {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << text;
        if (!file.flush()) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

struct Glue {
    bool on = CoopSwitch::On("LICHII_ORM_RESUME", true);
    std::filesystem::path path = ConfigDir() / kFileName;
    std::string room;
    std::string name;
    std::optional<Recorder> recorder;
    // Read once: a pack made (again) while the game runs is seen after a restart.
    CoopRooms::Table rooms = CoopFairy::RoomTable();
    std::optional<Returner> returner;
    std::uint64_t record_at_us = 0;  // 0: not set yet
    bool pending = false;            // a travel was handed to the trip this frame
    bool said_write_error = false;

    Glue() {
        if (!on) {
            ORM_LOG_INFO("coop: resume: off (LICHII_ORM_RESUME=0): no last place kept, no way back");
            return;
        }
        std::vector<std::string> errors;
        const auto settings = CoopHook::LoadSettings(ConfigDir() / "coop.ini", errors);
        name = Clean(settings.name);
        if (auto r = CoopSwitch::Value("LICHII_ORM_ROOM")) {
            room = Clean(*r);
        } else {
            room = Clean(settings.server + ":" + std::to_string(settings.port) + settings.path);
        }
        recorder.emplace(room, name);
        std::optional<Place> place;
        std::string from;
        if (auto given = CoopPlatform::Env("LICHII_ORM_RESUME_PLACE"); given && !given->empty()) {
            if (*given == "none") {
                from = "none given by the app";
            } else if (auto p = FromGiven(*given, name)) {
                place = std::move(p);
                from = "given by the app";
            } else {
                from = "LICHII_ORM_RESUME_PLACE has no good place line of this player: ignored";
            }
        } else {
            place = FromFile(ReadFile(path), room, name);
            from = place ? "this PC's " + std::string(kFileName) : "none in " + path.string();
        }
        if (place) {
            ORM_LOG_INFO("coop: resume: on: the last place of '{}' in room {}: {} at {:.1f} {:.1f} {:.1f} ({}, {})",
                         name, room, place->scene, place->at.x, place->at.y, place->at.z,
                         place->kind == Kind::ok ? "ok" : (place->kind == Kind::boss ? "boss" : "event"), from);
        } else {
            ORM_LOG_INFO("coop: resume: on: no last place of '{}' in room {} ({})", name, room, from);
        }
        // Issue 197: the spot of a dungeon's entrance room we last stood on,
        // this PC's (the channel keeps only the place line).
        std::optional<Door> door = DoorFromFile(ReadFile(path), room, name);
        if (door) {
            ORM_LOG_INFO("coop: resume: the spot of {}'s entrance room last stood on: {:.1f} {:.1f} {:.1f} (start "
                         "point {}, this PC's {})",
                         door->scene, door->at.x, door->at.y, door->at.z, door->start, kFileName);
        }
        ORM_LOG_INFO("coop: resume: dungeon rooms: {} (issue 197: a last place in another room than the entrance's "
                     "goes to that spot, else the entrance)",
                     rooms.empty() ? "no room table in the puppet pack (moves within 10 m of the entrance only)"
                                   : std::to_string(rooms.size()) + " placements in the puppet pack's table");
        recorder->KeepDoor(door);
        returner.emplace(std::move(place), std::move(door));
    }
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

} // namespace

std::optional<Decision> Frame(const CoopTeleport::Local& local, const CoopTeleport::Grounded& grounded,
                              bool resuming, bool teleport_on, std::uint64_t now_us) {
    Glue& g = TheGlue();
    if (!g.on) {
        return std::nullopt;
    }
    Now now;
    now.local = local;
    now.grounded = grounded.Last();
    now.grounded_scene = grounded.Scene();
    if (now.grounded && !now.grounded_scene.empty()) {
        now.boss_room = CoopBoss::RoomAt(now.grounded_scene,
                                         CoopBoss::Vec{now.grounded->x, now.grounded->y, now.grounded->z}) != nullptr;
    }
    now.unix_s = static_cast<std::int64_t>(std::time(nullptr));
    now.rooms = &g.rooms;

    std::optional<Decision> out;
    const bool was_pending = std::exchange(g.pending, false);
    if (!g.returner->Done()) {
        out = g.returner->Frame(now, now_us);
        if (out && out->what == Decision::What::travel && !teleport_on) {
            out->what = Decision::What::none;
            out->log += "; but /teleport is off here (LICHII_COOP_TELEPORT=0): no scene change, staying here";
            out->notice.clear();
        }
        if (out) {
            ORM_LOG_INFO("coop: {}", out->log);
            g.pending = out->what == Decision::What::travel;
        }
        return out;
    }
    if (!g.recorder->Started()) {
        if (was_pending || resuming) {
            return std::nullopt;  // the trip back still runs
        }
        if (g.record_at_us == 0) {
            g.record_at_us = now_us + kRecordAfterUs;
        }
        if (now_us < g.record_at_us) {
            return std::nullopt;
        }
        g.recorder->Start();
        ORM_LOG_INFO("coop: resume: keeping our last place in {}", g.path.string());
    }
    if (auto place = g.recorder->Frame(now, now_us)) {
        if (!WriteFile(g.path, FileText(*place, g.recorder->LastDoor()))) {
            if (!g.said_write_error) {
                g.said_write_error = true;
                ORM_LOG_WARNING("coop: resume: {} could not be written", g.path.string());
            }
        } else {
            ORM_LOG_DEBUG("coop: resume: kept {}", Format(*place));
        }
    }
    return std::nullopt;
}

} // namespace CoopResume
