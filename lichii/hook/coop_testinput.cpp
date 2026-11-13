// Scripted controller input for automatic tests; see coop_testinput.h.
// The parser and the stepper have no emulator code (the tests build them
// alone); the part that drives the emulated pad is only built with LICHII_COOP.
#include "coop_testinput.h"

#include "coop_age_game.h"
#include "orm_story_events.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace CoopTestInput {

namespace {

constexpr std::uint32_t kPressTicks = 6;
constexpr std::uint32_t kMaxStepTicks = 60 * 60 * 10;  // 10 minutes

std::vector<std::string_view> Words(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i > start) {
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

std::string Upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

bool Ticks(std::string_view word, std::uint32_t& out) {
    if (word.empty() || word.size() > 9) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char c : word) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value == 0 || value > kMaxStepTicks) {
        return false;
    }
    out = value;
    return true;
}

bool Axis(std::string_view word, std::int32_t& out) {
    const std::string s(word);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(v) || v < -1.0 || v > 1.0) {
        return false;
    }
    out = static_cast<std::int32_t>(std::lround(v * kStickMax));
    return true;
}

// Adds `word` ("A+LSTICK_UP") to the step. False on an unknown name.
bool Keys(std::string_view word, Step& step, std::string& bad) {
    struct Name {
        const char* name;
        std::uint64_t button;
    };
    static constexpr Name kNames[] = {
        {"A", kA},          {"B", kB},           {"X", kX},           {"Y", kY},
        {"L", kL},          {"R", kR},           {"ZL", kZL},         {"ZR", kZR},
        {"PLUS", kPlus},    {"MINUS", kMinus},   {"LCLICK", kLClick}, {"RCLICK", kRClick},
        {"DUP", kDUp},      {"DDOWN", kDDown},   {"DLEFT", kDLeft},   {"DRIGHT", kDRight},
    };
    std::size_t start = 0;
    while (start <= word.size()) {
        std::size_t end = word.find('+', start);
        if (end == std::string_view::npos) {
            end = word.size();
        }
        const std::string key = Upper(word.substr(start, end - start));
        start = end + 1;
        bool found = false;
        for (const Name& n : kNames) {
            if (key == n.name) {
                step.buttons |= n.button;
                found = true;
                break;
            }
        }
        if (!found && (key.rfind("LSTICK_", 0) == 0 || key.rfind("RSTICK_", 0) == 0)) {
            auto& stick = key[0] == 'L' ? step.left : step.right;
            if (!stick) {
                stick = Stick{};
            }
            const std::string dir = key.substr(7);
            found = true;
            if (dir == "UP") {
                stick->y = kStickMax;
            } else if (dir == "DOWN") {
                stick->y = -kStickMax;
            } else if (dir == "LEFT") {
                stick->x = -kStickMax;
            } else if (dir == "RIGHT") {
                stick->x = kStickMax;
            } else {
                found = false;
            }
        }
        if (!found) {
            bad = key;
            return false;
        }
    }
    return true;
}

bool Number(std::string_view word, float& out) {
    const std::string s(word);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(v) || std::fabs(v) > 1e6) {
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

constexpr double kPi = 3.14159265358979323846;

double Wrap(double a) {
    while (a > kPi) {
        a -= 2 * kPi;
    }
    while (a < -kPi) {
        a += 2 * kPi;
    }
    return a;
}

} // namespace

// wear: the PlayerStatus flag of each gear slot (docs/items.md), or nothing.
std::optional<std::string> WearFlag(std::string_view slot) {
    std::string low(slot);
    for (auto& ch : low) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (low == "weapon") return "EquipWeapon";
    if (low == "shield") return "EquipShield";
    if (low == "head") return "EquipHeadArmor";
    if (low == "armor") return "EquipArmor";
    if (low == "boots") return "EquipBoot";
    if (low == "hand") return "EquipHandArmor";
    return std::nullopt;
}

// assign: the values of PlayerStatus.AssignableItem.Child / .Adult (Enum,
// GameDataList of build e0f3ccfaf50065ae).
bool AssignableItem(std::string_view name) {
    static constexpr std::string_view kValues[] = {
        "Empty", "DekuStick", "DekuNut", "Bomb", "Boomerang", "SlingShot", "Hookshot",
        "Longshot", "Hammer", "GlassTruth", "Bombchu", "DinFire", "FaroreWind", "NayruLove",
        "MagicBean", "BottleA", "BottleB", "BottleC", "BottleD", "FishingRod", "Lure",
        "KeatonMask", "SkullMask", "SpookyMask", "BunnyMask", "TruthMask", "GoronMask",
        "ZoraMask", "GerudoMask", "HeavyBoot", "HoverBoot", "Bow", "WeirdEgg", "AlarmCucco",
        "ZeldaLetter", "PocketEgg", "PocketCucco", "CojiroCucco", "OddMushroom", "OddPowder",
        "PoacherSaw", "BrokenSword", "Prescription", "EyeballFrog", "EyeDrop", "ClaimCheck",
        "SheikahWarp", "FireArrow", "IceArrow", "LightArrow", "ScrubBusinessSeed", "BottleE",
        "Amiibo", "SinkLure", "NormalLure"};
    for (const std::string_view v : kValues) {
        if (v == name) return true;
    }
    return false;
}

bool WarpStartsEvent(double now_s, double asked_s, double last_event_s, int events, bool link_read,
                     bool event_running) {
    if (!link_read || event_running || events >= kWarpEvents || now_s - asked_s < kWarpEventAfterS) {
        return false;
    }
    return last_event_s < 0.0 || now_s - last_event_s >= kWarpEventEveryS;
}

// bottle: the game's BottleItem value names (docs/items.md).
std::optional<std::string> BottleValue(std::string_view word) {
    static constexpr const char* kNames[] = {
        "Empty", "RedPotion", "GreenPotion", "BluePotion", "BlueFire", "Fish",   "Letter",
        "BigPoe", "Poe",      "Bug",         "Fairy",      "MaxMilk",  "HalfMilk", "restore"};
    const std::string want = Upper(word);
    for (const char* name : kNames) {
        if (want == Upper(name)) {
            return std::string(name);
        }
    }
    return std::nullopt;
}

namespace {

constexpr std::uint32_t kGotoTicks = 600;  // 10 s
constexpr float kMovedM = 0.4f;            // movement that teaches the camera model
constexpr double kMinMagnitude = 0.45;     // slower near the point, never a crawl

} // namespace

