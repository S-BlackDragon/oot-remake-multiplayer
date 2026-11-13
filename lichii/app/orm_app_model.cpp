// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_app_model.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QDir>
#include <QJsonArray>
#include <QUrl>
#include <QJsonValue>

namespace OrmApp {

namespace {

QString Text(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<long long>(d))) {
            return QString::number(static_cast<long long>(d));
        }
        return QString::number(d);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    }
    return {};
}

// The first of `keys` that `object` has as a non-empty text.
QString First(const QJsonObject& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QString text = Text(object.value(QLatin1String(key)));
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QString Notes(const QJsonValue& value) {
    if (value.isArray()) {  // a list of lines is fine too
        QStringList lines;
        for (const QJsonValue& line : value.toArray()) {
            lines << Text(line);
        }
        return lines.join(QLatin1Char('\n')).trimmed();
    }
    return Text(value).trimmed();
}

} // namespace

bool Ok(const QJsonObject& reply) {
    return reply.value(QStringLiteral("ok")).toBool(false);
}

QString ErrorText(const QJsonObject& reply, const QString& fallback) {
    const QString error = reply.value(QStringLiteral("error")).toString().trimmed();
    return error.isEmpty() ? fallback : error;
}

Check ParseCheck(const QJsonObject& reply, const QString& key) {
    Check check;
    const QJsonValue value = reply.value(key);
    if (value.isBool()) {
        check.known = true;
        check.ok = value.toBool();
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        check.known = object.contains(QStringLiteral("ok"));
        check.ok = object.value(QStringLiteral("ok")).toBool(false);
        check.detail = First(object, {"detail", "text", "path", "version", "error"});
        check.folder = First(object, {"folder"});
    } else if (value.isString()) {  // a path: there when it names one
        check.known = true;
        check.detail = value.toString();
        check.ok = !check.detail.isEmpty();
    }
    return check;
}

Status ParseStatus(const QJsonObject& reply) {
    Status s;
    s.keys = ParseCheck(reply, QStringLiteral("keys"));
    s.downloads_pending = reply.value(QStringLiteral("downloads"))
                              .toObject()
                              .value(QStringLiteral("pending"))
                              .toBool(false);
    s.firmware = ParseCheck(reply, QStringLiteral("firmware"));
    s.game = ParseCheck(reply, QStringLiteral("game"));
    s.puppet = ParseCheck(reply, QStringLiteral("puppet_pack"));
    s.shaders = ParseCheck(reply, QStringLiteral("shaders"));
    s.app_version = First(reply, {"version", "app_version"});
    s.channel_version = First(reply, {"channel_version", "latest_version"});
    const QJsonObject channel = reply.value(QStringLiteral("channel")).toObject();
    if (s.channel_version.isEmpty()) {
        s.channel_version = First(channel, {"version"});
    }
    s.kit_version = First(reply, {"kit_version"});
    s.protocol = First(reply, {"protocol"});
    const QString shaders_version = First(reply, {"shaders_version"});
    if (s.shaders.detail.isEmpty() && !shaders_version.isEmpty()) {
        s.shaders.detail = QStringLiteral("versión ") + shaders_version;
    }
    const QJsonObject paths = reply.value(QStringLiteral("paths")).toObject();
    s.game_path = First(paths, {"game"});
    s.keys_path = First(paths, {"keys"});
    s.firmware_path = First(paths, {"firmware"});
    if (s.game_path.isEmpty()) {
        s.game_path = First(reply, {"game_path"});
    }
    if (s.keys_path.isEmpty()) {
        s.keys_path = First(reply, {"keys_path"});
    }
    if (s.firmware_path.isEmpty()) {
        s.firmware_path = First(reply, {"firmware_path"});
    }
    // A row with no detail of its own shows its folder.
    if (s.game.detail.isEmpty()) {
        s.game.detail = s.game_path;
    }
    if (s.keys.detail.isEmpty()) {
        s.keys.detail = s.keys_path;
    }
    if (s.firmware.detail.isEmpty()) {
        s.firmware.detail = s.firmware_path;
    }
    // "room" is a text here; an object {"room","name"} is read as well.
    const QJsonValue room = reply.value(QStringLiteral("room"));
    if (room.isObject()) {
        s.room = First(room.toObject(), {"room"});
        s.name = First(room.toObject(), {"name"});
    } else {
        s.room = Text(room);
        s.name = First(reply, {"name"});
    }
    s.host = First(reply, {"host"});  // ORM one host: "me", "auto", "never"
    s.notes_installed = Notes(reply.value(QStringLiteral("notes")));
    s.notes_available = Notes(reply.value(QStringLiteral("channel_notes")));
    if (s.notes_available.isEmpty()) {
        s.notes_available = Notes(channel.value(QStringLiteral("notes")));
    }
    s.update_ready = reply.value(QStringLiteral("update_ready")).toBool(false);
    s.previous_version = First(reply, {"previous_version"});
    s.rollback_available =
        reply.value(QStringLiteral("rollback")).toBool(false) || !s.previous_version.isEmpty();
    s.shortcut = ParseCheck(reply, QStringLiteral("shortcut"));
    s.app_dir = First(reply, {"app_dir"});
    return s;
}

