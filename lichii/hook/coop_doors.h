// ORM (oot-remake docs/PUERTAS.md): the doors and chests another player
// opens move in our game too. The rules are coop/host/doors.hpp (game-free);
// this is the game side, still without emulator code so it is tested on fake
// guest memory (tests/lichii/doors_test.cpp):
// - the present thread (Publish) hands over our Link and the others in our
//   place, with their AS command, from presence (CoopPuppet::Collect);
// - the game's calc thread (Mirror::Step, a CoopSvc caller) reads the door
//   and chest actors near a player who just started opening one (the live
//   actor list, as coop_objects walks it), and plays the door's own command
//   on it with the game's AS play (0x2022a0, as the puppets and copies do).
// The emulator side (coop_doors_game.cpp) reads the switches and registers
// the caller. LICHII_COOP_DOORS=0 turns it off; LICHII_COOP_DEBUG_DOORS=1
// logs every walk.
// ORM cofre (coop/host/chest_open.hpp, oot-remake docs/DETALLES_MARIONETA.md
// "Cofres"): the same walk reads each chest's placement hash and frame, and
// the mirror's ChestLids tells the chest our Link opens (CHEST_OPEN, taken by
// the present thread: TakeChestOpens) and opens here the lid of a chest
// another player opens (ReceiveChestOpen), at his frame. LICHII_ORM_CHEST_OPEN=0
// turns that part off.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/chest_open.hpp"
#include "coop/host/doors.hpp"
#include "coop_puppet.h"

namespace coop::host {
class Session;
}

namespace CoopDoors {

using Vec3 = std::array<float, 3>;

// Offsets into main of build e0f3ccfaf50065ae: the live actor list as
// coop_objects.cpp walks it, the actor's name, state and place
// (docs/puppet.md), and the AS play (CoopPuppet::addr::kPlayAs).
namespace addr {
inline constexpr std::uint64_t kActorManagerPtr = 0x4e91888;
inline constexpr std::uint64_t kManagerLists = 0x118;
inline constexpr std::uint64_t kListHead = 0x670;
inline constexpr std::uint64_t kListCount = 0x10;
inline constexpr std::uint64_t kNodeOwner = 0x10;
inline constexpr std::uint64_t kActorNode = 0x538;
inline constexpr std::uint64_t kActorName = 0x260;  // char*
inline constexpr std::uint64_t kActorState = 0x2c;
inline constexpr std::uint64_t kActorPosition = 0x31c;
// The placement hash u64 at [actor + 0x3d8] + 0x88 (~0 for actors the game
// made), as coop_objects.cpp reads it.
inline constexpr std::uint64_t kActorPlacement = 0x3d8;
inline constexpr std::uint64_t kPlacementHash = 0x88;
inline constexpr std::uint32_t kStatePresent = 4;
inline constexpr std::size_t kMaxWalk = 8192;
}  // namespace addr

// The mirror's block from the game's malloc: the play request (0x30 bytes,
// laid out as the puppet's, docs/puppet.md "Animation"), then one fixed cell
// per command name, since a slot may keep the pointer it was given.
namespace block {
inline constexpr std::uint64_t kRequest = 0x000;
inline constexpr std::uint64_t kCells = 0x040;
inline constexpr std::uint64_t kCell = 0x50;  // 79 characters and the 0
inline constexpr std::size_t kCellCount = 24;
inline constexpr std::uint64_t kSize = kCells + kCell * kCellCount;
}  // namespace block

struct Input {
    std::uint64_t main_base = 0;  // 0: no game, nothing is done
    std::string scene;            // ours; "" loading
    std::optional<Vec3> own;      // our Link
    std::vector<coop::host::DoorMover> others;  // in our place, with their command
    // ORM cofre: our Link's AS command ("" not read) and the server time (0
    // not connected), for the chest our Link opens.
    std::string own_command;
    std::uint64_t server_now_us = 0;
    std::uint8_t self = 0;  // ORM riesgos-visual: our player number, for a tie of two CHEST_OPENs
};

// The present thread's side: the others in our place (either era) who are
// shown, with their newest AS command, from the puppets' remote list.
Input Plan(std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<Vec3>& own, const std::vector<CoopPuppet::Remote>& remotes);

class Mirror {
public:
    using Log = std::function<void(const std::string&)>;
    explicit Mirror(Log log = {}, bool verbose = false, coop::host::DoorRules rules = {});

