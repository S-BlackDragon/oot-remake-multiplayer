// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: one clock for the whole session (oot-remake docs/HORA.md; D-R12). The rules are in
// coop/host/clock.hpp (tested in tests/host/clock_test.cpp); this is the game side: it takes the
// place of upstream's CoopTime::Sync in coop_time.cpp (which still reads the clock and runs the
// game's setter) and adds the nudges, written on the game's calc thread (a caller of the svc
// cave, right after the setter) and checked on the next frame.
//
// Switches: LICHII_ORM_CLOCK=0 (upstream's rules, coop_time_sync.h); LICHII_ORM_CLOCK_NUDGE=0
// (no nudges: sets as upstream); LICHII_COOP_DEBUG_TIME=1 also logs the clock every 2 s.
// Tests only: LICHII_ORM_CLOCK_TEST_OFF=<signed seconds>[@<delay s>] moves this game's clock
// that far without telling anyone, <delay> (default 5) seconds after joining, to watch it come
// back; LICHII_ORM_CLOCK_TEST_JUMP=<seconds>[@<delay s>] sets this game's clock there as the
// Sun's Song would (the others must follow it). Either works on any game, host or not.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {
class Session;
}

namespace CoopClock {

// LICHII_ORM_CLOCK is not 0.
bool On();

// Present thread, from CoopTime's Frame instead of Sync::Tick, a few times a second. `main` is
// the game's main (0: no game), `now` our clock (nothing: no clock). Fills `send`; returns a time
// to set with the game's setter (coop_time.cpp's), if any.
std::optional<std::uint32_t> Tick(std::uint64_t main, std::optional<std::uint32_t> now,
                                  coop::host::Session& session,
                                  std::vector<coop::protocol::Message>& send);

// Not connected any more.
void Reset();

// Network thread: a message from the session's on_event (only TIME_OF_DAY is kept).
void Receive(const coop::protocol::Message& message);

} // namespace CoopClock
