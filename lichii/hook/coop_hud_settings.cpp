// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_hud_settings.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <utility>

#include "coop_switch.h"

namespace OrmHud {
namespace {

constexpr std::string_view kPlaces = "arriba-izquierda/arriba/arriba-derecha/abajo-izquierda/abajo/abajo-derecha";

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

// ASCII to lower case, and the two capitals of our words (Ñ, Í) in UTF-8.
std::string Lower(std::string_view s) {
    std::string out(s);
    for (std::size_t i = 0; i < out.size(); ++i) {
        char& c = out[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (static_cast<unsigned char>(c) == 0xc3 && i + 1 < out.size()) {
            auto& next = reinterpret_cast<unsigned char&>(out[i + 1]);
            if (next == 0x91 || next == 0x8d) {  // Ñ -> ñ, Í -> í
                next = static_cast<unsigned char>(next + 0x20);
            }
        }
    }
    return out;
}

std::vector<std::string> Words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
            ++i;
        }
        std::size_t end = i;
        while (end < s.size() && s[end] != ' ' && s[end] != '\t') {
            ++end;
        }
        if (end > i) {
            out.emplace_back(s.substr(i, end - i));
        }
        i = end;
    }
    return out;
}

// The words from `from` on, joined by '-' ("arriba derecha" -> "arriba-derecha").
std::string Rest(const std::vector<std::string>& words, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < words.size(); ++i) {
        if (!out.empty()) {
            out += '-';
        }
        out += words[i];
    }
    return out;
}

std::string_view YesNo(bool on) {
    return on ? "s\xc3\xad" : "no";
}

std::string Usage() {
    return "HUD: /rupias on|off|<sitio>, /nombres on|off, /hud panel <sitio>, /hud tamano <" +
           std::to_string(kMinSize) + "-" + std::to_string(kMaxSize) + ">, /hud chat <0-" +
           std::to_string(kMaxChatRows) + ">, /hud reset. Sitios: " + std::string(kPlaces);
}

// "/rupias ..." and "/hud rupias ...": `args` after the word.
std::string Rupees(const std::vector<std::string>& args, Settings& s) {
    if (args.empty()) {
        s.rupees = !s.rupees;
    } else if (const auto on = ParseOnOff(Rest(args, 0))) {
        s.rupees = *on;
    } else if (const auto place = ParsePlace(Rest(args, 0))) {
        s.rupees = true;
        s.rupees_at = *place;
    } else {
        return "HUD: /rupias on, off o un sitio (" + std::string(kPlaces) + "); nada cambia";
    }
    if (!s.rupees) {
        return "HUD: rupias de todos ocultas (el panel F9 las sigue mostrando)";
    }
    return "HUD: rupias de todos a la vista (" + std::string(PlaceName(s.rupees_at)) + ")";
}

std::string Names(const std::vector<std::string>& args, Settings& s) {
    if (args.empty()) {
        s.names = !s.names;
    } else if (const auto on = ParseOnOff(Rest(args, 0))) {
        s.names = *on;
    } else {
        return "HUD: /nombres on u off; nada cambia";
    }
    return s.names ? "HUD: nombres sobre los compa\xc3\xb1" "eros a la vista"
                   : "HUD: nombres ocultos (quedan las flechas y los puntos)";
}

} // namespace

std::optional<Place> ParsePlace(std::string_view text) {
    std::string t = Lower(Trim(text));
    for (char& c : t) {
        if (c == '_' || c == ' ' || c == '\t') {
            c = '-';
        }
    }
    struct Name {
        std::string_view es, en;
        Place place;
    };
    constexpr Name kNames[] = {
        {"arriba-izquierda", "top-left", Place::top_left},
        {"arriba", "top", Place::top},
        {"arriba-derecha", "top-right", Place::top_right},
        {"abajo-izquierda", "bottom-left", Place::bottom_left},
        {"abajo", "bottom", Place::bottom},
        {"abajo-derecha", "bottom-right", Place::bottom_right},
    };
    for (const auto& n : kNames) {
        if (t == n.es || t == n.en) {
            return n.place;
        }
    }
    return std::nullopt;
}

