// D3D11 headers need windows.h first, and NOMINMAX keeps std::min/std::max usable.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include "render/d3d11/D3D11RenderDevice.h"

#include "core/log/LogManager.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{

template <typename T>
void release(T *&object)
{
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

DXGI_FORMAT vertexAttributeFormat(VertexFormat format)
{
    switch (format) {
        case VertexFormat::Float32:   return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float32x2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float32x3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float32x4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// Input layout semantics follow the attribute location: 0 -> POSITION, 1 -> COLOR, the rest ->
// TEXCOORD<location>. basic.hlsl is written against exactly that.
const char *semanticName(std::uint32_t location)
{
    if (location == 0) {
        return "POSITION";
    }
    if (location == 1) {
        return "COLOR";
    }
    return "TEXCOORD";
}

UINT semanticIndex(std::uint32_t location)
{
    return location >= 2 ? location : 0;
}

std::string wideToUtf8(const wchar_t *text)
{
    if (text == nullptr) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return std::string();
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    return result;
}

const char *featureLevelName(D3D_FEATURE_LEVEL level)
{
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        default: return "?";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Command list
//
// D3D11 has an immediate context, so recording a command *is* executing it (like OpenGL). The
// begin()/end() contract is kept anyway: that is the shape every other backend needs.
// ---------------------------------------------------------------------------
class D3D11RenderDevice::CommandList : public ICommandList
{
public:
    explicit CommandList(D3D11RenderDevice &device) : device(device) {}

    void begin() override { recording = true; }
    void end() override { recording = false; }

    void setViewport(std::uint32_t width, std::uint32_t height) override
    {
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        device.context->RSSetViewports(1, &viewport);
    }

    void clear(float red, float green, float blue, float alpha, float depth) override
    {
        if (device.render_target_view == nullptr) {
            LOG_ERROR() << "d3d11: clear() without a render target (create the swapchain first)";
            return;
        }

        const float color[4] = {red, green, blue, alpha};
        device.context->ClearRenderTargetView(device.render_target_view, color);
        if (device.depth_view != nullptr) {
            // The depth format has no stencil, so only the depth part is cleared
            device.context->ClearDepthStencilView(device.depth_view, D3D11_CLEAR_DEPTH, depth, 0);
        }
    }

    void bindPipeline(PipelineHandle pipeline) override
    {
        D3D11RenderDevice::PipelineSlot *slot = device.pipelineSlot(pipeline);
        D3D11RenderDevice::ShaderSlot *vertex_shader =
            slot != nullptr ? device.shaderSlot(slot->vertex_shader) : nullptr;
        D3D11RenderDevice::ShaderSlot *fragment_shader =
            slot != nullptr ? device.shaderSlot(slot->fragment_shader) : nullptr;
        if (slot == nullptr || vertex_shader == nullptr || fragment_shader == nullptr) {
            LOG_ERROR() << "d3d11: bindPipeline with an invalid handle " << pipeline;
            return;
        }

        device.context->IASetInputLayout(slot->input_layout);
        device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device.context->VSSetShader(vertex_shader->vertex, nullptr, 0);
        device.context->PSSetShader(fragment_shader->pixel, nullptr, 0);
        device.context->OMSetDepthStencilState(slot->depth_state, 0);
        device.context->RSSetState(slot->raster_state);
        device.bound_pipeline = slot;
    }

    void bindVertexBuffer(BufferHandle buffer) override
    {
        D3D11RenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Vertex) {
            LOG_ERROR() << "d3d11: bindVertexBuffer with a non-vertex buffer " << buffer;
            return;
        }
        if (device.bound_pipeline == nullptr) {
            LOG_ERROR() << "d3d11: bind a pipeline first (it carries the vertex stride)";
            return;
        }

        const UINT stride = device.bound_pipeline->vertex_stride;
        const UINT offset = 0;
        device.context->IASetVertexBuffers(0, 1, &slot->buffer, &stride, &offset);
    }

    void bindIndexBuffer(BufferHandle buffer, IndexFormat format) override
    {
        D3D11RenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Index) {
            LOG_ERROR() << "d3d11: bindIndexBuffer with a non-index buffer " << buffer;
            return;
        }
        const DXGI_FORMAT dxgi_format = format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT
                                                                     : DXGI_FORMAT_R32_UINT;
        device.context->IASetIndexBuffer(slot->buffer, dxgi_format, 0);
    }

    void bindUniformBuffer(BufferHandle buffer, std::uint32_t slot, std::uint32_t offset,
                           std::uint32_t size) override
    {
        D3D11RenderDevice::BufferSlot *buffer_slot = device.bufferSlot(buffer);
        if (buffer_slot == nullptr || buffer_slot->usage != BufferUsage::Uniform) {
            LOG_ERROR() << "d3d11: bindUniformBuffer with a non-uniform buffer " << buffer;
            return;
        }
        if (offset + size > buffer_slot->shadow.size()) {
            LOG_ERROR() << "d3d11: bindUniformBuffer range outside the buffer";
            return;
        }
        (void)slot;  // v1 binds the block to b0 in both stages, as basic.hlsl declares

        // One small constant buffer per block, uploaded and bound whole: see constantBlockBuffer for
        // why ranged binding is not used here
        ID3D11Buffer *block_buffer = device.constantBlockBuffer(*buffer_slot, offset, size);
        if (block_buffer == nullptr) {
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(device.context->Map(block_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            LOG_ERROR() << "d3d11: Map failed while uploading constants";
            return;
        }
        std::memcpy(mapped.pData, buffer_slot->shadow.data() + offset, size);
        device.context->Unmap(block_buffer, 0);

        device.context->VSSetConstantBuffers(0, 1, &block_buffer);
        device.context->PSSetConstantBuffers(0, 1, &block_buffer);
    }

    void drawIndexed(std::uint32_t index_count, std::uint32_t first_index) override
    {
        if (!recording) {
            LOG_ERROR() << "d3d11: drawIndexed outside of begin()/end()";
            return;
        }
        if (!device.logged_first_draw) {
            device.logged_first_draw = true;
            LOG_INFO() << "d3d11: first draw: " << index_count << " indices, stride "
                       << (device.bound_pipeline != nullptr ? device.bound_pipeline->vertex_stride : 0)
                       << ", viewport " << device.width << "x" << device.height;
        }
        device.context->DrawIndexed(index_count, first_index, 0);
    }

private:
    D3D11RenderDevice &device;
    bool recording = false;
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

D3D11RenderDevice::D3D11RenderDevice() = default;

D3D11RenderDevice::~D3D11RenderDevice()
{
    destroy();
}

D3D11RenderDevice::BufferSlot *D3D11RenderDevice::bufferSlot(BufferHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > buffers.size()) {
        return nullptr;
    }
    BufferSlot &slot = buffers[index - 1];
    return slot.valid ? &slot : nullptr;
}

D3D11RenderDevice::ShaderSlot *D3D11RenderDevice::shaderSlot(ShaderHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > shaders.size()) {
        return nullptr;
    }
    ShaderSlot &slot = shaders[index - 1];
    return (slot.vertex == nullptr && slot.pixel == nullptr) ? nullptr : &slot;
}

D3D11RenderDevice::PipelineSlot *D3D11RenderDevice::pipelineSlot(PipelineHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > pipelines.size()) {
        return nullptr;
    }
    PipelineSlot &slot = pipelines[index - 1];
    return slot.input_layout == nullptr ? nullptr : &slot;
}

bool D3D11RenderDevice::create(const NativeWindowHandle &window)
{
    error = nullptr;

    SwapchainDesc desc;
    desc.window = window;
    desc.width = 0;   // filled in by createSwapchain, which runs after this
    desc.height = 0;
    desc.vsync = true;

    // Device and context only; the swapchain needs the window size, so it is created later
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                       D3D11_SDK_VERSION, &device, &level, &context);
    if (result == E_INVALIDARG) {
        // Windows 7 without the platform update does not know about feature level 11_1
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &levels[1], 1,
                                   D3D11_SDK_VERSION, &device, &level, &context);
    }
    if (FAILED(result) || device == nullptr) {
        error = "D3D11CreateDevice failed";
        LOG_ERROR() << "d3d11: D3D11CreateDevice failed (0x" << logHex(static_cast<unsigned long long>(result), 8) << ")";
        return false;
    }

    // Adapter description for the log line
    std::string adapter_name;
    IDXGIDevice *dxgi_device = nullptr;
    IDXGIAdapter *adapter = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_device)))
        && dxgi_device != nullptr
        && SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter != nullptr) {
        DXGI_ADAPTER_DESC adapter_desc = {};
        if (SUCCEEDED(adapter->GetDesc(&adapter_desc))) {
            adapter_name = wideToUtf8(adapter_desc.Description);
        }
    }
    release(adapter);
    release(dxgi_device);

    backend_name = "D3D11 feature level ";
    backend_name += featureLevelName(level);
    if (!adapter_name.empty()) {
        backend_name += " | ";
        backend_name += adapter_name;
    }

    command_list.reset(new CommandList(*this));
    (void)desc;
    return true;
}

