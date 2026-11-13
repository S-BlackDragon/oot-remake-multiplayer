// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "PNJ que mira a la marioneta" (oot-remake docs/NPC_MIRADA.md, AUDITORIA_PLAN
// §10 item 16; LICHII_COOP_NPC_GAZE=1, off by default): a townsperson turns its
// head to a partner's puppet when that partner is the player nearest to it, or
// the one talking to it, as it does to our own Link. No emulator code: the
// rules and the memory work on a Guest, tested on fake memory
// (tests/lichii/npc_gaze_test.cpp); coop_objects.cpp routes the walk and the
// calc thread's turn at the svc cave.
//
// How the game picks what a townsperson looks at (found offline in main
// e0f3ccfa, docs/NPC_MIRADA.md; to confirm in a run, see the check lines):
// its AI perceives the player (NpcCommon.QueryGetPerceptionPlayerInfo, the
// player's resident PerceptionEmitter) and puts him into a slot of its Target
// component (OneShotTargetSetInfoFromPerceiveData, f_808aeb10 / f_808aefe0);
// the look IK is turned on for that slot (OneshotLookIKArbiterActivateByTargetSlot).
// A slot is either a place (kind 1: the point at +0xe0, which
// QueryTargetGetTargetInfoPosition reads) or an actor link (kind 2: the link at
// +0x8, kept up to date from the actor, IsUpdateFromActorLink). The puppets
// have no perception emitter (tools/puppet_pack keeps only what draws Link), so
// no townsperson ever puts one in a slot.
//
// What this does: for a townsperson that should look at partner P, each slot
// that links our Link is turned, at the start of each game frame, into a place
// slot at P's puppet's eyes; the link itself is left as it is (the game's
// slot clear, f_808b1ca0, lets go of it by its own fields, whatever the kind),
// so giving the slot back is one byte: the kind is set back to 2 while the
// slot still holds our place. Self-check: every frame the slots written the
// frame before are read again; after Settings::judge_frames frames with
// writes, "check: npc gaze ok" when the game left them as written, "check: npc
// gaze fallback" (and nothing more written this run, every slot given back)
// when it set them again most of the time. A slot the game changed (its AI
// re-targeted) is left as the game set it, and taken again only if it links
// our Link once more; a slot is never written while ambiguous (its key or link
// names both our Link and the puppet).
//
// Threads: Plan (rules) on the present thread; SetOrders from the present
// thread and OnCalcFrame from the calc thread, guarded by one mutex; TakeLog
// from the present thread.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "coop_puppet.h"

