/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
class ListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    explicit ListModel(QObject *parent = nullptr);
    int count() const { return m_rows.size(); }
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : count(); }
    QVariant data(const QModelIndex &i, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "record"}}; }
    Q_INVOKABLE QVariantMap get(int i) const;
    void reset(const QJsonArray &rows = {});
    void upsert(QJsonObject row, bool prepend = false);
    void delta(const QString &id, const QString &text, const QString &type);
    void remove(const QString &id);
    QJsonArray json() const;
  Q_SIGNALS:
    void countChanged();

  private:
    int find(const QString &id) const;
    void reindex();
    void flush();
    QList<QJsonObject> m_rows;
    // id -> row + m_front; prepending decrements m_front instead of shifting every entry.
    QHash<QString, qsizetype> m_index;
    qsizetype m_front = 0;
    // Streaming deltas update rows at once but notify views at a bounded rate.
    QSet<QString> m_dirty;
    QTimer m_flush;
};
