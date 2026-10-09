#include "maintenance.h"
#include <KLocalizedString>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
Maintenance::Maintenance(Settings *s, QObject *p) : QObject(p), m_settings(s) {}
void Maintenance::setRpc(RpcClient *rpc) {
    m_rpc = rpc;
    connect(rpc, &RpcClient::connected, this, &Maintenance::refreshRemote);
    connect(rpc, &RpcClient::notification, this, [this](const QString &method, const QJsonObject &p) {
        if (method == "remoteControl/status/changed") {
            m_state["remote"] = p["status"].toString();
            m_state["serverName"] = p["serverName"].toString();
            emit changed();
        }
    });
    m_pairTimer.setInterval(3000);
    connect(&m_pairTimer, &QTimer::timeout, this, [this] {
        if (QDateTime::currentSecsSinceEpoch() >= m_pairExpires) {
            m_pairTimer.stop();
            m_pairCodes = {};
            m_state["pairing"] = "";
            m_state["pairingStatus"] = i18n("Pairing code expired.");
            emit changed();
            return;
        }
        if (!m_rpc->ready())
            return;
        m_rpc->request("remoteControl/pairing/status", m_pairCodes, [this](auto r, auto e) {
            if (e.isEmpty() && r["claimed"].toBool()) {
                m_pairTimer.stop();
                m_pairCodes = {};
                m_state["pairing"] = "";
                m_state["pairingStatus"] = i18n("Device paired.");
                emit changed();
            }
        });
    });
}
void Maintenance::refreshRemote() {
    if (!m_rpc || !m_rpc->ready())
        return;
    m_rpc->request("remoteControl/status/read", {}, [this](auto r, auto e) {
        if (e.isEmpty()) {
            m_state["remote"] = r["status"].toString();
            m_state["serverName"] = r["serverName"].toString();
            emit changed();
        }
    });
}
void Maintenance::appendLog(const QString &s) {
    m_state["log"] = (m_state["log"].toString() + Settings::redact(s)).right(100000);
    emit changed();
}
void Maintenance::run(const QString &program, const QStringList &args, const QProcessEnvironment &env,
                      Done cb, bool log) {
    auto *p = new QProcess(this);
    p->setProcessEnvironment(env);
    auto out = std::make_shared<QByteArray>(), err = std::make_shared<QByteArray>();
    auto stdoutPending = std::make_shared<QByteArray>(), stderrPending = std::make_shared<QByteArray>();
    auto logLines = [this, log](QByteArray &pending, const QByteArray &chunk) {
        if (!log)
            return;
        pending += chunk;
        auto end = pending.lastIndexOf('\n');
        if (end >= 0) {
            appendLog(QString::fromUtf8(pending.left(end + 1)));
            pending.remove(0, end + 1);
        }
    };
    connect(p, &QProcess::readyReadStandardOutput, this, [p, out, stdoutPending, logLines] {
        auto b = p->readAllStandardOutput();
        *out += b;
        logLines(*stdoutPending, b);
    });
    connect(p, &QProcess::readyReadStandardError, this, [p, err, stderrPending, logLines] {
        auto b = p->readAllStandardError();
        *err += b;
        logLines(*stderrPending, b);
    });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p, out, err, cb, log, stdoutPending, stderrPending](int c, QProcess::ExitStatus s) {
                *out += p->readAllStandardOutput();
                *err += p->readAllStandardError();
                if (log) {
                    appendLog(QString::fromUtf8(*stdoutPending));
                    appendLog(QString::fromUtf8(*stderrPending));
                }
                p->deleteLater();
                cb(s == QProcess::NormalExit ? c : -1, *out, *err);
            });
    connect(p, &QProcess::errorOccurred, this, [p, cb](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            cb(-1, {}, "Failed to start executable");
        }
    });
    p->start(program, args);
    QTimer::singleShot(300000, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}
