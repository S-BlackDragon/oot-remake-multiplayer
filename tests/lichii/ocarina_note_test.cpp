// SPDX-License-Identifier: GPL-3.0-or-later
//
// OCARINA_NOTE, the sending side (coop_ocarina_note.h): the Ocarina control
// read from a fake game, the note the game plays, and the sender's rules.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../host/fake_memory.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_ocarina.h"
#include "coop_ocarina_note.h"
#include "orm_ocarina_sound.h"

namespace {

namespace proto = coop::protocol;
namespace naddr = CoopOcarinaNote::addr;
using CoopOcarinaNote::Control;
using CoopOcarinaNote::Look;
using CoopOcarinaNote::Sender;
using OrmOcarinaSound::GameNote;
using coop::host::test::FakeMemory;

constexpr std::uint64_t kBase = FakeMemory::kBase;
constexpr std::uint64_t kRoot = 0x90000000;
constexpr std::uint64_t kTable = 0x90001000;
constexpr std::uint64_t kArray = 0x90002000;
constexpr std::uint64_t kCtl = 0x90003000;
constexpr std::uint64_t kVtable = 0x90004000;

// The chain of coop_ocarina_note.h, with the control's words.
struct Game {
    FakeMemory mem;
    Game() {
        mem.put_u64(kBase + naddr::kGameRoot, kRoot);
        mem.put_u64(kRoot + naddr::kRootTable, kTable);
        mem.put_u32(kTable + naddr::kTableCount, 40);
        mem.put_u64(kTable + naddr::kTableArray, kArray);
        mem.put_u64(kArray + naddr::kControlEntry, kCtl);
        mem.put_u64(kCtl, kVtable);
        mem.put_u64(kVtable, kBase + naddr::kControlTypeCheck);
        Set(0, 0, 0, naddr::kNoSong);
        mem.put_u8(kCtl + naddr::kMute, 0);
        mem.put_u8(kCtl + naddr::kUseOtherNote, 0);
    }
    void Set(std::uint32_t state, std::uint32_t note, std::uint32_t instrument, std::uint32_t song) {
        mem.put_u32(kCtl + naddr::kState, state);
        mem.put_u32(kCtl + naddr::kNote, note);
        mem.put_u32(kCtl + naddr::kOtherNote, 0);
        mem.put_u32(kCtl + naddr::kInstrument, instrument);
        mem.put_u32(kCtl + naddr::kSong, song);
    }
};

Control Playing(std::uint8_t note, std::uint32_t song = naddr::kNoSong, std::uint32_t instrument = 0) {
    Control c;
    c.state = 1;
    c.note = note;
    c.instrument = instrument;
    c.song = song;
    return c;
}

Look With(std::optional<Control> control, std::optional<std::string> command, std::uint64_t now_ms) {
    Look l;
    l.control = control;
    l.command = std::move(command);
    l.now_ms = now_ms;
    return l;
}

constexpr const char* kFree = "Ocarina_Child_StartWait";

} // namespace

