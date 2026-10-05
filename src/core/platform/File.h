#pragma once

#ifndef CORE_PLATFORM_FILE_H
#define CORE_PLATFORM_FILE_H

#include <cstdint>
#include <string>
#include <vector>

// Whole-file reading, std-only (no Qt, no OS API).
//
// Shaders are read this way (GLSL text today, SPIR-V blobs from step A6 on), which is why both a
// text and a binary variant exist. Failures are logged and reported as false; callers decide whether
// that is fatal.
namespace File
{

bool readTextFile(const std::string &path, std::string &outText);
bool readBinaryFile(const std::string &path, std::vector<std::uint8_t> &outBytes);

}  // namespace File

#endif  // CORE_PLATFORM_FILE_H
