#include <QApplication>
#include <QIcon>
#include "ftwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/icon-ft.png"));

    FtWindow w;
    w.show();

    return app.exec();
}
