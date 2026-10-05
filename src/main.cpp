#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QtGlobal>

#include <cpr/cpr.h>

#include "core/log/LogManager.h"
#include "game/3d.h"

namespace {

// Temporary bridge while Qt is still part of the app: everything Qt itself logs (driver warnings,
// QOpenGLWidget messages, ...) is forwarded into our log, so the console stays silent and every
// line ends up in stv3d-lab.log. This bridge disappears together with Qt in the platform step.
void qtMessageBridge(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    LogLevel level = LogLevel::Info;
    switch (type) {
        case QtDebugMsg:    level = LogLevel::Debug;   break;
        case QtInfoMsg:     level = LogLevel::Info;    break;
        case QtWarningMsg:  level = LogLevel::Warning; break;
        case QtCriticalMsg: level = LogLevel::Error;   break;
        case QtFatalMsg:    level = LogLevel::Fatal;   break;
    }
    LogManager::write(level, message.toStdString());
}

}  // namespace

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

    // Logging: open <exe dir>/stv3d-lab.log. The path is resolved here (Qt knows the exe directory)
    // and handed to core, which only needs a file name and therefore stays platform-free.
    const QString log_path = QCoreApplication::applicationDirPath() + QStringLiteral("/stv3d-lab.log");
    if (!LogManager::init(log_path.toStdString())) {
        return 1;  // If logging cannot even be opened, there is no point continuing
    }
    qInstallMessageHandler(&qtMessageBridge);

    // Create the main window
    GLWidget window;
    window.resize(800, 600);
    window.setWindowTitle(QStringLiteral("stv3d-lab"));
    window.show();

    // Network request test
    const cpr::Response r = cpr::Get(cpr::Url{"https://httpbin.org/get"});
    LOG_INFO() << "Status: " << r.status_code;
    LOG_INFO() << "Content: " << r.text;

    const int exit_code = app.exec();

    // Log shutdown handling
    LOG_INFO() << "===== stv3d-lab exit, code = " << exit_code << " =====";
    qInstallMessageHandler(nullptr);
    LogManager::shutdown();
    return exit_code;
}
