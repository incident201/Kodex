/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "appcontroller.h"
#include "maintenance.h"
#include "richtext.h"
#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QApplication>
#include <QInputMethodEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <clocale>

namespace {
bool until(const std::function<bool()> &predicate, int timeout = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout)
        QTest::qWait(20);
    return predicate();
}
struct LiveUi {
    QTemporaryDir temp;
    Settings settings{nullptr, temp.filePath("kodexrc")};
    RpcClient rpc{&settings};
    GitService git;
    Maintenance maintenance{&settings};
    AppController app{&rpc, &settings, &git};
    RichText rich;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QString workspace = temp.filePath("workspace"), project;
    QStringList threads, errors;
    QHash<QString, QString> activeTurns;
    QHash<QString, QString> lastStatus;
    int completed = 0;
    QString artifacts = QCoreApplication::applicationDirPath() + "/live-artifacts";
    LiveUi() {
        QDir().mkpath(workspace);
        QDir().mkpath(artifacts);
        settings.save({{"language", "ru"}});
        std::setlocale(LC_MESSAGES, "ru_RU.UTF-8");
        KLocalizedString::setApplicationDomain("kodex");
        KLocalizedString::addDomainLocaleDir("kodex", QCoreApplication::applicationDirPath() + "/locale");
        KLocalizedString::setLanguages({"ru"});
        maintenance.setRpc(&rpc);
        QObject::connect(&app, &AppController::error, &engine, [this](const QString &e) {
            errors << e;
            qWarning().noquote() << e;
        });
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                         [this](const QList<QQmlError> &list) {
                             for (auto e : list)
                                 if (e.url().toString().contains("/qml/")) {
                                     errors << e.toString();
                                     qWarning().noquote() << e.toString();
                                 }
                         });
        QObject::connect(&rpc, &RpcClient::notification, &engine,
                         [this](const QString &m, const QJsonObject &p) {
                             const auto id = p["threadId"].toString();
                             if (!threads.contains(id))
                                 return;
                             if (m == "turn/started")
                                 activeTurns[id] = p["turn"].toObject()["id"].toString();
                             if (m == "turn/completed") {
                                 activeTurns.remove(id);
                                 lastStatus[id] = p["turn"].toObject()["status"].toString();
                                 ++completed;
                             }
                         });
        engine.setUiLanguage("ru");
        auto *context = new KLocalizedQmlContext(&engine);
        context->setTranslationDomain("kodex");
        engine.rootContext()->setContextObject(context);
        QQmlEngine::setContextForObject(context, engine.rootContext());
        engine.rootContext()->setContextProperty("App", &app);
        engine.rootContext()->setContextProperty("Preferences", &settings);
        engine.rootContext()->setContextProperty("Connection", &rpc);
        engine.rootContext()->setContextProperty("Repository", &git);
        engine.rootContext()->setContextProperty("Maintenance", &maintenance);
        engine.rootContext()->setContextProperty("RichText", &rich);
        engine.load(QUrl::fromLocalFile(SOURCE_DIR "/qml/Main.qml"));
        std::setlocale(LC_MESSAGES, "ru_RU.UTF-8");
        KLocalizedString::setLanguages({"ru"});
        engine.retranslate();
        if (!engine.rootObjects().isEmpty())
            window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    }
    QJsonObject request(const QString &method, const QJsonObject &params = {}) {
        struct Reply {
            bool done = false;
            QJsonObject data;
        };
        auto reply = std::make_shared<Reply>();
        rpc.request(method, params, [reply](auto r, auto e) {
            reply->data = r;
            if (!e.isEmpty())
                reply->data["testError"] = e;
            reply->done = true;
        });
        if (!until([reply] { return reply->done; }, 45000))
            reply->data["testError"] = QJsonObject{{"message", "Test RPC timeout"}};
        return reply->data;
    }
    ~LiveUi() {
        if (!rpc.ready())
            return;
        for (auto id : threads) {
            if (activeTurns.contains(id))
                request("turn/interrupt", {{"threadId", id}, {"turnId", activeTurns[id]}});
            request("thread/archive", {{"threadId", id}});
            request("thread/delete", {{"threadId", id}});
        }
        if (!project.isEmpty())
            request("project/delete", {{"projectId", project}});
    }
    bool begin() {
        if (!window)
            return false;
        rpc.connectServer();
        if (!until([this] {
                return rpc.ready() && app.models()->count() > 0 && app.state()["signedIn"].toBool();
            }))
            return false;
        app.addProject(QUrl::fromLocalFile(workspace));
        if (!until([this] { return app.state()["cwd"].toString() == workspace; }))
            return false;
        project = app.state()["projectId"].toString();
        return newTask();
    }
    QQuickItem *findVisual(QQuickItem *root, const QString &name) {
        if (root->objectName() == name)
            return root;
        for (auto *child : root->childItems())
            if (auto *found = findVisual(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *find(const QString &name) {
        if (auto *found = findVisual(window->contentItem(), name))
            return found;
        for (auto *found : engine.rootObjects().first()->findChildren<QQuickItem *>(name))
            if (found->window() && found->isVisible())
                return found;
        return nullptr;
    }
    bool click(const QString &name) {
        QQuickItem *item = nullptr;
        if (!until(
                [&] {
                    item = find(name);
                    return item && item->isVisible() && item->isEnabled();
                },
                5000)) {
            qWarning() << "UI control unavailable:" << name;
            return false;
        }
        // Scroll an item into its containing Flickable before sending a real mouse event.
        for (auto *p = item->parentItem(); p; p = p->parentItem()) {
            if (!p->property("contentY").isValid())
                continue;
            auto pos = item->mapToItem(p, QPointF(0, 0));
            auto y = p->property("contentY").toReal();
            if (pos.y() < 0)
                p->setProperty("contentY", y + pos.y());
            else if (pos.y() + item->height() > p->height())
                p->setProperty("contentY", y + pos.y() + item->height() - p->height());
        }
        QTest::qWait(60);
        item = find(name);
        if (!item || !item->isVisible() || !item->isEnabled())
            return false;
        auto *target = item->window();
        if (!target) {
            fprintf(stderr, "No window for control %s\n", qPrintable(name));
            return false;
        }
        const auto pos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, pos);
        QTest::qWait(60);
        return true;
    }
    bool newTask() {
        auto before = app.state()["threadId"].toString();
        if (!click("newTaskButton"))
            return false;
        if (!until([&] {
                return !app.state()["loading"].toBool() && !app.state()["threadId"].toString().isEmpty() &&
                       app.state()["threadId"].toString() != before;
            }))
            return false;
        threads << app.state()["threadId"].toString();
        return true;
    }
    bool send(const QString &text) {
        auto *edit = find("messageEditor");
        if (!edit)
            return false;
        edit->setProperty("text", text);
        return click("sendButton");
    }
    bool select(const QString &selector, const QString &value) {
        if (!click(selector))
            return false;
        return click("option-" + selector + "-" + value);
    }
    void screenshot(const QString &name) {
        QTest::qWait(150);
        window->grabWindow().save(artifacts + "/" + name + ".png");
    }
    QString diagnostics() const { return errors.join('\n'); }
    QString agentText() {
        QString text;
        for (auto v : app.messages()->json())
            if (v.toObject()["type"] == "agentMessage")
                text += v.toObject()["text"].toString() + "\n";
        return text;
    }
};
} // namespace