bool D3D11RenderDevice::createSwapchain(const SwapchainDesc &desc)
{
    error = nullptr;

    if (device == nullptr) {
        error = "create() must run before createSwapchain()";
        return false;
    }
    if (!desc.window.isValid()) {
        error = "no native window handle";
        return false;
    }

    width = desc.width;
    height = desc.height;
    vsync = desc.vsync;

    HWND hwnd = static_cast<HWND>(desc.window.window);

    // The swapchain is created through DXGI (the modern path; D3D11CreateDeviceAndSwapChain cannot
    // use FLIP_DISCARD)
    IDXGIDevice *dxgi_device = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIFactory2 *factory = nullptr;

    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_device)))
        || dxgi_device == nullptr
        || FAILED(dxgi_device->GetAdapter(&adapter)) || adapter == nullptr
        || FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory)))
        || factory == nullptr) {
        release(factory);
        release(adapter);
        release(dxgi_device);
        error = "could not reach IDXGIFactory2";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.Width = width;
    swapchain_desc.Height = height;
    swapchain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // matches the other two backends
    swapchain_desc.Stereo = FALSE;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = kFramesInFlight;
    swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    const HRESULT result = factory->CreateSwapChainForHwnd(device, hwnd, &swapchain_desc, nullptr,
                                                           nullptr, &swapchain);
    if (FAILED(result) || swapchain == nullptr) {
        release(factory);
        release(adapter);
        release(dxgi_device);
        error = "CreateSwapChainForHwnd failed";
        LOG_ERROR() << "d3d11: " << error << " (0x" << logHex(static_cast<unsigned long long>(result), 8) << ")";
        return false;
    }

    // Alt+Enter fullscreen switching would fight with our own window handling
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    release(factory);
    release(adapter);
    release(dxgi_device);

    if (!createBackbufferViews()) {
        return false;
    }

    LOG_INFO() << "d3d11: swapchain " << width << "x" << height << ", " << kFramesInFlight
               << " buffers, BGRA8";
    return true;
}

