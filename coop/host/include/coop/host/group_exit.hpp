#pragma once

// ORM, scene exits the group takes together (oot-remake docs/CONTENIDO_ADULTO.md, block 20).
// oot-64 (ZMP) D-165, "EscapeTakesGroup": in the escape from Ganon's collapsing castle any
// exit one Link takes is a scene change of the whole group; split, each part ran its own
// countdown with its own Zelda and reached Ganon alone. In the remake each game runs its
// own escape, so the rule is the other way round: a game still in the escape scene follows
// a mate who already reached the next one (the fight with Ganon).
//
// Plain decisions over plain readings: no game addresses, no sockets. The hook reads our
// scene and Link and the mates' scenes (the presence list) and, when update() says so,
// moves our Link to that mate the way /teleport does (CoopTeleport: JOIN_PARTNER, the
// game's scene change, then the mate's grounded place).
//
// - Only a mate who is up counts (a downed mate's scene says nothing about the group).
// - Our Link follows only while he is up and no event runs (the teleport refuses an event;
//   a scene change would bring a downed Link back up, oot-64 D-178): a downed Link waits
//   until a mate revives him (or the solo rule of D-R14 takes him through his own game
//   over and Continue), then follows.
// - He has to see the mate in the next scene for `settle_us` (a mate loading through the
//   scene flickers), and asks again every `retry_us` until our scene changes.
//
// Doc: oot-remake docs/CONTENIDO_ADULTO.md (sections 0 and 4).
// Switch: none yet: the hook glue is not in this tree (LICHII_ORM_GROUP_EXIT=0 is planned to turn
//   it off).
// Test: tests/host/group_exit_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace coop::host {

struct GroupExit {
  std::string from;  // the scene a game leaves (CrumblingGanonCastle)
  std::string to;    // where the group already is (FinalBattleField)
  bool operator==(const GroupExit&) const = default;
};

// The remake's: the collapsing castle (Ganondorf's room at its top and the escape down
// it, Banc GanonCastle/CrumblingGanonCastle) to the field of the last fight
// (GanonCastle/FinalBattleField).
std::vector<GroupExit> default_group_exits();

struct GroupExitSettings {
  bool on = true;                      // LICHII_ORM_GROUP_EXIT=0 turns it off
  std::uint64_t settle_us = 1'000'000;  // a mate seen there this long
  std::uint64_t retry_us = 10'000'000;  // ask again after this
};

struct MateScene {
  std::uint8_t number = 0;           // player number (1 to 6)
  std::optional<std::string> scene;  // nothing: not known yet
  bool up = true;                    // not downed, not in a game over
};

struct GroupExitInput {
  std::uint64_t now_us = 0;
  std::optional<std::string> scene;  // ours; nothing while loading
  bool up = true;                    // our Link is up
  bool event = false;                // an event runs in our game
  std::vector<MateScene> mates;
};

struct GroupExitAction {
  std::optional<std::uint8_t> follow;  // move our Link to this player now
  std::string to;                      // the scene he is in
  std::vector<std::string> log;
};

class GroupExitRule {
 public:
  explicit GroupExitRule(std::vector<GroupExit> exits = default_group_exits(),
                         GroupExitSettings settings = {});

  GroupExitAction update(const GroupExitInput& input);
  void reset();  // a session ends: nothing remembered

 private:
  struct Seen {
    std::uint8_t number = 0;
    std::string scene;
    std::uint64_t since_us = 0;
  };
  const GroupExit* exit_from(const std::string& scene) const;

  std::vector<GroupExit> exits_;
  GroupExitSettings settings_;
  std::vector<Seen> seen_;                 // where each mate was first seen, and since when
  std::optional<std::uint64_t> asked_us_;  // our last follow
  std::optional<std::string> asked_from_;  // the scene we were in then
  std::string waiting_;                    // the last wait logged (said once)
};

}  // namespace coop::host
