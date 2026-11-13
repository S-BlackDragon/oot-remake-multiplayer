// Puppets (coop_puppet.h). No emulator code.
#include "coop_puppet.h"

#include "coop_check_visual.h"

#include "coop_camera.h"
#include "coop_colors.h"
#include "coop_carry.h"  // ORM ocarina-mano: the hand place
#include "coop_ocarina.h"
#include "coop_detalles.h"  // ORM
#include "orm_ocarina_sound.h"  // ORM: the others' notes and songs at their puppets
#include "coop_perf.h"  // ORM: LICHII_ORM_PERF
#include "coop_pose.h"
#include "orm_ride.h"  // ORM: moving floors that carry a partner
#include "coop_switch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <mutex>

#include "coop/host/epona.hpp"  // ORM D-R19: riders on a horse
#include "coop/host/scene.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop/presence/presence.hpp"

namespace CoopPuppet {

// ORM orm/pvp: the lock-on variant's suffix and tally (coop_puppet.h).
namespace {
std::mutex g_suffix_mutex;
std::string g_suffix;
std::atomic<std::uint32_t> g_locks_made{0};
std::atomic<std::uint32_t> g_locks_failed{0};
constexpr std::string_view kLockSuffix = "_Lock";

bool IsLockActor(const std::string& name) {
    return name.size() > kLockSuffix.size() && name.compare(name.size() - kLockSuffix.size(), kLockSuffix.size(),
                                                            kLockSuffix) == 0;
}
void NoteLock(const std::string& name, bool made) {
    if (IsLockActor(name)) {
        (made ? g_locks_made : g_locks_failed).fetch_add(1);
    }
}
} // namespace
namespace {

// The puppet plays the other player's Ocarina notes (issue 152) only with
// LICHII_COOP_OCARINA_SOUND=1: off by default until the game's own note player
// is found (the notes are in the Ocarina.bars bank, not an SLink user, 2026-11-12).
bool OcarinaSoundOn() {
    static const bool on = [] {
        const char* v = std::getenv("LICHII_COOP_OCARINA_SOUND");
        return v != nullptr && std::string_view(v) == "1";
    }();
    return on;
}


template <typename T>
bool ReadValue(Guest& guest, std::uint64_t address, T& out) {
    return guest.Read(address, &out, sizeof(T));
}

template <typename T>
bool WriteValue(Guest& guest, std::uint64_t address, const T& value) {
    return guest.Write(address, &value, sizeof(T));
}

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string VecText(const Vec3& v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]);
    return buf;
}

bool Gone(std::uint32_t state) {
    return state >= 7 && state <= 11;  // as 0x37cca0
}

} // namespace

Mat3 QuatToMatrix(const Quat& q) {
    const double n = std::sqrt(double(q[0]) * q[0] + double(q[1]) * q[1] + double(q[2]) * q[2] +
                               double(q[3]) * q[3]);
    if (!(n > 1e-6) || !std::isfinite(n)) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    const double x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    return {
        float(1 - 2 * (y * y + z * z)), float(2 * (x * y - z * w)),     float(2 * (x * z + y * w)),
        float(2 * (x * y + z * w)),     float(1 - 2 * (x * x + z * z)), float(2 * (y * z - x * w)),
        float(2 * (x * z - y * w)),     float(2 * (y * z + x * w)),     float(1 - 2 * (x * x + y * y)),
    };
}

const char* PhaseText(Phase phase) {
    switch (phase) {
    case Phase::no_block:
        return "no block";
    case Phase::allocating:
        return "allocating";
    case Phase::idle:
        return "idle";
    case Phase::creating:
        return "creating";
    case Phase::live:
        return "live";
    case Phase::deleting:
        return "deleting";
    }
    return "?";
}

const char* ShowText(Show show) {
    switch (show) {
    case Show::show:
        return "show";
    case Show::hold:
        return "hold";
    case Show::hide:
        return "hide";
    }
    return "?";
}

Director::Director(Log log, bool verbose, bool anim)
    : log_(std::move(log)), verbose_(verbose), anim_(anim) {}

void Director::SetInput(Input input) {
    std::lock_guard lock(input_mutex_);
    next_input_ = std::move(input);
}

void Director::Say(const std::string& text) const {
    if (log_) {
        log_("coop: puppet: " + text);
    }
}

std::vector<Director::SlotView> Director::Slots() const {
    std::vector<SlotView> out;
    for (const auto& s : slots_) {
        out.push_back({s.number, s.phase, s.block, s.actor, s.actor_name, s.anim_name});
    }
    return out;
}

const Target* Director::Wanted(std::uint8_t number) const {
    for (const auto& t : input_.targets) {
        if (t.number == number) {
            return &t;
        }
    }
    return nullptr;
}

Director::Slot& Director::SlotFor(std::uint8_t number) {
    for (auto& s : slots_) {
        if (s.number == number) {
            return s;
        }
    }
    Slot s;
    s.number = number;
    slots_.push_back(s);
    return slots_.back();
}

std::optional<Call> Director::Step(Guest& guest, bool after_call, std::uint64_t result,
                                   std::uint64_t now_ms) {
    // ORM (coop_perf.h, LICHII_ORM_PERF): the Link puppets' director only
    // (the fairies' and the horse's are svc callers of their own order).
    CoopPerf::Lap lap;
    if (!after_call) {
        NewFrame(guest, now_ms);
        if (pose_copy_) lap.Mark(CoopPerf::Part::puppet_frame);
        PoseFrame(guest, now_ms);
        if (pose_copy_) lap.Mark(CoopPerf::Part::puppet_pose);
        FollowHeads(guest);
        if (pose_copy_) lap.Mark(CoopPerf::Part::puppet_heads);
        OcarinaBelt(guest);
        if (pose_copy_) lap.Mark(CoopPerf::Part::puppet_belt);
    } else if (pending_ != Pending::none) {
        Finish(guest, result, now_ms);
    }
    const CoopPerf::Scope calls_scope(pose_copy_ ? CoopPerf::Part::puppet_calls : CoopPerf::Part::kCount);
    if (input_.main_base == 0 || calls_ >= kMaxCallsPerFrame) {
        return std::nullopt;
    }
    for (const auto& t : input_.targets) {
        SlotFor(t.number);
    }
    for (auto& slot : slots_) {
        if (auto call = Next(slot, guest, now_ms)) {
            pending_slot_ = slot.number;
            ++calls_;
            return call;
        }
    }
    return std::nullopt;
}

// Once a frame, the full-body pose copy (coop_pose.cpp): our skeleton is
// read and each live puppet's is written.
void Director::PoseFrame(Guest& guest, std::uint64_t now_ms) {
    posed_ = 0;
    if (!pose_copy_) {
        return;
    }
    std::vector<std::pair<std::uint8_t, std::uint64_t>> puppets;
    for (const auto& slot : slots_) {
        if (slot.phase == Phase::live && slot.actor != 0) {
            puppets.emplace_back(slot.number, slot.actor);
        }
    }
    // ORM smoothing: the body poses shown for the time the positions are.
    std::uint64_t render_us = 0;
    if (smooth_) {
        const std::uint64_t server = ServerNow(now_ms);
        const std::uint64_t delay = smooth_delay_us_.load();
        render_us = server > delay ? server - delay : 0;
    }
    posed_ = CoopPose::OnCalcFrame(guest, input_.main_base, puppets, now_ms, render_us);
}

// ORM (coop_mascaras.h): after the pose copy wrote this frame's body, each
// live puppet's mask or lens takes its place and its head bones.
void Director::FollowHeads(Guest& guest) {
    if (!head_follow_ || input_.main_base == 0) {
        heads_.Clear();
        return;
    }
    for (const auto& slot : slots_) {
        if (slot.phase == Phase::live && slot.actor != 0) {
            heads_.Follow(guest, slot.number, slot.actor, [this](const std::string& s) { Say(s); });
        } else {
            heads_.Forget(slot.number);
        }
    }
}

namespace {
// The actor of an equipment link (a slot's or an armor link's holder).
std::uint64_t LinkActor(Guest& guest, std::uint64_t link) {
    std::uint64_t holder = 0;
    std::uint64_t piece = 0;
    if (!ReadValue(guest, link + addr::kEquipSlotHolder, holder) || holder == 0 ||
        !ReadValue(guest, holder + addr::kHolderActor, piece)) {
        return 0;
    }
    return piece;
}

// An actor's name (+0x260, as CoopFairy::addr::kActorName).
std::string PieceName(Guest& guest, std::uint64_t actor) {
    std::uint64_t text = 0;
    std::array<char, 65> name{};
    if (actor == 0 || !ReadValue(guest, actor + 0x260, text) || text == 0 || !guest.Read(text, name.data(), 64)) {
        return {};
    }
    return std::string(name.data(), strnlen(name.data(), 64));
}

// ORM review (REVISION_FINAL.md "Visual"): whether `piece` is still in one of
// `puppet`'s armor links (not emptied) and not gone, read again each frame
// before a parked belt Ocarina is written: the puppet made again or deleted
// (a scene change, a remake), or its belt piece swapped, leaves an address
// that is no longer that piece.
bool StillBelt(Guest& guest, std::uint64_t puppet, std::uint64_t piece) {
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    if (puppet == 0 || piece == 0 || !ReadValue(guest, puppet + addr::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
        return false;
    }
    for (std::uint32_t a = 0; a < addr::kArmorLinkCount; ++a) {
        const std::uint64_t link = eu + addr::kArmorLinks + addr::kEquipSlotStride * a;
        std::uint32_t id = 0xFFFFFFFFu;
        if (!ReadValue(guest, link + addr::kEquipSlotId, id) || id == 0xFFFFFFFFu || LinkActor(guest, link) != piece) {
            continue;
        }
        std::uint32_t state = 0;
        return ReadValue(guest, piece + addr::kActorState, state) && !Gone(state);
    }
    return false;
}
}  // namespace

// ORM (docs/SONDAS.md, 2026-11-12): the remake takes Link's own Ocarina from the belt to his hand
// with its "DemoOcarina" command (the effects rules play it on the puppet under the copied pose).
// While a puppet's player plays freely, its Ocarina piece (the armor link named as its player's
// Ocarina) is read kHandJudgeFrames later: up in the hand, "check: ocarina hand ok"; still on
// the belt, "fallback" (the command did not play on the puppet, or its binding did not follow).
// Once per play; a new play judges again.
void Director::OcarinaHandCheck(Guest& guest, Slot& slot, const Target* t) {
    const bool playing = t != nullptr && t->show == Show::show && t->anim &&
                         coop::host::activity_of(t->anim->name) == coop::host::Activity::ocarina &&
                         t->gear[kGearOcarina] && t->gear[kGearOcarina]->starts_with("Item_Ocarina_");
    if (!playing) {
        slot.hand_frames = 0;
        slot.hand_judged = false;
        return;
    }
    if (slot.hand_judged || ++slot.hand_frames < kHandJudgeFrames) {
        return;
    }
    slot.hand_judged = true;
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    Vec3 at{};
    if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0 ||
        !ReadValue(guest, slot.actor + addr::kActorPosition, at)) {
        return;
    }
    const std::string who = "P" + std::to_string(slot.number);
    for (std::uint32_t a = 0; a < addr::kArmorLinkCount; ++a) {
        const std::uint64_t piece = LinkActor(guest, eu + addr::kArmorLinks + addr::kEquipSlotStride * a);
        Vec3 piece_at{};
        if (piece == 0 || PieceName(guest, piece) != *t->gear[kGearOcarina] ||
            !ReadValue(guest, piece + addr::kActorPosition, piece_at)) {
            continue;
        }
        char up[32];
        std::snprintf(up, sizeof(up), "%.2f", static_cast<double>(piece_at[1] - at[1]));
        if (piece_at[1] - at[1] > kHandUp) {
            CoopVCheck::Ok("ocarina hand", who + "'s own Ocarina is in its hand while " + who + " plays ('" +
                                               t->anim->name + "', up " + up + ")");
        } else {
            CoopVCheck::Fallback("ocarina hand", who + "'s own Ocarina stays on its belt while " + who +
                                                     " plays ('" + t->anim->name + "', up " + up +
                                                     "): only the sub weapon one can show it in the hand");
        }
        return;
    }
    Say(who + " ocarina hand: no piece named '" + *t->gear[kGearOcarina] + "' on the puppet: not judged");
}

namespace {
// The player's own Ocarina while he plays (the ocarina hand probe, sondas12 and sondas13, a child Link
// playing 'DemoOcarina'): in the frame of skeleton matrix 0 read as CoopCarry reads it, the same two
// runs to the millimetre.
const CoopCarry::Frame kOcarinaHeld{{-0.167f, 0.740f, -0.020f},
                                    {0.223f, 0.633f, -0.741f, 0.276f, 0.688f, 0.671f, 0.935f, -0.354f, -0.021f}};
}  // namespace

bool Director::OcarinaHandPlace(Guest& guest, Slot& slot, const Target* t) {
    const bool playing = t != nullptr && t->show == Show::show && t->anim &&
                         coop::host::activity_of(t->anim->name) == coop::host::Activity::ocarina &&
                         t->gear[kGearOcarina] && t->gear[kGearOcarina]->starts_with("Item_Ocarina_");
    const auto actor = slot.actor != 0 ? CoopCarry::ReadActor(guest, slot.actor) : std::nullopt;
    if (!playing || !actor) {
        if (slot.hand_piece != 0 && actor && StillBelt(guest, slot.actor, slot.hand_piece)) {
            // Put away: back where it was on the puppet (its binding takes over).
            const Vec3 back{actor->p[0] + slot.hand_back[0], actor->p[1] + slot.hand_back[1],
                            actor->p[2] + slot.hand_back[2]};
            guest.Write(slot.hand_piece + addr::kActorPosition, back.data(), sizeof(back));
            guest.Write(slot.hand_piece + addr::kActorPositionRequest, back.data(), sizeof(back));
        }
        slot.hand_piece = 0;
        return false;
    }
    std::uint64_t piece = slot.hand_piece;
    if (piece != 0 && !StillBelt(guest, slot.actor, piece)) {
        piece = 0;
    }
    if (piece == 0) {
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
            !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
            return false;
        }
        for (std::uint32_t a = 0; a < addr::kArmorLinkCount && piece == 0; ++a) {
            const std::uint64_t p = LinkActor(guest, eu + addr::kArmorLinks + addr::kEquipSlotStride * a);
            std::uint32_t state = 0;
            if (p != 0 && ReadValue(guest, p + addr::kActorState, state) && !Gone(state) &&
                PieceName(guest, p) == *t->gear[kGearOcarina]) {
                piece = p;
            }
        }
        Vec3 at{};
        if (piece == 0 || !ReadValue(guest, piece + addr::kActorPosition, at)) {
            return false;  // not made yet
        }
        slot.hand_back = {at[0] - actor->p[0], at[1] - actor->p[1], at[2] - actor->p[2]};
    }
    std::optional<CoopCarry::Frame> anchor;
    if (const auto skeleton = CoopCarry::FindSkeleton(guest, slot.actor)) {
        anchor = CoopCarry::ReadBone(guest, *skeleton, *actor, 0);
    }
    if (slot.hand_piece != piece) {
        Say("P" + std::to_string(slot.number) + " ocarina hand: its own Ocarina put in its hands each frame while it "
            "plays (" + (anchor ? std::string("in skeleton matrix 0's frame") : std::string("in the actor frame")) +
            ", as the player's was measured)");
    }
    slot.hand_piece = piece;
    const CoopCarry::Frame f = CoopCarry::Compose(anchor.value_or(*actor), kOcarinaHeld);
    guest.Write(piece + addr::kActorPosition, f.p.data(), sizeof(f.p));
    guest.Write(piece + addr::kActorPositionRequest, f.p.data(), sizeof(f.p));
    guest.Write(piece + addr::kActorRotation, f.r.data(), sizeof(f.r));
    guest.Write(piece + addr::kActorRotationRequest, f.r.data(), sizeof(f.r));
    return true;
}