bool D3D11RenderDevice::createBackbufferViews()
{
    ID3D11Texture2D *backbuffer = nullptr;
    if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer)))
        || backbuffer == nullptr) {
        error = "swapchain GetBuffer(0) failed";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }

    const HRESULT result = device->CreateRenderTargetView(backbuffer, nullptr, &render_target_view);
    release(backbuffer);
    if (FAILED(result)) {
        error = "CreateRenderTargetView failed";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }

    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device->CreateTexture2D(&depth_desc, nullptr, &depth_texture)) || depth_texture == nullptr) {
        error = "depth texture creation failed";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }
    if (FAILED(device->CreateDepthStencilView(depth_texture, nullptr, &depth_view)) || depth_view == nullptr) {
        error = "CreateDepthStencilView failed";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }
    return true;
}

void D3D11RenderDevice::destroyBackbufferViews()
{
    release(depth_view);
    release(depth_texture);
    release(render_target_view);
}

void D3D11RenderDevice::destroySwapchain()
{
    if (context != nullptr) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->ClearState();
        context->Flush();
    }
    destroyBackbufferViews();
    release(swapchain);
    width = 0;
    height = 0;
}

bool D3D11RenderDevice::resizeSwapchain(std::uint32_t new_width, std::uint32_t new_height)
{
    if (new_width == 0 || new_height == 0 || swapchain == nullptr) {
        return false;
    }

    width = new_width;
    height = new_height;

    // Every view has to go before ResizeBuffers, and the context must not hold references
    context->OMSetRenderTargets(0, nullptr, nullptr);
    destroyBackbufferViews();
    context->Flush();

    const HRESULT result = swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) {
        error = "ResizeBuffers failed";
        LOG_ERROR() << "d3d11: " << error;
        return false;
    }
    return createBackbufferViews();
}

