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
  private slots:
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
        QTRY_COMPARE(m.state()["version"].toString(), "0.156.1");
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