ImportInfo ParseImport(const QJsonObject& reply) {
    ImportInfo info;
    info.id = reply.value(QStringLiteral("import_id"));
    info.imported = static_cast<int>(reply.value(QStringLiteral("imported")).toArray().size());
    info.skipped = static_cast<int>(reply.value(QStringLiteral("skipped")).toArray().size());
    info.ask = reply.value(QStringLiteral("ask")).toString().trimmed();
    info.summary = reply.value(QStringLiteral("summary")).toString().trimmed();
    if (info.id.isUndefined() || info.id.isNull()) {
        info.ask.clear();  // nothing to finish without an id
    }
    return info;
}

QStringList DroppedPaths(const QStringList& urls) {
    QStringList out;
    for (const QString& text : urls) {
        const QUrl url(text);
        if (!url.isLocalFile()) {
            continue;
        }
        QString path = QDir::toNativeSeparators(url.toLocalFile());
        while (path.size() > 3 && (path.endsWith(QLatin1Char('\\')) || path.endsWith(QLatin1Char('/')))) {
            path.chop(1);  // a folder dropped from some shells ends with a separator
        }
        if (!path.isEmpty() && !out.contains(path, Qt::CaseInsensitive)) {
            out << path;
        }
    }
    return out;
}

bool ShouldAutoPrepare(const Status& status) {
    const bool inputs = status.keys.ok && status.firmware.ok && status.game.ok;
    return inputs && (!status.puppet.ok || !status.shaders.ok);
}

bool IsUpdateToast(const QJsonObject& event) {
    return !event.value(QStringLiteral("version")).toString().trimmed().isEmpty() ||
           event.value(QStringLiteral("restart")).toBool(false);
}

bool ShowsSaved(const QString& name, const QJsonObject& event) {
    if (name != QStringLiteral("room_saved")) {
        return false;
    }
    const QString stored = event.value(QStringLiteral("stored")).toString();
    return stored.isEmpty() || stored == QStringLiteral("main");
}

UpdateInfo ParseUpdate(const QJsonObject& reply) {
    UpdateInfo u;
    u.available = reply.value(QStringLiteral("available")).toBool(false);
    u.ready = reply.value(QStringLiteral("ready")).toBool(false) ||
              reply.value(QStringLiteral("update_ready")).toBool(false);
    u.version = First(reply, {"version", "channel_version"});
    u.notes = Notes(reply.value(QStringLiteral("notes")));
    return u;
}

