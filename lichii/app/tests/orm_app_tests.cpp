// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests of the single app's helper client and status model
// (lichii/app/orm_helper_client.*, orm_app_model.*), Qt Core only: no widget,
// no window. The process tests run this same program as a fake helper
// (`orm_app_tests --fake-helper`), started with CREATE_NO_WINDOW like the
// real one, so they need no script interpreter.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include "orm_app_model.h"
#include "orm_helper_client.h"

using OrmApp::HelperClient;

namespace {

QJsonObject Obj(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

std::string S(const QString& text) {
    return text.toStdString();
}

bool WaitFor(const std::function<bool()>& done, int ms = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

// The fake helper: one JSON object per line on stdin, replies on stdout
// (text mode: CRLF on Windows, which the client must take), a note per
// request on stderr.
int FakeHelper() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject req = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const int id = req.value(QStringLiteral("id")).toInt();
        const std::string cmd = req.value(QStringLiteral("cmd")).toString().toStdString();
        std::cerr << "fake: got " << cmd << std::endl;
        QJsonObject reply{{QStringLiteral("id"), id}, {QStringLiteral("ok"), true}};
        if (cmd == "status") {
            reply.insert(QStringLiteral("version"), QStringLiteral("0.12.0"));
            reply.insert(QStringLiteral("keys"), true);
        } else if (cmd == "slow") {
            std::cout << R"({"event":"progress","task":"shaders","pct":50,"text":"a medias"})"
                      << std::endl;
        } else if (cmd == "split") {
            const std::string text =
                QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString();
            std::cout << text.substr(0, 5) << std::flush;
            QThread::msleep(100);
            std::cout << text.substr(5) << std::endl;
            continue;
        } else if (cmd == "import") {
            const QJsonArray paths = req.value(QStringLiteral("paths")).toArray();
            reply.insert(QStringLiteral("import_id"), QStringLiteral("imp-1"));
            reply.insert(QStringLiteral("imported"),
                         QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("keys")}}});
            reply.insert(QStringLiteral("originals"), paths);
            reply.insert(QStringLiteral("ask"),
                         QStringLiteral("¿Borrar los originales de %1?").arg(paths.size()));
        } else if (cmd == "import_finish") {
            const bool erase = req.value(QStringLiteral("delete")).toBool();
            reply.insert(QStringLiteral("deleted"), erase ? 1 : 0);
            reply.insert(QStringLiteral("summary"),
                         req.value(QStringLiteral("import_id")).toString() +
                             (erase ? QStringLiteral(" borrado") : QStringLiteral(" guardado")));
        } else if (cmd == "crash") {
            std::_Exit(3);
        } else if (cmd == "fail") {
            reply.insert(QStringLiteral("ok"), false);
            reply.insert(QStringLiteral("error"), QStringLiteral("no hay juego"));
        }
        std::cout << QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString()
                  << std::endl;
        if (cmd == "quit") {
            return 0;
        }
    }
    return 0;
}

// An in-memory client: what it sends is kept in `sent`.
struct Memory {
    HelperClient client;
    std::vector<QJsonObject> sent;
    std::vector<QString> log;
    std::vector<std::pair<QString, QJsonObject>> events;
    bool writable = true;

    Memory() {
        client.SetWriter([this](const QByteArray& bytes) {
            if (!writable) {
                return false;
            }
            REQUIRE(bytes.endsWith('\n'));
            REQUIRE(bytes.count('\n') == 1);
            sent.push_back(QJsonDocument::fromJson(bytes).object());
            return true;
        });
        client.OnLog([this](const QString& line) { log.push_back(line); });
        client.OnEvent([this](const QString& name, const QJsonObject& event) {
            events.emplace_back(name, event);
        });
    }
};

} // namespace

