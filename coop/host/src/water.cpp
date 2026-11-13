// ORM water levels that change for everyone: the rules are described in coop/host/water.hpp.
// Doc: oot-remake docs/AGUA.md.
// Switch: LICHII_ORM_WATER=1 (off by default), LICHII_ORM_WATER_MAX_WAIT_S, LICHII_ORM_WATER_JABU=1.
// Test: tests/host/water_test.cpp.

#include "coop/host/water.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "coop/host/scene.hpp"

namespace coop::host {
namespace {

using protocol::FlagEntry;
using protocol::FlagKind;

// Read from the romfs (AGUA.md, section 1): each flag is bound to a water mover
// (PlgGdTable of the Banc, or a GroundControlActor_ResidentMoveTo node of the room's
// logic graph that reads it with HoldGameDataGetBoolSimple).
constexpr WaterBody kBodies[] = {
    {"water-temple", "WaterTemple", "the Water Temple's level (0 low, 1 middle, 2 high)",
     "WaterTemple_Room00_WaterLevel", 0xbeb09982u, true, 0, 2,
     {"WaterTemple_WaterLevelChange01", "WaterTemple_WaterLevelChange02", "WaterTemple_WaterLevelChange03"},
     {0xa16f6696u, 0x2a8640b7u, 0xa4456820u}, false},
    {"water-temple-room06", "WaterTemple", "the Water Temple's room 6 water", "WaterTemple_Room06_00",
     0x56bb0a9du, false, 0, 1, {}, {}, false},
    {"water-temple-room14", "WaterTemple", "the Water Temple's room 14 water", "WaterTemple_Room14_00",
     0xf7e49728u, false, 0, 1, {}, {}, false},
    {"deku-basement", "InsideTheDekuTree", "the Deku Tree basement water",
     "InsideTheDekuTree_RoomG_WaterLevel_timing", 0x392a228au, false, 0, 1, {}, {}, false},
    {"well-drain", "BottomOfTheWell", "the Bottom of the Well's room 1 drain (Zelda's Lullaby)",
     "BottomOfTheWell_Room_01_Blow_Ocarina", 0x7a8ac442u, false, 0, 1, {}, {}, false},
    {"well-down", "BottomOfTheWell", "the Bottom of the Well's room 1 water and falls",
     "BottomOfTheWell_Room_01_WaterLevel_Down", 0x5d3edb04u, false, 0, 1, {}, {}, false},
    {"forest-well", "ForestTemple", "the Forest Temple's well (room 7)", "ForestTemple_Room07_Cleard",
     0xeb6e7c9eu, false, 0, 1, {}, {}, false},
    {"jabu-room14", "InsideJabuJabuBelly", "Jabu-Jabu's room 14 water",
     "Gimmick_Insidejabujabu_Updown_MoveWater", 0xc517f1b5u, false, 0, 1, {}, {}, true},
    {"jabu-room14-walls", "InsideJabuJabuBelly", "Jabu-Jabu's room 14 low-water walls",
     "Gimmick_Insidejabujabu_down_MoveWater", 0x4358c335u, false, 0, 1, {}, {}, true},
};

constexpr WaterFlag kUnshared[] = {
    {"WaterTemple_WaterLevelChange01", 0xa16f6696u},
    {"WaterTemple_WaterLevelChange02", 0x2a8640b7u},
    {"WaterTemple_WaterLevelChange03", 0xa4456820u},
    {"WaterTemple_SkipWaterChange", 0xd6991798u},
};

std::string seconds(std::uint64_t us) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(us) / 1e6);
  return buf;
}

std::string value_text(const WaterBody& body, std::uint32_t bits) {
  return body.is_int ? std::to_string(static_cast<std::int32_t>(bits)) : std::to_string(bits != 0 ? 1 : 0);
}

}  // namespace

std::span<const WaterBody> water_bodies() { return kBodies; }

const WaterBody* water_body_of(std::uint32_t hash) {
  for (const WaterBody& b : kBodies) {
    if (b.hash == hash) return &b;
  }
  return nullptr;
}

std::span<const WaterFlag> water_unshared() { return kUnshared; }

