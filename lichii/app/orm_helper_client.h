// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM single app (oot-remake docs/APP_UNICA.md, piece C): the client of the
// helper `orm-launcher.exe --service`, which speaks one JSON object per line
// on its stdin and stdout.
//
//   request   {"id":N,"cmd":"<name>",...}
//   reply     {"id":N,"ok":true|false,"error":"...",...}
//   event     {"event":"<name>",...}          (no id: progress, toast, log)
//
// Qt Core only (no widgets), so the unit tests drive it without a window:
// either in memory (a writer function for what is sent, Feed() for what
// arrives) or with a real child process (Start()). Not a QObject and no
// Q_OBJECT: callbacks are std::function, so nothing here needs moc. One
// thread, the one that owns the client (the Qt main thread in the app).
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace OrmApp {

class HelperClient {
public:
    // The reply of one request. A request that gets no reply (the helper
    // closed, it timed out, it could not be sent) gets a made-up one with
    // "ok":false and a Spanish "error", so every callback runs exactly once.
    using Reply = std::function<void(const QJsonObject&)>;
    using Event = std::function<void(const QString& name, const QJsonObject&)>;
    using Line = std::function<void(const QString&)>;
    using Exit = std::function<void(const QString& why)>;
    using Writer = std::function<bool(const QByteArray&)>;

    // A line longer than this without a newline is dropped (a broken helper
    // must not grow the buffer without end). The longest real reply, `status`
    // with the release notes, is a few KB.
    static constexpr qsizetype kMaxLine = 4 * 1024 * 1024;

    HelperClient();
    ~HelperClient();
    HelperClient(const HelperClient&) = delete;
    HelperClient& operator=(const HelperClient&) = delete;

    // In-memory mode: what Send() writes goes to `writer` (false = could not
    // be sent: the request fails at once). Replaces the process' writer.
    void SetWriter(Writer writer);

    // Process mode: starts `program args` with no window (CREATE_NO_WINDOW on
    // Windows), its stdout feeds the client, its stderr lines go to the log
    // callback. False with `error` set when it does not start.
    bool Start(const QString& program, const QStringList& args, QString* error);
    // Sends `quit`, waits up to `wait_ms` for the helper to end, then kills
    // it. Pending requests fail. Safe when not started or already stopped.
    void Stop(int wait_ms = 3000);
    bool Running() const;

    // Sends {"id":N,"cmd":cmd, ...fields}. `timeout_ms` 0 waits forever (the
    // long tasks: puppet pack, shaders, downloads report with progress
    // events). The timeout needs the Qt event loop. Returns the id.
    int Send(const QString& cmd, const QJsonObject& fields, Reply reply, int timeout_ms = 0);

    // Bytes from the helper's stdout, in any chunks: lines are put together
    // here (LF or CRLF), each one parsed and dispatched.
    void Feed(const QByteArray& bytes);

    // Every pending request gets {"ok":false,"error":error}.
    void FailAll(const QString& error);

    void OnEvent(Event handler) { on_event_ = std::move(handler); }
    void OnLog(Line handler) { on_log_ = std::move(handler); }
    void OnExit(Exit handler) { on_exit_ = std::move(handler); }

    std::size_t Pending() const { return pending_.size(); }

    // {"ok":false,"error":error}: what a failed request's callback gets.
    static QJsonObject Failure(const QString& error);

private:
    void HandleLine(const QByteArray& line);
    void Finish(int id, const QJsonObject& reply);
    void Log(const QString& line) const;

    Writer writer_;
    QByteArray buffer_;
    bool dropping_ = false;  // inside an over-long line, until its newline
    int next_id_ = 1;
    std::map<int, Reply> pending_;
    Event on_event_;
    Line on_log_;
    Exit on_exit_;
    // The context of the timers and the process' connections: destroyed
    // first in ~HelperClient, so no callback runs into a dead client.
    std::unique_ptr<QObject> guard_;
    QProcess* process_ = nullptr;  // owned by guard_
    QByteArray stderr_buffer_;
};

// The screen's look at the folders every few seconds (files put there by hand
// turn ✓): one `status` at a time and only when `can_poll` says so. The
// helper carries out requests one after the other, so a poll must never pile
// up behind a long task or behind another poll.
class StatusPoller {
public:
    using CanPoll = std::function<bool()>;
    StatusPoller(HelperClient& client, HelperClient::Reply on_status, CanPoll can_poll,
                 int timeout_ms = 20000);
    ~StatusPoller();
    StatusPoller(const StatusPoller&) = delete;
    StatusPoller& operator=(const StatusPoller&) = delete;

    // Sends `status` unless one of ours is still out or `can_poll` is false;
    // says if it sent one.
    bool Tick();
    bool Outstanding() const { return *outstanding_; }

private:
    HelperClient& client_;
    HelperClient::Reply on_status_;
    CanPoll can_poll_;
    int timeout_ms_;
    // Shared with the reply callback: it may run after this poller is gone
    // (the client fails what is pending when it stops).
    std::shared_ptr<bool> outstanding_ = std::make_shared<bool>(false);
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace OrmApp
