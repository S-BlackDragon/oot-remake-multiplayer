// ORM enemigos (oot-remake docs/ENEMIGOS.md): the mini-bosses count as bosses
// for the ownership rules, alone and with the boss flag in OBJECT_GROUP.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <vector>

#include "coop_enemies.h"
#include "coop_objects_sync.h"

namespace proto = coop::protocol;

TEST_CASE("enemigos: the mini-bosses by name, not their picture book or stand-ins") {
    using CoopEnemies::IsMiniBoss;
    CHECK(IsMiniBoss("Enemy_Stalfos"));
    CHECK(IsMiniBoss("Enemy_Stalfos_AlwaysRespawn"));
    CHECK(IsMiniBoss("Enemy_Lizalfos"));
    CHECK(IsMiniBoss("Enemy_Dinolfos"));
    CHECK(IsMiniBoss("Enemy_IronKnuckle_Black"));
    CHECK(IsMiniBoss("Enemy_IronKnuckle_White"));
    CHECK(IsMiniBoss("Enemy_Wolfos"));
    CHECK(IsMiniBoss("Enemy_Wolfos_White"));
    CHECK_FALSE(IsMiniBoss("Enemy_Stalfos_Dictionary"));
    CHECK_FALSE(IsMiniBoss("Enemy_Stalfos_Haribote"));
    CHECK_FALSE(IsMiniBoss("Enemy_Dinolfos_Haribote"));
    CHECK_FALSE(IsMiniBoss("Enemy_Stalchild"));
    CHECK_FALSE(IsMiniBoss("Enemy_Stalfo"));
    CHECK_FALSE(IsMiniBoss("Enemy_Stalfos_"));
    CHECK_FALSE(IsMiniBoss(""));
    // Dead Hand is a boss group already (its hands follow it).
    CHECK_FALSE(IsMiniBoss("Enemy_DeadHand"));
}

TEST_CASE("enemigos: a mini-boss is a boss to IsBoss, plain enemies are not") {
    using CoopObjects::IsBoss;
    CHECK(IsBoss("Enemy_Stalfos"));
    CHECK(IsBoss("Enemy_IronKnuckle_Silver"));
    CHECK(IsBoss("Enemy_Lizalfos"));
    CHECK(IsBoss("Enemy_MiddleBoss_Lizalfos"));  // daurosk's, by prefix
    CHECK(IsBoss("Enemy_DeadHand"));
    CHECK_FALSE(IsBoss("Enemy_Wolfos_Dictionary"));
    CHECK_FALSE(IsBoss("Enemy_Stalchild"));
    CHECK_FALSE(IsBoss("Enemy_Dekubaba"));
}

TEST_CASE("enemigos: two Stalfos in a room are two fights, each sent alone with the boss flag") {
    using CoopObjects::GroupInput;
    const auto id = [](std::uint64_t v) { return proto::ObjectId{proto::ObjectKind::placed, 0, v}; };
    const std::vector<GroupInput> walk{
        {id(1), "Enemy_Stalfos", {0, 0, 0}},
        {id(2), "Enemy_Stalfos", {6, 0, 0}},
        {id(3), "Enemy_Keese", {3, 0, 0}},
    };
    const std::vector<proto::ObjectGroupEntry> groups = CoopObjects::GroupsOf(walk, {});
    REQUIRE(groups.size() == 2);
    for (const proto::ObjectGroupEntry& e : groups) {
        CHECK(e.member == e.root);  // its own root: neither follows the other
        CHECK(e.flags == proto::object_group::boss);
        CHECK(e.member != id(3));   // the Keese is a plain enemy, not sent
    }
    CHECK(groups[0].member != groups[1].member);
}
