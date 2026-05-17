#include "MainWindow.hpp"
#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Model Repair");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("model-repair");

    gui::MainWindow window;
    window.show();

    return app.exec();
}
