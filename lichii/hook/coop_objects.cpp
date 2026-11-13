// SPDX-License-Identifier: GPL-3.0-or-later
//
// LICHII_COOP_DEBUG_OBJECTS=1: logs the game's actors as they come and go,
// and enemy life, for docs/objects.md ("Live check").
//
// The actor list, read live (build e0f3ccfaf50065ae):
//   manager  = [main + 0x4e91888]
//   head     = [manager + 0x118] + 0x670, a circular doubly linked list;
//              the u32 at head + 0x10 is its length
//   node     = {next +0, prev +8, owner +0x10}; an actor's own node is at
//              actor + 0x538, so owner == node - 0x538 for actors (a few
//              other objects share the list and are skipped)
// Actor fields: name char* +0x260, state u32 +0x2c, position 3 x f32 +0x31c,
// placement hash u64 at [+0x3d8] + 0x88 (~0 for actors the game made),
// life component = [[+0x270] + 0x2b8], life s32 at [life + 0x1c10] + 8,
// max at [life + 0x1c18] + 8.
//
// LICHII_COOP_OBJECTS_CHEAT=1 (test only, for the stage 5 live checks): keeps
// the bow, the small quiver with 30 arrows, the small bomb bag and 20 bombs in
// the local player's pouch, topped up once a second through the game's own
// flag write queue (docs/items.md hashes). It touches nothing else.
//
// LICHII_COOP_OBJECTS=1 shares enemies with the other games (Share and
// OnEvent below; the rules are in coop_objects_sync.h). Off by default until
// the two-game test passes. With it:
// - copies we do not own are placed every game frame where the owner's enemy
//   was, by the game's own setter on its calc thread (Mover; needs the svc
//   cave, so LICHII_COOP_PUPPET=1 or LICHII_COOP_SPAWN=1);
//   LICHII_COOP_OBJECTS_POSE=0 stops it;
// - a pickup another game took (a key64 flag set to 1) is deleted here;
// - a life a copy must lose is given to the game as damage (added to the s32
//   at Life + 0x228, which the Life step 0x49d8c0 sums into Life + 0x1cb0 and
//   takes off life), so the game reacts to it as to a hit;
//   that alone is not taken (LICHII_COOP_OBJECTS_HURT=1 still tries it), so
//   an enemy gets a whole hit record (kHitTemplate; on by default,
//   LICHII_COOP_OBJECTS_HIT_REPLAY=0 writes life directly instead);
// - a copy near our Link whose own AI plays another animation than its
//   owner's is asked for (issue 132; LICHII_COOP_OBJECTS_WAKE=0 stops it);
// LICHII_COOP_NPC=1 (ORM D-R12, off by default; docs/objects.md
// "Townspeople"): the townsperson our player talks to (coop/host/npc_talk.hpp,
// from our own event and the walk) is claimed in OBJECT_PRESENT, so the server
// gives it to us, and our copy of it is not moved or animated by its old
// owner meanwhile. Nothing new is read: the walk already has every Npc_.
// LICHII_COOP_NPC_POSE=1 (ORM, off by default; oot-remake
// docs/NPC_COMPARTIDOS.md, steps 5 to 7): the townspeople copies we do not
// own are held like enemy copies (the AI hold, coop_hold.h; mode 1 unless
// LICHII_COOP_OBJECTS_AI_HOLD says another), their plays fill the partial AS
// slots from slot 0 (0x3abec10, as the puppet), and the townsperson we own
// and talk to (LICHII_COOP_NPC=1) is sent bone by bone as OBJECT_POSE, which
// the other games write onto their copy (coop_npc_pose.h).
// LICHII_COOP_CARRY (ORM "Levantar", on by default; =0 off; oot-remake
// docs/DETALLES_MARIONETA.md, coop_carry.h): what our Link lifts is claimed
// and carried (its samples say it is in our hands), and what another player
// holds is drawn in the hands of his puppet, placed on the calc thread from
// the puppet as drawn there (LICHII_COOP_CARRY_BONE=0: its actor frame only),
// its speeds zeroed after each place so it does not fall between them
// (LICHII_COOP_CARRY_STILL=0: kept, the old path; CoopCarry::Still).
// LICHII_COOP_OBJECTS_TEST_DRIVE=1 (two-game tests only): on the game that
// LICHII_COOP_OBJECTS_TEST_TAKE=<hex key> (tests only): on player 2's game,
// 3 s after sharing starts, acts as if that 64-bit placement key arrived
// set to 1, so the actor with that placement hash is deleted here.
// owns it (player 1 only), one enemy (a ReDead if there is one) is held 3 m in front of our
// player, swaying 1.5 m side to side, and hurt by 1 every 4 s through the
// damage field. The other game's copy should do the same on its screen.
//
// This runs on the present thread while the guest runs, so a walk can see the
// list mid-change. A walk whose length differs from the head's count is
// dropped, and an actor counts as gone only when two walks in a row miss it.

#include "coop_objects.h"
#include "coop_doors.h"
#include "coop_carry.h"
#include "coop_every.h"
#include "coop_cheat_items.h"
#include "coop_flags.h"
#include "coop_hold.h"
#include "coop_puppet.h"
#include "coop_pvp_game.h"
#include "coop_svc.h"

#include <algorithm>
#include <utility>
#include <tuple>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "common/logging.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "coop/host/game_flags.hpp"
#include "coop/host/held_switch.hpp"
#include "coop/host/npc_talk.hpp"
#include "coop/host/scene.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop_npc_gaze.h"
#include "coop_npc_pose.h"
#include "coop_interest.h"
#include "coop_objects_sync.h"
#include "orm_ride.h"
#include "coop/host/ride.hpp"
#include "coop_ocarina.h"
#include "coop_spawn.h"
#include "coop_pickups.h"
#include "coop_projectiles.h"
#include "coop_guard.h"
#include <atomic>
#include <unordered_map>
#include "coop_world.h"
#include "coop_platforms.h"
#include "coop_counters.h"
#include "coop_scrubs.h"
#include <set>
#include "coop_switch.h"

namespace CoopObjects {
void NoteBosses(const std::vector<Seen>& seen);  // issue 109, below
namespace {

constexpr u64 kOcarinaProgramId = 0x0400486024DEC000ULL;
constexpr std::array<u8, 8> kGameBuildId{0xe0, 0xf3, 0xcc, 0xfa, 0xf5, 0x00, 0x65, 0xae};

constexpr u64 kActorManagerPtr = 0x4e91888;  // main offset
constexpr u64 kManagerLists = 0x118;
constexpr u64 kListHead = 0x670;
constexpr u64 kListCount = 0x10;
constexpr u64 kNodeOwner = 0x10;
constexpr u64 kActorNode = 0x538;
constexpr u64 kActorName = 0x260;
constexpr u64 kActorState = 0x2c;
constexpr u64 kActorPosition = 0x31c;
constexpr u64 kActorRotation = 0x328;  // 9 x f32, rows of 3
constexpr u64 kLifeHurt = 0x228;       // s32 damage this frame (0x49dc90)
constexpr u32 kStateAlive = 4;
// What the game sets on an actor about 25 m or more from its Link (docs/
// objects.md, "States seen"). A copy asleep at its home is still moved to
// its owner's place when that is this near our Link (issue 101), so an
// enemy chasing another player to us comes along and the game wakes it.
constexpr u32 kStateAsleep = 6;
constexpr float kAsleepPlaceM = 30.0f;
float Apart(const coop::protocol::Vec3& a, const coop::protocol::Vec3& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
// The game's own way to move an actor, as the AI node ExecuteActorForceSetMtx
// (run 0x2622e50) does it: write +0x31c..+0x34b (place, then the 3x3), then
// call vtable slot 0x218 (actor, 0). Every actor seen shares the vtable
// 0x4b25e18, whose slot 0x218 is 0x4c6580: it sets bit 23 of +0x4a8 and hands
// the new transform to the physics component (0x4c6630). Checked live: a
// plain write of +0x31c is undone by the game within a frame.
constexpr u64 kActorVtable = 0x4b25e18;
constexpr u64 kSetMatrix = 0x4c6580;
// Issues 150, 157, 101 (on unless LICHII_COOP_OBJECTS_RELEASE=0): the actor flags word the
// setter above marks (bit 23, docs/world.md). An enemy that becomes ours gets
// the bit cleared once, in case the game leaves it set after the Mover's
// last move and the body stays frozen or floats. Not proven.
constexpr u64 kActorFlags = 0x4a8;
constexpr u32 kWarpBit = u32{1} << 23;
constexpr u64 kRequestDelete = 0xa6490;  // (actor, 0), as docs/puppet.md
// Rolled enemies' copies in the spawn director: their keys have the top bit.
constexpr u64 kEnemyKey = u64{1} << 63;
constexpr unsigned kMoverCallerOrder = 30;  // after the puppets, spawn copies (10) and fairies (20)
constexpr unsigned kGrassCallerOrder = 40;  // the others' field grass cuts (coop_world.h)

CoopWorld::GrassSync& TheGrass() {
    static CoopWorld::GrassSync grass;
    return grass;
}

// Issue 149 (coop_scrubs.h): the Scrub Brothers' hits. The present thread
// looks and receives, the calc thread's cave gives the others' hits.
constexpr int kScrubsCallerOrder = 62;

std::string EnvText(const char* name);  // below EnvOn  // after the live events (60) and the teleport's (61)

struct ScrubsBox {
    std::mutex mutex;
    CoopScrubs::Puzzle puzzle{HitAnimations()};
    u64 main = 0;  // the game's main, for the cave

    static std::set<std::string> HitAnimations() {
        const std::string names = EnvText("LICHII_COOP_SCRUB_HIT_ANIMS");
        auto parsed = CoopScrubs::ParseAnimations(names);
        return parsed.empty() ? CoopScrubs::DefaultHitAnimations() : parsed;
    }
};

ScrubsBox& TheScrubs() {
    static ScrubsBox box;
    return box;
}

u64 SteadyNowMs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}
// Animations (docs/puppet.md "Animation"): the AS controller is
// [[[actor + 0x270] + 0x40] + 0x18] + 0x18; slot 0 is [controller + 0x20];
// its command name char* +0x10, rate f32 +0xd4, frame f32 +0xe8. The play
// request for 0x2022a0: +0 actor, +8 name, +0x10 slot, +0x14 rate, +0x18 and
// +0x1c -1.0, +0x20 skip-if-same, +0x28 node.
constexpr u64 kComponentsAs = 0x40;
constexpr u64 kAsInner = 0x18;
constexpr u64 kAsController = 0x18;
constexpr u64 kAsSlotCount = 0x18;
constexpr u64 kAsSlotArray = 0x20;
constexpr u64 kAsSlotEntry = 0x48;  // array entries; the first 8 bytes point to the slot (docs/puppet.md)
constexpr u64 kAsSlotName = 0x10;
constexpr u64 kAsSlotRate = 0xd4;
constexpr u64 kAsSlotFrame = 0xe8;
constexpr u64 kPlayAs = 0x2022a0;
constexpr u64 kSetAsFrame = 0x37b5d70;  // (controller, slot, s0 = frame), as the puppet
constexpr float kFrameSlack = 3.0f;     // frames off before a copy's frame is set
constexpr u64 kMalloc = 0x14c6c90;
// The mover's block from the game's malloc: the play request, then one
// fixed cell per animation name (a slot may keep the name's pointer).
constexpr u64 kBlockRequest = 0x0;
// ORM levantar-vasija: 16 zero bytes the speed calls of a carried copy point
// at (CoopCarry::Still), between the play request (0x30 bytes) and the names.
constexpr u64 kBlockZero = 0x30;
constexpr u64 kBlockNames = 0x40;
static_assert(kBlockZero >= kBlockRequest + 0x30 && kBlockZero + CoopCarry::still::kZeroBytes <= kBlockNames);
constexpr u64 kNameCell = 0x50;
constexpr u64 kNameCells = 400;
constexpr u64 kBlockSize = kBlockNames + kNameCell * kNameCells;

// The pause counters' ring (coop_counters.h): made once with the game's
// malloc on the calc thread, its address put where the cave reads it.
constexpr int kCountersCallerOrder = 63;
struct CounterRingBox {
    std::mutex mutex;
    u64 main = 0;    // set by Share while sharing; 0: nothing to do
    u64 ring = 0;
    bool asked = false;
    bool allocating = false;
};
CounterRingBox& TheCounterRing() {
    static CounterRingBox box;
    return box;
}

std::optional<CoopPuppet::Call> CounterRingStep(CoopPuppet::Guest& guest, bool after_call,
                                                std::uint64_t result) {
    CounterRingBox& box = TheCounterRing();
    std::lock_guard lock(box.mutex);
    if (after_call && box.allocating) {
        box.allocating = false;
        if (result != 0 && box.main != 0) {
            const std::array<u8, CoopCounters::kRingBytes> zeros{};
            guest.Write(result, zeros.data(), zeros.size());
            guest.Write(box.main + CoopCounters::kRingPointer, &result, 8);
            box.ring = result;
            LOG_INFO(Core, "coop: platforms: pause counter ring at {:#x}", result);
        }
        return std::nullopt;
    }
    if (after_call || box.main == 0 || box.ring != 0 || box.asked) {
        return std::nullopt;
    }
    u32 word = 0;  // only when the loader put the cave in (Hookshot: patch 0003's lift hook, not checkable here)
#ifdef COOP_HOOKSHOT
    word = CoopCounters::HookWord();
#else
    guest.Read(box.main + CoopCounters::kStartOffset, &word, 4);
#endif
    if (word != CoopCounters::HookWord()) {
        box.asked = true;
        LOG_INFO(Core, "coop: platforms: no counter cave (main+{:#x} holds {:#010x}): pause counters off",
                 CoopCounters::kStartOffset, word);
        return std::nullopt;
    }
    box.asked = true;
    box.allocating = true;
    CoopPuppet::Call c;
    c.function = box.main + kMalloc;
    c.x[0] = CoopCounters::kRingBytes;
    return c;
}
constexpr u64 kPlayLogMs = 1000;  // a copy's replays of one name: one log line a second
// LICHII_COOP_OBJECTS_HIT_REPLAY=1 (experiment): a life a copy must lose is
// given to the game as a whole hit, written into the copy's Life component
// on the calc thread before its step, shaped like the one a Kokiri Sword hit
// left on a ReDead (live dump, 2026-11-09): the attack header at +0x1f8, the
// amount at +0x224/+0x228, and the incoming hit at +0x240..+0x2d8 (attacker
// actor +0x240, hit id +0x248, present +0x24c, attack kind +0x250, the hit's
// place +0x27c, the attacker's place +0x2a0). The list links from +0x2d8 on
// are left alone. Our own player stands in as the attacker.
constexpr std::array<u32, 0x98 / 4> kHitTemplate{
    0x472d7288, 0x00000021, 0x000000b9, 0x00000001,  // +0x240 attacker, id, present
    0x00060001, 0x40040000, 0x00000014, 0x00000000,  // +0x250
    0x00220000, 0x00001000, 0x0000000a, 0x00000000,  // +0x260
    0x00000200, 0x00000000, 0x00000000, 0xc21085f6,  // +0x270, hit place from +0x27c
    0x42c63e88, 0x40501269, 0x3e76ed80, 0xb3c00000,  // +0x280
    0xbf787210, 0x40588db0, 0xc1129418, 0xc0cb90a0,  // +0x290
    0xc210600c, 0x42c45791, 0x409352f5, 0xbdcc4eb0,  // +0x2a0 attacker place
    0x3ecc555c, 0xc0d481b8, 0x000343ff, 0x00040000,  // +0x2b0
    0x00000004, 0x000000be, 0x00000002, 0x03000000,  // +0x2c0 (+0x2c4 id again)
    0x03010102, 0x00030000,                          // +0x2d0
};
constexpr u64 kHitRecord = 0x240;
constexpr u64 kHitLateMs = 200;  // under Sync::kHurtWaitUs
constexpr u64 kHitHeader = 0x1f8;
constexpr u64 kHitAmount = 0x224;
constexpr u64 kLastHitId = 0x1b18;
  // the copy's own AI took over: play again at most this often
constexpr u64 kDriftLogUs = 5'000'000;
constexpr u64 kActorComponents = 0x270;
// LICHII_COOP_OBJECTS_AI_HOLD=1 or 2 (off by default; docs/objects.md
// "Holding a copy's AI", coop_hold.h): the BSA component is table +0xb0
// (0xcad060), and bit 0 of its u16 at +0xc4 is what QueryBsaIsBrainSkip (run
// 0x29ce250) reads. Each game frame the Mover writes the held copies' BSA
// components into the table the hold cave reads, and counts in how many
// frames each one's bit read as asked.
constexpr u64 kComponentBsa = 0xb0;
constexpr u64 kBsaFlags = 0xc4;
constexpr u64 kComponentLife = 0x2b8;
constexpr u64 kLifeValue = 0x1c10;
constexpr u64 kLifeMax = 0x1c18;
constexpr u64 kLifeCopy = 0x1ca8;  // life and max as last mirrored
constexpr u64 kActorPlacement = 0x3d8;
constexpr u64 kPlacementHash = 0x88;
constexpr std::size_t kMaxWalk = 8192;

// LICHII_COOP_OBJECTS_CHEAT: what it sets is in coop_cheat_items.h.

// Keys to give back for locks opened twice (WorldSync::Out::key_refunds).
std::mutex& KeyRefundMutex() {
    static std::mutex m;
    return m;
}
std::vector<std::uint32_t>& KeyRefundQueue() {
    static std::vector<std::uint32_t> q;
    return q;
}
std::vector<std::uint32_t>& OwnDropQueue() {  // our own key spends, for WorldSync
    static std::vector<std::uint32_t> q;
    return q;
}
bool& ForgetKeptFlag() {  // a host save load: WorldSync::ForgetKept before the next walk
    static bool f = false;
    return f;
}
std::vector<std::string>& TestKillQueue() {  // the test script's kill steps (QueueTestKill)
    static std::vector<std::string> q;
    return q;
}

bool EnvFlag(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return false;
    }
    const bool on = value[0] != '\0' && std::string_view(value) != "0";
    std::free(value);
    return on;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::string_view(value) != "0";
#endif
}

// The variable as a hex number (0 when unset or not one).
u64 EnvHex(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return 0;
    }
    const u64 n = std::strtoull(value, nullptr, 16);
    std::free(value);
    return n;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? 0 : std::strtoull(value, nullptr, 16);
#endif
}

// The variable's number (0 when unset or not a number).
int EnvNumber(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return 0;
    }
    const int n = std::atoi(value);
    std::free(value);
    return n;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? 0 : std::atoi(value);
#endif
}

// Unset or anything but "0": on.
bool EnvOn(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return true;
    }
    const bool on = std::string_view(value) != "0";
    std::free(value);
    return on;
#else
    const char* value = std::getenv(name);
    return value == nullptr || std::string_view(value) != "0";
#endif
}

// Unset: empty.
std::string EnvText(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string text(value);
    std::free(value);
    return text;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

struct Actor {
    std::string name;
    u64 hash = 0;
    std::optional<std::array<s32, 4>> life;  // life, max, copy of life, copy of max
    unsigned missed = 0;
};

class Reader {
public:
    explicit Reader(Core::Memory::Memory& memory) : memory_(memory) {}

    template <typename T>
    std::optional<T> Get(u64 address) {
        if (address < 0x1000 || !memory_.IsValidVirtualAddressRange(address, sizeof(T))) {
            return std::nullopt;
        }
        T value{};
        memory_.ReadBlockUnsafe(address, &value, sizeof(T));
        return value;
    }

    std::optional<u64> Ptr(u64 address) {
        auto p = Get<u64>(address);
        if (!p || *p == 0) {
            return std::nullopt;
        }
        return p;
    }

    std::string Name(u64 actor) {
        const auto p = Ptr(actor + kActorName);
        if (!p) {
            return "?";
        }
        std::array<char, 64> buf{};
        if (!memory_.IsValidVirtualAddressRange(*p, buf.size())) {
            return "?";
        }
        memory_.ReadBlockUnsafe(*p, buf.data(), buf.size() - 1);
        return std::string(buf.data(), strnlen(buf.data(), buf.size()));
    }

private:
    Core::Memory::Memory& memory_;
};

// ORM "Levantar": the present thread's view of the game's memory for
// CoopCarry's reads (our Link's bones). Reads only.
class MemoryGuest final : public CoopPuppet::Guest {
public:
    explicit MemoryGuest(Core::Memory::Memory& memory) : memory_(memory) {}
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        if (address < 0x1000 || size == 0 || !memory_.IsValidVirtualAddressRange(address, size)) {
            return false;
        }
        memory_.ReadBlockUnsafe(address, out, size);
        return true;
    }
    bool Write(std::uint64_t, const void*, std::size_t) override { return false; }

private:
    Core::Memory::Memory& memory_;
};

// The game's calc thread, at the svc cave (coop_svc.h AddCaller): moves the
// copies with the game's own setter and deletes the taken pickups. The
// present thread hands it the latest wish list; each game frame it works
// through that list, one game call per pose or delete.
class Mover {
public:
    struct Pose {
        u64 actor = 0;
        std::array<float, 12> transform{};  // place, then the 3x3 rows
        bool rotation = false;              // false: keep the actor's own
        bool asleep = false;                // state 6 may be moved too (issue 101)
        // ORM "Levantar": in the hands of the puppet of carry.holder, placed
        // from that puppet as drawn this frame (CoopCarry::Attached), blended
        // from `transform` by `weight`. `transform` alone when the puppet or
        // its place cannot be read.
        struct Attach {
            coop::protocol::ObjectCarry carry;
            float weight = 1.0f;
        };
        std::optional<Attach> attach;
    };

    // ORM "Levantar": the carried copies follow the puppet's bone (on) or its
    // actor frame only (LICHII_COOP_CARRY_BONE=0).
    void SetCarryBone(bool on) {
        std::lock_guard lock(mutex_);
        carry_bone_ = on;
    }
    // ORM levantar-vasija: a copy in a puppet's hands has its speeds zeroed
    // after each place (on), or keeps them (LICHII_COOP_CARRY_STILL=0).
    void SetCarryStill(bool on) {
        std::lock_guard lock(mutex_);
        still_.SetOn(on);
    }
    // Carried copies placed since the last ask: on a bone, on the actor, and
    // left where the samples put them (no puppet of the holder here); then
    // the copy-frames held still (their speeds zeroed).
    std::array<std::size_t, 4> TakeCarryCounts() {
        std::lock_guard lock(mutex_);
        const std::array<std::size_t, 4> counts{carry_bone_count_, carry_actor_count_,
                                                carry_fallback_count_, still_.TakeStillFrames()};
        carry_bone_count_ = 0;
        carry_actor_count_ = 0;
        carry_fallback_count_ = 0;
        return counts;
    }
    // The lines of the copies let go since the last ask (CoopCarry::Still).
    std::vector<std::string> TakeStillLines() {
        std::lock_guard lock(mutex_);
        return still_.TakeLines();
    }

    struct Hit {
        u64 actor = 0;      // the copy
        u64 attacker = 0;   // our player
        s32 amount = 0;
        std::array<float, 3> at{};    // the copy's place
        std::array<float, 3> from{};  // the hitter's place (ours when not known)
        u64 asked_ms = 0;             // set by AddHit
        bool exact = false;           // `at` is where the blade was, not the actor's foot
    };

    // Present thread: hits to replay at the next game frame.
    void AddHit(const Hit& hit) {
        std::lock_guard lock(mutex_);
        if (hits_.size() < 64) {
            hits_.push_back(hit);
            hits_.back().asked_ms = SteadyMs();
        }
    }

    // ORM LICHII_COOP_NPC_POSE: write the townsperson poses on the calc
    // thread at the start of each game frame (coop_npc_pose.h).
    void SetNpcPose(bool on) {
        std::lock_guard lock(mutex_);
        npc_pose_ = on;
    }

    // ORM LICHII_COOP_NPC_GAZE: turn the townspeople's look slots to the
    // puppets on the calc thread at the start of each game frame (coop_npc_gaze.h).
    void SetNpcGaze(bool on) {
        std::lock_guard lock(mutex_);
        npc_gaze_ = on;
    }