namespace CoopNpcGaze {

using Vec3 = std::array<float, 3>;

// ---- The game's Target component (offline; docs/NPC_MIRADA.md "Dónde") ----
inline constexpr std::uint64_t kComponentTable = 0x270;   // actor -> its component table
inline constexpr std::uint64_t kTargetComponent = 0x418;  // table -> Target component (TargetRef)
inline constexpr std::uint64_t kSlotsAt = 0x28;           // component -> slot 0
inline constexpr std::uint64_t kSlotSize = 0x130;
inline constexpr unsigned kSlotCount = 16;                // the AI nodes clamp the slot to < 16
inline constexpr std::uint64_t kSlotLinkData = 0x10;      // u64: the link's data (0: no link)
inline constexpr std::uint64_t kSlotLinkId = 0x18;        // u32: the link's id (-1: none)
inline constexpr std::uint64_t kLinkDataActor = 0x40;     // u64 in the link's data: its actor (H2)
inline constexpr std::uint64_t kLinkDataId = 0x48;        // u32 in the link's data: the id a live link agrees with
inline constexpr std::uint64_t kSlotActorKey = 0xd4;      // u32: the linked actor's +0x3e8 (f_808aefe0)
inline constexpr std::uint64_t kActorKey = 0x3e8;
inline constexpr std::uint64_t kSlotPlace = 0xe0;         // 3 floats: a place slot's point
inline constexpr std::uint64_t kSlotFlags = 0x120;        // u8: bit 0 a rotation at +0xec, bit 1 a velocity
inline constexpr std::uint64_t kSlotKind = 0x12a;         // u8
inline constexpr std::uint8_t kKindNone = 0;
inline constexpr std::uint8_t kKindPlace = 1;
inline constexpr std::uint8_t kKindActor = 2;
inline constexpr std::uint8_t kPlaceFlags = 0x03;  // cleared while our place is in the slot (no rotation, no velocity)
inline constexpr std::uint64_t kActorPosition = 0x31c;  // a puppet's place

// One slot in use, as read.
struct Slot {
    unsigned index = 0;
    std::uint64_t at = 0;  // its address
    std::uint8_t kind = kKindNone;
    std::uint8_t flags = 0;
    std::uint32_t key = 0xffffffffu;
    std::uint64_t link_data = 0;
    std::uint32_t link_id = 0xffffffffu;
    std::optional<std::uint64_t> link_actor;  // [link_data + kLinkDataActor], when the link's data agrees with its id
    Vec3 place{};
};

// The Target component of `actor`, or nothing (no table, no component, a read failed).
std::optional<std::uint64_t> TargetComponent(CoopPuppet::Guest& guest, std::uint64_t actor);
// The slots of `actor`'s Target component whose kind is not none, in order.
std::vector<Slot> ReadSlots(CoopPuppet::Guest& guest, std::uint64_t actor);

// How a slot names an actor: by its link (the link's data points at it, H2),
// by the key the slot copied from it (+0x3e8, H3), or both.
enum class Names : std::uint8_t { no, by_link, by_key, both };
const char* ToString(Names names);
// `key`: the actor's u32 at kActorKey, when read (0 and -1 never match).
Names SlotNames(const Slot& slot, std::uint64_t actor, std::optional<std::uint32_t> key);

// ---- The rule: whom each townsperson looks at ----

struct Settings {
    // A townsperson looks at a player within this (InterestReceiveParam
    // NpcCommon: PlayerDistance 10, DistanceLimit 3 for a strong interest).
    float look_m = 10.0f;
    // A partner takes the look from the one looked at only when nearer by this,
    // and gives it back the same way, so two players about as near do not make
    // the head swing between them.
    float margin_m = 1.0f;
    // Once changed by distance, the look stays at least this long.
    std::uint64_t hold_us = 1'000'000;
    // At most this many townspeople are turned at once, the nearest to our Link.
    std::size_t max_npcs = 8;
    // The puppet's eyes above its feet (world units, about metres; Link's
    // PlayerCameraTargetParam: TargetHeight 1.68, the adult).
    float eye_adult_m = 1.6f;
    float eye_child_m = 1.1f;
    // The self-check: after this many calc frames with writes, judged.
    unsigned judge_frames = 60;
    float judge_kept = 0.8f;  // the share of slots found as written to be "ok"
};

// A partner whose puppet is live in our place.
struct Partner {
    std::uint8_t player = 0;
    Vec3 at{};
};

// A townsperson of the walk.
struct Npc {
    std::uint64_t actor = 0;
    std::string name;
    Vec3 at{};
    bool ours_talk = false;         // our player talks to it (NpcTalk's partner)
    std::uint8_t talked_by = 0;     // a partner talks to it (coop::host::owner_talks); 0 nobody
};

// What one townsperson should look at: a partner's puppet.
struct Order {
    std::uint64_t npc = 0;
    std::string name;
    std::uint8_t player = 0;
    bool operator==(const Order&) const = default;
};

enum class Why : std::uint8_t { none, talks, nearest };
const char* ToString(Why why);

class Rule {
public:
    explicit Rule(Settings s = {}) : s_(s) {}
    // Present thread, once a walk: the orders (townspeople to turn to a
    // partner; the others look as the game has them) and a line for each
    // change. `self` our Link's place (nothing: not known, every look stays
    // the game's).
    std::vector<Order> Plan(const std::vector<Npc>& npcs, const std::optional<Vec3>& self,
                            const std::vector<Partner>& partners, std::uint64_t now_us,
                            std::vector<std::string>& log);
    // The look of `npc` now: 0 our Link (the game's own), else a partner.
    std::uint8_t LookOf(std::uint64_t npc) const;
    void Reset() { state_.clear(); }

private:
    struct State {
        std::uint8_t player = 0;
        Why why = Why::none;
        std::uint64_t since = 0;
    };
    Settings s_;
    std::map<std::uint64_t, State> state_;
};

// ---- The writer: the calc thread's part ----

struct PuppetAt {
    std::uint8_t player = 0;
    std::uint64_t actor = 0;
    bool adult = false;
};

struct Counts {
    unsigned written = 0;      // slot-frames written
    unsigned kept = 0;         // a slot found as written the frame before
    unsigned overwritten = 0;  // a slot the game set again (given up, not written back)
    unsigned given_back = 0;   // slots set back to the link
    unsigned no_slot = 0;      // an order whose townsperson has no slot linking our Link this frame
    unsigned ambiguous = 0;    // a slot that names our Link and the puppet both: not written
};

class Writer {
public:
    using Alive = std::function<bool(CoopPuppet::Guest&, std::uint64_t)>;
    explicit Writer(Settings s = {}) : s_(s) {}
    // Present thread: the orders of the walk and our Link's actor (0: not
    // known, every slot is given back and nothing written).
    void SetOrders(std::vector<Order> orders, std::uint64_t link_actor);
    // Calc thread, at the start of a game frame. `puppets` the live puppets
    // of this frame; `alive` says an address is still a live actor.
    void OnCalcFrame(CoopPuppet::Guest& guest, const std::vector<PuppetAt>& puppets, const Alive& alive);
    // Present thread: the lines since the last take ("npc gaze: ...", "check: npc gaze ...").
    std::vector<std::string> TakeLog();
    Counts TakeCounts();
    bool fell_back() const;
    // A new scene, the switch off: forget every slot without writing (the
    // actors may be gone).
    void Reset();

private:
    struct Held {
        std::uint64_t npc = 0;
        unsigned index = 0;
        std::uint8_t kind = kKindActor;  // what it was
        std::uint8_t flags = 0;
        std::uint64_t link_data = 0;
        std::uint32_t link_id = 0;
        Vec3 wrote{};
        std::uint8_t player = 0;
    };
    bool StillOurs(CoopPuppet::Guest& guest, const Held& h, std::uint64_t slot) const;
    void GiveBack(CoopPuppet::Guest& guest, const Held& h);
    void Say(std::string line);

    Settings s_;
    mutable std::mutex mutex_;
    std::vector<Order> orders_;
    std::uint64_t link_ = 0;
    std::vector<Held> held_;
    std::set<std::pair<std::uint64_t, bool>> named_;  // (townsperson, slot found): its line logged
    Counts counts_;
    unsigned judged_frames_ = 0;
    unsigned judge_kept_ = 0;
    unsigned judge_over_ = 0;
    bool judged_ = false;
    bool fell_back_ = false;
    std::vector<std::string> log_;
};

// The one the hook uses.
Writer& TheWriter();

} // namespace CoopNpcGaze
