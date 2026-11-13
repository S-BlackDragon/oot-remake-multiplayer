// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "coop_ocarina.h"
#include "orm_ocarina_sound.h"

namespace {

using OrmOcarinaSound::Ask;
using OrmOcarinaSound::AskOf;
using OrmOcarinaSound::GameNote;
using OrmOcarinaSound::Player;
namespace saddr = OrmOcarinaSound::addr;
namespace paddr = CoopPuppet::addr;

constexpr std::uint64_t kBase = 0x80000000;    // main
constexpr std::uint64_t kHeap = 0x200000000;   // what the fake malloc gives
constexpr std::uint64_t kSound = 0x300000000;  // game::OcarinaSound
constexpr std::uint64_t kHolder = 0x300001000; // its "Ocarina" user's holder
constexpr std::uint64_t kHolderVtable = kBase + 0x4bd8000;
constexpr std::uint64_t kGetter = kBase + 0x1234560;
constexpr std::uint64_t kUser = 0x300002000;   // the user instance
constexpr std::uint64_t kActor = 0x400002000;  // the puppet of P2
constexpr std::uint64_t kActor3 = 0x400004000; // the puppet of P3

class Memory : public CoopPuppet::Guest {
public:
    void Map(std::uint64_t address, std::size_t size) {
        for (std::uint64_t p = address & ~0xfffULL; p < address + size; p += 0x1000) {
            pages_[p];
        }
    }
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) {
                return false;
            }
            o[i] = it->second[(address + i) & 0xfff];
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) {
                return false;
            }
            it->second[(address + i) & 0xfff] = p[i];
        }
        return true;
    }
    template <typename T>
    T Get(std::uint64_t address) {
        T v{};
        REQUIRE(Read(address, &v, sizeof(T)));
        return v;
    }
    template <typename T>
    void Set(std::uint64_t address, const T& v) {
        REQUIRE(Write(address, &v, sizeof(T)));
    }
    std::string Text(std::uint64_t address) {
        std::string s;
        for (char c = Get<char>(address); c != '\0' && s.size() < 128; c = Get<char>(++address)) {
            s.push_back(c);
        }
        return s;
    }

private:
    std::map<std::uint64_t, std::array<std::uint8_t, 0x1000>> pages_;
};

// The game's side of the calls, as main reads (orm_ocarina_sound.h).
struct Game {
    Memory mem;
    bool has_sound = true;
    std::vector<CoopPuppet::Call> calls;
    std::vector<std::string> heard;  // "Ocarina_A at 1,2,3", "off", "Melody_..."

