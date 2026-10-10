/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <KSyntaxHighlighting/Repository>
#include <QObject>
class RichText : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    Q_INVOKABLE QString markdown(const QString &source, bool dark);
    Q_INVOKABLE QString code(const QString &source, const QString &language, bool dark);

  private:
    KSyntaxHighlighting::Repository m_repository;
};
