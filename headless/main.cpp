/////////////////////////////////////////////////////////////
// MAIN.CPP - anacostiaiqd entry point (headless AnacostiaIQ)
//
//  A QCoreApplication, so it runs on a Pi with no display server.
//  Logs every reading with a timestamp and exits cleanly on
//  SIGINT/SIGTERM, releasing the GPIO lines on the way out.
/////////////////////////////////////////////////////////////

#include "HeadlessMonitor.h"
#include "RuntimeModeGuard.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSocketNotifier>
#include <QTextStream>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>

#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

static void logHandler(QtMsgType type, const QMessageLogContext &,
                       const QString &msg) {
    const char *level = "INFO ";
    bool toStdErr = false;

    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO "; break;
    case QtWarningMsg:  level = "WARN "; toStdErr = true; break;
    case QtCriticalMsg: level = "ERROR"; toStdErr = true; break;
    case QtFatalMsg:    level = "FATAL"; toStdErr = true; break;
    }

    FILE *sink = toStdErr ? stderr : stdout;
    QTextStream out(sink);
    out << QDateTime::currentDateTime().toString(Qt::ISODate)
        << " [" << level << "] " << msg << Qt::endl;
    out.flush();
    std::fflush(sink);

    if (type == QtFatalMsg)
        abort();
}

static int sigFd[2];

static void unixSignalHandler(int) {
    const char byte = 1;
    const ssize_t written = ::write(sigFd[0], &byte, sizeof(byte));
    static_cast<void>(written);
}

static bool installSignalHandlers(QCoreApplication &app,
                                  HeadlessMonitor &monitor) {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) != 0) {
        qWarning() << "socketpair() failed — Ctrl-C will not shut down cleanly";
        return false;
    }

    QSocketNotifier *notifier =
        new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &app);

    QObject::connect(notifier, &QSocketNotifier::activated, &app,
                     [&app, &monitor, notifier]() {
                         notifier->setEnabled(false);
                         char byte;
                         const ssize_t got =
                             ::read(sigFd[1], &byte, sizeof(byte));
                         static_cast<void>(got);

                         qInfo() << "Signal received — shutting down";
                         monitor.shutdown();
                         app.quit();
                     });

    std::signal(SIGINT,  unixSignalHandler);
    std::signal(SIGTERM, unixSignalHandler);
    return true;
}

static QString resolveConfigPath(const QString &explicitPath) {
    if (!explicitPath.isEmpty())
        return explicitPath;

    const QString beside =
        QCoreApplication::applicationDirPath() + "/config.json";
    if (QFileInfo::exists(beside))
        return beside;

    return QStringLiteral("config.json");
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("anacostiaiqd");
    QCoreApplication::setApplicationVersion("1.0");

    qInstallMessageHandler(logHandler);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "AnacostiaIQ headless monitor — polls the configured sensors and "
        "weather forecast and pushes every reading to the cloud API. "
        "Same config.json as the GUI build.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOption(
        QStringList() << "c" << "config",
        "Path to config.json (default: next to the executable, then ./config.json).",
        "path");
    parser.addOption(configOption);
    parser.process(app);

    RuntimeModeGuard modeGuard("headless");
    if (!modeGuard.acquired()) {
        qCritical().noquote() << modeGuard.errorString();
        return 2;
    }

    const QString configPath = resolveConfigPath(parser.value(configOption));

    qInfo() << "AnacostiaIQ headless monitor starting";

    HeadlessMonitor monitor(configPath);
    installSignalHandlers(app, monitor);

    if (!monitor.start())
        return 1;

    return app.exec();
}
