// ORM cases moved out of upstream's tests/lichii/dungeon_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the shared dungeon flags (lichii/hook/coop_dungeon.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <map>
#include <set>
#include <string>

#include "coop_dungeon.h"
#include "coop/host/flag_watcher.hpp"
#include "coop/protocol/codec.hpp"

namespace host = coop::host;

TEST_CASE("dungeon: Merge keeps the list's opted-in Enum flags") {
    const auto list = host::SharedFlags::parse("enum:Step_Meet_DekuTree Ready Step01\n");
    REQUIRE(list);
    std::optional<host::SharedFlags> shared = *list;
    CoopDungeon::Merge(shared, true);
    const std::uint32_t deku = host::flag_hash("Step_Meet_DekuTree");
    CHECK(shared->contains(host::FlagKey(deku, 0)));
    CHECK(shared->enums().allows(deku, host::flag_hash("Step01")));

    std::optional<host::SharedFlags> none;
    CoopDungeon::Merge(none, true);
    CHECK(none->enums().empty());  // the dungeon list opts no Enum flag in
}
