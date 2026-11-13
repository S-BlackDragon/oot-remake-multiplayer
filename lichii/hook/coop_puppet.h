// Puppets: a remote player shown as an actor in the local game (plan stage 1
// step 2, docs/puppet.md). No emulator code here, so it is tested on its own.
//
// Two threads:
// - the present thread (CoopHook::OnFrame) calls Publish: it decides what
//   should be shown (Planner) and hands it to the Director;
// - the game's calc thread runs the code cave once per game frame
//   (coop_svc.s). Each `svc #0xf4` from it reaches Director::Step, which
//   reads and writes guest memory directly and returns at most one game
//   function for the cave to call: malloc for a puppet's memory block, the
//   create request, the delete request.
// The pose is written straight into the actor every game frame.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_ocarina.h"
#include "coop_snap.h"
#include "coop_puppet_sound.h"

#include "coop/host/pose_smooth.hpp"
#include "coop/host/orm_bodies.hpp"  // ORM orm/cuerpos-continuar: a puppet on our Link drawn aside
#include "coop/host/passages.hpp"
#include "coop_effects.h"
#include "coop_mascaras.h"  // ORM: masks on the puppet's head

namespace coop::protocol {
struct Equipment;
}
namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace CoopPuppet {

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;  // x, y, z, w
using Mat3 = std::array<float, 9>;  // rows of 3, as the actor holds it

// Offsets into main of build e0f3ccfaf50065ae (docs/puppet.md).
namespace addr {
inline constexpr std::uint64_t kCreate = 0x372a70;          // requestCreateActorAsync
inline constexpr std::uint64_t kActorManagerPtr = 0x4e91888;
inline constexpr std::uint64_t kMalloc = 0x14c6c90;
inline constexpr std::uint64_t kRequestDelete = 0xa6490;    // (actor, 0)
// What the create puts in the handle slot is a request: its low 16 bits at
// +0 are its state, +8 the actor. SeqExecuteCreateActor's check (0x32bcb30)
// waits for state 3, then takes the actor with 0x36fbd0(request, 1, 0),
// which wakes it (0x94f30) and returns it, and clears the slot. State 4 it
// hands to 0x36fde0(request); 5 and 6 are failures.
inline constexpr std::uint64_t kTakeCreated = 0x36fbd0;     // (request, 1, 0) -> actor
inline constexpr std::uint64_t kDropCreate = 0x36fde0;      // (request)
inline constexpr std::uint64_t kRequestActor = 0x08;
inline constexpr std::uint32_t kRequestReady = 3;
inline constexpr std::uint32_t kRequestDrop = 4;
// The GOT entry the preload walker reads for create argument +0x40.
inline constexpr std::uint64_t kGotCreateDefault = 0x4c7dad0;

inline constexpr std::uint64_t kActorState = 0x2c;
inline constexpr std::uint64_t kActorPosition = 0x31c;
inline constexpr std::uint64_t kActorRotation = 0x328;
// The pending transform the engine's own setter 0xc6fd60(actor, mtx34, 0)
// (the run of OneShotActorRequestSetMtx, 0x28b2050) leaves on an actor with
// no physics body: position at +0x358, rotation rows at +0x364. The actor's
// calc applies it to +0x31c and +0x328; written with them every frame.
inline constexpr std::uint64_t kActorPositionRequest = 0x358;
inline constexpr std::uint64_t kActorRotationRequest = 0x364;
inline constexpr std::uint32_t kStatePresent = 4;
inline constexpr std::uint64_t kLinkHolder = 0x08;
inline constexpr std::uint64_t kLinkId = 0x10;
inline constexpr std::uint64_t kHolderActor = 0x40;
inline constexpr std::uint64_t kHolderId = 0x48;

// Animation (docs/puppet.md, Animation). The AS controller is actor +0x270,
// then +0x40, +0x18, +0x18; its slot count at +0x18, slots at +0x20 (0x48
// bytes each, the first 8 point to the slot); slot +0x10 the command name,
// +0xe8 the frame.
inline constexpr std::uint64_t kPlayAs = 0x2022a0;      // (request)
inline constexpr std::uint64_t kSetAsFrame = 0x37b5d70; // (controller, slot, s0 = frame)
inline constexpr std::array<std::uint64_t, 4> kControllerPath{0x270, 0x40, 0x18, 0x18};
inline constexpr std::uint64_t kSlotCount = 0x18;
inline constexpr std::uint64_t kSlots = 0x20;
inline constexpr std::uint64_t kSlotName = 0x10;
inline constexpr std::uint64_t kSlotFrame = 0xe8;
// ExecuteASSyncPartialAnimation's call: 0x3abec10(actor, from slot, to slot,
// node 0, 0) plays the command of one slot on another (request skip-if-same
// 1, the source's rate) through 0x37b59c0. Link's upper body and other
// partial slots are filled this way from slot 0.
inline constexpr std::uint64_t kSyncAsSlot = 0x3abec10;
inline constexpr std::int32_t kMaxAsSlots = 8;  // slots synced from slot 0

// Gear (docs/puppet.md, Equipment): the EquipmentUser is [[actor + 0x270] +
// 0x200]; 0x2df7b20(eu, kind, &char*, state 2, -1, 1, 0) wears the named
// piece in a slot kind, as the game's own GameData path does.
inline constexpr std::uint64_t kEquip = 0x2df7b20;
inline constexpr std::uint64_t kActorParts = 0x270;
inline constexpr std::uint64_t kPartsEquipmentUser = 0x200;
// In hand or put away: 0xe34c40(eu, slot, state) is the game's own change of
// an equipment slot's state (the shield node 0x2797850 calls it with slot 4,
// state 1). State 1 is in hand (0x6b6820), 2 put away on the back
// (0xe34c70, the state a piece is worn in). Each sets a bit in the slot's
// byte at EquipmentUser + 0x13bc + slot (2 in hand, 4 put away), which is how
// a call that was taken is told from one that was not. The slot's link is at
// EquipmentUser + 0x20 + 0x18 * slot (u32 id at +0x10, ~0 empty).
inline constexpr std::uint64_t kEquipChangeState = 0xe34c40;
// Taking armor off: 0x10aceb0(eu, index, 0) empties the EquipmentUser's
// armor link `index` (eu + 0xe0 + 0x18 * index, the same {holder, id} shape
// as a hand slot) and deletes its piece. The game's own GameData path
// (0x2df6c80) calls it this way when a PlayerStatus.Equip* armor flag is "":
// kind 7 (head) index 0, 8 (upper) 1, 9 (lower) 2, 10 (hand) 3. It returns
// 0 when there was a piece it could not take off.
inline constexpr std::uint64_t kUnequipArmor = 0x10aceb0;
inline constexpr std::uint32_t kArmorKindFirst = 7;   // ArmorHead
inline constexpr std::uint32_t kArmorKindLast = 10;   // ArmorHand
inline constexpr std::uint64_t kArmorLinks = 0xe0;    // kArmorLinkCount links of kEquipSlotStride
inline constexpr std::uint32_t kArmorLinkCount = 13;
inline constexpr std::uint32_t kEquipStateInHand = 1;
inline constexpr std::uint32_t kEquipStateAway = 2;
inline constexpr std::uint64_t kEquipSlots = 0x20;
inline constexpr std::uint64_t kEquipSlotStride = 0x18;
inline constexpr std::uint64_t kEquipSlotHolder = 0x08;
inline constexpr std::uint64_t kEquipSlotId = 0x10;
inline constexpr std::uint64_t kEquipSlotBits = 0x13bc;
inline constexpr std::uint8_t kEquipBitInHand = 0x2;
inline constexpr std::uint8_t kEquipBitAway = 0x4;
// The byte is a request the game clears when the change is done (seen in a
// two-player run, 2026-11-09), and a setter does nothing when the piece is in
// that state already. The user's slot records (EquipmentUser + 0x4b0, 0xa8
// each) are only logged (LICHII_COOP_DEBUG_ANIM): their byte at +0xa4 read 1
// with the sword out and put away alike (autotest, 2026-11-09), so it is not
// the state.
inline constexpr std::uint64_t kEquipRecords = 0x4b0;
inline constexpr std::uint64_t kEquipRecordStride = 0xa8;
inline constexpr std::uint64_t kEquipRecordInHand = 0xa4;
inline constexpr std::uint64_t kEquipRecordFlags = 0xa0;  // u32, logged only
// The game's own way to move an actor that has a physics body, as the AI
// node ExecuteActorForceSetMtx (run 0x2622e50): write +0x31c..+0x34b, then
// call vtable slot 0x218, which is 0x4c6580(actor, 0) for the vtable every
// actor seen has (docs/objects.md). A plain write is undone within a frame.
inline constexpr std::uint64_t kActorVtable = 0x4b25e18;
inline constexpr std::uint64_t kSetMatrix = 0x4c6580;
}  // namespace addr

// The slot kinds the puppet wears, in Target::gear order: weapon, shield,
// head, armor, boots (reports/s2-equip-create: kinds 0, 6, 7, 8, 9), and the
// item in the hand slot (kind 5, the bow kind: 0x2df7b20 tests slot 3's link
// for it; the slingshot is a bow to the game).
//
// The rest (branch held-items-2) follows the game's own two enums, read from
// rodata. The 18 slot kinds the equip call takes (0x3ef94a8): 0 Weapon,
// 1 SubWeapon, 2 ThirdWeapon, 3 Sheath, 4 Quiver, 5 Bow, 6 Shield,
// 7 ArmorHead, 8 ArmorUpper, 9 ArmorLower, 10 ArmorHand, 11 Tool, 12 SubTool,
// 13 to 16 Accessory1 to 4, 17 Extra. The 8 hand slots (0x3ef2033): 0 Weapon,
// 1 SubWeapon, 2 ThirdWeapon, 3 Bow, 4 Shield, 5 Tool, 6 SubTool, 7 Helmet.
// So the pieces after the held item are: the gauntlets (kind 10, no hand
// slot), and whatever sits in hand slots 1, 2, 5 and 6 (kinds 1, 2, 11, 12:
// which of the Deku stick, boomerang, hookshot, bottle and ocarina uses
// which is for a game run to show; all four are sent and worn the same way).
// The Ocarina (issue 152) is no hand slot: the player wears it as two
// accessories on the belt (Item_Ocarina_Fairy_Holder and Item_Ocarina_Fairy,
// or _Time; both packs have an AccessoryParam, and the player's
// EquipmentUserParam binds Accessory1 and Accessory4 to the bone Pod_B), and
// its playing animations take it in hand. The puppet wears the same two, as
// Accessory1 (kind 13) and Accessory4 (kind 16), from PLAYER_EQUIP's ocarina.
inline constexpr std::size_t kGearCount = 13;
inline constexpr std::size_t kGearHeld = 5;  // index of the held item
inline constexpr std::size_t kGearHand = 6;  // the gauntlets
inline constexpr std::size_t kGearSubWeapon = 7;
inline constexpr std::size_t kGearThirdWeapon = 8;
inline constexpr std::size_t kGearTool = 9;
inline constexpr std::size_t kGearSubTool = 10;
inline constexpr std::size_t kGearOcarinaHolder = 11;
inline constexpr std::size_t kGearOcarina = 12;
using Gear = std::array<std::optional<std::string>, kGearCount>;
inline constexpr std::array<std::uint32_t, kGearCount> kGearKinds{0, 6, 7, 8, 9, 5,
                                                                  10, 1, 2, 11, 12, 13, 16};
inline constexpr std::array<const char*, kGearCount> kGearNames{
    "weapon", "shield",     "head",         "armor", "boots",   "held",
    "hand",   "sub weapon", "third weapon", "tool",  "sub tool", "ocarina holder", "ocarina"};
// Armor (mask, tunic, boots, gauntlets): taken off with addr::kUnequipArmor.
constexpr bool GearIsArmor(std::size_t i) {
    return i < kGearCount && kGearKinds[i] >= addr::kArmorKindFirst &&
           kGearKinds[i] <= addr::kArmorKindLast;
}
// Pieces the game keeps elsewhere while they are put away (seen for the
// slingshot: state 6), so the left-behind check looks at them only in hand.
constexpr bool GearIsCarried(std::size_t i) {
    return i == kGearHeld || (i >= kGearSubWeapon && i <= kGearSubTool);
}
// The equipment slot each of them lands in (seen in the game: the sword in
// slot 0, the shield in slot 4, the slingshot in slot 3); -1: not a piece
// that is drawn or put away.
inline constexpr std::array<int, kGearCount> kGearSlots{0, 4, -1, -1, -1, 3, -1, 1, 2, 5, 6, -1, -1};

// A puppet's memory block, from the game's malloc, kept for the whole run.
namespace block {
inline constexpr std::uint64_t kSize = 0x700;
inline constexpr std::uint64_t kCreateArg = 0x000;  // 0x100 bytes
inline constexpr std::uint64_t kName = 0x100;       // {char*, u32 capacity}
inline constexpr std::uint64_t kHandles = 0x110;    // kHandleCount slots of 8 bytes
inline constexpr std::uint64_t kHandleCount = 6;
inline constexpr std::uint64_t kNameText = 0x140;   // kNameCapacity bytes
inline constexpr std::uint32_t kNameCapacity = 0x40;
// The play request for 0x2022a0: +0 actor, +8 name, +0x10 slot, +0x14 rate,
// +0x18 and +0x1c -1.0, +0x20 skip-if-same, +0x28 node (0, as 0x2a95c4).
inline constexpr std::uint64_t kPlayRequest = 0x200;  // 0x30 bytes
// Two name buffers, used in turn: the slot may keep the pointer it was
// given, and a new name in the same buffer would look like the same one.
inline constexpr std::uint64_t kAnimText = 0x240;
inline constexpr std::uint64_t kAnimCapacity = 0x50;  // 64 characters and the 0
// One name cell per gear kind (kGearKinds order), and the char* to each that
// the equip call is given.
inline constexpr std::uint64_t kGearText = 0x300;
inline constexpr std::uint64_t kGearCapacity = 0x40;
inline constexpr std::uint64_t kGearPtr = 0x640;  // kGearCount x 8 bytes (to 0x6a8)
}  // namespace block

// Guest memory, as the director sees it. Failures are quiet.
class Guest {
public:
    virtual ~Guest() = default;
    virtual bool Read(std::uint64_t address, void* out, std::size_t size) = 0;
    virtual bool Write(std::uint64_t address, const void* in, std::size_t size) = 0;
};

// One game function for the cave to call.
struct Call {
    std::uint64_t function = 0;          // absolute
    std::array<std::uint64_t, 8> x{};    // x0 to x7
    std::array<std::uint64_t, 4> stack{};// what the function sees at sp
    float s0 = 0.0f;                     // the first float argument
};

// A remote player's animation, as PLAYER_SAMPLE field 7 carries it.
struct Anim {
    std::string name;  // the AS command
    float frame = 0.0f;
    float rate = 1.0f;
};

// coop/host/scene.hpp's Visibility, for a puppet that exists.
enum class Show { show, hold, hide };

// What one puppet should show.
struct Target {
    std::uint8_t number = 0;  // the player; 0 is the debug puppet
    std::string actor;        // the actor pack name
    Vec3 position{};
    Mat3 rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    Show show = Show::show;   // hold: frozen as it is; hide: parked out of sight
    const char* why = "";     // for the log when `show` changes ("our cutscene", ...); string literals only
    std::optional<Anim> anim;
    std::uint32_t anim_serial = 0;  // goes up with each received animation
    // What the player wears, in kGearKinds order; unset: not known yet,
    // "" nothing (not taken off yet; the puppet keeps what it has).
    Gear gear;
    // PLAYER_EQUIP's drawn bits: bit i set when the player's equipment slot i
    // is in hand (sword out, shield on the arm). Unset: not known.
    std::optional<std::uint8_t> drawn;
    // The player's colour, 0xRRGGBB (coop_colors.h): the puppet is tinted
    // with it while live. Unset: no tint.
    std::optional<std::uint32_t> color;
    // The player's newest OCARINA_NOTE, played on the puppet (issue 152).
    CoopPuppetSound::Wanted ocarina;
    // How old the player's newest PLAYER_SAMPLE is (server time); unset: not
    // known, taken as fresh. The reveal waits for one under kFreshSampleMs.
    std::optional<std::uint64_t> sample_age_ms;
    // Already in `position`: the sideways step the planner draws the puppet
    // with while it stands on our Link after an event (Planner, presentation
    // only; the sample is kept). Zero: drawn on its sample. For the log.
    Vec3 side_step{};
    float first_sight_m = -1.0f;  // ORM: how far from our Link its first sight here was judged (-1: not yet)
    // ORM bodies (PlannerSettings::bodies): the part of `offset` that draws it
    // out of our Link (coop/host/orm_bodies.hpp); zero: none.
    Vec3 aside{};
    // ORM choque (Planner::SetColliders, coop_bodies.h): its body in our game
    // is solid this plan (coop::host::orm::bodies::BodyGate); false: a ghost,
    // or the colliders are off. `adult`: the player's age (the body's size).
    bool solid = false;
    bool adult = false;
    // What `position` adds to the sample (the side step at its weight now):
    // the director adds it to the smoothed position (SetSmoothing) too.
    Vec3 offset{};
    // ORM (oot-remake docs/PUERTAS.md): drawn only by a puppet that exists;
    // none is made for it (a player walking out of our place, or gone).
    bool keep_only = false;
    // ORM efectos2 (coop_effects.h). `effects`: while the pose copy drives
    // it, its player's command plays on it too when the effect rules allow
    // it (Director::SetEffectRules). `flash_since_ms`: the damage flash
    // began then (0: none); the director blinks it. `status`: the stand-in
    // on it (coop_effects_game.cpp places it through the spawn director).
    bool effects = false;
    std::uint64_t flash_since_ms = 0;
    CoopEffects::Status status = CoopEffects::Status::none;
};

struct Input {
    std::uint64_t main_base = 0;  // 0: no game, nothing is done
    std::string scene;            // the local scene; "" not known
    std::vector<Target> targets;
    // The local game's drawing, from the renderer's camera (coop_camera.h).
    // A puppet created while the game loads a scene (the scene is already
    // readable) is made but never drawn, so a create waits for the drawing
    // (Director::Drawn). `camera`: a camera has been seen since start; false
    // (no camera, or a test) creates at once, as before.
    bool camera = false;
    std::uint64_t drawn_since_ms = 0;  // the current unbroken run of drawn frames; 0 none
    // A create waits until the wanted actor has stayed the same this long:
    // while the other game loads, its age reads adult for a moment, and a
    // wrong-age puppet is made and dropped. 0 (a test) creates at once.
    std::uint64_t settle_ms = 0;
    // The receiver's estimate of the server clock when this was planned, and
    // the steady time then (ms, the calc thread's clock): the director runs
    // it on to its own frame for the smoothing (SetSmoothing). 0: not known.
    std::uint64_t server_now_us = 0;
    std::uint64_t server_at_ms = 0;
};

Mat3 QuatToMatrix(const Quat& q);  // rows; normalises q, identity if it is 0

enum class Phase { no_block, allocating, idle, creating, live, deleting };
const char* PhaseText(Phase phase);
const char* ShowText(Show show);  // "show", "hold", "hide"

class Director {
public:
    using Log = std::function<void(const std::string&)>;
    // `verbose`: a status line per puppet every second (LICHII_COOP_DEBUG_PUPPET).
    // `anim`: a line for each animation played or frame set (LICHII_COOP_DEBUG_ANIM).
    explicit Director(Log log = {}, bool verbose = false, bool anim = false);
    // The Link puppets' director runs the pose copy (coop_pose.h); others, such
    // as the fairies', do not.
    void SetPoseCopy(bool on) { pose_copy_ = on; }
    // Move each puppet with the game's own setter as well (addr::kSetMatrix),
    // once a frame: for an actor with a physics body, such as the fairy.
    void SetMatrixCall(bool on) { matrix_call_ = on; }
    // Tint each live puppet in its target's colour with this strength (the
    // alpha of the game's hit-flash colour, coop_colors.h); 0, the default,
    // writes no colour (the fairies' director).
    void SetTint(float strength) { tint_ = strength; }
    // Tunic puppets (CoopPuppet_<Age>_<Colour>) found not to work: the mod is
    // a pack from before the colours (two create timeouts of that actor while
    // no tunic puppet has spawned yet), or one spawned without a Player_ model
    // (kTunicModelCheckMs). The planner uses the plain puppet for these. Any
    // thread.
    std::vector<std::string> FailedTunics() const {
        std::lock_guard lock(tunic_mutex_);
        return {tunic_failed_.begin(), tunic_failed_.end()};
    }
    // How long after a tunic puppet spawns its model is checked.
    static constexpr std::uint64_t kTunicModelCheckMs = 3000;
    // CoopPuppet_<Age>_<Colour>, not the plain CoopPuppet_<Age>.
    static bool IsTunicActor(std::string_view name) {
        constexpr std::string_view kBase = "CoopPuppet_";
        return name.substr(0, kBase.size()) == kBase &&
               name.find('_', kBase.size()) != std::string_view::npos &&
               !name.ends_with("_Lock");  // ORM orm/pvp: the lock-on variant is no tunic
    }
    // The reveal (the Link puppets'): a new puppet is made parked
    // kHideDrop under its spot and kept there, posed but out of sight, until
    // it has both a fresh sample (Target::sample_age_ms under
    // kFreshSampleMs) and a first pose on it (a whole fresh pose from the
    // pose copy, or, for a player who sends none, its AS command played and
    // its frame set); then it shows at the sampled position, so no frame of
    // T-pose or of a stale place is drawn. A live puppet whose player was out
    // of the plan for kBackHideMs or more (a reconnect, a stall) goes through
    // the same reveal when it comes back, instead of snapping to the new
    // place. After kRevealTimeoutMs it shows whatever it has, with a line.
    // The debug puppet (number 0) shows at once.
    void SetRevealGate(bool on) { reveal_gate_ = on; }
    // ORM choque (coop_bodies.h): every actor is made kHideDrop under its spot
    // and placed by the next pose (shown, or parked for a hide), without the
    // reveal's wait: a body made where its mate stands could appear on our
    // Link for a frame and the game's physics push him.
    void SetCreateParked(bool on) { create_parked_ = on; }
    // ORM (coop_mascaras.h, LICHII_ORM_MASK_FOLLOW): each calc frame after
    // the pose copy, a live puppet's mask or lens is put on its head.
    void SetHeadFollow(bool on) { head_follow_ = on; }
    // ORM smoothing (oot-remake docs/SUAVIZADO.md, LICHII_ORM_SMOOTH): where
    // each shown puppet is drawn, asked each calc frame with the server time
    // of that frame (Input's run on): position (plus Target::offset) and
    // rotation from coop::host::PoseSmoother instead of the plan's, the body
    // pose shown for the same time (CoopPose::OnCalcFrame's render_us, the
    // server time less `delay_us`), and the AS fallback plays the sender's
    // command of that time. Without a source (the fairies, a test, the
    // switch off) everything is as before.
    using SmoothSource =
        std::function<std::optional<coop::host::SmoothedPose>(std::uint8_t number,
                                                              std::uint64_t server_now_us)>;
    void SetSmoothing(SmoothSource source, std::uint64_t delay_us) {
        smooth_ = std::move(source);
        smooth_delay_us_ = delay_us;
    }
    // ORM riesgos-visual (coop::host::DelayTuner): the delay the bodies are
    // drawn behind changed for everyone. Any thread.
    void SetSmoothDelay(std::uint64_t delay_us) { smooth_delay_us_.store(delay_us); }
    // ORM efectos2: the rules a Target::effects puppet's commands are played
    // by while posed (coop_effects.h). Before the game runs; the built-in
    // list until then.
    void SetEffectRules(CoopEffects::Rules rules) { effect_rules_ = std::move(rules); }

    // Any thread. The next frame of the cave uses it.
    void SetInput(Input input);

    // The game's calc thread, at each `svc #0xf4`. `after_call`: the cave
    // made the call returned last time and `result` is its x0. Returns the
    // next call, or nothing for "done for this frame".
    std::optional<Call> Step(Guest& guest, bool after_call, std::uint64_t result,
                             std::uint64_t now_ms);

    struct SlotView {
        std::uint8_t number = 0;
        Phase phase = Phase::no_block;
        std::uint64_t block = 0;
        std::uint64_t actor = 0;
        std::string actor_name;
        std::string anim_name;  // what was last played on it
    };
    std::vector<SlotView> Slots() const;  // the calc thread, or tests
    // Any thread: bit n set while puppet n's actor is live in this scene and
    // shown or held (not parked for a hide), as of the last calc frame. The
    // fairies follow it, so a Navi never shows without her puppet (issue 145).
    std::uint32_t LiveMask() const { return live_mask_.load(std::memory_order_relaxed); }
    std::uint64_t Frames() const { return frames_; }
    void Note(const std::string& text) const { Say(text); }  // any thread

    static constexpr int kMaxCallsPerFrame = 8;      // the cave allows 16
    static constexpr std::uint64_t kCreateTimeoutMs = 10000;
    // A create the game refuses (it returns 0: the scene is still being
    // unloaded or loaded) made nothing; it is asked again after this, not
    // after the create timeout and the 5 s wait.
    static constexpr std::uint64_t kRefusedRetryMs = 1000;
    // After this many refusals in a row (a long load or cutscene), every 5 s.
    static constexpr int kRefusedFast = 3;
    static constexpr std::uint64_t kRefusedSlowMs = 5000;
    // A tunic actor (CoopPuppet_<Age>_<Colour>) refused this many times in a
    // row, before any tunic puppet spawned, is not in the pack: the plain
    // puppet from then on (about 3 x 1 s + 3 x 5 s, longer than a load).
    static constexpr int kRefusedTunicFallback = 6;
    // A live puppet whose player drops out of the plan in the same scene
    // (a stalled or late peer fades to 0 for a moment) is kept this long
    // before it is deleted, so a hiccup does not delete and create it again.
    static constexpr std::uint64_t kLeaveGraceMs = 2000;
    // A received frame further than this from the puppet's is set on it.
    static constexpr float kFrameSlack = 3.0f;
    // A hidden puppet is moved this far down, under the floor.
    static constexpr float kHideDrop = 500.0f;
    // ORM riesgos-visual (OcarinaBelt): frames the belt Ocarina is parked
    // before it is judged, and how far apart the two must be to be two.
    static constexpr int kBeltJudgeFrames = 10;
    static constexpr float kBeltApart = 0.15f;
    // ORM (OcarinaBelt, the hand check; docs/SONDAS.md 2026-11-12): while its player plays the
    // Ocarina freely, the puppet's own Ocarina piece is judged after kHandJudgeFrames: above
    // kHandUp of the puppet's height it is in the hand (the player's own rose 0.49 -> 0.78).
    static constexpr int kHandJudgeFrames = 45;
    static constexpr float kHandUp = 0.65f;
    // A worn piece this far from its shown puppet was left behind (made while
    // the puppet was parked under the floor, seen 2026-11-09): worn again.
    static constexpr float kGearFar = 5.0f;
    static constexpr std::uint64_t kRewearMs = 1000;  // at most once a second
    static constexpr std::uint64_t kFarForMs = 100;   // far on two readings this far apart
    static constexpr std::uint8_t kRewearTries = 3;   // then given up, with one line
    static constexpr std::uint64_t kTakeOffMs = 1500;  // a slot empty this long: the piece is taken off
    // A create waits until the game has drawn this long without a break,
    // begun after the last scene change or puppet loss (a load drops the
    // camera)...
    static constexpr std::uint64_t kDrawnMs = 750;
    // ...or, when the drawing never broke, this long after it.
    static constexpr std::uint64_t kSteadyMs = 3000;
    // The reveal (SetRevealGate).
    static constexpr std::uint64_t kFreshSampleMs = 200;
    static constexpr std::uint64_t kRevealTimeoutMs = 3000;
    static constexpr std::uint64_t kBackHideMs = 500;
    // A live puppet whose actor reads neither present (4) nor gone (7 to 11),
    // say 6, is not posed, revealed or moved. When the plan wants it seen
    // (not hidden for our cutscene or a ride) and it stays so this long, it
    // is deleted and made again: parked under the floor it would stay there
    // for good (review 2026-11-11, oot-remake docs/REVISION_OBJETOS.md).
    static constexpr std::uint64_t kAbsentMs = 3000;

private:
    struct Slot {
        std::uint8_t number = 0;
        Phase phase = Phase::no_block;
        std::uint64_t block = 0;
        std::uint64_t handle = 0;   // the handle slot of the current create
        std::uint64_t handle_index = 0;
        std::uint64_t actor = 0;    // resolved this frame; 0 none
        std::uint64_t request = 0;  // what the handle slot held
        std::uint64_t taken = 0;    // the actor 0x36fbd0 gave
        std::uint64_t vtable = 0;   // its vtable then, to notice reuse
        bool take = false;          // call 0x36fbd0 next
        bool drop = false;          // call 0x36fde0 next
        std::string actor_name;     // what was created
        std::string scene;          // where it was created
        std::uint64_t since_ms = 0; // when the phase began
        std::uint64_t retry_ms = 0; // no new malloc or create before this
        std::uint64_t missing_ms = 0; // when its player left the plan; 0 wanted
        std::uint64_t lost_ms = 0;    // when the engine deleted it (a load, often)
        bool wait_said = false;       // "waiting for the game to draw" logged
        std::string wanted_actor;     // the actor its target asked for, last seen
        std::uint64_t wanted_since_ms = 0;  // since when (Input::settle_ms)
        int refused = 0;                    // creates refused in a row
        std::string anim_name;        // played on it; "" none yet
        CoopOcarina::HandBind ocarina_hand;  // issue 152: Ocarina in hand while it plays
        std::string their_anim;              // their last AS command (debug log)
        std::uint32_t anim_serial = 0;
        std::uint64_t anim_buffer = 0;  // which name buffer is next
        std::optional<float> frame_to_set;  // after a play
        std::optional<float> held_frame;    // while held
        CoopSnap::Smoother smooth;          // issue 194: where it is drawn, jumps eased
        CoopSnap::Meter meter;              // issue 194: its place read back, snaps logged
        bool meter_skip = false;            // the next read is still the parked spot
        Show shown = Show::show;            // the last `show` acted on (log on change)
        bool model_checked = false;         // a tunic puppet's model (kTunicModelCheckMs)
        std::uint64_t frame_set_in = 0;     // the cave frame of the last frame set
        std::string play_name;              // the play being made
        float play_frame = 0.0f;
        std::uint32_t play_serial = 0;
        std::int32_t sync_next = 0;         // next slot to sync from slot 0; 0 none
        std::uint64_t pose_log_ms = 0;      // last read-back line (debug anim)
        std::array<std::string, kGearCount> worn;    // equipped on it, by gear index
        std::size_t equip_index = 0;        // the equip being made
        std::string equip_name;
        std::uint64_t gear_log_ms = 0;      // last gear line (debug anim)
        std::uint8_t in_hand = 0;           // equipment slots asked into the hand (bits)
        std::uint8_t hold_known = 0;        // slots whose state we have set (bits)
        int hold_slot = 0;                  // the change being made
        bool hold_in_hand = false;
        std::uint64_t hold_retry_ms = 0;    // no new change before this
        std::array<std::uint32_t, kGearCount> hold_id{};  // the piece each change was made for
        std::uint64_t tint_actor = 0;       // the actor the colour line was logged for
        std::uint32_t tint_color = 0;       // and its colour
        std::uint64_t rewear_ms = 0;        // no new "left behind" check before this
        std::array<std::uint64_t, kGearCount> far_since_ms{};  // first far reading of each piece; 0 near
        std::array<std::uint8_t, kGearCount> rewear_tries{};   // re-wears since the piece was last near
        // A worn piece its player took off: armor (mask, tunic, boots,
        // gauntlets) with the game's own call (addr::kUnequipArmor); a sword
        // or a shield, for which no call is known, or armor the call
        // refused, makes the puppet again without it (Equip, issue 91).
        bool remake = false;
        std::array<std::uint64_t, kGearCount> off_since_ms{};  // its player's slot empty since (0: not)
        std::array<bool, kGearCount> unequip_refused{};        // the take-off call returned 0
        // ORM (coop_detalles.h): armor the puppet put on by itself from our
        // own flags, which its player does not wear, taken off too.
        std::string unequip_name;                              // the piece being taken off
        std::array<std::uint8_t, kGearCount> stray_offs{};     // such take-offs so far
        std::uint64_t moved_frame = 0;      // the cave frame of the last matrix call
        CoopPuppetSound::State sound;       // the player's Ocarina on it (issue 152)
        // The reveal (SetRevealGate): false while it is kept parked.
        bool revealed = true;
        std::uint64_t reveal_since_ms = 0;  // when the wait began (live, or back)
        const char* reveal_why = "";        // "new" or "back"; string literals only
        bool side_said = false;             // the side step's line was logged
        std::optional<Vec3> aside_drawn;    // ORM bodies: written last pass while drawn aside (read back next)
        bool first_said = false;            // the first sight's line was logged
        std::uint64_t absent_since_ms = 0;  // live, wanted seen, out of state 4 since; 0 not (kAbsentMs)
        // ORM smoothing: the sender's command at the shown time (Animate
        // plays it instead of the newest), and the rate last played.
        std::optional<Anim> smooth_anim;
        std::uint32_t smooth_serial = 0;
        // ORM riesgos-visual (OcarinaBelt): the belt Ocarina parked out of
        // sight while the one in the hand shows; 0 none parked.
        std::uint64_t belt_parked = 0;
        Vec3 belt_offset{};                 // where it was from the puppet, to put it back
        int belt_frames = 0;                // frames parked (judged at kBeltJudgeFrames)
        int hand_frames = 0;                // frames its player plays freely (hand check)
        std::uint64_t hand_piece = 0;       // OcarinaHandPlace: its Ocarina, put in the hand
        Vec3 hand_back{};                   // where that piece was from the puppet, to put it back
        bool hand_judged = false;           // the hand check said this play
        float anim_rate = 1.0f;
        float play_rate = 1.0f;
        coop::host::Gait gait = coop::host::Gait::unknown;  // last logged (debug anim)
        bool effects_said = false;  // ORM efectos2: the first posed command was logged
    };
    enum class Pending {
        none, malloc, create, take, drop, remove, play, frame, sync, equip, unequip, hold, move,
        sound
    };

    void NewFrame(Guest& guest, std::uint64_t now_ms);
    void PoseFrame(Guest& guest, std::uint64_t now_ms);
    void FollowHeads(Guest& guest);  // ORM: SetHeadFollow
    // ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md, OCARINA.md
    // "dos ocarinas"): while a puppet's Ocarina is in its hand (OcarinaInHand),
    // the belt one (the Accessory4 piece) would show too. Each frame it is
    // parked kHideDrop under its place; kBeltJudgeFrames later, if it read
    // parked, "coop: check: ocarina belt ok" (one Ocarina shows); if the
    // game's binding put it back each time, "fallback" and no more parking
    // this run (two show, as before). Put back by the puppet when the hand
    // one is put away.
    void OcarinaBelt(Guest& guest);
    // ORM (docs/SONDAS.md): whether a puppet's own Ocarina goes to its hand while its player plays.
    void OcarinaHandCheck(Guest& guest, Slot& slot, const Target* t);
    // ORM ocarina-mano (round 5, docs/SONDAS.md): the game moves a player's own Ocarina from the
    // belt to his hands by its own state (the holster's AI asks its owner), which a puppet never
    // runs, and the sub weapon one is never made. While the puppet's player plays freely, its own
    // Ocarina piece is put each frame where the player's was measured (kOcarinaHeld: in the frame
    // of skeleton matrix 0, which reads as the model's turn at the actor's place; the actor frame
    // when that does not read), and back on the belt when the play ends. True while it places.
    bool OcarinaHandPlace(Guest& guest, Slot& slot, const Target* t);
    void Finish(Guest& guest, std::uint64_t result, std::uint64_t now_ms);
    std::optional<Call> Next(Slot& slot, Guest& guest, std::uint64_t now_ms);
    std::optional<Call> Create(Slot& slot, const Target& target, Guest& guest);
    std::uint64_t Resolve(Slot& slot, Guest& guest);
    // `only` (ORM efectos2): this command and serial instead of the target's.
    std::optional<Call> Animate(Slot& slot, const Target& target, Guest& guest,
                                const Anim* only = nullptr, std::uint32_t only_serial = 0);
    // ORM efectos2: the command played while the pose copy drives it.
    std::optional<Call> PosedAnimate(Slot& slot, const Target& target, Guest& guest);
    std::optional<Call> Equip(Slot& slot, const Target& target, Guest& guest,
                              std::uint64_t now_ms);
    std::optional<Call> Hold(Slot& slot, const Target& target, Guest& guest,
                             std::uint64_t now_ms);
    void LeftBehind(Slot& slot, Guest& guest, std::uint64_t now_ms);
    std::optional<Call> TakeOffStray(Slot& slot, Guest& guest, std::size_t i,
                                     std::uint64_t now_ms);  // ORM, coop_detalles.h
    std::optional<Call> Move(Slot& slot, Guest& guest);
    std::optional<Call> SetFrame(Slot& slot, std::uint64_t controller, float frame);
    static std::uint64_t Controller(Guest& guest, std::uint64_t actor);
    static std::optional<float> Frame(Guest& guest, std::uint64_t controller);
    // The colour into the live puppet's model (coop_colors.h), every frame
    // it is not there yet (a new actor, or the game wrote its own).
    void Tint(Slot& slot, Guest& guest, std::uint32_t color);
    void Pose(Slot& slot, Guest& guest, const Vec3& position, const Mat3* rotation,
              std::uint64_t now_ms);
    void DrawAt(Slot& slot, Guest& guest, const Target& target, std::uint64_t now_ms);
    void Poll(Slot& slot, Guest& guest, std::uint64_t now_ms);
    const Target* Wanted(std::uint8_t number) const;
    bool Drawn(const Slot& slot, std::uint64_t now_ms) const;
    void Reveal(Slot& slot, const Target& target, std::uint64_t now_ms);
    Slot& SlotFor(std::uint8_t number);
    void Say(const std::string& text) const;
    void Status(Guest& guest, std::uint64_t now_ms);
    std::uint64_t ServerNow(std::uint64_t now_ms) const;  // 0: not known
    // The smoothed place of a shown puppet this frame, into `at` and `rot`.
    void Smooth(Slot& slot, const Target& target, std::uint64_t now_ms, Vec3& at, Mat3& rot);

    Log log_;
    bool verbose_;
    bool anim_;
    bool pose_copy_ = false;
    bool matrix_call_ = false;
    float tint_ = 0.0f;
    mutable std::mutex tunic_mutex_;
    std::set<std::string> tunic_failed_;           // guarded by tunic_mutex_
    std::map<std::string, int> tunic_timeouts_;    // by actor name
    bool tunic_proven_ = false;  // a tunic puppet spawned with its model: the pack has them
    void TunicFails(const std::string& actor, const std::string& why);
    bool reveal_gate_ = false;
    bool create_parked_ = false;  // ORM choque: SetCreateParked
    bool head_follow_ = false;
    bool belt_park_failed_ = false;  // OcarinaBelt: the binding undoes the park; never again this run
    CoopMascaras::Heads heads_;  // the calc thread
    SmoothSource smooth_;
    std::atomic<std::uint64_t> smooth_delay_us_{0};  // ORM riesgos-visual: SetSmoothDelay from the tuner
    CoopEffects::Rules effect_rules_ = CoopEffects::Rules::Default();
    std::uint32_t posed_ = 0;  // the last PoseFrame's whole fresh poses (CoopPose::OnCalcFrame)

    mutable std::mutex input_mutex_;
    Input next_input_;  // guarded

    // The calc thread only.
    Input input_;
    std::vector<Slot> slots_;
    coop::host::orm::bodies::ReadBack read_back_;  // ORM bodies: a puppet drawn aside, read back
    Pending pending_ = Pending::none;
    std::uint8_t pending_slot_ = 0;
    int calls_ = 0;  // this frame
    std::uint64_t frames_ = 0;
    std::atomic<std::uint32_t> live_mask_{0};  // LiveMask(); written by the calc thread
    std::uint64_t frames_at_status_ = 0;
    std::uint64_t status_ms_ = 0;
    std::string scene_;               // input_.scene as last seen
    std::uint64_t scene_since_ms_ = 0;
};

// The present thread's side: what to show this frame.
// A player's PLAYER_EQUIP as the puppet's pieces, in kGearKinds order.
Gear GearOf(const coop::protocol::Equipment& equipment);

// The Ocarina in the hand while its player plays (ORM, issue 152,
// oot-remake docs/OCARINA.md). The belt pieces (kGearOcarinaHolder,
// kGearOcarina) are what the player wears; on the player the ocarina goes
// to the hand in the player's own code, which the puppet does not run (as a
// drawn sword needs the change-state call on the puppet, not just its draw
// animation). So while the player's AS command is an Ocarina one
// (coop::host::Activity::ocarina) the target also carries the Ocarina as the
// carried piece `piece` (kGearSubWeapon: slot 1, the left hand Weapon_L,
// where the Deku stick and the bottle were seen in the puppet's hand;
// kGearHeld: slot 3, the right hand Weapon_R) with that slot's drawn bit
// set, so the Director equips it and draws it into the hand; when the
// command changes, the player's own slot comes back and the piece is put
// away. No change, false: `piece` is kGearCount (off) or not a hand slot,
// or the player has no Ocarina.
inline constexpr std::size_t kOcarinaHandOff = kGearCount;
bool OcarinaInHand(Target& target, std::size_t piece);
// LICHII_ORM_OCARINA_HAND: "" or "sub" kGearSubWeapon, "bow" kGearHeld,
// "0" or "off" kOcarinaHandOff; anything else is the default too.
std::size_t OcarinaHandPiece(std::string_view setting);

struct Remote {
    std::uint8_t number = 0;
    std::optional<std::string> scene;
    Vec3 position{};
    std::optional<Quat> rotation;
    float fade = 1.0f;
    std::uint8_t status = 0;  // PLAYER_STATUS flags
    bool adult = false;
    std::optional<Anim> anim;
    std::uint32_t anim_serial = 0;
    Gear gear;  // as Target::gear
    std::optional<std::uint8_t> drawn;               // as Target::drawn
    // PLAYER_EQUIP's colour; unset: the player chose none, and Plan gives the
    // player's slot colour (coop::protocol::slot_color).
    std::optional<std::uint32_t> color;
    CoopPuppetSound::Wanted ocarina;                 // as Target::ocarina
    std::optional<std::uint64_t> sample_age_ms;      // as Target::sample_age_ms
};

struct PlannerSettings {
    // The actor pack; "" picks CoopPuppet_Child or CoopPuppet_Adult by the
    // player's age (LICHII_COOP_PUPPET_ACTOR overrides it).
    std::string actor;
    bool debug_anim = false;      // LICHII_COOP_DEBUG_ANIM
    // Each player's tunic in their colour (docs/puppet.md, "Colour"): the
    // puppet pack's CoopPuppet_<Age>_<Colour>. LICHII_COOP_TUNICS=0: off.
    bool tunics = true;
    // Your own Link wears another colour (LICHII_COOP_SELF_TUNIC, set by the
    // kit when it put that colour's tile over the game's): the plain puppet
    // would wear it too, so a green player gets CoopPuppet_<Age>_Green.
    bool green_puppet = false;
    bool debug = false;           // LICHII_COOP_TEST_PUPPET: one more, beside you,
                                  // wearing your own gear
    std::uint64_t debug_delay_ms = 3000;  // after the scene is known
    Vec3 debug_offset{2.0f, 0.0f, 0.0f};  // from you, in game units (about metres)
    // The piece a remote player's Ocarina is carried as while they play
    // (OcarinaInHand; LICHII_ORM_OCARINA_HAND); kOcarinaHandOff: belt only.
    std::size_t ocarina_hand = kGearSubWeapon;
    // ORM: LICHII_ORM_SMOOTH (on unless =0): the Link puppets' director
    // smooths (Director::SetSmoothing, oot-remake docs/SUAVIZADO.md).
    bool smooth = true;
    // ORM D-R19 (LICHII_COOP_HORSE, on by default; coop_epona.h): a rider is
    // shown getting on and in the saddle (a horse of ours goes there), not parked.
    bool riders = false;
    // ORM (oot-remake docs/PUERTAS.md): leaving and arriving players
    // (coop/host/passages.hpp). LICHII_COOP_PASSAGES=0 turns it off.
    bool passages = true;
    // ORM bodies (LICHII_ORM_BODIES, on unless =0; oot-remake docs/CUERPOS.md):
    // a puppet within two body radii of our Link is drawn out to that
    // distance (coop::host::orm::bodies::PuppetAside). Our Link is never moved.
    bool bodies = false;
    // ORM efectos2 (coop_effects.h): the effect commands, the damage flash
    // and the status stand-ins. All off here; coop_svc.cpp turns them on
    // (LICHII_ORM_EFFECTS, LICHII_ORM_DAMAGE_FLASH, LICHII_ORM_STATUS_FX).
    CoopEffects::Settings effects;
    // ORM choque (coop_bodies.h): the bodies' gate runs from the start (a
    // test); in the game coop_bodies.cpp turns it on and off each frame
    // (Planner::SetColliders) by the room's /choque, its switch and the pack.
    bool colliders = false;
};

// A puppet pack's coop_puppet.json lists both green puppets
// (CoopPuppet_Child_Green, CoopPuppet_Adult_Green): only then may
// PlannerSettings::green_puppet ask for them. An older pack has neither, and a
// green player would wait for an actor ActorInfo does not know.
bool PackHasGreenPuppets(std::string_view manifest);

// A puppet pack's coop_puppet.json lists at least one tunic puppet
// (CoopPuppet_Child_<Colour> or CoopPuppet_Adult_<Colour>). A pack made
// before the tunics (or with --tunics none) lists only the plain ones; the
// game refuses an actor its ActorInfo does not have, so with such a pack every
// puppet must be the plain one (Hookshot proof 3: no puppet at all).
bool PackHasTunicPuppets(std::string_view manifest);

class Planner {
public:
    explicit Planner(PlannerSettings settings = {})
        : settings_(std::move(settings)), colliders_(settings_.colliders) {}
    // `local_event`: our own cutscene runs (it hides every puppet;
    // coop::host::local_event_hides). Each remote's case is
    // coop::host::visibility, with its animation (riding shows it, issue 199).
    Input Plan(std::uint64_t main_base, const std::optional<std::string>& scene,
               const std::optional<Vec3>& position, bool local_event,
               const std::vector<Remote>& remotes, std::uint64_t now_ms,
               const Gear& own_gear = {},
               std::optional<std::uint8_t> own_drawn = {},
               std::optional<std::uint32_t> own_color = {});
    // The actor for a player of that age and colour: CoopPuppet_<Age>, or
    // CoopPuppet_<Age>_<Colour> (CoopColors::TunicOf) while tunics are on.
    std::string ActorFor(bool adult, std::optional<std::uint32_t> color = {}) const;
    void SetTunics(bool on) { settings_.tunics = on; }
    void SetGreenPuppet(bool on) { settings_.green_puppet = on; }
    // Tunic actors that do not work here (Director::FailedTunics): the plain
    // puppet for them.
    void SetFailedTunics(std::vector<std::string> names) { failed_tunics_ = std::move(names); }
    const PlannerSettings& settings() const { return settings_; }
    // ORM efectos2: the effect changes of the plans since the last call, one
    // line each ("P2 effects: flash ('Damage_M_F')"), for the log.
    std::vector<std::string> TakeEffectLines();
    // ORM choque: each mate's body gate (Target::solid) from the next plan on;
    // off makes every body a ghost again (the next on waits until clear).
    // SetOurAdult: our Link's age, for the gate's heights (Publish sets it
    // from our last sample; a child's until known). The present thread.
    void SetColliders(bool on);
    bool Colliders() const { return colliders_; }
    void SetOurAdult(bool adult) { our_adult_ = adult; }

    // Two Links on one spot after a shared event (the Deku Tree's talk puts
    // both in front of the Tree): when a puppet's sample stays within
    // kOverlapNear of our Link for kOverlapForMs, begun within
    // kOverlapWindowMs after an event ended (ours, or the in-cutscene bit of
    // theirs), the puppet is drawn kSideStep to its side, away from us,
    // faded in over kSideStepInMs. The sample itself is not changed. The
    // step fades out over kSideStepOutMs once they walk kSideStepLeave from
    // that spot, we get kSideStepApart away, or an event runs again.
    static constexpr float kOverlapNear = 0.5f;
    static constexpr std::uint64_t kOverlapForMs = 1000;
    static constexpr std::uint64_t kOverlapWindowMs = 10000;
    static constexpr float kSideStep = 0.6f;
    static constexpr std::uint64_t kSideStepInMs = 250;
    static constexpr std::uint64_t kSideStepOutMs = 500;
    static constexpr float kSideStepLeave = 1.0f;
    static constexpr float kSideStepApart = 1.5f;
    // ORM (oot-remake docs/SONDAS.md round 7): a puppet whose first plan in a scene with our Link read (in an
    // event too: the game's boot event runs while it is first shown) finds it on our Link (two
    // players who pressed Continue at one spot: sondas22r7, B's puppet shown 0.17 m from A, A pushed 0.8 m and
    // then walking on the spot for a minute) is drawn kFirstSightStep aside at once, with no fade, before the
    // game's physics can push our Link off it.
    static constexpr float kFirstSightStep = 1.0f;

private:
    struct Overlap {
        bool cutscene = false;           // their in-cutscene bit, last plan
        std::uint64_t event_end_ms = 0;  // their cutscene ended
        std::uint64_t near_since_ms = 0; // on our Link since; 0 not
        bool on = false;                 // the step is wanted
        Vec3 step{};                     // the full step
        Vec3 anchor{};                   // their sample when it began
        float weight = 0.0f;             // how much of it is drawn
        std::uint64_t last_ms = 0;       // the last plan, for the fade
        bool judged = false;             // its first sight (with our Link read) was looked at
        bool first = false;              // the step on is a first sight's (kept through events)
        float first_m = -1.0f;           // our Link's distance then
    };
    // The full step (zero while none is drawn) and how much of it to draw.
    // `shown`: drawn this plan (its first sight is judged only then: a mate still loading sends no place yet).
    std::pair<Vec3, float> SideStep(const Remote& remote, const std::optional<Vec3>& ours,
                                    bool local_event, std::uint64_t now_ms, bool shown = true);
    float FirstSightM(std::uint8_t number) const;  // Overlap::first_m of that player (-1: none)

    PlannerSettings settings_;
    std::vector<std::string> failed_tunics_;
    std::string scene_;
    std::uint64_t scene_since_ms_ = 0;
    std::optional<Vec3> debug_spot_;
    CoopEffects::Tracker effects_;            // ORM efectos2
    std::vector<std::string> effect_lines_;   // TakeEffectLines
    bool local_event_ = false;
    std::uint64_t local_event_end_ms_ = 0;
    std::vector<std::pair<std::uint8_t, Overlap>> overlaps_;
    coop::host::Passages passages_;  // ORM: PlannerSettings::passages
    coop::host::orm::bodies::PuppetAside aside_;  // ORM: PlannerSettings::bodies
    coop::host::orm::bodies::BodyGate gate_;      // ORM choque: SetColliders
    bool colliders_ = false;
    bool our_adult_ = false;
};

// The process-wide pair the hook and the SVC handler share.
// Configure once, before the game starts (CoopSvc does it).
void Configure(PlannerSettings settings, bool verbose, Director::Log log);
Director& TheDirector();
// Any thread: TheDirector's LiveMask(), 0 while the puppets are off.
std::uint32_t LivePuppets();

// ORM orm/pvp (oot-remake docs/COMANDOS.md, "Fijar aliados"): a suffix added
// to the Link puppets' actor name ("_Lock": the pack's lock-on variant); ""
// none (the default). A live puppet whose name changes is made again. Any
// thread. LockTally: lock-on puppets made and failed since the start (any
// thread), for the ally target's check.
void SetActorSuffix(std::string suffix);
std::string ActorSuffix();
struct LockTally {
    std::uint32_t made = 0;
    std::uint32_t failed = 0;
};
LockTally Locks();

// ORM smoothing (oot-remake docs/EPONA.md, REVISION_NUEVAS orm/epona): the
// pose the Link puppets' director last drew player n with (the smoothing
// source it asks each calc frame), and the server time it was asked for. The
// riders' horses (coop_epona.h) are drawn from this very pose, so a horse and
// its rider are one smoothed place. Any thread; nothing while the smoothing is
// off, before a first pose, or after the player left.
struct ShownPose {
    std::uint64_t server_us = 0;
    coop::host::SmoothedPose pose;
};
std::optional<ShownPose> LastSmoothed(std::uint8_t number);

// Every remote player presence shows at `server_now_us`, with scene, status,
// age, newest animation and gear: what Publish plans from. CoopFairy plans
// from the same list, so a Navi hides when its puppet does. Null session or
// zero time: none.
std::vector<Remote> Collect(coop::host::Session* session, std::uint64_t server_now_us);

// The present thread: fills `input`'s camera, drawn_since_ms and settle_ms
// from the renderer's camera (coop_camera.h), so a create waits until the
// game draws again after a load. Publish does it for the puppets; CoopFairy
// does it for the fairies, whose creates were refused or never drawn while a
// scene loaded (issue 93).
void StampDrawing(Input& input, std::uint64_t now_ms);

// ORM efectos2: called by Publish with each plan before the director gets
// it (the present thread); coop_effects_game.cpp places the status
// stand-ins from it. One observer; an empty one removes it.
using PlanObserver = std::function<void(const Input&)>;
void OnPlan(PlanObserver observer);
// ORM choque: more observers, each called after OnPlan's, in the order
// added (coop_bodies.cpp's body director). Before the game runs.
void AddPlanObserver(PlanObserver observer);
// ORM choque: the puppets' planner's Planner::SetColliders. The present thread.
void SetColliders(bool on);

// CoopHook::OnFrame, once per present, with no lock held. `session` may be
// null (not connected): then only the debug puppet is planned.
// `local_event`: coop::host::local_event_hides (our cutscene; not the Ocarina).
void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& position, bool local_event,
             coop::host::Session* session, std::uint64_t server_now_us);

// The session's on_event (network thread): keeps each remote player's latest
// animation from PLAYER_SAMPLE field 7 and gear from PLAYER_EQUIP.
void OnEvent(const coop::net::Event& event);

// The colour another player chose (their PLAYER_EQUIP), unset when they chose
// none or are not known; and ours (SetOwnColor, from the settings). For the
// co-op window's names (coop_window.cpp). Any thread.
std::optional<std::uint32_t> PlayerColor(std::uint8_t number);
void SetOwnColor(std::optional<std::uint32_t> rgb);
std::optional<std::uint32_t> OwnColor();
// The colour to show player `number` in (we are `self`): theirs (ours), else
// their slot's (coop::protocol::slot_color).
std::uint32_t NameColor(std::uint8_t number, std::uint8_t self);

} // namespace CoopPuppet