    // Any thread. The next calc frame uses it.
    void SetInput(Input input);

    // The game's calc thread, as a CoopSvc caller (coop_svc.h): once a frame
    // with after_call false, then after each call it returned.
    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call,
                                         std::uint64_t result, std::uint64_t now_ms);

    std::uint64_t Plays() const { return plays_; }
    std::uint64_t Walks() const { return walks_; }
    const coop::host::DoorMirror& Rules() const { return mirror_; }

    // ORM cofre. Off: no chest is told and no CHEST_OPEN opens a lid here.
    // Set once, before the caller is registered.
    void SetChestLids(bool on) {
        chest_lids_ = on;
        mirror_.set_chests_by_message(on);  // a mate's chest: his CHEST_OPEN, not his move
    }
    bool ChestLidsOn() const { return chest_lids_; }
    // Any thread: another player's CHEST_OPEN (relayed). The next calc frame takes it.
    void ReceiveChestOpen(const coop::protocol::ChestOpen& m);
    // Any thread: the CHEST_OPENs our Link's chest made since the last take.
    std::vector<coop::protocol::ChestOpen> TakeChestOpens();
    // Any thread: the chests whose lid we opened for another player (actors),
    // for the objects sync (coop_objects_sync.h SetLidsShown).
    std::vector<std::uint64_t> LidsShown() const;
    std::uint64_t LidsOpened() const { return lids_played_; }

    // Walk the list no more often than this while a door waits.
    static constexpr std::uint64_t kWalkEveryMs = 100;
    static constexpr std::uint64_t kMallocRetryMs = 5000;

    // ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): whether the door's or
    // chest's own logic plays over what we played (PUERTAS.md "si la IA de la
    // puerta pisa nuestra animacion"). After each play its slot 0 is read
    // every frame: ours held kHoldMs is the ok path (one "coop: check: door
    // mirror ok" or "chest lid ok" line); another command within kHoldMs of
    // seeing ours, or ours never seen within kSeenMs, is played again, at
    // most kReplays times; after that the fallback path: one "coop: check:
    // door mirror fallback" line, and the door is left as its game has it.
    static constexpr std::uint64_t kHoldMs = 300;
    static constexpr std::uint64_t kSeenMs = 1000;
    static constexpr int kReplays = 2;
    std::uint64_t Replays() const { return replays_; }

    // ORM riesgos-visual (DETALLES_MARIONETA.md "Cofres": "entre la tapa y la
    // bandera (1 a 3 s) el cofre de B todavia podria abrirse otra vez"): for
    // kReopenWindowMs after a lid opened here for another player's
    // CHEST_OPEN, our Link starting a chest move within kReopenReachM of it
    // is that second opening: one "coop: check: chest reopen fallback" line
    // (its find may count twice). The window over without one: "chest reopen
    // ok". Reopens() counts them.
    static constexpr std::uint64_t kReopenWindowMs = 5000;
    static constexpr float kReopenReachM = 2.5f;
    std::uint64_t Reopens() const { return reopens_; }
    // ORM riesgos-visual: chests (placement hashes) our game opened second
    // since the last take: a lid opened here for another player that our Link
    // then opened (CheckReopens), or ChestLids' second (his CHEST_OPEN came
    // first). Their counted find must not count again (FlagWatcher::
    // expect_double_find, coop_chest_open_game.cpp). Any thread.
    std::vector<std::uint64_t> TakeSecondFinds();

private:
    enum class Pending { none, malloc, play, frame };
    // One play to make: a door's, or a lid's with the frame to set after it.
    struct Play {
        std::uint64_t actor = 0;
        std::string command;
        std::optional<float> frame;
        std::string name;  // ORM riesgos-visual: the actor's name, for the watch's lines
        bool lid = false;  // a chest lid for a CHEST_OPEN (else a door rule's play)
        int replays = 0;   // how many times this one was played again
    };
    // ORM riesgos-visual: a play whose slot 0 is watched (kHoldMs, kSeenMs).
    struct Watch {
        Play play;
        std::uint64_t played_ms = 0;
        std::uint64_t seen_ms = 0;  // 0: ours not seen in its slot 0 yet
    };
    void CheckWatches(CoopPuppet::Guest& guest, std::uint64_t now_ms);
    // ORM riesgos-visual: a lid opened here for another player, watched kReopenWindowMs.
    struct LidTime {
        std::uint64_t actor = 0;
        std::string name;
        Vec3 at{};
        std::uint64_t at_ms = 0;
        std::uint64_t hash = 0;  // its placement, for TakeSecondFinds
    };
    void CheckReopens(std::uint64_t now_ms);
    bool Walk(CoopPuppet::Guest& guest, std::vector<coop::host::DoorActor>& out);
    std::string Command(CoopPuppet::Guest& guest, std::uint64_t actor);
    // ORM cofre: a chest's slot 0 frame and placement hash (0 not read).
    std::optional<float> SlotFrame(CoopPuppet::Guest& guest, std::uint64_t actor);
    std::uint64_t Hash(CoopPuppet::Guest& guest, std::uint64_t actor);
    std::optional<CoopPuppet::Call> NextPlay(CoopPuppet::Guest& guest);
    std::uint64_t Cell(CoopPuppet::Guest& guest, const std::string& name);
    void Say(const std::string& line) const;

