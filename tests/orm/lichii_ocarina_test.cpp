// ORM cases moved out of upstream's tests/lichii/ocarina_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "coop_ocarina.h"

TEST_CASE("ocarina: a note or song node of Link's AS names its note or song (ORM issue 152)") {
    using CoopOcarina::CueOf;
    using CoopOcarina::CueText;
    auto cue = CueOf("Ocarina_Child_A_lp");
    REQUIRE(cue);
    CHECK(cue->note == CoopOcarina::NoteIndex("A"));
    CHECK(cue->song == 0);
    CHECK_FALSE(cue->rest);
    CHECK(CueText(*cue) == "note A");
    cue = CueOf("Ocarina_Adult_HighDsharp_Close_1st_lp");  // the first frames of a held note
    REQUIRE(cue);
    CHECK(CueText(*cue) == "note HighDsharp");
    cue = CueOf("Ocarina_Child_Fsharp_Rest");
    REQUIRE(cue);
    CHECK(cue->rest);
    CHECK(CueText(*cue) == "rest Fsharp");
    cue = CueOf("Ocarina_Adult_ZeldaLullaby");
    REQUIRE(cue);
    CHECK(cue->song == CoopOcarina::SongIndex("ZeldaLullaby"));
    CHECK(CueText(*cue) == "song ZeldaLullaby");
    CHECK(CueText(*CueOf("Ocarina_Child_LightPrelude")) == "song LightPrelude");
    // The other ocarina nodes say nothing about a note.
    for (const char* other : {"Ocarina", "OcarinaReadyStart", "Ocarina_Child_StartWait", "Ocarina_Child_Close_lp",
                              "Ocarina_Child_Close_1st_lp", "Ocarina_Child_Blow_lp", "Ocarina_Child_Face_lp",
                              "Ocarina_Child_B_Wait", "Ocarina_Child_D_PlayWait", "Ocarina_Child_ZeldaLullaby_Wait",
                              "Ocarina_Child_End", "DemoOcarina", "Wait", ""}) {
        CAPTURE(other);
        CHECK_FALSE(CueOf(other));
    }
    CHECK(CoopOcarina::SongNames().size() == 13);
    CHECK(CoopOcarina::SongIndex("Nothing") == 0);
}
