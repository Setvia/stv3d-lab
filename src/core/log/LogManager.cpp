#include "LogManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

namespace {

// The file object and mutex are kept inside the .cpp: unreachable from outside, so their
// lifetime can only be controlled through init()/shutdown()
QFile *g_log_file = nullptr;
QMutex g_log_mutex;

}  // namespace

QString LogManager::logFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/stv3d-lab.log");
}

bool LogManager::isReady()
{
    return g_log_file != nullptr && g_log_file->isOpen();
}

bool LogManager::init()
{
    // Static storage duration: the lifetime spans the whole process, avoiding a dangling pointer
    // if the file object were on the stack while the handler was still alive
    static QFile log_file;

    log_file.setFileName(logFilePath());
    if (!log_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return false;
    }

    log_file.write(QStringLiteral("\n===== stv3d-lab start =====\n").toUtf8());
    log_file.flush();

    g_log_file = &log_file;
    qInstallMessageHandler(&LogManager::messageHandler);
    return true;
}

void LogManager::shutdown()
{
    qInstallMessageHandler(nullptr);

    QMutexLocker locker(&g_log_mutex);
    if (g_log_file != nullptr) {
        g_log_file->flush();
        g_log_file->close();
        g_log_file = nullptr;
    }
}

void LogManager::messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    const char *level = "INFO";
    switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO";  break;
        case QtWarningMsg:  level = "WARN";  break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
    }

    // Lock: other threads may be writing to the log at the same time; the locker unlocks
    // automatically when it goes out of scope
    QMutexLocker locker(&g_log_mutex);
    if (g_log_file != nullptr && g_log_file->isOpen()) {
        const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                             + QStringLiteral(" [")
                             + QLatin1String(level)
                             + QStringLiteral("] ")
                             + message
                             + QLatin1Char('\n');

        g_log_file->write(line.toUtf8());
        g_log_file->flush();
    }

    if (type == QtFatalMsg) {
        abort();  // A fatal error must terminate, matching Qt's default behavior
    }
}