class InteractiveTests : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void visualDiffThemes() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_INTERACTIVE"))
            QSKIP("Real Codex opt-in required");
        LiveUi ui;
        QVERIFY2(ui.begin(), qPrintable(ui.diagnostics()));
        auto runGit = [&](const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(ui.workspace);
            p.start("git", args);
            if (!p.waitForFinished(10000))
                return -1;
            return p.exitCode();
        };
        QCOMPARE(runGit({"init", "-b", "main"}), 0);
        QFile original(ui.workspace + "/main.py");
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("print('OLD')\n");
        original.close();
        QCOMPARE(runGit({"add", "main.py"}), 0);
        QCOMPARE(runGit({"-c", "user.name=Kodex Test", "-c", "user.email=test@example.invalid", "commit",
                         "-m", "Initial fixture"}),
                 0);
        QVERIFY(ui.select("permissionsSelector", "workspace"));
        if (ui.app.state()["efforts"].toStringList().contains("low"))
            QVERIFY(ui.select("effortSelector", "low"));
        int before = ui.completed;
        QVERIFY(ui.send("Use apply_patch to change main.py: replace OLD with NEW, then add a function "
                        "greet(name) returning 'Hello, ' + name. Modify only main.py in the current "
                        "directory. Then reply VISUAL_OK."));
        QVERIFY(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 90000));
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        ui.git.refresh();
        QVERIFY(until([&] { return ui.git.files()->count() == 1; }));
        QVERIFY(ui.click("file-main.py"));
        QVERIFY(until([&] { return ui.git.state()["diff"].toString().contains("+print"); }));
        const auto theme = qEnvironmentVariable("KODEX_TEST_THEME", "System");
        const auto image = ui.window->grabWindow();
        if (theme == "Light")
            QVERIFY2(image.pixelColor(2, 2).lightness() > 128, "KDE did not load the light palette");
        if (theme == "Dark")
            QVERIFY2(image.pixelColor(2, 2).lightness() < 128, "KDE did not load the dark palette");
        ui.screenshot(theme == "Light"  ? "15-diff-light"
                      : theme == "Dark" ? "16-diff-dark"
                                        : "15-diff-system");
        ui.window->resize(850, 600);
        ui.screenshot(theme == "Light" ? "17-diff-compact-light" : "17-diff-compact");
        qInfo() << "PASS: real modified-file diff rendered with verified KDE theme" << theme;
    }
    void taskSwitching() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_INTERACTIVE"))
            QSKIP("Real Codex opt-in required");
        LiveUi ui;
        QVERIFY2(ui.begin(), qPrintable(ui.diagnostics()));
        const auto first = ui.app.state()["threadId"].toString();
        int before = ui.completed;
        QVERIFY(ui.send("Reply exactly FIRST_SWITCH_OK. No tools."));
        QVERIFY(until([&] { return ui.completed > before; }, 90000));
        QVERIFY(ui.newTask());
        const auto second = ui.app.state()["threadId"].toString();
        before = ui.completed;
        QVERIFY(ui.send("Reply exactly SECOND_SWITCH_OK. No tools."));
        QVERIFY(until([&] { return ui.completed > before; }, 90000));
        QVERIFY(until([&] { return ui.app.threads()->count() >= 2; }));
        for (const auto &id : {first, second, first, second}) {
            QVERIFY(ui.click("thread-" + id));
            if (ui.app.state()["threadId"].toString() != id) {
                ui.screenshot("switch-failure");
                qInfo() << "Actual thread" << ui.app.state()["threadId"] << "expected" << id;
            }
            QCOMPARE(ui.app.state()["threadId"].toString(), id);
            const auto expected = id == first ? "FIRST_SWITCH_OK" : "SECOND_SWITCH_OK";
            const bool loaded = until(
                [&] { return !ui.app.state()["loading"].toBool() && ui.agentText().contains(expected); });
            if (!loaded) {
                ui.screenshot("switch-history-failure");
                qInfo() << "Visible messages" << ui.app.messages()->json();
                qInfo() << "Server turns"
                        << ui.request("thread/turns/list", {{"threadId", id}, {"itemsView", "full"}});
            }
            QVERIFY2(loaded, qPrintable(ui.diagnostics()));
        }
        qInfo() << "PASS: four sidebar switches between two real task histories";
    }
    void approvalsCompactionAndArchive() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_INTERACTIVE"))
            QSKIP("Real Codex opt-in required");
        LiveUi ui;
        QVERIFY2(ui.begin(), qPrintable(ui.diagnostics()));
        const auto thread = ui.app.state()["threadId"].toString();
        if (ui.app.state()["efforts"].toStringList().contains("low"))
            QVERIFY(ui.select("effortSelector", "low"));
        QVERIFY(ui.select("permissionsSelector", "read"));
        const auto target = ui.temp.filePath("approved-marker.txt");
        const auto prompt =
            QString("This is a permissions UI test. Use exec_command with "
                    "sandbox_permissions=require_escalated and a short justification to run exactly: printf "
                    "APPROVAL_OK > '%1'. It is a test-owned file outside the read-only workspace. You MUST "
                    "request the user's approval before running it. If denied, stop and do not retry or find "
                    "another way. Do not read other files or use the network.")
                .arg(target);
        for (const auto &decision : {QString("decline"), QString("accept")}) {
            int before = ui.completed;
            QVERIFY(ui.send(prompt));
            QVERIFY(until(
                [&] {
                    return ui.app.requests()->count() > 0 || ui.completed > before || !ui.errors.isEmpty();
                },
                90000));
            QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
            QVERIFY2(ui.app.requests()->count() > 0, "Expected a real sandbox approval request");
            auto request = ui.app.requests()->get(0);
            auto detail = QJsonDocument(QJsonObject::fromVariantMap(request)).toJson();
            QVERIFY2(detail.contains(ui.temp.path().toUtf8()),
                     "Approval must target the test-owned temporary directory");
            QVERIFY(request["method"].toString().endsWith("/requestApproval"));
            ui.screenshot(decision == "decline" ? "10-approval-decline" : "11-approval-accept");
            const auto offered = request["availableDecisions"].toList();
            const auto wireDecision =
                decision == "decline" && !offered.isEmpty() && !offered.contains("decline")
                    ? QString("cancel")
                    : decision;
            QVERIFY(ui.click("approval-" + request["id"].toString() + "-" + wireDecision));
            QVERIFY(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 90000));
            QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
            QCOMPARE(QFileInfo::exists(target), decision == "accept");
        }
        QFile marker(target);
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(marker.readAll(), QByteArray("APPROVAL_OK"));
        qInfo() << "PASS: real approval declined without a write, then approved through the GUI";
        for (const auto &mode : {QString("workspace"), QString("auto"), QString("full"), QString("read")}) {
            QVERIFY(ui.select("permissionsSelector", mode));
            auto effective = ui.request("thread/resume", {{"threadId", thread}, {"excludeTurns", true}});
            QVERIFY(!effective.contains("testError"));
            QCOMPARE(effective["approvalsReviewer"].toString(),
                     mode == "auto" ? QString("auto_review") : QString("user"));
        }
        qInfo() << "PASS: access mode changes accepted by the real server";
        int before = ui.completed;
        QVERIFY(ui.send("/compact"));
        QVERIFY(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 120000));
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        bool compacted = false;
        for (auto v : ui.app.messages()->json())
            if (v.toObject()["type"] == "contextCompaction")
                compacted = true;
        QVERIFY(compacted);
        ui.screenshot("12-compacted");
        QVERIFY(ui.click("archiveTaskButton"));
        QVERIFY(until([&] { return ui.app.threads()->count() == 0; }));
        QVERIFY(ui.click("archivedTasksToggle"));
        QVERIFY(until([&] { return ui.app.threads()->count() == 1; }));
        QVERIFY(ui.click("thread-" + thread));
        QVERIFY(until([&] { return !ui.app.state()["loading"].toBool() && ui.app.messages()->count() > 0; }));
        ui.screenshot("13-archived-task");
        QVERIFY(ui.click("archiveTaskButton"));
        QVERIFY(until([&] { return ui.app.threads()->count() == 0; }));
        QVERIFY(ui.click("archivedTasksToggle"));
        QVERIFY(until([&] { return ui.app.threads()->count() == 1; }));
        QVERIFY(ui.click("thread-" + thread));
        QVERIFY(until([&] { return !ui.app.state()["loading"].toBool() && ui.app.messages()->count() > 0; }));
        ui.screenshot("14-restored-task");
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        qInfo() << "PASS: real compaction, archive and restore with history";
    }
    void realInteractiveScenarios() {
        if (!qEnvironmentVariableIsSet("KODEX_LIVE_INTERACTIVE"))
            QSKIP("Set KODEX_LIVE_INTERACTIVE=1; uses real models and temporary files");
        LiveUi ui;
        QVERIFY2(ui.begin(), qPrintable(ui.diagnostics()));
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        QVERIFY(ui.select("permissionsSelector", "workspace"));
        if (ui.app.state()["efforts"].toStringList().contains("medium"))
            QVERIFY(ui.select("effortSelector", "medium"));
        QVERIFY(ui.click("planToggle"));
        QVERIFY(ui.app.state()["plan"].toBool());
        const auto firstThread = ui.app.state()["threadId"].toString();
        int before = ui.completed;
        QVERIFY(
            ui.send("We are testing a native Codex client. Plan a tiny Python command-line greeter in "
                    "greet.py and one unittest in test_greet.py. Before proposing the plan, use the "
                    "request_user_input tool to ask exactly two multiple-choice questions: greeting language "
                    "(English or Russian), and capitalization (normal or uppercase). Ask in Russian; include "
                    "clear descriptions for each option. Wait for my answers. Then produce the final "
                    "proposed plan. Do not implement anything yet or create any files."));
        QVERIFY2(until(
                     [&] {
                         return ui.app.requests()->count() > 0 || ui.completed > before ||
                                !ui.errors.isEmpty();
                     },
                     120000),
                 "No question or plan response");
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        QVERIFY2(ui.app.requests()->count() > 0, "The model did not ask the requested structured questions");
        ui.screenshot("01-questions");
        ui.window->resize(850, 600);
        ui.screenshot("01-questions-compact");
        ui.window->resize(1320, 880);
        const auto pending = ui.app.requests()->get(0);
        const auto questions = pending["questions"].toList();
        QCOMPARE(questions.size(), 2);
        for (int i = 0; i < questions.size(); ++i) {
            const auto q = questions[i].toMap();
            const auto id = q["id"].toString();
            if (i == 0)
                QVERIFY(ui.click("questionOption-" + id + "-0"));
            else {
                QVERIFY(ui.click("questionAnswer-" + id));
                auto *edit = ui.find("questionAnswer-" + id);
                edit->forceActiveFocus();
                QInputMethodEvent input;
                input.setCommitString("Use uppercase");
                QCoreApplication::sendEvent(edit->window(), &input);
            }
        }
        ui.screenshot("02-questions-answered");
        QVERIFY(ui.click("submitAnswers-" + pending["id"].toString()));
        QVERIFY2(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 120000),
                 "Plan did not finish");
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        QVERIFY(ui.app.state()["planReady"].toBool());
        QVERIFY(!ui.app.state()["proposedPlan"].toString().isEmpty());
        QVERIFY(!QFileInfo::exists(ui.workspace + "/greet.py"));
        QVERIFY(ui.find("planDecision")->isVisible());
        ui.screenshot("03-plan-ready");
        qInfo() << "PASS: real Plan mode, two structured questions, option and free-text answers, "
                   "implementation decision";
        QVERIFY(ui.click("keepPlanningButton"));
        QVERIFY(ui.app.state()["plan"].toBool());
        QVERIFY(!ui.app.state()["planReady"].toBool());
        QVERIFY(!QFileInfo::exists(ui.workspace + "/greet.py"));
        ui.app.selectThread(firstThread);
        QVERIFY(until([&] { return ui.app.state()["planReady"].toBool(); }));
        before = ui.completed;
        QVERIFY(ui.click("implementPlanButton"));
        QVERIFY2(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 150000),
                 "Implementation did not finish");
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
        QVERIFY(!ui.app.state()["plan"].toBool());
        QVERIFY(QFileInfo::exists(ui.workspace + "/greet.py"));
        QVERIFY(QFileInfo::exists(ui.workspace + "/test_greet.py"));
        ui.screenshot("04-implemented");
        QProcess gitInit;
        gitInit.setWorkingDirectory(ui.workspace);
        gitInit.start("git", {"init", "-b", "main"});
        QVERIFY(gitInit.waitForFinished(10000));
        QCOMPARE(gitInit.exitCode(), 0);
        ui.git.refresh();
        QVERIFY(until([&] { return ui.git.state()["repository"].toBool() && ui.git.files()->count() > 0; }));
        QVERIFY(ui.click("file-greet.py"));
        QVERIFY(until([&] { return !ui.git.state()["diff"].toString().isEmpty(); }));
        ui.screenshot("04-file-diff");
        QProcess tests;
        tests.setWorkingDirectory(ui.workspace);
        tests.start("python", {"-m", "unittest", "discover"});
        QVERIFY(tests.waitForFinished(20000));
        QCOMPARE(tests.exitCode(), 0);
        qInfo() << "PASS: implement button left Plan mode, real code and passing generated unit test";
        before = ui.completed;
        QVERIFY(ui.send("Run the shell command sleep 30 in the current directory, then reply WAIT_DONE. Do "
                        "not change any files. This is a cancellation test."));
        QVERIFY(until(
            [&] {
                for (auto v : ui.app.messages()->json()) {
                    auto o = v.toObject();
                    if (o["type"] == "commandExecution" && o["status"] == "inProgress")
                        return true;
                }
                return false;
            },
            60000));
        ui.screenshot("05-running-before-stop");
        ui.window->resize(850, 600);
        ui.screenshot("05-running-compact");
        ui.window->resize(1320, 880);
        QVERIFY(ui.click("stopButton"));
        QVERIFY(until([&] { return ui.completed > before; }, 20000));
        QCOMPARE(ui.lastStatus[firstThread], "interrupted");
        ui.screenshot("06-stopped");
        before = ui.completed;
        QVERIFY(ui.send("Continue without running sleep. Reply exactly RESUMED_OK. Do not use tools."));
        QVERIFY(until([&] { return ui.completed > before; }, 90000));
        QVERIFY(ui.agentText().contains("RESUMED_OK"));
        qInfo() << "PASS: actual command interrupted and same task continued";
        QStringList modelIds{ui.app.state()["model"].toString()};
        for (auto v : ui.app.models()->json()) {
            auto m = v.toObject();
            if (!m["hidden"].toBool() && m["model"].toString() != modelIds.first() &&
                m["model"].toString().contains("sol")) {
                modelIds << m["model"].toString();
                break;
            }
        }
        if (modelIds.size() == 1)
            for (auto v : ui.app.models()->json()) {
                auto m = v.toObject();
                if (!m["hidden"].toBool() && !modelIds.contains(m["model"].toString())) {
                    modelIds << m["model"].toString();
                    break;
                }
            }
        QVERIFY2(modelIds.size() >= 2, "Account must expose two models for this scenario");
        for (const auto &model : modelIds) {
            QVERIFY(ui.select("modelSelector", model));
            QCOMPARE(ui.app.state()["model"].toString(), model);
            const auto supported = ui.app.state()["efforts"].toStringList();
            QStringList efforts;
            for (const auto &candidate : {"low", "medium", "high"})
                if (supported.contains(candidate) && efforts.size() < 2)
                    efforts << candidate;
            if (efforts.isEmpty())
                efforts << supported.first();
            for (const auto &effort : efforts) {
                QVERIFY(ui.select("effortSelector", effort));
                before = ui.completed;
                QVERIFY(ui.send("Reply exactly MODEL_OK. No tools or file changes."));
                QVERIFY2(until([&] { return ui.completed > before || !ui.errors.isEmpty(); }, 90000),
                         "Model response timed out");
                QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
                QVERIFY(ui.agentText().contains("MODEL_OK"));
                auto effective =
                    ui.request("thread/resume", {{"threadId", firstThread}, {"excludeTurns", true}});
                QCOMPARE(effective["model"].toString(), model);
                QCOMPARE(effective["reasoningEffort"].toString(), effort);
                qInfo() << "PASS: real model/effort" << model << effort;
            }
        }
        if (ui.app.state()["fastAvailable"].toBool()) {
            const bool initial = ui.app.state()["fast"].toBool();
            QVERIFY(ui.click("fastToggle"));
            QTest::qWait(100);
            auto effective = ui.request("thread/resume", {{"threadId", firstThread}, {"excludeTurns", true}});
            QCOMPARE(effective["serviceTier"].toString(),
                     initial ? QString("default") : ui.app.state()["fastTier"].toString());
            QVERIFY(ui.click("fastToggle"));
            qInfo() << "PASS: Fast toggle persisted to the real server";
        }
        QVERIFY(ui.click("modelSelector"));
        ui.screenshot("07-model-menu");
        QTest::keyClick(ui.window, Qt::Key_Escape);
        ui.window->resize(850, 600);
        ui.screenshot("08-compact-window");
        ui.window->resize(1320, 880);
        QVERIFY(ui.newTask());
        const auto secondThread = ui.app.state()["threadId"].toString();
        before = ui.completed;
        QVERIFY(ui.send("Reply exactly SECOND_TASK_OK. No tools."));
        QVERIFY(until([&] { return ui.completed > before; }, 90000));
        QVERIFY(until([&] { return ui.app.threads()->count() >= 2; }));
        ui.screenshot("09-two-tasks-before-switch");
        QVERIFY(ui.click("thread-" + firstThread));
        QVERIFY(until([&] { return !ui.app.state()["loading"].toBool() && ui.app.messages()->count() > 0; }));
        QVERIFY(!ui.agentText().contains("SECOND_TASK_OK"));
        QVERIFY(ui.agentText().contains("RESUMED_OK"));
        QVERIFY(ui.click("thread-" + secondThread));
        QVERIFY(until([&] {
            return !ui.app.state()["loading"].toBool() && ui.agentText().contains("SECOND_TASK_OK");
        }));
        ui.screenshot("09-second-task");
        qInfo() << "PASS: two independent tasks and restored histories through sidebar clicks";
        QVERIFY2(ui.errors.isEmpty(), qPrintable(ui.diagnostics()));
    }
};
int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");
    QApplication app(argc, argv);
    app.setApplicationName("kodex");
    app.setOrganizationName("Kodex");
    KAboutData::setApplicationData(KAboutData("kodex", "Kodex", "0.1.0"));
    std::setlocale(LC_MESSAGES, "ru_RU.UTF-8");
    KLocalizedString::setApplicationDomain("kodex");
    KLocalizedString::setLanguages({"ru"});
    QQuickStyle::setStyle("org.kde.desktop");
    InteractiveTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_interactive.moc"