// ---------------- resources ----------------

BufferHandle D3D11RenderDevice::createBuffer(const BufferDesc &desc)
{
    if (desc.size == 0) {
        error = "createBuffer: empty buffer";
        return kInvalidHandle;
    }

    BufferSlot slot;
    slot.size = desc.size;
    slot.usage = desc.usage;
    slot.valid = true;

    if (desc.usage == BufferUsage::Uniform) {
        // No aggregate GPU buffer: constant data is uploaded into per-block buffers at bind time
        slot.shadow.assign(desc.size, 0);
    } else {
        const bool dynamic = desc.initial_data == nullptr;
        D3D11_BUFFER_DESC buffer_desc = {};
        buffer_desc.ByteWidth = desc.size;
        buffer_desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        buffer_desc.BindFlags = desc.usage == BufferUsage::Vertex ? D3D11_BIND_VERTEX_BUFFER
                                                                 : D3D11_BIND_INDEX_BUFFER;
        buffer_desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = desc.initial_data;

        const HRESULT result = device->CreateBuffer(&buffer_desc,
                                                    desc.initial_data != nullptr ? &initial : nullptr,
                                                    &slot.buffer);
        if (FAILED(result) || slot.buffer == nullptr) {
            error = "CreateBuffer failed";
            LOG_ERROR() << "d3d11: " << error << " (0x" << logHex(static_cast<unsigned long long>(result), 8) << ")";
            return kInvalidHandle;
        }
    }

    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (!buffers[i].valid) {
            buffers[i] = slot;
            return static_cast<BufferHandle>(i + 1);
        }
    }

    buffers.push_back(slot);
    return static_cast<BufferHandle>(buffers.size());
}

void D3D11RenderDevice::destroyBuffer(BufferHandle buffer)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr) {
        return;
    }
    for (const std::pair<std::uint32_t, ID3D11Buffer *> &block : slot->blocks) {
        ID3D11Buffer *block_buffer = block.second;
        release(block_buffer);
    }
    slot->blocks.clear();
    release(slot->buffer);
    *slot = BufferSlot{};
}

void D3D11RenderDevice::updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                                     std::uint32_t offset)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr || data == nullptr || size == 0) {
        error = "updateBuffer: bad arguments";
        return;
    }
    if (offset + size > slot->size) {
        error = "updateBuffer: out of range";
        LOG_ERROR() << "d3d11: updateBuffer out of range";
        return;
    }

    // Only the CPU shadow is updated here; the GPU upload happens in bindUniformBuffer, which knows
    // exactly which block the next draw needs
    if (slot->shadow.size() != slot->size) {
        slot->shadow.resize(slot->size, 0);
    }
    std::memcpy(slot->shadow.data() + offset, data, size);

    if (slot->buffer != nullptr) {
        // Vertex/index buffers are ordinary dynamic resources: a straight upload works
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(slot->buffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
            error = "Map failed";
            LOG_ERROR() << "d3d11: Map failed in updateBuffer";
            return;
        }
        std::memcpy(static_cast<std::uint8_t *>(mapped.pData) + offset, data, size);
        context->Unmap(slot->buffer, 0);
    }
}

// D3D11 cannot bind a range of a constant buffer reliably: ID3D11DeviceContext1::*SetConstantBuffers1
// with a non-zero FirstConstant silently delivered the wrong data on this machine's Intel driver
// (measured: the same draw renders correctly when the whole buffer is bound). Each block therefore
// gets a small dedicated constant buffer, created on first use and rebound whole - which is also why
// no ID3D11DeviceContext1 is needed at all.
ID3D11Buffer *D3D11RenderDevice::constantBlockBuffer(BufferSlot &slot, std::uint32_t offset,
                                                     std::uint32_t size)
{
    for (const std::pair<std::uint32_t, ID3D11Buffer *> &block : slot.blocks) {
        if (block.first == offset) {
            return block.second;
        }
    }

    // Constant buffers must hold a multiple of 16 bytes
    const std::uint32_t rounded = (size + 15u) & ~15u;

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = rounded;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Buffer *block_buffer = nullptr;
    if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, &block_buffer)) || block_buffer == nullptr) {
        error = "constant buffer creation failed";
        LOG_ERROR() << "d3d11: " << error;
        return nullptr;
    }

    slot.blocks.emplace_back(offset, block_buffer);
    return block_buffer;
}

