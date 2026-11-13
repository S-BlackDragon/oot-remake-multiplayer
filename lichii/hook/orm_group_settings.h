// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: the group's settings in the emulator (oot-remake docs/AJUSTES_GRUPO.md;
// the rules are coop/host/orm_group_settings.hpp). Sends our world settings
// (GROUP_SETTINGS, type 246) after each WELCOME and when they change; as a
// guest, holds amiibo and ZELDA NOTES at the host's value and tells in the
// chat and the log which co-op switches differ from the host's; as the host,
// tells which guest's differ. Log lines "coop: settings: ...".
//
// Switch: LICHII_ORM_GROUP_SETTINGS, on unless "0".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}
namespace coop::net {
struct Event;
}

namespace OrmGroupSettings {

// LICHII_ORM_GROUP_SETTINGS is unset or not "0".
bool On();

// Network thread: GROUP_SETTINGS, WELCOME, reconnections and players leaving
// are queued for the next frame.
void OnEvent(const coop::net::Event& event);

// Present thread, once a frame. `main_base` 0: no game. `session` null: not
// connected.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session);

// Present thread: the chat notices since the last call.
std::vector<std::string> TakeLines();

} // namespace OrmGroupSettings
