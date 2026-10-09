#pragma once
#include "rpcclient.h"
#include "settings.h"
#include <QObject>
#include <QTemporaryDir>
#include <functional>
class Maintenance : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
  public:
    explicit Maintenance(Settings *, QObject *p = nullptr);
    void setRpc(RpcClient *rpc);
    QVariantMap state() const { return m_state; }
    Q_INVOKABLE void versions();
    Q_INVOKABLE void update();
    Q_INVOKABLE void remote(bool enabled);
    Q_INVOKABLE void pair();
    Q_INVOKABLE void restart();
  signals:
    void changed();
    void error(const QString &);
    void restarting();
    void restarted();

  private:
    using Done = std::function<void(int, const QByteArray &, const QByteArray &)>;
    void run(const QString &, const QStringList &, const QProcessEnvironment &, Done, bool log = false);
    void codex(const QStringList &, Done);
    void install(bool fallback = false);
    void appendLog(const QString &);
    Settings *m_settings;
    RpcClient *m_rpc = nullptr;
    QTimer m_pairTimer;
    QJsonObject m_pairCodes;
    qint64 m_pairExpires = 0;
    void refreshRemote();
    QTemporaryDir m_temp;
    QVariantMap m_state{{"busy", false}, {"version", ""}, {"daemonVersion", ""},
                        {"log", ""},     {"pairing", ""}, {"remote", "unknown"}};
};
