#include <QApplication>
#include <QTimer>
#include <QPixmap>
#include "ftwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    FtWindow w;
    w.show();

    // TEMPORARY screenshot hook (to be reverted)
    QByteArray shot = qgetenv("FT_SHOT");
    if (!shot.isEmpty()) {
        QString path = QString::fromLocal8Bit(shot);
        QTimer::singleShot(4000, &w, [&w, path]() {
            w.grab().save(path);
            QApplication::quit();
        });
    }

    return app.exec();
}