ShaderHandle D3D11RenderDevice::createShader(const ShaderDesc &desc)
{
    if (desc.code == nullptr || desc.size == 0) {
        error = "createShader: empty shader source";
        return kInvalidHandle;
    }

    // Convention: one HLSL file holds both stages, with these entry points
    const char *entry_point = desc.stage == ShaderStage::Vertex ? "VSMain" : "PSMain";
    const char *target = desc.stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
    const char *name = desc.debug_name != nullptr ? desc.debug_name : "shader";

    ID3DBlob *bytecode = nullptr;
    ID3DBlob *errors = nullptr;
    const HRESULT compile_result = D3DCompile(desc.code, desc.size, name, nullptr, nullptr, entry_point,
                                              target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
    if (FAILED(compile_result) || bytecode == nullptr) {
        const char *message = errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer())
                                                : "no compiler output";
        LOG_ERROR() << "d3d11: " << name << " (" << entry_point << ") compile failed: " << message;
        release(errors);
        release(bytecode);
        error = "HLSL compilation failed";
        return kInvalidHandle;
    }
    release(errors);

    ShaderSlot slot;
    slot.stage = desc.stage;

    HRESULT create_result = E_FAIL;
    if (desc.stage == ShaderStage::Vertex) {
        create_result = device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                                                   nullptr, &slot.vertex);
        // Keep the bytecode alive: createPipeline builds the input layout from it
        slot.vertex_bytecode = bytecode;
    } else {
        create_result = device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                                                  nullptr, &slot.pixel);
        release(bytecode);
    }

    if (FAILED(create_result)) {
        release(slot.vertex);
        ID3DBlob *kept_bytecode = static_cast<ID3DBlob *>(slot.vertex_bytecode);
        release(kept_bytecode);
        error = "shader object creation failed";
        LOG_ERROR() << "d3d11: " << error;
        return kInvalidHandle;
    }

    shaders.push_back(slot);
    return static_cast<ShaderHandle>(shaders.size());
}

void D3D11RenderDevice::destroyShader(ShaderHandle shader)
{
    ShaderSlot *slot = shaderSlot(shader);
    if (slot == nullptr) {
        return;
    }
    release(slot->vertex);
    release(slot->pixel);
    ID3DBlob *bytecode = static_cast<ID3DBlob *>(slot->vertex_bytecode);
    release(bytecode);
    *slot = ShaderSlot{};
}

