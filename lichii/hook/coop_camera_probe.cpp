// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM spectator camera, the emulator side (coop_camera_probe.h). Glue only:
// the walk, the search and the angles are coop_camera_aim.cpp.

#include "coop_camera_probe.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>

#include <fmt/format.h>

#include "coop/host/player_reader.hpp"
#include "coop_camera.h"
#include "coop_camera_aim.h"
#include "coop_check_visual.h"
#include "coop_downed_game.h"
#include "coop_every.h"
#include "coop_fairy.h"
#include "coop_map.h"
#include "coop_spectator.h"
#include "coop_switch.h"
#include "orm_quick_chat.h"
#include "platform.h"

namespace CoopCameraProbe {
namespace {

namespace aim = CoopCameraAim;

// The actor the player camera works for (Pack/Actor/PlayerCamera.pack.zs).
constexpr std::string_view kCameraActor = "PlayerCamera";
// The pointer walk from that actor: bytes looked at in each object, pointers
// deep, objects at most.
constexpr std::uint32_t kWalkSpan = 0x800;
constexpr int kWalkDepth = 4;
constexpr std::size_t kWalkBudget = 40000;
constexpr int kWalkTries = 5;
// The probe also logs which 32-bit words of the camera's first bytes changed
// since its last line (to tell a camera the stick has woken from one it has
// not: after a load the yaw word is not taken until the right stick moves).
constexpr std::size_t kDiffBytes = 0x400;
constexpr std::size_t kDiffMax = 32;
constexpr std::uint64_t kWalkEveryUs = 3 * CoopEvery::kSecondUs;
// The way C0 found (Kokiri, 2026-11-11), tried first: PlayerCamera actor
// +0xb8 -> +0x560 -> +0x730 is the camera.
constexpr std::array<std::uint32_t, 3> kKnownPath{0xb8, 0x560, 0x730};
// The search for the pitch limits, when the walk finds nothing: this much on
// each side of the actor, this many hits logged.
constexpr std::uint64_t kSearchSide = 512ull << 20;
constexpr std::size_t kSearchMax = 24;
// The yaw rule (coop_camera_aim.h YawRule) measured in C0 (Kokiri, rstick
// right and left, downed): yaw - matrix heading = 180 once the camera has
// moved (+-7 while it turns, one frame of the matrix's lag); the yaw falls as
// the view turns right. Overridable with LICHII_COOP_CAMERA_YAW_RULE=sign,offset.
constexpr aim::YawRule kYawRule{1.0f, 180.0f};
// A mate whose presence fades below this is leaving (gone): not followed.
constexpr float kPresentFade = 0.5f;

// The renderer drew a scene a moment ago (its matrix is fresh).
bool Fresh(const CoopCamera::Frame& frame) {
    return std::chrono::steady_clock::now() - frame.at < std::chrono::milliseconds(250);
}
bool Drawing() {
    const auto frame = CoopCamera::Latest();
    return frame && Fresh(*frame);
}

bool EnvOn(const char* name) {
    return CoopSwitch::On(name, false);  // coop_switch.h
}

template <typename T>
std::optional<T> Get(CoopPuppet::Guest& guest, std::uint64_t at) {
    T v{};
    if (at == 0 || !guest.Read(at, &v, sizeof(T))) {
        return std::nullopt;
    }
    return v;
}

std::string Hex(std::optional<std::uint64_t> v) {
    return v ? fmt::format("{:#x}", *v) : std::string("?");
}

// The names of the actors on the list that contain "Camera", for a log line
// when the PlayerCamera actor is not found by that name.
std::string CameraNames(CoopPuppet::Guest& guest, std::uint64_t main_base) {
    namespace a = CoopFairy::addr;
    const auto manager = Get<std::uint64_t>(guest, main_base + a::kManagerPtr);
    const auto lists = manager ? Get<std::uint64_t>(guest, *manager + a::kLists) : std::nullopt;
    if (!lists || *lists == 0) {
        return "(no actor list)";
    }
    const std::uint64_t head = *lists + a::kListHead;
    auto node = Get<std::uint64_t>(guest, head);
    std::string out;
    for (std::size_t walked = 0; node && *node != 0 && *node != head && walked < a::kMaxWalk; ++walked) {
        const auto owner = Get<std::uint64_t>(guest, *node + a::kNodeOwner);
        if (owner && *owner != 0 && *owner + a::kActorNode == *node) {
            const auto text = Get<std::uint64_t>(guest, *owner + a::kActorName);
            std::array<char, 64> buf{};
            if (text && *text != 0 && guest.Read(*text, buf.data(), buf.size() - 1)) {
                const std::string_view name(buf.data());
                if (name.find("Camera") != std::string_view::npos && out.size() < 600) {
                    out += fmt::format("{}{} {:#x}", out.empty() ? "" : ", ", name, *owner);
                }
            }
        }
        node = Get<std::uint64_t>(guest, *node);
    }
    return out.empty() ? "(none)" : out;
}

struct Glue {
    bool probe = EnvOn("LICHII_COOP_DEBUG_CAMERA2");
    bool spectator_on = EnvOn("LICHII_COOP_SPECTATOR");
    aim::YawRule rule = kYawRule;
    CoopEvery::Every resolve_every{CoopEvery::kSecondUs};
    CoopEvery::Every log_every{CoopEvery::kSecondUs / 2};
    std::uint64_t actor = 0;  // PlayerCamera
    std::uint64_t cam = 0;
    aim::Path path{{kKnownPath.begin(), kKnownPath.end()}};
    int walks = 0;           // for this actor
    bool searched = false;   // for this actor
    std::uint64_t walk_at = 0;
    bool names_logged = false;
    std::vector<std::uint32_t> words_seen;  // the camera's first kDiffBytes, as last logged
    CoopSpectator::Spectator spectator;
    // C2 (REVISION_CAIDO.md): what the spectator reads and writes goes
    // through this, checked each frame; `cam` is let go with it, so Resolve
    // looks again.
    CoopSpectator::CameraLease lease;
    bool nudging = false;  // the last Nudge held the stick
    std::string resolved_scene;  // the scene `cam` was found in
    std::mutex banner_mutex;
    std::string banner;  // CoopSpectator::FollowLine, for the downed line
    std::atomic<std::uint8_t> following{0};
    std::atomic<std::uint8_t> asked{0};  // ORM /espectar: taken by the next frame

