#include <QApplication>
#include "ftwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    FtWindow w;
    w.show();

    return app.exec();
}