PipelineHandle D3D11RenderDevice::createPipeline(const PipelineDesc &desc)
{
    ShaderSlot *vertex_shader = shaderSlot(desc.vertex_shader);
    ShaderSlot *fragment_shader = shaderSlot(desc.fragment_shader);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        error = "createPipeline: shader handles are invalid";
        return kInvalidHandle;
    }
    if (desc.attributes == nullptr || desc.attribute_count == 0 || desc.vertex_stride == 0) {
        error = "createPipeline: a vertex layout is required";
        return kInvalidHandle;
    }
    if (vertex_shader->vertex_bytecode == nullptr) {
        error = "createPipeline: the vertex shader bytecode is missing (needed for the input layout)";
        return kInvalidHandle;
    }
    ID3DBlob *vertex_bytecode = static_cast<ID3DBlob *>(vertex_shader->vertex_bytecode);

    PipelineSlot slot;
    slot.vertex_shader = desc.vertex_shader;
    slot.fragment_shader = desc.fragment_shader;
    slot.vertex_stride = desc.vertex_stride;

    std::vector<D3D11_INPUT_ELEMENT_DESC> elements(desc.attribute_count);
    for (std::uint32_t i = 0; i < desc.attribute_count; ++i) {
        elements[i].SemanticName = semanticName(desc.attributes[i].location);
        elements[i].SemanticIndex = semanticIndex(desc.attributes[i].location);
        elements[i].Format = vertexAttributeFormat(desc.attributes[i].format);
        elements[i].InputSlot = 0;
        elements[i].AlignedByteOffset = desc.attributes[i].offset;
        elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[i].InstanceDataStepRate = 0;
    }

    HRESULT result = device->CreateInputLayout(elements.data(), desc.attribute_count,
                                               vertex_bytecode->GetBufferPointer(),
                                               vertex_bytecode->GetBufferSize(),
                                               &slot.input_layout);
    if (FAILED(result) || slot.input_layout == nullptr) {
        error = "CreateInputLayout failed";
        LOG_ERROR() << "d3d11: " << error;
        return kInvalidHandle;
    }

    D3D11_DEPTH_STENCIL_DESC depth_desc = {};
    depth_desc.DepthEnable = desc.depth_test ? TRUE : FALSE;
    depth_desc.DepthWriteMask = desc.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS;  // same comparison as the other backends
    depth_desc.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&depth_desc, &slot.depth_state))
        || slot.depth_state == nullptr) {
        error = "CreateDepthStencilState failed";
        LOG_ERROR() << "d3d11: " << error;
        release(slot.input_layout);
        return kInvalidHandle;
    }

    D3D11_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;  // like the other two backends: no culling yet
    raster_desc.FrontCounterClockwise = FALSE;
    raster_desc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&raster_desc, &slot.raster_state))
        || slot.raster_state == nullptr) {
        error = "CreateRasterizerState failed";
        LOG_ERROR() << "d3d11: " << error;
        release(slot.input_layout);
        release(slot.depth_state);
        return kInvalidHandle;
    }

    pipelines.push_back(slot);
    return static_cast<PipelineHandle>(pipelines.size());
}

void D3D11RenderDevice::destroyPipeline(PipelineHandle pipeline)
{
    PipelineSlot *slot = pipelineSlot(pipeline);
    if (slot == nullptr) {
        return;
    }
    release(slot->raster_state);
    release(slot->depth_state);
    release(slot->input_layout);
    if (bound_pipeline == slot) {
        bound_pipeline = nullptr;
    }
    *slot = PipelineSlot{};
}

// ---------------- frames ----------------

ICommandList &D3D11RenderDevice::getCommandList()
{
    return *command_list;
}

void D3D11RenderDevice::beginFrame()
{
    frame_active = false;
    if (device == nullptr || swapchain == nullptr || render_target_view == nullptr) {
        return;
    }

    context->OMSetRenderTargets(1, &render_target_view, depth_view);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    command_list->begin();
    frame_active = true;
}

void D3D11RenderDevice::endFrame()
{
    if (!frame_active) {
        return;
    }
    command_list->end();
    frame_active = false;

    const HRESULT result = swapchain->Present(vsync ? 1 : 0, 0);
    if (FAILED(result)) {
        LOG_ERROR() << "d3d11: Present failed (0x" << logHex(static_cast<unsigned long long>(result), 8) << ")";
        error = "Present failed";
    }
}

void D3D11RenderDevice::waitIdle()
{
    if (context != nullptr) {
        context->Flush();  // the immediate context submits on Flush; this is D3D11's "wait for GPU"
    }
}

void D3D11RenderDevice::destroy()
{
    if (context != nullptr) {
        context->ClearState();
        context->Flush();
    }

    command_list.reset();

    for (BufferSlot &buffer : buffers) {
        for (const std::pair<std::uint32_t, ID3D11Buffer *> &block : buffer.blocks) {
            ID3D11Buffer *block_buffer = block.second;
            release(block_buffer);
        }
        buffer.blocks.clear();
        release(buffer.buffer);
    }
    buffers.clear();

    for (ShaderSlot &shader : shaders) {
        release(shader.vertex);
        release(shader.pixel);
        ID3DBlob *bytecode = static_cast<ID3DBlob *>(shader.vertex_bytecode);
        release(bytecode);
        shader.vertex_bytecode = nullptr;
    }
    shaders.clear();

    for (PipelineSlot &pipeline : pipelines) {
        release(pipeline.raster_state);
        release(pipeline.depth_state);
        release(pipeline.input_layout);
    }
    pipelines.clear();
    bound_pipeline = nullptr;

    destroyBackbufferViews();
    release(swapchain);
    release(context);
    release(device);

    width = 0;
    height = 0;
    frame_active = false;
    backend_name.clear();
}
