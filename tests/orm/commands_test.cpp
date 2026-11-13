// ORM chat commands (coop/host/orm_commands.hpp, oot-remake docs/COMANDOS.md):
// the registry, the parser, permissions, /ayuda, players by name, colours,
// names, the ini edit and the completion engine.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <set>
#include <string>
#include <vector>

#include "coop/host/orm_commands.hpp"

using namespace coop::host::orm::commands;

namespace {

const std::vector<PlayerName> kRoom = {{1, "Anju"}, {2, "Saria"}, {3, "Sam"}, {4, "Mido"}};

}  // namespace

TEST_CASE("commands: every name and alias is unique and lower case, every command has help") {
  std::set<std::string> seen;
  for (const Spec& s : all()) {
    CHECK_FALSE(s.help.empty());
    CHECK(s.usage.substr(0, 1 + s.name.size()) == "/" + std::string(s.name));
    CHECK(seen.insert(std::string(s.name)).second);
    for (const auto a : s.aliases) CHECK(seen.insert(std::string(a)).second);
    for (const char c : s.name) CHECK_FALSE((c >= 'A' && c <= 'Z'));
    CHECK(&spec(s.id) == &s);
  }
  CHECK(all().size() == 27);  // with orm/hud2's /rupias, /nombres, orm/anfitrion's /anfitrion and /choque
}

TEST_CASE("commands: /choque, the room's bodies switch (ORM choque, docs/CHOQUE.md)") {
  const Parsed on = parse("/choque on");
  REQUIRE(on.kind == Parsed::Kind::ok);
  CHECK(on.spec->id == Id::bodies);
  CHECK(on.spec->room);
  REQUIRE(on.toggle);
  CHECK(*on.toggle);
  CHECK(parse("/cuerpos off").spec->id == Id::bodies);
  CHECK(parse("/bodies").kind == Parsed::Kind::ok);  // alone: how it is
  CHECK_FALSE(parse("/bodies").toggle);
  CHECK(parse("/choque quizas").kind == Parsed::Kind::bad_args);
  Context ctx;
  CHECK_FALSE(refuse(spec(Id::bodies), ctx));
  ctx.connected = false;
  CHECK(refuse(spec(Id::bodies), ctx) == std::string("No estás conectado a una sala"));
  // Tab: the name, then on and off.
  ctx.connected = true;
  const auto ch = complete("/ch", kRoom, 1, ctx);
  REQUIRE(ch.size() == 1);
  CHECK(ch[0].insert == "/choque ");
  CHECK(ch[0].label == "/choque [on|off]");
  const auto args = complete("/choque ", kRoom, 1, ctx);
  REQUIRE(args.size() == 2);
  CHECK(args[0].insert == "/choque on");
  CHECK(args[1].insert == "/choque off");
  // /ayuda lists it, and /ayuda choque gives its line and aliases.
  bool listed = false;
  for (const auto& line : help_lines()) listed = listed || line.rfind("/choque [on|off] - ", 0) == 0;
  CHECK(listed);
  const auto one = help_lines("choque");
  REQUIRE(one.size() == 2);
  CHECK(one[1] == "También: /cuerpos /bodies");
}

TEST_CASE("commands: chat is not a command, a slash always is") {
  CHECK(parse("hola").kind == Parsed::Kind::not_command);
  CHECK(parse("  hola /pvp").kind == Parsed::Kind::not_command);
  CHECK(parse("").kind == Parsed::Kind::not_command);
  const Parsed unknown = parse("/xyz on");
  CHECK(unknown.kind == Parsed::Kind::unknown);
  CHECK(unknown.error == "Comando desconocido, escribe /ayuda");
  CHECK(parse("/").kind == Parsed::Kind::unknown);
  CHECK(parse("//pvp").kind == Parsed::Kind::unknown);
}

TEST_CASE("commands: pvp and fijar take every form of on and off, and nothing shows the state") {
  for (const char* on : {"on", "ON", "true", "si", "s\xc3\xad", "S\xc3\x8d", "1", "activar", "encender"}) {
    const Parsed p = parse(std::string("/pvp ") + on);
    REQUIRE(p.kind == Parsed::Kind::ok);
    CHECK(p.spec->id == Id::pvp);
    CHECK(p.toggle == true);
  }
  for (const char* off : {"off", "Off", "false", "no", "0", "desactivar", "apagar"}) {
    const Parsed p = parse(std::string("/fijar ") + off);
    REQUIRE(p.kind == Parsed::Kind::ok);
    CHECK(p.spec->id == Id::lock_mates);
    CHECK(p.toggle == false);
  }
  const Parsed show = parse("  /PvP  ");
  REQUIRE(show.kind == Parsed::Kind::ok);
  CHECK_FALSE(show.toggle.has_value());
  const Parsed bad = parse("/pvp tal vez");
  CHECK(bad.kind == Parsed::Kind::bad_args);
  CHECK(bad.error.find("Uso: /pvp [on|off]") != std::string::npos);
  CHECK(parse("/fijaraliados on").spec->id == Id::lock_mates);
}

