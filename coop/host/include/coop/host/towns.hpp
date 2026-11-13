#pragma once

// ORM towns with several players (oot-remake docs/PUEBLOS.md): the place a
// scene shows for a list's place rules (hold:, dungeon_lists.hpp).
//
// The romfs has a second version of some town scenes, loaded in later story
// states: "KakarikoVillage_Adult_Alternative", "ZoraDomain_Adult_Alternative"
// (tools/harness/escenas_banc.txt). It is the same town, so a flag held "in
// KakarikoVillage" is held there too; place_of (scene.hpp) drops only the
// era, and before this a hold: flag was never held in those two scenes.
// Drawing the other players (same_place) keeps place_of: this rule is for
// the lists only.
//
// Doc: oot-remake docs/PUEBLOS.md ("La regla de las escenas _Alternative").
// Switch: none (LICHII_COOP_DUNGEON_LISTS reads it).
// Test: tests/host/dungeon_lists_test.cpp, tests/lichii/town_list_test.cpp.

#include <string_view>

#include "coop/host/scene.hpp"

namespace coop::host {

// A trailing "_Alternative" is dropped, then the era (place_of):
// "KakarikoVillage_Adult_Alternative" -> "KakarikoVillage". A scene that is
// only "_Alternative" stays as it is.
inline std::string_view town_place_of(std::string_view scene) {
  constexpr std::string_view kAlternative = "_Alternative";
  if (scene.size() > kAlternative.size() && scene.ends_with(kAlternative)) {
    scene.remove_suffix(kAlternative.size());
  }
  return place_of(scene);
}

}  // namespace coop::host