void Director::OcarinaBelt(Guest& guest) {
    if (input_.main_base == 0) {
        return;
    }
    for (auto& slot : slots_) {
        const Target* t = slot.phase == Phase::live && slot.actor != 0 ? Wanted(slot.number) : nullptr;
        const bool placed_in_hand = OcarinaHandPlace(guest, slot, t);
        OcarinaHandCheck(guest, slot, t);
        if (placed_in_hand) {
            continue;  // its belt Ocarina is the one in the hand: nothing to park
        }
        // The Ocarina in a carried slot, drawn: OcarinaInHand's doing.
        std::optional<int> hand;
        if (t != nullptr && t->gear[kGearOcarina] && t->gear[kGearOcarina]->starts_with("Item_Ocarina_")) {
            for (std::size_t i = 0; i < kGearCount; ++i) {
                if (i != kGearOcarina && GearIsCarried(i) && kGearSlots[i] >= 0 && t->gear[i] == t->gear[kGearOcarina] &&
                    ((t->drawn.value_or(0) >> kGearSlots[i]) & 1u) != 0) {
                    hand = kGearSlots[i];
                }
            }
        }
        Vec3 at{};
        const bool placed = slot.actor != 0 && ReadValue(guest, slot.actor + addr::kActorPosition, at);
        if (slot.belt_parked != 0 && !StillBelt(guest, slot.actor, slot.belt_parked)) {
            // Not that puppet's belt piece any more: forgotten, never written.
            slot.belt_parked = 0;
            slot.belt_frames = 0;
        }
        if (!hand || belt_park_failed_ || !placed) {
            if (slot.belt_parked != 0 && placed) {
                // Put away: back where it was on the puppet (its binding takes over).
                const Vec3 back{at[0] + slot.belt_offset[0], at[1] + slot.belt_offset[1], at[2] + slot.belt_offset[2]};
                guest.Write(slot.belt_parked + addr::kActorPosition, back.data(), sizeof(back));
                guest.Write(slot.belt_parked + addr::kActorPositionRequest, back.data(), sizeof(back));
            }
            slot.belt_parked = 0;
            slot.belt_frames = 0;
            continue;
        }
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
            !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
            continue;
        }
        const std::uint64_t in_hand =
            LinkActor(guest, eu + addr::kEquipSlots + addr::kEquipSlotStride * static_cast<std::uint32_t>(*hand));
        std::uint64_t belt = slot.belt_parked;
        if (belt == 0) {
            for (std::uint32_t a = 0; a < addr::kArmorLinkCount && belt == 0; ++a) {
                const std::uint64_t piece = LinkActor(guest, eu + addr::kArmorLinks + addr::kEquipSlotStride * a);
                std::uint32_t state = 0;
                if (piece != 0 && piece != in_hand && ReadValue(guest, piece + addr::kActorState, state) &&
                    !Gone(state) && PieceName(guest, piece) == *t->gear[kGearOcarina]) {
                    belt = piece;
                }
            }
        }
        Vec3 belt_at{};
        Vec3 hand_at{};
        if (belt == 0 || in_hand == 0 || !ReadValue(guest, belt + addr::kActorPosition, belt_at) ||
            !ReadValue(guest, in_hand + addr::kActorPosition, hand_at)) {
            continue;  // not both made yet
        }
        if (slot.belt_parked == 0) {
            const float apart = std::hypot(belt_at[0] - hand_at[0], belt_at[1] - hand_at[1], belt_at[2] - hand_at[2]);
            if (!(apart > kBeltApart)) {
                continue;  // one place: one Ocarina shows
            }
            slot.belt_parked = belt;
            slot.belt_offset = {belt_at[0] - at[0], belt_at[1] - at[1], belt_at[2] - at[2]};
            slot.belt_frames = 0;
        } else if (++slot.belt_frames == kBeltJudgeFrames) {
            // Did it stay parked since the last frame's write?
            const bool held = belt_at[1] < at[1] - 0.5f * kHideDrop;
            if (held) {
                CoopVCheck::Ok("ocarina belt", "P" + std::to_string(slot.number) + "'s belt Ocarina stays parked out "
                                               "of sight while the one in the hand shows: one Ocarina");
            } else {
                CoopVCheck::Fallback("ocarina belt",
                                     "P" + std::to_string(slot.number) + "'s belt Ocarina is put back on the belt "
                                     "by the game each frame: two Ocarinas show while playing (as before)");
                belt_park_failed_ = true;
                slot.belt_parked = 0;
                continue;
            }
        }
        const Vec3 down{at[0] + slot.belt_offset[0], at[1] + slot.belt_offset[1] - kHideDrop,
                        at[2] + slot.belt_offset[2]};
        guest.Write(belt + addr::kActorPosition, down.data(), sizeof(down));
        guest.Write(belt + addr::kActorPositionRequest, down.data(), sizeof(down));
    }
}

std::uint64_t Director::ServerNow(std::uint64_t now_ms) const {
    if (input_.server_now_us == 0) {
        return 0;
    }
    return input_.server_now_us + (now_ms - std::min(now_ms, input_.server_at_ms)) * 1000;
}

// ORM smoothing (SetSmoothing): the puppet at the server time of this calc
// frame, not of the last present (the two threads do not run in step, so the
// plan's position repeats on one frame and skips on the next), and the
// sender's command of that time for the AS fallback.
void Director::Smooth(Slot& slot, const Target& target, std::uint64_t now_ms, Vec3& at,
                      Mat3& rot) {
    slot.smooth_anim.reset();
    if (!smooth_ || slot.number == 0) {
        return;
    }
    const std::uint64_t server = ServerNow(now_ms);
    if (server == 0) {
        return;
    }
    const auto p = smooth_(slot.number, server);
    if (!p) {
        return;
    }
    at = {p->position[0] + target.offset[0], p->position[1] + target.offset[1],
          p->position[2] + target.offset[2]};
    if (p->rotation) {
        rot = QuatToMatrix(*p->rotation);
    }
    if (p->animation) {
        slot.smooth_anim = Anim{p->animation->name, p->animation->frame, p->animation->rate};
        slot.smooth_serial = p->animation_sequence + 1;  // 0 is "none yet"
    }
    if (anim_ && p->gait != slot.gait) {
        slot.gait = p->gait;
        char speed[32] = "-";
        if (p->sender_speed) {
            std::snprintf(speed, sizeof(speed), "%.2f", static_cast<double>(*p->sender_speed));
        }
        Say("P" + std::to_string(slot.number) + " gait " + coop::host::to_string(p->gait) +
            " (sender's '" + (p->animation ? p->animation->name : std::string("?")) +
            "', speed " + speed + ", shown " + coop::host::to_string(p->how) + ")");
    }
}

// The reveal (SetRevealGate): a parked puppet shows once it has a fresh
// sample and a first pose on it, or after kRevealTimeoutMs whatever it has.
void Director::Reveal(Slot& slot, const Target& target, std::uint64_t now_ms) {
    const bool fresh = !target.sample_age_ms || *target.sample_age_ms < kFreshSampleMs;
    // A player whose poses come is posed by the pose copy (a whole fresh
    // pose written last frame); one who sends none by its AS command.
    const bool poses = pose_copy_ && CoopPose::Drives(slot.number, now_ms);
    const bool posed = poses ? (slot.number < 32 && ((posed_ >> slot.number) & 1u) != 0)
                             : (!slot.anim_name.empty() && !slot.frame_to_set && slot.sync_next == 0);
    const std::uint64_t waited = now_ms - std::min(now_ms, slot.reveal_since_ms);
    if (!(fresh && posed) && waited < kRevealTimeoutMs) {
        return;
    }
    slot.revealed = true;
    // ORM reveal: drawn at its fresh sample from this frame, not eased there from the place it
    // was parked at (issue 194's ease is for a jump of a puppet one sees).
    slot.smooth.Set(target.position);
    const std::string age =
        target.sample_age_ms ? std::to_string(*target.sample_age_ms) + " ms old" : "age not known";
    Say("P" + std::to_string(slot.number) + " shown after " + std::to_string(waited) + " ms (" +
        slot.reveal_why + "): " +
        (posed ? (poses ? "its pose on it" : "its animation playing") : "no pose yet") +
        ", sample " + age + ", at " + VecText(target.position) +
        (fresh && posed ? std::string() : std::string("; waited the most")));
}

void Director::NewFrame(Guest& guest, std::uint64_t now_ms) {
    ++frames_;
    calls_ = 0;
    if (pending_ != Pending::none) {
        // The cave did not make the call (its own limit): undo the phase.
        Slot& slot = SlotFor(pending_slot_);
        if (pending_ == Pending::malloc) {
            slot.phase = Phase::no_block;
        } else if (pending_ == Pending::create) {
            slot.phase = Phase::idle;
        } else if (pending_ == Pending::take || pending_ == Pending::drop) {
            // Still creating with the flag set: asked again.
        } else {
            slot.phase = Phase::live;
        }
        pending_ = Pending::none;
    }
    {
        std::lock_guard lock(input_mutex_);
        input_ = next_input_;
    }
    if (input_.scene != scene_) {
        scene_ = input_.scene;
        scene_since_ms_ = now_ms;
    }
    std::uint32_t live = 0;  // LiveMask(): rebuilt each frame
    if (input_.main_base == 0) {
        live_mask_.store(0, std::memory_order_relaxed);
        return;
    }
    for (auto& slot : slots_) {
        if (slot.phase != Phase::creating && slot.phase != Phase::live &&
            slot.phase != Phase::deleting) {
            continue;
        }
        if (slot.phase == Phase::creating) {
            Poll(slot, guest, now_ms);
            if (slot.phase != Phase::creating) {
                continue;
            }
        }
        slot.actor = Resolve(slot, guest);
        std::uint32_t state = 0;
        const bool have_state =
            slot.actor != 0 && ReadValue(guest, slot.actor + addr::kActorState, state);
        switch (slot.phase) {
        case Phase::creating:
            if (have_state && state == addr::kStatePresent) {
                Vec3 at{};
                ReadValue(guest, slot.actor + addr::kActorPosition, at);
                Say("P" + std::to_string(slot.number) + " " + slot.actor_name + " spawned after " +
                    std::to_string(now_ms - slot.since_ms) + " ms: request " + Hex(slot.request) +
                    ", actor " + Hex(slot.actor) + ", state 4, at " + VecText(at) +
                    (slot.revealed ? "" : "; parked until it has a pose and a fresh sample"));
                NoteLock(slot.actor_name, true);  // ORM orm/pvp: the ally target's check
                slot.phase = Phase::live;
                slot.since_ms = now_ms;
                slot.reveal_since_ms = now_ms;
                slot.shown = Show::show;  // a new actor stands where it was made
                slot.model_checked = !IsTunicActor(slot.actor_name);
            } else if (now_ms - slot.since_ms > kCreateTimeoutMs) {
                Say("P" + std::to_string(slot.number) + " " + slot.actor_name +
                    " not spawned after " + std::to_string(kCreateTimeoutMs) + " ms (actor " +
                    Hex(slot.actor) + ", state " + (have_state ? std::to_string(state) : "-") +
                    "); trying again in 5 s");
                // Twice for the same actor, and only while no tunic puppet has
                // spawned yet: slow loads in a heavy scene must not cost a
                // working pack its colours.
                if (IsTunicActor(slot.actor_name) && !tunic_proven_ &&
                    ++tunic_timeouts_[slot.actor_name] == 2) {
                    TunicFails(slot.actor_name, "not made twice: the puppet pack has no tunic "
                                                "colours (an older pack? run make-puppet-pack again)");
                }
                NoteLock(slot.actor_name, false);  // ORM orm/pvp
                slot.phase = Phase::idle;
                slot.retry_ms = now_ms + 5000;
                slot.take = slot.drop = false;
            }
            break;
        case Phase::live:
            if (!slot.model_checked && have_state && state == addr::kStatePresent &&
                now_ms - slot.since_ms >= kTunicModelCheckMs) {
                // A tunic puppet is the player's model under another project
                // name (tools/puppet_pack/tunic.py): if the engine did not load
                // it, the actor stands without a Player_ model. Plain then.
                slot.model_checked = true;
                // The name comes through the skeleton path (checked on the child),
                // the model object through +0x260 (seen on the adult too): only
                // neither, or a name other than Player_, counts as a failure.
                const auto model = CoopPose::ModelNameOf(guest, slot.actor);
                const std::uint64_t object = CoopColors::ModelOf(guest, slot.actor);
                if (model ? model->rfind("Player_", 0) == 0 : object != 0) {
                    tunic_proven_ = true;
                    Say("P" + std::to_string(slot.number) + " " + slot.actor_name + ": model " +
                        (model ? *model : "object " + Hex(object) + " (name not read)"));
                } else {
                    TunicFails(slot.actor_name, "spawned with " +
                                                    (model ? "model " + *model : std::string("no model")));
                }
            }
            if (!have_state || Gone(state)) {
                Say("P" + std::to_string(slot.number) + " gone (" +
                    (have_state ? "state " + std::to_string(state) : std::string("no actor")) +
                    ")");
                slot.phase = Phase::idle;
                slot.actor = 0;
                slot.ocarina_hand = {};  // its bind block is freed with it
                slot.taken = 0;
                slot.lost_ms = now_ms;  // a load deletes it before the camera goes
            } else if (const Target* absent = Wanted(slot.number);
                       absent != nullptr && state != addr::kStatePresent &&
                       absent->actor == slot.actor_name && input_.scene == slot.scene) {
                // Neither present nor gone: nothing is written to it, so a
                // puppet parked for its reveal or a hide stays down there.
                // Seen so for kAbsentMs while the plan wants it seen: made
                // again (Next deletes it for slot.remake).
                if (absent->show == Show::hide) {
                    slot.absent_since_ms = 0;
                } else if (slot.absent_since_ms == 0) {
                    slot.absent_since_ms = std::max<std::uint64_t>(now_ms, 1);
                } else if (!slot.remake && now_ms - slot.absent_since_ms >= kAbsentMs) {
                    Say("P" + std::to_string(slot.number) + " not present (state " +
                        std::to_string(state) + ") for " +
                        std::to_string(now_ms - slot.absent_since_ms) + " ms: made again");
                    slot.remake = true;
                }
            } else if (const Target* t = Wanted(slot.number);
                       t != nullptr && state == addr::kStatePresent && t->actor == slot.actor_name &&
                       input_.scene == slot.scene) {
                slot.absent_since_ms = 0;
                const std::string who = "P" + std::to_string(slot.number);
                // Back after a while out of the plan (a reconnect, a stall):
                // the reveal again, not a snap to where they are now.
                if (reveal_gate_ && slot.number != 0 && slot.revealed && slot.missing_ms != 0 &&
                    now_ms - slot.missing_ms >= kBackHideMs) {
                    slot.revealed = false;
                    slot.reveal_since_ms = now_ms;
                    slot.reveal_why = "back";
                    Say(who + " back after " + std::to_string(now_ms - slot.missing_ms) +
                        " ms out of the plan: parked until it has a pose and a fresh sample");
                }
                if (!slot.revealed) {
                    Reveal(slot, *t, now_ms);
                }
                if (t->show != slot.shown) {
                    if (slot.shown == Show::hide) {
                        slot.meter_skip = true;  // issue 194: it was parked, not drawn
                    }
                    Say(who + " " + ShowText(slot.shown) + " -> " + ShowText(t->show) +
                        (*t->why ? std::string(" (") + t->why + ")" : std::string()));
                    slot.shown = t->show;
                }
                if (t->first_sight_m < 0.0f) {
                    slot.first_said = false;  // a new scene: its first sight comes again
                } else if (!slot.first_said) {
                    slot.first_said = true;
                    char m[32];
                    std::snprintf(m, sizeof(m), "%.2f", static_cast<double>(t->first_sight_m));
                    Say(who + " first seen here " + m + " m from our Link" +
                        (t->first_sight_m < Planner::kOverlapNear ? ": on it, drawn aside at once"
                                                                  : ": apart, left on its sample"));
                }
                if (const bool stepped = t->side_step != Vec3{}; stepped != slot.side_said) {
                    slot.side_said = stepped;
                    char step[48];
                    std::snprintf(step, sizeof(step), "%.2f",
                                  static_cast<double>(std::hypot(t->side_step[0], t->side_step[2])));
                    const bool first = std::hypot(t->side_step[0], t->side_step[2]) > Planner::kSideStep + 0.01f;
                    Say(stepped ? who + (first ? " stands on our Link when first seen here (one Continue spot): drawn "
                                               : " stands on our Link after an event: drawn ") + step +
                                      " to its side " + VecText(t->side_step) + ", its sample kept"
                                : who + " drawn on its sample again");
                }
                // While it waits for the reveal it is parked as for a hide,
                // facing the right way, its pose and animation still written.
                const Show show = slot.revealed ? t->show : Show::hide;
                if (show != Show::hide && slot.number < 32) {
                    live |= 1u << slot.number;
                }
                // Issue 194 (coop_snap.h): a held puppet (their cutscene) follows their
                // place with its frame frozen, so it is not left behind and does not jump
                // when the cutscene ends; any jump is eased, a snap is logged.
                if (t->color && tint_ > 0.0f) {
                    Tint(slot, guest, *t->color);  // LICHII_COOP_PUPPET_TINT only
                }
                Vec3 at = t->position;
                Mat3 rot = t->rotation;
                if (show != Show::hold) {
                    Smooth(slot, *t, now_ms, at, rot);  // ORM: as planned without it
                }
                if (show == Show::show || show == Show::hold) {
                    if (show == Show::show) {
                        // ORM efectos2: the damage flash blinks it, drawn under
                        // its spot on every other CoopEffects::kBlinkMs.
                        if (CoopEffects::FlashHidden(t->flash_since_ms, now_ms)) {
                            at[1] -= kHideDrop;
                        }
                        // ORM bodies: drawn aside last pass? Its actor read back against it.
                        if (slot.aside_drawn) {
                            Vec3 read{};
                            if (ReadValue(guest, slot.actor + addr::kActorPosition, read)) {
                                if (const std::string line = read_back_.judge(slot.number, *slot.aside_drawn, read);
                                    !line.empty()) {
                                    Say(line);
                                }
                            }
                        }
                    }
                    // Issue 194's draw (eased jumps, the snap meter) at ORM's smoothed place.
                    Target drawn = *t;
                    drawn.position = at;
                    drawn.rotation = rot;
                    drawn.show = show;
                    DrawAt(slot, guest, drawn, now_ms);
                    if (show == Show::show && t->aside != Vec3{}) {
                        slot.aside_drawn = at;
                    } else if (slot.aside_drawn) {
                        slot.aside_drawn.reset();
                        read_back_.end(slot.number);
                    }
                } else if (show == Show::hide) {
                    slot.aside_drawn.reset();
                    slot.smooth.Set(at);  // out of sight: comes back where they are
                    slot.meter.Reset();
                    Vec3 parked = at;
                    parked[1] -= kHideDrop;
                    Pose(slot, guest, parked, slot.revealed ? nullptr : &rot, now_ms);
                }
                // Issue 152: the Ocarina in hand while it plays one (coop_ocarina.h),
                // by their own command (free play is DemoOcarina: shown, not held,
                // coop/host/scene.hpp activity_of).
                const std::string their = t->anim ? t->anim->name : std::string();
                if (anim_ && their != slot.their_anim) {
                    Say("P" + std::to_string(slot.number) + " their command '" + their + "'");
                }
                slot.their_anim = their;
                const std::string hand = slot.ocarina_hand.Step(
                    [&guest](std::uint64_t a) -> std::optional<std::uint64_t> {
                        std::uint64_t w = 0;
                        return guest.Read(a, &w, 8) ? std::optional<std::uint64_t>(w) : std::nullopt;
                    },
                    [&guest](std::uint64_t a, std::uint64_t w) { return guest.Write(a, &w, 8); },
                    slot.actor,
                    t->show != Show::hide &&
                        CoopOcarina::HandBind::Playing(their.empty() ? slot.anim_name : their));
                if (!hand.empty()) {
                    Say("P" + std::to_string(slot.number) + " " + hand + " ('" + their + "')");
                }
            }
            break;
        case Phase::deleting:
            if (!have_state || Gone(state) || now_ms - slot.since_ms > 5000) {
                Say("P" + std::to_string(slot.number) + " deleted (" +
                    (have_state ? "state " + std::to_string(state) : std::string("no actor")) +
                    ")");
                slot.phase = Phase::idle;
                slot.actor = 0;
                slot.ocarina_hand = {};  // its bind block is freed with it
                slot.taken = 0;
            }
            break;
        default:
            break;
        }
    }
    live_mask_.store(live, std::memory_order_relaxed);
    Status(guest, now_ms);
}

