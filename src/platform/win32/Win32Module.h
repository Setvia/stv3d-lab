#ifndef PLATFORM_WIN32_WIN32MODULE_H
#define PLATFORM_WIN32_WIN32MODULE_H

#include <string>

// Where the executable lives.
//
// The app needs this to find its own files (the log, shaders/) without depending on the working
// directory, which is whatever the shell happened to be in.

namespace Win32Module
{

// Directory that contains the executable, without a trailing separator ("D:/build")
std::string executableDirectory();

// Full path of the executable ("D:/build/stv3d-lab.exe")
std::string executablePath();

}  // namespace Win32Module

#endif  // PLATFORM_WIN32_WIN32MODULE_H
