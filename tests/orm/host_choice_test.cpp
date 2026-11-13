// ORM one host for the room, the game's side (coop/host/host_choice.hpp,
// lichii/hook/coop_host_choice.cpp) and HOST_CHOICE's wire format (type 254).
// The relay's choice is tested in tests/server/host_choice_test.cpp.

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <string>
#include <vector>

#include "coop/host/host_choice.hpp"
#include "coop/protocol/codec.hpp"
#include "coop_host_choice.h"

using namespace coop::protocol;
namespace hc = coop::host::host_choice;

namespace {

const std::vector<hc::Player> kRoom{{1, "Ana"}, {2, "Saria"}, {3, "Sam"}, {4, "Mido"}};

}  // namespace

// ------------------------------------------------------------ the wire

TEST_CASE("host choice: HOST_CHOICE has its number, channel and senders") {
  CHECK(static_cast<int>(MessageType::host_choice) == 0xF00E);
  CHECK(channel_of(MessageType::host_choice) == kChannelSession);
  CHECK_FALSE(allowed_on_channel(MessageType::host_choice, kChannelProgress));
  CHECK(may_send(Sender::client, MessageType::host_choice));
  CHECK(may_send(Sender::server, MessageType::host_choice));
  const HostChoice m{HostChoiceKind::told, HostWish::automatic, HostWhy::measured, 3, 0, 42, "Sam", ""};
  CHECK(check_received(Message{m}, Sender::server, kChannelSession) == Error::none);
  CHECK(check_received(Message{m}, Sender::client, kChannelSession) == Error::none);  // the server judges the kind
}

TEST_CASE("host choice: the layout is u8 kind, wish, why, host, by, u16 rtt, two names") {
  const HostChoice m{HostChoiceKind::told, HostWish::never, HostWhy::picked, 2, 5, 0x0102, "Ab", "C"};
  std::vector<std::uint8_t> b;
  REQUIRE(encode(Message{m}, b) == Error::none);
  REQUIRE(b.size() == 4 + 7 + 2 + 2 + 2 + 1);
  CHECK(b[0] == 0x0E);
  CHECK(b[1] == 0xF0);
  CHECK(b[4] == 3);  // told
  CHECK(b[5] == 2);  // never
  CHECK(b[6] == 5);  // picked
  CHECK(b[7] == 2);
  CHECK(b[8] == 5);
  CHECK(b[9] == 0x02);
  CHECK(b[10] == 0x01);
  CHECK(b[11] == 2);  // "Ab"
  CHECK(b[13] == 'A');
  // A kind and a why this build does not know are kept.
  b[4] = 9;
  b[6] = 200;
  Message back;
  REQUIRE(decode(b, back) == Error::none);
  CHECK(static_cast<int>(std::get<HostChoice>(back).kind) == 9);
  CHECK(static_cast<int>(std::get<HostChoice>(back).why) == 200);
  // A host past six, or a name past 16 characters, is refused.
  b[7] = 7;
  CHECK(decode(b, back) == Error::bad_value);
  HostChoice long_name = m;
  long_name.name = std::string(17, 'x');
  std::vector<std::uint8_t> nb;
  CHECK(encode(Message{long_name}, nb) != Error::none);
}

// ------------------------------------------------------------ the command

TEST_CASE("host choice: /anfitrion and /host, with nothing, a name or auto") {
  CHECK_FALSE(hc::parse("hola"));
  CHECK_FALSE(hc::parse("/teleport Ana"));
  CHECK_FALSE(hc::parse("/anfitriona"));
  auto c = hc::parse("  /Anfitrion  ");
  REQUIRE(c);
  CHECK(c->kind == hc::Command::Kind::show);
  c = hc::parse("/anfitrion  Saria ");
  REQUIRE(c);
  CHECK(c->kind == hc::Command::Kind::pick);
  CHECK(c->name == "Saria");
  c = hc::parse("/anfitri\xC3\xB3n sam");
  REQUIRE(c);
  CHECK(c->kind == hc::Command::Kind::pick);
  CHECK(c->name == "sam");
  c = hc::parse("/HOST auto");
  REQUIRE(c);
  CHECK(c->kind == hc::Command::Kind::automatic);
  c = hc::parse("/anfitrion Autom\xC3\xA1tico");
  REQUIRE(c);
  CHECK(c->kind == hc::Command::Kind::automatic);
}

TEST_CASE("host choice: the name is matched whole, by a unique start, or by number, ourselves too") {
  CHECK(hc::find_player("saria", kRoom).name == "Saria");
  CHECK(hc::find_player("Mi", kRoom).name == "Mido");
  CHECK(hc::find_player("ana", kRoom).name == "Ana");  // whole name before a start
  CHECK(hc::find_player("P3", kRoom).name == "Sam");
  CHECK(hc::find_player("4", kRoom).name == "Mido");
  const auto several = hc::find_player("S", kRoom);
  CHECK(several.name.empty());
  CHECK(several.error.find("Saria, Sam") != std::string::npos);
  const auto nobody = hc::find_player("Ruto", kRoom);
  CHECK(nobody.name.empty());
  CHECK(nobody.error.find("Ruto") != std::string::npos);
  CHECK(hc::find_player("  ", kRoom).name.empty());
}

