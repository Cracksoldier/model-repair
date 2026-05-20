#include "MainWindow.hpp"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("Model Repair");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("model-repair");

    gui::MainWindow window;
    window.show();

    return app.exec();
}
