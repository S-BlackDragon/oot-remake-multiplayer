// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "Continue with the group's game", the emulator side (oot-remake
// docs/CONTINUAR.md; the rule is coop/host/continue_load.hpp). Glue only:
// each frame, from CoopDowned::Frame (before the session's frame), it gives
// the downed state and our flag watcher's role and counters to the rule and
// tells the session how the next load window is taken (keep: the group goes
// on with what it had when it fell, the wallet too; save: it follows the
// host's reload). Only with LICHII_COOP_DOWNED=1 (no group defeat without
// it); LICHII_ORM_CONTINUE = keep (default) | save | 0 (off). One line per
// step: "coop: continue: ...".
#pragma once

#include <cstdint>
#include <string_view>

#include "coop/host/downed.hpp"

namespace coop::host {
class Session;
}

namespace CoopContinue {

// LICHII_ORM_CONTINUE is not "0" (keep or save).
bool On();

// Present thread, once a frame while LICHII_COOP_DOWNED is on. `now_us`: a
// steady clock; `event_running` and `event_name` ("" when not read): our
// event this frame, so a game over event still running keeps the Continue
// waiting for its reload (REVISION_NUEVAS).
void Frame(coop::host::Session* session, coop::host::DownedState state, bool group_defeat, std::uint64_t now_us,
           bool event_running, std::string_view event_name);

} // namespace CoopContinue