void Director::Finish(Guest& guest, std::uint64_t result, std::uint64_t now_ms) {
    Slot& slot = SlotFor(pending_slot_);
    const std::string who = "P" + std::to_string(slot.number);
    switch (pending_) {
    case Pending::malloc:
        if (result == 0) {
            Say(who + ": malloc gave nothing; trying again in 5 s");
            slot.phase = Phase::no_block;
            slot.retry_ms = now_ms + 5000;
        } else {
            const std::array<std::uint8_t, block::kSize> zero{};
            guest.Write(result, zero.data(), zero.size());
            slot.block = result;
            slot.phase = Phase::idle;
            Say(who + ": block at " + Hex(result));
        }
        break;
    case Pending::create:
        Say(who + ": create " + slot.actor_name + " requested (returned " +
            Hex(result & 0xffffffff) + "), handle slot " + Hex(slot.handle));
        if ((result & 0xffffffff) != 0) {
            slot.refused = 0;
            break;
        }
        // Refused (seen right after a load deleted the puppet, while the old
        // scene goes): the handle stayed empty and nothing was made, so ask
        // again soon. Should the game have queued a request after all, wait
        // for it as for any create, so there are never two.
        if (std::uint64_t queued = 0; ReadValue(guest, slot.handle, queued) && queued != 0) {
            break;
        }
        ++slot.refused;
        slot.phase = Phase::idle;
        // A tunic actor the game keeps refusing is not in its ActorInfo: an
        // older puppet pack without the colours (Hookshot proof 3: the pairs
        // had the pack from before the tunics, CoopPuppet_Child_Blue refused
        // for good, so no puppet at all). The plain puppet then, as after two
        // create timeouts. Only while no tunic puppet has spawned: refusals
        // during a load must not cost a working pack its colours.
        if (IsTunicActor(slot.actor_name) && !tunic_proven_ && slot.refused == kRefusedTunicFallback) {
            TunicFails(slot.actor_name, "refused " + std::to_string(kRefusedTunicFallback) +
                                            " times: the puppet pack has no tunic colours (an older pack? run "
                                            "make-puppet-pack again)");
            slot.refused = 0;
            slot.retry_ms = now_ms;  // the plain puppet at once
            break;
        }
        if (slot.refused < kRefusedFast) {
            slot.retry_ms = now_ms + kRefusedRetryMs;
            if (slot.refused == 1) {
                Say(who + ": create refused; trying again each second");
            }
        } else {
            slot.retry_ms = now_ms + kRefusedSlowMs;
            if (slot.refused == kRefusedFast) {
                Say(who + ": create refused " + std::to_string(kRefusedFast) +
                    " times; trying again every 5 s");
            }
        }
        break;
    case Pending::take:
        slot.take = false;
        WriteValue(guest, slot.handle, std::uint64_t{0});  // as the game does
        if (result == 0 || !ReadValue(guest, result, slot.vtable)) {
            Say(who + ": request " + Hex(slot.request) + " gave no actor; trying again in 5 s");
            slot.phase = Phase::idle;
            slot.retry_ms = now_ms + 5000;
        } else {
            slot.taken = result;
            Say(who + ": took actor " + Hex(result) + " from request " + Hex(slot.request));
        }
        break;
    case Pending::drop:
        slot.drop = false;
        WriteValue(guest, slot.handle, std::uint64_t{0});
        Say(who + ": request " + Hex(slot.request) + " dropped (state 4); trying again in 5 s");
        slot.phase = Phase::idle;
        slot.retry_ms = now_ms + 5000;
        break;
    case Pending::remove:
        Say(who + ": delete requested (returned " + Hex(result & 0xff) + ")");
        break;
    case Pending::play:
        slot.anim_name = slot.play_name;
        slot.anim_rate = slot.play_rate;
        slot.anim_serial = slot.play_serial;
        slot.frame_to_set = slot.play_frame;
        slot.sync_next = 1;  // then the partial slots, after the frame
        if (anim_) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), " from frame %.1f", slot.play_frame);
            Say(who + " plays '" + slot.play_name + "'" + buf + " (returned " +
                Hex(result & 0xff) + ")");
        }
        // ORM, issue 152: a note or song node of the Ocarina, named in the
        // command its player sent (oot-remake docs/OCARINA.md). Logged
        // always: it is what a run reads to know whether the notes reach
        // us in the command at all; the sound is not played yet.
        if (const auto cue = CoopOcarina::CueOf(slot.play_name)) {
            Say(who + " ocarina " + CoopOcarina::CueText(*cue) + " ('" + slot.play_name + "')");
        }
        // ORM: its note at the puppet (LICHII_COOP_OCARINA_SOUND, orm_ocarina_sound.h);
        // the Link puppets' director only (pose_copy_), not the fairies'.
        if (pose_copy_) {
            OrmOcarinaSound::ThePlayer().OnCommand(input_.main_base, slot.number, slot.actor,
                                                   slot.play_name, now_ms);
        }
        break;
    case Pending::frame:
        slot.frame_to_set.reset();
        break;
    case Pending::sync:
        if (anim_) {
            Say(who + " slot " + std::to_string(slot.sync_next) + " synced to slot 0 ('" +
                slot.anim_name + "')");
        }
        ++slot.sync_next;
        break;
    case Pending::equip:
        slot.worn[slot.equip_index] = slot.equip_name;
        if (const int s = kGearSlots[slot.equip_index]; s >= 0) {
            // A new piece is worn put away, whatever the old one was.
            slot.in_hand = static_cast<std::uint8_t>(slot.in_hand & ~(1u << s));
            slot.hold_known = static_cast<std::uint8_t>(slot.hold_known & ~(1u << s));
        }
        Say(who + " wears " + kGearNames[slot.equip_index] + " '" + slot.equip_name +
            "' (returned " + Hex(result & 0xff) + ")");
        break;
    case Pending::unequip: {
        const std::size_t i = slot.equip_index;
        // ORM: a piece it put on by itself from our flags is not in `worn`.
        const bool stray = slot.worn[i].empty();
        const std::string name = stray ? slot.unequip_name : slot.worn[i];
        const char* whose = stray ? " (ours, not its player's)" : "";
        if ((result & 0xff) != 0) {
            Say(who + " takes off " + kGearNames[i] + " '" + name + "'" + whose + " (returned " +
                Hex(result & 0xff) + ")");
            slot.worn[i].clear();
            slot.off_since_ms[i] = 0;
        } else {
            // Refused: the next Equip makes the puppet again without it (a
            // stray piece is left on: made again, it would put it on again).
            Say(who + " takes off " + kGearNames[i] + " '" + name + "'" + whose +
                ": the game's call returned 0" + (stray ? "; left on" : ""));
            slot.unequip_refused[i] = true;
        }
        break;
    }
    case Pending::hold: {
        // Taken when the slot's byte shows the state asked for.
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        std::uint8_t bits = 0;
        const bool read = ReadValue(guest, slot.actor + addr::kActorParts, parts) && parts != 0 &&
                          ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) && eu != 0 &&
                          ReadValue(guest, eu + addr::kEquipSlotBits + slot.hold_slot, bits);
        const std::uint8_t want = slot.hold_in_hand ? addr::kEquipBitInHand : addr::kEquipBitAway;
        const auto bit = static_cast<std::uint8_t>(1u << slot.hold_slot);
        if (read && (bits & want) != 0) {
            slot.hold_known = static_cast<std::uint8_t>(slot.hold_known | bit);
            slot.in_hand = static_cast<std::uint8_t>(slot.hold_in_hand ? (slot.in_hand | bit)
                                                                       : (slot.in_hand & ~bit));
            Say(who + " equipment slot " + std::to_string(slot.hold_slot) +
                (slot.hold_in_hand ? " in hand" : " put away"));
        } else {
            slot.hold_retry_ms = now_ms + 250;  // the piece is not ready: ask again
            if (anim_) {
                Say(who + " equipment slot " + std::to_string(slot.hold_slot) +
                    (slot.hold_in_hand ? " in hand" : " put away") + " not taken (byte " +
                    Hex(bits) + ")");
            }
        }
        break;
    }
    case Pending::move:
        break;
    case Pending::sound:
        CoopPuppetSound::Finish(slot.sound, result, who, [this](const std::string& line) { Say(line); });
        break;
    case Pending::none:
        break;
    }
    pending_ = Pending::none;
}

