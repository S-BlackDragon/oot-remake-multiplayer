// Tests for the spectator camera's rules (lichii/hook/coop_spectator.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop_spectator.h"

namespace sp = CoopSpectator;
namespace aim = CoopCameraAim;

namespace {

bool Has(const std::vector<std::string>& log, const std::string& part) {
    for (const auto& line : log) {
        if (line.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The game's camera as C0 and C1 saw it: awake, it takes a written yaw and
// the view follows a frame later (view heading = yaw - 180); asleep (after a
// load) the word is kept but the view stays where it is, until the right
// stick moves while Link stands: then the game writes the word from the view
// (yaw = heading + 180) and takes writes from then on. A stick touch while he
// is down does nothing. `bits_wake`: the +0x18 bits wake it too.
struct FakeCamera {
    bool awake = true;
    bool bits_wake = false;
    bool stick_wakes = true;
    float yaw = 0.0f;
    float pitch = 15.0f;
    float view = 0.0f;  // the renderer's heading
    std::uint8_t flags = 0x46;
    float right_turn = 2.0f;  // degrees a frame the stick turns it, each way (C1: the pair left 4-6 off)
    float left_turn = 2.0f;
    int nudged = 0;      // frames the stick was held
    int nudged_left = 0;
    int bits_written = 0;

    void Apply(const sp::Output& out, bool standing) {
        if (out.wake_bits) {
            ++bits_written;
            flags |= sp::kWakeBits;
            if (bits_wake) {
                awake = true;
            }
        }
        if (out.nudge) {
            ++nudged;
            nudged_left += *out.nudge < 0 ? 1 : 0;
            if (!awake && standing && stick_wakes) {
                awake = true;
                yaw = aim::Wrap(view + 180.0f);  // the game's own write, from the view
            }
            if (awake && standing) {
                yaw = aim::Wrap(yaw - (*out.nudge > 0 ? right_turn : left_turn) * *out.nudge);  // a little
            }
        }
        if (out.yaw) {
            yaw = *out.yaw;
        }
        if (out.pitch) {
            pitch = *out.pitch;
        }
    }
    // The next frame's view (one frame behind the word).
    void Render() {
        if (awake) {
            view = aim::Wrap(yaw - 180.0f);
        }
    }
};

struct Rig {
    sp::Spectator spectator;
    FakeCamera cam;
    std::uint64_t now = 1'000'000;
    std::vector<sp::Mate> mates;
    std::vector<sp::Away> away;  // E4 "E2": mates in another scene
    bool dungeon = false;        // E4 "E2": floors count as rooms
    aim::Vec3 self{0.0f, 0.0f, 0.0f};
    bool down = true;
    bool standing = false;
    std::uint64_t camera = 0x2140000000;
    std::uint64_t buttons = 0;
    std::vector<std::string> log;
    sp::Output last;

    explicit Rig(sp::Settings s = {}) : spectator(s) {}

    sp::Output Step(std::uint64_t dt_us = 16'667) {
        now += dt_us;
        sp::Input in;
        in.now_us = now;
        in.down = down;
        in.standing = standing;
        in.camera = camera;
        in.yaw = cam.yaw;
        in.pitch = cam.pitch;
        in.view_heading = cam.view;
        in.self = self;
        in.mates = mates;
        in.away = away;
        in.dungeon = dungeon;
        in.buttons = buttons;
        last = spectator.Tick(in);
        log.insert(log.end(), last.log.begin(), last.log.end());
        cam.Apply(last, standing);
        cam.Render();
        return last;
    }
    void Run(double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 60); ++i) {
            Step();
        }
    }
    void Stand() {
        down = false;
        standing = true;
    }
    void Fall() {
        down = true;
        standing = false;
    }
};

} // namespace

TEST_CASE("spectator: angles step the short way, capped, wrapped") {
    CHECK(sp::StepAngle(0.0f, 90.0f, 10.0f) == doctest::Approx(10.0f));
    CHECK(sp::StepAngle(0.0f, -90.0f, 10.0f) == doctest::Approx(-10.0f));
    CHECK(sp::StepAngle(170.0f, -170.0f, 30.0f) == doctest::Approx(-170.0f));  // 20 across +-180
    CHECK(sp::StepAngle(175.0f, -170.0f, 10.0f) == doctest::Approx(-175.0f));  // 185 wraps to -175
    CHECK(sp::StepAngle(-170.0f, 170.0f, 5.0f) == doctest::Approx(-175.0f));
    CHECK(sp::StepAngle(30.0f, 31.0f, 10.0f) == doctest::Approx(31.0f));
    CHECK(sp::StepLinear(15.0f, -20.0f, 9.0f) == doctest::Approx(6.0f));
    CHECK(sp::StepLinear(15.0f, 16.0f, 9.0f) == doctest::Approx(16.0f));
}

TEST_CASE("spectator: pitch toward a mate above, below, and level") {
    const aim::Vec3 self{0, 0, 0};
    CHECK(sp::PitchFor(self, {0, 0, 5}, 15.0f, 60.0f) == doctest::Approx(15.0f));
    CHECK(sp::PitchFor(self, {0, 5, 5}, 15.0f, 60.0f) == doctest::Approx(-30.0f));  // 45 up
    CHECK(sp::PitchFor(self, {0, -5, 5}, 15.0f, 60.0f) == doctest::Approx(60.0f));  // 15 + 45, at the limit
    CHECK(sp::PitchFor(self, {0, 50, 1}, 15.0f, 60.0f) == doctest::Approx(-60.0f));
}

TEST_CASE("spectator: awake when the yaw word is the view's heading + 180") {
    const aim::YawRule rule{1.0f, 180.0f};
    CHECK(sp::Awake(32.9f, -143.7f, rule, 25.0f));
    CHECK(sp::Awake(-126.0f, 54.0f, rule, 25.0f));
    CHECK_FALSE(sp::Awake(-140.0f, -140.0f, rule, 25.0f));  // C0: asleep after a load
    CHECK_FALSE(sp::Awake(NAN, 0.0f, rule, 25.0f));
    CHECK(sp::WakeFrom("Stick") == sp::Wake::stick);
    CHECK(sp::WakeFrom("both") == sp::Wake::both);
    CHECK_FALSE(sp::WakeFrom("maybe"));
}

TEST_CASE("spectator: an awake camera turns to the nearest mate at the capped rate, then holds") {
    Rig r;
    r.cam.yaw = 180.0f;  // view heading 0, looking down +z
    r.cam.view = 0.0f;
    r.mates = {{2, {5.0f, 0.0f, 0.0f}, false}};  // to +x: heading 90, yaw 270 = -90
    r.Step();
    CHECK(r.spectator.phase() == sp::Phase::following);
    CHECK(r.spectator.following() == 2);
    CHECK(Has(r.log, "the camera is awake"));
    CHECK(Has(r.log, "following P2 (nearest)"));
    CHECK_FALSE(r.last.wake_bits);
    CHECK_FALSE(r.last.nudge);
    // 0.25 s at 180/s: 45 degrees at most, never a jump.
    float before = r.cam.yaw;
    for (int i = 0; i < 15; ++i) {
        r.Step();
        CHECK(std::fabs(aim::Wrap(r.cam.yaw - before)) <= 3.0f + 0.01f);
        before = r.cam.yaw;
    }
    CHECK(std::fabs(aim::Wrap(r.cam.yaw - 180.0f)) == doctest::Approx(45.0f).epsilon(0.02));
    r.Run(1.0);
    CHECK(r.cam.yaw == doctest::Approx(-90.0f));
    CHECK(r.cam.view == doctest::Approx(90.0f));
    CHECK(r.cam.pitch == doctest::Approx(15.0f));  // level mate: the rest pitch
    // A long stall (a load) is one capped step, not a snap.
    r.mates[0].position = {0.0f, 0.0f, -5.0f};  // behind: heading 180, yaw 0
    r.Step(2'000'000);
    CHECK(std::fabs(aim::Wrap(r.cam.yaw - (-90.0f))) <= 18.0f + 0.01f);
}

TEST_CASE("spectator: a camera asleep after a load gets the nudge while Link stands, down later, it follows at once") {
    Rig r;
    r.Stand();
    r.cam.awake = false;
    r.cam.yaw = -140.0f;  // C0: the word after a load equals the view heading
    r.cam.view = -140.0f;
    r.mates = {{2, {5.0f, 0.0f, 5.0f}, false}};  // heading 45
    r.Run(2.9);
    CHECK(r.cam.nudged == 0);  // the player has 3 s to move it himself
    CHECK(r.spectator.prewake() == sp::Prewake::waiting);
    r.Run(0.4);
    CHECK(Has(r.log, "right-stick nudge, right then left"));
    CHECK(r.cam.nudged >= 10);
    CHECK(r.cam.nudged <= 14);  // 0.2 s of stick at most
    CHECK(r.cam.nudged_left >= 5);
    r.Run(1.0);
    CHECK(Has(r.log, "the camera woke (the right-stick nudge)"));
    CHECK(r.spectator.prewake() == sp::Prewake::awake);
    // Right then left: the view comes back about where it was.
    CHECK(std::fabs(aim::Wrap(r.cam.view - (-140.0f))) <= 4.0f);
    CHECK_FALSE(r.last.yaw);  // standing: nothing written
    r.Run(5.0);
    CHECK(r.cam.nudged <= 14);  // once
    r.Fall();
    r.Step();
    CHECK(r.spectator.phase() == sp::Phase::following);
    CHECK(Has(r.log, "spectator: on, the camera is awake"));
    CHECK(r.cam.bits_written == 0);
    r.Run(2.0);
    CHECK(r.cam.view == doctest::Approx(45.0f));
    CHECK_FALSE(r.last.nudge);
}

TEST_CASE("spectator: the player turns the camera himself: no nudge") {
    Rig r;
    r.Stand();
    r.cam.awake = false;
    r.cam.yaw = 10.0f;
    r.cam.view = 10.0f;
    r.Run(1.0);
    r.cam.awake = true;  // his own stick: the game rewrites the word from the view
    r.cam.yaw = aim::Wrap(r.cam.view + 180.0f + 5.0f);
    r.Run(5.0);
    CHECK(r.cam.nudged == 0);
    CHECK(r.spectator.prewake() == sp::Prewake::awake);
    // A new camera (a load): asleep again, waited for again.
    r.camera += 0x1000;
    r.cam.awake = false;
    r.cam.yaw = r.cam.view;
    r.Run(1.0);
    CHECK(r.spectator.prewake() == sp::Prewake::waiting);
    r.Run(3.0);
    CHECK(r.cam.nudged > 0);
}

TEST_CASE("spectator: a nudge that does not wake it is tried three times, then given up") {
    Rig r;
    r.Stand();
    r.cam.awake = false;
    r.cam.stick_wakes = false;
    r.cam.yaw = 10.0f;
    r.cam.view = 10.0f;
    r.Run(30.0);
    CHECK(r.spectator.prewake() == sp::Prewake::gave_up);
    CHECK(Has(r.log, "did not wake after 3 nudges"));
    CHECK(r.cam.nudged <= 3 * 14);
}

TEST_CASE("spectator: a downed Link's sleeping camera gets no nudge, the bits are tried only with wake bits") {
    Rig r;  // wake stick (the default)
    r.cam.awake = false;
    r.cam.yaw = -140.0f;
    r.cam.view = -140.0f;
    r.mates = {{2, {5.0f, 0.0f, 5.0f}, false}};
    r.Run(5.0);
    CHECK(r.cam.nudged == 0);
    CHECK(r.cam.bits_written == 0);
    CHECK(Has(r.log, "the view may not follow"));
    CHECK(r.spectator.phase() == sp::Phase::following);
    CHECK(r.cam.view == doctest::Approx(-140.0f));  // it never moved

    sp::Settings b;
    b.wake = sp::Wake::bits;
    Rig q(b);
    q.cam.awake = false;
    q.cam.yaw = -140.0f;
    q.cam.view = -140.0f;
    q.mates = {{2, {5.0f, 0.0f, 5.0f}, false}};  // heading 45: the short way from -140 is -175 (right)
    q.Step();
    CHECK(q.spectator.phase() == sp::Phase::bits);
    CHECK(q.last.wake_bits);
    REQUIRE(q.last.yaw);
    CHECK(std::fabs(aim::Wrap(*q.last.yaw - aim::Wrap(-140.0f + 180.0f))) == doctest::Approx(10.0f));
    q.Run(1.05);
    CHECK(Has(q.log, "the bits did not wake the camera; writing the yaw anyway"));
    CHECK(q.spectator.phase() == sp::Phase::following);
    CHECK(q.cam.nudged == 0);
    CHECK(q.cam.flags == 0x67);
}

TEST_CASE("spectator: the bits wake it (a camera that takes them)") {
    sp::Settings b;
    b.wake = sp::Wake::both;
    Rig r(b);
    r.cam.awake = false;
    r.cam.bits_wake = true;
    r.cam.yaw = -140.0f;
    r.cam.view = -140.0f;
    r.mates = {{3, {-5.0f, 0.0f, 0.0f}, false}};
    r.Run(0.2);
    CHECK(Has(r.log, "the camera woke (the +0x18 bits)"));
    CHECK(r.cam.nudged == 0);
    CHECK(r.spectator.phase() == sp::Phase::following);
}

TEST_CASE("spectator: wake none never writes the bits nor nudges") {
    sp::Settings n;
    n.wake = sp::Wake::none;
    Rig q(n);
    q.Stand();
    q.cam.awake = false;
    q.cam.yaw = 10.0f;
    q.cam.view = 10.0f;
    q.mates = {{2, {0.0f, 0.0f, 5.0f}, false}};
    q.Run(10.0);
    q.Fall();
    q.Run(2.0);
    CHECK(q.cam.bits_written == 0);
    CHECK(q.cam.nudged == 0);
    CHECK(sp::WakeFrom("NONE") == sp::Wake::none);
}

TEST_CASE("spectator: the nearest standing mate, the d-pad cycles, a pick that falls goes back to the nearest") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.mates = {{2, {1.0f, 0.0f, 0.0f}, true},    // nearest, but down
               {3, {0.0f, 0.0f, 4.0f}, false},
               {5, {0.0f, 0.0f, -9.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 3);
    r.buttons = sp::kDRight;
    r.Step();
    CHECK(r.spectator.following() == 5);
    CHECK(Has(r.log, "d-pad right: P5"));
    CHECK(Has(r.log, "following P5 (picked)"));
    r.Step();  // held: no second step
    CHECK(r.spectator.following() == 5);
    r.buttons = 0;
    r.Step();
    r.buttons = sp::kDRight;
    r.Step();
    CHECK(r.spectator.following() == 3);  // wraps (P2 is down, not in the ring)
    r.buttons = 0;
    r.Step();
    r.buttons = sp::kDLeft;
    r.Step();
    CHECK(r.spectator.following() == 5);
    r.buttons = 0;
    r.mates[2].down = true;  // P5 falls
    r.Step();
    CHECK(Has(r.log, "P5 is no longer there to follow"));
    CHECK(r.spectator.following() == 3);
    // Every one down: the nearest of them.
    r.mates[1].down = true;
    r.Step();
    CHECK(r.spectator.following() == 2);
}

TEST_CASE("spectator: released when no longer downed: nothing written, the yaw kept") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.mates = {{2, {5.0f, 0.0f, 0.0f}, false}};
    r.Run(0.3);
    const float kept = r.cam.yaw;
    r.down = false;
    r.Step();
    CHECK(Has(r.log, "released (no longer downed)"));
    CHECK_FALSE(r.last.yaw);
    CHECK_FALSE(r.last.pitch);
    CHECK_FALSE(r.last.nudge);
    CHECK(r.spectator.phase() == sp::Phase::off);
    CHECK(r.spectator.following() == 0);
    r.Run(1.0);
    CHECK(r.cam.yaw == doctest::Approx(kept));
    // Down again: on again, from where the view is.
    r.down = true;
    r.Step();
    CHECK(r.spectator.phase() == sp::Phase::following);
    REQUIRE(r.last.yaw);
    CHECK(std::fabs(aim::Wrap(*r.last.yaw - kept)) <= 3.01f);
}

TEST_CASE("spectator: no mate in the scene: on, but nothing written") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.Step();
    CHECK(Has(r.log, "no mate in the scene to follow"));
    CHECK_FALSE(r.last.yaw);
    CHECK(r.spectator.following() == 0);
    r.mates = {{4, {0.0f, 0.0f, 3.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 4);
    CHECK(r.last.yaw);
}

// ---- C2 -------------------------------------------------------------------

TEST_CASE("spectator C2: six players, the d-pad goes round the standing ones, skips the downed and the absent") {
    Rig r;
    r.cam.yaw = 180.0f;
    // Five mates (six players): P3 down, the others standing around us.
    r.mates = {{2, {2.0f, 0.0f, 0.0f}, false},
               {3, {1.0f, 0.0f, 0.0f}, true},
               {4, {0.0f, 0.0f, 6.0f}, false},
               {5, {-8.0f, 0.0f, 0.0f}, false},
               {6, {0.0f, 0.0f, -9.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 2);  // the nearest standing (P3 is nearer, but down)
    CHECK(r.last.ring_size == 4);
    CHECK(r.last.ring_at == 1);
    CHECK_FALSE(r.last.elsewhere);
    CHECK(sp::FollowLine("Ana", r.last) == "Mirando a Ana (1 de 4, cruceta para cambiar)");
    const auto press = [&](std::uint64_t button) {
        r.buttons = button;
        r.Step();
        r.buttons = 0;
        r.Step();
        return r.spectator.following();
    };
    CHECK(press(sp::kDRight) == 4);
    CHECK(press(sp::kDRight) == 5);
    CHECK(press(sp::kDRight) == 6);
    CHECK(r.last.ring_at == 4);
    CHECK(press(sp::kDRight) == 2);  // round, past the downed P3
    CHECK(press(sp::kDLeft) == 6);
    CHECK(Has(r.log, "d-pad left: P6 (4 of 4)"));
    // P6 leaves the session (presence no longer shows him): back to the nearest.
    r.mates.pop_back();
    r.Step();
    CHECK(Has(r.log, "P6 is no longer there to follow"));
    CHECK(r.spectator.following() == 2);
    CHECK(r.last.ring_size == 3);
    // P3 is revived: in the ring again, and the nearest.
    r.mates[1].down = false;
    r.Step();
    CHECK(r.spectator.following() == 3);
    CHECK(r.last.ring_size == 4);
    // Everyone else falls: the nearest downed one, said on the screen.
    for (auto& m : r.mates) {
        m.down = true;
    }
    r.Step();
    CHECK(r.spectator.following() == 3);
    CHECK(r.last.followed_down);
    CHECK(sp::FollowLine("", r.last) == "Mirando a P3 (2 de 4, cruceta para cambiar, ca\xc3\xad" "do)");
}

TEST_CASE("spectator C2: one mate only: no d-pad note, nobody: no line") {
    sp::Output out;
    CHECK(sp::FollowLine("Ana", out).empty());
    out.following = 2;
    out.ring_at = 1;
    out.ring_size = 1;
    CHECK(sp::FollowLine("Ana", out) == "Mirando a Ana");
    out.elsewhere = true;
    CHECK(sp::FollowLine("Ana", out) == "Mirando a Ana (en otra sala)");
    CHECK(sp::FollowedTag("Ana") == "> Ana <");
}

TEST_CASE("spectator C2: rooms: a boss room is its own, the rest of the dungeon another, outside one room") {
    // Gohma's room (coop_boss.cpp): centre 7.5 67.2, radius 37, height 43 to 63, DefaultDungeonRoomIndex 10.
    CHECK(sp::IsDungeon("InsideTheDekuTree"));
    CHECK(sp::IsDungeon("IceCavern"));
    CHECK_FALSE(sp::IsDungeon("KokiriForest_Child"));
    CHECK(sp::RoomOf("InsideTheDekuTree", {6.7f, 47.0f, 67.6f}) == 10);
    CHECK(sp::RoomOf("InsideTheDekuTree", {6.7f, 30.0f, 67.6f}) == -1);  // below the room's height band
    CHECK(sp::RoomOf("InsideTheDekuTree", {-54.4f, 112.0f, 12.2f}) == -1);
    CHECK(sp::RoomOf("IceCavern", {0.0f, 0.0f, 0.0f}) == -1);
    CHECK_FALSE(sp::RoomOf("KokiriForest_Child", {0.0f, 0.0f, 0.0f}));
}

TEST_CASE("spectator C2: behind the boss door: only the mates of our room, else the nearest, said so") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.self = {6.7f, 47.0f, 67.6f};
    const auto room = [](const aim::Vec3& p) { return sp::RoomOf("InsideTheDekuTree", p); };
    // P2 in the boss room with us, 12 m away; P3 and P4 under it (another room), right below us.
    r.mates = {{2, {18.7f, 47.0f, 67.6f}, false, room({18.7f, 47.0f, 67.6f})},
               {3, {6.7f, 37.0f, 67.6f}, false, room({6.7f, 37.0f, 67.6f})},
               {4, {7.7f, 37.0f, 67.6f}, false, room({7.7f, 37.0f, 67.6f})}};
    // As Rig::Step, with our room and the dungeon flag.
    const auto step = [&]() {
        r.now += 16'667;
        sp::Input in;
        in.now_us = r.now;
        in.down = r.down;
        in.standing = r.standing;
        in.camera = r.camera;
        in.yaw = r.cam.yaw;
        in.pitch = r.cam.pitch;
        in.view_heading = r.cam.view;
        in.self = r.self;
        in.room = room(r.self);
        in.dungeon = true;
        in.mates = r.mates;
        in.buttons = r.buttons;
        r.last = r.spectator.Tick(in);
        r.log.insert(r.log.end(), r.last.log.begin(), r.last.log.end());
        r.cam.Apply(r.last, r.standing);
        r.cam.Render();
    };
    step();
    CHECK(r.spectator.following() == 2);
    CHECK(r.last.ring_size == 1);  // the d-pad does not go to the others' room
    CHECK_FALSE(r.last.elsewhere);
    r.buttons = sp::kDRight;
    step();
    CHECK(r.spectator.following() == 2);
    r.buttons = 0;
    step();
    // P2 falls: nobody standing in our room, so the nearest standing elsewhere.
    r.mates[0].down = true;
    step();
    CHECK(r.spectator.following() == 3);
    CHECK(r.last.elsewhere);
    CHECK(r.last.ring_size == 2);
    CHECK(Has(r.log, "following P3 (nearest), in another room"));
    CHECK(sp::FollowLine("Ana", r.last) == "Mirando a Ana (1 de 2, cruceta para cambiar, en otra sala)");
    r.buttons = sp::kDRight;
    step();
    CHECK(r.spectator.following() == 4);
    r.buttons = 0;
    step();
    // P4, the one picked, comes up into our room: still him, now in our room.
    r.mates[2].position = {10.0f, 47.0f, 67.6f};
    r.mates[2].room = room(r.mates[2].position);
    step();
    CHECK(r.spectator.following() == 4);
    CHECK_FALSE(r.last.elsewhere);
    CHECK(r.last.ring_size == 1);
}

TEST_CASE("spectator C2: inside a dungeon a mate a floor above is in another room, outside one is not") {
    sp::Input in;
    in.now_us = 1'000'000;
    in.down = true;
    in.camera = 0x2140000000;
    in.yaw = 180.0f;
    in.pitch = 15.0f;
    in.view_heading = 0.0f;
    in.self = aim::Vec3{0.0f, 0.0f, 0.0f};
    in.mates = {{2, {1.0f, 9.0f, 0.0f}, false}, {3, {0.0f, 0.0f, 8.0f}, false}};
    in.dungeon = true;
    in.room = -1;
    sp::Spectator a;
    CHECK(a.Tick(in).following == 3);  // P2 is nearer on the ground but 9 m up
    in.dungeon = false;
    in.room.reset();
    sp::Spectator b;
    CHECK(b.Tick(in).following == 2);  // a hill outside: the same room
}

TEST_CASE("spectator C2: the view comes back after the wake nudge, the player's own stick wins") {
    Rig r;
    r.Stand();
    r.cam.awake = false;
    r.cam.right_turn = 2.0f;
    r.cam.left_turn = 1.2f;  // the pair leaves the view off, as in C1
    r.cam.yaw = -140.0f;
    r.cam.view = -140.0f;
    r.Run(3.3);
    REQUIRE(Has(r.log, "right-stick nudge"));
    r.Run(0.3);
    REQUIRE(Has(r.log, "the camera woke (the right-stick nudge)"));
    r.Run(1.0);
    CHECK(Has(r.log, "the view is back where it was before the nudge"));
    CHECK(r.spectator.prewake() == sp::Prewake::awake);
    CHECK(std::fabs(aim::Wrap(r.cam.view - (-140.0f))) <= 0.6f);
    CHECK_FALSE(r.last.yaw);

    // The same, but the player turns the camera while it is being turned back.
    Rig q;
    q.Stand();
    q.cam.awake = false;
    q.cam.right_turn = 2.0f;
    q.cam.left_turn = 0.5f;
    q.cam.yaw = 30.0f;
    q.cam.view = 30.0f;
    q.Run(3.3);
    for (int i = 0; i < 60 && q.spectator.prewake() != sp::Prewake::restore; ++i) {
        q.Step();
    }
    REQUIRE(q.spectator.prewake() == sp::Prewake::restore);
    q.Step();
    q.cam.yaw = aim::Wrap(q.cam.yaw + 20.0f);  // his stick
    q.Step();
    CHECK(Has(q.log, "the player turns the camera"));
    CHECK(q.spectator.prewake() == sp::Prewake::awake);
    const float his = q.cam.yaw;
    q.Run(1.0);
    CHECK(q.cam.yaw == doctest::Approx(his));  // nothing more written
}

TEST_CASE("spectator C2: revived: released, no line, nothing written") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.mates = {{2, {5.0f, 0.0f, 0.0f}, false}, {4, {0.0f, 0.0f, 5.0f}, false}};
    r.Run(0.5);
    CHECK_FALSE(sp::FollowLine("Ana", r.last).empty());
    r.down = false;  // reviving, then up
    r.Step();
    CHECK(Has(r.log, "released (no longer downed)"));
    CHECK(sp::FollowLine("Ana", r.last).empty());
    CHECK(r.last.following == 0);
    CHECK_FALSE(r.last.yaw);
    CHECK_FALSE(r.last.pitch);
}

namespace {

// Guest memory as bytes by address; unmapped reads and writes fail.
class MapGuest : public CoopPuppet::Guest {
public:
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(address + i);
            if (it == bytes.end()) {
                return false;
            }
            o[i] = it->second;
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            if (bytes.find(address + i) == bytes.end()) {
                return false;
            }
        }
        for (std::size_t i = 0; i < size; ++i) {
            bytes[address + i] = p[i];
        }
        ++writes;
        return true;
    }
    template <typename T>
    void Put(std::uint64_t at, T v) {
        std::uint8_t raw[sizeof(T)];
        std::memcpy(raw, &v, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes[at + i] = raw[i];
        }
    }
    template <typename T>
    T At(std::uint64_t at) {
        T v{};
        REQUIRE(Read(at, &v, sizeof(T)));
        return v;
    }
    std::map<std::uint64_t, std::uint8_t> bytes;
    int writes = 0;
};

// A camera object as C0 found it: pitch limits {-85, 89}, a yaw, and
// [[cam+8]+8]+0x1f0 = its PlayerCamera actor.
void PutCamera(MapGuest& g, std::uint64_t cam, std::uint64_t actor) {
    const std::uint64_t ctx = cam + 0x1000, inner = cam + 0x2000;
    g.Put<float>(cam + aim::kPitchLimits, aim::kPitchMin);
    g.Put<float>(cam + aim::kPitchLimits + 4, aim::kPitchMax);
    g.Put<float>(cam + aim::kPitch, 15.0f);
    g.Put<float>(cam + aim::kYaw, 10.0f);
    g.Put<std::uint8_t>(cam + sp::kWakeFlags, 0x46);
    g.Put<std::uint64_t>(cam + aim::kContext, ctx);
    g.Put<std::uint64_t>(ctx + aim::kContextInner, inner);
    g.Put<std::uint64_t>(inner + aim::kContextActor, actor);
}

} // namespace

