// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM D-R19 (oot-64 D-233): Epona is the group's, each player calls and rides
// his own, and the others see him on a horse (oot-remake docs/EPONA.md).
//
// - The shared step and the race hold (coop/host/epona.hpp): AddShared puts
//   Step_Get_Epona (Complete only) in the session's shared flags; RaceHold
//   says, once a frame, whether our own race with Ingo (or the escape after
//   it) runs, and the hook's HoldFlagStore then holds a Complete from
//   elsewhere until it is over.
// - The rider's horse (on by default, LICHII_ORM_HORSE=0 off; request: "tenemos
//   que vernos en todo momento"): the puppet of a player getting on, riding
//   or getting off is no longer parked out of sight (scene.hpp's "park"): it
//   is drawn where his samples put him, with his own animation (getting on,
//   seated, getting off), and a horse actor of ours stands there. Any horse
//   he rides is drawn with its own look (request: "Epona solo debe verse como
//   Epona"): his game finds the horse under him (MountReader) and sends its
//   name in PLAYER_EQUIP (`mount`); the others pick the puppet horse for it
//   (coop::host::epona::horse_look). The horse is
//   a third CoopPuppet::Director (svc caller 25, after the fairies), fed by
//   Rider from the puppets' own plan, as the fairies are by their Follower:
//   it is placed under the rider (his place minus the saddle offset, turned
//   by his heading), plays a gait from his speed, waits where he got off
//   while he stays in the place, and goes when he leaves it.
// - SaddleProbe (LICHII_ORM_DEBUG_HORSE=1): while our own Link sits still on
//   our own Epona, logs where he sits in her frame, the number the others'
//   horses need (LICHII_ORM_HORSE_SADDLE).
//
// The actors: our packs (tools/puppet_pack, HORSES): CoopPuppet_Horse
// (Epona's model, saddled), CoopPuppet_Horse_Ranch and _Ingo (those horses'
// own models and coats), each with the horse AS (Animal_Horse_New, with the
// gaits) and animations, ActorLogicSimpleActor, no physics, no HorseRef, no
// name, so nobody can get on one or take it for Epona. A puppet pack built
// before them lacks them: after two failed creates of one, the game's plain
// event horse (Animal_Horse_For_Event_Base) is used for it instead, with a
// line. LICHII_ORM_HORSE_ACTOR picks one pack for every horse (docs/EPONA.md).
//
// Rider and SaddleProbe::Offset are plain logic, tested on their own
// (tests/lichii/epona_test.cpp); the rest is the game glue (ORM_PLATFORM_LIVE).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <set>
#include <vector>

#include "coop/host/epona.hpp"
#include "coop/host/player_reader.hpp"
#include "coop_puppet.h"
#include "platform_env.h"

namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace CoopEpona {

using CoopPuppet::Mat3;
using CoopPuppet::Vec3;

struct Settings {
    // One actor for every horse (LICHII_ORM_HORSE_ACTOR); "" picks it by the
    // horse he is on (horse_look).
    std::string actor;
    // Where the rider's sampled place is in his horse's frame (x right, y up,
    // z forward), in game units (about metres): the horse goes that much
    // under him. A guess until SaddleProbe has measured it in a run.
    Vec3 saddle{0.0f, 1.2f, 0.0f};
    coop::host::epona::GaitSpeeds speeds;
    float speed_rate = 6.0f;          // 1/s: how fast the speed reading follows the samples
    std::uint64_t gait_hold_ms = 300; // a new gait plays once it held this long
    float mount_near = 3.0f;          // a waiting horse this near a rider getting on stays there
    // After he gets off, his horse waits where he left it this long (0: while
    // he stays in the place, as his own Epona does in his game).
    std::uint64_t linger_ms = 0;
    // Samples further apart than this (a warp, a new scene) start the speed again.
    float snap = 20.0f;
};

// The svc cave's order for the horses' director (puppets, spawn 10, fairies
// 20, then the horses, then the object mover 30).
inline constexpr int kCallerOrder = 25;

// Epona's look, and the game's event horse a kit falls back to without a pack.
inline constexpr const char* kHorsePack = "CoopPuppet_Horse";
inline constexpr const char* kEventHorse = "Animal_Horse_For_Event_Base";

// The yaw of a rotation (rows, columns are the local axes): the same heading,
// upright. Identity when the forward axis is straight up or down.
Mat3 Upright(const Mat3& rotation);
// Where the horse stands for a rider at `rider` with `heading` (Upright):
// the rider's place minus the saddle offset turned by the heading.
Vec3 HorseSpot(const Vec3& rider, const Mat3& heading, const Vec3& saddle);

// ORM smoothing (REVISION_NUEVAS orm/epona, docs/SUAVIZADO.md): the horse
// under a rider shown with `rider` (the pose the Link puppets' director drew
// him with, CoopPuppet::LastSmoothed): his place minus the saddle turned by
// his heading (Upright of his rotation; `heading` when the pose has none),
// facing that way, with none of his animation (the horse plays its gait).
coop::host::SmoothedPose HorseUnder(const coop::host::SmoothedPose& rider, const Mat3& heading,
                                    const Vec3& saddle);

// The horses' director's smoothing source (Director::SetSmoothing): a horse
// with its rider in the saddle is drawn from the same smoothed pose as he is,
// this calc frame, so the two never part (at a gallop one frame apart is
// about 18 cm: he would float over the saddle). A horse waiting where he got
// off, or standing while he gets on, keeps its planned place (nothing).
class UnderRiders {
public:
    // The present thread, after Rider::Follow: the riders in the saddle now
    // and their planned heading (Rider::Riding).
    void Set(std::map<std::uint8_t, Mat3> riding);
    using Lookup = std::function<std::optional<CoopPuppet::ShownPose>(std::uint8_t number)>;
    // The calc thread: the horse of player n at `server_now_us` (the horses'
    // director's time this frame), from `shown` (CoopPuppet::LastSmoothed).
    // Nothing when he is not in the saddle, or the rider's pose is not of
    // this frame (asked more than kSameFrameUs away: his puppet was not drawn
    // smoothed this frame), so the horse keeps the plan as the rider does.
    std::optional<coop::host::SmoothedPose> At(std::uint8_t number, std::uint64_t server_now_us,
                                               const Lookup& shown, const Vec3& saddle) const;

    static constexpr std::uint64_t kSameFrameUs = 50'000;

private:
    mutable std::mutex mutex_;
    std::map<std::uint8_t, Mat3> riding_;  // guarded
};

// Turns the puppets' plan into horse targets. The present thread only.
class Rider {
public:
    using Log = std::function<void(const std::string&)>;
    explicit Rider(Settings settings = {}, Log log = {})
        : settings_(std::move(settings)), log_(std::move(log)) {}

    // `puppets`: the puppet planner's plan for this frame, made with
    // PlannerSettings::riders on (a rider shown, not parked). Returns a horse
    // target for each player who rides, gets on beside his waiting horse, or
    // got off and has not left (same numbers). `live`: CoopPuppet::
    // LivePuppets(); the horse of a rider whose puppet is not live is parked
    // (never a horse alone under nobody).
    // `mounts`: each player's horse as his PLAYER_EQUIP says (by number;
    // missing: not said), for the look.
    CoopPuppet::Input Follow(const CoopPuppet::Input& puppets, std::uint64_t now_ms,
                             std::uint32_t live = ~0u,
                             const std::map<std::uint8_t, std::string>* mounts = nullptr);

    const Settings& settings() const { return settings_; }
    // One actor for every horse from now on.
    void SetActor(std::string actor) { settings_.actor = std::move(actor); }
    // ORM riesgos-visual: the saddle and the gait lines as measured (PickRide).
    void SetSaddle(const Vec3& saddle) { settings_.saddle = saddle; }
    void SetSpeeds(const coop::host::epona::GaitSpeeds& speeds) { settings_.speeds = speeds; }
    // A puppet horse the game could not make (an older puppet pack): the plain
    // event horse in its place from now on.
    void MarkBroken(std::string actor) { broken_.insert(std::move(actor)); }
    // The actor for a horse `mount` (as the settings and the broken ones say).
    std::string ActorFor(std::optional<std::string_view> mount) const;
    // For tests: player n's horse actor now, if he has a horse.
    std::optional<std::string> ActorOf(std::uint8_t number) const;
    // For tests and the status line: player n's gait now, if he has a horse.
    std::optional<coop::host::epona::Gait> GaitOf(std::uint8_t number) const;
    // The players in the saddle after the last Follow (a horse under them),
    // with the heading their horse was planned with (UnderRiders).
    std::map<std::uint8_t, Mat3> Riding() const;

private:
    struct Track {
        Vec3 last{};                 // his last sampled place
        std::uint64_t last_ms = 0;
        float speed = 0.0f;          // along the ground, smoothed
        coop::host::epona::Ride ride = coop::host::epona::Ride::none;
        bool horse = false;          // a horse is drawn for him
        Vec3 at{};                   // where it stands
        Mat3 heading{1, 0, 0, 0, 1, 0, 0, 0, 1};
        coop::host::epona::Gait gait = coop::host::epona::Gait::wait;
        std::optional<coop::host::epona::Gait> next;  // a gait waiting out gait_hold_ms
        std::uint64_t next_since_ms = 0;
        std::uint64_t off_ms = 0;    // when he got off; 0 he is on
        std::string actor;           // the horse's actor, as last picked
    };
    void Say(const std::string& text) const {
        if (log_) log_(text);
    }
    void Speed(Track& track, const Vec3& at, std::uint64_t now_ms) const;
    void Step(Track& track, std::uint64_t now_ms);  // the gait, with the hold

    Settings settings_;
    Log log_;
    std::map<std::uint8_t, Track> tracks_;
    std::string scene_;
    std::set<std::string> broken_;
};

// The puppet horses (coop::host::epona::kEponaLook, kRanchLook, kIngoLook) a
// puppet pack has: the names its coop_puppet.json lists (build.py "horses"),
// as CoopSpawn::StandInsIn reads the stand-ins. The others are never asked
// for (a create for a name ActorInfo does not know has never been run): the
// glue marks them broken from the start, so their riders get the plain horse.
std::set<std::string> HorseLooksIn(std::string_view manifest);

// One of the game's horses on the actor list, where it stands.
struct SeenHorse {
    std::string name;
    Vec3 at{};
    std::uint64_t actor = 0;  // ORM riesgos-visual: the actor, for SaddleProbe (0 not known)
};
// The horse our Link is on: the nearest of `horses` (coop::host::epona::
// is_horse names only) within `near` of him; nothing when none is.
std::optional<std::string> PickMount(const std::vector<SeenHorse>& horses, const Vec3& own,
                                     float near = 3.0f);

// The horse our own Link is on, for PLAYER_EQUIP's mount. SetOwn and Mount on
// the present thread, Frame on the calc thread (it walks the actor list).
class MountReader {
public:
    void SetOwn(const std::optional<Vec3>& position, bool on_horse);
    // While he is on a horse, every kEveryMs: the game's horses on the actor
    // list and PickMount; on foot, nothing ("").
    void Frame(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t now_ms);
    // "" on foot or no horse found under him.
    std::string Mount() const;
    // ORM riesgos-visual: the actor of that horse (0 on foot or none found).
    std::uint64_t MountActor() const;
    // Our Link's reading as sent: `mount` is set ("" on foot) when the
    // reading has its equipment (merge_equipment keeps an absent field).
    void Stamp(coop::host::PlayerReading& reading);

    static constexpr std::uint64_t kEveryMs = 250;

private:
    mutable std::mutex mutex_;
    std::optional<Vec3> own_;  // guarded
    bool on_ = false;          // guarded
    std::string mount_;        // guarded
    std::uint64_t mount_actor_ = 0;  // guarded
    std::uint64_t next_ms_ = 0;  // the calc thread only
};

// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): the saddle and the gait
// speeds were estimates (0, 1.2, 0 and 0.5 / 3.5 / 7 / 11 units a second).
// They are measured now on our own horse whenever we ride, kept for the next
// run (coop_ride.txt in the config folder), and used for every other
// player's horse; the estimates stay only as the fallback until then.
// Plain logic, no game memory: SaddleProbe feeds it. Thread safe (the calc
// thread adds, the present thread asks).
class RideMeasure {
public:
    // Where our Link sat, still, in our horse's frame (SaddleProbe::Offset).
    // A reading outside kSaddleMin..kSaddleMax (a horse mid-step, the rider
    // getting off, a wrong actor) is counted and left out.
    void AddSaddle(const Vec3& offset, std::string_view horse);
    // Our horse's AS slot 0 command and our speed along the ground while it
    // played. Only the gait commands (coop::host::epona::gait_command, walk
    // to gallop) count; a speed that is not finite or below 0 does not.
    void AddGait(std::string_view command, float speed);

    // The median of the last kSaddleKeep sane readings, once kSaddleNeed of
    // the newest agree within kSaddleSpread on each axis; nothing before.
    std::optional<Vec3> Saddle() const;
    // The gait lines from the medians of each gait's speeds, once every gait
    // from walk to gallop has kGaitNeed readings and their medians rise;
    // nothing before. A line sits halfway between two gaits' medians; the
    // walk line is the lower of the estimate and half the walk's median; the
    // margin is the estimate's, made smaller when two medians are close.
    std::optional<coop::host::epona::GaitSpeeds> Speeds(const coop::host::epona::GaitSpeeds& estimate = {}) const;
    // Why Saddle() or Speeds() has nothing yet (the fallback line).
    std::string SaddleWhy() const;
    std::string SpeedsWhy() const;
    // What the ok line says.
    std::string SaddleSays() const;
    std::string SpeedsSays() const;

    // The file (one reading a line: "saddle x y z <horse>", "gait <0..3>
    // <speed>"), and reading it back: a later run starts from what an
    // earlier one measured. Returns how many lines were taken; a line not
    // understood (or out of range) is left out.
    std::string Save() const;
    std::size_t Load(std::string_view text);
    bool FromFile() const;  // what it has came from Load (no new reading since)
    // A reading of this run replaces the file's readings of its own kind only:
    // a trot keeps the file's saddle, a still seat keeps the file's gaits.

    static constexpr float kSaddleMin[3] = {-0.6f, 0.5f, -1.2f};
    static constexpr float kSaddleMax[3] = {0.6f, 2.5f, 1.2f};
    static constexpr std::size_t kSaddleNeed = 3;
    static constexpr std::size_t kSaddleKeep = 7;
    static constexpr float kSaddleSpread = 0.1f;
    static constexpr std::size_t kGaitNeed = 10;
    static constexpr std::size_t kGaitKeep = 64;

    // walk 0, trot 1, canter 2, gallop 3; nothing for Wait or any other.
    static std::optional<int> GaitIndex(std::string_view command);

private:
    static float Median(std::vector<float> values);
    std::optional<Vec3> SaddleLocked() const;
    std::optional<std::array<float, 4>> MediansLocked() const;

    mutable std::mutex mutex_;
    std::vector<Vec3> saddles_;          // guarded, at most kSaddleKeep, newest last
    std::size_t rejected_ = 0;           // guarded
    std::optional<Vec3> last_rejected_;  // guarded
    std::string horse_;                  // guarded
    std::array<std::vector<float>, 4> gaits_;  // guarded, walk .. gallop, newest last
    bool from_file_ = false;             // guarded: nothing new since Load
    // guarded: the saddle readings, the gait readings came from Load (a
    // reading of this run replaces only its own kind).
    bool saddle_file_ = false;
    bool gaits_file_ = false;
};

// Where our Link sits on our horse and how fast each gait goes. SetOwn on
// the present thread, Frame on the calc thread. On with the horses (ORM
// riesgos-visual); LICHII_ORM_DEBUG_HORSE=1 adds the old 'saddle:' line.
class SaddleProbe {
public:
    using Log = std::function<void(const std::string&)>;
    explicit SaddleProbe(std::string horse = "Animal_Epona_Adult", Log log = {},
                         RideMeasure* measure = nullptr)
        : horse_(std::move(horse)), log_(std::move(log)), measure_(measure) {}

    // Our Link as the sampler sends him, and whether he rides.
    void SetOwn(const std::optional<Vec3>& position, bool riding, std::uint64_t now_ms);
    // Once a frame while he rides. Our horse: `horse_actor` (MountReader's)
    // or else the first `horse` on the actor list. Every kGaitEveryMs its
    // slot 0 command, once held kGaitHoldMs, and our speed go to the measure;
    // sat still for kStillMs, at most every kEveryMs, where he sits in its
    // frame goes to the measure and, with the log, to the 'saddle:' line when
    // that moved by kMoved or more since the last line.
    void Frame(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t now_ms,
               std::uint64_t horse_actor = 0);

    // `rider` in the frame of a horse at `horse` with `rotation` (rows).
    static Vec3 Offset(const Vec3& horse, const Mat3& rotation, const Vec3& rider);

    static constexpr std::uint64_t kStillMs = 500;
    static constexpr std::uint64_t kEveryMs = 250;  // ORM riesgos-visual: 1000 before; a still stretch of 2 s gives 6 readings
    static constexpr std::uint64_t kGaitEveryMs = 100;
    static constexpr std::uint64_t kGaitHoldMs = 400;
    static constexpr float kStill = 0.02f;
    static constexpr float kMoved = 0.02f;
    static constexpr float kSpeedRate = 6.0f;  // 1/s, as Settings::speed_rate
    static constexpr float kSnap = 20.0f;      // as Settings::snap

private:
    std::uint64_t Horse(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t hint);

    std::string horse_;
    Log log_;
    RideMeasure* measure_ = nullptr;
    std::mutex mutex_;
    std::optional<Vec3> own_;    // guarded
    bool riding_ = false;        // guarded
    std::uint64_t still_since_ms_ = 0;  // guarded
    std::uint64_t own_ms_ = 0;   // guarded: when own_ was set
    float speed_ = 0.0f;         // guarded: along the ground, smoothed
    // The calc thread only.
    std::uint64_t actor_ = 0;
    std::uint64_t search_ms_ = 0;
    std::uint64_t log_ms_ = 0;
    std::uint64_t gait_ms_ = 0;
    std::string command_;             // our horse's slot 0 at the last read
    std::uint64_t command_since_ms_ = 0;
    std::optional<Vec3> logged_;
};

// The saddle and the gait lines the others' horses are drawn with now: the
// switch's if set, else the measured ones, else the estimates; and the
// self-check lines of the path taken (CoopVCheck "saddle", "horse gaits"):
// ok when the switch or a measure gave it, fallback with why. `drawing`: a
// horse is drawn under someone here; the fallback is said only then, the ok
// as soon as it is known (also in the rider's own game, who draws nobody).
struct RideNow {
    Vec3 saddle{};
    coop::host::epona::GaitSpeeds speeds;
    bool saddle_measured = false;
    bool speeds_measured = false;
};
RideNow PickRide(const RideMeasure& measure, const std::optional<Vec3>& saddle_switch, bool drawing,
                 const Settings& estimates = {});

#if ORM_PLATFORM_LIVE  // platform_env.h

// The puppets on and LICHII_ORM_HORSE not 0: riders are drawn on a horse.
// Read once; CoopSvc's ConfigurePuppets and the fairies' planner ask it too.
bool HorsesOn();

// Begin, after the story's AddShared: Step_Get_Epona (Complete only) in the
// shared flags, unless LICHII_ORM_EPONA=0. Logs what it did.
void AddShared(std::optional<coop::host::SharedFlags>& shared);

// Once a frame with our game's memory, before the hold store is made: true
// while our race with Ingo or the escape after it runs (and the step is
// shared), so a Complete from elsewhere waits. Reads the step at most 4
// times a second; logs each start and end.
bool RaceHold(coop::host::GameMemory& memory);
// The hold store refused a write of the step (RaceHold was true): logs once a
// race "the group's Epona came during our race: it waits".
void NoteHeld();

// CoopHook's Read, on our reading before it is sent: PLAYER_EQUIP's mount.
void StampMount(coop::host::PlayerReading& reading);

// The session's on_event (network thread): each player's mount from PLAYER_EQUIP.
void OnEvent(const coop::net::Event& event);

// CoopHook::OnFrame, right after CoopFairy::Publish, with the same arguments
// and our own AS command (for SaddleProbe). Nothing unless HorsesOn() or
// LICHII_ORM_DEBUG_HORSE.
void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& position, bool local_event,
             std::optional<std::string_view> own_anim, coop::host::Session* session,
             std::uint64_t server_now_us);

#endif  // ORM_PLATFORM_LIVE

} // namespace CoopEpona