    Game() {
        mem.Map(kHeap, Player::kSlots * Player::kSlotBytes);
        mem.Map(kSound, 0x100);
        mem.Map(kHolder, 0x100);
        mem.Map(kHolderVtable, 0x100);
        mem.Map(kUser, 0x100);
        mem.Map(kActor, 0x400);
        mem.Map(kActor3, 0x400);
        mem.Set(kSound + saddr::kSoundUserHolder, kHolder);
        mem.Set(kHolder, kHolderVtable);
        mem.Set(kHolderVtable + saddr::kHolderGetUser, kGetter);
        Place(kActor, 10.0f, 2.5f, -40.0f);
        Place(kActor3, -5.0f, 0.0f, 7.0f);
    }
    void Place(std::uint64_t actor, float x, float y, float z) {
        const std::array<float, 3> p{x, y, z};
        REQUIRE(mem.Write(actor + paddr::kActorPosition, p.data(), sizeof(p)));
    }
    std::string At(std::uint64_t place) {
        std::array<float, 3> p{};
        REQUIRE(mem.Read(place, p.data(), sizeof(p)));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g,%g,%g", p[0], p[1], p[2]);
        return buf;
    }
    std::uint64_t Run(const CoopPuppet::Call& c) {
        calls.push_back(c);
        if (c.function == kBase + paddr::kMalloc) {
            return kHeap;
        }
        if (c.function == kBase + saddr::kGetOcarinaSound) {
            return has_sound ? kSound : 0;
        }
        if (c.function == kGetter) {
            return c.x[0] == kHolder ? kUser : 0;
        }
        if (c.function == kBase + saddr::kPlayNote) {
            // 0x335aa30: note 0 fades out and clears the handle.
            if (c.x[3] == 0) {
                mem.Write(c.x[0], saddr::kEmptyHandle.data(), saddr::kEmptyHandle.size());
                heard.push_back("off");
            } else {
                const std::array<std::uint8_t, 8> playing{1, 0, 5, 0, 9, 0, 0, 0};
                mem.Write(c.x[0], playing.data(), playing.size());
                heard.push_back("Ocarina_" + OrmOcarinaSound::GameNoteNames()[c.x[3]] + " at " +
                                At(c.x[1]));
            }
            return 0;
        }
        if (c.function == kBase + saddr::kSearchAndEmit) {
            const std::string name = mem.Text(c.x[1]);
            heard.push_back(name);
            return name.starts_with("Melody_") ? 1 : 0;
        }
        if (c.function == kBase + saddr::kSetSoundPlace) {
            heard.push_back("placed at " + At(c.x[1]));
            return 0;
        }
        FAIL("unknown call");
        return 0;
    }
    // One frame of the cave with the player as its only caller.
    void Frame(Player& p, std::uint64_t now_ms) {
        auto call = p.Step(mem, false, 0, now_ms);
        int guard = 0;
        while (call) {
            REQUIRE(++guard < 64);
            const std::uint64_t result = Run(*call);
            call = p.Step(mem, true, result, now_ms);
        }
    }
    std::vector<CoopPuppet::Call> Calls(std::uint64_t function) const {
        std::vector<CoopPuppet::Call> out;
        for (const auto& c : calls) {
            if (c.function == function) {
                out.push_back(c);
            }
        }
        return out;
    }
};

Player::Say Into(std::vector<std::string>& log) {
    return [&log](const std::string& line) { log.push_back(line); };
}

} // namespace

TEST_CASE("ocarina sound: note names are the game's enum, not the asset list's order") {
    const auto& names = OrmOcarinaSound::GameNoteNames();
    REQUIRE(names.size() == 20);
    CHECK(names[0] == "Rest");
    CHECK(GameNote("LowB") == 1);
    CHECK(GameNote("D") == 4);
    CHECK(GameNote("F") == 7);
    CHECK(GameNote("A") == 11);
    CHECK(GameNote("B") == 13);
    CHECK(GameNote("C") == 14);  // CoopOcarina::NoteNames() has it fourth
    CHECK(GameNote("HighD") == 16);
    CHECK(GameNote("HighF") == 19);
    CHECK(GameNote("Rest") == 0);
    CHECK(GameNote("Close") == 0);
    // Every note the AS can name is one of the game's.
    const auto& as_names = CoopOcarina::NoteNames();
    for (std::size_t i = 1; i < as_names.size(); ++i) {
        CHECK_MESSAGE(GameNote(as_names[i]) != 0, as_names[i]);
    }
}

TEST_CASE("ocarina sound: what a command asks") {
    using K = Ask::Kind;
    CHECK(AskOf("Ocarina_Child_A_Close_1st_lp", 0).kind == K::note);
    CHECK(AskOf("Ocarina_Child_A_Close_1st_lp", 0).note == GameNote("A"));
    // The same held note: its loop after its first frames.
    CHECK(AskOf("Ocarina_Child_A_lp", GameNote("A")).kind == K::none);
    // Another note at once.
    CHECK(AskOf("Ocarina_Adult_HighD_lp", GameNote("A")).kind == K::note);
    CHECK(AskOf("Ocarina_Adult_HighD_lp", GameNote("A")).note == GameNote("HighD"));
    // Let go: only the note that sounds.
    CHECK(AskOf("Ocarina_Child_A_Rest", GameNote("A")).kind == K::stop);
    CHECK(AskOf("Ocarina_Child_F_Rest", GameNote("A")).kind == K::none);
    CHECK(AskOf("Ocarina_Child_A_Rest", 0).kind == K::none);
    // Waiting, blowing, the idle fingers: as it was.
    CHECK(AskOf("Ocarina_Child_StartWait", GameNote("A")).kind == K::none);
    CHECK(AskOf("Ocarina_Child_Blow_lp", GameNote("A")).kind == K::none);
    CHECK(AskOf("Ocarina_Child_B_Wait", 0).kind == K::none);
    // Put away or anything else: let go if it sounds.
    CHECK(AskOf("Wait", GameNote("A")).kind == K::stop);
    CHECK(AskOf("Wait", 0).kind == K::none);
    CHECK(AskOf("DemoOcarina_Get", GameNote("A")).kind == K::stop);  // a cutscene
    // A song.
    const Ask song = AskOf("Ocarina_Adult_ZeldaLullaby", 0);
    CHECK(song.kind == K::song);
    CHECK(song.song == "ZeldaLullaby");
}

