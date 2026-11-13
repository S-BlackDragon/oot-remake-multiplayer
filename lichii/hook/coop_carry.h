// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "Levantar" (oot-remake docs/DETALLES_MARIONETA.md): what a player lifts
// is seen in his hands on every screen. Pots, rocks (the Goron silver rock
// with the gauntlets too), liftable crates and bushes, Cuccos, bomb flowers
// and child Ruto.
//
// - Ours (Lifter): when our own Link's AS command starts a lift (Lift_Ready_*,
//   Lift_Up_*, from Link's AS Player_<Child|Adult>.root.asb), the shared
//   liftable nearest in front of him is ours to carry: the objects sync flags
//   it targeting in OBJECT_PRESENT, so the server hands it to us (as for a
//   townsperson we talk to, coop/host/npc_talk.hpp), and our copy is left to
//   our game meanwhile (no pose or animation of the old owner written on it).
//   While Link holds it, its samples carry an ObjectCarry: where it sits in
//   the frame of our Link's actor, and in the frame of the Link bone it was
//   nearest when lifted. A throw (Lift_Throw_*), a put (Lift_Put_*), a drop
//   (Lift_Drop, Lift_Fail*) or the object leaving the box over Link ends it;
//   the claim stays kAfterUs more, so the throw and the break that follows
//   are the thrower's. Bombs from the bag stay with the projectiles
//   (coop_projectiles.h: their stand-in follows the puppet already).
// - Theirs (Sync::Carried, coop_objects_sync.h): a copy whose owner's sample
//   at the shown time (100 ms late, as the puppet) carries one is placed each
//   game frame, on the calc thread, in the hands of the holder's puppet as it
//   is drawn there (Attached): the puppet's bone, when its world matrix reads
//   as a rigid frame, else the puppet's actor frame. It blends in over
//   Sync::kAttachBlendUs from where the samples put it. Once the samples stop
//   carrying it (the throw), the copy follows them again: the owner's throw,
//   the Cucco's flight. A holder whose puppet is not live here (he left, or
//   went to another scene with it) has his copy let go: our game drops it.
// - Breaking on impact is the objects and world syncs' (a thrown pot breaks
//   in its owner's game, the lifter's, and that break reaches every copy).
//
// Plain logic and memory reads through CoopPuppet::Guest only: tested on fake
// memory in tests/lichii/carry_test.cpp. Switches: LICHII_COOP_CARRY (on;
// =0 off), LICHII_COOP_CARRY_BONE (on; =0 the actor frame only),
// LICHII_COOP_CARRY_FLOWERS (on; =0 bomb flowers are not shared),
// LICHII_COOP_CARRY_STILL (on; =0 a carried copy keeps its own speed: Still).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace CoopPuppet {
class Guest;
}

