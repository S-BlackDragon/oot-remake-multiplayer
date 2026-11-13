// Tests for ORM golpes (lichii/hook/coop_hits.cpp, coop_copy_smooth.cpp and their use in
// coop_objects_sync.cpp; oot-remake docs/GOLPES_LATENCIA.md): a player hitting an enemy another
// game owns, with the network's delay simulated. No emulator code: each game is a few lines of
// state (its enemy or its copy), each walk a list of actors, and the wire a queue that delivers
// each message `one_way` later.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coop_copy_smooth.h"
#include "coop_hits.h"
#include "coop_objects_sync.h"

namespace proto = coop::protocol;
using CoopHits::Feedback;
using CoopHits::IsHitReaction;
using CoopHits::OwnerLedger;
using CoopHits::Phase;
using CoopObjects::LifeWrite;
using CoopObjects::PlayWish;
using CoopObjects::PoseWrite;
using CoopObjects::Seen;
using CoopObjects::Sync;

namespace {

constexpr std::uint64_t kMs = 1000;
constexpr std::uint64_t kFrame = 16'667;  // a game frame at 60 a second
const std::string kScene = "Spot_GolpesTest";
constexpr std::uint64_t kHash = 0x77aa;
constexpr std::uint64_t kAddrA = 0x1000;  // the enemy in the owner's game (player 1)
constexpr std::uint64_t kAddrB = 0x2000;  // its copy in the hitter's game (player 2)

// -------------------------------------------------------------------------------------------
// The two games and the wire.

// The owner's own enemy: its game takes a hurt (the hit record Sync writes) on the next frame,
// plays its flinch for 600 ms and is knocked back 1 m along +x in 300 ms, as a real hit.
struct OwnerGame {
    std::int32_t life = 3;
    std::int32_t max = 3;
    float x = 0.0f;
    float z = 0.0f;
    float walk = 0.0f;  // z metres a second while not knocked back
    std::string anim = "Wait";
    std::uint64_t react_until = 0;
    std::uint64_t knock_until = 0;
    bool takes_hurts = true;
    int hurts_taken = 0;
    int writes_taken = 0;
    std::vector<LifeWrite> pending;

    void Frame(std::uint64_t now, float dt) {
        for (const LifeWrite& w : pending) {
            if (w.address != kAddrA) {
                continue;
            }
            if (w.hurt > 0) {
                if (!takes_hurts) {
                    continue;
                }
                life = w.life;
                ++hurts_taken;
                anim = "Damage_Front";
                react_until = now + 600 * kMs;
                knock_until = now + 300 * kMs;
            } else {
                life = w.life;
                ++writes_taken;
            }
        }
        pending.clear();
        if (now < knock_until) {
            x += dt / 0.3f;
        } else {
            z += walk * dt;
        }
        if (react_until != 0 && now >= react_until) {
            anim = walk > 0.0f ? "Walk" : "Wait";
            react_until = 0;
        }
        if (life <= 0) {
            anim = "Dead";
        }
    }
    Seen Walk() const {
        Seen s;
        s.address = kAddrA;
        s.name = "Enemy_ReDead";
        s.hash = kHash;
        s.life = proto::ObjectLife{life, max};
        s.position = {x, 0.0f, z};
        s.animation = proto::Animation{anim, 0.0f, 1.0f};
        return s;
    }
};

// The hitter's copy: placed by the Mover (Poses) when Sync asks, else moved by its own game (our
// hit's knockback); animated by the Mover (Plays) when Sync asks, else by its own game.
struct CopyGame {
    std::int32_t life = 3;
    std::int32_t max = 3;
    float x = 0.0f;
    float z = 0.0f;
    std::string anim = "Wait";
    std::uint64_t react_until = 0;
    std::uint64_t knock_until = 0;

