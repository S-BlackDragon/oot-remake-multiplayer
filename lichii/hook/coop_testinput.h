// Scripted controller input for automatic tests (LICHII_COOP_TEST_SCRIPT).
//
// LICHII_COOP_TEST_SCRIPT=<file> names a text file of input steps. The hook
// looks at the file about twice a second; each time it changes (and is not
// empty) it is read again and played from that moment into player 1's
// emulated pad (or the handheld pad when player 1 is not connected). The
// script's input is added to whatever the real keyboard or pad does. A test
// runner can so write one script to load the save, wait, then write the next.
//
// Time is counted in ticks of 1/60 s of real time, not in emulated frames:
// Ocarina of Time moves by elapsed time, so a step lasts as long whatever
// the frame rate. One step a line; `#` starts a comment:
//
//   wait 120               nothing held for 120 ticks (2 s)
//   hold A 30              hold A for 30 ticks
//   hold LSTICK_UP+B 90    left stick fully up and B together (run)
//   press A                hold A 6 ticks, then let go 6 ticks
//   press ZR+A 10          hold 10 ticks, then let go 10 ticks
//   stick 0.5 1.0 60       left stick at x 0.5, y 1.0 (each -1..1) for 60
//   stick 0 1 60 B         the same with buttons held too
//   rstick -1 0 30         right stick (the camera) the same way
//   goto -7.5 71.1         walk Link to world x -7.5, z 71.1 (within 1 m),
//   goto -7.5 71.1 1.5 600 ... within 1.5 m, giving up after 600 ticks
//   toward -7.5 71.1 8     push the stick toward that point for 8 ticks
//                          (Link turns to face it)
//   warp KakarikoVillage_Child Kakariko_WellSide
//   warp WaterTemple WaterTemple_FromLakeHylia adult
//                          (ORM) a last word adult or child asks that age for a
//                          scene whose name has no era (a dungeon), as X_Adult
//                          and X_Child do
//   chat /teleport Saria   (the rest of the line, sent as the chat box sends it)
//                          test only: changes scene the way the game's own
//                          sequence does (Sequence_ReservedNextBanc), to the
//                          start position given (StartPos names in the
//                          scene's Banc) or the scene's own. The game takes
//                          it only during an event, such as the one after a
//                          save loads: put the warp before the presses that
//                          load the save (the hook asks again every 0.5 s
//                          for up to 90 s, until Link is in that scene).
//                          ORM: asked in the world with no event, it starts
//                          the /teleport scene event after 2 s
//                          (WarpStartsEvent); "warp done ... (taken after
//                          our scene event, in free play)" says so
//   place -37.2 109.8 13.5 test only: puts Link at world x, y (height), z
//                          in this scene (the boss pull's free-play move,
//                          CoopBoss::MoveLink; needs LICHII_COOP_BOSS on, the
//                          default), so a scenario reaches a room no walk
//                          can; logs "coop: test place ...: done" or "failed"
//   kill Enemy_Skulltula_Gold test only: the nearest actor of that name to
//                          Link takes a hit from Link through the game's own
//                          damage (as a real kill, so it drops what it
//                          drops); needs LICHII_COOP_OBJECTS on; logs
//                          "coop: test kill NAME ...: hit" or "none here"
//   give bombs             test only: gives an item through coop_cheats
//                          (needs LICHII_COOP_CHEATS=1): bombs, bow, hookshot,
//                          or any item flag name coop_cheats knows
//   life 0                 test only: writes 0 (quarter hearts, up to Link's
//                          max) into Link's current life word, the Life
//                          component's value (docs/objects.md); 0 kills him.
//                          Refused, with a log line, when the path to it
//                          cannot be read
//   gate 1                 test only: writes 1 (closed) or 0 (open) into the
//                          downed gate's word (coop_downed.h); while closed
//                          the game over event does not start. The gate is
//                          only patched in with LICHII_COOP_DOWNED=1
//   bottle A fairy         test only: puts a value (Empty, Fairy, RedPotion,
//                          any BottleItem name, any case) straight into the
//                          live value of bottle A's (A to E) content flag,
//                          PlayerStatus.AssignableItem.BottleInfo.BottleA.
//                          BottleItem (an Enum flag), and, when bottle A is
//                          not owned, its IsValid and PouchData.BottleA.
//                          StockNum too; not through the game's queue, so the
//                          saved copy keeps the old value. Logs what it wrote
//                          and what was there
//   bottle A restore       test only: puts back what the first bottle A write
//                          found there
//   event KokiriForest_Meet_DekuTree_MeetDekuTree_01
//                          test only: asks the game to start that event, as
//                          the game over asks its own: main+0x4aa890 with
//                          Link's event component and the name, called from
//                          the svc cave (orm_story_events.h; the cave is patched in
//                          with LICHII_COOP_PUPPET=1). Logs "coop: story:"
//                          lines: what was asked and whether the game took it
//   skip                   test only (ORM, ZMP D-048): force skips the event
//                          that runs now, with the game's own force skip
//                          (orm_story_events.h, Skip): one shot a call, as a
//                          player's skip in a scenario (historia-saltar); the
//                          story layer counts it as the owner's skip press
//                          (its end goes out as STORY_EVENT skipped).
//                          Logs "coop: test skip" and a "coop: story: force
//                          skip ..." or "no event to skip" line
//   flag HavePlayedEvent.KokiriForest_Meet_DekuTree_MeetDekuTree_01 1
//                          test only: queues a Bool flag (by name, or 0x and
//                          its hash) to 0 or 1 through the game's own queue,
//                          as its setter does (GameFlags::set_bool), so the
//                          save keeps it. Logs whether the queue took it and
//                          what the flag read before
//   key 0x9f7d0ebfa5357dbb 1
//                          test only (ORM): queues a 64-bit-key flag
//                          (Bool64bitKey: a placement's saved state, the
//                          dungeon list's chests, red ice, cracked walls,
//                          room enemy counters) to 0 or 1 through the
//                          game's queue (GameFlags::set_key64), as the
//                          game does when the object is broken or taken.
//                          0x and 1 to 16 hex digits. Logs "coop: test key
//                          0x...: queued" or "NOT set" and what it read
//   enum Step_Meet_DekuTree [Complete]
//                          test only: logs an Enum flag (by name, or 0x and its
//                          hash) as the game holds it: find_type, the value's
//                          hash and name (the built-in story list's names),
//                          the game's own value list from the entry, and the
//                          entry's 0x38 bytes. With a value (a name or 0x
//                          hash), queues it through the game's Enum queue
//                          (GameFlags::set_enum), as the game's setter does
//   age adult              test only (ORM D-R17): this game reloads its scene at
//                          that age (child or adult), the way a game following
//                          a mate's age change does (coop_age_game.h): the
//                          era flag, an event of its own, the scene change at
//                          the other age, Link put back. Logs "coop: age:"
//   mark walk-start        writes "coop: test mark walk-start" to the log
//
// goto and toward steer from Link's position, read each frame. The stick is
// relative to the camera, which turns as Link walks: the hook gives the
// stepper the camera's ground directions from the game's camera matrix each
// frame (without them it learns them from how Link moves). A push that gets
// nowhere for a second steps aside. A goto that gives up logs
// "coop: test goto timed out" and the script goes on.
//
// Buttons: A B X Y L R ZL ZR PLUS MINUS LCLICK RCLICK, the d-pad DUP DDOWN
// DLEFT DRIGHT, and full stick directions LSTICK_UP LSTICK_DOWN LSTICK_LEFT
// LSTICK_RIGHT RSTICK_UP RSTICK_DOWN RSTICK_LEFT RSTICK_RIGHT; join with `+`.
// When the last step ends the hook writes "coop: test script done".
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {
class System;
}