std::optional<Script> Parse(std::string_view text, std::string& error) {
    Script script;
    int number = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++number;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        const auto w = Words(line);
        if (w.empty()) {
            continue;
        }
        const std::string cmd = Upper(w[0]);
        const auto fail = [&](const std::string& why) {
            error = "line " + std::to_string(number) + ": " + why;
            return std::nullopt;
        };
        Step step;
        step.line = number;
        std::string bad;
        if (cmd == "WAIT") {
            if (w.size() != 2 || !Ticks(w[1], step.ticks)) {
                return fail("want: wait TICKS");
            }
        } else if (cmd == "HOLD") {
            // An optional mark is said halfway through the hold, while the
            // keys are still down (an item aimed with ZR is put away when it
            // lets go, so a mark after the hold sees nothing in the hand).
            if (w.size() < 3 || w.size() > 4 || !Ticks(w[2], step.ticks)) {
                return fail("want: hold KEYS TICKS [MARK]");
            }
            if (w.size() == 4) {
                step.mark = std::string(w[3]);
            }
            if (!Keys(w[1], step, bad)) {
                return fail("unknown key " + bad);
            }
        } else if (cmd == "PRESS") {
            std::uint32_t ticks = kPressTicks;
            if (w.size() < 2 || w.size() > 3 || (w.size() == 3 && !Ticks(w[2], ticks))) {
                return fail("want: press KEYS [TICKS]");
            }
            if (!Keys(w[1], step, bad)) {
                return fail("unknown key " + bad);
            }
            step.ticks = ticks;
            script.steps.push_back(step);
            Step release;
            release.line = number;
            release.ticks = ticks;
            step = release;
        } else if (cmd == "STICK" || cmd == "RSTICK") {
            Stick s;
            if (w.size() < 4 || w.size() > 5 || !Axis(w[1], s.x) || !Axis(w[2], s.y) ||
                !Ticks(w[3], step.ticks)) {
                return fail("want: " + Upper(w[0]) + " X Y TICKS [KEYS], X and Y from -1 to 1");
            }
            if (w.size() == 5 && !Keys(w[4], step, bad)) {
                return fail("unknown key " + bad);
            }
            (cmd == "STICK" ? step.left : step.right) = s;
        } else if (cmd == "GOTO") {
            step.kind = Kind::go;
            step.ticks = kGotoTicks;
            if (w.size() < 3 || w.size() > 5 || !Number(w[1], step.x) || !Number(w[2], step.z) ||
                (w.size() >= 4 && (!Number(w[3], step.radius) || step.radius < 0.2f)) ||
                (w.size() == 5 && !Ticks(w[4], step.ticks))) {
                return fail("want: goto X Z [RADIUS] [TICKS], radius at least 0.2");
            }
        } else if (cmd == "TOWARD") {
            step.kind = Kind::toward;
            if (w.size() < 4 || w.size() > 5 || !Number(w[1], step.x) || !Number(w[2], step.z) ||
                !Ticks(w[3], step.ticks)) {
                return fail("want: toward X Z TICKS [KEYS]");
            }
            if (w.size() == 5 && !Keys(w[4], step, bad)) {
                return fail("unknown key " + bad);
            }
        } else if (cmd == "WARP") {
            // ORM: a last word "adult" or "child" (any case) asks that age, for a
            // scene with no era in its name (a dungeon): warp WaterTemple
            // WaterTemple_FromLakeHylia adult. A scene named X_Adult or X_Child
            // already says it.
            auto words = w.size();
            std::optional<bool> child;
            if (words >= 3) {
                std::string last(w[words - 1]);
                std::transform(last.begin(), last.end(), last.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (last == "adult" || last == "child") {
                    child = last == "child";
                    --words;
                }
            }
            if (words < 2 || words > 3) {
                return fail("want: warp SCENE [START] [adult|child]");
            }
            step.kind = Kind::warp;
            step.mark = std::string(w[1]);
            step.child = child;
            if (words == 3) {
                step.start = std::string(w[2]);
            }
        } else if (cmd == "PLACE") {
            step.kind = Kind::place;
            if (w.size() != 4 || !Number(w[1], step.x) || !Number(w[2], step.y) || !Number(w[3], step.z)) {
                return fail("want: place X Y Z");
            }
        } else if (cmd == "CHAT") {
            if (w.size() < 2) {
                return fail("want: chat TEXT");
            }
            step.kind = Kind::chat;
            const auto from = static_cast<std::size_t>(w[1].data() - line.data());
            const auto to = static_cast<std::size_t>(w.back().data() + w.back().size() - line.data());
            step.mark = std::string(line.substr(from, to - from));
        } else if (cmd == "KILL") {
            if (w.size() != 2) {
                return fail("want: kill ACTOR");
            }
            step.kind = Kind::kill;
            step.mark = std::string(w[1]);
        } else if (cmd == "GIVE") {
            if (w.size() != 2) {
                return fail("want: give ITEM");
            }
            step.kind = Kind::give;
            step.mark = std::string(w[1]);
        } else if (cmd == "SETFLAG") {
            // setflag NAME VALUE: this game's Int or Bool flag NAME (a GameDataList
            // name) to VALUE, through the game's queue. Test only.
            int value = 0;
            const auto* end = w.size() == 3 ? w[2].data() + w[2].size() : nullptr;
            if (w.size() != 3 || std::from_chars(w[2].data(), end, value).ptr != end) {
                return fail("want: setflag NAME VALUE (an Int or Bool flag, a whole number)");
            }
            step.kind = Kind::setflag;
            step.mark = std::string(w[1]);
            step.start = std::string(w[2]);
        } else if (cmd == "SETENUM") {
            // setenum NAME VALUE: this game's Enum flag NAME (a GameDataList name,
            // or 0x and its hash: a Threads of Time quest's name is not in the
            // romfs) to the value named VALUE (flag_hash), through the game's
            // queue. Test only (issue 193).
            if (w.size() != 3) {
                return fail("want: setenum NAME VALUE (an Enum flag, or 0xHASH, and a value's name)");
            }
            step.kind = Kind::setenum;
            step.mark = std::string(w[1]);
            step.start = std::string(w[2]);
        } else if (cmd == "WEAR") {
            if (w.size() != 3 || !WearFlag(w[1])) {
                return fail("want: wear weapon|shield|head|armor|boots|hand NAME");
            }
            step.kind = Kind::wear;
            step.start = std::string(w[1]);
            step.mark = std::string(w[2]);
        } else if (cmd == "ASSIGN") {
            if (w.size() != 2 || !AssignableItem(w[1])) {
                return fail("want: assign ITEM (Bow, Hookshot, Hammer, SlingShot, Bomb, ...: "
                            "the item button enum's names)");
            }
            step.kind = Kind::assign;
            step.mark = std::string(w[1]);
        } else if (cmd == "LIFE") {
            const bool digits =
                w.size() == 2 && !w[1].empty() && w[1].size() <= 3 &&
                std::all_of(w[1].begin(), w[1].end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!digits) {
                return fail("want: life N, N quarter hearts from 0 to 999");
            }
            step.kind = Kind::life;
            step.mark = std::string(w[1]);
        } else if (cmd == "AWAIT") {
            // ORM: "await mate [TICKS]": the two games start a scenario up to two minutes apart (the harness
            // staggers the second; hot instances 20 s): a step that needs the other there waits for him.
            if (w.size() < 2 || w.size() > 3 || w[1] != "mate" && w[1] != "MATE") {
                return fail("want: await mate [TICKS] (default 9000, 150 s)");
            }
            step.ticks = 9000;
            if (w.size() == 3 && !Ticks(w[2], step.ticks)) {
                return fail("want: await mate [TICKS] (default 9000, 150 s)");
            }
            step.kind = Kind::await_mate;
        } else if (cmd == "TALKTHROUGH") {
            // ORM: "talkthrough [TICKS]": a talk's text, page by page: A pressed every 0.75 s while our
            // event runs, over once it has stopped for 1 s (the remake's Kokiri talks took more pages than
            // a script's fixed presses, 2026-11-12, docs/SONDAS.md). Default 1800 ticks (30 s).
            step.ticks = 1800;
            if (w.size() > 2 || (w.size() == 2 && !Ticks(w[1], step.ticks))) {
                return fail("want: talkthrough [TICKS] (default 1800, 30 s)");
            }
            step.kind = Kind::talk_through;
        } else if (cmd == "GATE") {
            if (w.size() != 2 || (w[1] != "0" && w[1] != "1")) {
                return fail("want: gate 0|1 (0 open, 1 closed)");
            }
            step.kind = Kind::gate;
            step.mark = std::string(w[1]);
        } else if (cmd == "BOTTLE") {
            const std::string slot = w.size() == 3 ? Upper(w[1]) : std::string();
            if (w.size() != 3 || slot.size() != 1 || slot[0] < 'A' || slot[0] > 'E' ||
                !BottleValue(w[2])) {
                return fail("want: bottle A-E VALUE|restore, VALUE a BottleItem name (Fairy, Empty...)");
            }
            step.kind = Kind::bottle;
            step.mark = slot;
            step.start = *BottleValue(w[2]);
        } else if (cmd == "EVENT") {
            if (w.size() != 2 || !OrmStoryEvents::ValidName(w[1])) {
                return fail("want: event NAME, 1 to 127 of A-Z a-z 0-9 _");
            }
            step.kind = Kind::event;
            step.mark = std::string(w[1]);
        } else if (cmd == "FLAG") {
            const bool printable =
                w.size() == 3 && !w[1].empty() && w[1].size() <= 160 &&
                std::all_of(w[1].begin(), w[1].end(), [](char c) { return c > 0x20 && c < 0x7f; });
            if (!printable || (w[2] != "0" && w[2] != "1")) {
                return fail("want: flag NAME|0xHASH 0|1");
            }
            step.kind = Kind::flag;
            step.mark = std::string(w[1]);
            step.start = std::string(w[2]);
        } else if (cmd == "INT") {
            // int NAME|0xHASH VALUE: an Int flag (PlayerStatus.MP, a bombchu
            // count), decimal, -1000000 to 1000000.
            const bool printable =
                w.size() == 3 && !w[1].empty() && w[1].size() <= 160 &&
                std::all_of(w[1].begin(), w[1].end(), [](char c) { return c > 0x20 && c < 0x7f; });
            const std::string_view v = w.size() == 3 ? w[2] : std::string_view{};
            const std::string_view digits = !v.empty() && v[0] == '-' ? v.substr(1) : v;
            const bool is_number = !digits.empty() && digits.size() <= 7 &&
                                std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!printable || !is_number || std::stol(std::string(digits)) > 1000000) {
                return fail("want: int NAME|0xHASH VALUE (decimal, -1000000 to 1000000)");
            }
            step.kind = Kind::int_flag;
            step.mark = std::string(w[1]);
            step.start = std::string(v);
        } else if (cmd == "KEY") {
            // ORM: 0x and 1 to 16 hex digits, then 0 or 1.
            const bool hex = w.size() == 3 && w[1].size() > 2 && w[1].size() <= 18 &&
                             w[1][0] == '0' && (w[1][1] == 'x' || w[1][1] == 'X') &&
                             std::all_of(w[1].begin() + 2, w[1].end(), [](char c) {
                                 return std::isxdigit(static_cast<unsigned char>(c)) != 0;
                             });
            if (!hex || (w[2] != "0" && w[2] != "1")) {
                return fail("want: key 0xKEY 0|1 (1 to 16 hex digits)");
            }
            step.kind = Kind::key64;
            step.mark = std::string(w[1]);
            step.start = std::string(w[2]);
        } else if (cmd == "ENUM") {
            const auto printable = [](std::string_view word) {
                return !word.empty() && word.size() <= 160 &&
                       std::all_of(word.begin(), word.end(), [](char c) { return c > 0x20 && c < 0x7f; });
            };
            if ((w.size() != 2 && w.size() != 3) || !printable(w[1]) || (w.size() == 3 && !printable(w[2]))) {
                return fail("want: enum NAME|0xHASH [VALUE|0xHASH]");
            }
            step.kind = Kind::enum_flag;
            step.mark = std::string(w[1]);
            step.start = w.size() == 3 ? std::string(w[2]) : std::string();
        } else if (cmd == "SKIP") {
            if (w.size() != 1) {
                return fail("want: skip (no more words)");
            }
            step.kind = Kind::skip;
        } else if (cmd == "AGE") {
            const std::string age = w.size() == 2 ? Upper(w[1]) : std::string();
            if (age != "ADULT" && age != "CHILD") {
                return fail("want: age adult|child");
            }
            step.kind = Kind::age;
            step.mark = age == "CHILD" ? "child" : "adult";
        } else if (cmd == "MARK") {
            if (w.size() != 2) {
                return fail("want: mark NAME (one word)");
            }
            step.kind = Kind::mark;
            step.mark = std::string(w[1]);
        } else {
            return fail("unknown command " + std::string(w[0]));
        }
        script.steps.push_back(std::move(step));
    }
    if (script.steps.empty()) {
        error = "no steps";
        return std::nullopt;
    }
    return script;
}

Runner::Runner(Script script) : script_(std::move(script)) {}

bool Runner::Steering() const {
    return index_ < script_.steps.size() && (script_.steps[index_].kind == Kind::go ||
                                             script_.steps[index_].kind == Kind::toward);
}

// The left stick now points at `stick_angle` (none: centred or not ours).
// A new direction starts a new window to learn from.
void Runner::Hold(std::optional<double> stick_angle, double now_s) {
    const bool same = stick_angle && stick_angle_ &&
                      std::fabs(Wrap(*stick_angle - *stick_angle_)) < 0.2;
    if (!same) {
        stick_since_ = now_s;
        window_pos_.reset();
    }
    stick_angle_ = stick_angle;
}