std::optional<Call> Director::Next(Slot& slot, Guest& guest, std::uint64_t now_ms) {
    const Target* t = Wanted(slot.number);
    if (t == nullptr) {
        slot.wanted_actor.clear();
    } else if (t->actor != slot.wanted_actor) {
        slot.wanted_actor = t->actor;
        slot.wanted_since_ms = now_ms;
    }
    switch (slot.phase) {
    case Phase::no_block:
        if (t != nullptr && !t->keep_only && now_ms >= slot.retry_ms) {
            Call c;
            c.function = input_.main_base + addr::kMalloc;
            c.x[0] = block::kSize;
            slot.phase = Phase::allocating;
            pending_ = Pending::malloc;
            return c;
        }
        return std::nullopt;
    case Phase::idle:
        // A failed create waits 5 s before the same actor is tried again; a
        // different one (the player's age was read wrong while loading) at once.
        if (t != nullptr && !t->keep_only && slot.block != 0 && !input_.scene.empty() &&
            (now_ms >= slot.retry_ms || t->actor != slot.actor_name)) {
            if (now_ms - slot.wanted_since_ms < input_.settle_ms) {
                return std::nullopt;  // the age may still change (Input::settle_ms)
            }
            if (!Drawn(slot, now_ms)) {
                if (!slot.wait_said) {
                    slot.wait_said = true;
                    Say("P" + std::to_string(slot.number) + ": waiting for the game to draw");
                }
                return std::nullopt;
            }
            auto c = Create(slot, *t, guest);
            if (c) {
                slot.since_ms = now_ms;
                slot.wait_said = false;
            }
            return c;
        }
        return std::nullopt;
    case Phase::creating:
        if (slot.take || slot.drop) {
            Call c;
            c.x[0] = slot.request;
            if (slot.take) {
                c.function = input_.main_base + addr::kTakeCreated;
                c.x[1] = 1;
                pending_ = Pending::take;
            } else {
                c.function = input_.main_base + addr::kDropCreate;
                pending_ = Pending::drop;
            }
            return c;
        }
        return std::nullopt;
    case Phase::live:
        if (input_.scene.empty() || slot.actor == 0) {
            return std::nullopt;  // loading: wait and see what is left
        }
        if (t != nullptr) {
            slot.missing_ms = 0;
        } else if (input_.scene == slot.scene) {
            if (slot.missing_ms == 0) {
                slot.missing_ms = now_ms;
            }
            if (now_ms - slot.missing_ms < kLeaveGraceMs) {
                return std::nullopt;  // maybe a hiccup: keep it a moment
            }
        }
        if (t == nullptr || t->actor != slot.actor_name || input_.scene != slot.scene ||
            slot.remake) {
            slot.missing_ms = 0;
            Call c;
            c.function = input_.main_base + addr::kRequestDelete;
            c.x[0] = slot.actor;
            c.x[1] = 0;
            slot.phase = Phase::deleting;
            slot.since_ms = now_ms;
            pending_ = Pending::remove;
            return c;
        }
        // The player's Ocarina notes (issue 152), and with LICHII_COOP_DEBUG_ANIM
        // what the puppet's sound user plays (issue 131).
        // Not only in show: A playing is an event on A, so B holds the puppet (still,
        // seen) the whole song; the notes must sound then too.
        if (t->show != Show::hide) {
            const std::string who = "P" + std::to_string(slot.number);
            const auto say = [this](const std::string& line) { Say(line); };
            if (anim_) {
                CoopPuppetSound::LogPlaying(slot.sound, guest, slot.actor, now_ms, who, say);
            }
            if (!OcarinaSoundOn()) {
                // notes are received and logged by the receiver, not played
            } else if (auto c = CoopPuppetSound::Next(slot.sound, t->ocarina, guest,
                                                      input_.main_base, slot.actor, slot.block,
                                                      now_ms, who, say)) {
                pending_ = Pending::sound;
                return c;
            }
        }
        // Parked for the reveal: no gear yet (a piece made under the floor
        // stays there); it is worn in the frame the puppet shows.
        if (slot.revealed) {
            if (auto c = Equip(slot, *t, guest, now_ms)) {
                return c;
            }
            if (auto c = Hold(slot, *t, guest, now_ms)) {
                return c;
            }
        }
        if (matrix_call_ && t->show != Show::hold) {
            if (auto c = Move(slot, guest)) {
                return c;
            }
        }
        if (pose_copy_ && CoopPose::Drives(slot.number, now_ms)) {
            // The pose copy moves it; ORM efectos2: an allowed command plays
            // as well, for its effect and sound events (coop_effects.h).
            return t->effects ? PosedAnimate(slot, *t, guest) : std::nullopt;
        }
        return Animate(slot, *t, guest);
    default:
        return std::nullopt;
    }
}

// Whether a create now would be drawn. Made while the game loads a scene
// (the scene is readable, the camera is gone), a puppet reaches state 4 but
// never shows until it is made again (Caleb's two-player run, 2026-11-09:
// B created P1 at 37.1 s, its loading screen ran 36.8 to 40.2 s).
bool Director::Drawn(const Slot& slot, std::uint64_t now_ms) const {
    if (!input_.camera) {
        return true;  // nothing to go by
    }
    const std::uint64_t since = input_.drawn_since_ms;
    if (since == 0 || now_ms < since || now_ms - since < kDrawnMs) {
        return false;
    }
    const std::uint64_t change = std::max(scene_since_ms_, slot.lost_ms);
    return since > change || now_ms - change >= kSteadyMs;
}

std::optional<Call> Director::Create(Slot& slot, const Target& target, Guest& guest) {
    const std::uint64_t base = input_.main_base;
    std::uint64_t manager = 0;
    std::uint64_t create_default = 0;
    if (!ReadValue(guest, base + addr::kActorManagerPtr, manager) || manager == 0 ||
        !ReadValue(guest, base + addr::kGotCreateDefault, create_default)) {
        return std::nullopt;  // not set up yet
    }
    const std::uint64_t b = slot.block;

    // The create argument as SeqExecuteCreateActor (0x32bca10) fills it in
    // the middle of play, with our position and rotation (docs/puppet.md).
    // The preload walker's form (+0x48 = 0x10001, +0x98 = 0) is only taken
    // up while a scene loads: made later, its request stays in state 2.
    std::array<std::uint8_t, 0x100> arg{};
    const Vec3 scale{1.0f, 1.0f, 1.0f};
    // With the reveal, made parked under its spot: its first frames (in its
    // default T-pose, at a sample that may be old) are not seen.
    const bool parked = reveal_gate_ && slot.number != 0;
    Vec3 at = target.position;
    if (parked || create_parked_) {
        at[1] -= kHideDrop;  // ORM choque (SetCreateParked): a body never made on anyone
    }
    std::memcpy(arg.data() + 0x00, at.data(), 12);
    std::memcpy(arg.data() + 0x0c, target.rotation.data(), 36);
    std::memcpy(arg.data() + 0x30, scale.data(), 12);
    std::memcpy(arg.data() + 0x40, &create_default, 8);
    arg[0x98] = 1;
    if (!guest.Write(b + block::kCreateArg, arg.data(), arg.size())) {
        return std::nullopt;
    }

    std::array<char, block::kNameCapacity> text{};
    std::memcpy(text.data(), target.actor.data(),
                std::min<std::size_t>(target.actor.size(), text.size() - 1));
    guest.Write(b + block::kNameText, text.data(), text.size());
    WriteValue(guest, b + block::kName, b + block::kNameText);
    WriteValue(guest, b + block::kName + 8, std::uint64_t{block::kNameCapacity});

    // A fresh handle slot each time, in case the manager still holds the last one.
    slot.handle = b + block::kHandles + 8 * slot.handle_index;
    slot.handle_index = (slot.handle_index + 1) % block::kHandleCount;
    WriteValue(guest, slot.handle, std::uint64_t{0});

    Call c;
    c.function = base + addr::kCreate;
    c.x[0] = manager;
    c.x[1] = b + block::kName;
    c.x[2] = b + block::kCreateArg;
    c.x[3] = slot.handle;
    c.x[4] = 1;
    slot.phase = Phase::creating;
    slot.actor = 0;
    slot.actor_name = target.actor;
    slot.scene = input_.scene;
    slot.request = 0;
    slot.taken = 0;
    slot.vtable = 0;
    slot.take = slot.drop = false;
    slot.anim_name.clear();
    slot.ocarina_hand = {};  // a new puppet may reuse the old address
    slot.their_anim.clear();
    slot.anim_serial = 0;
    slot.anim_rate = 1.0f;
    slot.smooth_anim.reset();
    slot.smooth_serial = 0;
    slot.gait = coop::host::Gait::unknown;
    slot.frame_to_set.reset();
    slot.held_frame.reset();
    slot.smooth.Reset();
    slot.meter.Reset();
    slot.meter_skip = false;
    slot.sync_next = 0;
    slot.worn = {};
    slot.in_hand = 0;
    slot.hold_known = 0;
    slot.hold_retry_ms = 0;
    slot.hold_id = {};
    slot.far_since_ms = {};
    slot.rewear_tries = {};
    slot.remake = false;
    slot.off_since_ms = {};
    slot.unequip_refused = {};
    slot.unequip_name.clear();
    slot.stray_offs = {};
    slot.revealed = !parked;
    slot.reveal_why = "new";
    slot.side_said = false;
    slot.absent_since_ms = 0;
    pending_ = Pending::create;
    return c;
}

namespace {
// The state the equip call is given (issue 91, puppet-looks): 2 (worn put
// away, as the GameData path wears a sword or a tunic) for every piece but
// the mask, which gets 1. Through the GameData path (state 2) the game drew
// no mask even on the player's own Link (wear-masks 20261110-155536), and the
// call keeps the state per kind (eu + 0x12d4 + 8 * kind: {1, state}) for
// when the piece is made. LICHII_COOP_HEAD_STATE=2 goes back to 2.
std::uint64_t EquipState(std::uint32_t kind) {
    static const std::uint64_t head = [] {
        const char* v = std::getenv("LICHII_COOP_HEAD_STATE");
        return v != nullptr && std::string_view(v) == "2" ? std::uint64_t{2} : std::uint64_t{1};
    }();
    return kind == addr::kArmorKindFirst ? head : std::uint64_t{2};
}

// The eight equipment slots of an actor's EquipmentUser, for the debug log:
// for each slot that holds a piece its id, actor, state, place (height over
// the wearer and distance on the ground), the request byte, and the slot
// record's in-hand byte and flags (u32 at +0xa0: "put away" 0xe34c70 sets
// 0x800, "in hand" 0x6b6820 clears it; read from the dump, not run).
std::string SlotsText(Guest& guest, std::uint64_t actor, std::optional<std::uint8_t> wanted) {
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    if (!ReadValue(guest, actor + addr::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
        return "no equipment user";
    }
    Vec3 own{};
    ReadValue(guest, actor + addr::kActorPosition, own);
    std::string text = "eu " + Hex(eu);
    for (std::uint32_t s = 0; s < 8; ++s) {
        const std::uint64_t link = eu + addr::kEquipSlots + addr::kEquipSlotStride * s;
        std::uint32_t id = 0xFFFFFFFFu;
        std::uint64_t holder = 0;
        std::uint64_t piece = 0;
        std::uint32_t state = 0;
        std::uint8_t request = 0xff;
        std::uint8_t hand = 0xff;
        std::uint32_t flags = 0;
        Vec3 at{};
        ReadValue(guest, link + addr::kEquipSlotId, id);
        ReadValue(guest, eu + addr::kEquipSlotBits + s, request);
        if (id == 0xFFFFFFFFu && (request == 0 || request == 0xff)) {
            continue;  // nothing in this slot
        }
        ReadValue(guest, link + addr::kEquipSlotHolder, holder);
        if (holder != 0) {
            ReadValue(guest, holder + addr::kHolderActor, piece);
        }
        if (piece != 0) {
            ReadValue(guest, piece + addr::kActorState, state);
            ReadValue(guest, piece + addr::kActorPosition, at);
        }
        const std::uint64_t record = eu + addr::kEquipRecords + addr::kEquipRecordStride * s;
        ReadValue(guest, record + addr::kEquipRecordInHand, hand);
        ReadValue(guest, record + addr::kEquipRecordFlags, flags);
        char place[64];
        std::snprintf(place, sizeof(place), "up %.2f off %.2f", static_cast<double>(at[1] - own[1]),
                      static_cast<double>(std::hypot(at[0] - own[0], at[2] - own[2])));
        text += "; slot " + std::to_string(s) + ": id " + Hex(id) + " actor " + Hex(piece) +
                " state " + std::to_string(state) + " " + place + " request " + Hex(request) +
                " record " + Hex(hand) + " flags " + Hex(flags);
        if (wanted) {
            text += std::string(" want ") + (((*wanted >> s) & 1u) != 0 ? "hand" : "away");
        }
    }
    // The armor links (mask, tunic, boots, gauntlets and the belt pieces):
    // index, id, actor, its state and place.
    for (std::uint32_t a = 0; a < addr::kArmorLinkCount; ++a) {
        const std::uint64_t link = eu + addr::kArmorLinks + addr::kEquipSlotStride * a;
        std::uint32_t id = 0xFFFFFFFFu;
        std::uint64_t holder = 0;
        std::uint64_t piece = 0;
        std::uint32_t state = 0;
        Vec3 at{};
        if (!ReadValue(guest, link + addr::kEquipSlotId, id) || id == 0xFFFFFFFFu) {
            continue;
        }
        ReadValue(guest, link + addr::kEquipSlotHolder, holder);
        if (holder != 0) {
            ReadValue(guest, holder + addr::kHolderActor, piece);
        }
        if (piece != 0) {
            ReadValue(guest, piece + addr::kActorState, state);
            ReadValue(guest, piece + addr::kActorPosition, at);
        }
        char place[64];
        std::snprintf(place, sizeof(place), "up %.2f off %.2f", static_cast<double>(at[1] - own[1]),
                      static_cast<double>(std::hypot(at[0] - own[0], at[2] - own[2])));
        text += "; armor " + std::to_string(a) + ": id " + Hex(id) + " actor " + Hex(piece) +
                " state " + std::to_string(state) + " " + place;
    }
    // Issue 91 (puppet-looks): the equip call's per-kind words (eu + 0x12d4 +
    // 8 * kind, two u32), to tell how the game wears a real mask from the
    // puppet's (the GameData path asks state 1 or 2 per piece).
    text += "; kinds";
    for (std::uint32_t k = 0; k < 18; ++k) {
        std::array<std::uint32_t, 2> words{};
        if (ReadValue(guest, eu + 0x12d4 + 8 * k, words) && (words[0] != 0 || words[1] != 0)) {
            text += " " + std::to_string(k) + ":" + Hex(words[0]) + "/" + Hex(words[1]);
        }
    }
    return text;
}
}  // namespace

// One equip call for a live puppet whose player's gear differs from what it
// wears, or none. An empty name (nothing in that slot): a carried piece (the
// held item, the tool slots) is only put away, as its player's is; a worn one
// that stays empty for kTakeOffMs is taken off: armor (mask, tunic, boots,
// gauntlets) with the game's own call (addr::kUnequipArmor); a sword or a
// shield, which has no known call, or armor the call refused, by making the
// puppet again without it (slot.remake; issue 91: a mask taken off stayed on).
std::optional<Call> Director::Equip(Slot& slot, const Target& target, Guest& guest,
                                    std::uint64_t now_ms) {
    if (anim_ && now_ms - slot.gear_log_ms >= 2000) {
        slot.gear_log_ms = now_ms;
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        ReadValue(guest, slot.actor + addr::kActorParts, parts);
        if (parts != 0) {
            ReadValue(guest, parts + addr::kPartsEquipmentUser, eu);
        }
        std::string line = "P" + std::to_string(slot.number) + " gear: eu " + Hex(eu);
        for (std::size_t i = 0; i < target.gear.size(); ++i) {
            line += std::string(", ") + kGearNames[i] + " " +
                    (target.gear[i] ? "'" + *target.gear[i] + "'" : std::string("?")) +
                    (slot.worn[i].empty() ? "" : " (worn)");
        }
        Say(line);
        // Every equipment slot as the game has it, and the local player's
        // beside it: what differs between the two is what does not show.
        Say("P" + std::to_string(slot.number) + " slots (puppet " + Hex(slot.actor) +
            "): " + SlotsText(guest, slot.actor, target.drawn));
        if (const std::uint64_t player = CoopPose::FindPlayer(guest, input_.main_base)) {
            Say("P" + std::to_string(slot.number) + " slots (local player " + Hex(player) +
                "): " + SlotsText(guest, player, std::nullopt));
        }
    }
    if (target.show == Show::hide) {
        return std::nullopt;  // parked: a piece made now would stay down there
    }
    // ORM efectos2: not while it blinks (the pieces may read a frame behind
    // the puppet parked for a blink), nor a moment after.
    if (target.show == Show::show && now_ms >= slot.rewear_ms &&
        !CoopEffects::Flashing(target.flash_since_ms, now_ms, 2 * kFarForMs)) {
        LeftBehind(slot, guest, now_ms);
    }
    for (std::size_t i = 0; i < target.gear.size(); ++i) {
        const auto& want = target.gear[i];
        if (want && want->empty() && !slot.worn[i].empty() && !GearIsCarried(i)) {
            // Empty for a moment (a load) is not taken off: kTakeOffMs first.
            if (slot.off_since_ms[i] == 0) {
                slot.off_since_ms[i] = std::max<std::uint64_t>(now_ms, 1);
                continue;
            }
            if (now_ms - slot.off_since_ms[i] < kTakeOffMs) {
                continue;
            }
            if (GearIsArmor(i) && !slot.unequip_refused[i]) {
                std::uint64_t parts = 0;
                std::uint64_t eu = 0;
                if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
                    !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
                    continue;  // not set up (yet): try again next frame, other pieces go on
                }
                slot.equip_index = i;
                Call c;
                c.function = input_.main_base + addr::kUnequipArmor;
                c.x[0] = eu;
                c.x[1] = kGearKinds[i] - addr::kArmorKindFirst;
                c.x[2] = 0;
                pending_ = Pending::unequip;
                return c;
            }
            Say("P" + std::to_string(slot.number) + " takes off " + kGearNames[i] + " '" +
                slot.worn[i] + "': puppet made again without it");
            slot.remake = true;
            return std::nullopt;
        }
        // ORM (coop_detalles.h): armor of ours its player does not wear.
        if (want && want->empty() && slot.worn[i].empty() && GearIsArmor(i)) {
            if (auto c = TakeOffStray(slot, guest, i, now_ms)) {
                return c;
            }
            continue;
        }
        slot.off_since_ms[i] = 0;
        if (!want || want->empty() || *want == slot.worn[i] ||
            want->size() >= block::kGearCapacity) {
            continue;
        }
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
            !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
            return std::nullopt;  // not set up (yet)
        }
        const std::uint64_t text = slot.block + block::kGearText + block::kGearCapacity * i;
        const std::uint64_t cell = slot.block + block::kGearPtr + 8 * i;
        std::array<char, block::kGearCapacity> name{};
        std::memcpy(name.data(), want->data(), want->size());
        if (!guest.Write(text, name.data(), name.size()) || !WriteValue(guest, cell, text)) {
            return std::nullopt;
        }
        slot.equip_index = i;
        slot.equip_name = *want;
        Call c;
        c.function = input_.main_base + addr::kEquip;
        c.x[0] = eu;
        c.x[1] = kGearKinds[i];
        c.x[2] = cell;
        c.x[3] = EquipState(kGearKinds[i]);
        c.x[4] = 0xFFFFFFFF;
        c.x[5] = 1;
        c.x[6] = 0;
        pending_ = Pending::equip;
        return c;
    }
    return std::nullopt;
}

