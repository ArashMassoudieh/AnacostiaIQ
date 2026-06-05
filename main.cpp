/////////////////////////////////////////////////////////////
// MAIN.CPP - AnacostiaIQ entry point
/////////////////////////////////////////////////////////////

#include "anacostiaiq.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    AnacostiaIQ window;
    window.show();

    return app.exec();
}
