#pragma once

// ORM, puppet smoothing (oot-remake docs/SUAVIZADO.md; PLAN_100 E7). I saw my friend
// "walking when he was running" and jerky puppets. Where each remote Link is drawn, from its
// PLAYER_SAMPLEs, and which of its body poses (PLAYER_POSE) is shown with it:
//
// - An interpolation buffer: the puppet is drawn `delay_us` (100 ms) behind the receiver's
//   estimate of the server clock, between the two samples around that time (cubic Hermite with
//   the sender's velocities, tangents bounded so a stop does not overshoot; straight otherwise).
//   Samples are ordered by their server time and sequence, so jitter, bursts and reordering on
//   the wire do not show.
// - Bounded extrapolation: past the newest sample the position is carried on with the sender's
//   velocity (dead reckoning) for at most `extrapolate_us` (150 ms), then held still.
// - No jerk when the guess was wrong: when a sample arrives late (after extrapolating past it,
//   a stop, a turn) or the stream resumes after a gap, the jump in what is shown is spread over
//   `correct_us` (100 ms) instead of drawn in one frame.
// - Snap on teleport or scene change: two samples further apart than `snap_distance` and than
//   `max_speed` allows, or with another scene, are never joined; when the shown time reaches the
//   second one the puppet is drawn there at once (`SmoothedPose::snapped`), as is any correction
//   larger than `snap_distance`.
// - The render clock never goes back (small backward steps of the clock estimate are held), so
//   the puppet never walks backwards for a frame.
// - Animation from the sender: the AS command (name, frame, rate) and the speed shown with a
//   position are those of the sample at the shown time, as the sender sent them, and the gait
//   (still, walk, run) comes from the sender's command name or the sender's own velocity. Never
//   from the speed the smoothing draws (a held or corrected puppet "slows down" while its player
//   runs). Walk and run are one command, `Move` (R1.6 HALLAZGOS 2026-11-10): the game's blend
//   tree picks walk or run by speed, so `Move` is classed by the sender's speed.
//
// `Timeline<T>` is the same buffer for any timed value (the body poses of coop_pose: the hook
// blends the two it returns), so the body pose and the position are shown for the same time.
//
// Plain data in, plain data out: no game addresses, no sockets, no clocks of its own. The hook
// feeds it every PLAYER_SAMPLE from the network thread and asks it each calc frame.
//
// Doc: oot-remake docs/SUAVIZADO.md.
// Switch: LICHII_ORM_SMOOTH (on by default; LICHII_ORM_SMOOTH=0 turns it off: the puppet is
//   placed as before, from presence on the present thread, and the body poses by arrival time).
// Test: tests/host/pose_smooth_test.cpp.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "coop/protocol/messages.hpp"

