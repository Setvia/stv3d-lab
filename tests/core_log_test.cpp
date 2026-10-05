// Unit tests for the dependency-free logging layer (core/log).
//
// This target links nothing but stv3d_core: no Qt, no OpenGL, no window. Together with
// core_smoke.cpp it acts as a guard - if LogManager ever grows a Qt include, this stops building.
//
// The tests read the produced file back and check the exact line format, because the log is the
// main verification tool for the renderer: a broken format means broken regression comparisons.

#include "core/log/LogManager.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition)
    {
        ++g_failures;
    }
}

const char* kLogPath = "core_log_test.log";

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

int countOccurrences(const std::string& haystack, const std::string& needle)
{
    int count = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size()))
    {
        ++count;
    }
    return count;
}

// The lines of the file, without the trailing empty one.
// The file is written in text mode, so on Windows it uses CRLF: the '\r' is stripped here, which is
// what every other reader of the log does too.
std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        result.push_back(line);
    }
    return result;
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool digitsAt(const std::string& line, std::size_t pos, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (pos + i >= line.size() || !isDigit(line[pos + i]))
        {
            return false;
        }
    }
    return true;
}

// "YYYY-MM-DD HH:MM:SS.mmm" - 23 characters, the same layout the Qt version produced
bool startsWithTimestamp(const std::string& line)
{
    return digitsAt(line, 0, 4) && line[4] == '-' && digitsAt(line, 5, 2) && line[7] == '-'
           && digitsAt(line, 8, 2) && line[10] == ' ' && digitsAt(line, 11, 2) && line[13] == ':'
           && digitsAt(line, 14, 2) && line[16] == ':' && digitsAt(line, 17, 2) && line[19] == '.'
           && digitsAt(line, 20, 3) && line[23] == ' ';
}

// Everything after the "timestamp [LEVEL] " prefix
std::string payloadOf(const std::string& line)
{
    const std::size_t bracket = line.find("] ");
    return bracket == std::string::npos ? std::string() : line.substr(bracket + 2);
}

}  // namespace

int main()
{
    std::remove(kLogPath);

    // ---------- before init ----------
    {
        check(!LogManager::isReady(), "log: not ready before init");
        check(LogManager::logFilePath().empty(), "log: the path is empty before init");
        LOG_INFO() << "written before init (must not crash)";
        check(!LogManager::isReady(), "log: writing before init does not open the file");
    }

    // ---------- init ----------
    {
        check(LogManager::init(kLogPath), "log: init opens the file");
        check(LogManager::isReady(), "log: ready after init");
        check(LogManager::logFilePath() == kLogPath, "log: logFilePath returns what init was given");

        const std::string text = readFile(kLogPath);
        check(contains(text, "===== stv3d-lab start ====="), "log: init writes the start banner");
        check(!contains(text, "written before init"), "log: pre-init message was not written to the file");
    }

    // ---------- line format and levels ----------
    {
        LOG_DEBUG() << "debug line";
        LOG_INFO() << "info line";
        LOG_WARNING() << "warning line";
        LOG_ERROR() << "error line";

        const std::vector<std::string> all = lines(readFile(kLogPath));
        check(all.size() >= 5, "log: the file has one line per message plus the banner");
        check(startsWithTimestamp(all.back()), "log: a line starts with YYYY-MM-DD HH:MM:SS.mmm");

        const std::string text = readFile(kLogPath);
        check(contains(text, "[DEBUG] debug line"), "log: LOG_DEBUG uses the DEBUG tag");
        check(contains(text, "[INFO] info line"), "log: LOG_INFO uses the INFO tag");
        check(contains(text, "[WARN] warning line"), "log: LOG_WARNING uses the WARN tag");
        check(contains(text, "[ERROR] error line"), "log: LOG_ERROR uses the ERROR tag");

        // Every real message line must carry a well-formed timestamp and a bracketed level
        int malformed = 0;
        for (const std::string& line : all)
        {
            if (line.empty() || contains(line, "====="))
            {
                continue;  // the banner is intentionally not a formatted line
            }
            if (!startsWithTimestamp(line) || line.find("] ") == std::string::npos)
            {
                ++malformed;
            }
        }
        check(malformed == 0, "log: every message line is 'timestamp [LEVEL] text'");
    }

    // ---------- streaming: numbers, strings, bools ----------
    {
        LOG_INFO() << "id = " << 42 << " name = " << std::string("cube") << " ok = " << true
                   << " bad = " << false;
        const std::vector<std::string> all = lines(readFile(kLogPath));
        check(payloadOf(all.back()) == "id = 42 name = cube ok = true bad = false",
              "log: operator<< streams ints, std::string and bool (true/false)");
    }

    // ---------- formatting helpers ----------
    {
        LOG_INFO() << "fixed2 = " << logFixed(1.5, 2) << " fixed3 = " << logFixed(-0.08f, 3);
        check(payloadOf(lines(readFile(kLogPath)).back()) == "fixed2 = 1.50 fixed3 = -0.080",
              "log: logFixed pads to the requested number of decimals");

        LOG_INFO() << "hex4 = " << logHex(0x0500u, 4) << " hex = " << logHex(0x1Fu);
        check(payloadOf(lines(readFile(kLogPath)).back()) == "hex4 = 0x0500 hex = 0x1f",
              "log: logHex prints 0x + lowercase, zero padded to the requested width");

        // A helper must not leak stream state into the rest of the line
        LOG_INFO() << logFixed(2.0, 1) << " then int " << 7 << " then float " << 0.5f;
        check(payloadOf(lines(readFile(kLogPath)).back()) == "2.0 then int 7 then float 0.5",
              "log: logFixed/logHex restore the stream state");
    }

    // ---------- one line per message, even from several threads ----------
    {
        const int threads = 4;
        const int per_thread = 50;

        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back([t, per_thread] {
                for (int i = 0; i < per_thread; ++i)
                {
                    LOG_INFO() << "thread " << t << " line " << i;
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }

        const std::string text = readFile(kLogPath);
        const std::vector<std::string> all = lines(text);

        // Compare whole payloads, not substrings: "thread 1 line 1" is a prefix of "thread 1 line 10"
        int complete = 0;
        for (int t = 0; t < threads; ++t)
        {
            for (int i = 0; i < per_thread; ++i)
            {
                std::ostringstream expected;
                expected << "thread " << t << " line " << i;
                for (const std::string& line : all)
                {
                    if (payloadOf(line) == expected.str())
                    {
                        ++complete;
                    }
                }
            }
        }
        check(complete == threads * per_thread,
              "log: 200 lines from 4 threads are all present and unsplit");
    }

    // ---------- shutdown / restart ----------
    {
        LogManager::shutdown();
        check(!LogManager::isReady(), "log: not ready after shutdown");
        check(LogManager::logFilePath() == kLogPath, "log: the path survives shutdown");

        LOG_INFO() << "after shutdown (must be safe)";
        check(!contains(readFile(kLogPath), "after shutdown"),
              "log: writing after shutdown does not touch the closed file");

        check(LogManager::init(kLogPath), "log: init can be called again");
        LOG_INFO() << "second run";
        const std::string text = readFile(kLogPath);
        check(countOccurrences(text, "===== stv3d-lab start =====") == 2,
              "log: the second init appends (the file is not truncated)");
        check(contains(text, "second run"), "log: logging works again after a restart");
        LogManager::shutdown();
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "all checks passed" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