// Learns the camera from how Link moved while the stick held one direction:
// from kSettleS after it took that direction, every 0.4 m walked.
void Runner::Learn(double now_s, std::optional<std::array<float, 2>> player) {
    if (player && prev_player_ &&
        std::hypot((*player)[0] - (*prev_player_)[0], (*player)[1] - (*prev_player_)[1]) > 3.0f) {
        // A jump: a new scene or a warp, with a new camera. Learn again.
        heading_.reset();
        mirror_known_ = false;
        mirror_ = 1.0;
        window_pos_.reset();
    }
    if (player) {
        prev_player_ = player;
    }
    if (!player || !stick_angle_ || now_s - stick_since_ < kSettleS) {
        return;
    }
    if (!window_pos_) {
        window_pos_ = player;
        return;
    }
    const float dx = (*player)[0] - (*window_pos_)[0];
    const float dz = (*player)[1] - (*window_pos_)[1];
    if (std::hypot(dx, dz) < kMovedM) {
        return;
    }
    const double seen = std::atan2(dz, dx);
    const double stick = *stick_angle_ - kPi / 2;
    if (heading_ && !mirror_known_) {
        // The probe: stick off up (right first). Unmirrored, Link walks the
        // stick's turn from heading; mirrored, the same turn the other way.
        if (std::fabs(stick) > 0.7) {  // any stick well off up tells the two apart
            const double plain = std::fabs(Wrap(seen - (*heading_ + stick)));
            const double flipped = std::fabs(Wrap(seen - (*heading_ - stick)));
            mirror_ = plain <= flipped ? 1.0 : -1.0;
            mirror_known_ = true;
        }
        window_pos_ = player;
        return;
    }
    const double fresh = Wrap(seen - mirror_ * stick);
    heading_ = heading_ ? Wrap(*heading_ + 0.5 * Wrap(fresh - *heading_)) : fresh;
    window_pos_ = player;
}

void Runner::Advance(double now_s, std::optional<std::array<float, 2>> player,
                     std::vector<std::string>& log, std::optional<Camera> camera, bool mate_here,
                     std::optional<bool> event) {
    camera_ = camera;
    Learn(now_s, player);
    while (index_ < script_.steps.size()) {
        const Step& s = script_.steps[index_];
        const double length = static_cast<double>(s.ticks) / kTicksPerSecond;
        if (s.kind == Kind::talk_through) {
            const double in_step = now_s - step_start_;
            const bool running = event.value_or(false);
            if (running) {
                talk_seen_ = true;
                talk_free_since_ = -1.0;
            } else if (talk_free_since_ < 0.0) {
                talk_free_since_ = now_s;
            }
            const bool over = talk_seen_ ? !running && now_s - talk_free_since_ >= kTalkFreeS
                                         : in_step >= kTalkNoneS;
            if (over || in_step >= length) {
                char text[128];
                std::snprintf(text, sizeof text, "talkthrough: %s after %.1f s, %d presses",
                              over ? (talk_seen_ ? "event over" : "no event") : "timed out (event still on)",
                              in_step, talk_presses_);
                log.push_back(text);
                ++index_;
                step_start_ = now_s;
                talk_seen_ = false;
                talk_free_since_ = -1.0;
                talk_last_press_ = -1.0;
                talk_presses_ = 0;
                continue;
            }
            out_ = Output{};
            if (running && (talk_last_press_ < 0.0 || now_s - talk_last_press_ >= kTalkEveryS)) {
                talk_last_press_ = now_s;
                ++talk_presses_;
            }
            if (running && talk_last_press_ >= 0.0 && now_s - talk_last_press_ < kTalkPressS) {
                out_.buttons = kA;
            }
            Hold(std::nullopt, now_s);
            return;
        }
        if (s.kind == Kind::await_mate) {
            const double waited = now_s - step_start_;
            if (mate_here || waited >= length) {
                char text[96];
                std::snprintf(text, sizeof text, "await mate: %s after %.1f s",
                              mate_here ? "here" : "timed out", waited);
                log.push_back(text);
                ++index_;
                step_start_ = now_s;
                continue;
            }
            out_ = Output{};
            Hold(std::nullopt, now_s);
            return;
        }
        if (s.kind == Kind::mark) {
            log.push_back("mark " + s.mark);
            ++index_;  // the next step starts when this one did
            continue;
        }
        if (s.kind == Kind::warp || s.kind == Kind::give || s.kind == Kind::wear ||
            s.kind == Kind::assign || s.kind == Kind::chat || s.kind == Kind::kill ||
            s.kind == Kind::setflag || s.kind == Kind::setenum ||
            s.kind == Kind::life || s.kind == Kind::gate || s.kind == Kind::bottle ||  // ORM from here
            s.kind == Kind::event || s.kind == Kind::flag || s.kind == Kind::int_flag ||
            s.kind == Kind::enum_flag || s.kind == Kind::age || s.kind == Kind::key64 ||
            s.kind == Kind::skip) {
            requests_.push_back(Request{s.kind, s.mark, s.start, {}, s.child});
            ++index_;
            continue;
        }
        if (s.kind == Kind::place) {
            requests_.push_back(Request{s.kind, {}, {}, {s.x, s.y, s.z}});
            ++index_;
            continue;
        }
        if (s.kind == Kind::go) {
            const bool there =
                player && std::hypot(s.x - (*player)[0], s.z - (*player)[1]) <= s.radius;
            if (there || now_s - step_start_ >= length) {
                if (!there) {
                    log.push_back("goto timed out at line " + std::to_string(s.line));
                }
                ++index_;
                step_start_ = now_s;
                aimed_at_ = -1.0;
                progress_pos_.reset();
                continue;
            }
            Steer(s, now_s, player);
            return;
        }
        if (s.kind == Kind::timed && !s.mark.empty() && marked_index_ != index_ &&
            now_s - step_start_ >= length / 2) {
            log.push_back("mark " + s.mark);  // hold KEYS TICKS MARK: halfway
            marked_index_ = index_;
        }
        if (now_s - step_start_ >= length) {
            ++index_;
            step_start_ += length;  // timed steps keep exact time between them
            aimed_at_ = -1.0;
            continue;
        }
        if (s.kind == Kind::toward) {
            Steer(s, now_s, player);
            return;
        }
        out_ = Output{s.buttons, s.left, s.right};
        std::optional<double> angle;
        if (s.left && (s.left->x != 0 || s.left->y != 0)) {
            angle = std::atan2(static_cast<double>(s.left->y), s.left->x);
        }
        Hold(angle, now_s);
        return;
    }
    out_ = Output{};
    Hold(std::nullopt, now_s);
}

void Runner::Steer(const Step& step, double now_s, std::optional<std::array<float, 2>> player) {
    out_.buttons = step.buttons;
    out_.right.reset();
    if (!player) {
        out_.left = Stick{0, 0};
        Hold(std::nullopt, now_s);
        return;
    }
    // Keep the last aim for a while so Link walks one way long enough to
    // learn from; a toward step aims once.
    const double dist = std::hypot(step.x - (*player)[0], step.z - (*player)[1]);
    // With the camera given, aim every frame (it turns as Link walks); else
    // keep an aim long enough to learn from, shorter when close.
    const double every = camera_ ? 0.0 : (dist > 2.5 ? kAimEveryS : 0.1);
    const bool keep = aimed_at_ >= 0.0 && out_.left && now_s >= detour_until_ &&
                      (step.kind == Kind::toward || now_s - aimed_at_ < every);
    if (keep) {
        return;
    }
    // Stuck (a wall, the other player's puppet): no progress for kStuckS
    // turns the push a quarter turn, the other side each time, for a moment.
    if (!progress_pos_ || std::hypot((*player)[0] - (*progress_pos_)[0],
                                     (*player)[1] - (*progress_pos_)[1]) >= 0.3f) {
        progress_pos_ = player;
        progress_at_ = now_s;
    } else if (now_s - progress_at_ >= kStuckS) {
        detour_until_ = now_s + kDetourS;
        detour_side_ = -detour_side_;
        progress_pos_ = player;
        progress_at_ = now_s;
    }
    const double detour = now_s < detour_until_ ? detour_side_ * kPi / 2 : 0.0;
    if (camera_) {
        // Straight from the camera: the stick that walks Link toward the point.
        double wx = step.x - (*player)[0];
        double wz = step.z - (*player)[1];
        const double len = std::hypot(wx, wz);
        if (len > 1e-6) {
            wx /= len;
            wz /= len;
        }
        double sx = wx * camera_->right_x + wz * camera_->right_z;
        double sy = wx * camera_->forward_x + wz * camera_->forward_z;
        double angle = std::atan2(sy, sx) + detour;
        const double magnitude = (step.kind == Kind::go && detour == 0.0)
                                     ? std::clamp(dist / 2.0, kMinMagnitude, 1.0)
                                     : 1.0;
        out_.left = Stick{
            static_cast<std::int32_t>(std::lround(std::cos(angle) * magnitude * kStickMax)),
            static_cast<std::int32_t>(std::lround(std::sin(angle) * magnitude * kStickMax))};
        aimed_at_ = now_s;
        Hold(angle, now_s);
        return;
    }
    // No camera model yet: stick up, then stick right once, and learn from both.
    double angle = (!heading_ ? kPi / 2 : 0.0) + detour;
    double magnitude = 1.0;
    if (heading_ && mirror_known_) {
        const double want = std::atan2(step.z - (*player)[1], step.x - (*player)[0]);
        angle = kPi / 2 + mirror_ * Wrap(want - *heading_) + detour;
        if (step.kind == Kind::go && detour == 0.0) {
            magnitude = std::clamp(dist / 2.0, kMinMagnitude, 1.0);
        }
    }
    out_.left = Stick{static_cast<std::int32_t>(std::lround(std::cos(angle) * magnitude * kStickMax)),
                      static_cast<std::int32_t>(std::lround(std::sin(angle) * magnitude * kStickMax))};
    aimed_at_ = now_s;
    Hold(angle, now_s);
}

} // namespace CoopTestInput

