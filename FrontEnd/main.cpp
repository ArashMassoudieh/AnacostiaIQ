#include "SensorDashboard.h"
#include <QApplication>
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
    dashboard.show();

    return app.exec();
}
