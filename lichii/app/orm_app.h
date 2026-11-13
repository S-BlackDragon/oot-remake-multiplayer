// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM single app (oot-remake docs/APP_UNICA.md, piece C): the "Cooperativo"
// screen inside lichii.exe. The emulator's main window calls these three
// functions (lichii/patches/0102-orm-app.patch) and nothing else; the screen,
// the helper client and all the rules live in lichii/app/.
//
// Only when orm-app.ini is beside the exe: without it the emulator is the
// plain co-op build (game list first, co-op started at once as before).
#pragma once

#include <functional>
#include <vector>

class QBoxLayout;
class QString;
class QWidget;

namespace OrmApp {

// Boots `game` in this process (the main window's own BootGame), full screen
// when asked.
using BootFn = std::function<void(const QString& game, bool fullscreen)>;

// True when orm-app.ini is beside the exe. Then the main window does not
// start co-op at once (CoopWindow::Attach): the screen does it on "Jugar",
// after the helper's `play` has set the environment co-op reads.
bool Enabled();

// Puts the screen into `layout` (the main window's central row) in place of
// the game list: `home_hidden` (the game list and its placeholder) stays
// hidden while the screen is up, and when one of `game_widgets` (the render
// window, the loading screen) shows, the screen steps aside. Does nothing
// unless Enabled(). Once, on the GUI thread, after those widgets exist.
void Attach(QWidget* main, QWidget* render, QBoxLayout* layout, std::vector<QWidget*> home_hidden,
            std::vector<QWidget*> game_widgets, BootFn boot);

// The game stopped: the screen comes back in place of the game list.
void OnGameStopped();

} // namespace OrmApp
