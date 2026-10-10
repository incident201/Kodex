/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rpcclient.h"
#include <KLocalizedString>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtEndian>
QByteArray WebSocketCodec::frame(const QByteArray &data, quint8 opcode) {
    QByteArray out;
    out.append(char(0x80 | opcode));
    quint64 n = data.size();
    if (n < 126)
        out.append(char(0x80 | n));
    else if (n <= 65535) {
        out.append(char(0x80 | 126));
        quint16 be = qToBigEndian(quint16(n));
        out.append(reinterpret_cast<char *>(&be), 2);
    } else {
        out.append(char(0x80 | 127));
        quint64 be = qToBigEndian(n);
        out.append(reinterpret_cast<char *>(&be), 8);
    }
    quint32 mask = QRandomGenerator::global()->generate();
    QByteArray key(reinterpret_cast<char *>(&mask), 4);
    out += key;
    for (qsizetype i = 0; i < data.size(); ++i)
        out.append(data[i] ^ key[i % 4]);
    return out;
}
int WebSocketCodec::take(QByteArray &b, QByteArray &p, quint8 &opcode, bool &final) {
    if (b.size() < 2)
        return 0;
    auto a = quint8(b[0]), c = quint8(b[1]);
    if ((a & 0x70) || (c & 0x80))
        return -1;
    final = a & 0x80;
    opcode = a & 15;
    quint64 n = c & 127;
    qsizetype head = 2;
    if (n == 126) {
        if (b.size() < 4)
            return 0;
        n = qFromBigEndian<quint16>(b.constData() + 2);
        head = 4;
    } else if (n == 127) {
        if (b.size() < 10)
            return 0;
        n = qFromBigEndian<quint64>(b.constData() + 2);
        head = 10;
    }
    if (n > 64 * 1024 * 1024 || (opcode >= 8 && (!final || n > 125)))
        return -1;
    if (quint64(b.size()) < n + head)
        return 0;
    p = b.mid(head, n);
    b.remove(0, head + n);
    return 1;
}
RpcClient::RpcClient(Settings *s, QObject *p) : QObject(p), m_settings(s) {
    m_status = i18n("Not connected");
    m_retry.setSingleShot(true);
    m_handshakeTimeout.setSingleShot(true);
    m_handshakeTimeout.setInterval(15000);
    connect(&m_retry, &QTimer::timeout, this, &RpcClient::connectServer);
    connect(&m_handshakeTimeout, &QTimer::timeout, this,
            [this] { fail(i18n("Codex connection timed out.")); });
    connect(&m_process, &QProcess::started, this, [this] {
        QByteArray raw;
        for (int i = 0; i < 4; ++i) {
            auto n = QRandomGenerator::global()->generate();
            raw.append(reinterpret_cast<char *>(&n), 4);
        }
        m_key = raw.toBase64();
        m_process.write("GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: "
                        "Upgrade\r\nSec-WebSocket-Key: " +
                        m_key + "\r\nSec-WebSocket-Version: 13\r\n\r\n");
        m_handshakeTimeout.start();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &RpcClient::read);
    connect(&m_process, &QProcess::readyReadStandardError, this,
            [this] { m_process.readAllStandardError(); }); // Never log raw server payloads.
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (!m_stopping && e == QProcess::FailedToStart)
            fail(i18n("Could not start Codex. Check the executable path."));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this] {
        if (!m_stopping)
            fail(i18n("Disconnected from Codex. Reconnecting…"));
    });
}
RpcClient::~RpcClient() {
    disconnectServer();
}
void RpcClient::disconnectServer() {
    m_stopping = true;
    m_retry.stop();
    m_handshakeTimeout.stop();
    m_ready = false;
    m_upgraded = false;
    m_buffer.clear();
    m_fragment.clear();
    m_pending.clear();
    m_status = i18n("Not connected");
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(500))
            m_process.kill();
    }
    Q_EMIT disconnected();
    Q_EMIT stateChanged();
}
void RpcClient::connectServer() {
    if (m_ready || m_starting || m_process.state() != QProcess::NotRunning)
        return;
    if (m_settings->executable().isEmpty()) {
        m_status = i18n("Codex executable not found.");
        Q_EMIT stateChanged();
        return;
    }
    m_stopping = false;
    m_starting = true;
    m_status = i18n("Connecting to Codex…");
    Q_EMIT stateChanged();
    auto *p = new QProcess(this);
    p->setProcessEnvironment(m_settings->environment());
    auto complete = [this, p](bool ok) {
        p->deleteLater();
        m_starting = false;
        if (m_stopping)
            return;
        if (ok)
            openProxy();
        else
            fail(i18n("Could not start the Codex daemon."));
    };
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [complete](int c, QProcess::ExitStatus s) { complete(c == 0 && s == QProcess::NormalExit); });
    connect(p, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            complete(false);
    });
    p->start(m_settings->executable(),
             m_settings->arguments() + QStringList{"app-server", "daemon", "start"});
    QTimer::singleShot(15000, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}
