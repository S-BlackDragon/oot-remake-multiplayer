// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera, C1: the rules (coop_spectator.h).

#include "coop_spectator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "coop_boss.h"

namespace CoopSpectator {
namespace aim = CoopCameraAim;
namespace {

constexpr double kDeg = 180.0 / 3.14159265358979323846;
// A stall (a load, a pause) is not a long turn.
constexpr std::uint64_t kMaxStepUs = 100'000;
// The bits phase writes the yaw this far from the view, toward the mate: a
// woken camera turns to it within a frame or two, a sleeping one does not.
constexpr float kProbeDegrees = 10.0f;
constexpr float kProbeArrived = 4.0f;

std::string Fmt(const char* format, double a = 0, double b = 0, double c = 0, double d = 0) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), format, a, b, c, d);
    return buf;
}

std::string P(std::uint8_t n) {
    return "P" + std::to_string(n);
}

} // namespace

float StepAngle(float from, float to, float max_step) {
    const float diff = aim::Wrap(to - from);
    const float step = std::clamp(diff, -std::fabs(max_step), std::fabs(max_step));
    return aim::Wrap(from + step);
}

float StepLinear(float from, float to, float max_step) {
    return from + std::clamp(to - from, -std::fabs(max_step), std::fabs(max_step));
}

float PitchFor(const Vec3& self, const Vec3& mate, float rest, float limit) {
    const double dy = mate[1] - self[1];
    const double ground = std::hypot(mate[0] - self[0], mate[2] - self[2]);
    const double elevation = std::atan2(dy, std::max(ground, 0.01)) * kDeg;
    return std::clamp(static_cast<float>(rest - elevation), -std::fabs(limit), std::fabs(limit));
}

bool Awake(float yaw, float view_heading, const aim::YawRule& rule, float tolerance) {
    if (!std::isfinite(yaw) || !std::isfinite(view_heading)) {
        return false;
    }
    return std::fabs(aim::Wrap(yaw - aim::YawFor(view_heading, rule))) <= tolerance;
}

std::optional<Wake> WakeFrom(std::string_view text) {
    std::string t;
    for (const char c : text) {
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const Wake w : {Wake::none, Wake::bits, Wake::stick, Wake::both}) {
        if (t == ToString(w)) {
            return w;
        }
    }
    return std::nullopt;
}

const char* ToString(Wake wake) {
    switch (wake) {
    case Wake::none:
        return "none";
    case Wake::bits:
        return "bits";
    case Wake::stick:
        return "stick";
    case Wake::both:
        return "both";
    }
    return "?";
}

const char* ToString(Phase phase) {
    switch (phase) {
    case Phase::off:
        return "off";
    case Phase::bits:
        return "bits";
    case Phase::following:
        return "following";
    }
    return "?";
}

const char* ToString(Prewake prewake) {
    switch (prewake) {
    case Prewake::waiting:
        return "waiting";
    case Prewake::right:
        return "right";
    case Prewake::left:
        return "left";
    case Prewake::check:
        return "check";
    case Prewake::restore:
        return "restore";
    case Prewake::awake:
        return "awake";
    case Prewake::gave_up:
        return "gave_up";
    }
    return "?";
}

std::optional<int> RoomOf(std::string_view scene, const Vec3& feet) {
    if (!IsDungeon(scene)) {
        return std::nullopt;
    }
    if (const CoopBoss::Room* room = CoopBoss::RoomAt(scene, CoopBoss::Vec{feet[0], feet[1], feet[2]})) {
        return room->game_room;
    }
    return -1;
}

bool IsDungeon(std::string_view scene) {
    // The dungeons with no boss room in CoopBoss::Rooms (scene names as the
    // hook reads them: coop/host dungeon lists, the harness's warps).
    constexpr std::array<std::string_view, 4> kOthers{"BottomOfTheWell", "IceCavern", "GerudoTrainingGround",
                                                      "InsideGanonCastle"};
    return CoopBoss::RoomOfScene(scene) != nullptr ||
           std::find(kOthers.begin(), kOthers.end(), scene) != kOthers.end();
}

