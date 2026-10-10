/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include "gitservice.h"
#include "listmodel.h"
#include "rpcclient.h"
#include <QMimeData>
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(ListModel *projects READ projects CONSTANT)
    Q_PROPERTY(ListModel *threads READ threads CONSTANT)
    Q_PROPERTY(ListModel *messages READ messages CONSTANT)
    Q_PROPERTY(ListModel *requests READ requests CONSTANT)
    Q_PROPERTY(ListModel *models READ models CONSTANT)
    Q_PROPERTY(ListModel *mcp READ mcp CONSTANT)
    Q_PROPERTY(ListModel *attachments READ attachments CONSTANT)
  public:
    AppController(RpcClient *, Settings *, GitService *, QObject *p = nullptr);
    QVariantMap state() const { return m_state; }
    ListModel *projects() { return &m_projects; }
    ListModel *threads() { return &m_threads; }
    ListModel *messages() { return &m_messages; }
    ListModel *requests() { return &m_requests; }
    ListModel *models() { return &m_models; }
    ListModel *mcp() { return &m_mcp; }
    ListModel *attachments() { return &m_attachments; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addProject(const QUrl &folder);
    Q_INVOKABLE void selectProject(const QString &id);
    Q_INVOKABLE void showUnassigned();
    Q_INVOKABLE void renameProject(const QString &name);
    Q_INVOKABLE void loadThreads(bool archived = false, const QString &cursor = {});
    Q_INVOKABLE void newThread();
    Q_INVOKABLE void selectThread(const QString &id);
    Q_INVOKABLE void renameThread(const QString &name);
    Q_INVOKABLE void archiveThread(bool archived = true);
    Q_INVOKABLE void loadHistory(bool older = true);
    Q_INVOKABLE bool send(const QString &text);
    // Files and folders chosen in a dialog or dropped on the window; images are sent as images.
    Q_INVOKABLE void attachFiles(const QList<QUrl> &files);
    // Attaches clipboard files or a clipboard-only image; false leaves the paste to the text editor.
    Q_INVOKABLE bool pasteAttachments();
    bool attachFromMime(const QMimeData *mime);
    Q_INVOKABLE void removeAttachment(const QString &id);
    Q_INVOKABLE void interrupt();
    Q_INVOKABLE bool implementPlan();
    Q_INVOKABLE void keepPlanning();
    Q_INVOKABLE void setOption(const QString &name, const QVariant &value);
    Q_INVOKABLE void login(bool device = false);
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refreshMcp();
    Q_INVOKABLE void loginMcp(const QString &name);
    Q_INVOKABLE void reloadMcp();
    Q_INVOKABLE void answer(const QString &id, const QVariant &decision, const QVariantMap &values = {});
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void openLink(const QString &url);
    Q_INVOKABLE void copy(const QString &text);
  Q_SIGNALS:
    void changed();
    void error(const QString &);
    void navigate(const QString &page);
    void messageAccepted(const QString &text, const QString &threadId);

  private:
    void call(const QString &, const QJsonObject &params = {}, RpcClient::Callback callback = {});
    void notify(const QString &, const QJsonObject &);
    void receiveRequest(const QJsonValue &, const QString &, const QJsonObject &);
    void item(QJsonObject, bool prepend = false);
    void applySessionResponse(const QJsonObject &response);
    void clearThread();
    void loadProjects(const QString &cursor = {});
    void loadModels(const QString &cursor = {});
    void loadMcp(const QString &cursor = {});
    void chooseModel(const QString &);
    QJsonArray input(const QString &text) const;
    void swapAttachments(const QString &nextThread);
    void addAttachment(const QString &path);
    QString scope() const;
    QJsonObject options() const;
    bool slash(const QString &);
    RpcClient *m_rpc;
    Settings *m_settings;
    GitService *m_git;
    ListModel m_projects, m_threads, m_messages, m_requests, m_models, m_mcp, m_attachments;
    // Attachments wait with the thread they were added to while another thread is open.
    QHash<QString, QJsonArray> m_stashedAttachments;
    QVariantMap m_state{{"projectId", ""},     {"cwd", ""},
                        {"threadId", ""},      {"title", ""},
                        {"busy", false},       {"model", ""},
                        {"effort", ""},        {"efforts", QStringList{}},
                        {"plan", false},       {"permission", "inherit"},
                        {"fast", false},       {"fastAvailable", false},
                        {"account", ""},       {"contextPercent", -1},
                        {"limits", ""},        {"turnDiff", ""},
                        {"historyCursor", ""}, {"threadsCursor", ""},
                        {"archived", false},   {"loginInfo", ""},
                        {"loading", false},    {"unassigned", false}};
    QString m_turn, m_loginId;
    bool m_sending = false, m_historyLoading = false;
    bool m_implementingPlan = false;
    QHash<QString, QJsonObject> m_requestData;
    QHash<QString, QJsonValue> m_requestIds;
    QJsonObject m_inheritedPermissions;
    QHash<QString, QJsonObject> m_newThreads;
    quint64 m_optionRevision = 0;
};