namespace CoopTestInput {

constexpr int kTicksPerSecond = 60;
constexpr std::int32_t kStickMax = 0x7fff;  // HID_JOYSTICK_MAX

// The emulator's NpadButton bits (hid_core/hid_types.h).
enum Button : std::uint64_t {
    kA = 1u << 0,
    kB = 1u << 1,
    kX = 1u << 2,
    kY = 1u << 3,
    kLClick = 1u << 4,
    kRClick = 1u << 5,
    kL = 1u << 6,
    kR = 1u << 7,
    kZL = 1u << 8,
    kZR = 1u << 9,
    kPlus = 1u << 10,
    kMinus = 1u << 11,
    kDLeft = 1u << 12,
    kDUp = 1u << 13,
    kDRight = 1u << 14,
    kDDown = 1u << 15,
};

struct Stick {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const Stick&) const = default;
};

enum class Kind {
    timed,   // wait, hold, press, stick, rstick
    mark,
    go,      // goto
    toward,
    warp,    // test only: scene change
    give,    // test only: an item
    wear,    // test only: a PlayerStatus.Equip* flag (what the puppets in other games wear)
    assign,  // test only: the item on ZR (PlayerStatus.AssignableItem.Child and .Adult)
    chat,    // test only: a chat line or command, as typed in the chat box (/teleport B)
    place,   // test only: Link put at x, y, z in this scene
    kill,    // test only: the nearest actor of that name hit by our Link for all its life
    setflag, // test only: an Int or Bool flag of this game set to a value (issue 130: shop-buy)
    setenum, // test only: an Enum flag (name or 0x hash) set to a value's name (issue 193: quests)
    life,    // test only: Link's current life, in quarter hearts
    gate,    // test only: the downed gate's word, 0 open or 1 closed
    bottle,  // test only: a bottle's content flag (or "restore")
    event,   // test only: ask the game to start an event by name
    flag,    // test only: queue a Bool flag to 0 or 1
    int_flag,  // test only: queue an Int flag to a value (magic, a count the cheats do not give)
    enum_flag,  // test only: log an Enum flag (and queue a value when one is given)
    age,     // test only: this game reloaded at the other age (coop_age_game.h)
    key64,   // test only (ORM): queue a 64-bit-key flag to 0 or 1
    skip,    // test only (ORM, D-048): force skip the event that runs now
    await_mate,  // test only (ORM): wait until a mate stands in our scene, or the ticks run out
    talk_through,  // test only (ORM): press A while our event runs (a talk's text), until it is over
};

