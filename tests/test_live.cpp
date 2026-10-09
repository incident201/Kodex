#include "appcontroller.h"
#include "maintenance.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QtTest>

// Explicit opt-in: this test uses the installed Codex and a real model account.
// It owns only its temporary project/thread/repository; the shared daemon stays running.
class LiveTests : public QObject {
    Q_OBJECT
  private slots:
    void applicationWorkflow() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_WORKFLOW"))
            QSKIP("Set KODEX_LIVE_WORKFLOW=1 to exercise the application with real Codex");
        KLocalizedString::setApplicationDomain("kodex");
        QTemporaryDir temp;
        const auto workspace = temp.filePath("workspace");
        QDir().mkpath(workspace);
        auto gitRun = [&](const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(workspace);
            p.start("git", args);
            if (!p.waitForFinished(10000))
                return -1;
            return p.exitCode();
        };
        QCOMPARE(gitRun({"init", "-b", "main"}), 0);
        QCOMPARE(gitRun({"config", "user.name", "Kodex smoke test"}), 0);
        QCOMPARE(gitRun({"config", "user.email", "smoke@example.invalid"}), 0);
        Settings settings(nullptr, temp.filePath("kodexrc"));
        RpcClient rpc(&settings);
        GitService git;
        Maintenance maintenance(&settings);
        maintenance.setRpc(&rpc);
        AppController controller(&rpc, &settings, &git);
        QStringList failures;
        connect(&controller, &AppController::error, this, [&](const QString &error) {
            failures << error;
            qWarning().noquote() << error;
        });
        auto noErrors = [&] { return failures.isEmpty(); };
        auto report = [&] { return failures.join('\n').toUtf8(); };
        auto request = [&](const QString &method, const QJsonObject &params) {
            bool done = false;
            QJsonObject result;
            rpc.request(method, params, [&](auto r, auto e) {
                result = r;
                if (!e.isEmpty())
                    result["testError"] = e;
                done = true;
            });
            QElapsedTimer clock;
            clock.start();
            while (!done && clock.elapsed() < 45000)
                QTest::qWait(10);
            if (!done)
                result["testError"] = QJsonObject{{"message", "Test RPC timeout"}};
            return result;
        };
        QString projectId, threadId;
        auto cleanup = qScopeGuard([&] {
            if (!rpc.ready())
                return;
            if (!threadId.isEmpty()) {
                controller.interrupt();
                auto result = request("thread/archive", {{"threadId", threadId}});
                if (result.contains("testError"))
                    qWarning() << "Test thread cleanup failed";
                result = request("thread/delete", {{"threadId", threadId}});
                if (result.contains("testError"))
                    qWarning() << "Test thread deletion failed";
            }
            if (!projectId.isEmpty()) {
                const auto result = request("project/delete", {{"projectId", projectId}});
                if (result.contains("testError"))
                    qWarning() << "Test project cleanup failed";
            }
        });
        rpc.connectServer();
        QTRY_VERIFY_WITH_TIMEOUT(rpc.ready(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.models()->count() > 0, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.state()["signedIn"].toBool(), 20000);
        qInfo() << "PASS: actual account, model catalog, MCP and remote status";
        controller.addProject(QUrl::fromLocalFile(workspace));
        QTRY_COMPARE_WITH_TIMEOUT(controller.state()["cwd"].toString(), workspace, 20000);
        projectId = controller.state()["projectId"].toString();
        QVERIFY(!projectId.isEmpty());
        QVERIFY2(noErrors(), report().constData());
        controller.newThread();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.state()["threadId"].toString().isEmpty(), 20000);
        threadId = controller.state()["threadId"].toString();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.state()["loading"].toBool(), 20000);
        QTest::qWait(200);
        QVERIFY2(noErrors(), report().constData());
        qInfo() << "PASS: create project and thread through AppController";
        controller.setOption("permission", "workspace");
        const auto efforts = controller.state()["efforts"].toStringList();
        if (efforts.contains("low"))
            controller.setOption("effort", "low");
        else if (efforts.contains("medium"))
            controller.setOption("effort", "medium");
        controller.setOption("plan", true);
        QTest::qWait(100);
        QVERIFY2(noErrors(), report().constData());
        controller.setOption("plan", false);
        controller.setOption("fast", false);
        QTest::qWait(200);
        QVERIFY2(noErrors(), report().constData());
        bool completed = false;
        connect(&rpc, &RpcClient::notification, this, [&](const QString &m, const QJsonObject &p) {
            if (m == "turn/completed" && p["threadId"].toString() == threadId)
                completed = true;
        });
        QVERIFY(controller.send(
            "Use apply_patch in the current working directory to create KODEX_SMOKE.txt containing exactly "
            "one line: KODEX_SMOKE_OK. Do not access other directories, use network, or "
            "delegate. Then reply exactly KODEX_SMOKE_OK."));
        QTRY_VERIFY_WITH_TIMEOUT(completed || !noErrors() || controller.requests()->count() > 0, 90000);
        QVERIFY2(noErrors(), report().constData());
        QVERIFY2(controller.requests()->count() == 0,
                 "The isolated file-write test requested an approval; inspect it explicitly.");
        QVERIFY(completed);
        QFile created(workspace + "/KODEX_SMOKE.txt");
        QVERIFY(created.open(QIODevice::ReadOnly));
        QCOMPARE(created.readAll().trimmed(), QByteArray("KODEX_SMOKE_OK"));
        bool responseFound = false;
        for (auto v : controller.messages()->json()) {
            auto m = v.toObject();
            if (m["type"] == "agentMessage" && m["text"].toString().contains("KODEX_SMOKE_OK"))
                responseFound = true;
        }
        QVERIFY(responseFound);
        QTRY_COMPARE(git.files()->count(), 1);
        git.diff("KODEX_SMOKE.txt");
        QTRY_VERIFY(git.state()["diff"].toString().contains("+KODEX_SMOKE_OK"));
        qInfo() << "PASS: real streamed answer, file creation and Git diff; server turn diff received:"
                << !controller.state()["turnDiff"].toString().isEmpty();
        controller.setOption("plan", true);
        auto resumed = request("thread/resume", {{"threadId", threadId}, {"excludeTurns", true}});
        QVERIFY2(!resumed.contains("testError"),
                 qPrintable(QString::fromUtf8(QJsonDocument(resumed).toJson())));
        QCOMPARE(resumed["collaborationMode"].toObject()["mode"].toString(), "plan");
        controller.setOption("plan", false);
        controller.renameThread("Kodex live smoke test");
        QTRY_COMPARE(controller.state()["title"].toString(), "Kodex live smoke test");
        rpc.disconnectServer();
        rpc.connectServer();
        QTRY_VERIFY_WITH_TIMEOUT(rpc.ready(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !controller.state()["loading"].toBool() && controller.messages()->count() > 0, 20000);
        responseFound = false;
        for (auto v : controller.messages()->json()) {
            auto m = v.toObject();
            if (m["type"] == "agentMessage" && m["text"].toString().contains("KODEX_SMOKE_OK"))
                responseFound = true;
        }
        QVERIFY(responseFound);
        QVERIFY2(noErrors(), report().constData());
        qInfo() << "PASS: reconnect and restore the real thread history";
        QProcess window;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", temp.path());
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("QT_QUICK_BACKEND", "software");
        window.setProcessEnvironment(env);
        const auto screenshot = QCoreApplication::applicationDirPath() + "/live-smoke.png";
        QFile::remove(screenshot);
        window.start(QCoreApplication::applicationDirPath() + "/kodex",
                     {"--smoke-test", "--screenshot", screenshot});
        QVERIFY(window.waitForFinished(15000));
        QCOMPARE(window.exitStatus(), QProcess::NormalExit);
        QVERIFY2(window.exitCode() == 0, window.readAllStandardError().constData());
        QVERIFY(QFileInfo(screenshot).size() > 1000);
        qInfo() << "PASS: actual Kodex window loaded the real session; screenshot:" << screenshot;
        git.stage("KODEX_SMOKE.txt", false);
        QTRY_VERIFY(!git.state()["busy"].toBool());
        QTRY_VERIFY(git.files()->get(0)["staged"].toBool());
        git.commit("Verify Kodex live workflow");
        QTRY_VERIFY(!git.state()["busy"].toBool());
        QTRY_COMPARE(git.files()->count(), 0);
        qInfo() << "PASS: stage and commit the real agent change in the temporary repository";
    }
};
QTEST_GUILESS_MAIN(LiveTests)
#include "test_live.moc"
