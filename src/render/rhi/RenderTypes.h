#ifndef RENDER_RHI_RENDERTYPES_H
#define RENDER_RHI_RENDERTYPES_H

#include "core/math/conventions.h"
#include "core/platform/NativeWindowHandle.h"

#include <cstddef>
#include <cstdint>

// Value types shared by every render backend.
//
// Nothing in this header knows about OpenGL, Vulkan or D3D: it is the vocabulary that the app and
// the backends agree on. Everything resource-shaped is an opaque handle, so swapping the backend
// cannot leak an API object into the app.

// Opaque resource handles: an index into the backend's slot table, 0 = invalid.
using BufferHandle = std::uint32_t;
using ShaderHandle = std::uint32_t;
using PipelineHandle = std::uint32_t;

constexpr std::uint32_t kInvalidHandle = 0;

enum class Backend : std::uint8_t
{
    OpenGL,
    Vulkan,
    Direct3D11,
};

// What a shader blob contains. The app asks the device which dialect it wants and loads the matching
// file (basic.vert / basic.vert.spv / basic.hlsl), so a backend switch does not need app changes.
enum class ShaderLanguage : std::uint8_t
{
    GLSLSource,  // OpenGL
    SPIRV,       // Vulkan
    HLSLSource,  // Direct3D
};

enum class ShaderStage : std::uint8_t
{
    Vertex,
    Fragment,
};

enum class BufferUsage : std::uint8_t
{
    Vertex,   // vertex data, read by the input assembler
    Index,    // index data
    Uniform,  // shader constants, rewritten per draw/frame
};

enum class IndexFormat : std::uint8_t
{
    UInt16,
    UInt32,
};

enum class PrimitiveTopology : std::uint8_t
{
    TriangleList,
};

enum class VertexFormat : std::uint8_t
{
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
};

// One shader attribute: where it lives in the vertex and which location it feeds.
struct VertexAttribute
{
    std::uint32_t location = 0;  // must match layout(location = N) in the shader
    VertexFormat format = VertexFormat::Float32x3;
    std::uint32_t offset = 0;  // byte offset inside one vertex
};

struct BufferDesc
{
    std::uint32_t size = 0;               // bytes
    BufferUsage usage = BufferUsage::Vertex;
    const void *initial_data = nullptr;   // may be null: updateBuffer() can fill it later
};

struct ShaderDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    const void *code = nullptr;           // GLSL text / SPIR-V blob / HLSL text
    std::size_t size = 0;                 // bytes
    const char *debug_name = nullptr;     // used in error messages only
};

struct PipelineDesc
{
    ShaderHandle vertex_shader = kInvalidHandle;
    ShaderHandle fragment_shader = kInvalidHandle;

    const VertexAttribute *attributes = nullptr;
    std::uint32_t attribute_count = 0;
    std::uint32_t vertex_stride = 0;      // bytes per vertex

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    bool depth_test = true;
    bool depth_write = true;
};

struct SwapchainDesc
{
    NativeWindowHandle window;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool vsync = true;
};

// Bytes one vertex occupies, for PipelineDesc::vertex_stride
constexpr std::uint32_t vertexFormatSize(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float32:   return 4;
        case VertexFormat::Float32x2: return 8;
        case VertexFormat::Float32x3: return 12;
        case VertexFormat::Float32x4: return 16;
    }
    return 0;
}

#endif  // RENDER_RHI_RENDERTYPES_H
