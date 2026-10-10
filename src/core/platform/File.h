#pragma once

#ifndef CORE_PLATFORM_FILE_H
#define CORE_PLATFORM_FILE_H

#include <cstdint>
#include <string>
#include <vector>

// Whole-file reading, standard library only.
//
// Shaders are read this way (GLSL or HLSL text, SPIR-V binaries), which is why both a text and a
// binary variant exist. Failures are logged and reported as false; callers decide whether that is
// fatal.
namespace File
{

bool readTextFile(const std::string &path, std::string &outText);
bool readBinaryFile(const std::string &path, std::vector<std::uint8_t> &outBytes);

}  // namespace File

#endif  // CORE_PLATFORM_FILE_H