TEST_CASE("commands: arguments are checked by kind") {
  CHECK(parse("/jugadores ahora").kind == Parsed::Kind::bad_args);
  CHECK(parse("/donde").kind == Parsed::Kind::bad_args);
  CHECK(parse("/donde Saria").kind == Parsed::Kind::ok);
  CHECK(parse("/donde Saria").arg == "Saria");
  CHECK(parse("/ir").kind == Parsed::Kind::ok);  // alone: daurosk's list
  CHECK(parse("/tp sa").spec->id == Id::go);
  CHECK(parse("/teleport sa").spec->id == Id::go);
  CHECK(parse("/color").kind == Parsed::Kind::bad_args);
  CHECK(parse("/color #12ab9F").kind == Parsed::Kind::ok);
  CHECK(parse("/color violeta claro").kind == Parsed::Kind::bad_args);
  CHECK(parse("/nombre").kind == Parsed::Kind::bad_args);
  CHECK(parse("/nombre Anju-PC").arg == "Anju-PC");
  CHECK(parse("/d\xc3\xb3nde Sam").spec->id == Id::where);  // "dónde" is folded to "donde"
}

TEST_CASE("commands: permissions: host only, downed only, connection") {
  Context guest;
  CHECK(refuse(spec(Id::gather), guest) == "Solo el anfitrión puede usar /reunir");
  Context host;
  host.host = true;
  CHECK_FALSE(refuse(spec(Id::gather), host));
  CHECK(refuse(spec(Id::spectate), guest).has_value());
  Context down;
  down.downed = true;
  CHECK_FALSE(refuse(spec(Id::spectate), down));
  Context offline;
  offline.connected = false;
  CHECK(refuse(spec(Id::pvp), offline) == "No estás conectado a una sala");
  CHECK_FALSE(refuse(spec(Id::fps), offline));
  CHECK_FALSE(refuse(spec(Id::help), offline));
  CHECK(spec(Id::pvp).room);
  CHECK(spec(Id::name).room);
  CHECK_FALSE(spec(Id::fps).room);
}

TEST_CASE("commands: /ayuda lists every command, or one with its aliases") {
  const auto lines = help_lines();
  CHECK(lines.size() == all().size() + 1);
  CHECK(lines[2].substr(0, 4) == "/pvp");
  const auto one = help_lines("tp");
  REQUIRE(one.size() == 2);
  CHECK(one[0].substr(0, 3) == "/ir");
  CHECK(one[1] == "También: /teleport /tp /go");
  CHECK(help_lines("nada") == std::vector<std::string>{"Comando desconocido, escribe /ayuda"});
}

TEST_CASE("commands: players by whole name, unique start, number, never ourselves") {
  CHECK(find_player("saria", kRoom, 1).number == 2);
  CHECK(find_player("mi", kRoom, 1).number == 4);
  const Match two = find_player("sa", kRoom, 1);
  CHECK(two.number == 0);
  CHECK(two.error.find("más de un jugador") != std::string::npos);
  CHECK(find_player("Sam", kRoom, 1).number == 3);  // whole name wins over the start of Saria
  CHECK(find_player("P3", kRoom, 1).number == 3);
  CHECK(find_player("4", kRoom, 1).number == 4);
  CHECK(find_player("anju", kRoom, 1).error == "Ese eres tú");
  CHECK(find_player("anju", kRoom, 1, true).number == 1);
  CHECK(find_player("Zelda", kRoom, 1).error == "No hay ningún jugador \"Zelda\" en la sala");
  CHECK(find_player("al", kRoom, 1).number == 0);  // only ourselves starts so
}

TEST_CASE("commands: colours by hex and by name") {
  CHECK(parse_color("#FF8000") == std::array<std::uint8_t, 3>{255, 128, 0});
  CHECK(parse_color("#ff8000") == std::array<std::uint8_t, 3>{255, 128, 0});
  CHECK(parse_color("Rojo").has_value());
  CHECK(parse_color("blue").has_value());
  CHECK_FALSE(parse_color("#FF80"));
  CHECK_FALSE(parse_color("#GG8000"));
  CHECK_FALSE(parse_color("violeta"));
}

