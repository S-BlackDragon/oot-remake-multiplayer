// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM pause, menus, saving and leaving with partners, the emulator side
// (oot-remake docs/MENUS_PAUSA.md; the rules are coop/host/menus.hpp). On
// unless LICHII_ORM_MENUS=0. Each frame, on the present thread:
// - Frame (before Session::frame): reads Sequence_IsPause and
//   Sequence_IsLoadingBanc through the game's flag table, ticks our own state
//   (playing, paused, title) and gives the session its PLAYER_STATUS bits
//   (paused, or loading + at_title), so they go out with this frame's status;
// - After (after Session::frame): the others' states from presence (chat
//   notices for the title screen and the way back), and a save written from
//   our own menu (the guest is told only the host saves the group's game).
// Log lines: "coop: menus: playing -> paused" (ours), "coop: menus: P2 'Ana'
// paused" (a mate's), "coop: menus: menu save seen (...)".
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/menus.hpp"
#include "coop/host/player_reader.hpp"

namespace coop::host {
class Session;
class GameMemory;
}

namespace CoopMenus {

// LICHII_ORM_MENUS is on (the default). Any thread.
bool On();

// Present thread, once a frame, before Session::frame. `memory` null: no game
// (nothing is read and the state stays as it was). `scene_readable`: a scene
// (Banc) is loaded (PlayerReader::read_scene). `session` null: not connected.
void Frame(coop::host::GameMemory* memory, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading, bool scene_readable);

// Present thread, once a frame, after Session::frame (`session` null: not
// connected; the mates are forgotten, so a reconnect says nothing about them).
void After(coop::host::Session* session);

// Present thread: our own state as of this frame's Frame (playing when off).
coop::host::menus::State State();

// Present thread: our game is paused by a menu: no name tags or markers are
// drawn over it (oot-64 D-037). False when off.
bool Paused();

// The note after a mate's name tag ("pausa"), or "" (also when off). Call it
// inside Session::with_presence with that player's PLAYER_STATUS flags.
std::string TagNote(std::uint8_t status_flags);

// Present thread: chat notices since the last call ("Ana salio al menu
// principal", "Ana volvio a la partida", with the accents; the guest's save line).
std::vector<std::string> TakeLines();

} // namespace CoopMenus
