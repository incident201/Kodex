/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "appcontroller.h"
#include <KLocalizedString>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
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
        Q_EMIT changed();
    });
    connect(r, &RpcClient::notification, this, &AppController::notify);
    connect(r, &RpcClient::serverRequest, this, &AppController::receiveRequest);
}
void AppController::call(const QString &m, const QJsonObject &p, RpcClient::Callback cb) {
    m_rpc->request(m, p, [this, m, cb](auto r, auto e) {
        if (!e.isEmpty())
            Q_EMIT error(m + ": " + Settings::redact(e["message"].toString()));
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
        Q_EMIT changed();
    });
    loadModels();
    m_rpc->request("account/rateLimits/read", {}, [this](auto r, auto e) {
        if (e.isEmpty())
            notify("account/rateLimits/updated", r);
    });
    call("configRequirements/read", {}, [this](auto r, auto e) {
        if (e.isEmpty()) {
            m_state["requirements"] = r.toVariantMap();
            Q_EMIT changed();
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
        else if (m_state["projectId"].toString().isEmpty() && !m_state["unassigned"].toBool()) {
            const auto saved = m_settings->draft("last-project");
            for (auto project : m_projects.json())
                if (project.toObject()["id"].toString() == saved) {
                    selectProject(saved);
                    return;
                }
            if (m_projects.count() && saved != "unassigned")
                selectProject(m_projects.get(0)["id"].toString());
            else
                showUnassigned();
        }
    });
}
void AppController::addProject(const QUrl &folder) {
    auto path = QDir(folder.toLocalFile()).canonicalPath();
    if (path.isEmpty()) {
        Q_EMIT error(i18n("Choose an existing local directory."));
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
        Q_EMIT error(i18n("Wait for the Git operation to finish."));
        return;
    }
    for (auto v : m_projects.json()) {
        auto o = v.toObject();
        if (o["id"].toString() != id)
            continue;
        m_state["unassigned"] = false;
        m_state["projectId"] = id;
        m_settings->saveDraft("last-project", id);
        m_state["cwd"] = o["path"].toString();
        m_state["projectName"] = o["name"].toString();
        clearThread();
        m_git->setDirectory(o["path"].toString());
        Q_EMIT changed();
        loadThreads();
        break;
    }
}
void AppController::showUnassigned() {
    if (m_git->state()["busy"].toBool()) {
        Q_EMIT error(i18n("Wait for the Git operation to finish."));
        return;
    }
    // Sessions that belong to no project, from any folder: the equivalent of `codex resume --all`.
    m_state["unassigned"] = true;
    m_state["projectId"] = "";
    m_state["projectName"] = "";
    m_state["cwd"] = "";
    m_settings->saveDraft("last-project", "unassigned");
    clearThread();
    m_git->setDirectory({});
    Q_EMIT changed();
    loadThreads();
}
QString AppController::scope() const {
    return m_state["unassigned"].toBool() ? "unassigned" : m_state["projectId"].toString();
}
void AppController::clearThread() {
    // Responses for the previous thread are ignored, so reset everything they would have reset.
    swapAttachments({});
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
    const bool unassigned = m_state["unassigned"].toBool();
    auto cwd = m_state["cwd"].toString();
    if (cwd.isEmpty() && !unassigned)
        return;
    m_state["archived"] = archived;
    QJsonObject p{{"archived", archived}, {"limit", 50}};
    if (unassigned) {
        p["projectId"] = QJsonValue::Null;
        p["sortKey"] = "updated_at";
    } else
        p["cwd"] = cwd;
    if (!cursor.isEmpty())
        p["cursor"] = cursor;
    call("thread/list", p, [this, cwd, unassigned, cursor](auto r, auto e) {
        // The selected folder changes while browsing unassigned sessions, so only the view matters there.
        if (!e.isEmpty() || unassigned != m_state["unassigned"].toBool() ||
            (!unassigned && cwd != m_state["cwd"].toString()))
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
                const auto thread = session["thread"].toObject();
                if (unassigned ? thread["projectId"].toString().isEmpty() : session["cwd"].toString() == cwd)
                    m_threads.upsert(thread, true);
            }
        }
        if (m_state["threadId"].toString().isEmpty() && !m_state["archived"].toBool()) {
            const auto saved = m_settings->draft("last-thread-" + scope());
            for (auto v : r["data"].toArray())
                if (v.toObject()["id"].toString() == saved) {
                    selectThread(saved);
                    break;
                }
        }
        m_state["threadsCursor"] = r["nextCursor"].toString();
        Q_EMIT changed();
    });
}
void AppController::newThread() {
    if (m_state["cwd"].toString().isEmpty() || m_state["loading"].toBool())
        return;
    m_state["loading"] = true;
    m_state["archived"] = false;
    Q_EMIT changed();
    QJsonObject p{{"cwd", m_state["cwd"].toString()}, {"experimentalRawEvents", false}};
    if (!m_state["projectId"].toString().isEmpty())
        p["projectId"] = m_state["projectId"].toString();
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
        Q_EMIT changed();
    });
}
void AppController::selectThread(const QString &id) {
    if (m_git->state()["busy"].toBool()) {
        Q_EMIT error(i18n("Wait for the Git operation to finish."));
        return;
    }
    swapAttachments(id);
    m_state["threadId"] = id;
    m_state["loading"] = true;
    m_state["contextPercent"] = -1;
    m_state["turnDiff"] = "";
    m_state["historyCursor"] = "";
    m_state["planReady"] = false;
    m_state["proposedPlan"] = "";
    m_settings->saveDraft("last-thread-" + scope(), id);
    m_messages.reset();
    m_turn.clear();
    m_historyLoading = false;
    Q_EMIT changed();
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
            Q_EMIT changed();
        });
        return;
    }
    call("thread/resume", {{"threadId", id}, {"excludeTurns", true}}, [this, id](auto r, auto e) {
        if (id != m_state["threadId"].toString())
            return;
        m_state["loading"] = false;
        if (!e.isEmpty()) {
            Q_EMIT changed();
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
        // Unassigned sessions keep their list; the folder only drives the Git panel.
        if (!m_state["unassigned"].toBool()) {
            for (auto v : m_projects.json())
                if (v.toObject()["path"].toString() == cwd) {
                    m_state["projectId"] = v.toObject()["id"].toString();
                    m_state["projectName"] = v.toObject()["name"].toString();
                    break;
                }
            loadThreads();
        }
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
    Q_EMIT changed();
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
        Q_EMIT changed();
    });
}
void AppController::renameThread(const QString &name) {
    if (name.trimmed().isEmpty())
        return;
    call("thread/name/set", {{"threadId", m_state["threadId"].toString()}, {"name", name}},
         [this, name](auto, auto e) {
             if (e.isEmpty()) {
                 m_state["title"] = name;
                 Q_EMIT changed();
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
            Q_EMIT changed();
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
        Q_EMIT changed();
        return;
    }
    if (!model.isEmpty()) {
        m_state["model"] = model;
        Q_EMIT changed();
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
        Q_EMIT changed();
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
                Q_EMIT changed();
            }
        });
    }
}
QJsonArray AppController::input(const QString &text) const {
    QString body = text;
    QStringList files;
    QJsonArray images;
    for (auto v : m_attachments.json()) {
        const auto attachment = v.toObject();
        if (attachment["kind"] == "image")
            images.append(QJsonObject{{"type", "localImage"}, {"path", attachment["path"]}});
        else
            files << attachment["path"].toString();
    }
    // Codex has no input type for arbitrary files; like `@path` in the CLI, the path in the text is the
    // reference.
    if (!files.isEmpty()) {
        if (!body.trimmed().isEmpty())
            body += "\n\n";
        body += (files.size() == 1 ? "Attached file:\n- " : "Attached files:\n- ") + files.join("\n- ");
    }
    QJsonArray result;
    if (!body.trimmed().isEmpty())
        result.append(QJsonObject{{"type", "text"}, {"text", body}});
    for (auto image : images)
        result.append(image);
    return result;
}
void AppController::swapAttachments(const QString &next) {
    const auto current = m_state["threadId"].toString();
    if (current == next)
        return;
    if (m_attachments.count() > 0)
        m_stashedAttachments.insert(current, m_attachments.json());
    else
        m_stashedAttachments.remove(current);
    m_attachments.reset(m_stashedAttachments.take(next));
}
void AppController::addAttachment(const QString &path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    for (auto v : m_attachments.json())
        if (v.toObject()["path"].toString() == canonical)
            return;
    static const QStringList imageTypes{"image/png", "image/jpeg", "image/gif", "image/webp"};
    const auto mime = QMimeDatabase().mimeTypeForFile(canonical);
    const bool folder = info.isDir();
    m_attachments.upsert({{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                          {"path", canonical},
                          {"name", info.fileName()},
                          {"kind", folder                             ? "folder"
                                   : imageTypes.contains(mime.name()) ? "image"
                                                                      : "file"},
                          {"icon", mime.iconName()},
                          {"url", QUrl::fromLocalFile(canonical).toString(QUrl::FullyEncoded)},
                          {"sizeText", folder ? QString() : QLocale().formattedDataSize(info.size())}});
}
void AppController::attachFiles(const QList<QUrl> &files) {
    constexpr int limit = 20;
    for (const auto &url : files) {
        const auto path = url.toLocalFile();
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (m_attachments.count() >= limit) {
            Q_EMIT error(i18np("You can attach at most %1 file at once.",
                               "You can attach at most %1 files at once.", limit));
            return;
        }
        addAttachment(path);
    }
}
bool AppController::attachFromMime(const QMimeData *mime) {
    if (!mime || m_state["threadId"].toString().isEmpty() || m_state["archived"].toBool())
        return false;
    if (mime->hasUrls()) {
        QList<QUrl> local;
        for (const auto &url : mime->urls())
            if (url.isLocalFile())
                local << url;
        if (!local.isEmpty()) {
            attachFiles(local);
            return true;
        }
    }
    // A copied selection often carries a rendered image next to its text; the text wins then.
    if (mime->hasImage() && !mime->hasText()) {
        const auto image = qvariant_cast<QImage>(mime->imageData());
        if (image.isNull())
            return false;
        const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attachments");
        // Sent images stay referenced by their history, so they are kept for a long time.
        const auto cutoff = QDateTime::currentDateTime().addDays(-60);
        for (const auto &old : dir.entryInfoList(QDir::Files))
            if (old.lastModified() < cutoff)
                QFile::remove(old.absoluteFilePath());
        const auto path =
            dir.filePath("clipboard-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" +
                         QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png");
        if (!QDir().mkpath(dir.path()) || !image.save(path, "PNG")) {
            Q_EMIT error(i18n("Could not save the pasted image."));
            return true;
        }
        addAttachment(path);
        return true;
    }
    return false;
}
bool AppController::pasteAttachments() {
    return attachFromMime(QGuiApplication::clipboard()->mimeData());
}
void AppController::removeAttachment(const QString &id) {
    m_attachments.remove(id);
}
bool AppController::send(const QString &text) {
    if ((text.trimmed().isEmpty() && m_attachments.count() == 0) || m_state["archived"].toBool())
        return false;
    if (text.startsWith('/')) {
        bool ok = slash(text);
        if (ok)
            Q_EMIT messageAccepted(text, m_state["threadId"].toString());
        return ok;
    }
    if (!m_rpc->ready() || m_state["threadId"].toString().isEmpty() || m_sending ||
        m_state["loading"].toBool())
        return false;
    bool busy = m_state["busy"].toBool();
    if (busy && m_turn.isEmpty()) {
        Q_EMIT error(i18n("The active turn is not known yet. Try again in a moment."));
        return false;
    }
    auto id = m_state["threadId"].toString();
    auto p = busy ? QJsonObject{} : options();
    p["threadId"] = id;
    p["input"] = input(text);
    QStringList sentAttachments;
    for (auto a : m_attachments.json())
        sentAttachments << a.toObject()["id"].toString();
    if (busy)
        p["expectedTurnId"] = m_turn;
    m_sending = true;
    const bool implementing = std::exchange(m_implementingPlan, false);
    call(busy ? "turn/steer" : "turn/start", p,
         [this, id, text, implementing, sentAttachments](auto r, auto e) {
             m_sending = false;
             if (e.isEmpty()) {
                 // Only what was sent goes away; files added while the request ran stay for the next message.
                 for (const auto &attachment : sentAttachments)
                     m_attachments.remove(attachment);
                 if (m_stashedAttachments.contains(id)) {
                     QJsonArray rest;
                     for (auto v : m_stashedAttachments.value(id))
                         if (!sentAttachments.contains(v.toObject()["id"].toString()))
                             rest.append(v);
                     m_stashedAttachments.insert(id, rest);
                 }
                 m_newThreads.remove(id);
                 if (m_settings->draft(id) == text)
                     m_settings->saveDraft(id, "");
                 Q_EMIT messageAccepted(text, id);
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
             Q_EMIT changed();
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
    Q_EMIT changed();
    return accepted;
}
void AppController::keepPlanning() {
    m_state["planReady"] = false;
    Q_EMIT changed();
}
bool AppController::slash(const QString &text) {
    auto cmd = text.section(' ', 0, 0).toLower(), arg = text.section(' ', 1).trimmed();
    if (cmd == "/plan")
        setOption("plan", !m_state["plan"].toBool());
    else if (cmd == "/fast") {
        if (!m_state["fastAvailable"].toBool())
            Q_EMIT error(i18n("Fast mode is not available for this model."));
        else
            setOption("fast", !m_state["fast"].toBool());
    } else if (cmd == "/new")
        newThread();
    else if (cmd == "/resume")
        showUnassigned();
    else if (cmd == "/model") {
        if (!arg.isEmpty())
            setOption("model", arg);
        else
            Q_EMIT navigate("model");
    } else if (cmd == "/permissions")
        Q_EMIT navigate("permissions");
    else if (cmd == "/mcp") {
        refreshMcp();
        Q_EMIT navigate("settings");
    } else if (cmd == "/status") {
        refresh();
        Q_EMIT navigate("settings");
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
        Q_EMIT error(i18n("Unknown command: %1", cmd));
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
        Q_EMIT changed();
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
namespace {
// Pictures arrive as base64 inside the history. Keeping them in the message rows would hold every screenshot
// of a long chat in memory, so each one is stored once as a file and shown by URL instead.
QString storeImage(const QString &dataUrl) {
    static const QRegularExpression header("^data:(image/[a-z0-9.+-]+);base64,",
                                           QRegularExpression::CaseInsensitiveOption);
    const auto match = header.match(dataUrl);
    if (!match.hasMatch())
        return {};
    const auto bytes = QByteArray::fromBase64(dataUrl.mid(match.capturedLength()).toLatin1());
    if (bytes.isEmpty())
        return {};
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images");
    static bool pruned = false;
    if (!std::exchange(pruned, true)) {
        // The history can recreate any of these, so the cache stays bounded by age and total size.
        auto files = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
        qint64 total = 0;
        for (const auto &file : files)
            total += file.size();
        const auto cutoff = QDateTime::currentDateTime().addDays(-30);
        for (const auto &file : files)
            if (file.lastModified() < cutoff || total > (1LL << 30)) {
                total -= file.size();
                QFile::remove(file.absoluteFilePath());
            }
    }
    const auto suffix = QMimeDatabase().mimeTypeForName(match.captured(1)).preferredSuffix();
    const auto path = dir.filePath(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex() + "." +
                                   (suffix.isEmpty() ? "img" : suffix));
    if (!QFileInfo::exists(path)) {
        QSaveFile file(path);
        if (!QDir().mkpath(dir.path()) || !file.open(QIODevice::WriteOnly) ||
            file.write(bytes) != bytes.size() || !file.commit())
            return {};
    }
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}
} // namespace
void AppController::item(QJsonObject o, bool prepend) {
    auto type = o["type"].toString();
    QString text = o["text"].toString();
    // Pictures shown in the chat: local files by URL, embedded ones as data URLs.
    QJsonArray images;
    // Returns the URL the picture is shown from; embedded data is moved out of the row.
    auto addImage = [&images](const QString &source, const QString &name = {}) {
        const auto url = source.startsWith("data:image/") ? storeImage(source)
                         : source.startsWith("file:")     ? source
                                                          : QString();
        if (!url.isEmpty())
            images.append(QJsonObject{{"src", url}, {"name", name}});
        return url;
    };
    auto fileImage = [&addImage](const QString &path) {
        addImage(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), QFileInfo(path).fileName());
    };
    if (type == "userMessage") {
        QJsonArray content;
        for (auto v : o["content"].toArray()) {
            auto part = v.toObject();
            text += part["text"].toString();
            if (part["type"] == "localImage")
                fileImage(part["path"].toString());
            else if (part["type"] == "image") {
                const auto url = part["url"].toString();
                const auto shown = addImage(url);
                if (url.startsWith("data:"))
                    part["url"] = shown;
            }
            content.append(part);
        }
        o["content"] = content;
    } else if (type == "imageView") {
        text = o["path"].toString();
        fileImage(text);
    } else if (type == "imageGeneration") {
        const auto saved = o["savedPath"].toString();
        text = o["revisedPrompt"].toString(saved);
        if (!saved.isEmpty())
            fileImage(saved);
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
    } else if (type == "mcpToolCall") {
        // Base64 pictures would drown the text, so they are shown as images and left out of it.
        auto result = o["result"].toObject();
        QJsonArray content;
        for (auto v : result["content"].toArray()) {
            auto part = v.toObject();
            if (part["type"] == "image" && part.contains("data")) {
                part["url"] = addImage("data:" + part["mimeType"].toString("image/png") + ";base64," +
                                       part["data"].toString());
                part.remove("data");
            }
            content.append(part);
        }
        if (result.contains("content")) {
            result["content"] = content;
            o["result"] = result;
        }
        text = o["server"].toString() + " / " + o["tool"].toString() + "\n" +
               QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
    } else if (type == "dynamicToolCall" && o["contentItems"].isArray()) {
        QStringList lines;
        QJsonArray parts;
        for (auto v : o["contentItems"].toArray()) {
            auto part = v.toObject();
            if (part["type"] == "inputText")
                lines << part["text"].toString();
            else if (part["type"] == "inputImage")
                part["imageUrl"] = addImage(part["imageUrl"].toString());
            parts.append(part);
        }
        o["contentItems"] = parts;
        text = o["tool"].toString() + "\n" + lines.join("\n");
    } else if (type == "contextCompaction")
        text = i18n("Context compacted");
    else if (text.isEmpty())
        text = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
    o["text"] = text;
    if (!images.isEmpty())
        o["images"] = images;
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
            Q_EMIT error(p["error"].toString());
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
        Q_EMIT changed();
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
        Q_EMIT changed();
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
        Q_EMIT changed();
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
            Q_EMIT error(e["message"].toString());
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
        Q_EMIT error(p["error"].toObject()["message"].toString());
    if (m_state != before)
        Q_EMIT changed();
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
