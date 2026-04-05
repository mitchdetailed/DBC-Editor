#include "mainwindow.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>

namespace {
QRect safeInitialGeometry(const QSize& desiredSize)
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (!screen) {
        return QRect(QPoint(100, 100), desiredSize);
    }

    const QRect bounds = screen->availableGeometry();
    const int width = qMin(desiredSize.width(), bounds.width());
    const int height = qMin(desiredSize.height(), bounds.height());
    const int x = bounds.x() + (bounds.width() - width) / 2;
    const int y = bounds.y() + (bounds.height() - height) / 2;

    return QRect(x, y, width, height);
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("Qt DBC Tool");
    QApplication::setOrganizationName("AntiGravity Projects");

    MainWindow window;
    window.setGeometry(safeInitialGeometry(QSize(1360, 840)));
    window.show();

    return app.exec();
}