#ifdef LICHII_COOP

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#include "common/logging.h"
#include "coop_switch.h"
#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/host/story.hpp"
#include "coop_cheats.h"
#include "coop_downed.h"
#include "coop_every.h"
#include "coop_boss.h"
#include "coop_camera.h"
#include "coop_downed_game.h"
#include "coop_flags.h"
#include "coop_hook.h"
#include "coop_objects.h"
#include "coop_teleport_game.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#ifndef COOP_HOOKSHOT
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#endif

namespace CoopTestInput {

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kWarpGiveUpS = 90.0;

// What the injector gives the pad: written on the present thread, read on
// the input thread.
struct Shared {
    std::mutex mutex;
    Output out;
};

struct Running {
    std::unique_ptr<Runner> runner;
    std::shared_ptr<Shared> shared;
    Clock::time_point start;
#ifndef COOP_HOOKSHOT
    Core::HID::EmulatedController* controller = nullptr;
#endif
};

struct State {
    bool checked_env = false;
    std::optional<std::array<float, 3>> link;  // our Link's place this frame (x, height, z)
    std::filesystem::path path;  // empty: off
    std::filesystem::file_time_type seen_time{};
    std::uintmax_t seen_size = 0;
    bool seen = false;
    CoopEvery::Every check{CoopEvery::kQuarterUs};
    Kernel::KProcess* process = nullptr;
    u64 main = 0;
    std::optional<Running> running;
    struct PendingWarp {
        std::string scene;
        std::string start;
        double since = 0.0;
        double asked = 0.0;
        std::optional<std::array<float, 2>> from;  // Link before the warp
        std::optional<bool> child;                  // ORM: the age asked (warp ... adult|child)
        // ORM, a warp in free play (WarpStartsEvent): the scene events started for it, when the
        // last one was, whether an event of the game ran since, and the "off" line said once.
        int events = 0;
        double event_at = -1.0;
        bool event_seen = false;
        bool told_off = false;
    };
    std::optional<PendingWarp> warp;
    // A `place` whose Link was not found (it happens a frame after a load or an event: the
    // probes of 2026-11-12 lost the step onto the switch; levantar-vasija placed 2 s after a load): tried
    // again every 0.1 s, 10 s at most.
    struct PendingPlace {
        std::array<float, 3> at{};
        double next = 0.0;
        int tries = 0;
    };
    std::optional<PendingPlace> place;
#ifndef COOP_HOOKSHOT
    // Nudge's own injector, when no script runs (a script's injector adds the
    // nudge itself).
    Core::HID::EmulatedController* nudge_controller = nullptr;
#endif
} g_state;  // only touched on the present thread

// The emulator plays LICHII_COOP_TEST_SCRIPT whole, input and all. Hookshot's
// driver (tools/autotest/hookshot.py) gives the input through Hookshot's
// control bridge and writes only the steps that need the game's inside (place,
// give, assign, chat, setflag, setenum, wear, kill) to LICHII_COOP_TEST_COMMANDS,
// a few lines at a time; the module plays them at once and empties the file,
// so the driver knows they were taken.
#ifdef COOP_HOOKSHOT
#define COOP_TEST_ENV "LICHII_COOP_TEST_COMMANDS"
#else
#define COOP_TEST_ENV "LICHII_COOP_TEST_SCRIPT"
#endif
#define COOP_WIDEN2(x) L##x
#define COOP_WIDEN(x) COOP_WIDEN2(x)

// The nudge's right stick (Nudge): written on the present thread, read on the
// input thread by either injector.
struct NudgeShared {
    std::mutex mutex;
    std::optional<Stick> right;
    int reads = 0;  // pad reads that got it, since it was last set
} g_nudge;

std::optional<Stick> NudgeNow() {
    std::lock_guard lock(g_nudge.mutex);
    if (g_nudge.right) {
        ++g_nudge.reads;
    }
    return g_nudge.right;
}

std::filesystem::path EnvPath() {
#ifdef _MSC_VER
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, COOP_WIDEN(COOP_TEST_ENV)) != 0 || value == nullptr) {
        return {};
    }
    std::filesystem::path p(value);
    std::free(value);
    return p;
#else
    const char* value = std::getenv(COOP_TEST_ENV);
    return value ? std::filesystem::path(value) : std::filesystem::path{};
#endif
}

#ifndef COOP_HOOKSHOT
Core::HID::EmulatedController* PickController(Core::System& system) {
    auto& hid = system.HIDCore();
    auto* player1 = hid.GetEmulatedController(Core::HID::NpadIdType::Player1);
    if (player1 != nullptr && player1->IsConnected()) {
        return player1;
    }
    auto* handheld = hid.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    if (handheld != nullptr && handheld->IsConnected()) {
        return handheld;
    }
    return nullptr;
}
#endif

// Finds the game's main module for a new process. False with no process.
bool FindMain(Core::System& system) {
    auto* process = system.ApplicationProcess();
    if (process == nullptr) {
        return false;
    }
    if (process != g_state.process) {
        g_state.process = process;
        Loader::AppLoader::Modules modules;
        if (system.GetAppLoader().ReadNSOModules(modules) != Loader::ResultStatus::Success ||
            modules.empty()) {
            modules = Core::FindModules(process);
        }
        g_state.main = 0;
        for (const auto& [address, name] : modules) {
            if (name == "main" || name.starts_with("main.")) {
                g_state.main = address;
            }
        }
        if (g_state.main == 0) {
            g_state.main = GetInteger(Core::FindMainModuleEntrypoint(process));
        }
    }
    return true;
}

