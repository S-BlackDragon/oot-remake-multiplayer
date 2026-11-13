// Tests for the one switch reading of our hook code (lichii/hook/coop_switch.h).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdlib>
#include <string>

#include "coop_held_switch.h"
#include "coop_switch.h"

using CoopSwitch::Reading;

namespace {

void SetVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);  // "" removes it
#else
    if (value[0] == '\0') {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

// Sets a variable for one test and removes it after.
struct Var {
    const char* name;
    Var(const char* n, const char* value) : name(n) { SetVar(name, value); }
    ~Var() { SetVar(name, ""); }
};

} // namespace

TEST_CASE("switch: 0, false, no and off are off, and 1, true, yes and on are on, in any case") {
    for (const char* v : {"0", "false", "FALSE", "No", "off", "OFF", " 0 ", "off\r"}) {
        CHECK_MESSAGE(CoopSwitch::Parse(v) == Reading::off, v);
        CHECK_FALSE(CoopSwitch::From(v, true));
    }
    for (const char* v : {"1", "true", "True", "YES", "on", "On", "\t1"}) {
        CHECK_MESSAGE(CoopSwitch::Parse(v) == Reading::on, v);
        CHECK(CoopSwitch::From(v, false));
    }
}

TEST_CASE("switch: unset, empty and unknown values take the default") {
    CHECK(CoopSwitch::From(nullptr, true));
    CHECK_FALSE(CoopSwitch::From(nullptr, false));
    CHECK(CoopSwitch::Parse("") == Reading::unset);
    CHECK(CoopSwitch::Parse("   ") == Reading::unset);
    CHECK(CoopSwitch::From("", true));
    for (const char* v : {"2", "enabled", "of", "0n", "yes please", "-1"}) {
        CHECK_MESSAGE(CoopSwitch::Parse(v) == Reading::unknown, v);
        CHECK(CoopSwitch::From(v, true));
        CHECK_FALSE(CoopSwitch::From(v, false));
    }
}

TEST_CASE("switch: an unknown value gets one line naming the switch, the value and the default") {
    const auto line = CoopSwitch::Complaint("LICHII_ORM_AGE", "maybe", false);
    REQUIRE(line);
    CHECK(*line == "coop: switch LICHII_ORM_AGE=maybe is not 0/1/true/false/yes/no/on/off: off (its default)");
    CHECK(CoopSwitch::Complaint("LICHII_ORM_AGE", "maybe", true)->ends_with(": on (its default)"));
    CHECK_FALSE(CoopSwitch::Complaint("LICHII_ORM_AGE", "off", true));
    CHECK_FALSE(CoopSwitch::Complaint("LICHII_ORM_AGE", "", true));
}

TEST_CASE("switch: the LICHII_ORM_ name wins, and the old LICHII_COOP_ name is read when it is unset") {
    constexpr const char* kNew = "LICHII_ORM_SWITCH_TEST";
    constexpr const char* kOld = "LICHII_COOP_SWITCH_TEST";
    SetVar(kNew, "");
    SetVar(kOld, "");
    CHECK_FALSE(CoopSwitch::On(kNew, kOld, false));
    CHECK(CoopSwitch::On(kNew, kOld, true));
    {
        Var old_on(kOld, "1");
        CHECK(CoopSwitch::On(kNew, kOld, false));  // a kit of before test9
        CHECK(*CoopSwitch::Value(kNew, kOld) == "1");
        {
            Var new_off(kNew, "no");
            CHECK_FALSE(CoopSwitch::On(kNew, kOld, true));  // the new name decides
            CHECK(*CoopSwitch::Value(kNew, kOld) == "no");
        }
        CHECK_FALSE(CoopSwitch::On(kNew, false));  // without the old name
    }
    {
        Var old_off(kOld, "off");
        CHECK_FALSE(CoopSwitch::On(kNew, kOld, true));
    }
    {
        Var new_bad(kNew, "sometimes");
        CHECK(CoopSwitch::On(kNew, kOld, true));  // unknown: default, logged
        CHECK_FALSE(CoopSwitch::On(kNew, kOld, false));
    }
    CHECK_FALSE(CoopSwitch::Value(kNew, kOld));
    CHECK_FALSE(CoopSwitch::Value(kNew, nullptr));
}

TEST_CASE("switch: Find names the variable that was set, the new name first") {
    constexpr const char* kNew = "LICHII_ORM_SWITCH_FIND_TEST";
    constexpr const char* kOld = "LICHII_COOP_SWITCH_FIND_TEST";
    SetVar(kNew, "");
    SetVar(kOld, "");
    CHECK_FALSE(CoopSwitch::Find(kNew, kOld));
    {
        Var old_set(kOld, "12");
        const auto found = CoopSwitch::Find(kNew, kOld);
        REQUIRE(found);
        CHECK(std::string(found->name) == kOld);
        CHECK(found->value == "12");
        CHECK_FALSE(CoopSwitch::Find(kNew, nullptr));
        {
            Var new_set(kNew, "30");
            const auto both = CoopSwitch::Find(kNew, kOld);
            REQUIRE(both);
            CHECK(std::string(both->name) == kNew);
            CHECK(both->value == "30");
        }
    }
}

TEST_CASE("switch: a selector's log line says what it takes and its default") {
    CHECK(CoopSwitch::Unknown("LICHII_ORM_HELD_SWITCHES", "maybe", "0/1/true/false/yes/no/on/off/log/press", "log") ==
          "coop: switch LICHII_ORM_HELD_SWITCHES=maybe is not 0/1/true/false/yes/no/on/off/log/press: log (its default)");
}

TEST_CASE("switch: the held switches take the on and off words too, and press and log") {
    using CoopHeldSwitch::Mode;
    using CoopHeldSwitch::ParseMode;
    for (const char* v : {"0", "off", "false", "No", "OFF"}) {
        CHECK_MESSAGE(ParseMode(v) == Mode::off, v);
        CHECK(CoopHeldSwitch::KnownMode(v));
    }
    for (const char* v : {"1", "on", "true", "Yes", "press", "PRESS", " press "}) {
        CHECK_MESSAGE(ParseMode(v) == Mode::press, v);
        CHECK(CoopHeldSwitch::KnownMode(v));
    }
    for (const char* v : {"log", "LOG"}) {
        CHECK_MESSAGE(ParseMode(v) == Mode::log, v);
        CHECK(CoopHeldSwitch::KnownMode(v));
    }
    // docs/RIESGOS_RESUELTOS.md: unset (or "flag") presses with the flag path.
    for (const char* v : {"", "flag", "FLAG"}) {
        CHECK_MESSAGE(ParseMode(v) == Mode::flag, v);
        CHECK(CoopHeldSwitch::KnownMode(v));
    }
    for (const char* v : {"2", "pressed", "maybe"}) {
        CHECK_MESSAGE(ParseMode(v) == Mode::flag, v);  // the default, and a log line from the glue
        CHECK_FALSE(CoopHeldSwitch::KnownMode(v));
    }
}