WaterMergeReport merge_water(std::optional<SharedFlags>& shared, bool jabu) {
  WaterMergeReport report;
  if (!shared) return report;
  std::vector<FlagKey> keys;
  keys.reserve(shared->size() + std::size(kBodies));
  for (const FlagKey& k : shared->keys()) {
    const bool off = k.kind == protocol::FlagKeyKind::named && k.index == 0 &&
                     std::any_of(std::begin(kUnshared), std::end(kUnshared),
                                 [&](const WaterFlag& f) { return f.hash == k.hash; });
    if (off) {
      ++report.removed;
    } else {
      keys.push_back(k);
    }
  }
  for (const WaterBody& b : kBodies) {
    if (b.opt_in && !jabu) continue;
    const FlagKey key(b.hash, std::uint16_t{0});
    if (!shared->contains(key)) ++report.added;
    keys.push_back(key);
  }
  shared = SharedFlags(std::move(keys), shared->enums());
  return report;
}

bool water_unsafe_animation(std::string_view as_command) {
  constexpr std::string_view kPrefixes[] = {"Swim", "WaterDive", "Dive", "Fall", "Jump",
                                            "Climb", "Ladder", "Hang", "Float"};
  return std::any_of(std::begin(kPrefixes), std::end(kPrefixes),
                     [&](std::string_view p) { return as_command.starts_with(p); });
}

void WaterGuard::observe(const WaterView& view) {
  view_ = view;
  const std::string scene = view.scene ? *view.scene : std::string();
  if (scene != prev_scene_) swim_noted_ = false;
  if (!view.link || !view.position || scene != prev_scene_) {
    prev_ = view.link ? view.position : std::optional<protocol::Vec3>();
    prev_us_ = view.now_us;
    steady_since_us_ = view.now_us;
    prev_scene_ = scene;
  } else {
    const std::uint64_t dt = view.now_us > prev_us_ ? view.now_us - prev_us_ : 0;
    if (prev_ && dt > 0) {
      const double dy = std::fabs(static_cast<double>((*view.position)[1]) - static_cast<double>((*prev_)[1]));
      if (!std::isfinite(dy) || dy / (static_cast<double>(dt) / 1e6) > kMaxClimbMps) {
        steady_since_us_ = view.now_us;
      }
    } else if (!prev_) {
      steady_since_us_ = view.now_us;
    }
    if (dt > 0 || !prev_) {
      prev_ = view.position;
      prev_us_ = view.now_us;
    }
  }
  // A wait nobody asked about lately is over: the watcher dropped the value (our
  // game set the flag itself) or wrote it.
  std::erase_if(waits_, [&](const auto& w) {
    return view.now_us > w.second.last_us && view.now_us - w.second.last_us > kForgetUs;
  });
  watch_live();
  note_swim();
}

void WaterGuard::note_swim() {
  if (swim_noted_ || !view_.link || !view_.position || !view_.animation || !view_.scene) return;
  const std::string& a = *view_.animation;
  if (!(a.starts_with("Swim") || a.starts_with("WaterDive") || a.starts_with("Float"))) return;
  const std::string_view place = place_of(*view_.scene);
  const bool water_scene = std::any_of(std::begin(kBodies), std::end(kBodies), [&](const WaterBody& b) {
    return b.scene == place;
  });
  if (!water_scene) return;
  swim_noted_ = true;
  char where[96];
  std::snprintf(where, sizeof where, "%.2f %.2f %.2f", (*view_.position)[0], (*view_.position)[1],
                (*view_.position)[2]);
  std::string planes;
  if (view_.planes) {
    for (const float h : *view_.planes) {
      char one[24];
      std::snprintf(one, sizeof one, "%s%.2f", planes.empty() ? "" : ", ", h);
      planes += one;
    }
  }
  notes_.push_back("water: probe: our Link swims ('" + a + "') at " + where + " in " + *view_.scene +
                   (planes.empty() ? std::string() : " (water planes at " + planes + ")") +
                   ": a place for a Link in the water (meca-agua-nado: place " + where + ")");
}

const char* to_string(WaterLive live) {
  switch (live) {
    case WaterLive::live:
      return "live";
    case WaterLive::at_load:
      return "at load";
    case WaterLive::unknown:
      break;
  }
  return "unknown";
}

WaterLive WaterGuard::live(std::uint32_t hash) const {
  const auto it = live_.find(hash);
  return it == live_.end() ? WaterLive::unknown : it->second;
}

