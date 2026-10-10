/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "richtext.h"
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QColor>
#include <QRegularExpression>
#include <QTextDocument>
namespace {
class HtmlHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
  public:
    QString line, result;
    int position = 0;
    void applyFormat(int start, int length, const KSyntaxHighlighting::Format &f) override {
        if (start > position)
            result += line.mid(position, start - position).toHtmlEscaped();
        auto color = f.textColor(theme());
        result += "<span" + (color.isValid() ? QString(" style=\"color:%1\"").arg(color.name()) : QString()) +
                  ">" + line.mid(start, length).toHtmlEscaped() + "</span>";
        position = start + length;
    }
    QString render(const QString &text) {
        KSyntaxHighlighting::State state;
        QString html;
        for (auto l : text.split('\n')) {
            line = l;
            result.clear();
            position = 0;
            state = highlightLine(line, state);
            html += result + line.mid(position).toHtmlEscaped() + "\n";
        }
        return html;
    }
};
} // namespace
QString RichText::markdown(const QString &source, bool dark) {
    const auto theme = m_repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme
                                                      : KSyntaxHighlighting::Repository::LightTheme);
    // Rich text ignores backgrounds on <pre>; a single-cell table gives code blocks a panel.
    const auto block = [&](const QString &body) {
        return QString("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" "
                       "bgcolor=\"%1\"><tr><td><pre>%2</pre>"
                       "</td></tr></table>")
            .arg(QColor::fromRgba(theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor)).name(),
                 body);
    };
    QString html;
    QString prose, code, language;
    bool fenced = false;
    auto flush = [&] {
        if (prose.isEmpty())
            return;
        QTextDocument d;
        // Show raw HTML from the model as text instead of interpreting it.
        d.setMarkdown(prose, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
                                                             QTextDocument::MarkdownNoHTML));
        html += d.toHtml().section("<body", 1).section('>', 1).section("</body>", 0, 0);
        prose.clear();
    };
    for (auto line : source.split('\n')) {
        if (line.startsWith("```")) {
            if (fenced) {
                HtmlHighlighter h;
                auto def = m_repository.definitionForName(language);
                if (!def.isValid())
                    def = m_repository.definitionForFileName("file." + language);
                h.setDefinition(def);
                h.setTheme(theme);
                if (code.endsWith('\n'))
                    code.chop(1);
                html += block(h.render(code));
                code.clear();
            } else {
                flush();
                language = line.mid(3).trimmed();
            }
            fenced = !fenced;
        } else if (fenced)
            code += line + "\n";
        else
            prose += line + "\n";
    }
    if (fenced)
        html += block(code.toHtmlEscaped());
    flush();
    // Remote images must not trigger network requests while displaying a chat.
    html.remove(QRegularExpression("<img[^>]*>", QRegularExpression::CaseInsensitiveOption));
    return html;
}
QString RichText::code(const QString &source, const QString &language, bool dark) {
    HtmlHighlighter h;
    auto definition = m_repository.definitionForName(language);
    if (!definition.isValid())
        definition = m_repository.definitionForFileName("file." + language);
    h.setDefinition(definition);
    h.setTheme(m_repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme
                                              : KSyntaxHighlighting::Repository::LightTheme));
    return "<pre>" + h.render(source) + "</pre>";
}