TEST_CASE("ocarina note: the control is read through the game root, entry 32, its type check") {
    Game g;
    g.Set(7, 11, 0, 2);
    g.mem.put_u32(kCtl + naddr::kOtherNote, 4);
    g.mem.put_u8(kCtl + naddr::kUseOtherNote, 1);
    g.mem.put_u8(kCtl + naddr::kMute, 0);
    const auto c = CoopOcarinaNote::ReadControl(g.mem);
    REQUIRE(c.has_value());
    CHECK(c->address == kCtl);
    CHECK(c->state == 7);
    CHECK(c->note == 11);
    CHECK(c->other_note == 4);
    CHECK(c->use_other_note == 1);
    CHECK(c->mute == 0);
    CHECK(c->instrument == 0);
    CHECK(c->song == 2);

    // Exactly 33 entries is enough; 32 is not.
    g.mem.put_u32(kTable + naddr::kTableCount, 33);
    CHECK(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u32(kTable + naddr::kTableCount, 32);
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u32(kTable + naddr::kTableCount, 0xffffffffu);  // -1
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u32(kTable + naddr::kTableCount, 40);

    // Another object in that entry: not the control.
    g.mem.put_u64(kVtable, kBase + naddr::kControlTypeCheck + 0x10);
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u64(kVtable, kBase + naddr::kControlTypeCheck);
    // A null link anywhere, or a word not there to read.
    g.mem.put_u64(kArray + naddr::kControlEntry, 0);
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u64(kArray + naddr::kControlEntry, kCtl);
    g.mem.erase(kCtl + naddr::kSong, 4);
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u32(kCtl + naddr::kSong, naddr::kNoSong);
    CHECK(CoopOcarinaNote::ReadControl(g.mem).has_value());
    g.mem.put_u64(kBase + naddr::kGameRoot, 0);
    CHECK_FALSE(CoopOcarinaNote::ReadControl(g.mem).has_value());
}

TEST_CASE("ocarina note: the note the game plays, as its sound update picks it") {
    using CoopOcarinaNote::SoundingNote;
    Control c;
    c.note = 11;
    c.other_note = 4;
    for (std::uint32_t state : {0u, 5u, 6u}) {
        c.state = state;
        CHECK_MESSAGE(SoundingNote(c) == 0, state);
    }
    for (std::uint32_t state : {1u, 2u, 3u, 4u, 7u, 9u, 10u, 11u, 13u, 15u, 16u, 40u}) {
        c.state = state;
        CHECK_MESSAGE(SoundingNote(c) == 11, state);
    }
    for (std::uint32_t state : {8u, 12u, 14u}) {
        c.state = state;
        CHECK_MESSAGE(SoundingNote(c) == 4, state);
    }
    c.state = 1;
    c.use_other_note = 1;
    CHECK(SoundingNote(c) == 4);
    c.use_other_note = 2;  // only 1 means it
    CHECK(SoundingNote(c) == 11);
    c.mute = 1;
    CHECK(SoundingNote(c) == 0);
    c.mute = 0;
    c.note = 19;
    CHECK(SoundingNote(c) == 19);
    c.note = 20;
    CHECK(SoundingNote(c) == 0);
}

TEST_CASE("ocarina note: a note when it changes, a release when it stops, only while playing freely") {
    Sender s;
    // Not playing: nothing, even with a note in the control.
    CHECK_FALSE(s.Frame(With(Playing(11), std::string("Wait"), 1000)));
    CHECK_FALSE(s.Frame(With(Playing(0), std::string(kFree), 1016)));

    auto m = s.Frame(With(Playing(GameNote("A")), std::string("Ocarina_Child_A_Close_1st_lp"), 1033));
    REQUIRE(m.has_value());
    CHECK(*m == proto::OrmOcarinaNote{0, proto::OcarinaKind::note, 0, GameNote("A"), 0, 1});
    CHECK(s.Sent() == GameNote("A"));
    // Held: nothing more.
    CHECK_FALSE(s.Frame(With(Playing(GameNote("A")), std::string("Ocarina_Child_A_lp"), 1050)));
    // Another note at once.
    m = s.Frame(With(Playing(GameNote("HighD")), std::string("Ocarina_Child_A_lp"), 1066));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::note);
    CHECK(m->note == GameNote("HighD"));
    CHECK(m->serial == 2);
    // Let go.
    m = s.Frame(With(Playing(0), std::string(kFree), 1083));
    REQUIRE(m.has_value());
    CHECK(*m == proto::OrmOcarinaNote{0, proto::OcarinaKind::release, 0, 0, 0, 3});
    CHECK_FALSE(s.Frame(With(Playing(0), std::string(kFree), 1100)));

    // Put away while a note sounds: released.
    REQUIRE(s.Frame(With(Playing(GameNote("F")), std::string(kFree), 1116)));
    m = s.Frame(With(Playing(GameNote("F")), std::string("Wait"), 1133));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::release);
    // Another instrument (the Harp, a minigame's): not ours to send.
    CHECK_FALSE(s.Frame(With(Playing(GameNote("F"), naddr::kNoSong, 3), std::string(kFree), 1150)));
    // The mute byte: no note.
    Control muted = Playing(GameNote("F"));
    muted.mute = 1;
    CHECK_FALSE(s.Frame(With(muted, std::string(kFree), 1166)));
    // No command read (no Link): nothing.
    CHECK_FALSE(s.Frame(With(Playing(GameNote("F")), std::nullopt, 1183)));
}

