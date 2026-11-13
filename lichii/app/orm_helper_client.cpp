// SPDX-License-Identifier: GPL-3.0-or-later
#include "orm_helper_client.h"

#include <utility>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QTimer>

namespace OrmApp {

namespace {

// CREATE_NO_WINDOW (winbase.h), without windows.h in this file: the helper is
// a console program and must never open a console on the player's desktop.
constexpr unsigned long kCreateNoWindow = 0x08000000UL;

// A line as it goes into the log: one line, not too long.
QString Shown(const QByteArray& line) {
    QString text = QString::fromUtf8(line.left(300));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (line.size() > 300) {
        text += QStringLiteral("...");
    }
    return text;
}

} // namespace

HelperClient::HelperClient() : guard_(std::make_unique<QObject>()) {}

HelperClient::~HelperClient() {
    if (process_ != nullptr) {
        QObject::disconnect(process_, nullptr, nullptr, nullptr);
        if (process_->state() != QProcess::NotRunning) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
    guard_.reset();  // the process and every pending timer with it
    process_ = nullptr;
    // The pending callbacks are dropped, not called: whoever owns them is
    // being torn down too.
}

QJsonObject HelperClient::Failure(const QString& error) {
    QJsonObject reply;
    reply.insert(QStringLiteral("ok"), false);
    reply.insert(QStringLiteral("error"), error);
    return reply;
}

void HelperClient::SetWriter(Writer writer) {
    writer_ = std::move(writer);
}

bool HelperClient::Start(const QString& program, const QStringList& args, QString* error) {
    if (Running()) {
        return true;
    }
    if (process_ != nullptr) {
        QObject::disconnect(process_, nullptr, nullptr, nullptr);
        process_->deleteLater();
        process_ = nullptr;
    }
    auto* process = new QProcess(guard_.get());
    process->setProgram(program);
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* a) { a->flags |= kCreateNoWindow; });
#endif
    QObject::connect(process, &QProcess::readyReadStandardOutput, guard_.get(),
                     [this, process] { Feed(process->readAllStandardOutput()); });
    QObject::connect(process, &QProcess::readyReadStandardError, guard_.get(), [this, process] {
        stderr_buffer_ += process->readAllStandardError();
        qsizetype nl;
        while ((nl = stderr_buffer_.indexOf('\n')) >= 0) {
            QByteArray line = stderr_buffer_.left(nl);
            stderr_buffer_.remove(0, nl + 1);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            if (!line.isEmpty()) {
                Log(QStringLiteral("ayudante: ") + Shown(line));
            }
        }
        if (stderr_buffer_.size() > 64 * 1024) {
            stderr_buffer_.clear();
        }
    });
    QObject::connect(process, &QProcess::finished, guard_.get(),
                     [this, process](int code, QProcess::ExitStatus status) {
                         // What is still in the pipe comes first.
                         Feed(process->readAllStandardOutput());
                         const QString why =
                             status == QProcess::CrashExit
                                 ? QStringLiteral("el ayudante se cerró de golpe")
                                 : QStringLiteral("el ayudante se cerró (código %1)").arg(code);
                         FailAll(why);
                         if (on_exit_) {
                             on_exit_(why);
                         }
                     });
    process_ = process;
    writer_ = [this](const QByteArray& bytes) {
        return process_ != nullptr && process_->state() == QProcess::Running &&
               process_->write(bytes) == bytes.size();
    };
    process->start(QIODevice::ReadWrite);
    if (!process->waitForStarted(10000)) {
        const QString why = process->errorString();
        if (error != nullptr) {
            *error = why;
        }
        Log(QStringLiteral("no arranca el ayudante %1: %2").arg(program, why));
        QObject::disconnect(process, nullptr, nullptr, nullptr);
        process->deleteLater();
        process_ = nullptr;
        writer_ = nullptr;
        return false;
    }
    Log(QStringLiteral("ayudante en marcha: %1 %2").arg(program, args.join(QLatin1Char(' '))));
    return true;
}

void HelperClient::Stop(int wait_ms) {
    if (!Running()) {
        FailAll(QStringLiteral("el ayudante no está en marcha"));
        return;
    }
    Send(QStringLiteral("quit"), {}, [](const QJsonObject&) {}, 0);
    process_->closeWriteChannel();
    if (!process_->waitForFinished(wait_ms)) {
        Log(QStringLiteral("el ayudante no se cerró en %1 ms: se termina").arg(wait_ms));
        process_->kill();
        process_->waitForFinished(1000);
    }
    FailAll(QStringLiteral("el ayudante se cerró"));
}

