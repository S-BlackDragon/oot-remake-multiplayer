// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM golpes (oot-remake docs/GOLPES_LATENCIA.md; PLAN_100 E7): a player hitting an enemy that
// another game owns, with about 100 ms of network each way (the tunnel). The owner alone applies
// damage (docs/objects.md, "Damage"): the hit goes to it as DAMAGE and its answer comes back in
// its samples one round trip later. Before this, the hitter saw its own game's flinch and then:
// the copy pulled back to the owner's place the same frame (no knockback), the owner's older
// animations played over the flinch after 700 ms, the owner's own reaction played a second time
// when its answer came, a killed copy kept walking with the owner's enemy until the owner's death
// came back, and a slow answer (over 1 s) healed the copy and hit it again. What this does,
// without moving any authority (the owner still applies every hit and decides the life):
//
// - Feedback, on the hitter: right after our hit on a copy the copy is left to its own game for
//   `free_us` (the game's own knockback, flinch, flash and sound show at once), then held where
//   that left it until the owner's answer is on screen (its sample showing the hit, shown
//   `behind_us` late like every copy) and the owner's own knockback has come near it, then
//   blended back onto the owner's stream over `blend_us`. The owner's animations from before it knew of the hit, the animation the copy
//   was in when hit, and the owner's own hit reaction are not played over ours. A copy our hit
//   killed is left to its own game (it dies here at once) and is not moved again; if the owner
//   says it lives (Sync writes its life back), it is blended back onto the owner's stream.
// - The hit's dedupe window, on the hitter: a sample the owner took before our DAMAGE could
//   have reached it (`Before`) never heals our copy, whatever the latency; measured from the
//   samples themselves (arrival minus their server time), so a slow tunnel holds longer and a
//   fast LAN no longer than before.
// - On the owner (`OwnerLedger`): a life drop on an enemy we own with our Link out of reach and
//   exactly one other player beside it, next to a DAMAGE from that player, may be the same hit
//   twice (that player's puppet weapon hitting here, if the game lets it). Always logged; with
//   SetPutBack the second one is taken back (LICHII_COOP_HITS_DEDUPE=1, off by default until a
//   run shows whether puppet weapons hit at all).
//
// Plain data: no emulator code, no clocks of its own (server time in, as Sync has it). Present
// thread only. Switches: LICHII_COOP_HITS (on; =0 the hitter side is as before),
// LICHII_COOP_HITS_DEDUPE (off). Log: `coop: objects: share hits: ...`.
// Test: tests/lichii/hits_test.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace CoopHits {

using Vec3 = coop::protocol::Vec3;
using Quat = coop::protocol::Quat;

// An enemy's hit reaction animation: a word Damage, Dmg, Hit, Flinch, Knockback, KnockBack,
// Blow, Stagger, Recoil or Hurt at the start or after '_' (LOD suffix off), never a death
// animation ("Damage_Death" is a death: CoopObjects::IsDeathAnimation).
bool IsHitReaction(std::string_view name);

// The one-way delay from an owner to us, from its samples: arrival (our server clock) minus
// the sample's server time. Rises fast, falls slowly, so a spike holds for a while.
class Latency {
public:
    static constexpr std::uint64_t kDefaultUs = 50'000;  // before any sample
    static constexpr std::uint64_t kMostUs = 1'000'000;  // more is a clock problem: capped
    void Note(std::uint64_t sample_us, std::uint64_t now_us);
    std::uint64_t OneWayUs() const;
    bool Known() const { return known_; }
    void Clear() {
        known_ = false;
        value_ = 0.0;
    }

private:
    bool known_ = false;
    double value_ = 0.0;
};

