#ifndef RENDER_D3D11_D3D11RENDERDEVICE_H
#define RENDER_D3D11_D3D11RENDERDEVICE_H

#include "render/rhi/RenderDevice.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Keep this header free of <windows.h>/<d3d11.h>: the device internals live in the .cpp behind a
// small opaque state struct, so app code (and the include graph around it) stays clean.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Texture2D;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

// Direct3D 11 implementation of the RHI.
//
// Mapping notes:
//   * D3D11 has an immediate context, so its command list is "issue the calls now" like OpenGL; the
//     begin()/end() contract is still honoured.
//   * Per-draw constants: every (uniform buffer, byte offset) block gets its own small constant
//     buffer. D3D11's ranged binding (ID3D11DeviceContext1::*SetConstantBuffers1 with a non-zero
//     FirstConstant) silently delivered wrong data on this machine's Intel driver, and the extra
//     buffers cost a few dozen bytes each - see D3D11RenderDevice::constantBlockBuffer.
//   * Clip space is [0,1] like Vulkan but +Y is up like OpenGL, so flipY() is false here.
//   * Shaders are HLSL compiled at runtime with D3DCompile (d3dcompiler_47.dll ships with Windows);
//     the entry point names are the fixed convention VSMain / PSMain.
class D3D11RenderDevice : public IRenderDevice
{
public:
    D3D11RenderDevice();
    ~D3D11RenderDevice() override;

    D3D11RenderDevice(const D3D11RenderDevice &) = delete;
    D3D11RenderDevice &operator=(const D3D11RenderDevice &) = delete;

    bool create(const NativeWindowHandle &window);
    void destroy();

    // IRenderDevice
    const char *backendName() const override { return backend_name.c_str(); }
    ShaderLanguage shaderLanguage() const override { return ShaderLanguage::HLSLSource; }
    std::uint32_t uniformBufferAlignment() const override { return 256; }  // 16 is required, 256 matches the Vulkan layout
    std::uint32_t framesInFlight() const override { return kFramesInFlight; }
    ClipDepth clipDepth() const override { return ClipDepth::ZeroToOne; }
    bool flipY() const override { return false; }  // D3D's NDC keeps +Y up

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

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    struct BufferSlot
    {
        // Vertex/index buffers live here. A uniform buffer does not: D3D11 cannot bind a range of a
        // constant buffer reliably (see bindUniformBuffer), so each (buffer, offset) block gets its
        // own small constant buffer on demand - `blocks` maps the offset to it.
        ID3D11Buffer *buffer = nullptr;
        std::uint32_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        bool valid = false;
        std::vector<std::uint8_t> shadow;  // CPU copy of a uniform buffer, so blocks can be re-uploaded
        std::vector<std::pair<std::uint32_t, ID3D11Buffer *>> blocks;
    };

    struct ShaderSlot
    {
        ID3D11VertexShader *vertex = nullptr;
        ID3D11PixelShader *pixel = nullptr;
        // Compiler output (ID3DBlob*) for the vertex stage: createPipeline builds the input layout
        // from it. Kept as void* because ID3DBlob is a typedef and this header stays free of d3d11.h.
        void *vertex_bytecode = nullptr;
        ShaderStage stage = ShaderStage::Vertex;
    };

    struct PipelineSlot
    {
        ID3D11InputLayout *input_layout = nullptr;
        ID3D11DepthStencilState *depth_state = nullptr;
        ID3D11RasterizerState *raster_state = nullptr;
        ShaderHandle vertex_shader = kInvalidHandle;
        ShaderHandle fragment_shader = kInvalidHandle;
        std::uint32_t vertex_stride = 0;
    };

    bool createDeviceAndSwapchain(const SwapchainDesc &desc);
    bool createBackbufferViews();
    void destroyBackbufferViews();

    // The dedicated constant buffer for one (uniform buffer, byte offset) block, created on first use
    ID3D11Buffer *constantBlockBuffer(BufferSlot &slot, std::uint32_t offset, std::uint32_t size);

    BufferSlot *bufferSlot(BufferHandle handle);
    ShaderSlot *shaderSlot(ShaderHandle handle);
    PipelineSlot *pipelineSlot(PipelineHandle handle);

    class CommandList;

    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain1 *swapchain = nullptr;
    ID3D11RenderTargetView *render_target_view = nullptr;
    ID3D11Texture2D *depth_texture = nullptr;
    ID3D11DepthStencilView *depth_view = nullptr;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool vsync = true;
    bool frame_active = false;
    bool logged_first_draw = false;  // one-shot diagnostic
    std::string backend_name;
    const char *error = nullptr;

    std::vector<BufferSlot> buffers;
    std::vector<ShaderSlot> shaders;
    std::vector<PipelineSlot> pipelines;
    PipelineSlot *bound_pipeline = nullptr;

    std::unique_ptr<CommandList> command_list;
};

#endif  // RENDER_D3D11_D3D11RENDERDEVICE_H