PlayInfo ParsePlay(const QJsonObject& reply) {
    PlayInfo p;
    for (const QJsonValue& arg : reply.value(QStringLiteral("args")).toArray()) {
        p.args << Text(arg);
    }
    p.game = First(reply, {"game", "path"});
    if (p.game.isEmpty()) {
        // As on the emulator's command line: "-g <file>", else the last
        // argument that is not an option.
        for (qsizetype i = 0; i < p.args.size(); ++i) {
            if (p.args[i] == QStringLiteral("-g") && i + 1 < p.args.size()) {
                p.game = p.args[i + 1];
                break;
            }
        }
        if (p.game.isEmpty() && !p.args.isEmpty() && !p.args.last().startsWith(QLatin1Char('-'))) {
            p.game = p.args.last();
        }
    }
    p.fullscreen = reply.value(QStringLiteral("fullscreen")).toBool(false) ||
                   p.args.contains(QStringLiteral("-f"));
    const QJsonObject env = reply.value(QStringLiteral("env")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        if (!it.key().isEmpty() && !it.key().contains(QLatin1Char('='))) {
            p.env.emplace_back(it.key(), Text(it.value()));
        }
    }
    return p;
}

QString Mark(const Check& check) {
    if (!check.known) {
        return QStringLiteral("…");
    }
    return check.ok ? QStringLiteral("✓") : QStringLiteral("✗");
}

QString RowText(const QString& label, const Check& check) {
    QString text = label + QStringLiteral(": ") + Mark(check);
    if (!check.detail.isEmpty()) {
        text += QLatin1Char(' ') + check.detail;
    } else if (check.known && !check.ok) {
        text += QStringLiteral(" falta");
    }
    return text;
}

namespace {

QString TaskName(const QString& task) {
    if (task == QStringLiteral("puppet_pack") || task == QStringLiteral("build_puppet_pack")) {
        return QStringLiteral("Paquete de marioneta");
    }
    if (task == QStringLiteral("shaders") || task == QStringLiteral("prepare_shaders")) {
        return QStringLiteral("Shaders");
    }
    if (task == QStringLiteral("update") || task == QStringLiteral("update_download")) {
        return QStringLiteral("Actualización");
    }
    if (task == QStringLiteral("import")) {
        return QStringLiteral("Archivos");
    }
    if (task == QStringLiteral("firmware")) {
        return QStringLiteral("Firmware");
    }
    if (task == QStringLiteral("check_game")) {
        return QStringLiteral("Juego");
    }
    return task;
}

} // namespace

namespace {

QString Decimal(double v) {
    return QString::number(v, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(','));
}

} // namespace

QString SizePair(qint64 done, qint64 total) {
    constexpr double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    const double t = static_cast<double>(total), d = static_cast<double>(done);
    if (t >= gb) {
        return Decimal(d / gb) + QStringLiteral(" de ") + Decimal(t / gb) + QStringLiteral(" GB");
    }
    if (t >= mb) {
        return Decimal(d / mb) + QStringLiteral(" de ") + Decimal(t / mb) + QStringLiteral(" MB");
    }
    return QStringLiteral("%1 de %2 KB")
        .arg(static_cast<qint64>(std::ceil(d / kb)))
        .arg(static_cast<qint64>(std::ceil(t / kb)));
}

QString SpeedText(double bps) {
    constexpr double kb = 1024.0, mb = kb * 1024;
    if (bps <= 0) {
        return QStringLiteral("calculando…");
    }
    if (bps >= 10 * mb) {
        return QStringLiteral("%1 MB/s").arg(static_cast<qint64>(std::llround(bps / mb)));
    }
    if (bps >= mb) {
        return Decimal(bps / mb) + QStringLiteral(" MB/s");
    }
    return QStringLiteral("%1 KB/s").arg(static_cast<qint64>(std::llround(std::max(1.0, bps / kb))));
}

QString EtaText(int secs) {
    if (secs < 0) {
        return {};
    }
    if (secs < 60) {
        return QStringLiteral("quedan %1 s").arg(secs);
    }
    if (secs < 3600) {
        return QStringLiteral("quedan %1 min").arg((secs + 59) / 60);
    }
    int h = secs / 3600, m = (secs % 3600 + 59) / 60;
    if (m == 60) {
        ++h;
        m = 0;
    }
    if (m == 0) {
        return QStringLiteral("quedan %1 h").arg(h);
    }
    return QStringLiteral("quedan %1 h %2 min").arg(h).arg(m);
}

