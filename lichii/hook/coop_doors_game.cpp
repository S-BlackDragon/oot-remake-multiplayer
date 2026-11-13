// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM (oot-remake docs/PUERTAS.md): the emulator side of the door mirror
// (coop_doors.h). Reads the switches once, configures the mirror, registers
// it as a calc-thread caller of the puppet cave and publishes every frame.
// ORM cofre: LICHII_ORM_CHEST_OPEN (on unless 0) turns the chest lids on
// (CHEST_OPEN; the finds over the head and the notices: coop_chest_open.h).

#include "coop_doors.h"

#include <string>

#include "coop_switch.h"
#include "platform.h"  // AddCaller, Env, ORM_LOG_*

namespace CoopDoors {
namespace {

constexpr int kCallerOrder = 45;  // after the objects (30), drops and time (40); before the boss (50)

std::string Env(const char* name) {
    return CoopPlatform::Env(name).value_or(std::string());
}

struct Glue {
    bool on = true;
    Glue() {
        on = Env("LICHII_COOP_DOORS") != "0";
        const auto d = Env("LICHII_COOP_DEBUG_DOORS");
        const bool verbose = !d.empty() && d != "0";
        const bool lids = CoopSwitch::On("LICHII_ORM_CHEST_OPEN", true);
        if (on) {
            Configure([](const std::string& line) { ORM_LOG_INFO("coop: {}", line); }, verbose, lids);
            CoopPlatform::AddCaller(kCallerOrder, [](CoopPuppet::Guest& guest, bool after_call,
                                                     std::uint64_t result, std::uint64_t now_ms) {
                Mirror* mirror = TheMirror();
                return mirror != nullptr ? mirror->Step(guest, after_call, result, now_ms)
                                         : std::optional<CoopPuppet::Call>{};
            });
        }
        ORM_LOG_INFO("coop: doors and chests {}",
                     on ? "move for the other players too" : "off (LICHII_COOP_DOORS=0)");
        ORM_LOG_INFO("coop: chests: a chest's lid opens on every screen (CHEST_OPEN) {}",
                     !on ? "off (LICHII_COOP_DOORS=0)" : lids ? "on" : "off (LICHII_ORM_CHEST_OPEN=0)");
    }
};

}  // namespace

void Frame(std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<Vec3>& own, std::string_view own_command,
           coop::host::Session* session, std::uint64_t server_now_us) {
    static Glue glue;  // the present thread only
    if (glue.on) {
        Publish(main_base, scene, own, own_command, session, server_now_us);
    }
}

}  // namespace CoopDoors