    Glue() {
        // Values as coop_switch.h reads them (unset or empty: the default); a
        // value they do not take keeps the default and logs one line.
        if (const auto r = CoopSwitch::Value("LICHII_COOP_CAMERA_YAW_RULE")) {
            float s = 0, o = 0;
            if (std::sscanf(r->c_str(), "%f,%f", &s, &o) == 2 && (s == 1.0f || s == -1.0f) && std::isfinite(o)) {
                rule = {s, o};
            } else {
                CoopSwitch::Log(CoopSwitch::Unknown("LICHII_COOP_CAMERA_YAW_RULE", *r, "<1 or -1>,<offset>",
                                                    fmt::format("{:+.0f},{:+.1f}", rule.sign, rule.offset)));
            }
        }
        CoopSpectator::Settings settings;
        settings.rule = rule;
        if (const auto w = CoopSwitch::Value("LICHII_COOP_SPECTATOR_WAKE")) {
            if (const auto wake = CoopSpectator::WakeFrom(*w)) {
                settings.wake = *wake;
            } else {
                CoopSwitch::Log(CoopSwitch::Unknown("LICHII_COOP_SPECTATOR_WAKE", *w, "none/bits/stick/both",
                                                    CoopSpectator::ToString(settings.wake)));
            }
        }
        spectator = CoopSpectator::Spectator(settings);
        if (probe || spectator_on) {
            ORM_LOG_INFO("coop: camera2: {}{} (yaw = {:+.0f} * heading {:+.1f})",
                         probe ? "probe on (LICHII_COOP_DEBUG_CAMERA2)" : "",
                         spectator_on ? fmt::format(" spectator on (LICHII_COOP_SPECTATOR): a downed Link's view turns "
                                                    "to a mate, wake {}",
                                                    CoopSpectator::ToString(settings.wake))
                                      : std::string(),
                         rule.sign, rule.offset);
        }
    }