    // Partial slot calls made since the last ask (ORM step 6).
    std::size_t TakePartials() {
        std::lock_guard lock(mutex_);
        return std::exchange(partials_, 0);
    }

    // Present thread: the enemy copies whose AI is held, and how.
    void Hold(std::vector<u64> actors, const CoopHold::Masks& masks) {
        std::lock_guard lock(mutex_);
        holds_ = std::move(actors);
        masks_ = masks;
        holding_ = true;
    }

    // Present thread: copies that just became ours, to clear the setter's
    // bit of at the next game frame (issues 150, 157; on unless
    // LICHII_COOP_OBJECTS_RELEASE=0).
    void Release(const std::vector<u64>& actors) {
        std::lock_guard lock(mutex_);
        for (const u64 a : actors) {
            if (releases_.size() < 64) {
                releases_.push_back(a);
            } else {
                ++release_dropped_;
            }
            // A pose wished before it became ours would set the bit again
            // in the same frame: it is no copy any more.
            std::erase_if(poses_, [a](const Pose& p) { return p.actor == a; });
            // Issue 157 (peahat autotest 2026-11-12): the Peahat's own AI
            // started 'TakeOffAnticipation' for our Link, the copy was put
            // back to 'GroundWait', and once it became ours it never tried
            // again: its AI waits for the end of a play we took away. Give
            // it that play back once.
            if (const auto w = wanted_.find(a); w != wanted_.end()) {
                if (SteadyMs() - w->second.second <= kWakeReplayMs && wakes_.size() < 64) {
                    wakes_.emplace_back(a, w->second.first);
                }
                wanted_.erase(w);
            }
        }
    }

    // Releases made since the last ask: bit found set and cleared, and not.
    // Also the ones dropped because the frame's list was full.
    std::tuple<std::size_t, std::size_t, std::size_t> TakeReleases() {
        std::lock_guard lock(mutex_);
        return {std::exchange(released_, 0), std::exchange(release_clear_, 0),
                std::exchange(release_dropped_, 0)};
    }

    void Want(u64 main, std::vector<Pose> poses, std::vector<u64> removes,
              std::vector<PlayWish> plays) {
        std::lock_guard lock(mutex_);
        main_ = main;
        poses_ = std::move(poses);
        removes_ = std::move(removes);
        plays_ = std::move(plays);
        want_ms_ = SteadyMs();
    }

    // Calls made since the last ask (for the 5 s log line).
    std::array<std::size_t, 4> TakeCounts() {
        std::lock_guard lock(mutex_);
        const std::array<std::size_t, 4> counts{moved_, deleted_, played_, framed_};
        moved_ = 0;
        deleted_ = 0;
        played_ = 0;
        framed_ = 0;
        return counts;
    }

    std::optional<CoopPuppet::Call> Step(CoopPuppet::Guest& guest, bool after_call,
                                         std::uint64_t result, std::uint64_t now_ms) {
        std::lock_guard lock(mutex_);
        if (after_call && allocating_) {
            allocating_ = false;
            block_ = result;
            return std::nullopt;  // next frame
        }
        if (after_call && frame_after_) {
            // The play just made: start it at the owner's frame, not at 0.
            const auto [actor, index, frame] = *frame_after_;
            frame_after_.reset();
            if (const u64 controller = Controller(guest, actor); controller != 0) {
                return SetFrame(controller, frame, index);
            }
        }
        // ORM step 6: then slot 0's command on the copy's partial slots.
        if (after_call && !partial_queue_.empty()) {
            const CoopPuppet::Call c = partial_queue_.front();
            partial_queue_.erase(partial_queue_.begin());
            ++partials_;
            return c;
        }
        // ORM levantar-vasija: after a carried copy's place, its speed calls.
        if (after_call && !still_queue_.empty()) {
            const CoopPuppet::Call c = still_queue_.front();
            still_queue_.erase(still_queue_.begin());
            return c;
        }
        if (!after_call) {
            partial_queue_.clear();
            still_queue_.clear();
            still_.Frame();
            if (npc_pose_) {
                CoopNpcPose::TheNpcPose().OnCalcFrame(guest, now_ms);
            }
            if (npc_gaze_) {
                // The puppets shown or held (not parked under their spot for a hide).
                const std::uint32_t live = CoopPuppet::LivePuppets();
                std::vector<CoopNpcGaze::PuppetAt> puppets;
                for (const auto& slot : CoopPuppet::TheDirector().Slots()) {
                    if (slot.actor != 0 && slot.number != 0 && slot.number < 32 &&
                        slot.phase == CoopPuppet::Phase::live && ((live >> slot.number) & 1u) != 0) {
                        puppets.push_back({slot.number, slot.actor,
                                           slot.actor_name.find("Adult") != std::string::npos});
                    }
                }
                CoopNpcGaze::TheWriter().OnCalcFrame(
                    guest, puppets, [this](CoopPuppet::Guest& g, u64 a) { return Alive(g, a); });
            }
            for (const u64 actor : std::exchange(releases_, {})) {
                u32 flags = 0;
                if (!Alive(guest, actor) || !guest.Read(actor + kActorFlags, &flags, 4)) {
                    continue;
                }
                if ((flags & kWarpBit) != 0) {
                    flags &= ~kWarpBit;
                    guest.Write(actor + kActorFlags, &flags, 4);
                    ++released_;
                } else {
                    ++release_clear_;
                }
            }
            // The block holds the play request and names, and the zero cell
            // of the carried copies' speed calls (levantar-vasija).
            const bool carrying = std::any_of(poses_.begin(), poses_.end(),
                                              [](const Pose& p) { return p.attach.has_value(); });
            if (block_ == 0 && main_ != 0 && !allocating_ && (!plays_.empty() || carrying) &&
                !alloc_tried_) {
                allocating_ = true;
                alloc_tried_ = true;
                CoopPuppet::Call c;
                c.function = main_ + kMalloc;
                c.x[0] = kBlockSize;
                return c;
            }
            work_plays_ = plays_;
            next_play_ = 0;
            work_wakes_ = std::exchange(wakes_, {});
            next_wake_ = 0;
            for (const Hit& h : hits_) {
                // A hit that waited too long (no game frame ran) is dropped:
                // by then its life has been written directly.
                if (now_ms < h.asked_ms + kHitLateMs) {
                    Replay(guest, h);
                }
            }
            hits_.clear();
            if (holding_) {
                WriteHolds(guest);
            }
            work_ = poses_;
            // ORM "Levantar": the puppets, by player, for the carried copies.
            puppets_.clear();
            if (std::any_of(work_.begin(), work_.end(), [](const Pose& p) { return p.attach.has_value(); })) {
                for (const auto& slot : CoopPuppet::TheDirector().Slots()) {
                    if (slot.actor != 0 && slot.number != 0) {
                        puppets_[slot.number] = slot.actor;
                    }
                }
            }
            if (work_.size() > 1) {
                std::rotate(work_.begin(), work_.begin() + (++turn_ % work_.size()), work_.end());
            }
            work_removes_.clear();
            for (const u64 a : removes_) {
                if (!asked_.contains(a)) {
                    work_removes_.push_back(a);
                }
            }
            if (removes_.empty()) {
                asked_.clear();
            }
            next_ = 0;
            next_remove_ = 0;
        }
        if (main_ == 0) {
            return std::nullopt;
        }
        while (next_wake_ < work_wakes_.size() && block_ != 0) {
            const auto [actor, name] = work_wakes_[next_wake_++];
            if (!Alive(guest, actor)) {
                continue;
            }
            const u64 controller = Controller(guest, actor);
            if (controller == 0 || SlotName(guest, controller) == name) {
                continue;  // its AI already plays it again
            }
            const u64 text = Cell(guest, name);
            if (text == 0) {
                continue;
            }
            std::array<std::uint8_t, 0x30> request{};
            const float unset = -1.0f;
            const float rate = 1.0f;
            const std::int32_t index = 0;
            std::memcpy(request.data() + 0x00, &actor, 8);
            std::memcpy(request.data() + 0x08, &text, 8);
            std::memcpy(request.data() + 0x10, &index, 4);
            std::memcpy(request.data() + 0x14, &rate, 4);
            std::memcpy(request.data() + 0x18, &unset, 4);
            std::memcpy(request.data() + 0x1c, &unset, 4);
            if (!guest.Write(block_ + kBlockRequest, request.data(), request.size())) {
                continue;
            }
            played_at_.erase(actor);
            ++woken_;
            LOG_INFO(Core, "coop: objects: {:#x} became ours: plays its own AI's '{}' again (issue 157)",
                     actor, name);
            CoopPuppet::Call c;
            c.function = main_ + kPlayAs;
            c.x[0] = block_ + kBlockRequest;
            return c;
        }
        while (next_play_ < work_plays_.size() && block_ != 0) {
            const PlayWish& p = work_plays_[next_play_++];
            if (!Alive(guest, p.address)) {
                continue;
            }
            const u64 controller = Controller(guest, p.address);
            if (controller == 0 || (p.slot != 0 && !HasSlot(guest, controller, p.slot))) {
                continue;
            }
            // Issue 150: a boss's other slots keep their own entries; only
            // slot 0 can be the copy's own AI taking over.
            const u64 key = p.slot == 0 ? p.address : p.address ^ (static_cast<u64>(p.slot) << 56);
            // Checked every game frame: the copy's own AI may have put its
            // own animation back since the last one.
            const float rate = p.rate;
            const u64 since = now_ms >= want_ms_ ? std::min<u64>(now_ms - want_ms_, 100) : 0;
            const float frame =
                FrameAfter(p.frame, rate, static_cast<std::int64_t>(since) * 1000);
            const std::string slot_name = SlotName(guest, controller, p.slot);
            if (slot_name == p.name) {
                auto& checked = checked_sample_[key];
                if (checked == p.sample) {
                    continue;
                }
                checked = p.sample;
                float now_frame = 0.0f;
                if (SlotFrame(guest, controller, now_frame, p.slot) &&
                    std::fabs(now_frame - frame) <= kFrameSlack) {
                    continue;
                }
                ++framed_;
                return SetFrame(controller, frame, p.slot);
            }
            // Issue 195: a chest shown open is played again only over its
            // closed idle, never over its own opening (our Link opens it).
            if (!p.only_over.empty() && !slot_name.empty() &&
                slot_name.find(p.only_over) == std::string::npos) {
                continue;
            }
            // Issue 157: the name we played last is gone from slot 0, so the
            // copy's own AI chose another one (a Peahat rising for our Link).
            // It is put back below at once, so the walk would rarely see it:
            // told to Sync as a wake (TakeTakeovers). A LOD variant of ours
            // is not the AI's.
            std::string took_over;
            if (const auto last = played_at_.find(p.address);
                p.slot == 0 && last != played_at_.end() && last->second.first == p.name &&
                !slot_name.empty() &&
                AnimationBase(slot_name) != AnimationBase(p.name)) {
                took_over = slot_name;
                if (took_over_.size() < 256) {
                    took_over_.emplace_back(p.address, slot_name);
                }
            }
            // Remembered even on the first play we make on this copy (the
            // peahat rerun: its rise was the first thing the Mover put back,
            // so the check above had nothing to compare with).
            if (p.slot == 0 && !slot_name.empty() &&
                AnimationBase(slot_name) != AnimationBase(p.name)) {
                if (wanted_.size() >= 256) {
                    std::erase_if(wanted_, [now_ms](const auto& e) {
                        return now_ms - e.second.second > kWakeReplayMs;
                    });
                }
                if (wanted_.size() < 256) {
                    wanted_[p.address] = {slot_name, now_ms};
                }
            }
            const u64 text = Cell(guest, p.name);
            if (text == 0) {
                continue;
            }
            std::array<std::uint8_t, 0x30> request{};
            const float unset = -1.0f;
            const std::int32_t index = p.slot;
            std::memcpy(request.data() + 0x00, &p.address, 8);
            std::memcpy(request.data() + 0x08, &text, 8);
            std::memcpy(request.data() + 0x10, &index, 4);
            std::memcpy(request.data() + 0x14, &rate, 4);
            std::memcpy(request.data() + 0x18, &unset, 4);
            std::memcpy(request.data() + 0x1c, &unset, 4);
            if (!guest.Write(block_ + kBlockRequest, request.data(), request.size())) {
                continue;
            }
            ++played_;
            auto& last = played_at_[key];
            if (last.first != p.name || now_ms - last.second >= kPlayLogMs) {
                LOG_INFO(Core, "coop: objects: copy {:#x} plays '{}' at frame {:.1f}{}{}{}{}",
                         p.address, p.name, frame,
                         p.slot == 0 ? std::string() : fmt::format(" on slot {}", p.slot),
                         took_over.empty() ? "" : " (its own AI: '", took_over,
                         took_over.empty() ? "" : "')");
                last = {p.name, now_ms};
            }
            checked_sample_[key] = p.sample;
            frame_after_ = std::make_tuple(p.address, p.slot, frame);
            if (p.partial) {  // ORM: the townsperson's other parts after its play (coop_npc_pose.h)
                partial_queue_ = CoopNpcPose::PartialCalls(
                    main_, p.address, CoopNpcPose::AsSlotCount(guest, p.address));
            }
            CoopPuppet::Call c;
            c.function = main_ + kPlayAs;
            c.x[0] = block_ + kBlockRequest;
            return c;
        }
        while (next_remove_ < work_removes_.size()) {
            const u64 a = work_removes_[next_remove_++];
            if (!Alive(guest, a)) {
                // Not awake now (another room, asleep): asked again on a
                // later frame. Marked asked only once really asked, or a
                // dead copy asleep when first asked stays for good (issue
                // 133: enemies left standing, frozen, for a late entrant).
                continue;
            }
            asked_.insert(a);
            ++deleted_;
            CoopPuppet::Call c;
            c.function = main_ + kRequestDelete;
            c.x[0] = a;
            c.x[1] = 0;
            return c;
        }
        // Poses last: the cave makes at most CoopSvc::kCallsPerFrame calls a
        // frame for everyone, and a scene may hold more copies than are left.
        // The list starts one further on each frame, so no copy is left out
        // twice in a row (run3, 2026-11-09: with 16 calls a frame, 3 to 5 of
        // 19 to 21 copies were never moved and no animation was ever played).
        while (next_ < work_.size()) {
            const Pose& p = work_[next_++];
            if (!Alive(guest, p.actor, p.asleep)) {
                continue;
            }
            if (p.attach) {
                // ORM "Levantar": in the holder's puppet's hands as it stands now.
                std::array<float, 12> t = p.transform;
                bool turned = p.rotation;
                const bool in_hands = Attach(guest, p, t);
                if (in_hands) {
                    turned = true;
                }
                guest.Write(p.actor + kActorPosition, t.data(), turned ? 48 : 12);
                // ORM levantar-vasija: in the hands, a still double: its
                // speeds zeroed with the game's own calls right after this
                // place (CoopCarry::Still); queued for the after-calls.
                bool still = false;
                if (in_hands) {
                    still = HoldStill(guest, p.actor);
                }
                if (hold_watch_.Falling() || (in_hands && !still)) {
                    // ORM riesgos-visual: it fell between our writes (HoldWatch),
                    // or it is not held still: the pending place too, which the
                    // actor's calc applies.
                    guest.Write(p.actor + CoopPuppet::addr::kActorPositionRequest, t.data(), 12);
                    if (turned) {
                        guest.Write(p.actor + CoopPuppet::addr::kActorRotationRequest, t.data() + 3, 36);
                    }
                }
            } else if (p.rotation) {
                guest.Write(p.actor + kActorPosition, p.transform.data(), 48);
            } else {
                guest.Write(p.actor + kActorPosition, p.transform.data(), 12);
            }
            ++moved_;
            CoopPuppet::Call c;
            c.function = main_ + kSetMatrix;
            c.x[0] = p.actor;
            c.x[1] = 0;
            return c;
        }
        return std::nullopt;
    }

    // Present thread: the copies whose own AI put another animation in slot 0
    // over the owner's since the last ask (issue 157).
    std::vector<std::pair<u64, std::string>> TakeTakeovers() {
        std::lock_guard lock(mutex_);
        return std::exchange(took_over_, {});
    }

    // Since the last ask: copy-frames held, and in how many of them the
    // copy's brain skip bit read as asked (the cave's work of the frame
    // before, seen after the game's frame).
    std::pair<std::size_t, std::size_t> TakeHolds() {
        std::lock_guard lock(mutex_);
        return {std::exchange(hold_set_, 0), std::exchange(hold_kept_, 0)};
    }

private:
    // Start of a game frame: the held copies' BSA components go into the
    // table the hold cave reads (an empty list empties it, so an enemy that
    // became ours runs its own AI again from this frame).
    void WriteHolds(CoopPuppet::Guest& guest) {
        if (main_ == 0) {
            return;
        }
        std::vector<u64> components;
        const u16 want = static_cast<u16>(masks_.or_mask & CoopHold::kBrainSkip);
        for (const u64 actor : holds_) {
            u64 table = 0;
            u64 bsa = 0;
            u16 flags = 0;
            if (!Alive(guest, actor) || !guest.Read(actor + kActorComponents, &table, 8) ||
                table == 0 || !guest.Read(table + kComponentBsa, &bsa, 8) || bsa == 0 ||
                !guest.Read(bsa + kBsaFlags, &flags, 2)) {
                continue;
            }
            if (held_before_.contains(bsa)) {
                ++hold_set_;
                if ((flags & CoopHold::kBrainSkip) == want) {
                    ++hold_kept_;
                }
            }
            components.push_back(bsa);
        }
        if (components.size() > CoopHold::kMaxHeld) {
            components.resize(CoopHold::kMaxHeld);
        }
        const auto bytes = CoopHold::Table(components, masks_);
        guest.Write(main_ + CoopHold::kTableOffset, bytes.data(), bytes.size());
        held_before_ = std::unordered_set<u64>(components.begin(), components.end());
    }

    static u64 SteadyMs() {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
    }

    CoopPuppet::Call SetFrame(u64 controller, float frame, std::int32_t index = 0) {
        CoopPuppet::Call c;
        c.function = main_ + kSetAsFrame;
        c.x[0] = controller;
        c.x[1] = static_cast<u64>(index);
        c.s0 = frame;
        return c;
    }

    // The actor's AS controller, or 0.
    u64 Controller(CoopPuppet::Guest& guest, u64 actor) {
        u64 table = 0;
        u64 as = 0;
        u64 inner = 0;
        u64 controller = 0;
        s32 count = 0;
        if (!guest.Read(actor + kActorComponents, &table, 8) || table == 0 ||
            !guest.Read(table + kComponentsAs, &as, 8) || as == 0 ||
            !guest.Read(as + kAsInner, &inner, 8) || inner == 0 ||
            !guest.Read(inner + kAsController, &controller, 8) || controller == 0 ||
            !guest.Read(controller + kAsSlotCount, &count, 4) || count < 1) {
            return 0;
        }
        return controller;
    }

    // Whether the controller has that many slots (issue 150: a boss's).
    bool HasSlot(CoopPuppet::Guest& guest, u64 controller, std::int32_t index) {
        s32 count = 0;
        return index >= 0 && guest.Read(controller + kAsSlotCount, &count, 4) && index < count &&
               count <= 64;
    }

    u64 Slot0(CoopPuppet::Guest& guest, u64 controller, std::int32_t index = 0) {
        u64 array = 0;
        u64 slot = 0;
        if (!guest.Read(controller + kAsSlotArray, &array, 8) || array == 0 ||
            !guest.Read(array + kAsSlotEntry * static_cast<u64>(index), &slot, 8)) {
            return 0;
        }
        return slot;
    }

    // A slot's command name, slot 0 unless told ("" when unreadable).
    std::string SlotName(CoopPuppet::Guest& guest, u64 controller, std::int32_t index = 0) {
        const u64 slot = Slot0(guest, controller, index);
        u64 at = 0;
        std::array<char, 64> text{};
        if (slot == 0 || !guest.Read(slot + kAsSlotName, &at, 8) || at == 0 ||
            !guest.Read(at, text.data(), text.size())) {
            return {};
        }
        return std::string(text.data(), strnlen(text.data(), text.size()));
    }

    bool SlotFrame(CoopPuppet::Guest& guest, u64 controller, float& frame, std::int32_t index = 0) {
        const u64 slot = Slot0(guest, controller, index);
        return slot != 0 && guest.Read(slot + kAsSlotFrame, &frame, 4) && std::isfinite(frame);
    }

    // Writes one hit into the copy's Life component (see kHitTemplate).
    void Replay(CoopPuppet::Guest& guest, const Hit& h) {
        if (main_ == 0 || !Alive(guest, h.actor)) {
            return;
        }
        u64 table = 0;
        u64 life = 0;
        if (!guest.Read(h.actor + kActorComponents, &table, 8) || table == 0 ||
            !guest.Read(table + kComponentLife, &life, 8) || life == 0) {
            return;
        }
        u32 last = 0;
        guest.Read(life + kLastHitId, &last, 4);
        const u32 id = (last == 0xffffffffu ? 0x1000u : last + 2u) & 0x7fffffffu;
        std::array<u32, kHitTemplate.size()> rec = kHitTemplate;
        std::memcpy(&rec[0], &h.attacker, 8);
        rec[2] = id;
        rec[0x2c4 / 4 - kHitRecord / 4] = id;
        const std::array<float, 3> at{h.at[0], h.at[1] + (h.exact ? 0.0f : 1.0f), h.at[2]};
        std::memcpy(&rec[0x27c / 4 - kHitRecord / 4], at.data(), 12);
        std::memcpy(&rec[0x2a0 / 4 - kHitRecord / 4], h.from.data(), 12);
        rec[3] = 0;  // present last, below
        const std::array<u32, 4> header{0x00060001, 0x40040000, id, 2};
        const std::array<s32, 2> amount{4, h.amount};
        guest.Write(life + kHitHeader, header.data(), sizeof(header));
        guest.Write(life + kHitAmount, amount.data(), sizeof(amount));
        guest.Write(life + kHitRecord, rec.data(), sizeof(rec));
        const u32 present = 1;
        guest.Write(life + kHitRecord + 0xc, &present, 4);
        ++replayed_;
        LOG_INFO(Core, "coop: objects: copy {:#x} replays a hit of {} (id {:#x})", h.actor,
                 h.amount, id);
    }

    // The fixed cell holding `name` in the block (written once), or 0 when
    // the block is full or the name too long.
    u64 Cell(CoopPuppet::Guest& guest, const std::string& name) {
        if (const auto it = cells_.find(name); it != cells_.end()) {
            return it->second;
        }
        if (name.size() >= kNameCell || cells_.size() >= kNameCells) {
            return 0;
        }
        const u64 at = block_ + kBlockNames + kNameCell * cells_.size();
        std::array<char, kNameCell> text{};
        std::memcpy(text.data(), name.data(), name.size());
        if (!guest.Write(at, text.data(), text.size())) {
            return 0;
        }
        cells_.emplace(name, at);
        return at;
    }

    // ORM "Levantar": the transform of a carried copy from its holder's
    // puppet (bone or actor frame), blended in from `t` (the samples' place)
    // by the pose's weight. False (t unchanged) when the puppet is not here
    // or its place does not read.
    bool Attach(CoopPuppet::Guest& guest, const Pose& p, std::array<float, 12>& t) {
        const auto puppet = puppets_.find(p.attach->carry.holder);
        CoopCarry::Anchor used = CoopCarry::Anchor::none;
        const auto at = puppet != puppets_.end()
                            ? CoopCarry::Attached(guest, puppet->second, p.attach->carry,
                                                  carry_bone_, &used)
                            : std::nullopt;
        if (!at) {
            ++carry_fallback_count_;
            hold_watch_.Forget(p.actor);
            return false;
        }
        (used == CoopCarry::Anchor::bone ? carry_bone_count_ : carry_actor_count_)++;
        CoopCarry::Frame frame = *at;
        if (p.attach->weight < 1.0f) {
            CoopCarry::Frame from;
            from.p = {p.transform[0], p.transform[1], p.transform[2]};
            if (p.rotation) {
                std::copy(p.transform.begin() + 3, p.transform.end(), from.r.begin());
            } else if (const auto own = CoopCarry::ReadActor(guest, p.actor)) {
                from.r = own->r;
            }
            frame = CoopCarry::Blend(from, *at, p.attach->weight);
        }
        // ORM riesgos-visual: does the copy stay where it was put last frame?
        if (const auto now = CoopCarry::ReadActor(guest, p.actor)) {
            hold_watch_.Note(p.actor, now->p, frame.p);
        }
        std::copy(frame.p.begin(), frame.p.end(), t.begin());
        std::copy(frame.r.begin(), frame.r.end(), t.begin() + 3);
        return true;
    }

