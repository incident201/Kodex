/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gitservice.h"
#include "settings.h"
#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
GitService::GitService(QObject *p) : QObject(p) {
    m_timer.setInterval(4000);
    connect(&m_timer, &QTimer::timeout, this, &GitService::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { refresh(); });
}
void GitService::setDirectory(const QString &dir) {
    if (m_dir == dir)
        return;
    ++m_generation;
    m_dir = dir;
    m_files.reset();
    m_state["diff"] = "";
    if (dir.isEmpty()) {
        m_state["repository"] = false;
        Q_EMIT changed();
    }
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (QFileInfo(dir).isDir())
        m_watcher.addPath(dir);
    m_timer.start();
    refresh();
}
void GitService::run(const QStringList &args, Done cb) {
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_dir);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_OPTIONAL_LOCKS", "0");
    env.insert("LC_ALL", "C");
    p->setProcessEnvironment(env);
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [p, cb](int code, QProcess::ExitStatus s) {
                auto out = p->readAllStandardOutput(), err = p->readAllStandardError();
                p->deleteLater();
                cb(s == QProcess::NormalExit ? code : -1, out, err);
            });
    connect(p, &QProcess::errorOccurred, this, [p, cb](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            cb(-1, {}, "Could not start Git");
        }
    });
    p->start("git", QStringList{"--no-pager", "--literal-pathspecs", "-c", "core.quotepath=false"} + args);
    QTimer::singleShot(120000, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}