    bool Valid(CoopPuppet::Guest& guest, std::uint64_t c) const {
        return c != 0 && aim::LooksLikeCamera(guest, c) && aim::ActorOfCamera(guest, c) == actor;
    }

    void Resolve(CoopPuppet::Guest& guest, std::uint64_t main_base, const std::optional<std::string>& scene,
                 bool drawing) {
        // C2 (REVISION_CAIDO.md): a camera is only taken in a known scene
        // that is being drawn, and looked for afresh in each new scene. One
        // found during a load could be the old scene's, about to be freed,
        // and a freed one still passes Valid (its bytes stay until reused).
        if (!scene) {
            cam = 0;
            return;
        }
        if (*scene != resolved_scene) {
            cam = 0;
            resolved_scene = *scene;
        }
        if (cam == 0 && !drawing) {
            return;
        }
        const std::uint64_t found = CoopFairy::FindActor(guest, main_base, kCameraActor);
        if (found != actor) {
            ORM_LOG_INFO("coop: camera2: actor {} {:#x} (was {:#x})", kCameraActor, found, actor);
            actor = found;
            walks = 0;
            searched = false;
            if (found != 0) {
                LogComponent(guest);
            }
        }
        if (actor == 0) {
            if (!names_logged) {
                names_logged = true;
                ORM_LOG_INFO("coop: camera2: no actor named {}; actors with 'Camera': {}", kCameraActor,
                             CameraNames(guest, main_base));
            }
            cam = 0;
            return;
        }
        if (Valid(guest, cam)) {
            return;
        }
        {
            const std::uint64_t c = aim::Follow(guest, actor, path);
            if (Valid(guest, c)) {
                if (c != cam) {
                    ORM_LOG_INFO("coop: camera2: camera {:#x} by the known way {}", c, path.Text());
                }
                // ORM riesgos-visual: the way C0 found in Kokiri, checked in each scene.
                CoopVCheck::Ok("spectator camera", "by the known way " + path.Text() + " in " + resolved_scene +
                                                       " (its limits and actor checked)");
                cam = c;
                return;
            }
        }
        cam = 0;
        // The camera object comes a moment after its actor: the walk is tried
        // again every few seconds (it is long), the search once after that.
        const std::uint64_t now = CoopEvery::NowUs();
        if (walks < kWalkTries && now - walk_at >= kWalkEveryUs) {
            ++walks;
            walk_at = now;
            const auto f = aim::Walk(guest, actor, kWalkSpan, kWalkDepth, kWalkBudget, true);
            if (f) {
                ORM_LOG_INFO("coop: camera2: camera {:#x} from actor {:#x} by {} ([[cam+8]+8]+0x1f0 = the actor)",
                             f->cam, actor, f->path.Text());
                cam = f->cam;
                path = f->path;
                CoopVCheck::Fallback("spectator camera", "the known way did not reach it in " + resolved_scene +
                                                             ": found by the pointer walk " + path.Text() +
                                                             ", used from now");
                return;
            }
            if (walks >= kWalkTries) {
                CoopVCheck::Fallback("spectator camera", "no camera of " + std::string(kCameraActor) + " in " +
                                                             resolved_scene + " after " + std::to_string(kWalkTries) +
                                                             " walks: a downed Link's view stays the game's");
            }
            const auto g = aim::Walk(guest, actor, kWalkSpan, kWalkDepth, kWalkBudget, false);
            ORM_LOG_INFO("coop: camera2: walk from actor {:#x}: no camera of this actor{}", actor,
                         g ? fmt::format("; a camera-like object {:#x} by {} works for {}", g->cam, g->path.Text(),
                                         Hex(aim::ActorOfCamera(guest, g->cam)))
                           : std::string("; nothing camera-like"));
        }
        if (!searched && probe && walks >= kWalkTries) {
            searched = true;
            const std::uint64_t from = actor > kSearchSide ? actor - kSearchSide : 0;
            const auto hits = aim::SearchLimits(guest, from, 2 * kSearchSide, kSearchMax);
            ORM_LOG_INFO("coop: camera2: search {:#x}..{:#x} for the pitch limits {{-85, 89}}: {} hit(s)", from,
                         from + 2 * kSearchSide, hits.size());
            for (const auto h : hits) {
                const auto yaw = Get<float>(guest, h + aim::kYaw);
                const auto pitch = Get<float>(guest, h + aim::kPitch);
                const auto state = Get<std::uint32_t>(guest, h + aim::kState);
                const std::uint64_t owner = aim::ActorOfCamera(guest, h);
                ORM_LOG_INFO("coop: camera2:   candidate {:#x}: yaw {:.1f} pitch {:.1f} state {} works for {}{}", h,
                             yaw.value_or(NAN), pitch.value_or(NAN), state ? fmt::format("{}", *state) : "?",
                             Hex(owner), owner == actor ? " (the PlayerCamera actor)" : "");
                if (cam == 0 && owner == actor && aim::LooksLikeCamera(guest, h)) {
                    cam = h;
                }
            }
            if (cam != 0) {
                ORM_LOG_INFO("coop: camera2: camera {:#x} by the search (no pointer way found)", cam);
            }
        }
    }

