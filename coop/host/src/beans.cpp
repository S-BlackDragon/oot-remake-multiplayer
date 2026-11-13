#include "coop/host/beans.hpp"

#include <array>
#include <cstdio>

#include "coop/host/scene.hpp"

namespace coop::host::bean {

namespace {

// From the romfs (Banc *_Static: each Obj_SmallFarm_A_01/_02 and the Obj_SmallFarm_A_03 at
// the same Translate, its Plant link; the Bool from GameData struct 0x820c6b5e, as
// tools/romfs/collectible_list.py checks it). Read 2026-11-11.
constexpr std::array<Soil, 11> kSoils{{
    {"Death Mountain Crater's soil", "DeathMountainCrater", 0x562fab76u, 0xa7268760e39c8fbeull,
     0x12a6f85c447c98adull},
    {"Death Mountain Trail's soil", "DeathMountainTrail", 0xf2ceba92u, 0xfea9dd3f7283625cull, 0xcd33fac7d6cb5cacull},
    {"Desert Colossus's soil", "DesertColossus", 0x2a1c58ceu, 0xc83ecf87be2b0886ull, 0xfba28d80a9ec16aaull},
    {"Gerudo Valley's soil", "GerudoValley", 0x50a1775eu, 0x96ae62bf11d410d7ull, 0x3bae3d861877068aull},
    {"the Graveyard's soil", "Graveyard", 0x274b7e3du, 0xf2ddb69c4fb84d45ull, 0xdd3f85e7a1905ce6ull},
    {"Lake Hylia's soil (Hyrule Field's scene)", "HyruleField", 0xb0377637u, 0x5ed586d79f090861ull,
     0x0de8c9deae412700ull},
    {"Kokiri Forest's soil without bugs", "KokiriForest", 0xe7bb7f2fu, 0x8a726d99552e91e7ull, 0x5be677723b4bd101ull},
    {"Kokiri Forest's soil with bugs", "KokiriForest", 0xb55acaddu, 0x548d80c51011dd32ull, 0x7dafcfc1694ac1d7ull},
    {"the Lost Woods' first soil", "LostWood", 0x610eb13cu, 0x0272bbd7298beaa1ull, 0xbf4398d002b3973eull},
    {"the Lost Woods' second soil", "LostWood", 0x5ebf7c8bu, 0x48a64c7346c62c51ull, 0x79a56970e2803e10ull},
    {"Zora's River's soil", "ZoraRiver", 0x5eea40a6u, 0xed33bcd7f15cbac0ull, 0x4bf282177c79a4d6ull},
}};

std::string hex32(std::uint32_t v) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(v));
  return text;
}

}  // namespace

std::span<const Soil> soils() { return kSoils; }

const char* to_string(Seen seen) {
  switch (seen) {
    case Seen::planted: return "planted: platform seen";
    case Seen::not_seen: return "platform not seen: Bool meaning unconfirmed";
    case Seen::platform_alone:
      return "platform seen with the Bool at 0: the platform does not follow the Bool alone (meaning unconfirmed)";
    case Seen::bare: return "not planted: no platform";
  }
  return "?";
}

std::optional<Seen> BeanCheck::seen(std::uint32_t flag) const {
  const auto it = states_.find(flag);
  return it == states_.end() ? std::nullopt : it->second.seen;
}

bool BeanCheck::confirmed(std::uint32_t flag) const {
  const auto it = states_.find(flag);
  return it != states_.end() && it->second.saw_planted && it->second.saw_bare;
}

void BeanCheck::look(const Soil& soil, bool value, bool platform, std::uint64_t now_us) {
  State& st = states_[soil.flag];
  const Seen now = value ? (platform ? Seen::planted : Seen::not_seen) : (platform ? Seen::platform_alone : Seen::bare);
  const std::string head = "beans: " + std::string(soil.what) + " (Bool " + hex32(soil.flag) + " = " +
                           (value ? "1" : "0") + ", " + scene_ + "): ";
  // A Bool that turned 1 while we are here: did the platform come at once?
  if (st.bool_was && !*st.bool_was && value) st.turned_on_us = now_us != 0 ? now_us : 1;
  st.bool_was = value;
  if (st.turned_on_us != 0) {
    if (platform) {
      notes_.push_back(head + "the bean came while we are here and its platform came live");
      st.turned_on_us = 0;
    } else if (now_us - st.turned_on_us >= kLiveUs) {
      notes_.push_back(head + "the bean came while we are here and no platform came in " +
                       std::to_string(kLiveUs / 1'000'000) + " s: not live, it should come on the next load");
      st.turned_on_us = 0;
    } else {
      return;  // still waiting for the platform: no verdict yet
    }
  }
  if (now == Seen::planted) st.saw_planted = true;
  if (now == Seen::bare) st.saw_bare = true;
  if (st.seen != now) {
    st.seen = now;
    notes_.push_back(head + to_string(now));
  }
  if (st.saw_planted && st.saw_bare && !st.confirmed_noted) {
    st.confirmed_noted = true;
    notes_.push_back("beans: " + std::string(soil.what) +
                     ": confirmed in this run: its Bool 1 shows the platform and 0 none (the Bool is 'planted')");
  }
}

bool BeanCheck::wants(const std::optional<std::string>& scene) const {
  if (!on_ || !scene || scene->empty()) return false;
  for (const Soil& s : kSoils) {
    if (place_of(*scene) == s.place) return true;
  }
  return false;
}

void BeanCheck::frame(const BeanView& view, FlagStore& game) {
  if (!on_) return;
  const std::string scene = view.scene ? *view.scene : std::string();
  if (scene != scene_) {
    scene_ = scene;
    in_scene_since_us_ = 0;
    for (auto& [flag, st] : states_) {  // a new visit: say again what this one shows
      st.seen.reset();
      st.bool_was.reset();
      st.turned_on_us = 0;
    }
  }
  if (scene_.empty()) return;
  bool any = false;
  for (const Soil& s : kSoils) any = any || place_of(scene_) == s.place;
  if (!any) return;
  if (!view.walked || !view.loaded) {
    in_scene_since_us_ = 0;
    if (!noted_no_walk_) {
      noted_no_walk_ = true;
      notes_.push_back("beans: no actor walk in " + scene_ +
                       " (LICHII_COOP_OBJECTS off?): its soils' Bools are shared unchecked");
    }
    return;
  }
  if (in_scene_since_us_ == 0) in_scene_since_us_ = view.now_us != 0 ? view.now_us : 1;
  if (view.now_us - in_scene_since_us_ < kSettleUs) return;  // the room's actors are still coming
  for (const Soil& s : kSoils) {
    if (place_of(scene_) != s.place) continue;
    const auto value = game.read(FlagKey(s.flag, std::uint16_t{0}));
    if (!value || value->kind != protocol::FlagKind::boolean) continue;  // not readable: no verdict
    look(s, value->value != 0, view.loaded(s.platform), view.now_us);
  }
}

}  // namespace coop::host::bean