struct Settings {
    std::uint64_t behind_us = 100'000;  // copies show the owner this late (Sync::kBehindUs)
    std::uint64_t free_us = 300'000;    // our game alone moves the copy after our hit
    std::uint64_t blend_us = 200'000;   // back onto the owner's stream
    // The owner's answer, at worst: its hurt waits up to 400 ms (Sync::kHurtWaitUs), then its
    // next sample (Sync::kSampleEveryUs).
    std::uint64_t owner_apply_us = 450'000;
    // ... and as usual: the game takes the hit next frame, a sample follows.
    std::uint64_t owner_react_us = 50'000;
    std::uint64_t margin_us = 100'000;    // clocks and walks
    std::uint64_t react_min_us = 700'000;  // never shorter than Sync::kReactHoldUs was
    std::uint64_t reaction_us = 600'000;   // how long an owner's hit reaction plays
    std::uint64_t react_most_us = 2'500'000;
    std::uint64_t hold_most_us = 1'500'000;  // a hold never longer than this
    // Once the owner's answer is on screen its own knockback plays in its stream: the hold
    // ends when that stream comes within converge_m of where ours left the copy, or
    // converge_most_us after the answer showed, so the blend is short and does not dip back.
    float converge_m = 0.25f;
    std::uint64_t converge_most_us = 400'000;
    std::uint64_t kill_most_us = 6'000'000;  // a kill here nobody answered: back to the owner
    std::uint64_t window_most_us = 3'000'000;  // the dedupe window never longer
};

enum class Phase : std::uint8_t {
    follow,  // the owner's stream places it (blend done; plays may still be filtered)
    free,    // our game moves it (just hit here)
    hold,    // held where our game's reaction left it, until the owner's answer shows
    blend,   // from there back onto the owner's stream
    dead,    // killed here and the owner agrees: our game's death plays, nothing moves it
};
const char* ToString(Phase phase);

// What to write into a copy this frame.
struct PoseOut {
    bool write = true;
    Vec3 position{};
    std::optional<Quat> rotation;
};

class Feedback {
public:
    explicit Feedback(Settings settings = {}) : settings_(settings) {}
    const Settings& settings() const { return settings_; }
    Latency& latency() { return latency_; }
    const Latency& latency() const { return latency_; }

    // Our hit on a copy we do not own: the walk saw its life fall to `life` (0 or less: killed)
    // at `now_us`, standing at `at` facing `rotation`; `over` is the owner's latest animation.
    void OurHit(std::uint64_t address, std::uint64_t now_us, std::int32_t life, const Vec3& at,
                std::optional<Quat> rotation, std::string over);
    // Our DAMAGE for it left at `now_us`.
    void Sent(std::uint64_t address, std::uint64_t now_us);
    // The owner's sample for it, taken at server time `sample_us`, says `life`.
    void OwnerLife(std::uint64_t address, std::uint64_t sample_us, std::int32_t life);
    // Our copy was written back up to the owner's life: the owner did not take our hit.
    void Rejected(std::uint64_t address, std::uint64_t now_us);

    // The dedupe window: a sample the owner took at `sample_us` cannot show our last DAMAGE
    // yet (taken before it could have arrived and been applied, at worst), so its higher life
    // is no reason to heal our copy. False with no DAMAGE sent, and after window_most_us.
    bool Before(std::uint64_t address, std::uint64_t sample_us, std::uint64_t now_us) const;

    // Every frame, for a copy the owner's stream would put at `target`: what to write. `here`
    // is where our game has it now (the last walk). May change the phase (time-driven, so a
    // second call with the same `now_us` gives the same answer).
    PoseOut Pose(std::uint64_t address, std::uint64_t now_us, const Vec3& target,
                 std::optional<Quat> target_rotation, const Vec3& here,
                 std::optional<Quat> here_rotation);
    // Whether the owner's animation `name`, from its sample taken at `sample_us`, must not be
    // played on the copy now (our game's reaction or death shows instead).
    bool SkipPlay(std::uint64_t address, std::uint64_t now_us, std::string_view name,
                  std::uint64_t sample_us);
    // Our hit's feedback is on for it (the wake rule leaves it alone meanwhile).
    bool Active(std::uint64_t address, std::uint64_t now_us) const;
    std::optional<Phase> PhaseOf(std::uint64_t address) const;

    // Ours now, gone, another actor at that address.
    void Forget(std::uint64_t address) { entries_.erase(address); }
    void Clear() {
        entries_.clear();
        latency_.Clear();
    }
    // Present thread, once per walk: entries done, or of actors no longer loaded, go.
    void Expire(std::uint64_t now_us, const std::function<bool(std::uint64_t)>& loaded);
    std::size_t Size() const { return entries_.size(); }