namespace coop::host {

struct SmoothSettings {
  // How far behind the server clock estimate the puppet is drawn. Bigger hides more jitter and
  // loss; smaller is less late. The same as presence's show delay.
  std::uint64_t delay_us = 100'000;
  // Past the newest sample, how long the position is carried on with its velocity; then held.
  std::uint64_t extrapolate_us = 150'000;
  // Two samples further apart in time than this are not joined: the first is shown (carried
  // on, then held) until the second, and the step to it is corrected over correct_us.
  std::uint64_t max_gap_us = 500'000;
  // A step between two samples longer than this (game units; about metres) and faster than
  // max_speed is a teleport: snapped, never drawn as a slide.
  float snap_distance = 2.0f;
  float max_speed = 40.0f;  // game units a second
  // A jump in what is shown (a late sample, the end of an extrapolation) is spread over this.
  std::uint64_t correct_us = 100'000;
  // The clock estimate going back further than this is a new clock (a reconnect): the render
  // clock follows it and the puppet snaps. Smaller steps back are held.
  std::uint64_t clock_back_us = 1'000'000;
  // Cubic Hermite with the samples' velocities when both have one; else a straight line.
  bool hermite = true;
  std::size_t capacity = 32;  // samples kept per player (half a second at 60 a second)
};

enum class Gait : std::uint8_t {
  unknown,  // no command and no velocity to tell
  still,
  walk,
  run,
  other,  // a command that is not walking or running (attack, roll, climb, ...)
};
const char* to_string(Gait gait);

struct GaitSettings {
  // Horizontal speed of the sender (game units a second) for a blend command such as `Move`.
  // To be measured in a game (LICHII_COOP_DEBUG_ANIM logs both); about metres a second.
  float still_below = 0.3f;
  float run_from = 3.5f;
};

// The x and z of a velocity (y is up), as a length.
float horizontal_speed(const protocol::Vec3& v);

// The gait of a sender's AS command: by its words ("Run", "Dash" -> run; "Walk" -> walk; "Wait",
// "Idle", "Stand" -> still; "Move" and the like -> by `sender_speed`), else other. Words are the
// command's pieces split at '_' and at capitals ("RunStart_L" is Run, Start, L). No speed
// (nothing sent) for a blend command: unknown.
Gait gait_of(std::string_view as_command, std::optional<float> sender_speed,
             const GaitSettings& settings = {});

// What the AS fallback (a puppet without poses) should do with a wanted command: play it (a new
// name, or the same name at another rate: the rate is set only by a play), set its frame (a new
// sample and the puppet's frame is more than frame_slack off), or nothing.
enum class AnimStep : std::uint8_t { none, play, set_frame };
struct AnimPlayed {
  std::string name;
  float rate = 1.0f;
};
struct AnimSlack {
  float frame = 3.0f;
  float rate = 0.05f;  // absolute
};
AnimStep next_anim_step(const std::optional<AnimPlayed>& played, const protocol::Animation& wanted,
                        std::optional<float> puppet_frame, bool new_sample,
                        const AnimSlack& slack = {});

// One sample as the smoother keeps it.
struct SmoothSample {
  protocol::SampleTime time;
  protocol::Vec3 position{};
  std::optional<protocol::Vec3> velocity;  // game units a second, as sent
  std::optional<protocol::Quat> rotation;
  std::optional<std::string> scene;
  std::optional<protocol::Animation> animation;
};
// Nothing for a sample without a position (scene and age only).
std::optional<SmoothSample> smooth_sample_of(const protocol::PlayerSample& sample);

enum class SmoothHow : std::uint8_t {
  early,         // the shown time is before every sample: the oldest is shown
  interpolated,  // between two samples
  extrapolated,  // past the newest (or before a gap), carried on with its velocity
  held,          // past the extrapolation: still
};
const char* to_string(SmoothHow how);

struct SmoothedPose {
  protocol::Vec3 position{};
  std::optional<protocol::Quat> rotation;  // the newest up to the shown time; not extrapolated
  SmoothHow how = SmoothHow::interpolated;
  // Draw it here at once: the first pose, a teleport or scene change reached, a correction
  // larger than snap_distance, or the clock started over.
  bool snapped = false;
  std::uint64_t render_us = 0;  // the server time shown
  std::uint64_t ahead_us = 0;   // past the newest sample (extrapolated or held)
  // The sender's, from the newest sample at or before the shown time that carried them.
  std::optional<protocol::Animation> animation;
  std::uint32_t animation_sequence = 0;  // that sample's sequence: a new one is a new sample
  std::optional<float> sender_speed;     // horizontal, from that sample's velocity as sent
  Gait gait = Gait::unknown;
};

enum class SmoothPush : std::uint8_t {
  buffered,
  cut,        // buffered, and a teleport or scene change from the sample before it
  duplicate,  // that sequence is already buffered
  too_old,    // older than the shown time and than the newest sample, or than a full buffer
  invalid,    // a NaN or infinite float, or a rotation of length 0
};

class PoseSmoother {
 public:
  explicit PoseSmoother(SmoothSettings settings = {}, GaitSettings gait = {});

