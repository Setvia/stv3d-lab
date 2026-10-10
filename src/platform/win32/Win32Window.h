#ifndef PLATFORM_WIN32_WIN32WINDOW_H
#define PLATFORM_WIN32_WIN32WINDOW_H

#include "core/platform/FrameInput.h"
#include "core/platform/NativeWindowHandle.h"

// The Win32 window: window class, message pump and input collection.
//
// It provides the HWND, pumps messages and reports input; creating a graphics context is the render
// layer's job (see render/gl/GLContext.h). Keeping those apart is what lets the same window serve the
// OpenGL, Vulkan and D3D11 backends.
//
// This header does not include <windows.h>: the message handler is spelled with plain integers
// (see handleMessage) so that the classic macro landmines (`near`, `far`, `min`, `max`) cannot leak
// into the engine or the app.
class Win32Window
{
public:
    struct Config
    {
        const char *title = "stv3d-lab";
        int width = 800;   // client area, in pixels
        int height = 600;
    };

    Win32Window() = default;
    ~Win32Window();

    Win32Window(const Win32Window &) = delete;
    Win32Window &operator=(const Win32Window &) = delete;

    // Register the class, create and show the window; false on failure
    bool create(const Config &config);
    void destroy();

    bool isValid() const { return handle.isValid(); }

    // HWND + HINSTANCE for the render backends (WGL, VkSurfaceKHR, DXGI swapchain)
    NativeWindowHandle nativeHandle() const { return handle; }

    // Process every message that is currently queued (never blocks) and return false once the
    // window has been closed. Call this once per turn of the main loop.
    bool pumpMessages();

    // Ask the window to close: handled like a click on the close button
    void requestClose();

    // Input collected by pumpMessages(). Read it, act on it, then call endFrame().
    const FrameInput &input() const { return frame_input; }
    void endFrame() { frame_input.clearPerFrame(); }

    int clientWidth() const { return frame_input.client_width; }
    int clientHeight() const { return frame_input.client_height; }

    // Message handler, called from the static window procedure.
    // Spelled with plain integers so this header stays free of <windows.h>; the .cpp static_asserts
    // that they match HWND/WPARAM/LPARAM/LRESULT on this platform.
    long long handleMessage(void *window, unsigned int message, unsigned long long wparam, long long lparam);

private:
    bool processKey(bool is_down, unsigned long long virtual_key);

    NativeWindowHandle handle;
    FrameInput frame_input;
    bool closed = false;
};

#endif  // PLATFORM_WIN32_WIN32WINDOW_H