namespace CoopCarry {

using Vec3 = coop::protocol::Vec3;
using Quat = coop::protocol::Quat;
// Rows of 3, as the actor holds it at +0x328: world = M * local + place.
// Column 2 is the way the actor faces (coop_objects.cpp, the test drive).
using Mat3 = std::array<float, 9>;

// Things a Link lifts that are shared objects: pots (Obj_Pot_*), liftable
// rocks (Obj_LiftRock_*: A the small ones, B_01 the silver rock that needs
// the Silver Gauntlets, LiftGrade Middle; C_01 and C_05 the golden ones),
// the liftable crate (Obj_BrokenBox_A_03), bushes (Obj_Plant_Grass_A_*), the
// Cuccos (Animal_Cucco*, Animal_SuperCucco; not the attacking flock), bomb
// flowers (IsBombFlower) and child Ruto (Npc_Zora_Ruto_Child_*). Read from the
// romfs: every actor pack with a LiftableRef (2026-11-11). Bombs and bombchus
// are projectiles; pickups (rupees, hearts) are liftable only by tools.
bool IsLiftable(std::string_view name);
// A bomb flower's fruit: the placed Obj_BombFlower_A_01 / _A_04 (their pack
// is the fruit: LiftableParam Obj_BombFlower_Fruit, parent Item_Bomb) and the
// Death Mountain Trail's Obj_BombFlower_Fruit_For_Break_*.
bool IsBombFlower(std::string_view name);
// Bomb flowers are not shared otherwise (docs/world.md: "each player picks
// their own"); the objects sync shares them while this is on (any thread).
void SetShareFlowers(bool on);
bool ShareFlowers();

// What our Link's AS slot 0 command says about a lift.
enum class Phase : std::uint8_t { none, lift, thrown, put, dropped };
Phase PhaseOf(std::string_view command);
// The start of a lift (Lift_Ready_*, Lift_Up_*, LiftReady, LiftUp): only
// then is a new object taken as lifted.
bool IsLiftStart(std::string_view command);
// A lift of a bomb or a bombchu (Lift_*_Bomb, Lift_*_BombChu): only a bomb
// flower may be the lifted object then (a bomb from the bag is a projectile).
bool IsBombLift(std::string_view command);

// A rigid frame: a place and a turn.
struct Frame {
    Vec3 p{};
    Mat3 r{1, 0, 0, 0, 1, 0, 0, 0, 1};
};
Vec3 Apply(const Frame& f, const Vec3& local);
// `b` given in `a`'s frame, in the world.
Frame Compose(const Frame& a, const Frame& b);
Frame Inverse(const Frame& f);
// `object` in `anchor`'s frame (Compose(anchor, Relative(anchor, object)) is object).
Frame Relative(const Frame& anchor, const Frame& object);
// From `a` (t 0) to `b` (t 1): the place along the line, the turn by the
// shortest way.
Frame Blend(const Frame& a, const Frame& b, float t);
// The nearest rigid turn (columns made unit and square to each other), or
// nothing when the matrix is not finite or flat.
std::optional<Mat3> Rigid(const Mat3& m);
Quat QuatOfRows(const Mat3& m);
Mat3 RowsOfQuat(const Quat& q);

// The holder-side frames of a carry, and back.
struct Local {
    Frame actor;                // in the frame of the holder's actor
    std::optional<std::uint16_t> bone;
    Frame bone_frame;           // in that bone's frame (with `bone`)
};
coop::protocol::ObjectCarry ToCarry(std::uint8_t holder, const Local& local);
Local FromCarry(const coop::protocol::ObjectCarry& carry);
// Between two carries of the same holder: their frames blended at t (the
// bone kept when both have the same one, else none).
coop::protocol::ObjectCarry BlendCarry(const coop::protocol::ObjectCarry& a,
                                       const coop::protocol::ObjectCarry& b, float t);

// ---- memory (any thread with a Guest)

// The skeleton object of an actor (CoopPose::addr::kSkeletonPath) and its
// world matrices: skeleton object + kWorldArray points to one matrix per bone.
// ORM round 5 (docs/SONDAS.md, the bone census of sondas12 and sondas13):
// read as 3x4 rows 0x30 apart, 301 of Link's 309 read as rigid turns, every
// one with no place of its own (all at the actor: NearestBone always says
// bone 0); read as 4x4 with the place in floats 12 to 14, none do. So these
// are turns, not the bones' places: a carry by "bone" is in fact by the model
// root's turn at the actor's place. FindSkeleton still tells a 4x4 layout
// from bone 0 (rows or columns, from the actor's own turn) should a build have
// one, and keeps the 3x4 reading otherwise (what every run so far chose);
// the census and the carry bone check name the one in use.
inline constexpr std::uint64_t kWorldArray = 0x20;
inline constexpr std::uint64_t kWorldStride = 0x40;
inline constexpr std::uint64_t kWorldStride34 = 0x30;
enum class Layout : std::uint8_t { m44, m34 };
struct Skeleton {
    std::uint64_t world = 0;  // the first bone's matrix
    std::uint32_t count = 0;
    Layout layout = Layout::m44;
    bool columns = false;  // the 4x4 turn is stored by columns (transposed)
    std::uint64_t Stride() const { return layout == Layout::m44 ? kWorldStride : kWorldStride34; }
};
// "4x4 by rows", "4x4 by columns", "3x4 (old)": for the log lines.
const char* NameOf(const Skeleton& s);
// One bone's matrix as read (Stride() bytes) in the 3x4 rows BoneWorld takes.
std::array<float, 12> RowsOf(const Skeleton& s, const float* m);
std::optional<Skeleton> FindSkeleton(CoopPuppet::Guest& guest, std::uint64_t actor);
// An actor's place (+0x31c) and turn (+0x328, made rigid).
std::optional<Frame> ReadActor(CoopPuppet::Guest& guest, std::uint64_t actor);

// A bone matrix (rows of 4) as a frame in the world. The matrices may be in
// the world or in the model's own frame: one whose place is within
// kBoneReachM of the actor is taken as in the world, one within kBoneReachM of
// the origin as the model's (composed with the actor), the nearer of the two
// when both are. Nothing when it is neither, or its turn is not a rigid one.
inline constexpr float kBoneReachM = 3.0f;
std::optional<Frame> BoneWorld(const Frame& actor, const std::array<float, 12>& rows);
std::optional<Frame> ReadBone(CoopPuppet::Guest& guest, const Skeleton& skeleton,
                              const Frame& actor, std::uint16_t bone);
// The bone of the skeleton whose place is nearest `at` (one read of the
// whole array), with its distance. Nothing when no bone reads right.
std::optional<std::pair<std::uint16_t, float>> NearestBone(CoopPuppet::Guest& guest,
                                                           const Skeleton& skeleton,
                                                           const Frame& actor, const Vec3& at);

// How Attached placed a copy.
enum class Anchor : std::uint8_t { bone, actor, none };
// Calc thread: where a copy carried by the puppet actor `puppet` goes now:
// the carry's bone frame on the puppet's bone when `use_bone` and it reads,
// else its actor frame on the puppet's actor. Nothing when the puppet's
// place cannot be read.
std::optional<Frame> Attached(CoopPuppet::Guest& guest, std::uint64_t puppet,
                              const coop::protocol::ObjectCarry& carry, bool use_bone,
                              Anchor* used = nullptr);

// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): whether a carried copy
// stays where it is put. The bomb held in a puppet's hands fell and came
// back each frame (issue 169) because the game's setter keeps the physics
// body's speed. Calc thread, each frame a copy is placed in a puppet's
// hands: where it reads now and where it is placed again. After kJudgeFrames
// frames of one copy, one check line: "carry hold ok" (it held) or "carry
// hold fallback" (it fell in kFallFrames or more): from then Falling() says
// so, and the mover also writes the pending place the actor's calc applies
// (as for an actor with no body), so the drop is undone before it is drawn;
// kJudgeFrames later a second line says whether that held ("carry hold fix").
class HoldWatch {
public:
    void Note(std::uint64_t actor, const Vec3& read_now, const Vec3& placed);
    void Forget(std::uint64_t actor);
    bool Falling() const { return falling_; }
    bool Judged() const { return judged_; }

