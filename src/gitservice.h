#pragma once
#include "listmodel.h"
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
class GitService : public QObject {
    Q_OBJECT
    Q_PROPERTY(ListModel *files READ files CONSTANT)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
  public:
    explicit GitService(QObject *p = nullptr);
    ListModel *files() { return &m_files; }
    QVariantMap state() const { return m_state; }
    void setDirectory(const QString &);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void diff(const QString &path, bool staged = false);
    Q_INVOKABLE void stage(const QString &path, bool staged);
    Q_INVOKABLE void commit(const QString &message);
    Q_INVOKABLE void push();
    Q_INVOKABLE void sync();
    static QJsonArray parseStatus(const QByteArray &);
  signals:
    void changed();
    void committed();
    void error(const QString &);

  private:
    using Done = std::function<void(int, const QByteArray &, const QByteArray &)>;
    void run(const QStringList &, Done);
    void mutate(const QStringList &);
    void finish(const QString &error = {});
    bool begin();
    QString m_dir;
    ListModel m_files;
    QVariantMap m_state{
        {"busy", false}, {"repository", false}, {"branch", ""}, {"diff", ""}, {"upstream", false}};
    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    bool m_reading = false, m_refreshQueued = false;
    quint64 m_generation = 0;
};
