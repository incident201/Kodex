#pragma once
#include "settings.h"
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
// WebSocket framing over `codex app-server proxy` (the proxy is a byte tunnel,
// not the JSONL transport offered by a standalone app-server --stdio).
class WebSocketCodec {
  public:
    static QByteArray frame(const QByteArray &data, quint8 opcode = 1);
    static int take(QByteArray &buffer, QByteArray &payload, quint8 &opcode, bool &final);
};
class RpcClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  public:
    using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;
    explicit RpcClient(Settings *, QObject *parent = nullptr);
    ~RpcClient() override;
    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    Q_INVOKABLE void connectServer();
    void disconnectServer();
    QString request(const QString &, const QJsonObject &params = {}, Callback cb = {});
    void respond(const QJsonValue &id, const QJsonObject &result);
    void reject(const QJsonValue &id, const QString &reason);
    void ingest(const QJsonObject &message);
  signals:
    void stateChanged();
    void connected();
    void disconnected();
    void notification(const QString &, const QJsonObject &);
    void serverRequest(const QJsonValue &, const QString &, const QJsonObject &);
    void error(const QString &);

  private:
    void send(const QJsonObject &);
    void read();
    void openProxy();
    void fail(const QString &);
    Settings *m_settings;
    QProcess m_process;
    QTimer m_retry, m_handshakeTimeout;
    QByteArray m_buffer, m_fragment, m_key;
    QHash<QString, Callback> m_pending;
    quint64 m_next = 1;
    bool m_ready = false, m_upgraded = false, m_stopping = false, m_starting = false;
    int m_backoff = 1000;
    QString m_status;
};