TEST_CASE("ocarina sound: off by default, nothing is called") {
    Player p;
    Game g;
    CHECK_FALSE(p.Enabled());
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 1000);
    g.Frame(p, 1000);
    CHECK(g.calls.empty());
    CHECK(p.Waiting() == 0);
}

TEST_CASE("ocarina sound: a note of P2 plays on the game's Ocarina user at its puppet") {
    std::vector<std::string> log;
    Player p(Into(log));
    p.SetEnabled(true);
    Game g;
    g.Frame(p, 1000);
    CHECK(g.calls.empty());  // nothing asked: not even the block

    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_Close_1st_lp", 1000);
    g.Frame(p, 1010);
    REQUIRE(g.calls.size() == 4);
    CHECK(g.calls[0].function == kBase + paddr::kMalloc);
    CHECK(g.calls[0].x[0] == Player::kSlots * Player::kSlotBytes);
    CHECK(g.calls[1].function == kBase + saddr::kGetOcarinaSound);
    CHECK(g.calls[2].function == kGetter);
    CHECK(g.calls[2].x[0] == kHolder);
    const CoopPuppet::Call& note = g.calls[3];
    const std::uint64_t slot = kHeap + 2 * Player::kSlotBytes;
    CHECK(note.function == kBase + saddr::kPlayNote);
    CHECK(note.x[0] == slot + Player::kNoteHandle);
    CHECK(note.x[1] == slot + Player::kPlace);
    CHECK(note.x[2] == saddr::kInstrumentOcarina);
    CHECK(note.x[3] == GameNote("A"));
    CHECK(note.x[4] == kUser);
    CHECK(note.x[5] == saddr::kModeFree);
    CHECK(note.s0 == 0.0f);
    REQUIRE(g.heard.size() == 1);
    CHECK(g.heard[0] == "Ocarina_A at 10,2.5,-40");
    CHECK(p.Sounding(2) == GameNote("A"));
    CHECK(p.Block() == kHeap);
    // The other handles were cleared as the game clears one.
    std::array<std::uint8_t, 8> h{};
    REQUIRE(g.mem.Read(kHeap + 5 * Player::kSlotBytes + Player::kSongHandle, h.data(), 8));
    CHECK(h == saddr::kEmptyHandle);

    // The same note held: nothing more. The block is made once.
    g.calls.clear();
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 1100);
    g.Frame(p, 1110);
    CHECK(g.calls.empty());

    // Let go: note 0 on the same handle.
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_Rest", 1200);
    g.Frame(p, 1210);
    REQUIRE(g.calls.size() == 3);
    CHECK(g.calls[0].function == kBase + saddr::kGetOcarinaSound);
    CHECK(g.calls[2].function == kBase + saddr::kPlayNote);
    CHECK(g.calls[2].x[0] == slot + Player::kNoteHandle);
    CHECK(g.calls[2].x[3] == 0);
    CHECK(g.heard.back() == "off");
    CHECK(p.Sounding(2) == 0);
    CHECK(g.Calls(kBase + paddr::kMalloc).empty());
}