// ORM (coop_detalles.h): the puppet put on a piece of armor by itself from
// our own game's flags, and its player wears nothing of that kind (want ""
// and nothing worn by us). Taken off with the game's own armor take-off once
// it has been there kTakeOffMs, at most CoopDetalles::kStrayTries times.
std::optional<Call> Director::TakeOffStray(Slot& slot, Guest& guest, std::size_t i,
                                           std::uint64_t now_ms) {
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0) {
        slot.off_since_ms[i] = 0;
        return std::nullopt;
    }
    const std::uint32_t index = kGearKinds[i] - addr::kArmorKindFirst;
    const auto stray = CoopDetalles::StrayArmor(guest, input_.main_base, eu, index);
    if (!stray || stray->empty()) {
        slot.off_since_ms[i] = 0;
        return std::nullopt;
    }
    if (slot.unequip_refused[i]) {
        return std::nullopt;  // said once, in Finish
    }
    if (slot.stray_offs[i] >= CoopDetalles::kStrayTries) {
        if (slot.stray_offs[i] == CoopDetalles::kStrayTries) {
            ++slot.stray_offs[i];
            Say("P" + std::to_string(slot.number) + " " + kGearNames[i] + " '" + *stray +
                "' (ours) is back after " + std::to_string(CoopDetalles::kStrayTries) +
                " take-offs: left on");
        }
        return std::nullopt;
    }
    if (slot.off_since_ms[i] == 0) {
        slot.off_since_ms[i] = std::max<std::uint64_t>(now_ms, 1);
        return std::nullopt;
    }
    if (now_ms - slot.off_since_ms[i] < kTakeOffMs) {
        return std::nullopt;
    }
    ++slot.stray_offs[i];
    slot.equip_index = i;
    slot.unequip_name = *stray;
    Call c;
    c.function = input_.main_base + addr::kUnequipArmor;
    c.x[0] = eu;
    c.x[1] = index;
    c.x[2] = 0;
    pending_ = Pending::unequip;
    return c;
}

// A worn sword or shield far from its puppet does not follow it: the game
// made it while the puppet was parked under the floor (Show::hide), or it
// stayed behind when the puppet came back up. It is worn again, so the game
// makes it anew at the puppet (and the in-hand state is set again for the
// new piece, Hold).
void Director::LeftBehind(Slot& slot, Guest& guest, std::uint64_t now_ms) {
    std::uint64_t parts = 0;
    std::uint64_t eu = 0;
    Vec3 own{};
    if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
        !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0 ||
        !ReadValue(guest, slot.actor + addr::kActorPosition, own)) {
        return;
    }
    for (std::size_t i = 0; i < kGearSlots.size(); ++i) {
        const int s = kGearSlots[i];
        if (s < 0 || slot.worn[i].empty()) {
            continue;
        }
        if (GearIsCarried(i) && (slot.in_hand & (1u << s)) == 0) {
            continue;  // put away, the game keeps a held item elsewhere (state 6)
        }
        const std::uint64_t link = eu + addr::kEquipSlots + addr::kEquipSlotStride * s;
        std::uint32_t id = 0xFFFFFFFFu;
        std::uint64_t holder = 0;
        std::uint64_t piece = 0;
        std::uint32_t state = 0;
        Vec3 at{};
        if (!ReadValue(guest, link + addr::kEquipSlotId, id) || id == 0xFFFFFFFFu ||
            !ReadValue(guest, link + addr::kEquipSlotHolder, holder) || holder == 0 ||
            !ReadValue(guest, holder + addr::kHolderActor, piece) || piece == 0 ||
            !ReadValue(guest, piece + addr::kActorState, state) || state != addr::kStatePresent ||
            !ReadValue(guest, piece + addr::kActorPosition, at)) {
            continue;
        }
        const float far = std::sqrt((at[0] - own[0]) * (at[0] - own[0]) +
                                    (at[1] - own[1]) * (at[1] - own[1]) +
                                    (at[2] - own[2]) * (at[2] - own[2]));
        if (!(far > kGearFar)) {
            slot.far_since_ms[i] = 0;
            slot.rewear_tries[i] = 0;  // where it belongs: a later miss may try again
            continue;
        }
        // Far on one reading may be a piece one frame behind a puppet that
        // jumped: it must still be far a moment later.
        if (slot.far_since_ms[i] == 0) {
            slot.far_since_ms[i] = now_ms;
            continue;
        }
        if (now_ms - slot.far_since_ms[i] < kFarForMs) {
            continue;
        }
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(far));
        const std::string what =
            "P" + std::to_string(slot.number) + " " + kGearNames[i] + " '" + slot.worn[i] + "'";
        if (slot.rewear_tries[i] >= kRewearTries) {
            if (slot.rewear_tries[i] == kRewearTries) {
                Say(what + " still left behind (" + text + " away) after " +
                    std::to_string(kRewearTries) + " tries: given up");
                ++slot.rewear_tries[i];
            }
            continue;
        }
        ++slot.rewear_tries[i];
        Say(what + " left behind (" + text + " away): worn again");
        slot.worn[i].clear();
        slot.far_since_ms[i] = 0;
        slot.rewear_ms = now_ms + kRewearMs;
    }
}

// One change of an equipment slot's state for a live puppet: a worn piece
// its player holds in the hand while the puppet's is put away, or the other
// way round. A piece just worn is put away, so only "in hand" is asked then.
std::optional<Call> Director::Hold(Slot& slot, const Target& target, Guest& guest,
                                   std::uint64_t now_ms) {
    if (!target.drawn || now_ms < slot.hold_retry_ms || target.show == Show::hide) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kGearSlots.size(); ++i) {
        const int s = kGearSlots[i];
        if (s < 0 || slot.worn[i].empty()) {
            continue;
        }
        const auto bit = static_cast<std::uint8_t>(1u << s);
        const bool want = (*target.drawn & bit) != 0;
        std::uint64_t parts = 0;
        std::uint64_t eu = 0;
        std::uint32_t id = 0;
        if (!ReadValue(guest, slot.actor + addr::kActorParts, parts) || parts == 0 ||
            !ReadValue(guest, parts + addr::kPartsEquipmentUser, eu) || eu == 0 ||
            !ReadValue(guest,
                       eu + addr::kEquipSlots + addr::kEquipSlotStride * s + addr::kEquipSlotId,
                       id) ||
            id == 0xFFFFFFFFu) {
            continue;  // the piece is not there (yet)
        }
        if (id != slot.hold_id[i]) {
            // Another piece than the one last set: the game makes a worn
            // piece again a moment after the equip call (seen 2026-11-09),
            // and the new one is put away whatever the old one was.
            slot.hold_id[i] = id;
            slot.hold_known = static_cast<std::uint8_t>(slot.hold_known & ~bit);
            slot.in_hand = static_cast<std::uint8_t>(slot.in_hand & ~bit);
        }
        if ((slot.hold_known & bit) != 0 && want == ((slot.in_hand & bit) != 0)) {
            continue;
        }
        if ((slot.hold_known & bit) == 0 && !want) {
            // Worn put away, as wanted: nothing to ask.
            slot.hold_known = static_cast<std::uint8_t>(slot.hold_known | bit);
            continue;
        }
        slot.hold_slot = s;
        slot.hold_in_hand = want;
        Call c;
        c.function = input_.main_base + addr::kEquipChangeState;
        c.x[0] = eu;
        c.x[1] = static_cast<std::uint64_t>(s);
        c.x[2] = want ? addr::kEquipStateInHand : addr::kEquipStateAway;
        pending_ = Pending::hold;
        return c;
    }
    return std::nullopt;
}

// The game's own setter, once a frame, after Pose wrote the transform.
std::optional<Call> Director::Move(Slot& slot, Guest& guest) {
    (void)guest;
    if (slot.moved_frame == frames_) {
        return std::nullopt;
    }
    slot.moved_frame = frames_;
    if (slot.vtable != input_.main_base + addr::kActorVtable) {
        return std::nullopt;  // not the actor class the setter is known for
    }
    Call c;
    c.function = input_.main_base + addr::kSetMatrix;
    c.x[0] = slot.actor;
    c.x[1] = 0;
    pending_ = Pending::move;
    return c;
}

// The AS controller of an actor; 0 when the chain is not there.
std::uint64_t Director::Controller(Guest& guest, std::uint64_t actor) {
    std::uint64_t p = actor;
    for (const std::uint64_t offset : addr::kControllerPath) {
        if (p == 0 || !ReadValue(guest, p + offset, p)) {
            return 0;
        }
    }
    return p;
}

// Slot 0's frame, when the controller has a slot 0 and its frame is a number.
std::optional<float> Director::Frame(Guest& guest, std::uint64_t controller) {
    std::int32_t count = 0;
    std::uint64_t slots = 0;
    std::uint64_t slot0 = 0;
    float frame = 0.0f;
    if (!ReadValue(guest, controller + addr::kSlotCount, count) || count <= 0 ||
        !ReadValue(guest, controller + addr::kSlots, slots) || slots == 0 ||
        !ReadValue(guest, slots, slot0) || slot0 == 0 ||
        !ReadValue(guest, slot0 + addr::kSlotFrame, frame) || !std::isfinite(frame)) {
        return std::nullopt;
    }
    return frame;
}

std::optional<Call> Director::SetFrame(Slot& slot, std::uint64_t controller, float frame) {
    Call c;
    c.function = input_.main_base + addr::kSetAsFrame;
    c.x[0] = controller;
    c.x[1] = 0;  // slot 0
    c.s0 = frame;
    slot.frame_set_in = frames_;
    pending_ = Pending::frame;
    return c;
}

// One animation call for a live puppet in its scene, or none.
// - show: play the received command when it changes, then set its frame;
//   after that, set the frame only when a new sample finds it more than
//   kFrameSlack off, so the game's own playback runs in between.
// - hold: the frame it had when the hold began, every frame.
// - hide: nothing (it is out of sight).
std::optional<Call> Director::Animate(Slot& slot, const Target& target, Guest& guest,
                                      const Anim* only, std::uint32_t only_serial) {
    if (target.show == Show::hide) {
        return std::nullopt;
    }
    const std::uint64_t controller = Controller(guest, slot.actor);
    if (controller == 0) {
        return std::nullopt;
    }
    const std::string who = "P" + std::to_string(slot.number);
    if (target.show == Show::hold) {
        if (!slot.held_frame) {
            slot.held_frame = Frame(guest, controller);
            if (!slot.held_frame) {
                return std::nullopt;
            }
            if (anim_) {
                Say(who + " held");
            }
        }
        if (slot.frame_set_in == frames_) {
            return std::nullopt;  // once a frame
        }
        return SetFrame(slot, controller, *slot.held_frame);
    }
    if (slot.held_frame && anim_) {
        Say(who + " moves again");
    }
    slot.held_frame.reset();
    // ORM smoothing: the sender's command at the shown time, else the newest.
    // ORM efectos2: or the one PosedAnimate chose.
    const std::optional<Anim>& newest = slot.smooth_anim ? slot.smooth_anim : target.anim;
    const Anim* wanted = only != nullptr ? only : newest ? &*newest : nullptr;
    const std::uint32_t serial = only != nullptr      ? only_serial
                                 : slot.smooth_anim ? slot.smooth_serial
                                                    : target.anim_serial;
    if (wanted == nullptr || wanted->name.empty() || wanted->name.size() >= block::kAnimCapacity) {
        return std::nullopt;
    }
    const Anim& anim = *wanted;
    // A new name, or (ORM) the same name at another rate: the rate is only
    // set by a play, so a change of rate alone was never shown.
    const std::optional<coop::host::AnimPlayed> played =
        slot.anim_name.empty()
            ? std::nullopt
            : std::optional<coop::host::AnimPlayed>(coop::host::AnimPlayed{slot.anim_name, slot.anim_rate});
    if (coop::host::next_anim_step(played, {anim.name, anim.frame, anim.rate}, std::nullopt, false) ==
        coop::host::AnimStep::play) {
        const std::uint64_t text =
            slot.block + block::kAnimText + block::kAnimCapacity * slot.anim_buffer;
        std::array<char, block::kAnimCapacity> name{};
        std::memcpy(name.data(), anim.name.data(), anim.name.size());
        if (!guest.Write(text, name.data(), name.size())) {
            return std::nullopt;
        }
        slot.anim_buffer ^= 1;
        const float rate = std::isfinite(anim.rate) ? anim.rate : 1.0f;
        std::array<std::uint8_t, 0x30> request{};
        const float unset = -1.0f;
        const std::int32_t index = 0;
        std::memcpy(request.data() + 0x00, &slot.actor, 8);
        std::memcpy(request.data() + 0x08, &text, 8);
        std::memcpy(request.data() + 0x10, &index, 4);
        std::memcpy(request.data() + 0x14, &rate, 4);
        std::memcpy(request.data() + 0x18, &unset, 4);
        std::memcpy(request.data() + 0x1c, &unset, 4);
        // +0x20 skip-if-same stays 0: we only play a new name or rate.
        const std::uint64_t at = slot.block + block::kPlayRequest;
        if (!guest.Write(at, request.data(), request.size())) {
            return std::nullopt;
        }
        slot.play_name = anim.name;
        slot.play_rate = rate;
        slot.play_frame = std::isfinite(anim.frame) ? anim.frame : 0.0f;
        slot.play_serial = serial;
        Call c;
        c.function = input_.main_base + addr::kPlayAs;
        c.x[0] = at;
        pending_ = Pending::play;
        return c;
    }
    if (slot.frame_to_set) {
        return SetFrame(slot, controller, *slot.frame_to_set);
    }
    if (slot.sync_next > 0) {
        std::int32_t count = 0;
        ReadValue(guest, controller + addr::kSlotCount, count);
        if (slot.sync_next < std::min(count, addr::kMaxAsSlots)) {
            Call c;
            c.function = input_.main_base + addr::kSyncAsSlot;
            c.x[0] = slot.actor;
            c.x[1] = 0;
            c.x[2] = static_cast<std::uint64_t>(slot.sync_next);
            pending_ = Pending::sync;
            return c;
        }
        slot.sync_next = 0;
    }
    if (serial != slot.anim_serial) {
        slot.anim_serial = serial;
        const auto now = Frame(guest, controller);
        if (std::isfinite(anim.frame) &&
            (!now || std::fabs(*now - anim.frame) > kFrameSlack)) {
            if (anim_) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), " frame %.1f -> %.1f", now.value_or(-1.0f),
                              anim.frame);
                Say(who + " '" + anim.name + "'" + buf);
            }
            return SetFrame(slot, controller, anim.frame);
        }
    }
    return std::nullopt;
}