std::string FollowLine(const std::string& name, const Output& out, const std::string& place) {
    if (out.following == 0) {
        return {};
    }
    std::string line = "Mirando a " + (name.empty() ? P(out.following) : name);
    std::vector<std::string> notes;
    if (!out.away_scene.empty()) {
        notes.push_back("en " + (place.empty() ? out.away_scene : place));
    }
    if (out.ring_size > 1 && out.ring_at != 0) {
        notes.push_back(std::to_string(out.ring_at) + " de " + std::to_string(out.ring_size) +
                        ", cruceta para cambiar");
    } else if (out.ring_at == 0 && out.ring_size != 0) {
        notes.push_back("cruceta para cambiar");  // E4 "E2": kept outside the ring; the d-pad goes to the ring
    }
    if (out.elsewhere) {
        notes.push_back("en otra sala");
    }
    if (out.followed_down) {
        notes.push_back("ca\xc3\xad" "do");
    }
    for (std::size_t i = 0; i < notes.size(); ++i) {
        line += (i == 0 ? " (" : ", ") + notes[i];
    }
    if (!notes.empty()) {
        line += ")";
    }
    return line;
}

std::string FollowedTag(const std::string& tag) {
    return "> " + tag + " <";
}

bool Spectator::SameRoom(const Input& in, const Mate& m) const {
    if (in.room && m.room && *in.room != *m.room) {
        return false;
    }
    if (in.dungeon && in.self && std::fabs(m.position[1] - (*in.self)[1]) > s_.floor_gap) {
        return false;  // another floor
    }
    return true;
}

Spectator::Choice Spectator::Choose(const Input& in, Output& out) {
    // The mates standing; when none is, all of them (a group about to fall).
    std::vector<const Mate*> pool;
    for (const auto& m : in.mates) {
        if (!m.down) {
            pool.push_back(&m);
        }
    }
    const bool none_up = pool.empty();
    if (none_up) {
        for (const auto& m : in.mates) {
            pool.push_back(&m);
        }
    }
    // C2: those of them in our room; when none is, the whole scene's.
    std::vector<const Mate*> ring;
    for (const Mate* m : pool) {
        if (SameRoom(in, *m)) {
            ring.push_back(m);
        }
    }
    if (ring.empty()) {
        ring = pool;
    }
    std::sort(ring.begin(), ring.end(), [](const Mate* a, const Mate* b) { return a->number < b->number; });
    const auto at = [&](std::uint8_t n) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i]->number == n) {
                return i;
            }
        }
        return std::nullopt;
    };

    const std::uint64_t buttons = in.buttons.value_or(0);
    const std::uint64_t pressed = buttons & ~buttons_;
    buttons_ = buttons;
    // E4 "E2": the one watched (picked, else the one followed) goes on being
    // followed when he leaves our room for another of our scene, standing
    // (or with nobody standing, as the pool), and for a while when he leaves
    // for another scene (the view holds there; the screen says where he is).
    const std::uint8_t watched = picked_ != 0 ? picked_ : following_;
    const Mate* here = nullptr;
    for (const auto& m : in.mates) {
        if (watched != 0 && m.number == watched) {
            here = &m;
        }
    }
    const Away* there = nullptr;
    for (const auto& a : in.away) {
        if (watched != 0 && here == nullptr && a.number == watched && !a.scene.empty()) {
            there = &a;
        }
    }
    bool keep_room = here != nullptr && !at(watched) && (!here->down || none_up);
    bool keep_away = false;
    if (there != nullptr && !there->down) {
        if (away_of_ != watched || there->scene != away_scene_) {
            away_of_ = watched;
            away_since_ = in.now_us;
            out.log.push_back("spectator: " + P(watched) + " went to " + there->scene +
                              ": the camera cannot go there, the screen says where he is");
        }
        keep_away = ring.empty() || in.now_us - away_since_ < s_.away_hold_us;
        if (!keep_away) {
            out.log.push_back("spectator: " + P(watched) + " stays in " + there->scene + Fmt(" (%.0f s)", s_.away_hold_us / 1e6) +
                              "; back to the nearest of our scene");
            away_of_ = 0;
        }
    } else {
        away_of_ = 0;
    }
    if (picked_ != 0 && !at(picked_) && !keep_room && !keep_away) {
        out.log.push_back("spectator: " + P(picked_) +
                          " is no longer there to follow (down, gone, or not in our room); back to the nearest");
        picked_ = 0;
    }
    if (in.asked != 0) {  // ORM /espectar <nombre> (oot-remake docs/COMANDOS.md)
        if (at(in.asked)) {
            picked_ = in.asked;
            out.log.push_back("spectator: /espectar: " + P(picked_));
        } else {
            out.log.push_back("spectator: /espectar: " + P(in.asked) + " is not there to follow");
        }
    }
    if (!ring.empty() && (pressed & (kDLeft | kDRight)) != 0) {
        keep_room = false;
        keep_away = false;
        away_of_ = 0;
        std::size_t i = at(following_).value_or(0);
        const bool right = (pressed & kDRight) != 0;
        if (!at(following_)) {
            i = right ? ring.size() - 1 : 0;  // from outside the ring: right lands on the first, left on the last
        }
        i = right ? (i + 1) % ring.size() : (i + ring.size() - 1) % ring.size();
        picked_ = ring[i]->number;
        out.log.push_back(std::string("spectator: d-pad ") + (right ? "right" : "left") + ": " + P(picked_) + " (" +
                          std::to_string(i + 1) + " of " + std::to_string(ring.size()) + ")");
    }
    if (keep_away) {
        return {watched, 0, static_cast<std::uint8_t>(ring.size()), false, false, there->scene};
    }
    if (keep_room) {
        return {watched, 0, static_cast<std::uint8_t>(ring.size()), true, here->down, {}};
    }
    if (ring.empty()) {
        // Nobody in our scene: name one standing elsewhere (the lowest number), if any.
        const Away* first = nullptr;
        for (const auto& a : in.away) {
            if (!a.down && !a.scene.empty() && (first == nullptr || a.number < first->number)) {
                first = &a;
            }
        }
        if (first != nullptr) {
            if (away_of_ != first->number || first->scene != away_scene_) {
                away_of_ = first->number;
                away_since_ = in.now_us;
                out.log.push_back("spectator: nobody in our scene; " + P(first->number) + " is in " + first->scene);
            }
            return {first->number, 0, 0, false, false, first->scene};
        }
    }
    std::optional<std::size_t> chosen;
    if (picked_ != 0) {
        chosen = at(picked_);
    } else if (!ring.empty() && in.self) {
        std::vector<Vec3> places;
        for (const Mate* m : ring) {
            places.push_back(m->position);
        }
        chosen = aim::Nearest(*in.self, places);
    }
    if (!chosen) {
        return {};
    }
    const Mate& m = *ring[*chosen];
    return {m.number, static_cast<std::uint8_t>(*chosen + 1), static_cast<std::uint8_t>(ring.size()),
            !SameRoom(in, m), none_up};
}