    static constexpr int kJudgeFrames = 60;
    static constexpr int kFallFrames = 10;
    static constexpr float kFallM = 0.03f;  // more than gravity's first frame (0.003) and jitter
    static constexpr float kFarM = 2.0f;    // further sideways: the holder jumped, not a fall

private:
    struct One {
        std::optional<Vec3> last;  // where it was placed the frame before
        int frames = 0;
        int falls = 0;
        float largest = 0.0f;
    };
    std::map<std::uint64_t, One> watch_;
    bool judged_ = false;
    bool falling_ = false;
    bool fix_judged_ = false;  // after a fall: judged again with the pending place ("carry hold fix")
};

// ---- a still double while carried (levantar-vasija, oot-remake docs/SONDAS.md round 6)
//
// The copy in a puppet's hands fell between our places (52-57 of 60 frames,
// up to 1.1 m a frame, sondas20r6) and the pending place did not stop it: the
// setter (vtable 0x218, 0x4c6580 -> 0x4c6630) warps the physics body but keeps
// its speed, so gravity keeps adding to it. Read in main (build e0f3ccfa, the
// AI nodes OneShotPhysicsRigidBodySetVelocityMps, run 0x973f30, and
// OneShotPhysicsSetLinearVelocity, run 0x29097e0): an actor's main rigid body
// is [[[actor + 0x270] + 0x50] + 0x20] + 0x190 (the body 0x4c6630 warps), and
// the game gives it its speeds with 0x12dbe0(body, const float[3]*, 0)
// (linear) and 0x12b790(body, const float[3]*) (angular), under the body's
// lock (+0x38). Each does nothing when its bit of the body's flags (+0x70:
// bit 30 linear, bit 31 angular) is set, and the speeds the body was given
// sit at +0xe0 and +0xec.
//
// So, after each place in a puppet's hands, the mover zeroes both speeds with
// those two calls: the body is a still double in the hands (one frame of
// gravity at most, which the next place takes back). Nothing is switched off,
// so nothing has to be given back: when the holder lets go the zeroing stops
// and the copy's own physics runs again (the thrown pot flies on its owner's
// samples, falls and breaks once, by the owner's rule). Fallback when the body
// does not read, its flags keep its speed, or there is no zero cell: the old
// path (the setter and the pending place). One check line says which path
// ran ("carry still ok" or "carry still fallback"), and one line per copy let
// go. LICHII_COOP_CARRY_STILL=0 turns the zeroing off (the old path).
namespace still {
inline constexpr std::uint64_t kSetLinearVelocity = 0x12dbe0;   // (body, const float[3]*, 0)
inline constexpr std::uint64_t kSetAngularVelocity = 0x12b790;  // (body, const float[3]*)
inline constexpr std::array<std::uint64_t, 3> kRootPath{0x270, 0x50, 0x20};  // actor to the physics root
inline constexpr std::uint64_t kRootBody = 0x190;
inline constexpr std::uint64_t kBodyFlags = 0x70;    // u64
inline constexpr std::uint64_t kBodyLinear = 0xe0;   // 3 x f32, m/s
inline constexpr std::uint64_t kBodyAngular = 0xec;  // 3 x f32
inline constexpr std::uint64_t kLinearKept = std::uint64_t{1} << 30;   // the linear setter does nothing
inline constexpr std::uint64_t kAngularKept = std::uint64_t{1} << 31;  // the angular setter does nothing
inline constexpr std::size_t kZeroBytes = 16;  // the zero cell (12 used)
}  // namespace still

// An actor's main rigid body.
struct Body {
    std::uint64_t address = 0;
    std::uint64_t flags = 0;
    Vec3 linear{};   // the speeds it keeps
    Vec3 angular{};
};
// Nothing when a link of the path is 0 or does not read, or a speed is not finite.
std::optional<Body> ReadBody(CoopPuppet::Guest& guest, std::uint64_t actor);

// A game call for the cave: the function (absolute) and x0 to x2.
struct GameCall {
    std::uint64_t function = 0;
    std::array<std::uint64_t, 3> x{};
};

// Calc thread (the mover's lock): which copies in a puppet's hands are held
// still, and the calls that do it.
class Still {
public:
    // Where the 12 zero bytes the calls point at are in the game's memory.
    enum class Cell : std::uint8_t { ready, coming, none };  // coming: the block is being made

