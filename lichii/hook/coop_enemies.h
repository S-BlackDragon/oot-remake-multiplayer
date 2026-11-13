// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM enemigos (oot-remake docs/ENEMIGOS.md): the mini-bosses of Ocarina of
// Time that the remake names as plain enemies (Enemy_Stalfos, Enemy_Lizalfos,
// Enemy_Dinolfos, Enemy_IronKnuckle_*, Enemy_Wolfos*), so the ownership rules
// treat them as daurosk's Enemy_MiddleBoss_* already are: each its own fight
// with the boss flag (60 s fight lock, the hit goes to whoever hurts it, the
// player clearly nearer it takes it; server/session.cpp, issue 150). With six
// players around one Iron Knuckle its AI then fights the one hitting it, in
// that player's own game, and every hit still goes to one owner (one life,
// one death). The names come from the game's Pack/Actor list (romfs, names
// only); picture book (_Dictionary) and stand-in (_Haribote) actors never
// count. No emulator code: CoopObjects::IsBoss asks this list.
#pragma once

#include <string_view>

namespace CoopEnemies {

// A mini-boss by its actor name (not the Enemy_MiddleBoss_* ones: IsBoss
// knows those by their prefix).
bool IsMiniBoss(std::string_view name);

}  // namespace CoopEnemies
