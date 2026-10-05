#include "app/Sandbox.h"

#include "core/log/LogManager.h"
#include "game/InputMapping.h"
#include "platform/win32/Win32Module.h"
#include "platform/win32/Win32Window.h"
#include "render/gl/GLRenderDevice.h"
#include "render/rhi/RenderDevice.h"

#include <cpr/cpr.h>

// stv3d-lab entry point.
//
// A plain main(): log, window, render device, scene, then pump/input/advance/render/present. The only
// line that names a graphics API is the concrete device below - everything else talks to
// IRenderDevice, which is what makes the Vulkan (A6) and D3D11 (A7) backends a local change.
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

    // ---- render device ----
    GLRenderDevice gl_device;      // the backend choice lives exactly here
    IRenderDevice &device = gl_device;

    if (!gl_device.create(window.nativeHandle())) {
        LOG_ERROR() << "render device creation failed: " << device.lastError();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    SwapchainDesc swapchain_desc;
    swapchain_desc.window = window.nativeHandle();
    swapchain_desc.width = static_cast<std::uint32_t>(window.clientWidth());
    swapchain_desc.height = static_cast<std::uint32_t>(window.clientHeight());
    swapchain_desc.vsync = true;

    if (!device.createSwapchain(swapchain_desc)) {
        LOG_ERROR() << "swapchain creation failed: " << device.lastError();
        gl_device.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    const char *dialect = "unknown";
    switch (device.shaderLanguage()) {
        case ShaderLanguage::GLSLSource: dialect = "GLSL"; break;
        case ShaderLanguage::SPIRV:      dialect = "SPIR-V"; break;
        case ShaderLanguage::HLSLSource: dialect = "HLSL"; break;
    }
    LOG_INFO() << "render backend: " << device.backendName() << " | shaders: " << dialect
               << " | swapchain: " << device.swapchainWidth() << "x" << device.swapchainHeight();

    // ---- scene ----
    Sandbox sandbox;
    if (!sandbox.createResources(device, exe_directory)) {
        LOG_ERROR() << "scene creation failed";
        device.destroySwapchain();
        gl_device.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }
    sandbox.resize(window.clientWidth(), window.clientHeight());

    // Backend-specific diagnostics after the first resources exist (the GL backend reads glGetError)
    gl_device.reportErrors("after scene creation");

    // Network request test (kept from the Qt era: it only proves the cpr dependency works)
    const cpr::Response response = cpr::Get(cpr::Url{"https://httpbin.org/get"});
    LOG_INFO() << "Status: " << response.status_code;
    LOG_INFO() << "Content: " << response.text;

    // ---- main loop ----
    // One turn: pump the window messages, apply input, advance the game loop (fixed logic ticks),
    // then record and submit one frame. vsync in endFrame() caps the frame rate; the logic ticks stay
    // on their own fixed 1/60 s step.
    while (window.pumpMessages()) {
        const FrameInput &input = window.input();

        if (InputMapping::quitRequested(input)) {
            break;
        }

        sandbox.handleInput(input);

        if (input.resized) {
            device.resizeSwapchain(static_cast<std::uint32_t>(window.clientWidth()),
                                   static_cast<std::uint32_t>(window.clientHeight()));
            sandbox.resize(window.clientWidth(), window.clientHeight());
        }

        sandbox.getGameLoop().advance();

        device.beginFrame();
        sandbox.render(device);
        device.endFrame();

        window.endFrame();
    }

    // ---- shutdown: GPU work first, then resources, then the device and the window ----
    device.waitIdle();
    sandbox.releaseResources();
    device.destroySwapchain();
    gl_device.destroy();
    window.destroy();

    LOG_INFO() << "===== stv3d-lab exit, code = 0 =====";
    LogManager::shutdown();
    return 0;
}
