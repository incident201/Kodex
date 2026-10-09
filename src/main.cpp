#include "appcontroller.h"
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
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("kodex");
    app.setOrganizationName("Kodex");
    app.setApplicationVersion("0.1.0");
    app.setWindowIcon(QIcon::fromTheme("org.kde.kodex", QIcon::fromTheme("utilities-terminal")));
    KLocalizedString::setApplicationDomain("kodex");
    KLocalizedString::addDomainLocaleDir("kodex", QCoreApplication::applicationDirPath() + "/locale");
    Settings preferences;
    if (!preferences.value("language").isEmpty()) {
        // gettext suppresses translations in the C locale, even with a language override.
        // Change only message locale; numeric parsing and process environments stay intact.
        const auto localeName = (QLocale(preferences.value("language")).name() + ".UTF-8").toUtf8();
        std::setlocale(LC_MESSAGES, localeName.constData());
        KLocalizedString::setLanguages({preferences.value("language")});
    }
    KAboutData about("kodex", i18n("Kodex"), "0.1.0", i18n("A native Codex client for KDE"),
                     KAboutLicense::Unknown);
    KAboutData::setApplicationData(about);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"offline", "Start without connecting to Codex"});
    parser.addOption({"smoke-test", "Exit after loading the user interface"});
    parser.addOption({"screenshot", "Save a UI screenshot for testing", "path"});
    parser.addOption({"settings", "Open the settings page"});
    parser.process(app);
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
                             if (warning.url().toString().contains("/org/kde/kodex/"))
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
    engine.loadFromModule("org.kde.kodex", "Main");
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
