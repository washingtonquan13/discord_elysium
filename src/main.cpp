#include "core/Settings.h"
#include "ui/AppController.h"
#include "ui/ImageProvider.h"
#include "ui/QrProvider.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

using namespace kestrel;

// Everything qDebug/qInfo/qWarning prints also goes to a log file in the app
// data folder, so problems can be diagnosed on Windows where there's no console.
static QFile *g_log = nullptr;
static void logHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    static const char *names[] = { "debug", "warning", "critical", "fatal", "info" };
    const QByteArray line = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz").toUtf8() + " [" + names[type] + "] " + msg.toUtf8() + "\n";
    fwrite(line.constData(), 1, line.size(), stderr);
    if (g_log) {
        g_log->write(line);
        g_log->flush();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("Kestrel");
    QCoreApplication::setApplicationName("Kestrel");
    QCoreApplication::setApplicationVersion("0.1.0");
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/icons/app.png"));
    app.setQuitOnLastWindowClosed(true);
    QQuickStyle::setStyle("Basic");

    QCommandLineParser cli;
    cli.addHelpOption();
    QCommandLineOption screenshot("screenshot", "Save a screenshot of the window after <ms> and exit (testing).", "file");
    QCommandLineOption delay("delay", "Screenshot delay in ms.", "ms", "4000");
    QCommandLineOption token("token", "Log in with this token (testing).", "token");
    cli.addOption(screenshot);
    cli.addOption(delay);
    cli.addOption(token);
    cli.process(app);

    auto *settings = new Settings(&app);
    {
        const QString logPath = settings->dataDir() + "/kestrel.log";
        if (QFileInfo(logPath).size() > 5 * 1024 * 1024) QFile::remove(logPath + ".old"), QFile::rename(logPath, logPath + ".old");
        g_log = new QFile(logPath, &app);
        if (!g_log->open(QIODevice::Append | QIODevice::Text)) { delete g_log; g_log = nullptr; }
        qInstallMessageHandler(logHandler);
        qInfo() << "Kestrel" << QCoreApplication::applicationVersion() << "starting";
    }
    auto *controller = new AppController(settings, &app);

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images";
    QDir().mkpath(cacheDir);
    auto *loader = new ImageLoader(cacheDir, &app);

    QQmlApplicationEngine engine;
    engine.addImageProvider("remote", new RemoteImageProvider(loader));
    engine.addImageProvider("qr", new QrProvider);
    engine.rootContext()->setContextProperty("app", controller);
    engine.rootContext()->setContextProperty("appSettings", settings);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Kestrel/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    controller->setWindow(window);
    // Free decoded images when the window is hidden to the tray.
    QObject::connect(window, &QWindow::visibleChanged, loader, [loader](bool visible) {
        if (!visible) loader->clearMemory();
    });

    if (cli.isSet(token)) controller->loginWithToken(cli.value(token));
    else controller->autoLogin();

    // development: run a QML test script against the live UI
    if (qEnvironmentVariableIsSet("KESTREL_TEST_SCRIPT")) {
        auto *component = new QQmlComponent(&engine, QUrl::fromLocalFile(qEnvironmentVariable("KESTREL_TEST_SCRIPT")), &app);
        QObject *obj = component->create(engine.rootContext());
        if (!obj) qWarning() << component->errors();
    }

    if (cli.isSet(screenshot)) {
        QTimer::singleShot(cli.value(delay).toInt(), &app, [window, file = cli.value(screenshot)]() {
            window->grabWindow().save(file);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
