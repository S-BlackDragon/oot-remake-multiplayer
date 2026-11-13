// SPDX-License-Identifier: GPL-3.0-or-later
//
// The other players' Ocarina notes, heard at their puppets (issue 152; ORM
// docs/OCARINA.md, "Sonido"). Off unless LICHII_COOP_OCARINA_SOUND=1.
//
// The notes are SLink assets of the user "Ocarina" with no triggers: the
// game's own ocarina sound code (game::OcarinaSound) plays them by name.
// Read in main of build e0f3ccfaf50065ae (not run in the game):
//
// - 0x3299570() returns the game's OcarinaSound (cached in a global, found
//   in the sound manager's list on the first call).
// - OcarinaSound+0x40 holds the object made for the user "Ocarina" at sound
//   start (0xd1f360: sound manager+0x18, vtable +0x28, with the name
//   "Ocarina"); its vtable +0x20 returns the user instance. The game asks it
//   that way every frame (0x68a3b0).
// - 0x335aa30 plays one note on a user instance: (x0 handle, x1 const Vec3*
//   place, w2 instrument, w3 note, x4 user instance, w5 mode, s0 bend). It
//   builds "Ocarina_<note>" ("System_..." when place.x is NaN), looks it up,
//   fades out whatever the handle still plays, emits into the handle and,
//   for the Ocarina, puts the sound at `place` (0x194f0). Note 0 (Rest)
//   only fades the handle out and clears it. The game's one call (0x68a998)
//   passes its own handle (OcarinaSound+0x78) and the playing Link's place.
// - 0x440170 is xlink2 UserInstance::searchAndEmit(user, name, handle, 0);
//   0x194f0(handle, const Vec3*) moves a playing sound.
//
// So a puppet needs no sound user of its own: each puppet gets its own
// handle in a block of ours, and the game's own "Ocarina" user plays its
// notes at the puppet's place. Only this game hears it; a song played by
// another player changes nothing in our world (that comes by the flags).
//
// ORM "a mate is down" (coop/host/mate_down_sound.hpp, LICHII_ORM_MATE_DOWN_SOUND,
// off by default): the same user plays one cue note, held kCueMs, on a handle
// of its own (slot kCueSlot, no player's): at the fallen mate's puppet, or
// with a NaN place, for which 0x335aa30 plays the 2D copy System_Ocarina_<note>
// (the score window's), on the interface. The Ocarina user is the one sound
// user the hook can play by name today; the game's interface sounds
// (SE_Sys_*) are another user's, whose play call is not known.
//
// The notes come two ways: from the puppet's AS command (OnCommand, when the
// note nodes travel in PLAYER_SAMPLE) and from OCARINA_NOTE (OnNote, the
// player's game reading its own Ocarina control, coop_ocarina_note.h). Once
// a player's OCARINA_NOTE came, its commands only let its note go when the
// Ocarina is put away, so nothing sounds twice.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"
#include "coop_puppet.h"

namespace OrmOcarinaSound {

namespace addr {
inline constexpr std::uint64_t kGetOcarinaSound = 0x3299570;  // () -> game::OcarinaSound*
inline constexpr std::uint64_t kSoundUserHolder = 0x40;       // OcarinaSound+0x40
inline constexpr std::uint64_t kHolderGetUser = 0x20;         // the holder's vtable slot
inline constexpr std::uint64_t kPlayNote = 0x335aa30;
inline constexpr std::uint64_t kSearchAndEmit = 0x440170;     // (user, name, handle, 0) -> bool
inline constexpr std::uint64_t kSetSoundPlace = 0x194f0;      // (handle, const Vec3*)
inline constexpr std::uint32_t kInstrumentOcarina = 0;        // Ocarina, Piccolo, Malon, Harp
inline constexpr std::uint32_t kModeFree = 0;
// A handle as 0x335aa30 clears it: s8 -1 at +0, s16 -1 at +2, u32 0 at +4.
inline constexpr std::array<std::uint8_t, 8> kEmptyHandle{0xff, 0, 0xff, 0xff, 0, 0, 0, 0};
} // namespace addr

// The mate-down cue's note by default: the lowest of the Ocarina's notes.
inline constexpr std::string_view kMateDownNote = "LowB";

// The game's note enum (main+0x3ee6ae0), the w3 of 0x335aa30. 0 is Rest.
const std::vector<std::string>& GameNoteNames();
// The enum value of a note name ("A", "Csharp", "HighD"), or 0.
std::uint8_t GameNote(std::string_view name);
// The game's song enum (main+0x3effdb0; ctl+0x220 of the Ocarina control,
// 13 for none), OCARINA_NOTE's song: ZeldaLullaby 0 to ScarecrowSong 12.
// Not the order of CoopOcarina::SongNames().
const std::vector<std::string>& GameSongNames();
// The enum value of a song name ("SariaSong"), or nothing.
std::optional<std::uint8_t> GameSong(std::string_view name);

// What a puppet's new AS command asks of its Ocarina sound.
struct Ask {
    enum class Kind : std::uint8_t { none, note, stop, song };
    Kind kind = Kind::none;
    std::uint8_t note = 0;  // GameNoteNames() index, for note
    std::string song;       // "ZeldaLullaby", for song
};
// sounding: the game note this puppet plays now, 0 none. A command that is
// not the Ocarina's lets the note go; the Ocarina's waits and fingers
// without a note keep it; the same note again (<note>_Close_1st_lp then
// <note>_lp) is the same held note.
Ask AskOf(std::string_view command, std::uint8_t sounding);

class Player {
public:
    using Say = std::function<void(const std::string&)>;