bool Spectator::AwakeNow(const Input& in) const {
    return in.yaw && in.view_heading && Awake(*in.yaw, *in.view_heading, s_.rule, s_.awake_tolerance);
}

// Standing: a camera found asleep after a load gets the right-stick nudge
// once the player has had a few seconds to move it himself. The stick wakes
// it and the game writes the yaw word from the view (C0: -140 -> 32.9), so a
// woken camera's word has changed since the nudge began and agrees with the
// view.
void Spectator::WakeAfterLoad(const Input& in, Output& out) {
    if (s_.wake != Wake::stick && s_.wake != Wake::both) {
        return;
    }
    if (in.camera == 0) {
        return;
    }
    if (in.camera != pre_camera_) {
        pre_camera_ = in.camera;
        pre_ = Prewake::waiting;
        pre_at_ = in.now_us;
        pre_tries_ = 0;
    }
    if (!in.standing && pre_ == Prewake::restore) {
        pre_ = Prewake::awake;  // awake already; a fall stops the turn back
        return;
    }
    if (!in.standing && (pre_ == Prewake::right || pre_ == Prewake::left || pre_ == Prewake::check)) {
        pre_ = Prewake::waiting;  // a fall, a cutscene's game over: try again later
        pre_at_ = in.now_us;
        out.log.push_back("spectator: the wake nudge stopped (Link is not standing)");
        return;
    }
    const std::uint64_t since = in.now_us - pre_at_;
    switch (pre_) {
    case Prewake::waiting:
        if (AwakeNow(in)) {
            pre_ = Prewake::awake;
            out.log.push_back(Fmt("spectator: the camera is awake (yaw word %.1f, view heading %.1f)", *in.yaw,
                                  *in.view_heading));
        } else if (!in.standing || !in.yaw || !in.view_heading) {
            pre_at_ = in.now_us;  // count from when he stands and the view is known
        } else if (since >= (pre_tries_ == 0 ? s_.nudge_after_us : s_.nudge_retry_us)) {
            if (pre_tries_ >= s_.nudge_tries) {
                pre_ = Prewake::gave_up;
                out.log.push_back("spectator: the camera did not wake after " + std::to_string(pre_tries_) +
                                  " nudges; giving up until the next load");
                break;
            }
            ++pre_tries_;
            pre_ = Prewake::right;
            pre_at_ = in.now_us;
            yaw_mark_ = in.yaw;
            view_mark_ = in.view_heading;
            out.nudge = s_.nudge_x;
            out.log.push_back(Fmt("spectator: the camera sleeps (yaw word %.1f, view heading %.1f): right-stick "
                                  "nudge, right then left, %.2f s each (try %.0f)",
                                  *in.yaw, *in.view_heading, s_.nudge_us / 1e6, pre_tries_));
        }
        break;
    case Prewake::right:
        if (since < s_.nudge_us) {
            out.nudge = s_.nudge_x;
        } else {
            pre_ = Prewake::left;
            pre_at_ = in.now_us;
            out.nudge = -s_.nudge_x;
        }
        break;
    case Prewake::left:
        if (since < s_.nudge_us) {
            out.nudge = -s_.nudge_x;
        } else {
            pre_ = Prewake::check;
            pre_at_ = in.now_us;
        }
        break;
    case Prewake::check: {
        const bool changed = in.yaw && yaw_mark_ && std::fabs(aim::Wrap(*in.yaw - *yaw_mark_)) > 0.5f;
        if (AwakeNow(in) && changed) {
            pre_ = Prewake::restore;
            pre_at_ = in.now_us;
            restore_wrote_.reset();
            out.log.push_back(Fmt("spectator: the camera woke (the right-stick nudge): yaw word %.1f -> %.1f, view "
                                  "heading %.1f (was %.1f)",
                                  *yaw_mark_, *in.yaw, *in.view_heading, view_mark_.value_or(NAN)));
        } else if (since >= s_.nudge_check_us) {
            pre_ = Prewake::waiting;
            pre_at_ = in.now_us;
            out.log.push_back("spectator: the nudge did not wake the camera");
        }
        break;
    }
    case Prewake::restore: {
        // C2: the pair leaves the view a few degrees off (C1: 4-6); the
        // camera is awake now and takes the yaw word, so it is turned back.
        if (!view_mark_ || !in.yaw) {
            pre_ = Prewake::awake;
            break;
        }
        const float target = aim::YawFor(*view_mark_, s_.rule);
        if (restore_wrote_ && std::fabs(aim::Wrap(*in.yaw - *restore_wrote_)) > s_.restore_taken) {
            pre_ = Prewake::awake;
            out.log.push_back(Fmt("spectator: the player turns the camera (yaw word %.1f, written %.1f): the view "
                                  "is not turned back",
                                  *in.yaw, *restore_wrote_));
            break;
        }
        const float off = aim::Wrap(*in.yaw - target);
        if (std::fabs(off) <= s_.restore_done || since >= s_.restore_us) {
            pre_ = Prewake::awake;
            out.log.push_back(Fmt("spectator: the view is back where it was before the nudge (view heading %.1f, "
                                  "%.1f off, %.2f s)",
                                  *view_mark_, off, since / 1e6));
            break;
        }
        out.yaw = StepAngle(*in.yaw, target, s_.restore_rate * static_cast<float>(dt_us_) / 1e6f);
        restore_wrote_ = out.yaw;
        break;
    }
    case Prewake::awake:
    case Prewake::gave_up:
        break;
    }
}