// Link's position (x, z), for goto and toward.
std::optional<std::array<float, 2>> PlayerXZ(Core::System& system) {
    if (!FindMain(system)) {
        return std::nullopt;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::PlayerReader reader(memory);
    const auto p = reader.read_position();
    if (!p) {
        g_state.link.reset();
        return std::nullopt;
    }
    g_state.link = std::array<float, 3>{(*p)[0], (*p)[1], (*p)[2]};
    return std::array<float, 2>{(*p)[0], (*p)[2]};
}

// ORM talkthrough: whether our own event runs; nothing when it cannot be read.
std::optional<bool> OwnEventRunning(Core::System& system) {
    if (!FindMain(system)) {
        return std::nullopt;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    return coop::host::PlayerReader(memory).read_event_running();
}

// The camera's ground directions from the last frame's world-to-clip matrix
// (row-major): clip w grows along the view (row 3), clip x to the right
// (row 0). Nothing when there is no recent camera or it looks straight down.
std::optional<Runner::Camera> CameraXZ() {
    const auto frame = CoopCamera::Latest();
    if (!frame || Clock::now() - frame->at > std::chrono::milliseconds(500)) {
        return std::nullopt;
    }
    const auto& m = frame->clip_from_world;
    const float fl = std::hypot(m[12], m[14]);
    const float rl = std::hypot(m[0], m[2]);
    if (fl < 1e-4f || rl < 1e-4f) {
        return std::nullopt;
    }
    return Runner::Camera{m[12] / fl, m[14] / fl, m[0] / rl, m[2] / rl};
}

// warp: asks the game's own sequence for a scene change. ChangeBancOnMain
// changes scene when Sequence_ReservedNextBanc is set (and clears it);
// ChangeBancOnEvent also reads Sequence_ReservedNextStartPosName. The start
// name is written first, so it is there when the scene is read.
void Warp(Core::System& system, const std::string& scene, const std::string& start,
          std::optional<bool> age_child = std::nullopt) {
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::GameFlags flags(memory);
    const auto banc = coop::host::flag_hash("Sequence_ReservedNextBanc");
    const auto start_name = coop::host::flag_hash("Sequence_ReservedNextStartPosName");
    const auto position = coop::host::flag_hash("Sequence_ReservedNextPosition");
    const auto start_type = flags.find_type(start_name);
    bool start_ok = start.empty();
    if (!start.empty() && start_type == 16) {
        start_ok = coop::host::GameFlags::set_string64_now(memory, start_name, start);
    }
    // A scene of the other era (X_Adult, X_Child) also asks for that age:
    // Sequence_IsChildhood (a bool in GameDataList) is set through the game's
    // queue before the scene change is asked, so Link loads at that age.
    const auto childhood = coop::host::flag_hash("Sequence_IsChildhood");
    const std::string_view name = scene;
    std::optional<bool> child;
    if (name.size() > 6 && name.substr(name.size() - 6) == "_Adult") {
        child = false;
    } else if (name.size() > 6 && name.substr(name.size() - 6) == "_Child") {
        child = true;
    }
    if (age_child) {  // ORM: warp SCENE [START] adult|child
        child = age_child;
    }
    const bool era_ok = child && coop::host::GameFlags::set_bool(memory, childhood, *child);
    const bool banc_ok = coop::host::GameFlags::set_string64_now(memory, banc, scene);
    LOG_INFO(Core,
             "coop: test warp to {} (start {}): scene {} (type {}), start {} (type {}), "
             "position type {}, childhood {} {} (type {})",
             scene, start.empty() ? "-" : start, banc_ok ? "set" : "NOT set",
             flags.find_type(banc).value_or(-1), start_ok ? "set" : "NOT set",
             start_type.value_or(-1), flags.find_type(position).value_or(-1),
             !child ? "-" : *child ? "child" : "adult", era_ok ? "set" : "NOT set",
             flags.find_type(childhood).value_or(-1));
}

// wear: writes the String64 flag PlayerStatus.Equip<slot> straight into the
// game's table. Only the flag changes (the sampler sends it in PLAYER_EQUIP,
// so the other games' puppets wear it); this game's own Link is not changed.
void Wear(Core::System& system, const std::string& slot, const std::string& name) {
    const auto flag = WearFlag(slot);
    if (!flag) {
        return;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    // "none" empties the flag: the piece is taken off (a mask, boots).
    const bool ok = coop::host::GameFlags::set_string64_now(
        memory, coop::host::flag_hash(std::string("PlayerStatus.") + *flag),
        name == "none" ? std::string{} : name);
    LOG_INFO(Core, "coop: test wear {} {}: {}", slot, name, ok ? "set" : "NOT set");
}

// assign: puts ITEM on ZR through the game's own Enum queue, for both ages
// (only the one in play counts). Test only: the item must be owned to be
// used. The menu does the same when the player picks it.
void Assign(Core::System& system, const std::string& item) {
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const std::uint32_t value = coop::host::flag_hash(item);
    bool all = true;
    for (const char* flag : {"PlayerStatus.AssignableItem.Child", "PlayerStatus.AssignableItem.Adult"}) {
        all = coop::host::GameFlags::set_enum(memory, coop::host::flag_hash(flag), value) && all;
    }
    LOG_INFO(Core, "coop: test assign {}: {}", item, all ? "set" : "NOT set");
}

// setflag: this game's Int flag, or else its Bool flag, of that name to the
// value, through the game's own queue (issue 130: shop-buy empties the kit
// saves' Deku Shield and fills the wallet). A max-rule item lowered so stays
// in this game (FlagWatcher max_drops_kept). Test only.
void SetFlag(Core::System& system, const std::string& name, int value) {
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const std::uint32_t hash = coop::host::flag_hash(name);
    coop::host::GameFlags flags(memory);
    bool ok = false;
    const char* type = "unknown";
    if (flags.get_int(hash)) {
        type = "int";
        ok = coop::host::GameFlags::set_int(memory, hash, value);
    } else if (flags.get_bool(hash)) {
        type = "bool";
        ok = coop::host::GameFlags::set_bool(memory, hash, value != 0);
    }
    LOG_INFO(Core, "coop: test setflag {} ({}) {}: {}", name, type, value, ok ? "set" : "NOT set");
}

// setenum: this game's Enum flag (a name, or 0x and its hash) to the value of
// that name, through the game's own queue (issue 193: a Threads of Time
// quest's step). Test only.
void SetEnum(Core::System& system, const std::string& name, const std::string& value) {
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    std::uint32_t hash = 0;
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        const char* end = name.data() + name.size();
        if (std::from_chars(name.data() + 2, end, hash, 16).ptr != end) hash = 0;
    } else {
        hash = coop::host::flag_hash(name);
    }
    const bool known = hash != 0 && coop::host::GameFlags(memory).get_enum(hash).has_value();
    const bool ok = known && coop::host::GameFlags::set_enum(memory, hash, coop::host::flag_hash(value));
    LOG_INFO(Core, "coop: test setenum {} {}: {}", name, value, ok ? "set" : known ? "NOT set" : "unknown");
}

// give: bombs, bow and hookshot are the item flags of each; any other name is
// given as it is. Needs LICHII_COOP_CHEATS=1 (applied on the co-op frame).
void Give(const std::string& item) {
    std::vector<std::string> names;
    if (item == "bombs") {
        names = {"PouchData.Item_BombBag_Small.StockNum", "PouchData.Item_Bomb.StockNum"};
    } else if (item == "bow") {
        names = {"PouchData.Weapon_Range_Bow.StockNum", "PouchData.Item_Quiver_Small.StockNum",
                 "PouchData.Item_Arrow_Normal.StockNum"};
    } else if (item == "hookshot") {
        names = {"PouchData.Item_Hookshot.StockNum"};
    } else {
        names = {item};
    }
    const auto known = CoopCheats::Items();
    for (const auto& name : names) {
        const bool listed = std::find(known.begin(), known.end(), name) != known.end();
        if (listed && CoopCheats::On()) {
            CoopCheats::Give(name);
        }
        LOG_INFO(Core, "coop: test give {}: {}", name,
                 !CoopCheats::On() ? "NOT given (LICHII_COOP_CHEATS is off)"
                 : listed          ? "queued"
                                   : "NOT given (unknown item flag)");
    }
}

// Link's Life component (docs/objects.md): life component =
// [[actor + 0x270] + 0x2b8]; the current life s32 at [life + 0x1c10] + 8, the
// max at [life + 0x1c18] + 8 (quarter hearts). The Death component is
// [[actor + 0x270] + 0x1c0]; its u32 at +0x20 is what QueryDeathIsDead reads
// (oot-remake docs/CAIDO_REVIVIR.md 2.2): dead when (w & 0xe) != 0 and
// (w & 0x6000) != 0x2000.
constexpr u64 kActorComponents = 0x270;
constexpr u64 kComponentLife = 0x2b8;
constexpr u64 kComponentDeath = 0x1c0;
constexpr u64 kLifeValue = 0x1c10;
constexpr u64 kLifeMax = 0x1c18;
constexpr u64 kValueAt = 8;
constexpr u64 kDeathWord = 0x20;
// The current event (docs/dungeons-boss.md): S = [manager + 0x68],
// event = [[S + 0xeb8] + 8 * u32 [S + 0xf18]]. Where its name is kept is not
// known, so only the object is logged.
constexpr u64 kEventManagerPtr = 0x4e94840;
constexpr u64 kEventSession = 0x68;
constexpr u64 kSessionEvents = 0xeb8;
constexpr u64 kSessionCurrent = 0xf18;
constexpr s32 kMaxLifeSeen = 999;  // quarter hearts; more is not a life word

struct LifeWords {
    u64 value = 0;  // where the current life s32 is
    u64 max = 0;    // where the max s32 is
};

// The two life words of our Link, or nothing when any link of the path is
// unreadable or the values are not plausible.
std::optional<LifeWords> FindLife(CoopHook::EmulatorMemory& memory,
                                  coop::host::PlayerReader& reader) {
    using coop::host::read_ptr;
    using coop::host::read_u32;
    const auto actor = reader.find_player_actor();
    const auto table = actor ? read_ptr(memory, *actor + kActorComponents) : std::nullopt;
    const auto life = table ? read_ptr(memory, *table + kComponentLife) : std::nullopt;
    const auto value = life ? read_ptr(memory, *life + kLifeValue) : std::nullopt;
    const auto max = life ? read_ptr(memory, *life + kLifeMax) : std::nullopt;
    if (!value || !max) {
        return std::nullopt;
    }
    const auto v = read_u32(memory, *value + kValueAt);
    const auto m = read_u32(memory, *max + kValueAt);
    if (!v || !m || static_cast<s32>(*v) < 0 || static_cast<s32>(*m) <= 0 ||
        static_cast<s32>(*m) > kMaxLifeSeen || static_cast<s32>(*v) > static_cast<s32>(*m)) {
        return std::nullopt;
    }
    return LifeWords{*value + kValueAt, *max + kValueAt};
}

// life N: Link's current life = N quarter hearts (at most his max).
void SetLife(Core::System& system, const std::string& text) {
    const int want = std::atoi(text.c_str());
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test life {}: NOT written (no game process)", want);
        return;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::PlayerReader reader(memory);
    const auto words = FindLife(memory, reader);
    if (!words) {
        LOG_ERROR(Core, "coop: test life {}: NOT written (Link's life is not readable)", want);
        return;
    }
    const s32 was = static_cast<s32>(coop::host::read_u32(memory, words->value).value_or(0));
    const s32 max = static_cast<s32>(coop::host::read_u32(memory, words->max).value_or(0));
    if (want > max) {
        LOG_ERROR(Core, "coop: test life {}: NOT written (more than the max {})", want, max);
        return;
    }
    const u32 value = static_cast<u32>(want);
    std::array<std::uint8_t, 4> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    const bool ok = memory.write(words->value, bytes);
    LOG_INFO(Core, "coop: test life {}: {} (was {} of {})", want, ok ? "written" : "NOT written",
             was, max);
}

// gate 0|1: the downed gate's word in main (coop_downed.h), 0 open, 1 closed.
// From then on the downed state machine (coop_downed_game.h) leaves the gate
// to the script.
void SetGate(Core::System& system, const std::string& text) {
    CoopDowned::SetManual();
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test gate {}: NOT written (no game process)", text);
        return;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const u32 value = text == "1" ? 1u : 0u;
    std::array<std::uint8_t, 4> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    const u64 at = g_state.main + CoopDowned::kGateOffset;
    const bool ok = memory.write(at, bytes);
    const u32 now = coop::host::read_u32(memory, at).value_or(~0u);
    LOG_INFO(Core, "coop: test gate {}: {} (main+{:#x} reads {:#x})", value,
             ok && now == value ? "written" : "NOT written", CoopDowned::kGateOffset, now);
}

// flag NAME|0xHASH 0|1: a Bool through the game's queue, as its setter.
void SetFlag(Core::System& system, const std::string& name, bool value) {
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test flag {}: NOT set (no game process)", name);
        return;
    }
    std::uint32_t hash = 0;
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        hash = static_cast<std::uint32_t>(std::strtoul(name.c_str() + 2, nullptr, 16));
    } else {
        hash = coop::host::flag_hash(name);
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const auto before = coop::host::GameFlags(memory).get_bool(hash);
    const bool ok = hash != 0 && coop::host::GameFlags::set_bool(memory, hash, value);
    LOG_INFO(Core, "coop: test flag {} ({:#010x}) = {}: {} (it read {})", name, hash, value ? 1 : 0,
             ok ? "queued" : "NOT set", before ? (*before ? "1" : "0") : "nothing: not a Bool flag");
}

// int NAME|0xHASH VALUE: an Int through the game's queue, as its setter.
void SetInt(Core::System& system, const std::string& name, const std::string& value) {
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test int {}: NOT set (no game process)", name);
        return;
    }
    std::uint32_t hash = 0;
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        hash = static_cast<std::uint32_t>(std::strtoul(name.c_str() + 2, nullptr, 16));
    } else {
        hash = coop::host::flag_hash(name);
    }
    const auto v = static_cast<std::int32_t>(std::strtol(value.c_str(), nullptr, 10));
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const auto before = coop::host::GameFlags(memory).get_int(hash);
    const bool ok = hash != 0 && coop::host::GameFlags::set_int(memory, hash, v);
    LOG_INFO(Core, "coop: test int {} ({:#010x}) = {}: {} (it read {})", name, hash, v,
             ok ? "queued" : "NOT set", before ? std::to_string(*before) : "nothing: not an Int flag");
}

