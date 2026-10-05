#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <QString>
#include <QtGlobal>

// Global log manager: routes all of Qt's logging (qDebug / qInfo / qWarning / qCritical / qFatal)
// into stv3d-lab.log next to the exe.
//
// Usage (see main.cpp):
//     if (!LogManager::init()) return 1;   // open the file + install the message handler
//     ...
//     LogManager::shutdown();              // restore the handler + close the file
//
// The implementation lives entirely in LogManager.cpp: including this header from several TUs
// cannot produce duplicate definitions (ODR), and the file object is never exposed to outsiders,
// so it cannot be modified by mistake.

class LogManager
{
public:
    // Open the log file (append mode) and install the message handler; returns true on success
    static bool init();

    // Restore the default message handler and close the file; safe to call repeatedly
    static void shutdown();

    // Full path of the log file: <directory of the exe>/stv3d-lab.log
    static QString logFilePath();

    // Whether the manager is ready (file open and handler installed)
    static bool isReady();

private:
    // Qt message handler; output format: timestamp [level] message
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);

    LogManager() = delete;  // Pure static utility class; instantiation is forbidden
};

#endif  // LOG_MANAGER_H