  const SmoothSettings& settings() const { return settings_; }
  // ORM riesgos-visual: the delay changed for everyone (DelayTuner). A longer
  // one holds the drawing for its difference (the render clock never goes
  // back); a shorter one catches up through the correction.
  void set_delay_us(std::uint64_t delay_us) { settings_.delay_us = delay_us; }

  SmoothPush push(const SmoothSample& sample);
  // What to draw at `server_now_us` (the receiver's estimate of the server clock), the delay
  // taken off here. Advances the render clock. Nothing before any sample.
  std::optional<SmoothedPose> at(std::uint64_t server_now_us);
  // A player that left, a reconnect: everything forgotten.
  void reset();

  std::size_t size() const { return buffer_.size(); }

  // Counts since the last take_stats(), for a log line a second.
  struct Stats {
    std::uint32_t buffered = 0;
    std::uint32_t reordered = 0;  // buffered before a newer one already there
    std::uint32_t late = 0;       // buffered although its time was already shown
    std::uint32_t duplicate = 0;
    std::uint32_t too_old = 0;
    std::uint32_t invalid = 0;
    std::uint32_t cuts = 0;
    std::uint32_t frames = 0;
    std::uint32_t extrapolated = 0;  // frames
    std::uint32_t held = 0;          // frames
    std::uint32_t corrections = 0;   // jumps spread over correct_us
    std::uint32_t snaps = 0;
    std::uint64_t max_ahead_us = 0;
  };
  Stats take_stats();

 private:
  struct Entry {
    SmoothSample sample;
    bool cut = false;     // a teleport or scene change from the entry before
    bool joined = false;  // drawn as a line from the entry before (not cut, gap small enough)
  };
  struct Raw {
    protocol::Vec3 position{};
    std::optional<protocol::Quat> rotation;
    SmoothHow how = SmoothHow::interpolated;
    std::size_t a = 0;  // the entry at or before the shown time (0 when early)
    std::uint64_t ahead_us = 0;
  };

  void relink();
  std::optional<Raw> evaluate(std::uint64_t render_us) const;
  protocol::Vec3 reckon(std::size_t i, std::uint64_t at_us) const;  // carried on from entry i
  protocol::Vec3 offset_at(std::uint64_t render_us) const;

  SmoothSettings settings_;
  GaitSettings gait_;
  std::deque<Entry> buffer_;  // by server time, then sequence
  std::optional<std::uint64_t> render_us_;
  std::optional<protocol::Vec3> last_raw_;  // at render_us_, before the correction
  bool dirty_ = false;  // a sample was buffered since the last at()
  protocol::Vec3 offset_{};
  std::uint64_t offset_from_us_ = 0;
  Stats stats_;
};

// The same buffer for any value with a sample time (coop_pose's body poses): ordered by server
// time and sequence, duplicates dropped, at most `capacity`. at() gives the entry at or before
// the shown time and, while the next one is no further than max_gap_us from it, that one and
// how far between them (0 to 1). The caller blends.
template <typename T>
class Timeline {
 public:
  explicit Timeline(std::size_t capacity = 8) : capacity_(std::max<std::size_t>(capacity, 1)) {}

  enum class Push : std::uint8_t { buffered, duplicate, too_old };
  Push push(const protocol::SampleTime& time, T value) {
    for (const Item& i : items_) {
      if (i.time.sequence == time.sequence && i.time.server_time_us == time.server_time_us) {
        return Push::duplicate;
      }
    }
    if (items_.size() >= capacity_ && earlier(time, items_.front().time)) return Push::too_old;
    auto at = std::upper_bound(items_.begin(), items_.end(), time,
                               [](const protocol::SampleTime& key, const Item& x) {
                                 return earlier(key, x.time);
                               });
    items_.insert(at, Item{time, std::move(value)});
    while (items_.size() > capacity_) items_.pop_front();
    return Push::buffered;
  }

