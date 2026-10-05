#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <sstream>
#include <string>

// Dependency-free logging for the whole project: no Qt, no third-party code, std only.
//
// A log line looks like this (same format the Qt version produced):
//     2026-10-05 22:11:53.506 [INFO] shader program ready: id = 3 | vertex: shaders/basic.vert
//
// Usage:
//     LogManager::init("D:/build/stv3d-lab.log");  // the caller decides where the file goes
//     LOG_INFO() << "mesh ready: " << vertex_count << " vertices";
//     LogManager::shutdown();
//
// Design notes:
//   * LOG_*() builds a temporary LogStream; the line is written and flushed when that temporary
//     dies at the end of the full expression. One logical message can therefore never be split
//     apart or interleaved with another thread's output.
//   * A small atomic spin lock guards the file (see LogManager.cpp for why it is not std::mutex),
//     so logging from several threads is safe and lines never interleave.
//   * Before init() (or after a failed init) messages go to stderr instead of being dropped:
//     silence would be worse than an unstyled line.
//   * init() takes the path as an argument on purpose. Resolving "the directory of the exe" is a
//     platform question (GetModuleFileNameW on Windows), so it belongs to the app/platform layer,
//     not to core.

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error,
    Fatal  // written (and flushed), then abort()
};

// Text that goes between the brackets: "DEBUG" / "INFO" / "WARN" / "ERROR" / "FATAL"
const char *logLevelTag(LogLevel level);

// Formatting helpers, so that log text does not depend on any stream manipulator state:
//     LOG_WARNING() << "GL error, code = " << logHex(error, 4);        -> 0x0500
//     LOG_INFO()    << "forward = " << logFixed(forward.y, 3);         -> -0.080
struct LogHex
{
    unsigned long long value;
    int width;  // minimum number of zero-padded digits (0 = as many as needed)
};

struct LogFixed
{
    double value;
    int decimals;
};

inline LogHex logHex(unsigned long long value, int width = 0)
{
    return LogHex{value, width};
}

inline LogFixed logFixed(double value, int decimals)
{
    return LogFixed{value, decimals};
}

class LogManager
{
public:
    // Open `filePath` in append mode and write the "start" banner; false when the file cannot be opened
    static bool init(const std::string &filePath);

    // Flush and close the file; safe to call repeatedly
    static void shutdown();

    // The path handed to init() (empty before init)
    static std::string logFilePath();

    // True while the file is open
    static bool isReady();

    // Write one complete line (timestamp + level + message + newline).
    // Called by ~LogStream(), but usable directly - the Qt bridge in main.cpp does exactly that.
    static void write(LogLevel level, const std::string &message);

private:
    LogManager() = delete;  // pure static utility class; instantiation is forbidden
};

// Accumulates one line and flushes it in the destructor.
// Only ever used as a temporary, which is what the LOG_*() macros are for.
class LogStream
{
public:
    explicit LogStream(LogLevel level) : level(level) {}
    ~LogStream() { LogManager::write(level, buffer.str()); }

    LogStream(const LogStream &) = delete;
    LogStream &operator=(const LogStream &) = delete;

    template <typename T>
    LogStream &operator<<(const T &value)
    {
        buffer << value;
        return *this;
    }

    // The overloads below win over the template (exact match beats a template specialization)
    LogStream &operator<<(bool value);      // true/false, not 1/0
    LogStream &operator<<(LogHex hex);
    LogStream &operator<<(LogFixed fixed);

private:
    LogLevel level;
    std::ostringstream buffer;
};

#define LOG_DEBUG() LogStream(LogLevel::Debug)
#define LOG_INFO() LogStream(LogLevel::Info)
#define LOG_WARNING() LogStream(LogLevel::Warning)
#define LOG_ERROR() LogStream(LogLevel::Error)
#define LOG_FATAL() LogStream(LogLevel::Fatal)

#endif  // LOG_MANAGER_H