    void SetOn(bool on) { on_ = on; }
    bool On() const { return on_; }
    // Start of a calc frame. A copy held still that has not been placed in
    // the hands for kLetGoFrames frames is let go: one line (TakeLines).
    void Frame();
    // Right after the copy `actor` was placed in a puppet's hands: the calls
    // that zero its speeds (linear, then angular), to be made now; none when
    // it takes the fallback (the setter and the pending place). `zero` is the
    // zero cell's address when `cell` is ready.
    std::vector<GameCall> Hold(CoopPuppet::Guest& guest, std::uint64_t main, std::uint64_t actor,
                               std::uint64_t zero, Cell cell);
    // Lines for the log (copies let go), and copy-frames held still since the last ask.
    std::vector<std::string> TakeLines();
    std::size_t TakeStillFrames();
    bool Holding(std::uint64_t actor) const { return held_.contains(actor); }
    void Reset();

    static constexpr int kLetGoFrames = 3;  // the cave's call budget may skip one frame

private:
    struct One {
        std::uint64_t frames = 0;  // held still
        int missed = 0;            // frames in a row not placed in the hands
        bool placed = false;       // placed in the hands this frame
    };
    bool on_ = true;
    std::map<std::uint64_t, One> held_;
    std::vector<std::string> lines_;
    std::size_t still_frames_ = 0;
};

// ---- ours

// A shared liftable of one walk.
struct Candidate {
    std::uint64_t address = 0;
    std::string name;
    Vec3 position{};
    std::optional<Mat3> rotation;
    bool held_by_other = false;  // another player's puppet holds our copy (Sync::CarriedByOther)
};

// Our own Link in one walk.
struct LinkNow {
    Frame frame;
    std::optional<std::string> command;  // AS slot 0, when read
};

enum class Release : std::uint8_t { thrown, put, dropped, lost, gone, scene, no_link };
const char* NameOf(Release why);

struct Event {
    enum class Kind : std::uint8_t { lifted, released };
    Kind kind = Kind::lifted;
    std::uint64_t address = 0;
    std::string name;
    Release why = Release::thrown;   // released
    std::optional<std::uint16_t> bone;  // lifted: the bone it was nearest
};

class Lifter {
public:
    // At the lift: the object is at most this far from our Link's feet
    // sideways, and between kLowM and kHighM above them.
    static constexpr float kReachM = 1.6f;
    static constexpr float kLowM = -0.6f;
    static constexpr float kHighM = 3.2f;   // an adult's raised arms under the silver rock
    // While held: sideways this far at most, the same heights.
    static constexpr float kHoldReachM = 1.4f;
    // Walks in a row out of that box: let go (a hit made Link drop it, a
    // throw whose command was not read).
    static constexpr int kLostWalks = 3;
    // After it was let go, the claim stays this long (the throw, the break).
    static constexpr std::uint64_t kAfterUs = 1'500'000;
    // A candidate behind Link counts this much farther (the game lifts the
    // one in front).
    static constexpr float kBehindPenaltyM = 1.0f;
    // A bone farther than this from the object when it is lifted is not used.
    static constexpr float kBoneNearM = 1.0f;
    // A new object is taken only this long into a run of lift-start commands
    // (so a Link who stands lifting beside a second pot does not take it).
    static constexpr std::uint64_t kStartWindowUs = 1'000'000;

