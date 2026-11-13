// ORM one host for the room: the game's side (coop/host/host_choice.hpp).

#include "coop/host/host_choice.hpp"

#include <vector>

namespace coop::host::host_choice {

using protocol::HostChoice;
using protocol::HostChoiceKind;
using protocol::HostWhy;
using protocol::HostWish;

namespace {

// UTF-8 for the chat (the sources stay ASCII).
constexpr const char* kOacute = "\xC3\xB3";  // o with an acute accent

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::string host_word() { return std::string("anfitri") + kOacute + "n"; }

std::string name_of(std::uint8_t number, std::span<const Player> names) {
  for (const Player& p : names) {
    if (p.number == number && !p.name.empty()) return p.name;
  }
  return "P" + std::to_string(number);
}

// Why, in a few words for the end of a line ("" for none).
std::string why_text(const HostChoice& t, std::uint8_t self, std::span<const Player> names) {
  switch (t.why) {
    case HostWhy::first: return "fue el primero en entrar";
    case HostWhy::left: return "el anterior se fue";
    case HostWhy::measured:
      if (t.rtt_ms != 0) return "mejor conexi" + std::string(kOacute) + "n: " + std::to_string(t.rtt_ms) + " ms de media";
      return "mejor conexi" + std::string(kOacute) + "n";
    case HostWhy::unfit: return "el anterior no pod" + std::string("\xC3\xAD") + "a hacer de " + host_word();
    case HostWhy::picked:
      if (t.by == 0) return "elegido a mano";
      if (t.by == self) return "lo elegiste t" + std::string("\xC3\xBA");
      return "lo eligi" + std::string(kOacute) + " " + name_of(t.by, names);
    case HostWhy::wished: return "lo pidi" + std::string(kOacute);
    case HostWhy::refused: break;
  }
  return {};
}

const char* why_word(HostWhy why) {
  switch (why) {
    case HostWhy::first: return "first";
    case HostWhy::left: return "left";
    case HostWhy::measured: return "measured";
    case HostWhy::unfit: return "unfit";
    case HostWhy::picked: return "picked";
    case HostWhy::wished: return "wished";
    case HostWhy::refused: return "refused";
  }
  return "unknown";
}

// One line of the host file: the value stays on its line.
std::string clean(std::string_view value) {
  std::string out;
  for (char c : value) out += (c == '\r' || c == '\n') ? ' ' : c;
  return out;
}

}  // namespace

std::optional<Command> parse(std::string_view line) {
  line = trim(line);
  if (line.empty() || line.front() != '/') return std::nullopt;
  line.remove_prefix(1);
  const auto space = line.find_first_of(" \t");
  const std::string word = lower(line.substr(0, space));
  // Typed with the accent (UTF-8 o acute), the same command.
  if (word != "anfitrion" && word != "anfitri\xc3\xb3n" && word != "host") return std::nullopt;
  Command c;
  const std::string_view rest = space == std::string_view::npos ? std::string_view{} : trim(line.substr(space));
  if (rest.empty()) {
    c.kind = Command::Kind::show;
    return c;
  }
  const std::string r = lower(rest);
  if (r == "auto" || r == "automatico" || r == "autom\xc3\xa1tico" || r == "automatic") {
    c.kind = Command::Kind::automatic;
    return c;
  }
  c.kind = Command::Kind::pick;
  c.name = std::string(rest);
  return c;
}

HostWish wish_from(std::string_view value) {
  const std::string v = lower(trim(value));
  if (v == "me" || v == "yo" || v == "1" || v == "on" || v == "si" || v == "s\xc3\xad" || v == "true") {
    return HostWish::me;
  }
  if (v == "never" || v == "nunca" || v == "0" || v == "off" || v == "no" || v == "false") return HostWish::never;
  return HostWish::automatic;
}

std::string_view wish_word(HostWish wish) {
  switch (wish) {
    case HostWish::me: return "me";
    case HostWish::never: return "never";
    case HostWish::automatic: break;
  }
  return "auto";
}

Found find_player(std::string_view wanted, std::span<const Player> players) {
  Found f;
  const std::string want = lower(trim(wanted));
  if (want.empty()) {
    f.error = "Escribe el nombre del jugador: /anfitrion <nombre>";
    return f;
  }
  for (const Player& p : players) {
    if (!p.name.empty() && lower(p.name) == want) {
      f.name = p.name;
      return f;
    }
  }
  std::vector<const Player*> starts;
  for (const Player& p : players) {
    if (!p.name.empty() && lower(p.name).rfind(want, 0) == 0) starts.push_back(&p);
  }
  if (starts.size() == 1) {
    f.name = starts[0]->name;
    return f;
  }
  if (starts.size() > 1) {
    std::string several;
    for (const Player* p : starts) several += (several.empty() ? "" : ", ") + p->name;
    f.error = "\"" + std::string(trim(wanted)) + "\" puede ser " + several + ": escribe m" + "\xC3\xA1" + "s del nombre";
    return f;
  }
  std::string_view digits = want;
  if (!digits.empty() && digits.front() == 'p') digits.remove_prefix(1);
  if (digits.size() == 1 && digits[0] >= '1' && digits[0] <= '9') {
    const std::uint8_t n = static_cast<std::uint8_t>(digits[0] - '0');
    for (const Player& p : players) {
      if (p.number == n && !p.name.empty()) {
        f.name = p.name;
        return f;
      }
    }
  }
  f.error = "No hay nadie llamado \"" + std::string(trim(wanted)) + "\" en la sala";
  return f;
}

std::string told_line(const HostChoice& t, std::uint8_t self, std::span<const Player> names) {
  if (t.kind != HostChoiceKind::told) return {};
  if (t.why == HostWhy::refused) {
    return "No se puede hacer " + host_word() + " a " + (t.name.empty() ? std::string("ese jugador") : t.name) +
           ": no est" + "\xC3\xA1" + " en la sala o pidi" + kOacute + " no serlo nunca. Sigue " +
           name_of(t.host, names);
  }
  if (t.host == 0) return {};
  const std::string why = why_text(t, self, names);
  const std::string tail = why.empty() ? std::string() : " (" + why + ")";
  if (t.host == self) return "Eres el " + host_word() + " de la sala" + tail;
  const std::string who = t.name.empty() ? name_of(t.host, names) : t.name;
  return "Ahora el " + host_word() + " es " + who + tail;
}

std::string status_line(const std::optional<HostChoice>& told, std::uint8_t host, std::uint8_t self,
                        std::span<const Player> names) {
  if (host == 0) return "Sin conexi" + std::string(kOacute) + "n: no hay " + host_word();
  std::string line = host == self ? "Eres el " + host_word() : "El " + host_word() + " es " + name_of(host, names);
  if (told && told->kind == HostChoiceKind::told && told->host == host) {
    const std::string why = why_text(*told, self, names);
    if (!why.empty()) line += " (" + why + ")";
    line += told->pinned.empty() ? ". Se elige solo por la conexi" + std::string(kOacute) + "n"
                                 : ". Fijado a mano: " + told->pinned;
  }
  return line + ". Cambiar: /anfitrion <nombre>, o /anfitrion auto";
}

std::string asked_line(const Command& command, std::string_view name) {
  if (command.kind == Command::Kind::automatic) {
    return "Pedido: el " + host_word() + " se elige solo por la conexi" + std::string(kOacute) + "n";
  }
  return "Pedido: " + std::string(name) + " como " + host_word();
}

std::string host_file(std::string_view self, std::string_view host, bool hosting, HostWhy why,
                      std::int64_t unix_seconds) {
  std::string out;
  out += "self = " + clean(self) + "\n";
  out += "host = " + clean(host) + "\n";
  out += std::string("hosting = ") + (hosting ? "1" : "0") + "\n";
  out += std::string("why = ") + why_word(why) + "\n";
  out += "at = " + std::to_string(unix_seconds) + "\n";
  return out;
}

}  // namespace coop::host::host_choice
