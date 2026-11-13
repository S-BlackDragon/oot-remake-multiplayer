// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_ocarina_sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "coop_ocarina.h"

namespace OrmOcarinaSound {

namespace {

// main's .text (63,201,744 bytes): a function the game hands us must be in it.
constexpr std::uint64_t kMainTextBytes = 0x3c462d0;
constexpr std::size_t kMaxAsks = 64;

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

template <typename T>
bool ReadValue(CoopPuppet::Guest& guest, std::uint64_t address, T& out) {
    return address != 0 && guest.Read(address, &out, sizeof(T));
}

} // namespace

const std::vector<std::string>& GameNoteNames() {
    // sead enum text at main+0x3ee6ae0, offsets at +0x7c (20 values).
    static const std::vector<std::string> names{
        "Rest", "LowB",  "LowC",   "LowCsharp", "D",     "Dsharp",     "E",
        "F",    "Fsharp", "G",     "Gsharp",    "A",     "Asharp",     "B",
        "C",    "Csharp", "HighD", "HighDsharp", "HighE", "HighF"};
    return names;
}

std::uint8_t GameNote(std::string_view name) {
    const auto& names = GameNoteNames();
    for (std::size_t i = 1; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return 0;
}

const std::vector<std::string>& GameSongNames() {
    // sead enum text at main+0x3effdb0 (13 values).
    static const std::vector<std::string> names{
        "ZeldaLullaby",   "EponaSong",    "SariaSong",      "SunSong",       "TimeSong",
        "StormSong",      "ForestMinuet", "FireBolero",     "WaterSerenade", "SpiritRequiem",
        "ShadowNocturne", "LightPrelude", "ScarecrowSong"};
    return names;
}

std::optional<std::uint8_t> GameSong(std::string_view name) {
    const auto& names = GameSongNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return std::nullopt;
}

Ask AskOf(std::string_view command, std::uint8_t sounding) {
    Ask ask;
    if (!CoopOcarina::PlaysFreely(command)) {
        // Not the Ocarina any more (put away, hit, a cutscene): let go.
        if (sounding != 0) {
            ask.kind = Ask::Kind::stop;
        }
        return ask;
    }
    const auto cue = CoopOcarina::CueOf(command);
    if (!cue) {
        return ask;  // waiting, blowing, the idle fingers: as it was
    }
    if (cue->song != 0) {
        ask.kind = Ask::Kind::song;
        ask.song = CoopOcarina::SongNames()[cue->song];
        return ask;
    }
    const auto& names = CoopOcarina::NoteNames();
    const std::uint8_t note = cue->note < names.size() ? GameNote(names[cue->note]) : 0;
    if (note == 0) {
        return ask;
    }
    if (cue->rest) {
        // "<note>_Rest": the note is let go (only the one that sounds).
        if (sounding == note) {
            ask.kind = Ask::Kind::stop;
        }
        return ask;
    }
    if (sounding != note) {
        ask.kind = Ask::Kind::note;
        ask.note = note;
    }
    return ask;
}

Player::Player(Say say) : say_(std::move(say)) {
    // The Link puppets' director: the live slot of that player, if any.
    find_ = [](std::uint8_t number) -> std::uint64_t {
        if (number >= 32 || ((CoopPuppet::LivePuppets() >> number) & 1u) == 0) {
            return 0;
        }
        for (const auto& slot : CoopPuppet::TheDirector().Slots()) {
            if (slot.number == number && slot.phase == CoopPuppet::Phase::live) {
                return slot.actor;
            }
        }
        return 0;
    };
}

void Player::SetFindActor(FindActor find) {
    std::lock_guard lock(mutex_);
    find_ = std::move(find);
}

bool Player::ByMessage(std::uint8_t number) const {
    std::lock_guard lock(mutex_);
    return number < kSlots && voices_[number].by_message;
}

void Player::SetSay(Say say) {
    std::lock_guard lock(mutex_);
    say_ = std::move(say);
}

void Player::SetEnabled(bool on) {
    std::lock_guard lock(mutex_);
    enabled_ = on;
    if (!on) {
        asks_.clear();
    }
}

bool Player::Enabled() const {
    std::lock_guard lock(mutex_);
    return enabled_;
}

void Player::SetSongsOnly(bool on) {
    std::lock_guard lock(mutex_);
    songs_only_ = on;
}

bool Player::SongsOnly() const {
    std::lock_guard lock(mutex_);
    return songs_only_;
}

void Player::SetCue(std::uint8_t note, std::uint64_t hold_ms) {
    std::lock_guard lock(mutex_);
    cue_note_ = note < GameNoteNames().size() ? note : 0;
    cue_ms_ = hold_ms == 0 ? kCueMs : std::min(hold_ms, kHeldMaxMs);
    if (cue_note_ == 0) {
        DropQueued(kCueSlot);
    }
}

bool Player::CuesEnabled() const {
    std::lock_guard lock(mutex_);
    return cue_note_ != 0;
}

void Player::OnCue(std::uint64_t main_base, std::uint8_t mate, bool at_mate, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (cue_note_ == 0 || main_base == 0) {
        return;
    }
    // Only the newest cue: 0x335aa30 fades out what the handle still plays.
    DropQueued(kCueSlot);
    Voice& v = voices_[kCueSlot];
    v.note = cue_note_;
    v.main_base = main_base;
    v.hold_ms = cue_ms_;
    Request r;
    r.number = kCueSlot;
    r.main_base = main_base;
    r.at_ms = now_ms;
    r.cue = true;
    r.at_mate = at_mate;
    r.of = mate;
    r.ask.kind = Ask::Kind::note;
    r.ask.note = cue_note_;
    Enqueue(std::move(r));
}

std::uint8_t Player::Sounding(std::uint8_t number) const {
    std::lock_guard lock(mutex_);
    return number < kSlots ? voices_[number].playing : 0;
}

std::size_t Player::Waiting() const {
    std::lock_guard lock(mutex_);
    return asks_.size();
}

void Player::Tell(const std::string& text) const {
    if (say_) {
        say_("coop: ocarina sound: " + text);
    }
}

void Player::Enqueue(Request r) {
    // mutex_ held.
    if (asks_.size() >= kMaxAsks) {
        asks_.erase(asks_.begin());
    }
    asks_.push_back(std::move(r));
}

bool Player::DropQueued(std::uint8_t number) {
    // mutex_ held.
    const auto before = asks_.size();
    std::erase_if(asks_, [number](const Request& r) {
        return r.number == number && r.ask.kind != Ask::Kind::stop;
    });
    return asks_.size() != before;
}

void Player::OnCommand(std::uint64_t main_base, std::uint8_t number, std::uint64_t actor,
                       std::string_view command, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || main_base == 0 || number >= kSlots) {
        return;
    }
    Voice& v = voices_[number];
    Ask ask;
    if (v.by_message) {
        // OCARINA_NOTE says this player's notes: its command only lets the
        // note go when the Ocarina is put away (or a cutscene takes it).
        if (CoopOcarina::PlaysFreely(command) || (v.note == 0 && v.playing == 0)) {
            return;
        }
        ask.kind = Ask::Kind::stop;
    } else {
        ask = AskOf(command, v.note);
    }
    if (songs_only_ && ask.kind != Ask::Kind::song) {
        return;  // notes are upstream's (SetSongsOnly)
    }
    if (ask.kind == Ask::Kind::none) {
        return;
    }
    Request r;
    r.number = number;
    r.actor = actor;
    r.main_base = main_base;
    r.at_ms = now_ms;
    if (ask.kind == Ask::Kind::song && v.note != 0) {
        Request stop = r;
        stop.ask.kind = Ask::Kind::stop;
        Enqueue(std::move(stop));
    }
    v.note = ask.kind == Ask::Kind::note ? ask.note : 0;
    v.actor = actor;
    v.main_base = main_base;
    r.ask = ask;
    Enqueue(std::move(r));
}

void Player::OnNote(std::uint64_t main_base, const coop::protocol::OrmOcarinaNote& note,
                    std::uint64_t now_ms) {
    namespace proto = coop::protocol;
    const std::uint8_t number = note.player_number;
    std::lock_guard lock(mutex_);
    if (!enabled_ || main_base == 0 || number >= kSlots) {
        return;
    }
    Voice& v = voices_[number];
    if (v.serial && *v.serial == note.serial) {
        return;  // the same message again
    }
    v.serial = note.serial;
    if (songs_only_ && note.kind != coop::protocol::OcarinaKind::song) {
        return;  // notes are upstream's (SetSongsOnly); by_message stays as it was
    }
    v.by_message = true;
    v.main_base = main_base;
    if (note.instrument != addr::kInstrumentOcarina) {
        return;  // 0x335aa30 places only the Ocarina's sound at a spot
    }
    Request r;
    r.number = number;
    r.main_base = main_base;
    r.at_ms = now_ms;
    r.find = true;
    // A new message makes this player's older notes that have not played yet
    // pointless: dropped, so its notes stay in order and few.
    if (DropQueued(number)) {
        v.note = v.playing;
    }
    switch (note.kind) {
    case proto::OcarinaKind::note:
        if (note.note == 0 || note.note >= GameNoteNames().size() || v.note == note.note) {
            return;  // no note, or the one asked already
        }
        v.note = note.note;
        r.ask.kind = Ask::Kind::note;
        r.ask.note = note.note;
        break;
    case proto::OcarinaKind::release:
        // Always asked: a note whose call is out now has not set `playing`
        // yet; Play skips the stop when nothing sounds by then.
        v.note = 0;
        r.ask.kind = Ask::Kind::stop;
        break;
    case proto::OcarinaKind::song: {
        if (note.song >= GameSongNames().size()) {
            return;
        }
        Request stop = r;
        stop.ask.kind = Ask::Kind::stop;
        Enqueue(std::move(stop));
        v.note = 0;
        r.ask.kind = Ask::Kind::song;
        r.ask.song = GameSongNames()[note.song];
        break;
    }
    default:
        return;
    }
    Enqueue(std::move(r));
}

void Player::Forget(std::uint8_t number, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (number >= kSlots) {
        return;
    }
    Voice& v = voices_[number];
    v.serial.reset();
    v.by_message = false;
    v.said_no_puppet = false;
    DropQueued(number);
    if (enabled_ && v.main_base != 0 && (v.note != 0 || v.playing != 0)) {
        Request stop;
        stop.number = number;
        stop.main_base = v.main_base;
        stop.ask.kind = Ask::Kind::stop;
        stop.at_ms = now_ms;
        Enqueue(std::move(stop));
    }
    v.note = 0;
}

std::optional<CoopPuppet::Call> Player::Step(CoopPuppet::Guest& guest, bool after_call,
                                             std::uint64_t result, std::uint64_t now_ms) {
    bool active = false;
    {
        // The notes, the cues, or a cue still sounding after cues were turned off (let go).
        std::lock_guard lock(mutex_);
        active = enabled_ || cue_note_ != 0 || voices_[kCueSlot].playing != 0;
    }
    if (!active) {
        pending_ = Pending::none;
        return std::nullopt;
    }
    if (!after_call) {
        // A new frame: the user instance is asked again (as the game does).
        pending_ = Pending::none;
        current_.reset();
        user_ = 0;
        played_ = 0;
        noted_ = 0;
        {
            std::lock_guard lock(mutex_);
            for (std::uint8_t n = 0; n < kSlots; ++n) {
                Voice& v = voices_[n];
                const std::uint64_t most = v.hold_ms != 0 ? v.hold_ms : kHeldMaxMs;
                if (v.playing != 0 && v.note == v.playing && now_ms >= v.since_ms + most) {
                    Request stop;
                    stop.number = n;
                    stop.actor = v.actor;
                    stop.main_base = v.main_base;
                    stop.ask.kind = Ask::Kind::stop;
                    stop.at_ms = now_ms;
                    v.note = 0;
                    Enqueue(std::move(stop));
                }
            }
        }
        return Next(guest, now_ms);
    }
    const Pending was = std::exchange(pending_, Pending::none);
    switch (was) {
    case Pending::none:
        return std::nullopt;
    case Pending::malloc:
        if (result == 0) {
            retry_ms_ = now_ms + kRetryMs;
            Tell("malloc gave nothing; again in 2 s");
            return std::nullopt;
        }
        for (std::uint8_t n = 0; n < kSlots; ++n) {
            const std::uint64_t b = result + n * kSlotBytes;
            const std::array<std::uint8_t, kSlotBytes> zero{};
            guest.Write(b, zero.data(), zero.size());
            guest.Write(b + kNoteHandle, addr::kEmptyHandle.data(), addr::kEmptyHandle.size());
            guest.Write(b + kSongHandle, addr::kEmptyHandle.data(), addr::kEmptyHandle.size());
        }
        block_ = result;
        Tell("block at " + Hex(result));
        return Next(guest, now_ms);
    case Pending::sound: {
        // The holder of the "Ocarina" user and its getter, as 0x68a3b0 reads them.
        std::uint64_t holder = 0;
        std::uint64_t vtable = 0;
        std::uint64_t getter = 0;
        if (result == 0 || !ReadValue(guest, result + addr::kSoundUserHolder, holder) ||
            !ReadValue(guest, holder, vtable) ||
            !ReadValue(guest, vtable + addr::kHolderGetUser, getter) || getter < base_ ||
            getter >= base_ + kMainTextBytes) {
            retry_ms_ = now_ms + kRetryMs;
            if (!said_no_user_) {
                said_no_user_ = true;
                Tell("no Ocarina sound user yet (OcarinaSound " + Hex(result) + ", holder " +
                     Hex(holder) + ", getter " + Hex(getter) + "); asking again every 2 s");
            }
            return std::nullopt;
        }
        CoopPuppet::Call c;
        c.function = getter;
        c.x[0] = holder;
        pending_ = Pending::user;
        return c;
    }
    case Pending::user:
        if (result == 0) {
            retry_ms_ = now_ms + kRetryMs;
            if (!said_no_user_) {
                said_no_user_ = true;
                Tell("the Ocarina's holder gave no user instance; asking again every 2 s");
            }
            return std::nullopt;
        }
        if (said_no_user_) {
            said_no_user_ = false;
            Tell("Ocarina sound user at " + Hex(result));
        }
        user_ = result;
        return Next(guest, now_ms);
    case Pending::note:
        if (current_) {
            std::lock_guard lock(mutex_);
            Voice& v = voices_[current_->number];
            const bool stop = current_->ask.kind == Ask::Kind::stop;
            v.playing = stop ? 0 : current_->ask.note;
            v.since_ms = now_ms;
        }
        return Next(guest, now_ms);
    case Pending::song:
        if (current_ && (result & 1) != 0) {
            CoopPuppet::Call c;
            c.function = base_ + addr::kSetSoundPlace;
            c.x[0] = SlotBase(current_->number) + kSongHandle;
            c.x[1] = SlotBase(current_->number) + kPlace;
            pending_ = Pending::place;
            return c;
        }
        if (current_) {
            Tell("P" + std::to_string(current_->number) + " Melody_" + current_->ask.song +
                 " not played (searchAndEmit returned " + Hex(result & 0xff) + ")");
        }
        return Next(guest, now_ms);
    case Pending::place:
        return Next(guest, now_ms);
    }
    return std::nullopt;
}

std::optional<CoopPuppet::Call> Player::Next(CoopPuppet::Guest& guest, std::uint64_t now_ms) {
    current_.reset();
    while (played_ < kMaxPerFrame) {
        Request r;
        {
            std::lock_guard lock(mutex_);
            // A note or song this late would sound wrong: dropped (a stop never is).
            for (auto it = asks_.begin(); it != asks_.end();) {
                if (it->ask.kind == Ask::Kind::stop || now_ms <= it->at_ms + kStaleMs) {
                    ++it;
                    continue;
                }
                Voice& v = voices_[it->number];
                if (it->ask.kind == Ask::Kind::note && v.note == it->ask.note) {
                    v.note = v.playing;  // so the same note asked again sounds
                }
                it = asks_.erase(it);
            }
            if (asks_.empty()) {
                return std::nullopt;
            }
            if (now_ms < retry_ms_) {
                return std::nullopt;
            }
            // The first ask that may play now, in each player's own order: a
            // stop, or a note or song of a player with none yet this frame
            // (each sets the shared user's properties 0 and 1). A player whose
            // note waits waits whole: its later stop does not jump ahead of it.
            auto pick = asks_.end();
            std::uint32_t skipped = 0;
            for (auto it = asks_.begin(); it != asks_.end(); ++it) {
                const std::uint32_t bit = 1u << it->number;
                if ((skipped & bit) != 0) {
                    continue;
                }
                if (it->ask.kind == Ask::Kind::stop || (noted_ & bit) == 0) {
                    pick = it;
                    break;
                }
                skipped |= bit;
            }
            if (pick == asks_.end()) {
                return std::nullopt;  // the rest next frame
            }
            if (pick->main_base != base_) {
                block_ = 0;  // another game start: its block was the old process's
                base_ = pick->main_base;
            }
            if (block_ == 0) {
                CoopPuppet::Call c;
                c.function = base_ + CoopPuppet::addr::kMalloc;
                c.x[0] = kSlots * kSlotBytes;
                pending_ = Pending::malloc;
                return c;
            }
            if (user_ == 0) {
                CoopPuppet::Call c;
                c.function = base_ + addr::kGetOcarinaSound;
                pending_ = Pending::sound;
                return c;
            }
            r = std::move(*pick);
            asks_.erase(pick);
        }
        ++played_;
        if (r.ask.kind != Ask::Kind::stop) {
            noted_ |= 1u << r.number;
        }
        if (auto c = Play(guest, r)) {
            current_ = std::move(r);
            return c;
        }
    }
    return std::nullopt;
}

std::optional<CoopPuppet::Call> Player::Play(CoopPuppet::Guest& guest, const Request& r) {
    const std::uint64_t slot = SlotBase(r.number);
    const std::string who = "P" + std::to_string(r.number);
    if (r.ask.kind == Ask::Kind::stop) {
        {
            std::lock_guard lock(mutex_);
            if (voices_[r.number].playing == 0) {
                return std::nullopt;  // nothing sounds
            }
        }
        // Note 0 (Rest): 0x335aa30 fades the handle out and clears it; it
        // reads no other argument then.
        CoopPuppet::Call c;
        c.function = base_ + addr::kPlayNote;
        c.x[0] = slot + kNoteHandle;
        c.x[1] = slot + kPlace;
        c.x[2] = addr::kInstrumentOcarina;
        c.x[3] = 0;
        c.x[4] = user_;
        c.x[5] = addr::kModeFree;
        pending_ = Pending::note;
        Tell(r.number == kCueSlot ? std::string("the mate-down cue note off") : who + " note off");
        return c;
    }
    if (r.cue) {
        // The mate-down cue: at his puppet, or a NaN place (the 2D copy).
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::array<float, 3> place{nan, nan, nan};
        std::string where = "on the interface";
        if (r.at_mate) {
            FindActor find;
            {
                std::lock_guard lock(mutex_);
                find = find_;
            }
            const std::uint64_t actor = find ? find(r.of) : 0;
            std::array<float, 3> at{};
            if (actor != 0 &&
                guest.Read(actor + CoopPuppet::addr::kActorPosition, at.data(), sizeof(at)) &&
                std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2])) {
                place = at;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "at its puppet (%.1f, %.1f, %.1f)", at[0], at[1], at[2]);
                where = buf;
            } else {
                where = "on the interface (no puppet place here)";
            }
        }
        if (!guest.Write(slot + kPlace, place.data(), sizeof(place))) {
            std::lock_guard lock(mutex_);
            voices_[r.number].note = voices_[r.number].playing;
            return std::nullopt;
        }
        CoopPuppet::Call c;
        c.function = base_ + addr::kPlayNote;
        c.x[0] = slot + kNoteHandle;
        c.x[1] = slot + kPlace;
        c.x[2] = addr::kInstrumentOcarina;
        c.x[3] = r.ask.note;
        c.x[4] = user_;
        c.x[5] = addr::kModeFree;
        c.s0 = 0.0f;  // no bend
        pending_ = Pending::note;
        Tell("P" + std::to_string(r.of) + " is down: cue note " + GameNoteNames()[r.ask.note] + " " + where);
        return c;
    }
    std::uint64_t actor = r.actor;
    if (r.find) {
        FindActor find;
        {
            std::lock_guard lock(mutex_);
            find = find_;
        }
        actor = find ? find(r.number) : 0;
    }
    std::array<float, 3> place{};
    if (actor == 0 ||
        !guest.Read(actor + CoopPuppet::addr::kActorPosition, place.data(), sizeof(place)) ||
        !std::isfinite(place[0]) || !std::isfinite(place[1]) || !std::isfinite(place[2]) ||
        !guest.Write(slot + kPlace, place.data(), sizeof(place))) {
        bool say = true;
        {
            std::lock_guard lock(mutex_);
            Voice& v = voices_[r.number];
            if (r.ask.kind == Ask::Kind::note && v.note == r.ask.note) {
                v.note = v.playing;  // not played: the same note asked again may sound
            }
            if (r.find) {
                say = !v.said_no_puppet;  // a player with no puppet here: one line, not one a note
                v.said_no_puppet = true;
            }
        }
        if (say) {
            Tell(who + ": no place for its puppet " + Hex(actor) + "; not played");
        }
        return std::nullopt;
    }
    if (r.find) {
        std::lock_guard lock(mutex_);
        voices_[r.number].said_no_puppet = false;
    }
    char at[64];
    std::snprintf(at, sizeof(at), " at (%.1f, %.1f, %.1f)", place[0], place[1], place[2]);
    if (r.ask.kind == Ask::Kind::note) {
        CoopPuppet::Call c;
        c.function = base_ + addr::kPlayNote;
        c.x[0] = slot + kNoteHandle;
        c.x[1] = slot + kPlace;  // a real place, never NaN: not the 2D System_ copy
        c.x[2] = addr::kInstrumentOcarina;
        c.x[3] = r.ask.note;
        c.x[4] = user_;
        c.x[5] = addr::kModeFree;
        c.s0 = 0.0f;  // no bend
        pending_ = Pending::note;
        Tell(who + " note " + GameNoteNames()[r.ask.note] + at);
        return c;
    }
    if (r.ask.kind == Ask::Kind::song) {
        const std::string name = "Melody_" + r.ask.song;
        std::array<char, kNameBytes> text{};
        std::memcpy(text.data(), name.data(), std::min(name.size(), kNameBytes - 1));
        if (!guest.Write(slot + kName, text.data(), text.size())) {
            return std::nullopt;
        }
        CoopPuppet::Call c;
        c.function = base_ + addr::kSearchAndEmit;
        c.x[0] = user_;
        c.x[1] = slot + kName;
        c.x[2] = slot + kSongHandle;
        c.x[3] = 0;
        pending_ = Pending::song;
        Tell(who + " " + name + at);
        return c;
    }
    return std::nullopt;
}

