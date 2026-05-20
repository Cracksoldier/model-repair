#include "MainWindow.hpp"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("Model Repair");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("model-repair");

    gui::MainWindow window;
    window.show();

    return app.exec();
}