TEST_CASE("commands: a new name follows the app's rule and is not another player's") {
  CHECK_FALSE(check_new_name("Anju-PC", kRoom, 1));
  CHECK_FALSE(check_new_name("a.b_c-9", kRoom, 1));
  CHECK(check_new_name("saria", kRoom, 1) == "Ese nombre ya lo usa otro jugador");
  CHECK(check_new_name("Anju", kRoom, 1) == "Ya te llamas así");
  CHECK_FALSE(check_new_name("ANJU", kRoom, 1));  // our own in another case: allowed
  CHECK(check_new_name("con espacio", kRoom, 1).has_value());
  CHECK(check_new_name("Mart\xc3\xadn", kRoom, 1).has_value());
  CHECK(check_new_name("abcdefghijklmnopq", kRoom, 1).has_value());  // 17
  CHECK_FALSE(check_new_name("abcdefghijklmnop", kRoom, 1));         // 16
}

TEST_CASE("commands: the ini edit keeps everything else") {
  const std::string ini = "# mine\nserver = 1.2.3.4\r\nName=Old ; a comment\nport = 27510\n";
  CHECK(set_ini_value(ini, "name", "Anju-PC") == "# mine\nserver = 1.2.3.4\r\nname = Anju-PC\nport = 27510\n");
  CHECK(set_ini_value("server = x", "name", "A") == "server = x\nname = A\n");
  CHECK(set_ini_value("server = x\r\n", "name", "A") == "server = x\r\nname = A\r\n");
  CHECK(set_ini_value("", "name", "A") == "name = A\n");
  CHECK(set_ini_value("# name = commented\n", "name", "A") == "# name = commented\nname = A\n");
}

TEST_CASE("completion: a slash lists the commands the player may run, a start narrows them") {
  Context ctx;
  const auto every = complete("/", kRoom, 1, ctx);
  CHECK(every.size() == all().size() - 2);  // /reunir (host) and /espectar (downed) left out
  const auto fp = complete("/fp", kRoom, 1, ctx);
  REQUIRE(fp.size() == 1);
  CHECK(fp[0].insert == "/fps ");
  CHECK(fp[0].label == "/fps [on|off]");
  CHECK_FALSE(fp[0].help.empty());
  const auto tp = complete("/tel", kRoom, 1, ctx);  // by alias: the command's own name
  REQUIRE(tp.size() == 1);
  CHECK(tp[0].insert == "/ir ");
  CHECK(complete("/jug", kRoom, 1, ctx)[0].insert == "/jugadores");  // no argument: no space
  CHECK(complete("hola", kRoom, 1, ctx).empty());
  CHECK(complete("/zz", kRoom, 1, ctx).empty());
  ctx.host = true;
  CHECK(complete("/reu", kRoom, 1, ctx).size() == 1);
}

TEST_CASE("completion: after the name, the arguments") {
  Context ctx;
  const auto toggles = complete("/fps ", kRoom, 1, ctx);
  REQUIRE(toggles.size() == 2);
  CHECK(toggles[0].insert == "/fps on");
  CHECK(toggles[1].insert == "/fps off");
  const auto o = complete("/pvp of", kRoom, 1, ctx);
  REQUIRE(o.size() == 1);
  CHECK(o[0].insert == "/pvp off");
  const auto names = complete("/ir ", kRoom, 1, ctx);
  REQUIRE(names.size() == 3);  // not ourselves
  CHECK(names[0].insert == "/ir Saria");
  const auto sa = complete("/traer sa", kRoom, 1, ctx);
  REQUIRE(sa.size() == 2);
  CHECK(sa[1].label == "Sam");
  CHECK(complete("/donde ", kRoom, 1, ctx).size() == 4);  // ourselves too
  CHECK(complete("/ayuda pv", kRoom, 1, ctx)[0].insert == "/ayuda pvp");
  CHECK(complete("/color ro", kRoom, 1, ctx)[0].insert == "/color rojo");
  CHECK(complete("/nombre ", kRoom, 1, ctx).empty());
  CHECK(complete("/tp m", kRoom, 1, ctx)[0].insert == "/ir Mido");
}

TEST_CASE("completion: Tab takes the first, Tab again cycles, the arrows choose, Enter runs the chosen") {
  Completer c;
  Context ctx;
  c.typed("/f", complete("/f", kRoom, 1, ctx));
  REQUIRE(c.list().size() == 2);  // /fps, /fijar
  CHECK(c.selected() == -1);
  CHECK_FALSE(c.enter());
  CHECK(c.tab() == c.list()[0].insert);
  CHECK(c.tab() == c.list()[1].insert);
  CHECK(c.tab() == c.list()[0].insert);  // round again
  c.typed("/f", complete("/f", kRoom, 1, ctx));
  c.move(-1);  // up from none: the last
  CHECK(c.selected() == 1);
  c.move(1);
  CHECK(c.selected() == 0);
  CHECK(c.enter() == c.list()[0].insert);
  CHECK(c.tab() == c.list()[0].insert);  // Tab takes the chosen one
  CHECK_FALSE(c.enter());                // after Tab, Enter runs the box
  c.typed("/zz", {});
  CHECK_FALSE(c.tab());
  c.move(1);
  CHECK(c.selected() == -1);
}