Player& ThePlayer() {
    static Player player;
    return player;
}

} // namespace OrmOcarinaSound

#include "platform_env.h"  // ORM_PLATFORM_LIVE

#if !ORM_PLATFORM_LIVE

namespace OrmOcarinaSound {
void Configure() {}
bool UpstreamNotes() { return true; }
} // namespace OrmOcarinaSound

#else  // the hook, built into what runs the game

#include "coop_switch.h"  // CoopSwitch (through platform_env.h)
#include "platform.h"     // AddCaller, Env, ORM_LOG_*

namespace OrmOcarinaSound {

namespace {

constexpr int kCallerOrder = 70;  // after the puppets, spawn, fairy, objects, boss, story, hook

std::string Env(const char* name) {
    return CoopPlatform::Env(name).value_or(std::string());
}

} // namespace

bool UpstreamNotes() {
    // ORM test10: LICHII_ORM_OCARINA_NOTES=1 gives the notes to ours (and the
    // puppet director then skips upstream's OCARINA_NOTE); else upstream's.
    static const bool upstream = !CoopSwitch::On("LICHII_ORM_OCARINA_NOTES", false);
    return upstream;
}

void Configure() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const std::string v = Env("LICHII_COOP_OCARINA_SOUND");
    const bool notes = !(v.empty() || v == "0");
    // ORM "a mate is down" (coop/host/mate_down_sound.hpp): its cue note on the same user.
    const bool cues = CoopSwitch::On("LICHII_ORM_MATE_DOWN_SOUND", false);  // coop_switch.h
    if (!notes) {
        ORM_LOG_INFO("coop: ocarina sound: off (LICHII_COOP_OCARINA_SOUND=1 plays the others' notes)");
    }
    if (!notes && !cues) {
        return;
    }
    // ThePlayer() is a function-local static: the cave calls it for the whole run.
    ThePlayer().SetSay([](const std::string& line) { ORM_LOG_INFO("{}", line); });
    if (notes) {
        ThePlayer().SetEnabled(true);
        const bool songs_only = UpstreamNotes();
        ThePlayer().SetSongsOnly(songs_only);
        if (songs_only) {
            ORM_LOG_INFO("coop: ocarina sound: on, songs only: the others' notes are upstream's "
                         "(OCARINA_NOTE 140), ours plays their songs at their puppets (game user "
                         "\"Ocarina\", main+{:#x}; LICHII_ORM_OCARINA_NOTES=1 gives ours the notes)",
                         addr::kPlayNote);
        } else {
            ORM_LOG_INFO("coop: ocarina sound: on, notes and songs by ours (LICHII_ORM_OCARINA_NOTES=1; "
                         "upstream's OCARINA_NOTE is not played), game user \"Ocarina\", main+{:#x}",
                         addr::kPlayNote);
        }
    }
    if (cues) {
        std::string name(kMateDownNote);
        if (const auto n = CoopSwitch::Value("LICHII_ORM_MATE_DOWN_NOTE"); n && GameNote(*n) != 0) {
            name = *n;
        }
        ThePlayer().SetCue(GameNote(name));
        ORM_LOG_INFO("coop: ocarina sound: a mate's fall plays the note {} for {} ms "
                     "(LICHII_ORM_MATE_DOWN_SOUND; at his puppet when near, else on the interface)",
                     name, Player::kCueMs);
    }
    CoopPlatform::AddCaller(kCallerOrder, [](CoopPuppet::Guest& guest, bool after_call,
                                             std::uint64_t result, std::uint64_t now_ms) {
        return ThePlayer().Step(guest, after_call, result, now_ms);
    });
}

} // namespace OrmOcarinaSound

#endif  // ORM_PLATFORM_LIVE
