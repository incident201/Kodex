#include "listmodel.h"
static QString key(const QJsonObject &row) {
    return row.value("id").toVariant().toString();
}
ListModel::ListModel(QObject *parent) : QAbstractListModel(parent) {
    m_flush.setSingleShot(true);
    m_flush.setInterval(50);
    connect(&m_flush, &QTimer::timeout, this, &ListModel::flush);
}
QVariant ListModel::data(const QModelIndex &i, int role) const {
    return role == Qt::UserRole && i.row() >= 0 && i.row() < count() ? m_rows[i.row()].toVariantMap()
                                                                     : QVariant{};
}
QVariantMap ListModel::get(int i) const {
    return i >= 0 && i < count() ? m_rows[i].toVariantMap() : QVariantMap{};
}
int ListModel::find(const QString &id) const {
    auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? -1 : int(*it - m_front);
}
void ListModel::reindex() {
    m_index.clear();
    m_front = 0;
    // The first row wins for duplicate ids, as with the previous linear search.
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i)
        m_index.insert(key(m_rows[i]), i);
}
void ListModel::flush() {
    m_flush.stop();
    for (const auto &id : std::exchange(m_dirty, {})) {
        int i = find(id);
        if (i >= 0)
            emit dataChanged(index(i), index(i));
    }
}
void ListModel::reset(const QJsonArray &rows) {
    beginResetModel();
    m_rows.clear();
    m_dirty.clear();
    m_flush.stop();
    for (auto r : rows)
        m_rows << r.toObject();
    reindex();
    endResetModel();
    emit countChanged();
}
void ListModel::upsert(QJsonObject row, bool prepend) {
    const auto id = key(row);
    if (int i = find(id); i >= 0) {
        auto &target = m_rows[i];
        for (auto it = row.begin(); it != row.end(); ++it)
            target[it.key()] = it.value();
        m_dirty.remove(id);
        emit dataChanged(index(i), index(i));
        return;
    }
    int pos = prepend ? 0 : count();
    beginInsertRows({}, pos, pos);
    m_rows.insert(pos, row);
    if (prepend)
        --m_front;
    m_index.insert(id, pos + m_front);
    endInsertRows();
    emit countChanged();
}
void ListModel::delta(const QString &id, const QString &text, const QString &type) {
    int i = find(id);
    if (i < 0) {
        upsert({{"id", id}, {"type", type}, {"text", text}});
        return;
    }
    auto &row = m_rows[i];
    row["text"] = row.value("text").toString() + text;
    m_dirty.insert(id);
    if (!m_flush.isActive())
        m_flush.start();
}
void ListModel::remove(const QString &id) {
    int i = find(id);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_rows.removeAt(i);
    m_dirty.remove(id);
    reindex();
    endRemoveRows();
    emit countChanged();
}
QJsonArray ListModel::json() const {
    QJsonArray a;
    for (const auto &r : m_rows)
        a.append(r);
    return a;
}