TEST_CASE("ocarina sound: two players, a new note, put away, and a song") {
    Player p;
    p.SetEnabled(true);
    Game g;
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_D_lp", 1000);
    p.OnCommand(kBase, 3, kActor3, "Ocarina_Adult_F_lp", 1000);
    g.Frame(p, 1005);
    const auto notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].x[0] == kHeap + 2 * Player::kSlotBytes + Player::kNoteHandle);
    CHECK(notes[1].x[0] == kHeap + 3 * Player::kSlotBytes + Player::kNoteHandle);
    CHECK(g.heard == std::vector<std::string>{"Ocarina_D at 10,2.5,-40", "Ocarina_F at -5,0,7"});
    // The user instance is asked once a frame, not once a note.
    CHECK(g.Calls(kGetter).size() == 1);

    // P2 goes to another note: the game's call fades the old one itself.
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_B_Close_1st_lp", 1100);
    g.Frame(p, 1105);
    CHECK(g.heard.back() == "Ocarina_B at 10,2.5,-40");
    CHECK(p.Sounding(2) == GameNote("B"));

    // P3 puts the Ocarina away while F sounds.
    p.OnCommand(kBase, 3, kActor3, "Wait", 1200);
    g.Frame(p, 1205);
    CHECK(g.heard.back() == "off");
    CHECK(g.Calls(kBase + saddr::kPlayNote).back().x[0] ==
          kHeap + 3 * Player::kSlotBytes + Player::kNoteHandle);
    CHECK(p.Sounding(3) == 0);
    CHECK(p.Sounding(2) == GameNote("B"));

    // P2's song: its note off, then Melody_<song> at its puppet.
    g.Place(kActor, 1.0f, 2.0f, 3.0f);
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_SariaSong", 1300);
    g.Frame(p, 1305);
    REQUIRE(g.heard.size() >= 3);
    CHECK(g.heard[g.heard.size() - 3] == "off");
    CHECK(g.heard[g.heard.size() - 2] == "Melody_SariaSong");
    CHECK(g.heard.back() == "placed at 1,2,3");
    const auto emit = g.Calls(kBase + saddr::kSearchAndEmit);
    REQUIRE(emit.size() == 1);
    CHECK(emit[0].x[0] == kUser);
    CHECK(emit[0].x[1] == kHeap + 2 * Player::kSlotBytes + Player::kName);
    CHECK(emit[0].x[2] == kHeap + 2 * Player::kSlotBytes + Player::kSongHandle);
    CHECK(emit[0].x[3] == 0);
    const auto place = g.Calls(kBase + saddr::kSetSoundPlace);
    REQUIRE(place.size() == 1);
    CHECK(place[0].x[0] == kHeap + 2 * Player::kSlotBytes + Player::kSongHandle);
    CHECK(place[0].x[1] == kHeap + 2 * Player::kSlotBytes + Player::kPlace);
}

TEST_CASE("ocarina sound: no OcarinaSound yet, late asks, a lost puppet, a note held too long") {
    std::vector<std::string> log;
    Player p(Into(log));
    p.SetEnabled(true);
    Game g;
    g.has_sound = false;
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 1000);
    g.Frame(p, 1000);
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    CHECK(g.Calls(kGetter).empty());
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().find("no Ocarina sound user yet") != std::string::npos);
    // Asking again only after kRetryMs; by then the note is stale and dropped.
    g.calls.clear();
    g.Frame(p, 1500);
    CHECK(g.calls.empty());
    g.has_sound = true;
    g.Frame(p, 1000 + Player::kRetryMs + 1);
    CHECK(g.calls.empty());
    CHECK(p.Waiting() == 0);
    // The dropped note is forgotten: the same note asked again sounds.
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 4000);
    g.Frame(p, 4000);
    REQUIRE_FALSE(g.heard.empty());
    CHECK(g.heard.back() == "Ocarina_A at 10,2.5,-40");

    // A puppet with no actor: nothing played, nothing broken.
    p.OnCommand(kBase, 3, 0, "Ocarina_Child_D_lp", 4100);
    g.Frame(p, 4100);
    CHECK(g.Calls(kBase + saddr::kPlayNote).size() == 1);
    CHECK(p.Sounding(3) == 0);

    // A note left on (its player gone): let go after kHeldMaxMs.
    g.Frame(p, 4000 + Player::kHeldMaxMs - 1);
    CHECK(p.Sounding(2) == GameNote("A"));
    g.Frame(p, 4000 + Player::kHeldMaxMs + 1);
    CHECK(p.Sounding(2) == 0);
    CHECK(g.heard.back() == "off");

    // Out of range numbers and no game: ignored.
    p.OnCommand(kBase, Player::kSlots, kActor, "Ocarina_Child_D_lp", 30000);
    p.OnCommand(0, 2, kActor, "Ocarina_Child_D_lp", 30000);
    CHECK(p.Waiting() == 0);
}

