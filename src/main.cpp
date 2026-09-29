#include <QApplication>
#include <QSurfaceFormat>

#include <cpr/cpr.h>

#include "core/log/LogManager.h"
#include "game/3d.h"

int main(int argc, char* argv[]) {
    // The default surface format must be set BEFORE QApplication is created:
    // the renderer uses glVertexAttribFormat / glVertexAttribBinding / glBindVertexBuffer,
    // which only entered the core profile in OpenGL 4.3.
    QSurfaceFormat fmt;
    fmt.setVersion(4, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // Logging: open <exe dir>/stv3d-lab.log and redirect all of Qt's logging there
    // (nothing is printed to the command line anymore)
    if (!MyLogManager::init()) {
        return 1;  // If logging cannot even be opened, there is no point continuing
    }

    // Create the main window
    MyGLWidget window;
    window.resize(800, 600);
    window.setWindowTitle(QStringLiteral("stv3d-lab"));
    window.show();

    // Network request test
    const cpr::Response r = cpr::Get(cpr::Url{"https://httpbin.org/get"});
    qInfo().noquote() << "Status:" << r.status_code;
    qInfo().noquote() << "Content:" << QString::fromStdString(r.text);

    const int exit_code = app.exec();

    // Log shutdown handling
    qInfo().noquote() << "===== stv3d-lab exit, code =" << exit_code << "=====";
    MyLogManager::shutdown();
    return exit_code;
}