void Maintenance::codex(const QStringList &a, Done cb) {
    run(m_settings->executable(), m_settings->arguments() + a, m_settings->environment(), std::move(cb));
}
void Maintenance::versions() {
    codex({"app-server", "daemon", "version"}, [this](int c, auto out, auto err) {
        if (c) {
            emit error(Settings::redact(QString::fromUtf8(err)));
            return;
        }
        auto j = QJsonDocument::fromJson(out).object();
        m_state["version"] = j["cliVersion"].toString();
        m_state["daemonVersion"] = j["appServerVersion"].toString();
        emit changed();
    });
}
void Maintenance::update() {
    if (m_state["busy"].toBool())
        return;
    m_state["log"] = "";
    if (m_settings->value("updateMode") == "builtin") {
        auto path = QFileInfo(m_settings->executable()).canonicalFilePath();
        if (!path.contains("/.codex/packages/standalone/")) {
            emit error(i18n(
                "Built-in updates require a standalone Codex installation. Configure an external updater."));
            return;
        }
    }
    m_state["busy"] = true;
    emit changed();
    if (m_settings->value("updateMode") == "external")
        run(
            m_settings->value("updater"), QProcess::splitCommand(m_settings->value("updaterArguments")),
            m_settings->environment(),
            [this](int c, auto, auto) {
                m_state["busy"] = false;
                appendLog(c ? i18n("\nUpdate failed.\n")
                            : i18n("\nUpdate finished. Restart the daemon to use the new version.\n"));
                versions();
            },
            true);
    else
        install();
}
void Maintenance::install(bool fallback) {
    if (!m_temp.isValid()) {
        m_state["busy"] = false;
        emit error(i18n("Could not create a temporary directory."));
        emit changed();
        return;
    }
    auto env = m_settings->environment(fallback ? m_settings->value("fallbackProxy") : QString());
    auto file = m_temp.filePath("install.sh");
    run(
        "curl",
        {"--fail", "--location", "--silent", "--show-error", "--proto", "=https", "--proto-redir", "=https",
         "--max-time", "90", "--output", file, "https://chatgpt.com/codex/install.sh"},
        env,
        [this, fallback, env, file](int c, auto, auto) {
            if (c) {
                if (!fallback && !m_settings->value("fallbackProxy").isEmpty()) {
                    appendLog(i18n("Trying the fallback proxy…\n"));
                    install(true);
                } else {
                    m_state["busy"] = false;
                    appendLog(i18n("Download failed. The installer was not run.\n"));
                }
                return;
            }
            run(
                "sh", {file}, env,
                [this](int c, auto, auto) {
                    m_state["busy"] = false;
                    appendLog(c ? i18n("Installation failed. Check the log before retrying.\n")
                                : i18n("Update finished. Restart the daemon to use the new version.\n"));
                    versions();
                },
                true);
        },
        true);
}
void Maintenance::remote(bool enabled) {
    if (m_state["busy"].toBool())
        return;
    m_state["busy"] = true;
    emit changed();
    codex({"app-server", "daemon", enabled ? "enable-remote-control" : "disable-remote-control"},
          [this, enabled](int c, auto, auto err) {
              m_state["busy"] = false;
              if (c)
                  emit error(Settings::redact(QString::fromUtf8(err)));
              else
                  m_state["remote"] = enabled ? "enabled" : "disabled";
              emit changed();
              refreshRemote();
          });
}
void Maintenance::pair() {
    if (m_state["busy"].toBool())
        return;
    if (!m_rpc || !m_rpc->ready()) {
        emit error(i18n("Codex is not connected."));
        return;
    }
    m_state["busy"] = true;
    m_state["pairing"] = "";
    m_pairTimer.stop();
    m_pairCodes = {};
    m_state["pairingStatus"] = "";
    emit changed();
    m_rpc->request("remoteControl/pairing/start", {{"manualCode", true}}, [this](auto r, auto err) {
        m_state["busy"] = false;
        if (!err.isEmpty())
            emit error(Settings::redact(err["message"].toString()));
        else {
            m_state["pairing"] = r["manualPairingCode"].toString(r["pairingCode"].toString());
            m_pairCodes = {{"pairingCode", r["pairingCode"]}};
            if (r["manualPairingCode"].isString())
                m_pairCodes["manualPairingCode"] = r["manualPairingCode"];
            m_pairExpires = r["expiresAt"].toInteger();
            if (m_pairExpires > 100000000000LL)
                m_pairExpires /= 1000;
            m_state["pairingStatus"] =
                i18n("Expires at %1",
                     QDateTime::fromSecsSinceEpoch(m_pairExpires).toLocalTime().toString("HH:mm:ss"));
            m_pairTimer.start();
        }
        emit changed();
    });
}
void Maintenance::restart() {
    if (m_state["busy"].toBool())
        return;
    m_state["busy"] = true;
    emit changed();
    emit restarting();
    codex({"app-server", "daemon", "restart"}, [this](int c, auto, auto err) {
        m_state["busy"] = false;
        if (c)
            emit error(Settings::redact(QString::fromUtf8(err)));
        emit changed();
        emit restarted();
        versions();
    });
}
