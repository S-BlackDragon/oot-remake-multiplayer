// SPDX-License-Identifier: GPL-3.0-or-later
// ORM D-R19: the rider's horse, the shared step and the race hold (coop_epona.h).
#include "coop_epona.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "coop_check_visual.h"
#include "coop_fairy.h"
#include "coop_switch.h"

namespace CoopEpona {

namespace epona = coop::host::epona;

namespace {

float Ground(const Vec3& a, const Vec3& b) {
    const float x = a[0] - b[0], z = a[2] - b[2];
    return std::sqrt(x * x + z * z);
}

float Distance(const Vec3& a, const Vec3& b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

std::string Who(std::uint8_t number) {
    return "P" + std::to_string(number);
}

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string Xyz(const Vec3& v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", v[0], v[1], v[2]);
    return buf;
}

// A player missing from the plan this long (a stall, a reconnect) loses his
// track; shorter, his horse keeps where it was (the director's own grace
// keeps the actor 2 s).
constexpr std::uint64_t kForgetMs = 2000;

} // namespace

Mat3 Upright(const Mat3& m) {
    // Columns are the local axes in the world: forward is column 2.
    float fx = m[2], fz = m[8];
    const float len = std::sqrt(fx * fx + fz * fz);
    if (!(len > 1e-4f)) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    fx /= len;
    fz /= len;
    // Right = up x forward = (fz, 0, -fx); up = (0, 1, 0).
    return {fz, 0, fx, 0, 1, 0, -fx, 0, fz};
}

Vec3 HorseSpot(const Vec3& rider, const Mat3& h, const Vec3& s) {
    Vec3 out = rider;
    for (int i = 0; i < 3; ++i) {
        out[i] -= h[i * 3 + 0] * s[0] + h[i * 3 + 1] * s[1] + h[i * 3 + 2] * s[2];
    }
    return out;
}

coop::host::SmoothedPose HorseUnder(const coop::host::SmoothedPose& rider, const Mat3& heading,
                                    const Vec3& saddle) {
    const Mat3 h = rider.rotation ? Upright(CoopPuppet::QuatToMatrix(*rider.rotation)) : Upright(heading);
    coop::host::SmoothedPose out;
    out.position = HorseSpot(rider.position, h, saddle);
    // The yaw as a quaternion about y: forward (column 2) is (sin a, 0, cos a).
    const float half = std::atan2(h[2], h[8]) * 0.5f;
    out.rotation = coop::protocol::Quat{0.0f, std::sin(half), 0.0f, std::cos(half)};
    out.how = rider.how;
    out.snapped = rider.snapped;
    out.render_us = rider.render_us;
    out.ahead_us = rider.ahead_us;
    // No animation, sender's speed or gait: the AS fallback plays the horse's own gait.
    return out;
}

void UnderRiders::Set(std::map<std::uint8_t, Mat3> riding) {
    std::lock_guard lock(mutex_);
    riding_ = std::move(riding);
}

std::optional<coop::host::SmoothedPose> UnderRiders::At(std::uint8_t number, std::uint64_t server_now_us,
                                                        const Lookup& shown, const Vec3& saddle) const {
    Mat3 heading{};
    {
        std::lock_guard lock(mutex_);
        const auto it = riding_.find(number);
        if (it == riding_.end()) {
            return std::nullopt;
        }
        heading = it->second;
    }
    if (!shown) {
        return std::nullopt;
    }
    const auto rider = shown(number);
    if (!rider) {
        return std::nullopt;
    }
    const std::uint64_t apart = rider->server_us > server_now_us ? rider->server_us - server_now_us
                                                                  : server_now_us - rider->server_us;
    if (apart > kSameFrameUs) {
        return std::nullopt;
    }
    return HorseUnder(rider->pose, heading, saddle);
}

void Rider::Speed(Track& t, const Vec3& at, std::uint64_t now_ms) const {
    if (t.last_ms == 0 || now_ms <= t.last_ms) {
        if (t.last_ms == 0) {
            t.last = at;
            t.last_ms = now_ms;
        }
        return;
    }
    const float dt = static_cast<float>(now_ms - t.last_ms) / 1000.0f;
    const float d = Ground(at, t.last);
    t.last = at;
    t.last_ms = now_ms;
    if (dt > 1.0f || d > settings_.snap) {
        t.speed = 0.0f;  // a warp or a long gap: start again
        return;
    }
    const float k = 1.0f - std::exp(-settings_.speed_rate * dt);
    t.speed += (d / dt - t.speed) * k;
}

void Rider::Step(Track& t, std::uint64_t now_ms) {
    const epona::Gait want = epona::gait_for(t.speed, t.gait, settings_.speeds);
    if (want == t.gait) {
        t.next.reset();
        return;
    }
    if (!t.next || *t.next != want) {
        t.next = want;
        t.next_since_ms = now_ms;
        return;
    }
    if (now_ms - t.next_since_ms >= settings_.gait_hold_ms) {
        t.gait = want;
        t.next.reset();
    }
}

std::string Rider::ActorFor(std::optional<std::string_view> mount) const {
    if (!settings_.actor.empty()) {
        return settings_.actor;
    }
    std::string look(epona::horse_look(mount));
    if (broken_.contains(look)) {
        return std::string(epona::kPlainLook);
    }
    return look;
}

std::optional<std::string> Rider::ActorOf(std::uint8_t number) const {
    const auto it = tracks_.find(number);
    if (it == tracks_.end() || !it->second.horse) {
        return std::nullopt;
    }
    return it->second.actor;
}

CoopPuppet::Input Rider::Follow(const CoopPuppet::Input& puppets, std::uint64_t now_ms,
                                std::uint32_t live, const std::map<std::uint8_t, std::string>* mounts) {
    CoopPuppet::Input out;
    out.main_base = puppets.main_base;
    out.scene = puppets.scene;
    // The puppets' wait for the drawing after a load (issue 93): a horse made
    // during the loading screen would be refused or never drawn too.
    out.camera = puppets.camera;
    out.drawn_since_ms = puppets.drawn_since_ms;
    out.settle_ms = puppets.settle_ms;
    // The server time, so the horses' director asks UnderRiders for the same
    // calc frame's time as the puppets' director asks the smoother.
    out.server_now_us = puppets.server_now_us;
    out.server_at_ms = puppets.server_at_ms;
    if (puppets.scene != scene_) {
        scene_ = puppets.scene;
        tracks_.clear();  // a new place: every horse of the old one is gone
    }
    std::map<std::uint8_t, bool> planned;
    for (const auto& p : puppets.targets) {
        if (p.number == 0) {
            continue;  // the test puppet rides nothing
        }
        planned[p.number] = true;
        const auto anim = p.anim ? std::optional<std::string_view>(p.anim->name) : std::nullopt;
        const epona::Ride ride = epona::ride_of(anim);
        Track& t = tracks_[p.number];
        Speed(t, p.position, now_ms);
        const epona::Ride was = t.ride;
        t.ride = ride;
        const std::string who = Who(p.number);
        switch (ride) {
        case epona::Ride::riding: {
            if (!t.horse || was != epona::Ride::riding) {
                t.gait = epona::gait_for(t.speed, epona::Gait::wait, settings_.speeds);
                t.next.reset();
                Say(who + (t.horse ? " rides again: his horse goes under him"
                                   : " rides: a horse under him"));
            }
            t.horse = true;
            t.off_ms = 0;
            t.heading = Upright(p.rotation);
            t.at = HorseSpot(p.position, t.heading, settings_.saddle);
            Step(t, now_ms);
            break;
        }
        case epona::Ride::mounting:
            // His own Epona stands by him in his game, and he climbs onto her.
            // Ours stays if it waits there (he gets on where he got off); else
            // it stands where he starts to get on (where his Epona is is not
            // sent), and goes under him once he sits.
            if (t.horse && was != epona::Ride::mounting &&
                Ground(t.at, p.position) > settings_.mount_near) {
                t.horse = false;
                Say(who + " gets on far from where his horse waited: it comes to him");
            }
            if (!t.horse) {
                t.horse = true;
                t.heading = Upright(p.rotation);
                t.at = p.position;  // on the ground beside him
                Say(who + " gets on: a horse beside him");
            }
            t.off_ms = 0;
            t.gait = epona::Gait::wait;
            t.next.reset();
            break;
        case epona::Ride::none:
            if (t.horse && was == epona::Ride::riding) {
                t.off_ms = std::max<std::uint64_t>(now_ms, 1);
                t.gait = epona::Gait::wait;
                t.next.reset();
                Say(who + " got off: his horse waits where he left it");
            }
            if (t.horse && settings_.linger_ms > 0 && t.off_ms != 0 &&
                now_ms - t.off_ms >= settings_.linger_ms) {
                t.horse = false;
                Say(who + "'s horse goes (waited " + std::to_string(settings_.linger_ms / 1000) + " s)");
            }
            break;
        }
        if (!t.horse) {
            continue;
        }
        // The look: the horse he is on, as his game says (kept while he is
        // off it, so a waiting horse keeps its coat). Not said yet: the plain
        // horse, never Epona's.
        if (ride != epona::Ride::none || t.actor.empty()) {
            std::optional<std::string_view> mount;
            if (mounts != nullptr) {
                if (const auto m = mounts->find(p.number); m != mounts->end()) {
                    mount = m->second;
                }
            }
            if ((mount && !mount->empty()) || t.actor.empty() || !settings_.actor.empty()) {
                t.actor = ActorFor(mount);
            }
        }
        if (broken_.contains(t.actor)) {
            t.actor = std::string(epona::kPlainLook);
        }
        CoopPuppet::Target h;
        h.number = p.number;
        h.actor = t.actor;
        h.position = t.at;
        h.rotation = t.heading;
        if (ride == epona::Ride::riding) {
            // In the saddle, smoothed (UnderRiders): the side step his puppet is
            // drawn with is added to the horse under him too.
            h.offset = p.offset;
        }
        // With him on it (getting on or in the saddle) the horse shows as its
        // rider does; waiting where he got off it stands, whatever he does.
        const bool with_him = ride != epona::Ride::none;
        h.show = with_him ? p.show : CoopPuppet::Show::show;
        h.why = with_him ? p.why : "";
        if (with_him && h.show != CoopPuppet::Show::hide &&
            !(p.number < 32 && (live >> p.number & 1u) != 0)) {
            // As the fairy (issue 145): never a horse under nobody while his
            // puppet is still being made, refused or gone.
            h.show = CoopPuppet::Show::hide;
            h.why = "its rider is not shown";
        }
        h.anim = CoopPuppet::Anim{std::string(epona::gait_command(t.gait)), 0.0f, 1.0f};
        h.anim_serial = 0;  // the gait loops on its own; only a new name is played
        out.targets.push_back(std::move(h));
    }
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        Track& t = it->second;
        if (planned.contains(it->first)) {
            ++it;
            continue;
        }
        // Not in the plan: another place, or out for a moment.
        if (now_ms - std::min(now_ms, t.last_ms) >= kForgetMs) {
            if (t.horse) {
                Say(Who(it->first) + " left: his horse goes");
            }
            it = tracks_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

std::map<std::uint8_t, Mat3> Rider::Riding() const {
    std::map<std::uint8_t, Mat3> out;
    for (const auto& [number, t] : tracks_) {
        if (t.horse && t.ride == epona::Ride::riding) {
            out.emplace(number, t.heading);
        }
    }
    return out;
}

std::optional<epona::Gait> Rider::GaitOf(std::uint8_t number) const {
    const auto it = tracks_.find(number);
    if (it == tracks_.end() || !it->second.horse) {
        return std::nullopt;
    }
    return it->second.gait;
}

std::set<std::string> HorseLooksIn(std::string_view manifest) {
    std::set<std::string> out;
    for (const std::string_view look : {epona::kEponaLook, epona::kRanchLook, epona::kIngoLook}) {
        // Quoted: "CoopPuppet_Horse" is not found in "CoopPuppet_Horse_Ranch".
        const std::string quoted = "\"" + std::string(look) + "\"";
        if (manifest.find(quoted) != std::string_view::npos) {
            out.insert(std::string(look));
        }
    }
    return out;
}

// ---------------------------------------------------------------- MountReader

std::optional<std::string> PickMount(const std::vector<SeenHorse>& horses, const Vec3& own, float near) {
    const SeenHorse* best = nullptr;
    float best_d = near;
    for (const auto& h : horses) {
        if (!epona::is_horse(h.name)) {
            continue;
        }
        const float d = Distance(h.at, own);
        if (d <= best_d) {
            best_d = d;
            best = &h;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return best->name;
}

namespace {

// The game's horses on the actor list (CoopFairy::FindActor's walk).
std::vector<SeenHorse> WalkHorses(CoopPuppet::Guest& guest, std::uint64_t main_base) {
    namespace a = CoopFairy::addr;
    std::vector<SeenHorse> out;
    std::uint64_t manager = 0, lists = 0, node = 0;
    if (main_base == 0 || !guest.Read(main_base + a::kManagerPtr, &manager, 8) || manager == 0 ||
        !guest.Read(manager + a::kLists, &lists, 8) || lists == 0) {
        return out;
    }
    const std::uint64_t head = lists + a::kListHead;
    if (!guest.Read(head, &node, 8)) {
        return out;
    }
    for (std::size_t walked = 0; node != 0 && node != head && walked < a::kMaxWalk; ++walked) {
        std::uint64_t owner = 0, text = 0;
        std::uint32_t state = 0;
        std::array<char, 65> name{};
        if (guest.Read(node + a::kNodeOwner, &owner, 8) && owner != 0 && owner + a::kActorNode == node &&
            guest.Read(owner + a::kActorState, &state, 4) && state == a::kStatePresent &&
            guest.Read(owner + a::kActorName, &text, 8) && text != 0 && guest.Read(text, name.data(), 64)) {
            name[64] = '\0';
            const std::string n(name.data());
            Vec3 at{};
            if (epona::is_horse(n) && guest.Read(owner + CoopPuppet::addr::kActorPosition, at.data(), sizeof(at))) {
                out.push_back({n, at, owner});
            }
        }
        if (!guest.Read(node, &node, 8)) {
            break;
        }
    }
    return out;
}

} // namespace

void MountReader::SetOwn(const std::optional<Vec3>& position, bool on_horse) {
    std::lock_guard lock(mutex_);
    own_ = position;
    on_ = on_horse;
    if (!on_horse) {
        mount_.clear();
        mount_actor_ = 0;
    }
}

void MountReader::Frame(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t now_ms) {
    std::optional<Vec3> own;
    bool on = false;
    {
        std::lock_guard lock(mutex_);
        own = own_;
        on = on_;
    }
    if (!on || !own || now_ms < next_ms_) {
        return;
    }
    next_ms_ = now_ms + kEveryMs;
    const auto horses = WalkHorses(guest, main_base);
    const auto picked = PickMount(horses, *own);
    // The actor of the picked one: the nearest of that name (ORM riesgos-visual).
    std::uint64_t actor = 0;
    float best = 1e30f;
    for (const auto& h : horses) {
        if (picked && h.name == *picked && Distance(h.at, *own) < best) {
            best = Distance(h.at, *own);
            actor = h.actor;
        }
    }
    std::lock_guard lock(mutex_);
    if (on_) {
        mount_ = picked.value_or("");
        mount_actor_ = picked ? actor : 0;
    }
}

std::uint64_t MountReader::MountActor() const {
    std::lock_guard lock(mutex_);
    return on_ ? mount_actor_ : 0;
}

std::string MountReader::Mount() const {
    std::lock_guard lock(mutex_);
    return on_ ? mount_ : std::string();
}

void MountReader::Stamp(coop::host::PlayerReading& reading) {
    const auto anim = reading.animation ? std::optional<std::string_view>(reading.animation->name) : std::nullopt;
    SetOwn(reading.position, epona::ride_of(anim) != epona::Ride::none);
    if (reading.equipment) {
        reading.equipment->mount = Mount();
    }
}

// ---------------------------------------------------------------- RideMeasure

namespace {

bool SaddleSane(const Vec3& v) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(v[i]) || v[i] < RideMeasure::kSaddleMin[i] || v[i] > RideMeasure::kSaddleMax[i]) {
            return false;
        }
    }
    return true;
}

std::string Num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

} // namespace

std::optional<int> RideMeasure::GaitIndex(std::string_view command) {
    for (int i = 0; i < 4; ++i) {
        if (command == epona::gait_command(static_cast<epona::Gait>(i + 1))) {
            return i;
        }
    }
    return std::nullopt;
}

float RideMeasure::Median(std::vector<float> v) {
    if (v.empty()) {
        return 0.0f;
    }
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

void RideMeasure::AddSaddle(const Vec3& offset, std::string_view horse) {
    std::lock_guard lock(mutex_);
    if (!SaddleSane(offset)) {
        ++rejected_;
        last_rejected_ = offset;
        return;
    }
    if (saddle_file_) {
        // A reading of this run: the file's saddle is only a start, never mixed
        // in. Its gaits stay until a gait of this run comes (ORM review: a first
        // trot dropped the file's saddle, and the next save lost it for good).
        saddles_.clear();
        saddle_file_ = false;
    }
    from_file_ = false;
    if (horse_ != horse) {
        saddles_.clear();  // another horse: its own saddle
        horse_ = std::string(horse);
    }
    saddles_.push_back(offset);
    if (saddles_.size() > kSaddleKeep) {
        saddles_.erase(saddles_.begin());
    }
}

void RideMeasure::AddGait(std::string_view command, float speed) {
    const auto i = GaitIndex(command);
    if (!i || !std::isfinite(speed) || speed < 0.0f || speed > 60.0f) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (gaits_file_) {
        for (auto& g : gaits_) {
            g.clear();  // the file's gaits only; its saddle stays (AddSaddle)
        }
        gaits_file_ = false;
    }
    from_file_ = false;
    auto& g = gaits_[static_cast<std::size_t>(*i)];
    g.push_back(speed);
    if (g.size() > kGaitKeep) {
        g.erase(g.begin());
    }
}

std::optional<Vec3> RideMeasure::SaddleLocked() const {
    if (saddles_.size() < kSaddleNeed) {
        return std::nullopt;
    }
    // The newest kSaddleNeed must agree: a horse that shifted under a still
    // rider (a slope, a turn on the spot) has not settled yet.
    for (int axis = 0; axis < 3; ++axis) {
        float lo = 1e30f, hi = -1e30f;
        for (std::size_t k = saddles_.size() - kSaddleNeed; k < saddles_.size(); ++k) {
            lo = std::min(lo, saddles_[k][axis]);
            hi = std::max(hi, saddles_[k][axis]);
        }
        if (hi - lo > kSaddleSpread) {
            return std::nullopt;
        }
    }
    Vec3 out{};
    for (int axis = 0; axis < 3; ++axis) {
        std::vector<float> v;
        v.reserve(saddles_.size());
        for (const auto& s : saddles_) {
            v.push_back(s[axis]);
        }
        out[axis] = Median(std::move(v));
    }
    return out;
}

std::optional<Vec3> RideMeasure::Saddle() const {
    std::lock_guard lock(mutex_);
    return SaddleLocked();
}

std::optional<std::array<float, 4>> RideMeasure::MediansLocked() const {
    std::array<float, 4> m{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (gaits_[i].size() < kGaitNeed) {
            return std::nullopt;
        }
        m[i] = Median(gaits_[i]);
    }
    for (std::size_t i = 1; i < 4; ++i) {
        if (!(m[i] > m[i - 1] + 0.1f)) {
            return std::nullopt;  // a faster gait no faster: not a reading to draw with
        }
    }
    return m;
}

std::optional<epona::GaitSpeeds> RideMeasure::Speeds(const epona::GaitSpeeds& estimate) const {
    std::lock_guard lock(mutex_);
    const auto m = MediansLocked();
    if (!m) {
        return std::nullopt;
    }
    epona::GaitSpeeds s = estimate;
    s.walk = std::min(estimate.walk, 0.5f * (*m)[0]);
    s.trot = 0.5f * ((*m)[0] + (*m)[1]);
    s.canter = 0.5f * ((*m)[1] + (*m)[2]);
    s.gallop = 0.5f * ((*m)[2] + (*m)[3]);
    // The margin never reaches past the middle of the narrowest gap: a speed
    // at a gait's own median must still pick that gait.
    float gap = (*m)[0] - s.walk;
    for (std::size_t i = 1; i < 4; ++i) {
        gap = std::min(gap, 0.5f * ((*m)[i] - (*m)[i - 1]));
    }
    s.margin = std::max(0.0f, std::min(estimate.margin, 0.5f * gap));
    return s;
}

std::string RideMeasure::SaddleWhy() const {
    std::lock_guard lock(mutex_);
    if (SaddleLocked()) {
        return "";
    }
    std::string why;
    if (saddles_.empty()) {
        why = "we have not sat still on our own horse yet";
    } else if (saddles_.size() < kSaddleNeed) {
        why = std::to_string(saddles_.size()) + " of " + std::to_string(kSaddleNeed) + " still readings so far";
    } else {
        why = "our last readings on our own horse disagree by more than " + Num(kSaddleSpread);
    }
    if (rejected_ > 0 && last_rejected_) {
        why += "; " + std::to_string(rejected_) + " out of range left out (last " + Xyz(*last_rejected_) + ")";
    }
    return why;
}

std::string RideMeasure::SpeedsWhy() const {
    std::lock_guard lock(mutex_);
    std::string have;
    bool all = true;
    for (int i = 0; i < 4; ++i) {
        const std::size_t n = gaits_[static_cast<std::size_t>(i)].size();
        all = all && n >= kGaitNeed;
        have += (i == 0 ? "" : ", ") + std::string(epona::gait_command(static_cast<epona::Gait>(i + 1))) + " " +
                std::to_string(n);
    }
    if (all) {
        return "the measured gaits do not get faster in order (" + have + " readings)";
    }
    return "not every gait ridden yet on our own horse (" + have + " readings of " + std::to_string(kGaitNeed) +
           " each)";
}

std::string RideMeasure::SaddleSays() const {
    std::lock_guard lock(mutex_);
    const auto s = SaddleLocked();
    if (!s) {
        return "";
    }
    return "(" + Xyz(*s) + "), the median of " + std::to_string(saddles_.size()) +
           (saddle_file_ ? " still readings of an earlier run on our own " : " still readings on our own ") +
           (horse_.empty() ? std::string("horse") : horse_);
}

std::string RideMeasure::SpeedsSays() const {
    std::lock_guard lock(mutex_);
    const auto m = MediansLocked();
    if (!m) {
        return "";
    }
    return "walk " + Num((*m)[0]) + ", trot " + Num((*m)[1]) + ", canter " + Num((*m)[2]) + ", gallop " +
           Num((*m)[3]) + " units/s on our own horse" + (gaits_file_ ? " (an earlier run)" : "");
}

std::string RideMeasure::Save() const {
    std::lock_guard lock(mutex_);
    std::string out = "# coop: our own horse as measured (docs/RIESGOS_RESUELTOS.md); delete to measure again\n";
    char buf[128];
    for (const auto& s : saddles_) {
        std::snprintf(buf, sizeof(buf), "saddle %.4f %.4f %.4f ", s[0], s[1], s[2]);
        out += buf + (horse_.empty() ? std::string("horse") : horse_) + "\n";
    }
    for (std::size_t i = 0; i < 4; ++i) {
        for (const float v : gaits_[i]) {
            std::snprintf(buf, sizeof(buf), "gait %zu %.4f\n", i, v);
            out += buf;
        }
    }
    return out;
}

std::size_t RideMeasure::Load(std::string_view text) {
    std::vector<Vec3> saddles;
    std::string horse;
    std::array<std::vector<float>, 4> gaits;
    std::size_t taken = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string line(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        Vec3 v{};
        char name[96] = {};
        unsigned index = 0;
        float speed = 0.0f;
        if (std::sscanf(line.c_str(), "saddle %f %f %f %95s", &v[0], &v[1], &v[2], name) == 4 &&
            SaddleSane(v)) {
            if (horse != name) {
                saddles.clear();
                horse = name;
            }
            saddles.push_back(v);
            if (saddles.size() > kSaddleKeep) {
                saddles.erase(saddles.begin());
            }
            ++taken;
        } else if (std::sscanf(line.c_str(), "gait %u %f", &index, &speed) == 2 && index < 4 &&
                   std::isfinite(speed) && speed >= 0.0f && speed <= 60.0f) {
            gaits[index].push_back(speed);
            if (gaits[index].size() > kGaitKeep) {
                gaits[index].erase(gaits[index].begin());
            }
            ++taken;
        }
    }
    std::lock_guard lock(mutex_);
    if (taken == 0) {
        return 0;
    }
    saddles_ = std::move(saddles);
    horse_ = std::move(horse);
    gaits_ = std::move(gaits);
    from_file_ = true;
    saddle_file_ = true;
    gaits_file_ = true;
    return taken;
}

bool RideMeasure::FromFile() const {
    std::lock_guard lock(mutex_);
    return from_file_;
}

RideNow PickRide(const RideMeasure& measure, const std::optional<Vec3>& saddle_switch, bool drawing,
                 const Settings& estimates) {
    RideNow now;
    now.saddle = estimates.saddle;
    now.speeds = estimates.speeds;
    if (saddle_switch) {
        now.saddle = *saddle_switch;
        now.saddle_measured = true;
        CoopVCheck::Ok("saddle", "(" + Xyz(*saddle_switch) + ") as LICHII_ORM_HORSE_SADDLE sets it");
    } else if (const auto s = measure.Saddle()) {
        now.saddle = *s;
        now.saddle_measured = true;
        CoopVCheck::Ok("saddle", measure.SaddleSays());
    } else if (drawing) {
        CoopVCheck::Fallback("saddle", "(" + Xyz(estimates.saddle) + "), the estimate: " + measure.SaddleWhy());
    }
    if (const auto g = measure.Speeds(estimates.speeds)) {
        now.speeds = *g;
        now.speeds_measured = true;
        CoopVCheck::Ok("horse gaits", measure.SpeedsSays() + "; lines " + Num(g->walk) + " / " + Num(g->trot) +
                                          " / " + Num(g->canter) + " / " + Num(g->gallop));
    } else if (drawing) {
        CoopVCheck::Fallback("horse gaits", "the estimates " + Num(estimates.speeds.walk) + " / " +
                                                Num(estimates.speeds.trot) + " / " +
                                                Num(estimates.speeds.canter) + " / " +
                                                Num(estimates.speeds.gallop) + ": " + measure.SpeedsWhy());
    }
    return now;
}

// ---------------------------------------------------------------- SaddleProbe

Vec3 SaddleProbe::Offset(const Vec3& horse, const Mat3& r, const Vec3& rider) {
    const Vec3 d{rider[0] - horse[0], rider[1] - horse[1], rider[2] - horse[2]};
    Vec3 out{};
    for (int j = 0; j < 3; ++j) {
        out[j] = r[0 * 3 + j] * d[0] + r[1 * 3 + j] * d[1] + r[2 * 3 + j] * d[2];
    }
    return out;
}

void SaddleProbe::SetOwn(const std::optional<Vec3>& position, bool riding, std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (!position || !own_ || Distance(*position, *own_) > kStill || (riding && !riding_)) {
        still_since_ms_ = now_ms;
    }
    // Our speed along the ground, smoothed as the Rider smooths a rider's.
    if (position && own_ && own_ms_ != 0 && now_ms > own_ms_) {
        const float dt = static_cast<float>(now_ms - own_ms_) / 1000.0f;
        const float d = Ground(*position, *own_);
        if (dt > 1.0f || d > kSnap) {
            speed_ = 0.0f;
        } else {
            speed_ += (d / dt - speed_) * (1.0f - std::exp(-kSpeedRate * dt));
        }
    } else if (!position) {
        speed_ = 0.0f;
    }
    if (!riding) {
        speed_ = 0.0f;
    }
    if (!own_ || !position || now_ms > own_ms_) {
        own_ = position;
        own_ms_ = now_ms;
    }
    riding_ = riding;
}

std::uint64_t SaddleProbe::Horse(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t hint) {
    if (hint != 0) {
        return hint;
    }
    return CoopFairy::FindActor(guest, main_base, horse_);
}

void SaddleProbe::Frame(CoopPuppet::Guest& guest, std::uint64_t main_base, std::uint64_t now_ms,
                        std::uint64_t horse_actor) {
    std::optional<Vec3> own;
    bool riding = false;
    std::uint64_t still = 0;
    float speed = 0.0f;
    {
        std::lock_guard lock(mutex_);
        own = own_;
        riding = riding_;
        still = still_since_ms_;
        speed = speed_;
    }
    if (main_base == 0 || !riding || !own) {
        command_.clear();
        return;
    }
    // The gaits: our horse's slot 0 command with our speed, once it held.
    if (measure_ != nullptr && (gait_ms_ == 0 || now_ms - std::min(now_ms, gait_ms_) >= kGaitEveryMs)) {
        gait_ms_ = std::max<std::uint64_t>(now_ms, 1);
        const std::uint64_t actor = Horse(guest, main_base, horse_actor);
        std::string command;
        if (actor != 0) {
            std::uint64_t p = actor;
            for (const std::uint64_t step : CoopPuppet::addr::kControllerPath) {
                if (p == 0 || !guest.Read(p + step, &p, sizeof(p))) {
                    p = 0;
                    break;
                }
            }
            std::int32_t slots = 0;
            std::uint64_t array = 0, slot0 = 0, text = 0;
            std::array<char, 64> name{};
            if (p != 0 && guest.Read(p + CoopPuppet::addr::kSlotCount, &slots, sizeof(slots)) && slots >= 1 &&
                guest.Read(p + CoopPuppet::addr::kSlots, &array, sizeof(array)) && array != 0 &&
                guest.Read(array, &slot0, sizeof(slot0)) && slot0 != 0 &&
                guest.Read(slot0 + CoopPuppet::addr::kSlotName, &text, sizeof(text)) && text != 0 &&
                guest.Read(text, name.data(), name.size() - 1)) {
                command.assign(name.data(), strnlen(name.data(), name.size()));
            }
        }
        if (command != command_) {
            command_ = command;
            command_since_ms_ = now_ms;
        } else if (!command.empty() && now_ms - std::min(now_ms, command_since_ms_) >= kGaitHoldMs) {
            measure_->AddGait(command, speed);
        }
    }
    if (now_ms - std::min(now_ms, still) < kStillMs) {
        return;
    }
    if (log_ms_ != 0 && now_ms - std::min(now_ms, log_ms_) < kEveryMs) {
        return;
    }
    log_ms_ = std::max<std::uint64_t>(now_ms, 1);
    const std::uint64_t actor = Horse(guest, main_base, horse_actor);
    if (actor == 0) {
        if (actor_ != 0 || search_ms_ == 0) {
            if (log_) log_("saddle: we ride, but no " + horse_ + " is on the actor list");
        }
        actor_ = 0;
        search_ms_ = log_ms_;
        return;
    }
    actor_ = actor;
    Vec3 at{};
    Mat3 rot{};
    if (!guest.Read(actor + CoopPuppet::addr::kActorPosition, at.data(), sizeof(at)) ||
        !guest.Read(actor + CoopPuppet::addr::kActorRotation, rot.data(), sizeof(rot))) {
        return;
    }
    const Vec3 off = Offset(at, rot, *own);
    std::string name = horse_;
    if (horse_actor != 0) {
        std::uint64_t text = 0;
        std::array<char, 65> buf{};
        if (guest.Read(actor + CoopFairy::addr::kActorName, &text, sizeof(text)) && text != 0 &&
            guest.Read(text, buf.data(), 64)) {
            name.assign(buf.data(), strnlen(buf.data(), 64));
        }
    }
    if (measure_ != nullptr) {
        measure_->AddSaddle(off, name);
    }
    if (logged_ && Distance(off, *logged_) < kMoved) {
        return;
    }
    logged_ = off;
    if (log_) {
        log_("saddle: our Link sits at (" + Xyz(off) + ") in the frame of our " + name + " " +
             Hex(actor) + " (horse at " + Xyz(at) + ", Link at " + Xyz(*own) +
             "); LICHII_ORM_HORSE_SADDLE=" + Xyz(off) + " draws the others' horses so");
    }
}

} // namespace CoopEpona

#include "platform_env.h"  // ORM_PLATFORM_LIVE

#if ORM_PLATFORM_LIVE

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string_view>
#include <variant>
#include <vector>

#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "platform.h"  // FolderPath: the puppet pack's mods, coop_ride.txt

namespace CoopEpona {

namespace {

// ORM test10: upstream's rider copy (lichii/hook/coop_ride.h, CoopRide::kHorse
// and Rides::kStaleUs): the horse it draws and how long a ride stays fresh.
constexpr std::string_view kUpstreamHorse = "Animal_Epona_Adult";
constexpr std::uint64_t kUpstreamRideFreshMs = 600;

std::string Env(const char* name) {
    return CoopPlatform::Env(name).value_or(std::string());
}

bool EnvOn(const char* name) {
    const auto v = Env(name);
    return !v.empty() && v != "0";
}

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

// The puppet horses the installed puppet pack has: every mod folder's
// coop_puppet.json under load/<title id> (as CoopSpawn's PackStandIns). None
// found: none of ours is asked for.
std::set<std::string> PackHorseLooks() {
    std::set<std::string> looks;
    std::error_code ec;
    const auto title = CoopPlatform::FolderPath(CoopPlatform::Folder::mods) / "0400486024DEC000";
    for (const auto& mod : std::filesystem::directory_iterator(title, ec)) {
        std::ifstream in(mod.path() / "coop_puppet.json", std::ios::binary);
        if (!in) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        looks.merge(HorseLooksIn(text));
    }
    return looks;
}

struct Glue;
void CountLine(Glue& g, const std::string& line);

struct Glue {
    void Count(const std::string& line) { CountLine(*this, line); }
    bool horses = false;  // LICHII_ORM_HORSE (with the puppets)
    bool probe_on = false;
    std::unique_ptr<CoopPuppet::Director> director;
    std::unique_ptr<CoopPuppet::Planner> planner;  // the present thread only
    std::unique_ptr<Rider> rider;                  // the present thread only
    UnderRiders under;  // the riders in the saddle: present thread sets, calc thread asks
    std::unique_ptr<SaddleProbe> probe;
    // ORM riesgos-visual: the saddle and the gaits measured on our own horse
    // (RideMeasure), the switch's saddle if set, and what the horses are drawn
    // with now (the present thread picks, the calc thread's smoothing reads).
    RideMeasure measure;
    std::optional<Vec3> saddle_switch;
    Settings estimates;
    bool drawing = false;  // the present thread: a horse under someone at the last Publish
    std::mutex saddle_mutex;
    Vec3 saddle_now{0.0f, 1.2f, 0.0f};  // guarded
    std::string saved;                  // the present thread: what coop_ride.txt holds
    std::uint64_t save_ms = 0;
    std::atomic<std::uint64_t> main_base{0};
    // The shared step and the race hold (the present thread only).
    std::atomic<bool> step_shared{false};  // set by Begin, read each frame
    std::optional<std::uint32_t> step;
    std::uint64_t step_ms = 0;
    bool racing = false;
    bool held_said = false;  // NoteHeld's line, once a race
    // The fallback to the event horse for a puppet horse the pack lacks: the
    // director's lines (calc thread) counted per actor, read on the present thread.
    std::mutex fail_mutex;
    std::map<int, std::string> requested;  // guarded: each player's last create
    std::map<std::string, int> failures;   // guarded
    std::set<std::string> made;            // guarded
    std::set<std::string> broken;          // the present thread only
    // Our own horse, for PLAYER_EQUIP (with the puppets: the cave walks the list).
    std::unique_ptr<MountReader> mount;
    // Each other player's horse, from PLAYER_EQUIP (network thread writes).
    std::mutex mounts_mutex;
    std::map<std::uint8_t, std::string> mounts;  // guarded
    // ORM test10: when each other player last sent upstream's PLAYER_RIDE on
    // Epona (NowMs), guarded by mounts_mutex. Upstream's CoopRide then draws his
    // horse (its real pose and animation); ours steps aside for him.
    std::map<std::uint8_t, std::uint64_t> upstream_rides;
    std::set<std::uint8_t> yielded;  // the present thread: riders left to upstream (for the log)

    static std::filesystem::path RideFile() {
        return CoopPlatform::FolderPath(CoopPlatform::Folder::config) / "coop_ride.txt";
    }
    // ORM riesgos-visual: what an earlier run measured on our own horse.
    void LoadRide() {
        std::ifstream in(RideFile(), std::ios::binary);
        if (!in) {
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::size_t taken = measure.Load(text);
        saved = measure.Save();
        ORM_LOG_INFO("coop: epona: {} reading(s) of our own horse from an earlier run ({})", taken,
                 RideFile().string());
    }
    // At most every 10 s, when the measure has new readings.
    void SaveRide(std::uint64_t now_ms) {
        if (save_ms != 0 && now_ms - std::min(now_ms, save_ms) < 10'000) {
            return;
        }
        save_ms = std::max<std::uint64_t>(now_ms, 1);
        std::string text = measure.Save();
        // Nothing new, what came from the file, or no reading at all (the header only).
        if (text == saved || measure.FromFile() || text.find('\n') + 1 >= text.size()) {
            return;
        }
        std::ofstream out(RideFile(), std::ios::binary | std::ios::trunc);
        if (out << text) {
            saved = std::move(text);
        }
    }

    Glue() {
        // The cave is there only with the puppets; the horses follow them.
        const bool puppets = EnvOn("LICHII_COOP_PUPPET") || EnvOn("LICHII_COOP_DEBUG_PUPPET");
        horses = HorsesOn();
        probe_on = puppets && CoopSwitch::On("LICHII_ORM_DEBUG_HORSE", "LICHII_COOP_DEBUG_HORSE", false);
        if (!puppets) {
            ORM_LOG_INFO("coop: epona: no puppets: no riders' horses, our horse not sent");
            return;
        }
        mount = std::make_unique<MountReader>();  // the others pick our horse's look by it
        if (!horses) {
            ORM_LOG_INFO("coop: epona: riders' horses off (LICHII_ORM_HORSE=0)");
        }
        if (horses) {
            Settings settings;
            if (auto actor = CoopSwitch::Value("LICHII_ORM_HORSE_ACTOR", "LICHII_COOP_HORSE_ACTOR")) {
                settings.actor = std::move(*actor);  // every horse, no fallback: the player asked for this one
            }
            if (const auto saddle = CoopSwitch::Find("LICHII_ORM_HORSE_SADDLE", "LICHII_COOP_HORSE_SADDLE")) {
                Vec3 s{};
                if (std::sscanf(saddle->value.c_str(), "%f , %f , %f", &s[0], &s[1], &s[2]) == 3 &&
                    std::isfinite(s[0]) && std::isfinite(s[1]) && std::isfinite(s[2])) {
                    settings.saddle = s;
                    saddle_switch = s;  // the player's own number wins over any measure
                } else {
                    ORM_LOG_ERROR("coop: epona: {} '{}' is not x,y,z; the default is used", saddle->name,
                              saddle->value);
                }
            }
            ORM_LOG_INFO("coop: epona: riders drawn on a horse, actor {}, saddle ({}, {}, {})",
                     settings.actor.empty() ? std::string("by the horse he is on") : settings.actor,
                     settings.saddle[0], settings.saddle[1], settings.saddle[2]);
            director = std::make_unique<CoopPuppet::Director>(
                [this](const std::string& line) {
                    Count(line);  // a pack missing from an older puppet pack: its creates fail
                    ORM_LOG_INFO("horse {}", line);
                },
                EnvOn("LICHII_COOP_DEBUG_PUPPET"), EnvOn("LICHII_COOP_DEBUG_ANIM"));
            // The event horse has a physics body: a plain write of its place is undone.
            director->SetMatrixCall(true);
            // ORM smoothing (REVISION_NUEVAS orm/epona): a horse with its rider in the
            // saddle is drawn from the pose his puppet was drawn with this calc frame
            // (the puppets' director goes first in the cave); nothing to use while the
            // smoothing is off (LICHII_ORM_SMOOTH=0): the plan's place, as his puppet.
            {
                std::lock_guard lock(saddle_mutex);
                saddle_now = settings.saddle;
            }
            director->SetSmoothing(
                [this](std::uint8_t number, std::uint64_t server_now_us) {
                    Vec3 saddle{};
                    {
                        std::lock_guard lock(saddle_mutex);
                        saddle = saddle_now;  // measured or estimated, as PickRide said (Publish)
                    }
                    return under.At(number, server_now_us, &CoopPuppet::LastSmoothed, saddle);
                },
                coop::host::SmoothSettings{}.delay_us);
            CoopPuppet::PlannerSettings puppet_settings;
            puppet_settings.riders = true;  // the plan the puppets' director gets (CoopSvc)
            planner = std::make_unique<CoopPuppet::Planner>(puppet_settings);
            rider = std::make_unique<Rider>(std::move(settings), [](const std::string& line) {
                ORM_LOG_INFO("coop: epona: {}", line);
            });
            // Only the looks the pack has are asked for (a pack from before the
            // horses has none); the two-failures fallback below stays for the rest.
            const std::set<std::string> looks = PackHorseLooks();
            std::string missing;
            for (const std::string_view look : {epona::kEponaLook, epona::kRanchLook, epona::kIngoLook}) {
                if (!looks.contains(std::string(look))) {
                    broken.insert(std::string(look));
                    rider->MarkBroken(std::string(look));
                    missing += (missing.empty() ? "" : ", ") + std::string(look);
                }
            }
            if (!missing.empty()) {
                ORM_LOG_INFO("coop: epona: the puppet pack has no {} (run make-puppet-pack again for the horses' "
                               "looks): the game's {} instead",
                         missing, kEventHorse);
            }
        }
        if (probe_on || horses) {
            // ORM riesgos-visual: on with the horses, so the others' horses are
            // drawn with our own measured saddle and gaits; the 'saddle:' line
            // only with LICHII_ORM_DEBUG_HORSE.
            SaddleProbe::Log say;
            if (probe_on) {
                say = [](const std::string& line) { ORM_LOG_INFO("coop: epona: {}", line); };
            }
            probe = std::make_unique<SaddleProbe>("Animal_Epona_Adult", std::move(say), &measure);
            LoadRide();
            if (probe_on) {
                ORM_LOG_INFO("coop: epona: saddle probe on (LICHII_ORM_DEBUG_HORSE)");
            }
        }
        CoopPlatform::AddCaller(kCallerOrder, [this](CoopPuppet::Guest& guest, bool after_call,
                                                std::uint64_t result, std::uint64_t now_ms) {
            if (!after_call && probe) {
                probe->Frame(guest, main_base.load(), now_ms, mount ? mount->MountActor() : 0);
            }
            if (!after_call && mount) {
                mount->Frame(guest, main_base.load(), now_ms);
            }
            if (!director) {
                return std::optional<CoopPuppet::Call>{};
            }
            return director->Step(guest, after_call, result, now_ms);
        });
    }
};

// One director line: "coop: puppet: P<n>: create <actor> requested ...",
// "... P<n> <actor> spawned after ...", "... not spawned after ...", "P<n>:
// request ... dropped (state 4) ...".
void CountLine(Glue& g, const std::string& line) {
    const std::string_view prefix = "coop: puppet: P";
    if (!std::string_view(line).starts_with(prefix)) {
        return;
    }
    std::size_t at = prefix.size();
    int n = 0;
    while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
        n = n * 10 + (line[at++] - '0');
    }
    const std::string rest = line.substr(at);
    std::lock_guard lock(g.fail_mutex);
    if (rest.starts_with(": create ")) {
        const auto end = rest.find(" requested");
        if (end != std::string::npos) {
            g.requested[n] = rest.substr(9, end - 9);
        }
    } else if (rest.find(" not spawned after ") != std::string::npos ||
               rest.find(" dropped (state") != std::string::npos) {
        if (const auto it = g.requested.find(n); it != g.requested.end()) {
            ++g.failures[it->second];
        }
    } else if (const auto s = rest.find(" spawned after "); s != std::string::npos && s > 1) {
        g.made.insert(rest.substr(1, s - 1));
    }
}

Glue& TheGlue() {
    static Glue glue;  // kept for the whole run: the cave holds its director
    return glue;
}

bool EponaOn() {
    static const bool on = CoopSwitch::On("LICHII_ORM_EPONA", "LICHII_COOP_EPONA", true);  // coop_switch.h
    return on;
}

} // namespace

bool HorsesOn() {
    // From the switches alone: CoopSvc asks while it sets the puppets up, before
    // the glue (which registers with the cave) may be made.
    static const bool on = (EnvOn("LICHII_COOP_PUPPET") || EnvOn("LICHII_COOP_DEBUG_PUPPET")) &&
                           CoopSwitch::On("LICHII_ORM_HORSE", "LICHII_COOP_HORSE", true);
    return on;
}

void AddShared(std::optional<coop::host::SharedFlags>& shared) {
    Glue& g = TheGlue();
    if (!EponaOn()) {
        ORM_LOG_INFO("coop: epona: Step_Get_Epona not shared (LICHII_ORM_EPONA=0): each game wins its own");
        g.step_shared = false;
        return;
    }
    const auto added = epona::add_shared_step(shared);
    g.step_shared = added == epona::Added::added;
    ORM_LOG_INFO("coop: epona: Step_Get_Epona = Complete shared forward (the group's Epona): {}",
             epona::to_string(added));
}

bool RaceHold(coop::host::GameMemory& memory) {
    Glue& g = TheGlue();
    if (!g.step_shared) {
        return false;
    }
    const std::uint64_t now = NowMs();
    if (g.step_ms == 0 || now - std::min(now, g.step_ms) >= 250) {
        g.step_ms = std::max<std::uint64_t>(now, 1);
        const auto value = epona::read_step(memory);
        if (value != g.step && value) {
            const auto name = epona::value_name(*value);
            ORM_LOG_INFO("coop: epona: Step_Get_Epona here is {}",
                     name ? std::string(*name) : Hex(*value));
        }
        if (value) {
            g.step = value;
        }
        const bool racing = epona::race_running(g.step);
        if (racing != g.racing) {
            g.racing = racing;
            g.held_said = false;
            if (racing) {
                ORM_LOG_INFO("coop: epona: our race with Ingo runs: the group's Epona waits until it is over");
            } else {
                ORM_LOG_INFO("coop: epona: our race with Ingo is over: the group's Epona may come");
            }
        }
    }
    return g.racing;
}

void NoteHeld() {
    Glue& g = TheGlue();
    if (!g.held_said) {
        g.held_said = true;
        ORM_LOG_INFO("coop: epona: the group's Epona came during our race: it waits");
    }
}

void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& position, bool local_event,
             std::optional<std::string_view> own_anim, coop::host::Session* session,
             std::uint64_t server_now_us) {
    Glue& g = TheGlue();
    if (!g.horses && !g.probe_on) {
        return;
    }
    const std::uint64_t now_ms = NowMs();
    g.main_base.store(main_base);
    if (g.probe) {
        g.probe->SetOwn(position, epona::ride_of(own_anim) == epona::Ride::riding, now_ms);
    }
    if (g.probe) {
        g.SaveRide(now_ms);
    }
    if (!g.horses) {
        return;
    }
    // ORM riesgos-visual: the saddle and the gaits the horses are drawn with
    // now (the switch, our own measure, or the estimates) and their check lines.
    const RideNow ride = PickRide(g.measure, g.saddle_switch, g.drawing, g.estimates);
    g.rider->SetSaddle(ride.saddle);
    g.rider->SetSpeeds(ride.speeds);
    {
        std::lock_guard lock(g.saddle_mutex);
        g.saddle_now = ride.saddle;
    }
    {
        std::lock_guard lock(g.fail_mutex);
        for (const auto& [actor, count] : g.failures) {
            if (count >= 2 && actor != kEventHorse && !g.made.contains(actor) && !g.broken.contains(actor)) {
                g.broken.insert(actor);
                g.rider->MarkBroken(actor);
                ORM_LOG_INFO("coop: epona: {} was not made twice (a puppet pack from before the horses: run "
                               "make-puppet-pack again for the horses' looks); the game's {} instead",
                         actor, kEventHorse);
            }
        }
    }
    // The same remotes and plan the puppets get, so a horse is under exactly
    // the rider the player sees.
    const std::vector<CoopPuppet::Remote> remotes = CoopPuppet::Collect(session, server_now_us);
    auto puppets = g.planner->Plan(main_base, scene, position, local_event, remotes, now_ms);
    CoopPuppet::StampDrawing(puppets, now_ms);
    puppets.server_now_us = server_now_us;  // as CoopPuppet::Publish: run on by the director
    puppets.server_at_ms = now_ms;
    std::map<std::uint8_t, std::string> mounts;
    std::map<std::uint8_t, std::uint64_t> upstream_rides;
    {
        std::lock_guard lock(g.mounts_mutex);
        mounts = g.mounts;
        upstream_rides = g.upstream_rides;
    }
    // ORM test10 (D-R21, docs/PIN_TEST10.md): Epona under a rider is upstream's
    // (PLAYER_RIDE, CoopRide: the horse's own place and animation from the
    // rider's game). Ours draws only the horses it does not (the ranch's,
    // Ingo's, the event horses), so a rider on Epona never gets two horses.
    std::set<std::uint8_t> yield;
    for (const auto& [number, at_ms] : upstream_rides) {
        if (now_ms <= at_ms + kUpstreamRideFreshMs) {
            yield.insert(number);
        }
    }
    for (const auto& [number, mount] : mounts) {
        if (mount == kUpstreamHorse) {
            yield.insert(number);
        }
    }
    std::erase_if(puppets.targets,
                  [&yield](const CoopPuppet::Target& t) { return t.number != 0 && yield.contains(t.number); });
    if (yield != g.yielded) {
        for (const std::uint8_t n : yield) {
            if (!g.yielded.contains(n)) {
                ORM_LOG_INFO("coop: epona: P{} on Epona: drawn by upstream's PLAYER_RIDE (CoopRide), not ours", n);
            }
        }
        g.yielded = yield;
    }
    auto horses = g.rider->Follow(puppets, now_ms, CoopPuppet::LivePuppets(), &mounts);
    g.under.Set(g.rider->Riding());  // before the input: never a rider smoothed on an old plan's horse
    g.drawing = !horses.targets.empty();
    g.director->SetInput(std::move(horses));
}

void StampMount(coop::host::PlayerReading& reading) {
    Glue& g = TheGlue();
    if (g.mount) {
        g.mount->Stamp(reading);
    }
}

void OnEvent(const coop::net::Event& event) {
    using coop::net::EventKind;
    Glue& g = TheGlue();
    std::lock_guard lock(g.mounts_mutex);
    if (event.kind == EventKind::disconnected) {
        g.mounts.clear();
        g.upstream_rides.clear();
    } else if (event.kind == EventKind::player_left) {
        g.mounts.erase(event.player);
        g.upstream_rides.erase(event.player);
    } else if (const auto* ride = event.kind == EventKind::message
                                      ? std::get_if<coop::protocol::PlayerRide>(&event.message)
                                      : nullptr;
               ride != nullptr && ride->player_number != 0) {
        // ORM test10: upstream's rider (PLAYER_RIDE, about 30 a second while on).
        if ((ride->flags & coop::protocol::kRideRiding) != 0 && ride->horse == kUpstreamHorse) {
            g.upstream_rides[ride->player_number] = NowMs();
        } else {
            g.upstream_rides.erase(ride->player_number);
        }
    } else if (event.kind == EventKind::player_equip && event.player != 0) {
        if (event.equipment.mount) {
            g.mounts[event.player] = *event.equipment.mount;
        } else {
            g.mounts.erase(event.player);  // an older game: not said
        }
    }
}

} // namespace CoopEpona

#endif  // ORM_PLATFORM_LIVE
