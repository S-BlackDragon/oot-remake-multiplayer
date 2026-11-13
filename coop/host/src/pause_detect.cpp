// ORM: our own pause from Sequence_IsPause, or from a frozen frame when the flag is not to be
//   trusted. Described in coop/host/pause_detect.hpp.
// Doc: oot-remake docs/MENUS_PAUSA.md, docs/RIESGOS_RESUELTOS.md ("Mundo e historia").
// Switch: LICHII_ORM_PAUSE_DETECT=flag (the flag alone).
// Test: tests/host/pause_detect_test.cpp.

#include "coop/host/pause_detect.hpp"

namespace coop::host {

const char* to_string(PauseSource source) {
  switch (source) {
    case PauseSource::flag_or_frozen: return "Sequence_IsPause or a frozen frame";
    case PauseSource::flag: return "Sequence_IsPause";
    case PauseSource::frozen: return "a frozen frame";
  }
  return "?";
}

void PauseDetect::switch_to(PauseSource s, const std::string& why, PauseOutput& out) {
  if (s == source_) return;
  source_ = s;
  out.log.push_back(std::string("pause detector: ") + to_string(s) + " (" + why + ")");
}

PauseOutput PauseDetect::frame(const PauseInput& in) {
  PauseOutput out;
  const std::uint64_t now = in.now_us;
  if (!announced_) {
    announced_ = true;
    if (flag_only_) {
      source_ = PauseSource::flag;
      out.log.push_back("pause detector: Sequence_IsPause alone (LICHII_ORM_PAUSE_DETECT=flag)");
    } else if (flag_first_) {
      out.log.push_back(
          "pause detector: Sequence_IsPause, a frozen frame only while it cannot be read");
    } else {
      out.log.push_back(
          "pause detector: Sequence_IsPause or a frozen frame, until the flag is seen set in a pause");
    }
  }
  if (flag_only_) {
    out.source = source_;
    out.is_pause = in.is_pause;
    return out;
  }

  // The frozen frame: our Link's place and AS command (name, frame, rate) the same for
  // kFrozenUs while the command runs (a rate: in play its frame moves every frame).
  const bool eligible = in.link && in.position && in.animation && in.animation->rate != 0.0f &&
                        !in.event_running && !in.loading_scene && !in.ocarina;
  bool frozen_now = false;
  if (eligible) {
    const bool same = last_pos_ && last_anim_ && *last_pos_ == *in.position && *last_anim_ == *in.animation;
    if (!same) same_since_us_ = now;
    last_pos_ = in.position;
    last_anim_ = in.animation;
    frozen_now = same && now - same_since_us_ >= kFrozenUs;
  } else {
    last_pos_.reset();
    last_anim_.reset();
  }
  if (frozen_now && !frozen_) {
    frozen_since_us_ = same_since_us_;
    flag_seen_in_freeze_ = false;
  }
  if (frozen_now && in.is_pause == std::optional<bool>(true)) flag_seen_in_freeze_ = true;
  if (!frozen_now && frozen_) {
    // A frozen pause ended: did the flag say it? (Only while it could be read.)
    const bool long_one = now - frozen_since_us_ >= kMissedPauseUs;
    if (long_one && !flag_seen_in_freeze_ && source_ == PauseSource::flag_or_frozen && in.is_pause) {
      ++missed_;
      if (missed_ >= kFlagMissed) {
        switch_to(PauseSource::frozen,
                  "Sequence_IsPause stayed 0 through " + std::to_string(missed_) + " frozen pauses", out);
      }
    }
  }
  frozen_ = frozen_now;

  // The flag: seen set once, it is trusted; unreadable with our Link in the world, the frozen
  // frame takes over.
  if (in.is_pause == std::optional<bool>(true)) {
    switch_to(PauseSource::flag, "set in a pause: trusted from now", out);
  } else if (flag_first_ && in.is_pause && source_ == PauseSource::frozen) {
    switch_to(PauseSource::flag, "readable again", out);  // flag first: back as soon as it reads
  }
  if (in.link && !in.is_pause) {
    if (!unreadable_since_us_) unreadable_since_us_ = now;
    if (now - *unreadable_since_us_ >= kUnreadableUs) {
      switch_to(PauseSource::frozen,
                "Sequence_IsPause not readable for " + std::to_string(kUnreadableUs / 1'000'000) + " s", out);
    }
  } else if (in.is_pause) {
    unreadable_since_us_.reset();
  }

  out.source = source_;
  out.frozen = frozen_now;
  switch (source_) {
    case PauseSource::flag: out.is_pause = in.is_pause; break;
    case PauseSource::frozen: out.is_pause = frozen_now; break;
    case PauseSource::flag_or_frozen:
      out.is_pause = frozen_now ? std::optional<bool>(true) : in.is_pause;
      break;
  }
  return out;
}

}  // namespace coop::host
