// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera, C1 and C2 (oot-remake docs/ESPECTADOR_Y_JEFES.md):
// what a downed Link's camera does, as rules with no emulator code (tested in
// tests/lichii/spectator_test.cpp; the glue is coop_camera_probe.cpp).
//
// While our Link lies downed the view turns, from where it is and at a capped
// rate, to look from his body at a mate: the nearest one still standing, or
// the one picked with the d-pad (right: next by number, left: previous). C2:
// the d-pad ring is the standing mates of our scene that are also in our room
// (RoomOf: a boss room is a room of its own, and inside a dungeon a mate more
// than a floor above or below is in another); downed and absent mates are
// skipped. With nobody standing in our room the nearest standing mate of the
// scene is followed (and the ring is all of them); with nobody standing at
// all, the nearest downed one. FollowLine is what the screen says ("Mirando a
// Ana"). When he is no longer downed (revived, game over) nothing more is
// written and the camera keeps the last yaw.
//
// E4 "E2" (AUDITORIA_PLAN §10 item 17: the camera goes with the one watched):
// the mate followed, picked or nearest, is kept when he walks into another
// room or floor of our scene while he stands ("en otra sala"), even with
// others standing in ours; the view keeps turning to him. When he leaves for
// another scene the camera cannot go there (it hangs on our body, and our
// body stays where it can be revived), so nothing is written, the view holds,
// and the screen says where he is ("en Aldea Kakariko") for away_hold_us, or
// for as long as nobody is in our scene; then the nearest of our scene. With
// nobody in our scene at all, the line names a mate standing elsewhere. He
// comes back: followed again.
//
// After a load the game's camera is "asleep" (C0): until the right stick has
// moved once it ignores a written yaw. A camera is awake when its yaw word
// agrees with the renderer's view (yaw = heading + 180, CoopCameraAim
// YawRule; a sleeping one has yaw = heading). C1 found that neither the bits
// the first touch sets at +0x18 nor a stick touch once Link is down wake it;
// a touch while he stands does. So, with the "stick" wake, a camera still
// asleep a few seconds after a load (the player has not moved it) gets a
// short right-then-left nudge on the emulated pad, while Link stands. C1 saw
// the pair leave the view 4-6 degrees off; C2 turns it back (the camera is
// awake by then, so the written yaw is taken) unless the player moves it.
//
// CameraLease (C2, REVISION_CAIDO.md): the glue reads and writes the camera
// object only through it, checked the same frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_camera_aim.h"
#include "coop_puppet.h"