    // Our Link's bones in the world: the nearest to a place (once, at the
    // lift), and one bone (each walk while held). Either may be empty.
    using BoneFinder = std::function<std::optional<std::pair<std::uint16_t, float>>(const Vec3&)>;
    using BoneReader = std::function<std::optional<Frame>(std::uint16_t)>;

    // Present thread, once a walk. `link`: nothing when the walk did not see
    // our Link (a load, a cutscene that hides him).
    void Update(const std::string& scene, const std::optional<LinkNow>& link,
                const std::vector<Candidate>& candidates, std::uint64_t now_us,
                const BoneFinder& find = {}, const BoneReader& bone = {});

    // The object our Link holds now.
    std::optional<std::uint64_t> Held() const;
    // While held, and kAfterUs after it was let go: flagged targeting, our
    // copy left to our game.
    std::set<std::uint64_t> Claimed(std::uint64_t now_us) const;
    // The carry our samples of `address` carry now (held, its frames read
    // this walk), with `self` as the holder.
    std::optional<coop::protocol::ObjectCarry> CarryOf(std::uint64_t address,
                                                       std::uint8_t self) const;
    std::vector<Event> TakeEvents();
    void Reset();

private:
    struct Hold {
        std::uint64_t address = 0;
        std::string name;
        std::uint64_t since_us = 0;
        std::optional<std::uint16_t> bone;
        std::optional<Local> local;  // this walk's frames; nothing when not read
        int out = 0;                 // walks in a row out of the box
    };
    void LetGo(Release why, std::uint64_t now_us);

    std::optional<Hold> hold_;
    std::map<std::uint64_t, std::uint64_t> let_go_;  // address: when
    std::string scene_;
    std::vector<Event> events_;
    bool starting_ = false;            // the last walk's command started a lift
    std::uint64_t start_since_us_ = 0; // when that run of lift-start commands began
};

} // namespace CoopCarry
