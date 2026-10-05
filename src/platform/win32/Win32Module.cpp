#include "Win32Module.h"

#include <windows.h>

#include <vector>

namespace Win32Module
{

namespace
{

std::string wideToUtf8(const std::wstring &text)
{
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &result[0], size, nullptr, nullptr);
    return result;
}

}  // namespace

std::string executablePath()
{
    // Ask for the size first: a long path (or a network install) can exceed MAX_PATH
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::string();
        }
        if (length < buffer.size()) {
            return wideToUtf8(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);  // truncated: try again with more room
    }
}

std::string executableDirectory()
{
    std::string path = executablePath();
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

}  // namespace Win32Module