TEST_CASE("ocarina sound: a getter outside main is not called") {
    std::vector<std::string> log;
    Player p(Into(log));
    p.SetEnabled(true);
    Game g;
    g.mem.Set(kHolderVtable + saddr::kHolderGetUser, std::uint64_t{0x1234});
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 1000);
    g.Frame(p, 1000);
    CHECK(g.Calls(kGetter).empty());
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    CHECK(g.calls.size() == 2);  // the block and OcarinaSound, nothing else
}

namespace {

namespace proto = coop::protocol;

proto::OrmOcarinaNote NoteMsg(std::uint8_t player, std::uint8_t note, std::uint8_t serial) {
    return proto::OrmOcarinaNote{player, proto::OcarinaKind::note, 0, note, 0, serial};
}
proto::OrmOcarinaNote ReleaseMsg(std::uint8_t player, std::uint8_t serial) {
    return proto::OrmOcarinaNote{player, proto::OcarinaKind::release, 0, 0, 0, serial};
}
proto::OrmOcarinaNote SongMsg(std::uint8_t player, std::uint8_t song, std::uint8_t serial) {
    return proto::OrmOcarinaNote{player, proto::OcarinaKind::song, 0, 0, song, serial};
}

// P2's puppet is kActor, P3's kActor3, nobody else has one.
Player::FindActor Puppets() {
    return [](std::uint8_t number) -> std::uint64_t {
        return number == 2 ? kActor : number == 3 ? kActor3 : 0;
    };
}

} // namespace

TEST_CASE("ocarina sound: OCARINA_NOTE plays at the puppet found when it plays") {
    std::vector<std::string> log;
    Player p(Into(log));
    p.SetEnabled(true);
    p.SetFindActor(Puppets());
    Game g;
    p.OnNote(kBase, NoteMsg(2, GameNote("A"), 1), 1000);
    CHECK(p.ByMessage(2));
    g.Frame(p, 1005);
    REQUIRE(g.heard.size() == 1);
    CHECK(g.heard[0] == "Ocarina_A at 10,2.5,-40");
    CHECK(p.Sounding(2) == GameNote("A"));
    const auto notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].x[0] == kHeap + 2 * Player::kSlotBytes + Player::kNoteHandle);
    CHECK(notes[0].x[3] == GameNote("A"));

    // The same serial again (a message seen twice): dropped.
    p.OnNote(kBase, NoteMsg(2, GameNote("B"), 1), 1100);
    CHECK(p.Waiting() == 0);
    // Moved puppet, next note: at its new place.
    g.Place(kActor, 4.0f, 5.0f, 6.0f);
    p.OnNote(kBase, NoteMsg(2, GameNote("B"), 2), 1100);
    g.Frame(p, 1105);
    CHECK(g.heard.back() == "Ocarina_B at 4,5,6");
    // Released.
    p.OnNote(kBase, ReleaseMsg(2, 3), 1200);
    g.Frame(p, 1205);
    CHECK(g.heard.back() == "off");
    CHECK(p.Sounding(2) == 0);
    // A song: Melody_<the game's song name> at the puppet.
    p.OnNote(kBase, SongMsg(2, 12, 4), 1300);
    g.Frame(p, 1305);
    CHECK(g.heard.back() == "placed at 4,5,6");
    CHECK(g.heard[g.heard.size() - 2] == "Melody_ScarecrowSong");
    // A release with nothing sounding calls nothing but the frame's sound user.
    const auto before = g.calls.size();
    p.OnNote(kBase, ReleaseMsg(2, 5), 1400);
    g.Frame(p, 1405);
    CHECK(g.Calls(kBase + saddr::kPlayNote).size() == 3);
    CHECK(g.calls.size() == before + 2);  // OcarinaSound and its user, nothing else
}

