#include "GLContext.h"

#include <GL/glcorearb.h>
#include <GL/wglext.h>

#include <windows.h>

namespace
{

// WGL extension entry points, resolved once through the throwaway context below
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;

void *wglResolve(const char *name)
{
    void *address = reinterpret_cast<void *>(wglGetProcAddress(name));
    if (address == reinterpret_cast<void *>(1) || address == reinterpret_cast<void *>(2)
        || address == reinterpret_cast<void *>(3) || address == reinterpret_cast<void *>(-1)) {
        return nullptr;  // the documented "not supported" sentinels
    }
    return address;
}

// A window plus a legacy context, created only to query the WGL extension functions.
// Destroyed again before the real context is created.
bool loadWglExtensions()
{
    if (wglChoosePixelFormatARB != nullptr && wglCreateContextAttribsARB != nullptr) {
        return true;
    }

    const HINSTANCE instance = GetModuleHandleA(nullptr);
    const char *class_name = "stv3d_wgl_probe";

    WNDCLASSA window_class = {};
    window_class.lpfnWndProc = DefWindowProcA;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    // A second call would fail with ERROR_CLASS_ALREADY_EXISTS, which is fine: the class is enough
    RegisterClassA(&window_class);

    HWND window = CreateWindowExA(0, class_name, "probe", WS_OVERLAPPEDWINDOW,
                                  0, 0, 8, 8, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return false;
    }

    HDC dc = GetDC(window);

    PIXELFORMATDESCRIPTOR descriptor = {};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;

    const int format = ChoosePixelFormat(dc, &descriptor);
    bool ok = format != 0 && SetPixelFormat(dc, format, &descriptor) != FALSE;

    HGLRC legacy_context = ok ? wglCreateContext(dc) : nullptr;
    ok = legacy_context != nullptr && wglMakeCurrent(dc, legacy_context) != FALSE;

    if (ok) {
        wglChoosePixelFormatARB =
            reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(wglResolve("wglChoosePixelFormatARB"));
        wglCreateContextAttribsARB =
            reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglResolve("wglCreateContextAttribsARB"));
        wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(wglResolve("wglSwapIntervalEXT"));
    }

    wglMakeCurrent(nullptr, nullptr);
    if (legacy_context != nullptr) {
        wglDeleteContext(legacy_context);
    }
    ReleaseDC(window, dc);
    DestroyWindow(window);

    return wglChoosePixelFormatARB != nullptr && wglCreateContextAttribsARB != nullptr;
}

}  // namespace

GLContext::~GLContext()
{
    destroy();
}

bool GLContext::create(const NativeWindowHandle &window)
{
    return create(window, Config{});
}

bool GLContext::create(const NativeWindowHandle &window, const Config &config)
{
    destroy();
    error = nullptr;

    if (!window.isValid()) {
        error = "no native window handle";
        return false;
    }

    HDC dc = GetDC(static_cast<HWND>(window.window));
    if (dc == nullptr) {
        error = "GetDC failed";
        return false;
    }
    device_context = dc;

    if (!loadWglExtensions()) {
        error = "WGL_ARB_pixel_format / WGL_ARB_create_context are not available";
        destroy();
        return false;
    }

    // Pixel format: only wglChoosePixelFormatARB can request MSAA and a depth buffer explicitly
    const int pixel_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, config.depth_bits,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, config.samples > 0 ? 1 : 0,
        WGL_SAMPLES_ARB, config.samples,
        0
    };

    int format = 0;
    UINT format_count = 0;
    if (wglChoosePixelFormatARB(dc, pixel_attribs, nullptr, 1, &format, &format_count) == FALSE
        || format_count == 0) {
        error = "wglChoosePixelFormatARB found no matching pixel format";
        destroy();
        return false;
    }

    PIXELFORMATDESCRIPTOR descriptor = {};
    DescribePixelFormat(dc, format, sizeof(descriptor), &descriptor);
    if (SetPixelFormat(dc, format, &descriptor) == FALSE) {
        error = "SetPixelFormat failed (it can only be called once per window)";
        destroy();
        return false;
    }

    const int context_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, config.major,
        WGL_CONTEXT_MINOR_VERSION_ARB, config.minor,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    HGLRC render_context_handle = wglCreateContextAttribsARB(dc, nullptr, context_attribs);
    if (render_context_handle == nullptr) {
        error = "wglCreateContextAttribsARB failed: the driver does not offer this core profile";
        destroy();
        return false;
    }
    render_context = render_context_handle;

    if (!makeCurrent()) {
        error = "wglMakeCurrent failed";
        destroy();
        return false;
    }

    if (wglSwapIntervalEXT != nullptr) {
        wglSwapIntervalEXT(config.vsync ? 1 : 0);
    }
    return true;
}

void GLContext::destroy()
{
    if (render_context != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(static_cast<HGLRC>(render_context));
        render_context = nullptr;
    }
    if (device_context != nullptr) {
        ReleaseDC(WindowFromDC(static_cast<HDC>(device_context)), static_cast<HDC>(device_context));
        device_context = nullptr;
    }
}

bool GLContext::makeCurrent()
{
    if (device_context == nullptr || render_context == nullptr) {
        return false;
    }
    if (wglGetCurrentContext() == static_cast<HGLRC>(render_context)) {
        return true;  // already current; making it current again would be harmless but pointless
    }
    return wglMakeCurrent(static_cast<HDC>(device_context), static_cast<HGLRC>(render_context)) != FALSE;
}

void GLContext::swapBuffers()
{
    if (device_context != nullptr) {
        SwapBuffers(static_cast<HDC>(device_context));
    }
}