  struct View {
    const T* a = nullptr;  // always set
    const T* b = nullptr;  // set while between two joined entries
    float t = 0.0f;        // from a to b
    bool early = false;    // the shown time is before every entry: a is the oldest
    std::uint64_t ahead_us = 0;  // past a, when there is no b
  };
  std::optional<View> at(std::uint64_t render_us, std::uint64_t max_gap_us) const {
    if (items_.empty()) return std::nullopt;
    View v;
    if (render_us < items_.front().time.server_time_us) {
      v.a = &items_.front().value;
      v.early = true;
      return v;
    }
    std::size_t i = 0;
    while (i + 1 < items_.size() && items_[i + 1].time.server_time_us <= render_us) ++i;
    v.a = &items_[i].value;
    if (i + 1 < items_.size()) {
      const std::uint64_t t0 = items_[i].time.server_time_us;
      const std::uint64_t gap = items_[i + 1].time.server_time_us - t0;  // > 0 here
      if (gap <= max_gap_us) {
        v.b = &items_[i + 1].value;
        v.t = static_cast<float>(static_cast<double>(render_us - t0) / static_cast<double>(gap));
        return v;
      }
    }
    v.ahead_us = render_us - items_[i].time.server_time_us;
    return v;
  }

  void clear() { items_.clear(); }
  bool empty() const { return items_.empty(); }
  std::size_t size() const { return items_.size(); }
  const T& newest() const { return items_.back().value; }
  const protocol::SampleTime& newest_time() const { return items_.back().time; }

 private:
  struct Item {
    protocol::SampleTime time;
    T value;
  };
  static bool earlier(const protocol::SampleTime& a, const protocol::SampleTime& b) {
    if (a.server_time_us != b.server_time_us) return a.server_time_us < b.server_time_us;
    return a.sequence < b.sequence;
  }
  std::size_t capacity_;
  std::deque<Item> items_;
};

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md, SUAVIZADO.md
// "Decisiones": a fixed 100 ms, "adaptarlo ... si el registro muestra muchos
// carried on"). The delay is measured now: each player's 10 s window of the
// smoother's counts (frames drawn, frames carried on or held past the
// newest sample) goes in; a window with more than `up_share` of its frames
// late raises the delay for everyone by `step` (at most `max_us`); `calm`
// windows in a row under `down_share` lower it by `step` (never under
// `min_us`). A window with fewer than `min_frames` frames, or whose sender
// sent fewer samples than half its frames (a load, a pause: his silence, not
// the network), says nothing. One delay for all, so every puppet, its bones
// and a horse under it are drawn for the same time.
struct DelayTunerSettings {
  std::uint64_t min_us = 100'000;
  std::uint64_t max_us = 200'000;
  std::uint64_t step_us = 50'000;
  double up_share = 0.05;
  double down_share = 0.01;
  std::uint32_t calm = 6;  // windows in a row (a minute with one player)
  std::uint32_t min_frames = 60;
};

class DelayTuner {
 public:
  explicit DelayTuner(DelayTunerSettings settings = {}) : settings_(settings), delay_us_(settings.min_us) {}

  enum class Change : std::uint8_t { none, up, down, kept };
  struct Verdict {
    Change change = Change::none;  // none: the window said nothing
    std::uint64_t delay_us = 0;    // the delay from now
    double share = 0.0;            // that window's late frames / frames
  };
  // One player's window (PoseSmoother::Stats of 10 s).
  Verdict feed(const PoseSmoother::Stats& window);
  std::uint64_t delay_us() const { return delay_us_; }
  const DelayTunerSettings& settings() const { return settings_; }

 private:
  DelayTunerSettings settings_;
  std::uint64_t delay_us_;
  std::uint32_t calm_ = 0;
};

}  // namespace coop::host
