#include "app/Sandbox.h"

#include "core/log/LogManager.h"
#include "game/InputMapping.h"
#include "platform/win32/Win32Module.h"
#include "platform/win32/Win32Window.h"
#include "render/gl/GLContext.h"
#include "render/gl/GLFunctions.h"

#include <cpr/cpr.h>

// stv3d-lab entry point.
//
// No Qt and no hidden event loop: this is a plain main() that owns the window, the GL context and
// the message pump, and drives the game loop itself. The shape of it is exactly what a Vulkan or
// D3D11 backend will slot into - only the render calls change.
int main()
{
    // ---- logging first: every later step wants to report somewhere ----
    // The exe directory (not the working directory) decides where the log goes
    const std::string exe_directory = Win32Module::executableDirectory();
    if (!LogManager::init(exe_directory + "/stv3d-lab.log")) {
        return 1;  // if logging cannot even be opened there is no point continuing
    }

    // ---- window ----
    Win32Window::Config window_config;
    window_config.title = "stv3d-lab";
    window_config.width = 800;
    window_config.height = 600;

    Win32Window window;
    if (!window.create(window_config)) {
        LOG_ERROR() << "window creation failed";
        LogManager::shutdown();
        return 1;
    }

    // ---- OpenGL 4.3 core context and its function table ----
    // The renderer needs glVertexAttribFormat / glVertexAttribBinding / glBindVertexBuffer, which
    // only entered the core profile in OpenGL 4.3, so the context is requested explicitly.
    const GLContext::Config context_config;  // 4.3 core, 24-bit depth, 4x MSAA, vsync
    GLContext gl_context;
    if (!gl_context.create(window.nativeHandle(), context_config)) {
        LOG_ERROR() << "OpenGL context creation failed: " << gl_context.lastError();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    GLFunctions gfx;
    if (!gfx.load()) {
        LOG_ERROR() << "OpenGL function missing (context too old for 4.3 core): " << gfx.missingFunction();
        gl_context.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    GLint profile_mask = 0;
    gfx.glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    LOG_INFO() << "GL context: " << context_config.major << "." << context_config.minor
               << " | core profile: " << ((profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0)
               << " | GL_VERSION: " << reinterpret_cast<const char *>(gfx.glGetString(GL_VERSION))
               << " | GPU: " << reinterpret_cast<const char *>(gfx.glGetString(GL_RENDERER));

    // ---- scene ----
    Sandbox sandbox;
    if (!sandbox.createResources(gfx, exe_directory + "/shaders")) {
        LOG_ERROR() << "scene creation failed";
        gl_context.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }
    sandbox.resize(window.clientWidth(), window.clientHeight(), gfx);

    const GLenum init_error = gfx.glGetError();
    if (init_error != GL_NO_ERROR) {
        LOG_WARNING() << "GL error after initialization, code = " << logHex(init_error, 4);
    }

    // Network request test (kept from the Qt era: it only proves the cpr dependency works)
    const cpr::Response response = cpr::Get(cpr::Url{"https://httpbin.org/get"});
    LOG_INFO() << "Status: " << response.status_code;
    LOG_INFO() << "Content: " << response.text;

    // ---- main loop ----
    // One turn = pump the window messages, apply input, advance the game loop (which runs the fixed
    // logic ticks), then draw and present. SwapBuffers with vsync caps the frame rate; the logic
    // ticks stay on their own fixed 1/60 s step.
    while (window.pumpMessages()) {
        const FrameInput &input = window.input();

        if (InputMapping::quitRequested(input)) {
            break;
        }

        sandbox.handleInput(input);

        if (input.resized) {
            sandbox.resize(window.clientWidth(), window.clientHeight(), gfx);
        }

        sandbox.getGameLoop().advance();

        gl_context.makeCurrent();
        sandbox.render(gfx);
        gl_context.swapBuffers();

        window.endFrame();
    }

    // ---- shutdown: GL objects must die while the context is still current ----
    gl_context.makeCurrent();
    sandbox.releaseResources();
    gl_context.destroy();
    window.destroy();

    LOG_INFO() << "===== stv3d-lab exit, code = 0 =====";
    LogManager::shutdown();
    return 0;
}
