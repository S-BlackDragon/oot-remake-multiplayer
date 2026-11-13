// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM: the in-process channel between the game's hook and the Cooperativo
// screen (lichii/app), which run in the same lichii.exe (oot-remake
// docs/COMANDOS.md, "La app"). No new process, no window, no socket: a few
// callbacks and a queue behind one mutex.
//
// - Hook -> app: /registros (the helper's upload_logs), /reintentar (its
//   retry_downloads) and /nombre (its room name, so the app keeps the new
//   name). The app listens from its GUI thread with Listen(); the listener
//   is called on the hook's thread and must post to the GUI thread itself.
//   Ask() says false when no app listens (the plain co-op build): the hook
//   then says so in the chat and logs which path ran.
// - App -> hook: the answers for the chat (Answer), and the screen's PvP and
//   "Fijar aliados" toggles (SetSwitch, through the setter the hook gives).
// - Both: the room's switches as the hook has them (PublishSwitches /
//   CurrentSwitches) and a counter the screen polls to redraw its toggles.
//
// Any thread. Built into core with the hook (core.cmake); the app links it
// through core. Tested in tests/orm/app_channel_test.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace OrmAppChannel {

enum class Ask : std::uint8_t {
    upload_logs,      // the helper's upload_logs
    retry_downloads,  // the helper's retry_downloads
    save_name,        // the helper's room {name}: `text` is the new name
};

struct Request {
    Ask ask = Ask::upload_logs;
    std::string text;
};

using Listener = std::function<void(const Request&)>;
// The app starts or stops listening (an empty listener: none). The last one wins.
void Listen(Listener listener);
bool Listening();
// The hook asks the app: false (nothing done) when no app listens.
bool Send(const Request& request);

// The app's answer for the chat ("Registros enviados"), from any thread; the
// hook shows them on its next frame.
void Answer(std::string line);
std::vector<std::string> TakeAnswers();

// The room's switches as the hook has them.
struct Switches {
    bool connected = false;
    bool pvp = false;
    bool lock_mates = false;
    bool lock_available = true;  // false: the ally target fell back (COMANDOS.md)
    bool operator==(const Switches&) const = default;
};
void PublishSwitches(const Switches& switches);
Switches CurrentSwitches();
// Steps each time PublishSwitches changes something: the screen redraws.
std::uint64_t SwitchesVersion();

// key 1 PvP, 2 Fijar aliados (protocol::RoomSwitchKey). The hook gives the
// setter (it sends ROOM_SWITCH, as /pvp on does); SetSwitch is false while
// it has not or when not sent.
using SwitchSetter = std::function<bool(std::uint8_t key, bool on)>;
void SetSwitchSetter(SwitchSetter setter);
bool SetSwitch(std::uint8_t key, bool on);

// Tests: everything back to nothing.
void Reset();

} // namespace OrmAppChannel