TEST_CASE("ocarina sound: once a player's messages come, its commands only let the note go") {
    Player p;
    p.SetEnabled(true);
    p.SetFindActor(Puppets());
    Game g;
    // Before any message the command plays.
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_D_lp", 1000);
    g.Frame(p, 1005);
    CHECK(g.heard.back() == "Ocarina_D at 10,2.5,-40");
    // Its message takes over: the same note is not asked twice, and the
    // command's notes and songs no longer count.
    p.OnNote(kBase, NoteMsg(2, GameNote("D"), 1), 1100);
    CHECK(p.Waiting() == 0);
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_F_Close_1st_lp", 1100);
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_D_Rest", 1100);
    p.OnCommand(kBase, 2, kActor, "Ocarina_Adult_ZeldaLullaby", 1100);
    CHECK(p.Waiting() == 0);
    // Putting the Ocarina away still lets the note go.
    p.OnCommand(kBase, 2, kActor, "Wait", 1200);
    g.Frame(p, 1205);
    CHECK(g.heard.back() == "off");
    CHECK(g.heard.size() == 2);
    // And nothing more while nothing sounds.
    p.OnCommand(kBase, 2, kActor, "DemoOcarina_Get", 1300);
    CHECK(p.Waiting() == 0);
    // P3 sends nothing: its commands still play.
    p.OnCommand(kBase, 3, kActor3, "Ocarina_Adult_F_lp", 1400);
    g.Frame(p, 1405);
    CHECK(g.heard.back() == "Ocarina_F at -5,0,7");
    CHECK_FALSE(p.ByMessage(3));
}

TEST_CASE("ocarina sound: songs only leaves every note to upstream's OCARINA_NOTE (test10)") {
    Player p;
    p.SetEnabled(true);
    p.SetSongsOnly(true);
    CHECK(p.SongsOnly());
    p.SetFindActor(Puppets());
    Game g;
    // A puppet's note command and a note message: nothing (upstream plays them).
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_D_lp", 1000);
    p.OnNote(kBase, NoteMsg(3, GameNote("A"), 1), 1000);
    p.OnNote(kBase, ReleaseMsg(3, 2), 1000);
    CHECK(p.Waiting() == 0);
    g.Frame(p, 1005);
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    CHECK(p.Sounding(2) == 0);
    CHECK_FALSE(p.ByMessage(3));
    // A song, by message or by command: ours plays it.
    p.OnNote(kBase, SongMsg(3, 12, 3), 1100);
    g.Frame(p, 1105);
    CHECK(g.heard.back() == "placed at -5,0,7");
    CHECK(g.heard[g.heard.size() - 2] == "Melody_ScarecrowSong");
    p.OnCommand(kBase, 2, kActor, "Ocarina_Adult_ZeldaLullaby", 1200);
    g.Frame(p, 1205);
    CHECK(g.heard[g.heard.size() - 2] == "Melody_ZeldaLullaby");
}

