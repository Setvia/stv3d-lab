#ifndef CORE_PLATFORM_NATIVEWINDOWHANDLE_H
#define CORE_PLATFORM_NATIVEWINDOWHANDLE_H

// The native window, as an opaque pair of pointers.
//
// Every graphics API needs the OS window in its own spelling:
//     OpenGL (WGL)  - HWND for GetDC + SetPixelFormat
//     Vulkan        - VkWin32SurfaceCreateInfoKHR{ hinstance, hwnd }
//     D3D11/D3D12   - DXGI_SWAP_DESC.OutputWindow (HWND)
//
// core must not include <windows.h> (that would drag in near/far macros and end the layering), so
// the handle travels as void* and only the platform and render layers give it meaning. On a future
// Linux port the two fields would carry a Display* and a Window instead.

struct NativeWindowHandle
{
    void *window = nullptr;    // HWND on Windows
    void *instance = nullptr;  // HINSTANCE (module handle) on Windows

    bool isValid() const { return window != nullptr; }
};

#endif  // CORE_PLATFORM_NATIVEWINDOWHANDLE_H