TEST_CASE("orm app client: a request is one JSON line with id, cmd and fields") {
    Memory m;
    const int a = m.client.Send(QStringLiteral("status"), {}, [](const QJsonObject&) {});
    const int b = m.client.Send(QStringLiteral("room"),
                                QJsonObject{{QStringLiteral("room"), QStringLiteral("sala1")},
                                            {QStringLiteral("name"), QStringLiteral("Ñandú")}},
                                [](const QJsonObject&) {});
    REQUIRE(m.sent.size() == 2);
    CHECK(a != b);
    CHECK(m.sent[0].value(QStringLiteral("id")).toInt() == a);
    CHECK(S(m.sent[0].value(QStringLiteral("cmd")).toString()) == "status");
    CHECK(S(m.sent[1].value(QStringLiteral("cmd")).toString()) == "room");
    CHECK(S(m.sent[1].value(QStringLiteral("name")).toString()) == "Ñandú");
    CHECK(m.client.Pending() == 2);
}

TEST_CASE("orm app client: replies in any chunks, CRLF, two in one chunk") {
    Memory m;
    std::vector<QJsonObject> got;
    const int a = m.client.Send(QStringLiteral("a"), {}, [&](const QJsonObject& r) { got.push_back(r); });
    const int b = m.client.Send(QStringLiteral("b"), {}, [&](const QJsonObject& r) { got.push_back(r); });
    const QByteArray both = QStringLiteral("{\"id\":%1,\"ok\":true,\"x\":1}\r\n{\"id\":%2,\"ok\":false,"
                                           "\"error\":\"mal\"}\n")
                                .arg(b)
                                .arg(a)
                                .toUtf8();
    m.client.Feed(both.left(7));
    CHECK(got.empty());
    m.client.Feed(both.mid(7, 20));
    m.client.Feed(both.mid(27));
    REQUIRE(got.size() == 2);
    CHECK(got[0].value(QStringLiteral("x")).toInt() == 1);  // b answered first
    CHECK(OrmApp::Ok(got[0]));
    CHECK_FALSE(OrmApp::Ok(got[1]));
    CHECK(S(OrmApp::ErrorText(got[1], QStringLiteral("otro"))) == "mal");
    CHECK(m.client.Pending() == 0);
}

TEST_CASE("orm app client: events go to the event callback, log events to the log too") {
    Memory m;
    m.client.Feed("{\"event\":\"progress\",\"task\":\"shaders\",\"pct\":40}\n"
                  "{\"event\":\"log\",\"line\":\"hola\"}\n");
    REQUIRE(m.events.size() == 2);
    CHECK(S(m.events[0].first) == "progress");
    CHECK(m.events[0].second.value(QStringLiteral("pct")).toInt() == 40);
    REQUIRE(m.log.size() == 1);
    CHECK(S(m.log[0]) == "ayudante: hola");
}

TEST_CASE("orm app client: bad lines are logged and change nothing") {
    Memory m;
    int calls = 0;
    m.client.Send(QStringLiteral("a"), {}, [&](const QJsonObject&) { ++calls; });
    m.client.Feed("no es json\n[1,2]\n{\"id\":999,\"ok\":true}\n{\"ok\":true}\n\n\r\n");
    CHECK(calls == 0);
    CHECK(m.client.Pending() == 1);
    CHECK(m.log.size() == 4);
    CHECK(m.events.empty());
}

