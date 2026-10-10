/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "appcontroller.h"
#include "gitservice.h"
#include "listmodel.h"
#include "maintenance.h"
#include "richtext.h"
#include "rpcclient.h"
#include "settings.h"
#include <KConfigGroup>
#include <KLocalizedString>
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void initTestCase() { KLocalizedString::setApplicationDomain("kodex"); }
    void conversation() {
        QTemporaryDir dir;
        qputenv("KODEX_FIXTURE_DIR", dir.path().toUtf8());
        const auto log = dir.filePath("requests.jsonl");
        qputenv("KODEX_RECORD_REQUESTS", log.toUtf8());
        Settings settings(nullptr, dir.filePath("settings"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        QSignalSpy errors(&controller, &AppController::error);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.projects()->count(), 1, 10000);
        QTRY_COMPARE(controller.threads()->count(), 1);
        controller.selectThread("t1");
        QTRY_COMPARE(controller.messages()->count(), 2);
        QCOMPARE(controller.messages()->get(0)["id"].toString(), "old-user");
        controller.setOption("plan", true);
        controller.setOption("permission", "auto");
        controller.setOption("fast", true);
        QVERIFY(controller.send("Test message"));
        QTRY_COMPARE(controller.messages()->count(), 4);
        QCOMPARE(controller.messages()->get(3)["text"].toString(), "Streamed response");
        QTRY_COMPARE(controller.requests()->count(), 1);
        QCOMPARE(controller.state()["contextPercent"].toDouble(), 10.);
        controller.answer("987", "accept", {{"q1", "Yes"}});
        QCOMPARE(controller.requests()->count(), 0);
        rpc.ingest({{"id", "approval"},
                    {"method", "item/fileChange/requestApproval"},
                    {"params", QJsonObject{{"threadId", "t1"}}}});
        QCOMPARE(controller.requests()->count(), 1);
        rpc.ingest(
            {{"method", "serverRequest/resolved"}, {"params", QJsonObject{{"requestId", "approval"}}}});
        QCOMPARE(controller.requests()->count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(log).size() > 1000, 3000);
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        bool sawTurn = false;
        for (const auto &line : file.readAll().split('\n')) {
            auto obj = QJsonDocument::fromJson(line).object();
            if (obj["method"] == "turn/start") {
                sawTurn = true;
                auto p = obj["params"].toObject();
                QCOMPARE(p["collaborationMode"].toObject()["mode"].toString(), "plan");
                QCOMPARE(p["approvalsReviewer"].toString(), "auto_review");
                QCOMPARE(p["serviceTier"].toString(), "fast");
            }
        }
        QVERIFY(sawTurn);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        rpc.ingest({{"method", "item/started"},
                    {"params", QJsonObject{{"threadId", "t1"},
                                           {"item", QJsonObject{{"id", "interrupted-command"},
                                                                {"type", "commandExecution"},
                                                                {"status", "inProgress"},
                                                                {"command", "sleep 30"}}}}}});
        rpc.ingest({{"method", "turn/completed"},
                    {"params", QJsonObject{{"threadId", "t1"},
                                           {"turn", QJsonObject{{"id", "interrupt-turn"},
                                                                {"status", "interrupted"}}}}}});
        QCOMPARE(controller.messages()->get(4)["status"].toString(), "interrupted");
        qunsetenv("KODEX_RECORD_REQUESTS");
        qunsetenv("KODEX_FIXTURE_DIR");
    }
    void newThreadWithoutRollout() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        QSignalSpy errors(&controller, &AppController::error);
        rpc.connectServer();
        QTRY_COMPARE(controller.threads()->count(), 1);
        controller.newThread();
        QTRY_COMPARE(controller.state()["threadId"].toString(), "fresh-thread");
        QTRY_VERIFY(!controller.state()["loading"].toBool());
        QTest::qWait(50);
        QVERIFY(errors.isEmpty());
        QCOMPARE(controller.messages()->count(), 0);
        controller.selectThread("t1");
        QTRY_COMPARE(controller.messages()->count(), 2);
        controller.selectThread("fresh-thread");
        QVERIFY(!controller.state()["loading"].toBool());
        QCOMPARE(controller.messages()->count(), 0);
        QVERIFY(errors.isEmpty());
        controller.loadThreads();
        QTRY_COMPARE(controller.threads()->count(), 2);
    }
    void unassignedSessions() {
        QTemporaryDir dir, loose;
        qputenv("KODEX_FIXTURE_DIR", dir.path().toUtf8());
        qputenv("KODEX_LOOSE_DIR", loose.path().toUtf8());
        const auto log = dir.filePath("requests.jsonl");
        qputenv("KODEX_RECORD_REQUESTS", log.toUtf8());
        auto cleanup = qScopeGuard([] {
            qunsetenv("KODEX_LOOSE_DIR");
            qunsetenv("KODEX_RECORD_REQUESTS");
        });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        QSignalSpy errors(&controller, &AppController::error);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.state()["projectId"].toString(), "p1", 10000);
        QTRY_COMPARE(controller.threads()->get(0)["id"].toString(), "t1");
        controller.showUnassigned();
        QVERIFY(controller.state()["unassigned"].toBool());
        QVERIFY(controller.state()["projectId"].toString().isEmpty());
        QVERIFY(controller.state()["cwd"].toString().isEmpty());
        QTRY_COMPARE(controller.threads()->get(0)["id"].toString(), "loose");
        controller.selectThread("loose");
        QTRY_COMPARE(controller.messages()->count(), 2);
        // The session's folder drives Git; the list keeps showing every unassigned session.
        QCOMPARE(controller.state()["cwd"].toString(), loose.path());
        QTest::qWait(100);
        QVERIFY(controller.state()["unassigned"].toBool());
        QVERIFY(controller.state()["projectId"].toString().isEmpty());
        QCOMPARE(controller.threads()->count(), 1);
        QCOMPARE(controller.threads()->get(0)["id"].toString(), "loose");
        controller.newThread();
        QTRY_COMPARE(controller.state()["threadId"].toString(), "fresh-thread");
        QTRY_VERIFY(!controller.state()["loading"].toBool());
        QCOMPARE(controller.threads()->get(0)["id"].toString(), "fresh-thread");
        QVERIFY(errors.isEmpty());
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject listParams, startParams;
        for (const auto &line : file.readAll().split('\n')) {
            const auto request = QJsonDocument::fromJson(line).object();
            const auto params = request["params"].toObject();
            if (request["method"] == "thread/list" && params.contains("projectId"))
                listParams = params;
            if (request["method"] == "thread/start")
                startParams = params;
        }
        QVERIFY(listParams["projectId"].isNull());
        QVERIFY(!listParams.contains("cwd"));
        QCOMPARE(listParams["sortKey"].toString(), "updated_at");
        QCOMPARE(startParams["cwd"].toString(), loose.path());
        QVERIFY(!startParams.contains("projectId"));
        controller.selectProject("p1");
        QVERIFY(!controller.state()["unassigned"].toBool());
        QTRY_COMPARE(controller.threads()->get(0)["id"].toString(), "t1");
    }
    void resumeCommandShowsAllSessions() {
        QTemporaryDir dir, loose;
        qputenv("KODEX_FIXTURE_DIR", dir.path().toUtf8());
        qputenv("KODEX_LOOSE_DIR", loose.path().toUtf8());
        auto cleanup = qScopeGuard([] { qunsetenv("KODEX_LOOSE_DIR"); });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.state()["projectId"].toString(), "p1", 10000);
        QVERIFY(controller.send("/resume"));
        QVERIFY(controller.state()["unassigned"].toBool());
        QTRY_COMPARE(controller.threads()->get(0)["id"].toString(), "loose");
    }
    void withoutProjectsShowsUnassigned() {
        QTemporaryDir dir, loose;
        qputenv("KODEX_LOOSE_DIR", loose.path().toUtf8());
        qputenv("KODEX_NO_PROJECTS", "1");
        auto cleanup = qScopeGuard([] {
            qunsetenv("KODEX_LOOSE_DIR");
            qunsetenv("KODEX_NO_PROJECTS");
        });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        rpc.connectServer();
        QTRY_VERIFY_WITH_TIMEOUT(controller.state()["unassigned"].toBool(), 10000);
        QCOMPARE(controller.projects()->count(), 0);
        QTRY_COMPARE(controller.threads()->count(), 1);
        QCOMPARE(controller.threads()->get(0)["cwd"].toString(), loose.path());
        QCOMPARE(settings.draft("last-project"), "unassigned");
    }
    void attachmentsAreSentWithTheMessage() {
        QTemporaryDir dir;
        qputenv("KODEX_FIXTURE_DIR", dir.path().toUtf8());
        const auto log = dir.filePath("requests.jsonl");
        qputenv("KODEX_RECORD_REQUESTS", log.toUtf8());
        auto cleanup = qScopeGuard([] { qunsetenv("KODEX_RECORD_REQUESTS"); });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        QSignalSpy errors(&controller, &AppController::error);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.threads()->count(), 1, 10000);
        controller.selectThread("t1");
        QTRY_COMPARE(controller.messages()->count(), 2);
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::red);
        const auto png = dir.filePath("shot.png");
        QVERIFY(image.save(png));
        QFile note(dir.filePath("notes.txt"));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("notes");
        note.close();
        QVERIFY(QDir().mkpath(dir.filePath("folder")));
        controller.attachFiles({QUrl::fromLocalFile(png), QUrl::fromLocalFile(note.fileName()),
                                QUrl::fromLocalFile(dir.filePath("folder")), QUrl::fromLocalFile(png),
                                QUrl::fromLocalFile(dir.filePath("missing"))});
        // Duplicates and missing files are skipped.
        QCOMPARE(controller.attachments()->count(), 3);
        QCOMPARE(controller.attachments()->get(0)["kind"].toString(), "image");
        QCOMPARE(controller.attachments()->get(1)["kind"].toString(), "file");
        QCOMPARE(controller.attachments()->get(2)["kind"].toString(), "folder");
        // They wait with their thread while another one is open.
        controller.newThread();
        QTRY_COMPARE(controller.state()["threadId"].toString(), "fresh-thread");
        QCOMPARE(controller.attachments()->count(), 0);
        QTRY_VERIFY(!controller.state()["loading"].toBool());
        controller.selectThread("t1");
        QCOMPARE(controller.attachments()->count(), 3);
        QTRY_VERIFY(!controller.state()["loading"].toBool());
        controller.removeAttachment(controller.attachments()->get(2)["id"].toString());
        QVERIFY(controller.send("Please review"));
        QTRY_COMPARE(controller.attachments()->count(), 0);
        QVERIFY(errors.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(log).size() > 1000, 3000);
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonArray input;
        for (const auto &line : file.readAll().split('\n')) {
            const auto request = QJsonDocument::fromJson(line).object();
            if (request["method"] == "turn/start")
                input = request["params"].toObject()["input"].toArray();
        }
        QCOMPARE(input.size(), 2);
        QCOMPARE(input[0].toObject()["type"].toString(), "text");
        QCOMPARE(input[0].toObject()["text"].toString(),
                 "Please review\n\nAttached file:\n- " + QFileInfo(note.fileName()).canonicalFilePath());
        QCOMPARE(input[1].toObject()["type"].toString(), "localImage");
        QCOMPARE(input[1].toObject()["path"].toString(), QFileInfo(png).canonicalFilePath());
    }
    void pastedAttachments() {
        QTemporaryDir dir;
        qputenv("KODEX_FIXTURE_DIR", dir.path().toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        auto cleanup = qScopeGuard([] { qunsetenv("XDG_DATA_HOME"); });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.threads()->count(), 1, 10000);
        QFile note(dir.filePath("notes.txt"));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("notes");
        note.close();
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(note.fileName())});
        QVERIFY(!controller.attachFromMime(&files)); // no task is open yet
        controller.selectThread("t1");
        QTRY_COMPARE(controller.messages()->count(), 2);
        QVERIFY(controller.attachFromMime(&files));
        QCOMPARE(controller.attachments()->count(), 1);
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QMimeData picture;
        picture.setImageData(image);
        QVERIFY(controller.attachFromMime(&picture));
        QCOMPARE(controller.attachments()->count(), 2);
        const auto pasted = controller.attachments()->get(1);
        QCOMPARE(pasted["kind"].toString(), "image");
        QVERIFY(pasted["path"].toString().startsWith(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attachments/"));
        QCOMPARE(QImage(pasted["path"].toString()).size(), image.size());
        // A copied selection with text and a rendered image, plain text, and web links stay ordinary pastes.
        QMimeData both;
        both.setImageData(image);
        both.setText("cells");
        QVERIFY(!controller.attachFromMime(&both));
        QMimeData text;
        text.setText("plain");
        QVERIFY(!controller.attachFromMime(&text));
        QMimeData web;
        web.setUrls({QUrl("https://example.test/a.png")});
        QVERIFY(!controller.attachFromMime(&web));
        QCOMPARE(controller.attachments()->count(), 2);
    }
    void imagesInChat() {
        QTemporaryDir dir;
        qputenv("XDG_CACHE_HOME", dir.filePath("cache").toUtf8());
        auto cleanup = qScopeGuard([] { qunsetenv("XDG_CACHE_HOME"); });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController controller(&rpc, &settings, &git);
        rpc.connectServer();
        QTRY_COMPARE_WITH_TIMEOUT(controller.threads()->count(), 1, 10000);
        controller.selectThread("t1");
        QTRY_COMPARE(controller.messages()->count(), 2);
        auto show = [&](const QJsonObject &item) {
            rpc.ingest(
                {{"method", "item/completed"}, {"params", QJsonObject{{"threadId", "t1"}, {"item", item}}}});
            return controller.messages()->get(controller.messages()->count() - 1);
        };
        auto sources = [](const QVariantMap &record) {
            QStringList result;
            for (const auto &image : record["images"].toList())
                result << image.toMap()["src"].toString();
            return result;
        };
        auto user = show(
            {{"id", "u"},
             {"type", "userMessage"},
             {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "see"}},
                                    QJsonObject{{"type", "localImage"}, {"path", "/tmp/a b.png"}},
                                    QJsonObject{{"type", "image"}, {"url", "data:image/png;base64,AAAA"}},
                                    QJsonObject{{"type", "image"}, {"url", "https://example.test/x.png"}}}}});
        QCOMPARE(user["text"].toString(), "see");
        // Embedded pictures are stored as files, so the rows never hold their bytes.
        const auto stored = sources(user);
        QCOMPARE(stored.size(), 2);
        QCOMPARE(stored[0], "file:///tmp/a%20b.png");
        QVERIFY(stored[1].startsWith("file://") && stored[1].endsWith(".png"));
        QFile embedded(QUrl(stored[1]).toLocalFile());
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        QCOMPARE(embedded.readAll(), QByteArray::fromBase64("AAAA"));
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(user)).toJson().contains("base64"));
        QCOMPARE(user["images"].toList()[0].toMap()["name"].toString(), "a b.png");
        auto view = show({{"id", "v"}, {"type", "imageView"}, {"path", "/tmp/shots/one.png"}});
        QCOMPARE(view["text"].toString(), "/tmp/shots/one.png");
        QCOMPARE(sources(view), QStringList{"file:///tmp/shots/one.png"});
        auto generated = show({{"id", "g"},
                               {"type", "imageGeneration"},
                               {"status", "completed"},
                               {"result", ""},
                               {"revisedPrompt", "a red card"},
                               {"savedPath", "/tmp/shots/card.png"}});
        QCOMPARE(generated["text"].toString(), "a red card");
        QCOMPARE(sources(generated), QStringList{"file:///tmp/shots/card.png"});
        QVERIFY(
            sources(show({{"id", "g2"}, {"type", "imageGeneration"}, {"status", "failed"}, {"result", ""}}))
                .isEmpty());
        auto mcp =
            show({{"id", "m"},
                  {"type", "mcpToolCall"},
                  {"server", "screens"},
                  {"tool", "capture"},
                  {"result",
                   QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "captured"}},
                                                      QJsonObject{{"type", "image"},
                                                                  {"mimeType", "image/png"},
                                                                  {"data", "QUJDREVGRw=="}}}}}}});
        QCOMPARE(sources(mcp).size(), 1);
        QVERIFY(sources(mcp)[0].startsWith("file://"));
        QFile captured(QUrl(sources(mcp)[0]).toLocalFile());
        QVERIFY(captured.open(QIODevice::ReadOnly));
        QCOMPARE(captured.readAll(), QByteArray("ABCDEFG"));
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(mcp)).toJson().contains("QUJDREVGRw"));
        QVERIFY(!mcp["text"].toString().contains("QUJDREVGRw"));
        QVERIFY(mcp["text"].toString().contains("captured"));
        auto dynamic = show(
            {{"id", "d"},
             {"type", "dynamicToolCall"},
             {"tool", "preview"},
             {"contentItems",
              QJsonArray{QJsonObject{{"type", "inputText"}, {"text", "hello"}},
                         QJsonObject{{"type", "inputImage"}, {"imageUrl", "data:image/png;base64,AAAA"}}}}});
        QCOMPARE(dynamic["text"].toString(), "preview\nhello");
        QCOMPARE(sources(dynamic), QStringList{stored[1]}); // the same bytes are stored only once
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(dynamic)).toJson().contains("base64"));
    }
    void archivedHistoryIsReadOnly() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController app(&rpc, &settings, &git);
        QSignalSpy errors(&app, &AppController::error);
        rpc.connectServer();
        QTRY_COMPARE(app.threads()->count(), 1);
        app.selectThread("t1");
        QTRY_COMPARE(app.messages()->count(), 2);
        app.archiveThread();
        QTRY_COMPARE(app.threads()->count(), 0);
        app.loadThreads(true);
        QTRY_COMPARE(app.threads()->count(), 1);
        app.selectThread("t1");
        QTRY_COMPARE(app.messages()->count(), 2);
        QVERIFY(!app.send("Must not send to archive"));
        QVERIFY(errors.isEmpty());
        app.archiveThread(false);
        QTRY_COMPARE(app.threads()->count(), 0);
        app.loadThreads(false);
        QTRY_COMPARE(app.threads()->count(), 1);
        app.selectThread("t1");
        QTRY_COMPARE(app.messages()->count(), 2);
        QVERIFY(errors.isEmpty());
    }
    void gitOperations() {
        QTemporaryDir temp;
        auto gitRun = [](const QString &dir, const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(dir);
            p.start("git", args);
            if (!p.waitForFinished(10000))
                return -1;
            return p.exitCode();
        };
        auto root = temp.filePath("local"), bare = temp.filePath("remote.git"),
             other = temp.filePath("other");
        QDir().mkpath(root);
        QCOMPARE(gitRun(root, {"init", "-b", "main"}), 0);
        QCOMPARE(gitRun(root, {"config", "user.name", "Fixture"}), 0);
        QCOMPARE(gitRun(root, {"config", "user.email", "fixture@example.test"}), 0);
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            return f.write(data) == data.size();
        };
        QVERIFY(write(root + "/hello world.txt", "one\n"));
        GitService service;
        QSignalSpy errors(&service, &GitService::error);
        service.setDirectory(root);
        QTRY_VERIFY(service.state()["repository"].toBool());
        QTRY_COMPARE(service.files()->count(), 1);
        service.stage("hello world.txt", false);
        QTRY_VERIFY(!service.state()["busy"].toBool());
        QTRY_VERIFY(service.files()->get(0)["staged"].toBool());
        service.commit("Initial fixture");
        QTRY_VERIFY(!service.state()["busy"].toBool());
        QTRY_COMPARE(service.files()->count(), 0);
        QCOMPARE(gitRun(temp.path(), {"init", "--bare", bare}), 0);
        QCOMPARE(gitRun(root, {"remote", "add", "origin", bare}), 0);
        QCOMPARE(gitRun(root, {"push", "-u", "origin", "main"}), 0);
        QCOMPARE(gitRun(temp.path(), {"clone", "-b", "main", bare, other}), 0);
        QCOMPARE(gitRun(other, {"config", "user.name", "Fixture"}), 0);
        QCOMPARE(gitRun(other, {"config", "user.email", "fixture@example.test"}), 0);
        QVERIFY(write(other + "/remote.txt", "remote\n"));
        QCOMPARE(gitRun(other, {"add", "."}), 0);
        QCOMPARE(gitRun(other, {"commit", "-m", "Remote change"}), 0);
        QCOMPARE(gitRun(other, {"push"}), 0);
        service.refresh();
        QTRY_VERIFY(service.state()["upstream"].toBool());
        service.sync();
        QTRY_VERIFY(!service.state()["busy"].toBool());
        QVERIFY(QFileInfo::exists(root + "/remote.txt"));
        QVERIFY(errors.isEmpty());
        QVERIFY(write(root + "/dirty.txt", "dirty\n"));
        service.sync();
        QTRY_VERIFY(!service.state()["busy"].toBool());
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.last().first().toString().contains("working tree"));
        QCOMPARE(gitRun(root, {"add", "."}), 0);
        QCOMPARE(gitRun(root, {"commit", "-m", "Local divergence"}), 0);
        QVERIFY(write(other + "/another.txt", "another\n"));
        QCOMPARE(gitRun(other, {"add", "."}), 0);
        QCOMPARE(gitRun(other, {"commit", "-m", "Remote divergence"}), 0);
        QCOMPARE(gitRun(other, {"push"}), 0);
        service.sync();
        QTRY_VERIFY(!service.state()["busy"].toBool());
        QCOMPARE(errors.count(), 2);
        QVERIFY(errors.last().first().toString().contains("diverged"));
    }
    void updaterFailure() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")},
                               {"updateMode", "external"},
                               {"updater", "/bin/sh"},
                               {"updaterArguments", "-c 'echo test-update; exit 3'"}}));
        Maintenance m(&settings);
        QSignalSpy restart(&m, &Maintenance::restarting);
        m.update();
        QTRY_VERIFY(!m.state()["busy"].toBool());
        QVERIFY(m.state()["log"].toString().contains("test-update"));
        QVERIFY(m.state()["log"].toString().contains("failed"));
        QCOMPARE(restart.count(), 0);
    }
    void transportRecovery() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        QSignalSpy connected(&rpc, &RpcClient::connected);
        rpc.connectServer();
        QTRY_VERIFY(rpc.ready());
        QList<int> order;
        rpc.request("fixture/reorder", {}, [&](auto, auto) { order << 1; });
        rpc.request("fixture/reorder", {}, [&](auto, auto) { order << 2; });
        QTRY_COMPARE(order.size(), 2);
        QCOMPARE(order, QList<int>({2, 1}));
        int callbacks = 0;
        rpc.request("fixture/disconnect", {}, [&](auto, auto error) {
            if (!error.isEmpty())
                ++callbacks;
        });
        QTRY_COMPARE(callbacks, 1);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 2, 10000);
        QCOMPARE(callbacks, 1);
        int staleCallbacks = 0;
        rpc.request("fixture/reorder", {}, [&](auto, auto) { ++staleCallbacks; });
        rpc.disconnectServer();
        rpc.connectServer();
        QTRY_VERIFY(rpc.ready());
        QCOMPARE(staleCallbacks, 0);
    }
    void builtinUpdateFallback() {
        QTemporaryDir dir;
        const auto bin = dir.filePath("bin"),
                   codexDir = dir.filePath(".codex/packages/standalone/current/bin");
        QDir().mkpath(bin);
        QDir().mkpath(codexDir);
        QVERIFY(QFile::copy(SOURCE_DIR "/tests/fake_codex.py", codexDir + "/codex"));
        QFile curl(bin + "/curl");
        QVERIFY(curl.open(QIODevice::WriteOnly));
        curl.write(
            "#!/usr/bin/env python3\nimport os,sys,pathlib\n"
            "if not os.environ.get('HTTPS_PROXY','').startswith('socks5h://'): sys.exit(7)\n"
            "pathlib.Path(sys.argv[sys.argv.index('--output')+1]).write_text('echo fixture-installed\\n')\n");
        curl.close();
        QVERIFY(
            curl.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const auto oldPath = qgetenv("PATH");
        qputenv("PATH", bin.toUtf8() + ":" + oldPath);
        const auto restore = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", codexDir + "/codex"},
                               {"updateMode", "builtin"},
                               {"proxyMode", "custom"},
                               {"proxyUrl", "http://fixture.invalid:8"},
                               {"fallbackProxy", "socks5h://fixture.invalid:9"}}));
        Maintenance m(&settings);
        QSignalSpy restarted(&m, &Maintenance::restarting);
        m.update();
        QTRY_VERIFY_WITH_TIMEOUT(!m.state()["busy"].toBool(), 10000);
        QVERIFY(m.state()["log"].toString().contains("fallback proxy"));
        QVERIFY(m.state()["log"].toString().contains("fixture-installed"));
        QCOMPARE(restarted.count(), 0);
        QTRY_COMPARE(m.state()["version"].toString(), "0.162.1");
    }
    void pairing() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        Maintenance maintenance(&settings);
        maintenance.setRpc(&rpc);
        rpc.connectServer();
        QTRY_VERIFY(rpc.ready());
        QTRY_COMPARE(maintenance.state()["remote"].toString(), "connected");
        maintenance.pair();
        QTRY_COMPARE(maintenance.state()["pairing"].toString(), "123-456");
        QVERIFY(maintenance.state()["log"].toString().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(maintenance.state()["pairingStatus"].toString(), "Device paired.", 10000);
        QVERIFY(maintenance.state()["pairing"].toString().isEmpty());
    }
    void switchingProjectCancelsLoading() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        QVERIFY(settings.save({{"executable", QString(SOURCE_DIR "/tests/fake_codex.py")}}));
        RpcClient rpc(&settings);
        GitService git;
        AppController app(&rpc, &settings, &git);
        rpc.connectServer();
        QTRY_COMPARE(app.threads()->count(), 1);
        app.selectThread("t1");
        QVERIFY(app.state()["loading"].toBool());
        const auto project = app.state()["projectId"].toString();
        settings.saveDraft("last-thread-" + project, ""); // Do not restore t1 automatically.
        app.selectProject(project);
        QVERIFY(!app.state()["loading"].toBool());
        QVERIFY(app.state()["threadId"].toString().isEmpty());
        QTest::qWait(200);
        QVERIFY(!app.state()["loading"].toBool());
        QCOMPARE(app.messages()->count(), 0);
    }
    void drafts() {
        QTemporaryDir dir;
        Settings settings(nullptr, dir.filePath("config"));
        settings.saveDraft("t1", "text");
        QCOMPARE(settings.draft("t1"), "text");
        settings.saveDraft("t1", "");
        QVERIFY(!KConfigGroup(KSharedConfig::openConfig(dir.filePath("config")), "Drafts").hasKey("t1"));
    }
    void markdownKeepsRawHtmlAsText() {
        RichText rich;
        auto html = rich.markdown("Use <b>tags</b> and **bold**", false);
        QVERIFY(html.contains("&lt;b&gt;"));
        QVERIFY(html.contains("font-weight"));
    }
    void model() {
        ListModel m;
        m.upsert({{"id", "a"}, {"text", "hello"}});
        m.delta("a", " world", "agentMessage");
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.get(0)["text"].toString(), "hello world");
        m.upsert({{"id", "b"}}, true);
        QCOMPARE(m.get(0)["id"].toString(), "b");
        m.remove("b");
        QCOMPARE(m.count(), 1);
        // The id index must survive prepends and removals in the middle.
        for (int i = 0; i < 5; ++i)
            m.upsert({{"id", QString("p%1").arg(i)}}, true);
        m.upsert({{"id", "z"}});
        m.remove("p2");
        QCOMPARE(m.count(), 6);
        m.upsert({{"id", "p0"}, {"text", "updated"}});
        m.upsert({{"id", "z"}, {"text", "last"}});
        QCOMPARE(m.count(), 6);
        QCOMPARE(m.get(3)["text"].toString(), "updated");
        QCOMPARE(m.get(5)["text"].toString(), "last");
        // Deltas update data immediately and notify views once per burst.
        QSignalSpy changes(&m, &ListModel::dataChanged);
        for (int i = 0; i < 100; ++i)
            m.delta("a", "!", "agentMessage");
        QVERIFY(m.get(4)["text"].toString().endsWith(QString(100, '!')));
        QCOMPARE(changes.count(), 0);
        QTRY_COMPARE(changes.count(), 1);
    }
    void proxy() {
        QProcessEnvironment e;
        e.insert("PATH", "/bin");
        e.insert("https_proxy", "old");
        auto inherited = Settings::proxyEnvironment(e, "inherit", "", "");
        QCOMPARE(inherited.value("https_proxy"), "old");
        auto clean = Settings::proxyEnvironment(e, "none", "", "");
        QVERIFY(!clean.contains("https_proxy"));
        QCOMPARE(clean.value("PATH"), "/bin");
        auto custom = Settings::proxyEnvironment(e, "custom", "socks5h://localhost:1234", "localhost");
        QCOMPARE(custom.value("HTTPS_PROXY"), "socks5h://localhost:1234");
        QCOMPARE(custom.value("https_proxy"), custom.value("HTTPS_PROXY"));
        QCOMPARE(custom.value("NO_PROXY"), "localhost");
    }
    void shortPaths() {
        QTemporaryDir home;
        const auto saved = qgetenv("HOME");
        qputenv("HOME", home.path().toUtf8());
        auto restore = qScopeGuard([&] { qputenv("HOME", saved); });
        QCOMPARE(Settings::shortPath(home.path()), "~");
        QCOMPARE(Settings::shortPath(home.path() + "/Vídeos/clip"), "~/Vídeos/clip");
        QCOMPARE(Settings::shortPath(home.path() + "x/other"), home.path() + "x/other");
        QCOMPARE(Settings::shortPath("/mnt/Data/Kodex"), "/mnt/Data/Kodex");
    }
    void programPaths() {
        QTemporaryDir dir;
        QFile tool(dir.filePath("tool"));
        QVERIFY(tool.open(QIODevice::WriteOnly));
        tool.write("#!/bin/sh\n");
        tool.close();
        QFile::setPermissions(tool.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(Settings::resolveProgram(tool.fileName()).isEmpty());
        QFile::setPermissions(tool.fileName(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(Settings::resolveProgram(" " + tool.fileName() + " "), tool.fileName());
        QVERIFY(Settings::resolveProgram(dir.path()).isEmpty());
        QVERIFY(Settings::resolveProgram(dir.filePath("missing")).isEmpty());
        QVERIFY(Settings::resolveProgram("").isEmpty());
        QVERIFY(Settings::resolveProgram("kodex-no-such-program").isEmpty());
        QCOMPARE(Settings::resolveProgram("sh"), QStandardPaths::findExecutable("sh"));
        QCOMPARE(Settings::localPath(QUrl::fromLocalFile(dir.filePath("a b"))), dir.filePath("a b"));
        QCOMPARE(Settings::dialogFolder(tool.fileName()), QUrl::fromLocalFile(dir.path()));
        QCOMPARE(Settings::dialogFolder("kodex-no-such-program"), QUrl::fromLocalFile(QDir::homePath()));
    }
    void standardInstall() {
        QTemporaryDir home;
        QVERIFY(QDir().mkpath(home.filePath("empty")));
        const auto savedHome = qgetenv("HOME"), savedPath = qgetenv("PATH");
        qputenv("HOME", home.path().toUtf8());
        qputenv("PATH", home.filePath("empty").toUtf8());
        auto restore = qScopeGuard([&] {
            qputenv("HOME", savedHome);
            qputenv("PATH", savedPath);
        });
        QCOMPARE(Settings(nullptr, home.filePath("a")).value("executable"), "codex");
        QVERIFY(QDir().mkpath(home.filePath(".local/bin")));
        const auto codex = home.filePath(".local/bin/codex");
        QFile file(codex);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        file.close();
        Settings settings(nullptr, home.filePath("b"));
        QCOMPARE(settings.value("executable"), codex);
        QCOMPARE(settings.executable(), codex);
        // A bare name is found there even when the session PATH leaves it out.
        QVERIFY(settings.save({{"executable", "codex"}}));
        QCOMPARE(settings.executable(), codex);
        QCOMPARE(Settings::resolveProgram("codex"), codex);
    }
    void redact() {
        auto s = Settings::redact("https://user:secret@example.test:123 path");
        QVERIFY(!s.contains("secret"));
        QVERIFY(s.contains("example.test"));
    }
    void framing() {
        QByteArray b("\x81\x05hello", 7), out;
        quint8 op = 0;
        bool fin = false;
        auto part = b.left(3);
        QCOMPARE(WebSocketCodec::take(part, out, op, fin), 0);
        part += b.mid(3);
        QCOMPARE(WebSocketCodec::take(part, out, op, fin), 1);
        QCOMPARE(out, "hello");
        QVERIFY(fin);
        QCOMPARE(op, 1);
        QVERIFY(part.isEmpty());
        auto masked = WebSocketCodec::frame("hello");
        QVERIFY(quint8(masked[1]) & 128);
        QByteArray payload = masked.mid(6);
        for (int i = 0; i < payload.size(); ++i)
            payload[i] = payload[i] ^ masked[2 + i % 4];
        QCOMPARE(payload, "hello");
    }
    void gitParsing() {
        QByteArray s = " M file with space.cpp";
        s.append('\0');
        s += "R  new.cpp";
        s.append('\0');
        s += "old.cpp";
        s.append('\0');
        s += "?? new\nline";
        s.append('\0');
        auto rows = GitService::parseStatus(s);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[1].toObject()["oldPath"].toString(), "old.cpp");
        QVERIFY(rows[1].toObject()["staged"].toBool());
        QCOMPARE(rows[2].toObject()["path"].toString(), "new\nline");
    }
    void liveHandshake() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_TEST"))
            QSKIP("Set KODEX_LIVE_TEST=1 to check the installed daemon");
        Settings settings;
        RpcClient rpc(&settings);
        rpc.connectServer();
        QTRY_VERIFY_WITH_TIMEOUT(rpc.ready(), 20000);
        bool done = false;
        QJsonObject result, error;
        rpc.request("model/list", {}, [&](auto r, auto e) {
            result = r;
            error = e;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY2(error.isEmpty(), qPrintable(error["message"].toString()));
        QVERIFY(!result["data"].toArray().isEmpty());
        done = false;
        rpc.request("project/list", {}, [&](auto r, auto e) {
            result = r;
            error = e;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY2(error.isEmpty(), qPrintable(error["message"].toString()));
        QVERIFY(result.contains("data"));
    }
    void liveTurn() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_TURN"))
            QSKIP("Set KODEX_LIVE_TURN=1 for a short, read-only model turn");
        QTemporaryDir dir;
        Settings settings;
        RpcClient rpc(&settings);
        rpc.connectServer();
        QTRY_VERIFY_WITH_TIMEOUT(rpc.ready(), 20000);
        QJsonObject result, error;
        bool done = false;
        rpc.request("thread/start",
                    {{"cwd", dir.path()},
                     {"ephemeral", true},
                     {"experimentalRawEvents", false},
                     {"sandbox", "read-only"},
                     {"approvalPolicy", "never"}},
                    [&](auto r, auto e) {
                        result = r;
                        error = e;
                        done = true;
                    });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY2(error.isEmpty(), qPrintable(error["message"].toString()));
        auto id = result["thread"].toObject()["id"].toString();
        QVERIFY(!id.isEmpty());
        bool complete = false;
        QString response;
        connect(&rpc, &RpcClient::notification, this, [&](const QString &m, const QJsonObject &p) {
            if (p["threadId"].toString() != id)
                return;
            if (m == "item/agentMessage/delta")
                response += p["delta"].toString();
            if (m == "turn/completed") {
                error = p["turn"].toObject()["error"].toObject();
                complete = true;
            }
        });
        done = false;
        rpc.request(
            "turn/start",
            {{"threadId", id},
             {"input", QJsonArray{QJsonObject{
                           {"type", "text"},
                           {"text", "Reply exactly KODEX_OK. Do not call any tools or inspect files."}}}}},
            [&](auto r, auto e) {
                result = r;
                error = e;
                done = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY2(error.isEmpty(), qPrintable(error["message"].toString()));
        QTRY_VERIFY_WITH_TIMEOUT(complete, 90000);
        QVERIFY2(error.isEmpty(), qPrintable(error["message"].toString()));
        QVERIFY(response.contains("KODEX_OK"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