    void Hit(std::uint64_t now, std::int32_t amount = 1) {
        life -= amount;
        anim = life <= 0 ? "Dead" : "Damage_Front";
        react_until = now + 600 * kMs;
        knock_until = now + 300 * kMs;
    }
    void Apply(const std::vector<LifeWrite>& writes) {
        for (const LifeWrite& w : writes) {
            if (w.address == kAddrB) {
                life = w.life;
            }
        }
    }
    void Frame(std::uint64_t now, float dt, const PoseWrite* pose, const PlayWish* play) {
        if (pose != nullptr) {
            x = pose->position[0];
            z = pose->position[2];
        } else if (now < knock_until) {
            x += dt / 0.3f;
        }
        if (play != nullptr) {
            anim = play->name;
        } else if (react_until != 0 && now >= react_until) {
            if (life > 0) {
                anim = "Wait";
            }
            react_until = 0;
        }
    }
    Seen Walk() const {
        Seen s;
        s.address = kAddrB;
        s.name = "Enemy_ReDead";
        s.hash = kHash;
        s.life = proto::ObjectLife{life, max};
        s.position = {x, 0.0f, z};
        s.animation = proto::Animation{anim, 0.0f, 1.0f};
        return s;
    }
};

struct Wire {
    std::uint64_t at = 0;
    std::uint8_t to = 0;
    proto::Message message;
};

struct Frame {
    std::uint64_t now = 0;
    std::optional<proto::Vec3> pose;  // what B's Sync wrote into the copy, if anything
    std::optional<std::string> play;  // what B's Sync asked the copy to play, if anything
    float shown_x = 0.0f;             // where the copy is on B's screen after the frame
};

struct Sim {
    Sync a;  // player 1, the owner
    Sync b;  // player 2, the hitter
    OwnerGame owner;
    CopyGame copy;
    std::uint64_t one_way = 100 * kMs;
    bool lose_damage = false;
    std::uint64_t now = 0;
    std::uint64_t frame = 0;
    std::vector<Wire> wires;
    std::vector<std::uint64_t> hits;  // when B's Link hits the copy
    std::vector<std::pair<std::uint64_t, LifeWrite>> b_writes;
    std::vector<std::uint64_t> b_heals;  // when a write raised the copy's life as our game had it
    std::vector<std::uint64_t> damage_sent;
    std::vector<Frame> frames;

    explicit Sim(bool golpes, bool smooth = false) {
        a.SetHurt(true);
        b.SetHurt(true);
        a.SetHits(golpes);
        b.SetHits(golpes);
        b.SetCopySmooth(smooth);
    }

    void Send(std::uint8_t from, const std::vector<proto::Message>& out) {
        for (const proto::Message& m : out) {
            if (const auto* s = std::get_if<proto::ObjectSample>(&m)) {
                proto::ObjectSample copy = *s;
                copy.player_number = from;
                wires.push_back({now + one_way, static_cast<std::uint8_t>(from == 1 ? 2 : 1), copy});
            } else if (const auto* r = std::get_if<proto::ObjectRemove>(&m)) {
                proto::ObjectRemove copy = *r;
                copy.player_number = from;
                wires.push_back({now + one_way, static_cast<std::uint8_t>(from == 1 ? 2 : 1), copy});
            } else if (const auto* d = std::get_if<proto::Damage>(&m)) {
                if (from == 2) {
                    damage_sent.push_back(now);
                }
                if (lose_damage) {
                    continue;
                }
                proto::Damage copy = *d;
                copy.player_number = from;
                wires.push_back({now + one_way, 1, copy});  // the server relays it to the owner
            }
        }
    }

    void Start() {
        std::vector<proto::Message> send;
        std::vector<LifeWrite> writes;
        a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
        send.clear();
        b.Tick(kScene, 2, now, {copy.Walk()}, send, writes);
        const proto::ObjectId id = *a.IdOf(kAddrA);
        REQUIRE(id == *b.IdOf(kAddrB));
        a.Receive(proto::ObjectOwner{id, 1, 0, 0});
        b.Receive(proto::ObjectOwner{id, 1, 0, 0});
    }

    void Step() {
        ++frame;
        now = frame * kFrame;
        const float dt = static_cast<float>(kFrame) / 1e6f;
        for (auto it = wires.begin(); it != wires.end();) {
            if (it->at <= now) {
                (it->to == 1 ? a : b).Receive(it->message);
                it = wires.erase(it);
            } else {
                ++it;
            }
        }
        for (const std::uint64_t h : hits) {
            if (h > now - kFrame && h <= now) {
                copy.Hit(now);
            }
        }
        if (frame % 2 == 0) {  // the walk, every second frame
            std::vector<proto::Message> send;
            std::vector<LifeWrite> writes;
            a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
            owner.pending.insert(owner.pending.end(), writes.begin(), writes.end());
            Send(1, send);
            send.clear();
            writes.clear();
            b.Tick(kScene, 2, now, {copy.Walk()}, send, writes);
            for (const LifeWrite& w : writes) {
                b_writes.emplace_back(now, w);
                if (w.address == kAddrB && w.life > copy.life) {
                    b_heals.push_back(now);
                }
            }
            copy.Apply(writes);
            Send(2, send);
        }
        std::vector<PoseWrite> poses;
        b.Poses(now, poses);
        std::vector<PlayWish> plays;
        b.Plays(now, plays);
        Frame f;
        f.now = now;
        const PoseWrite* pose = nullptr;
        for (const PoseWrite& w : poses) {
            if (w.address == kAddrB) {
                pose = &w;
                f.pose = w.position;
            }
        }
        const PlayWish* play = nullptr;
        for (const PlayWish& p : plays) {
            if (p.address == kAddrB) {
                play = &p;
                f.play = p.name;
            }
        }
        copy.Frame(now, dt, pose, play);
        f.shown_x = copy.x;
        frames.push_back(f);
        owner.Frame(now, dt);
    }

