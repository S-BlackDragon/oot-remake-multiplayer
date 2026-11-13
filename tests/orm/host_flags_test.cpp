// ORM cases moved out of upstream's tests/host/flags_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <filesystem>
#include <string>

#include "coop/host/flags.hpp"

using namespace coop::host;

TEST_CASE("shared flags: enum: lines opt Enum flags in with their values") {
  std::string error;
  const auto flags = SharedFlags::parse(
      "Some.Flag\n"
      "enum:Step_Meet_DekuTree NotReady Ready Step01 Step02 Step03 Complete  # story\n"
      "enum:0x1234 0x1 Two\n"
      "Step_Meet_DekuTree\n",  // also as a plain line: still one key
      &error);
  REQUIRE_MESSAGE(flags, error);
  const std::uint32_t deku = flag_hash("Step_Meet_DekuTree");
  CHECK(deku == 0x3372689bu);  // the hash BANDERAS_CHARLA.md found in GameDataList
  CHECK(flags->size() == 3);
  CHECK(flags->contains({deku, 0}));
  CHECK(flags->contains({0x1234u, 0}));
  const EnumFlags& enums = flags->enums();
  CHECK(enums.size() == 2);
  CHECK(enums.contains(deku));
  CHECK_FALSE(enums.contains(flag_hash("Some.Flag")));
  REQUIRE(enums.values(deku));
  CHECK(enums.values(deku)->size() == 6);
  CHECK(enums.values(deku)->front() == flag_hash("NotReady"));  // list order kept
  CHECK(enums.allows(deku, flag_hash("Step02")));
  CHECK_FALSE(enums.allows(deku, flag_hash("Step04")));
  CHECK(enums.allows(0x1234u, 1));
  CHECK(enums.allows(0x1234u, flag_hash("Two")));
  CHECK_FALSE(enums.allows(0x9999u, 1));
  CHECK(enums.values(0x9999u) == nullptr);

  // A list built again from its keys keeps the Enum flags only when given them.
  CHECK(SharedFlags(flags->keys(), flags->enums()).enums() == enums);
  CHECK(SharedFlags(flags->keys()).enums().empty());
  // A list with no enum: line opts nothing in (today's lists).
  CHECK(SharedFlags::parse("Some.Flag\nkey64:5\n")->enums().empty());
}

TEST_CASE("shared flags: bad enum: lines are refused") {
  const char* bad[] = {
      "enum:Step_A\n",                // no values
      "enum: A B\n",                  // no flag
      "enum:0x0 A\n",                 // hash 0
      "enum:0x123456789 A\n",         // a hash over 8 digits
      "enum:Step_A A 0x123456789\n",  // a value hash over 8 digits
      "enum:Step_A A B A\n",          // a value twice
      "enum:Step_A A\nenum:Step_A B\n",  // a flag twice
  };
  for (const char* text : bad) {
    std::string error;
    CHECK_FALSE(SharedFlags::parse(std::string("ok\n") + text, &error));
    CHECK(error.rfind("line ", 0) == 0);
  }
  std::string many = "enum:Step_A";
  for (std::size_t i = 0; i <= EnumFlags::kMaxValues; ++i) many += " V" + std::to_string(i);
  CHECK_FALSE(SharedFlags::parse(many));

  EnumFlags e;
  CHECK_FALSE(e.add(0, {1}));
  CHECK_FALSE(e.add(5, {}));
  CHECK_FALSE(e.add(5, {1, 1}));
  CHECK(e.add(5, {1, 2}));
  CHECK_FALSE(e.add(5, {3}));
  CHECK(e.keys() == std::vector<FlagKey>{FlagKey{5u, 0}});
}

TEST_CASE("enum flags: steps keep their order, merge refuses a flag with other values") {
  EnumFlags a;
  REQUIRE(a.add(0x10u, {1, 2, 3}, true));
  CHECK(a.steps(0x10u));
  CHECK(a.rank(0x10u, 1) == 0u);
  CHECK(a.rank(0x10u, 3) == 2u);
  CHECK_FALSE(a.rank(0x10u, 4));
  CHECK_FALSE(a.rank(0x11u, 1));
  CHECK_FALSE(a.steps(0x11u));
  EnumFlags b;
  REQUIRE(b.add(0x20u, {7, 8}));
  CHECK_FALSE(b.steps(0x20u));  // a plain Enum flag by default
  REQUIRE(b.add(0x10u, {1, 2, 3}, true));
  CHECK(a.merge(b));  // the same 0x10, and 0x20 added
  CHECK(a.size() == 2);
  CHECK(a.allows(0x20u, 8));
  EnumFlags other_order;
  REQUIRE(other_order.add(0x10u, {3, 2, 1}, true));
  CHECK_FALSE(a.merge(other_order));
  EnumFlags not_steps;
  REQUIRE(not_steps.add(0x10u, {1, 2, 3}));
  CHECK_FALSE(a.merge(not_steps));
  CHECK(a.steps(0x10u));
  CHECK(a.rank(0x10u, 3) == 2u);  // unchanged
}