TEST_CASE("orm app client: a request that cannot be written fails at once") {
    Memory m;
    m.writable = false;
    QJsonObject got;
    m.client.Send(QStringLiteral("status"), {}, [&](const QJsonObject& r) { got = r; });
    CHECK_FALSE(OrmApp::Ok(got));
    CHECK_FALSE(got.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(m.client.Pending() == 0);
}

TEST_CASE("orm app client: FailAll answers every pending request once") {
    Memory m;
    int calls = 0;
    for (int i = 0; i < 3; ++i) {
        m.client.Send(QStringLiteral("x"), {}, [&](const QJsonObject& r) {
            ++calls;
            CHECK(S(r.value(QStringLiteral("error")).toString()) == "se cerró");
        });
    }
    m.client.FailAll(QStringLiteral("se cerró"));
    m.client.FailAll(QStringLiteral("otra vez"));
    CHECK(calls == 3);
    CHECK(m.client.Pending() == 0);
}

TEST_CASE("orm app client: an over-long line is dropped, the next one is read") {
    Memory m;
    QJsonObject got;
    const int id = m.client.Send(QStringLiteral("a"), {}, [&](const QJsonObject& r) { got = r; });
    const QByteArray junk(HelperClient::kMaxLine / 4, 'x');
    for (int i = 0; i < 5; ++i) {
        m.client.Feed(junk);
    }
    m.client.Feed(QByteArray("tail\n") +
                  QStringLiteral("{\"id\":%1,\"ok\":true}\n").arg(id).toUtf8());
    CHECK(OrmApp::Ok(got));
    CHECK(m.log.size() == 1);
}

TEST_CASE("orm app client: a callback may send the next request") {
    Memory m;
    QJsonObject second;
    const int first = m.client.Send(QStringLiteral("room"), {}, [&](const QJsonObject&) {
        m.client.Send(QStringLiteral("play"), {}, [&](const QJsonObject& r) { second = r; });
    });
    m.client.Feed(QStringLiteral("{\"id\":%1,\"ok\":true}\n").arg(first).toUtf8());
    REQUIRE(m.sent.size() == 2);
    const int next = m.sent[1].value(QStringLiteral("id")).toInt();
    m.client.Feed(QStringLiteral("{\"id\":%1,\"ok\":true,\"game\":\"g\"}\n").arg(next).toUtf8());
    CHECK(S(second.value(QStringLiteral("game")).toString()) == "g");
}

TEST_CASE("orm app client: a timed-out request fails, its late reply is ignored") {
    Memory m;
    QJsonObject got;
    int calls = 0;
    const int id = m.client.Send(QStringLiteral("status"), {}, [&](const QJsonObject& r) {
        got = r;
        ++calls;
    }, 30);
    CHECK(WaitFor([&] { return calls > 0; }, 2000));
    CHECK_FALSE(OrmApp::Ok(got));
    m.client.Feed(QStringLiteral("{\"id\":%1,\"ok\":true}\n").arg(id).toUtf8());
    CHECK(calls == 1);
}

TEST_CASE("orm app model: status rows from booleans, objects and paths") {
    const OrmApp::Status s = OrmApp::ParseStatus(Obj(R"({
        "ok": true, "version": "0.12.0", "channel_version": "0.13.0", "kit_version": "0.11",
        "protocol": 109, "keys": true, "firmware": {"ok": false, "detail": "falta 20.1"},
        "game": "D:/juegos/oot", "puppet_pack": {"ok": true, "version": "3"},
        "shaders_version": "7", "paths": {"keys": "D:/claves"},
        "room": "sala1", "name": "Anju", "notes": ["- uno", "- dos"],
        "channel_notes": "- tres", "update_ready": true, "previous_version": "0.11.0"})"));
    CHECK(s.keys.known);
    CHECK(s.keys.ok);
    CHECK(S(s.keys.detail) == "D:/claves");  // its folder when it has no detail
    CHECK(s.firmware.known);
    CHECK_FALSE(s.firmware.ok);
    CHECK(S(s.firmware.detail) == "falta 20.1");
    CHECK(s.game.ok);
    CHECK(S(s.game.detail) == "D:/juegos/oot");
    CHECK(S(s.puppet.detail) == "3");
    CHECK_FALSE(s.shaders.known);
    CHECK(S(s.shaders.detail) == "versión 7");
    CHECK(S(s.protocol) == "109");
    CHECK(S(s.room) == "sala1");
    CHECK(S(s.name) == "Anju");
    CHECK(S(s.notes_installed) == "- uno\n- dos");
    CHECK(S(s.notes_available) == "- tres");
    CHECK(s.update_ready);
    CHECK(s.rollback_available);
    CHECK(S(OrmApp::Mark(s.keys)) == "✓");
    CHECK(S(OrmApp::Mark(s.firmware)) == "✗");
    CHECK(S(OrmApp::Mark(s.shaders)) == "…");
    CHECK(S(OrmApp::RowText(QStringLiteral("Firmware"), s.firmware)) == "Firmware: ✗ falta 20.1");
}

TEST_CASE("orm app model: the host wish for the toggle (ORM one host)") {
    CHECK(S(OrmApp::ParseStatus(Obj(R"({"ok":true,"host":"me"})")).host) == "me");
    CHECK(S(OrmApp::ParseStatus(Obj(R"({"ok":true,"host":"never"})")).host) == "never");
    CHECK(OrmApp::ParseStatus(Obj(R"({"ok":true})")).host.isEmpty());  // an older helper: no toggle
}

TEST_CASE("orm app model: an empty status leaves every row unknown") {
    const OrmApp::Status s = OrmApp::ParseStatus(Obj(R"({"ok":true})"));
    for (const OrmApp::Check* c : {&s.keys, &s.firmware, &s.game, &s.puppet, &s.shaders}) {
        CHECK_FALSE(c->known);
    }
    CHECK_FALSE(s.rollback_available);
    CHECK_FALSE(s.update_ready);
}

TEST_CASE("orm app model: play gives the game, the environment and full screen") {
    OrmApp::PlayInfo p = OrmApp::ParsePlay(Obj(R"({"ok":true,"args":["-f","-g","D:/oot.nsp"],
        "env":{"LICHII_COOP_HUD":"1","LICHII_COOP_PORT":47100,"BAD=NAME":"x"}})"));
    CHECK(S(p.game) == "D:/oot.nsp");
    CHECK(p.fullscreen);
    REQUIRE(p.env.size() == 2);
    CHECK(S(p.env[0].first) == "LICHII_COOP_HUD");
    CHECK(S(p.env[1].second) == "47100");
    p = OrmApp::ParsePlay(Obj(R"({"ok":true,"args":["D:/oot.nsp"]})"));
    CHECK(S(p.game) == "D:/oot.nsp");
    CHECK_FALSE(p.fullscreen);
    p = OrmApp::ParsePlay(Obj(R"({"ok":true,"game":"E:/a.xci","args":["-g","D:/b"]})"));
    CHECK(S(p.game) == "E:/a.xci");
    p = OrmApp::ParsePlay(Obj(R"({"ok":true,"args":["-f"]})"));
    CHECK(p.game.isEmpty());
}

TEST_CASE("orm app model: update, progress, versions and notes texts") {
    const OrmApp::UpdateInfo u = OrmApp::ParseUpdate(
        Obj(R"({"ok":true,"available":true,"version":"0.13.0","notes":"- arreglos"})"));
    CHECK(u.available);
    CHECK_FALSE(u.ready);
    CHECK(S(u.version) == "0.13.0");
    CHECK(S(u.notes) == "- arreglos");

    CHECK(OrmApp::ProgressPct(Obj(R"({"pct":140})")) == 100);
    CHECK(OrmApp::ProgressPct(Obj(R"({"task":"x"})")) == -1);
    CHECK(S(OrmApp::ProgressText(Obj(R"({"task":"prepare_shaders","pct":39.6,"text":"compilando"})"))) ==
          "Shaders: 40 % - compilando");
    CHECK(S(OrmApp::ProgressText(Obj(R"({"task":"puppet_pack"})"))) == "Paquete de marioneta");

    OrmApp::Status s;
    s.app_version = QStringLiteral("0.12.0");
    s.channel_version = QStringLiteral("0.13.0");
    s.kit_version = QStringLiteral("0.11");
    s.protocol = QStringLiteral("110");
    s.notes_installed = QStringLiteral("- viejo");
    s.notes_available = QStringLiteral("- nuevo");
    const QStringList lines = OrmApp::VersionLines(s, QStringLiteral("Fritchii 5.8.4"),
                                                   QStringLiteral("109"));
    REQUIRE(lines.size() == 5);
    CHECK(S(lines[0]) == "Versión de la app: 0.12.0");
    CHECK(S(lines[1]) == "Versión del kit: 0.11");
    CHECK(S(lines[2]) == "Protocolo: 109 (el ayudante espera 110: actualiza)");
    CHECK(S(lines[3]) == "Emulador: Fritchii 5.8.4");
    CHECK(S(lines[4]) == "Última versión en el servidor: 0.13.0 (hay una nueva)");
    CHECK(S(OrmApp::NotesText(s)) ==
          "Versión nueva 0.13.0:\n- nuevo\n\nVersión instalada 0.12.0:\n- viejo");
    s.channel_version = s.app_version;
    CHECK(S(OrmApp::NotesText(s)) == "Versión instalada 0.12.0:\n- viejo");
    CHECK(S(OrmApp::VersionLines(s, {}, QStringLiteral("110"))[4]) ==
          "Última versión en el servidor: 0.12.0 (estás al día)");
}

TEST_CASE("orm app model: a download's line with its speed and time left") {
    // The helper's transfer.go writes the same line; the two are checked with the same cases.
    CHECK(S(OrmApp::TransferText(Obj(
              R"({"task":"","pct":12,"label":"Juego desde Drive","note":"intento 1 de 5","bytes_done":3435973836,)"
              R"("bytes_total":27487790694,"speed_bps":50331648,"eta_s":480})"))) ==
          "Juego desde Drive: 3,2 de 25,6 GB · 48 MB/s · quedan 8 min (intento 1 de 5)");
    CHECK(S(OrmApp::ProgressText(Obj(
              R"({"task":"","label":"Juego","detail":"parte 1 ✓, parte 2 61 %, parte 3 desde tu servidor 40 %",)"
              R"("bytes_done":10,"bytes_total":20,"speed_bps":125829120,"eta_s":180})"))) ==
          "Juego: parte 1 ✓, parte 2 61 %, parte 3 desde tu servidor 40 % · 120 MB/s · quedan 3 min");
    CHECK(S(OrmApp::TransferText(Obj(
              R"({"label":"Claves","bytes_done":300,"bytes_total":1500,"speed_bps":0,"eta_s":-1})"))) ==
          "Claves: 1 de 2 KB · calculando…");
    CHECK(S(OrmApp::TransferText(Obj(R"({"label":"Versión 0.11.8","bytes_done":5,"bytes_total":5})"))) ==
          "Versión 0.11.8: 1 de 1 KB");
    CHECK(OrmApp::TransferText(Obj(R"({"task":"shaders","pct":40,"text":"x"})")).isEmpty());
    CHECK(S(OrmApp::SizePair(12LL << 20, 588LL * (1 << 20) / 10)) == "12,0 de 58,8 MB");
    CHECK(S(OrmApp::SpeedText(4.5 * (1 << 20))) == "4,5 MB/s");
    CHECK(S(OrmApp::SpeedText(350 * 1024)) == "350 KB/s");
    CHECK(S(OrmApp::SpeedText(500)) == "1 KB/s");
    CHECK(S(OrmApp::EtaText(59)) == "quedan 59 s");
    CHECK(S(OrmApp::EtaText(61)) == "quedan 2 min");
    CHECK(S(OrmApp::EtaText(3600)) == "quedan 1 h");
    CHECK(S(OrmApp::EtaText(3661)) == "quedan 1 h 2 min");
    CHECK(S(OrmApp::EtaText(7199)) == "quedan 2 h");
    CHECK(OrmApp::EtaText(-1).isEmpty());
    // "Reintentar" while the owner's files are still coming
    CHECK(OrmApp::ParseStatus(Obj(R"({"downloads":{"pending":true}})")).downloads_pending);
    CHECK_FALSE(OrmApp::ParseStatus(Obj(R"({})")).downloads_pending);
}

TEST_CASE("orm app model: row folders, the shortcut and the app folder") {
    const OrmApp::Status s = OrmApp::ParseStatus(Obj(R"({"ok":true,
        "keys": {"ok": false, "detail": "Faltan prod.keys y title.keys", "folder": "D:/App/emulator/user/keys"},
        "game": {"ok": true, "detail": "Ocarina of Time 1.1.1", "folder": "D:/App/juego"},
        "shortcut": {"ok": true, "path": "C:/Users/player/Desktop/OoT Coop.lnk", "detail": "C:/Users/player/Desktop/OoT Coop.lnk"},
        "app_dir": "D:/App"})"));
    CHECK(S(s.keys.folder) == "D:/App/emulator/user/keys");
    CHECK(S(s.game.folder) == "D:/App/juego");
    CHECK(s.firmware.folder.isEmpty());
    CHECK(s.shortcut.known);
    CHECK(s.shortcut.ok);
    CHECK(S(s.app_dir) == "D:/App");
    const OrmApp::Status old = OrmApp::ParseStatus(Obj(R"({"ok":true,"keys":true})"));
    CHECK_FALSE(old.shortcut.known);  // an older helper: the shortcut button stays hidden
    CHECK(old.keys.folder.isEmpty());
}

TEST_CASE("orm app model: dropped URLs become local paths") {
    const QStringList paths = OrmApp::DroppedPaths(
        {QStringLiteral("file:///D:/Juegos/Zelda%20%5B0400486024dec000%5D%23.nup"),
         QStringLiteral("https://example.com/x.nup"), QStringLiteral("file:///E:/claves/"),
         QStringLiteral("file:///d:/juegos/zelda%20%5B0400486024dec000%5D%23.nup"),
         QStringLiteral("file:///E:/Firmware%C3%B1")});
    REQUIRE(paths.size() == 3);
#ifdef _WIN32
    CHECK(S(paths[0]) == "D:\\Juegos\\Zelda [0400486024dec000]#.nup");
    CHECK(S(paths[1]) == "E:\\claves");
    CHECK(S(paths[2]) == "E:\\Firmwareñ");
#endif
    CHECK(OrmApp::DroppedPaths({QStringLiteral("mailto:a@b")}).isEmpty());
}

TEST_CASE("orm app model: import replies") {
    OrmApp::ImportInfo info = OrmApp::ParseImport(Obj(R"({"ok":true,"import_id":7,
        "imported":[{"kind":"keys"},{"kind":"game"}],"skipped":[{"path":"x","why":"?"}],
        "ask":" ¿Borrar los originales de prod.keys? ","summary":"2 archivos colocados"})"));
    CHECK(info.id.toInt() == 7);
    CHECK(info.imported == 2);
    CHECK(info.skipped == 1);
    CHECK(S(info.ask) == "¿Borrar los originales de prod.keys?");
    CHECK(S(info.summary) == "2 archivos colocados");
    info = OrmApp::ParseImport(Obj(R"({"ok":true,"ask":"¿Borrar?"})"));
    CHECK(info.ask.isEmpty());  // no id: nothing to finish, nothing to ask
    CHECK(S(OrmApp::ProgressText(Obj(R"({"task":"import","pct":50,"text":"juego.nup"})"))) ==
          "Archivos: 50 % - juego.nup");
}

TEST_CASE("orm app model: Preparar on its own and the update toast") {
    OrmApp::Status s;
    for (OrmApp::Check* c : {&s.keys, &s.firmware, &s.game}) {
        c->known = c->ok = true;
    }
    CHECK(OrmApp::ShouldAutoPrepare(s));  // puppet pack and shaders unknown
    s.puppet.known = s.puppet.ok = true;
    s.shaders.known = true;
    CHECK(OrmApp::ShouldAutoPrepare(s));  // shaders not ready
    s.shaders.ok = true;
    CHECK_FALSE(OrmApp::ShouldAutoPrepare(s));  // all done
    s.shaders.ok = false;
    s.game.ok = false;
    CHECK_FALSE(OrmApp::ShouldAutoPrepare(s));  // the game is missing

    CHECK(OrmApp::IsUpdateToast(Obj(R"({"text":"Hay una versión nueva","version":"0.12"})")));
    CHECK(OrmApp::IsUpdateToast(Obj(R"({"text":"Reinicia","restart":true})")));
    CHECK_FALSE(OrmApp::IsUpdateToast(Obj(R"({"text":"Actualizada a la 0.12.","restart":false})")));
}

TEST_CASE("orm app model: the helper's room_saved shows Guardado, a save kept aside does not") {
    const QString saved = QStringLiteral("room_saved");
    CHECK(OrmApp::ShowsSaved(saved, Obj(R"({"event":"room_saved","stored":"main","room":"sala1"})")));
    CHECK(OrmApp::ShowsSaved(saved, Obj(R"({"event":"room_saved"})")));  // a channel of v1
    CHECK_FALSE(OrmApp::ShowsSaved(saved, Obj(R"({"event":"room_saved","stored":"alt"})")));
    CHECK_FALSE(OrmApp::ShowsSaved(QStringLiteral("toast"), Obj(R"({"stored":"main"})")));
    CHECK_FALSE(OrmApp::ShowsSaved(QStringLiteral("progress"), Obj(R"({"task":"play"})")));
}

TEST_CASE("orm app poller: one status at a time, only when allowed") {
    Memory m;
    bool allowed = true;
    std::vector<QJsonObject> got;
    OrmApp::StatusPoller poller(m.client, [&](const QJsonObject& r) { got.push_back(r); },
                                [&] { return allowed; }, 0);
    CHECK(poller.Tick());
    CHECK_FALSE(poller.Tick());  // the first one is still out
    CHECK_FALSE(poller.Tick());
    REQUIRE(m.sent.size() == 1);
    CHECK(S(m.sent[0].value(QStringLiteral("cmd")).toString()) == "status");
    CHECK(poller.Outstanding());
    const int id = m.sent[0].value(QStringLiteral("id")).toInt();
    m.client.Feed(QStringLiteral("{\"id\":%1,\"ok\":true,\"keys\":true}\n").arg(id).toUtf8());
    REQUIRE(got.size() == 1);
    CHECK_FALSE(poller.Outstanding());
    allowed = false;  // busy (Preparar, a download, the game)
    CHECK_FALSE(poller.Tick());
    CHECK(m.sent.size() == 1);
    allowed = true;
    CHECK(poller.Tick());
    m.client.FailAll(QStringLiteral("se cerró"));  // a failed poll frees the next one too
    CHECK(got.size() == 2);
    CHECK_FALSE(poller.Outstanding());
    CHECK(poller.Tick());
    CHECK(m.sent.size() == 3);
}

TEST_CASE("orm app poller: a reply after the poller is gone is dropped") {
    Memory m;
    int calls = 0;
    {
        OrmApp::StatusPoller poller(m.client, [&](const QJsonObject&) { ++calls; }, nullptr, 0);
        CHECK(poller.Tick());
    }
    m.client.FailAll(QStringLiteral("fin"));
    CHECK(calls == 0);
}

TEST_CASE("orm app client: import then import_finish with a real helper process") {
    HelperClient client;
    QString error;
    REQUIRE(client.Start(QCoreApplication::applicationFilePath(),
                         {QStringLiteral("--fake-helper")}, &error));
    QJsonObject imported, finished;
    client.Send(QStringLiteral("import"),
                QJsonObject{{QStringLiteral("paths"),
                             QJsonArray{QStringLiteral("D:\\Descargas\\prod.keys"),
                                        QStringLiteral("D:\\Descargas\\title.keys")}}},
                [&](const QJsonObject& r) { imported = r; });
    REQUIRE(WaitFor([&] { return !imported.isEmpty(); }));
    REQUIRE(OrmApp::Ok(imported));
    const OrmApp::ImportInfo info = OrmApp::ParseImport(imported);
    CHECK(S(info.ask) == "¿Borrar los originales de 2?");
    client.Send(QStringLiteral("import_finish"),
                QJsonObject{{QStringLiteral("import_id"), info.id}, {QStringLiteral("delete"), true}},
                [&](const QJsonObject& r) { finished = r; }, 10000);
    REQUIRE(WaitFor([&] { return !finished.isEmpty(); }));
    CHECK(S(finished.value(QStringLiteral("summary")).toString()) == "imp-1 borrado");
    client.Stop(5000);
}

TEST_CASE("orm app client: a missing helper does not start") {
    HelperClient client;
    QString error;
    CHECK_FALSE(client.Start(QStringLiteral("Z:/no/such/orm-launcher.exe"),
                             {QStringLiteral("--service")}, &error));
    CHECK_FALSE(error.isEmpty());
    CHECK_FALSE(client.Running());
    QJsonObject got;
    client.Send(QStringLiteral("status"), {}, [&](const QJsonObject& r) { got = r; });
    CHECK_FALSE(OrmApp::Ok(got));
}

TEST_CASE("orm app client: a real helper process, replies, events, stderr, quit") {
    HelperClient client;
    std::vector<QString> log;
    std::vector<QString> events;
    QString exit_why;
    client.OnLog([&](const QString& line) { log.push_back(line); });
    client.OnEvent([&](const QString& name, const QJsonObject&) { events.push_back(name); });
    client.OnExit([&](const QString& why) { exit_why = why; });
    QString error;
    REQUIRE(client.Start(QCoreApplication::applicationFilePath(),
                         {QStringLiteral("--fake-helper")}, &error));
    CHECK(client.Running());

    QJsonObject status, split, slow, fail;
    client.Send(QStringLiteral("status"), {}, [&](const QJsonObject& r) { status = r; }, 10000);
    client.Send(QStringLiteral("split"), {}, [&](const QJsonObject& r) { split = r; }, 10000);
    client.Send(QStringLiteral("slow"), {}, [&](const QJsonObject& r) { slow = r; }, 10000);
    client.Send(QStringLiteral("fail"), {}, [&](const QJsonObject& r) { fail = r; }, 10000);
    REQUIRE(WaitFor([&] { return !fail.isEmpty(); }));
    CHECK(S(OrmApp::ParseStatus(status).app_version) == "0.12.0");
    CHECK(OrmApp::Ok(split));
    CHECK(OrmApp::Ok(slow));
    REQUIRE(events.size() == 1);
    CHECK(S(events[0]) == "progress");
    CHECK(S(OrmApp::ErrorText(fail, {})) == "no hay juego");
    CHECK(WaitFor([&] {
        int notes = 0;
        for (const QString& line : log) {
            notes += line.startsWith(QStringLiteral("ayudante: fake: got")) ? 1 : 0;
        }
        return notes >= 4;
    }));

    client.Stop(5000);
    CHECK_FALSE(client.Running());
    CHECK(client.Pending() == 0);
}

TEST_CASE("orm app client: a helper that dies fails what is pending") {
    HelperClient client;
    QString exit_why;
    client.OnExit([&](const QString& why) { exit_why = why; });
    QString error;
    REQUIRE(client.Start(QCoreApplication::applicationFilePath(),
                         {QStringLiteral("--fake-helper")}, &error));
    QJsonObject crash, after;
    client.Send(QStringLiteral("crash"), {}, [&](const QJsonObject& r) { crash = r; });
    REQUIRE(WaitFor([&] { return !crash.isEmpty(); }));
    CHECK_FALSE(OrmApp::Ok(crash));
    CHECK(WaitFor([&] { return !exit_why.isEmpty(); }));
    CHECK_FALSE(client.Running());
    client.Send(QStringLiteral("status"), {}, [&](const QJsonObject& r) { after = r; });
    CHECK_FALSE(OrmApp::Ok(after));
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--fake-helper") {
            return FakeHelper();
        }
    }
    QCoreApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
