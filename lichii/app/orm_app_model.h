// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM single app (oot-remake docs/APP_UNICA.md, "Pantalla"): what the screen
// reads from the helper's replies and the Spanish texts it shows, apart from
// the widgets so the unit tests check them with Qt Core only. Every reader is
// lenient: a field the helper does not send leaves its default (a check row
// shows "…", not a cross), so the screen and the helper can grow apart a
// little without breaking each other.
#pragma once

#include <utility>
#include <vector>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace OrmApp {

// One status row: keys, firmware, game, puppet pack, shaders.
struct Check {
    bool known = false;  // the helper said something about it
    bool ok = false;
    QString detail;      // a path, a version, or why not
    QString folder;      // the app folder the row's files go in (keys, firmware, game)
};

struct Status {
    Check keys;
    Check firmware;
    Check game;
    Check puppet;
    Check shaders;
    QString app_version;      // the installed app
    QString channel_version;  // the newest one on the server, empty if unknown
    QString kit_version;
    QString protocol;         // the helper's (its kit's) co-op protocol
    QString game_path;
    QString keys_path;
    QString firmware_path;
    QString room;             // the last room and name, to fill the fields
    QString name;
    // ORM one host (oot-remake docs/PARTIDA_SALA.md): the helper's "host", "me"
    // (the toggle "Quiero ser el anfitrion" on), "auto" or "never" (this PC never
    // hosts: room_save_host = false, the toggle greyed out); empty: an older helper.
    QString host;
    QString notes_installed;  // release notes of the installed version
    QString notes_available;  // and of the newer one, when there is one
    bool update_ready = false;  // a newer version is downloaded, waiting for a restart
    QString previous_version;   // the one "Volver a la versión anterior" goes back to
    bool rollback_available = false;
    Check shortcut;   // the desktop shortcut: known when the helper says, ok = it is there
    QString app_dir;  // the app's own folder
    bool downloads_pending = false;  // the owner's files are still coming: "Reintentar"
};

// What the helper did with files dropped on the screen (`import`).
struct ImportInfo {
    QJsonValue id;      // import_id as the helper gave it, sent back verbatim
    int imported = 0;   // files put in the app folders
    int skipped = 0;    // files it did not take
    QString ask;        // the question before deleting the originals; empty: none
    QString summary;    // one Spanish line of what happened
};

struct UpdateInfo {
    bool available = false;  // the server has a version other than ours
    bool ready = false;      // already downloaded (update/), applied on restart
    QString version;
    QString notes;
};

struct PlayInfo {
    QString game;  // the file the emulator boots
    QStringList args;
    std::vector<std::pair<QString, QString>> env;  // set before co-op starts
    bool fullscreen = false;
};

// A reply's "ok" (false when missing) and its error, or `fallback`.
bool Ok(const QJsonObject& reply);
QString ErrorText(const QJsonObject& reply, const QString& fallback);

Check ParseCheck(const QJsonObject& reply, const QString& key);
Status ParseStatus(const QJsonObject& reply);
UpdateInfo ParseUpdate(const QJsonObject& reply);
ImportInfo ParseImport(const QJsonObject& reply);
// The local file paths of a drop's URLs (other URLs are left out), native
// separators, no duplicates, in the order they came.
QStringList DroppedPaths(const QStringList& urls);

// "Preparar" on its own: keys, firmware and game are there and the puppet
// pack or the shaders are not.
bool ShouldAutoPrepare(const Status& status);
// A helper toast about a version to install (it names one, or asks for a
// restart): it gets the "Actualizar ahora" button.
bool IsUpdateToast(const QJsonObject& event);
// The helper's room_saved event (it sent the room's save while the game runs)
// that shows "Guardado" on the game's HUD (oot-64 D-054): the save is the
// room's one now ("stored" main, or none from a channel of v1), not one kept
// aside.
bool ShowsSaved(const QString& name, const QJsonObject& event);

// Empty `game` when the reply names none (then the screen refuses to play).
PlayInfo ParsePlay(const QJsonObject& reply);

// "✓", "✗" or "…" (unknown) for a row.
QString Mark(const Check& check);
// The row's text: "Claves: ✓ C:\\...\\prod.keys".
QString RowText(const QString& label, const Check& check);
// A `progress` event as one line: "Shaders: 40 % - compilando".
QString ProgressText(const QJsonObject& event);
// The progress bar's value, 0..100 (a pct outside is clamped; -1 = unknown).
int ProgressPct(const QJsonObject& event);
// A download's line from the event's bytes_done, bytes_total, speed_bps and
// eta_s (helper 0.11.8): "Juego desde Drive: 3,2 de 25,6 GB · 48 MB/s ·
// quedan 8 min (intento 1 de 5)"; empty when the event has no bytes.
QString TransferText(const QJsonObject& event);
// Its pieces: "3,2 de 25,6 GB", "48 MB/s" ("calculando…" when 0), "quedan 8 min"
// ("" when the time left is not known).
QString SizePair(qint64 done, qint64 total);
QString SpeedText(double bytes_per_second);
QString EtaText(int seconds);
// The version lines of the screen (app, kit, protocol, emulator, server).
QStringList VersionLines(const Status& status, const QString& emulator_build,
                         const QString& own_protocol);
// The "Novedades" panel: the installed version's notes, then the new one's.
QString NotesText(const Status& status);

} // namespace OrmApp
