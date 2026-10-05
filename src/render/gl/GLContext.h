#ifndef RENDER_GL_GLCONTEXT_H
#define RENDER_GL_GLCONTEXT_H

#include "core/platform/NativeWindowHandle.h"

// WGL context creation: the Win32/OpenGL equivalent of what QOpenGLWidget + QSurfaceFormat did.
//
// The sequence is the awkward part of WGL, and the reason this class exists:
//   1. a throwaway window plus a legacy context, only to reach wglChoosePixelFormatARB and
//      wglCreateContextAttribsARB (extension pointers cannot be queried without a current context)
//   2. the real window's DC gets a pixel format chosen by the ARB function - SetPixelFormat may only
//      be called once per window, so this must not be done speculatively
//   3. wglCreateContextAttribsARB asks for a 4.3 core profile context
//   4. wglMakeCurrent, then wglSwapIntervalEXT for vsync
class GLContext
{
public:
    struct Config
    {
        int major = 4;
        int minor = 3;
        int depth_bits = 24;
        int samples = 4;  // MSAA samples; 0 disables multisampling
        bool vsync = true;
    };

    GLContext() = default;
    ~GLContext();

    GLContext(const GLContext &) = delete;
    GLContext &operator=(const GLContext &) = delete;

    // Create the context for `window` and make it current; false on failure (see lastError())
    // (two overloads instead of a default argument: Config's initializers are not complete yet
    // inside the class body)
    bool create(const NativeWindowHandle &window);
    bool create(const NativeWindowHandle &window, const Config &config);
    void destroy();

    bool isValid() const { return render_context != nullptr; }

    // Make this context current (no-op when it already is); every GL call needs a current context
    bool makeCurrent();

    // Present the back buffer (SwapBuffers)
    void swapBuffers();

    // Reason the last create() failed, for the log
    const char *lastError() const { return error; }

private:
    void *device_context = nullptr;  // HDC, owned by this context
    void *render_context = nullptr;  // HGLRC
    const char *error = nullptr;
};

#endif  // RENDER_GL_GLCONTEXT_H
