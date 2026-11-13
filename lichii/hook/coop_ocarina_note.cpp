// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_ocarina_note.h"

#include <algorithm>
#include <cstdio>

#include "coop/host/game_memory.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_ocarina.h"
#include "orm_ocarina_sound.h"

namespace CoopOcarinaNote {

namespace proto = coop::protocol;

std::optional<Control> ReadControl(coop::host::GameMemory& memory) {
    using coop::host::read_ptr;
    using coop::host::read_u32;
    using coop::host::read_u64;
    using coop::host::read_u8;
    const std::uint64_t base = memory.main_base();
    if (base == 0) {
        return std::nullopt;
    }
    const auto root = read_ptr(memory, base + addr::kGameRoot);
    if (!root) {
        return std::nullopt;
    }
    const auto table = read_ptr(memory, *root + addr::kRootTable);
    if (!table) {
        return std::nullopt;
    }
    const auto count = read_u32(memory, *table + addr::kTableCount);
    if (!count || static_cast<std::int32_t>(*count) < addr::kMinCount) {
        return std::nullopt;
    }
    const auto array = read_ptr(memory, *table + addr::kTableArray);
    if (!array) {
        return std::nullopt;
    }
    const auto object = read_ptr(memory, *array + addr::kControlEntry);
    if (!object) {
        return std::nullopt;
    }
    const auto vtable = read_ptr(memory, *object);
    if (!vtable) {
        return std::nullopt;
    }
    const auto check = read_u64(memory, *vtable);
    if (!check || *check != base + addr::kControlTypeCheck) {
        return std::nullopt;  // another object in that entry
    }
    const std::uint64_t ctl = *object;
    const auto state = read_u32(memory, ctl + addr::kState);
    const auto note = read_u32(memory, ctl + addr::kNote);
    const auto other = read_u32(memory, ctl + addr::kOtherNote);
    const auto use_other = read_u8(memory, ctl + addr::kUseOtherNote);
    const auto mute = read_u8(memory, ctl + addr::kMute);
    const auto instrument = read_u32(memory, ctl + addr::kInstrument);
    const auto song = read_u32(memory, ctl + addr::kSong);
    if (!state || !note || !other || !use_other || !mute || !instrument || !song) {
        return std::nullopt;
    }
    Control c;
    c.address = ctl;
    c.state = *state;
    c.note = *note;
    c.other_note = *other;
    c.use_other_note = *use_other;
    c.mute = *mute;
    c.instrument = *instrument;
    c.song = *song;
    return c;
}

std::uint8_t SoundingNote(const Control& c) {
    if (c.state == 0 || c.state == 5 || c.state == 6) {
        return 0;
    }
    const bool other = (c.state < 32 && ((1u << c.state) & addr::kOtherNoteStates) != 0) ||
                       c.state == addr::kOtherNoteState || c.use_other_note == 1;
    const std::uint32_t note = other ? c.other_note : c.note;
    if (c.mute != 0 || note > proto::kMaxOcarinaNote) {
        return 0;
    }
    return static_cast<std::uint8_t>(note);
}

void Sender::Song(std::uint8_t song, std::uint64_t now_ms) {
    if (song > proto::kMaxOcarinaSong) {
        return;
    }
    if (last_song_sent_ && *last_song_sent_ == song && now_ms < last_song_ms_ + kSongAgainMs) {
        return;  // the same song again so soon: the one sent stands
    }
    pending_song_ = song;
}

std::optional<proto::OrmOcarinaNote> Sender::Frame(const Look& look) {
    const std::uint64_t now = look.now_ms;
    if (!tokens_ms_) {
        tokens_ms_ = now;
    }
    if (now > *tokens_ms_) {
        tokens_ = std::min(kBurst, tokens_ + static_cast<double>(now - *tokens_ms_) * kPerSecond / 1000.0);
        tokens_ms_ = now;
    }

    const bool ocarina = look.control ? look.control->instrument == 0 : true;
    // "Ocarina..." or the remake's "DemoOcarina" (CoopOcarina::PlaysFreely, docs/SONDAS.md).
    const bool playing = look.command && CoopOcarina::PlaysFreely(*look.command) && ocarina;
    if (playing) {
        playing_ms_ = now;
    }
    const bool recent = playing || (playing_ms_ && now <= *playing_ms_ + kSongAfterMs && ocarina);

    if (look.control) {
        wanted_ = playing ? SoundingNote(*look.control) : 0;
        const std::uint32_t song = look.control->song;
        // A song on a change only: the word keeps the last one recognised
        // until the game puts 13 back, and holds one already when we start.
        if (last_song_word_ && song != *last_song_word_ && song <= proto::kMaxOcarinaSong &&
            recent) {
            Song(static_cast<std::uint8_t>(song), now);
        }
        last_song_word_ = song;
    } else {
        last_song_word_.reset();
        if (!playing) {
            wanted_ = 0;
        } else if (*look.command != last_command_) {
            // The fallback: the note or song our own AS node names.
            const auto ask = OrmOcarinaSound::AskOf(*look.command, wanted_);
            switch (ask.kind) {
            case OrmOcarinaSound::Ask::Kind::note:
                wanted_ = ask.note;
                break;
            case OrmOcarinaSound::Ask::Kind::stop:
                wanted_ = 0;
                break;
            case OrmOcarinaSound::Ask::Kind::song:
                if (const auto s = OrmOcarinaSound::GameSong(ask.song)) {
                    Song(*s, now);
                }
                wanted_ = 0;
                break;
            case OrmOcarinaSound::Ask::Kind::none:
                break;
            }
        }
    }
    last_command_ = look.command.value_or(std::string());

    if (tokens_ < 1.0) {
        return std::nullopt;  // waits; the latest state goes when there is room
    }
    proto::OrmOcarinaNote m;
    m.instrument = 0;
    if (pending_song_) {
        // The receiver lets the note go before a song.
        m.kind = proto::OcarinaKind::song;
        m.note = 0;
        m.song = *pending_song_;
        pending_song_.reset();
        last_song_sent_ = m.song;
        last_song_ms_ = now;
        sent_note_ = 0;
    } else if (wanted_ != sent_note_ && songs_only_) {
        sent_note_ = wanted_;  // upstream's OCARINA_NOTE carries the notes
        return std::nullopt;
    } else if (wanted_ != sent_note_) {
        if (wanted_ != 0) {
            m.kind = proto::OcarinaKind::note;
            m.note = wanted_;
        } else {
            m.kind = proto::OcarinaKind::release;
            m.note = 0;
        }
        m.song = 0;
        sent_note_ = wanted_;
    } else {
        return std::nullopt;
    }
    m.serial = ++serial_;
    tokens_ -= 1.0;
    return m;
}

void Sender::Reset() {
    const std::uint8_t serial = serial_;
    const bool songs_only = songs_only_;
    *this = Sender{};
    serial_ = serial;
    songs_only_ = songs_only;
}

std::string Describe(const proto::OrmOcarinaNote& note) {
    switch (note.kind) {
    case proto::OcarinaKind::note: {
        const auto& names = OrmOcarinaSound::GameNoteNames();
        return "note " + (note.note < names.size() ? names[note.note] : std::to_string(note.note));
    }
    case proto::OcarinaKind::release:
        return "release";
    case proto::OcarinaKind::song: {
        const auto& names = OrmOcarinaSound::GameSongNames();
        return "song " + (note.song < names.size() ? names[note.song] : std::to_string(note.song));
    }
    }
    return "kind " + std::to_string(static_cast<int>(note.kind));
}

std::optional<SongState> ReadSongState(coop::host::GameMemory& memory, std::uint64_t ctl) {
    using coop::host::read_u32;
    using coop::host::read_u8;
    if (ctl == 0) {
        return std::nullopt;
    }
    SongState s;
    const auto word = [&](std::uint64_t off, std::uint32_t& out) {
        const auto v = read_u32(memory, ctl + off);
        if (v) {
            out = *v;
        }
        return v.has_value();
    };
    std::uint32_t count = 0;
    std::uint32_t count_b = 0;
    bool ok = word(addr::kState, s.state) && word(song_addr::kMode, s.mode) && word(addr::kSong, s.song) &&
              word(song_addr::kSongB, s.song_b) && word(song_addr::kSongC, s.song_c) &&
              word(song_addr::kCount, count) && word(song_addr::kCountB, count_b);
    for (std::size_t i = 0; ok && i < song_addr::kRingSize; ++i) {
        ok = word(song_addr::kRing + 4 * i, s.ring[i]) && word(song_addr::kRingB + 4 * i, s.ring_b[i]);
    }
    for (std::size_t i = 0; ok && i < song_addr::kSongs; ++i) {
        ok = word(song_addr::kHashes + 4 * i, s.hashes[i]);
        const auto k = read_u8(memory, ctl + song_addr::kKnown + i);
        ok = ok && k.has_value();
        if (k) {
            s.known[i] = *k;
        }
    }
    if (!ok) {
        return std::nullopt;
    }
    s.count = static_cast<std::int32_t>(count);
    s.count_b = static_cast<std::int32_t>(count_b);
    return s;
}

namespace {

std::string SongName(std::uint32_t song) {
    const auto& names = OrmOcarinaSound::GameSongNames();
    if (song == addr::kNoSong) {
        return "-";
    }
    return song < names.size() ? names[song] : std::to_string(song);
}

std::string RingText(const std::uint32_t (&ring)[song_addr::kRingSize], std::int32_t count) {
    // The game reads the newest note at (count - 1) mod 8, older ones before it.
    const auto& names = OrmOcarinaSound::GameNoteNames();
    const std::int32_t n = std::clamp<std::int32_t>(count, 0, static_cast<std::int32_t>(song_addr::kRingSize));
    std::string out;
    for (std::int32_t k = n; k >= 1; --k) {
        const auto idx = static_cast<std::size_t>(((count - k) % 8 + 8) % 8);
        const std::uint32_t v = ring[idx];
        out += (out.empty() ? "" : " ") + (v < names.size() ? names[v] : std::to_string(v));
    }
    return out.empty() ? "-" : out;
}

} // namespace

std::string DescribeSongState(const SongState& s) {
    std::string known;
    const auto& names = OrmOcarinaSound::GameSongNames();
    for (std::size_t i = 0; i < song_addr::kSongs; ++i) {
        if (s.known[i] == 1) {
            known += (known.empty() ? "" : ",") + (i < names.size() ? names[i] : std::to_string(i));
        } else if (s.known[i] != 0) {
            known += (known.empty() ? "" : ",") + std::to_string(i) + "=" + std::to_string(s.known[i]);
        }
    }
    return "state " + std::to_string(s.state) + " mode " + std::to_string(s.mode) + " song " + SongName(s.song) +
           "/" + SongName(s.song_b) + "/" + SongName(s.song_c) + " count " + std::to_string(s.count) + " ring " +
           RingText(s.ring, s.count) + " count2 " + std::to_string(s.count_b) + " ring2 " +
           RingText(s.ring_b, s.count_b) + " known " + (known.empty() ? "none" : known);
}

std::vector<std::string> SongKnowledge(const SongState& s,
                                       const std::function<std::optional<bool>(std::uint32_t)>& flag) {
    // sondas7: the hash at +0xe0 + 4 i is the song's item name (Item_Melody_<song>); 0x3025f20 reads
    // its IsGet flag.
    std::vector<std::string> out;
    const auto& names = OrmOcarinaSound::GameSongNames();
    for (std::size_t i = 0; i < song_addr::kSongs && i < names.size(); ++i) {
        const std::string item = "Item_Melody_" + names[i];
        const bool ours = s.hashes[i] == proto::flag_hash(item);
        const auto value = flag && ours ? flag(proto::flag_hash("IsGet." + item)) : std::nullopt;
        char hex[16];
        std::snprintf(hex, sizeof hex, "0x%08x", s.hashes[i]);
        if (!ours) {
            out.push_back(names[i] + " (hash " + hex + " is not " + item + ", known " + std::to_string(s.known[i]) +
                          ")");
        } else if (value && (*value ? 1 : 0) != s.known[i]) {
            out.push_back(names[i] + " (IsGet." + item + " " + (*value ? "1" : "0") + ", known " +
                          std::to_string(s.known[i]) + ": set when the Ocarina comes out)");
        }
    }
    return out;
}

} // namespace CoopOcarinaNote