// ORM orm/hud2 and orm/anfitrion in the registry (docs/HUD_AJUSTES.md,
// PARTIDA_SALA.md): /hud on|off is the panel, any other word a HUD setting
// that coop_hud_settings.cpp checks; /rupias takes a place too; /anfitrion a
// player (ourselves too) or auto.
TEST_CASE("commands: hud, rupias, nombres and anfitrion are in the registry") {
  const Parsed panel = parse("/hud off");
  REQUIRE(panel.kind == Parsed::Kind::ok);
  CHECK(panel.spec->id == Id::hud);
  CHECK(panel.toggle == false);
  const Parsed setting = parse("/HUD panel abajo-derecha");
  REQUIRE(setting.kind == Parsed::Kind::ok);
  CHECK(setting.spec->id == Id::hud);
  CHECK_FALSE(setting.toggle);
  CHECK(setting.arg == "panel abajo-derecha");
  CHECK(parse("/hud").kind == Parsed::Kind::ok);
  const Parsed rupees = parse("/rupias abajo");
  REQUIRE(rupees.kind == Parsed::Kind::ok);
  CHECK(rupees.spec->id == Id::rupees);
  CHECK_FALSE(rupees.toggle);
  CHECK(parse("/rupias no").toggle == false);
  CHECK(parse("/rupees on").spec->id == Id::rupees);
  CHECK(parse("/nombres off").spec->id == Id::names);
  CHECK(parse("/nombres x").kind == Parsed::Kind::bad_args);
  CHECK(parse("/nombre x").spec->id == Id::name);  // /nombre is still the rename
  const Parsed host = parse("/anfitrion Saria");
  REQUIRE(host.kind == Parsed::Kind::ok);
  CHECK(host.spec->id == Id::host);
  CHECK(host.arg == "Saria");
  CHECK(parse("/host auto").spec->id == Id::host);
  CHECK(parse("/anfitrion").kind == Parsed::Kind::ok);
  CHECK(spec(Id::host).room);
  CHECK_FALSE(spec(Id::rupees).room);
  // Local: run without a room; /anfitrion needs one.
  Context away;
  away.connected = false;
  CHECK_FALSE(refuse(spec(Id::hud), away));
  CHECK_FALSE(refuse(spec(Id::rupees), away));
  CHECK_FALSE(refuse(spec(Id::names), away));
  CHECK(refuse(spec(Id::host), away) == "No estás conectado a una sala");
  bool listed = false;
  for (const auto& line : help_lines()) listed = listed || line.rfind("/anfitrion [nombre|auto] - ", 0) == 0;
  CHECK(listed);
  CHECK(help_lines("host")[1] == "También: /host");
}

TEST_CASE("completion: hud settings and places, rupias places, anfitrion names and auto") {
  Context ctx;
  const auto hud = complete("/hud ", kRoom, 1, ctx);
  REQUIRE(hud.size() == 2 + hud_setting_words().size());
  CHECK(hud[0].insert == "/hud on");
  CHECK(hud[2].insert == "/hud nombres");
  const auto ta = complete("/hud ta", kRoom, 1, ctx);
  REQUIRE(ta.size() == 1);
  CHECK(ta[0].insert == "/hud tamano");
  const auto places = complete("/hud panel ab", kRoom, 1, ctx);
  REQUIRE(places.size() == 3);
  CHECK(places[0].insert == "/hud panel abajo-izquierda");
  CHECK(complete("/hud chat ", kRoom, 1, ctx).empty());
  const auto rupees = complete("/rupias arriba-", kRoom, 1, ctx);
  REQUIRE(rupees.size() == 2);
  CHECK(rupees[1].insert == "/rupias arriba-derecha");
  CHECK(complete("/rupias ", kRoom, 1, ctx).size() == 2 + hud_place_names().size());
  const auto host = complete("/anfitrion ", kRoom, 1, ctx);
  REQUIRE(host.size() == 5);  // the four players, ourselves too, and auto
  CHECK(host[0].insert == "/anfitrion Anju");
  CHECK(host[4].insert == "/anfitrion auto");
  CHECK(complete("/anfitrion a", kRoom, 1, ctx).size() == 2);  // Anju, auto
  const auto n = complete("/nombre", kRoom, 1, ctx);
  REQUIRE(n.size() == 2);  // /nombre and /nombres
  CHECK(n[0].insert == "/nombres ");  // in /ayuda's order: /nombres comes with the HUD
}
