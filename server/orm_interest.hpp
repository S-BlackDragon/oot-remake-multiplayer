#pragma once

// ORM (oot-remake docs/ANCHO_DE_BANDA.md, "Interés"): the relay's share of
// interest management. A PLAYER_POSE is Link's whole skeleton (some 300
// bones, about 4 KB) 30 times a second, and only a puppet in the same place
// uses it: the relay sends it only to the players in the sender's place, a
// scene in either era (as coop::host::same_place, which the server does not
// link: "Graveyard_Child" and "Graveyard_Adult" are one graveyard). A player
// whose scene the server does not know yet, or a sender whose scene it does
// not know, still gets and sends every pose. The sender sends its poses at
// the native rate, whatever the distance, and none while nobody is in its
// place (lichii/hook/coop_interest.h; 2026-11-11: no compression and
// no rate by distance).

#include <string_view>

namespace coop::server::orm {

// The place a scene shows, without a trailing "_Child" or "_Adult".
inline std::string_view place_of(std::string_view scene) {
  for (const std::string_view era : {std::string_view("_Child"), std::string_view("_Adult")}) {
    if (scene.size() > era.size() && scene.substr(scene.size() - era.size()) == era) {
      return scene.substr(0, scene.size() - era.size());
    }
  }
  return scene;
}

// Whether a pose from a player in scene `from` goes to a player in `to`
// (empty: not known).
inline bool pose_goes(std::string_view from, std::string_view to) {
  return from.empty() || to.empty() || place_of(from) == place_of(to);
}

}  // namespace coop::server::orm
