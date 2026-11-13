// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM resume: the rules (coop_resume.h). No emulator code.
#include "coop_resume.h"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>
#include <vector>

#include "coop_boss.h"
#include "coop_dungeon.h"

namespace CoopResume {
namespace {

using coop::protocol::Age;

constexpr std::string_view kVersion = "v1";
constexpr std::size_t kMaxField = 64;
constexpr std::size_t kFields = 10;
constexpr std::string_view kDoorVersion = "d1";  // issue 197: the entrance room's spot
constexpr std::size_t kDoorFields = 10;
constexpr float kDoorProbeUnits = 0.25f;  // a grounded place this close to the last one looked at: not again

std::string Number(float value) {
    std::array<char, 32> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::fixed, 2);
    if (r.ec != std::errc{}) {
        return "0";
    }
    return std::string(buf.data(), r.ptr);
}

std::optional<float> ParseFloat(std::string_view text) {
    float value = 0.0f;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> ParseInt(std::string_view text) {
    std::int64_t value = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::string_view KindName(Kind kind) {
    switch (kind) {
    case Kind::ok:
        return "ok";
    case Kind::boss:
        return "boss";
    case Kind::event:
        return "event";
    }
    return "ok";
}

std::string_view AgeText(const std::optional<Age>& age) {
    if (!age) {
        return "-";
    }
    return *age == Age::child ? "child" : "adult";
}

std::vector<std::string_view> Split(std::string_view line) {
    std::vector<std::string_view> out;
    while (true) {
        const auto cut = line.find(';');
        out.push_back(line.substr(0, cut));
        if (cut == std::string_view::npos) {
            return out;
        }
        line.remove_prefix(cut + 1);
    }
}

float Distance(const Vec& a, const Vec& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string Where(const Vec& v) {
    return Number(v.x) + " " + Number(v.y) + " " + Number(v.z);
}

} // namespace

std::string Clean(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    std::string out(text.substr(0, kMaxField));
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (c == ';' || c == '#' || u < 0x20 || u == 0x7f) {
            c = '_';
        }
    }
    return out;
}

std::string Format(const Place& place) {
    std::string out(kVersion);
    for (const std::string& part :
         {Clean(place.room), Clean(place.name), Clean(place.scene), Number(place.at.x), Number(place.at.y),
          Number(place.at.z), std::string(AgeText(place.age)), std::string(KindName(place.kind)),
          std::to_string(place.unix_s)}) {
        out += ';';
        out += part;
    }
    return out;
}

std::optional<Place> Parse(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.remove_suffix(1);
    }
    while (!line.empty() && line.front() == ' ') {
        line.remove_prefix(1);
    }
    const auto f = Split(line);
    if (f.size() != kFields || f[0] != kVersion) {
        return std::nullopt;
    }
    Place p;
    p.room = std::string(f[1]);
    p.name = std::string(f[2]);
    p.scene = std::string(f[3]);
    if (p.scene.empty() || p.scene.size() > kMaxField || p.room.size() > kMaxField || p.name.size() > kMaxField ||
        Clean(p.scene) != p.scene || Clean(p.room) != p.room || Clean(p.name) != p.name) {
        return std::nullopt;
    }
    const auto x = ParseFloat(f[4]);
    const auto y = ParseFloat(f[5]);
    const auto z = ParseFloat(f[6]);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    p.at = Vec{*x, *y, *z};
    if (f[7] == "child") {
        p.age = Age::child;
    } else if (f[7] == "adult") {
        p.age = Age::adult;
    } else if (f[7] != "-") {
        return std::nullopt;
    }
    if (f[8] == "ok") {
        p.kind = Kind::ok;
    } else if (f[8] == "boss") {
        p.kind = Kind::boss;
    } else if (f[8] == "event") {
        p.kind = Kind::event;
    } else {
        return std::nullopt;
    }
    const auto at = ParseInt(f[9]);
    if (!at) {
        return std::nullopt;
    }
    p.unix_s = *at;
    return p;
}

std::string FormatDoor(std::string_view room, std::string_view name, const Door& door) {
    std::string out(kDoorVersion);
    for (const std::string& part :
         {Clean(room), Clean(name), Clean(door.scene), Clean(door.start), Number(door.at.x), Number(door.at.y),
          Number(door.at.z), std::string(AgeText(door.age)), std::to_string(door.unix_s)}) {
        out += ';';
        out += part;
    }
    return out;
}

std::optional<Door> DoorFromFile(std::string_view text, std::string_view room, std::string_view name) {
    const std::string want_room = Clean(room);
    const std::string want_name = Clean(name);
    while (!text.empty()) {
        const auto cut = text.find('\n');
        std::string_view line = text.substr(0, cut);
        text = cut == std::string_view::npos ? std::string_view{} : text.substr(cut + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        const auto f = Split(line);
        if (f.size() != kDoorFields || f[0] != kDoorVersion || f[1] != want_room || f[2] != want_name) {
            continue;
        }
        Door d;
        d.scene = std::string(f[3]);
        d.start = std::string(f[4]);
        if (d.scene.empty() || d.start.empty() || Clean(d.scene) != d.scene || Clean(d.start) != d.start) {
            continue;
        }
        const auto x = ParseFloat(f[5]);
        const auto y = ParseFloat(f[6]);
        const auto z = ParseFloat(f[7]);
        const auto at = ParseInt(f[9]);
        if (!x || !y || !z || !at) {
            continue;
        }
        d.at = Vec{*x, *y, *z};
        if (f[8] == "child") {
            d.age = Age::child;
        } else if (f[8] == "adult") {
            d.age = Age::adult;
        } else if (f[8] != "-") {
            continue;
        }
        d.unix_s = *at;
        return d;
    }
    return std::nullopt;
}

std::string FileText(const Place& place, const std::optional<Door>& door) {
    std::string out =
        "# ORM resume (oot-remake docs/PARTIDA_SALA.md): this player's last place in this room.\n"
        "# v1;room;name;scene;x;y;z;age;kind;unix seconds. Written by the game; the app takes it to the server.\n" +
        Format(place) + "\n";
    if (door) {
        out += "# d1;room;name;dungeon;start;x;y;z;age;unix seconds: the spot of that dungeon's entrance room we last\n"
               "# stood on (issue 197: a last place in another room goes back there). This PC only.\n" +
               FormatDoor(place.room, place.name, *door) + "\n";
    }
    return out;
}

std::optional<Door> DoorAt(const CoopRooms::Table& rooms, std::string_view scene, const Vec& at,
                           std::optional<Age> age, std::int64_t unix_s) {
    if (!CoopDungeon::IsDungeonScene(scene)) {
        return std::nullopt;
    }
    const auto start = CoopTeleport::NearestStart(scene, at);
    if (!start) {
        return std::nullopt;
    }
    const auto from = CoopTeleport::StartPosition(scene, *start);
    if (!from || CoopTeleport::CheckMove(rooms, scene, *from, at) != CoopTeleport::MoveCheck::ok) {
        return std::nullopt;
    }
    Door d;
    d.scene = Clean(scene);
    d.start = *start;
    d.at = at;
    d.age = age;
    d.unix_s = unix_s;
    return d;
}

std::optional<Place> FromFile(std::string_view text, std::string_view room, std::string_view name) {
    const std::string want_room = Clean(room);
    const std::string want_name = Clean(name);
    while (!text.empty()) {
        const auto cut = text.find('\n');
        const std::string_view line = text.substr(0, cut);
        text = cut == std::string_view::npos ? std::string_view{} : text.substr(cut + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (auto p = Parse(line); p && p->room == want_room && p->name == want_name) {
            return p;
        }
    }
    return std::nullopt;
}

std::optional<Place> FromGiven(std::string_view given, std::string_view name) {
    std::vector<Place> places;
    bool bad = false;
    while (true) {
        const auto cut = given.find('|');
        if (auto p = Parse(given.substr(0, cut))) {
            places.push_back(std::move(*p));
        } else {
            bad = true;
        }
        if (cut == std::string_view::npos) {
            break;
        }
        given.remove_prefix(cut + 1);
    }
    if (places.size() == 1 && !bad) {
        return places.front();
    }
    const std::string want = Clean(name);
    for (auto& p : places) {
        if (p.name == want) {
            return p;
        }
    }
    return std::nullopt;
}

std::optional<Place> Recorder::Frame(const Now& now, std::uint64_t now_us) {
    if (!started_ || !now.grounded || now.grounded_scene.empty()) {
        return std::nullopt;
    }
    Place p;
    p.room = room_;
    p.name = name_;
    p.scene = Clean(now.grounded_scene);
    p.at = *now.grounded;
    p.age = now.local.age;
    if (now.local.fighting || now.boss_room) {
        p.kind = Kind::boss;
    } else if (now.local.event) {
        p.kind = Kind::event;
    }
    p.unix_s = now.unix_s;
    if (now.rooms != nullptr && p.kind == Kind::ok &&
        (!door_probe_ || door_probe_scene_ != p.scene || Distance(*door_probe_, p.at) > kDoorProbeUnits)) {
        door_probe_ = p.at;
        door_probe_scene_ = p.scene;
        if (auto d = DoorAt(*now.rooms, p.scene, p.at, p.age, p.unix_s)) {
            door_ = std::move(d);
        }
    }
    if (last_) {
        const bool other = last_->scene != p.scene || last_->kind != p.kind || last_->age != p.age;
        const bool moved = Distance(last_->at, p.at) > kMoveUnits;
        if (!other && !moved) {
            return std::nullopt;
        }
        if (now_us - last_us_ < (other ? kSoonUs : kEveryUs)) {
            return std::nullopt;
        }
    }
    last_ = p;
    last_us_ = now_us;
    return p;
}

namespace {

// The kept door, if it is one to go to for `place` (same dungeon, same age).
const Door* DoorFor(const std::optional<Door>& door, const Place& place, const std::optional<Age>& age) {
    if (!door || door->scene != place.scene) {
        return nullptr;
    }
    if (door->age && age && *door->age != *age) {
        return nullptr;
    }
    return &*door;
}

std::string MoveCheckText(CoopTeleport::MoveCheck check) {
    return check == CoopTeleport::MoveCheck::other_room ? "another room of this dungeon"
                                                        : "too far in this dungeon, with no room table for it";
}

constexpr const char* kWhy197 =
    "issue 197: a dungeon loads one room at a time and its only start points are in the entrance room, so Link is "
    "never put into another room";

} // namespace

Decision Decide(const Place& place, const Now& now, const std::optional<Door>& door) {
    Decision d;
    const auto& local = now.local;
    const std::string where = place.scene + " at " + Where(place.at);
    if (place.age && local.age && *place.age != *local.age) {
        d.log = "resume: the last place (" + where + ") was kept with the other age: staying here";
        return d;
    }
    if (const auto age = CoopTeleport::AgeOfScene(place.scene); age && local.age && *age != *local.age) {
        d.log = "resume: the last place (" + where + ") is a scene of the other age: staying here";
        return d;
    }
    const CoopBoss::Room* room = CoopBoss::RoomAt(place.scene, CoopBoss::Vec{place.at.x, place.at.y, place.at.z});
    const bool safe = place.kind == Kind::ok && room == nullptr;
    const std::string why = place.kind == Kind::event ? "it was kept during a cutscene or a talk"
                                                      : "it is in a boss room";
    const bool same = local.scene && *local.scene == place.scene;
    if (same) {
        if (!safe) {
            d.log = "resume: the last place (" + where + ") is not a place to go back to (" + why +
                    "): staying at this scene's entrance";
            return d;
        }
        if (local.position && Distance(*local.position, place.at) < Returner::kThereUnits) {
            d.log = "resume: already at the last place (" + where + ")";
            return d;
        }
        if (now.rooms != nullptr && local.position) {
            const auto check = CoopTeleport::CheckMove(*now.rooms, place.scene, *local.position, place.at);
            if (check != CoopTeleport::MoveCheck::ok) {
                d.door = true;
                const Door* to = DoorFor(door, place, local.age);
                if (to != nullptr &&
                    CoopTeleport::CheckMove(*now.rooms, place.scene, *local.position, to->at) ==
                        CoopTeleport::MoveCheck::ok) {
                    if (Distance(*local.position, to->at) < Returner::kThereUnits) {
                        d.log = "resume: the last place (" + where + ") is in " + MoveCheckText(check) + " (" +
                                kWhy197 + "): already at the spot of this room we last left it by";
                        return d;
                    }
                    d.what = Decision::What::move;
                    d.at = to->at;
                    d.log = "resume: the last place (" + where + ") is in " + MoveCheckText(check) + " (" + kWhy197 +
                            "): to the spot of this room we last left it by, " + Where(to->at) + " (same scene)";
                    d.notice = "De vuelta a la sala por donde saliste: tu sitio está en otra sala";
                    return d;
                }
                d.log = "resume: the last place (" + where + ") is in " + MoveCheckText(check) + " (" + kWhy197 +
                        "): no spot of this room kept, staying here";
                return d;
            }
        }
        d.what = Decision::What::move;
        d.at = place.at;
        d.log = "resume: back to the last place, " + where + " (same scene)";
        d.notice = "De vuelta donde lo dejaste";
        return d;
    }
    const auto start = CoopTeleport::NearestStart(place.scene, place.at);
    if (!start) {
        d.log = "resume: the last place (" + where + ") is in a scene with no start point known: staying here";
        return d;
    }
    d.what = Decision::What::travel;
    d.scene = place.scene;
    d.start = *start;
    d.at = place.at;
    d.place = safe;
    if (safe && now.rooms != nullptr) {
        // Issue 197: the load lands at the start point; the last place is
        // reached only when it is in that start's room.
        const auto from = CoopTeleport::StartPosition(place.scene, *start);
        const auto check = from ? CoopTeleport::CheckMove(*now.rooms, place.scene, *from, place.at)
                                : CoopTeleport::MoveCheck::too_far;
        if (check != CoopTeleport::MoveCheck::ok) {
            d.door = true;
            const Door* to = DoorFor(door, place, local.age);
            const auto door_from = to != nullptr ? CoopTeleport::StartPosition(place.scene, to->start) : std::nullopt;
            if (to != nullptr && door_from &&
                CoopTeleport::CheckMove(*now.rooms, place.scene, *door_from, to->at) == CoopTeleport::MoveCheck::ok) {
                d.start = to->start;
                d.at = to->at;
                d.log = "resume: the last place (" + where + ") is in " + MoveCheckText(check) + " (" + kWhy197 +
                        "): going to the spot of its entrance room we last left it by, " + Where(to->at) +
                        " (start point " + to->start + ")";
                return d;
            }
            d.place = false;
            d.log = "resume: the last place (" + where + ") is in " + MoveCheckText(check) + " (" + kWhy197 +
                    "): no spot of its entrance room kept, going to its entrance (" + *start + ")";
            return d;
        }
    }
    d.log = safe ? "resume: going back to the last place, " + where + " (start point " + *start + ")"
                 : "resume: the last place (" + where + ") is not a place to go back to (" + why +
                       "): going to the entrance of " + place.scene + " (" + *start + ")";
    return d;
}

std::optional<Decision> Returner::Frame(const Now& now, std::uint64_t now_us) {
    if (done_) {
        return std::nullopt;
    }
    if (!place_) {
        done_ = true;
        Decision d;
        d.log = "resume: no last place kept for this player in this room";
        return d;
    }
    const auto& local = now.local;
    const bool read = local.scene.has_value() && local.position.has_value();
    if (read && !seen_) {
        seen_ = true;
        first_us_ = now_us;
    }
    const bool asked = local.reserved_banc && !local.reserved_banc->empty();
    const bool ready = read && !local.event && !local.loading_banc && !local.fighting && !asked;
    if (!ready) {
        steady_ = false;
    } else if (!steady_) {
        steady_ = true;
        steady_us_ = now_us;
    }
    if (steady_ && now_us - steady_us_ >= kSteadyUs) {
        done_ = true;
        return Decide(*place_, now, door_);
    }
    if (seen_ && now_us - first_us_ > kWindowUs) {
        done_ = true;
        Decision d;
        d.log = "resume: our Link never stood still after the load: no way back to the last place";
        return d;
    }
    return std::nullopt;
}

} // namespace CoopResume