TEST_CASE("ocarina note: the remake's DemoOcarina is playing freely (docs/SONDAS.md, 2026-11-12)") {
    CHECK(CoopOcarina::PlaysFreely("DemoOcarina"));
    CHECK(CoopOcarina::PlaysFreely("DemoOcarinaFace"));
    CHECK(CoopOcarina::PlaysFreely("DemoOcarinaFinger"));
    CHECK(CoopOcarina::PlaysFreely("Ocarina_Child_A_lp"));
    CHECK_FALSE(CoopOcarina::PlaysFreely("DemoOcarinaMelodyGet"));
    CHECK_FALSE(CoopOcarina::PlaysFreely("DemoOcarinaWarp"));
    CHECK_FALSE(CoopOcarina::PlaysFreely("Wait"));
    Sender s;
    auto m = s.Frame(With(Playing(GameNote("A")), std::string("DemoOcarina"), 1000));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::note);
    CHECK(m->note == GameNote("A"));
    // Put away (the command goes back to Wait): released.
    m = s.Frame(With(Playing(GameNote("A")), std::string("Wait"), 1100));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::release);
}

TEST_CASE("ocarina note: songs only sends no note nor release, only the song (test10)") {
    Sender s;
    s.SetSongsOnly(true);
    // Notes go as upstream's OCARINA_NOTE 140: ours stays quiet.
    CHECK_FALSE(s.Frame(With(Playing(GameNote("A")), std::string("Ocarina_Child_A_Close_1st_lp"), 1000)));
    CHECK_FALSE(s.Frame(With(Playing(GameNote("HighD")), std::string(kFree), 1016)));
    CHECK_FALSE(s.Frame(With(Playing(0), std::string(kFree), 1033)));
    // The word starts with no song, then the game recognises one: sent.
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string(kFree), 1050)));
    const auto m = s.Frame(With(Playing(0, 2), std::string(kFree), 1066));
    REQUIRE(m.has_value());
    CHECK(*m == proto::OrmOcarinaNote{0, proto::OcarinaKind::song, 0, 0, 2, 1});
    // A reset keeps the mode.
    s.Reset();
    CHECK_FALSE(s.Frame(With(Playing(GameNote("F")), std::string(kFree), 1100)));
}

TEST_CASE("ocarina note: a song when the game recognises one, not twice within 3 s") {
    Sender s;
    // The word already holds a song when we start: not a new one.
    CHECK_FALSE(s.Frame(With(Playing(0, 2), std::string(kFree), 1000)));
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string(kFree), 1016)));
    auto m = s.Frame(With(Playing(0, 2), std::string(kFree), 1033));
    REQUIRE(m.has_value());
    CHECK(*m == proto::OrmOcarinaNote{0, proto::OcarinaKind::song, 0, 0, 2, 1});
    CHECK_FALSE(s.Frame(With(Playing(0, 2), std::string("Ocarina_Child_SariaSong"), 1050)));
    // The game puts 13 back and recognises the same song again soon: not sent.
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string(kFree), 2000)));
    CHECK_FALSE(s.Frame(With(Playing(0, 2), std::string(kFree), 2016)));
    // Another song at once is.
    m = s.Frame(With(Playing(0, 0), std::string(kFree), 2033));
    REQUIRE(m.has_value());
    CHECK(m->song == 0);  // Zelda's Lullaby
    // The first again after 3 s is.
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string(kFree), 4000)));
    m = s.Frame(With(Playing(0, 2), std::string(kFree), 4100));
    REQUIRE(m.has_value());
    CHECK(m->song == 2);

    // A song that starts an event changes our command at once: still sent
    // within 1 s of playing; not later, nor when we never played.
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string("DemoOcarina_Wait"), 4500)));
    m = s.Frame(With(Playing(0, 7), std::string("DemoOcarina_Wait"), 4600));
    REQUIRE(m.has_value());
    CHECK(m->song == 7);
    CHECK_FALSE(s.Frame(With(Playing(0, naddr::kNoSong), std::string("Wait"), 9000)));
    CHECK_FALSE(s.Frame(With(Playing(0, 9), std::string("Wait"), 9016)));
    Sender fresh;
    CHECK_FALSE(fresh.Frame(With(Playing(0, naddr::kNoSong), std::string("Wait"), 100)));
    CHECK_FALSE(fresh.Frame(With(Playing(0, 9), std::string("Wait"), 116)));

    // A song while a note was sent: the song alone (the receiver lets the
    // note go), then the note again if the game still plays one.
    Sender t;
    REQUIRE(t.Frame(With(Playing(GameNote("D")), std::string(kFree), 100)));
    m = t.Frame(With(Playing(GameNote("D"), 4), std::string(kFree), 116));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::song);
    CHECK(t.Sent() == 0);
    m = t.Frame(With(Playing(GameNote("D"), 4), std::string(kFree), 133));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::note);
    // A word past the songs is none.
    CHECK_FALSE(t.Frame(With(Playing(GameNote("D"), 40), std::string(kFree), 150)));
}

