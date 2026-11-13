#pragma once

// ORM: the filled-in examples of our messages and of our fields in upstream's
// (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2: kept out of upstream's
// tests/protocol/samples.hpp, whose every_message() they used to extend).
// tests/orm/protocol_codec_test.cpp runs upstream's every-message checks over
// them; tests/server/session_fuzz_test.cpp seeds from them too.

#include <array>
#include <optional>
#include <vector>

#include "../protocol/samples.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::protocol::test {

inline std::vector<Message> orm_messages() {
  std::vector<Message> all;
  {
    // ORM "Levantar": a pot in player 3's hands, by bone 120.
    ObjectSample carried{2, kPlacedId, 4, full_object_state()};
    carried.state.carry = ObjectCarry{3, 120, Vec3{0.0f, 1.6f, 0.3f}, Quat{0.0f, 0.0f, 0.0f, 1.0f},
                                      Vec3{0.1f, -0.2f, 0.05f}, Quat{0.0f, 0.7071068f, 0.0f, 0.7071068f}};
    all.push_back(carried);
    ObjectSample by_actor{2, kPlacedId, 4, {}};
    by_actor.state.time = {7, 8};
    by_actor.state.carry = ObjectCarry{1, kCarryNoBone, Vec3{0.0f, 1.0f, 0.5f}, Quat{0.0f, 0.0f, 0.0f, 1.0f},
                                       Vec3{}, Quat{0.0f, 0.0f, 0.0f, 1.0f}};
    all.push_back(by_actor);
  }
  all.push_back(Revive{0, 2, kReviveDone});
  all.push_back(Revive{5, 1, 0});
  all.push_back(Revive{0, 3, 40});
  all.push_back(GroupDefeat{2, 123456789});
  all.push_back(StoryEvent{0, StoryKind::start, "KokiriForest_Child",
                           "KokiriForest_Meet_DekuTree_MeetDekuTree_01", {52.3f, 119.1f, -20.0f}, 0});
  all.push_back(StoryEvent{2, StoryKind::end, "KokiriForest_Child", "Dm_Kokiri_0070", {}, 4000000000u});
  all.push_back(StoryEvent{6, StoryKind::played, "InsideTheDekuTree", "InsideTheDekuTree_Arrive",
                           {-1.5f, 0.0f, 3.25f}, 77});
  all.push_back(PlayerLook{0, {0x2e, 0x9e, 0x3a}, 0});
  all.push_back(PlayerLook{6, {255, 0, 128}, 0});
  all.push_back(PlayerLook{3, {0, 0, 0}, 200});  // a reserved style is kept as it came
  // ORM OBJECT_POSE: a townsperson's head and face bones (part 0 of a pose in
  // one part), and an empty part 2 of a pose in parts 0 and 2.
  all.push_back(ObjectPose{0, kPlacedId, 3, {123456789, 8}, 0x00a1b2c3,
                           {PoseBone{5, 0x02, std::array<std::int16_t, 4>{0, 23170, 0, 23170},
                                     std::nullopt, std::nullopt},
                            PoseBone{41, 0, std::array<std::int16_t, 4>{100, -200, 300, 32000},
                                     std::array<std::int16_t, 3>{10, -20, 30},
                                     std::array<std::int16_t, 3>{8192, 8192, 8192}}}});
  all.push_back(ObjectPose{4, kMadeId, 0, {1, 1}, 1, {}, 2, 0x05});
  // ORM CHEST_OPEN: a dungeon chest's lid, from a client and relayed.
  all.push_back(ChestOpen{0, 0x0e3e4a1f2b3c4d5eull, "InsideTheDekuTree", {"Open", 0.0f, 1.0f}, 123456789});
  all.push_back(ChestOpen{5, 0x88226065e35dab4dull, "DeathMountainTrail", {"Open_Kick", 12.5f, 0.5f}, 0});
  // ORM OCARINA_NOTE: a note (A) from a client, a release, the last song with a
  // serial at the top, and the Harp's highest note.
  all.push_back(OrmOcarinaNote{0, OcarinaKind::note, 0, 11, 0, 1});
  all.push_back(OrmOcarinaNote{2, OcarinaKind::release, 0, 0, 0, 2});
  all.push_back(OrmOcarinaNote{6, OcarinaKind::song, 0, 0, kMaxOcarinaSong, 255});
  all.push_back(OrmOcarinaNote{4, OcarinaKind::note, kMaxOcarinaInstrument, kMaxOcarinaNote, 0, 0});
  // ORM HOST_CHOICE: a wish and a pick from a client, the relay's word with a
  // name picked by hand, and a refused pick.
  all.push_back(HostChoice{HostChoiceKind::wish, HostWish::me, HostWhy::first, 0, 0, 0, "", ""});
  all.push_back(HostChoice{HostChoiceKind::pick, HostWish::automatic, HostWhy::first, 0, 0, 0, "Saria", ""});
  all.push_back(HostChoice{HostChoiceKind::told, HostWish::automatic, HostWhy::picked, 2, 5, 61, "Saria", "saria"});
  all.push_back(HostChoice{HostChoiceKind::told, HostWish::never, HostWhy::refused, 6, 0, 65535, "Mido", ""});
  return all;
}

}  // namespace coop::protocol::test
