// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM chat commands, PvP and the on-screen statistics in the emulator
// (oot-remake docs/COMANDOS.md). The rules are portable
// (coop/host/orm_commands.hpp, orm_pvp.hpp, orm_stats.hpp); this runs them:
//
// - OnChat: a line the player sends that starts with '/' is a command, run
//   here and never sent as chat (daurosk's /teleport is /ir now, through its
//   own code). Any thread.
// - OnEvent: ROOM_SWITCH, PLAYER_HIT, PLAYER_CALL, PLAYER_RENAME, WELCOME and
//   the others' TIME_OF_DAY. Network thread.
// - Frame: PvP (our hits on mates' puppets out, theirs on us applied: damage
//   written to our Link's life, the push by moving him), our body kept out of
//   the puppets, a mate's bomb that went off with no copy here, the stats.
//   Present thread.
// - The HUD, the map, the chat and the Cooperativo screen ask the getters.
//
// Switches: LICHII_ORM_COMMANDS (on unless 0: the commands; with it off a
// '/' line goes to daurosk's /teleport as before), LICHII_ORM_PVP (on unless
// 0: hits, pushes, bodies and bombs; the room switch still shows),
// LICHII_ORM_PVP_PUSH (on unless 0: the knockback), LICHII_ORM_BODIES (on
// unless 0: our Link kept out of the puppets), LICHII_ORM_BLAST_FALLBACK (on
// unless 0). Log lines "coop: commands: ...", "coop: pvp: ...".
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/downed.hpp"
#include "coop/host/orm_commands.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/overlay/overlay.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {
class Session;
}
namespace coop::net {
class Client;
struct Event;
}
namespace CoopPlatform {
class Platform;
}

namespace OrmCommands {

// A chat line the player sends: true when it was a command (done; its answer
// shows in the chat as a notice). Any thread; call before anything else of
// the chat path takes a lock.
bool OnChat(std::string_view text, coop::host::Session& session);

// Network thread, with the client (session.on_event).
void OnEvent(const coop::net::Event& event, coop::net::Client& client);

// Present thread, once a frame: at the top of the hook's frame (its time
// for /stats) and after the features (everything else). `session` null:
// not connected. `others`: the mates as presence shows them this frame.
void FrameStart();
void Frame(CoopPlatform::Platform& platform, const std::optional<coop::host::PlayerReading>& reading,
           coop::host::Session* session, std::uint64_t server_now_us,
           const std::vector<coop::overlay::Shown>& others, coop::host::DownedState downed);

// Present thread: the chat notices since the last call.
std::vector<std::string> TakeLines();

// ---- Getters (any thread) ----

bool PvpOn();        // the session's PvP (upstream's PVP_MODE since test10)

// ORM test10 (coop/host/orm_pvp.hpp, "On upstream's PvP"): our rules as a thin
// layer on upstream's PvP, called from its coop_pvp_game.cpp (present thread).
// With LICHII_ORM_PVP=0 both give upstream's value back unchanged.
// - The amount of a PVP_HIT our game sends: our real weapon damage.
std::int32_t PvpAttackAmount(coop::protocol::DamageCause cause, std::int32_t upstream_amount,
                             const std::optional<coop::host::PlayerReading>& reading);
// - What a PVP_HIT for us takes after upstream's victim decided `upstream_take`:
//   0 when our shield faces the attacker, Nayru's Love is up, we are invulnerable
//   (just hit, or our own hit reaction plays) or downed; never our last quarter
//   while nobody else is up. Starts the knockback (LICHII_ORM_PVP_PUSH).
std::int32_t PvpVictimTake(const coop::protocol::PvpHit& hit, std::int32_t upstream_take,
                           const std::optional<coop::host::PlayerReading>& reading,
                           std::optional<std::int32_t> life);
bool LockMatesOn();  // the room's "Fijar aliados" switch (the puppet pack's lock-on: COMANDOS.md)
bool BodiesOn();     // ORM choque: the room's /choque (its default until the room sets it: docs/CHOQUE.md)
// ORM choque (coop_bodies.cpp): our game cannot give the mates a body (the
// pack, a failed create, LICHII_ORM_BODY_COLLIDER=0): /choque says so.
void SetBodiesFallback(bool fallback);
bool Muted();        // /silencio: the mates' chat is not shown here
bool MapShown();     // /mapa
// /fps, /ping, /red, /stats: the HUD's corner lines, top right.
std::vector<std::string> CornerLines();

// ---- The Cooperativo screen (oot-remake docs/COMANDOS.md, "Pantalla") ----

// Set a room switch as /pvp on does (false: not connected). Any thread.
bool SetRoomSwitch(coop::protocol::RoomSwitchKey key, bool on);
// The commands' help lines, for the screen's list.
std::vector<std::string> HelpLines();

// ---- The chat box's suggestions (orm_chat_complete.cpp) ----

// The list for `text` as typed, with the room's players. Any thread.
std::vector<coop::host::orm::commands::Suggestion> Suggest(std::string_view text);

} // namespace OrmCommands