// key 0xKEY 0|1 (ORM): a 64-bit-key flag through the game's queue, as the
// game sets it when the placement is broken or taken.
void SetKey(Core::System& system, const std::string& text, bool value) {
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test key {}: NOT set (no game process)", text);
        return;
    }
    const std::uint64_t key = std::strtoull(text.c_str() + 2, nullptr, 16);
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    const auto before = coop::host::GameFlags(memory).get_key64(key);
    const bool ok = key != 0 && before && coop::host::GameFlags::set_key64(memory, key, value);
    LOG_INFO(Core, "coop: test key {:#018x} = {}: {} (it read {})", key, value ? 1 : 0,
             ok ? "queued" : "NOT set", before ? (*before ? "1" : "0") : "nothing: not a known key");
}

// bottle SLOT VALUE / restore. The flag store's tables (docs/flags.md):
// manager M = [main + 0x4e842a8]; a typed table has its entry count u32,
// entries pointer, hash slots pointer ({u32 hash, u32 index}, open
// addressing from hash % slot count) and slot count u32. Enum flags (type 6)
// as the game's Enum getter main+0x7e73d0 reads them: count M + 0x250,
// entries M + 0x258, slots M + 0x260, slot count M + 0x26c, entries of 0x38
// bytes, the live u32 at +0x1c (an index of the count or more is taken as 0,
// as there). Bool (type 0): entries of 0x18, live u8 at +0x16 (the saved copy
// at +0x17 is left alone). Int (type 2): entries of 0x28, live u32 at +0x1c.
// The writes go straight into the live values, not through the game's queue,
// so the save keeps what was there: a test-only fake that `restore` undoes.
constexpr u64 kFlagManagerPtr = 0x4e842a8;
struct FlagTable {
    u64 count, entries, slots, capacity, stride, value;
};
constexpr FlagTable kEnumTable{0x250, 0x258, 0x260, 0x26c, 0x38, 0x1c};
constexpr FlagTable kBoolTable{0x10, 0x18, 0x20, 0x2c, 0x18, 0x16};
constexpr FlagTable kIntTable{0xd0, 0xd8, 0xe0, 0xec, 0x28, 0x1c};
constexpr int kEnumType = 6;

// A name or 0x hash, as the flag lists take it.
u32 HashOf(const std::string& name) {
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        return static_cast<u32>(std::strtoul(name.c_str() + 2, nullptr, 16));
    }
    return coop::host::flag_hash(name);
}

// enum NAME [VALUE]: an Enum flag as the game holds it, by the store's own
// reads (GameFlags::get_enum, enum_values), with names from the built-in
// story list's steps.
void EnumFlag(Core::System& system, const std::string& name, const std::string& value) {
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test enum {}: NOT read (no game process)", name);
        return;
    }
    static const coop::host::StoryList names = coop::host::StoryList::defaults();
    const u32 hash = HashOf(name);
    const auto named = [&](u32 v) {
        const auto n = names.value_name(hash, v);
        return n ? fmt::format("{} ({:#010x})", *n, v) : fmt::format("{:#010x} (no listed name)", v);
    };
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::GameFlags flags(memory);
    const auto type = flags.find_type(hash);
    const auto now = flags.get_enum(hash);
    std::string list = "none read";
    if (const auto values = flags.enum_values(hash)) {
        list = fmt::format("{}:", values->size());
        for (const u32 v : *values) {
            const auto n = names.value_name(hash, v);
            list += " " + (n ? *n : fmt::format("{:#010x}", v));
        }
        const coop::host::EnumFlags steps = names.step_flags();  // kept: values() points into it
        if (const auto* ours = steps.values(hash)) {
            list += *ours == *values ? " (the story list's order)" : " (NOT the story list's order)";
        }
    }
    std::string raw = "none";
    if (const auto at = flags.live_address(coop::host::GameFlags::Live::enum32, hash)) {
        raw.clear();
        for (u64 i = 0; i < kEnumTable.stride; i += 4) {
            const u32 word = coop::host::read_u32(memory, *at - kEnumTable.value + i).value_or(0);
            raw += fmt::format("{}{:08x}", i ? " " : "", word);
        }
    }
    LOG_INFO(Core, "coop: test enum {} ({:#010x}): find_type {}, value {}; the game's values {}; entry {}",
             name, hash, type.value_or(-1), now ? named(*now) : std::string("NOT read"), list, raw);
    if (value.empty()) {
        return;
    }
    const u32 v = HashOf(value);
    const bool ok = v != 0 && coop::host::GameFlags::set_enum(memory, hash, v);
    LOG_INFO(Core, "coop: test enum {} = {}: {}", name, named(v), ok ? "queued" : "NOT queued");
}

// Where the live value of flag `hash` is in table `t`, or nothing.
std::optional<u64> FlagValueAt(CoopHook::EmulatorMemory& memory, const FlagTable& t, u32 hash) {
    using coop::host::read_ptr;
    using coop::host::read_u32;
    const auto mgr = read_ptr(memory, g_state.main + kFlagManagerPtr);
    if (!mgr || hash == 0) {
        return std::nullopt;
    }
    const auto count = read_u32(memory, *mgr + t.count);
    const auto entries = read_ptr(memory, *mgr + t.entries);
    const auto slots = read_ptr(memory, *mgr + t.slots);
    const auto cap = read_u32(memory, *mgr + t.capacity);
    if (!count || !entries || !slots || !cap || *cap == 0 || *count == 0) {
        return std::nullopt;
    }
    for (u32 n = 0, i = hash % *cap; n < *cap; ++n, i = i + 1 >= *cap ? 0 : i + 1) {
        const auto h = read_u32(memory, *slots + 8ull * i);
        if (!h || *h == 0) {
            return std::nullopt;
        }
        if (*h == hash) {
            const auto index = read_u32(memory, *slots + 8ull * i + 4);
            if (!index) {
                return std::nullopt;
            }
            const u64 at = *index < *count ? *index : 0;
            return *entries + t.stride * at + t.value;
        }
    }
    return std::nullopt;
}

bool WriteU32(CoopHook::EmulatorMemory& memory, u64 at, u32 value) {
    std::array<std::uint8_t, 4> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    return memory.write(at, bytes);
}

std::uint8_t ReadU8(CoopHook::EmulatorMemory& memory, u64 at) {
    std::array<std::uint8_t, 1> b{};
    return memory.read(at, b) ? b[0] : 0xff;
}

bool WriteU8(CoopHook::EmulatorMemory& memory, u64 at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> b{value};
    return memory.write(at, b);
}

// The BottleItem value name whose flag_hash is `value`, or its hex.
std::string BottleName(u32 value) {
    static constexpr const char* kNames[] = {
        "Empty", "RedPotion", "GreenPotion", "BluePotion", "BlueFire", "Fish",    "Letter",
        "BigPoe", "Poe",      "Bug",         "Fairy",      "MaxMilk",  "HalfMilk"};
    for (const char* name : kNames) {
        if (coop::host::flag_hash(name) == value) {
            return name;
        }
    }
    return fmt::format("{:#x} (not a value name's hash)", value);
}

// What the first write to a bottle found, for restore.
struct BottleBefore {
    u32 item = 0;
    std::optional<std::uint8_t> valid;  // set when a write changed it
    std::optional<u32> stock;           // set when a write changed it
};
std::array<std::optional<BottleBefore>, 5> g_bottles;  // A to E; present thread only