namespace {

std::vector<float> sorted_heights(const std::vector<float>& planes) {
  std::vector<float> out;
  out.reserve(planes.size());
  for (const float y : planes) {
    if (std::isfinite(y)) out.push_back(y);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

// docs/RIESGOS_RESUELTOS.md ("Mundo e historia"): does the water mover re-read
// the level while its room is loaded? Watched from the water planes' heights
// after each live write; the answer is kept per body for the next lines.
void WaterGuard::watch_live() {
  // No Link read (the hook holds a missed frame for 0.5 s first) or no scene: a load.
  const bool loading = !view_.link || !view_.scene || view_.scene->empty();
  const std::string place = loading ? std::string() : std::string(place_of(*view_.scene));
  // The bounce hold of an opt-in water ends with the visit of its scene.
  if (place != last_place_ || loading) {
    for (auto it = bounced_.begin(); it != bounced_.end();) {
      const WaterBody* body = water_body_of(it->first);
      if (body == nullptr || body->scene != place) {
        if (body != nullptr) {
          notes_.push_back("water: " + std::string(body->flag) + ": the bounce hold ends (we left " +
                           std::string(body->scene) + "): the others' value is written, the room loads with it");
        }
        flips_.erase(it->first);
        it = bounced_.erase(it);
      } else {
        ++it;
      }
    }
  }
  // A load ended in a scene with levels that came while it could not show them.
  if (!place.empty() && (place != last_place_ || was_loading_)) {
    for (auto it = on_load_.begin(); it != on_load_.end();) {
      const WaterBody* body = water_body_of(it->first);
      if (body != nullptr && body->scene == place) {
        notes_.push_back("water: " + std::string(body->flag) + ": applied on load: " + place +
                         " loaded with " +
                         (it->second == kLevelUnread ? std::string("its level")
                                                     : value_text(*body, static_cast<std::uint32_t>(it->second))));
        it = on_load_.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (loading) {
    was_loading_ = true;
  } else {
    last_place_ = place;
    was_loading_ = false;
  }

  for (auto it = checks_.begin(); it != checks_.end();) {
    const WaterBody* body = water_body_of(it->first);
    LiveCheck& c = it->second;
    if (body == nullptr) {
      it = checks_.erase(it);
      continue;
    }
    if (place != body->scene) {  // left, or loading: the room loads with the level
      notes_.push_back("water: " + std::string(body->flag) + ": live not told: " +
                       (loading ? std::string("we loaded") : "our Link left " + std::string(body->scene)) +
                       " before its water moved; " + std::string(body->scene) + " loads with it");
      on_load_[it->first] = c.level;
      it = checks_.erase(it);
      continue;
    }
    if (view_.planes && !view_.planes->empty()) {
      c.planes_seen = true;
      const std::vector<float> now = sorted_heights(*view_.planes);
      if (!c.base) {
        c.base = now;
      } else {
        std::string how;
        if (now.size() != c.base->size()) {
          how = "the water planes changed (" + std::to_string(c.base->size()) + " -> " +
                std::to_string(now.size()) + ")";
        } else {
          float most = 0.0f;
          for (std::size_t i = 0; i < now.size(); ++i) most = std::max(most, std::fabs(now[i] - (*c.base)[i]));
          if (most > kPlaneMoveM) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "a water plane moved %.2f m", static_cast<double>(most));
            how = buf;
          }
        }
        if (!how.empty()) {
          live_[it->first] = WaterLive::live;
          notes_.push_back("water: " + std::string(body->flag) + ": live: " + how + " " +
                           seconds(view_.now_us - c.since_us) + " s after the write (its mover reads the flag)");
          it = checks_.erase(it);
          continue;
        }
      }
    }
    if (view_.now_us >= c.since_us && view_.now_us - c.since_us >= kLiveCheckUs) {
      if (c.planes_seen) {
        live_[it->first] = WaterLive::at_load;
        on_load_[it->first] = c.level;
        notes_.push_back("water: " + std::string(body->flag) + ": not live: no water plane moved in " +
                         seconds(kLiveCheckUs) + " s; " + std::string(body->scene) +
                         " applies it on its next load");
      } else {
        notes_.push_back("water: " + std::string(body->flag) + ": live unknown: no water plane read in " +
                         seconds(kLiveCheckUs) + " s (LICHII_COOP_OBJECTS off?); " + std::string(body->scene) +
                         " loads with it at the latest");
      }
      it = checks_.erase(it);
      continue;
    }
    ++it;
  }
}

bool WaterGuard::steady() const {
  return view_.link && view_.position && view_.now_us >= steady_since_us_ &&
         view_.now_us - steady_since_us_ >= kSteadyUs;
}

std::optional<std::string> WaterGuard::unsafe_reason() const {
  if (!view_.link) return std::nullopt;  // loading: the room loads with the value
  if (view_.event_running) return std::string("an event runs here");
  if (view_.animation && water_unsafe_animation(*view_.animation)) {
    return "our Link plays '" + *view_.animation + "'";
  }
  if (!steady()) return std::string("our Link moves up or down");
  return std::nullopt;
}

bool WaterGuard::in_scene(const WaterBody& body) const {
  return view_.link && view_.scene && place_of(*view_.scene) == body.scene;
}

void WaterGuard::release(std::uint32_t hash, const WaterBody& body, std::string_view why) {
  const auto it = waits_.find(hash);
  if (it == waits_.end()) return;
  if (!it->second.forced) {
    notes_.push_back("water: " + std::string(body.flag) + " (" + std::string(body.what) + ") waited " +
                     seconds(view_.now_us - it->second.since_us) + " s: written, " + std::string(why));
    if (in_scene(body)) arm_after_wait(hash, body);
  }
  waits_.erase(it);
}

void WaterGuard::arm_after_wait(std::uint32_t hash, const WaterBody& body) {
  switch (live(hash)) {
    case WaterLive::live:
      notes_.push_back("water: " + std::string(body.flag) + ": written in " + std::string(body.scene) +
                       " after the wait, live (its mover reads the flag, seen before)");
      return;
    case WaterLive::at_load:
      notes_.push_back("water: " + std::string(body.flag) + ": written in " + std::string(body.scene) +
                       " after the wait; its mover reads it at load (seen before): applied when " +
                       std::string(body.scene) + " loads again");
      on_load_[hash] = kLevelUnread;  // the level: derive() reads it
      return;
    case WaterLive::unknown:
      break;
  }
  notes_.push_back("water: " + std::string(body.flag) + ": written in " + std::string(body.scene) +
                   " after the wait, live (its mover reads the flag): watching its water planes " +
                   seconds(kLiveCheckUs) + " s");
  LiveCheck& c = checks_[hash];
  c = LiveCheck{};
  c.since_us = view_.now_us;
  c.level = kLevelUnread;
  if (view_.planes && !view_.planes->empty()) {
    c.base = sorted_heights(*view_.planes);  // before the write: the store writes after holds() said no
    c.planes_seen = true;
  }
}

bool WaterGuard::holds(FlagKey key) {
  if (key.kind != protocol::FlagKeyKind::named || key.index != 0) return false;
  const WaterBody* body = water_body_of(key.hash);
  if (body == nullptr || (body->opt_in && !jabu_)) return false;
  if (bounced_.count(key.hash) != 0 && in_scene(*body)) return true;  // it bounced: ours settles it here
  if (!in_scene(*body)) {
    release(key.hash, *body, view_.link ? "our Link left " + std::string(body->scene) : "we are loading");
    return false;
  }
  const auto reason = unsafe_reason();
  if (!reason) {
    release(key.hash, *body, "our Link stands");
    return false;
  }
  auto [it, fresh] = waits_.try_emplace(key.hash);
  Wait& w = it->second;
  w.last_us = view_.now_us;
  if (fresh) {
    w.since_us = view_.now_us;
    notes_.push_back("water: " + std::string(body->flag) + " (" + std::string(body->what) +
                     ") from another player waits: " + *reason);
  }
  if (w.forced) return false;
  if (max_wait_us_ != 0 && view_.now_us - w.since_us >= max_wait_us_) {
    w.forced = true;
    notes_.push_back("water: " + std::string(body->flag) + " waited " + seconds(view_.now_us - w.since_us) +
                     " s: written although " + *reason);
    arm_after_wait(key.hash, *body);
    return false;
  }
  return true;
}

void WaterGuard::received(const FlagEntry& before, const FlagEntry& after, std::uint8_t from, bool join) {
  if (after.key.kind != protocol::FlagKeyKind::named || after.key.index != 0) return;
  const WaterBody* body = water_body_of(after.key.hash);
  if (body == nullptr) return;
  std::string line = "water: " + std::string(body->flag) + " " + value_text(*body, before.value) + " -> " +
                     value_text(*body, after.value);
  if (from != 0) line += " (P" + std::to_string(from) + ")";
  line += join ? " joining" : "";
  const std::int32_t level =
      body->is_int ? static_cast<std::int32_t>(after.value) : (after.value != 0 ? 1 : 0);
  // An opt-in water set both ways by step switches: does it bounce between games?
  if (body->opt_in && from != 0 && !join && in_scene(*body) && bounced_.count(after.key.hash) == 0) {
    auto& times = flips_[after.key.hash];
    times.push_back(view_.now_us);
    std::erase_if(times, [&](std::uint64_t at) { return view_.now_us > at && view_.now_us - at > kBounceUs; });
    if (static_cast<int>(times.size()) >= kBounceFlips) {
      bounced_[after.key.hash] = true;
      notes_.push_back("water: " + std::string(body->flag) + " bounced: " + std::to_string(times.size()) +
                       " values from the others in " + seconds(kBounceUs) +
                       " s: theirs wait until we leave " + std::string(body->scene) + " (ours still go out)");
    }
  }
  if (in_scene(*body)) {
    switch (live(after.key.hash)) {
      case WaterLive::live:
        line += ": written in " + std::string(body->scene) + ", live (its mover reads the flag, seen before)";
        break;
      case WaterLive::at_load:
        // Seen before: this mover reads the level when its room loads.
        line += ": written in " + std::string(body->scene) + "; its mover reads it at load (seen before): " +
                "applied when " + std::string(body->scene) + " loads again";
        on_load_[after.key.hash] = level;
        break;
      case WaterLive::unknown: {
        line += ": written in " + std::string(body->scene) + ", live (its mover reads the flag): " +
                "watching its water planes " + seconds(kLiveCheckUs) + " s";
        LiveCheck& c = checks_[after.key.hash];
        c = LiveCheck{};
        c.since_us = view_.now_us;
        c.level = level;
        if (view_.planes && !view_.planes->empty()) {
          c.base = sorted_heights(*view_.planes);
          c.planes_seen = true;
        }
        break;
      }
    }
  } else {
    line += ": " + std::string(body->scene) + " loads with it" +
            (view_.scene ? " (we are in " + *view_.scene + ")" : std::string());
    on_load_[after.key.hash] = level;
    checks_.erase(after.key.hash);
  }
  notes_.push_back(std::move(line));
}

std::size_t WaterGuard::derive(FlagStore& store) {
  // The level of a check (or a load) armed after a wait: what the store holds now.
  const auto level_of = [&](std::uint32_t hash) -> std::optional<std::int32_t> {
    const WaterBody* body = water_body_of(hash);
    if (body == nullptr) return std::nullopt;
    const auto v = store.read(FlagKey(hash, std::uint16_t{0}));
    if (!v) return std::nullopt;
    return body->is_int ? static_cast<std::int32_t>(v->value) : (v->value != 0 ? 1 : 0);
  };
  for (auto& [hash, c] : checks_) {
    if (c.level == kLevelUnread) {
      if (const auto l = level_of(hash)) c.level = *l;
    }
  }
  for (auto& [hash, level] : on_load_) {
    if (level == kLevelUnread) {
      if (const auto l = level_of(hash)) level = *l;
    }
  }
  if (view_.event_running) return 0;
  std::size_t written = 0;
  for (const WaterBody& body : kBodies) {
    if (!body.is_int || body.one_hot[0].empty()) continue;
    const auto level = store.read(FlagKey(body.hash, std::uint16_t{0}));
    if (!level || level->kind != FlagKind::int32) continue;
    const auto value = static_cast<std::int32_t>(level->value);
    if (value < body.lowest || value > body.highest) {
      if (const auto seen = bad_level_.find(body.hash); seen == bad_level_.end() || seen->second != value) {
        bad_level_[body.hash] = value;
        notes_.push_back("water: " + std::string(body.flag) + " is " + std::to_string(value) +
                         ", not a level: its one-hot flags are left alone");
      }
      continue;
    }
    bad_level_.erase(body.hash);
    std::string changed;
    bool failed = false;
    for (std::size_t i = 0; i < body.one_hot.size(); ++i) {
      if (body.one_hot[i].empty()) continue;
      const FlagKey key(body.one_hot_hash[i], std::uint16_t{0});
      const std::uint32_t want = (body.lowest + static_cast<std::int32_t>(i)) == value ? 1u : 0u;
      const auto now = store.read(key);
      if (now && now->kind == FlagKind::boolean && (now->value != 0 ? 1u : 0u) == want) continue;
      if (store.write(FlagEntry{key, FlagKind::boolean, want})) {
        ++written;
        changed += (changed.empty() ? "" : ", ") + std::string(body.one_hot[i]) + " " + std::to_string(want);
      } else {
        failed = true;
      }
    }
    if (!changed.empty()) {
      notes_.push_back("water: " + std::string(body.flag) + " is " + std::to_string(value) + ": " + changed +
                       " (one level, its flags follow)");
    }
    if (failed && !derive_failed_[body.hash]) {
      derive_failed_[body.hash] = true;
      notes_.push_back("water: " + std::string(body.flag) + " is " + std::to_string(value) +
                       ": a one-hot flag could not be written (tried again each frame)");
    } else if (!failed) {
      derive_failed_.erase(body.hash);
    }
  }
  return written;
}

}  // namespace coop::host