    void RunTo(std::uint64_t until) {
        while (now < until) {
            Step();
        }
    }

    // Frames in [from, to).
    std::vector<Frame> Between(std::uint64_t from, std::uint64_t to) const {
        std::vector<Frame> out;
        for (const Frame& f : frames) {
            if (f.now >= from && f.now < to) {
                out.push_back(f);
            }
        }
        return out;
    }
    bool Played(std::uint64_t from, std::uint64_t to, const std::string& name) const {
        for (const Frame& f : Between(from, to)) {
            if (f.play && *f.play == name) {
                return true;
            }
        }
        return false;
    }
    float MaxStep(std::uint64_t from, std::uint64_t to) const {
        float most = 0.0f;
        const auto fs = Between(from, to);
        for (std::size_t i = 1; i < fs.size(); ++i) {
            most = std::max(most, std::fabs(fs[i].shown_x - fs[i - 1].shown_x));
        }
        return most;
    }
    float ShownAt(std::uint64_t t) const {
        float x = 0.0f;
        for (const Frame& f : frames) {
            if (f.now <= t) {
                x = f.shown_x;
            }
        }
        return x;
    }
    // A write that raised our copy's life above what our game had (our own hit lowers it
    // with no write, so the writes alone cannot tell).
    bool HealedB() const { return !b_heals.empty(); }
    bool HurtB() const {  // a hit replayed on our copy (a second flinch)
        for (const auto& [t, w] : b_writes) {
            if (w.address == kAddrB && w.hurt > 0) {
                return true;
            }
        }
        return false;
    }
};

proto::Vec3 V(float x, float y = 0.0f, float z = 0.0f) {
    return {x, y, z};
}

}  // namespace

// -------------------------------------------------------------------------------------------
// The pieces.

TEST_CASE("golpes: hit reactions are told apart from deaths and the rest") {
    CHECK(IsHitReaction("Damage_Front"));
    CHECK(IsHitReaction("Damage_Head"));
    CHECK(IsHitReaction("Dmg_L"));
    CHECK(IsHitReaction("Hit"));
    CHECK(IsHitReaction("Hit_ModelOnlyLOD"));
    CHECK(IsHitReaction("Battle_KnockBack_Lp"));
    CHECK(IsHitReaction("Stagger2"));
    CHECK_FALSE(IsHitReaction("Damage_Death"));  // a death
    CHECK_FALSE(IsHitReaction("Dead_Fall"));
    CHECK_FALSE(IsHitReaction("Wait"));
    CHECK_FALSE(IsHitReaction("Attack_Start"));
    CHECK_FALSE(IsHitReaction("WhiteHit"));  // not a word of its own
    CHECK_FALSE(IsHitReaction(""));
}