// Issue 194 (coop_snap.h): a shown or held puppet drawn where its player is. Every jump of
// the place we are handed is logged ("target jump"; eased while shown, at once while held:
// their cutscene's samples move smoothly, so a jump then is a real teleport). The meter reads
// the puppet's place back from the game (what it drew since our last write) and logs each step
// past CoopSnap::kSnapM as a snap ("warp" past kWarpM).
void Director::DrawAt(Slot& slot, Guest& guest, const Target& target, std::uint64_t now_ms) {
    const std::string who = "P" + std::to_string(slot.number);
    const bool held = target.show == Show::hold;
    Vec3 at{};
    if (slot.meter_skip) {
        slot.meter_skip = false;  // still the parked spot: start again from the next frame
        slot.meter.Reset();
    } else if (ReadValue(guest, slot.actor + addr::kActorPosition, at)) {
        if (const auto snap = slot.meter.Step(at)) {
            Say(who + " snap " + std::to_string(*snap).substr(0, 5) + " m (" +
                (*snap >= CoopSnap::kWarpM ? std::string("warp") : ShowText(target.show)) + ")");
        }
        slot.smooth.Spawned(at, target.position);  // a new puppet: eased from where the game made it (no spawn pop)
    }
    float jump = 0.0f;
    const Vec3 drawn = slot.smooth.Step(target.position, now_ms, !held, &jump);
    if (jump > 0.0f && slot.smooth.FromSpawn()) {
        // Held (their cutscene) the smoother does not ease: the step is drawn at once then.
        Say(who + (held ? " drawn at once from where it was made, " : " eased from where it was made, ") +
            std::to_string(jump).substr(0, 5) + " m");
    } else if (jump > 0.0f) {
        Say(who + " target jump " + std::to_string(jump).substr(0, 5) + " m (" + ShowText(target.show) +
            (jump >= CoopSnap::kWarpM ? ", warp" : held ? ", at once" : ", eased") + ")");
    }
    Pose(slot, guest, drawn, &target.rotation, now_ms);
}

// ORM efectos2 (coop_effects.h): while the pose copy writes the bones, its
// player's command is played as well when the effect rules allow it, only
// for the command's effect and sound events; after an allowed command, the
// rest command once. A play's frame and partial slots are still finished.
// Shown puppets only (held and parked ones keep what they have).
std::optional<Call> Director::PosedAnimate(Slot& slot, const Target& target, Guest& guest) {
    if (target.show != Show::show || !slot.revealed) {
        return std::nullopt;
    }
    const std::optional<Anim>& newest = slot.smooth_anim ? slot.smooth_anim : target.anim;
    const std::uint32_t serial = slot.smooth_anim ? slot.smooth_serial : target.anim_serial;
    std::optional<CoopEffects::Command> wanted;
    if (newest) {
        wanted = CoopEffects::Command{newest->name, newest->frame, newest->rate, false};
    }
    const auto posed = CoopEffects::PosedCommand(effect_rules_, wanted, slot.anim_name);
    if (!posed) {
        if (!slot.anim_name.empty() && (slot.frame_to_set || slot.sync_next > 0)) {
            const Anim same{slot.anim_name, slot.frame_to_set.value_or(0.0f), slot.anim_rate};
            return Animate(slot, target, guest, &same, slot.anim_serial);
        }
        return std::nullopt;
    }
    const std::string who = "P" + std::to_string(slot.number);
    if (!slot.effects_said) {
        slot.effects_said = true;
        Say(who + " effects: '" + posed->name +
            "' played under the copied pose (its effect and sound events)");
    } else if (anim_ && posed->name != slot.anim_name) {
        Say(who + " effects: " +
            (posed->rest ? "rest '" + posed->name + "' after '" + slot.anim_name + "'"
                         : "'" + posed->name + "' played under the copied pose"));
    }
    const Anim anim{posed->name, posed->frame, posed->rate};
    return Animate(slot, target, guest, &anim, posed->rest ? slot.anim_serial : serial);
}

// The puppet's pose: +0x31c/+0x328 and the engine's pending transform
// (+0x358/+0x364), which the actor's calc applies over +0x31c. With the
// debug anim log, once a second, what the game left there since the last
// write, so a writer that undoes ours shows up.
void Director::Tint(Slot& slot, Guest& guest, std::uint32_t color) {
    const auto result = CoopColors::Apply(guest, slot.actor, CoopColors::TintOf(color, tint_));
    if (result == CoopColors::Applied::no_model) {
        return;  // not made yet; asked again next frame
    }
    if (slot.tint_actor != slot.actor || slot.tint_color != color) {
        slot.tint_actor = slot.actor;
        slot.tint_color = color;
        char strength[16];
        std::snprintf(strength, sizeof(strength), "%.2f", tint_);
        Say("P" + std::to_string(slot.number) + " colour " + CoopColors::Text(color) +
            " (strength " + strength + ") on model " +
            Hex(CoopColors::ModelOf(guest, slot.actor)) + " of " + Hex(slot.actor));
    }
}

void Director::Pose(Slot& slot, Guest& guest, const Vec3& position, const Mat3* rotation,
                    std::uint64_t now_ms) {
    if (anim_ && now_ms - slot.pose_log_ms >= 1000) {
        slot.pose_log_ms = now_ms;
        Vec3 at{};
        Vec3 asked{};
        ReadValue(guest, slot.actor + addr::kActorPosition, at);
        ReadValue(guest, slot.actor + addr::kActorPositionRequest, asked);
        Say("P" + std::to_string(slot.number) + " pose: at " + VecText(at) + ", request " +
            VecText(asked) + ", now to " + VecText(position));
    }
    WriteValue(guest, slot.actor + addr::kActorPosition, position);
    WriteValue(guest, slot.actor + addr::kActorPositionRequest, position);
    if (rotation != nullptr) {
        WriteValue(guest, slot.actor + addr::kActorRotation, *rotation);
        WriteValue(guest, slot.actor + addr::kActorRotationRequest, *rotation);
    }
}

// The actor taken from the request, while its vtable is the one it had then
// (an actor the engine freed and reused is not written to).
std::uint64_t Director::Resolve(Slot& slot, Guest& guest) {
    std::uint64_t vtable = 0;
    if (slot.taken == 0 || !ReadValue(guest, slot.taken, vtable) || vtable != slot.vtable) {
        return 0;
    }
    return slot.taken;
}

// While creating, before the actor is taken: what the request says.
void Director::Poll(Slot& slot, Guest& guest, std::uint64_t now_ms) {
    std::uint64_t request = 0;
    std::uint32_t word = 0;
    if (slot.taken != 0 || slot.take || slot.drop || slot.handle == 0 ||
        !ReadValue(guest, slot.handle, request) || request == 0 ||
        !ReadValue(guest, request, word)) {
        return;
    }
    const std::uint32_t state = word & 0xffff;
    slot.request = request;
    if (state == addr::kRequestReady) {
        slot.take = true;
    } else if (state == addr::kRequestDrop) {
        slot.drop = true;
    } else if (state == 5 || state == 6) {
        Say("P" + std::to_string(slot.number) + " " + slot.actor_name + ": request " +
            Hex(request) + " failed (state " + std::to_string(state) + "); trying again in 5 s");
        NoteLock(slot.actor_name, false);  // ORM orm/pvp
        WriteValue(guest, slot.handle, std::uint64_t{0});
        slot.phase = Phase::idle;
        slot.retry_ms = now_ms + 5000;
    }
}

void Director::Status(Guest& guest, std::uint64_t now_ms) {
    if (!verbose_) {
        return;
    }
    if (status_ms_ == 0) {
        status_ms_ = now_ms;
        frames_at_status_ = frames_;
        return;
    }
    if (now_ms - status_ms_ < 1000) {
        return;
    }
    const double seconds = double(now_ms - status_ms_) / 1000.0;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.1f cave frames/s, scene '%s', %zu wanted",
                  double(frames_ - frames_at_status_) / seconds, input_.scene.c_str(),
                  input_.targets.size());
    Say(buf);
    status_ms_ = now_ms;
    frames_at_status_ = frames_;
    for (const auto& slot : slots_) {
        std::string line = "P" + std::to_string(slot.number) + " " + PhaseText(slot.phase);
        if (slot.actor != 0) {
            std::uint32_t state = 0;
            Vec3 at{};
            ReadValue(guest, slot.actor + addr::kActorState, state);
            ReadValue(guest, slot.actor + addr::kActorPosition, at);
            line += " actor " + Hex(slot.actor) + " state " + std::to_string(state) + " at " +
                    VecText(at);
        }
        if (!slot.anim_name.empty()) {
            const auto frame = slot.actor ? Frame(guest, Controller(guest, slot.actor))
                                          : std::nullopt;
            char frame_text[32];
            std::snprintf(frame_text, sizeof(frame_text), " frame %.1f", frame.value_or(-1.0f));
            line += " plays '" + slot.anim_name + "'" + frame_text;
        }
        if (const Target* t = Wanted(slot.number)) {
            line += ", wanted at " + VecText(t->position);
            line += t->show == Show::show ? " (show)" : t->show == Show::hold ? " (hold)" : " (hide)";
        }
        if (!slot.revealed && slot.phase == Phase::live) {
            line += " parked until it has a pose and a fresh sample";
        }
        // While it is being made, what the handle holds, to see it fill in.
        if (std::uint64_t h = 0; slot.phase == Phase::creating && slot.handle != 0 &&
                                 ReadValue(guest, slot.handle, h) && h != 0) {
            std::array<std::uint64_t, 8> words{};
            guest.Read(h, words.data(), sizeof(words));
            line += "; handle holds " + Hex(h) + ":";
            for (auto w : words) {
                line += " " + Hex(w);
            }
        }
        Say(line);
    }
}

Gear GearOf(const coop::protocol::Equipment& e) {
    Gear gear{e.weapon, e.shield,     e.head,         e.armor, e.boots,   e.held,
              e.hand,   e.sub_weapon, e.third_weapon, e.tool,  e.sub_tool};
    // The Ocarina and its belt holder (issue 152): "Item_Ocarina_Fairy" and
    // "Item_Ocarina_Fairy_Holder" (or _Time); none while the player has none.
    if (e.ocarina && e.ocarina->starts_with("Item_Ocarina_")) {
        gear[kGearOcarinaHolder] = *e.ocarina + "_Holder";
        gear[kGearOcarina] = e.ocarina;
    }
    // What is really in the sword hand (a Deku stick) goes before the
    // equipped sword's name; an empty slot leaves the flag's name.
    if (e.weapon_slot && !e.weapon_slot->empty()) {
        gear[0] = e.weapon_slot;
    }
    return gear;
}

bool OcarinaInHand(Target& target, std::size_t piece) {
    if (piece >= kGearCount || kGearSlots[piece] < 0 || !GearIsCarried(piece)) {
        return false;
    }
    const auto& ocarina = target.gear[kGearOcarina];
    if (!ocarina || !ocarina->starts_with("Item_Ocarina_")) {
        return false;
    }
    target.gear[piece] = *ocarina;
    target.drawn = static_cast<std::uint8_t>(target.drawn.value_or(0) | (1u << kGearSlots[piece]));
    return true;
}

std::size_t OcarinaHandPiece(std::string_view setting) {
    if (CoopSwitch::Parse(setting) == CoopSwitch::Reading::off) {  // 0, false, no, off
        return kOcarinaHandOff;
    }
    if (setting == "bow") {
        return kGearHeld;
    }
    return kGearSubWeapon;
}

void Planner::SetColliders(bool on) {
    if (on == colliders_) {
        return;
    }
    colliders_ = on;
    gate_.reset();  // off: every body goes; on: each waits until clear
}

std::vector<std::string> Planner::TakeEffectLines() {
    std::vector<std::string> lines;
    lines.swap(effect_lines_);
    return lines;
}

