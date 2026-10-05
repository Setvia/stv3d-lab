#include "GLFunctions.h"

#include <windows.h>

namespace
{

// wglGetProcAddress reports failure with 0 and with the sentinel values 1..3 and -1
bool isUsable(void *address)
{
    return address != nullptr
           && address != reinterpret_cast<void *>(1)
           && address != reinterpret_cast<void *>(2)
           && address != reinterpret_cast<void *>(3)
           && address != reinterpret_cast<void *>(-1);
}

void *resolve(const char *name)
{
    void *address = reinterpret_cast<void *>(wglGetProcAddress(name));
    if (isUsable(address)) {
        return address;
    }

    // The OpenGL 1.1 subset is exported straight from opengl32.dll; wglGetProcAddress refuses those
    static HMODULE opengl_module = LoadLibraryA("opengl32.dll");
    if (opengl_module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void *>(GetProcAddress(opengl_module, name));
}

}  // namespace

bool GLFunctions::load()
{
    loaded = false;
    missing = nullptr;

    // One line per entry point: resolve it, and remember the first name that does not resolve
#define X(type, name)                                \
    name = reinterpret_cast<type>(resolve(#name));   \
    if (name == nullptr) {                           \
        missing = #name;                             \
        return false;                                \
    }
#include "GLFunctions.inc"
#undef X

    loaded = true;
    return true;
}