bool HelperClient::Running() const {
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

int HelperClient::Send(const QString& cmd, const QJsonObject& fields, Reply reply, int timeout_ms) {
    const int id = next_id_++;
    QJsonObject request = fields;
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("cmd"), cmd);
    QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact);
    line.append('\n');
    pending_[id] = std::move(reply);
    if (!writer_ || !writer_(line)) {
        Finish(id, Failure(QStringLiteral("no se pudo hablar con el ayudante (%1)").arg(cmd)));
        return id;
    }
    if (timeout_ms > 0) {
        QTimer::singleShot(timeout_ms, guard_.get(), [this, id, cmd] {
            if (pending_.count(id) != 0) {
                Log(QStringLiteral("sin respuesta del ayudante a %1").arg(cmd));
                Finish(id, Failure(QStringLiteral("el ayudante no respondió a tiempo (%1)").arg(cmd)));
            }
        });
    }
    return id;
}

void HelperClient::Feed(const QByteArray& bytes) {
    qsizetype start = 0;
    while (start < bytes.size()) {
        const qsizetype nl = bytes.indexOf('\n', start);
        if (nl < 0) {
            if (!dropping_) {
                buffer_.append(bytes.mid(start));
                if (buffer_.size() > kMaxLine) {
                    Log(QStringLiteral("línea del ayudante demasiado larga: se descarta"));
                    buffer_.clear();
                    dropping_ = true;
                }
            }
            return;
        }
        if (dropping_) {
            dropping_ = false;  // the over-long line ends here
            buffer_.clear();
        } else {
            buffer_.append(bytes.mid(start, nl - start));
            QByteArray line = std::exchange(buffer_, QByteArray());
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            if (!line.trimmed().isEmpty()) {
                HandleLine(line);
            }
        }
        start = nl + 1;
    }
}

void HelperClient::FailAll(const QString& error) {
    // Moved out first: a callback may send a new request.
    auto pending = std::exchange(pending_, {});
    for (auto& entry : pending) {
        if (entry.second) {
            entry.second(Failure(error));
        }
    }
}

void HelperClient::HandleLine(const QByteArray& line) {
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        Log(QStringLiteral("línea del ayudante que no es JSON: ") + Shown(line));
        return;
    }
    const QJsonObject object = doc.object();
    const QJsonValue id = object.value(QStringLiteral("id"));
    if (id.isDouble()) {
        const int n = id.toInt(-1);
        if (pending_.count(n) == 0) {
            Log(QStringLiteral("respuesta del ayudante a un pedido desconocido: ") + Shown(line));
            return;
        }
        Finish(n, object);
        return;
    }
    const QString event = object.value(QStringLiteral("event")).toString();
    if (!event.isEmpty()) {
        if (event == QStringLiteral("log")) {
            Log(QStringLiteral("ayudante: ") + object.value(QStringLiteral("line")).toString());
        }
        if (on_event_) {
            on_event_(event, object);
        }
        return;
    }
    Log(QStringLiteral("línea del ayudante sin id ni evento: ") + Shown(line));
}

void HelperClient::Finish(int id, const QJsonObject& reply) {
    const auto it = pending_.find(id);
    if (it == pending_.end()) {
        return;
    }
    Reply callback = std::move(it->second);
    pending_.erase(it);
    if (callback) {
        callback(reply);
    }
}

void HelperClient::Log(const QString& line) const {
    if (on_log_) {
        on_log_(line);
    }
}

StatusPoller::StatusPoller(HelperClient& client, HelperClient::Reply on_status, CanPoll can_poll,
                           int timeout_ms)
    : client_(client), on_status_(std::move(on_status)), can_poll_(std::move(can_poll)),
      timeout_ms_(timeout_ms) {}

StatusPoller::~StatusPoller() {
    *alive_ = false;
}

bool StatusPoller::Tick() {
    if (*outstanding_ || (can_poll_ && !can_poll_())) {
        return false;
    }
    *outstanding_ = true;
    auto outstanding = outstanding_;
    auto alive = alive_;
    client_.Send(QStringLiteral("status"), {}, [this, outstanding, alive](const QJsonObject& reply) {
        *outstanding = false;
        if (*alive && on_status_) {
            on_status_(reply);
        }
    }, timeout_ms_);
    return true;
}

} // namespace OrmApp