Input Planner::Plan(std::uint64_t main_base, const std::optional<std::string>& scene,
                    const std::optional<Vec3>& position, bool local_event,
                    const std::vector<Remote>& remotes, std::uint64_t now_ms,
                    const Gear& own_gear,
                    std::optional<std::uint8_t> own_drawn,
                    std::optional<std::uint32_t> own_color) {
    Input in;
    if (main_base == 0) {
        return in;
    }
    in.main_base = main_base;
    in.scene = scene.value_or("");
    if (in.scene != scene_) {
        scene_ = in.scene;
        scene_since_ms_ = now_ms;
        debug_spot_.reset();
        overlaps_.clear();  // everyone is somewhere else now
        gate_.reset();      // ORM choque: every body a ghost until clear again
    }
    if (in.scene.empty()) {
        return in;
    }
    if (local_event_ && !local_event) {
        local_event_end_ms_ = now_ms;  // kept over a scene change: both may enter at one door
    }
    local_event_ = local_event;
    for (const auto& r : remotes) {
        if (r.number == 0 || !(r.fade > 0.0f)) {
            continue;
        }
        const std::optional<std::string_view> theirs =
            r.scene ? std::optional<std::string_view>(*r.scene) : std::nullopt;
        const std::optional<std::string_view> anim =
            r.anim ? std::optional<std::string_view>(r.anim->name) : std::nullopt;
        Show show = Show::show;
        const char* why = "";
        // ORM (oot-remake docs/PUERTAS.md): walking out of our place, gone
        // from it, or coming into it (coop/host/passages.hpp).
        const coop::host::Passage passage =
            settings_.passages ? passages_.note(r.number, in.scene, theirs, r.position, now_ms)
                               : coop::host::Passage::none;
        bool keep_only = false;
        switch (coop::host::visibility(in.scene, local_event, theirs, r.status, anim)) {
        case coop::host::Visibility::hide:
            if (!coop::host::same_place(in.scene, theirs)) {
                if (passage == coop::host::Passage::leaving) {
                    keep_only = true;  // drawn on its shown pose: the walk into the exit
                    why = passages_.why(r.number);
                    break;
                }
                if (passage == coop::host::Passage::gone) {
                    keep_only = true;  // out of sight where it left, then let go
                    show = Show::hide;
                    why = passages_.why(r.number);
                    break;
                }
                continue;  // not here: no puppet (one made is deleted)
            }
            show = Show::hide;  // our cutscene: kept, out of sight
            why = "our cutscene";
            break;
        case coop::host::Visibility::ride:
            // Issue 199: shown in its riding animation; CoopRide draws a
            // copy of the horse under it. ORM D-R19 (request: "tenemos que vernos
            // en todo momento"): with our riders' horses on (settings_.riders,
            // coop_epona.h) the getting on is named too; which horse is drawn
            // (theirs, CoopRide_Epona, or ours, coop_epona's) is chosen there,
            // never both (oot-remake docs/PIN_TEST10.md).
            if (settings_.riders &&
                coop::host::epona::ride_of(anim) == coop::host::epona::Ride::mounting) {
                why = "getting on";
                break;
            }
            why = "riding";
            break;
        case coop::host::Visibility::hold:
            show = Show::hold;
            // ORM menus (MENUS_PAUSA.md M2): their game is paused by a menu, so
            // the puppet stands frozen where it is, still wearing their gear.
            why = (r.status & coop::protocol::status::paused) != 0 ? "paused" : "their cutscene";
            break;
        case coop::host::Visibility::show:
            if (coop::host::activity_of(anim) == coop::host::Activity::ocarina) {
                why = "ocarina";
            }
            break;
        }
        if (passage == coop::host::Passage::arriving && show != Show::hold) {
            show = Show::hide;  // made, kept out of sight until it stands at the entrance
            why = passages_.why(r.number);
        } else if (show == Show::show && *passages_.why(r.number) != '\0' && *why == '\0') {
            why = passages_.why(r.number);  // "arrived (waited the most)", for the log
        }
        Target t;
        t.number = r.number;
        t.color = r.color ? r.color : coop::protocol::slot_color(r.number);
        t.actor = ActorFor(r.adult, t.color);
        t.adult = r.adult;
        // Drawn a step aside while it stands on our Link after an event.
        const auto [step, weight] = SideStep(r, position, local_event, now_ms, show == Show::show);
        t.offset = {step[0] * weight, step[1] * weight, step[2] * weight};
        // ORM choque: its real body, solid or a ghost (coop/host/orm_bodies.hpp BodyGate).
        if (colliders_) {
            coop::host::orm::bodies::GateIn g;
            g.number = r.number;
            g.sample = {r.position[0] + t.offset[0], r.position[1] + t.offset[1], r.position[2] + t.offset[2]};
            g.ours = position;
            g.shown = show == Show::show && !keep_only;
            g.down = (r.status & coop::protocol::status::dead) != 0;
            g.riding = coop::host::epona::ride_of(anim) != coop::host::epona::Ride::none;
            g.adult = r.adult;
            g.our_adult = our_adult_;
            g.now_ms = now_ms;
            t.solid = gate_.solid(g);
        }
        // ORM bodies: out of our Link, drawn (our Link is never moved: CUERPOS.md).
        if (settings_.bodies) {
            coop::host::orm::bodies::AsideIn a;
            a.number = r.number;
            a.sample = {r.position[0] + t.offset[0], r.position[1] + t.offset[1], r.position[2] + t.offset[2]};
            a.ours = position;
            a.shown = show == Show::show;
            a.down = (r.status & coop::protocol::status::dead) != 0;
            a.solid = t.solid;  // ORM choque: a solid body parts the two; drawn on its sample
            a.now_ms = now_ms;
            t.aside = aside_.step(a);
            for (int i = 0; i < 3; ++i) {
                t.offset[i] += t.aside[i];
            }
        }
        t.position = {r.position[0] + t.offset[0], r.position[1] + t.offset[1],
                      r.position[2] + t.offset[2]};
        t.side_step = step;
        t.first_sight_m = FirstSightM(r.number);
        if (r.rotation) {
            t.rotation = QuatToMatrix(*r.rotation);
        }
        t.show = show;
        t.why = why;
        t.anim = r.anim;
        t.anim_serial = r.anim_serial;
        t.gear = r.gear;
        t.drawn = r.drawn;
        t.ocarina = r.ocarina;
        if (coop::host::activity_of(anim) == coop::host::Activity::ocarina) {
            OcarinaInHand(t, settings_.ocarina_hand);
        }
        t.sample_age_ms = r.sample_age_ms;
        t.keep_only = keep_only;
        // ORM efectos2 (coop_effects.h): effect commands, damage flash, status.
        if (settings_.effects.any()) {
            const auto look =
                effects_.Update(settings_.effects, r.number, theirs.value_or(std::string_view{}), anim,
                                (r.status & coop::protocol::status::dead) != 0, now_ms);
            if (const std::string& said = effects_.Said(r.number); !said.empty()) {
                effect_lines_.push_back("P" + std::to_string(r.number) + " effects: " + said);
            }
            t.effects = settings_.effects.as_effects;
            t.flash_since_ms = look.flash_since_ms;
            t.status = t.show == Show::show ? look.status : CoopEffects::Status::none;
        }
        in.targets.push_back(std::move(t));
    }
    if (settings_.bodies) {
        aside_.prune(now_ms);
        for (auto& line : aside_.take_lines()) {
            effect_lines_.push_back(std::move(line));
        }
        if (effect_lines_.size() > 64) {  // nobody takes them (a test): keep the newest
            effect_lines_.erase(effect_lines_.begin(), effect_lines_.end() - 64);
        }
    }
    if (colliders_) {
        gate_.prune(now_ms);
        for (auto& line : gate_.take_lines()) {
            effect_lines_.push_back(std::move(line));
        }
        if (effect_lines_.size() > 64) {  // nobody takes them (a test): keep the newest
            effect_lines_.erase(effect_lines_.begin(), effect_lines_.end() - 64);
        }
    }
    if (settings_.effects.any()) {
        effects_.Prune(now_ms);
        if (effect_lines_.size() > 64) {  // nobody takes them (a test): keep the newest
            effect_lines_.erase(effect_lines_.begin(), effect_lines_.end() - 64);
        }
    }
    if (settings_.debug && position && now_ms - scene_since_ms_ >= settings_.debug_delay_ms) {
        if (!debug_spot_) {
            debug_spot_ = Vec3{(*position)[0] + settings_.debug_offset[0],
                               (*position)[1] + settings_.debug_offset[1],
                               (*position)[2] + settings_.debug_offset[2]};
        }
        Target t;
        t.number = 0;
        t.actor = ActorFor(false, own_color);
        t.position = *debug_spot_;
        if (colliders_) {
            // ORM choque: the test puppet has its body too (one game can walk into it).
            coop::host::orm::bodies::GateIn g;
            g.number = 0;
            g.sample = t.position;
            g.ours = position;
            g.our_adult = our_adult_;
            g.now_ms = now_ms;
            t.solid = gate_.solid(g);
            for (auto& line : gate_.take_lines()) {
                effect_lines_.push_back(std::move(line));
            }
        }
        t.gear = own_gear;
        t.drawn = own_drawn;
        t.color = own_color;  // the test puppet wears your own colour, if you chose one
        in.targets.push_back(std::move(t));
    }
    return in;
}

bool PackHasGreenPuppets(std::string_view manifest) {
    return manifest.find("\"CoopPuppet_Child_Green\"") != std::string_view::npos &&
           manifest.find("\"CoopPuppet_Adult_Green\"") != std::string_view::npos;
}

bool PackHasTunicPuppets(std::string_view manifest) {
    return manifest.find("\"CoopPuppet_Child_") != std::string_view::npos ||
           manifest.find("\"CoopPuppet_Adult_") != std::string_view::npos;
}

namespace {
float Distance(const Vec3& a, const Vec3& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                     (a[2] - b[2]) * (a[2] - b[2]));
}
}  // namespace

float Planner::FirstSightM(std::uint8_t number) const {
    for (const auto& [n, o] : overlaps_) {
        if (n == number) return o.first_m;
    }
    return -1.0f;
}

std::pair<Vec3, float> Planner::SideStep(const Remote& r, const std::optional<Vec3>& ours,
                                         bool local_event, std::uint64_t now_ms, bool shown) {
    auto it = std::find_if(overlaps_.begin(), overlaps_.end(),
                           [&](const auto& kv) { return kv.first == r.number; });
    if (it == overlaps_.end()) {
        overlaps_.emplace_back(r.number, Overlap{});
        it = overlaps_.end() - 1;
    }
    Overlap& o = it->second;
    // Its first plan in this scene with our Link read and the puppet drawn (sondas23r7: the first plans came while
    // our Link was not read yet; sondas24r7: while the mate was still loading, its place not yet the one it lands
    // on; and the game's boot event ran while the puppet was first shown on us).
    const bool first_sight = shown && !o.judged && ours.has_value();
    if (first_sight) {
        o.judged = true;
    }
    const float apart_now = ours ? Distance(r.position, *ours) : 1e9f;
    if (first_sight) {
        o.first_m = apart_now;
    }
    const bool cutscene = coop::host::in_cutscene(r.status);
    if (o.cutscene && !cutscene) {
        o.event_end_ms = now_ms;
    }
    o.cutscene = cutscene;
    const std::uint64_t ended = std::max(o.event_end_ms, local_event_end_ms_);
    const bool event = local_event || cutscene;
    const float apart = ours ? Distance(r.position, *ours) : 1e9f;
    // To the puppet's own side (its rotation's x axis on the ground), the side away from our Link.
    const auto side_away = [&]() {
        Vec3 side{1.0f, 0.0f, 0.0f};
        if (r.rotation) {
            const Mat3 m = QuatToMatrix(*r.rotation);
            const float n = std::hypot(m[0], m[6]);
            if (n > 1e-3f) {
                side = {m[0] / n, 0.0f, m[6] / n};
            }
        }
        if (ours && (side[0] * (r.position[0] - (*ours)[0]) + side[2] * (r.position[2] - (*ours)[2])) < 0.0f) {
            side = {-side[0], 0.0f, -side[2]};
        }
        return side;
    };
    if (first_sight && apart < kOverlapNear) {
        // Both at one Continue spot: aside at once, whole (no fade-in), further than after an event.
        const Vec3 side = side_away();
        o.on = true;
        o.first = true;
        o.anchor = r.position;
        o.step = {side[0] * kFirstSightStep, 0.0f, side[2] * kFirstSightStep};
        o.weight = 1.0f;
        o.last_ms = now_ms;
        return {o.step, o.weight};
    }
    if (!o.on) {
        const bool after = ended != 0 && now_ms - std::min(now_ms, ended) <= kOverlapWindowMs;
        if (after && !event && apart < kOverlapNear) {
            if (o.near_since_ms == 0) {
                o.near_since_ms = std::max<std::uint64_t>(now_ms, 1);
            } else if (now_ms - o.near_since_ms >= kOverlapForMs) {
                const Vec3 side = side_away();
                o.on = true;
                o.anchor = r.position;
                o.step = {side[0] * kSideStep, 0.0f, side[2] * kSideStep};
            }
        } else {
            o.near_since_ms = 0;
        }
    } else if ((event && !o.first) || apart > kSideStepApart || Distance(r.position, o.anchor) > kSideStepLeave) {
        // A first sight's step is kept through the mate's own boot event (sondas26r7: B, just loaded, was in its
        // DefaultBootEvent: the step went in the next plan and B's puppet stood on us again).
        o.on = false;
        o.first = false;
        o.near_since_ms = 0;
    }
    const std::uint64_t dt =
        o.last_ms == 0 ? 0 : std::min<std::uint64_t>(now_ms - std::min(now_ms, o.last_ms), 100);
    o.last_ms = now_ms;
    if (o.on) {
        o.weight = std::min(1.0f, o.weight + static_cast<float>(std::max<std::uint64_t>(dt, 1)) /
                                                 static_cast<float>(kSideStepInMs));
    } else {
        o.weight = std::max(0.0f, o.weight - static_cast<float>(dt) / static_cast<float>(kSideStepOutMs));
    }
    if (!(o.weight > 0.0f)) {
        return {Vec3{}, 0.0f};
    }
    return {o.step, o.weight};
}

std::string Planner::ActorFor(bool adult, std::optional<std::uint32_t> color) const {
    if (!settings_.actor.empty()) {
        return settings_.actor;
    }
    const std::string plain = adult ? "CoopPuppet_Adult" : "CoopPuppet_Child";
    // ORM orm/pvp: the lock-on variant while "Fijar aliados" asks for it; it
    // wears no tunic colour (the pack has no lock-on per colour).
    if (const std::string suffix = ActorSuffix(); !suffix.empty()) {
        return plain + suffix;
    }
    if (settings_.tunics && color) {
        std::string_view tunic = CoopColors::TunicOf(*color);
        if (tunic.empty() && settings_.green_puppet) {
            tunic = "Green";  // the plain puppet wears your own colour (PlannerSettings)
        }
        if (!tunic.empty()) {
            std::string name = plain + '_' + std::string(tunic);
            if (std::find(failed_tunics_.begin(), failed_tunics_.end(), name) == failed_tunics_.end()) {
                return name;
            }
        }
    }
    return plain;
}

void Director::TunicFails(const std::string& actor, const std::string& why) {
    std::lock_guard lock(tunic_mutex_);
    if (tunic_failed_.insert(actor).second) {
        Say(actor + " " + why + "; that colour shows as the plain puppet from now on");
    }
}


void SetActorSuffix(std::string suffix) {
    std::lock_guard lock(g_suffix_mutex);
    g_suffix = std::move(suffix);
}

std::string ActorSuffix() {
    std::lock_guard lock(g_suffix_mutex);
    return g_suffix;
}

LockTally Locks() {
    return {g_locks_made.load(), g_locks_failed.load()};
}

