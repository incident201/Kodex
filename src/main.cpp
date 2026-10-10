/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "appcontroller.h"
#include "kodex_version.h"
#include "maintenance.h"
#include "richtext.h"
#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <clocale>
#ifdef __GLIBC__
#include <malloc.h>
#endif
int main(int argc, char **argv) {
#ifdef __GLIBC__
    // Decoding screenshots allocates buffers of several megabytes. With glibc's default, self-adjusting mmap
    // threshold they land on the heap, fragment it and are never returned; a fixed threshold frees them at
    // once.
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
#endif
    QApplication app(argc, argv);
    app.setWindowIcon(
        QIcon::fromTheme("io.github.incident201.kodex", QIcon::fromTheme("utilities-terminal")));
    KLocalizedString::setApplicationDomain("kodex");
    // Running from the build directory: the catalogs are next to the executable, not in the install prefix.
    if (const auto uninstalled = QCoreApplication::applicationDirPath() + "/locale";
        QDir(uninstalled).exists())
        KLocalizedString::addDomainLocaleDir("kodex", uninstalled);
    Settings preferences;
    if (!preferences.value("language").isEmpty()) {
        // gettext suppresses translations in the C locale, even with a language override.
        // Change only message locale; numeric parsing and process environments stay intact.
        const auto localeName = (QLocale(preferences.value("language")).name() + ".UTF-8").toUtf8();
        std::setlocale(LC_MESSAGES, localeName.constData());
        KLocalizedString::setLanguages({preferences.value("language")});
        // Dates and numbers shown in the interface follow the chosen language too.
        QLocale::setDefault(QLocale(preferences.value("language")));
    }
    KAboutData about("kodex", i18n("Kodex"), KODEX_VERSION_STRING, i18n("A native Codex client for KDE"),
                     KAboutLicense::GPL_V3, i18n("© 2026 incident201"), QString(),
                     "https://github.com/incident201/Kodex", "https://github.com/incident201/Kodex/issues");
    about.addAuthor(i18n("incident201"), i18n("Author"), QString(), "https://github.com/incident201");
    about.setOrganizationDomain("github.com");
    about.setDesktopFileName("io.github.incident201.kodex");
    about.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"),
                        i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    KAboutData::setApplicationData(about);
    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addOption({"offline", i18n("Start without connecting to Codex")});
    parser.addOption({"smoke-test", i18n("Exit after loading the user interface")});
    parser.addOption({"screenshot", i18n("Save a UI screenshot for testing"), "path"});
    parser.addOption({"settings", i18n("Open the settings page")});
    parser.process(app);
    about.processCommandLine(&parser);
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle("org.kde.desktop");
    RpcClient rpc(&preferences);
    GitService repository;
    Maintenance maintenance(&preferences);
    maintenance.setRpc(&rpc);
    AppController controller(&rpc, &preferences, &repository);
    RichText richText;
    QObject::connect(&maintenance, &Maintenance::restarting, &rpc, &RpcClient::disconnectServer);
    QObject::connect(&maintenance, &Maintenance::restarted, &rpc, &RpcClient::connectServer);
    QQmlApplicationEngine engine;
    QStringList uiWarnings;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                     [&](const QList<QQmlError> &warnings) {
                         for (const auto &warning : warnings)
                             if (warning.url().toString().contains("/io/github/incident201/kodex/"))
                                 uiWarnings << warning.toString();
                     });
    if (!preferences.value("language").isEmpty())
        engine.setUiLanguage(preferences.value("language"));
    auto *localized = new KLocalizedQmlContext(&engine);
    localized->setTranslationDomain("kodex");
    engine.rootContext()->setContextObject(localized);
    QQmlEngine::setContextForObject(localized, engine.rootContext());
    if (!preferences.value("language").isEmpty())
        KLocalizedString::setLanguages({preferences.value("language")});
    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Preferences", &preferences);
    engine.rootContext()->setContextProperty("Connection", &rpc);
    engine.rootContext()->setContextProperty("Repository", &repository);
    engine.rootContext()->setContextProperty("Maintenance", &maintenance);
    engine.rootContext()->setContextProperty("RichText", &richText);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("io.github.incident201.kodex", "Main");
    if (parser.isSet("settings") && !engine.rootObjects().isEmpty())
        engine.rootObjects().first()->setProperty("settingsOpen", true);
    if (!parser.isSet("offline"))
        QTimer::singleShot(0, &rpc, &RpcClient::connectServer);
    if (parser.isSet("smoke-test"))
        QTimer::singleShot(2000, &app, [&] {
            QStringList errors = uiWarnings;
            if (!engine.rootObjects().isEmpty()) {
                const auto error = engine.rootObjects().first()->property("errorText").toString();
                if (!error.isEmpty())
                    errors << error;
            }
            if (!errors.isEmpty())
                fprintf(stderr, "%s\n", qPrintable(errors.join('\n')));
            app.exit(errors.isEmpty() ? 0 : 2);
        });
    if (parser.isSet("screenshot"))
        QTimer::singleShot(1500, &engine, [&engine, &parser] {
            if (!engine.rootObjects().isEmpty())
                if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
                    window->grabWindow().save(parser.value("screenshot"));
        });
    return app.exec();
}