    static constexpr std::size_t kSlots = 8;          // player numbers 0 to 7
    static constexpr std::uint64_t kSlotBytes = 0x80;
    static constexpr std::uint64_t kNoteHandle = 0x00;
    static constexpr std::uint64_t kSongHandle = 0x08;
    static constexpr std::uint64_t kPlace = 0x10;     // Vec3
    static constexpr std::uint64_t kName = 0x20;      // "Melody_<song>", NUL ended
    static constexpr std::size_t kNameBytes = 0x40;
    static constexpr std::size_t kMaxPerFrame = 8;    // asks played per frame
    static constexpr std::uint64_t kStaleMs = 500;    // an ask older than this is dropped
    static constexpr std::uint64_t kHeldMaxMs = 15000;  // a note left on longer is let go
    static constexpr std::uint64_t kRetryMs = 2000;   // after the game gave no sound user
    // The cue's own handle: number 7 is no player's (players are 1 to 6).
    static constexpr std::uint8_t kCueSlot = 7;
    static constexpr std::uint64_t kCueMs = 700;      // the cue note's length

    explicit Player(Say say = {});

    // Before the cave first calls (Configure).
    void SetSay(Say say);
    void SetEnabled(bool on);
    bool Enabled() const;
    // ORM test10 (docs/PIN_TEST10.md): upstream plays the others' NOTES on their
    // puppets itself (OCARINA_NOTE 140, coop_ocarina_sound.h + coop_puppet_sound.h,
    // always on). With songs only, this player leaves every note to it and
    // plays only the songs (from OCARINA_NOTE song or a puppet's Melody_
    // command), which upstream lacks; notes, stops and releases are dropped.
    // Off by default here (the tests); Configure turns it on unless
    // LICHII_ORM_OCARINA_NOTES=1.
    void SetSongsOnly(bool on);
    bool SongsOnly() const;
    // The mate-down cue (OnCue): on with its note (GameNoteNames() index, not
    // 0) held `hold_ms`; note 0 turns it off. Independent of SetEnabled.
    void SetCue(std::uint8_t note, std::uint64_t hold_ms = kCueMs);
    bool CuesEnabled() const;
    // Any present thread: mate `mate` went down. One cue note, at his
    // puppet's place when `at_mate` and he has a live puppet here, else on
    // the interface. A cue still sounding is cut by the new one.
    void OnCue(std::uint64_t main_base, std::uint8_t mate, bool at_mate, std::uint64_t now_ms);

    // The puppet director, on the calc thread, after the cave played
    // `command` on puppet `number` (its actor `actor`, in main `main_base`).
    void OnCommand(std::uint64_t main_base, std::uint8_t number, std::uint64_t actor,
                   std::string_view command, std::uint64_t now_ms);

