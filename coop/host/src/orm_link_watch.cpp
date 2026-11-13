#include "coop/host/orm_link_watch.hpp"

#include <string>
#include <utility>

namespace coop::host {

namespace proto = coop::protocol;

namespace {

constexpr const char* kTips = "Cierra descargas, vídeos o juegos en otros equipos de la casa, o juega por cable.";

std::string seconds(std::uint64_t ms) { return std::to_string((ms + 500) / 1000); }

}  // namespace

LinkWatch::LinkWatch(LinkWatchConfig config) : config_(config) {}

bool LinkWatch::may_tell(const Side& side, std::uint64_t now_ms) const {
  return !side.told_ms || now_ms < *side.told_ms || now_ms - *side.told_ms >= config_.repeat_ms;
}

std::vector<LinkAdvice> LinkWatch::sample(const LinkSample& s) {
  std::vector<LinkAdvice> out;
  if (!s.connected) {
    // A new connection starts clean; the drop total keeps counting, so the
    // window starts again from the next sample. What the player was told
    // stays (told_ms), so a flapping link does not repeat the notice.
    drops_.clear();
    queued_since_ms_.reset();
    loss_since_ms_.reset();
    up_.told = false;  // a new connection: "fine again" belongs to the old one
    if (up_.behind) {
      up_.behind = false;
      out.push_back({false, "coop: link: uplink watch reset (not connected)", ""});
    }
    return out;
  }

  // Dropped frames within the window.
  while (!drops_.empty() && (s.now_ms < drops_.front().at_ms || s.now_ms - drops_.front().at_ms > config_.window_ms)) {
    drops_.pop_front();
  }
  if (!drops_.empty() && s.send_dropped_frames < drops_.back().total) drops_.clear();  // a new transport
  drops_.push_back({s.now_ms, s.send_dropped_frames});
  const std::uint64_t dropped = drops_.back().total - drops_.front().total;

  if (s.send_queued_bytes >= config_.queued_bytes_to_warn) {
    if (!queued_since_ms_) queued_since_ms_ = s.now_ms;
  } else {
    queued_since_ms_.reset();
  }
  if (s.has_loss && s.loss_percent >= config_.loss_percent_to_warn) {
    if (!loss_since_ms_) loss_since_ms_ = s.now_ms;
  } else {
    loss_since_ms_.reset();
  }
  const bool by_drops = dropped >= config_.dropped_to_warn;
  const bool by_queue = queued_since_ms_ && s.now_ms >= *queued_since_ms_ &&
                        s.now_ms - *queued_since_ms_ >= config_.queued_for_ms;
  const bool by_loss = loss_since_ms_ && s.now_ms >= *loss_since_ms_ &&
                       s.now_ms - *loss_since_ms_ >= config_.loss_for_ms;
  const bool trigger = by_drops || by_queue || by_loss;
  if (trigger) last_trigger_ms_ = s.now_ms;

  if (trigger && !up_.behind) {
    up_.behind = true;
    up_.behind_since_ms = s.now_ms;
    LinkAdvice a;
    a.warning = true;
    a.log = "coop: link: our uplink cannot keep up: " + std::to_string(dropped) + " of our frames dropped in " +
            seconds(config_.window_ms) + " s, " + std::to_string(s.send_queued_bytes / 1024) + " KiB waiting" +
            (s.has_loss ? ", loss " + std::to_string(s.loss_percent) + "%" : std::string()) +
            ": the others see us in jumps (fewer poses, never later ones)";
    if (may_tell(up_, s.now_ms)) {
      if (by_loss && !by_drops && !by_queue) {
        a.notice = "Tu conexión con el servidor pierde muchos paquetes (" + std::to_string(s.loss_percent) +
                   " %): los demás te verán a saltos. " + kTips;
      } else {
        a.notice = std::string("Tu conexión no da abasto para enviar tu movimiento: los demás te verán a saltos. ") +
                   kTips;
      }
      up_.told = true;
      up_.told_ms = s.now_ms;
    }
    out.push_back(std::move(a));
  } else if (!trigger && up_.behind && s.now_ms >= last_trigger_ms_ &&
             s.now_ms - last_trigger_ms_ >= config_.calm_ms) {
    up_.behind = false;
    LinkAdvice a;
    a.log = "coop: link: our uplink keeps up again (behind for " +
            seconds(s.now_ms - up_.behind_since_ms) + " s)";
    if (up_.told) {
      a.notice = "Tu conexión vuelve a ir bien: los demás ya te ven con normalidad.";
      up_.told = false;
    }
    out.push_back(std::move(a));
  }
  return out;
}

std::vector<LinkAdvice> LinkWatch::notice(const proto::LinkNotice& n, std::uint64_t now_ms) {
  std::vector<LinkAdvice> out;
  switch (n.kind) {
    case proto::LinkNoticeKind::behind: {
      LinkAdvice a;
      a.warning = true;
      a.log = "coop: link: the server drops " + std::to_string(n.a) + "% of the frames for us (" +
              std::to_string(n.b) + " KiB waiting there): our line cannot take everyone's poses; "
              "the others' puppets jump";
      if (!down_.behind) down_.behind_since_ms = now_ms;
      down_.behind = true;
      if (may_tell(down_, now_ms)) {
        a.notice = "Tu conexión no da abasto para recibir a todos (se pierde el " + std::to_string(n.a) +
                   " % de los movimientos): verás a los compañeros a saltos. " + kTips;
        down_.told = true;
        down_.told_ms = now_ms;
      }
      out.push_back(std::move(a));
      break;
    }
    case proto::LinkNoticeKind::caught_up: {
      LinkAdvice a;
      a.log = "coop: link: the server's frames for us keep up again (behind for " + std::to_string(n.a) + " s)";
      if (down_.told) {
        a.notice = "Tu conexión vuelve a ir bien: ya ves a los compañeros con normalidad.";
        down_.told = false;
      }
      down_.behind = false;
      out.push_back(std::move(a));
      break;
    }
    case proto::LinkNoticeKind::over_rate: {
      LinkAdvice a;
      a.warning = true;
      a.log = "coop: link: the server dropped " + std::to_string(n.a) + " of our samples or poses and " +
              std::to_string(n.b) + " reliable messages over our message rate (many of our own enemies moving?)";
      std::string text = "El servidor ha descartado parte de lo que manda tu juego (" + std::to_string(n.a) +
                         " movimientos";
      if (n.b != 0) text += " y " + std::to_string(n.b) + " cambios de la partida";
      text += "): manda más de lo que se permite. Los demás te verán a saltos";
      if (n.b != 0) text += " y puede que algún cambio no les haya llegado";
      text += ". Suele pasar con muchos enemigos tuyos moviéndose a la vez; avisa a quien hospeda.";
      a.notice = std::move(text);
      out.push_back(std::move(a));
      break;
    }
    case proto::LinkNoticeKind::server_protocol:
      out.push_back({false, "coop: link: the server speaks protocol " + std::to_string(n.a), ""});
      break;
    default:
      out.push_back({false,
                     "coop: link: a notice of kind " + std::to_string(static_cast<unsigned>(n.kind)) +
                         " from the server, unknown to this build (a newer server?); ignored",
                     ""});
      break;
  }
  return out;
}

std::string refused_notice(proto::RefuseReason reason, std::optional<std::uint16_t> server_protocol,
                           std::uint16_t ours) {
  const std::string ours_text = std::to_string(ours);
  switch (reason) {
    case proto::RefuseReason::wrong_protocol_version: {
      if (!server_protocol) {
        return "El servidor es de otra versión del kit (este juego habla el protocolo " + ours_text +
               "). Todos con el mismo kit: mirad la primera línea de VERSION.txt, también el que hospeda.";
      }
      const std::string theirs = std::to_string(*server_protocol);
      std::string who;
      if (*server_protocol < ours) {
        who = " El servidor es más viejo: el que hospeda tiene que actualizar su kit (o el servidor).";
      } else if (*server_protocol > ours) {
        who = " Tu kit es más viejo que el servidor: actualízalo (el lanzador lo hace solo al abrirlo).";
      }
      return "El servidor es de otra versión del kit (protocolo " + theirs + "; este juego, el " + ours_text +
             ")." + who + " Todos con el mismo kit: mirad la primera línea de VERSION.txt.";
    }
    case proto::RefuseReason::wrong_game_build:
      return "Tu juego es de otra versión (otra actualización) que la del servidor: todos con la misma versión del "
             "juego.";
    case proto::RefuseReason::server_full:
      return "La partida está llena (caben 6): espera a que salga alguien.";
    case proto::RefuseReason::name_taken:
      return "Ese nombre ya está en la partida: elige otro (play.cmd change).";
    case proto::RefuseReason::bad_name:
      return "Tu nombre está vacío: pon uno (play.cmd change).";
    case proto::RefuseReason::bad_password:
      return "Contraseña de entrada equivocada: pídesela al anfitrión y vuelve a abrir Join.bat (cinco fallos "
             "seguidos dejan tu dirección fuera un minuto).";
  }
  return "El servidor te ha rechazado (motivo " + std::to_string(static_cast<unsigned>(reason)) +
         ", que este kit no conoce): seguramente es de otra versión del kit. Todos con el mismo kit (VERSION.txt).";
}

}  // namespace coop::host
