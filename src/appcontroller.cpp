#include "appcontroller.h"
#include <KLocalizedString>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
AppController::AppController(RpcClient *r, Settings *s, GitService *g, QObject *p)
    : QObject(p), m_rpc(r), m_settings(s), m_git(g) {
    m_state["planReady"] = false;
    m_state["proposedPlan"] = "";
    connect(r, &RpcClient::connected, this, [this] {
        refresh();
        auto id = m_state["threadId"].toString();
        if (!id.isEmpty())
            selectThread(id);
    });
    connect(r, &RpcClient::disconnected, this, [this] {
        m_requests.reset();
        m_requestData.clear();
        m_requestIds.clear();
        m_sending = false;
        m_historyLoading = false;
        m_state["loading"] = false;
        emit changed();
    });
    connect(r, &RpcClient::notification, this, &AppController::notify);
    connect(r, &RpcClient::serverRequest, this, &AppController::receiveRequest);
}
void AppController::call(const QString &m, const QJsonObject &p, RpcClient::Callback cb) {
    m_rpc->request(m, p, [this, m, cb](auto r, auto e) {
        if (!e.isEmpty())
            emit error(m + ": " + Settings::redact(e["message"].toString()));
        if (cb)
            cb(r, e);
    });
}
void AppController::refresh() {
    loadProjects();
    call("account/read", {{"refreshToken", false}}, [this](auto r, auto e) {
        if (!e.isEmpty())
            return;
        auto a = r["account"].toObject();
        m_state["account"] = a["email"].toString(a["type"].toString());
        m_state["signedIn"] = !a.isEmpty();
        emit changed();
    });
    loadModels();
    m_rpc->request("account/rateLimits/read", {}, [this](auto r, auto e) {
        if (e.isEmpty())
            notify("account/rateLimits/updated", r);
    });
    call("configRequirements/read", {}, [this](auto r, auto e) {
        if (e.isEmpty()) {
            m_state["requirements"] = r.toVariantMap();
            emit changed();
        }
    });
    refreshMcp();
}
void AppController::loadModels(const QString &cursor) {
    QJsonObject params;
    if (!cursor.isEmpty())
        params["cursor"] = cursor;
    call("model/list", params, [this, cursor](auto r, auto e) {
        if (!e.isEmpty())
            return;
        if (cursor.isEmpty())
            m_models.reset();
        for (auto v : r["data"].toArray())
            m_models.upsert(v.toObject());
        if (!r["nextCursor"].toString().isEmpty()) {
            loadModels(r["nextCursor"].toString());
            return;
        }
        if (m_state["model"].toString().isEmpty()) {
            for (auto v : m_models.json()) {
                auto o = v.toObject();
                if (o["isDefault"].toBool()) {
                    chooseModel(o["model"].toString());
                    break;
                }
            }
        } else
            chooseModel(m_state["model"].toString());
    });
}
void AppController::loadProjects(const QString &cursor) {
    QJsonObject p{{"limit", 100}};
    if (!cursor.isEmpty())
        p["cursor"] = cursor;
    call("project/list", p, [this, cursor](auto r, auto e) {
        if (!e.isEmpty())
            return;
        if (cursor.isEmpty())
            m_projects.reset();
        for (auto v : r["data"].toArray()) {
            auto o = v.toObject();
            o["path"] = o["roots"].toArray().isEmpty() ? QJsonValue()
                                                       : o["roots"].toArray().first().toObject()["path"];
            m_projects.upsert(o);
        }
        auto next = r["nextCursor"].toString();
        if (!next.isEmpty())
            loadProjects(next);
        else if (m_state["projectId"].toString().isEmpty() && m_projects.count()) {
            const auto saved = m_settings->draft("last-project");
            for (auto project : m_projects.json())
                if (project.toObject()["id"].toString() == saved) {
                    selectProject(saved);
                    return;
                }
            selectProject(m_projects.get(0)["id"].toString());
        }
    });
}
void AppController::addProject(const QUrl &folder) {
    auto path = QDir(folder.toLocalFile()).canonicalPath();
    if (path.isEmpty()) {
        emit error(i18n("Choose an existing local directory."));
        return;
    }
    for (auto v : m_projects.json())
        if (v.toObject()["path"].toString() == path) {
            selectProject(v.toObject()["id"].toString());
            return;
        }
    call("project/create",
         {{"name", QFileInfo(path).fileName()},
          {"idempotencyKey", QUuid::createUuid().toString(QUuid::WithoutBraces)},
          {"roots", QJsonArray{QJsonObject{{"path", path}}}}},
         [this, path](auto r, auto e) {
             if (e.isEmpty()) {
                 auto project = r["project"].toObject();
                 project["path"] = path;
                 m_projects.upsert(project);
                 selectProject(project["id"].toString());
                 loadProjects();
             }
         });
}
void AppController::selectProject(const QString &id) {
    if (m_git->state()["busy"].toBool()) {
        emit error(i18n("Wait for the Git operation to finish."));
        return;
    }
    for (auto v : m_projects.json()) {
        auto o = v.toObject();
        if (o["id"].toString() != id)
            continue;
        m_state["projectId"] = id;
        m_settings->saveDraft("last-project", id);
        m_state["cwd"] = o["path"].toString();
        m_state["projectName"] = o["name"].toString();
        clearThread();
        m_git->setDirectory(o["path"].toString());
        emit changed();
        loadThreads();
        break;
    }
}
void AppController::clearThread() {
    // Responses for the previous thread are ignored, so reset everything they would have reset.
    m_state["threadId"] = "";
    m_state["title"] = "";
    m_state["busy"] = false;
    m_state["loading"] = false;
    m_state["turnDiff"] = "";
    m_state["contextPercent"] = -1;
    m_state["historyCursor"] = "";
    m_state["planReady"] = false;
    m_state["proposedPlan"] = "";
    m_messages.reset();
    m_turn.clear();
    m_historyLoading = false;
}
void AppController::renameProject(const QString &name) {
    if (name.trimmed().isEmpty())
        return;
    call("project/update", {{"projectId", m_state["projectId"].toString()}, {"name", name}},
         [this](auto, auto e) {
             if (e.isEmpty())
                 loadProjects();
         });
}
void AppController::loadThreads(bool archived, const QString &cursor) {
    auto cwd = m_state["cwd"].toString();
    if (cwd.isEmpty())
        return;
    m_state["archived"] = archived;
    QJsonObject p{{"cwd", cwd}, {"archived", archived}, {"limit", 50}};
    if (!cursor.isEmpty())
        p["cursor"] = cursor;
    call("thread/list", p, [this, cwd, cursor](auto r, auto e) {
        if (!e.isEmpty() || cwd != m_state["cwd"].toString())
            return;
        if (cursor.isEmpty())
            m_threads.reset();
        for (auto v : r["data"].toArray()) {
            m_threads.upsert(v.toObject());
            const auto entry = v.toObject();
            if (entry["id"].toString() == m_state["threadId"].toString()) {
                const auto title = entry["name"].toString().isEmpty() ? entry["preview"].toString()
                                                                      : entry["name"].toString();
                if (!title.isEmpty())
                    m_state["title"] = title;
            }
        }
        if (!m_state["archived"].toBool()) {
            for (auto it = m_newThreads.begin(); it != m_newThreads.end(); ++it) {
                auto session = it.value();
                if (session["cwd"].toString() == cwd)
                    m_threads.upsert(session["thread"].toObject(), true);
            }
        }
        if (m_state["threadId"].toString().isEmpty() && !m_state["archived"].toBool()) {
            const auto saved = m_settings->draft("last-thread-" + m_state["projectId"].toString());
            for (auto v : r["data"].toArray())
                if (v.toObject()["id"].toString() == saved) {
                    selectThread(saved);
                    break;
                }
        }
        m_state["threadsCursor"] = r["nextCursor"].toString();
        emit changed();
    });
}
void AppController::newThread() {
    if (m_state["cwd"].toString().isEmpty() || m_state["loading"].toBool())
        return;
    m_state["loading"] = true;
    m_state["archived"] = false;
    emit changed();
    QJsonObject p{{"cwd", m_state["cwd"].toString()},
                  {"projectId", m_state["projectId"].toString()},
                  {"experimentalRawEvents", false}};
    if (!m_state["model"].toString().isEmpty())
        p["model"] = m_state["model"].toString();
    const auto project = m_state["projectId"].toString();
    call("thread/start", p, [this, project](auto r, auto e) {
        if (project != m_state["projectId"].toString())
            return;
        m_state["loading"] = false;
        if (e.isEmpty()) {
            auto t = r["thread"].toObject();
            m_threads.upsert(t, true);
            m_newThreads.insert(t["id"].toString(), r);
            selectThread(t["id"].toString());
        }
        emit changed();
    });
}
void AppController::selectThread(const QString &id) {
    if (m_git->state()["busy"].toBool()) {
        emit error(i18n("Wait for the Git operation to finish."));
        return;
    }
    m_state["threadId"] = id;
    m_state["loading"] = true;
    m_state["contextPercent"] = -1;
    m_state["turnDiff"] = "";
    m_state["historyCursor"] = "";
    m_state["planReady"] = false;
    m_state["proposedPlan"] = "";
    m_settings->saveDraft("last-thread-" + m_state["projectId"].toString(), id);
    m_messages.reset();
    m_turn.clear();
    m_historyLoading = false;
    emit changed();
    // A new thread has no persisted rollout until its first turn. Keep using the
    // session returned by thread/start; thread/resume requires persisted history.
    if (m_newThreads.contains(id)) {
        m_state["loading"] = false;
        applySessionResponse(m_newThreads.value(id));
        return;
    }
    if (m_state["archived"].toBool()) {
        call("thread/read", {{"threadId", id}, {"includeTurns", false}}, [this, id](auto r, auto e) {
            if (id != m_state["threadId"].toString())
                return;
            m_state["loading"] = false;
            m_state["busy"] = false;
            if (e.isEmpty()) {
                auto t = r["thread"].toObject();
                m_state["title"] = t["name"].toString(t["preview"].toString());
                loadHistory(false);
            }
            emit changed();
        });
        return;
    }
    call("thread/resume", {{"threadId", id}, {"excludeTurns", true}}, [this, id](auto r, auto e) {
        if (id != m_state["threadId"].toString())
            return;
        m_state["loading"] = false;
        if (!e.isEmpty()) {
            emit changed();
            return;
        }
        applySessionResponse(r);
        loadHistory(false);
    });
}
void AppController::applySessionResponse(const QJsonObject &r) {
    auto t = r["thread"].toObject();
    auto cwd = r["cwd"].toString(t["cwd"].toString());
    if (!cwd.isEmpty() && cwd != m_state["cwd"].toString()) {
        m_state["cwd"] = cwd;
        m_git->setDirectory(cwd);
        for (auto v : m_projects.json())
            if (v.toObject()["path"].toString() == cwd) {
                m_state["projectId"] = v.toObject()["id"].toString();
                m_state["projectName"] = v.toObject()["name"].toString();
                break;
            }
        loadThreads();
    }
    m_state["title"] = t["name"].toString(t["preview"].toString());
    m_state["busy"] = t["status"].toObject()["type"].toString() == "active";
    chooseModel(r["model"].toString());
    m_state["effort"] = r["reasoningEffort"].toString();
    m_state["plan"] = r["collaborationMode"].toObject()["mode"].toString() == "plan";
    m_state["fast"] = r["serviceTier"].toString() == "fast" || r["serviceTier"].toString() == "priority";
    m_state["permission"] = "inherit";
    m_inheritedPermissions = {};
    if (!r["activePermissionProfile"].toObject()["id"].toString().isEmpty())
        m_inheritedPermissions["permissions"] = r["activePermissionProfile"].toObject()["id"];
    else if (r.contains("sandbox"))
        m_inheritedPermissions["sandboxPolicy"] = r["sandbox"];
    if (r.contains("approvalPolicy"))
        m_inheritedPermissions["approvalPolicy"] = r["approvalPolicy"];
    if (r.contains("approvalsReviewer"))
        m_inheritedPermissions["approvalsReviewer"] = r["approvalsReviewer"];
    emit changed();
}
void AppController::loadHistory(bool older) {
    auto id = m_state["threadId"].toString(), cursor = m_state["historyCursor"].toString();
    if (id.isEmpty() || m_historyLoading || (older && cursor.isEmpty()))
        return;
    m_historyLoading = true;
    QJsonObject p{{"threadId", id}, {"limit", 15}, {"itemsView", "full"}, {"sortDirection", "desc"}};
    if (older)
        p["cursor"] = cursor;
    call("thread/turns/list", p, [this, id](auto r, auto e) {
        if (id != m_state["threadId"].toString())
            return;
        m_historyLoading = false;
        if (!e.isEmpty())
            return;
        auto turns = r["data"].toArray();
        if (m_state["historyCursor"].toString().isEmpty() && !turns.isEmpty() && m_state["plan"].toBool()) {
            const auto latest = turns.first().toObject();
            for (auto v : latest["items"].toArray())
                if (v.toObject()["type"] == "plan") {
                    m_state["proposedPlan"] = v.toObject()["text"].toString();
                    m_state["planReady"] = latest["status"] == "completed";
                }
        }
        for (auto t : turns) {
            auto turn = t.toObject();
            if (turn["status"] == "inProgress") {
                m_turn = turn["id"].toString();
                m_state["busy"] = true;
            }
            auto items = turn["items"].toArray();
            for (int i = items.size() - 1; i >= 0; --i) {
                auto entry = items[i].toObject();
                if (entry["status"] == "inProgress" && turn["status"] != "inProgress")
                    entry["status"] = turn["status"];
                item(entry, true);
            }
        }
        m_state["historyCursor"] = r["nextCursor"].toString();
        emit changed();
    });
}
void AppController::renameThread(const QString &name) {
    if (name.trimmed().isEmpty())
        return;
    call("thread/name/set", {{"threadId", m_state["threadId"].toString()}, {"name", name}},
         [this, name](auto, auto e) {
             if (e.isEmpty()) {
                 m_state["title"] = name;
                 emit changed();
                 loadThreads();
             }
         });
}
void AppController::archiveThread(bool archived) {
    auto id = m_state["threadId"].toString();
    if (id.isEmpty())
        return;
    call(archived ? "thread/archive" : "thread/unarchive", {{"threadId", id}}, [this, id](auto, auto e) {
        if (!e.isEmpty())
            return;
        if (id == m_state["threadId"].toString()) {
            clearThread();
            emit changed();
        }
        loadThreads(m_state["archived"].toBool());
    });
}
void AppController::chooseModel(const QString &model) {
    for (auto v : m_models.json()) {
        auto o = v.toObject();
        if (o["model"].toString() != model)
            continue;
        m_state["model"] = model;
        QStringList efforts;
        for (auto e : o["supportedReasoningEfforts"].toArray())
            efforts << e.toObject()["reasoningEffort"].toString();
        m_state["efforts"] = efforts;
        if (!efforts.contains(m_state["effort"].toString()))
            m_state["effort"] = o["defaultReasoningEffort"].toString();
        QString fast;
        for (auto t : o["serviceTiers"].toArray()) {
            auto tier = t.toObject();
            if (tier["id"] == "fast" || tier["id"] == "priority")
                fast = tier["id"].toString();
        }
        m_state["fastTier"] = fast;
        m_state["fastAvailable"] = !fast.isEmpty();
        if (fast.isEmpty())
            m_state["fast"] = false;
        emit changed();
        return;
    }
    if (!model.isEmpty()) {
        m_state["model"] = model;
        emit changed();
    }
}
QJsonObject AppController::options() const {
    QJsonObject p;
    auto model = m_state["model"].toString(), effort = m_state["effort"].toString();
    if (!model.isEmpty()) {
        p["model"] = model;
        p["collaborationMode"] = QJsonObject{
            {"mode", m_state["plan"].toBool() ? "plan" : "default"},
            {"settings",
             QJsonObject{{"model", model},
                         {"reasoning_effort", effort.isEmpty() ? QJsonValue() : QJsonValue(effort)},
                         {"developer_instructions", QJsonValue()}}}};
    }
    if (!effort.isEmpty())
        p["effort"] = effort;
    if (m_state["fastAvailable"].toBool())
        p["serviceTier"] = m_state["fast"].toBool() ? m_state["fastTier"].toString() : "default";
    auto permission = m_state["permission"].toString();
    if (permission != "inherit") {
        p["sandboxPolicy"] = QJsonObject{{"type", permission == "full"   ? "dangerFullAccess"
                                                  : permission == "read" ? "readOnly"
                                                                         : "workspaceWrite"}};
        p["approvalPolicy"] = permission == "full" ? "never" : "on-request";
        p["approvalsReviewer"] = permission == "auto" ? "auto_review" : "user";
    } else
        for (auto it = m_inheritedPermissions.begin(); it != m_inheritedPermissions.end(); ++it)
            p[it.key()] = it.value();
    return p;
}
void AppController::setOption(const QString &key, const QVariant &value) {
    if (m_state["archived"].toBool())
        return;
    const auto before = m_state;
    const auto revision = ++m_optionRevision;
    if (key == "model")
        chooseModel(value.toString());
    else if (QStringList{"effort", "plan", "permission", "fast"}.contains(key)) {
        m_state[key] = value;
        emit changed();
    } else
        return;
    auto id = m_state["threadId"].toString();
    if (!id.isEmpty() && m_rpc->ready()) {
        auto p = options();
        p["threadId"] = id;
        call("thread/settings/update", p, [this, before, revision, id, p](auto, auto e) {
            if (e.isEmpty() && m_newThreads.contains(id)) {
                auto &session = m_newThreads[id];
                for (const auto &key :
                     {"model", "collaborationMode", "approvalPolicy", "approvalsReviewer", "serviceTier"})
                    if (p.contains(key))
                        session[key] = p[key];
                if (p.contains("effort"))
                    session["reasoningEffort"] = p["effort"];
                if (p.contains("sandboxPolicy")) {
                    session["sandbox"] = p["sandboxPolicy"];
                    session.remove("activePermissionProfile");
                }
                if (p.contains("permissions"))
                    session["activePermissionProfile"] = QJsonObject{{"id", p["permissions"]}};
            }
            if (!e.isEmpty() && m_optionRevision == revision && m_state["threadId"] == id) {
                for (const auto &key : {"model", "effort", "efforts", "plan", "permission", "fast",
                                        "fastAvailable", "fastTier"})
                    m_state[key] = before[key];
                emit changed();
            }
        });
    }
}
bool AppController::send(const QString &text) {
    if (text.trimmed().isEmpty() || m_state["archived"].toBool())
        return false;
    if (text.startsWith('/')) {
        bool ok = slash(text);
        if (ok)
            emit messageAccepted(text, m_state["threadId"].toString());
        return ok;
    }
    if (!m_rpc->ready() || m_state["threadId"].toString().isEmpty() || m_sending ||
        m_state["loading"].toBool())
        return false;
    bool busy = m_state["busy"].toBool();
    if (busy && m_turn.isEmpty()) {
        emit error(i18n("The active turn is not known yet. Try again in a moment."));
        return false;
    }
    auto id = m_state["threadId"].toString();
    auto p = busy ? QJsonObject{} : options();
    p["threadId"] = id;
    p["input"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}};
    if (busy)
        p["expectedTurnId"] = m_turn;
    m_sending = true;
    const bool implementing = std::exchange(m_implementingPlan, false);
    call(busy ? "turn/steer" : "turn/start", p, [this, id, text, implementing](auto r, auto e) {
        m_sending = false;
        if (e.isEmpty()) {
            m_newThreads.remove(id);
            if (m_settings->draft(id) == text)
                m_settings->saveDraft(id, "");
            emit messageAccepted(text, id);
        }
        if (id != m_state["threadId"].toString())
            return;
        if (implementing) {
            m_state["plan"] = !e.isEmpty();
            m_state["planReady"] = !e.isEmpty();
            if (e.isEmpty())
                m_state["proposedPlan"] = "";
        }
        if (e.isEmpty()) {
            auto turn = r["turn"].toObject();
            if (!turn.isEmpty())
                m_turn = turn["id"].toString();
            m_state["busy"] = true;
        }
        emit changed();
    });
    return true;
}
void AppController::interrupt() {
    if (m_turn.isEmpty())
        return;
    call("turn/interrupt", {{"threadId", m_state["threadId"].toString()}, {"turnId", m_turn}});
}
bool AppController::implementPlan() {
    if (!m_state["planReady"].toBool() || m_state["busy"].toBool() || !m_rpc->ready() ||
        m_state["archived"].toBool())
        return false;
    m_state["plan"] = false;
    m_implementingPlan = true;
    const bool accepted = send("Implement the plan.");
    if (!accepted) {
        m_state["plan"] = true;
        m_implementingPlan = false;
    }
    emit changed();
    return accepted;
}
void AppController::keepPlanning() {
    m_state["planReady"] = false;
    emit changed();
}
bool AppController::slash(const QString &text) {
    auto cmd = text.section(' ', 0, 0).toLower(), arg = text.section(' ', 1).trimmed();
    if (cmd == "/plan")
        setOption("plan", !m_state["plan"].toBool());
    else if (cmd == "/fast") {
        if (!m_state["fastAvailable"].toBool())
            emit error(i18n("Fast mode is not available for this model."));
        else
            setOption("fast", !m_state["fast"].toBool());
    } else if (cmd == "/new")
        newThread();
    else if (cmd == "/resume")
        emit navigate("projects");
    else if (cmd == "/model") {
        if (!arg.isEmpty())
            setOption("model", arg);
        else
            emit navigate("model");
    } else if (cmd == "/permissions")
        emit navigate("permissions");
    else if (cmd == "/mcp") {
        refreshMcp();
        emit navigate("settings");
    } else if (cmd == "/status") {
        refresh();
        emit navigate("settings");
    } else if (cmd == "/compact" || cmd == "/review") {
        auto id = m_state["threadId"].toString();
        if (id.isEmpty())
            return false;
        QJsonObject p{{"threadId", id}};
        if (cmd == "/review")
            p["target"] = QJsonObject{{"type", "uncommittedChanges"}};
        call(cmd == "/compact" ? "thread/compact/start" : "review/start", p);
    } else if (cmd == "/help")
        m_messages.upsert({{"id", QUuid::createUuid().toString()},
                           {"type", "notice"},
                           {"text", "/plan · /compact · /model · /permissions · /fast · /new · /resume · "
                                    "/review · /status · /mcp · /help"}});
    else {
        emit error(i18n("Unknown command: %1", cmd));
        return false;
    }
    return true;
}
void AppController::login(bool device) {
    call("account/login/start", {{"type", device ? "chatgptDeviceCode" : "chatgpt"}}, [this](auto r, auto e) {
        if (!e.isEmpty())
            return;
        m_loginId = r["loginId"].toString();
        auto url = r["authUrl"].toString(r["verificationUrl"].toString());
        m_state["loginInfo"] = r["userCode"].toString();
        m_state["loginPending"] = true;
        emit changed();
        if (!url.isEmpty())
            openLink(url);
    });
}
void AppController::cancelLogin() {
    if (m_loginId.isEmpty())
        return;
    call("account/login/cancel", {{"loginId", m_loginId}});
}
void AppController::logout() {
    call("account/logout", {}, [this](auto, auto e) {
        if (e.isEmpty())
            refresh();
    });
}
void AppController::refreshMcp() {
    loadMcp();
}
void AppController::loadMcp(const QString &cursor) {
    QJsonObject params;
    if (!cursor.isEmpty())
        params["cursor"] = cursor;
    call("mcpServerStatus/list", params, [this, cursor](auto r, auto e) {
        if (!e.isEmpty())
            return;
        if (cursor.isEmpty())
            m_mcp.reset();
        for (auto v : r["data"].toArray()) {
            auto o = v.toObject();
            o["id"] = o["name"];
            m_mcp.upsert(o);
        }
        if (!r["nextCursor"].toString().isEmpty())
            loadMcp(r["nextCursor"].toString());
    });
}
void AppController::loginMcp(const QString &name) {
    call("mcpServer/oauth/login", {{"name", name}}, [this](auto r, auto e) {
        if (e.isEmpty())
            openLink(r["authorizationUrl"].toString());
    });
}
void AppController::reloadMcp() {
    call("config/mcpServer/reload", {}, [this](auto, auto e) {
        if (e.isEmpty())
            refreshMcp();
    });
}
void AppController::item(QJsonObject o, bool prepend) {
    auto type = o["type"].toString();
    QString text = o["text"].toString();
    if (type == "userMessage") {
        for (auto v : o["content"].toArray())
            text += v.toObject()["text"].toString();
    } else if (type == "commandExecution")
        text = o["command"].toString() + "\n" + o["aggregatedOutput"].toString();
    else if (type == "fileChange") {
        for (auto v : o["changes"].toArray()) {
            auto c = v.toObject();
            text += c["path"].toString() + "\n" + c["diff"].toString() + "\n";
        }
    } else if (type == "reasoning") {
        for (auto v : o["summary"].toArray())
            text += v.toString() + "\n";
    } else if (type == "mcpToolCall")
        text = o["server"].toString() + " / " + o["tool"].toString() + "\n" +
               QString::fromUtf8(QJsonDocument(o["result"].toObject()).toJson(QJsonDocument::Indented));
    else if (type == "contextCompaction")
        text = i18n("Context compacted");
    else if (text.isEmpty())
        text = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
    o["text"] = text;
    // Collapsed cards show one line; computing it here keeps QML from scanning long output.
    o["summary"] = text.left(text.indexOf('\n')).left(300);
    m_messages.upsert(o, prepend);
}
void AppController::notify(const QString &m, const QJsonObject &p) {
    if (m == "account/updated") {
        refresh();
        return;
    }
    if (m == "account/login/completed") {
        m_state["loginPending"] = false;
        m_state["loginInfo"] = "";
        m_loginId.clear();
        if (!p["success"].toBool())
            emit error(p["error"].toString());
        refresh();
        return;
    }
    if (m == "account/rateLimits/updated") {
        QStringList summaries;
        auto buckets = p["rateLimitsByLimitId"].toObject();
        if (buckets.isEmpty())
            buckets["codex"] = p["rateLimits"];
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            auto b = it.value().toObject();
            for (const auto &key : {"primary", "secondary"}) {
                auto w = b[key].toObject();
                if (w.isEmpty())
                    continue;
                summaries << i18n("%1: %2% used · resets %3", it.key(), w["usedPercent"].toDouble(),
                                  QDateTime::fromSecsSinceEpoch(w["resetsAt"].toInteger())
                                      .toLocalTime()
                                      .toString("ddd HH:mm"));
            }
        }
        m_state["limits"] = summaries.join("\n");
        emit changed();
        return;
    }
    if (m == "serverRequest/resolved") {
        auto id = p["requestId"].toVariant().toString();
        m_requests.remove(id);
        m_requestData.remove(id);
        m_requestIds.remove(id);
        return;
    }
    if (m.startsWith("mcpServer/")) {
        refreshMcp();
        return;
    }
    auto id = p["threadId"].toString();
    if (m == "thread/name/updated") {
        for (auto v : m_threads.json())
            if (v.toObject()["id"].toString() == id) {
                m_threads.upsert({{"id", id}, {"name", p["threadName"]}});
                break;
            }
        if (id == m_state["threadId"].toString())
            m_state["title"] = p["threadName"].toString();
        emit changed();
        return;
    }
    if (m == "thread/status/changed") {
        for (auto t : m_threads.json())
            if (t.toObject()["id"].toString() == id) {
                m_threads.upsert({{"id", id}, {"status", p["status"]}});
                break;
            }
        if (id == m_state["threadId"].toString())
            m_state["busy"] = p["status"].toObject()["type"] == "active";
        emit changed();
    }
    if (!id.isEmpty() && id != m_state["threadId"].toString())
        return;
    // Streaming deltas arrive per token and rarely touch the state; every changed()
    // re-evaluates all QML bindings on App.state, so emit it only for real changes.
    const auto before = m_state;
    if (m == "turn/started") {
        m_turn = p["turn"].toObject()["id"].toString();
        m_state["busy"] = true;
        m_state["planReady"] = false;
        m_state["proposedPlan"] = "";
    } else if (m == "turn/completed") {
        m_state["busy"] = false;
        m_turn.clear();
        const QJsonValue status = p.value("turn").toObject().value("status");
        if (status == "interrupted" || status == "failed")
            for (auto v : m_messages.json())
                if (v.toObject()["status"] == "inProgress")
                    m_messages.upsert({{"id", v.toObject()["id"]}, {"status", status}});
        auto e = p["turn"].toObject()["error"].toObject();
        if (!e.isEmpty())
            emit error(e["message"].toString());
        m_state["planReady"] = m_state["plan"].toBool() && !m_state["proposedPlan"].toString().isEmpty() &&
                               p["turn"].toObject()["status"] == "completed";
        m_git->refresh();
        loadThreads();
    } else if (m == "item/fileChange/patchUpdated")
        item({{"id", p["itemId"]}, {"type", "fileChange"}, {"changes", p["changes"]}});
    else if (m == "item/started" || m == "item/completed") {
        auto entry = p["item"].toObject();
        item(entry);
        if (m == "item/completed" && entry["type"] == "plan")
            m_state["proposedPlan"] = entry["text"].toString();
    } else if (m == "item/agentMessage/delta" || m == "item/reasoning/summaryTextDelta" ||
               m == "item/commandExecution/outputDelta" || m == "item/plan/delta")
        m_messages.delta(p["itemId"].toString(), p["delta"].toString(),
                         m == "item/plan/delta"           ? "plan"
                         : m == "item/agentMessage/delta" ? "agentMessage"
                         : m.contains("reasoning")        ? "reasoning"
                                                          : "commandExecution");
    else if (m == "turn/diff/updated")
        m_state["turnDiff"] = p["diff"].toString();
    else if (m == "turn/plan/updated") {
        QString text = p["explanation"].toString();
        for (auto v : p["plan"].toArray()) {
            auto s = v.toObject();
            text += "\n- [" + (s["status"] == "completed" ? QString("x") : QString(" ")) + "] " +
                    s["step"].toString();
        }
        m_messages.upsert(
            {{"id", "plan-" + p["turnId"].toString()}, {"type", "planProgress"}, {"text", text}});
    } else if (m == "thread/tokenUsage/updated") {
        auto u = p["tokenUsage"].toObject();
        auto window = u["modelContextWindow"].toDouble();
        auto tokens = u["last"].toObject()["totalTokens"].toDouble();
        m_state["contextPercent"] = window > 0 ? qBound(0., tokens / window * 100., 100.) : -1;
        m_state["contextTokens"] = tokens;
        m_state["contextWindow"] = window;
        m_state["totalTokens"] = u["total"].toObject()["totalTokens"].toDouble();
    } else if (m == "error")
        emit error(p["error"].toObject()["message"].toString());
    if (m_state != before)
        emit changed();
}
void AppController::receiveRequest(const QJsonValue &id, const QString &m, const QJsonObject &p) {
    if (m == "currentTime/read") {
        m_rpc->respond(id, {{"currentTimeAt", QDateTime::currentSecsSinceEpoch()}});
        return;
    }
    if (!QStringList{"item/commandExecution/requestApproval", "item/fileChange/requestApproval",
                     "item/permissions/requestApproval", "item/tool/requestUserInput",
                     "mcpServer/elicitation/request"}
             .contains(m)) {
        m_rpc->reject(id, "Unsupported client request");
        return;
    }
    auto key = id.toVariant().toString();
    auto row = p;
    row["id"] = key;
    row["method"] = m;
    row["details"] = QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Indented));
    if (m == "item/fileChange/requestApproval")
        for (auto v : m_messages.json())
            if (v.toObject()["id"] == p["itemId"])
                row["operationPreview"] = v.toObject()["text"];
    if (m == "item/permissions/requestApproval")
        row["operationPreview"] =
            QString::fromUtf8(QJsonDocument(p["permissions"].toObject()).toJson(QJsonDocument::Indented));
    m_requestData[key] = row;
    m_requestIds[key] = id;
    m_requests.upsert(row);
}
void AppController::answer(const QString &id, const QVariant &decision, const QVariantMap &values) {
    if (!m_requestData.contains(id))
        return;
    auto p = m_requestData[id];
    auto m = p["method"].toString();
    auto d = decision.toString();
    QJsonObject result;
    if (m == "item/tool/requestUserInput") {
        QJsonObject a;
        for (auto it = values.begin(); it != values.end(); ++it)
            a[it.key()] = QJsonObject{{"answers", QJsonArray{it.value().toString()}}};
        result["answers"] = a;
    } else if (m == "item/permissions/requestApproval") {
        result["permissions"] = d == "accept" ? p["permissions"].toObject() : QJsonObject{};
        result["scope"] = "turn";
    } else if (m == "mcpServer/elicitation/request") {
        result["action"] = d == "accept" ? "accept" : d == "cancel" ? "cancel" : "decline";
        result["content"] = d == "accept" ? QJsonValue(QJsonObject::fromVariantMap(values)) : QJsonValue();
    } else
        result["decision"] = QJsonValue::fromVariant(decision);
    m_rpc->respond(m_requestIds.take(id), result);
    m_requestData.remove(id);
    m_requests.remove(id);
}
void AppController::openFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_state["cwd"].toString()));
}
void AppController::openLink(const QString &link) {
    QUrl u(link);
    if (u.scheme() == "https" || u.scheme() == "http" || u.scheme() == "file")
        QDesktopServices::openUrl(u);
}
void AppController::copy(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}
