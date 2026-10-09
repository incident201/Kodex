#include "settings.h"
#include <KConfigGroup>
#include <KLocalizedString>
#include <KWallet>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <memory>
Settings::Settings(QObject *p, const QString &configFile)
    : QObject(p), m_config(KSharedConfig::openConfig(configFile)) {
    m_values = {
        {"executable", "codex"},
        {"arguments", ""},
        {"proxyMode", "inherit"},
        {"proxyUrl", ""},
        {"noProxy", "localhost,127.0.0.1,::1"},
        {"fallbackProxy", ""},
        {"language", ""},
        {"updateMode", QStandardPaths::findExecutable("codex-update").isEmpty() ? "builtin" : "external"},
        {"updater", QStandardPaths::findExecutable("codex-update")},
        {"updaterArguments", ""}};
    KConfigGroup g(m_config, "Runtime");
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        it.value() = g.readEntry(it.key(), it.value().toString());
}
QString Settings::executable() const {
    auto e = value("executable");
    return e.contains('/') ? e : QStandardPaths::findExecutable(e);
}
QStringList Settings::arguments() const {
    return QProcess::splitCommand(value("arguments"));
}
bool Settings::save(const QVariantMap &v, const QString &password) {
    auto merged = m_values;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (merged.contains(it.key()))
            merged[it.key()] = it.value();
    for (const auto &key : {"proxyUrl", "fallbackProxy"}) {
        QUrl u(merged[key].toString());
        if (!u.isEmpty() && (!QStringList{"http", "https", "socks5", "socks5h"}.contains(u.scheme()) ||
                             u.host().isEmpty() || !u.password().isEmpty())) {
            emit error(i18n("Enter a valid proxy URL without a password; use the password field."));
            return false;
        }
    }
    if (merged["proxyMode"] == "custom" && merged["proxyUrl"].toString().isEmpty()) {
        emit error(i18n("A proxy URL is required."));
        return false;
    }
    if (!password.isEmpty()) {
        std::unique_ptr<KWallet::Wallet> w(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0));
        if (!w || (!w->hasFolder("Kodex") && !w->createFolder("Kodex")) || !w->setFolder("Kodex") ||
            w->writePassword(merged["proxyUrl"].toString(), password) != 0) {
            emit error(i18n("Could not save the proxy password in KWallet."));
            return false;
        }
        m_password = password;
    } else if (merged["proxyUrl"] != m_values["proxyUrl"])
        m_password.clear();
    m_values = merged;
    KConfigGroup g(m_config, "Runtime");
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        g.writeEntry(it.key(), it.value());
    g.sync();
    emit changed();
    return true;
}
QProcessEnvironment Settings::proxyEnvironment(QProcessEnvironment env, const QString &mode,
                                               const QString &url, const QString &noProxy) {
    if (mode == "inherit")
        return env;
    for (const auto &key : {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy",
                            "all_proxy", "NO_PROXY", "no_proxy"})
        env.remove(key);
    if (mode == "custom") {
        for (const auto &key :
             {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"})
            env.insert(key, url);
        env.insert("NO_PROXY", noProxy);
        env.insert("no_proxy", noProxy);
    }
    return env;
}
QProcessEnvironment Settings::environment(const QString &overrideProxy) const {
    QString proxy = overrideProxy.isEmpty() ? value("proxyUrl") : overrideProxy;
    QUrl u(proxy);
    if (overrideProxy.isEmpty() && value("proxyMode") == "custom" && !u.userName().isEmpty()) {
        if (m_password.isEmpty()) {
            std::unique_ptr<KWallet::Wallet> w(
                KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0));
            if (w && w->setFolder("Kodex"))
                w->readPassword(proxy, m_password);
        }
        if (!m_password.isEmpty()) {
            u.setPassword(m_password);
            proxy = u.toString(QUrl::FullyEncoded);
        }
    }
    return proxyEnvironment(QProcessEnvironment::systemEnvironment(),
                            overrideProxy.isEmpty() ? value("proxyMode") : "custom", proxy, value("noProxy"));
}
QString Settings::redact(QString s) {
    s.replace(QRegularExpression(R"((https?|socks5h?)://[^\s/@]+@)"), "\\1://[redacted]@");
    s.replace(QRegularExpression(
                  R"((?i)(authorization:|access_token[=:]|refresh_token[=:]|api_key[=:])\s*[^\s,;]+)"),
              "\\1 [redacted]");
    return s;
}
QString Settings::draft(const QString &id) const {
    return KConfigGroup(m_config, "Drafts").readEntry(id, QString());
}
void Settings::saveDraft(const QString &id, const QString &text) {
    if (id.isEmpty())
        return;
    KConfigGroup g(m_config, "Drafts");
    if (text.isEmpty())
        g.deleteEntry(id);
    else
        g.writeEntry(id, text);
    g.sync();
}
