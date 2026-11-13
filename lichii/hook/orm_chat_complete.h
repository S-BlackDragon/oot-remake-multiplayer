// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM suggestions while typing a command in the Qt chat box (oot-remake
// docs/COMANDOS.md, "Sugerencias"), Minecraft's way: "/" lists the commands
// with their help, "/fp" narrows them, "/fps " shows the arguments (on, off;
// the players' names; colours), Tab puts the selected or first one in the box
// and Tab again goes on to the next, Up and Down move in the list, Enter runs.
// The engine is coop::host::orm::commands::Completer (tested without
// widgets); this is the thin Qt layer: an event filter on the box's line edit
// and a small list drawn over it. Built into the Qt frontend (qt.cmake).
#pragma once

class QLineEdit;

namespace OrmChatComplete {

// Hooks the suggestions to the chat box's line edit (once, after the box
// made it). The list lives and dies with `edit`.
void Attach(QLineEdit* edit);

} // namespace OrmChatComplete
