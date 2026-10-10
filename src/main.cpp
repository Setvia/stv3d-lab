#include "app/Sandbox.h"

#include "core/log/LogManager.h"
#include "game/InputMapping.h"
#include "platform/win32/Win32Module.h"
#include "platform/win32/Win32Window.h"
#include "render/d3d11/D3D11RenderDevice.h"
#include "render/gl/GLRenderDevice.h"
#include "render/rhi/RenderDevice.h"
#include "render/vk/VulkanRenderDevice.h"

#include <cpr/cpr.h>

#include <string>

namespace
{

// Command line: --api gl|vk|d3d11 (default gl). This is the assembly root, so it is the one place
// that knows which backends exist; everything below receives an IRenderDevice&.
std::string parseRequestedApi(int argc, char **argv)
{
    std::string api = "gl";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--api" && i + 1 < argc) {
            api = argv[++i];
        }
    }
    return api;
}

const char *dialectName(ShaderLanguage language)
{
    switch (language) {
        case ShaderLanguage::GLSLSource: return "GLSL";
        case ShaderLanguage::SPIRV:      return "SPIR-V";
        case ShaderLanguage::HLSLSource: return "HLSL";
    }
    return "unknown";
}

}  // namespace

// stv3d-lab entry point.
//
// A plain main(): log, window, render device, scene, then pump/input/advance/render/present. The only
// lines that name a graphics API are the two concrete devices below - everything else talks to
// IRenderDevice, which is what makes picking a backend a command-line flag instead of a rewrite.
int main(int argc, char **argv)
{
    // ---- logging first: every later step wants to report somewhere ----
    // The exe directory (not the working directory) decides where the log goes
    const std::string exe_directory = Win32Module::executableDirectory();
    if (!LogManager::init(exe_directory + "/stv3d-lab.log")) {
        return 1;  // if logging cannot even be opened there is no point continuing
    }

    const std::string requested_api = parseRequestedApi(argc, argv);
    LOG_INFO() << "requested API: " << requested_api;

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
    GLRenderDevice gl_device;
    VulkanRenderDevice vk_device;
    D3D11RenderDevice d3d11_device;

    // const bool use_opengl = requested_api == "gl" || requested_api == "opengl";
    const bool use_vulkan = requested_api == "vk" || requested_api == "vulkan";
    const bool use_d3d11 = requested_api == "d3d11" || requested_api == "d3d" || requested_api == "dx11";

    IRenderDevice *device = &gl_device;
    if (use_vulkan) {
        device = &vk_device;
    } else if (use_d3d11) {
        device = &d3d11_device;
    }

    bool device_created = false;
    if (use_vulkan) {
        device_created = vk_device.create(window.nativeHandle());
    } else if (use_d3d11) {
        device_created = d3d11_device.create(window.nativeHandle());
    } else {
        device_created = gl_device.create(window.nativeHandle());
    }

    if (!device_created) {
        LOG_ERROR() << "render device creation failed: " << device->lastError();
        gl_device.destroy();
        vk_device.destroy();
        d3d11_device.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    SwapchainDesc swapchain_desc;
    swapchain_desc.window = window.nativeHandle();
    swapchain_desc.width = static_cast<std::uint32_t>(window.clientWidth());
    swapchain_desc.height = static_cast<std::uint32_t>(window.clientHeight());
    swapchain_desc.vsync = true;

    if (!device->createSwapchain(swapchain_desc)) {
        LOG_ERROR() << "swapchain creation failed: " << device->lastError();
        gl_device.destroy();
        vk_device.destroy();
        d3d11_device.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }

    LOG_INFO() << "render backend: " << device->backendName()
               << " | shaders: " << dialectName(device->shaderLanguage())
               << " | swapchain: " << device->swapchainWidth() << "x" << device->swapchainHeight()
               << " | frames in flight: " << device->framesInFlight()
               << " | uniform alignment: " << device->uniformBufferAlignment();

    // ---- scene ----
    Sandbox sandbox;
    if (!sandbox.createResources(*device, exe_directory)) {
        LOG_ERROR() << "scene creation failed";
        device->destroySwapchain();
        gl_device.destroy();
        vk_device.destroy();
        d3d11_device.destroy();
        window.destroy();
        LogManager::shutdown();
        return 1;
    }
    sandbox.resize(window.clientWidth(), window.clientHeight());

    // Backend-specific diagnostics after the first resources exist (the GL backend reads glGetError)
    if (!use_vulkan && !use_d3d11) {
        gl_device.reportErrors("after scene creation");
    }

    // Network request test: proves the cpr dependency works
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
            device->resizeSwapchain(static_cast<std::uint32_t>(window.clientWidth()),
                                    static_cast<std::uint32_t>(window.clientHeight()));
            sandbox.resize(window.clientWidth(), window.clientHeight());
        }

        sandbox.getGameLoop().advance();

        device->beginFrame();
        sandbox.render(*device);
        device->endFrame();

        window.endFrame();
    }

    // ---- shutdown: GPU work first, then resources, then the device and the window ----
    device->waitIdle();
    sandbox.releaseResources();
    device->destroySwapchain();
    gl_device.destroy();
    vk_device.destroy();
    d3d11_device.destroy();
    window.destroy();

    LOG_INFO() << "===== stv3d-lab exit, code = 0 =====";
    LogManager::shutdown();
    return 0;
}
