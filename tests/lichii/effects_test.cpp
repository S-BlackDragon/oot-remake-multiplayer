// Tests for what a puppet shows beyond its copied pose (ORM efectos2,
// lichii/hook/coop_effects.cpp): the rules, the posed command, the flash,
// the status tracker, the stand-in keys and the switches. No emulator code.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "coop_effects.h"

namespace {

using CoopEffects::Command;
using CoopEffects::Rules;
using CoopEffects::Settings;
using CoopEffects::Status;
using CoopEffects::Tracker;

Settings AllOn() {
    Settings s;
    s.as_effects = s.flash = s.status = true;
    return s;
}

std::optional<Command> Cmd(const char* name, float frame = 0.0f) {
    return Command{name, frame, 1.0f, false};
}

bool Has(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& l : lines) {
        if (l.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("effects: a pattern's only wildcard is the star") {
    using CoopEffects::Match;
    CHECK(Match("Magic_*", "Magic_Din_St"));
    CHECK(Match("*Attack*", "Sword_Attack_S_R"));
    CHECK(Match("*Attack*", "Attack"));
    CHECK(Match("Swim*", "Swim"));
    CHECK(Match("a*b*c", "aXbYbZc"));
    CHECK(Match("*_Nayru*", "Damage_Land_Stand_Nayru"));
    CHECK(Match("Wait", "Wait"));
    CHECK_FALSE(Match("Wait", "Wait_01"));
    CHECK_FALSE(Match("Damage_*", "Damage"));
    CHECK_FALSE(Match("a*b*c", "aXbYbZ"));
    CHECK_FALSE(Match("magic_*", "Magic_Din"));  // case counts
    CHECK_FALSE(Match("", "Wait"));
    CHECK(Match("*", ""));
}

TEST_CASE("effects: the built-in rules play casts, swimming and moves, never attacks, damage, items or the horse") {
    const Rules& r = Rules::Default();
    CHECK(r.size() > 40);
    CHECK(r.rest() == "Wait");
    for (const char* yes : {"Magic_Din_St", "Magic_Din", "Magic_Din_Ed", "Magic_Farore_St", "Magic_Farore_Lp",
                            "Magic_Nayru", "Magic_Nayru_Edge_Ed", "Guard_Nayru", "Swim_Move_F", "Wait",
                            "Move_Run_F", "Rolling_Short", "Rolling_Long_Sword", "Land_Run_F_FootL",
                            "Fall", "ClimbMove", "Tunnel_Move_F", "DemoOcarina",
                            "DemoStartFaroreWind", "DemoEndFaroreWindSetting"}) {
        CHECK_MESSAGE(r.Plays(yes), yes);
    }
    for (const char* no : {"Sword_Attack_S_R", "LSword_Attack_Charge_Full", "Damage_M_F", "Damage_Burning",
                           "Damage_Land_Nayru", "Freeze_01", "Die_Land", "Death_01", "Lift_Up_Rock",
                           "Lift_Throw_Box", "Demo_Get_Child", "Dm_Something_Player", "EvtTalk_St",
                           "HorseRideOn_L", "Bow_Attack_Charge", "Hook_Aim", "Boomerang_Throw",
                           "Ocarina_Play", "DemoOcarinaMelodyGet", "DemoOcarinaWarp", "Bottle_Drink",
                           "Fishing_Cast", "Magic_Farore_Warp", "DemoStartFaroreWindWarp", "DemoEndFaroreWind",
                           "Magic_Farore_Cancel", "Guard_Break_Nayru", "Boots_Underwater_Change",
                           "SwimBootsChange", "Cliff_Climb_Ed", "Rolling_Long_Bow", "Movable_Push_F",
                           "Lift_Up_Bomb", "Heavy_Land", "Heavy_Land_Run_F_FootL", "Unknown", ""}) {
        CHECK_MESSAGE(!r.Plays(no), no);
    }
}

// oot-64 D-053 (oot-remake docs/SACUDIDAS.md): a camera shake a player's own
// move makes is his alone. The moves that shake (the hammer's blows, the iron
// boots' landings, a hit or a hard fall on him; names from
// AS/Player_Adult.root.asb) are never played on a puppet, so a mate's puppet
// never shakes our camera.
TEST_CASE("effects: a player's own shaking moves are never played on his puppet (D-053)") {
    const Rules& r = Rules::Default();
    for (const char* no : {"Hammer_Attack_Vertical_Advance", "Hammer_Attack_Vertical_Jump_Land",
                           "Hammer_Attack_Left1_Advance", "Hammer_Attack_Jump", "Pose_Hammer_DamageLand",
                           "Heavy_Land", "Heavy_Land_Walk_F_FootR", "Heavy_Land_Run_Underwater_F_FootR",
                           "LandDamage", "LandDamageStand", "LandDead", "Damage_Though_Hammer"}) {
        CHECK_MESSAGE(!r.Plays(no), no);
    }
    // A plain landing still plays (its dust and steps shake nothing).
    CHECK(r.Plays("Land_Run_BR_FootR"));
}

TEST_CASE("effects: the built-in rules flash on hits, show burn, freeze and shock, and know Nayru's cast") {
    const Rules& r = Rules::Default();
    for (const char* hit : {"Damage_M_F", "Damage_S_B", "Damage_Burning", "Damage_Electric", "Damage_Though_Rock",
                            "LandDamage"}) {
        CHECK_MESSAGE(r.Flashes(hit), hit);
    }
    for (const char* no : {"Damage_Land_Nayru", "Damage_Land_Stand_Nayru", "Damage_Down_F_StandUp",
                           "Freeze_01", "Wait", ""}) {
        CHECK_MESSAGE(!r.Flashes(no), no);
    }
    CHECK(r.StatusOf("Damage_Burning") == Status::burn);
    CHECK(r.StatusOf("Damage_Burning_Horse") == Status::burn);
    CHECK(r.StatusOf("Burning") == Status::burn);
    CHECK(r.StatusOf("Freeze_03") == Status::freeze);
    CHECK(r.StatusOf("FreezeBreak") == Status::freeze);
    CHECK(r.StatusOf("Damage_Electric_St") == Status::shock);
    CHECK(r.StatusOf("Damage_M_F") == Status::none);
    CHECK(r.StatusOf("") == Status::none);
    CHECK(r.CastsNayru("Magic_Nayru_St"));
    CHECK(r.CastsNayru("Magic_Nayru_Edge"));
    CHECK_FALSE(r.CastsNayru("Guard_Nayru"));
    CHECK_FALSE(r.CastsNayru("Magic_Din_St"));
}

TEST_CASE("effects: rules are read line by line, the first match of a kind wins, bad lines are left out") {
    std::vector<std::string> errors;
    const Rules r = Rules::Parse("# a comment\r\n"
                                 "play A*   # an allowed family\r\n"
                                 "skip AB*\n"
                                 "\n"
                                 "  flash Hit*\n"
                                 "noflash HitSoft\n"
                                 "status burn Fire*\n"
                                 "status nayru X\n"
                                 "status cold Ice*\n"
                                 "status freeze\n"
                                 "nayru Cast_*\n"
                                 "rest Idle\n"
                                 "rest\n"
                                 "jump Foo\n"
                                 "play\n"
                                 "play A B\n",
                                 &errors);
    CHECK(r.size() == 6);
    CHECK(r.rest() == "Idle");
    CHECK(r.Plays("ABC"));  // play A* comes first
    CHECK_FALSE(r.Plays("BC"));
    CHECK(r.Flashes("HitHard"));
    CHECK(r.Flashes("HitSoft"));  // flash Hit* comes first
    CHECK(r.StatusOf("Fire_01") == Status::burn);
    CHECK(r.StatusOf("Ice_01") == Status::none);
    CHECK(r.CastsNayru("Cast_1"));
    REQUIRE(errors.size() == 7);
    CHECK(errors[0] == "line 8: 'nayru' is not burn, freeze or shock");
    CHECK(errors[1] == "line 9: 'cold' is not burn, freeze or shock");
    CHECK(errors[2] == "line 10: status wants a status and a pattern");
    CHECK(errors[3] == "line 13: rest wants one command");
    CHECK(errors[4] == "line 14: 'jump' is not play, skip, flash, noflash, status, nayru or rest");
    CHECK(errors[5] == "line 15: play wants one pattern");
    CHECK(errors[6] == "line 16: play wants one pattern");

    const Rules reversed = Rules::Parse("skip AB*\nplay A*\n");
    CHECK_FALSE(reversed.Plays("ABC"));
    CHECK(reversed.Plays("AC"));
    CHECK(Rules::Parse("").size() == 0);
    CHECK_FALSE(Rules::Parse("").Plays("Wait"));  // nothing listed: nothing played
}

TEST_CASE("effects: the posed command is the allowed one, the rest once after it, else nothing") {
    const Rules& r = Rules::Default();
    auto c = CoopEffects::PosedCommand(r, Cmd("Magic_Din_St", 3.0f), "");
    REQUIRE(c);
    CHECK(c->name == "Magic_Din_St");
    CHECK(c->frame == 3.0f);
    CHECK_FALSE(c->rest);
    c = CoopEffects::PosedCommand(r, Cmd("Sword_Attack_S_R"), "Magic_Din_St");
    REQUIRE(c);
    CHECK(c->name == "Wait");
    CHECK(c->rest);
    CHECK_FALSE(CoopEffects::PosedCommand(r, Cmd("Sword_Attack_S_R"), "Wait"));
    CHECK_FALSE(CoopEffects::PosedCommand(r, Cmd("Sword_Attack_S_R"), "Damage_M_F"));  // the AS fallback's
    CHECK_FALSE(CoopEffects::PosedCommand(r, Cmd("Sword_Attack_S_R"), ""));
    c = CoopEffects::PosedCommand(r, std::nullopt, "Swim_Move_F");  // no command known now
    REQUIRE(c);
    CHECK(c->rest);
    // The list's own rest command, once.
    const Rules idle = Rules::Parse("play A*\nrest Idle\n");
    CHECK(CoopEffects::PosedCommand(idle, Cmd("B"), "A1")->name == "Idle");
    CHECK_FALSE(CoopEffects::PosedCommand(idle, Cmd("B"), "Idle"));
}

TEST_CASE("effects: the flash blinks every other kBlinkMs for kFlashMs") {
    using CoopEffects::FlashHidden;
    using CoopEffects::Flashing;
    using CoopEffects::kBlinkMs;
    using CoopEffects::kFlashMs;
    CHECK_FALSE(Flashing(0, 5000));
    CHECK_FALSE(FlashHidden(0, 5000));
    const std::uint64_t since = 1000;
    CHECK_FALSE(Flashing(since, since - 1));  // not yet (another thread's older clock)
    CHECK(Flashing(since, since));
    CHECK_FALSE(FlashHidden(since, since));
    CHECK_FALSE(FlashHidden(since, since + kBlinkMs - 1));
    CHECK(FlashHidden(since, since + kBlinkMs));
    CHECK(FlashHidden(since, since + 2 * kBlinkMs - 1));
    CHECK_FALSE(FlashHidden(since, since + 2 * kBlinkMs));
    CHECK(Flashing(since, since + kFlashMs - 1));
    CHECK_FALSE(Flashing(since, since + kFlashMs));
    CHECK_FALSE(FlashHidden(since, since + kFlashMs + kBlinkMs));
    CHECK(Flashing(since, since + kFlashMs + 50, 100));
    CHECK_FALSE(Flashing(since, since + kFlashMs + 100, 100));
}

TEST_CASE("effects: a new hit flashes once, a chained hit or one while flashing does not start it again") {
    Tracker t;
    const Settings s = AllOn();
    CHECK(t.Update(s, 2, "Kokiri", "Wait", false, 1000).flash_since_ms == 0u);
    auto look = t.Update(s, 2, "Kokiri", "Damage_M_F", false, 1100);
    CHECK(look.flash_since_ms == 1100u);
    CHECK(t.Said(2) == "flash ('Damage_M_F')");
    CHECK(t.Update(s, 2, "Kokiri", "Damage_M_F", false, 1150).flash_since_ms == 1100u);  // the same, sampled again
    CHECK(t.Said(2).empty());
    CHECK(t.Update(s, 2, "Kokiri", "Damage_Down_F_StandUp", false, 1200).flash_since_ms == 1100u);
    CHECK(t.Update(s, 2, "Kokiri", "Wait", false, 1300).flash_since_ms == 1100u);
    CHECK(t.Update(s, 2, "Kokiri", "Damage_S_F", false, 1500).flash_since_ms == 1100u);  // still flashing
    CHECK(t.Update(s, 2, "Kokiri", "Wait", false, 1100 + CoopEffects::kFlashMs).flash_since_ms == 0u);
    CHECK(t.Update(s, 2, "Kokiri", "Damage_S_B", false, 2500).flash_since_ms == 2500u);
    // No command known: nothing changes.
    CHECK(t.Update(s, 2, "Kokiri", std::nullopt, false, 2600).flash_since_ms == 2500u);
    // The flash switch off: no blink; another player is apart.
    Settings no_flash = AllOn();
    no_flash.flash = false;
    Tracker u;
    CHECK(u.Update(no_flash, 3, "Kokiri", "Damage_M_F", false, 100).flash_since_ms == 0u);
    CHECK(t.Update(s, 3, "Kokiri", "Wait", false, 2600).flash_since_ms == 0u);
    CHECK(t.size() == 2);
}

TEST_CASE("effects: burn and shock last at least their minimum, frozen exactly its commands") {
    Tracker t;
    const Settings s = AllOn();
    CHECK(t.Update(s, 2, "F", "Damage_Burning", false, 0).status == Status::burn);
    CHECK(t.Update(s, 2, "F", "Damage_Burning", false, 400).status == Status::burn);
    CHECK(t.Update(s, 2, "F", "Wait", false, 500).status == Status::burn);
    CHECK(t.Update(s, 2, "F", "Wait", false, 400 + CoopEffects::kBurnMinMs - 1).status == Status::burn);
    CHECK(t.Update(s, 2, "F", "Wait", false, 400 + CoopEffects::kBurnMinMs).status == Status::none);
    CHECK(t.Said(2) == "shows none (was burn)");

    CHECK(t.Update(s, 2, "F", "Freeze_01", false, 5000).status == Status::freeze);
    CHECK(t.Update(s, 2, "F", "FreezeBreak", false, 5400).status == Status::freeze);
    CHECK(t.Update(s, 2, "F", "Wait", false, 5420).status == Status::none);

    CHECK(t.Update(s, 2, "F", "Damage_Electric", false, 6000).status == Status::shock);
    CHECK(t.Update(s, 2, "F", "Wait", false, 6100).status == Status::shock);
    CHECK(t.Update(s, 2, "F", "Wait", false, 6000 + CoopEffects::kShockMinMs).status == Status::none);

    // A burn ends at a scene change and while downed; the status switch off shows none.
    CHECK(t.Update(s, 2, "F", "Damage_Burning", false, 7000).status == Status::burn);
    CHECK(t.Update(s, 2, "G", "Wait", false, 7100).status == Status::none);
    CHECK(t.Update(s, 2, "G", "Damage_Burning", true, 7200).status == Status::none);
    Settings off = AllOn();
    off.status = false;
    Tracker u;
    const auto look = u.Update(off, 2, "F", "Damage_Burning", false, 10);
    CHECK(look.status == Status::none);
    CHECK(look.flash_since_ms == 10u);  // the flash has its own switch
    CHECK(u.Update(off, 2, "F", "Wait", false, 20).status == Status::none);
}

TEST_CASE("effects: Nayru's crystal comes after the cast, for its time, and goes with a hit, a load or a fall") {
    Settings s = AllOn();
    s.nayru_ms = 20000;
    Tracker t;
    CHECK(t.Update(s, 4, "F", "Magic_Nayru_St", false, 1000).status == Status::none);  // casting
    CHECK(t.Update(s, 4, "F", "Magic_Nayru", false, 1500).status == Status::none);
    auto look = t.Update(s, 4, "F", "Wait", false, 2000);
    CHECK(look.status == Status::nayru);
    CHECK(t.Said(4) == "crystal on for 20 s; shows nayru (was none)");
    // A hit it blocks keeps it; frozen shows over it.
    CHECK(t.Update(s, 4, "F", "Damage_Land_Nayru", false, 3000).status == Status::nayru);
    CHECK(t.Update(s, 4, "F", "Freeze_01", false, 3100).status == Status::freeze);
    CHECK(t.Update(s, 4, "F", "Wait", false, 3200).status == Status::nayru);
    CHECK(t.Update(s, 4, "F", "Wait", false, 2000 + 20000 - 1).status == Status::nayru);
    CHECK(t.Update(s, 4, "F", "Wait", false, 2000 + 20000).status == Status::none);
    CHECK(t.Said(4) == "crystal off (time); shows none (was nayru)");

    // A hit it did not block: it is gone.
    t.Update(s, 4, "F", "Magic_Nayru", false, 30000);
    CHECK(t.Update(s, 4, "F", "Wait", false, 30100).status == Status::nayru);
    CHECK(t.Update(s, 4, "F", "Damage_M_F", false, 30200).status == Status::none);
    CHECK(t.Said(4).find("crystal off (hurt)") != std::string::npos);
    // A scene change.
    t.Update(s, 4, "F", "Magic_Nayru", false, 40000);
    CHECK(t.Update(s, 4, "F", "Wait", false, 40100).status == Status::nayru);
    CHECK(t.Update(s, 4, "G", "Wait", false, 40200).status == Status::none);
    CHECK(t.Said(4).find("crystal off (scene)") != std::string::npos);
    // Downed.
    t.Update(s, 4, "G", "Magic_Nayru", false, 50000);
    CHECK(t.Update(s, 4, "G", "Wait", false, 50100).status == Status::nayru);
    CHECK(t.Update(s, 4, "G", "Wait", true, 50200).status == Status::none);
    CHECK(t.Said(4).find("crystal off (downed)") != std::string::npos);
    // A cast cut short by a load does not raise it in the new scene.
    t.Update(s, 4, "G", "Magic_Nayru", false, 60000);
    CHECK(t.Update(s, 4, "H", "Wait", false, 60100).status == Status::none);
}

TEST_CASE("effects: a player not planned for kForgetMs is forgotten") {
    Tracker t;
    const Settings s = AllOn();
    t.Update(s, 2, "F", "Wait", false, 1000);
    t.Update(s, 3, "F", "Wait", false, 5000);
    t.Prune(1000 + CoopEffects::kForgetMs - 1);
    CHECK(t.size() == 2);
    t.Prune(1000 + CoopEffects::kForgetMs);
    CHECK(t.size() == 1);
    CHECK(t.Said(2).empty());
}

TEST_CASE("effects: each status has its stand-in, and each stand-in copy its own key") {
    CHECK(CoopEffects::StandInFor(Status::burn) == "CoopFx_Burn");
    CHECK(CoopEffects::StandInFor(Status::freeze) == "CoopFx_Freeze");
    CHECK(CoopEffects::StandInFor(Status::shock) == "CoopFx_Shock");
    CHECK(CoopEffects::StandInFor(Status::nayru) == "CoopFx_Nayru");
    CHECK(CoopEffects::StandInFor(Status::none).empty());
    for (const auto name : CoopEffects::kFxStandIns) {
        CHECK(CoopEffects::IsFxStandIn(name));
    }
    CHECK_FALSE(CoopEffects::IsFxStandIn("CoopShot_DinFire"));
    CHECK(CoopEffects::StatusNamed("shock") == Status::shock);
    CHECK_FALSE(CoopEffects::StatusNamed("none"));
    CHECK_FALSE(CoopEffects::StatusNamed("Burn"));

    std::set<std::uint64_t> keys;
    for (std::uint8_t n = 1; n <= 6; ++n) {
        for (const Status st : {Status::burn, Status::freeze, Status::shock, Status::nayru}) {
            for (const std::uint16_t gen : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{65535}}) {
                const std::uint64_t k = CoopEffects::FxKey(n, st, gen);
                CHECK((k & CoopEffects::kFxKeyBit) != 0u);
                CHECK((k >> 62) == 0u);  // never the enemies' (bit 63) or the drops' (bit 62)
                keys.insert(k);
            }
        }
    }
    CHECK(keys.size() == 6u * 4u * 3u);
    const auto f = CoopEffects::FxAt(3, Status::freeze, {1.0f, 2.0f, 3.0f}, 7);
    CHECK(f.actor == "CoopFx_Freeze");
    CHECK(f.key == CoopEffects::FxKey(3, Status::freeze, 7));
    CHECK(f.position[0] == 1.0f);
    CHECK(f.position[1] == doctest::Approx(2.0f + CoopEffects::kFxLift));
    CHECK(f.position[2] == 3.0f);
    CHECK(CoopEffects::FxKey(3, Status::freeze, 7, 0) == CoopEffects::FxKey(3, Status::freeze, 7));
    CHECK(CoopEffects::FxKey(3, Status::freeze, 7, 1) != CoopEffects::FxKey(3, Status::freeze, 7));
    CHECK((CoopEffects::FxKey(6, Status::nayru, 65535, 65535) >> 61) == 0u);  // never the chest finds' bit
}

// ORM review (REVISION_FINAL.md "Visual"): the spawn director makes a key
// once, so a status shown again needs a key of its own.
TEST_CASE("effects: a status shown again gets a new stand-in key, one kept plan after plan keeps its own") {
    using Shown = CoopEffects::FxKeys::Shown;
    CoopEffects::FxKeys keys;
    const auto first = keys.Plan("F", {Shown{2, Status::burn}, Shown{4, Status::nayru}});
    REQUIRE(first.size() == 2);
    CHECK(first[0] != first[1]);
    // Still burning, still crystal: the same copies.
    CHECK(keys.Plan("F", {Shown{2, Status::burn}, Shown{4, Status::nayru}}) == first);
    // P2's burn is over, P4's crystal stays.
    const auto between = keys.Plan("F", {Shown{4, Status::nayru}});
    REQUIRE(between.size() == 1);
    CHECK(between[0] == first[1]);
    // P2 burns again in the same scene: a new key (the old one is never made again).
    const auto again = keys.Plan("F", {Shown{2, Status::burn}, Shown{4, Status::nayru}});
    REQUIRE(again.size() == 2);
    CHECK(again[0] != first[0]);
    CHECK(again[1] == first[1]);
    // Our scene changes: every key is new.
    const auto moved = keys.Plan("G", {Shown{2, Status::burn}, Shown{4, Status::nayru}});
    REQUIRE(moved.size() == 2);
    CHECK(moved[0] != again[0]);
    CHECK(moved[1] != again[1]);
    std::set<std::uint64_t> all{first[0], first[1], again[0], moved[0], moved[1]};
    CHECK(all.size() == 5u);
    for (const std::uint64_t k : all) {
        CHECK((k & CoopEffects::kFxKeyBit) != 0u);
    }
}

TEST_CASE("effects: the switches are on unless set off, with a line for what cannot be read") {
    std::vector<std::string> notes;
    Settings s = CoopEffects::FromValues(nullptr, nullptr, nullptr, nullptr, nullptr, std::nullopt, notes);
    CHECK(s.as_effects);
    CHECK(s.flash);
    CHECK(s.status);
    CHECK(s.nayru_ms == Settings::kNayruDefaultMs);
    CHECK(s.rules.size() == Rules::Default().size());
    REQUIRE(notes.size() == 1);
    CHECK(notes[0] == "coop: effects: commands under the pose on, damage flash on, burn/freeze/shock and "
                      "Nayru's crystal on (60 s crystal)");

    notes.clear();
    s = CoopEffects::FromValues("0", "off", "maybe", "30", nullptr, std::nullopt, notes);
    CHECK_FALSE(s.as_effects);
    CHECK_FALSE(s.flash);
    CHECK(s.status);  // not a switch value: its default
    CHECK(s.nayru_ms == 30000u);
    CHECK(Has(notes, "coop: switch LICHII_ORM_STATUS_FX=maybe is not 0/1/true/false/yes/no/on/off: on (its default)"));

    for (const char* bad : {"0", "601", "abc", "-5", "12x", "99999999999"}) {
        notes.clear();
        s = CoopEffects::FromValues(nullptr, nullptr, nullptr, bad, nullptr, std::nullopt, notes);
        CHECK_MESSAGE(s.nayru_ms == Settings::kNayruDefaultMs, bad);
        CHECK_MESSAGE(Has(notes, std::string("LICHII_ORM_NAYRU_S=") + bad + " is not 1 to 600 seconds"), bad);
    }

    notes.clear();
    s = CoopEffects::FromValues(nullptr, nullptr, nullptr, nullptr, "my_rules.txt",
                                std::string("play Swim*\nbogus line\nrest Idle\n"), notes);
    CHECK(s.rules.size() == 1);
    CHECK(s.rules.rest() == "Idle");
    CHECK(s.rules.Plays("Swim_Move_F"));
    CHECK_FALSE(s.rules.Plays("Magic_Din_St"));
    CHECK(Has(notes, "coop: effects: my_rules.txt line 2: 'bogus' is not play"));
    CHECK(Has(notes, "coop: effects: rules from my_rules.txt (1 lines, rest 'Idle')"));

    notes.clear();
    s = CoopEffects::FromValues(nullptr, nullptr, nullptr, nullptr, "missing.txt", std::nullopt, notes);
    CHECK(s.rules.size() == Rules::Default().size());
    CHECK(Has(notes, "LICHII_ORM_EFFECTS_LIST=missing.txt cannot be read: the built-in rules are used"));
}

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): Nayru's crystal
// lasts what the game's NayruLoveTime says (read by the puppet pack).
#include "coop_check_visual.h"

TEST_CASE("effects (riesgos-visual): the crystal's time from the pack, the switch, or the estimate") {
    CHECK(CoopEffects::NayruSecondsIn("{\"effects\": true, \"nayru_s\": 34.0, \"tables\": []}") == 34.0);
    CHECK(CoopEffects::NayruSecondsIn("{\"nayru_s\":12}") == 12.0);
    CHECK_FALSE(CoopEffects::NayruSecondsIn("{\"nayru_s\": null}"));
    CHECK_FALSE(CoopEffects::NayruSecondsIn("{\"nayru_s\": 0.5}"));
    CHECK_FALSE(CoopEffects::NayruSecondsIn("{\"nayru_s\": 9000}"));
    CHECK_FALSE(CoopEffects::NayruSecondsIn("{\"sound\": true}"));
    std::vector<std::string> checks;
    CoopVCheck::ResetForTest();
    CoopVCheck::SetSinkForTest([&](const std::string& line) { checks.push_back(line); });
    CoopEffects::Settings s;
    CoopEffects::PickNayru(s, false, 34.0);
    CHECK(s.nayru_ms == 34000);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0] ==
          "coop: check: nayru crystal ok 34.0 s, the game's NayruLoveTime (PlayerGameParam, read by the puppet pack)");
    CoopVCheck::ResetForTest();
    checks.clear();
    CoopEffects::Settings old;
    CoopEffects::PickNayru(old, false, std::nullopt);
    CHECK(old.nayru_ms == CoopEffects::Settings::kNayruDefaultMs);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].rfind("coop: check: nayru crystal fallback 60 s, the estimate", 0) == 0);
    CoopVCheck::ResetForTest();
    checks.clear();
    CoopEffects::Settings set;
    set.nayru_ms = 20000;
    CoopEffects::PickNayru(set, true, 34.0);  // the switch wins over the pack
    CHECK(set.nayru_ms == 20000);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0] == "coop: check: nayru crystal ok 20 s as LICHII_ORM_NAYRU_S sets it");
    CoopVCheck::SetSinkForTest({});
    CoopVCheck::ResetForTest();
}
