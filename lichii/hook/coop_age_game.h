// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "the room's age" (oot-remake D-R17, docs/EDAD.md), the emulator side;
// the rules are coop/host/age.hpp. With LICHII_ORM_AGE=1 a mate's age change
// (his PLAYER_SAMPLE age, settled) is followed: once nothing runs here, our
// game asks by name the game's own era change (Market_Child_Field_ChildToAdult
// or Market_Adult_Field_AdultToChild, what the pedestal runs): the game swaps
// the equipment (its sub flow ReplaceEquipmentForEraChange, which it never
// starts by name), sets Sequence_IsChildhood and loads the Temple of Time.
// With LICHII_ORM_AGE_EVENT naming another event, the in-place reload: that
// event, then (once it runs) Sequence_IsChildhood and the scene change to the
// same scene at the other age (Sequence_ReservedNextBanc, the boss pull's way)
// and, once loaded, Link put back where he stood. A reload that does not take
// puts the era back to our Link's age. Log lines start "coop: age:". The test
// command `age adult|child` asks the same reload in this game, on or off.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coop/host/player_reader.hpp"

namespace CoopPlatform {
class Platform;
}
namespace coop::host {
class Session;
}

namespace CoopAge {

// LICHII_ORM_AGE is on.
bool On();

// Present thread: reload this game at that age (the test command `age`).
void Change(bool child);

// Present thread: the chat lines of the room's age (2026-11-13: "<Nombre>
// ha cambiado de época: ...", "Esperando a <Nombre> ...", "Grupo reunido en el
// Templo del Tiempo", "No se pudo cambiar de época ..."), oldest first; the
// big centred notice goes by CoopNotices::TheBanner. coop_hook.cpp adds them
// to the chat after Frame.
std::vector<std::string> TakeLines();

// Present thread: a reload under way began while our Link lay down (D-R62,
// oot-64 D-178): the downed machine holds him down through it and puts him
// down again if the load got him up (coop/host/downed.hpp
// DownedInput::hold_down). As of the last Frame.
bool HoldsDown();

// Present thread, once a frame, after the downed state machine's frame.
// `main_base` 0: no game.
void Frame(CoopPlatform::Platform& platform, std::uint64_t main_base, coop::host::Session* session,
           const std::optional<coop::host::PlayerReading>& reading);

} // namespace CoopAge
