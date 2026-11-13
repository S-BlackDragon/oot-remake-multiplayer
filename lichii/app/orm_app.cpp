// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_app.h"

#include <algorithm>
#include <utility>

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "common/logging.h"
#include "orm_app_window.h"

namespace OrmApp {

namespace {

QString IniPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("orm-app.ini"));
}

// The helper: `helper=` in orm-app.ini's [app] section (relative to the exe's
// folder), else ..\orm-launcher.exe: the app's root holds the helper and
// lichii.exe lives in its emulator\ folder. Always `--service`, plus
// `helper_args=` when the ini gives some (tests, a second channel).
std::pair<QString, QStringList> HelperCommand() {
    QSettings ini(IniPath(), QSettings::IniFormat);
    const QDir dir(QCoreApplication::applicationDirPath());
    QString helper = ini.value(QStringLiteral("app/helper")).toString().trimmed();
    if (helper.isEmpty()) {
        helper = QStringLiteral("../orm-launcher.exe");
    }
    if (QFileInfo(helper).isRelative()) {
        helper = dir.filePath(helper);
    }
    QStringList args{QStringLiteral("--service")};
    const QString extra = ini.value(QStringLiteral("app/helper_args")).toString().trimmed();
    if (!extra.isEmpty()) {
        args << QProcess::splitCommand(extra);
    }
    return {QDir::cleanPath(helper), args};
}

// Keeps the game list hidden while the screen is home, and steps the screen
// aside when the game shows. Not a QObject subclass with Q_OBJECT: only
// eventFilter is overridden, which needs no moc.
class HomeFilter : public QObject {
public:
    HomeFilter(QObject* parent, Window* window, QBoxLayout* layout,
               std::vector<QWidget*> home_hidden, std::vector<QWidget*> game_widgets)
        : QObject(parent), window_(window), layout_(layout) {
        for (QWidget* w : home_hidden) {
            Track(w);
        }
        for (QWidget* w : game_widgets) {
            if (w != nullptr) {
                game_widgets_.emplace_back(w);
                w->installEventFilter(this);
            }
        }
    }

    // Everything else in the central row (Fritchii's own home page, which it
    // adds after us and shows again on its own) is kept hidden while the
    // screen is home, so the screen has the whole area. Fritchii's left rail
    // stays: it holds the emulator's settings and menus.
    void AdoptSiblings() {
        if (!layout_) {
            return;
        }
        for (int i = 0; i < layout_->count(); ++i) {
            QWidget* w = layout_->itemAt(i) != nullptr ? layout_->itemAt(i)->widget() : nullptr;
            if (w == nullptr || w == window_.data() || IsGame(w) ||
                w->objectName() == QStringLiteral("FritchiiRail")) {
                continue;
            }
            Track(w);
            if (!playing_) {
                w->hide();
            }
        }
    }

    void GoHome() {
        const bool was_playing = std::exchange(playing_, false);
        AdoptSiblings();
        for (const auto& w : home_hidden_) {
            if (w) {
                w->hide();
            }
        }
        if (window_) {
            window_->show();
            if (was_playing) {
                window_->GameEnded();  // tells the helper, then the status again
            } else {
                window_->Refresh();
            }
        }
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() != QEvent::Show) {
            return false;
        }
        auto* widget = static_cast<QWidget*>(obj);  // only widgets are watched
        if (IsGame(widget)) {
            playing_ = true;
            // A render window of its own (not single-window mode) leaves the
            // screen where it is.
            if (window_ && !widget->isWindow()) {
                window_->hide();
            }
            return false;
        }
        // The game list wants to show. Decided right after (hiding a widget
        // inside its own show event is not safe): while home it is hidden
        // again; when the game is gone (a boot that failed without the
        // emulation's stop) the screen comes back.
        QPointer<QWidget> w(widget);
        QTimer::singleShot(0, this, [this, w] {
            if (!w || !w->isVisible()) {
                return;
            }
            if (!playing_) {
                w->hide();
            } else if (!AnyGameVisible()) {
                GoHome();
            }
        });
        return false;
    }

private:
    void Track(QWidget* w) {
        if (w == nullptr) {
            return;
        }
        for (const auto& known : home_hidden_) {
            if (known == w) {
                return;
            }
        }
        home_hidden_.emplace_back(w);
        w->installEventFilter(this);
    }

    bool AnyGameVisible() const {
        return std::any_of(game_widgets_.begin(), game_widgets_.end(),
                           [](const QPointer<QWidget>& g) { return g && g->isVisible(); });
    }

    bool IsGame(QWidget* widget) const {
        return std::any_of(game_widgets_.begin(), game_widgets_.end(),
                           [widget](const QPointer<QWidget>& w) { return w == widget; });
    }

    QPointer<Window> window_;
    QPointer<QBoxLayout> layout_;
    std::vector<QPointer<QWidget>> home_hidden_;
    std::vector<QPointer<QWidget>> game_widgets_;
    bool playing_ = false;
};

QPointer<HomeFilter> g_filter;

} // namespace

bool Enabled() {
    return QFileInfo::exists(IniPath());
}

void Attach(QWidget* main, QWidget* render, QBoxLayout* layout, std::vector<QWidget*> home_hidden,
            std::vector<QWidget*> game_widgets, BootFn boot) {
    if (!Enabled() || main == nullptr || layout == nullptr || g_filter) {
        return;
    }
    auto [helper, args] = HelperCommand();
    LOG_INFO(Frontend, "orm-app: on ({}), helper {}", IniPath().toStdString(),
             helper.toStdString());
    auto* window = new Window(main, render, std::move(boot), helper, args);
    layout->addWidget(window, 1);  // the whole central area, growing with the window
    for (QWidget* w : home_hidden) {
        if (w != nullptr) {
            w->hide();
        }
    }
    g_filter = new HomeFilter(main, window, layout, std::move(home_hidden), std::move(game_widgets));
    window->show();
    // Fritchii builds its home page after this call, in the same constructor:
    // once the event loop runs it is in the row and is hidden with the rest.
    QTimer::singleShot(0, g_filter.data(), [] {
        if (g_filter) {
            g_filter->AdoptSiblings();
        }
    });
}

void OnGameStopped() {
    if (g_filter) {
        g_filter->GoHome();
    }
}

} // namespace OrmApp
