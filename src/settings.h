/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <KSharedConfig>
#include <QObject>
#include <QProcessEnvironment>
#include <QUrl>
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
    // Absolute path that `program` (a PATH name or a path) would run, or empty if it cannot run.
    Q_INVOKABLE static QString resolveProgram(const QString &program);
    // Local path of a file chosen in a QML dialog.
    Q_INVOKABLE static QString localPath(const QUrl &file);
    // Folder where a file dialog for `program` should open: next to its current binary, else home.
    Q_INVOKABLE static QUrl dialogFolder(const QString &program);
    // A folder path with the home directory shown as "~".
    Q_INVOKABLE static QString shortPath(const QString &path);
  Q_SIGNALS:
    void changed();
    void error(const QString &message);

  private:
    KSharedConfig::Ptr m_config;
    QVariantMap m_values;
    mutable QString m_password;
};