struct Step {
    Kind kind = Kind::timed;
    std::uint32_t ticks = 0;  // timed and toward; goto: the give-up time
    std::uint64_t buttons = 0;
    std::optional<Stick> left;   // unset: the real stick
    std::optional<Stick> right;
    std::string mark;  // mark: its name; warp: the scene; give: the item; wear: the piece;
                       // assign: the item's name in the button enum (Bow, Hookshot, Hammer)
                       // life and gate: the value, in decimal; bottle: the slot, A to E;
                       // event: the event name; flag and int: the flag's name or 0x hash;
                       // key: the 0x key (1 to 16 hex digits)
    std::string start; // warp: the start position name (empty: the scene's own); wear: the slot;
                       // bottle: the BottleItem value name as the game spells it, or "restore";
                       // flag and key: "0" or "1"; int: the value, in decimal;
                       // enum: the value to queue (empty: only log)
    float x = 0.0f;  // goto and toward: the world point (x, z); place: (x, y, z)
    float y = 0.0f;
    float z = 0.0f;
    float radius = 1.0f;
    std::optional<bool> child;  // ORM, warp: the age asked by a last word adult|child
    int line = 0;  // the script line it came from
};

struct Script {
    std::vector<Step> steps;
};

// Reads a script. On a bad line returns nothing and sets `error` to
// "line N: why".
std::optional<Script> Parse(std::string_view text, std::string& error);

// wear SLOT: the PlayerStatus flag name for weapon, shield, head, armor or
// boots (any case), e.g. "EquipHeadArmor"; nothing for another word.
std::optional<std::string> WearFlag(std::string_view slot);

// assign ITEM: true for a value of the item button enum
// (PlayerStatus.AssignableItem.Child / .Adult in GameDataList: Empty,
// DekuStick, Bomb, SlingShot, Hookshot, Longshot, Hammer, Bow, ...), as the
// game spells it.
bool AssignableItem(std::string_view name);

// bottle SLOT VALUE: the BottleItem value as the game spells it ("Fairy" for
// "fairy"), "restore" for restore (any case), nothing for another word.
std::optional<std::string> BottleValue(std::string_view word);

// ORM (oot-remake docs/PUEBLOS.md "pueblo-lago", docs/RIESGOS_RESUELTOS.md "Mundo e historia"): a
// warp asked in the middle of the world. The game takes the reserved scene change when an event
// ends (the one after a save loads, a talk); in free play none comes. So a warp not taken
// kWarpEventAfterS after it was asked, with our Link read (not loading) and no event running,
// starts the /teleport trip's short scene event (CoopTeleport::StartSceneEvent), again every
// kWarpEventEveryS, at most kWarpEvents times. While an event runs it waits: its end takes it.
// `last_event_s` below 0: none started yet.
inline constexpr double kWarpEventAfterS = 2.0;
inline constexpr double kWarpEventEveryS = 6.0;
inline constexpr int kWarpEvents = 3;
bool WarpStartsEvent(double now_s, double asked_s, double last_event_s, int events, bool link_read,
                     bool event_running);

// What the pad gets now.
struct Output {
    std::uint64_t buttons = 0;
    std::optional<Stick> left;
    std::optional<Stick> right;
    bool operator==(const Output&) const = default;
};

// Plays a script step by step. No emulator code: the hook calls Advance once
// a frame with the time since the start and Link's position (x, z) when it
// can read it, and gives Current() to the pad.
class Runner {
public:
    explicit Runner(Script script);

    // The camera on the ground: unit vectors (x, z) of where stick up and
    // stick right walk Link. From the game's camera matrix when the hook has
    // it; without it the stepper learns them from how Link moves.
    struct Camera {
        float forward_x = 0.0f;
        float forward_z = 0.0f;
        float right_x = 0.0f;
        float right_z = 0.0f;
    };