    // ORM levantar-vasija: queue the speed calls that hold the copy `actor`,
    // just placed in a puppet's hands, still (CoopCarry::Still). False when it
    // takes the fallback (no body, its flags keep its speed, no zero cell).
    bool HoldStill(CoopPuppet::Guest& guest, u64 actor) {
        using Cell = CoopCarry::Still::Cell;
        Cell cell = Cell::none;
        if (block_ != 0) {
            const std::array<std::uint8_t, CoopCarry::still::kZeroBytes> zero{};
            cell = guest.Write(block_ + kBlockZero, zero.data(), zero.size()) ? Cell::ready : Cell::none;
        } else if (allocating_ || !alloc_tried_) {
            cell = Cell::coming;
        }
        const auto calls = still_.Hold(guest, main_, actor, block_ + kBlockZero, cell);
        for (const CoopCarry::GameCall& g : calls) {
            CoopPuppet::Call c;
            c.function = g.function;
            std::copy(g.x.begin(), g.x.end(), c.x.begin());
            still_queue_.push_back(c);
        }
        return !calls.empty();
    }

    // On the game thread, so the list cannot change under us: still on the
    // actor list (its node points back at it), awake, and an actor.
    // `asleep`: state 6 counts too (a copy moved to us from its far home).
    bool Alive(CoopPuppet::Guest& guest, u64 a, bool asleep = false) {
        u32 state = 0;
        u64 owner = 0;
        u64 vtable = 0;
        return a != 0 && guest.Read(a + kActorState, &state, 4) &&
               (state == kStateAlive || (asleep && state == kStateAsleep)) &&
               guest.Read(a + kActorNode + kNodeOwner, &owner, 8) && owner == a &&
               guest.Read(a, &vtable, 8) && vtable == main_ + kActorVtable;
    }

    std::mutex mutex_;
    u64 main_ = 0;
    // ORM "Levantar" (guarded): the puppets of this frame by player, and counts.
    std::map<std::uint8_t, u64> puppets_;
    bool carry_bone_ = true;
    std::size_t carry_bone_count_ = 0;
    std::size_t carry_actor_count_ = 0;
    std::size_t carry_fallback_count_ = 0;
    CoopCarry::HoldWatch hold_watch_;  // ORM riesgos-visual: carried copies that fall between writes
    CoopCarry::Still still_;           // ORM levantar-vasija: carried copies held still
    std::vector<CoopPuppet::Call> still_queue_;  // their speed calls, after the place
    std::vector<Pose> poses_;
    std::vector<u64> removes_;
    std::vector<PlayWish> plays_;
    std::vector<Hit> hits_;
    std::size_t replayed_ = 0;
    std::vector<PlayWish> work_plays_;
    std::size_t next_play_ = 0;
    std::size_t played_ = 0;
    std::size_t framed_ = 0;  // frames set on a copy already showing the name
    u64 want_ms_ = 0;
    std::optional<std::tuple<u64, std::int32_t, float>> frame_after_;  // actor, slot, frame: set after the play
    std::unordered_map<u64, std::uint64_t> checked_sample_;  // actor: sample time checked
    std::unordered_map<u64, std::pair<std::string, u64>> played_at_;  // actor: name, ms
    std::vector<std::pair<u64, std::string>> took_over_;  // copies whose own AI changed slot 0, and to what
    std::unordered_map<std::string, u64> cells_;
    u64 block_ = 0;
    bool allocating_ = false;
    bool alloc_tried_ = false;
    std::vector<Pose> work_;
    // Issue 157: the animation each copy's own AI last chose that we put
    // back (name, steady ms), and the ones to give back once it is ours.
    static constexpr u64 kWakeReplayMs = 8000;
    std::unordered_map<u64, std::pair<std::string, u64>> wanted_;
    std::vector<std::pair<u64, std::string>> wakes_;
    std::vector<std::pair<u64, std::string>> work_wakes_;
    std::size_t next_wake_ = 0;
    std::size_t woken_ = 0;
    std::vector<u64> work_removes_;
    std::unordered_set<u64> asked_;  // deletes asked once
    std::size_t next_ = 0;
    std::size_t turn_ = 0;  // where this frame's poses start
    std::size_t next_remove_ = 0;
    std::size_t moved_ = 0;
    std::size_t deleted_ = 0;
    std::vector<u64> holds_;
    CoopHold::Masks masks_;
    bool holding_ = false;
    std::vector<u64> releases_;
    std::size_t released_ = 0;       // bit 23 was set: cleared
    std::size_t release_clear_ = 0;  // bit 23 was already clear
    std::size_t release_dropped_ = 0;  // over 64 in one frame: not looked at
    std::unordered_set<u64> held_before_;  // BSA components in last frame's table
    std::size_t hold_set_ = 0;
    std::size_t hold_kept_ = 0;
    bool npc_pose_ = false;                          // ORM LICHII_COOP_NPC_POSE
    bool npc_gaze_ = false;                          // ORM LICHII_COOP_NPC_GAZE
    std::vector<CoopPuppet::Call> partial_queue_;    // after a play's frame set
    std::size_t partials_ = 0;
};

Mover& TheMover() {
    static Mover mover;
    return mover;
}

// Whether our own Link is in an event (a talk, a cutscene), from the hook's
// reading each frame (SetLocalEvent; issue 144).
std::atomic<bool>& TheLocalEvent() {
    static std::atomic<bool> in_event{false};
    return in_event;
}

// ORM c3: the running event's name, from the emulator thread to the present thread's talk (SetLocalEventName).
struct LocalEventName {
    std::mutex mutex;
    std::optional<std::string> name;
};
LocalEventName& TheLocalEventName() {
    static LocalEventName n;
    return n;
}
std::optional<std::string> LocalEventNameNow() {
    auto& n = TheLocalEventName();
    std::lock_guard lock(n.mutex);
    return n.name;
}

// ORM endgame (SetKeep): bit 0 up, bit 1 frozen, bit 2 charging, bit 3 held (ORM boss
// families), bits 8 to 15 the charge + 1 (0: not read).
std::atomic<std::uint32_t>& TheKeep() {
    static std::atomic<std::uint32_t> keep{1};
    return keep;
}

Sync::Keep KeepNow() {
    const std::uint32_t word = TheKeep().load();
    Sync::Keep keep;
    keep.up = (word & 1u) != 0;
    keep.frozen = (word & 2u) != 0;
    keep.charging = (word & 4u) != 0;
    keep.held = (word & 8u) != 0;
    if (const std::uint32_t charge = (word >> 8) & 0xffu; charge != 0) {
        keep.charge = charge - 1;
    }
    return keep;
}

// The MaxLife flag as the session left it (SetMaxLife); -1: not known.
std::atomic<unsigned>& TheChestsOpenedTwice() {
    static std::atomic<unsigned> count{0};
    return count;
}

std::atomic<s32>& TheMaxLife() {
    static std::atomic<s32> quarters{-1};
    return quarters;
}

// Our Link's Life component as the walk last read it (OurLife): quarter
// hearts, and when (CoopEvery::NowUs; 0: never).
struct LifeSeen {
    std::atomic<s32> life{-1};
    std::atomic<s32> max{-1};
    std::atomic<u64> at_us{0};
};
LifeSeen& TheLife() {
    static LifeSeen seen;
    return seen;
}
// PvP (coop_pvp_game.cpp): our Link's life as the last walk read it, and
// when (steady ms; 0: never), for OurLinkLife; and the hurts the victim's
// rules decided (HurtOurLink), for the walk to give.
std::atomic<s32>& TheLinkLife() {
    static std::atomic<s32> quarters{-1};
    return quarters;
}
std::atomic<u64>& TheLinkLifeAt() {
    static std::atomic<u64> ms{0};
    return ms;
}
struct PvpAsk {
    s32 take = 0;
    std::array<float, 3> at{};
    std::array<float, 3> from{};
};
std::mutex& PvpMutex() {
    static std::mutex mutex;
    return mutex;
}
std::vector<PvpAsk>& PvpAsks() {
    static std::vector<PvpAsk> asks;
    return asks;
}
constexpr u64 kLinkLifeStaleMs = 1500;
constexpr u64 kPvpHitWaitMs = 400;  // the game's own hit, before the life is written

class Objects {
public:
    void Frame(Core::System& system) {
        const auto now_us = CoopEvery::NowUs();
        if (!scan_.Due(now_us) || !Ready(system)) {
            return;
        }
        if (cheat_ && cheat_every_.Due(now_us)) {
            Cheat(system);
        }
        if (debug_) {
            Reader r(system.ApplicationMemory());
            Walk(r);
        }
    }

    // Present thread, after the session's frame. Not connected, or no scene:
    // forgets what it shared.
    void Share(Core::System& system, coop::host::Session* session,
               const std::optional<std::string>& scene, u64 server_now_us, bool event) {
        if (session == nullptr || !scene || scene->empty() || server_now_us == 0) {
            if (sharing_) {
                sync_.Reset();
                world_.Reset();
                lifter_.Reset();
                carried_logged_.clear();
                npc_talk_.reset();
                npc_claim_.reset();
                npc_claim_logged_ = coop::host::NpcClaim::State::none;
                npc_rejoin_logged_.clear();
                npc_checked_.clear();
                CoopNpcPose::TheNpcPose().Reset();
                npc_pose_source_logged_.reset();
                npc_pose_targets_logged_.clear();
                sharing_ = false;
            }
            walk_actors_.clear();
            walk_scene_.clear();
            return;
        }
        if (!Ready(system)) {
            return;
        }
        if (test_take_ != 0 && sharing_ && self_ == 2 && take_at_ != 0 &&
            server_now_us >= take_at_) {
            take_at_ = 0;
            coop::protocol::FlagState state;
            state.player_number = 1;
            state.flags.push_back(coop::protocol::FlagEntry(
                coop::protocol::FlagKey::key64_flag(test_take_), coop::protocol::FlagKind::boolean, 1));
            sync_.Receive(state);
            LOG_INFO(Core, "coop: objects: test take {:#x}", test_take_);
        }
        if (drive_ && sharing_) {
            Drive(system, server_now_us);
        }
        if (pose_ && sharing_) {
            Place(system, server_now_us);
        }
        if (npc_pose_ && sharing_) {
            NpcPoseFrame(*session, server_now_us);
        }
        if (!share_every_.Due(CoopEvery::NowUs())) {
            return;
        }
        Reader r(system.ApplicationMemory());
        memory_ = &system.ApplicationMemory();
        std::vector<Seen> seen;
        if (!Collect(r, seen)) {
            return;
        }
        NoteBosses(seen);
        // ORM moving floors (orm_ride.h): the floors and our Link of this walk; and (ORM boss
        // families, docs/JEFES.md section 9) the grabbing parts and what holds our Link, from the
        // last Tick in this scene (one walk old).
        {
            OrmRide::Grabs grabs;
            if (families_ && sync_.FamilyScene() == *scene) {
                grabs.parts = sync_.FamilyParts();
                grabs.ours = sync_.FamilyRide();
            }
            OrmRide::Walked(*scene, player_, ride_platforms_, grabs);
        }
        const u8 self =
            session->with_client([](coop::net::Client& c) { return c.player_number(); });
        if (self == 0) {
            return;
        }
        if (!sharing_) {
            static const bool registered = [] {
                CoopSvc::AddCaller(kMoverCallerOrder,
                                   [](CoopPuppet::Guest& guest, bool after_call,
                                      std::uint64_t result, std::uint64_t now_ms) {
                                       return TheMover().Step(guest, after_call, result, now_ms);
                                   });
                CoopSvc::AddCaller(kCountersCallerOrder,
                                   [](CoopPuppet::Guest& guest, bool after_call,
                                      std::uint64_t result, std::uint64_t) {
                                       return CounterRingStep(guest, after_call, result);
                                   });
                CoopSvc::AddCaller(kGrassCallerOrder,
                                   [](CoopPuppet::Guest& guest, bool after_call,
                                      std::uint64_t result, std::uint64_t now_ms) {
                                       return TheGrass().Step(guest, after_call, result, now_ms);
                                   });
                // Issue 149: another player's hit on a Scrub Brother, given to our manager.
                CoopSvc::AddCaller(kScrubsCallerOrder,
                                   [](CoopPuppet::Guest& guest, bool after_call, std::uint64_t result,
                                      std::uint64_t) -> std::optional<CoopPuppet::Call> {
                                       ScrubsBox& box = TheScrubs();
                                       std::lock_guard lock(box.mutex);
                                       if (box.main == 0) {
                                           return std::nullopt;
                                       }
                                       auto call = box.puzzle.Step(guest, after_call, result, box.main,
                                                                   CoopEvery::NowUs());
                                       for (const auto& line : box.puzzle.TakeNotes()) {
                                           LOG_INFO(Core, "coop: {}", line);
                                       }
                                       return call;
                                   });
                return true;
            }();
            (void)registered;
            CoopSpawn::UseDirector();  // makes the rolled enemies another game decided
            sync_.SetHurt(hurt_);
            sync_.SetWake(wake_);  // issue 132: LICHII_COOP_OBJECTS_WAKE=0 turns it off
            // ORM golpes: our hits on others' enemies shown at once, copies smoothed.
            sync_.SetHits(hit_feedback_);
            sync_.SetHitDedupe(hit_dedupe_);
            sync_.SetCopySmooth(copy_smooth_);
            LOG_INFO(Core, "coop: objects: share hits feedback {}, owner dedupe {}, copy smoothing {}",
                     hit_feedback_ ? "on" : "off", hit_dedupe_ ? "on" : "off (logged only)",
                     copy_smooth_ ? "on" : "off");
            // ORM steps 5 to 7: townspeople copies held, partial slots, OBJECT_POSE.
            sync_.SetNpcPose(npc_pose_);
            sync_.SetHoldEnemies(ai_hold_ != 0);
            // ORM boss families (coop/host/boss_rules.hpp, oot-remake docs/JEFES.md section 9).
            sync_.SetFamilies(families_);
            LOG_INFO(Core,
                     "coop: objects: boss families {} (a grab keeps its grabber with the held one's game, "
                     "a volley its tennis boss; LICHII_ORM_BOSS_FAMILIES)",
                     families_ ? "on" : "off");
            TheMover().SetNpcPose(npc_pose_);
            TheMover().SetNpcGaze(npc_gaze_ && npc_);
            if (npc_gaze_) {
                LOG_INFO(Core, "coop: npc gaze: {}",
                         npc_ ? "on (townspeople look at a partner's puppet when he is the nearest "
                                "player or talks to them)"
                              : "asked, but LICHII_COOP_NPC is off: nothing turned");
            }
            TheMover().SetCarryBone(carry_bone_);
            TheMover().SetCarryStill(carry_still_);
            if (carry_) {
                LOG_INFO(Core,
                         "coop: carry: on (lifted objects claimed and carried; others' drawn in "
                         "their puppet's hands by {}; bomb flowers {}; a copy in the hands {})",
                         carry_bone_ ? "bone" : "the actor frame",
                         CoopCarry::ShareFlowers() ? "shared" : "each game's own",
                         carry_still_ ? "held still (its speeds zeroed)"
                                      : "keeps its speed (LICHII_COOP_CARRY_STILL=0)");
            }
            if (npc_pose_) {
                LOG_INFO(Core,
                         "coop: npc pose: on (townspeople copies held in ai hold mode {}, partial "
                         "slots, OBJECT_POSE at {} a second){}",
                         ai_hold_ != 0 ? ai_hold_ : 1, CoopNpcPose::kSendHz,
                         npc_ ? "" : "; LICHII_COOP_NPC is off, so this game sends no pose");
            }
            // A projectile copy's blast here is not our hit (docs/objects.md, "Projectiles").
            sync_.SetBlastNear([](const coop::protocol::Vec3& at, std::uint64_t now) {
                return CoopProjectiles::TheBlasts().Near(at, now);
            });
            take_at_ = server_now_us + 3'000'000;
            LOG_INFO(Core, "coop: objects: share on, poses {}, hurt {}", pose_ ? "on" : "off",
                     hurt_ ? "through the game" : "written as life");
        }
        self_ = self;
        sharing_ = true;
        std::vector<coop::protocol::Message> send;
        std::vector<LifeWrite> writes;
        {
            std::map<std::uint64_t, std::uint64_t> live;
            for (const auto& [key, actor] : CoopSpawn::TheDirector().Live()) {
                if ((key & kEnemyKey) != 0) {
                    live[key & ~kEnemyKey] = actor;
                }
            }
            sync_.Bind(live);
        }
        if (npc_) {
            // ORM: the others' PLAYER_STATUS flags as presence holds them (the
            // one store; the downed, spectator and escape code read it too).
            std::map<u8, u8> statuses;
            session->with_presence([&](coop::presence::Presence& p) {
                for (const u8 n : p.players()) {
                    if (const auto* player = p.player(n)) {
                        statuses[n] = player->flags();
                    }
                }
            });
            Talk(*scene, event, server_now_us, seen, self, statuses);
        }
        sync_.SetLidsShown(CoopDoors::LidsShown());  // ORM cofre: lids opened for a CHEST_OPEN stay
        sync_.SetKeep(KeepNow());  // ORM endgame: Twinrova's owner (docs/FINAL.md)
        sync_.SetFamilyExtras(std::move(family_extras_));  // ORM boss families: the tennis balls
        family_extras_.clear();
        if (carry_) {
            Carry(system.ApplicationMemory(), *scene, server_now_us, seen, self);
        }
        sync_.Tick(*scene, self, server_now_us, seen, send, writes);
        if (const unsigned twice = sync_.ChestsOpenedTwice(); twice != chests_twice_seen_) {
            // Issue 195: a chest shown open that our Link opened again; the
            // flag watcher takes back its content's count (TakeChestsOpenedTwice).
            if (twice > chests_twice_seen_) {
                TheChestsOpenedTwice() += twice - chests_twice_seen_;
            }
            chests_twice_seen_ = twice;
        }
        {
            // Enemy shots (coop_enemy_shots.h): which game's enemy fired one.
            std::vector<CoopEnemyShots::Shooter> shooters;
            for (const Seen& s : seen) {
                if (s.name.starts_with("Enemy_")) {
                    const auto owner = sync_.OwnerOf(s.address);
                    shooters.push_back({s.name, s.position,
                                        !owner || *owner == 0 ? CoopEnemyShots::Owner::unknown
                                        : *owner == self      ? CoopEnemyShots::Owner::ours
                                                              : CoopEnemyShots::Owner::theirs});
                }
            }
            CoopEnemyShots::NoteShooters(std::move(shooters), server_now_us);
        }
        Walked(*scene, seen, self);
        if (sync_.BlastDrops() != blast_drops_logged_) {
            blast_drops_logged_ = sync_.BlastDrops();
            LOG_INFO(Core, "coop: shots: life drop by a projectile copy's blast kept back ({} so far)",
                     blast_drops_logged_);
        }
        {
            std::vector<CoopSpawn::Copy> enemies;
            for (const EnemyCopy& e : sync_.Creates()) {
                CoopSpawn::Copy c;
                c.key = e.key | kEnemyKey;
                c.actor = e.name;
                c.position = e.position;
                c.act = e.removed ? CoopSpawn::Act::remove : CoopSpawn::Act::show;
                c.place = false;
                c.remakes = e.remakes;  // issue 124: a copy that sank by itself comes back
                enemies.push_back(std::move(c));
            }
            if (enemies.size() != enemies_logged_ || sync_.Refused() != refused_logged_ ||
                sync_.Decider() != decider_logged_) {
                enemies_logged_ = enemies.size();
                refused_logged_ = sync_.Refused();
                decider_logged_ = sync_.Decider();
                LOG_INFO(Core, "coop: objects: rolls decided by player {}, {} copies to make, {} own rolls deleted",
                         decider_logged_, enemies_logged_, refused_logged_);
            }
            CoopSpawn::TheDirector().SetEnemies(main_, std::move(enemies));
        }
        for (const auto& note : sync_.TakeNotes()) {
            LOG_INFO(Core, "coop: objects: share {}", note);
        }
        for (const auto& line : sync_.TakeFamilyLines()) {  // ORM boss families
            LOG_INFO(Core, "coop: {}", line);
        }
        // Breaks and moves of placed world objects (docs/world.md).
        {
            CoopWorld::WorldSync::Out world;
            {
                std::lock_guard lock(KeyRefundMutex());
                for (const std::uint32_t key : OwnDropQueue()) {
                    world_.OwnCountDrop(key, server_now_us);
                }
                OwnDropQueue().clear();
                if (std::exchange(ForgetKeptFlag(), false)) {
                    world_.ForgetKept();
                }
            }
            // ORM "Levantar": a break of what a Link holds is the holder's game's.
            world_.SetHands(carry_ ? lifter_.Claimed(server_now_us) : std::set<std::uint64_t>{},
                            std::exchange(carried_by_others_, {}));
            world_.Tick(*scene, self, server_now_us, seen, player_, world);
            for (auto& m : world.send) {
                send.emplace_back(std::move(m));
            }
            writes.insert(writes.end(), world.lives.begin(), world.lives.end());
            TestKills(seen, writes);
            if (!world.key_refunds.empty()) {
                std::lock_guard lock(KeyRefundMutex());
                auto& refunds = KeyRefundQueue();
                refunds.insert(refunds.end(), world.key_refunds.begin(), world.key_refunds.end());
            }
            sign_words_ = std::move(world.words);  // written after the probe's look
            for (std::string& count : world.give_back) {
                CoopWorld::QueueGiveBack(std::move(count));  // a small key or token both took
            }
            for (const std::uint64_t key : world.taken_keys) {
                CoopWorld::QueueTakenKey(key);  // a token taken elsewhere: its Skulltula's key
            }
            block_fixes_ = std::move(world.blocks);  // issue 147, written below
            if (!world.hits.empty()) {
                // A sign another player's sword passed: the same hit on ours.
                u64 link = 0;
                for (const auto& [a, known] : names_) {
                    if (known.first == "Player_Child" || known.first == "Player_Adult") {
                        link = a;
                        break;
                    }
                }
                for (const auto& hit : world.hits) {
                    if (link == 0) {
                        break;
                    }
                    Mover::Hit h;
                    h.actor = hit.address;
                    h.attacker = link;
                    h.amount = 1;
                    h.at = hit.at;
                    h.from = hit.from;
                    h.exact = true;
                    TheMover().AddHit(h);
                }
            }
            for (const auto& note : world.notes) {
                if (world_lines_ < 2000) {
                    ++world_lines_;
                    LOG_INFO(Core, "coop: {}", note);
                }
            }
        }
        if (scrubs_on_) {  // issue 149: our hits on the Scrub Brothers go out
            CoopScrubs::Puzzle::Out out;
            {
                ScrubsBox& box = TheScrubs();
                std::lock_guard lock(box.mutex);
                box.main = main_;
                box.puzzle.SetEvent(TheLocalEvent().load(), CoopEvery::NowUs());  // issue 144
                box.puzzle.Look(*scene, scrubs_seen_, player_, CoopEvery::NowUs(), out);
            }
            for (auto& h : out.send) {
                send.emplace_back(std::move(h));
            }
            for (const auto& line : out.notes) {
                LOG_INFO(Core, "coop: {}", line);
            }
        }
        // Field grass (docs/world.md): our new cuts go out, the others' are
        // made at the svc cave.
        for (auto& cut : TheGrass().Look([&r](u64 address) { return r.Get<u64>(address); }, main_,
                                         *scene, SteadyNowMs())) {
            LOG_INFO(Core, "coop: grass: cut at {:.2f} {:.2f} {:.2f} radius {:.2f} flags {:#x} sent",
                     cut.position[0], cut.position[1], cut.position[2], cut.radius, cut.flags);
            {
                CoopWorld::WorldSync::Out swing;  // a sign the sword passed (docs/world.md)
                world_.Swing(cut, player_, server_now_us, swing);
                CoopPvp::OwnSwing(cut);  // PvP: the swing may reach another Link
                for (auto& m : swing.send) {
                    send.emplace_back(std::move(m));
                }
                for (const auto& note : swing.notes) {
                    LOG_INFO(Core, "coop: {}", note);
                }
            }
            send.emplace_back(std::move(cut));
        }
        for (const auto& note : TheGrass().TakeNotes()) {
            LOG_INFO(Core, "coop: {}", note);
        }
        // Signs (issue 90, docs/world.md "Signs"): the probe; our cut's words
        // go out (SIGN_CUT), another player's are written into our sign.
        {
            CoopWorld::WorldSync::Out signs;
            world_.ProbeSigns([&r](u64 address) { return r.Get<u64>(address); }, server_now_us,
                              signs);
            for (auto& m : signs.send) {
                send.emplace_back(std::move(m));
            }
            for (const auto& note : signs.notes) {
                LOG_INFO(Core, "coop: {}", note);
            }
            for (const auto& w : sign_words_) {
                std::string why;
                const auto at = world_.SignWordTarget(
                    [&r](u64 address) { return r.Get<u64>(address); }, w, &why);
                if (at) {
                    memory_->Write64(*at, w.value);
                } else {
                    LOG_INFO(Core, "coop: world: {:016x} sign word {} not written: {}", w.hash,
                             CoopWorld::SignProbe::Where(w.key), why);
                }
            }
            sign_words_.clear();
            // Issue 147: a block another game moved rests here now; its own
            // memory's old rest place gets the new one.
            for (const auto& fix : block_fixes_) {
                // world-objects-2: the block's push node (known fields,
                // found by its vtables and owner): on unless
                // LICHII_COOP_BLOCK_NODE=0.
                static const bool node_on = EnvOn("LICHII_COOP_BLOCK_NODE");
                if (node_on) {
                    const auto node = CoopWorld::FindBlockNode(
                        [&r](u64 address) { return r.Get<u64>(address); }, fix.address, main_);
                    if (!node) {
                        LOG_INFO(Core, "coop: world: {:016x} {} push node not found: its next push may "
                                       "start from the old place",
                                 fix.hash, fix.name);
                    } else {
                        const auto words = CoopWorld::NodeRestWords(
                            [&r](u64 address) { return r.Get<u64>(address); }, *node, fix.from, fix.to);
                        if (words.busy) {
                            LOG_INFO(Core, "coop: world: {:016x} {} push node {:x} busy (mid-push here): "
                                           "not written",
                                     fix.hash, fix.name, *node);
                            continue;  // RestWords below too: one block, one decision
                        }
                        for (const auto& w : words.writes) {
                            memory_->Write32(w.address, w.value);
                        }
                        std::string at;
                        for (const auto& w : words.where) {
                            at += " " + w;
                        }
                        LOG_INFO(Core, "coop: world: {:016x} {} push node {:x}: {} of its places moved to "
                                       "the new rest{}{}",
                                 fix.hash, fix.name, *node, words.where.size(), at.empty() ? "" : ":", at);
                    }
                }
                const auto scan = CoopWorld::RestWords(
                    [&r](u64 address) { return r.Get<u64>(address); }, fix.address, fix.from,
                    fix.to);
                std::string where;
                for (const auto& w : scan.where) {
                    where += " " + w;
                }
                if (scan.too_many) {
                    LOG_INFO(Core, "coop: world: {:016x} {} rest place {:.2f} {:.2f} {:.2f} found {} times, "
                                   "not written (too many):{}",
                             fix.hash, fix.name, fix.from[0], fix.from[1], fix.from[2],
                             scan.where.size(), where);
                    continue;
                }
                // The searched sizes are not known object sizes (review):
                // written only with LICHII_COOP_BLOCK_REST=1; otherwise only
                // where they would go is logged, for a run to name the word.
                static const bool write_rest = EnvFlag("LICHII_COOP_BLOCK_REST");
                if (write_rest) {
                    for (const auto& w : scan.writes) {
                        memory_->Write32(w.address, w.value);
                    }
                }
                LOG_INFO(Core, "coop: world: {:016x} {} old rest {:.2f} {:.2f} {:.2f} -> {:.2f} {:.2f} {:.2f}: "
                               "{} places {}{}{}",
                         fix.hash, fix.name, fix.from[0], fix.from[1], fix.from[2], fix.to[0],
                         fix.to[1], fix.to[2], scan.where.size(),
                         write_rest ? "rewritten" : "found, not written (LICHII_COOP_BLOCK_REST off)",
                         where.empty() ? "" : ":", where);
            }
            block_fixes_.clear();
        }
        // Probe (docs/dungeon.md "Live dungeon actions"): which words of a
        // switch, water plane or lift step when it changes. Reads only.
        if (gimmick_probe_on_) {
            for (const auto& line : gimmick_probe_.Look([&r](u64 address) { return r.Get<u64>(address); },
                                                        gimmicks_, player_, server_now_us)) {
                LOG_INFO(Core, "coop: {}", line);
            }
        }
        // Issue 152 (docs/puppet.md "Ocarina"): where our note is kept. Reads only.
        if (ocarina_probe_on_) {
            for (const auto& line : ocarina_probe_.Look([&r](u64 address) { return r.Get<u64>(address); },
                                                        our_link_, server_now_us)) {
                LOG_INFO(Core, "coop: {}", line);
            }
            // Issue 152: what moves the Ocarina from the belt to the hand.
            for (const auto& line : ocarina_hold_probe_.Step([&r](u64 address) { return r.Get<u64>(address); },
                                                             our_link_, ocarina_probe_.Active(), server_now_us)) {
                LOG_INFO(Core, "coop: {}", line);
            }
            OcarinaHandProbe(system, r, server_now_us);
        }
        if (const u64 made = TheGrass().Made(); made != grass_made_logged_) {
            grass_made_logged_ = made;
            LOG_INFO(Core, "coop: grass: {} cuts of the others made here", made);
        }
        for (const Seen& s : seen) {
            if (!s.animation || !IsShared(s.name)) {
                continue;
            }
            auto& last = anim_logged_[s.address];
            if (last != s.animation->name && anim_lines_ < 400) {
                ++anim_lines_;
                LOG_INFO(Core, "coop: objects: anim {} {:#x} '{}'{}", s.name, s.address,
                         s.animation->name,
                         sync_.OwnerOf(sync_.IdOf(s.address).value_or(coop::protocol::ObjectId{})) ==
                                 self
                             ? " (ours)"
                             : "");
                last = s.animation->name;
            }
        }
        // Issue 144: a kill or hurt from another game waits while our Link is in
        // an event with that actor (near it), and is made when the event ends.
        {
            if (*scene != guard_scene_) {
                guard_scene_ = *scene;
                // Those actors are gone with the scene (enemies come back with it).
                if (const std::size_t dropped = held_writes_.Forget(); dropped != 0) {
                    LOG_INFO(Core, "coop: objects: the scene changed: {} waiting changes dropped", dropped);
                }
                guard_said_ = false;
            }
            where_.clear();
            for (const Seen& s : seen) {
                // Who is at the address: placement hash, name and the sync's object id
                // (actors the game makes share one "no hash"), so a reused address is
                // told apart (CoopGuard::Who).
                std::optional<std::uint64_t> object;
                if (const auto id = sync_.IdOf(s.address)) {
                    object = id->value ^ (std::uint64_t{id->maker} << 56) ^
                             (std::uint64_t(static_cast<unsigned>(id->kind)) << 48);
                }
                const u64 who = CoopGuard::Who(s.hash, s.name, object);
                where_.emplace(s.address, std::make_pair(s.position, who));
            }
            const CoopGuard::Local local = GuardLocal();
            auto passed = held_writes_.Pass(
                std::move(writes), local, [this](std::uint64_t a) { return Where(a); },
                [this](std::uint64_t a) { return HashAt(a); });
            writes = std::move(passed.apply);
            // The held ones land now: the Sync asked for them long ago, and their drop
            // is no hit of our Link's (else it went to the owner as DAMAGE again).
            for (std::size_t i = 0; i < passed.released; ++i) {
                sync_.Written(writes[i].address, writes[i].life, server_now_us);
            }
            if (held_writes_.Size() != 0 && !guard_said_) {
                guard_said_ = true;
                LOG_INFO(Core, "coop: objects: our Link is in an event: changes to {} actors near it wait until it ends",
                         held_writes_.Size());
            } else if (held_writes_.Size() == 0 && guard_said_ && !local.in_event) {
                guard_said_ = false;
                LOG_INFO(Core, "coop: objects: the event ended: {} waiting changes made now, {} dropped (another actor there now)",
                         passed.released, passed.dropped);
            }
        }
        for (const LifeWrite& w : writes) {
            WriteLife(system.ApplicationMemory(), r, w);
        }
        if (platforms_mode_ != 0) {
            Platforms(r, *scene, self, server_now_us, send);
        }
        if (send.empty()) {
            return;
        }
        session->with_client([&](coop::net::Client& c) {
            for (const auto& m : send) {
                const bool sent = c.send(m);
                if (const auto* sp = std::get_if<coop::protocol::ObjectSpawn>(&m)) {
                    LOG_INFO(Core, "coop: objects: rolled {} shared as made {}{}", sp->actor,
                             sp->id.value, sent ? "" : " (not sent)");
                } else if (const auto* rm = std::get_if<coop::protocol::ObjectRemove>(&m)) {
                    LOG_INFO(Core, "coop: objects: share remove {:#x} reason {}{}", rm->id.value,
                             static_cast<int>(rm->reason), sent ? "" : " (not sent)");
                } else if (const auto* d = std::get_if<coop::protocol::Damage>(&m)) {
                    LOG_INFO(Core, "coop: objects: share damage {} to {:#x} epoch {}{}", d->amount,
                             d->id.value, d->epoch, sent ? "" : " (not sent)");
                } else if (const auto* p = std::get_if<coop::protocol::ObjectPresent>(&m);
                           p != nullptr && p->objects.size() != present_logged_) {
                    present_logged_ = p->objects.size();
                    LOG_INFO(Core, "coop: objects: share present {} enemies in {}{}",
                             p->objects.size(), p->scene, sent ? "" : " (not sent)");
                } else if (const auto* g = std::get_if<coop::protocol::ObjectGroup>(&m);
                           g != nullptr && g->entries != group_logged_) {
                    // Only the entries that are new or changed, at most 8 lines.
                    std::size_t logged = 0, changed = 0;
                    for (const auto& e : g->entries) {
                        if (std::find(group_logged_.begin(), group_logged_.end(), e) !=
                            group_logged_.end()) {
                            continue;
                        }
                        ++changed;
                        if (logged++ < 8) {
                            LOG_INFO(Core, "coop: objects: group {:#x} follows {:#x}{}",
                                     e.member.value, e.root.value,
                                     e.flags != 0 ? " (boss)" : "");
                        }
                    }
                    LOG_INFO(Core, "coop: objects: share group {} entries, {} changed{}",
                             g->entries.size(), changed, sent ? "" : " (not sent)");
                    group_logged_ = g->entries;
                } else if (const auto* f = std::get_if<coop::protocol::FishLure>(&m)) {
                    // Issue 151: one line when the fish at our lure change.
                    std::size_t in_reach = 0;
                    std::string at;
                    for (const auto& e : f->fish) {
                        if (e.distance_cm == coop::protocol::kFishLureNone) continue;
                        ++in_reach;
                        if (e.flags != 0) {
                            at += fmt::format(" {:#x} {} cm {}", e.id.value, e.distance_cm,
                                              (e.flags & coop::protocol::fish_lure::hooked)
                                                  ? "hooked"
                                                  : "drawn");
                        }
                    }
                    if (std::make_pair(in_reach, at) != fish_logged_) {
                        fish_logged_ = std::make_pair(in_reach, at);
                        LOG_INFO(Core, "coop: objects: share fish lure {} fish, {} near our lure{}{}",
                                 f->fish.size(), in_reach, at, sent ? "" : " (not sent)");
                    }
                }
            }
        });
    }