    // Question 7 of the doc: [[actor+0x270]+0x360], its +0x10, that +0x150 and +0x164.
    void LogComponent(CoopPuppet::Guest& guest) {
        const auto table = Get<std::uint64_t>(guest, actor + 0x270);
        const auto comp = table ? Get<std::uint64_t>(guest, *table + 0x360) : std::nullopt;
        const auto link = comp ? Get<std::uint64_t>(guest, *comp + 0x10) : std::nullopt;
        const auto obj = link && *link ? Get<std::uint64_t>(guest, *link + 0x150) : std::nullopt;
        const auto count = link && *link ? Get<std::uint32_t>(guest, *link + 0x164) : std::nullopt;
        ORM_LOG_INFO("coop: camera2: actor table {} component +0x360 {} its +0x10 {} that +0x150 {} +0x164 {}",
                     Hex(table), Hex(comp), Hex(link), Hex(obj), count ? fmt::format("{}", *count) : "?");
    }

    void Log(CoopPuppet::Guest& guest, CoopPlatform::GuestMemory& memory, const std::optional<coop::protocol::Vec3>& position,
             const std::optional<std::string>& scene, const std::vector<coop::overlay::Shown>& others) {
        const auto camera = CoopCamera::Latest();
        const bool fresh = camera && std::chrono::steady_clock::now() - camera->at < std::chrono::milliseconds(250);
        const auto view = fresh ? aim::ViewOf(camera->clip_from_world) : std::nullopt;
        std::string words = "camera not found";
        if (cam != 0) {
            const auto yaw = Get<float>(guest, cam + aim::kYaw);
            const auto pitch = Get<float>(guest, cam + aim::kPitch);
            const auto state = Get<std::uint32_t>(guest, cam + aim::kState);
            const auto target = Get<std::uint64_t>(guest, cam + 0x20);
            const auto player = coop::host::PlayerReader(memory).find_player_actor();
            words = fmt::format("cam {:#x} yaw {:.2f} pitch {:.2f} state {} [cam+0x20] {}{}", cam, yaw.value_or(NAN),
                                pitch.value_or(NAN), state ? fmt::format("{}", *state) : "?", Hex(target),
                                player && target && *player == *target ? " (Link)" : fmt::format(" (Link {})", Hex(player)));
            if (view && yaw) {
                words += fmt::format(" | yaw+heading {:.1f} yaw-heading {:.1f}", aim::Wrap(*yaw + view->heading),
                                     aim::Wrap(*yaw - view->heading));
            }
        }
        const std::string matrix =
            view ? fmt::format("matrix heading {:.2f} pitch {:.2f}", view->heading, view->pitch) : "no matrix";
        const std::string link = position ? fmt::format("link ({:.2f}, {:.2f}, {:.2f})", (*position)[0],
                                                        (*position)[1], (*position)[2])
                                          : "link ?";
        ORM_LOG_INFO("coop: camera2: {} | {} | {}", words, matrix, link);
        if (cam != 0) {
            std::vector<std::uint32_t> now(kDiffBytes / 4);
            if (guest.Read(cam, now.data(), kDiffBytes)) {
                if (words_seen.size() == now.size()) {
                    std::string diff;
                    std::size_t n = 0;
                    for (std::size_t i = 0; i < now.size(); ++i) {
                        if (now[i] != words_seen[i] && n++ < kDiffMax) {
                            float a = 0, b = 0;
                            std::memcpy(&a, &words_seen[i], 4);
                            std::memcpy(&b, &now[i], 4);
                            diff += fmt::format(" +{:#x} {:#x}->{:#x} ({:g}->{:g})", i * 4, words_seen[i], now[i], a, b);
                        }
                    }
                    if (n != 0) {
                        ORM_LOG_INFO("coop: camera2: words changed ({}):{}", n, diff);
                    }
                }
                words_seen = std::move(now);
            }
        }
        for (const auto& o : others) {
            if (!scene || !o.scene || *o.scene != *scene || !fresh) {
                continue;
            }
            const auto ndc = aim::Project(camera->clip_from_world, o.position);
            ORM_LOG_INFO("coop: camera2: P{} at ({:.2f}, {:.2f}, {:.2f}) {}", o.number, o.position[0],
                         o.position[1], o.position[2],
                         ndc ? fmt::format("ndc ({:.3f}, {:.3f}) w {:.2f}{}", ndc->x, ndc->y, ndc->w,
                                           std::fabs(ndc->x) < 1.0f / 3 && std::fabs(ndc->y) < 1.0f / 3
                                               ? " centre third"
                                               : "")
                             : std::string("behind the camera"));
        }
    }