void Spectator::Start(const Input& in, Output& out) {
    camera_ = in.camera;
    phase_at_ = in.now_us;
    probe_from_.reset();
    const bool awake = AwakeNow(in);
    const bool bits = s_.wake == Wake::bits || s_.wake == Wake::both;
    if (awake || !bits) {
        phase_ = Phase::following;
        out.log.push_back(awake ? Fmt("spectator: on, the camera is awake (yaw word %.1f, view heading %.1f)",
                                      *in.yaw, *in.view_heading)
                                : std::string("spectator: on, the camera sleeps (no stick since the load): the "
                                              "yaw is written but the view may not follow"));
        return;
    }
    phase_ = Phase::bits;
    out.log.push_back(std::string("spectator: on, the camera sleeps (yaw word ") +
                      (in.yaw ? Fmt("%.1f", *in.yaw) : "?") + ", view heading " +
                      (in.view_heading ? Fmt("%.1f", *in.view_heading) : "?") + "): the +0x18 bits");
}

Output Spectator::Tick(const Input& in) {
    Output out;
    const std::uint64_t dt = last_us_ != 0 && in.now_us > last_us_ ? std::min(in.now_us - last_us_, kMaxStepUs) : 0;
    last_us_ = in.now_us;
    dt_us_ = dt;
    if (!in.down || in.camera == 0) {
        if (phase_ != Phase::off) {
            out.log.push_back(!in.down ? std::string("spectator: released (no longer downed); the camera keeps yaw ") +
                                             (in.yaw ? Fmt("%.1f", *in.yaw) : "?")
                                       : "spectator: off (the camera object is gone)");
        }
        phase_ = Phase::off;
        following_ = 0;
        elsewhere_ = false;
        away_scene_.clear();
        away_of_ = 0;
        away_since_ = 0;
        picked_ = 0;
        buttons_ = in.buttons.value_or(0);
        no_mate_logged_ = false;
        WakeAfterLoad(in, out);
        return out;
    }
    if (pre_ == Prewake::right || pre_ == Prewake::left || pre_ == Prewake::check) {
        pre_ = Prewake::waiting;  // down mid-nudge: let go (a downed Link's camera does not wake by the stick)
        pre_at_ = in.now_us;
    } else if (pre_ == Prewake::restore) {
        pre_ = Prewake::awake;  // down while turning back: awake, and the mate's turn takes over
    }
    if (phase_ == Phase::off || in.camera != camera_) {
        Start(in, out);
    }

    const Choice choice = Choose(in, out);
    const std::uint8_t target = choice.number;
    if (target != 0 &&
        (target != following_ || choice.elsewhere != elsewhere_ || choice.away_scene != away_scene_)) {
        const bool kept = choice.ring_at == 0;  // E4 "E2": kept outside the ring
        out.log.push_back("spectator: following " + P(target) + (picked_ != 0 ? " (picked)" : " (nearest)") +
                          (!choice.away_scene.empty() ? ", in " + choice.away_scene + " (the view holds)"
                           : choice.elsewhere && kept
                               ? std::string(", in another room (the camera goes with him)")
                           : choice.elsewhere ? std::string(", in another room (nobody standing in ours)")
                                              : std::string()) +
                          (choice.down ? ", down (nobody standing)" : ""));
        no_mate_logged_ = false;
        logged_ = false;
    } else if (target == 0 && !no_mate_logged_) {
        out.log.push_back("spectator: no mate in the scene to follow");
        no_mate_logged_ = true;
    }
    following_ = target;
    elsewhere_ = choice.elsewhere;
    away_scene_ = choice.away_scene;
    out.following = following_;
    out.ring_at = choice.ring_at;
    out.ring_size = choice.ring_size;
    out.elsewhere = choice.elsewhere;
    out.followed_down = choice.down;
    out.away_scene = choice.away_scene;
    const Mate* mate = nullptr;
    for (const auto& m : in.mates) {
        if (m.number == following_) {
            mate = &m;
        }
    }
    const auto heading = mate && in.self ? aim::HeadingTo(*in.self, mate->position) : std::nullopt;

    if (phase_ == Phase::bits) {
        out.wake_bits = true;
        // A small turn toward the mate: a woken camera takes it at once.
        if (in.view_heading && heading) {
            const float side = aim::Wrap(*heading - *in.view_heading) >= 0 ? 1.0f : -1.0f;
            if (!probe_from_) {
                probe_from_ = *in.view_heading;
            }
            const float probe = aim::Wrap(*probe_from_ + side * kProbeDegrees);
            out.yaw = aim::YawFor(probe, s_.rule);
            if (std::fabs(aim::Wrap(*in.view_heading - probe)) <= kProbeArrived) {
                phase_ = Phase::following;
                out.log.push_back(Fmt("spectator: the camera woke (the +0x18 bits) after %.2f s",
                                      (in.now_us - phase_at_) / 1e6));
            }
        }
        if (phase_ == Phase::bits && in.now_us - phase_at_ >= s_.bits_wait_us) {
            phase_ = Phase::following;
            out.log.push_back("spectator: the bits did not wake the camera; writing the yaw anyway");
        }
        if (phase_ == Phase::bits || out.yaw) {
            return out;
        }
    }

    if (phase_ != Phase::following || !heading) {
        return out;
    }
    const float seconds = static_cast<float>(dt) / 1e6f;
    const float want_yaw = aim::YawFor(*heading, s_.rule);
    const float from_yaw = in.yaw.value_or(want_yaw);
    out.yaw = StepAngle(from_yaw, want_yaw, s_.yaw_rate * seconds);
    const float want_pitch = PitchFor(*in.self, mate->position, s_.rest_pitch, s_.pitch_limit);
    if (in.pitch && std::isfinite(*in.pitch)) {
        out.pitch = StepLinear(*in.pitch, want_pitch, s_.pitch_rate * seconds);
    }
    if (!logged_ || in.now_us - logged_at_ >= s_.log_every_us) {
        logged_ = true;
        logged_at_ = in.now_us;
        out.log.push_back("spectator: following " + P(following_) +
                          Fmt(": yaw %.1f -> %.1f (target %.1f), ", from_yaw, *out.yaw, want_yaw) +
                          (out.pitch ? Fmt("pitch %.1f -> %.1f (target %.1f)", *in.pitch, *out.pitch, want_pitch)
                                     : std::string("pitch ?")) +
                          (in.view_heading ? Fmt(", view heading %.1f", *in.view_heading) : ""));
    }
    return out;
}