    // ORM "Levantar" (coop_carry.h): what our own Link lifts is claimed and
    // our copy left to our game (Sync::SetCarry); while it is ours, its
    // samples carry where it sits in our Link's hands. Only routes what the
    // walk read and our Link's bones; the rule is CoopCarry::Lifter's.
    void Carry(Core::Memory::Memory& memory, const std::string& scene, u64 now_us,
               const std::vector<Seen>& seen, u8 self) {
        std::optional<CoopCarry::LinkNow> link;
        if (player_ && link_rotation_) {
            CoopCarry::LinkNow l;
            l.frame.p = *player_;
            l.frame.r = CoopCarry::Rigid(*link_rotation_).value_or(l.frame.r);
            l.command = link_command_;
            link = l;
        }
        std::vector<CoopCarry::Candidate> candidates;
        for (const Seen& s : seen) {
            if (!CoopCarry::IsLiftable(s.name) || !IsShared(s.name)) {
                continue;
            }
            CoopCarry::Candidate c;
            c.address = s.address;
            c.name = s.name;
            c.position = s.position;
            c.rotation = s.rotation;
            c.held_by_other = sync_.CarriedByOther(s.address, now_us);
            if (c.held_by_other) {
                carried_by_others_.insert(s.address);  // the world sync's SetHands
            }
            candidates.push_back(std::move(c));
        }
        MemoryGuest guest(memory);
        std::optional<CoopCarry::Skeleton> skeleton;
        if (carry_bone_ && our_link_ != 0 && link) {
            skeleton = CoopCarry::FindSkeleton(guest, our_link_);
        }
        const CoopCarry::Frame link_frame = link ? link->frame : CoopCarry::Frame{};
        CoopCarry::Lifter::BoneFinder find;
        CoopCarry::Lifter::BoneReader bone;
        if (skeleton) {
            find = [&](const coop::protocol::Vec3& at) {
                return CoopCarry::NearestBone(guest, *skeleton, link_frame, at);
            };
            bone = [&](std::uint16_t b) {
                return CoopCarry::ReadBone(guest, *skeleton, link_frame, b);
            };
        }
        lifter_.Update(scene, link, candidates, now_us, find, bone);
        std::map<std::uint64_t, coop::protocol::ObjectCarry> ours;
        if (const auto held = lifter_.Held()) {
            if (const auto carry = lifter_.CarryOf(*held, self)) {
                ours.emplace(*held, *carry);
            }
        }
        sync_.SetCarry(lifter_.Claimed(now_us), std::move(ours));
        for (const CoopCarry::Event& e : lifter_.TakeEvents()) {
            const auto id = sync_.IdOf(e.address);
            const u8 owner = id ? sync_.OwnerOf(*id) : 0;
            if (e.kind == CoopCarry::Event::Kind::lifted) {
                LOG_INFO(Core, "coop: carry: lifting {} {:#x} ({}, claimed){}", e.name, e.address,
                         owner == self ? std::string("ours")
                         : owner == 0  ? std::string("no owner yet")
                                       : fmt::format("owner P{}", owner),
                         e.bone ? fmt::format(", by bone {}", *e.bone)
                                : std::string(", by our Link's frame"));
            } else {
                LOG_INFO(Core, "coop: carry: let go of {} {:#x} ({}){}", e.name, e.address,
                         CoopCarry::NameOf(e.why), owner == self ? "" : ", not ours yet");
            }
        }
        // The hand-over of what we hold, once: from then on its samples carry it.
        const auto held = lifter_.Held();
        const auto held_id = held ? sync_.IdOf(*held) : std::nullopt;
        const bool ours_now = held_id && sync_.OwnerOf(*held_id) == self;
        if (held && ours_now && carry_ours_logged_ != *held) {
            carry_ours_logged_ = *held;
            LOG_INFO(Core, "coop: carry: {:#x} is ours now: its samples carry it in our hands",
                     *held);
        } else if (!held) {
            carry_ours_logged_ = 0;
        }
    }

    // ORM D-R12: the townsperson our player talks to (coop/host/npc_talk.hpp)
    // is claimed and our copy of it freed (Sync::SetTalk). Only routes what
    // the walk read; the rule is portable.
    void Talk(const std::string& scene, bool event, u64 now_us, const std::vector<Seen>& seen,
              u8 self, const std::map<u8, u8>& statuses) {
        if (scene != npc_scene_) {
            npc_talk_.reset();
            npc_claim_.reset();
            npc_gaze_rule_.Reset();
            CoopNpcGaze::TheWriter().Reset();  // the old scene's townspeople are gone: nothing to give back
            npc_claim_logged_ = coop::host::NpcClaim::State::none;
            npc_scene_ = scene;
        }
        std::vector<coop::host::NpcSeen> npcs;
        // ORM charla-radio: each one's event component phase, so the game
        // itself says who we talk to (coop::host::read_event_phase).
        std::optional<CoopHook::EmulatorMemory> guest;
        if (memory_ != nullptr && main_ != 0) {
            guest.emplace(*memory_, main_);
        }
        for (const Seen& s : seen) {
            if (!IsShared(s.name) || !coop::host::is_talker(s.name)) {
                continue;
            }
            coop::host::NpcSeen n;
            n.address = s.address;
            n.name = s.name;
            n.position = s.position;
            if (s.animation) {
                n.animation = s.animation->name;
            }
            if (guest) {
                n.event_phase = coop::host::read_event_phase(*guest, s.address);
            }
            npcs.push_back(std::move(n));
        }
        const std::size_t talkers = npcs.size();
        for (const Seen& s : stand_ins_) {
            coop::host::NpcSeen n;
            n.address = s.address;
            n.name = s.name;
            n.position = s.position;
            if (guest) {
                n.event_phase = coop::host::read_event_phase(*guest, s.address);
            }
            npcs.push_back(std::move(n));
        }
        npc_talk_.set_event_name(event ? LocalEventNameNow() : std::nullopt);
        npc_talk_.update(event, player_, npcs, now_us);
        if (const auto refused = npc_talk_.take_refused()) {
            LOG_INFO(Core, "coop: npc: our event '{}' is not a talk: no townsperson taken by reach", *refused);
        }
        npcs.resize(talkers);  // the partner is always a townsperson, never a stand-in
        std::set<std::uint64_t> claims, freed;
        const auto partner = npc_talk_.partner();
        // ORM "Dos jugadores con el mismo PNJ": a townsperson its owner is
        // talking to is not claimed from it (coop::host::NpcClaim); our talk
        // still plays on our freed copy.
        for (const auto& [player, flags] : statuses) {
            npc_claim_.see_status(player, coop::host::in_cutscene(flags), now_us);
        }
        const auto partner_seen =
            partner ? std::find_if(npcs.begin(), npcs.end(),
                                   [&](const auto& n) { return n.address == *partner; })
                    : npcs.end();
        coop::host::NpcOwner npc_owner;
        if (partner) {
            const auto partner_id = sync_.IdOf(*partner);
            npc_owner.player = partner_id ? sync_.OwnerOf(*partner_id) : 0;
            if (npc_owner.player != 0 && npc_owner.player != self) {
                const auto st = statuses.find(npc_owner.player);
                npc_owner.in_event = st != statuses.end() && coop::host::in_cutscene(st->second);
                npc_owner.link = sync_.PlaceOf(npc_owner.player, now_us);
                if (npc_pose_ && partner_id) {
                    for (const CoopNpcPose::Heard& h :
                         CoopNpcPose::TheNpcPose().Fresh(MonotonicMs())) {
                        npc_owner.posing =
                            npc_owner.posing || (h.id == *partner_id && h.player == npc_owner.player);
                    }
                }
            }
        }
        const auto claim = npc_claim_.update(
            partner, partner_seen != npcs.end() ? std::string_view(partner_seen->name) : "",
            partner_seen != npcs.end() ? partner_seen->position : coop::protocol::Vec3{}, self,
            npc_owner, now_us);
        if (partner) {
            if (claim == coop::host::NpcClaim::State::claimed) {
                claims.insert(*partner);
            }
            freed.insert(*partner);
        }
        sync_.SetTalk(std::move(claims), std::move(freed), now_us);
        if (claim != npc_claim_logged_) {
            const std::string name =
                partner_seen != npcs.end() ? partner_seen->name : std::string("?");
            const u64 address = partner ? *partner : 0;
            switch (claim) {
            case coop::host::NpcClaim::State::yielded:
                LOG_INFO(Core,
                         "coop: npc: {} {:#x} talks with its owner P{}: not claimed, our talk "
                         "plays on our copy only",
                         name, address, npc_owner.player);
                break;
            case coop::host::NpcClaim::State::timed_out:
                LOG_INFO(Core, "coop: npc: claim on {} {:#x} held {} s, let go (stuck?)", name,
                         address, coop::host::NpcClaim::claim_max_us(name) / 1'000'000);
                break;
            case coop::host::NpcClaim::State::claimed:
                if (npc_claim_logged_ == coop::host::NpcClaim::State::yielded) {
                    LOG_INFO(Core, "coop: npc: {} {:#x} is ours now: claimed", name, address);
                } else if (npc_claim_.owner_stuck()) {
                    LOG_INFO(Core,
                             "coop: npc: owner P{} of {} {:#x} has been in its event over {} s: "
                             "taken as stuck, claimed",
                             npc_owner.player, name, address,
                             coop::host::NpcClaim::claim_max_us(name) / 1'000'000);
                }
                break;
            case coop::host::NpcClaim::State::none:
                break;
            }
            npc_claim_logged_ = claim;
        }
        {
            std::vector<std::uint64_t> rejoining = sync_.Rejoining(now_us);
            for (const std::uint64_t address : rejoining) {
                if (std::find(npc_rejoin_logged_.begin(), npc_rejoin_logged_.end(), address) ==
                    npc_rejoin_logged_.end()) {
                    LOG_INFO(Core,
                             "coop: npc: copy {:#x} blends back to its owner's pose over {} ms",
                             address, CoopObjects::Sync::kRejoinUs / 1000);
                }
            }
            npc_rejoin_logged_ = std::move(rejoining);
        }
        if (partner != npc_logged_) {
            if (partner) {
                const auto it = std::find_if(npcs.begin(), npcs.end(),
                                             [&](const auto& n) { return n.address == *partner; });
                const auto id = sync_.IdOf(*partner);
                const u8 owner = id ? sync_.OwnerOf(*id) : 0;
                LOG_INFO(Core, "coop: npc: talking to {} {:#x} ({}){}, found {}",
                         it != npcs.end() ? it->name : std::string("?"), *partner,
                         owner == self ? std::string("ours")
                         : owner == 0  ? std::string("no owner yet")
                         : claim == coop::host::NpcClaim::State::claimed
                             ? fmt::format("owner P{}, claimed", owner)
                             : fmt::format("owner P{}, not claimed", owner),
                         npc_talk_.lingering() ? ", talk over" : "",
                         coop::host::to_string(npc_talk_.found()));
                // ORM charla-radio self-check: which path took it, once per
                // townsperson and talk (the scenarios expect this line).
                if (npc_checked_.insert(*partner).second) {
                    LOG_INFO(Core, "coop: {}",
                             coop::host::talk_check_line(
                                 it != npcs.end() ? std::string_view(it->name) : "?",
                                 npc_talk_.found(), npc_talk_.stand_in(),
                                 npc_talk_.distance_m()));
                }
            } else {
                LOG_INFO(Core, "coop: npc: talk over, claim let go");
                npc_checked_.clear();
            }
            npc_logged_ = partner;
        }
        if (npc_pose_) {
            // ORM step 7: the townsperson we own and talk to goes out bone by
            // bone (at most one: npc_talk_ has one partner).
            std::optional<CoopNpcPose::Source> source;
            if (partner) {
                const auto id = sync_.IdOf(*partner);
                const auto epoch = id ? sync_.EpochOf(*id) : std::nullopt;
                if (id && epoch && sync_.OwnerOf(*id) == self) {
                    source = CoopNpcPose::Source{*id, *epoch, *partner};
                }
            }
            CoopNpcPose::TheNpcPose().SetSource(source);
            if (source != npc_pose_source_logged_) {
                if (source) {
                    const auto it = std::find_if(npcs.begin(), npcs.end(), [&](const auto& n) {
                        return n.address == source->actor;
                    });
                    LOG_INFO(Core, "coop: npc pose: sending {} {:#x} (id {:#x}, epoch {})",
                             it != npcs.end() ? it->name : std::string("?"), source->actor,
                             source->id.value, source->epoch);
                } else {
                    LOG_INFO(Core, "coop: npc pose: sending stopped");
                }
                npc_pose_source_logged_ = source;
            }
        }
        if (npc_gaze_) {
            Gaze(event, now_us, npcs, partner, self, statuses);
        }
    }

