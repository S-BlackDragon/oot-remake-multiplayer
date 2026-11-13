// ORM: whether a world flag moves its actor live, on the next load, or not at all (an
//   unexplained change: a key the list misses). Described in coop/host/live_check.hpp.
// Doc: oot-remake docs/RIESGOS_RESUELTOS.md ("Mundo e historia"), docs/CANCIONES.md.
// Switch: LICHII_ORM_LIVE_CHECK=0 off (the hook side, lichii/hook/coop_songs_game.cpp).
// Test: tests/host/live_check_test.cpp.

#include "coop/host/live_check.hpp"

#include <cstdio>

#include "coop/host/scene.hpp"

namespace coop::host {
namespace {

std::size_t count_of(const std::vector<SeenActor>& actors, std::string_view part) {
  std::size_t n = 0;
  for (const auto& a : actors) {
    if (a.name.find(part) != std::string::npos) ++n;
  }
  return n;
}

std::string seconds(std::uint64_t us) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f", static_cast<double>(us) / 1e6);
  return text;
}

}  // namespace

std::string live_key_text(const FlagKey& key) {
  char text[40];
  if (key.kind == protocol::FlagKeyKind::key64) {
    std::snprintf(text, sizeof text, "key64:0x%016llx", static_cast<unsigned long long>(key.key));
  } else {
    std::snprintf(text, sizeof text, "flag 0x%08x", static_cast<unsigned>(key.hash));
  }
  return text;
}

std::string_view LiveCheck::verdict(std::size_t watch) const {
  return watch < states_.size() ? std::string_view(states_[watch].verdict) : std::string_view();
}

void LiveCheck::note_key(std::size_t i, State& st, const FlagKey& key, std::uint32_t before, std::uint32_t after,
                         const LiveView& view) {
  const LiveWatch& w = watches_[i];
  const std::string what = w.id + ": " + live_key_text(key) + " " + std::to_string(before) + " -> " +
                           std::to_string(after);
  st.key_changed_us = view.now_us;
  st.key_changed_once = true;
  if (st.odd && st.in_place) {
    // The actors went first and the key came within kAnswerUs: live after all.
    st.verdict = "live";
    notes_.push_back(what + ": live: " + std::to_string(st.odd->before) + " -> " + std::to_string(st.odd->after) +
                     " " + w.actor + " actor(s) in " + *view.scene + " (the actors went first)");
    st.odd.reset();
    return;
  }
  st.odd.reset();
  if (!st.in_place) {
    st.load_check = true;
    notes_.push_back(what + ": " + w.place + " loads with it" +
                     (view.scene && !view.scene->empty() ? " (we are in " + *view.scene + ")" : std::string()));
    return;
  }
  const std::optional<std::size_t> now_count = st.count ? st.count : st.raw;
  if (!now_count) {
    st.verdict = "unknown";
    notes_.push_back(what + ": unknown, no actor walk to see it (LICHII_COOP_OBJECTS off?); " + w.place +
                     " takes it on its next load in any case");
    return;
  }
  st.pending = State::Pending{view.now_us, *now_count, live_key_text(key)};
}

void LiveCheck::tick(const LiveView& view, FlagStore& store) {
  const std::uint64_t now = view.now_us;
  if (view.animation) {
    for (const auto& w : watches_) {
      if (!w.cause.empty() && view.animation->starts_with(w.cause)) {
        cause_us_ = now;
        break;
      }
    }
  }
  const std::string scene = view.scene.value_or(std::string());
  for (std::size_t i = 0; i < watches_.size(); ++i) {
    const LiveWatch& w = watches_[i];
    State& st = states_[i];
    const bool in_place = !scene.empty() && place_of(scene) == w.place;

    // The count of watched actors in our scene, taken once it held kSteadyUs (a walk mid-load).
    const bool new_scene = scene != st.scene;
    if (new_scene || !in_place) {
      st.count.reset();
      st.raw.reset();
      st.pending.reset();
      st.odd.reset();
      st.scene = scene;
      st.fresh = in_place;  // just in: its first steady count is the room as it loaded
    }
    st.in_place = in_place;
    if (in_place && view.actors) {
      const std::size_t n = count_of(*view.actors, w.actor);
      if (!st.raw || *st.raw != n) {
        st.raw = n;
        st.raw_since_us = now;
      }
      if (now - st.raw_since_us >= kSteadyUs) {
        const std::optional<std::size_t> before = st.count;
        st.count = n;
        if (st.fresh && st.load_check) {
          st.load_check = false;
          notes_.push_back(w.id + ": loaded: " + scene + " came in with " + std::to_string(n) + " " + w.actor +
                           " actor(s), as its flags say now");
        }
        st.fresh = false;
        if (before && *before != n && !st.pending) {
          // A change with no key yet: a key may still come (ours is read a little later).
          const bool keyed = st.key_changed_once && now - st.key_changed_us <= kAnswerUs;
          if (!keyed) st.odd = State::Odd{now, *before, n};
        }
      }
    } else if (in_place && !view.actors && !st.no_walk_noted && !scene.empty()) {
      st.no_walk_noted = true;
      notes_.push_back(w.id + ": no actor walk in " + scene +
                       " (LICHII_COOP_OBJECTS off?): its flags are written, nobody sees whether they show live");
    }

    // The keys: our game's values, ours or written for another player.
    for (const auto& key : w.keys) {
      const auto e = store.read(key);
      if (!e) continue;
      const std::uint32_t v = e->value;
      const auto it = st.values.find(key);
      if (it == st.values.end()) {
        st.values.emplace(key, v);  // first read: where it starts
        continue;
      }
      if (it->second == v) continue;
      const std::uint32_t before = it->second;
      it->second = v;
      note_key(i, st, key, before, v, view);
    }

    if (st.pending) {
      if (st.count && *st.count != st.pending->before) {
        st.verdict = "live";
        notes_.push_back(w.id + ": live: " + std::to_string(st.pending->before) + " -> " + std::to_string(*st.count) +
                         " " + w.actor + " actor(s) in " + scene + " " +
                         seconds(now - st.pending->since_us) + " s after " + st.pending->key);
        st.pending.reset();
      } else if (now - st.pending->since_us >= kAnswerUs) {
        st.verdict = "not live";
        st.load_check = true;
        notes_.push_back(w.id + ": not live: still " + std::to_string(st.pending->before) + " " + w.actor +
                         " actor(s) in " + scene + " " + seconds(kAnswerUs) + " s after " + st.pending->key +
                         "; it changes when " + w.place + " loads");
        st.pending.reset();
      }
    }
    if (st.odd && now - st.odd->since_us >= kAnswerUs) {
      const bool caused = w.cause.empty() || (cause_us_ && now - *cause_us_ <= kCauseUs + kAnswerUs);
      if (caused) {
        notes_.push_back(w.id + ": unexplained: " + std::to_string(st.odd->before) + " -> " +
                         std::to_string(st.odd->after) + " " + w.actor + " actor(s) in " + scene +
                         " with no listed key changing" + (w.cause.empty() ? "" : " after our " + w.cause) +
                         ": the list misses its key (look for the flag that changed then, LICHII_COOP_DEBUG_FLAGS=1)");
      }
      st.odd.reset();
    }
  }
}

}  // namespace coop::host
