// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM OCARINA_NOTE (MessageType::orm_ocarina_note, 0xF007 since protocol 110, 247 in 109; issue 152, ORM docs/OCARINA.md section 7): our
// Link's Ocarina notes and songs for the others, read from the game's own
// Ocarina control, so the others hear them at our puppet whether or not the
// note nodes travel in our AS command. The receiving side is
// OrmOcarinaSound::Player::OnNote (orm_ocarina_sound.h).
//
// Read in main of build e0f3ccfaf50065ae (not run in the game). Only reads,
// on the present thread, with no call into the game:
//
// - GameRoot = *(main+0x4e97648) (the GOT entry main+0x4c7e178 that every
//   reader of the control loads). t = *(GameRoot+0x1f0); while s32 t+0x50 is
//   33 or more, obj = *(*(t+0x58)+0x100) (entry 32). It is the Ocarina
//   control when the first slot of its vtable is main+0x3299390 (the type
//   check that hands the object back as it is; main+0x68ac40, the getter the
//   AIs use, does the same).
// - ctl+0x230 u32 state, +0x234 u32 note, +0x280 u32 the other note, +0x288
//   u8 "use +0x280", +0x271 u8 mute, +0x240 u32 instrument (0 Ocarina, 1
//   Piccolo, 2 Malon, 3 Harp), +0x220 u32 the song recognised (13 none;
//   0x3292070 writes it, 0x3293610 and 0x3294880 put 13 back).
// - The note the game plays, as the sound update 0x68a3b0 picks it before its
//   call to 0x335aa30: state 0, 5 or 6: none; states 8, 12, 14 or +0x288 == 1:
//   +0x280; else +0x234; mute: 0 (Rest).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/player_reader.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {
class GameMemory;
}

namespace CoopOcarinaNote {

namespace addr {
inline constexpr std::uint64_t kGameRoot = 0x4e97648;        // main+: GameRoot*
inline constexpr std::uint64_t kRootTable = 0x1f0;           // GameRoot+: the table
inline constexpr std::uint64_t kTableCount = 0x50;           // s32
inline constexpr std::uint64_t kTableArray = 0x58;           // pointer array
inline constexpr std::uint64_t kControlEntry = 0x100;        // entry 32
inline constexpr std::int32_t kMinCount = 33;
inline constexpr std::uint64_t kControlTypeCheck = 0x3299390;  // main+: the vtable's first slot
inline constexpr std::uint64_t kSong = 0x220;
inline constexpr std::uint64_t kState = 0x230;
inline constexpr std::uint64_t kNote = 0x234;
inline constexpr std::uint64_t kInstrument = 0x240;
inline constexpr std::uint64_t kMute = 0x271;
inline constexpr std::uint64_t kOtherNote = 0x280;
inline constexpr std::uint64_t kUseOtherNote = 0x288;
inline constexpr std::uint32_t kNoSong = 13;
inline constexpr std::uint32_t kOtherNoteStates = 0x1160;  // states 5, 6, 8, 12 (5 and 6 play none)
inline constexpr std::uint32_t kOtherNoteState = 14;
} // namespace addr

// One reading of the Ocarina control.
struct Control {
    std::uint64_t address = 0;
    std::uint32_t state = 0;
    std::uint32_t note = 0;
    std::uint32_t other_note = 0;
    std::uint8_t use_other_note = 0;
    std::uint8_t mute = 0;
    std::uint32_t instrument = 0;
    std::uint32_t song = addr::kNoSong;
};

// The control, or nothing (no game root, the table too short, another type).
std::optional<Control> ReadControl(coop::host::GameMemory& memory);

// The note the game plays now (its note enum: 0 none or Rest, 1 to 19), as
// 0x68a3b0 picks it. A value past HighF reads as none.
std::uint8_t SoundingNote(const Control& control);

// What our game shows this frame.
struct Look {
    std::optional<Control> control;      // nothing: the notes come from `command`
    std::optional<std::string> command;  // our Link's AS command (PLAYER_SAMPLE field 7)
    std::uint64_t now_ms = 0;
};

// The sender's rules: at most one message a frame. While our command starts
// with "Ocarina" (playing freely; "DemoOcarina..." is a cutscene) and the
// instrument is the Ocarina: `note` when the note the game plays changes,
// `release` when it goes back to none, `song` when ctl+0x220 turns to a song
// (the same song not again within kSongAgainMs; still within kSongAfterMs of
// putting the Ocarina away, as a song that starts an event changes the
// command at once). Without a control, the notes and songs our own command
// names (OrmOcarinaSound::AskOf). At most kPerSecond messages a second (a
// burst of kBurst): what does not fit waits, and the latest state goes out.
class Sender {
public:
    static constexpr double kPerSecond = 12.0;
    static constexpr double kBurst = 12.0;
    static constexpr std::uint64_t kSongAgainMs = 3000;
    static constexpr std::uint64_t kSongAfterMs = 1000;