void RpcClient::openProxy() {
    m_buffer.clear();
    m_fragment.clear();
    m_upgraded = false;
    m_process.setProcessEnvironment(m_settings->environment());
    m_process.start(m_settings->executable(), m_settings->arguments() + QStringList{"app-server", "proxy"});
}
void RpcClient::fail(const QString &reason) {
    if (m_stopping)
        return;
    m_handshakeTimeout.stop();
    m_stopping = true;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
    m_stopping = false;
    m_ready = false;
    m_upgraded = false;
    m_buffer.clear();
    m_fragment.clear();
    m_status = reason;
    auto pending = std::exchange(m_pending, {});
    for (const auto &cb : pending)
        if (cb)
            cb({}, {{"message", i18n("Connection lost; the operation outcome is unknown.")}});
    Q_EMIT disconnected();
    Q_EMIT stateChanged();
    m_retry.start(m_backoff);
    m_backoff = qMin(m_backoff * 2, 30000);
}
void RpcClient::send(const QJsonObject &j) {
    if (m_upgraded && !m_stopping && m_process.state() == QProcess::Running)
        m_process.write(WebSocketCodec::frame(QJsonDocument(j).toJson(QJsonDocument::Compact)));
}
QString RpcClient::request(const QString &m, const QJsonObject &p, Callback cb) {
    if (!m_ready && m != "initialize") {
        if (cb)
            cb({}, {{"message", i18n("Codex is not connected.")}});
        return {};
    }
    QString id = QString::number(m_next++);
    m_pending.insert(id, std::move(cb));
    // Unit-parameter methods use JSON null in the generated protocol schema.
    const bool unit = QStringList{"memory/reset",
                                  "rollout/compress",
                                  "remoteControl/status/read",
                                  "config/mcpServer/reload",
                                  "windowsSandbox/readiness",
                                  "account/logout",
                                  "account/workspaceMessages/read",
                                  "externalAgentConfig/import/readHistories",
                                  "configRequirements/read"}
                          .contains(m);
    send({{"id", id}, {"method", m}, {"params", unit ? QJsonValue() : QJsonValue(p)}});
    QTimer::singleShot(60000, this, [this, id] {
        if (!m_pending.contains(id))
            return;
        auto cb = m_pending.take(id);
        if (cb)
            cb({},
               {{"message",
                 i18n("The request timed out; its outcome is unknown. Check the task before retrying.")}});
    });
    return id;
}
void RpcClient::respond(const QJsonValue &id, const QJsonObject &r) {
    send({{"id", id}, {"result", r}});
}
void RpcClient::reject(const QJsonValue &id, const QString &s) {
    send({{"id", id}, {"error", QJsonObject{{"code", -32601}, {"message", s}}}});
}
void RpcClient::read() {
    if (m_stopping) {
        m_process.readAllStandardOutput();
        return;
    }
    m_buffer += m_process.readAllStandardOutput();
    if (!m_upgraded) {
        auto end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 16384)
                fail(i18n("Invalid Codex handshake."));
            return;
        }
        auto header = m_buffer.left(end);
        auto expected =
            QCryptographicHash::hash(m_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1)
                .toBase64();
        if (!header.startsWith("HTTP/1.1 101 ") || !header.contains(expected)) {
            fail(i18n("Invalid Codex handshake."));
            return;
        }
        m_buffer.remove(0, end + 4);
        m_upgraded = true;
        request("initialize",
                {{"clientInfo", QJsonObject{{"name", "kodex"}, {"title", "Kodex"}, {"version", "0.1.0"}}},
                 {"capabilities", QJsonObject{{"experimentalApi", true}}}},
                [this](auto r, auto e) {
                    Q_UNUSED(r);
                    if (!e.isEmpty()) {
                        fail(e["message"].toString());
                        return;
                    }
                    send({{"method", "initialized"}, {"params", QJsonObject{}}});
                    m_handshakeTimeout.stop();
                    m_ready = true;
                    m_backoff = 1000;
                    m_status = i18n("Connected");
                    Q_EMIT stateChanged();
                    Q_EMIT connected();
                });
    }
    while (true) {
        QByteArray payload;
        quint8 op = 0;
        bool fin = false;
        int rc = WebSocketCodec::take(m_buffer, payload, op, fin);
        if (!rc)
            return;
        if (rc < 0) {
            fail(i18n("Invalid WebSocket frame."));
            return;
        }
        if (op == 8) {
            fail(i18n("Codex closed the connection."));
            return;
        }
        if (op == 9) {
            m_process.write(WebSocketCodec::frame(payload, 10));
            continue;
        }
        if (op == 10)
            continue;
        if (op != 0 && op != 1) {
            fail(i18n("Unsupported WebSocket message."));
            return;
        }
        m_fragment += payload;
        if (m_fragment.size() > 64 * 1024 * 1024) {
            fail(i18n("Codex message is too large."));
            return;
        }
        if (!fin)
            continue;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(std::exchange(m_fragment, {}), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            fail(i18n("Invalid JSON from Codex."));
            return;
        }
        ingest(doc.object());
        // A callback may have dropped the connection; remaining bytes belong to the old stream.
        if (!m_upgraded)
            return;
    }
}
void RpcClient::ingest(const QJsonObject &j) {
    if (j.contains("method")) {
        auto m = j["method"].toString();
        auto p = j["params"].toObject();
        if (j.contains("id"))
            Q_EMIT serverRequest(j["id"], m, p);
        else
            Q_EMIT notification(m, p);
    } else if (j.contains("id")) {
        auto cb = m_pending.take(j["id"].toVariant().toString());
        if (cb)
            cb(j["result"].toObject(), j["error"].toObject());
    }
}