TEST_CASE("ocarina sound: each player in series, one note a frame, a new message drops the old") {
    Player p;
    p.SetEnabled(true);
    p.SetFindActor(Puppets());
    Game g;
    // Three notes of P2 before a frame: only the last plays.
    p.OnNote(kBase, NoteMsg(2, GameNote("D"), 1), 1000);
    p.OnNote(kBase, NoteMsg(2, GameNote("E"), 2), 1000);
    p.OnNote(kBase, NoteMsg(2, GameNote("F"), 3), 1000);
    CHECK(p.Waiting() == 1);
    // P3's at the same time: a note each, in one frame.
    p.OnNote(kBase, NoteMsg(3, GameNote("A"), 1), 1000);
    g.Frame(p, 1005);
    CHECK(g.heard == std::vector<std::string>{"Ocarina_F at 10,2.5,-40", "Ocarina_A at -5,0,7"});

    // From commands, two notes of one player and a stop: one note a frame, in
    // order; the stop does not jump the note that waits.
    Player q;
    q.SetEnabled(true);
    Game h;
    q.OnCommand(kBase, 2, kActor, "Ocarina_Child_D_Close_1st_lp", 2000);
    q.OnCommand(kBase, 2, kActor, "Ocarina_Child_B_Close_1st_lp", 2000);
    q.OnCommand(kBase, 2, kActor, "Wait", 2000);
    q.OnCommand(kBase, 3, kActor3, "Ocarina_Child_A_Close_1st_lp", 2000);
    h.Frame(q, 2005);
    CHECK(h.heard == std::vector<std::string>{"Ocarina_D at 10,2.5,-40", "Ocarina_A at -5,0,7"});
    CHECK(q.Waiting() == 2);
    h.Frame(q, 2021);
    CHECK(h.heard == std::vector<std::string>{"Ocarina_D at 10,2.5,-40", "Ocarina_A at -5,0,7",
                                              "Ocarina_B at 10,2.5,-40", "off"});
    CHECK(q.Sounding(2) == 0);
    CHECK(q.Sounding(3) == GameNote("A"));
    // A stop before a note plays in the same frame, first.
    q.OnCommand(kBase, 3, kActor3, "Ocarina_Child_A_Rest", 2100);
    q.OnCommand(kBase, 3, kActor3, "Ocarina_Child_E_Close_1st_lp", 2100);
    h.Frame(q, 2105);
    CHECK(h.heard.back() == "Ocarina_E at -5,0,7");
    CHECK(h.heard[h.heard.size() - 2] == "off");
}

TEST_CASE("ocarina sound: a player with no puppet, another instrument, a late note, one who leaves") {
    std::vector<std::string> log;
    Player p(Into(log));
    p.SetEnabled(true);
    p.SetFindActor(Puppets());
    Game g;
    // P4 has no puppet here: nothing played, one line for its notes.
    p.OnNote(kBase, NoteMsg(4, GameNote("D"), 1), 1000);
    g.Frame(p, 1005);
    p.OnNote(kBase, NoteMsg(4, GameNote("E"), 2), 1100);
    g.Frame(p, 1105);
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    CHECK(std::count_if(log.begin(), log.end(), [](const std::string& l) {
              return l.find("P4: no place for its puppet") != std::string::npos;
          }) == 1);
    // The Harp (instrument 3): not placed at a puppet, dropped.
    p.OnNote(kBase, proto::OrmOcarinaNote{2, proto::OcarinaKind::note, 3, GameNote("D"), 0, 1}, 1200);
    CHECK(p.Waiting() == 0);
    // A note that waited too long (no sound user for a while) is dropped.
    g.has_sound = false;
    p.OnNote(kBase, NoteMsg(2, GameNote("D"), 2), 1300);
    g.Frame(p, 1305);
    g.has_sound = true;
    g.Frame(p, 1300 + Player::kRetryMs + 1);
    CHECK(p.Waiting() == 0);
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    // Unknown numbers, no game: ignored.
    p.OnNote(kBase, NoteMsg(Player::kSlots, GameNote("D"), 9), 5000);
    p.OnNote(0, NoteMsg(2, GameNote("D"), 9), 5000);
    CHECK(p.Waiting() == 0);

    // P2 plays and leaves: its note is let go, its serial and messages forgotten.
    p.OnNote(kBase, NoteMsg(2, GameNote("G"), 3), 6000);
    g.Frame(p, 6005);
    CHECK(p.Sounding(2) == GameNote("G"));
    p.Forget(2, 6100);
    CHECK_FALSE(p.ByMessage(2));
    g.Frame(p, 6105);
    CHECK(g.heard.back() == "off");
    CHECK(p.Sounding(2) == 0);
    // A new player 2 starting its serials over: heard.
    p.OnNote(kBase, NoteMsg(2, GameNote("G"), 3), 6200);
    g.Frame(p, 6205);
    CHECK(g.heard.back() == "Ocarina_G at 10,2.5,-40");
    // Forgetting a player with nothing sounding asks nothing.
    p.Forget(5, 6300);
    CHECK(p.Waiting() == 0);

    // Off: nothing kept.
    Player off;
    off.OnNote(kBase, NoteMsg(2, GameNote("G"), 1), 100);
    CHECK(off.Waiting() == 0);
    CHECK_FALSE(off.ByMessage(2));
}

