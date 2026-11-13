#include "coop/host/orm_commands.hpp"

#include <algorithm>

#include "coop/protocol/messages.hpp"  // valid_new_nickname

namespace coop::host::orm::commands {

namespace {

constexpr std::string_view kUnknown = "Comando desconocido, escribe /ayuda";

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool same(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

bool starts(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && same(text.substr(0, prefix.size()), prefix);
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

// UTF-8 "í" and "Í" folded to 'i', so "sí", "SÍ", "dónde" read as ASCII.
std::string fold(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xc3 && i + 1 < s.size()) {
      const auto d = static_cast<unsigned char>(s[i + 1]);
      char plain = 0;
      switch (d) {
        case 0xa1: case 0x81: plain = 'a'; break;  // á Á
        case 0xa9: case 0x89: plain = 'e'; break;  // é É
        case 0xad: case 0x8d: plain = 'i'; break;  // í Í
        case 0xb3: case 0x93: plain = 'o'; break;  // ó Ó
        case 0xba: case 0x9a: plain = 'u'; break;  // ú Ú
        default: break;
      }
      if (plain != 0) {
        out.push_back(plain);
        ++i;
        continue;
      }
    }
    out.push_back(lower(static_cast<char>(c)));
  }
  return out;
}

constexpr std::string_view kHelpAliases[] = {"help", "comandos", "?"};
constexpr std::string_view kLockAliases[] = {"fijaraliados", "lock"};
constexpr std::string_view kPlayersAliases[] = {"players", "quien"};
constexpr std::string_view kWhereAliases[] = {"where"};
constexpr std::string_view kGoAliases[] = {"teleport", "tp", "go"};
constexpr std::string_view kBringAliases[] = {"bring", "llamar"};
constexpr std::string_view kGatherAliases[] = {"gather"};
constexpr std::string_view kAcceptAliases[] = {"accept"};
constexpr std::string_view kNoAliases[] = {"-"};  // never typed: '-' is no name
constexpr std::string_view kNetAliases[] = {"net"};
constexpr std::string_view kMapAliases[] = {"map"};
constexpr std::string_view kColorAliases[] = {"colour"};
constexpr std::string_view kSpectateAliases[] = {"mirar", "spectate"};
constexpr std::string_view kLogsAliases[] = {"logs"};
constexpr std::string_view kTimeAliases[] = {"time"};
constexpr std::string_view kRetryAliases[] = {"retry"};
constexpr std::string_view kMuteAliases[] = {"mute"};
constexpr std::string_view kNameAliases[] = {"name", "nick"};
constexpr std::string_view kRupeesAliases[] = {"rupees"};
constexpr std::string_view kNamesAliases[] = {"names", "etiquetas"};
constexpr std::string_view kHostAliases[] = {"host"};
constexpr std::string_view kBodiesAliases[] = {"cuerpos", "bodies"};
// As OrmHud::PlaceName (lichii/hook/coop_hud_settings.cpp) writes them.
constexpr std::string_view kHudPlaces[] = {"arriba-izquierda", "arriba", "arriba-derecha",
                                           "abajo-izquierda",  "abajo",  "abajo-derecha"};
constexpr std::string_view kHudSettings[] = {"nombres", "rupias", "panel", "tamano", "chat", "reset"};

std::span<const std::string_view> none() { return std::span<const std::string_view>(kNoAliases, 0); }

const Spec kSpecs[] = {
    {Id::help, "ayuda", kHelpAliases, Arg::command_opt, Who::anyone, false, "/ayuda [comando]",
     "esta lista, o la de un comando"},
    {Id::pvp, "pvp", none(), Arg::toggle_opt, Who::anyone, true, "/pvp [on|off]",
     "las armas de un jugador dañan a los demás (toda la sala; lo cambia el anfitrión); solo: cómo está"},
    {Id::lock_mates, "fijar", kLockAliases, Arg::toggle_opt, Who::anyone, true, "/fijar [on|off]",
     "fijar con Z a los compañeros como a un enemigo (toda la sala); solo: cómo está"},
    {Id::bodies, "choque", kBodiesAliases, Arg::toggle_opt, Who::anyone, true, "/choque [on|off]",
     "los Links chocan entre sí y puedes subirte encima de un compañero (toda la sala); solo: cómo está"},
    {Id::players, "jugadores", kPlayersAliases, Arg::none, Who::anyone, false, "/jugadores",
     "quién está: nombre, zona, en pie o caído, ping"},
    {Id::where, "donde", kWhereAliases, Arg::player, Who::anyone, false, "/donde <nombre>",
     "dónde está ese jugador"},
    {Id::go, "ir", kGoAliases, Arg::player_opt, Who::anyone, false, "/ir <nombre>",
     "ir junto a un compañero (no en una cinemática ni en un jefe)"},
    {Id::bring, "traer", kBringAliases, Arg::player, Who::anyone, false, "/traer <nombre>",
     "pedirle a un compañero que venga (él acepta con /aceptar)"},
    {Id::gather, "reunir", kGatherAliases, Arg::none, Who::host, false, "/reunir",
     "el anfitrión llama a todos a su sitio (cada uno acepta)"},
    {Id::accept, "aceptar", kAcceptAliases, Arg::none, Who::anyone, false, "/aceptar",
     "ir junto a quien te ha llamado"},
    {Id::host, "anfitrion", kHostAliases, Arg::host_opt, Who::anyone, true, "/anfitrion [nombre|auto]",
     "quién es el anfitrión; con un nombre, que lo sea él; auto: lo elige el servidor"},
    {Id::ping, "ping", none(), Arg::toggle_opt, Who::anyone, false, "/ping [on|off]",
     "latencia al servidor y a cada compañero; on/off: siempre en pantalla"},
    {Id::fps, "fps", none(), Arg::toggle_opt, Who::anyone, false, "/fps [on|off]",
     "los fps del juego en pantalla"},
    {Id::net, "red", kNetAliases, Arg::toggle_opt, Who::anyone, false, "/red [on|off]",
     "subida y bajada en kbps y pérdida en pantalla"},
    {Id::stats, "stats", none(), Arg::toggle_opt, Who::anyone, false, "/stats [on|off]",
     "todo junto en pantalla; solo: un informe en el chat"},
    {Id::hud, "hud", none(), Arg::words_opt, Who::anyone, false,
     "/hud [on|off|nombres|rupias|panel|tamano|chat|reset]",
     "on/off: el panel del cooperativo (como F9); un ajuste: tu HUD (panel <sitio>, tamano <50-300>, "
     "chat <0-6>, reset); solo: cómo lo tienes"},
    {Id::rupees, "rupias", kRupeesAliases, Arg::words_opt, Who::anyone, false, "/rupias [on|off|<sitio>]",
     "las rupias de todos en una línea fija, y dónde (arriba, abajo-derecha...)"},
    {Id::names, "nombres", kNamesAliases, Arg::toggle_opt, Who::anyone, false, "/nombres [on|off]",
     "los nombres sobre los compañeros"},
    {Id::map, "mapa", kMapAliases, Arg::toggle_opt, Who::anyone, false, "/mapa [on|off]",
     "los compañeros en el mapa"},
    {Id::color, "color", kColorAliases, Arg::color, Who::anyone, false, "/color <#RRGGBB|nombre>",
     "tu color, al momento"},
    {Id::spectate, "espectar", kSpectateAliases, Arg::player, Who::downed, false, "/espectar <nombre>",
     "caído: mirar a ese compañero"},
    {Id::logs, "registros", kLogsAliases, Arg::none, Who::anyone, false, "/registros",
     "subir tus registros ahora"},
    {Id::version, "version", none(), Arg::none, Who::anyone, false, "/version",
     "versión de la app, del kit y del protocolo"},
    {Id::time, "hora", kTimeAliases, Arg::none, Who::anyone, false, "/hora", "la hora del día compartida"},
    {Id::retry, "reintentar", kRetryAliases, Arg::none, Who::anyone, false, "/reintentar",
     "reintentar las descargas (con la app abierta)"},
    {Id::mute, "silencio", kMuteAliases, Arg::toggle_opt, Who::anyone, false, "/silencio [on|off]",
     "no ver el chat de los demás en tu pantalla"},
    {Id::name, "nombre", kNameAliases, Arg::text, Who::anyone, true, "/nombre <nuevo>",
     "cambiar tu nombre al momento (1-16 letras, números, . _ -)"},
};

bool takes_arg(Arg arg) { return arg != Arg::none; }

std::vector<std::string_view> toggle_words() { return {"on", "off"}; }

}  // namespace

std::span<const Spec> all() { return kSpecs; }

const Spec& spec(Id id) {
  for (const Spec& s : kSpecs) {
    if (s.id == id) return s;
  }
  return kSpecs[0];
}

const Spec* find(std::string_view name) {
  if (!name.empty() && name.front() == '/') name.remove_prefix(1);
  const std::string folded = fold(name);
  if (folded.empty()) return nullptr;
  for (const Spec& s : kSpecs) {
    if (same(s.name, folded)) return &s;
    for (const std::string_view a : s.aliases) {
      if (same(a, folded)) return &s;
    }
  }
  return nullptr;
}

std::optional<bool> parse_toggle(std::string_view word) {
  const std::string w = fold(trim(word));
  constexpr std::string_view kOn[] = {"on", "true", "si", "1", "activar", "encender", "activado", "yes"};
  constexpr std::string_view kOff[] = {"off", "false", "no", "0", "desactivar", "apagar", "desactivado"};
  for (const std::string_view v : kOn) {
    if (w == v) return true;
  }
  for (const std::string_view v : kOff) {
    if (w == v) return false;
  }
  return std::nullopt;
}

Parsed parse(std::string_view line) {
  Parsed out;
  const std::string_view text = trim(line);
  if (text.empty() || text.front() != '/') return out;  // chat
  std::string_view rest = text.substr(1);
  const std::size_t space = rest.find_first_of(" \t");
  const std::string_view name = space == std::string_view::npos ? rest : rest.substr(0, space);
  const std::string_view arg = space == std::string_view::npos ? std::string_view() : trim(rest.substr(space));
  out.spec = name.empty() || name.front() == '/' ? nullptr : find(name);  // "//pvp" is no command
  if (out.spec == nullptr) {
    out.kind = Parsed::Kind::unknown;
    out.error = std::string(kUnknown);
    return out;
  }
  out.arg = std::string(arg);
  const Spec& s = *out.spec;
  const auto bad = [&](std::string why) {
    out.kind = Parsed::Kind::bad_args;
    out.error = why.empty() ? "Uso: " + std::string(s.usage) : std::move(why) + ". Uso: " + std::string(s.usage);
  };
  switch (s.arg) {
    case Arg::none:
      if (!arg.empty()) { bad(""); return out; }
      break;
    case Arg::toggle_opt:
      if (!arg.empty()) {
        out.toggle = parse_toggle(arg);
        if (!out.toggle) { bad("\"" + std::string(arg) + "\" no es on ni off"); return out; }
      }
      break;
    case Arg::words_opt:  // any other word goes to the command's own code
      if (!arg.empty()) out.toggle = parse_toggle(arg);
      break;
    case Arg::player:
    case Arg::color:
    case Arg::text:
      if (arg.empty()) { bad(""); return out; }
      if (s.arg == Arg::color && !parse_color(arg)) {
        { bad("\"" + std::string(arg) + "\" no es un color"); return out; }
      }
      break;
    case Arg::player_opt:
    case Arg::command_opt:
    case Arg::host_opt:
      break;
  }
  out.kind = Parsed::Kind::ok;
  return out;
}

std::optional<std::string> refuse(const Spec& s, const Context& context) {
  const bool needs_room = s.id != Id::help && s.id != Id::fps && s.id != Id::hud && s.id != Id::map &&
                          s.id != Id::version && s.id != Id::logs && s.id != Id::retry && s.id != Id::mute &&
                          s.id != Id::stats && s.id != Id::net && s.id != Id::rupees && s.id != Id::names;
  if (needs_room && !context.connected) return "No estás conectado a una sala";
  if (s.who == Who::host && !context.host) return "Solo el anfitrión puede usar /" + std::string(s.name);
  if (s.who == Who::downed && !context.downed) return "/" + std::string(s.name) + " es para cuando estás caído";
  return std::nullopt;
}

std::string help_line(const Spec& s) { return std::string(s.usage) + " - " + std::string(s.help); }

std::vector<std::string> help_lines(std::string_view about) {
  std::vector<std::string> out;
  about = trim(about);
  if (!about.empty()) {
    const Spec* s = find(about);
    if (s == nullptr) return {std::string(kUnknown)};
    out.push_back(help_line(*s));
    if (!s->aliases.empty()) {
      std::string line = "También:";
      for (const std::string_view a : s->aliases) line += " /" + std::string(a);
      out.push_back(line);
    }
    return out;
  }
  out.push_back("Comandos (Tab completa, flechas eligen):");
  for (const Spec& s : kSpecs) out.push_back(help_line(s));
  return out;
}

Match find_player(std::string_view wanted, std::span<const PlayerName> players, std::uint8_t self,
                  bool allow_self) {
  Match out;
  wanted = trim(wanted);
  if (wanted.empty()) {
    out.error = "Falta el nombre del jugador";
    return out;
  }
  const auto pick = [&](const PlayerName& p) {
    if (p.number == self && !allow_self) {
      out.error = "Ese eres tú";
      return;
    }
    out.number = p.number;
    out.name = p.name.empty() ? "P" + std::to_string(p.number) : p.name;
  };
  for (const PlayerName& p : players) {
    if (!p.name.empty() && same(p.name, wanted)) {
      pick(p);
      return out;
    }
  }
  const PlayerName* only = nullptr;
  int count = 0;
  for (const PlayerName& p : players) {
    if (!p.name.empty() && starts(p.name, wanted) && (allow_self || p.number != self)) {
      only = &p;
      ++count;
    }
  }
  if (count == 1) {
    pick(*only);
    return out;
  }
  if (count > 1) {
    out.error = "\"" + std::string(wanted) + "\" puede ser más de un jugador: escribe más letras";
    return out;
  }
  std::string_view digits = wanted;
  if (!digits.empty() && (digits.front() == 'P' || digits.front() == 'p')) digits.remove_prefix(1);
  if (digits.size() == 1 && digits[0] >= '1' && digits[0] <= '9') {
    const auto number = static_cast<std::uint8_t>(digits[0] - '0');
    for (const PlayerName& p : players) {
      if (p.number == number) {
        pick(p);
        return out;
      }
    }
  }
  out.error = "No hay ningún jugador \"" + std::string(wanted) + "\" en la sala";
  return out;
}

namespace {

struct NamedColor {
  std::string_view name;
  std::array<std::uint8_t, 3> rgb;
};
constexpr NamedColor kColors[] = {
    {"rojo", {230, 60, 60}},     {"azul", {70, 130, 240}},    {"verde", {70, 200, 90}},
    {"amarillo", {240, 210, 60}}, {"naranja", {245, 150, 50}}, {"morado", {160, 90, 220}},
    {"rosa", {240, 120, 190}},   {"blanco", {240, 240, 240}}, {"negro", {40, 40, 40}},
    {"cian", {60, 210, 220}},    {"gris", {150, 150, 150}},   {"red", {230, 60, 60}},
    {"blue", {70, 130, 240}},    {"green", {70, 200, 90}},    {"yellow", {240, 210, 60}},
    {"orange", {245, 150, 50}},  {"purple", {160, 90, 220}},  {"pink", {240, 120, 190}},
    {"white", {240, 240, 240}},  {"black", {40, 40, 40}},     {"cyan", {60, 210, 220}},
    {"grey", {150, 150, 150}},   {"gray", {150, 150, 150}},
};
constexpr std::string_view kColorNames[] = {"rojo", "azul",  "verde",  "amarillo", "naranja", "morado",
                                             "rosa", "blanco", "negro", "cian",     "gris"};

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::optional<std::array<std::uint8_t, 3>> parse_color(std::string_view text) {
  text = trim(text);
  if (text.size() == 7 && text[0] == '#') {
    std::array<std::uint8_t, 3> rgb{};
    for (int i = 0; i < 3; ++i) {
      const int hi = hex(text[1 + 2 * i]);
      const int lo = hex(text[2 + 2 * i]);
      if (hi < 0 || lo < 0) return std::nullopt;
      rgb[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return rgb;
  }
  const std::string folded = fold(text);
  for (const NamedColor& c : kColors) {
    if (folded == c.name) return c.rgb;
  }
  return std::nullopt;
}

std::span<const std::string_view> color_names() { return kColorNames; }

std::span<const std::string_view> hud_place_names() { return kHudPlaces; }
std::span<const std::string_view> hud_setting_words() { return kHudSettings; }

std::optional<std::string> check_new_name(std::string_view name, std::span<const PlayerName> players,
                                          std::uint8_t self) {
  name = trim(name);
  if (!protocol::valid_new_nickname(name)) {
    return "Nombre no válido: de 1 a 16 letras, números, punto, guion o guion bajo (sin espacios ni tildes)";
  }
  for (const PlayerName& p : players) {
    if (p.number != self && same(p.name, name)) return "Ese nombre ya lo usa otro jugador";
  }
  for (const PlayerName& p : players) {
    if (p.number == self && p.name == name) return "Ya te llamas así";
  }
  return std::nullopt;
}

std::string set_ini_value(std::string_view text, std::string_view key, std::string_view value) {
  std::string out;
  bool done = false;
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t end = text.find('\n', at);
    const bool last = end == std::string_view::npos;
    if (last) end = text.size();
    std::string_view line = text.substr(at, end - at);
    const bool cr = !line.empty() && line.back() == '\r';
    std::string_view body = trim(line);
    bool replaced = false;
    if (!done && !body.empty() && body.front() != '#' && body.front() != ';' && body.front() != '[') {
      const std::size_t eq = body.find('=');
      if (eq != std::string_view::npos && same(trim(body.substr(0, eq)), key)) {
        out += std::string(key) + " = " + std::string(value);
        if (cr) out += '\r';
        replaced = true;
        done = true;
      }
    }
    if (!replaced) out += std::string(line);
    if (!last) out += '\n';
    at = last ? text.size() : end + 1;
  }
  if (!done) {
    const bool crlf = text.find("\r\n") != std::string_view::npos;
    if (!out.empty() && out.back() != '\n') out += crlf ? "\r\n" : "\n";
    out += std::string(key) + " = " + std::string(value) + (crlf ? "\r\n" : "\n");
  }
  return out;
}

// ---- Suggestions ------------------------------------------------------------

std::vector<Suggestion> complete(std::string_view text, std::span<const PlayerName> players,
                                 std::uint8_t self, const Context& context) {
  std::vector<Suggestion> out;
  // Leading spaces are ignored, as parse() does; trailing ones matter (the
  // space after a name opens its arguments).
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  if (text.empty() || text.front() != '/') return out;
  const std::string_view rest = text.substr(1);
  const std::size_t space = rest.find(' ');
  if (space == std::string_view::npos) {
    const std::string prefix = fold(rest);
    for (const Spec& s : kSpecs) {
      if (refuse(s, context)) continue;
      bool hit = starts(s.name, prefix);
      for (const std::string_view a : s.aliases) hit = hit || starts(a, prefix);
      if (!hit) continue;
      out.push_back({"/" + std::string(s.name) + (takes_arg(s.arg) ? " " : ""), std::string(s.usage),
                     std::string(s.help)});
    }
    return out;
  }
  const Spec* s = find(rest.substr(0, space));
  if (s == nullptr) return out;
  std::string_view arg = rest.substr(space + 1);
  while (!arg.empty() && arg.front() == ' ') arg.remove_prefix(1);
  const std::string head = "/" + std::string(s->name) + " ";
  const auto add = [&](std::string_view word, std::string help) {
    if (starts(word, arg)) out.push_back({head + std::string(word), std::string(word), std::move(help)});
  };
  switch (s->arg) {
    case Arg::toggle_opt:
      for (const std::string_view w : toggle_words()) add(w, w == "on" ? "activar" : "desactivar");
      break;
    case Arg::player:
    case Arg::player_opt:
    case Arg::host_opt: {
      const bool self_too = s->id == Id::where || s->arg == Arg::host_opt;
      for (const PlayerName& p : players) {
        if (p.name.empty() || (p.number == self && !self_too)) continue;
        add(p.name, "P" + std::to_string(p.number));
      }
      if (s->arg == Arg::host_opt) add("auto", "lo elige el servidor por las conexiones");
      break;
    }
    case Arg::words_opt: {
      for (const std::string_view w : toggle_words()) {
        add(w, s->id == Id::hud ? (w == "on" ? "mostrar el panel (F9)" : "ocultar el panel (F9)")
                                : (w == "on" ? "activar" : "desactivar"));
      }
      if (s->id == Id::rupees) {
        for (const std::string_view place : kHudPlaces) add(place, "la línea de rupias ahí");
      } else if (s->id == Id::hud) {
        // The words first; after "panel " or "rupias ", the places.
        const std::size_t gap = arg.find(' ');
        if (gap == std::string_view::npos) {
          for (const std::string_view w : kHudSettings) add(w, "");
        } else {
          const std::string first = fold(arg.substr(0, gap));
          if (first == "panel" || first == "rupias") {
            for (const std::string_view place : kHudPlaces) add(first + " " + std::string(place), "");
          }
        }
      }
      break;
    }
    case Arg::command_opt:
      for (const Spec& c : kSpecs) add(c.name, std::string(c.help));
      break;
    case Arg::color:
      for (const std::string_view c : kColorNames) add(c, "");
      add("#RRGGBB", "un color en hexadecimal");
      break;
    case Arg::none:
    case Arg::text:
      break;
  }
  return out;
}

void Completer::typed(std::string text, std::vector<Suggestion> list) {
  text_ = std::move(text);
  list_ = std::move(list);
  selected_ = -1;
  moved_ = false;
}

void Completer::move(int delta) {
  if (list_.empty()) return;
  const int n = static_cast<int>(list_.size());
  if (selected_ < 0) {
    selected_ = delta > 0 ? 0 : n - 1;
  } else {
    selected_ = ((selected_ + delta) % n + n) % n;
  }
  moved_ = true;
}

std::optional<std::string> Completer::tab() {
  if (list_.empty()) return std::nullopt;
  const int n = static_cast<int>(list_.size());
  // A repeated Tab (the box holds what the last Tab put there) goes on to the
  // next; the first one takes the selected or the first.
  if (!moved_ && selected_ >= 0 && text_ == list_[static_cast<std::size_t>(selected_)].insert) {
    selected_ = (selected_ + 1) % n;
  } else if (selected_ < 0) {
    selected_ = 0;
  }
  moved_ = false;
  text_ = list_[static_cast<std::size_t>(selected_)].insert;
  return text_;
}

std::optional<std::string> Completer::enter() const {
  if (!moved_ || selected_ < 0 || list_.empty()) return std::nullopt;
  return list_[static_cast<std::size_t>(selected_)].insert;
}

void Completer::clear() {
  text_.clear();
  list_.clear();
  selected_ = -1;
  moved_ = false;
}

}  // namespace coop::host::orm::commands
