// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM chat commands, PvP and the on-screen statistics, the emulator side
// (orm_commands_game.h, oot-remake docs/COMANDOS.md).

#include "orm_commands_game.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/hearts.hpp"
#include "coop/host/orm_as_slots.hpp"  // ORM PvP shield: every slot of our Link
#include "coop/host/orm_pvp.hpp"
#include "coop/host/quick_chat.hpp"  // ORM PvP shield: the guard button by name
#include "coop/host/orm_stats.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop_camera_probe.h"
#include "coop_hook.h"
#include "coop_host_choice.h"  // /anfitrion (orm/anfitrion)
#include "orm_hud.h"
#include "coop_map.h"
#include "coop_notices.h"
#include "coop_switch.h"
#include "coop_teleport_game.h"
#include "coop_puppet.h"
#include "orm_app_channel.h"
#include "orm_pvp_shared.h"
#include "coop_pvp_game.h"  // ORM test10: PvP on/off and hits are upstream's (CoopPvp)
#include "platform.h"

namespace OrmCommands {
namespace {

namespace proto = coop::protocol;
namespace cmd = coop::host::orm::commands;
namespace pvp = coop::host::orm::pvp;
namespace stats = coop::host::orm::stats;
using cmd::Id;

constexpr std::uint64_t kCallLifeMs = 60'000;      // a /traer or /reunir waits this long for /aceptar
constexpr std::uint64_t kSwitchAnswerMs = 3'000;   // a /pvp the server never answers is said so
constexpr std::uint64_t kNoticeMs = 5'000;         // the HUD's notice of a switch or a rename

std::uint64_t SteadyMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
std::uint64_t SteadyUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

std::filesystem::path ConfigFile(const char* name) {
    return CoopPlatform::FolderPath(CoopPlatform::Folder::config) / name;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

std::string Hex(const std::array<std::uint8_t, 3>& rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    return buf;
}

struct Call {
    std::uint8_t from = 0;
    std::string name;
    std::uint64_t until_ms = 0;
};

struct TimeSeen {
    std::uint32_t seconds = 0;
    std::uint8_t from = 0;
    std::uint64_t at_ms = 0;
};

// The guard button: LICHII_ORM_PVP_GUARD (L, R, ZL, ZR, ...), else ZL (the
// game's guard and lock-on, docs/puppet.md "Guarding (ZL)").
std::uint64_t GuardButton() {
    if (const auto name = CoopSwitch::Value("LICHII_ORM_PVP_GUARD")) {
        if (const auto bit = coop::host::quick_chat::button_bit(*name)) {
            return *bit;
        }
        ORM_LOG_WARNING("coop: pvp: LICHII_ORM_PVP_GUARD={} is not L, R, ZL, ZR, PLUS, MINUS, LCLICK or RCLICK: ZL",
                        *name);
    }
    return coop::host::quick_chat::kZL;
}

struct Glue {
    const bool commands_on = CoopSwitch::On("LICHII_ORM_COMMANDS", true);
    const bool pvp_on = CoopSwitch::On("LICHII_ORM_PVP", true);
    const bool push_on = CoopSwitch::On("LICHII_ORM_PVP_PUSH", true);
    const bool bodies_on = CoopSwitch::On("LICHII_ORM_BODIES", true);
    const bool blast_on = CoopSwitch::On("LICHII_ORM_BLAST_FALLBACK", true);

    // Any thread, under `mutex` (never held while asking the session: the
    // network thread holds its own lock when it calls OnEvent, which takes
    // this one).
    std::mutex mutex;
    pvp::RoomSwitches switches;
    std::map<std::uint8_t, std::string> names;
    std::uint8_t self = 0;
    std::optional<stats::Prefs> prefs;  // loaded on first use
    std::vector<std::string> lines;
    std::vector<proto::PlayerHit> hits_in;
    std::optional<Call> call;
    std::optional<TimeSeen> time_seen;
    std::optional<std::string> ini_name;   // /nombre taken: to coop.ini on the next frame
    std::optional<std::string> ini_color;  // /color: to coop.ini on the next frame
    std::uint64_t switch_asked_ms = 0;     // a /pvp sent and not answered yet
    std::vector<std::string> corner;
    stats::Input last_stats;
    std::uint32_t call_id = 0;

    // Present thread only.
    pvp::Attacker attacker;
    pvp::Victim victim;
    pvp::UpstreamVictim upstream_victim;  // ORM test10: our rules on upstream's PVP_HIT
    // ORM PvP shield (pvp-espada 0.12: the shield never stopped a hit): what our
    // game shows of a raised shield, read each frame while PvP is on (every AS
    // slot, the equipped shield, the guard button) for PvpVictimTake.
    pvp::GuardView guard_view;
    std::uint64_t guard_view_us = 0;  // 0: never read
    std::uint64_t guard_button = GuardButton();
    bool guard_logged = false;
    pvp::Shove shove;
    stats::FpsMeter fps;
    stats::Rate up;
    stats::Rate down;
    std::uint64_t frame_start_us = 0;
    std::optional<float> hook_ms;
    bool logged_on = false;
    std::optional<bool> lock_pack;  // the installed puppet pack has CoopPuppet_*_Lock (read once)
    pvp::AllyTarget ally;           // "Fijar aliados": the lock-on puppet and its check
    bool setter_given = false;      // the screen's toggles go through SetRoomSwitch

    std::atomic<bool> downed{false};
    std::atomic<bool> others_up{true};  // another player of the room is up (last frame's)
    std::atomic<bool> lock_now{false};
    std::atomic<bool> bodies_now{pvp::default_on(proto::RoomSwitchKey::bodies)};  // ORM choque: /choque
    std::atomic<bool> muted{false};
    std::atomic<bool> map_shown{true};
    std::atomic<bool> lock_available{true};  // false once the ally target fell back
    std::atomic<bool> bodies_fallback{false};  // ORM choque: our game draws the mates aside (SetBodiesFallback)

    void Say(std::string text) {  // with `mutex` held
        ORM_LOG_INFO("coop: commands: {}", text);
        lines.push_back(std::move(text));
    }
    std::string NameOf(std::uint8_t number) const {  // with `mutex` held
        const auto it = names.find(number);
        return it != names.end() && !it->second.empty() ? it->second : "P" + std::to_string(number);
    }
    stats::Prefs& Prefs() {  // with `mutex` held
        if (!prefs) {
            const auto text = ReadFile(ConfigFile("orm_stats.txt"));
            prefs = text ? stats::decode(*text) : stats::Prefs{};
            muted.store(prefs->mute);
            map_shown.store(prefs->map);
        }
        return *prefs;
    }
    void SavePrefs() {  // with `mutex` held
        stats::Prefs& p = Prefs();
        muted.store(p.mute);
        map_shown.store(p.map);
        std::error_code ec;
        std::filesystem::create_directories(ConfigFile("orm_stats.txt").parent_path(), ec);
        if (!WriteFile(ConfigFile("orm_stats.txt"), stats::encode(p))) {
            ORM_LOG_WARNING("coop: commands: could not write orm_stats.txt");
        }
    }
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

std::string OnOff(bool on) {
    return on ? "activado" : "desactivado";
}

// What the chat thread knows of the room, asked of the session first (its
// lock), our own mutex after.
struct Room {
    bool connected = false;
    std::uint8_t self = 0;
    std::uint8_t host = 0;
    std::vector<cmd::PlayerName> players;
    std::map<std::uint8_t, bool> down;  // by number: its PLAYER_STATUS dead bit
    std::optional<std::uint64_t> round_trip_us;
};

Room AskRoom(coop::host::Session& session) {
    Room room;
    session.with_client([&room](coop::net::Client& c) {
        room.connected = c.state() == coop::net::State::connected;
        room.self = c.player_number();
        room.host = c.host_number();
        room.round_trip_us = c.round_trip_us();
        for (const auto& p : c.players()) {
            room.players.push_back({p.number, p.nickname});
            room.down[p.number] = p.status && (*p.status & proto::status::dead) != 0;
        }
    });
    return room;
}

bool Send(coop::host::Session& session, const proto::Message& message) {
    return session.with_client([&message](coop::net::Client& c) {
        return c.state() == coop::net::State::connected && c.send(message);
    });
}

// The installed puppet pack's lock-on variants: every mod folder's
// coop_puppet.json under load/<title id> (as the spawn sync's PackStandIns).
bool PackHasLockOn() {
    std::error_code ec;
    const auto title = CoopPlatform::FolderPath(CoopPlatform::Folder::mods) / "0400486024DEC000";
    bool child = false;
    bool adult = false;
    for (const auto& mod : std::filesystem::directory_iterator(title, ec)) {
        if (const auto text = ReadFile(mod.path() / "coop_puppet.json")) {
            child = child || text->find("\"CoopPuppet_Child_Lock\"") != std::string::npos;
            adult = adult || text->find("\"CoopPuppet_Adult_Lock\"") != std::string::npos;
        }
    }
    return child && adult;
}

void Toggle(Glue& g, std::optional<bool> asked, bool stats::Prefs::*field, std::string_view what) {
    std::lock_guard lock(g.mutex);
    stats::Prefs& p = g.Prefs();
    p.*field = asked.value_or(!(p.*field));
    g.SavePrefs();
    g.Say(std::string(what) + ": " + OnOff(p.*field));
}

void Run(const cmd::Parsed& parsed, const Room& room, coop::host::Session& session) {
    Glue& g = TheGlue();
    const cmd::Spec& s = *parsed.spec;
    const auto say = [&g](std::string text) {
        std::lock_guard lock(g.mutex);
        g.Say(std::move(text));
    };
    switch (s.id) {
    case Id::help:
        for (auto& line : cmd::help_lines(parsed.arg)) {
            say(std::move(line));
        }
        return;
    case Id::pvp: {
        // ORM test10 (docs/PIN_TEST10.md, D-R21): PvP is upstream's now: the
        // host's PVP_MODE (241), the server's --pvp, upstream's hits (PVP_HIT,
        // PVP_HURT). /pvp goes to its chat command (its answer comes as its own
        // chat line); our ROOM_SWITCH key 1 is never set, so our hit path
        // (pvp::Attacker, PLAYER_HIT) stays still and nobody is hurt twice.
        const std::string line = parsed.toggle ? (*parsed.toggle ? "/pvp on" : "/pvp off") : "/pvp";
        if (!CoopPvp::OnChat(line, session)) {
            say("pvp: no se pudo pedir");
        }
        return;
    }
    case Id::lock_mates:
    case Id::bodies: {
        const auto key = s.id == Id::lock_mates ? proto::RoomSwitchKey::lock_mates : proto::RoomSwitchKey::bodies;
        if (!parsed.toggle) {
            std::lock_guard lock(g.mutex);
            std::string line = g.switches.describe(key, [&g](std::uint8_t n) { return g.NameOf(n); });
            if (key == proto::RoomSwitchKey::lock_mates && !g.lock_available.load()) {
                line += " (no disponible en tu juego: la marioneta fijable no está)";
            }
            if (key == proto::RoomSwitchKey::bodies && g.bodies_fallback.load()) {
                line += " (en tu juego los compañeros solo se apartan: mira el registro, 'bodies: collider')";
            }
            g.Say(std::move(line));
            return;
        }
        const bool sent = Send(session, pvp::RoomSwitches::request(key, *parsed.toggle));
        std::lock_guard lock(g.mutex);
        if (!sent) {
            g.Say("No se pudo enviar: ¿estás conectado?");
        } else {
            g.switch_asked_ms = std::max<std::uint64_t>(SteadyMs(), 1);
        }
        return;
    }
    case Id::players: {
        const auto rows = CoopHook::PlayerList();
        for (const auto& row : rows) {
            std::string line = "P" + std::to_string(row.number) + " " + row.name + (row.self ? " (tú)" : "");
            line += " - " + (row.scene ? CoopMap::PlaceName(*row.scene) : std::string("zona ?"));
            const auto d = room.down.find(row.number);
            line += d != room.down.end() && d->second ? " - caído" : " - en pie";
            line += row.ms ? " - " + std::string(row.self ? "" : "~") + std::to_string(*row.ms) + " ms" : " - ? ms";
            say(std::move(line));
        }
        if (rows.empty()) {
            say("No hay nadie en la sala");
        }
        return;
    }
    case Id::where: {
        const cmd::Match m = cmd::find_player(parsed.arg, room.players, room.self, true);
        if (m.number == 0) {
            say(m.error);
            return;
        }
        const auto mates = CoopHook::CurrentMates();
        if (!mates) {
            say("No se sabe: el juego no corre");
            return;
        }
        if (m.number == room.self) {
            say("Estás en " + (mates->scene ? CoopMap::PlaceName(*mates->scene) : std::string("?")));
            return;
        }
        for (const auto& o : mates->others) {
            if (o.number != m.number) {
                continue;
            }
            std::string line = m.name + " está en " + (o.scene ? CoopMap::PlaceName(*o.scene) : std::string("?"));
            if (o.scene && mates->scene && *o.scene == *mates->scene && mates->position) {
                const auto& a = o.position;
                const auto& b = *mates->position;
                const double d = std::sqrt(double(a[0] - b[0]) * (a[0] - b[0]) + double(a[1] - b[1]) * (a[1] - b[1]) +
                                           double(a[2] - b[2]) * (a[2] - b[2]));
                line += ", a " + std::to_string(static_cast<long>(std::lround(d))) + " m de ti";
            }
            say(std::move(line));
            return;
        }
        say(m.name + ": no se sabe dónde está (aún no ha mandado su sitio)");
        return;
    }
    case Id::go:
        // daurosk's /teleport and its rules (refused in an event or a boss fight).
        CoopTeleport::OnChat(parsed.arg.empty() ? "/teleport" : "/teleport " + parsed.arg, session);
        return;
    case Id::bring:
    case Id::gather: {
        proto::PlayerCall call;
        call.kind = s.id == Id::bring ? proto::CallKind::come : proto::CallKind::gather;
        std::string who;
        if (s.id == Id::bring) {
            const cmd::Match m = cmd::find_player(parsed.arg, room.players, room.self);
            if (m.number == 0) {
                say(m.error);
                return;
            }
            call.to = m.number;
            who = m.name;
        }
        {
            std::lock_guard lock(g.mutex);
            call.call_id = ++g.call_id;
        }
        if (!Send(session, call)) {
            say("No se pudo enviar: ¿estás conectado?");
        } else {
            say(s.id == Id::bring ? "Le has pedido a " + who + " que venga" : "Has llamado a todos a tu sitio");
        }
        return;
    }
    case Id::accept: {
        std::optional<Call> call;
        {
            std::lock_guard lock(g.mutex);
            if (g.call && g.call->until_ms > SteadyMs()) {
                call = g.call;
            }
            g.call.reset();
        }
        if (!call) {
            say("Nadie te ha llamado (o hace más de un minuto)");
            return;
        }
        CoopTeleport::OnChat("/teleport " + std::to_string(call->from), session);
        return;
    }
    case Id::ping: {
        if (parsed.toggle) {
            Toggle(g, parsed.toggle, &stats::Prefs::ping, "Ping en pantalla");
            return;
        }
        stats::Input in;
        if (room.round_trip_us) {
            in.ping_ms = static_cast<std::uint32_t>(*room.round_trip_us / 1000);
        }
        for (const auto& row : CoopHook::PlayerList()) {
            if (!row.self) {
                in.mates.push_back({row.name, row.ms});
            }
        }
        for (auto& line : stats::ping_report(in)) {
            say(std::move(line));
        }
        return;
    }
    case Id::fps:
        Toggle(g, parsed.toggle, &stats::Prefs::fps, "FPS en pantalla");
        return;
    case Id::net:
        Toggle(g, parsed.toggle, &stats::Prefs::net, "Red en pantalla");
        return;
    case Id::stats: {
        if (parsed.toggle) {
            Toggle(g, parsed.toggle, &stats::Prefs::all, "Estadísticas en pantalla");
            return;
        }
        stats::Input in;
        {
            std::lock_guard lock(g.mutex);
            in = g.last_stats;
        }
        in.mates.clear();
        for (const auto& row : CoopHook::PlayerList()) {
            if (!row.self) {
                in.mates.push_back({row.name, row.ms});
            }
        }
        for (auto& line : stats::report(in)) {
            say(std::move(line));
        }
        return;
    }
    case Id::hud: {
        if (!parsed.toggle) {
            // orm/hud2 (docs/HUD_AJUSTES.md): a setting of this player's HUD,
            // or with nothing the settings now and the panel's state.
            if (auto answer = OrmHud::OnChat(parsed.arg.empty() ? "/hud" : "/hud " + parsed.arg)) {
                say(std::move(*answer));
            }
            if (parsed.arg.empty()) {
                std::lock_guard lock(g.mutex);
                g.Say(std::string("Panel del cooperativo: ") + OnOff(g.Prefs().panel));
            }
            return;
        }
        Toggle(g, parsed.toggle, &stats::Prefs::panel, "Panel del cooperativo");
        bool shown = true;
        {
            std::lock_guard lock(g.mutex);
            shown = g.Prefs().panel;
        }
        OrmHud::SetPanelShown(shown);
        return;
    }
    case Id::rupees:  // orm/hud2: answered by its own code (coop_hud_settings.h)
    case Id::names: {
        // Every on/off word of the registry as the one OrmHud reads.
        const std::string word = parsed.toggle ? (*parsed.toggle ? "on" : "off") : parsed.arg;
        const std::string line = "/" + std::string(s.name) + (word.empty() ? "" : " " + word);
        if (auto answer = OrmHud::OnChat(line)) {
            say(std::move(*answer));
        }
        return;
    }
    case Id::host:  // orm/anfitrion: its answer comes back through CoopHostChoice::TakeLines
        CoopHostChoice::OnChat(parsed.arg.empty() ? "/anfitrion" : "/anfitrion " + parsed.arg, session);
        return;
    case Id::map:
        Toggle(g, parsed.toggle, &stats::Prefs::map, "Compañeros en el mapa");
        return;
    case Id::mute:
        Toggle(g, parsed.toggle, &stats::Prefs::mute, "Silencio (chat de los demás oculto)");
        return;
    case Id::color: {
        const auto rgb = cmd::parse_color(parsed.arg);
        if (!rgb) {
            say("Ese color no lo conozco");
            return;
        }
        if (!Send(session, proto::PlayerLook{proto::kPlayerSelf, *rgb, 0})) {
            say("No se pudo enviar: ¿estás conectado?");
            return;
        }
        std::lock_guard lock(g.mutex);
        g.ini_color = Hex(*rgb);
        g.Say("Tu color ahora es " + Hex(*rgb));
        return;
    }
    case Id::spectate: {
        const cmd::Match m = cmd::find_player(parsed.arg, room.players, room.self);
        if (m.number == 0) {
            say(m.error);
            return;
        }
        CoopCameraProbe::AskFollow(m.number);
        say("Mirando a " + m.name + " (con la cámara de espectador activada)");
        return;
    }
    case Id::logs: {
        // The app's helper uploads them (orm_app_channel.h: the screen runs in this process).
        const bool asked = OrmAppChannel::Send({OrmAppChannel::Ask::upload_logs, {}});
        ORM_LOG_INFO("coop: commands: /registros: {}",
                     asked ? "asked the app's helper (upload_logs)"
                           : "no app in this process (fallback: nothing sent)");
        say(asked ? "Subiendo tus registros..."
                  : "La app no está abierta en este juego: abre el juego desde la app para subir los registros");
        return;
    }
    case Id::retry: {
        // The connection now (between attempts), and the app's downloads.
        const bool reconnect = session.with_client([](coop::net::Client& c) { return c.retry_now(); });
        const bool asked = OrmAppChannel::Send({OrmAppChannel::Ask::retry_downloads, {}});
        ORM_LOG_INFO("coop: commands: /reintentar: connection {}, downloads {}",
                     reconnect ? "retried now" : "not waiting to retry (connected or connecting)",
                     asked ? "asked the app's helper (retry_downloads)" : "not asked (no app in this process)");
        if (reconnect) {
            say("Reintentando la conexión ahora");
        } else if (!asked) {
            say("La conexión no está esperando para reintentar y la app no está abierta: nada que reintentar");
        }
        if (asked) {
            say("Reintentando las descargas (la app)...");
        }
        return;
    }
    case Id::version: {
        std::string kit = "sin VERSION.txt";
        std::error_code ec;
        const auto here = std::filesystem::current_path(ec);
        for (const auto& path : {here / "VERSION.txt", here.parent_path() / "VERSION.txt"}) {
            if (const auto text = ReadFile(path)) {
                kit = text->substr(0, text->find_first_of("\r\n"));
                break;
            }
        }
        say("Protocolo " + std::to_string(proto::kProtocolVersion) + ", kit: " + kit);
        return;
    }
    case Id::time: {
        std::lock_guard lock(g.mutex);
        if (!g.time_seen) {
            g.Say("Aún no ha llegado la hora de nadie (cada juego la manda al cambiar)");
            return;
        }
        const TimeSeen t = *g.time_seen;
        const std::uint64_t ago_s = (SteadyMs() - std::min(SteadyMs(), t.at_ms)) / 1000;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(t.seconds / 3600 % 24),
                      static_cast<unsigned>(t.seconds / 60 % 60));
        g.Say(std::string("Hora del juego: ") + buf + " (la de " + g.NameOf(t.from) + ", hace " +
              std::to_string(ago_s) + " s)");
        return;
    }
    case Id::name: {
        if (const auto why = cmd::check_new_name(parsed.arg, room.players, room.self)) {
            say(*why);
            return;
        }
        if (!Send(session, proto::PlayerRename{proto::kPlayerSelf, proto::RenameResult::ok, parsed.arg})) {
            say("No se pudo enviar: ¿estás conectado?");
        }
        return;
    }
    }
}

} // namespace

bool OnChat(std::string_view text, coop::host::Session& session) {
    Glue& g = TheGlue();
    if (!g.commands_on) {
        return false;
    }
    const cmd::Parsed parsed = cmd::parse(text);
    if (parsed.kind == cmd::Parsed::Kind::not_command) {
        return false;
    }
    if (parsed.kind != cmd::Parsed::Kind::ok) {
        std::lock_guard lock(g.mutex);
        g.Say(parsed.error);
        return true;
    }
    const Room room = AskRoom(session);  // the session's lock, then ours
    cmd::Context context;
    context.connected = room.connected;
    context.host = room.self != 0 && room.host == room.self;
    context.downed = g.downed.load();
    ORM_LOG_INFO("coop: commands: /{} '{}'", parsed.spec->name, parsed.arg);
    if (const auto why = cmd::refuse(*parsed.spec, context)) {
        std::lock_guard lock(g.mutex);
        g.Say(*why);
        return true;
    }
    Run(parsed, room, session);
    return true;
}

void OnEvent(const coop::net::Event& event, coop::net::Client& client) {
    using coop::net::EventKind;
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    const auto names = [&g](std::uint8_t n) { return g.NameOf(n); };
    switch (event.kind) {
    case EventKind::welcomed:
    case EventKind::reconnected:
        g.switches.connection_new(event.welcome.server_time_us);
        g.lock_now.store(g.switches.on(proto::RoomSwitchKey::lock_mates));  // their defaults until the room's come
        g.bodies_now.store(g.switches.on(proto::RoomSwitchKey::bodies));
        g.names.clear();
        for (const auto& p : client.players()) {
            g.names[p.number] = p.nickname;
        }
        g.self = client.player_number();
        g.call.reset();
        g.hits_in.clear();
        break;
    case EventKind::player_joined:
        g.names[event.player] = event.nickname;
        break;
    case EventKind::player_left:
        g.names.erase(event.player);
        if (g.call && g.call->from == event.player) {
            g.call.reset();
        }
        break;
    case EventKind::message:
        if (const auto* sw = std::get_if<proto::RoomSwitch>(&event.message)) {
            g.switch_asked_ms = 0;
            for (auto& line : g.switches.received(*sw, names)) {
                CoopNotices::TheBoard().Add(sw->player_number, g.NameOf(sw->player_number), line, SteadyMs(),
                                            kNoticeMs);
                g.Say(std::move(line));
            }
            g.lock_now.store(g.switches.on(proto::RoomSwitchKey::lock_mates));
            g.bodies_now.store(g.switches.on(proto::RoomSwitchKey::bodies));
        } else if (const auto* hit = std::get_if<proto::PlayerHit>(&event.message)) {
            if (hit->victim == g.self && g.hits_in.size() < 64) {
                g.hits_in.push_back(*hit);
            }
        } else if (const auto* call = std::get_if<proto::PlayerCall>(&event.message)) {
            const std::string who = g.NameOf(call->player_number);
            g.call = Call{call->player_number, who, SteadyMs() + kCallLifeMs};
            g.Say(call->kind == proto::CallKind::gather ? who + " (anfitrión) llama a todos: escribe /aceptar para ir"
                                                        : who + " te llama: escribe /aceptar para ir");
        } else if (const auto* rn = std::get_if<proto::PlayerRename>(&event.message)) {
            if (rn->result == proto::RenameResult::ok) {
                const std::string before = g.NameOf(rn->player_number);
                g.names[rn->player_number] = rn->nickname;
                const std::string line = before + " ahora se llama " + rn->nickname;
                CoopNotices::TheBoard().Add(rn->player_number, rn->nickname, line, SteadyMs(), kNoticeMs);
                g.Say(line);
                if (rn->player_number == g.self) {
                    g.ini_name = rn->nickname;
                }
            } else if (rn->result == proto::RenameResult::name_taken) {
                g.Say("Ese nombre ya lo usa otro jugador");
            } else {
                g.Say("Nombre no válido: de 1 a 16 letras, números, punto, guion o guion bajo");
            }
        } else if (const auto* tod = std::get_if<proto::TimeOfDay>(&event.message)) {
            g.time_seen = TimeSeen{tod->seconds, tod->player_number, SteadyMs()};
        }
        break;
    default:
        break;
    }
}

void FrameStart() {
    TheGlue().frame_start_us = SteadyUs();
}

void Frame(CoopPlatform::Platform& platform, const std::optional<coop::host::PlayerReading>& reading,
           coop::host::Session* session, std::uint64_t server_now_us,
           const std::vector<coop::overlay::Shown>& others, coop::host::DownedState downed) {
    Glue& g = TheGlue();
    const std::uint64_t now_us = SteadyUs();
    g.fps.frame(now_us);
    g.downed.store(downed == coop::host::DownedState::downed);
    if (!g.logged_on) {
        g.logged_on = true;
        ORM_LOG_INFO("coop: commands: {}; pvp {} (push {}, bodies {}, bombs with no copy {}); stats in "
                     "orm_stats.txt",
                     g.commands_on ? "on (/ayuda)" : "off (LICHII_ORM_COMMANDS=0)", g.pvp_on ? "on" : "off",
                     g.push_on ? "on" : "off", g.bodies_on ? "on (the puppets drawn aside)" : "off", g.blast_on ? "on" : "off");
    }

    // Our Link this frame.
    const bool game = platform.MainBase() != 0 && reading.has_value();
    const bool able = game && downed == coop::host::DownedState::alive && !reading->event_running.value_or(true);
    const pvp::Vec3 at = game ? reading->position : pvp::Vec3{};
    const pvp::Vec3 forward = game && reading->rotation ? pvp::forward_of(*reading->rotation) : pvp::Vec3{0, 0, 1};
    std::optional<std::string> command;
    if (game && reading->animation) {
        command = reading->animation->name;
    }
    std::vector<pvp::Mate> mates;
    bool others_up = false;
    for (const auto& o : others) {
        const bool here = game && o.scene && reading->scene && *o.scene == *reading->scene && o.fade >= 0.5f;
        mates.push_back({o.number, o.position, !o.down, here});
        others_up = others_up || !o.down;
    }
    g.others_up.store(others_up);

    std::vector<proto::PlayerHit> hits_in;
    std::vector<std::array<float, 3>> blasts = OrmPvpShared::TheBoard().TakeUnseenBlasts();
    bool pvp_on = false;
    bool lock_wanted = false;
    {
        std::lock_guard lock(g.mutex);
        hits_in.swap(g.hits_in);
        pvp_on = g.switches.on(proto::RoomSwitchKey::pvp);
        lock_wanted = g.switches.on(proto::RoomSwitchKey::lock_mates);
        if (g.switch_asked_ms != 0 && SteadyMs() - g.switch_asked_ms > kSwitchAnswerMs) {
            g.switch_asked_ms = 0;
            g.Say("El servidor no contestó al cambio: ¿un servidor sin esta función?");
        }
    }

    // Our hits on the mates' puppets (only with the room's PvP on).
    std::vector<proto::PlayerHit> hits_out;
    if (g.pvp_on && session != nullptr) {
        pvp::AttackFrame f;
        f.now_us = now_us;
        f.pvp = pvp_on;
        f.able = able;
        f.command = command;
        f.at = at;
        f.forward = forward;
        f.mates = mates;
        for (const auto& s : OrmPvpShared::TheBoard().Shots()) {
            f.shots.push_back({s.id, s.actor, s.at});
        }
        hits_out = g.attacker.step(f);
    }

    // The life we lose this frame and the push we take.
    std::int32_t lose = 0;
    std::optional<pvp::Vec3> push;
    std::optional<std::int32_t> life;
    coop::host::LifeWords words;
    bool words_found = false;
    if (game) {
        coop::host::PlayerReader reader(platform.Memory());
        if (const auto actor = reader.find_player_actor()) {
            life = coop::host::read_vitals(platform.Memory(), *actor).life;
            if (const auto w = coop::host::find_life_words(platform.Memory(), *actor)) {
                words = *w;
                words_found = true;
            }
            // ORM PvP shield: what shows a raised shield, for the PVP_HITs of this
            // frame and the next (PvpVictimTake), only while upstream's PvP is on.
            if (g.pvp_on && CoopPvp::On()) {
                pvp::GuardView view;
                view.commands = coop::host::orm::read_as_commands(platform.Memory(), *actor);
                if (view.commands.empty() && command) {
                    view.commands.push_back(*command);  // the controller unread: the main slot as read
                }
                if (reading->equipment) {
                    view.shield = reading->equipment->shield;
                }
                if (const auto buttons = platform.PadButtons()) {
                    view.guard_held = (*buttons & g.guard_button) != 0;
                }
                g.guard_view = std::move(view);
                g.guard_view_us = now_us;
                if (!g.guard_logged) {
                    g.guard_logged = true;
                    ORM_LOG_INFO("coop: pvp: shield read from {} AS slots of our Link, the equipped shield ({}) and "
                                 "the guard button {:#x} (LICHII_ORM_PVP_GUARD)",
                                 g.guard_view.commands.size(),
                                 g.guard_view.shield ? (g.guard_view.shield->empty() ? "none" : *g.guard_view.shield)
                                                     : "unread",
                                 g.guard_button);
                }
            }
        }
    }
    for (const auto& hit : hits_in) {
        pvp::VictimFrame f;
        f.now_us = now_us;
        f.pvp = pvp_on && g.pvp_on;
        f.up = able;
        f.command = command;
        f.at = at;
        f.forward = forward;
        f.life = life ? std::optional<std::int32_t>(*life - lose) : std::nullopt;
        f.others_up = others_up;
        const pvp::Judged j = g.victim.judge(hit, f);
        ORM_LOG_INFO("coop: pvp: hit #{} from P{} ({} {}): {}{}", hit.hit_id, hit.player_number,
                     static_cast<int>(hit.weapon), hit.damage, pvp::to_string(j.verdict),
                     j.spared ? " (the last one up: one quarter left)" : "");
        if (j.verdict == pvp::Verdict::take || j.verdict == pvp::Verdict::push_only) {
            lose += j.damage;
            push = j.push;
        }
    }
    // ORM test10: with upstream's PvP on, its attacker already sends a PVP_HIT
    // for a bomb that went off by a Link (CoopPvp::OwnShot): ours would hurt twice.
    if (g.blast_on && game && !CoopPvp::On()) {
        for (const auto& b : blasts) {
            if (const auto hurt = pvp::blast_hurt(b, at, able, false)) {
                lose += hurt->damage;
                push = hurt->push;
                ORM_LOG_INFO("coop: pvp: a mate's bomb went off {:.1f} m from us with no copy here: {} quarters",
                             std::sqrt((b[0] - at[0]) * (b[0] - at[0]) + (b[1] - at[1]) * (b[1] - at[1]) +
                                   (b[2] - at[2]) * (b[2] - at[2])),
                             hurt->damage);
            }
        }
    }
    if (lose > 0 && life && words_found) {
        coop::host::HeartsAction action;
        action.life = std::max<std::int32_t>(0, *life - lose);
        const bool written = coop::host::apply_hearts(platform.Memory(), words, action);
        ORM_LOG_INFO("coop: pvp: life {} -> {}{}", *life, *action.life, written ? "" : " (NOT written)");
    }
    if (push && g.push_on) {
        g.shove.start(*push, now_us);
    }

    // Where our Link goes this frame: the PvP push. Never out of a mate's
    // body: that one is drawn (the puppet aside, coop_puppet.cpp with
    // coop/host/orm_bodies.hpp), since a written position skips the game's
    // collision (oot-remake docs/CUERPOS.md).
    if (const auto part = g.shove.step(now_us)) {
        if (able) {
            platform.MoveLink(at[0] + (*part)[0], at[1] + (*part)[1], at[2] + (*part)[2]);
        }
    }
    if (!able) {
        g.shove.stop();
    }

    // The session: our hits out, the link's numbers in.
    stats::Input in;
    std::uint64_t sent_bytes = 0;
    std::uint64_t got_bytes = 0;
    bool linked = false;
    if (session != nullptr) {
        session->with_client([&](coop::net::Client& c) {
            for (const auto& h : hits_out) {
                if (c.send(h)) {
                    ORM_LOG_INFO("coop: pvp: our hit #{} on P{} ({} {})", h.hit_id, h.victim,
                                 static_cast<int>(h.weapon), h.damage);
                }
            }
            if (const auto rtt = c.round_trip_us()) {
                in.ping_ms = static_cast<std::uint32_t>(*rtt / 1000);
            }
            const auto link = c.link();
            in.loss_percent = link.loss_percent;
            sent_bytes = link.sent_bytes;
            got_bytes = link.received_bytes;
            linked = c.state() == coop::net::State::connected;
        });
    }
    if (linked) {
        g.up.add(sent_bytes, now_us);
        g.down.add(got_bytes, now_us);
    }
    in.fps = g.fps.fps(now_us);
    in.up_kbps = g.up.kbps();
    in.down_kbps = g.down.kbps();
    for (const auto& m : mates) {
        in.puppets += m.here ? 1 : 0;
    }
    if (g.frame_start_us != 0 && now_us >= g.frame_start_us) {
        const float ms = float(now_us - g.frame_start_us) / 1000.0f;
        g.hook_ms = g.hook_ms ? *g.hook_ms * 0.9f + ms * 0.1f : ms;
    }
    in.hook_ms = g.hook_ms;
    (void)server_now_us;

    // "Fijar aliados": the puppets as the pack's lock-on variant, checked once
    // (ok when one was made; fallback when the pack has none or it fails).
    if (!g.lock_pack) {
        g.lock_pack = PackHasLockOn();
        ORM_LOG_INFO("coop: pvp: lock-on puppets in the puppet pack: {}",
                     *g.lock_pack ? "yes (CoopPuppet_Child_Lock, CoopPuppet_Adult_Lock)" : "no (an older pack)");
    }
    const auto tally = CoopPuppet::Locks();
    const auto ally = g.ally.step(lock_wanted, *g.lock_pack, tally.made, tally.failed);
    CoopPuppet::SetActorSuffix(ally.lock ? "_Lock" : "");
    g.lock_available.store(g.ally.available());
    if (ally.log) {
        ORM_LOG_INFO("coop: {}", *ally.log);
    }
    // The screen's toggles (orm_app_channel.h): our state out, its clicks in.
    if (!g.setter_given) {
        g.setter_given = true;
        OrmAppChannel::SetSwitchSetter([](std::uint8_t key, bool on) {
            return SetRoomSwitch(static_cast<coop::protocol::RoomSwitchKey>(key), on);
        });
    }
    OrmAppChannel::Switches published;
    published.connected = linked;
    published.pvp = CoopPvp::On();  // ORM test10: upstream's PVP_MODE
    published.lock_mates = lock_wanted;
    published.lock_available = g.ally.available();
    OrmAppChannel::PublishSwitches(published);

    std::optional<std::string> ini_name;
    std::optional<std::string> ini_color;
    {
        std::lock_guard lock(g.mutex);
        g.last_stats = in;
        g.corner = stats::corner_lines(g.Prefs(), in);
        ini_name = std::exchange(g.ini_name, std::nullopt);
        ini_color = std::exchange(g.ini_color, std::nullopt);
        if (ally.chat) {
            g.Say(*ally.chat);
        }
        for (auto& line : OrmAppChannel::TakeAnswers()) {  // the app's answers (/registros, ...)
            g.Say(std::move(line));
        }
    }
    // /nombre: the app keeps it too (its room name), when it runs in this process.
    if (ini_name) {
        const bool asked = OrmAppChannel::Send({OrmAppChannel::Ask::save_name, *ini_name});
        ORM_LOG_INFO("coop: commands: /nombre {}: {}", *ini_name,
                     asked ? "the app saves it too (room name)" : "no app in this process: coop.ini only");
    }
    // /nombre and /color are kept in coop.ini (the next start uses them).
    for (const auto& [key, value] : {std::make_pair(std::string("name"), ini_name),
                                     std::make_pair(std::string("color"), ini_color)}) {
        if (!value) {
            continue;
        }
        const auto path = ConfigFile("coop.ini");
        const std::string text = ReadFile(path).value_or("");
        const bool ok = WriteFile(path, coop::host::orm::commands::set_ini_value(text, key, *value));
        ORM_LOG_INFO("coop: commands: coop.ini {} = {}{}", key, *value, ok ? "" : " (NOT written)");
    }
}

std::vector<std::string> TakeLines() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    return std::exchange(g.lines, {});
}

bool PvpOn() {
    return CoopPvp::On();  // ORM test10: the session's PVP_MODE (upstream's)
}

std::int32_t PvpAttackAmount(proto::DamageCause cause, std::int32_t upstream_amount,
                             const std::optional<coop::host::PlayerReading>& reading) {
    Glue& g = TheGlue();
    if (!g.pvp_on) {
        return upstream_amount;
    }
    const std::string command = reading && reading->animation ? reading->animation->name : std::string();
    const std::int32_t amount = pvp::attack_amount(cause, upstream_amount, command);
    if (amount != upstream_amount) {
        ORM_LOG_INFO("coop: pvp: our hit ({}, cause {}) takes {} quarter hearts, not upstream's {}",
                     command.empty() ? "?" : command, static_cast<int>(cause), amount, upstream_amount);
    }
    return amount;
}

// A shield reading older than this is not used (a frame without our Link).
constexpr std::uint64_t kGuardFreshUs = 250'000;

std::int32_t PvpVictimTake(const proto::PvpHit& hit, std::int32_t upstream_take,
                           const std::optional<coop::host::PlayerReading>& reading,
                           std::optional<std::int32_t> life) {
    Glue& g = TheGlue();
    if (!g.pvp_on || upstream_take <= 0) {
        return upstream_take;
    }
    const std::uint64_t now_us = SteadyUs();
    pvp::VictimFrame f;
    f.now_us = now_us;
    f.pvp = true;
    f.up = reading.has_value() && !g.downed.load() && !reading->event_running.value_or(false);
    if (reading) {
        if (reading->animation) {
            f.command = reading->animation->name;
        }
        f.at = reading->position;
        if (reading->rotation) {
            f.forward = pvp::forward_of(*reading->rotation);
        }
    }
    f.life = life;
    f.others_up = g.others_up.load();
    // The shield as last read (OrmCommands::Frame, this frame or the last); an
    // older reading or none: the main command alone (the old rule), said so.
    const bool fresh = g.guard_view_us != 0 && now_us - std::min(now_us, g.guard_view_us) <= kGuardFreshUs;
    if (fresh) {
        f.guard = pvp::guard_of(g.guard_view);
    }
    const pvp::Judged j = g.upstream_victim.judge(hit, upstream_take, "", f);
    const std::int32_t take = j.verdict == pvp::Verdict::take ? j.damage : 0;
    ORM_LOG_INFO("coop: pvp: our rules on P{}'s hit ({} quarter hearts by upstream's): {}, takes {}{}{}{}",
                 hit.player_number, upstream_take, pvp::to_string(j.verdict), take,
                 j.spared ? " (the last one up: one quarter left)" : "", j.shield.empty() ? "" : "; ",
                 j.shield.empty() ? std::string() : j.shield + (fresh ? "" : " (no fresh shield reading: main command only)"));
    if ((j.verdict == pvp::Verdict::take || j.verdict == pvp::Verdict::push_only) && g.push_on && f.up) {
        g.shove.start(j.push, now_us);
    }
    return take;
}
bool LockMatesOn() {
    return TheGlue().lock_now.load();
}
bool BodiesOn() {
    return TheGlue().bodies_now.load();
}
void SetBodiesFallback(bool fallback) {
    TheGlue().bodies_fallback.store(fallback);
}
bool Muted() {
    Glue& g = TheGlue();
    {
        std::lock_guard lock(g.mutex);
        g.Prefs();  // loads the saved switches once
    }
    return g.muted.load();
}
bool MapShown() {
    return TheGlue().map_shown.load();
}

std::vector<std::string> CornerLines() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    return g.corner;
}

bool SetRoomSwitch(coop::protocol::RoomSwitchKey key, bool on) {
    // The screen has no session of its own: through the chat path, as /pvp on.
    const char* name = key == coop::protocol::RoomSwitchKey::pvp          ? "/pvp "
                       : key == coop::protocol::RoomSwitchKey::lock_mates ? "/fijar "
                                                                          : "/choque ";
    const std::string line = std::string(name) + (on ? "on" : "off");
    return CoopHook::SendChat(line);
}

std::vector<std::string> HelpLines() {
    return cmd::help_lines();
}

std::vector<cmd::Suggestion> Suggest(std::string_view text) {
    std::vector<cmd::PlayerName> players;
    std::uint8_t self = 0;
    for (const auto& row : CoopHook::PlayerList()) {
        players.push_back({row.number, row.name});
        if (row.self) {
            self = row.number;
        }
    }
    cmd::Context context;
    context.connected = !players.empty();
    context.downed = TheGlue().downed.load();
    // The host is not in the player list: /reunir shows for everyone, and
    // refuses when run by a guest.
    context.host = true;
    return cmd::complete(text, players, self, context);
}

} // namespace OrmCommands
