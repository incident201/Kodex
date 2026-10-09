#pragma once
#include <KSharedConfig>
#include <QObject>
#include <QProcessEnvironment>
#include <QVariantMap>
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
  public:
    explicit Settings(QObject *parent = nullptr, const QString &configFile = "kodexrc");
    QVariantMap values() const { return m_values; }
    QString value(const QString &key) const { return m_values.value(key).toString(); }
    Q_INVOKABLE bool save(const QVariantMap &values, const QString &proxyPassword = {});
    Q_INVOKABLE QString draft(const QString &id) const;
    Q_INVOKABLE void saveDraft(const QString &id, const QString &text);
    QProcessEnvironment environment(const QString &overrideProxy = {}) const;
    static QProcessEnvironment proxyEnvironment(QProcessEnvironment env, const QString &mode,
                                                const QString &url, const QString &noProxy);
    static QString redact(QString text);
    QString executable() const;
    QStringList arguments() const;
  signals:
    void changed();
    void error(const QString &message);

  private:
    KSharedConfig::Ptr m_config;
    QVariantMap m_values;
    mutable QString m_password;
};
