#include "core/platform/File.h"

#include "core/log/LogManager.h"

#include <fstream>
#include <sstream>

namespace File
{

bool readTextFile(const std::string &path, std::string &outText)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR() << "cannot open file: " << path;
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    outText = contents.str();

    if (outText.find_first_not_of(" \t\r\n") == std::string::npos) {
        LOG_ERROR() << "file is empty: " << path;
        return false;
    }
    return true;
}

bool readBinaryFile(const std::string &path, std::vector<std::uint8_t> &outBytes)
{
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR() << "cannot open file: " << path;
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        LOG_ERROR() << "file is empty: " << path;
        return false;
    }
    file.seekg(0, std::ios::beg);

    outBytes.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char *>(outBytes.data()), size)) {
        LOG_ERROR() << "short read on file: " << path;
        outBytes.clear();
        return false;
    }
    return true;
}

}  // namespace File