// ORM "a mate is down" (coop/host/mate_down_sound.hpp): the cue note on the
// game's Ocarina user, on a handle of its own, at the mate's puppet or with a
// NaN place (the game's 2D copy, on the interface), let go after kCueMs.
TEST_CASE("ocarina sound: the mate-down cue plays once at his puppet or on the interface, then lets go") {
    std::vector<std::string> log;
    Player p(Into(log));
    Game g;
    p.SetFindActor([](std::uint8_t n) -> std::uint64_t { return n == 2 ? kActor : 0; });
    CHECK_FALSE(p.CuesEnabled());
    p.OnCue(kBase, 2, true, 1000);  // off: nothing
    g.Frame(p, 1000);
    CHECK(g.calls.empty());

    p.SetCue(GameNote(OrmOcarinaSound::kMateDownNote));
    CHECK(p.CuesEnabled());
    CHECK_FALSE(p.Enabled());  // the others' notes stay off
    p.OnCommand(kBase, 2, kActor, "Ocarina_Child_A_lp", 1000);
    CHECK(p.Waiting() == 0);

    const std::uint64_t slot = kHeap + Player::kCueSlot * Player::kSlotBytes;
    p.OnCue(kBase, 2, true, 1000);
    g.Frame(p, 1010);
    auto notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].x[0] == slot + Player::kNoteHandle);
    CHECK(notes[0].x[1] == slot + Player::kPlace);
    CHECK(notes[0].x[3] == GameNote("LowB"));
    CHECK(notes[0].x[4] == kUser);
    CHECK(g.heard.back() == "Ocarina_LowB at 10,2.5,-40");
    CHECK(p.Sounding(Player::kCueSlot) == GameNote("LowB"));
    CHECK(p.Sounding(2) == 0);  // not P2's own handle
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().find("P2 is down: cue note LowB at its puppet (10.0, 2.5, -40.0)") != std::string::npos);

    // Held kCueMs, then note 0 on the same handle.
    g.calls.clear();
    g.Frame(p, 1010 + Player::kCueMs - 10);
    CHECK(g.Calls(kBase + saddr::kPlayNote).empty());
    g.Frame(p, 1010 + Player::kCueMs);
    notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].x[0] == slot + Player::kNoteHandle);
    CHECK(notes[0].x[3] == 0);
    CHECK(p.Sounding(Player::kCueSlot) == 0);

    // On the interface: a NaN place, so the game plays System_Ocarina_LowB.
    g.calls.clear();
    p.OnCue(kBase, 3, false, 3000);
    g.Frame(p, 3010);
    notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 1);
    std::array<float, 3> place{};
    REQUIRE(g.mem.Read(slot + Player::kPlace, place.data(), sizeof(place)));
    CHECK(std::isnan(place[0]));
    CHECK(std::isnan(place[1]));
    CHECK(std::isnan(place[2]));
    CHECK(log.back().find("P3 is down: cue note LowB on the interface") != std::string::npos);

    // Near, but no puppet of his here: on the interface too.
    g.Frame(p, 3010 + Player::kCueMs);  // let go
    p.OnCue(kBase, 4, true, 5000);
    g.Frame(p, 5010);
    REQUIRE(g.mem.Read(slot + Player::kPlace, place.data(), sizeof(place)));
    CHECK(std::isnan(place[0]));
    CHECK(log.back().find("on the interface (no puppet place here)") != std::string::npos);

    // Turned off: nothing queued plays.
    p.SetCue(0);
    CHECK_FALSE(p.CuesEnabled());
    g.calls.clear();
    g.Frame(p, 5010 + Player::kCueMs);  // the note still sounding is let go all the same
    notes = g.Calls(kBase + saddr::kPlayNote);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].x[3] == 0);
    CHECK(p.Sounding(Player::kCueSlot) == 0);
    g.calls.clear();
    p.OnCue(kBase, 2, true, 9000);
    g.Frame(p, 9010);
    CHECK(g.calls.empty());
}