    // ORM LICHII_COOP_NPC_GAZE (coop_npc_gaze.h): which townspeople look at a
    // partner's puppet, from what the walk read; the calc thread writes it.
    void Gaze(bool event, u64 now_us, const std::vector<coop::host::NpcSeen>& npcs,
              std::optional<std::uint64_t> partner, u8 self, const std::map<u8, u8>& statuses) {
        const std::uint32_t live = CoopPuppet::LivePuppets();
        const auto puppet_live = [live](u8 p) { return p != 0 && p < 32 && ((live >> p) & 1u) != 0; };
        std::vector<CoopNpcGaze::Partner> partners;
        for (u8 p = 1; p < 32; ++p) {
            if (p != self && puppet_live(p)) {
                if (const auto at = sync_.PlaceOf(p, now_us)) {
                    partners.push_back({p, *at});
                }
            }
        }
        std::vector<CoopNpcGaze::Npc> list;
        for (const auto& n : npcs) {
            CoopNpcGaze::Npc g;
            g.actor = n.address;
            g.name = n.name;
            g.at = n.position;
            // During our own event (a talk, a cutscene) every look stays the game's.
            g.ours_talk = event || (partner && *partner == n.address);
            if (const auto id = sync_.IdOf(n.address)) {
                const u8 owner = sync_.OwnerOf(*id);
                if (owner != self && puppet_live(owner)) {
                    coop::host::NpcOwner o;
                    o.player = owner;
                    const auto st = statuses.find(owner);
                    o.in_event = st != statuses.end() && coop::host::in_cutscene(st->second);
                    o.link = sync_.PlaceOf(owner, now_us);
                    if (npc_pose_) {
                        for (const CoopNpcPose::Heard& h : CoopNpcPose::TheNpcPose().Fresh(MonotonicMs())) {
                            o.posing = o.posing || (h.id == *id && h.player == owner);
                        }
                    }
                    if (coop::host::owner_talks(o, n.position, n.name)) {
                        g.talked_by = owner;
                    }
                }
            }
            list.push_back(std::move(g));
        }
        std::vector<std::string> lines;
        std::vector<CoopNpcGaze::Order> orders = npc_gaze_rule_.Plan(list, player_, partners, now_us, lines);
        const std::size_t turned = orders.size();
        u64 link = 0;
        for (const auto& [a, known] : names_) {
            if (known.first == "Player_Child" || known.first == "Player_Adult") {
                link = a;
                break;
            }
        }
        auto& writer = CoopNpcGaze::TheWriter();
        writer.SetOrders(std::move(orders), link);
        for (const auto& line : lines) {
            LOG_INFO(Core, "coop: {}", line);
        }
        for (const auto& line : writer.TakeLog()) {
            LOG_INFO(Core, "coop: {}", line);
        }
        if (now_us >= npc_gaze_log_us_ + 5'000'000 || now_us < npc_gaze_log_us_) {
            npc_gaze_log_us_ = now_us;
            const CoopNpcGaze::Counts c = writer.TakeCounts();
            if (turned != 0 || c.written != 0 || c.overwritten != 0 || c.given_back != 0) {
                LOG_INFO(Core,
                         "coop: npc gaze: {} turned to a puppet; slot-frames written {}, kept {}, set again "
                         "by the game {}, given back {}, no slot {}, ambiguous {}{}",
                         turned, c.written, c.kept, c.overwritten, c.given_back, c.no_slot, c.ambiguous,
                         writer.fell_back() ? " (fallen back: nothing written)" : "");
            }
        }
    }

    // ORM step 7, every frame: our copies that follow an owner's OBJECT_POSE,
    // our own townsperson's pose out, and a line every 5 s.
    void NpcPoseFrame(coop::host::Session& session, u64 server_now_us) {
        auto& npc_pose = CoopNpcPose::TheNpcPose();
        const u64 now_ms = MonotonicMs();
        std::vector<CoopNpcPose::Target> targets;
        std::vector<std::string> follows;
        for (const CoopNpcPose::Heard& h : npc_pose.Fresh(now_ms)) {
            if (const auto actor = sync_.CopyFor(h.id, h.player, h.epoch)) {
                // Just let go by our talk: blended back, not snapped (Sync::SetTalk).
                targets.push_back({h.id, *actor,
                                   sync_.RejoinWeight(*actor, server_now_us).value_or(1.0f)});
                follows.push_back(fmt::format("P{} {:#x}", h.player, *actor));
            }
        }
        if (follows != npc_pose_targets_logged_) {
            if (follows.empty()) {
                LOG_INFO(Core, "coop: npc pose: no copy follows a pose now");
            } else {
                std::string list;
                for (const auto& f : follows) {
                    list += (list.empty() ? "" : ", ") + f;
                }
                LOG_INFO(Core, "coop: npc pose: copies following their owner's pose: {}", list);
            }
            npc_pose_targets_logged_ = follows;
        }
        npc_pose.SetTargets(std::move(targets));
        // ORM (coop_interest.h): the townsperson's pose goes at its native
        // rate while another player is in our place (nobody else sees it).
        // A townsperson of many bones goes in parts (coop_npc_pose.h), in order.
        if (CoopInterest::The().AnyoneHere(CoopEvery::NowUs() / 1000)) {
            if (auto parts = npc_pose.TakeOutgoing(server_now_us, now_ms); !parts.empty()) {
                session.with_client([&](coop::net::Client& c) {
                    for (const auto& m : parts) c.send(m);
                });
            }
        }
        if (now_ms - npc_pose_log_ms_ >= 5000) {
            npc_pose_log_ms_ = now_ms;
            const CoopNpcPose::Counts n = npc_pose.TakeCounts();
            const std::size_t partials = TheMover().TakePartials();
            if (n.read + n.not_read + n.sent + n.received + n.stale + n.incomplete + n.bad +
                    n.written + n.other_skeleton + n.no_skeleton + n.gone + partials >
                0) {
                LOG_INFO(Core,
                         "coop: npc pose: 5 s: {} read ({} bones), {} not read, {} sent ({} "
                         "parts, {} bones), {} received ({} stale, {} incomplete, {} bad), {} "
                         "written, {} other skeleton, {} no skeleton, {} gone; last write kept "
                         "{}, overwritten {}; {} partial slot plays",
                         n.read, n.bones, n.not_read, n.sent, n.sent_parts, n.sent_bones,
                         n.received, n.stale, n.incomplete, n.bad, n.written, n.other_skeleton,
                         n.no_skeleton, n.gone, n.kept, n.overwritten, partials);
            }
        }
    }

    static u64 MonotonicMs() {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
    }

    // ORM D-R12: the walk's actor list for the talk flag deferral
    // (coop/host/flag_defer.hpp, WalkActors). Only copies what the walk read.
    void Walked(const std::string& scene, const std::vector<Seen>& seen, u8 self) {
        walk_actors_.clear();
        std::unordered_map<u64, std::size_t> at;
        for (const auto& [address, known] : names_) {
            at.emplace(address, walk_actors_.size());
            walk_actors_.push_back({known.first, std::nullopt, false});
        }
        for (const Seen& s : seen) {
            const auto it = at.find(s.address);
            if (it == at.end()) {
                continue;
            }
            auto& a = walk_actors_[it->second];
            a.position = s.position;
            const auto id = sync_.IdOf(s.address);
            const u8 owner = id ? sync_.OwnerOf(*id) : 0;
            a.copy = owner != 0 && owner != self;
        }
        for (const auto& [address, w] : watched_) {  // ORM: RIESGOS_RESUELTOS.md
            const auto it = at.find(address);
            if (it == at.end()) {
                continue;
            }
            auto& seen_actor = walk_actors_[it->second];
            if (!seen_actor.position) {
                seen_actor.position = w.position;
            }
            seen_actor.animation = w.animation;
        }
        walk_scene_ = scene;
    }

    // ORM held switches (coop_held_switch.h): the actor of a placement hash
    // in the last walk, when that walk was in `scene`. The walk runs every
    // two frames and keeps its old list when one fails (the list changing
    // under it, as when a room unloads), so the address is read again: it is
    // given only while its placement still has this hash (a freed actor, or
    // another actor in its place, is not written into).
    std::optional<u64> LoadedActor(u64 hash, const std::optional<std::string>& scene) const {
        if (hash == 0 || !scene || scene->empty() || walk_scene_ != *scene || memory_ == nullptr) {
            return std::nullopt;
        }
        for (const auto& [address, known] : names_) {
            if (known.second == hash) {
                Reader r(*memory_);
                const auto placement = r.Ptr(address + kActorPlacement);
                if (!placement || r.Get<u64>(*placement + kPlacementHash).value_or(0) != hash) {
                    return std::nullopt;
                }
                return address;
            }
        }
        return std::nullopt;
    }

    std::optional<std::vector<coop::host::SeenActor>> WalkActors(
        const std::optional<std::string>& scene) const {
        if (!scene || scene->empty() || walk_scene_ != *scene) {
            return std::nullopt;
        }
        return walk_actors_;
    }

    // Network thread.
    void ForgetOwnerLog() {  // network thread
        owner_log_.Clear();
        sync_.ForgetOwners();  // the server's epochs may start at 0 again (issue 133)
    }

    void Take(std::uint64_t key) { sync_.Take(key); }  // any thread (Sync::Take locks)

    void Receive(const coop::protocol::Message& m) {
        if (const auto* o = std::get_if<coop::protocol::ObjectOwner>(&m)) {
            if (owner_log_.Changed(*o)) {  // owner, epoch or group changed
                LOG_INFO(Core, "coop: objects: share owner {:#x} is player {} epoch {} group {}",
                         o->id.value, o->owner, o->epoch, o->group);
            }
        } else if (const auto* d = std::get_if<coop::protocol::Damage>(&m)) {
            LOG_INFO(Core, "coop: objects: share got damage {} for {:#x}", d->amount,
                     d->id.value);
        } else if (const auto* g = std::get_if<coop::protocol::GrassCut>(&m)) {
            LOG_INFO(Core, "coop: grass: got cut from P{} in {} at {:.2f} {:.2f} {:.2f} radius {:.2f}",
                     g->player_number, g->scene, g->position[0], g->position[1], g->position[2],
                     g->radius);
            TheGrass().Receive(*g);
            return;
        } else if (const auto* sh = std::get_if<coop::protocol::WorldHit>(&m);
                   sh != nullptr && scrubs_on_ && CoopScrubs::FindBrother(sh->hash) != nullptr) {
            ScrubsBox& box = TheScrubs();  // issue 149: a Scrub Brother hit in another game
            std::lock_guard lock(box.mutex);
            box.puzzle.Receive(*sh, CoopEvery::NowUs());
            for (const auto& line : box.puzzle.TakeNotes()) {
                LOG_INFO(Core, "coop: {}", line);
            }
            return;
        } else if (std::holds_alternative<coop::protocol::WorldBreak>(m) ||
                   std::holds_alternative<coop::protocol::WorldPlace>(m) ||
                   std::holds_alternative<coop::protocol::WorldHit>(m) ||
                   std::holds_alternative<coop::protocol::SignCut>(m) ||
                   std::holds_alternative<coop::protocol::WorldMove>(m)) {
            if (const auto* b = std::get_if<coop::protocol::WorldBreak>(&m)) {
                LOG_INFO(Core, "coop: world: got break {:#x} from P{} in {}{}", b->hash,
                         b->player_number, b->scene,
                         (b->flags & coop::protocol::world_break::restored) != 0 ? " (back)" : "");
            } else if (const auto* p = std::get_if<coop::protocol::WorldPlace>(&m)) {
                LOG_INFO(Core, "coop: world: got place {:#x} from P{} at {:.2f} {:.2f} {:.2f}",
                         p->hash, p->player_number, p->position[0], p->position[1], p->position[2]);
            }
            world_.Receive(m);
            return;
        } else if (const auto* pc = std::get_if<coop::protocol::PlatformClock>(&m)) {
            std::lock_guard lock(platforms_mutex_);  // taken on the present thread (Platforms)
            if (platforms_in_.size() < 64) {
                platforms_in_.push_back(*pc);
            }
            return;
        } else if (const auto* pk = std::get_if<coop::protocol::PlatformCounter>(&m)) {
            std::lock_guard lock(platforms_mutex_);
            if (counters_in_.size() < 64) {
                counters_in_.push_back(*pk);
            }
            return;
        }        sync_.Receive(m);
    }

    bool debug_ = false;
    u64 grass_made_logged_ = 0;
    CoopWorld::WorldSync world_;
    CoopWorld::GimmickProbe gimmick_probe_;  // LICHII_COOP_GIMMICK_PROBE=1 turns it on
    // Moving platforms (coop_platforms.h, issues 196 and 194).
    // LICHII_COOP_PLATFORMS: on unless 0; "probe" finds and sends, never writes.
    int platforms_mode_ = 1;  // 0 off, 1 on, 2 probe
    CoopPlatforms::PlatformSync platforms_;
    std::vector<CoopPlatforms::PlatformSync::Driver> platform_drivers_;
    std::mutex platforms_mutex_;
    std::vector<coop::protocol::PlatformClock> platforms_in_;  // under platforms_mutex_
    std::vector<coop::protocol::PlatformCounter> counters_in_;  // under platforms_mutex_
    CoopCounters::CounterSync counters_;
    std::set<u64> counter_nodes_;  // the platform nodes the ring named, while their actor is here
    CoopCounters::OtherNodes counter_others_;  // nodes of other actors (not Obj_), not read every look
    std::string counter_scene_;
    u32 ring_count_seen_ = 0;      // the ring's count at the last look
    u64 ring_starts_ = 0;          // counter starts since the last line
    u64 ring_overruns_ = 0;        // looks that found more starts than the ring holds
    u64 counter_said_us_ = 0;
    int counter_lines_ = 0;
    std::size_t platform_lines_ = 0;
    std::vector<CoopWorld::GimmickProbe::Gimmick> gimmicks_;
    // ORM (docs/RIESGOS_RESUELTOS.md): the water planes (water.hpp, live or at
    // load) and the door actors a held switch opens (held_switch.hpp,
    // HeldDoorCheck) of the last walk, by actor, with their place and AS
    // command; given to WalkActors.
    struct Watched {
        coop::protocol::Vec3 position{};
        std::optional<std::string> animation;
    };
    std::unordered_map<u64, Watched> watched_;
    bool gimmick_probe_on_ = false;
    bool scrubs_on_ = true;  // LICHII_COOP_SCRUBS=0 turns it off (issue 149)
    std::vector<CoopScrubs::Seen> scrubs_seen_;
    bool ocarina_probe_on_ = false;           // issue 152: LICHII_COOP_OCARINA_PROBE=1
    CoopOcarina::OcarinaProbe ocarina_probe_;
    CoopOcarina::HoldProbe ocarina_hold_probe_;
    u64 our_link_ = 0;                        // our Link's actor, from the last walk
    std::vector<CoopWorld::WorldSync::Out::Word> sign_words_;  // SIGN_CUT words to write
    std::vector<CoopWorld::WorldSync::Out::BlockFix> block_fixes_;  // issue 147: blocks to fix
    std::optional<coop::protocol::Vec3> player_;  // our Link in the last walk
    std::vector<coop::host::RidePlatform> ride_platforms_;  // ORM: the moving floors of the last walk
    // ORM boss families (LICHII_ORM_BOSS_FAMILIES, on): the walk's unshared actors of the table.
    bool families_ = true;
    std::vector<std::pair<std::string, coop::protocol::Vec3>> family_extras_;
    // Issue 144: shared changes to actors near our Link wait while it is in an
    // event (coop_guard.h). Where each actor of the last walk was, kills and
    // hurts held back, and what was said about it.
    std::unordered_map<u64, std::pair<coop::protocol::Vec3, u64>> where_;  // place, who (hash and name)
    CoopGuard::Held<LifeWrite> held_writes_{
        [](LifeWrite& kept, LifeWrite&& newer) { CoopGuard::MergeLife(kept, std::move(newer)); }};
    std::string guard_scene_;
    bool guard_said_ = false;
    CoopGuard::Local GuardLocal() const { return {TheLocalEvent().load(), player_}; }
    std::optional<coop::protocol::Vec3> Where(u64 address) const {
        const auto it = where_.find(address);
        if (it == where_.end()) {
            return std::nullopt;
        }
        return it->second.first;
    }
    std::optional<u64> HashAt(u64 address) const {
        const auto it = where_.find(address);
        if (it == where_.end()) {
            return std::nullopt;
        }
        return it->second.second;
    }
    int world_lines_ = 0;
    CoopWorld::FoliageProbe foliage_;
    // Issue 88 (docs/items.md, "Heart containers").
    s32 link_life_logged_ = -1;
    s32 link_max_logged_ = -1;
    s32 flag_max_logged_ = -1;
    bool hearts_to_link_ = true;  // LICHII_COOP_HEARTS_TO_LINK, on unless =0 (issue 88)
    // PvP hurts being given to our Link (GivePvp).
    struct PvpGive {
        s32 take = 0;
        s32 expect = 0;  // the life after it
        u64 asked_ms = 0;
        bool replayed = false;
    };
    std::vector<PvpGive> pvp_;
    bool pvp_replay_ = true;  // LICHII_COOP_PVP_REPLAY=0: write the life only
    Core::Memory::Memory* memory_ = nullptr;  // set by Share for Collect
    bool cheat_ = false;
    bool share_ = false;

    // Present thread: the shared things this game owns (for coop_drops).
    auto OwnedNow() const { return sync_.Owned(); }
    bool pose_ = true;
    bool hurt_ = true;
    bool replay_ = false;
    bool wake_ = true;
    bool release_ = true;  // LICHII_COOP_OBJECTS_RELEASE=0 turns it off (issues 150, 157)
    bool wake_far_ = true;  // LICHII_COOP_OBJECTS_ASLEEP=0 turns it off (issue 101)
    std::size_t asleep_placed_ = 0;  // copy-frames placed in state 6 since the last log
    // ORM golpes (coop_hits.h, coop_copy_smooth.h, oot-remake docs/GOLPES_LATENCIA.md).
    bool hit_feedback_ = true;  // LICHII_COOP_HITS=0 turns it off
    bool hit_dedupe_ = false;   // LICHII_COOP_HITS_DEDUPE=1 turns it on
    bool copy_smooth_ = true;   // LICHII_COOP_COPY_SMOOTH=0 turns it off
    bool npc_ = false;  // LICHII_COOP_NPC (ORM D-R12)
    // ORM "Levantar" (coop_carry.h).
    CoopCarry::Lifter lifter_;
    std::set<std::uint64_t> carried_by_others_;  // this walk's copies in another's puppet's hands
    bool carry_ = true;       // LICHII_COOP_CARRY
    bool carry_bone_ = true;  // LICHII_COOP_CARRY_BONE
    bool carry_still_ = true;  // LICHII_COOP_CARRY_STILL (levantar-vasija)
    std::optional<std::array<float, 9>> link_rotation_;  // our Link at the last walk
    std::optional<std::string> link_command_;           // his AS slot 0
    u64 carry_ours_logged_ = 0;
    // The copies in another player's hands at the last frame: (holder, fell).
    std::map<u64, std::pair<u8, bool>> carried_logged_;
    coop::host::NpcTalk npc_talk_;
    std::vector<Seen> stand_ins_;  // ORM charla-radio: Npc_Invisible_<X> of the last walk
    std::set<std::uint64_t> npc_checked_;  // ORM charla-radio: check line logged this talk
    coop::host::NpcClaim npc_claim_;  // ORM: two players talking to the same townsperson
    coop::host::NpcClaim::State npc_claim_logged_ = coop::host::NpcClaim::State::none;
    std::vector<std::uint64_t> npc_rejoin_logged_;
    std::string npc_scene_;
    std::vector<coop::host::SeenActor> walk_actors_;  // ORM D-R12: the last walk (Walked)
    std::string walk_scene_;
    std::optional<u64> npc_logged_;
    bool npc_pose_ = false;  // LICHII_COOP_NPC_POSE (ORM steps 5 to 7)
    std::optional<CoopNpcPose::Source> npc_pose_source_logged_;
    std::vector<std::string> npc_pose_targets_logged_;
    u64 npc_pose_log_ms_ = 0;
    bool npc_gaze_ = false;  // LICHII_COOP_NPC_GAZE (ORM, coop_npc_gaze.h)
    CoopNpcGaze::Rule npc_gaze_rule_;
    u64 npc_gaze_log_us_ = 0;
    bool drive_ = false;
    int ai_hold_ = 0;  // 0 off, 1 set the brain skip bit on copies, 2 clear it
    u64 test_take_ = 0;
    u64 take_at_ = 0;
    int drive_mode_ = 0;

private:
    // The game is the right one; finds main once per process.
    bool Ready(Core::System& system) {
        auto* process = system.ApplicationProcess();
        if (process == nullptr || system.GetApplicationProcessProgramID() != kOcarinaProgramId) {
            return false;
        }
        const auto& build = system.GetApplicationProcessBuildID();
        if (std::memcmp(build.data(), kGameBuildId.data(), kGameBuildId.size()) != 0) {
            return false;
        }
        if (process != process_) {
            process_ = process;
            main_ = FindMain(system, process);
            actors_.clear();
            first_ = true;
            names_.clear();
            sync_.Reset();
            world_.Reset();
            {
                ScrubsBox& box = TheScrubs();  // a new game run: its block is gone
                std::lock_guard lock(box.mutex);
                box.puzzle = CoopScrubs::Puzzle(ScrubsBox::HitAnimations());
                box.main = 0;
            }
            LOG_INFO(Core, "coop: objects: main at {:#x}", main_);
        }
        return true;
    }

    // The shared actors of one whole walk, with life and place. False when
    // the list changed under us (try the next frame).
    // Moving platforms (coop_platforms.h): our drivers' clocks out, the
    // owners' clocks into ours.
    void Platforms(Reader& r, const std::string& scene, u8 self, u64 server_now_us,
                   std::vector<coop::protocol::Message>& send) {
        {
            std::vector<coop::protocol::PlatformClock> in;
            {
                std::lock_guard lock(platforms_mutex_);
                in.swap(platforms_in_);
            }
            for (const auto& m : in) {
                platforms_.Receive(m);
            }
        }
        // Looping platforms' pause counters first (coop_counters.h): the
        // clock-word path below leaves their actors alone.
        Counters(r, scene, self, server_now_us, send);
        if (const auto& looping = counters_.CounterOwners(); !looping.empty()) {
            std::erase_if(platform_drivers_, [&](const auto& d) { return looping.contains(d.hash); });
        }
        const auto read = [&r](const CoopPlatforms::PlatformSync::Driver& d) {
            auto words = CoopWorld::SignProbe::Read([&r](u64 a) { return r.Get<u64>(a); }, d.address);
            if (!words.empty()) {
                if (const u64 slot = AsSlot0(r, d.address); slot != 0) {
                    if (const auto frame = r.Get<u32>(slot + kAsSlotFrame)) {
                        words[CoopPlatforms::Key(CoopPlatforms::kRegionAsFrame, 0)] = *frame;
                    }
                }
            }
            return words;
        };
        CoopPlatforms::PlatformSync::Out out;
        platforms_.Tick(scene, self, CoopEvery::NowUs(), server_now_us, platform_drivers_, player_, read,
                        platforms_mode_ == 2, out);
        for (auto& m : out.send) {
            send.emplace_back(std::move(m));
        }
        for (const auto& w : out.writes) {
            const u64 at = PlatformWordAt(r, w.address, w.key);
            if (at == 0 || memory_ == nullptr || !memory_->IsValidVirtualAddressRange(at, 4)) {
                continue;
            }
            u32 bits = 0;
            std::memcpy(&bits, &w.value, 4);
            memory_->Write32(at, bits);
            if (platform_lines_ < 200) {
                ++platform_lines_;
                LOG_INFO(Core, "coop: platforms: {:016x} {} {:.3f} -> {:.3f}", w.hash,
                         CoopPlatforms::Where(w.key), w.was, w.value);
            }
        }
        for (const auto& line : out.notes) {
            LOG_INFO(Core, "coop: platforms: {}", line);
        }
    }

