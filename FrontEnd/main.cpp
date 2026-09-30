#include "SensorDashboard.h"
#include <QApplication>
#include <QScreen>
#include <QStringList>

// Entry point. The config file path can be overridden on the command
// line (desktop builds); otherwise it defaults to "config.json" in the
// working directory. For WebAssembly builds, config.json is fetched
// from the same directory the app is served from (see notes in
// config.json and the deployment guide).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QString configPath = "config.json";
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if ((args[i] == "--config" || args[i] == "-c") && i + 1 < args.size())
            configPath = args[i + 1];
    }

    SensorDashboard dashboard(configPath);
#ifdef Q_OS_WASM
    // Treat the browser viewport as the application window. Qt/Wasm can
    // maximize only the top-level decoration while leaving a previously
    // resized central widget at its old width, so set the complete geometry
    // explicitly and repeat that whenever the browser viewport changes.
    QScreen *browserScreen = QApplication::primaryScreen();
    if (browserScreen)
        dashboard.setGeometry(browserScreen->availableGeometry());
    dashboard.show();
    if (browserScreen) {
        QObject::connect(browserScreen, &QScreen::availableGeometryChanged,
                         &dashboard, [&dashboard](const QRect &geometry) {
            dashboard.setGeometry(geometry);
        });
    }
#else
    dashboard.show();
#endif

    return app.exec();
}