namespace {

std::once_flag g_once;
std::atomic<bool> g_configured = false;
std::unique_ptr<Director> g_director;
std::unique_ptr<Planner> g_planner;  // the present thread only
PlanObserver g_plan_observer;        // ORM efectos2; the present thread only
std::vector<PlanObserver> g_plan_observers;  // ORM choque (AddPlanObserver); the present thread only

// Each remote player's newest animation, from the network thread.
struct Received {
    Anim anim;
    std::uint32_t serial = 0;
};
std::mutex g_anim_mutex;
std::array<std::optional<Received>, coop::protocol::kMaxPlayers + 1> g_anims;  // by player number; guarded
std::array<Gear, coop::protocol::kMaxPlayers + 1> g_gear;  // the same; guarded
std::array<std::optional<std::uint8_t>, coop::protocol::kMaxPlayers + 1> g_drawn;  // the same; guarded
std::array<std::optional<std::uint32_t>, coop::protocol::kMaxPlayers + 1> g_color;  // the same; guarded
std::optional<std::uint32_t> g_own_color;                                           // ours; guarded
std::array<CoopPuppetSound::Wanted, coop::protocol::kMaxPlayers + 1> g_ocarina;  // the same; guarded (issue 152)
// ORM smoothing (LICHII_ORM_SMOOTH): each remote player's samples, fed from
// the network thread, asked from the calc thread; guarded by g_anim_mutex.
std::array<coop::host::PoseSmoother, coop::protocol::kMaxPlayers + 1> g_smooth;
std::array<std::uint64_t, coop::protocol::kMaxPlayers + 1> g_smooth_log_ms{};  // the same
// What SmoothAt last gave for each player (LastSmoothed, the riders' horses); the same.
std::array<std::optional<ShownPose>, coop::protocol::kMaxPlayers + 1> g_shown;
constexpr std::uint64_t kSmoothLogMs = 10'000;
// ORM riesgos-visual: the delay measured from each player's 10 s window
// (coop::host::DelayTuner), the same for everyone; guarded by g_anim_mutex.
coop::host::DelayTuner g_tuner;

// The calc thread (Director::SetSmoothing): where player n is shown at
// server time `server_now_us`. Every 10 s, a line of what the smoothing had
// to do for that player, when it had to do anything but interpolate.
std::optional<coop::host::SmoothedPose> SmoothAt(std::uint8_t number, std::uint64_t server_now_us,
                                                 std::uint64_t now_ms) {
    if (number == 0 || number >= g_smooth.size()) {
        return std::nullopt;
    }
    std::optional<coop::host::SmoothedPose> pose;
    std::optional<coop::host::PoseSmoother::Stats> stats;
    std::optional<coop::host::DelayTuner::Verdict> verdict;
    {
        std::lock_guard lock(g_anim_mutex);
        pose = g_smooth[number].at(server_now_us);
        if (pose) {
            g_shown[number] = ShownPose{server_now_us, *pose};
        } else {
            g_shown[number].reset();
        }
        if (g_smooth_log_ms[number] == 0) {
            g_smooth_log_ms[number] = now_ms;
        } else if (now_ms - std::min(now_ms, g_smooth_log_ms[number]) >= kSmoothLogMs) {
            g_smooth_log_ms[number] = now_ms;
            stats = g_smooth[number].take_stats();
            verdict = g_tuner.feed(*stats);
            if (verdict->change == coop::host::DelayTuner::Change::up ||
                verdict->change == coop::host::DelayTuner::Change::down) {
                for (auto& s : g_smooth) {
                    s.set_delay_us(verdict->delay_us);
                }
            }
        }
    }
    if (verdict && g_director) {
        // ORM riesgos-visual: the delay's check line, and each change of it.
        const auto& v = *verdict;
        const std::string who = "P" + std::to_string(number);
        const std::string share = std::to_string(static_cast<int>(v.share * 1000.0 + 0.5) / 10) + "." +
                                  std::to_string(static_cast<int>(v.share * 1000.0 + 0.5) % 10) + " %";
        using C = coop::host::DelayTuner::Change;
        if (v.change == C::up || v.change == C::down) {
            g_director->SetSmoothDelay(v.delay_us);  // the bodies with the positions
            g_director->Note(who + " smooth: the delay is " + std::to_string(v.delay_us / 1000) +
                             " ms for everyone now (" + share + " of " + who + "'s frames were past his newest "
                             "sample)");
        }
        if (v.change == C::up) {
            CoopVCheck::Fallback("smooth delay", std::to_string(v.delay_us / 1000) + " ms for everyone: " + share +
                                                     " of " + who + "'s frames were carried on or held at the " +
                                                     "shorter one");
        } else if (v.change == C::kept && v.share <= g_tuner.settings().up_share) {
            CoopVCheck::Ok("smooth delay", std::to_string(v.delay_us / 1000) + " ms covers " + who +
                                               "'s samples (" + share + " of his frames carried on or held)");
        }
    }
    if (stats && g_director &&
        (stats->reordered || stats->late || stats->too_old || stats->extrapolated || stats->held ||
         stats->corrections || stats->snaps || stats->cuts)) {
        const auto& t = *stats;
        g_director->Note("P" + std::to_string(number) + " smooth: " +
                         std::to_string(t.buffered) + " samples in 10 s, " +
                         std::to_string(t.reordered) + " out of order, " + std::to_string(t.late) +
                         " late, " + std::to_string(t.too_old) + " too old; " +
                         std::to_string(t.frames) + " frames: " + std::to_string(t.extrapolated) +
                         " carried on, " + std::to_string(t.held) + " held moving, " +
                         std::to_string(t.corrections) + " eased, " + std::to_string(t.snaps) +
                         " snapped (" + std::to_string(t.cuts) + " teleports or scene changes); " +
                         "at most " + std::to_string(t.max_ahead_us / 1000) + " ms past a sample");
    }
    return pose;
}

} // namespace

std::optional<std::uint32_t> PlayerColor(std::uint8_t number) {
    std::lock_guard lock(g_anim_mutex);
    return number < g_color.size() ? g_color[number] : std::nullopt;
}

void SetOwnColor(std::optional<std::uint32_t> rgb) {
    std::lock_guard lock(g_anim_mutex);
    g_own_color = rgb;
}

std::optional<std::uint32_t> OwnColor() {
    std::lock_guard lock(g_anim_mutex);
    return g_own_color;
}

std::uint32_t NameColor(std::uint8_t number, std::uint8_t self) {
    return (number == self ? OwnColor() : PlayerColor(number))
        .value_or(coop::protocol::slot_color(number));
}

void OnEvent(const coop::net::Event& event) {
    if (!g_configured) {
        return;
    }
    using coop::net::EventKind;
    if (event.kind == EventKind::disconnected) {
        std::lock_guard lock(g_anim_mutex);
        g_anims = {};
        g_gear = {};
        g_drawn = {};
        g_color = {};
        g_ocarina = {};
        for (auto& s : g_smooth) {
            s.reset();
        }
        g_shown = {};
    } else if (event.kind == EventKind::player_left && event.player < g_anims.size()) {
        std::lock_guard lock(g_anim_mutex);
        g_anims[event.player].reset();
        g_gear[event.player] = {};
        g_drawn[event.player].reset();
        g_color[event.player].reset();
        g_ocarina[event.player] = {};
        g_smooth[event.player].reset();
        g_shown[event.player].reset();
    } else if (event.kind == EventKind::player_equip && event.player != 0 &&
               event.player < g_gear.size()) {
        const auto& e = event.equipment;
        g_director->Note("P" + std::to_string(event.player) + " PLAYER_EQUIP: weapon '" +
                         e.weapon.value_or("?") + "', shield '" + e.shield.value_or("?") + "', held '" +
                         e.held.value_or("?") + "', slots 0 '" + e.weapon_slot.value_or("?") +
                         "' 1 '" + e.sub_weapon.value_or("?") + "' 2 '" +
                         e.third_weapon.value_or("?") + "' 5 '" + e.tool.value_or("?") + "' 6 '" +
                         e.sub_tool.value_or("?") + "', hand '" + e.hand.value_or("?") + "'");
        std::lock_guard lock(g_anim_mutex);
        g_gear[event.player] = GearOf(e);
        g_drawn[event.player] = e.drawn;
        g_color[event.player] = e.color;
    } else if (const auto* note = std::get_if<coop::protocol::OcarinaNote>(&event.message);
               event.kind == EventKind::message && note != nullptr) {
        if (!OrmOcarinaSound::UpstreamNotes()) {
            return;  // ORM: LICHII_ORM_OCARINA_NOTES=1: our orm_ocarina_sound plays the notes, not this
        }
        if (note->player_number == 0 || note->player_number >= g_ocarina.size()) {
            return;
        }
        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        std::lock_guard lock(g_anim_mutex);
        auto& w = g_ocarina[note->player_number];
        w = CoopPuppetSound::Wanted{note->note, w.serial + 1, now_ms};
    } else if (event.kind == EventKind::message) {
        const auto* sample = std::get_if<coop::protocol::PlayerSample>(&event.message);
        if (sample == nullptr || sample->player_number == 0 ||
            sample->player_number >= g_anims.size()) {
            return;
        }
        std::lock_guard lock(g_anim_mutex);
        if (const auto s = coop::host::smooth_sample_of(*sample)) {
            g_smooth[sample->player_number].push(*s);  // ORM smoothing (positions too)
        }
        if (!sample->animation) {
            return;
        }
        auto& slot = g_anims[sample->player_number];
        const std::uint32_t serial = slot ? slot->serial + 1 : 1;
        slot = Received{{sample->animation->name, sample->animation->frame,
                         sample->animation->rate},
                        serial};
    }
}

void Configure(PlannerSettings settings, bool verbose, Director::Log log) {
    std::call_once(g_once, [&] {
        g_director = std::make_unique<Director>(std::move(log), verbose, settings.debug_anim);
        g_director->SetPoseCopy(true);
        // docs/puppet.md, "Colour". LICHII_COOP_PUPPET_TINT: 0 to 1, or off.
        // The model tint darkens the whole Link (reports puppet-colors-2): off
        // unless asked for; the colour is the tunic (PlannerSettings::tunics).
        const char* tint = std::getenv("LICHII_COOP_PUPPET_TINT");
        g_director->SetTint(tint != nullptr ? CoopColors::StrengthFrom(tint) : 0.0f);
        g_director->SetRevealGate(true);
        // ORM: masks and the lens on the puppet's head (coop_mascaras.h).
        g_director->SetHeadFollow(CoopSwitch::On("LICHII_ORM_MASK_FOLLOW", true));
        if (settings.smooth) {
            // ORM smoothing (oot-remake docs/SUAVIZADO.md); the fairies' own
            // director keeps the plan's positions.
            g_director->SetSmoothing(
                [](std::uint8_t number, std::uint64_t server_now_us) {
                    const auto now_ms = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
                    return SmoothAt(number, server_now_us, now_ms);
                },
                coop::host::SmoothSettings{}.delay_us);
        }
        g_director->SetEffectRules(settings.effects.rules);  // ORM efectos2
        g_planner = std::make_unique<Planner>(std::move(settings));
        g_configured = true;
    });
}

Director& TheDirector() {
    Configure({}, false, {});
    return *g_director;
}

std::optional<ShownPose> LastSmoothed(std::uint8_t number) {
    if (number == 0 || number >= g_shown.size()) {
        return std::nullopt;
    }
    std::lock_guard lock(g_anim_mutex);
    return g_shown[number];
}

std::uint32_t LivePuppets() {
    return g_configured && g_director ? g_director->LiveMask() : 0;
}

std::vector<Remote> Collect(coop::host::Session* session, std::uint64_t server_now_us) {
    std::vector<Remote> remotes;
    if (session != nullptr && server_now_us != 0) {
        session->with_presence([&](coop::presence::Presence& p) {
            for (const std::uint8_t n : p.players()) {
                // The same time as the markers' collect() this frame.
                const auto pose = p.shown(n, server_now_us);
                if (!pose) {
                    continue;
                }
                Remote r;
                r.number = n;
                r.position = pose->position;
                r.rotation = pose->rotation;
                r.fade = pose->fade;
                if (const auto* player = p.player(n)) {
                    r.scene = player->scene();
                    r.status = player->flags();
                    r.adult = player->age() == coop::protocol::Age::adult;
                    // The newest sample's age, for the reveal (oldest first).
                    if (!player->samples().empty()) {
                        const std::uint64_t at = player->samples().back().time.server_time_us;
                        r.sample_age_ms = server_now_us > at ? (server_now_us - at) / 1000 : 0;
                    }
                }
                remotes.push_back(std::move(r));
            }
        });
        std::lock_guard lock(g_anim_mutex);
        for (auto& r : remotes) {
            if (r.number < g_anims.size() && g_anims[r.number]) {
                r.anim = g_anims[r.number]->anim;
                r.anim_serial = g_anims[r.number]->serial;
            }
            if (r.number < g_gear.size()) {
                r.gear = g_gear[r.number];
                r.drawn = g_drawn[r.number];
                r.color = g_color[r.number];
                r.ocarina = g_ocarina[r.number];
            }
        }
    }
    return remotes;
}

void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& position, bool local_event,
             coop::host::Session* session, std::uint64_t server_now_us) {
    if (!g_configured) {
        return;  // puppets are off
    }
    std::vector<Remote> remotes = Collect(session, server_now_us);
    Gear own_gear;
    std::optional<std::uint8_t> own_drawn;
    std::optional<std::uint32_t> own_color;
    if (session != nullptr) {
        if (const auto& e = session->sampler().last_equipment()) {
            own_gear = GearOf(*e);
            own_drawn = e->drawn;
            own_color = e->color;
        }
    }
    const auto now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (session != nullptr) {
        // ORM choque: our age, for the bodies' heights (BodyGate).
        if (const auto& sample = session->sampler().last_sample(); sample && sample->age) {
            g_planner->SetOurAdult(*sample->age == coop::protocol::Age::adult);
        }
    }
    g_planner->SetFailedTunics(g_director->FailedTunics());
    auto input = g_planner->Plan(main_base, scene, position, local_event, remotes, now_ms, own_gear, own_drawn, own_color);
    // ORM moving floors (orm_ride.h): a partner on a lift or boat drawn on our copy of it.
    OrmRide::Draw(input.targets, input.scene);
    StampDrawing(input, now_ms);
    input.server_now_us = server_now_us;  // ORM smoothing: run on by the director
    input.server_at_ms = now_ms;
    // ORM efectos2: what the effects did, and the status stand-ins' places.
    for (const auto& line : g_planner->TakeEffectLines()) {
        g_director->Note(line);
    }
    if (g_plan_observer) {
        g_plan_observer(input);
    }
    for (const auto& observer : g_plan_observers) {
        observer(input);  // ORM choque: the bodies (coop_bodies.cpp)
    }
    g_director->SetInput(std::move(input));
}

void OnPlan(PlanObserver observer) {
    g_plan_observer = std::move(observer);
}

void AddPlanObserver(PlanObserver observer) {
    if (observer) {
        g_plan_observers.push_back(std::move(observer));
    }
}

void SetColliders(bool on) {
    if (g_configured && g_planner) {
        g_planner->SetColliders(on);
    }
}

void StampDrawing(Input& input, std::uint64_t now_ms) {
    // The camera as the hook's markers judge it (kCameraStale there). Called
    // by the puppets and the fairies in the same frame: the second call finds
    // the run already begun and gives the same answer.
    static bool camera_seen = false;           // the present thread only
    static std::uint64_t drawn_since_ms = 0;
    const auto camera = CoopCamera::Latest();
    const bool drawn = camera && std::chrono::steady_clock::now() - camera->at <
                                     std::chrono::milliseconds(250);
    if (!drawn) {
        drawn_since_ms = 0;
    } else if (drawn_since_ms == 0) {
        drawn_since_ms = now_ms;
    }
    camera_seen = camera_seen || drawn;
    input.camera = camera_seen;
    input.drawn_since_ms = drawn_since_ms;
    input.settle_ms = 1000;
}

} // namespace CoopPuppet