    // The pause counters (coop_counters.h): the nodes the cave kept, read.
    void Counters(Reader& r, const std::string& scene, u8 self, u64 server_now_us,
                  std::vector<coop::protocol::Message>& send) {
        u64 ring = 0;
        {
            CounterRingBox& box = TheCounterRing();
            std::lock_guard lock(box.mutex);
            if (box.main != main_) {
                // A new game boot in this process: a new ring for its heap (review).
                box.ring = 0;
                box.asked = false;
                box.allocating = false;
                counter_nodes_.clear();
                counter_others_.Clear();
                ring_count_seen_ = 0;
            }
            box.main = main_;
            ring = box.ring;
        }
        {
            std::vector<coop::protocol::PlatformCounter> in;
            {
                std::lock_guard lock(platforms_mutex_);
                in.swap(counters_in_);
            }
            for (const auto& m : in) {
                counters_.Receive(m);
            }
        }
        if (scene != counter_scene_) {
            counter_scene_ = scene;  // a new scene: its actors are new
            counter_nodes_.clear();
            counter_others_.Clear();
        }
        counter_others_.Keep([this](std::uint64_t actor) { return names_.contains(actor); });
        const auto actor_of = [&](u64 node, u64* graph_out) -> u64 {
            const u64 vtable = r.Get<u64>(node).value_or(0);
            const u64 graph = vtable == main_ + CoopCounters::kNodeVtable
                                  ? r.Get<u64>(node + CoopCounters::kNodeGraph).value_or(0)
                                  : 0;
            const u64 mid = graph ? r.Get<u64>(graph + CoopCounters::kGraphOwner).value_or(0) : 0;
            if (graph_out != nullptr) {
                *graph_out = graph;
            }
            return mid ? r.Get<u64>(mid + CoopCounters::kOwnerActor).value_or(0) : 0;
        };
        std::size_t named_new = 0;
        if (ring != 0) {
            std::array<u64, CoopCounters::kRingNodes> entries{};
            for (std::size_t i = 0; i < entries.size(); ++i) {
                entries[i] = r.Get<u64>(ring + 8 + 8 * i).value_or(0);
            }
            // Starts since the last look: more than the ring holds and some were lost.
            const u32 count = r.Get<u32>(ring).value_or(0);
            const u32 since = count - ring_count_seen_;
            ring_count_seen_ = count;
            ring_starts_ += since;
            if (since > CoopCounters::kRingNodes) {
                ++ring_overruns_;
            }
            for (const u64 node : CoopCounters::RingNodes(entries)) {
                if (counter_nodes_.size() < 512 && !counter_nodes_.contains(node) &&
                    !(counter_others_.Contains(node) && counter_others_.Skip(node, actor_of(node, nullptr)))) {
                    counter_nodes_.insert(node);
                    ++named_new;
                }
            }
        }
        std::vector<CoopCounters::Seen> seen;
        for (auto it = counter_nodes_.begin(); it != counter_nodes_.end();) {
            const u64 node = *it;
            u64 graph = 0;
            const u64 actor = actor_of(node, &graph);
            const auto known = actor ? names_.find(actor) : names_.end();
            if (known == names_.end()) {
                it = counter_nodes_.erase(it);  // gone, reused, or its actor left the walk
                continue;
            }
            if (!CoopCounters::IsPlatformOwner(known->second.first)) {
                // An enemy's, an NPC's, Link's: not read again while it is
                // that actor's (CoopCounters::OtherNodes).
                counter_others_.Add(node, actor);
                it = counter_nodes_.erase(it);
                continue;
            }
            ++it;
            CoopCounters::Seen s;
            s.node = node;
            s.owner_name = known->second.first;
            s.owner_hash = known->second.second;
            s.remaining = r.Get<float>(node + CoopCounters::kNodeRemaining).value_or(0.0f);
            s.duration = r.Get<float>(node + CoopCounters::kNodeDuration).value_or(0.0f);
            s.key = CoopCounters::KeyOf(s.owner_hash, s.duration, node, graph);
            seen.push_back(std::move(s));
        }
        // What the ring caught, every 10 s while a platform counter is near
        // (the 2026-11-12 run had no counter line at all; this says why).
        if (ring != 0 && counter_lines_ < 60 &&
            (counter_said_us_ == 0 || server_now_us >= counter_said_us_ + 10'000'000)) {
            counter_said_us_ = server_now_us;
            ++counter_lines_;
            std::string platforms;
            for (std::size_t i = 0; i < seen.size() && i < 6; ++i) {
                platforms += fmt::format(" {} {:.1f}/{:.1f}", seen[i].owner_name, seen[i].remaining,
                                         seen[i].duration);
            }
            LOG_INFO(Core,
                     "coop: platforms: pause counters: {} starts in 10 s ({} looks overran the ring), "
                     "{} new nodes now, {} platform nodes, {} other actors' nodes, {} looping:{}",
                     ring_starts_, ring_overruns_, named_new, seen.size(), counter_others_.Size(),
                     counters_.LoopingOwners().size(), platforms);
            ring_starts_ = 0;
            ring_overruns_ = 0;
        }
        CoopCounters::CounterSync::Out out;
        counters_.Tick(scene, self, server_now_us, seen, platforms_mode_ == 2, out);
        for (auto& m : out.send) {
            send.emplace_back(std::move(m));
        }
        for (const auto& w : out.writes) {
            if (platforms_mode_ == 2 || memory_ == nullptr ||
                !memory_->IsValidVirtualAddressRange(w.node + CoopCounters::kNodeRemaining, 4)) {
                continue;  // LICHII_COOP_PLATFORMS=probe: logged, not written
            }
            u32 bits = 0;
            std::memcpy(&bits, &w.remaining, 4);
            memory_->Write32(w.node + CoopCounters::kNodeRemaining, bits);
        }
        for (const auto& line : out.notes) {
            LOG_INFO(Core, "coop: platforms: {}", line);
        }
    }

    // The actor's AS slot 0, or 0 (the chain Animation reads).
    static u64 AsSlot0(Reader& r, u64 actor) {
        const u64 table = r.Get<u64>(actor + kActorComponents).value_or(0);
        const u64 as = table ? r.Get<u64>(table + kComponentsAs).value_or(0) : 0;
        const u64 inner = as ? r.Get<u64>(as + kAsInner).value_or(0) : 0;
        const u64 controller = inner ? r.Get<u64>(inner + kAsController).value_or(0) : 0;
        const u64 array = controller ? r.Get<u64>(controller + kAsSlotArray).value_or(0) : 0;
        return array ? r.Get<u64>(array).value_or(0) : 0;
    }

    // Where a PLATFORM_CLOCK key is in this actor, or 0.
    static u64 PlatformWordAt(Reader& r, u64 actor, std::uint64_t key) {
        const std::uint64_t region = key >> 32;
        const std::uint64_t offset = key & 0xffffffffu;
        if (region == CoopPlatforms::kRegionAsFrame) {
            const u64 slot = AsSlot0(r, actor);
            return slot ? slot + kAsSlotFrame : 0;
        }
        if (region == CoopPlatforms::kRegionActor) {
            return offset < coop::protocol::kPlatformActorBytes ? actor + offset : 0;
        }
        if (region < CoopPlatforms::kRegionComponent ||
            region - CoopPlatforms::kRegionComponent >= coop::protocol::kPlatformComponents ||
            offset >= coop::protocol::kPlatformComponentBytes) {
            return 0;
        }
        const u64 table = r.Get<u64>(actor + kActorComponents).value_or(0);
        const u64 c = table ? r.Get<u64>(table + 8 * (region - CoopPlatforms::kRegionComponent)).value_or(0) : 0;
        return c ? c + offset : 0;
    }