TEST_CASE("ocarina note: 12 a second, the rest merged into the latest state") {
    Sender s;
    std::vector<proto::OrmOcarinaNote> sent;
    // A note a frame for 20 frames at once (a flood): the first 12 go.
    for (std::uint8_t i = 0; i < 20; ++i) {
        const std::uint8_t note = static_cast<std::uint8_t>(1 + i % 19);
        if (auto m = s.Frame(With(Playing(note), std::string(kFree), 1000))) {
            sent.push_back(*m);
        }
    }
    REQUIRE(sent.size() == 12);
    CHECK(sent.back().note == 12);
    CHECK(sent.back().serial == 12);
    // The game plays note 20 % 19 = 1 now; 100 ms give one more: the latest.
    auto m = s.Frame(With(Playing(1), std::string(kFree), 1100));
    REQUIRE(m.has_value());
    CHECK(m->note == 1);
    CHECK_FALSE(s.Frame(With(Playing(2), std::string(kFree), 1100)));
    // The note let go while waiting: the release goes when there is room, the
    // note in between never.
    CHECK_FALSE(s.Frame(With(Playing(0), std::string(kFree), 1110)));
    m = s.Frame(With(Playing(0), std::string(kFree), 1200));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::release);
    // A long wait refills to 12, not more.
    std::size_t burst = 0;
    for (std::uint8_t i = 0; i < 30; ++i) {
        if (s.Frame(With(Playing(static_cast<std::uint8_t>(1 + i % 2)), std::string(kFree), 60000))) {
            ++burst;
        }
    }
    CHECK(burst == 12);
}

TEST_CASE("ocarina note: without the control, the notes our own AS command names") {
    Sender s;
    auto m = s.Frame(With(std::nullopt, std::string("Ocarina_Child_A_Close_1st_lp"), 1000));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::note);
    CHECK(m->note == GameNote("A"));
    CHECK_FALSE(s.Frame(With(std::nullopt, std::string("Ocarina_Child_A_lp"), 1016)));
    CHECK_FALSE(s.Frame(With(std::nullopt, std::string("Ocarina_Child_A_lp"), 1033)));
    m = s.Frame(With(std::nullopt, std::string("Ocarina_Child_A_Rest"), 1050));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::release);
    // A song node: the game's song enum (ShadowNocturne is 10 there).
    m = s.Frame(With(std::nullopt, std::string("Ocarina_Adult_ShadowNocturne"), 1066));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::song);
    CHECK(m->song == 10);
    // The same node on following frames: one song.
    CHECK_FALSE(s.Frame(With(std::nullopt, std::string("Ocarina_Adult_ShadowNocturne"), 1083)));
    // Put away with a note on.
    REQUIRE(s.Frame(With(std::nullopt, std::string("Ocarina_Adult_B_Close_1st_lp"), 1100)));
    m = s.Frame(With(std::nullopt, std::string("Wait"), 1116));
    REQUIRE(m.has_value());
    CHECK(m->kind == proto::OcarinaKind::release);
}

TEST_CASE("ocarina note: a reset forgets what sounds, the serial goes on") {
    Sender s;
    REQUIRE(s.Frame(With(Playing(5), std::string(kFree), 1000)));
    REQUIRE(s.Frame(With(Playing(6), std::string(kFree), 1016)));
    CHECK(s.Serial() == 2);
    s.Reset();
    CHECK(s.Sent() == 0);
    CHECK(s.Serial() == 2);
    const auto m = s.Frame(With(Playing(6), std::string(kFree), 1033));
    REQUIRE(m.has_value());
    CHECK(m->note == 6);
    CHECK(m->serial == 3);
}

TEST_CASE("ocarina note: log names") {
    using CoopOcarinaNote::Describe;
    CHECK(Describe(proto::OrmOcarinaNote{0, proto::OcarinaKind::note, 0, 11, 0, 0}) == "note A");
    CHECK(Describe(proto::OrmOcarinaNote{0, proto::OcarinaKind::release, 0, 0, 0, 0}) == "release");
    CHECK(Describe(proto::OrmOcarinaNote{0, proto::OcarinaKind::song, 0, 0, 12, 0}) == "song ScarecrowSong");
    CHECK(OrmOcarinaSound::GameSongNames().size() == proto::kMaxOcarinaSong + 1u);
    CHECK(OrmOcarinaSound::GameSong("ZeldaLullaby") == std::optional<std::uint8_t>{0});
    CHECK(OrmOcarinaSound::GameSong("SpiritRequiem") == std::optional<std::uint8_t>{9});
    CHECK(OrmOcarinaSound::GameSong("ShadowNocturne") == std::optional<std::uint8_t>{10});
    CHECK_FALSE(OrmOcarinaSound::GameSong("Correct").has_value());
    // Every song an AS node names is one of the game's.
    const auto& as_songs = CoopOcarina::SongNames();
    for (std::size_t i = 1; i < as_songs.size(); ++i) {
        CHECK_MESSAGE(OrmOcarinaSound::GameSong(as_songs[i]).has_value(), as_songs[i]);
    }
}

