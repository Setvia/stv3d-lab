#ifndef RENDER_GL_GLRENDERDEVICE_H
#define RENDER_GL_GLRENDERDEVICE_H

#include "render/gl/GLContext.h"
#include "render/gl/GLFunctions.h"
#include "render/rhi/RenderDevice.h"

#include <memory>
#include <string>
#include <vector>

// OpenGL 4.3 implementation of the RHI.
//
// How the explicit frame model maps onto GL:
//   beginFrame  -> makeCurrent + viewport from the swapchain + command list begin
//   record      -> plain GL calls (GL has no command buffer; the state machine *is* the recording)
//   endFrame    -> command list end + SwapBuffers
//   waitIdle    -> glFinish
//
// Resource handles are indices into the slot vectors below (0 = invalid). The device owns the GL
// context, so create()/destroy() bracket everything else.
class GLRenderDevice : public IRenderDevice
{
public:
    GLRenderDevice();
    ~GLRenderDevice() override;

    GLRenderDevice(const GLRenderDevice &) = delete;
    GLRenderDevice &operator=(const GLRenderDevice &) = delete;

    // Create the WGL context for `window` and resolve the function table.
    // (Two overloads rather than a default argument: GLContext::Config is not complete yet inside a
    // default argument of the class that declares it.)
    bool create(const NativeWindowHandle &window);
    bool create(const NativeWindowHandle &window, const GLContext::Config &config);
    void destroy();

    // IRenderDevice
    const char *backendName() const override;
    ShaderLanguage shaderLanguage() const override { return ShaderLanguage::GLSLSource; }
    std::uint32_t uniformBufferAlignment() const override { return uniform_buffer_alignment; }
    std::uint32_t framesInFlight() const override { return 1; }  // GL calls take effect immediately
    ClipDepth clipDepth() const override { return ClipDepth::NegativeOneToOne; }  // GL convention
    bool flipY() const override { return false; }

    bool createSwapchain(const SwapchainDesc &desc) override;
    void destroySwapchain() override;
    bool resizeSwapchain(std::uint32_t width, std::uint32_t height) override;
    std::uint32_t swapchainWidth() const override { return width; }
    std::uint32_t swapchainHeight() const override { return height; }

    BufferHandle createBuffer(const BufferDesc &desc) override;
    void destroyBuffer(BufferHandle buffer) override;
    void updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                      std::uint32_t offset) override;

    ShaderHandle createShader(const ShaderDesc &desc) override;
    void destroyShader(ShaderHandle shader) override;

    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    void destroyPipeline(PipelineHandle pipeline) override;

    ICommandList &getCommandList() override;
    void beginFrame() override;
    void endFrame() override;
    void waitIdle() override;

    const char *lastError() const override { return error; }

    // Backend-specific escape hatch (probes, tests, and the pieces of GL that the RHI does not cover
    // yet). The app is not supposed to need this.
    GLFunctions &functions() { return gfx; }
    GLContext &context() { return gl_context; }

    // Read glGetError() once and log it: GL reports errors asynchronously, so this is how a failure
    // during setup becomes visible instead of silently producing a black frame
    void reportErrors(const char *stage);

private:
    struct BufferSlot
    {
        GLuint id = 0;
        GLenum target = 0;
        BufferUsage usage = BufferUsage::Vertex;
    };

    struct ShaderSlot
    {
        GLuint id = 0;
        ShaderStage stage = ShaderStage::Vertex;
    };

    struct PipelineSlot
    {
        GLuint program = 0;
        // The vertex layout is VAO state, so it is re-applied whenever a pipeline is bound; the
        // attributes are copied because PipelineDesc only borrows the caller's array.
        std::vector<VertexAttribute> attributes;
        std::uint32_t vertex_stride = 0;
        bool depth_test = true;
        bool depth_write = true;
    };

    BufferSlot *bufferSlot(BufferHandle handle);
    ShaderSlot *shaderSlot(ShaderHandle handle);
    PipelineSlot *pipelineSlot(PipelineHandle handle);

    GLContext gl_context;
    GLFunctions gfx;
    std::string backend_name;
    const char *error = nullptr;

    GLuint vertex_array = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t uniform_buffer_alignment = 16;  // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, queried in create()

    std::vector<BufferSlot> buffers;
    std::vector<ShaderSlot> shaders;
    std::vector<PipelineSlot> pipelines;

    class CommandList;  // defined in the .cpp: it needs the device internals
    std::unique_ptr<CommandList> command_list;

    // Bindings the command list recorded, so that bindVertexBuffer/bindIndexBuffer can do their work
    PipelineSlot *bound_pipeline = nullptr;
};

#endif  // RENDER_GL_GLRENDERDEVICE_H