    bool Collect(Reader& r, std::vector<Seen>& out) {
        gimmicks_.clear();  // dungeon gimmicks for the probe
        platform_drivers_.clear();
        scrubs_seen_.clear();  // issue 149
        ride_platforms_.clear();  // ORM moving floors (orm_ride.h)
        family_extras_.clear();   // ORM boss families: the tennis balls of this walk
        stand_ins_.clear();
        watched_.clear();
        std::vector<u64> actors;
        if (!List(r, actors)) {
            return false;
        }
        std::optional<coop::protocol::Vec3> player;
        link_rotation_.reset();
        link_command_.reset();
        std::unordered_map<u64, std::pair<std::string, u64>> names;
        for (const u64 a : actors) {
            auto it = names_.find(a);
            std::pair<std::string, u64> known;
            if (it != names_.end()) {
                known = std::move(it->second);
            } else {
                known.first = r.Name(a);
                const auto placement = r.Ptr(a + kActorPlacement);
                known.second = placement ? r.Get<u64>(*placement + kPlacementHash).value_or(0) : 0;
                CoopPickups::Note(known.first, known.second);  // one-time pickups (docs/items.md)
                if (IsShared(known.first) && vtables_logged_.insert(known.first).second) {
                    // The set-matrix node (0x2622e50) writes +0x31c..+0x34b and
                    // then calls vtable slot 0x218 (actor, 0): what does it do?
                    const u64 vt = r.Get<u64>(a).value_or(0);
                    const u64 slot = vt ? r.Get<u64>(vt + 0x218).value_or(0) : 0;
                    LOG_INFO(Core, "coop: objects: share vtable {} main+{:#x}, slot 0x218 main+{:#x}",
                             known.first, vt - main_, slot - main_);
                }
            }
            if (known.first.starts_with("Player_")) {  // our own Link, for coop_world
                if (known.first == "Player_Child" || known.first == "Player_Adult") {
                    our_link_ = a;  // the ocarina probe (issue 152)
                }
                coop::protocol::Vec3 at{};
                for (std::size_t i = 0; i < 3; ++i) {
                    at[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                player = at;
                if (carry_) {  // ORM "Levantar": his turn and AS command, for the lift
                    link_rotation_ = r.Get<std::array<float, 9>>(a + kActorRotation);
                    if (const auto anim = Animation(r, a)) {
                        link_command_ = anim->name;
                    }
                }
                // Issue 88: the hearts the game shows come from Link's own Life
                // component; logged beside the MaxLife flag (coop_flags debug)
                // so one run shows whether a shared MaxLife reaches it.
                if (const auto value = LifeValue(r, a)) {
                    const s32 life = r.Get<s32>(value->first + 8).value_or(-1);
                    const s32 max = r.Get<s32>(value->second + 8).value_or(-1);
                    const s32 flag = TheMaxLife().load();
                    if (life >= 0 && max > 0) {  // OurLife (the HUD, PvP)
                        TheLife().life.store(life);
                        TheLife().max.store(max);
                        TheLife().at_us.store(CoopEvery::NowUs());
                    }
                    if (max != link_max_logged_ || flag != flag_max_logged_ ||
                        (debug_ && life != link_life_logged_)) {
                        link_max_logged_ = max;
                        link_life_logged_ = life;
                        flag_max_logged_ = flag;
                        LOG_INFO(Core,
                                 "coop: objects: our Link's life {} of {}, MaxLife flag {} (quarter hearts)",
                                 life, max, flag);
                    }
                    if (known.first == "Player_Child" || known.first == "Player_Adult") {
                        GivePvp(a, value->first + 8, life);  // PvP (coop_pvp_game.cpp)
                    }
                    // On by default (=0 turns it off): a MaxLife raised by
                    // another player is put into the component the hearts are
                    // drawn from, so a heart container crosses (issue 88).
                    const auto raised = hearts_to_link_ ? RaisedMaxLife(max, flag) : std::nullopt;
                    if (raised && memory_ != nullptr &&
                        memory_->IsValidVirtualAddressRange(value->second + 8, 4)) {
                        memory_->Write32(value->second + 8, static_cast<u32>(*raised));
                        LOG_INFO(Core, "coop: objects: our Link's max life {} -> {} (from MaxLife)",
                                 max, *raised);
                    }
                }
            }
            if (scrubs_on_ && (CoopScrubs::FindBrother(known.second) != nullptr ||
                               CoopScrubs::IsManager(known.second))) {
                CoopScrubs::Seen sc;  // issue 149: the Scrub Brothers and their manager
                sc.address = a;
                sc.hash = known.second;
                sc.id = r.Get<u32>(a + CoopScrubs::addr::kTransceiverId).value_or(0);
                if (const auto anim = Animation(r, a)) {
                    sc.animation = anim->name;
                }
                for (std::size_t i = 0; i < 3; ++i) {
                    sc.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                scrubs_seen_.push_back(std::move(sc));
            }
            if (platforms_mode_ != 0 && CoopPlatforms::IsDriver(known.first)) {
                CoopPlatforms::PlatformSync::Driver d;  // moving platforms (coop_platforms.h)
                d.address = a;
                d.hash = known.second;
                d.name = known.first;
                for (std::size_t i = 0; i < 3; ++i) {
                    d.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                platform_drivers_.push_back(std::move(d));
            }
            // ORM boss families (coop/host/boss_rules.hpp): an actor of the table that is no
            // shared object (a tennis boss's ball), by name and place, for the watcher.
            if (families_ && !IsShared(known.first) && coop::host::family_actor(known.first) != nullptr) {
                coop::protocol::Vec3 at{};
                for (std::size_t i = 0; i < 3; ++i) {
                    at[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                family_extras_.emplace_back(known.first, at);
            }
            // ORM (orm_ride.h): a lift, moving floor or boat, by its
            // placement hash (one the game made itself has none: skipped).
            if (known.second != 0 && known.second != ~u64{0} &&
                coop::host::is_ride_platform(known.first)) {
                coop::host::RidePlatform p;
                p.key = known.second;
                p.name = known.first;
                for (std::size_t i = 0; i < 3; ++i) {
                    p.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                ride_platforms_.push_back(std::move(p));
            }
            if (known.first.find("WaterPlane") != std::string::npos ||
                coop::host::held_door_actor(known.first)) {  // ORM: RIESGOS_RESUELTOS.md
                Watched w;
                for (std::size_t i = 0; i < 3; ++i) {
                    w.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                if (const auto anim = Animation(r, a)) {
                    w.animation = anim->name;
                }
                watched_[a] = std::move(w);
            }
            if (gimmick_probe_on_ && CoopWorld::IsGimmick(known.first)) {
                CoopWorld::GimmickProbe::Gimmick g;
                g.address = a;
                g.hash = known.second;
                g.name = known.first;
                for (std::size_t i = 0; i < 3; ++i) {
                    g.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                gimmicks_.push_back(std::move(g));
            }
            if (IsShared(known.first) || CoopWorld::IsWorldObject(known.first) ||
                CoopWorld::IsBlockAnchor(known.first) ||  // moved with its block (issue 147)
                (known.second != 0 && known.second != ~u64{0} && sync_.Taken(known.second))) {
                Seen s;
                s.address = a;
                s.name = known.first;
                s.hash = known.second;
                for (std::size_t i = 0; i < 3; ++i) {
                    s.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                if (const auto m = r.Get<std::array<float, 9>>(a + kActorRotation)) {
                    s.rotation = *m;
                }
                s.animation = Animation(r, a);
                if (IsBoss(s.name)) {
                    s.slots = OtherSlots(r, a);  // issue 150
                }
                if (const auto value = LifeValue(r, a)) {
                    const auto life = r.Get<s32>(value->first + 8);
                    const auto max = r.Get<s32>(value->second + 8);
                    if (life && max) {
                        s.life = coop::protocol::ObjectLife{*life, *max};
                    }
                }
                out.push_back(std::move(s));
            }
            // ORM charla-radio: invisible talk stand-ins (Npc_Invisible_<X>,
            // never shared) for the talk partner (coop/host/npc_talk.hpp).
            if (npc_ && known.first.starts_with("Npc_Invisible_") &&
                coop::host::stand_in_body(known.first)) {
                Seen s;
                s.address = a;
                s.name = known.first;
                for (std::size_t i = 0; i < 3; ++i) {
                    s.position[i] = r.Get<float>(a + kActorPosition + 4 * i).value_or(0.0f);
                }
                stand_ins_.push_back(std::move(s));
            }
            names.emplace(a, std::move(known));
        }
        names_ = std::move(names);
        sync_.SetPlayer(player);
        sync_.SetEvent(TheLocalEvent().load());  // issue 200: our cutscene's NPCs become ours
        sync_.SetLures(CoopProjectiles::TheLures().Get());  // issue 151: our cast lures
        player_ = player;
        if (debug_) {  // research: the field grass that is no actor (world.md)
            for (const std::string& line : foliage_.Look(
                     [&r](u64 address) { return r.Get<u64>(address); }, main_, player)) {
                LOG_INFO(Core, "coop: {}", line);
            }
        }
        return true;
    }

    // AS slot 0 of an actor: command name, frame, rate (as PlayerReader).
    // ORM (oot-remake docs/SONDAS.md, 2026-11-12): the puppet's own Ocarina stays on its belt under the
    // copied pose (check: ocarina hand fallback), while our Link's rises to his hand. Which of our bones
    // holds it, and where in that bone's frame: 1.5 s into each free play, one line. Reads only.
    void OcarinaHandProbe(Core::System& system, Reader& r, std::uint64_t now_us) {
        const auto anim = our_link_ != 0 ? Animation(r, our_link_) : std::nullopt;
        if (!anim || !CoopOcarina::PlaysFreely(anim->name)) {
            ocarina_hand_since_us_ = 0;
            ocarina_hand_said_ = false;
            OcarinaRestSnapshot(system, r, now_us);
            return;
        }
        if (ocarina_hand_since_us_ == 0) {
            ocarina_hand_since_us_ = now_us;
        }
        if (ocarina_hand_said_ || now_us - ocarina_hand_since_us_ < 1'500'000) {
            return;
        }
        ocarina_hand_said_ = true;
        namespace pa = CoopPuppet::addr;
        MemoryGuest guest(system.ApplicationMemory());
        const auto link = CoopCarry::ReadActor(guest, our_link_);
        const auto skeleton = CoopCarry::FindSkeleton(guest, our_link_);
        const u64 parts = r.Get<u64>(our_link_ + pa::kActorParts).value_or(0);
        const u64 eu = parts != 0 ? r.Get<u64>(parts + pa::kPartsEquipmentUser).value_or(0) : 0;
        if (!link || !skeleton || eu == 0) {
            LOG_INFO(Core, "coop: ocarina hand probe: our Link's skeleton or equipment not read");
            return;
        }
        for (std::uint32_t a = 0; a < pa::kArmorLinkCount; ++a) {
            const u64 holder = r.Get<u64>(eu + pa::kArmorLinks + pa::kEquipSlotStride * a + pa::kEquipSlotHolder)
                                   .value_or(0);
            const u64 piece = holder != 0 ? r.Get<u64>(holder + pa::kHolderActor).value_or(0) : 0;
            const auto name = piece != 0 ? r.Name(piece) : std::string();
            if (!name.starts_with("Item_Ocarina_") || name.ends_with("_Holder")) {
                continue;
            }
            const auto frame = CoopCarry::ReadActor(guest, piece);
            if (!frame) {
                continue;
            }
            const auto nearest = CoopCarry::NearestBone(guest, *skeleton, *link, frame->p);
            if (!nearest) {
                LOG_INFO(Core, "coop: ocarina hand probe: {} at up {:.2f}: no bone read", name,
                         frame->p[1] - link->p[1]);
                return;
            }
            const auto bone = CoopCarry::ReadBone(guest, *skeleton, *link, nearest->first);
            const CoopCarry::Frame in_bone = bone ? CoopCarry::Relative(*bone, *frame) : CoopCarry::Frame{};
            OcarinaHandDiff(guest, eu, a, holder, piece);
            BoneCensus(guest, *skeleton, *link, frame->p);
            LOG_INFO(Core,
                     "coop: ocarina hand probe: our {} (armor link {}) at up {:.2f} is {:.3f} m from bone {} "
                     "while we play '{}'; in its frame at ({:.3f}, {:.3f}, {:.3f}), rows ({:.3f} {:.3f} {:.3f}, "
                     "{:.3f} {:.3f} {:.3f}, {:.3f} {:.3f} {:.3f})",
                     name, a, frame->p[1] - link->p[1], nearest->second, nearest->first, anim->name, in_bone.p[0],
                     in_bone.p[1], in_bone.p[2], in_bone.r[0], in_bone.r[1], in_bone.r[2], in_bone.r[3],
                     in_bone.r[4], in_bone.r[5], in_bone.r[6], in_bone.r[7], in_bone.r[8]);
            return;
        }
        LOG_INFO(Core, "coop: ocarina hand probe: no Item_Ocarina_ piece among our armor links");
    }
    // ORM ocarina-mano (round 5): what moves our Ocarina from the belt to the hand. While we do not
    // play, once a second, the EquipmentUser, the Ocarina's armor link holder and its piece are read;
    // 1.5 s into a free play every word of them that changed is logged (at most kDiffLines a region),
    // so the field the puppet needs (a binding, a bone index, a state byte) shows. Reads only.
    struct RestSnap {
        u64 eu = 0, holder = 0, piece = 0;
        std::uint32_t link = 0;
        std::vector<u32> eu_words, holder_words, piece_words;
        std::uint64_t at_us = 0;
    };
    static constexpr std::size_t kEuBytes = 0x1600, kHolderBytes = 0x100, kPieceBytes = 0x400;
    static constexpr int kDiffLines = 40;
    static std::vector<u32> Words(CoopPuppet::Guest& guest, u64 at, std::size_t bytes) {
        std::vector<u32> w(bytes / 4);
        if (at == 0 || !guest.Read(at, w.data(), bytes)) {
            w.clear();
        }
        return w;
    }
    void OcarinaRestSnapshot(Core::System& system, Reader& r, std::uint64_t now_us) {
        if (our_link_ == 0 || (rest_.at_us != 0 && now_us - rest_.at_us < 1'000'000)) {
            return;
        }
        namespace pa = CoopPuppet::addr;
        const u64 parts = r.Get<u64>(our_link_ + pa::kActorParts).value_or(0);
        const u64 eu = parts != 0 ? r.Get<u64>(parts + pa::kPartsEquipmentUser).value_or(0) : 0;
        if (eu == 0) {
            return;
        }
        MemoryGuest guest(system.ApplicationMemory());
        for (std::uint32_t a = 0; a < pa::kArmorLinkCount; ++a) {
            const u64 holder = r.Get<u64>(eu + pa::kArmorLinks + pa::kEquipSlotStride * a + pa::kEquipSlotHolder)
                                   .value_or(0);
            const u64 piece = holder != 0 ? r.Get<u64>(holder + pa::kHolderActor).value_or(0) : 0;
            const auto name = piece != 0 ? r.Name(piece) : std::string();
            if (!name.starts_with("Item_Ocarina_") || name.ends_with("_Holder")) {
                continue;
            }
            rest_.eu = eu;
            rest_.holder = holder;
            rest_.piece = piece;
            rest_.link = a;
            rest_.eu_words = Words(guest, eu, kEuBytes);
            rest_.holder_words = Words(guest, holder, kHolderBytes);
            rest_.piece_words = Words(guest, piece, kPieceBytes);
            rest_.at_us = now_us;
            return;
        }
    }
    void OcarinaHandDiff(CoopPuppet::Guest& guest, u64 eu, std::uint32_t link, u64 holder, u64 piece) {
        if (rest_.at_us == 0 || rest_.eu != eu || rest_.holder != holder || rest_.piece != piece) {
            LOG_INFO(Core, "coop: ocarina hand probe: diff: no rest snapshot of the same pieces (link {})", link);
            return;
        }
        const auto diff = [&](const char* what, u64 base, const std::vector<u32>& before, std::size_t bytes) {
            const std::vector<u32> now = Words(guest, base, bytes);
            if (now.size() != before.size() || now.empty()) {
                LOG_INFO(Core, "coop: ocarina hand probe: diff {}: not read", what);
                return;
            }
            int lines = 0, changed = 0;
            for (std::size_t i = 0; i < now.size(); ++i) {
                if (now[i] == before[i]) {
                    continue;
                }
                ++changed;
                if (lines++ < kDiffLines) {
                    float fb = 0, fn = 0;
                    std::memcpy(&fb, &before[i], 4);
                    std::memcpy(&fn, &now[i], 4);
                    LOG_INFO(Core, "coop: ocarina hand probe: diff {}+{:#x}: {:#010x} -> {:#010x} ({:g} -> {:g})",
                             what, i * 4, before[i], now[i], fb, fn);
                }
            }
            LOG_INFO(Core, "coop: ocarina hand probe: diff {}: {} words changed of {}", what, changed, now.size());
        };
        diff("eu", eu, rest_.eu_words, kEuBytes);
        diff("holder", holder, rest_.holder_words, kHolderBytes);
        diff("piece", piece, rest_.piece_words, kPieceBytes);
    }
    // ORM levantar-vasija / ocarina-mano: NearestBone found only bone 0 both times. How many of the
    // skeleton's world matrices read as rigid frames near Link, at the stride in use and at 0x40,
    // and the three nearest to `at`. Reads only.
    static void BoneCensus(CoopPuppet::Guest& guest, const CoopCarry::Skeleton& skeleton,
                           const CoopCarry::Frame& link, const coop::protocol::Vec3& at) {
        for (int k = 0; k < 3; ++k) {
            CoopCarry::Skeleton t = skeleton;
            t.layout = k == 2 ? CoopCarry::Layout::m34 : CoopCarry::Layout::m44;
            t.columns = k == 1;
            int ok = 0;
            std::vector<std::pair<float, std::uint32_t>> near;
            for (std::uint32_t i = 0; i < t.count; ++i) {
                std::array<float, 16> m{};
                if (!guest.Read(t.world + t.Stride() * i, m.data(), t.Stride())) {
                    continue;
                }
                const auto f = CoopCarry::BoneWorld(link, CoopCarry::RowsOf(t, m.data()));
                if (!f) {
                    continue;
                }
                ++ok;
                const float dx = f->p[0] - at[0], dy = f->p[1] - at[1], dz = f->p[2] - at[2];
                near.emplace_back(std::sqrt(dx * dx + dy * dy + dz * dz), i);
            }
            std::sort(near.begin(), near.end());
            std::string best;
            for (std::size_t n = 0; n < near.size() && n < 3; ++n) {
                best += fmt::format(" {}:{:.3f}", near[n].second, near[n].first);
            }
            LOG_INFO(Core,
                     "coop: bone census: {}{}: {} of {} bones read as rigid frames near Link; nearest{}",
                     CoopCarry::NameOf(t), t.layout == skeleton.layout && t.columns == skeleton.columns
                                               ? " (the one in use)" : "",
                     ok, t.count, best.empty() ? std::string(" none") : best);
        }
    }
    RestSnap rest_;
    std::uint64_t ocarina_hand_since_us_ = 0;
    bool ocarina_hand_said_ = false;

    std::optional<coop::protocol::Animation> Animation(Reader& r, u64 a) {
        return AnimationAt(r, a, 0, false);
    }

    // The AS controller of an actor and its slot count.
    std::optional<std::pair<u64, s32>> AsController(Reader& r, u64 a) {
        const auto table = r.Ptr(a + kActorComponents);
        const auto as = table ? r.Ptr(*table + kComponentsAs) : std::nullopt;
        const auto inner = as ? r.Ptr(*as + kAsInner) : std::nullopt;
        const auto controller = inner ? r.Ptr(*inner + kAsController) : std::nullopt;
        if (!controller) {
            return std::nullopt;
        }
        const s32 count = static_cast<s32>(r.Get<u32>(*controller + kAsSlotCount).value_or(0));
        if (count < 1 || count > 64) {
            return std::nullopt;
        }
        return std::make_pair(*controller, count);
    }

    // Issue 150: a boss's AS slots after slot 0 (Gohma's eye guard and
    // eyelids), up to kMaxObjectSlots. A slot that plays nothing is sent with
    // an empty name (the copy leaves its own slot alone then).
    std::vector<coop::protocol::SlotAnimation> OtherSlots(Reader& r, u64 a) {
        std::vector<coop::protocol::SlotAnimation> out;
        const auto c = AsController(r, a);
        if (!c) {
            return out;
        }
        const s32 last = std::min<s32>(c->second - 1, static_cast<s32>(coop::protocol::kMaxObjectSlots));
        for (s32 i = 1; i <= last && i <= coop::protocol::kMaxSlotIndex; ++i) {
            if (auto anim = AnimationAt(r, a, i, true)) {
                out.push_back({static_cast<std::uint8_t>(i), std::move(*anim)});
            }
        }
        return out;
    }

    // AS slot `index` of an actor. empty_ok: a slot that plays nothing gives
    // an empty name (slot 0 gives nothing then).
    std::optional<coop::protocol::Animation> AnimationAt(Reader& r, u64 a, s32 index,
                                                         bool empty_ok) {
        const auto c = AsController(r, a);
        if (!c || index >= c->second) {
            return std::nullopt;
        }
        const auto array = r.Ptr(c->first + kAsSlotArray);
        const auto slot = array ? r.Ptr(*array + kAsSlotEntry * static_cast<u64>(index)) : std::nullopt;
        const auto name_at = slot ? r.Ptr(*slot + kAsSlotName) : std::nullopt;
        if (!name_at) {
            return std::nullopt;
        }
        const auto chunk = r.Get<std::array<char, 64>>(*name_at);
        if (!chunk) {
            return std::nullopt;
        }
        const std::size_t n = strnlen(chunk->data(), chunk->size());
        if (n == 0 && empty_ok) {
            return coop::protocol::Animation{std::string(), 0.0f, 0.0f};
        }
        if (n == 0 || n >= chunk->size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if ((*chunk)[i] < 0x20 || (*chunk)[i] > 0x7e) {
                return std::nullopt;
            }
        }
        const float frame = r.Get<float>(*slot + kAsSlotFrame).value_or(0.0f);
        const float rate = r.Get<float>(*slot + kAsSlotRate).value_or(1.0f);
        if (!std::isfinite(frame) || !std::isfinite(rate) || std::fabs(frame) > 1e5f ||
            std::fabs(rate) > 100.0f) {
            return std::nullopt;
        }
        return coop::protocol::Animation{std::string(chunk->data(), n), frame, rate};
    }

    // The life and max value entries ([life + 0x1c10], [life + 0x1c18]).
    std::optional<std::pair<u64, u64>> LifeValue(Reader& r, u64 a) {
        const auto table = r.Ptr(a + kActorComponents);
        const auto life = table ? r.Ptr(*table + kComponentLife) : std::nullopt;
        const auto value = life ? r.Ptr(*life + kLifeValue) : std::nullopt;
        const auto max = life ? r.Ptr(*life + kLifeMax) : std::nullopt;
        if (!value || !max) {
            return std::nullopt;
        }
        return std::pair{*value, *max};
    }

    // PvP: our Link's life `life` (at `life_at`) was just read. Notes it for
    // OurLinkLife, and gives the hurts the victim's rules decided: first as
    // the game's own hit (the hit replay, as an enemy copy gets one: Link
    // flinches and is knocked away from the attacker), then, if the game has
    // not taken it within kPvpHitWaitMs, by writing the life directly (as
    // docs/objects.md "Damage" found enough for any actor).
    void GivePvp(u64 link, u64 life_at, s32 life) {
        const u64 now = SteadyNowMs();
        TheLinkLife().store(life);
        TheLinkLifeAt().store(now == 0 ? 1 : now);
        if (life < 0) {
            return;  // not read: nothing is given until it is
        }
        std::vector<PvpAsk> asks;
        {
            std::lock_guard lock(PvpMutex());
            asks.swap(PvpAsks());
        }
        for (const PvpAsk& ask : asks) {
            PvpGive h;
            h.take = ask.take;
            h.expect = std::max<s32>(0, life - ask.take);
            h.asked_ms = now;
            if (replay_ && pvp_replay_) {
                Mover::Hit hit;
                hit.actor = link;
                hit.attacker = link;  // the attacker's puppet is another game's; our Link stands in
                hit.amount = ask.take;
                hit.at = ask.at;
                hit.from = ask.from;
                hit.exact = true;
                TheMover().AddHit(hit);
                h.replayed = true;
            }
            LOG_INFO(Core, "coop: pvp: our Link hurt by {} at life {} ({})", ask.take, life,
                     h.replayed ? "the game's hit" : "written");
            pvp_.push_back(h);
        }
        for (auto it = pvp_.begin(); it != pvp_.end();) {
            if (life <= it->expect) {
                if (it->replayed) {
                    LOG_INFO(Core, "coop: pvp: the game took the hit (life {})", life);
                }
                it = pvp_.erase(it);
                continue;
            }
            if (!it->replayed || now >= it->asked_ms + kPvpHitWaitMs) {
                if (memory_ != nullptr && memory_->IsValidVirtualAddressRange(life_at, 4)) {
                    memory_->Write32(life_at, static_cast<u32>(it->expect));
                    LOG_INFO(Core, "coop: pvp: our Link's life {} -> {}{}", life, it->expect,
                             it->replayed ? " (the game's hit was not taken)" : "");
                    life = it->expect;
                }
                it = pvp_.erase(it);
                continue;
            }
            ++it;
        }
    }

    // A direct write of life is enough for the game (docs/objects.md, "Damage").
    // A hurt is added to the damage the Life step takes off this frame.
    // The test script's kill steps (coop_testinput, test only): the nearest
    // actor of that name to our Link with a life takes a hit from our Link of
    // its whole life, through the game's damage, so it dies as from a real hit
    // (and drops what it drops: a Gold Skulltula its token).
    void TestKills(const std::vector<Seen>& seen, std::vector<LifeWrite>& writes) {
        std::vector<std::string> names;
        {
            std::lock_guard lock(KeyRefundMutex());
            names.swap(TestKillQueue());
        }
        for (const std::string& name : names) {
            const Seen* best = nullptr;
            float best_m = 0.0f;
            for (const Seen& s : seen) {
                if (s.name != name || !s.life || s.life->life <= 0) {
                    continue;
                }
                float m = 0.0f;
                if (player_) {
                    const float dx = s.position[0] - (*player_)[0];
                    const float dy = s.position[1] - (*player_)[1];
                    const float dz = s.position[2] - (*player_)[2];
                    m = std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                if (!best || m < best_m) {
                    best = &s;
                    best_m = m;
                }
            }
            if (!best) {
                LOG_INFO(Core, "coop: test kill {}: none here with a life", name);
                continue;
            }
            LifeWrite w;
            w.address = best->address;
            w.life = 0;
            w.hurt = std::max<std::int32_t>(best->life->life, 1);
            w.from = player_;
            writes.push_back(w);
            LOG_INFO(Core, "coop: test kill {} {:#x} at {:.2f} {:.2f} {:.2f} ({:.1f} m, life {}): hit",
                     name, best->address, best->position[0], best->position[1], best->position[2],
                     best_m, best->life->life);
        }
    }

    void WriteLife(Core::Memory::Memory& memory, Reader& r, const LifeWrite& w) {
        const auto value = LifeValue(r, w.address);
        if (!value || !memory.IsValidVirtualAddressRange(value->first + 8, 4)) {
            return;
        }
        const s32 old = r.Get<s32>(value->first + 8).value_or(-1);
        if (w.hurt > 0 && replay_) {
            u64 player = 0;
            for (const auto& [a, known] : names_) {
                if (known.first == "Player_Child" || known.first == "Player_Adult") {
                    player = a;
                    break;
                }
            }
            if (player != 0) {
                Mover::Hit h;
                h.actor = w.address;
                h.attacker = player;
                h.amount = w.hurt;
                h.at = r.Get<std::array<float, 3>>(w.address + kActorPosition).value_or(h.at);
                h.from = r.Get<std::array<float, 3>>(player + kActorPosition).value_or(h.at);
                if (w.from) {
                    // The player who hit, so the enemy is knocked away from them.
                    h.from = *w.from;
                }
                TheMover().AddHit(h);
                return;  // the game takes the life; Sync writes it after 400 ms if not
            }
        }
        if (w.hurt > 0) {
            const auto table = r.Ptr(w.address + kActorComponents);
            const auto life = table ? r.Ptr(*table + kComponentLife) : std::nullopt;
            if (life && memory.IsValidVirtualAddressRange(*life + kLifeHurt, 4)) {
                const s32 pending = r.Get<s32>(*life + kLifeHurt).value_or(0);
                memory.Write32(*life + kLifeHurt, static_cast<u32>(pending + w.hurt));
                LOG_INFO(Core, "coop: objects: share hurt {:#x}: {} at life {} (to {})",
                         w.address, w.hurt, old, w.life);
                return;
            }
        }
        if (w.max > 0 && memory.IsValidVirtualAddressRange(value->second + 8, 4)) {
            const s32 old_max = r.Get<s32>(value->second + 8).value_or(-1);
            memory.Write32(value->second + 8, static_cast<u32>(w.max));  // first: the game clamps life to it
            LOG_INFO(Core, "coop: objects: share max {:#x}: {} -> {}", w.address, old_max, w.max);
        }
        memory.Write32(value->first + 8, static_cast<u32>(w.life));
        LOG_INFO(Core, "coop: objects: share life {:#x}: {} -> {}", w.address, old, w.life);
    }

    // LICHII_COOP_OBJECTS_TEST_DRIVE: see the top of this file.
    void Drive(Core::System& system, u64 now_us) {
        u64 player = 0;
        for (const auto& [a, known] : names_) {
            if (known.first == "Player_Child" || known.first == "Player_Adult") {
                player = a;
                break;
            }
        }
        const auto owned = sync_.Owned();
        drive_pose_.reset();
        if (self_ != 1 || player == 0 || owned.empty()) {  // player 1's game only
            return;
        }
        u64 enemy = owned.front().first;
        for (const auto& [a, name] : owned) {
            if (name == "Enemy_ReDead") {
                enemy = a;
                break;
            }
        }
        auto& memory = system.ApplicationMemory();
        Reader r(memory);
        const auto at = r.Get<std::array<float, 3>>(player + kActorPosition);
        const auto m = r.Get<std::array<float, 9>>(player + kActorRotation);
        if (!at || !m || r.Get<u32>(enemy + kActorState).value_or(0) != kStateAlive) {
            return;
        }
        // Columns of the player's rotation: column 0 is his side, column 2
        // the way he faces (two-game test 2026-11-09: row 2 put the enemy
        // between him and the camera).
        // Mode 2: still, 2 m ahead, no hurts (for real sword hits).
        const bool still = drive_mode_ == 2;
        const float t = static_cast<float>(now_us % 4'000'000) / 4'000'000.0f;
        const float sway = still ? 0.0f : 1.5f * std::sin(t * 6.2831853f);
        const float ahead = still ? 2.0f : 3.0f;
        Mover::Pose pose;
        pose.actor = enemy;
        pose.transform = {(*at)[0] + ahead * (*m)[2] + sway * (*m)[0], (*at)[1],
                          (*at)[2] + ahead * (*m)[8] + sway * (*m)[6]};
        drive_pose_ = pose;
        if (now_us - drive_log_at_ >= 2'000'000) {
            drive_log_at_ = now_us;
            LOG_INFO(Core,
                     "coop: objects: test drive player at {:.2f} {:.2f} {:.2f} rot {:.2f} {:.2f} {:.2f} | "
                     "{:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f}",
                     (*at)[0], (*at)[1], (*at)[2], (*m)[0], (*m)[1], (*m)[2], (*m)[3], (*m)[4],
                     (*m)[5], (*m)[6], (*m)[7], (*m)[8]);
        }
        if (enemy != drive_enemy_) {
            drive_enemy_ = enemy;
            drive_hurt_at_ = now_us;
            LOG_INFO(Core, "coop: objects: test drive {:#x} {} in front of {:#x}", enemy,
                     names_[enemy].first, player);
        }
        if (!still && now_us - drive_hurt_at_ >= 4'000'000) {
            drive_hurt_at_ = now_us;
            const auto value = LifeValue(r, enemy);
            const s32 life = value ? r.Get<s32>(value->first + 8).value_or(0) : 0;
            if (life > 1) {
                WriteLife(memory, r, {enemy, life - 1, hurt_ ? 1 : 0, {}});
            }
        }
    }

    // Issue 200 (npc-anim). The townspeople our event claimed (Sync::EventNpc)
    // keep this game's cutscene motion while the server hands them to us: the
    // owner's routine is not put on them. Logs the claim (our side) and, on the
    // other games, each NPC copy that moves to another owner; with
    // LICHII_COOP_DEBUG_NPC=1 every NPC copy's place and animation once a second.
    void NpcAnim(u64 now_us, std::vector<u64>& removes, std::vector<Mover::Pose>& wish,
                 std::vector<PlayWish>& plays) {
        std::erase_if(removes, [&](u64 a) { return sync_.EventNpc(a, now_us); });
        std::erase_if(wish, [&](const Mover::Pose& p) { return sync_.EventNpc(p.actor, now_us); });
        std::erase_if(plays, [&](const PlayWish& p) { return sync_.EventNpc(p.address, now_us); });
        const std::size_t claimed = sync_.EventNpcs(now_us);
        if ((claimed == 0) != (npc_claimed_ == 0)) {
            if (claimed != 0) {
                LOG_INFO(Core, "coop: npc-anim: our Link is in an event: {} NPCs near it claimed", claimed);
            } else {
                LOG_INFO(Core, "coop: npc-anim: the event's NPCs are free again");
            }
        }
        // The hand-over itself (the server may give them 0.5 s later, or
        // keep one in another game's fight lock): log each change of how
        // many of the claimed ones are ours.
        const std::size_t ours = claimed == 0 ? 0 : sync_.EventNpcsOurs(now_us);
        if (ours != npc_ours_ && claimed != 0) {  // the end has its own line
            LOG_INFO(Core, "coop: npc-anim: {} of the {} claimed NPCs are ours now", ours, claimed);
        }
        npc_ours_ = ours;
        npc_claimed_ = claimed;
        static const bool debug_npc = EnvOn("LICHII_COOP_DEBUG_NPC");
        const bool every = debug_npc && now_us - npc_log_at_ >= 1'000'000;
        if (every) {
            npc_log_at_ = now_us;
        }
        for (const Mover::Pose& p : wish) {
            const auto name = names_.find(p.actor);
            if (name == names_.end() || !name->second.first.starts_with("Npc_")) {
                continue;
            }
            const u8 owner = sync_.OwnerAt(p.actor);
            auto& last = npc_owner_[p.actor];
            if (!every && last == owner) {
                continue;
            }
            const auto play = std::find_if(plays.begin(), plays.end(),
                                           [&p](const PlayWish& w) { return w.address == p.actor; });
            LOG_INFO(Core, "coop: npc-anim: {} copied from P{} at {:.2f} {:.2f} {:.2f} plays '{}' frame {:.1f}",
                     name->second.first, owner, p.transform[0], p.transform[1], p.transform[2],
                     play != plays.end() ? play->name : std::string(), play != plays.end() ? play->frame : 0.0f);
            last = owner;
        }
        if (npc_owner_.size() > 512) {
            npc_owner_.clear();
        }
    }

    // ORM "Levantar": a line when another player's hold of a copy starts,
    // ends (the copy follows its samples again) or is left to fall.
    void NoteCarried(const std::vector<Sync::CarryPose>& carried) {
        std::map<u64, std::pair<u8, bool>> now;
        for (const Sync::CarryPose& c : carried) {
            now[c.address] = {c.carry.holder, c.fall};
            const auto before = carried_logged_.find(c.address);
            if (before != carried_logged_.end() && before->second == now[c.address]) {
                continue;
            }
            if (c.fall) {
                LOG_INFO(Core,
                         "coop: carry: P{} holds {} {:#x} but has no puppet here: it falls",
                         c.carry.holder, c.name, c.address);
            } else {
                LOG_INFO(Core, "coop: carry: P{} holds {} {:#x}: drawn in the puppet's hands{}",
                         c.carry.holder, c.name, c.address,
                         c.carry.bone != coop::protocol::kCarryNoBone
                             ? fmt::format(" (bone {})", c.carry.bone)
                             : std::string(" (actor frame)"));
            }
        }
        for (const auto& [address, was] : carried_logged_) {
            if (!now.contains(address)) {
                LOG_INFO(Core, "coop: carry: P{} let go of {:#x}: it follows the samples", was.first,
                         address);
            }
        }
        carried_logged_ = std::move(now);
    }

    // Every frame: each copy we do not own goes where the owner's enemy was.
    // How far the game moved it since our last write is logged every 5 s: if
    // its AI or physics fight the write, that shows here.
    void Place(Core::System& system, u64 now_us) {
        std::vector<PoseWrite> poses;
        sync_.Poses(now_us, poses);
        // A placed world object goes where coop_world says, or stays where
        // this game has it: the enemy sharing does not place those.
        std::erase_if(poses, [this](const PoseWrite& w) { return world_.Places(w.address); });
        world_.Poses(now_us, poses);
        // ORM "Levantar": what our Link lifts is our game's to move; what
        // another player holds goes in his puppet's hands (or, his puppet
        // gone from here, is left to fall).
        std::vector<Sync::CarryPose> carried;
        if (carry_) {
            std::erase_if(poses, [this](const PoseWrite& w) { return sync_.CarryFreed(w.address); });
            sync_.Carried(now_us, CoopPuppet::LivePuppets(), carried);
            std::erase_if(poses, [&carried](const PoseWrite& w) {
                return std::any_of(carried.begin(), carried.end(),
                                   [&w](const Sync::CarryPose& c) { return c.address == w.address; });
            });
            NoteCarried(carried);
            // ORM levantar-vasija: copies no longer held still (CoopCarry::Still).
            for (const std::string& line : TheMover().TakeStillLines()) {
                LOG_INFO(Core, "{}", line);
            }
        }
        auto& memory = system.ApplicationMemory();
        Reader r(memory);
        std::unordered_map<u64, coop::protocol::Vec3> placed;
        std::vector<Mover::Pose> wish;
        if (drive_pose_) {
            wish.push_back(*drive_pose_);
        }
        // Copies whose own AI wanted another animation (issue 157).
        for (const auto& [a, name] : TheMover().TakeTakeovers()) {
            sync_.NoteTakeover(a, now_us, name);
        }
        for (const PoseWrite& w : poses) {
            const u32 state = r.Get<u32>(w.address + kActorState).value_or(0);
            bool asleep = false;
            if (state != kStateAlive) {
                // Issue 101: our copy sleeps at its home far from our Link
                // while the owner's enemy chases another player up to us.
                // Skipping it left it there; move it when the owner's place
                // is near our Link, and the game wakes it there.
                if (!wake_far_ || state != kStateAsleep || !player_ ||
                    Apart(*player_, w.position) >= kAsleepPlaceM) {
                    continue;
                }
                asleep = true;
                ++asleep_placed_;
            }
            if (const auto before = placed_.find(w.address); before != placed_.end()) {
                coop::protocol::Vec3 now{};
                float d2 = 0.0f;
                for (std::size_t i = 0; i < 3; ++i) {
                    now[i] = r.Get<float>(w.address + kActorPosition + 4 * i).value_or(0.0f);
                    const float d = now[i] - before->second[i];
                    d2 += d * d;
                }
                const float d = std::sqrt(d2);
                // A copy whose own AI pulls it at our Link (issue 101).
                sync_.NoteDrift(w.address, before->second, now, now_us);
                drift_sum_ += d;
                if (d > drift_max_) {
                    drift_max_ = d;
                    drift_worst_ = w.address;
                }
                ++drift_count_;
            }
            Mover::Pose p;
            p.actor = w.address;
            p.asleep = asleep;
            std::copy(w.position.begin(), w.position.end(), p.transform.begin());
            if (w.rotation) {
                std::copy(w.rotation->begin(), w.rotation->end(), p.transform.begin() + 3);
                p.rotation = true;
            }
            wish.push_back(p);
            placed.emplace(w.address, w.position);
        }
        for (const Sync::CarryPose& c : carried) {
            if (c.fall) {
                continue;  // no puppet holds it here: our game drops it
            }
            Mover::Pose p;
            p.actor = c.address;
            std::copy(c.fallback.position.begin(), c.fallback.position.end(), p.transform.begin());
            if (c.fallback.rotation) {
                std::copy(c.fallback.rotation->begin(), c.fallback.rotation->end(),
                          p.transform.begin() + 3);
                p.rotation = true;
            }
            p.attach = Mover::Pose::Attach{c.carry, c.weight};
            wish.push_back(p);
        }
        placed_ = std::move(placed);
        std::vector<PlayWish> plays;
        sync_.Plays(now_us, plays);
        if (ai_hold_ != 0 || npc_pose_) {
            // ORM: LICHII_COOP_NPC_POSE alone holds the townspeople copies in mode 1.
            TheMover().Hold(sync_.Holds(now_us), CoopHold::MasksFor(ai_hold_ != 0 ? ai_hold_ : 1));
        }
        if (const auto ours = sync_.TakeOursNow(); release_ && !ours.empty()) {
            TheMover().Release(ours);
        }
        std::vector<u64> removes = sync_.Removes();
        for (const u64 a : world_.Removes()) {
            removes.push_back(a);
        }
        // Issue 144: nothing near our Link is removed, moved or made to play while
        // it is in an event; they come again each frame and are made after it.
        {
            const CoopGuard::Local local = GuardLocal();
            const auto where = [this](std::uint64_t a) { return Where(a); };
            CoopGuard::Skip(removes, local, where, [](u64 a) { return a; });
            // Issue 194: a world object another game moves (coop_world: a falling ladder,
            // a block and its base anchor) is no event's partner: it follows at once, else
            // it jumped after.
            const auto world = [this](std::uint64_t a) { return world_.Follows(a); };
            CoopGuard::Skip(wish, local, where, [](const Mover::Pose& p) { return p.actor; }, world);
            std::vector<u64> wanted_plays;
            wanted_plays.reserve(plays.size());
            for (const PlayWish& p : plays) {
                wanted_plays.push_back(p.address);
            }
            CoopGuard::Skip(plays, local, where, [](const PlayWish& p) { return p.address; }, world);
            if (wanted_plays.size() != plays.size()) {
                // A chest's open idle held back: its show-open clock waits too.
                std::set<u64> sent;
                for (const PlayWish& p : plays) {
                    sent.insert(p.address);
                }
                for (const u64 a : wanted_plays) {
                    if (!sent.contains(a)) {
                        sync_.ChestPlayHeld(a, now_us);
                    }
                }
            }
        }
        NpcAnim(now_us, removes, wish, plays);
        TheMover().Want(main_, std::move(wish), std::move(removes), std::move(plays));
        if (now_us - drift_at_ >= kDriftLogUs) {
            const auto [moved, deleted, played, framed] = TheMover().TakeCounts();
            if (drift_count_ > 0 || moved > 0 || deleted > 0 || played > 0 || framed > 0 ||
                sync_.HurtsTaken() + sync_.HurtsMissed() > 0) {
                LOG_INFO(Core,
                         "coop: objects: share placed {} copies; off from where we put them "
                         "avg {:.3f} max {:.3f} over {}; game calls: {} moves, {} deletes, {} "
                         "plays, {} frame sets; hurts taken {} missed {}",
                         placed_.size(), drift_count_ ? drift_sum_ / drift_count_ : 0.0f,
                         drift_max_, drift_count_, moved, deleted, played, framed, sync_.HurtsTaken(),
                         sync_.HurtsMissed());
            }
            // Issue 101: which copy was furthest from where we put it, so
            // one run tells a copy the setter does not hold (a flying or
            // sleeping enemy) from the AI's own small pull.
            if (asleep_placed_ > 0) {
                LOG_INFO(Core,
                         "coop: objects: share {} copy-frames placed while asleep (owner's "
                         "place within {:.0f} m of our Link, issue 101)",
                         asleep_placed_, kAsleepPlaceM);
                asleep_placed_ = 0;
            }
            if (debug_ && drift_worst_ != 0 && drift_max_ >= kDriftWorstM) {
                LOG_INFO(Core, "coop: objects: share worst copy {:#x} '{}' off {:.2f} m", drift_worst_,
                         r.Name(drift_worst_), drift_max_);
            }
            if (carry_) {
                const auto [on_bone, on_actor, fell_back, held_still] = TheMover().TakeCarryCounts();
                if (on_bone + on_actor + fell_back > 0) {
                    LOG_INFO(Core,
                             "coop: carry: 5 s: {} carried copy-frames on a puppet's bone, {} on "
                             "its actor frame, {} where the samples put them (no puppet); {} held "
                             "still (speeds zeroed)",
                             on_bone, on_actor, fell_back, held_still);
                }
            }
            if (release_) {
                const auto [cleared, clear, dropped] = TheMover().TakeReleases();
                if (cleared + clear + dropped > 0) {
                    LOG_INFO(Core,
                             "coop: objects: release: {} that became ours had bit 23 set "
                             "(cleared), {} had it clear, {} skipped (over 64 in a frame)",
                             cleared, clear, dropped);
                }
            }
            if (ai_hold_ != 0 || npc_pose_) {
                const auto [frames, kept] = TheMover().TakeHolds();
                LOG_INFO(Core,
                         "coop: objects: ai hold mode {}: {} copy-frames held, brain skip bit as "
                         "asked in {}; {} plays in {:.1f} s",
                         ai_hold_ != 0 ? ai_hold_ : 1, frames, kept, played,
                         static_cast<double>(now_us - drift_at_) / 1e6);
            }
            drift_at_ = now_us;
            drift_sum_ = 0.0f;
            drift_max_ = 0.0f;
            drift_worst_ = 0;
            drift_count_ = 0;
        }
    }

    // The cheat items (coop_cheat_items.h), topped up once a second.
    void Cheat(Core::System& system) {
        CoopHook::EmulatorMemory memory(system.ApplicationMemory(), main_);
        for (const auto& note : CoopCheatItems::TopUp(memory, cheat_state_)) {
            LOG_INFO(Core, "coop: objects: {}", note);
        }
    }

    static u64 FindMain(Core::System& system, Kernel::KProcess* process) {
        Loader::AppLoader::Modules modules;
        if (system.GetAppLoader().ReadNSOModules(modules) != Loader::ResultStatus::Success ||
            modules.empty()) {
            modules = Core::FindModules(process);
        }
        for (const auto& [address, name] : modules) {
            if (name == "main" || name.starts_with("main.")) {
                return address;
            }
        }
        return GetInteger(Core::FindMainModuleEntrypoint(process));
    }

    // The actors on the live list. False when the list changed under us.
    bool List(Reader& r, std::vector<u64>& out) {
        const auto manager = r.Ptr(main_ + kActorManagerPtr);
        if (!manager) {
            return false;
        }
        const auto lists = r.Ptr(*manager + kManagerLists);
        if (!lists) {
            return false;
        }
        const u64 head = *lists + kListHead;
        const auto count = r.Get<u32>(head + kListCount);
        if (!count) {
            return false;
        }
        std::size_t walked = 0;
        auto node = r.Ptr(head);
        while (node && *node != head && walked < kMaxWalk) {
            ++walked;
            const auto owner = r.Get<u64>(*node + kNodeOwner);
            if (owner && *owner + kActorNode == *node) {
                out.push_back(*owner);
            }
            node = r.Ptr(*node);
        }
        const bool whole = node && walked == *count;
        if (first_ && whole) {
            LOG_INFO(Core, "coop: objects: list head {:#x}, {} entries, {} actors", head, walked,
                     out.size());
        }
        return whole;
    }

    void Walk(Reader& r) {
        std::vector<u64> list;
        if (!List(r, list)) {
            return;  // the list changed under us; try the next frame
        }
        const std::unordered_set<u64> seen(list.begin(), list.end());
        for (const u64 a : seen) {
            auto [it, added] = actors_.try_emplace(a);
            Actor& actor = it->second;
            actor.missed = 0;
            if (added) {
                actor.name = r.Name(a);
                const auto placement = r.Ptr(a + kActorPlacement);
                actor.hash = placement ? r.Get<u64>(*placement + kPlacementHash).value_or(0) : 0;
                if (!first_) {
                    LogPose(r, "+", a, actor);
                }
            }
            if (IsShared(actor.name)) {  // enemies and breakables
                Life(r, a, actor);
            }
        }
        for (auto it = actors_.begin(); it != actors_.end();) {
            if (seen.contains(it->first) || ++it->second.missed < 2) {
                ++it;
                continue;
            }
            LOG_INFO(Core, "coop: objects: - {} {:#x} hash {:#x}", it->second.name, it->first,
                     it->second.hash);
            it = actors_.erase(it);
        }
        first_ = false;
    }

    void LogPose(Reader& r, const char* what, u64 a, const Actor& actor) {
        const auto x = r.Get<float>(a + kActorPosition);
        const auto y = r.Get<float>(a + kActorPosition + 4);
        const auto z = r.Get<float>(a + kActorPosition + 8);
        const auto state = r.Get<u32>(a + kActorState);
        LOG_INFO(Core, "coop: objects: {} {} {:#x} state {} at {:.2f} {:.2f} {:.2f} hash {:#x}",
                 what, actor.name, a, state.value_or(0), x.value_or(0.0f), y.value_or(0.0f),
                 z.value_or(0.0f), actor.hash);
    }

    void Life(Reader& r, u64 a, Actor& actor) {
        const auto table = r.Ptr(a + kActorComponents);
        const auto life = table ? r.Ptr(*table + kComponentLife) : std::nullopt;
        if (!life) {
            return;
        }
        const auto value = r.Ptr(*life + kLifeValue);
        const auto max = r.Ptr(*life + kLifeMax);
        if (!value || !max) {
            return;
        }
        const std::array<s32, 4> now{
            r.Get<s32>(*value + 8).value_or(-1), r.Get<s32>(*max + 8).value_or(-1),
            r.Get<s32>(*life + kLifeCopy).value_or(-1), r.Get<s32>(*life + kLifeCopy + 4).value_or(-1)};
        if (actor.life && (*actor.life)[0] == now[0] && (*actor.life)[1] == now[1]) {
            return;  // the copy alone moves a frame or two later; not logged
        }
        LOG_INFO(Core, "coop: objects: life {} {:#x} hash {:#x}: {} -> {} of {} (copy {} of {})",
                 actor.name, a, actor.hash, actor.life ? (*actor.life)[0] : now[0], now[0], now[1],
                 now[2], now[3]);
        if (actor.life && now[0] < (*actor.life)[0]) {
            // A hit: the hit record the Life step reads (+0x1f0..+0x2f0) and
            // the copy it keeps of the last one (+0x1b00..+0x1bb0), in hex.
            for (const auto [from, to] : {std::pair<u64, u64>{0x1f0, 0x2f0}, {0x1b00, 0x1bb0}}) {
                std::string hex;
                for (u64 o = from; o < to; o += 4) {
                    hex += fmt::format("{:08x}{}", r.Get<u32>(*life + o).value_or(0),
                                       (o - from) % 32 == 28 ? " | " : " ");
                }
                LOG_INFO(Core, "coop: objects: hit record {} +{:#x}: {}", actor.name, from, hex);
            }
        }
        actor.life = now;
    }

    std::unordered_map<u64, coop::protocol::Vec3> placed_;  // what we wrote last frame
    u8 self_ = 0;
    std::unordered_set<std::string> vtables_logged_;
    std::unordered_map<u64, std::string> anim_logged_;
    unsigned anim_lines_ = 0;
    std::optional<Mover::Pose> drive_pose_;
    // Issue 200 (npc-anim): the log of our event's claim and of NPC copies.
    std::size_t npc_claimed_ = 0;
    std::size_t npc_ours_ = 0;     // how many of them the server gave us (log)
    u64 npc_log_at_ = 0;
    std::unordered_map<u64, u8> npc_owner_;  // the owner each NPC copy was last logged with
    u64 drive_enemy_ = 0;
    u64 drive_log_at_ = 0;
    u64 drive_hurt_at_ = 0;
    float drift_sum_ = 0.0f;
    float drift_max_ = 0.0f;
    u64 drift_worst_ = 0;  // the copy at drift_max_
    static constexpr float kDriftWorstM = 1.0f;
    std::size_t drift_count_ = 0;
    u64 drift_at_ = 0;
    CoopEvery::Every scan_{CoopEvery::kTwoFramesUs};
    CoopEvery::Every cheat_every_{CoopEvery::kSecondUs};
    CoopCheatItems::State cheat_state_;
    CoopEvery::Every share_every_{CoopEvery::kTwoFramesUs};
    bool sharing_ = false;
    std::size_t blast_drops_logged_ = 0;
    unsigned chests_twice_seen_ = 0;  // sync_.ChestsOpenedTwice() passed on (issue 195)
    std::size_t present_logged_ = ~std::size_t{0};
    std::pair<std::size_t, std::string> fish_logged_;  // the last FISH_LURE logged
    std::vector<coop::protocol::ObjectGroupEntry> group_logged_;
    CoopObjects::OwnerLog owner_log_;  // network thread
    std::size_t enemies_logged_ = 0;
    std::size_t refused_logged_ = 0;
    std::uint8_t decider_logged_ = 0;
    std::unordered_map<u64, std::pair<std::string, u64>> names_;  // name, hash by actor
    Sync sync_;
    Kernel::KProcess* process_ = nullptr;
    u64 main_ = 0;
    bool first_ = true;
    std::unordered_map<u64, Actor> actors_;
};

Objects& Instance() {
    static Objects objects;  // first called on the present thread
    static const bool once = [] {
        objects.debug_ = EnvFlag("LICHII_COOP_DEBUG_OBJECTS");
        objects.cheat_ = EnvFlag("LICHII_COOP_OBJECTS_CHEAT");
        objects.share_ = EnvFlag("LICHII_COOP_OBJECTS");
        objects.pose_ = EnvOn("LICHII_COOP_OBJECTS_POSE");
        objects.gimmick_probe_on_ = EnvFlag("LICHII_COOP_GIMMICK_PROBE");  // opt-in: many reads
        {
            const std::string platforms = EnvText("LICHII_COOP_PLATFORMS");
            objects.platforms_mode_ = platforms == "0" ? 0 : platforms == "probe" ? 2 : 1;
        }
        objects.scrubs_on_ = EnvOn("LICHII_COOP_SCRUBS");  // issue 149, on unless =0
        objects.ocarina_probe_on_ = EnvFlag("LICHII_COOP_OCARINA_PROBE");  // issue 152, reads only
        // On by default (=0 turns it off): a life an enemy must lose is given
        // to the game as a whole hit, so it flinches, is knocked back and a
        // Stalchild loses its head on every screen. An amount alone at
        // Life + 0x228 is not taken (live test, 2026-11-09).
        objects.replay_ = EnvOn("LICHII_COOP_OBJECTS_HIT_REPLAY");
        objects.hurt_ = EnvFlag("LICHII_COOP_OBJECTS_HURT") || objects.replay_;
        objects.drive_ = EnvFlag("LICHII_COOP_OBJECTS_TEST_DRIVE");
        objects.ai_hold_ = EnvNumber("LICHII_COOP_OBJECTS_AI_HOLD");
        objects.test_take_ = EnvHex("LICHII_COOP_OBJECTS_TEST_TAKE");
        objects.drive_mode_ = objects.drive_ ? EnvNumber("LICHII_COOP_OBJECTS_TEST_DRIVE") : 0;
        objects.hearts_to_link_ = EnvOn("LICHII_COOP_HEARTS_TO_LINK");
        objects.wake_ = EnvOn("LICHII_COOP_OBJECTS_WAKE");
        objects.release_ = EnvOn("LICHII_COOP_OBJECTS_RELEASE");
        objects.pvp_replay_ = EnvOn("LICHII_COOP_PVP_REPLAY");
        objects.wake_far_ = EnvOn("LICHII_COOP_OBJECTS_ASLEEP");
        objects.hit_feedback_ = CoopSwitch::On("LICHII_COOP_HITS", true);           // ORM golpes, on by default
        objects.hit_dedupe_ = CoopSwitch::On("LICHII_COOP_HITS_DEDUPE", false);    // off by default
        objects.copy_smooth_ = CoopSwitch::On("LICHII_COOP_COPY_SMOOTH", true);    // on by default
        objects.npc_ = CoopSwitch::On("LICHII_COOP_NPC", false);  // ORM D-R12, off by default
        objects.npc_pose_ = CoopSwitch::On("LICHII_COOP_NPC_POSE", false);  // ORM steps 5 to 7, off by default
        objects.npc_gaze_ = CoopSwitch::On("LICHII_COOP_NPC_GAZE", false);  // ORM NPC_MIRADA.md, off by default
        // ORM "Levantar" (coop_carry.h), on by default.
        objects.carry_ = CoopSwitch::On("LICHII_COOP_CARRY", true);
        objects.carry_bone_ = CoopSwitch::On("LICHII_COOP_CARRY_BONE", true);
        objects.carry_still_ = CoopSwitch::On("LICHII_COOP_CARRY_STILL", true);  // levantar-vasija
        // ORM boss families (oot-remake docs/JEFES.md section 9), on by default.
        objects.families_ = CoopSwitch::On("LICHII_ORM_BOSS_FAMILIES", true);
        CoopCarry::SetShareFlowers(objects.carry_ &&
                                   CoopSwitch::On("LICHII_COOP_CARRY_FLOWERS", true));
        return true;
    }();
    (void)once;
    return objects;
}

} // namespace

void SetLocalEvent(bool in_event) {
    TheLocalEvent().store(in_event);
}

std::optional<LinkHearts> OurLife() {
    const u64 at = TheLife().at_us.load();
    if (at == 0 || CoopEvery::NowUs() - at > 2'000'000) {
        return std::nullopt;
    }
    return LinkHearts{TheLife().life.load(), TheLife().max.load()};
}

void SetLocalEventName(std::optional<std::string> name) {
    auto& n = TheLocalEventName();
    std::lock_guard lock(n.mutex);
    n.name = std::move(name);
}

void SetKeep(bool up, bool frozen, bool charging, std::optional<unsigned> charge, bool held) {
    std::uint32_t word = (up ? 1u : 0u) | (frozen ? 2u : 0u) | (charging ? 4u : 0u) | (held ? 8u : 0u);
    if (charge && *charge < 0xffu) {
        word |= (*charge + 1u) << 8;
    }
    TheKeep().store(word);
}

void SetMaxLife(std::optional<std::int32_t> quarters) {
    TheMaxLife().store(quarters.value_or(-1));
}

std::optional<std::int32_t> OurLinkLife() {
    const u64 at = TheLinkLifeAt().load();
    const s32 life = TheLinkLife().load();
    if (at == 0 || life < 0 || SteadyNowMs() > at + kLinkLifeStaleMs) {
        return std::nullopt;
    }
    return life;
}

void HurtOurLink(std::int32_t quarters, const std::array<float, 3>& at,
                 const std::array<float, 3>& from) {
    if (quarters <= 0) {
        return;
    }
    std::lock_guard lock(PvpMutex());
    if (PvpAsks().size() < 16) {
        PvpAsks().push_back({quarters, at, from});
    }
}

void OnFrame(Core::System& system) {
    Objects& objects = Instance();
    if (objects.debug_ || objects.cheat_) {
        objects.Frame(system);  // the present thread only
    }
}

void Share(Core::System& system, coop::host::Session* session,
           const std::optional<std::string>& scene, std::uint64_t server_now_us, bool event) {
    Objects& objects = Instance();
    if (objects.share_) {
        objects.Share(system, session, scene, server_now_us, event);
    }
}

std::vector<std::uint64_t> OwnedActors() {
    Objects& objects = Instance();
    std::vector<std::uint64_t> out;
    if (objects.share_) {
        for (const auto& [address, name] : objects.OwnedNow()) {
            out.push_back(address);
        }
    }
    return out;
}

void OwnCountDrops(const std::vector<std::uint32_t>& key_flags) {
    if (key_flags.empty()) {
        return;
    }
    std::lock_guard lock(KeyRefundMutex());
    auto& drops = OwnDropQueue();
    drops.insert(drops.end(), key_flags.begin(), key_flags.end());
}

void QueueTestKill(std::string name) {
    std::lock_guard lock(KeyRefundMutex());
    if (TestKillQueue().size() < 16) {
        TestKillQueue().push_back(std::move(name));
    }
}

void ForgetKeptBreaks() {
    std::lock_guard lock(KeyRefundMutex());
    ForgetKeptFlag() = true;
}

std::vector<std::uint32_t> TakeKeyRefunds() {
    std::lock_guard lock(KeyRefundMutex());
    std::vector<std::uint32_t> out;
    out.swap(KeyRefundQueue());
    return out;
}

unsigned TakeChestsOpenedTwice() {
    return TheChestsOpenedTwice().exchange(0);
}

void TakenForOthers(const std::vector<std::uint64_t>& keys) {
    if (keys.empty()) {
        return;
    }
    Objects& objects = Instance();
    for (const std::uint64_t key : keys) {
        objects.Take(key);
    }
}

std::optional<std::vector<coop::host::SeenActor>> WalkActors(const std::optional<std::string>& scene) {
    Objects& objects = Instance();
    if (!objects.share_) {
        return std::nullopt;
    }
    return objects.WalkActors(scene);
}

std::optional<std::uint64_t> LoadedActor(std::uint64_t hash, const std::optional<std::string>& scene) {
    Objects& objects = Instance();
    if (!objects.share_) {
        return std::nullopt;
    }
    return objects.LoadedActor(hash, scene);
}

void OnEvent(const coop::net::Event& event) {
    Objects& objects = Instance();
    if (event.kind == coop::net::EventKind::reconnected) {
        objects.ForgetOwnerLog();  // a restarted server may hand out the same owner and epoch again
        // Its epochs start at 0 again: an old track would take the new poses
        // for stale (review 2026-11-11, oot-remake docs/REVISION_OBJETOS.md).
        CoopNpcPose::TheNpcPose().Reset();
    }
    if (event.kind != coop::net::EventKind::message) {
        return;
    }
    if (objects.share_) {
        objects.Receive(event.message);
    }
    if (objects.share_ && objects.npc_pose_) {
        if (const auto* pose = std::get_if<coop::protocol::ObjectPose>(&event.message)) {
            CoopNpcPose::TheNpcPose().Receive(*pose, Objects::MonotonicMs());
        }
    }
}

namespace {

// The bosses of the last walk, for LastBossAlive (issue 109).
std::mutex g_bosses_mutex;
std::vector<Seen> g_bosses;
std::uint64_t g_bosses_at = 0;
constexpr std::uint64_t kBossesFreshUs = 1'500'000;

} // namespace

void NoteBosses(const std::vector<Seen>& seen) {
    std::vector<Seen> bosses;
    for (const Seen& s : seen) {
        if (s.name.starts_with("Enemy_DungeonBoss_")) {
            bosses.push_back(s);
        }
    }
    std::lock_guard lock(g_bosses_mutex);
    g_bosses = std::move(bosses);
    g_bosses_at = CoopEvery::NowUs();
}

std::optional<bool> LastBossAlive(std::span<const std::string_view> bodies) {
    std::lock_guard lock(g_bosses_mutex);
    const std::uint64_t now = CoopEvery::NowUs();
    if (g_bosses_at == 0 || now < g_bosses_at || now - g_bosses_at > kBossesFreshUs) {
        return std::nullopt;
    }
    return BossAlive(g_bosses, bodies);
}

} // namespace CoopObjects
