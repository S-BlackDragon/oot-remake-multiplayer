// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM cofre (oot-remake docs/DETALLES_MARIONETA.md, "Cofres"): short notices
// for the in-game HUD, "<nombre> encontró <objeto>", each shown kShowMs and
// at most kMaxLines at once (the newest), as oot-64's D-049 ("los demás ven
// '<nombre> ha obtenido: <objeto>' 5 s", never the finder's text box). They
// never take the player's input or pause anything: the HUD only draws them
// (orm_hud.h Model::notices). Header only, any thread; the tests build it
// without the emulator (tests/lichii/hud_test.cpp).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CoopNotices {

inline constexpr std::uint64_t kShowMs = 5000;
inline constexpr std::size_t kMaxLines = 3;

struct Line {
    std::uint8_t number = 0;  // the player it is about (his colour); 0 none
    std::string name;         // drawn in his colour; "" none
    std::string rest;         // after the name: " encontró una llave pequeña"
    std::uint64_t until_ms = 0;
};

class Board {
public:
    // `text` starts with `name` when the notice is about a player: the name
    // part is kept apart (for its colour). `now_ms`: a steady clock.
    void Add(std::uint8_t number, std::string_view name, std::string_view text, std::uint64_t now_ms,
             std::uint64_t show_ms = kShowMs) {
        Line l;
        l.number = number;
        if (!name.empty() && text.substr(0, name.size()) == name) {
            l.name = std::string(name);
            l.rest = std::string(text.substr(name.size()));
        } else {
            l.rest = std::string(text);
        }
        l.until_ms = now_ms + show_ms;
        std::lock_guard lock(mutex_);
        lines_.push_back(std::move(l));
        if (lines_.size() > 16) {
            lines_.erase(lines_.begin());
        }
    }
    // The lines still shown at `now_ms`, oldest first, at most kMaxLines.
    std::vector<Line> Current(std::uint64_t now_ms) {
        std::lock_guard lock(mutex_);
        std::erase_if(lines_, [&](const Line& l) { return l.until_ms <= now_ms; });
        const std::size_t skip = lines_.size() > kMaxLines ? lines_.size() - kMaxLines : 0;
        return std::vector<Line>(lines_.begin() + static_cast<std::ptrdiff_t>(skip), lines_.end());
    }
    void Clear() {
        std::lock_guard lock(mutex_);
        lines_.clear();
    }

private:
    std::mutex mutex_;
    std::vector<Line> lines_;
};

inline Board& TheBoard() {
    static Board board;
    return board;
}

// ORM "Guardado" (oot-64 D-054: "Guardado" 2 s abajo a la derecha, solo en la
// pantalla de quien guarda; oot-remake docs/PARTIDA_SALA.md): one short word in
// the picture's bottom right corner, the newest only, for kSavedMs. Shown when
// the app's helper has sent the room's save while the game runs (its event
// room_saved, lichii/app), so only on the screen of the PC that sent it.
inline constexpr std::uint64_t kSavedMs = 2000;
inline constexpr std::string_view kSavedText = "Guardado";

class Corner {
public:
    void Show(std::string_view text, std::uint64_t now_ms, std::uint64_t show_ms) {
        std::lock_guard lock(mutex_);
        text_ = std::string(text);
        until_ms_ = now_ms + show_ms;
    }
    // The word shown at `now_ms`; "" none.
    std::string Current(std::uint64_t now_ms) {
        std::lock_guard lock(mutex_);
        if (now_ms >= until_ms_) {
            text_.clear();
        }
        return text_;
    }

private:
    std::mutex mutex_;
    std::string text_;
    std::uint64_t until_ms_ = 0;
};

inline Corner& TheCorner() {
    static Corner corner;
    return corner;
}

// ORM the room's age (2026-11-13, oot-remake docs/EDAD.md): one big
// notice in the middle of the picture, the newest replacing the one shown:
// "<nombre> ha cambiado de época: moviendo al grupo al Templo del Tiempo..."
// with a second line under it (a countdown "3", "2", "1", or when we will
// move), "Moviendo al grupo contigo...", "Esperando a <nombre> para cambiar de
// época...", "Grupo reunido en el Templo del Tiempo". Readable from afar: the
// HUD draws it larger than the other notices (orm_hud.h Model::banner).
struct BannerLine {
    std::uint8_t number = 0;  // the player it names (his colour); 0 none
    std::string name;         // drawn in his colour; "" none
    std::string rest;         // after the name
    std::string detail;       // the second line; "" none
};

class Banner {
public:
    // `text` starts with `name` when the notice names a player.
    void Show(std::uint8_t number, std::string_view name, std::string_view text, std::string_view detail,
              std::uint64_t now_ms, std::uint64_t show_ms) {
        BannerLine l;
        l.number = number;
        if (!name.empty() && text.substr(0, name.size()) == name) {
            l.name = std::string(name);
            l.rest = std::string(text.substr(name.size()));
        } else {
            l.rest = std::string(text);
        }
        l.detail = std::string(detail);
        std::lock_guard lock(mutex_);
        line_ = std::move(l);
        until_ms_ = now_ms + show_ms;
    }
    // The notice shown at `now_ms`, if any.
    std::optional<BannerLine> Current(std::uint64_t now_ms) {
        std::lock_guard lock(mutex_);
        if (now_ms >= until_ms_) {
            return std::nullopt;
        }
        return line_;
    }
    void Clear() {
        std::lock_guard lock(mutex_);
        until_ms_ = 0;
    }

private:
    std::mutex mutex_;
    BannerLine line_;
    std::uint64_t until_ms_ = 0;
};

inline Banner& TheBanner() {
    static Banner banner;
    return banner;
}

// Any thread (the app's helper events, the Qt thread): "Guardado" for kSavedMs
// in the bottom right corner, with one log line. In coop_hud_game.cpp (the
// emulator only).
void ShowSaved();

}  // namespace CoopNotices
