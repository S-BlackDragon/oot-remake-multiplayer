// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM one host for the room, the hook's side (coop/host/host_choice.hpp has
// the rules, server/host_choice.hpp the relay's choice; oot-remake
// docs/PARTIDA_SALA.md, "Un solo anfitrion"). No emulator code: the tests
// build it on its own.
//
// - /anfitrion [name|auto] in the chat box (OnChat): who hosts, or a pick
//   sent to the relay (HOST_CHOICE pick). Until the chat command registry
//   (orm/pvp, coop/host/orm_commands.hpp) is in, SendChat asks here first;
//   with the registry its "anfitrion" entry calls OnChat.
// - LICHII_ORM_HOST = auto | me | never: our wish, sent after each WELCOME.
// - the relay's word (HOST_CHOICE told) goes to the chat as a notice.
// - LICHII_ORM_HOST_FILE = <path>: the launcher's file of who hosts
//   (host_choice::host_file), written when it changes and every 20 s while
//   connected, so the room's save follows the relay's host.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace coop::host {
class Session;
}
namespace coop::net {
class Client;
struct Event;
}

namespace CoopHostChoice {

// Any thread (the chat box): true when `text` is /anfitrion, which is then
// done here and not sent as chat. Takes the session's network lock.
bool OnChat(std::string_view text, coop::host::Session& session);

// Network thread, from the session's on_event (the network lock is held):
// sends our wish after each WELCOME, keeps the relay's word.
void OnEvent(const coop::net::Event& event, coop::net::Client& client);

// Any thread, once per frame; `session` null when not connected. Writes the
// host file when due (steady clock `now_us`, unix seconds `unix_s`).
void Frame(coop::host::Session* session, std::uint64_t now_us, std::int64_t unix_s);

// The chat notices since the last call (Spanish, UTF-8).
std::vector<std::string> TakeLines();

// For the tests: forget everything (the wish is read again).
void Reset();

} // namespace CoopHostChoice
