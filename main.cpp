/////////////////////////////////////////////////////////////
// MAIN.CPP - AnacostiaIQ entry point
/////////////////////////////////////////////////////////////

#include "anacostiaiq.h"
#include "RuntimeModeGuard.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    RuntimeModeGuard modeGuard("gui");
    if (!modeGuard.acquired()) {
        QMessageBox::critical(nullptr, "AnacostiaIQ",
                              modeGuard.errorString());
        return 2;
    }

    AnacostiaIQ window;
    window.show();

    return app.exec();
}