    // How long after our hit the owner's answer is on our screen, usually and at worst.
    std::uint64_t AnswerUs() const;
    std::uint64_t WorstAnswerUs() const;

    struct Stats {
        std::uint32_t hits = 0;       // our hits on copies
        std::uint32_t kills = 0;      // of them, kills shown here at once
        std::uint32_t answered = 0;   // the owner's sample showed it
        // From our hit to the owner's sample that showed it (server time): one way, the
        // owner's game taking it, its next sample.
        std::uint64_t answer_sum_us = 0;
        std::uint32_t timed_out = 0;  // the hold ended with no answer
        std::uint32_t rejected = 0;   // written back up
        std::uint32_t plays_skipped = 0;
    };
    Stats TakeStats();

private:
    struct Entry {
        std::uint64_t hit_us = 0;
        std::uint64_t sent_us = 0;
        std::int32_t life = 0;
        bool kill = false;
        bool rejected = false;
        std::string over;
        Phase phase = Phase::free;
        Vec3 hold{};
        std::optional<Quat> hold_rotation;
        std::uint64_t hold_since = 0;
        std::uint64_t blend_since = 0;
        std::uint64_t answered_sample_us = 0;  // 0: no sample showed our hit yet
        bool moved_on = false;  // the owner's stream played something else after its answer
    };
    void Advance(Entry& e, std::uint64_t now_us, const Vec3& target, const Vec3& here,
                 std::optional<Quat> here_rotation);
    std::uint64_t ReactEnd(const Entry& e) const;
    std::uint64_t WindowUs() const;

    Settings settings_;
    Latency latency_;
    std::map<std::uint64_t, Entry> entries_;  // by actor address
    Stats stats_;
};

// Owner side: drops on an enemy we own that another player's hit may explain, and DAMAGEs, by
// enemy, kept for `window_us` each way.
class OwnerLedger {
public:
    static constexpr std::uint64_t kWindowUs = 600'000;
    static constexpr float kReachM = 4.0f;  // a sword's reach and a step
    void SetPutBack(bool on) { put_back_ = on; }
    bool PutBack() const { return put_back_; }

    // The player a drop at `at` is attributable to: our Link (`self_at`, nothing: unknown) is
    // further than kReachM from it and exactly one of `others` (player, place) is within it.
    static std::optional<std::uint8_t> Near(const Vec3& at, std::optional<Vec3> self_at,
                                            const std::vector<std::pair<std::uint8_t, Vec3>>& others);

    // A drop of `amount` on our enemy at `address`, attributable to `player`. Returns how much
    // of it to put back (written up again): the part a DAMAGE from that player already gave,
    // when put-back is on; else 0 (noted for the log either way).
    std::int32_t Drop(std::uint64_t address, std::uint64_t now_us, std::int32_t amount, std::uint8_t player);
    // A DAMAGE of `amount` from `player` for our enemy at `address`. Returns how much of it to
    // apply: less the part a drop attributable to that player already took, when put-back is
    // on; else all of it.
    std::int32_t Damage(std::uint64_t address, std::uint64_t now_us, std::int32_t amount, std::uint8_t player);
    void Forget(std::uint64_t address) { entries_.erase(address); }
    void Clear() { entries_.clear(); }
    void Expire(std::uint64_t now_us, const std::function<bool(std::uint64_t)>& loaded);

    // Same-hit pairs seen (and taken back) since the last ask.
    std::uint32_t TakePairs() { return std::exchange(pairs_, 0u); }
    std::uint32_t TakeTakenBack() { return std::exchange(taken_back_, 0u); }

private:
    struct Part {
        std::uint8_t player = 0;
        std::int32_t amount = 0;
        std::uint64_t until = 0;
    };
    struct Entry {
        std::vector<Part> drops;    // attributable drops not yet matched
        std::vector<Part> damages;  // DAMAGEs not yet matched
    };
    static std::int32_t Match(std::vector<Part>& parts, std::uint64_t now_us, std::uint8_t player,
                              std::int32_t amount);
    bool put_back_ = false;
    std::map<std::uint64_t, Entry> entries_;
    std::uint32_t pairs_ = 0;
    std::uint32_t taken_back_ = 0;
};

}  // namespace CoopHits