    // Moves on to the step for `now_s`. Appends to `log` the marks reached
    // and "goto timed out" lines, in order.
    // `mate_here`: another player is in our scene as presence shows it (await mate).
    // `event`: whether our own event runs (talkthrough); unknown when it cannot be read.
    void Advance(double now_s, std::optional<std::array<float, 2>> player,
                 std::vector<std::string>& log, std::optional<Camera> camera = std::nullopt,
                 bool mate_here = false, std::optional<bool> event = std::nullopt);
    // talkthrough: A pressed for kTalkPressS every kTalkEveryS while the event runs; over once no
    // event is seen for kTalkFreeS (or none came in kTalkNoneS).
    static constexpr double kTalkPressS = 0.1;
    static constexpr double kTalkEveryS = 0.75;
    static constexpr double kTalkFreeS = 1.0;
    static constexpr double kTalkNoneS = 3.0;
    const Output& Current() const { return out_; }
    // The warp and give steps reached since the last call, for the hook.
    struct Request {
        Kind kind = Kind::warp;
        std::string name;
        std::string start;
        std::array<float, 3> at{};  // place
        std::optional<bool> child;  // ORM, warp: adult (false) or child (true) asked
    };
    std::vector<Request> TakeRequests() { return std::exchange(requests_, {}); }
    bool Done() const { return index_ >= script_.steps.size(); }
    std::size_t StepCount() const { return script_.steps.size(); }
    // Steering now (goto or toward), for the log.
    bool Steering() const;
    std::optional<double> Heading() const { return heading_; }
    double Mirror() const { return mirror_; }

    // Link turns before he walks: movement is learnt from only after the
    // stick has held one direction this long, and a steering stick is
    // re-aimed at most this often, so such windows happen.
    static constexpr double kSettleS = 0.3;
    static constexpr double kAimEveryS = 0.5;
    // No 0.3 m of progress in kStuckS: push a quarter turn aside for kDetourS.
    static constexpr double kStuckS = 1.0;
    static constexpr double kDetourS = 0.7;

private:
    void Learn(double now_s, std::optional<std::array<float, 2>> player);
    void Steer(const Step& step, double now_s, std::optional<std::array<float, 2>> player);
    std::optional<Camera> camera_;  // this frame's, when given
    std::vector<Request> requests_;
    void Hold(std::optional<double> stick_angle, double now_s);

    Script script_;
    std::size_t index_ = 0;
    std::size_t marked_index_ = static_cast<std::size_t>(-1);  // a hold whose mark was said
    double step_start_ = 0.0;  // the script starts at time 0
    Output out_;
    // The camera model: stick up moves Link along world angle `heading_`
    // (radians, atan2 of z and x); `mirror_` is -1 when stick right turns the
    // other way round. Learnt from Link's movement while the stick is held.
    std::optional<double> heading_;
    double mirror_ = 1.0;
    bool mirror_known_ = false;
    std::optional<double> stick_angle_;  // the left stick's direction now (none: let go)
    double stick_since_ = 0.0;           // when it took that direction
    std::optional<std::array<float, 2>> window_pos_;  // where a steady window began
    std::optional<std::array<float, 2>> prev_player_;  // last frame's position
    double aimed_at_ = -1.0;             // when Steer last chose a direction
    std::optional<std::array<float, 2>> progress_pos_;  // stuck check
    double progress_at_ = 0.0;
    double detour_until_ = -1.0;
    double detour_side_ = -1.0;
    // talkthrough (ORM): the step's state.
    bool talk_seen_ = false;          // the event was seen running in this step
    double talk_free_since_ = -1.0;   // since when no event (-1: an event now)
    double talk_last_press_ = -1.0;   // when the last press began
    int talk_presses_ = 0;
};

// Called once per emulated frame by the hook. Does nothing unless
// LICHII_COOP_TEST_SCRIPT or LICHII_COOP_DEBUG_LIFE is set. In the Hookshot
// module (COOP_HOOKSHOT) it reads LICHII_COOP_TEST_COMMANDS instead: the
// autotest driver (tools/autotest/hookshot.py) holds the pad through
// Hookshot's control bridge and writes only the test-only steps there; they
// are played without a pad and the file is emptied once read.
//
// LICHII_COOP_DEBUG_LIFE=1 (the "downed, not dead" probe, oot-remake
// docs/CAIDO_REVIVIR.md): reads every frame Link's life and max, the Death
// component's word ([[actor + 0x270] + 0x1c0] + 0x20), whether an event runs,
// the current event object, the AS command in the main slot and the scene,
// and writes a "coop: life:" line (with Link's position) on each change, at
// most ten a second.
void OnFrame(Core::System& system);

// The spectator camera's wake nudge (coop_spectator.h): while set, player 1's
// pad (or the handheld one) gets the right stick at `right`; a running test
// script's input keeps its own right stick when it sets one. Nothing: let go.
// Present thread. Only in the emulator build.
void Nudge(Core::System& system, std::optional<Stick> right);

} // namespace CoopTestInput
