// SPDX-License-Identifier: GPL-3.0-or-later
//
// OCARINA_NOTE, the emulator side (coop_ocarina_note.h). Glue only: the
// sender's rules are CoopOcarinaNote::Sender, the receiver's
// OrmOcarinaSound::Player::OnNote.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/game_flags.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop/protocol/messages.hpp"
#include "coop_every.h"
#include "coop_ocarina.h"
#include "coop_ocarina_note.h"
#include "orm_ocarina_sound.h"
#include "coop_switch.h"
#include "platform.h"  // Platform, ORM_LOG_*

namespace CoopOcarinaNote {
namespace {

namespace proto = coop::protocol;

constexpr std::size_t kMaxInbox = 64;
constexpr std::uint8_t kEveryone = 0xff;  // a forget for every player (a new connection)

struct Inbound {
    std::optional<proto::OrmOcarinaNote> note;  // a note; otherwise a forget
    std::uint8_t forget = 0;                 // with no note: this player, or kEveryone
};

struct Glue {
    std::once_flag checked;
    bool send = false;
    std::mutex inbox_mutex;
    std::vector<Inbound> inbox;  // guarded by inbox_mutex
    Sender sender;               // the present thread
    bool connected = false;
    bool said_control = false;   // "control at" / "no control" logged since the last change
    bool had_control = false;
    // LICHII_COOP_OCARINA_PROBE: the control's song check, on each change while
    // we play freely (docs/SONDAS.md round 4); the knowledge line once a take-out.
    bool song_probe = false;
    std::string last_song_state;
    bool said_knowledge = false;
    int song_lines = 0;
};

Glue& TheGlue() {
    static Glue glue;
    std::call_once(glue.checked, [] {
        // Unset: as LICHII_COOP_OCARINA_SOUND (empty or 0 off, anything else
        // on, as orm_ocarina_sound.cpp reads it): a game that plays the
        // others' notes sends its own.
        const auto sound = CoopSwitch::Value("LICHII_COOP_OCARINA_SOUND");
        const bool sound_on = sound && *sound != "0";
        // LICHII_ORM_OCARINA_NOTE, or LICHII_COOP_OCARINA_NOTE (D-R65 names).
        glue.send = CoopSwitch::On("LICHII_ORM_OCARINA_NOTE", "LICHII_COOP_OCARINA_NOTE", sound_on);
        // ORM test10: upstream sends our notes itself (OCARINA_NOTE 140, from the
        // note buttons); ours then sends only the songs it lacks, unless
        // LICHII_ORM_OCARINA_NOTES=1 gave the notes back to ours.
        const bool songs_only = OrmOcarinaSound::UpstreamNotes();
        glue.sender.SetSongsOnly(songs_only);
        const auto probe = CoopSwitch::Value("LICHII_COOP_OCARINA_PROBE");
        glue.song_probe = probe && *probe != "0";
        ORM_LOG_INFO("coop: ocarina note: {} (LICHII_ORM_OCARINA_NOTE; our notes read at main+{:#x}, "
                     "ORM OCARINA_NOTE {:#x}; {})",
                     glue.send ? "our notes are sent" : "ours not sent", addr::kGameRoot,
                     static_cast<int>(proto::MessageType::orm_ocarina_note),
                     songs_only ? "songs only: the notes go as upstream's OCARINA_NOTE 140"
                                : "notes and songs (LICHII_ORM_OCARINA_NOTES=1)");
    });
    return glue;
}

// The song check's words while we play freely (LICHII_COOP_OCARINA_PROBE):
// one line each time they change (at most kMaxSongLines), and once per
// take-out the songs our flags know but the control does not.
constexpr int kMaxSongLines = 160;

void SongProbe(Glue& g, coop::host::GameMemory& memory, const Look& look, bool playing) {
    if (!playing || !look.control) {
        g.said_knowledge = false;
        g.last_song_state.clear();
        return;
    }
    const auto st = ReadSongState(memory, look.control->address);
    if (!st) {
        if (g.last_song_state != "unreadable" && g.song_lines < kMaxSongLines) {
            ++g.song_lines;
            g.last_song_state = "unreadable";
            ORM_LOG_INFO("coop: ocarina song probe: the song words of the control at {:#x} cannot be read",
                         look.control->address);
        }
        return;
    }
    const std::string text = DescribeSongState(*st);
    if (text != g.last_song_state && g.song_lines < kMaxSongLines) {
        ++g.song_lines;
        g.last_song_state = text;
        ORM_LOG_INFO("coop: ocarina song probe: {}", text);
    }
    if (!g.said_knowledge) {
        g.said_knowledge = true;
        coop::host::GameFlags flags(memory);
        const auto odd = SongKnowledge(*st, [&](std::uint32_t h) { return flags.get_bool(h); });
        std::string joined;
        for (const auto& line : odd) {
            joined += (joined.empty() ? "" : "; ") + line;
        }
        ORM_LOG_INFO("coop: ocarina song probe: knowledge: {}",
                     joined.empty() ? "every known byte matches its IsGet.Item_Melody_ flag (hashes are the songs' item names)" : joined);
    }
}

void Push(Glue& g, Inbound in) {
    std::lock_guard lock(g.inbox_mutex);
    if (g.inbox.size() >= kMaxInbox) {
        g.inbox.erase(g.inbox.begin());
    }
    g.inbox.push_back(std::move(in));
}

} // namespace

void OnEvent(const coop::net::Event& event) {
    using coop::net::EventKind;
    Glue& g = TheGlue();
    switch (event.kind) {
    case EventKind::message:
        if (const auto* m = std::get_if<proto::OrmOcarinaNote>(&event.message)) {
            Push(g, Inbound{*m, 0});
        }
        return;
    case EventKind::player_joined:
    case EventKind::player_left:
        Push(g, Inbound{std::nullopt, event.player});
        return;
    case EventKind::welcomed:
    case EventKind::reconnected:
    case EventKind::disconnected:
        Push(g, Inbound{std::nullopt, kEveryone});
        return;
    default:
        return;
    }
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading) {
    Glue& g = TheGlue();
    const std::uint64_t now_ms = CoopEvery::NowUs() / 1000;  // the cave's clock (steady, ms)

    std::vector<Inbound> inbox;
    {
        std::lock_guard lock(g.inbox_mutex);
        inbox.swap(g.inbox);
    }
    auto& player = OrmOcarinaSound::ThePlayer();
    for (const Inbound& in : inbox) {
        if (in.note) {
            if (main_base != 0) {
                player.OnNote(main_base, *in.note, now_ms);
            }
        } else if (in.forget == kEveryone) {
            for (std::uint8_t n = 0; n < OrmOcarinaSound::Player::kSlots; ++n) {
                player.Forget(n, now_ms);
            }
        } else {
            player.Forget(in.forget, now_ms);
        }
    }

    if (!g.send) {
        return;
    }
    if (session == nullptr || main_base == 0) {
        if (g.connected) {
            g.sender.Reset();
            g.connected = false;
        }
        return;
    }
    g.connected = true;
    CoopPlatform::GuestMemory& memory = platform.Memory();
    Look look;
    look.control = ReadControl(memory);
    if (reading && reading->animation) {
        look.command = reading->animation->name;
    }
    look.now_ms = now_ms;
    const bool playing = look.command && CoopOcarina::PlaysFreely(*look.command);
    if (playing && (!g.said_control || g.had_control != look.control.has_value())) {
        g.said_control = true;
        g.had_control = look.control.has_value();
        if (look.control) {
            ORM_LOG_INFO("coop: ocarina note: our Ocarina control at {:#x} (state {}, instrument {})",
                         look.control->address, look.control->state, look.control->instrument);
        } else {
            ORM_LOG_INFO("coop: ocarina note: no Ocarina control at main+{:#x}; our notes come from "
                         "our AS command",
                         addr::kGameRoot);
        }
    }
    if (g.song_probe) {
        SongProbe(g, memory, look, playing);
    }
    const auto out = g.sender.Frame(look);
    if (!out) {
        return;
    }
    const bool sent = session->with_client([&](coop::net::Client& c) { return c.send(*out); });
    ORM_LOG_INFO("coop: ocarina note: {} {}(serial {})", Describe(*out), sent ? "sent " : "NOT sent ",
                 out->serial);
}

} // namespace CoopOcarinaNote