QString TransferText(const QJsonObject& event) {
    const qint64 total = static_cast<qint64>(event.value(QStringLiteral("bytes_total")).toDouble(0));
    if (total <= 0) {
        return {};
    }
    const qint64 done = static_cast<qint64>(event.value(QStringLiteral("bytes_done")).toDouble(0));
    QStringList parts;
    const QString detail = event.value(QStringLiteral("detail")).toString().trimmed();
    parts << (detail.isEmpty() ? SizePair(done, total) : detail);
    if (done < total) {
        parts << SpeedText(event.value(QStringLiteral("speed_bps")).toDouble(0));
        const QString eta = EtaText(event.value(QStringLiteral("eta_s")).toInt(-1));
        if (!eta.isEmpty()) {
            parts << eta;
        }
    }
    QString line = parts.join(QStringLiteral(" · "));
    const QString label = event.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty()) {
        line = label + QStringLiteral(": ") + line;
    }
    const QString note = event.value(QStringLiteral("note")).toString().trimmed();
    if (!note.isEmpty()) {
        line += QStringLiteral(" (") + note + QStringLiteral(")");
    }
    return line;
}

int ProgressPct(const QJsonObject& event) {
    const QJsonValue pct = event.value(QStringLiteral("pct"));
    if (!pct.isDouble()) {
        return -1;
    }
    return std::clamp(static_cast<int>(pct.toDouble() + 0.5), 0, 100);
}

QString ProgressText(const QJsonObject& event) {
    if (const QString transfer = TransferText(event); !transfer.isEmpty()) {
        return transfer;  // a download says its own words, speed and time left
    }
    QString line = TaskName(event.value(QStringLiteral("task")).toString());
    const int pct = ProgressPct(event);
    if (pct >= 0) {
        line += (line.isEmpty() ? QString() : QStringLiteral(": ")) + QString::number(pct) +
                QStringLiteral(" %");
    }
    const QString text = event.value(QStringLiteral("text")).toString().trimmed();
    if (!text.isEmpty()) {
        line += (line.isEmpty() ? QString() : QStringLiteral(" - ")) + text;
    }
    return line;
}

QStringList VersionLines(const Status& status, const QString& emulator_build,
                         const QString& own_protocol) {
    auto or_unknown = [](const QString& text) {
        return text.isEmpty() ? QStringLiteral("desconocida") : text;
    };
    QStringList lines;
    lines << QStringLiteral("Versión de la app: ") + or_unknown(status.app_version);
    lines << QStringLiteral("Versión del kit: ") + or_unknown(status.kit_version);
    QString protocol = own_protocol;
    if (!status.protocol.isEmpty() && status.protocol != own_protocol) {
        protocol += QStringLiteral(" (el ayudante espera %1: actualiza)").arg(status.protocol);
    }
    lines << QStringLiteral("Protocolo: ") + or_unknown(protocol);
    lines << QStringLiteral("Emulador: ") + or_unknown(emulator_build);
    QString server = or_unknown(status.channel_version);
    if (!status.channel_version.isEmpty() && !status.app_version.isEmpty()) {
        server += status.channel_version == status.app_version ? QStringLiteral(" (estás al día)")
                                                               : QStringLiteral(" (hay una nueva)");
    }
    lines << QStringLiteral("Última versión en el servidor: ") + server;
    return lines;
}

QString NotesText(const Status& status) {
    QString text;
    const bool newer = !status.channel_version.isEmpty() &&
                       status.channel_version != status.app_version;
    if (newer) {
        text += QStringLiteral("Versión nueva %1:\n").arg(status.channel_version);
        text += status.notes_available.isEmpty() ? QStringLiteral("(sin notas)")
                                                 : status.notes_available;
        text += QStringLiteral("\n\n");
    }
    text += QStringLiteral("Versión instalada %1:\n")
                .arg(status.app_version.isEmpty() ? QStringLiteral("desconocida")
                                                  : status.app_version);
    text += status.notes_installed.isEmpty() ? QStringLiteral("(sin notas)")
                                             : status.notes_installed;
    return text;
}

} // namespace OrmApp