    // OCARINA_NOTE from player `note.player_number` (the present thread,
    // CoopOcarinaNote::Frame), in main `main_base`. Its puppet is looked up
    // when the note plays (SetFindActor). In series per player: a repeated
    // serial is dropped; a new message drops that player's notes and songs
    // not yet played; at most one note or song of a player plays a frame (a
    // release does not count: note 0 sets no property of the shared user).
    // Only the Ocarina (instrument 0) is placed at a puppet; others dropped.
    void OnNote(std::uint64_t main_base, const coop::protocol::OrmOcarinaNote& note,
                std::uint64_t now_ms);
    // PLAYER_LEFT or PLAYER_JOINED of `number` (or a reconnect, for each):
    // its serial is forgotten, its commands count again, and a note it left
    // sounding is let go.
    void Forget(std::uint8_t number, std::uint64_t now_ms);
    // Where puppet `number`'s actor is, for a note that came by message:
    // asked on the calc thread when it plays; 0 when it has none. Default:
    // the Link puppets' director (LivePuppets() bit and a live slot).
    using FindActor = std::function<std::uint64_t(std::uint8_t number)>;
    void SetFindActor(FindActor find);

    // CoopSvc::Caller contract: once a frame with after_call false, then
    // once after each call it returned, with the call's x0.
    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call,
                                         std::uint64_t result, std::uint64_t now_ms);

    // For tests.
    std::uint64_t Block() const { return block_; }
    std::uint8_t Sounding(std::uint8_t number) const;
    std::size_t Waiting() const;
    bool ByMessage(std::uint8_t number) const;

private:
    struct Request {
        std::uint8_t number = 0;
        std::uint64_t actor = 0;
        std::uint64_t main_base = 0;
        Ask ask;
        std::uint64_t at_ms = 0;
        bool find = false;  // OCARINA_NOTE: the actor is looked up when it plays
        // The mate-down cue (number kCueSlot): `of`'s puppet when at_mate.
        bool cue = false;
        bool at_mate = false;
        std::uint8_t of = 0;
    };
    struct Voice {
        std::uint8_t note = 0;      // asked (what AskOf compares with)
        std::uint8_t playing = 0;   // played by the game's call; 0 none
        std::uint64_t since_ms = 0;
        std::uint64_t actor = 0;
        std::uint64_t main_base = 0;
        bool by_message = false;             // an OCARINA_NOTE came from this player
        std::optional<std::uint8_t> serial;  // its last OCARINA_NOTE's
        bool said_no_puppet = false;         // "no puppet" logged since its last one
        std::uint64_t hold_ms = 0;           // let go after this; 0: kHeldMaxMs
    };
    enum class Pending { none, malloc, sound, user, note, song, place };

    std::optional<CoopPuppet::Call> Next(CoopPuppet::Guest& guest, std::uint64_t now_ms);
    std::optional<CoopPuppet::Call> Play(CoopPuppet::Guest& guest, const Request& r);
    void Enqueue(Request r);
    bool DropQueued(std::uint8_t number);  // its notes and songs not yet played; mutex_ held
    std::uint64_t SlotBase(std::uint8_t number) const { return block_ + number * kSlotBytes; }
    void Tell(const std::string& text) const;

    Say say_;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    bool songs_only_ = false;               // notes are upstream's (SetSongsOnly)
    std::uint8_t cue_note_ = 0;               // 0: no cues
    std::uint64_t cue_ms_ = kCueMs;
    std::vector<Request> asks_;                // guarded by mutex_
    std::array<Voice, kSlots> voices_{};       // guarded by mutex_
    FindActor find_;                           // guarded by mutex_

    // The cave's own state (calc thread).
    std::uint64_t base_ = 0;
    std::uint64_t block_ = 0;
    std::uint64_t user_ = 0;                   // this frame's user instance
    std::uint64_t retry_ms_ = 0;
    Pending pending_ = Pending::none;
    std::optional<Request> current_;           // the ask whose call is out
    std::size_t played_ = 0;                   // this frame
    std::uint32_t noted_ = 0;                  // players with a note or song this frame (bits)
    bool said_no_user_ = false;
};

Player& ThePlayer();

// Whether upstream's own note path (OCARINA_NOTE 140 played on the puppets by
// coop_puppet.cpp) should play: true unless LICHII_ORM_OCARINA_NOTES=1 gave
// the notes to ours. Read once; true outside the emulator.
bool UpstreamNotes();

// The emulator: reads LICHII_COOP_OCARINA_SOUND and, when on, adds the
// player to the cave's callers (songs only unless LICHII_ORM_OCARINA_NOTES=1).
// Once, with the puppets.
void Configure();

} // namespace OrmOcarinaSound
