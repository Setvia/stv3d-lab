#include "LogManager.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>   // only for std::lock_guard; the lock itself is the SpinLock below
#include <thread>

namespace
{

// A tiny spin lock instead of std::mutex.
//
// The guarded section is one short line (assemble a line, write it, flush), so an atomic flag is
// enough - and staying clear of pthread_mutex_* keeps threading-library symbols out of the link,
// where they clash with the same symbols exported by the third-party DLL import libraries.
class SpinLock
{
public:
    void lock()
    {
        while (flag.test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield();  // maps to SwitchToThread on MinGW, not to pthread_yield
        }
    }

    void unlock()
    {
        flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;  // C++17 spelling of "clear at startup"
};

// File, path and lock live inside this .cpp: unreachable from outside, so their lifetime can only
// be controlled through init()/shutdown(). Static storage duration spans the whole process.
SpinLock g_log_lock;
std::ofstream g_log_file;
std::string g_log_path;
bool g_ready = false;

// "2026-10-05 22:11:53.506" - always called while the log lock is held, so std::localtime is safe
std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto whole_seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - whole_seconds).count();

    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (localtime_s(&local, &raw) != 0)
    {
        return "0000-00-00 00:00:00.000";
    }

    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(millis));
    return text;
}

}  // namespace

const char *logLevelTag(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
    }
    return "INFO";
}

bool LogManager::init(const std::string &filePath)
{
    std::lock_guard<SpinLock> lock(g_log_lock);

    if (g_ready)
    {
        return true;  // idempotent: a second init() keeps the already open file
    }

    g_log_file.open(filePath, std::ios::out | std::ios::app);
    if (!g_log_file.is_open())
    {
        g_log_path.clear();
        return false;
    }

    g_log_path = filePath;
    g_ready = true;

    g_log_file << "\n===== stv3d-lab start =====\n";
    g_log_file.flush();
    return true;
}

void LogManager::shutdown()
{
    std::lock_guard<SpinLock> lock(g_log_lock);

    if (g_log_file.is_open())
    {
        g_log_file.flush();
        g_log_file.close();
    }
    g_ready = false;
}

std::string LogManager::logFilePath()
{
    std::lock_guard<SpinLock> lock(g_log_lock);
    return g_log_path;
}

bool LogManager::isReady()
{
    std::lock_guard<SpinLock> lock(g_log_lock);
    return g_ready;
}

void LogManager::write(LogLevel level, const std::string &message)
{
    // The lock covers timestamp + line assembly + write: a line is always complete and in order
    std::lock_guard<SpinLock> lock(g_log_lock);

    const std::string line = timestampNow() + " [" + logLevelTag(level) + "] " + message + "\n";

    if (g_ready && g_log_file.is_open())
    {
        g_log_file << line;
        g_log_file.flush();
    }
    else
    {
        std::fputs(line.c_str(), stderr);
    }

    if (level == LogLevel::Fatal)
    {
        if (g_log_file.is_open())
        {
            g_log_file.flush();
        }
        std::abort();  // a fatal error terminates the process, as before
    }
}

// ---------------- LogStream ----------------

LogStream &LogStream::operator<<(bool value)
{
    buffer << (value ? "true" : "false");
    return *this;
}

LogStream &LogStream::operator<<(LogHex hex)
{
    // Save/restore the stream state: a helper must not change how later values are printed
    const std::ios::fmtflags flags = buffer.flags();
    const char fill = buffer.fill();

    buffer << "0x" << std::hex << std::nouppercase;
    if (hex.width > 0)
    {
        buffer << std::setw(hex.width) << std::setfill('0');
    }
    buffer << hex.value;

    buffer.flags(flags);
    buffer.fill(fill);
    return *this;
}

LogStream &LogStream::operator<<(LogFixed fixed)
{
    const std::ios::fmtflags flags = buffer.flags();
    const std::streamsize precision = buffer.precision();

    buffer << std::fixed << std::setprecision(fixed.decimals) << fixed.value;

    buffer.precision(precision);
    buffer.flags(flags);
    return *this;
}