TEST_CASE("spectator C2: the camera lease: written while it holds, never after a load (REVISION_CAIDO)") {
    constexpr std::uint64_t kCam = 0x2140000000, kActor = 0x2150000000;
    MapGuest g;
    PutCamera(g, kCam, kActor);
    const std::optional<std::string> kokiri = std::string("KokiriForest_Child");
    const std::optional<std::string> kakariko = std::string("KakarikoVillage_Child");
    sp::CameraLease lease;
    const std::uint64_t now = 1'000'000;
    lease.Take(kCam, kActor, *kokiri, now);
    CHECK(lease.Check(g, kokiri, true, now) == kCam);
    const float yaw = -60.0f;
    CHECK(lease.Write(g, aim::kYaw, &yaw, sizeof(yaw)));
    CHECK(g.At<float>(kCam + aim::kYaw) == doctest::Approx(-60.0f));
    const float other = 33.0f;

    SUBCASE("a scene change: the old object is let go although its bytes still look right (freed, not reused)") {
        CHECK(lease.Check(g, kakariko, true, now + 16'667) == 0);
        CHECK(lease.TakeWhy().find("the scene changed") != std::string::npos);
        const int before = g.writes;
        CHECK_FALSE(lease.Write(g, aim::kYaw, &other, sizeof(other)));
        CHECK(g.writes == before);
        CHECK(g.At<float>(kCam + aim::kYaw) == doctest::Approx(-60.0f));
        // Back in the same scene it does not come back: only a new Take does.
        CHECK(lease.Check(g, kokiri, true, now + 33'000) == 0);
        CHECK(lease.TakeWhy().empty());  // said once
    }
    SUBCASE("the scene not known (a load): let go") {
        CHECK(lease.Check(g, std::nullopt, true, now + 16'667) == 0);
        CHECK(lease.TakeWhy().find("not known") != std::string::npos);
        CHECK(lease.cam() == 0);
    }
    SUBCASE("no scene drawn for half a second: let go, a shorter gap is not") {
        CHECK(lease.Check(g, kokiri, false, now + 300'000) == kCam);
        CHECK(lease.Check(g, kokiri, true, now + 316'000) == kCam);
        CHECK(lease.Check(g, kokiri, false, now + 700'000) == kCam);
        CHECK(lease.Check(g, kokiri, false, now + 816'000) == 0);
        CHECK(lease.TakeWhy().find("no scene drawn") != std::string::npos);
    }
    SUBCASE("the block reused by something else: let go at the write itself") {
        g.Put<float>(kCam + aim::kPitchLimits, 1.0f);
        const int before = g.writes;
        CHECK_FALSE(lease.Write(g, aim::kYaw, &other, sizeof(other)));
        CHECK(g.writes == before);
        CHECK(lease.cam() == 0);
        CHECK(lease.TakeWhy().find("no longer the camera") != std::string::npos);
    }
    SUBCASE("its chain points at another actor (a new PlayerCamera): let go at the check") {
        PutCamera(g, kCam, kActor + 0x800);
        CHECK(lease.Check(g, kokiri, true, now + 16'667) == 0);
        CHECK(lease.cam() == 0);
    }
    SUBCASE("a new camera in the new scene is taken and written") {
        constexpr std::uint64_t kCam2 = 0x2160000000;
        PutCamera(g, kCam2, kActor);
        CHECK(lease.Check(g, kakariko, true, now + 16'667) == 0);
        lease.Take(kCam2, kActor, *kakariko, now + 2'000'000);
        CHECK(lease.Check(g, kakariko, true, now + 2'000'000) == kCam2);
        CHECK(lease.Write(g, aim::kYaw, &yaw, sizeof(yaw)));
        CHECK(g.At<float>(kCam2 + aim::kYaw) == doctest::Approx(-60.0f));
    }
    SUBCASE("no camera or no scene: nothing held") {
        sp::CameraLease empty;
        empty.Take(0, kActor, "KokiriForest_Child", now);
        CHECK(empty.Check(g, kokiri, true, now) == 0);
        empty.Take(kCam, kActor, "", now);
        CHECK(empty.Check(g, kokiri, true, now) == 0);
        CHECK_FALSE(empty.Write(g, aim::kYaw, &yaw, sizeof(yaw)));
    }
}

// ---- E4 "E2": the camera goes with the one watched --------------------------

TEST_CASE("spectator E2: the one followed goes to another floor: the camera goes with him, picked or nearest") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.dungeon = true;
    r.mates = {{2, {0.0f, 0.0f, 3.0f}, false}, {3, {0.0f, 0.0f, -8.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 2);
    // P2 climbs to the floor above (9 m up): another room, P3 still in ours. Still P2.
    r.mates[0].position = {0.0f, 9.0f, 3.0f};
    r.Step();
    CHECK(r.spectator.following() == 2);
    CHECK(r.last.elsewhere);
    CHECK(r.last.ring_at == 0);
    CHECK(r.last.ring_size == 1);
    CHECK(Has(r.log, "following P2 (nearest), in another room (the camera goes with him)"));
    CHECK(sp::FollowLine("Ana", r.last) == "Mirando a Ana (cruceta para cambiar, en otra sala)");
    REQUIRE(r.last.yaw);
    REQUIRE(r.last.pitch);
    r.Run(1.0);
    CHECK(r.cam.pitch < 0.0f);  // looking up at him
    CHECK(r.spectator.following() == 2);
    // The d-pad goes to the ring: P3, picked.
    r.buttons = sp::kDRight;
    r.Step();
    r.buttons = 0;
    r.Step();
    CHECK(r.spectator.following() == 3);
    // The one picked goes to the floor below: still him.
    r.mates[1].position = {0.0f, -9.0f, -8.0f};
    r.Step();
    CHECK(r.spectator.following() == 3);
    CHECK(r.last.elsewhere);
    CHECK_FALSE(Has(r.log, "P3 is no longer there to follow"));
    // He falls there: the nearest standing (P2, who is in another room too).
    r.mates[1].down = true;
    r.Step();
    CHECK(Has(r.log, "P3 is no longer there to follow"));
    CHECK(r.spectator.following() == 2);
}

TEST_CASE("spectator E2: the one followed leaves for another scene: the view holds, the screen says where, then the nearest") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.mates = {{2, {0.0f, 0.0f, 3.0f}, false}, {3, {0.0f, 0.0f, -8.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 2);
    r.mates.erase(r.mates.begin());
    r.away = {{2, "KakarikoVillage", false}};
    r.Step();
    CHECK(r.spectator.following() == 2);
    CHECK(r.last.away_scene == "KakarikoVillage");
    CHECK_FALSE(r.last.yaw);
    CHECK_FALSE(r.last.pitch);
    CHECK(Has(r.log, "P2 went to KakarikoVillage: the camera cannot go there"));
    CHECK(Has(r.log, "following P2 (nearest), in KakarikoVillage (the view holds)"));
    CHECK(sp::FollowLine("Ana", r.last, "Aldea Kakariko") == "Mirando a Ana (en Aldea Kakariko, cruceta para cambiar)");
    CHECK(sp::FollowLine("Ana", r.last) == "Mirando a Ana (en KakarikoVillage, cruceta para cambiar)");
    const float held = r.cam.yaw;
    r.Run(5.0);
    CHECK(r.spectator.following() == 2);
    CHECK(r.cam.yaw == doctest::Approx(held));
    // He comes back before the hold ends: followed again, the view turns to him.
    r.away.clear();
    r.mates.insert(r.mates.begin(), sp::Mate{2, {3.0f, 0.0f, 0.0f}, false});
    r.Step();
    CHECK(r.spectator.following() == 2);
    CHECK(r.last.away_scene.empty());
    CHECK(r.last.yaw);
    // He leaves again and stays: the hold starts over, then the nearest of our scene.
    r.mates.erase(r.mates.begin());
    r.away = {{2, "KakarikoVillage", false}};
    r.Run(9.0);
    CHECK(r.spectator.following() == 2);
    r.Run(1.5);
    CHECK(r.spectator.following() == 3);
    CHECK(Has(r.log, "P2 stays in KakarikoVillage (10 s); back to the nearest of our scene"));
    CHECK(r.last.away_scene.empty());
    // The d-pad while he is away goes to the ring at once.
    sp::Settings s;
    Rig q(s);
    q.cam.yaw = 180.0f;
    q.mates = {{2, {0.0f, 0.0f, 3.0f}, false}, {3, {0.0f, 0.0f, -8.0f}, false}};
    q.Step();
    q.mates.erase(q.mates.begin());
    q.away = {{2, "KakarikoVillage", false}};
    q.Step();
    q.buttons = sp::kDRight;
    q.Step();
    CHECK(q.spectator.following() == 3);
    CHECK(q.last.away_scene.empty());
    // Down where he went: not kept.
    Rig d;
    d.cam.yaw = 180.0f;
    d.mates = {{2, {0.0f, 0.0f, 3.0f}, false}, {3, {0.0f, 0.0f, -8.0f}, false}};
    d.Step();
    d.mates.erase(d.mates.begin());
    d.away = {{2, "KakarikoVillage", true}};
    d.Step();
    CHECK(d.spectator.following() == 3);
}

TEST_CASE("spectator E2: nobody in our scene: the line names a mate standing elsewhere while that lasts") {
    Rig r;
    r.cam.yaw = 180.0f;
    r.away = {{4, "LostWoods", false}, {3, "KakarikoVillage", true}};
    r.Step();
    CHECK(r.spectator.following() == 4);
    CHECK(r.last.away_scene == "LostWoods");
    CHECK_FALSE(r.last.yaw);
    CHECK(Has(r.log, "nobody in our scene; P4 is in LostWoods"));
    CHECK(sp::FollowLine("", r.last) == "Mirando a P4 (en LostWoods)");
    r.Run(15.0);
    CHECK(r.spectator.following() == 4);  // kept past the hold: there is nobody here to look at
    // A mate comes into our scene: him (P4 was away longer than the hold).
    r.mates = {{5, {0.0f, 0.0f, 4.0f}, false}};
    r.Step();
    CHECK(r.spectator.following() == 5);
    CHECK(r.last.yaw);
    // Nobody anywhere: no line.
    Rig e;
    e.Step();
    CHECK(e.spectator.following() == 0);
    CHECK(Has(e.log, "no mate in the scene to follow"));
}
