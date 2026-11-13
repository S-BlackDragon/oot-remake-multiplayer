// SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Cooperativo" screen (oot-remake docs/APP_UNICA.md, "Pantalla"): status
// rows with "Abrir carpeta", files dropped anywhere on it go to the helper's
// `import`, "Preparar" with its progress bar (on its own once everything is
// there), room and name, "Jugar", the version lines with "Novedades" and the
// update buttons, and the update toast with "Actualizar ahora". Every action is one or two helper requests
// (orm_helper_client.h); the screen keeps no rules of its own.
#pragma once

#include <cstdint>
#include <memory>

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "orm_app.h"
#include "orm_app_model.h"
#include "orm_helper_client.h"
#include "orm_app_channel.h"  // ORM orm/pvp: the hook in this process (lichii/hook)

class QCheckBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFrame;
class QTimer;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTextEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace OrmApp {

class Window : public QWidget {
public:
    Window(QWidget* main, QWidget* render, BootFn boot, QString helper, QStringList helper_args);
    ~Window() override;

    // Asks the helper for `status` again and fills the screen.
    void Refresh();
    // The game stopped: `game_ended` to the helper (the room's save, the
    // logs), then Refresh().
    void GameEnded();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Row {
        QLabel* mark = nullptr;
        QLabel* text = nullptr;
        QLabel* open = nullptr;  // "Abrir carpeta", for the rows with a folder
        QString folder;
    };

    void Build();
    void StartHelper();
    void ApplyStatus(const Status& status);
    void SetRow(Row& row, const QString& label, const Check& check);
    void SetBusy(bool busy, const QString& line = {});
    void SetLine(const QString& line, bool error = false);
    void SetUpdateLine(const QString& line, bool error = false);
    void OnEvent(const QString& name, const QJsonObject& event);
    // A plain notice (text and ×), or the update one with "Actualizar ahora".
    void ShowToast(const QString& text, bool update);
    void ShowUpdateToast(const QString& version);
    void SetToastProgress(int pct, const QString& text);
    void UpdateNow();
    void UpdateFailed(const QString& text);

    void Import(const QStringList& paths);
    void FinishImport(const ImportInfo& info);
    void OpenFolder(const Row& row);
    void Poll();
    void MaybeAutoPrepare();
    void ToggleShortcut();
    void Prepare();
    void RetryDownloads();
    // ORM orm/pvp (oot-remake docs/COMANDOS.md, "La app"): what the game's chat
    // asks of the app (/registros, /reintentar, /nombre), on the GUI thread; and
    // the PvP and "Fijar aliados" toggles redrawn from the hook's state.
    void OnChannel(const OrmAppChannel::Request& request);
    void SyncSwitches();
    void Play();
    void BootFromPlay(const PlayInfo& play);
    void CheckUpdate(bool automatic);
    void DownloadUpdate(bool restart_after);
    void ApplyAndRestart();
    void Rollback();
    void Restart();
    void OnQuit();

    QPointer<QWidget> main_;
    QPointer<QWidget> render_;
    BootFn boot_;
    QString helper_;
    QStringList helper_args_;
    HelperClient client_;
    Status status_;
    bool status_loaded_ = false;
    bool busy_ = false;
    bool update_busy_ = false;
    bool staged_ = false;      // an update is downloaded, applied on restart
    bool applied_ = false;     // update_apply_on_restart answered ok
    bool stopping_ = false;    // we stop the helper ourselves (quit, restart)
    bool coop_started_ = false;
    bool in_game_ = false;         // play booted a game, until game_ended
    bool auto_prepared_ = false;   // "Preparar" ran on its own once this run
    bool want_update_ = false;     // "Actualizar ahora" pressed: restart when the download is in
    bool downloading_ = false;     // update_download is out
    QString toast_closed_;         // the version whose toast × hid (until the next start)
    QString toast_version_;        // the version the update toast is about
    QString coop_room_;        // the room and name co-op started with
    QString coop_name_;

    Row keys_, firmware_, game_, puppet_, shaders_, version_;
    QFrame* toast_ = nullptr;
    QLabel* toast_text_ = nullptr;
    QPushButton* toast_button_ = nullptr;   // "Actualizar ahora" or "Reiniciar"
    QProgressBar* toast_progress_ = nullptr;
    QLabel* toast_line_ = nullptr;          // the download's step
    QPushButton* prepare_button_ = nullptr;
    QPushButton* retry_button_ = nullptr;  // "Reintentar": the owner's downloads again, now
    QProgressBar* progress_ = nullptr;
    QTextEdit* line_ = nullptr;  // the current step or the error, copyable
    QLineEdit* room_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* password_ = nullptr;
    QCheckBox* host_me_ = nullptr;  // ORM one host: "Quiero ser el anfitrión" (the helper's host = me)
    QPushButton* play_button_ = nullptr;
    QCheckBox* pvp_box_ = nullptr;   // ORM orm/pvp: the room's PvP switch
    QCheckBox* lock_box_ = nullptr;  // ORM orm/pvp: the room's "Fijar aliados" switch
    std::uint64_t switches_seen_ = ~std::uint64_t{0};
    QLabel* versions_ = nullptr;
    QLabel* update_line_ = nullptr;
    QPushButton* check_button_ = nullptr;
    QPushButton* update_button_ = nullptr;
    QPushButton* rollback_button_ = nullptr;
    QTextBrowser* notes_ = nullptr;
    QPushButton* shortcut_button_ = nullptr;
    QTimer* poll_timer_ = nullptr;
    std::unique_ptr<StatusPoller> poller_;
};

} // namespace OrmApp