TEST_CASE("golpes: the one-way delay comes from the samples, up fast and down slowly") {
    CoopHits::Latency l;
    CHECK_FALSE(l.Known());
    CHECK(l.OneWayUs() == CoopHits::Latency::kDefaultUs);
    l.Note(1'000 * kMs, 1'100 * kMs);
    CHECK(l.OneWayUs() == 100 * kMs);
    l.Note(2'000 * kMs, 2'500 * kMs);  // a 500 ms spike: a quarter of the way at once
    CHECK(l.OneWayUs() == 200 * kMs);
    l.Note(3'000 * kMs, 3'100 * kMs);  // back to 100: a sixteenth of the way
    CHECK(l.OneWayUs() == 193'750);
    l.Note(5'000 * kMs, 4'990 * kMs);  // a sample from "the future" (clocks): 0, not a wrap
    CHECK(l.OneWayUs() < 193'750);
    l.Note(0, 100'000 * kMs);  // never more than a second
    CHECK(l.OneWayUs() <= CoopHits::Latency::kMostUs);
}

TEST_CASE("golpes: after our hit the copy is ours to move, then held, then blended back") {
    Feedback fb;  // one way 50 ms (nothing measured)
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    // Our game's knockback shows: nothing is written for free_us.
    auto p = fb.Pose(kAddrB, t0 + 100 * kMs, V(0.0f), std::nullopt, V(0.4f), std::nullopt);
    CHECK_FALSE(p.write);
    CHECK(fb.PhaseOf(kAddrB) == Phase::free);
    // Then held where our knockback left it, while the owner's stream still shows 0.
    p = fb.Pose(kAddrB, t0 + 300 * kMs, V(0.0f), std::nullopt, V(1.0f), std::nullopt);
    REQUIRE(p.write);
    CHECK(fb.PhaseOf(kAddrB) == Phase::hold);
    CHECK(p.position[0] == doctest::Approx(1.0f));
    // The same call again gives the same (Holds asks Poses a second time each frame).
    p = fb.Pose(kAddrB, t0 + 300 * kMs, V(0.0f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(p.position[0] == doctest::Approx(1.0f));
    // The owner's answer: its sample taken at t0 + 150 ms shows the life. On screen 100 ms
    // later; its knockback then comes towards ours.
    fb.OwnerLife(kAddrB, t0 + 150 * kMs, 2);
    p = fb.Pose(kAddrB, t0 + 400 * kMs, V(0.5f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::hold);  // shown, but 0.5 m off: still held
    CHECK(p.position[0] == doctest::Approx(1.0f));
    p = fb.Pose(kAddrB, t0 + 450 * kMs, V(0.85f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::blend);  // within converge_m
    CHECK(p.position[0] == doctest::Approx(1.0f));
    p = fb.Pose(kAddrB, t0 + 550 * kMs, V(0.95f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(p.position[0] > 0.95f);
    CHECK(p.position[0] < 1.0f);
    p = fb.Pose(kAddrB, t0 + 650 * kMs, V(1.0f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::follow);
    CHECK(p.position[0] == doctest::Approx(1.0f));
    const auto stats = fb.TakeStats();
    CHECK(stats.hits == 1);
    CHECK(stats.answered == 1);
    CHECK(stats.timed_out == 0);
}

TEST_CASE("golpes: with no answer the hold ends at the worst answer time") {
    Feedback fb;
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    const std::uint64_t worst = fb.WorstAnswerUs();  // 2 x 50 + 450 + 100 + 100
    CHECK(worst == 750 * kMs);
    fb.Pose(kAddrB, t0 + 300 * kMs, V(0.0f), std::nullopt, V(1.0f), std::nullopt);
    fb.Pose(kAddrB, t0 + worst - kFrame, V(0.0f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::hold);
    fb.Pose(kAddrB, t0 + worst, V(0.0f), std::nullopt, V(1.0f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::blend);
    CHECK(fb.TakeStats().timed_out == 1);
}

TEST_CASE("golpes: owner animations from before the hit and its own reaction are kept off") {
    Feedback fb;  // one way 50 ms
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    CHECK(fb.SkipPlay(kAddrB, t0 + 10 * kMs, "Attack_Start", t0 - 100 * kMs));  // from before
    CHECK(fb.SkipPlay(kAddrB, t0 + 10 * kMs, "Attack_Start", t0 + 60 * kMs));   // the owner did not know yet
    CHECK(fb.SkipPlay(kAddrB, t0 + 200 * kMs, "Wait", t0 + 150 * kMs));         // still in it: no answer yet
    CHECK(fb.SkipPlay(kAddrB, t0 + 200 * kMs, "Damage_Front", t0 + 150 * kMs));  // its reaction
    CHECK_FALSE(fb.SkipPlay(kAddrB, t0 + 200 * kMs, "Attack_Start", t0 + 150 * kMs));  // it acts
    fb.OwnerLife(kAddrB, t0 + 160 * kMs, 2);
    CHECK(fb.SkipPlay(kAddrB, t0 + 300 * kMs, "Damage_Front", t0 + 160 * kMs));  // ours showed it
    CHECK_FALSE(fb.SkipPlay(kAddrB, t0 + 800 * kMs, "Wait", t0 + 760 * kMs));    // moved on
    CHECK_FALSE(fb.SkipPlay(kAddrB, t0 + 900 * kMs, "Damage_Front", t0 + 860 * kMs));  // a later hit
    // And after the reaction window nothing is kept off.
    Feedback late;
    late.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    CHECK_FALSE(late.SkipPlay(kAddrB, t0 + 3'000 * kMs, "Wait", t0 - 100 * kMs));
    CHECK(late.TakeStats().plays_skipped == 0);
}

TEST_CASE("golpes: a kill here dies here, and comes back if the owner says it lives") {
    Feedback fb;
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 0, V(0.0f), std::nullopt, "Walk");
    CHECK(fb.TakeStats().kills == 1);
    CHECK_FALSE(fb.Pose(kAddrB, t0 + 500 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt).write);
    CHECK(fb.SkipPlay(kAddrB, t0 + 500 * kMs, "Walk", t0 + 400 * kMs));
    CHECK(fb.SkipPlay(kAddrB, t0 + 500 * kMs, "Dead", t0 + 400 * kMs));  // our death already plays
    fb.OwnerLife(kAddrB, t0 + 150 * kMs, 0);
    fb.Pose(kAddrB, t0 + 600 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt);
    CHECK(fb.PhaseOf(kAddrB) == Phase::dead);
    CHECK(fb.Active(kAddrB, t0 + 60'000 * kMs));  // dead here until it goes

    Feedback no;
    no.OurHit(kAddrB, t0, 0, V(0.0f), std::nullopt, "Walk");
    no.Rejected(kAddrB, t0 + 1'500 * kMs);  // the owner's enemy lives: written back up
    const auto p = no.Pose(kAddrB, t0 + 1'500 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt);
    REQUIRE(p.write);
    CHECK(no.PhaseOf(kAddrB) == Phase::blend);
    CHECK(p.position[0] == doctest::Approx(0.2f));  // from where it lies here
    CHECK_FALSE(no.SkipPlay(kAddrB, t0 + 1'500 * kMs, "Walk", t0 + 1'400 * kMs));
    CHECK(no.Pose(kAddrB, t0 + 1'800 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt).position[0] ==
          doctest::Approx(3.0f));

    Feedback silent;  // nobody answers at all: back to the owner's stream after kill_most_us
    silent.OurHit(kAddrB, t0, 0, V(0.0f), std::nullopt, "Walk");
    CHECK_FALSE(silent.Pose(kAddrB, t0 + 5'900 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt).write);
    CHECK(silent.Pose(kAddrB, t0 + 6'000 * kMs, V(3.0f), std::nullopt, V(0.2f), std::nullopt).write);
}

TEST_CASE("golpes: the dedupe window follows the measured latency") {
    Feedback fb;
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    CHECK_FALSE(fb.Before(kAddrB, t0, t0));  // nothing sent yet
    fb.Sent(kAddrB, t0);
    // One way 50 ms (default): a sample taken before t0 + 50 + 450 + 100 cannot show it.
    CHECK(fb.Before(kAddrB, t0 + 590 * kMs, t0 + 640 * kMs));
    CHECK_FALSE(fb.Before(kAddrB, t0 + 600 * kMs, t0 + 650 * kMs));
    // Over a slow tunnel (300 ms one way) the window is 250 ms longer.
    fb.latency().Note(t0, t0 + 300 * kMs);
    CHECK(fb.Before(kAddrB, t0 + 840 * kMs, t0 + 1'140 * kMs));
    CHECK_FALSE(fb.Before(kAddrB, t0 + 850 * kMs, t0 + 1'150 * kMs));
    // Never waited for longer than window_most_us.
    CHECK_FALSE(fb.Before(kAddrB, t0 + 100 * kMs, t0 + 3'000 * kMs));
}

TEST_CASE("golpes: a second hit during the first keeps the animation from before both") {
    Feedback fb;
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    fb.OurHit(kAddrB, t0 + 200 * kMs, 1, V(0.5f), std::nullopt, "Damage_Front");
    CHECK(fb.SkipPlay(kAddrB, t0 + 400 * kMs, "Wait", t0 + 350 * kMs));
    CHECK(fb.PhaseOf(kAddrB) == Phase::free);  // free again for the second knockback
    fb.Forget(kAddrB);
    CHECK_FALSE(fb.PhaseOf(kAddrB).has_value());
    CHECK_FALSE(fb.SkipPlay(kAddrB, t0 + 400 * kMs, "Wait", t0 + 350 * kMs));
}

TEST_CASE("golpes: entries go once done or once their actor left") {
    Feedback fb;
    const std::uint64_t t0 = 1'000 * kMs;
    fb.OurHit(kAddrB, t0, 2, V(0.0f), std::nullopt, "Wait");
    fb.OurHit(kAddrA, t0, 2, V(0.0f), std::nullopt, "Wait");
    fb.Sent(kAddrB, t0);
    fb.Expire(t0 + 100 * kMs, [](std::uint64_t a) { return a == kAddrB; });
    CHECK(fb.Size() == 1);  // kAddrA left the walk
    for (std::uint64_t t = t0; t <= t0 + 4'000 * kMs; t += 50 * kMs) {
        fb.Pose(kAddrB, t, V(1.0f), std::nullopt, V(1.0f), std::nullopt);
    }
    fb.Expire(t0 + 4'000 * kMs, [](std::uint64_t) { return true; });
    CHECK(fb.Size() == 0);
}

TEST_CASE("golpes: owner side, a drop beside another player's DAMAGE is the same hit") {
    using Others = std::vector<std::pair<std::uint8_t, proto::Vec3>>;
    CHECK(OwnerLedger::Near(V(0.0f), V(10.0f), Others{{2, V(1.0f)}}) == std::optional<std::uint8_t>(2));
    CHECK_FALSE(OwnerLedger::Near(V(0.0f), V(2.0f), Others{{2, V(1.0f)}}).has_value());  // our Link near
    CHECK_FALSE(OwnerLedger::Near(V(0.0f), std::nullopt, Others{{2, V(1.0f)}}).has_value());
    CHECK_FALSE(OwnerLedger::Near(V(0.0f), V(10.0f), Others{{2, V(1.0f)}, {3, V(-1.0f)}}).has_value());
    CHECK_FALSE(OwnerLedger::Near(V(0.0f), V(10.0f), Others{{2, V(6.0f)}}).has_value());

    const std::uint64_t t0 = 1'000 * kMs;
    {  // logged only (the default): everything is applied, the pair counted
        OwnerLedger l;
        CHECK(l.Drop(kAddrA, t0, 1, 2) == 0);
        CHECK(l.Damage(kAddrA, t0 + 100 * kMs, 1, 2) == 1);
        CHECK(l.TakePairs() == 1);
        CHECK(l.TakeTakenBack() == 0);
    }
    {  // put back on: the drop came first, the DAMAGE gives nothing more
        OwnerLedger l;
        l.SetPutBack(true);
        CHECK(l.Drop(kAddrA, t0, 1, 2) == 0);
        CHECK(l.Damage(kAddrA, t0 + 100 * kMs, 2, 2) == 1);  // one of the two was that drop
        CHECK(l.TakePairs() == 1);
        CHECK(l.TakeTakenBack() == 1);
        // The DAMAGE first: the drop after it is written back up.
        CHECK(l.Damage(kAddrA, t0 + 1'000 * kMs, 1, 3) == 1);
        CHECK(l.Drop(kAddrA, t0 + 1'150 * kMs, 1, 3) == 1);
        // Another player's, or too late: not the same hit.
        CHECK(l.Damage(kAddrA, t0 + 2'000 * kMs, 1, 2) == 1);
        CHECK(l.Drop(kAddrA, t0 + 2'050 * kMs, 1, 3) == 0);
        CHECK(l.Drop(kAddrA, t0 + 2'700 * kMs, 1, 2) == 0);
        l.Expire(t0 + 10'000 * kMs, [](std::uint64_t) { return true; });
        CHECK(l.Damage(kAddrA, t0 + 10'000 * kMs, 1, 2) == 1);
    }
}

TEST_CASE("golpes: copies smoothed, a lost sample eased and a teleport snapped") {
    CoopCopySmooth::Streams streams;
    std::uint32_t seq = 0;
    // 3 m a second along x, a sample every 50 ms, the one at 300 ms lost.
    for (std::uint64_t t = 0; t <= 600 * kMs; t += 50 * kMs) {
        if (t == 300 * kMs) {
            ++seq;
            continue;
        }
        const float x = 3.0f * static_cast<float>(t) / 1e6f;
        CHECK(streams.Push(kAddrB, {t, ++seq}, V(x), V(3.0f), std::nullopt));
    }
    CHECK_FALSE(streams.Push(kAddrB, {600 * kMs, seq}, V(1.8f), V(3.0f), std::nullopt));  // a repeat
    float last = -1.0f;
    float most = 0.0f;
    for (std::uint64_t now = 150 * kMs; now <= 700 * kMs; now += kFrame) {
        const auto p = streams.At(kAddrB, now);
        REQUIRE(p.has_value());
        // Drawn 100 ms behind, where the owner's enemy was then.
        CHECK(p->position[0] == doctest::Approx(3.0f * static_cast<float>(now - 100 * kMs) / 1e6f).epsilon(0.02));
        if (last >= 0.0f) {
            most = std::max(most, std::fabs(p->position[0] - last));
        }
        last = p->position[0];
    }
    CHECK(most < 0.07f);  // 3 m/s x 16.7 ms is 0.05 m a frame
    // A Poe vanishing here and coming back 10 m on: drawn there at once, not slid across.
    CHECK(streams.Push(kAddrB, {650 * kMs, ++seq}, V(11.95f), std::nullopt, std::nullopt));
    bool slid = false;
    bool snapped = false;
    for (std::uint64_t now = 700 * kMs; now <= 900 * kMs; now += kFrame) {
        const auto p = streams.At(kAddrB, now);
        REQUIRE(p.has_value());
        slid = slid || (p->position[0] > 3.0f && p->position[0] < 11.0f);
        snapped = snapped || p->snapped;
    }
    CHECK_FALSE(slid);
    CHECK(snapped);
    streams.Reset(kAddrB);  // another owner: nothing until its first sample
    CHECK_FALSE(streams.At(kAddrB, 1'000 * kMs).has_value());
    streams.Expire([](std::uint64_t) { return false; });
    CHECK(streams.Size() == 0);
}

// -------------------------------------------------------------------------------------------
// Two games, 100 ms each way.

TEST_CASE("golpes: a hit on another game's enemy shows at once and once, 100 ms each way") {
    for (const bool golpes : {false, true}) {
        CAPTURE(golpes);
        Sim sim(golpes);
        sim.Start();
        const std::uint64_t hit = 2'000 * kMs;
        sim.hits = {hit};
        sim.RunTo(5'000 * kMs);

        // The owner applied the hit once, through its own game.
        CHECK(sim.owner.life == 2);
        CHECK(sim.owner.hurts_taken == 1);
        CHECK(sim.copy.life == 2);
        CHECK_FALSE(sim.HealedB());
        CHECK_FALSE(sim.HurtB());  // the owner's answer is not a second hit here
        REQUIRE(sim.damage_sent.size() == 1);
        if (golpes) {
            // Sent in the walk that saw it (not after a 100 ms sum).
            CHECK(sim.damage_sent[0] - hit < 2 * kFrame + 1);
            // The knockback shows on our screen at once: nothing pulls the copy back.
            CHECK(sim.ShownAt(hit + 150 * kMs) > 0.3f);
            for (const Frame& f : sim.Between(hit + kFrame, hit + 300 * kMs)) {
                CHECK_FALSE(f.pose.has_value());
            }
            // No owner animation from before the hit, and no second flinch, over ours.
            CHECK_FALSE(sim.Played(hit, hit + 2'500 * kMs, "Damage_Front"));
            CHECK_FALSE(sim.Played(hit, hit + 700 * kMs, "Wait"));
            // Once the owner's reaction is over, the copy follows its animation again.
            CHECK(sim.Played(hit + 700 * kMs, 5'000 * kMs, "Wait"));
            // No jump back and forth: at most a short step a frame, and it ends where the
            // owner's enemy is.
            CHECK(sim.MaxStep(hit, 5'000 * kMs) < 0.12f);
            CHECK(sim.copy.x == doctest::Approx(sim.owner.x).epsilon(0.05));
            // About 100 ms one way, measured from the samples.
            const std::uint64_t one_way = sim.b.Hits().latency().OneWayUs();
            CHECK(one_way >= 100 * kMs);
            CHECK(one_way <= 140 * kMs);
        } else {
            // Before: summed 100 ms first, the copy pulled back to the owner's place, and the
            // owner's own flinch played a second time when its answer came.
            CHECK(sim.damage_sent[0] - hit >= 100 * kMs);
            CHECK(sim.ShownAt(hit + 150 * kMs) < 0.1f);
            CHECK(sim.Played(hit, hit + 2'500 * kMs, "Damage_Front"));
        }
    }
}

TEST_CASE("golpes: a kill here dies here at once and is not walked on by the owner's enemy") {
    for (const bool golpes : {false, true}) {
        CAPTURE(golpes);
        Sim sim(golpes);
        sim.owner.life = 1;
        sim.copy.life = 1;
        sim.owner.walk = 1.0f;  // the owner's enemy walks on until it hears of the hit
        sim.Start();
        const std::uint64_t hit = 2'000 * kMs;
        sim.hits = {hit};
        sim.RunTo(4'000 * kMs);
        CHECK(sim.owner.life == 0);
        CHECK(sim.copy.life == 0);
        CHECK_FALSE(sim.HealedB());
        if (golpes) {
            for (const Frame& f : sim.Between(hit + kFrame, 4'000 * kMs)) {
                CHECK_FALSE(f.pose.has_value());  // where it fell
                CHECK_FALSE(f.play.has_value());  // our game's death, not the owner's walk
            }
            CHECK(sim.b.Hits().PhaseOf(kAddrB) == Phase::dead);
        } else {
            // Before: the copy kept being moved with the owner's walking enemy.
            bool moved = false;
            for (const Frame& f : sim.Between(hit + 50 * kMs, hit + 200 * kMs)) {
                moved = moved || f.pose.has_value();
            }
            CHECK(moved);
        }
    }
}

TEST_CASE("golpes: a slow owner (350 ms each way, hit written after the wait) is not healed then hit again") {
    for (const bool golpes : {false, true}) {
        CAPTURE(golpes);
        Sim sim(golpes);
        sim.one_way = 350 * kMs;
        sim.owner.walk = 0.5f;  // samples 20 a second
        sim.owner.takes_hurts = false;  // the owner's game does not take the hit record: life written at 400 ms
        sim.Start();
        const std::uint64_t hit = 3'000 * kMs;
        sim.hits = {hit};
        sim.RunTo(7'000 * kMs);
        CHECK(sim.owner.life == 2);
        CHECK(sim.owner.writes_taken == 1);
        CHECK(sim.copy.life == 2);
        if (golpes) {
            CHECK_FALSE(sim.HealedB());
            CHECK_FALSE(sim.HurtB());
        } else {
            // Before: 1 s after the DAMAGE a sample from before the owner wrote it healed the
            // copy, and the next one hit it again (a second flinch).
            CHECK(sim.HealedB());
            CHECK(sim.HurtB());
        }
    }
}

TEST_CASE("golpes: an owner that never got the hit wins, and the copy goes back to it") {
    Sim sim(true);
    sim.lose_damage = true;
    sim.Start();
    const std::uint64_t hit = 2'000 * kMs;
    sim.hits = {hit};
    sim.RunTo(5'000 * kMs);
    CHECK(sim.owner.life == 3);
    CHECK(sim.copy.life == 3);  // written back up: the owner decides
    REQUIRE(sim.HealedB());
    std::uint64_t healed = 0;
    for (const auto& [t, w] : sim.b_writes) {
        if (w.address == kAddrB && w.life == 3) {
            healed = t;
            break;
        }
    }
    CHECK(healed >= sim.damage_sent[0] + Sync::kHitHoldUs);  // never sooner than before
    CHECK(sim.b.Hits().TakeStats().rejected == 1);
    // Placed and animated by the owner again afterwards.
    bool placed = false;
    for (const Frame& f : sim.Between(healed + 300 * kMs, 5'000 * kMs)) {
        placed = placed || f.pose.has_value();
    }
    CHECK(placed);
    CHECK(sim.Played(healed, 5'000 * kMs, "Wait"));
}

TEST_CASE("golpes: owner side, a puppet's hit and its player's DAMAGE count once with dedupe on") {
    for (const bool dedupe : {false, true}) {
        CAPTURE(dedupe);
        Sync a;
        a.SetHurt(true);
        a.SetHits(true);
        a.SetHitDedupe(dedupe);
        a.SetPlayer(V(20.0f));  // our Link far away
        OwnerGame owner;
        std::vector<proto::Message> send;
        std::vector<LifeWrite> writes;
        std::uint64_t now = 1'000 * kMs;
        a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
        a.Receive(proto::ObjectOwner{*a.IdOf(kAddrA), 1, 0, 0});
        proto::PlayerSample p2;
        p2.player_number = 2;
        p2.time = {now, 1};
        p2.scene = kScene;
        p2.position = V(1.0f);  // player 2 beside the enemy
        a.Receive(p2);
        now += 33 * kMs;
        a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
        // Player 2's puppet's sword hits here (shown 100 ms late)...
        owner.life = 2;
        now += 33 * kMs;
        writes.clear();
        a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
        // ...and player 2's DAMAGE for the same swing comes.
        proto::Damage d;
        d.player_number = 2;
        d.id = *a.IdOf(kAddrA);
        d.amount = 1;
        a.Receive(d);
        now += 33 * kMs;
        a.Tick(kScene, 1, now, {owner.Walk()}, send, writes);
        const bool hurt = std::any_of(writes.begin(), writes.end(),
                                      [](const LifeWrite& w) { return w.address == kAddrA && w.life < 2; });
        CHECK(hurt == !dedupe);
        const auto notes = a.TakeNotes();
        const bool noted = std::any_of(notes.begin(), notes.end(), [](const std::string& n) {
            return n.find("not given again") != std::string::npos;
        });
        CHECK(noted == dedupe);
    }
}

TEST_CASE("golpes: copies through the smoother follow the owner and snap on a teleport") {
    Sim sim(true, true);
    sim.owner.walk = 3.0f;
    sim.Start();
    sim.RunTo(2'000 * kMs);
    // Drawn 100 ms behind the owner's enemy, without jumps.
    CHECK(sim.MaxStep(500 * kMs, 2'000 * kMs) < 0.07f);
    CHECK(sim.frames.back().pose.has_value());
    const float shown_z = (*sim.frames.back().pose)[2];
    CHECK(shown_z == doctest::Approx(sim.owner.z - 3.0f * 0.1f).epsilon(0.05));
    // The owner's enemy is put 10 m away at once (a teleport): drawn there, not slid.
    sim.owner.x += 10.0f;
    sim.RunTo(3'000 * kMs);
    bool slid = false;
    for (const Frame& f : sim.Between(2'000 * kMs, 3'000 * kMs)) {
        slid = slid || (f.pose && (*f.pose)[0] > 1.0f && (*f.pose)[0] < 9.0f);
    }
    CHECK_FALSE(slid);
    CHECK((*sim.frames.back().pose)[0] == doctest::Approx(10.0f));
}
