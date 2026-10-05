#ifndef RENDER_RHI_RENDERDEVICE_H
#define RENDER_RHI_RENDERDEVICE_H

#include "render/rhi/RenderTypes.h"

// The render hardware interface: what the app talks to instead of talking to a graphics API.
//
// Shape of the interface - the important decision:
//
//   * It follows the EXPLICIT frame model that Vulkan and D3D12 force on you: a frame is acquired,
//     a command list is recorded between begin() and end(), and the frame is submitted and presented.
//     OpenGL's immediate state machine is mapped onto that shape (its command list is "issue the
//     calls now"), because the reverse - designing around GL and bolting Vulkan on later - does not
//     work.
//   * Resources are opaque handles, never API objects, so the app cannot accidentally depend on the
//     backend it happens to run on.
//   * Shaders are blobs: the device says which dialect it wants (ShaderLanguage), the app hands over
//     bytes. A backend switch therefore means loading a different shader file, not editing app code.
//
// Status (step A5): the OpenGL backend implements this. Vulkan (A6) and D3D11 (A7) follow, and the
// app is expected to need no changes beyond picking a device at startup.

// Everything recorded for one frame.
//
// OpenGL: begin()/end() only toggle a flag - GL calls take effect immediately, which is exactly why
// the interface does not allow recording outside begin()/end(): Vulkan and D3D12 would silently
// misbehave, so the contract is enforced from the start.
class ICommandList
{
public:
    virtual ~ICommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    // Viewport in pixels (usually the swapchain size; beginFrame already sets that)
    virtual void setViewport(std::uint32_t width, std::uint32_t height) = 0;

    // Clear the colour and depth attachments currently bound as the render target
    virtual void clear(float red, float green, float blue, float alpha, float depth) = 0;

    virtual void bindPipeline(PipelineHandle pipeline) = 0;

    // Stride comes from the pipeline; binding index 0 is used for the interleaved vertex buffer
    virtual void bindVertexBuffer(BufferHandle buffer) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, IndexFormat format) = 0;

    // Constant buffer for the shaders.
    //
    // The offset/size pair is what makes per-draw constants work on Vulkan and D3D12: a frame in
    // flight must not have its constants overwritten, so the app writes every draw's block into one
    // buffer at aligned offsets (see IRenderDevice::uniformBufferAlignment) and binds a range.
    // OpenGL maps it to glBindBufferRange, which is the same thing.
    virtual void bindUniformBuffer(BufferHandle buffer, std::uint32_t slot, std::uint32_t offset,
                                   std::uint32_t size) = 0;

    virtual void drawIndexed(std::uint32_t index_count, std::uint32_t first_index = 0) = 0;
};

class IRenderDevice
{
public:
    virtual ~IRenderDevice() = default;

    // "OpenGL 4.3 (WGL)" / "Vulkan 1.3" / "D3D11 feature level 11_0" - for the log
    virtual const char *backendName() const = 0;

    // Which shader dialect createShader() expects
    virtual ShaderLanguage shaderLanguage() const = 0;

    // Required alignment for uniform buffer ranges: lay per-draw constant blocks out at multiples of
    // this (Vulkan: minUniformBufferOffsetAlignment, often 64 or 256; OpenGL: 16 is enough for std140)
    virtual std::uint32_t uniformBufferAlignment() const = 0;

    // How many frames the backend may have in flight. The app must not rewrite constants a frame in
    // flight may still read, so it keeps one constant-block region per frame in flight
    // (OpenGL: 1, Vulkan/D3D12: 2).
    virtual std::uint32_t framesInFlight() const = 0;

    // How this backend wants clip space. OpenGL maps NDC z to [-1, 1] with +Y up; Vulkan wants
    // [0, 1] with +Y down. The app hands both to its camera, so the projection matrix is built
    // correctly per backend instead of being patched up in a shader.
    virtual ClipDepth clipDepth() const = 0;
    virtual bool flipY() const = 0;

    // ---- swapchain ----
    // The surface is the native window; the backend creates whatever it needs from it
    virtual bool createSwapchain(const SwapchainDesc &desc) = 0;
    virtual void destroySwapchain() = 0;
    virtual bool resizeSwapchain(std::uint32_t width, std::uint32_t height) = 0;
    virtual std::uint32_t swapchainWidth() const = 0;
    virtual std::uint32_t swapchainHeight() const = 0;

    // ---- resources ----
    virtual BufferHandle createBuffer(const BufferDesc &desc) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;

    // Rewrite part of a buffer (the usual way to feed a uniform buffer once per draw)
    virtual void updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                              std::uint32_t offset = 0) = 0;

    virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
    virtual void destroyShader(ShaderHandle shader) = 0;

    virtual PipelineHandle createPipeline(const PipelineDesc &desc) = 0;
    virtual void destroyPipeline(PipelineHandle pipeline) = 0;

    // ---- frame ----
    virtual ICommandList &getCommandList() = 0;
    virtual void beginFrame() = 0;  // acquire the next back buffer and make the device current
    virtual void endFrame() = 0;    // submit the recorded work and present

    // Block until the GPU is done; needed before destroying resources or the swapchain
    virtual void waitIdle() = 0;

    // Human-readable reason the last create* call failed
    virtual const char *lastError() const = 0;
};

#endif  // RENDER_RHI_RENDERDEVICE_H