void Bottle(Core::System& system, const std::string& slot, const std::string& value) {
    if (!FindMain(system) || slot.size() != 1 || slot[0] < 'A' || slot[0] > 'E') {
        LOG_ERROR(Core, "coop: test bottle {} {}: NOT written (no game process)", slot, value);
        return;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::GameFlags flags(memory);
    const std::string item_name =
        "PlayerStatus.AssignableItem.BottleInfo.Bottle" + slot + ".BottleItem";
    const std::string valid_name =
        "PlayerStatus.AssignableItem.BottleInfo.Bottle" + slot + ".IsValid";
    const std::string stock_name = "PouchData.Bottle" + slot + ".StockNum";
    const u32 item_hash = coop::host::flag_hash(item_name);
    const auto item_at = FlagValueAt(memory, kEnumTable, item_hash);
    const auto valid_at = FlagValueAt(memory, kBoolTable, coop::host::flag_hash(valid_name));
    const auto stock_at = FlagValueAt(memory, kIntTable, coop::host::flag_hash(stock_name));
    const auto type = flags.find_type(item_hash);
    if (!item_at || !valid_at || !stock_at || type != kEnumType) {
        LOG_ERROR(Core,
                  "coop: test bottle {} {}: NOT written (item {} type {}, IsValid {}, "
                  "StockNum {})",
                  slot, value, item_at ? "found" : "missing", type.value_or(-1),
                  valid_at ? "found" : "missing", stock_at ? "found" : "missing");
        return;
    }
    const u32 item = coop::host::read_u32(memory, *item_at).value_or(0);
    const std::uint8_t valid = ReadU8(memory, *valid_at);
    const u32 stock = coop::host::read_u32(memory, *stock_at).value_or(0);
    // The Enum entry around the value, to check the layout in the log.
    std::string raw;
    for (u64 i = 0; i < kEnumTable.stride; i += 4) {
        const u32 word = coop::host::read_u32(memory, *item_at - kEnumTable.value + i).value_or(0);
        raw += fmt::format("{}{:08x}", i ? " " : "", word);
    }
    LOG_INFO(Core, "coop: test bottle {} {}: before: {} = {}, {} = {}, {} = {}; enum entry {}",
             slot, value, item_name, BottleName(item), valid_name, valid, stock_name,
             static_cast<s32>(stock), raw);
    auto& saved = g_bottles[slot[0] - 'A'];
    if (value == "restore") {
        if (!saved) {
            LOG_ERROR(Core, "coop: test bottle {} restore: nothing to restore", slot);
            return;
        }
        bool ok = WriteU32(memory, *item_at, saved->item);
        if (saved->valid) {
            ok = WriteU8(memory, *valid_at, *saved->valid) && ok;
        }
        if (saved->stock) {
            ok = WriteU32(memory, *stock_at, *saved->stock) && ok;
        }
        LOG_INFO(Core, "coop: test bottle {} restore: {} (item {}{}{})", slot,
                 ok ? "written" : "NOT written", BottleName(saved->item),
                 saved->valid ? fmt::format(", IsValid {}", *saved->valid) : "",
                 saved->stock ? fmt::format(", StockNum {}", static_cast<s32>(*saved->stock))
                              : "");
        saved.reset();
        return;
    }
    if (!saved) {
        saved = BottleBefore{item, std::nullopt, std::nullopt};
    }
    // An unowned bottle is owned for the fake too (IsValid is shared in a
    // session: only in one-game tests).
    bool ok = WriteU32(memory, *item_at, coop::host::flag_hash(value));
    if (valid == 0) {
        if (!saved->valid) {
            saved->valid = valid;
        }
        ok = WriteU8(memory, *valid_at, 1) && ok;
    }
    if (static_cast<s32>(stock) <= 0) {
        if (!saved->stock) {
            saved->stock = stock;
        }
        ok = WriteU32(memory, *stock_at, 1) && ok;
    }
    LOG_INFO(Core, "coop: test bottle {} {}: {} (now {}, IsValid {}, StockNum {})", slot, value,
             ok ? "written" : "NOT written",
             BottleName(coop::host::read_u32(memory, *item_at).value_or(0)),
             ReadU8(memory, *valid_at),
             static_cast<s32>(coop::host::read_u32(memory, *stock_at).value_or(0)));
}

// event NAME: asks the game to start that event with Link's event component
// (orm_story_events.h), from the cave on the calc thread.
void AskEvent(Core::System& system, const std::string& name) {
    if (!FindMain(system)) {
        LOG_ERROR(Core, "coop: test event {}: NOT asked (no game process)", name);
        return;
    }
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::PlayerReader reader(memory);
    const auto actor = reader.find_player_actor();
    const auto position = reader.read_position();
    LOG_INFO(Core, "coop: test event {}: Link {} at {}, scene {}, event {}", name,
             actor ? fmt::format("{:#x}", *actor) : std::string("not found"),
             position ? fmt::format("{:.2f} {:.2f} {:.2f}", (*position)[0], (*position)[1],
                                    (*position)[2])
                      : std::string("-"),
             reader.read_scene().value_or("-"), reader.read_event_name().value_or("none"));
    OrmStoryEvents::Ask(g_state.main, actor.value_or(0), name);  // ORM platform.h: no System
}

// LICHII_COOP_DEBUG_LIFE=1: one "coop: life:" line on each change of what
// the probe reads, at most ten a second (see coop_testinput.h).
struct LifeProbe {
    bool checked = false;
    bool on = false;
    CoopEvery::Every every{100'000};
    bool logged = false;
    s32 life = -1;
    s32 max = -1;
    std::optional<u32> death;
    std::optional<bool> event;
    std::string anim;
    std::string scene;
    u64 event_object = 0;
    double since = 0.0;  // steady seconds at the first line
} g_life;  // present thread only

bool EnvOn(const char* name) {
    return CoopSwitch::On(name, false);  // ORM: coop_switch.h
}

void ProbeLife(Core::System& system) {
    if (!g_life.checked) {
        g_life.checked = true;
        g_life.on = EnvOn("LICHII_COOP_DEBUG_LIFE");
        if (g_life.on) {
            LOG_INFO(Core, "coop: life: probe on (LICHII_COOP_DEBUG_LIFE)");
        }
    }
    if (!g_life.on || !FindMain(system)) {
        return;
    }
    using coop::host::read_ptr;
    using coop::host::read_u32;
    CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
    coop::host::PlayerReader reader(memory);
    const auto words = FindLife(memory, reader);
    const s32 life = words ? static_cast<s32>(read_u32(memory, words->value).value_or(~0u)) : -1;
    const s32 max = words ? static_cast<s32>(read_u32(memory, words->max).value_or(~0u)) : -1;
    std::optional<u32> death;
    if (const auto actor = reader.find_player_actor()) {
        const auto table = read_ptr(memory, *actor + kActorComponents);
        const auto component = table ? read_ptr(memory, *table + kComponentDeath) : std::nullopt;
        death = component ? read_u32(memory, *component + kDeathWord) : std::nullopt;
    }
    const auto event = reader.read_event_running();
    u64 event_object = 0;
    if (event && *event) {
        const auto manager = read_ptr(memory, g_state.main + kEventManagerPtr);
        const auto session = manager ? read_ptr(memory, *manager + kEventSession) : std::nullopt;
        const auto events = session ? read_ptr(memory, *session + kSessionEvents) : std::nullopt;
        const auto index = session ? read_u32(memory, *session + kSessionCurrent) : std::nullopt;
        if (events && index && *index < 64) {
            event_object = read_ptr(memory, *events + 8 * u64{*index}).value_or(0);
        }
    }
    const auto animation = reader.read_animation();
    const std::string anim = animation ? animation->name : std::string("-");
    const std::string scene = reader.read_scene().value_or("-");
    const bool same = g_life.logged && life == g_life.life && max == g_life.max &&
                      death == g_life.death && event == g_life.event && anim == g_life.anim &&
                      event_object == g_life.event_object && scene == g_life.scene;
    const std::uint64_t now_us = CoopEvery::NowUs();
    if (same || !g_life.every.Due(now_us)) {
        return;
    }
    const double now = static_cast<double>(now_us) / 1e6;
    if (!g_life.logged) {
        g_life.since = now;
    }
    if (event_object != g_life.event_object) {
        LOG_INFO(Core, "coop: life: event object {:#x} -> {:#x}", g_life.event_object,
                 event_object);
    }
    const bool dead = death && (*death & 0xe) != 0 && (*death & 0x6000) != 0x2000;
    const auto position = reader.read_position();
    LOG_INFO(Core,
             "coop: life: t {:.2f} life {} of {}, death word {}{}, event {}, anim {}, scene {}, "
             "at {}",
             now - g_life.since, life, max, death ? fmt::format("{:#x}", *death) : "-",
             death ? (dead ? " (dead)" : " (alive)") : "", !event ? "-" : *event ? "on" : "off",
             anim, scene,
             position ? fmt::format("{:.2f} {:.2f} {:.2f}", (*position)[0], (*position)[1],
                                    (*position)[2])
                      : "-");
    g_life.logged = true;
    g_life.life = life;
    g_life.max = max;
    g_life.death = death;
    g_life.event = event;
    g_life.anim = anim;
    g_life.scene = scene;
    g_life.event_object = event_object;
}

void StopRunning() {
#ifndef COOP_HOOKSHOT
    if (g_state.running && g_state.running->controller) {
        g_state.running->controller->SetInputInjector({});
    }
#endif
    g_state.running.reset();
}

#ifdef COOP_HOOKSHOT
// Hookshot: no pad here (the driver holds the buttons through the bridge);
// the steps are the driver's test commands, played from this frame.
void Begin(Core::System&, Script script) {
    StopRunning();
    Running r;
    r.runner = std::make_unique<Runner>(std::move(script));
    r.shared = std::make_shared<Shared>();
    r.start = Clock::now();
    LOG_INFO(Core, "coop: test commands started: {} steps", r.runner->StepCount());
    g_state.running = std::move(r);
}
#else
void Begin(Core::System& system, Script script) {
    StopRunning();
    auto* controller = PickController(system);
    if (controller == nullptr) {
        LOG_ERROR(Core, "coop: test script: no player 1 or handheld controller connected");
        return;
    }
    Running r;
    r.runner = std::make_unique<Runner>(std::move(script));
    r.shared = std::make_shared<Shared>();
    r.start = Clock::now();
    r.controller = controller;
    controller->SetInputInjector(
        [shared = r.shared]() -> std::optional<Core::HID::InjectedInput> {
            Output out;
            {
                std::lock_guard lock(shared->mutex);
                out = shared->out;
            }
            Core::HID::InjectedInput in;
            in.buttons = static_cast<Core::HID::NpadButton>(out.buttons);
            if (out.left) {
                in.left = Core::HID::AnalogStickState{out.left->x, out.left->y};
            }
            if (!out.right) {
                out.right = NudgeNow();
            }
            if (out.right) {
                in.right = Core::HID::AnalogStickState{out.right->x, out.right->y};
            }
            return in;
        });
    g_state.nudge_controller = nullptr;  // replaced by the script's, which adds the nudge
    LOG_INFO(Core, "coop: test script started: {} steps", r.runner->StepCount());
    g_state.running = std::move(r);
}
#endif

void CheckFile(Core::System& system) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(g_state.path, ec);
    if (ec) {
        return;
    }
    const auto size = std::filesystem::file_size(g_state.path, ec);
    if (ec || (g_state.seen && time == g_state.seen_time && size == g_state.seen_size)) {
        return;
    }
    g_state.seen = true;
    g_state.seen_time = time;
    g_state.seen_size = size;
    if (size == 0) {
        return;
    }
    std::stringstream text;
    {
        std::ifstream in(g_state.path, std::ios::binary);
        text << in.rdbuf();
    }
#ifdef COOP_HOOKSHOT
    // Taken: emptied, so the driver writes the next ones (an empty file is
    // not played).
    std::ofstream(g_state.path, std::ios::binary | std::ios::trunc);
#endif
    std::string error;
    auto script = Parse(text.str(), error);
    if (!script) {
        LOG_ERROR(Core, "coop: test script {}: {}", g_state.path.string(), error);
        return;
    }
    Begin(system, std::move(*script));
}

} // namespace