TEST_CASE("ocarina note: the song check's words, read and told (docs/SONDAS.md round 4)") {
    namespace sa = CoopOcarinaNote::song_addr;
    Game g;
    g.Set(1, 0, 0, naddr::kNoSong);
    g.mem.put_u32(kCtl + sa::kMode, 0);
    g.mem.put_u32(kCtl + sa::kSongB, naddr::kNoSong);
    g.mem.put_u32(kCtl + sa::kSongC, naddr::kNoSong);
    g.mem.put_u32(kCtl + sa::kCount, 10);  // the ring wrapped: the newest at (10 - 1) mod 8 = 1
    g.mem.put_u32(kCtl + sa::kCountB, 0);
    for (std::uint32_t i = 0; i < sa::kRingSize; ++i) {
        g.mem.put_u32(kCtl + sa::kRing + 4 * i, 0);
        g.mem.put_u32(kCtl + sa::kRingB + 4 * i, 0);
    }
    g.mem.put_u32(kCtl + sa::kRing + 4 * 0, GameNote("B"));      // count 9
    g.mem.put_u32(kCtl + sa::kRing + 4 * 1, GameNote("HighD"));  // count 10, the newest
    g.mem.put_u32(kCtl + sa::kRing + 4 * 7, GameNote("A"));      // count 8
    const auto& songs = OrmOcarinaSound::GameSongNames();
    for (std::uint32_t i = 0; i < sa::kSongs; ++i) {
        g.mem.put_u32(kCtl + sa::kHashes + 4 * i, proto::flag_hash("Item_Melody_" + songs[i]));
        g.mem.put_u8(kCtl + sa::kKnown + i, 0);
    }
    g.mem.put_u8(kCtl + sa::kKnown + 3, 1);  // SunSong

    const auto st = CoopOcarinaNote::ReadSongState(g.mem, kCtl);
    REQUIRE(st.has_value());
    CHECK(st->state == 1);
    CHECK(st->count == 10);
    const std::string text = CoopOcarinaNote::DescribeSongState(*st);
    CHECK(text.find("state 1 mode 0 song -/-/- count 10 ring ") == 0);
    CHECK(text.find(" A B HighD count2 0 ring2 - known SunSong") != std::string::npos);

    // ZeldaLullaby's flag is set but its byte is 0: told; SunSong known with its flag: not; the rest unset.
    CHECK(proto::flag_hash("Item_Melody_ZeldaLullaby") == 0x0f88f970u);  // read in a game (sondas7)
    const auto lullaby = proto::flag_hash("IsGet.Item_Melody_ZeldaLullaby");
    const auto sun = proto::flag_hash("IsGet.Item_Melody_SunSong");
    auto flag = [&](std::uint32_t h) -> std::optional<bool> { return h == lullaby || h == sun; };
    auto odd = CoopOcarinaNote::SongKnowledge(*st, flag);
    REQUIRE(odd.size() == 1);
    CHECK(odd[0] == "ZeldaLullaby (IsGet.Item_Melody_ZeldaLullaby 1, known 0: set when the Ocarina comes out)");
    // A hash that is not the song's item is told whatever its flag.
    g.mem.put_u32(kCtl + sa::kHashes + 4 * 2, 0x12345678);
    odd = CoopOcarinaNote::SongKnowledge(*CoopOcarinaNote::ReadSongState(g.mem, kCtl), flag);
    REQUIRE(odd.size() == 2);
    CHECK(odd[1] == "SariaSong (hash 0x12345678 is not Item_Melody_SariaSong, known 0)");

    // A word not there to read: nothing.
    g.mem.erase(kCtl + sa::kKnown + 12, 1);
    CHECK_FALSE(CoopOcarinaNote::ReadSongState(g.mem, kCtl).has_value());
}