namespace CoopSpectator {

using Vec3 = CoopCameraAim::Vec3;

// `from` turned toward `to` (degrees, the short way round) by at most
// `max_step`; the result wrapped to (-180, 180].
float StepAngle(float from, float to, float max_step);

// `from` moved toward `to` by at most `max_step` (no wrap: the pitch).
float StepLinear(float from, float to, float max_step);

// The pitch word that looks from `self` at `mate`: `rest` (the pitch word a
// woken camera has on level ground, C0: 14.93, looking down) less the mate's
// elevation in degrees; clamped to `limit` either way.
float PitchFor(const Vec3& self, const Vec3& mate, float rest, float limit);

// The camera's yaw word agrees with the view: |yaw - (heading + offset)| is
// within `tolerance` (C0: a sleeping camera after a load has yaw = heading,
// 180 away; a woken one heading + 180, +-7 while it turns).
bool Awake(float yaw, float view_heading, const CoopCameraAim::YawRule& rule, float tolerance);

enum class Wake : std::uint8_t {
    none,   // never woken: the written yaw is taken only after a stick touch
    bits,   // on going down, the +0x18 bits (C1: they do not wake it)
    stick,  // after a load, while Link stands, the right-stick nudge
    both,   // the two
};
// "none", "bits", "stick", "both" (any case); nothing for another word.
std::optional<Wake> WakeFrom(std::string_view text);
const char* ToString(Wake wake);

struct Settings {
    CoopCameraAim::YawRule rule{1.0f, 180.0f};
    float yaw_rate = 180.0f;     // degrees a second
    float pitch_rate = 90.0f;    // degrees a second
    float rest_pitch = 15.0f;    // pitch word on level ground (C0: 14.93)
    float pitch_limit = 60.0f;   // the pitch word stays within +-this
    float awake_tolerance = 25.0f;
    Wake wake = Wake::stick;
    std::uint64_t bits_wait_us = 1'000'000;      // the bits written, then this long to see the camera wake
    std::uint64_t nudge_after_us = 3'000'000;    // a camera asleep this long after it is found gets the nudge
    std::uint64_t nudge_retry_us = 5'000'000;    // and again this long after a nudge that did not wake it
    int nudge_tries = 3;
    std::uint64_t nudge_us = 100'000;            // each half (right, then left): 6 ticks of 1/60 s
    float nudge_x = 1.0f;                        // right stick x (C1: 0.6 did not wake a downed Link's camera)
    std::uint64_t nudge_check_us = 1'000'000;    // after the nudge, this long to see it wake
    // C2: after a nudge that woke it, the view is turned back to where it was
    // (the pair leaves 4-6 degrees, C1) at `restore_rate` degrees a second,
    // until within `restore_done` or for `restore_us` at most. A yaw word
    // that moved by more than `restore_taken` from what was written the frame
    // before is the player's own stick: he keeps it.
    float restore_rate = 90.0f;
    float restore_done = 0.5f;
    float restore_taken = 1.0f;
    std::uint64_t restore_us = 1'000'000;
    // C2: inside a dungeon, a mate whose feet are more than this above or
    // below ours is on another floor, so in another room (world units, about metres: 6 m).
    float floor_gap = 6.0f;
    // E4 "E2": a mate followed who left for another scene stays the one
    // watched (the screen says where) this long while others are in our scene.
    std::uint64_t away_hold_us = 10'000'000;
    std::uint64_t log_every_us = 1'000'000;
};

struct Mate {
    std::uint8_t number = 0;
    Vec3 position{};
    bool down = false;        // PLAYER_STATUS dead
    std::optional<int> room;  // RoomOf his feet; nothing: not known (taken as ours)
};

// E4 "E2": a mate present in another scene than ours.
struct Away {
    std::uint8_t number = 0;
    std::string scene;
    bool down = false;
};

// The room a point of `scene` is in, as far as the spectator can tell: a boss
// room is a room of its own (its DefaultDungeonRoomIndex, CoopBoss::RoomAt);
// the rest of a dungeon's scene is room -1; nothing outside a dungeon (the
// whole scene is one room). The place to add room volumes taken from the
// romfs (PLAN_100 E13), as data.
std::optional<int> RoomOf(std::string_view scene, const Vec3& feet);
// The scene is a dungeon: a boss's dungeon, the Well, the Ice Cavern, the
// Gerudo Training Ground or Ganon's Castle (floors count as rooms there).
bool IsDungeon(std::string_view scene);

// The d-pad bits (the emulator's NpadButton, coop_testinput.h).
inline constexpr std::uint64_t kDLeft = 1u << 12;
inline constexpr std::uint64_t kDRight = 1u << 14;

struct Input {
    std::uint64_t now_us = 0;
    bool down = false;              // our Link lies downed (not reviving, not game over)
    bool standing = false;          // our Link is up and playing (not downed, reviving or in a game over)
    std::uint64_t camera = 0;       // the camera object's address; 0 not found (or its lease dropped)
    std::optional<float> yaw;       // its yaw word
    std::optional<float> pitch;     // its pitch word
    std::optional<float> view_heading;  // the renderer's view now (CoopCameraAim::ViewOf)
    std::optional<Vec3> self;       // our Link
    std::optional<int> room;        // RoomOf our Link's feet
    bool dungeon = false;           // IsDungeon(our scene)
    std::vector<Mate> mates;        // the mates present in our scene (absent ones left out)
    std::vector<Away> away;         // E4 "E2": the mates present in another scene
    std::optional<std::uint64_t> buttons;  // the pad (nothing: unknown)
    std::uint8_t asked = 0;         // ORM /espectar: this mate from now (0: nothing asked)
};

struct Output {
    std::optional<float> yaw;    // write the yaw word
    std::optional<float> pitch;  // write the pitch word
    bool wake_bits = false;      // OR kWakeBits into the byte at kWakeFlags
    std::optional<float> nudge;  // hold the right stick at this x now; nothing: let go
    std::uint8_t following = 0;  // 0: no one
    std::uint8_t ring_at = 0;    // `following` is the ring_at-th (from 1) of the ring_size the d-pad goes round
    std::uint8_t ring_size = 0;
    bool elsewhere = false;      // nobody standing in our room: the one followed is in another room
    bool followed_down = false;  // nobody standing at all: the one followed is down too
    // E4 "E2": the one followed is in this other scene (nothing written: the
    // view holds); empty when he is in ours.
    std::string away_scene;
    std::vector<std::string> log;
};

// The line for the screen while following (Spanish, for the players); empty
// when not following anyone. "Mirando a Ana", then in brackets "2 de 3,
// cruceta para cambiar" when the d-pad has others to go to ("cruceta para
// cambiar" alone when the one followed is outside the d-pad's ring), "en
// otra sala" when `elsewhere`, "en <place>" when `away_scene` is set (`place`
// is what players call it, CoopMap::PlaceName; empty: the scene's own name),
// "caido" when `followed_down`. `name` empty: "P<number>".
std::string FollowLine(const std::string& name, const Output& out, const std::string& place = {});
// The followed mate's name tag in the in-game HUD: "> Ana <".
std::string FollowedTag(const std::string& tag);

// The byte at +0x18 went 0x46 -> 0x67 on the first right-stick touch after a
// load (C0); these are the bits that changed.
inline constexpr std::uint64_t kWakeFlags = 0x18;
inline constexpr std::uint8_t kWakeBits = 0x21;

// Downed: off, the bits being tried, following a mate.
enum class Phase : std::uint8_t { off, bits, following };
const char* ToString(Phase phase);

// Standing, the camera after a load: waiting to see it awake, the nudge's two
// halves, the check after it, the view turned back (C2), awake, given up.
enum class Prewake : std::uint8_t { waiting, right, left, check, restore, awake, gave_up };
const char* ToString(Prewake prewake);

class Spectator {
public:
    explicit Spectator(Settings settings = {}) : s_(settings) {}
    Output Tick(const Input& in);
    Phase phase() const { return phase_; }
    Prewake prewake() const { return pre_; }
    std::uint8_t following() const { return following_; }

private:
    struct Choice {
        std::uint8_t number = 0;
        std::uint8_t ring_at = 0;
        std::uint8_t ring_size = 0;
        bool elsewhere = false;
        bool down = false;
        std::string away_scene;  // E4 "E2": he is in this other scene
    };
    void Start(const Input& in, Output& out);
    void WakeAfterLoad(const Input& in, Output& out);
    Choice Choose(const Input& in, Output& out);
    bool AwakeNow(const Input& in) const;
    bool SameRoom(const Input& in, const Mate& m) const;

