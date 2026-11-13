// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: the group's settings, the emulator side (orm_group_settings.h).

#include "orm_group_settings.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/game_flags.hpp"
#include "coop/host/orm_group_settings.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop_every.h"
#include "platform.h"  // Platform, Env, ORM_LOG_*

namespace OrmGroupSettings {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

std::optional<std::string> Env(const char* name) {
    auto v = CoopPlatform::Env(name);
    if (!v || v->empty()) {
        return std::nullopt;
    }
    return v;
}

// The game's options through its own flag store (coop/host/game_flags.hpp):
// OptionData flags are ordinary Bool and Enum flags of save file 1.
class FlagOptions final : public host::orm::GameOptions {
public:
    explicit FlagOptions(CoopPlatform::GuestMemory& memory) : memory_(memory), flags_(memory) {}
    std::optional<bool> get_bool(std::uint32_t hash) override {
        return flags_.get_bool(hash);
    }
    std::optional<std::uint32_t> get_enum(std::uint32_t hash) override {
        return flags_.get_enum(hash);
    }
    bool set_bool(std::uint32_t hash, bool value) override {
        return host::GameFlags::set_bool(memory_, hash, value);
    }
    bool set_enum(std::uint32_t hash, std::uint32_t value) override {
        return host::GameFlags::set_enum(memory_, hash, value);
    }

private:
    CoopPlatform::GuestMemory& memory_;
    host::GameFlags flags_;
};

struct Glue {
    std::once_flag checked;
    bool on = false;
    std::optional<host::orm::GroupSettings> logic;  // present thread, after Check
    std::mutex inbox_mutex;                         // network thread to present
    std::vector<proto::GroupSettings> inbox;
    std::vector<std::uint8_t> left;
    bool fresh = false;
    std::vector<std::string> lines;  // present thread
};

Glue& TheGlue() {
    static Glue glue;
    return glue;
}

void Check(Glue& glue) {
    std::call_once(glue.checked, [&glue] {
        const auto v = Env("LICHII_ORM_GROUP_SETTINGS");
        glue.on = !v || *v != "0";
        if (!glue.on) {
            ORM_LOG_INFO("coop: settings: group settings off (LICHII_ORM_GROUP_SETTINGS=0)");
            return;
        }
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::current_path(ec);  // the lists' folder
        auto switches = host::orm::switch_entries(Env, base);
        std::string line;
        for (const auto& e : switches) {
            line += " " + e.key + "=" + e.value;
        }
        ORM_LOG_INFO(
                 "coop: settings: group settings on: amiibo and ZELDA NOTES follow the host, "
                 "{} co-op switches are compared with the host's (off with LICHII_ORM_GROUP_SETTINGS=0)",
                 switches.size());
        ORM_LOG_INFO("coop: settings: ours:{}", line);
        glue.logic.emplace(std::move(switches));
    });
}

} // namespace

bool On() {
    Glue& glue = TheGlue();
    Check(glue);
    return glue.on;
}

void OnEvent(const coop::net::Event& event) {
    Glue& glue = TheGlue();
    Check(glue);
    if (!glue.on) {
        return;
    }
    using Kind = coop::net::EventKind;
    std::lock_guard lock(glue.inbox_mutex);
    switch (event.kind) {
    case Kind::welcomed:
    case Kind::reconnected:
        glue.fresh = true;
        glue.inbox.clear();
        glue.left.clear();
        break;
    case Kind::player_left:
        glue.left.push_back(event.player);
        break;
    case Kind::message:
        if (const auto* m = std::get_if<proto::GroupSettings>(&event.message)) {
            glue.inbox.push_back(*m);
        }
        break;
    default:
        break;
    }
}

void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session) {
    Glue& glue = TheGlue();
    Check(glue);
    if (!glue.on || !glue.logic) {
        return;
    }
    std::vector<proto::GroupSettings> inbox;
    std::vector<std::uint8_t> left;
    bool fresh = false;
    {
        std::lock_guard lock(glue.inbox_mutex);
        inbox.swap(glue.inbox);
        left.swap(glue.left);
        fresh = std::exchange(glue.fresh, false);
    }
    auto& logic = *glue.logic;
    if (fresh) {
        logic.connection_new();
    }
    for (const std::uint8_t number : left) {
        logic.player_left(number);
    }
    for (const auto& m : inbox) {
        logic.received(m);
    }

    host::orm::RoomView room;
    if (session != nullptr) {
        session->with_client([&room](coop::net::Client& c) {
            room.connected = c.state() == coop::net::State::connected;
            room.self = c.player_number();
            room.host = c.host_number();
            for (const auto& p : c.players()) {
                room.names[p.number] = p.nickname;
            }
        });
    }

    std::optional<FlagOptions> options;
    if (main_base != 0) {
        options.emplace(platform.Memory());
    }
    auto step = logic.step(options ? &*options : nullptr, room, CoopEvery::NowUs());
    for (const auto& line : step.log) {
        ORM_LOG_INFO("coop: settings: {}", line);
    }
    for (auto& line : step.chat) {
        glue.lines.push_back(std::move(line));
    }
    if (step.send && session != nullptr) {
        const bool sent = session->with_client([&step](coop::net::Client& c) { return c.send(*step.send); });
        if (!sent) {
            logic.send_failed();
        }
    }
}

std::vector<std::string> TakeLines() {
    return std::exchange(TheGlue().lines, {});
}

} // namespace OrmGroupSettings