    // C1: a downed Link's view turns to a mate (coop_spectator.h has the rules).
    void Spectate(CoopPlatform::Platform& platform, CoopPuppet::Guest& guest, const std::optional<coop::protocol::Vec3>& position,
                  const std::optional<std::string>& scene, const std::vector<coop::overlay::Shown>& others,
                  bool down, bool standing) {
        namespace sp = CoopSpectator;
        sp::Input in;
        in.now_us = CoopEvery::NowUs();
        in.down = down;
        in.standing = standing;
        const auto camera = CoopCamera::Latest();
        const bool drawing = camera && Fresh(*camera);
        if (drawing) {
            if (const auto view = aim::ViewOf(camera->clip_from_world)) {
                in.view_heading = view->heading;
            }
        }
        // C2: the camera only while its lease holds (same scene, a scene
        // drawn, still the camera of its actor); a dropped lease lets go of
        // `cam` too, so Resolve finds the new one.
        if (cam != 0 && cam != lease.cam() && scene && *scene == resolved_scene) {
            lease.Take(cam, actor, *scene, in.now_us);
        }
        const std::uint64_t held = lease.cam();
        const std::uint64_t live = lease.Check(guest, scene, drawing, in.now_us);
        if (const std::string why = lease.TakeWhy(); !why.empty()) {
            ORM_LOG_INFO("coop: spectator: camera {:#x} let go: {}", held, why);
        }
        if (live == 0) {
            cam = 0;
        }
        in.camera = live;
        if (live != 0) {
            in.yaw = Get<float>(guest, live + aim::kYaw);
            in.pitch = Get<float>(guest, live + aim::kPitch);
        }
        in.self = position;
        if (scene && position) {
            in.room = sp::RoomOf(*scene, *position);
            in.dungeon = sp::IsDungeon(*scene);
        }
        if (down && scene) {
            for (const auto& o : others) {
                // Present in our scene; a mate fading out of presence (gone) is absent.
                if (o.scene && *o.scene == *scene && o.fade >= kPresentFade) {
                    sp::Mate m{o.number, o.position, o.down};  // CoopDowned::MateDown, set by the HUD
                    m.room = sp::RoomOf(*scene, o.position);
                    in.mates.push_back(m);
                } else if (o.scene && !o.scene->empty() && *o.scene != *scene && o.fade >= kPresentFade) {
                    in.away.push_back({o.number, *o.scene, o.down});  // E4 "E2": the screen says where
                }
            }
        }
        // ORM: the d-pad of a quick message (L + d-pad, orm_quick_chat.h) is not also a mate
        // change; player 1's pad or the handheld one, scripted input included.
        in.buttons = OrmQuickChat::ForOthers(platform.PadButtons());
        in.asked = asked.exchange(0);  // ORM /espectar
        const sp::Output out = spectator.Tick(in);
        for (const auto& line : out.log) {
            ORM_LOG_INFO("coop: {}", line);
        }
        // Whom we look at, for the downed line and the HUD.
        std::string name;
        for (const auto& o : others) {
            if (o.number == out.following) {
                name = o.name;
            }
        }
        std::string text =
            sp::FollowLine(name, out, out.away_scene.empty() ? std::string() : CoopMap::PlaceName(out.away_scene));
        following.store(out.following);
        {
            std::lock_guard lock(banner_mutex);
            if (text != banner) {
                if (!text.empty()) {
                    ORM_LOG_INFO("coop: spectator: screen: '{}'", text);
                }
                banner = std::move(text);
            }
        }
        // Review (REVISION_NUEVAS.md): written only while a scene is drawn
        // this instant. The lease keeps the camera up to blind_us without a
        // drawn scene, and a load starts (the old camera is freed) before the
        // scene name changes; a write then would land in freed memory.
        const bool writable = live != 0 && drawing;
        if (out.wake_bits && writable) {
            if (const auto flags = Get<std::uint8_t>(guest, live + sp::kWakeFlags)) {
                const std::uint8_t want = *flags | sp::kWakeBits;
                if (want != *flags) {
                    lease.Write(guest, sp::kWakeFlags, &want, 1);
                }
            }
        }
        if (out.yaw && writable) {
            lease.Write(guest, aim::kYaw, &*out.yaw, sizeof(float));
        }
        if (out.pitch && writable) {
            lease.Write(guest, aim::kPitch, &*out.pitch, sizeof(float));
        }
        if (out.nudge || nudging) {
            platform.NudgeRightStick(out.nudge ? std::optional<CoopPlatform::Stick>(CoopPlatform::Stick{
                                                     static_cast<std::int32_t>(*out.nudge * CoopPlatform::kStickMax), 0})
                                               : std::nullopt);
            nudging = out.nudge.has_value();
        }
    }
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

} // namespace

std::string Banner() {
    Glue& g = TheGlue();
    if (!g.spectator_on) {
        return {};
    }
    std::lock_guard lock(g.banner_mutex);
    return g.banner;
}

std::uint8_t Following() {
    Glue& g = TheGlue();
    return g.spectator_on ? g.following.load() : 0;
}

void AskFollow(std::uint8_t number) {
    TheGlue().asked.store(number);
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<coop::protocol::Vec3>& position, const std::vector<coop::overlay::Shown>& others,
           bool down, bool standing) {
    Glue& g = TheGlue();
    if ((!g.probe && !g.spectator_on) || main_base == 0) {
        return;
    }
    CoopPlatform::GuestView guest(platform.Memory());
    const std::uint64_t now = CoopEvery::NowUs();
    if (g.resolve_every.Due(now)) {
        g.Resolve(guest, main_base, scene, Drawing());
    }
    if (g.spectator_on) {
        g.Spectate(platform, guest, position, scene, others, down, standing);
    }
    if (g.probe && g.log_every.Due(now)) {
        g.Log(guest, platform.Memory(), position, scene, others);  // its main_base() == main_base
    }
}

} // namespace CoopCameraProbe