    Settings s_;
    Phase phase_ = Phase::off;
    std::uint64_t camera_ = 0;
    std::uint64_t phase_at_ = 0;
    std::uint64_t last_us_ = 0;
    std::uint64_t dt_us_ = 0;  // this tick's step, capped
    std::uint64_t logged_at_ = 0;
    bool logged_ = false;
    std::uint8_t following_ = 0;
    bool elsewhere_ = false;
    std::string away_scene_;   // E4 "E2": the scene the one followed is in, when not ours
    std::uint8_t away_of_ = 0;      // the one watched who left our scene (0: none)
    std::uint64_t away_since_ = 0;  // when he was first seen elsewhere
    std::uint8_t picked_ = 0;  // chosen with the d-pad; 0: the nearest
    std::uint64_t buttons_ = 0;
    bool no_mate_logged_ = false;
    std::optional<float> probe_from_;  // bits: the view heading the probe turn starts from
    // The wake after a load (standing).
    CoopSpectator::Prewake pre_ = Prewake::waiting;
    std::uint64_t pre_camera_ = 0;
    std::uint64_t pre_at_ = 0;
    int pre_tries_ = 0;
    std::optional<float> yaw_mark_;       // the yaw word when the nudge began (a woken camera rewrites it)
    std::optional<float> view_mark_;      // the view heading when the nudge began (C2: turned back to it)
    std::optional<float> restore_wrote_;  // the yaw written last tick while turning back
};

// The camera object the glue reads and writes, held only while it is the
// same live object. REVISION_CAIDO.md's suspicion, checked in C2: the camera
// was resolved once a second but written every frame, and its check (pitch
// limits, [[cam+8]+8]+0x1f0 = the actor) passes on a freed block, which keeps
// its old bytes until reused; so an unload with Link down left a second of
// yaw and pitch writes in freed memory, and more when the new PlayerCamera
// actor came back at the old address. A lease is taken for one scene and
// dropped when the scene changes or is not known (a load), when the renderer
// has drawn no scene for `blind_us`, or when the object no longer looks like
// the camera of its actor; every write checks it again the same instant.
class CameraLease {
public:
    explicit CameraLease(std::uint64_t blind_us = 500'000) : blind_us_(blind_us) {}
    // `cam` found for `actor` in `scene` (cam 0 or an empty scene: dropped).
    void Take(std::uint64_t cam, std::uint64_t actor, std::string_view scene, std::uint64_t now_us);
    void Drop(std::string why);
    // Once a frame, before the camera is read for a write: the camera, or 0
    // (dropped now or before; TakeWhy says why, once).
    std::uint64_t Check(CoopPuppet::Guest& guest, const std::optional<std::string>& scene, bool drawing,
                        std::uint64_t now_us);
    // `size` bytes at camera + `offset`, only while held and still the camera.
    bool Write(CoopPuppet::Guest& guest, std::uint64_t offset, const void* in, std::size_t size);
    std::uint64_t cam() const { return cam_; }
    // The last drop's reason, once (empty after it is taken).
    std::string TakeWhy() { return std::exchange(why_, {}); }

private:
    bool Same(CoopPuppet::Guest& guest) const;
    std::uint64_t blind_us_;
    std::uint64_t cam_ = 0;
    std::uint64_t actor_ = 0;
    std::string scene_;
    std::uint64_t drawn_at_ = 0;
    std::string why_;
};

} // namespace CoopSpectator