TEST_CASE("host choice: LICHII_ORM_HOST words") {
  CHECK(hc::wish_from("me") == HostWish::me);
  CHECK(hc::wish_from(" YO ") == HostWish::me);
  CHECK(hc::wish_from("1") == HostWish::me);
  CHECK(hc::wish_from("never") == HostWish::never);
  CHECK(hc::wish_from("nunca") == HostWish::never);
  CHECK(hc::wish_from("0") == HostWish::never);
  CHECK(hc::wish_from("") == HostWish::automatic);
  CHECK(hc::wish_from("auto") == HostWish::automatic);
  CHECK(hc::wish_from("maybe") == HostWish::automatic);
  CHECK(hc::wish_word(HostWish::me) == "me");
  CHECK(hc::wish_word(HostWish::never) == "never");
  CHECK(hc::wish_word(HostWish::automatic) == "auto");
}

// ------------------------------------------------------------ the lines

TEST_CASE("host choice: the chat lines say who hosts and why, in Spanish") {
  HostChoice t{HostChoiceKind::told, HostWish::automatic, HostWhy::measured, 2, 0, 38, "Saria", ""};
  CHECK(hc::told_line(t, 1, kRoom) == "Ahora el anfitri\xC3\xB3n es Saria (mejor conexi\xC3\xB3n: 38 ms de media)");
  CHECK(hc::told_line(t, 2, kRoom) == "Eres el anfitri\xC3\xB3n de la sala (mejor conexi\xC3\xB3n: 38 ms de media)");
  t.why = HostWhy::picked;
  t.by = 4;
  t.pinned = "Saria";
  CHECK(hc::told_line(t, 1, kRoom) == "Ahora el anfitri\xC3\xB3n es Saria (lo eligi\xC3\xB3 Mido)");
  CHECK(hc::told_line(t, 4, kRoom) == "Ahora el anfitri\xC3\xB3n es Saria (lo elegiste t\xC3\xBA)");
  t.why = HostWhy::left;
  CHECK(hc::told_line(t, 1, kRoom).find("el anterior se fue") != std::string::npos);
  HostChoice refused{HostChoiceKind::told, HostWish::automatic, HostWhy::refused, 1, 3, 0, "Ruto", ""};
  const std::string r = hc::told_line(refused, 3, kRoom);
  CHECK(r.find("Ruto") != std::string::npos);
  CHECK(r.find("Sigue Ana") != std::string::npos);
  HostChoice wish{HostChoiceKind::wish, HostWish::me, HostWhy::first, 0, 0, 0, "", ""};
  CHECK(hc::told_line(wish, 1, kRoom).empty());
}

TEST_CASE("host choice: /anfitrion alone says who, why and how to change it") {
  const HostChoice t{HostChoiceKind::told, HostWish::automatic, HostWhy::picked, 3, 1, 0, "Sam", "Sam"};
  const std::string s = hc::status_line(t, 3, 1, kRoom);
  CHECK(s.find("El anfitri\xC3\xB3n es Sam") == 0);
  CHECK(s.find("lo elegiste") != std::string::npos);
  CHECK(s.find("Fijado a mano: Sam") != std::string::npos);
  CHECK(s.find("/anfitrion auto") != std::string::npos);
  const std::string me = hc::status_line(std::nullopt, 1, 1, kRoom);
  CHECK(me.find("Eres el anfitri\xC3\xB3n") == 0);
  CHECK(hc::status_line(std::nullopt, 0, 1, kRoom).find("Sin conexi") == 0);
  HostChoice automatic = t;
  automatic.pinned.clear();
  automatic.why = HostWhy::first;
  CHECK(hc::status_line(automatic, 3, 1, kRoom).find("Se elige solo por la conexi") != std::string::npos);
}

TEST_CASE("host choice: the host file for the launcher") {
  CHECK(hc::host_file("Ana", "Saria", false, HostWhy::measured, 1700000000) ==
        "self = Ana\nhost = Saria\nhosting = 0\nwhy = measured\nat = 1700000000\n");
  CHECK(hc::host_file("Ana", "Ana", true, HostWhy::picked, 5) ==
        "self = Ana\nhost = Ana\nhosting = 1\nwhy = picked\nat = 5\n");
  // A name never breaks a line.
  CHECK(hc::host_file("A\nb", "c\rd", true, HostWhy::first, 0).find("self = A b\nhost = c d\n") == 0);
}

// ------------------------------------------------------------ the glue

TEST_CASE("host choice: the hook's glue does nothing without a session") {
  CoopHostChoice::Reset();
  CoopHostChoice::Frame(nullptr, 1, 1);
  CHECK(CoopHostChoice::TakeLines().empty());
}