std::string_view PlaceName(Place place) {
    switch (place) {
    case Place::top_left:
        return "arriba-izquierda";
    case Place::top:
        return "arriba";
    case Place::top_right:
        return "arriba-derecha";
    case Place::bottom_left:
        return "abajo-izquierda";
    case Place::bottom:
        return "abajo";
    case Place::bottom_right:
        return "abajo-derecha";
    }
    return "arriba";
}

std::optional<int> ParseSize(std::string_view text) {
    text = Trim(text);
    if (!text.empty() && text.back() == '%') {
        text.remove_suffix(1);
        text = Trim(text);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    int v = 0;
    const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (err != std::errc() || end != text.data() + text.size() || v < kMinSize || v > kMaxSize) {
        return std::nullopt;
    }
    return v;
}

std::optional<std::size_t> ParseChatLines(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned v = 0;
    const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (err != std::errc() || end != text.data() + text.size() || v > kMaxChatRows) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(v);
}

std::optional<bool> ParseOnOff(std::string_view text) {
    const std::string t = Lower(Trim(text));
    if (t == "si" || t == "s\xc3\xad") {
        return true;
    }
    switch (CoopSwitch::Parse(t)) {
    case CoopSwitch::Reading::on:
        return true;
    case CoopSwitch::Reading::off:
        return false;
    case CoopSwitch::Reading::unset:
    case CoopSwitch::Reading::unknown:
        break;
    }
    return std::nullopt;
}

Settings ReadSettings(const EnvReader& env, std::vector<std::string>* log) {
    Settings s;
    const Settings d;
    auto value = [&](const char* name) -> std::optional<std::string> {
        if (!env) {
            return std::nullopt;
        }
        auto v = env(name);
        if (!v || Trim(*v).empty()) {
            return std::nullopt;
        }
        return std::string(Trim(*v));
    };
    auto complain = [&](const char* name, const std::string& v, std::string_view accepted,
                        std::string_view default_text) {
        if (log != nullptr) {
            log->push_back(CoopSwitch::Unknown(name, v, accepted, default_text));
        }
    };
    auto on_off = [&](const char* name, bool& out, bool default_on) {
        if (const auto v = value(name)) {
            if (const auto on = ParseOnOff(*v)) {
                out = *on;
            } else {
                complain(name, *v, "0/1/true/false/yes/no/on/off", default_on ? "on" : "off");
            }
        }
    };
    auto place = [&](const char* name, Place& out, Place default_place) {
        if (const auto v = value(name)) {
            if (const auto p = ParsePlace(*v)) {
                out = *p;
            } else {
                complain(name, *v, kPlaces, PlaceName(default_place));
            }
        }
    };
    on_off("LICHII_ORM_HUD_NAMES", s.names, d.names);
    on_off("LICHII_ORM_HUD_RUPEES", s.rupees, d.rupees);
    place("LICHII_ORM_HUD_RUPEES_AT", s.rupees_at, d.rupees_at);
    place("LICHII_ORM_HUD_PANEL_AT", s.panel_at, d.panel_at);
    if (const auto v = value("LICHII_ORM_HUD_SIZE")) {
        if (const auto size = ParseSize(*v)) {
            s.size = *size;
        } else {
            complain("LICHII_ORM_HUD_SIZE", *v,
                     "a percent from " + std::to_string(kMinSize) + " to " + std::to_string(kMaxSize),
                     std::to_string(d.size));
        }
    }
    if (const auto v = value("LICHII_ORM_HUD_CHAT_LINES")) {
        if (const auto lines = ParseChatLines(*v)) {
            s.chat_lines = *lines;
        } else {
            complain("LICHII_ORM_HUD_CHAT_LINES", *v, "0 to " + std::to_string(kMaxChatRows),
                     std::to_string(d.chat_lines));
        }
    }
    return s;
}

std::string Describe(const Settings& s) {
    std::string out = "HUD: nombres " + std::string(YesNo(s.names)) + ", rupias " + std::string(YesNo(s.rupees));
    if (s.rupees) {
        out += " (" + std::string(PlaceName(s.rupees_at)) + ")";
    }
    out += ", panel " + std::string(PlaceName(s.panel_at)) + ", tama\xc3\xb1o " + std::to_string(s.size) + " %, chat " +
           std::to_string(s.chat_lines) + (s.chat_lines == 1 ? " l\xc3\xadnea" : " l\xc3\xadneas");
    return out;
}

bool IsCommand(std::string_view text) {
    text = Trim(text);
    if (text.empty() || text.front() != '/') {
        return false;
    }
    const auto words = Words(text.substr(1));
    if (words.empty()) {
        return false;
    }
    const std::string first = Lower(words[0]);
    return std::find(kCommandNames.begin(), kCommandNames.end(), first) != kCommandNames.end();
}

std::optional<std::string> Command(std::string_view text, Settings& s, const Settings& file) {
    if (!IsCommand(text)) {
        return std::nullopt;
    }
    text = Trim(text);
    std::vector<std::string> words = Words(text.substr(1));
    for (auto& w : words) {
        w = Lower(w);
    }
    const std::string first = words[0];
    words.erase(words.begin());
    if (first == "rupias") {
        return Rupees(words, s);
    }
    if (first == "nombres") {
        return Names(words, s);
    }
    // /hud ...
    if (words.empty()) {
        return Describe(s);
    }
    const std::string what = words[0];
    const std::vector<std::string> args(words.begin() + 1, words.end());
    if (what == "rupias") {
        return Rupees(args, s);
    }
    if (what == "nombres") {
        return Names(args, s);
    }
    if (what == "panel") {
        if (const auto place = ParsePlace(Rest(args, 0))) {
            s.panel_at = *place;
            return "HUD: panel (F9) " + std::string(PlaceName(s.panel_at));
        }
        return "HUD: /hud panel <sitio> (" + std::string(kPlaces) + "); nada cambia";
    }
    if (what == "tamano" || what == "tama\xc3\xb1o" || what == "size") {
        if (const auto size = args.size() == 1 ? ParseSize(args[0]) : std::nullopt) {
            s.size = *size;
            return "HUD: tama\xc3\xb1o " + std::to_string(s.size) + " %";
        }
        return "HUD: /hud tamano <" + std::to_string(kMinSize) + "-" + std::to_string(kMaxSize) +
               "> (por ciento); nada cambia";
    }
    if (what == "chat") {
        if (const auto lines = args.size() == 1 ? ParseChatLines(args[0]) : std::nullopt) {
            s.chat_lines = *lines;
            return "HUD: chat " + std::to_string(s.chat_lines) +
                   (s.chat_lines == 1 ? " l\xc3\xadnea" : " l\xc3\xadneas");
        }
        return "HUD: /hud chat <0-" + std::to_string(kMaxChatRows) + ">; nada cambia";
    }
    if (what == "reset") {
        s = file;
        return Describe(s) + " (coop-options.ini)";
    }
    return Usage();
}

namespace {

struct Live {
    std::mutex mutex;
    bool read = false;
    Settings file;
    Settings now;
};

Live& TheLive() {
    static Live live;
    return live;
}

// Under the lock.
void ReadOnce(Live& live) {
    if (live.read) {
        return;
    }
    live.read = true;
    std::vector<std::string> log;
    live.file = ReadSettings([](const char* name) { return CoopSwitch::Value(name); }, &log);
    live.now = live.file;
    for (const auto& line : log) {
        CoopSwitch::Log(line);
    }
    if (!(live.file == Settings{})) {
        ORM_LOG_INFO("coop: hud: {}", Describe(live.file));
    }
}

} // namespace

Settings LiveSettings() {
    Live& live = TheLive();
    std::lock_guard lock(live.mutex);
    ReadOnce(live);
    return live.now;
}

std::optional<std::string> OnChat(std::string_view text) {
    if (!IsCommand(text)) {
        return std::nullopt;
    }
    Live& live = TheLive();
    std::lock_guard lock(live.mutex);
    ReadOnce(live);
    auto answer = Command(text, live.now, live.file);
    if (answer) {
        ORM_LOG_INFO("coop: hud: {} -> {}", std::string(Trim(text)), Describe(live.now));
    }
    return answer;
}

} // namespace OrmHud
