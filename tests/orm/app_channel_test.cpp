// ORM: the in-process channel between the hook and the Cooperativo screen
// (lichii/hook/orm_app_channel.h, oot-remake docs/COMANDOS.md "La app").
#include <doctest/doctest.h>

#include <ostream>

#include <string>
#include <utility>
#include <vector>

#include "orm_app_channel.h"

using namespace OrmAppChannel;

TEST_CASE("app channel: with no app listening the hook is told, and nothing is queued") {
    Reset();
    CHECK_FALSE(Listening());
    CHECK_FALSE(Send({Ask::upload_logs, {}}));
    CHECK(TakeAnswers().empty());
}

TEST_CASE("app channel: the app gets each ask, answers come back for the chat, a new listener replaces the old") {
    Reset();
    std::vector<Request> got;
    Listen([&got](const Request& r) { got.push_back(r); });
    CHECK(Listening());
    CHECK(Send({Ask::save_name, "Anju-PC"}));
    CHECK(Send({Ask::retry_downloads, {}}));
    REQUIRE(got.size() == 2);
    CHECK(got[0].ask == Ask::save_name);
    CHECK(got[0].text == "Anju-PC");
    CHECK(got[1].ask == Ask::retry_downloads);
    Answer("Registros enviados");
    Answer("La app guarda tu nombre nuevo: Anju-PC");
    CHECK(TakeAnswers() == std::vector<std::string>{"Registros enviados", "La app guarda tu nombre nuevo: Anju-PC"});
    CHECK(TakeAnswers().empty());
    int other = 0;
    Listen([&other](const Request&) { ++other; });
    CHECK(Send({Ask::upload_logs, {}}));
    CHECK(other == 1);
    CHECK(got.size() == 2);
    Listen({});
    CHECK_FALSE(Send({Ask::upload_logs, {}}));
    for (int i = 0; i < 100; ++i) Answer("x");  // held to a few: a stuck hook never grows it
    CHECK(TakeAnswers().size() == 32);
}

TEST_CASE("app channel: the switches go out with a version that steps only on a change, and the toggles come in") {
    Reset();
    CHECK(SwitchesVersion() == 0);
    Switches s;
    s.connected = true;
    s.pvp = true;
    PublishSwitches(s);
    CHECK(SwitchesVersion() == 1);
    PublishSwitches(s);  // the same: no redraw
    CHECK(SwitchesVersion() == 1);
    CHECK(CurrentSwitches() == s);
    s.lock_available = false;
    PublishSwitches(s);
    CHECK(SwitchesVersion() == 2);
    CHECK_FALSE(CurrentSwitches().lock_available);

    CHECK_FALSE(SetSwitch(1, true));  // no setter yet: not sent
    std::vector<std::pair<int, bool>> asked;
    SetSwitchSetter([&asked](std::uint8_t key, bool on) {
        asked.emplace_back(key, on);
        return key == 1;
    });
    CHECK(SetSwitch(1, false));
    CHECK_FALSE(SetSwitch(2, true));  // the setter says it was not sent
    REQUIRE(asked.size() == 2);
    CHECK(asked[1] == std::pair<int, bool>{2, true});
    Reset();
}
