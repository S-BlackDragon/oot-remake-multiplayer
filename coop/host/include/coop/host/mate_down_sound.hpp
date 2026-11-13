#pragma once

// ORM "a mate is down" sound (oot-64 phase 6; oot-remake docs/CAIDO_REVIVIR.md,
// "Sonido de compañero caído"). When another player's PLAYER_STATUS dead bit
// (bit 4) comes on, our game plays one game sound, once for that fall: at his
// puppet when he is near us in our scene (the game's own distance fade), else
// on the interface (no place), so a fall far away or in another scene is heard
// too. The sound itself is the hook's (lichii/hook/orm_ocarina_sound.h,
// Player::OnCue); this is the rule, over the mates the downed glue already
// reads from the presence. No memory, no emulator, no sockets.
//
// Once a fall: only a mate seen standing before counts (one first seen down,
// after our join or his, or after he was out of the list, makes no sound: the
// chat notice still says it); several falls close together make one sound
// (gap_us); none while our own game over runs.
//
// Switch: LICHII_ORM_MATE_DOWN_SOUND (off until heard in a game).
// Test: tests/host/mate_down_sound_test.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

struct MateDownSoundSettings {
  // Within this (game units, metres unmeasured) in our scene: at his puppet.
  float near = 20.0f;
  // At most one sound this often, whoever falls.
  std::uint64_t gap_us = 2'000'000;
};

struct MateDownSoundInput {
  std::uint64_t now_us = 0;
  std::optional<std::string> scene;        // ours
  std::optional<protocol::Vec3> position;  // ours
  bool game_over = false;                  // our own game over runs
  std::vector<Mate> mates;                 // as ReviveWatch reads them
};

struct MateDownCue {
  std::uint8_t mate = 0;
  bool at_mate = false;  // false: on the interface
  bool operator==(const MateDownCue&) const = default;
};

struct MateDownSoundOutput {
  std::optional<MateDownCue> cue;
  std::vector<std::string> log;
};

class MateDownSound {
 public:
  explicit MateDownSound(MateDownSoundSettings settings = {}) : settings_(settings) {}
  MateDownSoundOutput tick(const MateDownSoundInput& in);

 private:
  enum class Seen : std::uint8_t { unknown, up, down };
  MateDownSoundSettings settings_;
  std::array<Seen, protocol::kMaxPlayers + 1> seen_{};
  std::uint64_t last_ = 0;  // the last sound
  bool played_ = false;
};

}  // namespace coop::host