    std::optional<coop::protocol::OrmOcarinaNote> Frame(const Look& look);
    // Not connected: nothing sounds for the others any more. The serial goes on.
    void Reset();
    // ORM test10: upstream sends the notes (OCARINA_NOTE 140); with songs only
    // this sender sends nothing but songs (a note or a release is followed
    // silently). Off by default (the tests); the glue sets it.
    void SetSongsOnly(bool on) { songs_only_ = on; }

    // For tests.
    std::uint8_t Sent() const { return sent_note_; }
    std::uint8_t Serial() const { return serial_; }

private:
    void Song(std::uint8_t song, std::uint64_t now_ms);

    std::uint8_t wanted_ = 0;     // the note our game plays now
    std::uint8_t sent_note_ = 0;  // what the others were last told sounds
    std::optional<std::uint8_t> pending_song_;
    std::optional<std::uint32_t> last_song_word_;  // ctl+0x220 last frame
    std::optional<std::uint8_t> last_song_sent_;
    std::uint64_t last_song_ms_ = 0;
    std::optional<std::uint64_t> playing_ms_;      // the last frame we played freely
    std::string last_command_;
    double tokens_ = kBurst;
    std::optional<std::uint64_t> tokens_ms_;
    std::uint8_t serial_ = 0;
    bool songs_only_ = false;
};

// "note A", "release", "song SariaSong": for the log.
std::string Describe(const coop::protocol::OrmOcarinaNote& note);

// The control's song check (docs/SONDAS.md, round 4; read in main e0f3ccfa,
// not changed by us). 0x3293610 (ctl, state) starts a state: mode +0x278 = 0,
// +0x220 = 13, and for states 1 (free play) and 3 the 13 "known" bytes
// +0x264.. = IsGet of the item whose name hash is the u32 at +0xe0 + 4 i
// (0x3025f20; Item_Melody_<song>, sondas7). 0x3297400, while the state is 1: song i is recognised when
// +0x264 + i is 1 and the last notes of the 8-note ring +0x158 (count
// +0x25c) match its score; it writes +0x220 (0x3297250). The ring +0x138
// (count +0x258) is the second one it tries.
namespace song_addr {
inline constexpr std::uint64_t kHashes = 0xe0;    // 13 u32: each song's item name hash
inline constexpr std::uint64_t kSongB = 0x228;    // u32, 13 none
inline constexpr std::uint64_t kSongC = 0x22c;    // u32, 13 none
inline constexpr std::uint64_t kRingB = 0x138;    // 8 u32
inline constexpr std::uint64_t kRing = 0x158;     // 8 u32 notes
inline constexpr std::uint64_t kCountB = 0x258;   // s32
inline constexpr std::uint64_t kCount = 0x25c;    // s32
inline constexpr std::uint64_t kKnown = 0x264;    // 13 u8
inline constexpr std::uint64_t kMode = 0x278;     // u32
inline constexpr std::size_t kSongs = 13;
inline constexpr std::size_t kRingSize = 8;
} // namespace song_addr

struct SongState {
    std::uint32_t state = 0;
    std::uint32_t mode = 0;
    std::uint32_t song = addr::kNoSong;
    std::uint32_t song_b = addr::kNoSong;
    std::uint32_t song_c = addr::kNoSong;
    std::int32_t count = 0;
    std::int32_t count_b = 0;
    std::uint32_t ring[song_addr::kRingSize] = {};
    std::uint32_t ring_b[song_addr::kRingSize] = {};
    std::uint8_t known[song_addr::kSongs] = {};
    std::uint32_t hashes[song_addr::kSongs] = {};
};

// The words above, or nothing when one cannot be read.
std::optional<SongState> ReadSongState(coop::host::GameMemory& memory, std::uint64_t ctl);

// "state 1 mode 0 song - count 3 ring D F A known ZeldaLullaby,SunSong":
// the ring from its oldest note, by the game's note names; the songs whose
// known byte is 1 ("none" when no byte is).
std::string DescribeSongState(const SongState& s);

// What does not match: a hash that is not the song's item name (Item_Melody_<song>, as 0x3025f20
// reads its IsGet flag; seen in a game, sondas7), or a known byte that differs from that IsGet flag
// ("ZeldaLullaby (IsGet.Item_Melody_ZeldaLullaby 1, known 0: ...)"). `flag` reads a bool by its hash.
std::vector<std::string> SongKnowledge(const SongState& s,
                                       const std::function<std::optional<bool>(std::uint32_t)>& flag);

} // namespace CoopOcarinaNote

namespace coop::net {
struct Event;
}
namespace coop::host {
class Session;
}
namespace CoopPlatform {
class Platform;
}

namespace CoopOcarinaNote {

// What runs the game (coop_ocarina_note_game.cpp, through platform.h). Network thread: OCARINA_NOTE
// and the players coming and going, kept for Frame.
void OnEvent(const coop::net::Event& event);
// Present thread, once a frame: the received notes to OrmOcarinaSound, and
// ours sent (LICHII_ORM_OCARINA_NOTE, or its old name LICHII_COOP_OCARINA_NOTE:
// 0 never, 1 always; unset, as LICHII_COOP_OCARINA_SOUND). `main_base` 0: no game; `session` null: not
// connected.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading);

} // namespace CoopOcarinaNote