QJsonArray GitService::parseStatus(const QByteArray &data) {
    QJsonArray rows;
    auto parts = data.split('\0');
    for (int i = 0; i < parts.size(); ++i) {
        auto p = parts[i];
        if (p.size() < 4)
            continue;
        QString status = QString::fromLatin1(p.left(2)), path = QString::fromUtf8(p.mid(3)), old;
        if (status.contains('R') || status.contains('C')) {
            if (i + 1 < parts.size())
                old = QString::fromUtf8(parts[++i]);
        }
        rows.append(QJsonObject{{"id", path},
                                {"path", path},
                                {"oldPath", old},
                                {"status", status},
                                {"staged", status[0] != ' ' && status[0] != '?'},
                                {"untracked", status == "??"}});
    }
    return rows;
}
void GitService::refresh() {
    if (m_dir.isEmpty() || m_state["busy"].toBool())
        return;
    if (m_reading) {
        // The running status may predate a change; read again once it finishes.
        m_refreshQueued = true;
        return;
    }
    m_reading = true;
    m_refreshQueued = false;
    auto gen = m_generation;
    run({"status", "--porcelain=v1", "-z", "--untracked-files=all"}, [this, gen](int c, auto out, auto) {
        m_reading = false;
        if (gen != m_generation || m_refreshQueued) {
            refresh();
            return;
        }
        m_state["repository"] = c == 0;
        m_files.reset(c == 0 ? parseStatus(out) : QJsonArray{});
        Q_EMIT changed();
        if (c == 0)
            run({"status", "--porcelain=v2", "--branch", "--untracked-files=no"},
                [this, gen](int, auto out, auto) {
                    if (gen != m_generation)
                        return;
                    QString branch, up;
                    for (const auto &l : out.split('\n')) {
                        if (l.startsWith("# branch.head "))
                            branch = QString::fromUtf8(l.mid(14));
                        if (l.startsWith("# branch.upstream "))
                            up = QString::fromUtf8(l.mid(18));
                    }
                    m_state["branch"] = branch;
                    m_state["upstream"] = !up.isEmpty();
                    m_state["upstreamName"] = up;
                    Q_EMIT changed();
                });
    });
}
void GitService::diff(const QString &path, bool staged) {
    bool untracked = false;
    for (auto r : m_files.json())
        if (r.toObject()["path"].toString() == path)
            untracked = r.toObject()["untracked"].toBool();
    QStringList args{"diff", "--no-ext-diff", "--no-textconv"};
    if (untracked)
        args << "--no-index" << "--" << "/dev/null" << path;
    else {
        if (staged)
            args << "--cached";
        args << "--" << path;
    }
    auto gen = m_generation;
    run(args, [this, gen](int c, auto out, auto err) {
        if (gen != m_generation)
            return;
        m_state["diff"] = QString::fromUtf8(out.left(2 * 1024 * 1024));
        if (out.size() > 2 * 1024 * 1024)
            m_state["diff"] = m_state["diff"].toString() + i18n("\nDiff truncated at 2 MiB.");
        if (c > 1)
            Q_EMIT error(Settings::redact(QString::fromUtf8(err)));
        Q_EMIT changed();
    });
}
bool GitService::begin() {
    if (m_state["busy"].toBool() || !m_state["repository"].toBool())
        return false;
    m_state["busy"] = true;
    Q_EMIT changed();
    return true;
}
void GitService::finish(const QString &e) {
    m_state["busy"] = false;
    if (!e.isEmpty())
        Q_EMIT error(Settings::redact(e));
    Q_EMIT changed();
    refresh();
}
void GitService::mutate(const QStringList &a) {
    if (!begin())
        return;
    run(a, [this](int c, auto, auto err) { finish(c ? QString::fromUtf8(err) : QString()); });
}
void GitService::stage(const QString &path, bool staged) {
    if (!begin())
        return;
    QStringList paths{path};
    for (auto v : m_files.json()) {
        auto row = v.toObject();
        if (row["path"].toString() == path && !row["oldPath"].toString().isEmpty())
            paths << row["oldPath"].toString();
    }
    if (staged)
        run({"rev-parse", "--verify", "HEAD"}, [this, paths](int c, auto, auto) {
            run((c ? QStringList{"rm", "--cached", "--"} : QStringList{"reset", "-q", "HEAD", "--"}) + paths,
                [this](int rc, auto, auto err) { finish(rc ? QString::fromUtf8(err) : QString()); });
        });
    else
        run(QStringList{"add", "--"} + paths,
            [this](int c, auto, auto err) { finish(c ? QString::fromUtf8(err) : QString()); });
}
void GitService::commit(const QString &message) {
    if (message.trimmed().isEmpty()) {
        Q_EMIT error(i18n("Enter a commit message."));
        return;
    }
    if (!begin())
        return;
    run({"commit", "-m", message}, [this](int c, auto, auto err) {
        if (!c)
            Q_EMIT committed();
        finish(c ? QString::fromUtf8(err) : QString());
    });
}
void GitService::push() {
    if (!m_state["upstream"].toBool()) {
        Q_EMIT error(i18n("Configure an upstream branch before pushing."));
        return;
    }
    mutate({"push"});
}
void GitService::sync() {
    if (!m_state["upstream"].toBool()) {
        Q_EMIT error(i18n("Configure an upstream branch before syncing."));
        return;
    }
    if (!begin())
        return;
    run({"status", "--porcelain=v1", "-z"}, [this](int c, auto out, auto err) {
        if (c || !out.isEmpty()) {
            finish(c ? QString::fromUtf8(err)
                     : i18n("Commit or resolve working tree changes before syncing."));
            return;
        }
        run({"fetch"}, [this](int c, auto, auto err) {
            if (c) {
                finish(QString::fromUtf8(err));
                return;
            }
            run({"rev-list", "--left-right", "--count", "HEAD...@{upstream}"},
                [this](int c, auto out, auto err) {
                    if (c) {
                        finish(QString::fromUtf8(err));
                        return;
                    }
                    auto counts = out.simplified().split(' ');
                    if (counts.size() != 2) {
                        finish(i18n("Could not compare branches."));
                        return;
                    }
                    int ahead = counts[0].toInt(), behind = counts[1].toInt();
                    if (ahead && behind) {
                        finish(i18n("Branches have diverged. Resolve them before syncing."));
                        return;
                    }
                    auto push = [this] {
                        run({"push"}, [this](int c, auto, auto err) {
                            finish(c ? QString::fromUtf8(err) : QString());
                        });
                    };
                    if (behind)
                        run({"merge", "--ff-only", "@{upstream}"}, [this, push](int c, auto, auto err) {
                            if (c)
                                finish(QString::fromUtf8(err));
                            else
                                push();
                        });
                    else
                        push();
                });
        });
    });
}