void CameraLease::Take(std::uint64_t cam, std::uint64_t actor, std::string_view scene, std::uint64_t now_us) {
    if (cam == 0 || scene.empty()) {
        Drop(cam == 0 ? "no camera" : "the scene is not known");
        return;
    }
    cam_ = cam;
    actor_ = actor;
    scene_ = std::string(scene);
    drawn_at_ = now_us;
    why_.clear();
}

void CameraLease::Drop(std::string why) {
    if (cam_ != 0) {
        why_ = std::move(why);
    }
    cam_ = 0;
    actor_ = 0;
    scene_.clear();
}

bool CameraLease::Same(CoopPuppet::Guest& guest) const {
    return cam_ != 0 && aim::LooksLikeCamera(guest, cam_) && aim::ActorOfCamera(guest, cam_) == actor_;
}

std::uint64_t CameraLease::Check(CoopPuppet::Guest& guest, const std::optional<std::string>& scene, bool drawing,
                                 std::uint64_t now_us) {
    if (cam_ == 0) {
        return 0;
    }
    if (!scene) {
        Drop("the scene is not known (a load)");
        return 0;
    }
    if (*scene != scene_) {
        Drop("the scene changed (" + scene_ + " -> " + *scene + ")");
        return 0;
    }
    if (drawing) {
        drawn_at_ = now_us;
    } else if (now_us > drawn_at_ && now_us - drawn_at_ >= blind_us_) {
        Drop(Fmt("no scene drawn for %.1f s (a load)", (now_us - drawn_at_) / 1e6));
        return 0;
    }
    if (!Same(guest)) {
        Drop("the object is no longer the camera of its actor");
        return 0;
    }
    return cam_;
}

bool CameraLease::Write(CoopPuppet::Guest& guest, std::uint64_t offset, const void* in, std::size_t size) {
    if (!Same(guest)) {
        Drop("the object is no longer the camera of its actor");
        return false;
    }
    return guest.Write(cam_ + offset, in, size);
}

} // namespace CoopSpectator