    Log log_;
    bool verbose_;
    coop::host::DoorMirror mirror_;

    std::mutex input_mutex_;
    Input next_input_;  // guarded

    // The calc thread only.
    Input input_;
    std::string scene_;
    std::uint64_t base_ = 0;   // the main the block belongs to
    std::uint64_t block_ = 0;
    std::uint64_t malloc_retry_ms_ = 0;
    Pending pending_ = Pending::none;
    std::uint64_t walked_ms_ = 0;
    std::vector<Play> queue_;
    std::vector<std::string> cells_;  // the name in each cell, by index
    std::uint64_t plays_ = 0;
    std::uint64_t walks_ = 0;
    // The lid just played whose frame is set at the next call (Pending::frame).
    std::optional<Play> played_;
    // ORM riesgos-visual: the play just made (watched once the call is back).
    std::optional<Play> just_played_;
    std::vector<Watch> watches_;
    std::uint64_t replays_ = 0;
    std::vector<LidTime> lid_times_;
    coop::host::DoorMove own_move_ = coop::host::DoorMove::none;
    std::uint64_t reopens_ = 0;

    // ORM cofre.
    bool chest_lids_ = true;
    coop::host::ChestLids lids_;  // the calc thread only
    std::uint64_t lids_played_ = 0;
    mutable std::mutex chest_mutex_;
    std::vector<coop::protocol::ChestOpen> chest_in_;   // guarded
    std::vector<coop::protocol::ChestOpen> chest_out_;  // guarded
    std::vector<std::uint64_t> lids_shown_;             // guarded
    std::vector<std::uint64_t> second_finds_;           // guarded (TakeSecondFinds)
};

// The process-wide mirror (coop_doors_game.cpp configures it once and
// registers its caller). Any thread; null until configured.
void Configure(Mirror::Log log, bool verbose, bool chest_lids = true);
Mirror* TheMirror();
// CoopHook::OnFrame, the present thread: our scene and Link, our Link's AS
// command, the session (or null) and the server time, as CoopPuppet::Publish
// has them.
void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& own, std::string_view own_command,
             coop::host::Session* session, std::uint64_t server_now_us);
// Any thread: TheMirror()'s LidsShown; empty without a mirror.
std::vector<std::uint64_t> LidsShown();

// The emulator side (coop_doors_game.cpp): reads LICHII_COOP_DOORS,
// LICHII_COOP_DEBUG_DOORS and LICHII_ORM_CHEST_OPEN once, configures and
// registers the caller, then publishes every frame.
void Frame(std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<Vec3>& own, std::string_view own_command,
           coop::host::Session* session, std::uint64_t server_now_us);

}  // namespace CoopDoors
