// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM one host for the room, the hook's side (coop_host_choice.h).

#include "coop_host_choice.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>

#include "coop/host/host_choice.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "platform_env.h"

namespace CoopHostChoice {
namespace {

namespace hc = coop::host::host_choice;
namespace proto = coop::protocol;

// The host file is written again this often while nothing changes, so the
// launcher can tell a live game from one that ended (it reads the file's time).
constexpr std::uint64_t kFileEveryUs = 20'000'000;

struct Glue {
    std::mutex mutex;
    bool read_env = false;
    proto::HostWish wish = proto::HostWish::automatic;
    std::string file;  // LICHII_ORM_HOST_FILE; empty: none
    std::optional<proto::HostChoice> told;  // the relay's last word (host, why)
    std::vector<std::string> lines;
    // What the host file says now, and when it was written.
    std::string written;
    std::uint64_t written_at = 0;
    bool logged_file_error = false;
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

// Under g.mutex.
void ReadEnv(Glue& g) {
    if (g.read_env) {
        return;
    }
    g.read_env = true;
    g.wish = hc::wish_from(CoopPlatform::Env("LICHII_ORM_HOST").value_or(""));
    g.file = CoopPlatform::Env("LICHII_ORM_HOST_FILE").value_or("");
    ORM_LOG_INFO("coop: host: our wish is {} (LICHII_ORM_HOST), host file {}", hc::wish_word(g.wish),
                 g.file.empty() ? std::string("none (LICHII_ORM_HOST_FILE unset)") : g.file);
}

void AddLine(Glue& g, std::string line) {
    if (line.empty() || g.lines.size() >= 16) {
        return;
    }
    ORM_LOG_INFO("coop: host: {}", line);
    g.lines.push_back(std::move(line));
}

std::vector<hc::Player> Players(const coop::net::Client& client) {
    std::vector<hc::Player> out;
    for (const auto& p : client.players()) {
        out.push_back({p.number, p.nickname});
    }
    return out;
}

bool SendWish(coop::net::Client& client, proto::HostWish wish) {
    proto::HostChoice m;
    m.kind = proto::HostChoiceKind::wish;
    m.wish = wish;
    return client.send(proto::Message{m});
}

// Writes `text` to `path` through a temporary file beside it, so the launcher
// never reads half a file. False on any error.
bool WriteFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    const std::filesystem::path target(path);
    std::filesystem::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << text;
        if (!out.flush()) {
            return false;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace

bool OnChat(std::string_view text, coop::host::Session& session) {
    const auto command = hc::parse(text);
    if (!command) {
        return false;
    }
    Glue& g = TheGlue();
    // The network lock first, then ours: the order OnEvent has them in.
    std::string line;
    session.with_client([&](coop::net::Client& client) {
        std::lock_guard lock(g.mutex);
        if (client.state() != coop::net::State::connected) {
            line = "Sin conexi\xC3\xB3n: no se puede elegir el anfitri\xC3\xB3n";
            return;
        }
        const auto players = Players(client);
        if (command->kind == hc::Command::Kind::show) {
            line = hc::status_line(g.told, client.host_number(), client.player_number(), players);
            return;
        }
        proto::HostChoice m;
        m.kind = proto::HostChoiceKind::pick;
        std::string name;
        if (command->kind == hc::Command::Kind::pick) {
            const hc::Found found = hc::find_player(command->name, players);
            if (found.name.empty()) {
                line = found.error;
                return;
            }
            name = found.name;
            m.name = name;
        }
        line = client.send(proto::Message{m}) ? hc::asked_line(*command, name)
                                              : "No se pudo enviar: prueba otra vez";
    });
    std::lock_guard lock(g.mutex);
    AddLine(g, std::move(line));
    return true;
}

void OnEvent(const coop::net::Event& event, coop::net::Client& client) {
    using coop::net::EventKind;
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    ReadEnv(g);
    switch (event.kind) {
    case EventKind::welcomed:
    case EventKind::reconnected:
        g.told.reset();
        g.written.clear();  // the file is written again with the new room
        if (g.wish != proto::HostWish::automatic && !SendWish(client, g.wish)) {
            ORM_LOG_WARNING("coop: host: our wish ({}) could not be sent", hc::wish_word(g.wish));
        }
        break;
    case EventKind::message:
        if (const auto* m = std::get_if<proto::HostChoice>(&event.message)) {
            if (m->kind != proto::HostChoiceKind::told) {
                break;
            }
            const auto players = Players(client);
            // A joiner's first word says only who was there before: no line.
            const bool first_word = !g.told.has_value() && m->why != proto::HostWhy::refused;
            if (m->why != proto::HostWhy::refused) {
                const bool same = g.told && g.told->host == m->host && g.told->why == m->why &&
                                  g.told->pinned == m->pinned;
                g.told = *m;
                if (first_word || same) {
                    ORM_LOG_INFO("coop: host: player {} {} hosts ({}{})", m->host, m->name,
                                 static_cast<int>(m->why), m->pinned.empty() ? "" : ", picked by hand");
                    break;
                }
            }
            AddLine(g, hc::told_line(*m, client.player_number(), players));
        }
        break;
    default:
        break;
    }
}

void Frame(coop::host::Session* session, std::uint64_t now_us, std::int64_t unix_s) {
    Glue& g = TheGlue();
    if (session == nullptr) {
        return;
    }
    struct View {
        bool connected = false;
        std::uint8_t self = 0;
        std::uint8_t host = 0;
        std::string self_name;
        std::string host_name;
    };
    const View v = session->with_client([](coop::net::Client& c) {
        View out;
        out.connected = c.state() == coop::net::State::connected;
        out.self = c.player_number();
        out.host = c.host_number();
        for (const auto& p : c.players()) {
            if (p.number == out.self) {
                out.self_name = p.nickname;
            }
            if (p.number == out.host) {
                out.host_name = p.nickname;
            }
        }
        return out;
    });
    std::lock_guard lock(g.mutex);
    ReadEnv(g);
    if (g.file.empty() || !v.connected || v.self == 0 || v.host == 0) {
        return;
    }
    const proto::HostWhy why = g.told && g.told->host == v.host ? g.told->why : proto::HostWhy::first;
    // The text without the time: written at once when it changes.
    const std::string what = hc::host_file(v.self_name, v.host_name, v.self == v.host, why, 0);
    if (what == g.written && now_us - g.written_at < kFileEveryUs) {
        return;
    }
    if (!WriteFile(g.file, hc::host_file(v.self_name, v.host_name, v.self == v.host, why, unix_s))) {
        if (!g.logged_file_error) {
            g.logged_file_error = true;
            ORM_LOG_WARNING("coop: host: could not write the host file {}", g.file);
        }
        g.written = what;  // tried: again in 20 s, not every frame
        g.written_at = now_us;
        return;
    }
    if (what != g.written) {
        ORM_LOG_INFO("coop: host: host file: {} hosts{}", v.host_name, v.self == v.host ? " (us)" : "");
    }
    g.written = what;
    g.written_at = now_us;
    g.logged_file_error = false;
}

std::vector<std::string> TakeLines() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    return std::exchange(g.lines, {});
}

void Reset() {
    Glue& g = TheGlue();
    std::lock_guard lock(g.mutex);
    g.read_env = false;
    g.wish = proto::HostWish::automatic;
    g.file.clear();
    g.told.reset();
    g.lines.clear();
    g.written.clear();
    g.written_at = 0;
    g.logged_file_error = false;
}

} // namespace CoopHostChoice