void OnFrame(Core::System& system) {
    if (!g_state.checked_env) {
        g_state.checked_env = true;
        g_state.path = EnvPath();
        if (!g_state.path.empty()) {
#ifdef COOP_HOOKSHOT
            constexpr const char* kWhat = "commands";
#else
            constexpr const char* kWhat = "script";
#endif
            LOG_INFO(Core, "coop: test {} file {}", kWhat, g_state.path.string());
        }
    }
    ProbeLife(system);
    if (g_state.path.empty()) {
        return;
    }
    const bool quarter = g_state.check.Due(CoopEvery::NowUs());
    if (quarter) {
        CheckFile(system);
    }
    if (!g_state.running) {
        return;
    }
    Running& r = *g_state.running;
    const double now = std::chrono::duration<double>(Clock::now() - r.start).count();
    std::vector<std::string> log;
    const auto player = PlayerXZ(system);
    const auto camera = CameraXZ();
    // await mate: another player in our scene, as presence shows it (CoopHook::CurrentMates).
    bool mate_here = false;
    if (const auto mates = CoopHook::CurrentMates(); mates && mates->scene) {
        for (const auto& m : mates->others) {
            if (m.scene && *m.scene == *mates->scene) {
                mate_here = true;
                break;
            }
        }
    }
    r.runner->Advance(now, player, log, camera, mate_here, OwnEventRunning(system));
    if (r.runner->Steering() && quarter) {
        const auto& out = r.runner->Current();
        LOG_INFO(Core,
                 "coop: test steer at {:.2f} {:.2f}, camera {:.2f} {:.2f}, heading {:.2f}, "
                 "mirror {}, stick {} {}",
                 player ? (*player)[0] : 0.0f, player ? (*player)[1] : 0.0f,
                 camera ? camera->forward_x : 0.0f, camera ? camera->forward_z : 0.0f,
                 r.runner->Heading().value_or(99.0), r.runner->Mirror(),
                 out.left ? out.left->x : 0, out.left ? out.left->y : 0);
    }
    for (const auto& line : log) {
        if (line.starts_with("mark ") && g_state.link) {
            // With the height, so a scenario can tell a rider fell off (deku-water-floor).
            LOG_INFO(Core, "coop: test {} (Link at {:.2f} {:.2f} {:.2f})", line, (*g_state.link)[0],
                     (*g_state.link)[1], (*g_state.link)[2]);
        } else {
            LOG_INFO(Core, "coop: test {}", line);
        }
    }
    if (g_state.place && now >= g_state.place->next) {
        auto& p = *g_state.place;
        const bool ok = CoopBoss::MoveLink(system, g_state.main, CoopBoss::Vec{p.at[0], p.at[1], p.at[2]});
        ++p.tries;
        if (ok || p.tries >= 100) {
            LOG_INFO(Core, "coop: test place {:.2f} {:.2f} {:.2f}: {} ({} tries)", p.at[0], p.at[1], p.at[2],
                     ok ? "done" : "failed", p.tries);
            g_state.place.reset();
        } else {
            p.next = now + 0.1;
        }
    }
    for (const auto& request : r.runner->TakeRequests()) {
        if (request.kind == Kind::warp) {
            Warp(system, request.name, request.start, request.child);
            g_state.warp = State::PendingWarp{request.name, request.start, now, now, player, request.child};
        } else if (request.kind == Kind::wear) {
            Wear(system, request.start, request.name);
        } else if (request.kind == Kind::assign) {
            Assign(system, request.name);
        } else if (request.kind == Kind::setflag) {
            SetFlag(system, request.name, std::stoi(request.start));
        } else if (request.kind == Kind::setenum) {
            SetEnum(system, request.name, request.start);
        } else if (request.kind == Kind::place) {
            const bool ok = CoopBoss::MoveLink(system, g_state.main,
                                               CoopBoss::Vec{request.at[0], request.at[1], request.at[2]});
            if (ok) {
                g_state.place.reset();
                LOG_INFO(Core, "coop: test place {:.2f} {:.2f} {:.2f}: done", request.at[0], request.at[1],
                         request.at[2]);
            } else {
                g_state.place = State::PendingPlace{request.at, now + 0.1, 1};  // ORM: tried again
            }
        } else if (request.kind == Kind::kill) {
            CoopObjects::QueueTestKill(request.name);  // logged where it is done
        } else if (request.kind == Kind::chat) {
            const bool sent = CoopHook::SendChat(request.name);
            LOG_INFO(Core, "coop: test chat '{}': {}", request.name, sent ? "sent" : "NOT sent");
        } else if (request.kind == Kind::life) {
            SetLife(system, request.name);
        } else if (request.kind == Kind::gate) {
            SetGate(system, request.name);
        } else if (request.kind == Kind::bottle) {
            Bottle(system, request.name, request.start);
        } else if (request.kind == Kind::event) {
            AskEvent(system, request.name);
        } else if (request.kind == Kind::flag) {
            SetFlag(system, request.name, request.start == "1");
        } else if (request.kind == Kind::int_flag) {
            SetInt(system, request.name, request.start);
        } else if (request.kind == Kind::enum_flag) {
            EnumFlag(system, request.name, request.start);
        } else if (request.kind == Kind::age) {
            LOG_INFO(Core, "coop: test age {}: asked", request.name);
            CoopAge::Change(request.name == "child");
        } else if (request.kind == Kind::key64) {
            SetKey(system, request.name, request.start == "1");
        } else if (request.kind == Kind::skip) {
            const bool found = FindMain(system);
            LOG_INFO(Core, "coop: test skip: {}", found ? "asked" : "NOT asked (no game process)");
            if (found) {
                OrmStoryEvents::NoteSkip("test skip");  // the owner's skip, as a press would be
                OrmStoryEvents::Skip(g_state.main, "test skip");
            }
        } else {
            Give(request.name);
        }
    }
    // A warp is asked again every 0.5 s until Link is in that scene: the game
    // takes the request only while an event runs (the one after a save is
    // loaded, a talk), so a warp is best asked before the save is loaded. ORM: one
    // asked in the world starts a scene event of ours (WarpStartsEvent).
    if (g_state.warp) {
        State::PendingWarp& w = *g_state.warp;
        CoopHook::EmulatorMemory memory(system.ApplicationMemory(), g_state.main);
        coop::host::PlayerReader reader(memory);
        const auto scene = reader.read_scene();
        const bool event_running = reader.read_event_running().value_or(false);
        if (event_running) {
            w.event_seen = true;
        }
        if (scene && *scene == w.scene) {
            // Which path took it (docs/RIESGOS_RESUELTOS.md "Mundo e historia").
            const char* path = w.events > 0  ? "taken after our scene event, in free play"
                               : w.event_seen ? "taken when an event of the game ended"
                                              : "taken";
            LOG_INFO(Core, "coop: test warp done after {:.1f} s, Link at {:.2f} {:.2f} ({})", now - w.since,
                     player ? (*player)[0] : 0.0f, player ? (*player)[1] : 0.0f, path);
            g_state.warp.reset();
        } else if (now - w.since > kWarpGiveUpS) {
            LOG_ERROR(Core, "coop: test warp to {} did not happen in {} s (scene {}; {} scene event(s) started)",
                      w.scene, kWarpGiveUpS, scene.value_or("?"), w.events);
            g_state.warp.reset();
        } else {
            if (now - w.asked >= 0.5) {
                Warp(system, w.scene, w.start, w.child);
                w.asked = now;
            }
            // Asked with Link in the world (not at the title, where the save's own event takes
            // it): no event comes by itself, so one of ours.
            if (w.from &&
                WarpStartsEvent(now, w.since, w.event_at, w.events, scene.has_value(), event_running)) {
                if (CoopTeleport::StartSceneEvent()) {
                    ++w.events;
                    w.event_at = now;
                    LOG_INFO(Core,
                             "coop: test warp to {}: no event runs in free play: started the game's event {} "
                             "so it takes the scene change ({} of {})",
                             w.scene, CoopTeleport::Trip::kSceneEvent, w.events, kWarpEvents);
                } else if (!w.told_off) {
                    w.told_off = true;
                    LOG_WARNING(Core,
                                "coop: test warp to {}: no event runs and the scene event is off "
                                "(LICHII_COOP_TELEPORT=0): waiting for one of the game's",
                                w.scene);
                }
            }
        }
    }
    Output out = r.runner->Current();
    {
        std::lock_guard lock(r.shared->mutex);
        r.shared->out = out;
    }
    if (r.runner->Done()) {
#ifdef COOP_HOOKSHOT
        LOG_INFO(Core, "coop: test commands done");  // the driver writes "test script done"
#else
        LOG_INFO(Core, "coop: test script done");
#endif
        StopRunning();
    }
}

void Nudge(Core::System& system, std::optional<Stick> right) {
    {
        std::lock_guard lock(g_nudge.mutex);
        if (!right && g_nudge.right) {
            LOG_INFO(Core, "coop: spectator: right-stick nudge let go: {} pad reads took it ({})", g_nudge.reads,
                     g_state.running ? "through the test script's input" : "its own input");
        } else if (right && !g_nudge.right) {
            g_nudge.reads = 0;
        }
        g_nudge.right = right;
    }
#ifdef COOP_HOOKSHOT
    // ORM (spectator camera, coop_camera_probe.h): Hookshot's module has no
    // input injector (the bridge gives the pad's word, not a way in), so the
    // nudge is not played there and a downed Link's camera does not wake.
    (void)system;
    static bool said = false;
    if (right && !std::exchange(said, true)) {
        LOG_INFO(Core, "coop: spectator: right-stick nudge not played on Hookshot (no input injector); "
                       "the camera stays as it is");
    }
    return;
#else
    if (g_state.running) {
        return;  // the script's injector adds it
    }
    if (right && g_state.nudge_controller == nullptr) {
        auto* controller = PickController(system);
        if (controller == nullptr || controller->HasInputInjector()) {
            return;  // no pad, or someone else's injector (the debug server)
        }
        controller->SetInputInjector([]() -> std::optional<Core::HID::InjectedInput> {
            const auto r = NudgeNow();
            if (!r) {
                return std::nullopt;
            }
            Core::HID::InjectedInput in;
            in.right = Core::HID::AnalogStickState{r->x, r->y};
            return in;
        });
        g_state.nudge_controller = controller;
    } else if (!right && g_state.nudge_controller != nullptr) {
        g_state.nudge_controller->SetInputInjector({});
        g_state.nudge_controller = nullptr;
    }
#endif
}

} // namespace CoopTestInput

#endif // LICHII_COOP
