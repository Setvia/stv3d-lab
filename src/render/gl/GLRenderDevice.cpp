#include "render/gl/GLRenderDevice.h"

#include "core/log/LogManager.h"

#include <utility>

namespace
{

GLenum bufferTarget(BufferUsage usage)
{
    switch (usage) {
        case BufferUsage::Vertex:  return GL_ARRAY_BUFFER;
        case BufferUsage::Index:   return GL_ELEMENT_ARRAY_BUFFER;
        case BufferUsage::Uniform: return GL_UNIFORM_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

GLenum shaderStageEnum(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
}

const char *shaderStageName(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? "vertex shader" : "fragment shader";
}

// GL info logs are padded and newline-wrapped; the project logs one message per line
std::string sanitizedInfoLog(const std::string &info)
{
    std::string text;
    std::string line;
    const std::size_t first = info.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return text;
    }
    const std::string trimmed = info.substr(first);
    for (const char c : trimmed) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) {
                if (!text.empty()) {
                    text += " | ";
                }
                text += line;
                line.clear();
            }
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        if (!text.empty()) {
            text += " | ";
        }
        text += line;
    }
    return text;
}

GLsizei indexFormatSize(IndexFormat format)
{
    return format == IndexFormat::UInt16 ? 2 : 4;
}

GLenum indexFormatEnum(IndexFormat format)
{
    return format == IndexFormat::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

}  // namespace

// ---------------------------------------------------------------------------
// Command list
// ---------------------------------------------------------------------------
class GLRenderDevice::CommandList : public ICommandList
{
public:
    explicit CommandList(GLRenderDevice &device) : device(device) {}

    void begin() override
    {
        recording = true;
    }

    void end() override
    {
        recording = false;
    }

    void setViewport(std::uint32_t width, std::uint32_t height) override
    {
        device.gfx.glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void clear(float red, float green, float blue, float alpha, float depth) override
    {
        device.gfx.glClearColor(red, green, blue, alpha);
        device.gfx.glDepthMask(GL_TRUE);  // a depth write mask of false would silently skip the depth clear
        device.gfx.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void bindPipeline(PipelineHandle pipeline) override
    {
        GLRenderDevice::PipelineSlot *slot = device.pipelineSlot(pipeline);
        if (slot == nullptr) {
            LOG_ERROR() << "bindPipeline: invalid pipeline handle " << pipeline;
            return;
        }

        device.gfx.glUseProgram(slot->program);

        // Fixed state (in GL this is global state, which is exactly what the RHI hides)
        if (slot->depth_test) {
            device.gfx.glEnable(GL_DEPTH_TEST);
        } else {
            device.gfx.glDisable(GL_DEPTH_TEST);
        }
        device.gfx.glDepthMask(slot->depth_write ? GL_TRUE : GL_FALSE);

        // The vertex layout lives in the VAO, so it is re-applied on every pipeline change
        device.gfx.glBindVertexArray(device.vertex_array);
        for (const VertexAttribute &attribute : slot->attributes) {
            device.gfx.glVertexAttribFormat(attribute.location,
                                            static_cast<GLint>(vertexFormatComponents(attribute.format)),
                                            GL_FLOAT, GL_FALSE, attribute.offset);
            device.gfx.glVertexAttribBinding(attribute.location, kBindingIndex);
            device.gfx.glEnableVertexAttribArray(attribute.location);
        }

        device.bound_pipeline = slot;
    }

    void bindVertexBuffer(BufferHandle buffer) override
    {
        GLRenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Vertex) {
            LOG_ERROR() << "bindVertexBuffer: not a vertex buffer handle " << buffer;
            return;
        }
        if (device.bound_pipeline == nullptr) {
            LOG_ERROR() << "bindVertexBuffer: bind a pipeline first (it carries the vertex stride)";
            return;
        }

        device.gfx.glBindVertexBuffer(kBindingIndex, slot->id, 0,
                                      static_cast<GLsizei>(device.bound_pipeline->vertex_stride));
    }

    void bindIndexBuffer(BufferHandle buffer, IndexFormat format) override
    {
        GLRenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Index) {
            LOG_ERROR() << "bindIndexBuffer: not an index buffer handle " << buffer;
            return;
        }

        device.gfx.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot->id);
        index_format = format;
    }

    void bindUniformBuffer(BufferHandle buffer, std::uint32_t slot) override
    {
        GLRenderDevice::BufferSlot *buffer_slot = device.bufferSlot(buffer);
        if (buffer_slot == nullptr || buffer_slot->usage != BufferUsage::Uniform) {
            LOG_ERROR() << "bindUniformBuffer: not a uniform buffer handle " << buffer;
            return;
        }

        device.gfx.glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffer_slot->id);
    }

    void drawIndexed(std::uint32_t index_count, std::uint32_t first_index) override
    {
        if (!recording) {
            LOG_ERROR() << "drawIndexed outside of begin()/end()";
            return;
        }

        const GLsizei index_size = indexFormatSize(index_format);
        const void *offset = reinterpret_cast<const void *>(static_cast<std::uintptr_t>(first_index) * static_cast<std::uintptr_t>(index_size));
        device.gfx.glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count),
                                  indexFormatEnum(index_format), offset);
    }

private:
    static constexpr GLuint kBindingIndex = 0;  // one interleaved vertex buffer per draw (v1)

    static int vertexFormatComponents(VertexFormat format)
    {
        switch (format) {
            case VertexFormat::Float32:   return 1;
            case VertexFormat::Float32x2: return 2;
            case VertexFormat::Float32x3: return 3;
            case VertexFormat::Float32x4: return 4;
        }
        return 0;
    }

    GLRenderDevice &device;
    bool recording = false;
    IndexFormat index_format = IndexFormat::UInt32;
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

GLRenderDevice::GLRenderDevice() = default;

GLRenderDevice::~GLRenderDevice()
{
    destroy();
}

bool GLRenderDevice::create(const NativeWindowHandle &window)
{
    return create(window, GLContext::Config{});
}

bool GLRenderDevice::create(const NativeWindowHandle &window, const GLContext::Config &config)
{
    error = nullptr;

    if (!gl_context.create(window, config)) {
        error = gl_context.lastError();
        return false;
    }

    if (!gfx.load()) {
        error = gfx.missingFunction();
        LOG_ERROR() << "OpenGL function missing (context too old for 4.3 core): " << error;
        gl_context.destroy();
        return false;
    }

    // Core profile requires a bound VAO before any attribute state can be set
    gfx.glGenVertexArrays(1, &vertex_array);
    gfx.glBindVertexArray(vertex_array);

    backend_name = "OpenGL ";
    const char *version = reinterpret_cast<const char *>(gfx.glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(gfx.glGetString(GL_RENDERER));
    backend_name += version != nullptr ? version : "?";
    if (renderer != nullptr) {
        backend_name += " | ";
        backend_name += renderer;
    }

    // Report the profile we actually got - the renderer needs a core profile (that is where
    // glVertexAttribFormat / glBindVertexBuffer live)
    GLint profile_mask = 0;
    gfx.glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    backend_name += (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0 ? " | core profile"
                                                                     : " | compatibility profile";

    command_list.reset(new CommandList(*this));
    return true;
}

void GLRenderDevice::destroy()
{
    command_list.reset();

    if (gfx.isLoaded()) {
        // Resources can only be deleted while the context is current
        gl_context.makeCurrent();
        for (const BufferSlot &buffer : buffers) {
            if (buffer.id != 0) {
                GLuint id = buffer.id;
                gfx.glDeleteBuffers(1, &id);
            }
        }
        buffers.clear();
        for (const ShaderSlot &shader : shaders) {
            if (shader.id != 0) {
                gfx.glDeleteShader(shader.id);
            }
        }
        shaders.clear();
        for (const PipelineSlot &pipeline : pipelines) {
            if (pipeline.program != 0) {
                gfx.glDeleteProgram(pipeline.program);
            }
        }
        pipelines.clear();

        if (vertex_array != 0) {
            gfx.glDeleteVertexArrays(1, &vertex_array);
            vertex_array = 0;
        }
    }

    gl_context.destroy();
    vertex_array = 0;
    width = 0;
    height = 0;
    bound_pipeline = nullptr;
}

const char *GLRenderDevice::backendName() const
{
    return backend_name.c_str();
}

bool GLRenderDevice::createSwapchain(const SwapchainDesc &desc)
{
    error = nullptr;

    if (!gl_context.isValid()) {
        error = "no GL context: call create() first";
        return false;
    }
    if (!desc.window.isValid()) {
        error = "no native window handle";
        return false;
    }

    width = desc.width;
    height = desc.height;

    // The WGL swapchain is the window's device context; "creating" it just means making the context
    // current and setting the viewport (vsync was already requested when the context was created)
    if (!gl_context.makeCurrent()) {
        error = "wglMakeCurrent failed";
        return false;
    }
    gfx.glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return true;
}

void GLRenderDevice::destroySwapchain()
{
    // Nothing to destroy for WGL: the DC belongs to the window and the context is owned by create()
    width = 0;
    height = 0;
}

bool GLRenderDevice::resizeSwapchain(std::uint32_t new_width, std::uint32_t new_height)
{
    if (new_width == 0 || new_height == 0) {
        return false;  // minimised: keep the old size
    }

    width = new_width;
    height = new_height;

    if (gl_context.makeCurrent()) {
        gfx.glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }
    return true;
}

GLRenderDevice::BufferSlot *GLRenderDevice::bufferSlot(BufferHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > buffers.size()) {
        return nullptr;
    }
    BufferSlot &slot = buffers[index - 1];
    return slot.id == 0 ? nullptr : &slot;
}

GLRenderDevice::ShaderSlot *GLRenderDevice::shaderSlot(ShaderHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > shaders.size()) {
        return nullptr;
    }
    ShaderSlot &slot = shaders[index - 1];
    return slot.id == 0 ? nullptr : &slot;
}

GLRenderDevice::PipelineSlot *GLRenderDevice::pipelineSlot(PipelineHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > pipelines.size()) {
        return nullptr;
    }
    PipelineSlot &slot = pipelines[index - 1];
    return slot.program == 0 ? nullptr : &slot;
}

BufferHandle GLRenderDevice::createBuffer(const BufferDesc &desc)
{
    if (desc.size == 0) {
        error = "createBuffer: empty buffer";
        return kInvalidHandle;
    }
    if (!gl_context.makeCurrent()) {
        error = "createBuffer: no current context";
        return kInvalidHandle;
    }

    BufferSlot slot;
    slot.target = bufferTarget(desc.usage);
    slot.usage = desc.usage;

    gfx.glGenBuffers(1, &slot.id);
    if (slot.id == 0) {
        error = "createBuffer: glGenBuffers failed";
        return kInvalidHandle;
    }

    gfx.glBindBuffer(slot.target, slot.id);
    // Uniform buffers are rewritten often, everything else is uploaded once
    const GLenum usage_hint = desc.usage == BufferUsage::Uniform ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    gfx.glBufferData(slot.target, static_cast<GLsizeiptr>(desc.size), desc.initial_data, usage_hint);
    gfx.glBindBuffer(slot.target, 0);

    // Reuse a freed slot when there is one, so handles stay small
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i].id == 0) {
            buffers[i] = slot;
            return static_cast<BufferHandle>(i + 1);
        }
    }

    buffers.push_back(slot);
    return static_cast<BufferHandle>(buffers.size());
}

void GLRenderDevice::destroyBuffer(BufferHandle buffer)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr) {
        return;
    }
    if (gl_context.makeCurrent()) {
        gfx.glDeleteBuffers(1, &slot->id);
    }
    *slot = BufferSlot{};
}

void GLRenderDevice::updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                                  std::uint32_t offset)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr || data == nullptr || size == 0) {
        error = "updateBuffer: bad arguments";
        return;
    }
    if (!gl_context.makeCurrent()) {
        error = "updateBuffer: no current context";
        return;
    }

    gfx.glBindBuffer(slot->target, slot->id);
    gfx.glBufferSubData(slot->target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    gfx.glBindBuffer(slot->target, 0);
}

ShaderHandle GLRenderDevice::createShader(const ShaderDesc &desc)
{
    if (desc.code == nullptr || desc.size == 0) {
        error = "createShader: empty shader source";
        return kInvalidHandle;
    }
    if (!gl_context.makeCurrent()) {
        error = "createShader: no current context";
        return kInvalidHandle;
    }

    const char *name = desc.debug_name != nullptr ? desc.debug_name : "shader";
    const GLuint shader = gfx.glCreateShader(shaderStageEnum(desc.stage));

    const char *source = static_cast<const char *>(desc.code);
    const GLint length = static_cast<GLint>(desc.size);
    gfx.glShaderSource(shader, 1, &source, &length);
    gfx.glCompileShader(shader);

    GLint compiled = GL_FALSE;
    gfx.glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint log_length = 0;
        gfx.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::string info(static_cast<std::size_t>(log_length > 0 ? log_length : 1), '\0');
        gfx.glGetShaderInfoLog(shader, log_length, nullptr, &info[0]);
        LOG_ERROR() << name << ": compile failed: " << sanitizedInfoLog(info);
        gfx.glDeleteShader(shader);
        error = "shader compilation failed";
        return kInvalidHandle;
    }

    ShaderSlot slot;
    slot.id = shader;
    slot.stage = desc.stage;
    shaders.push_back(slot);
    return static_cast<ShaderHandle>(shaders.size());
}

void GLRenderDevice::destroyShader(ShaderHandle shader)
{
    ShaderSlot *slot = shaderSlot(shader);
    if (slot == nullptr) {
        return;
    }
    if (gl_context.makeCurrent()) {
        gfx.glDeleteShader(slot->id);
    }
    *slot = ShaderSlot{};
}

PipelineHandle GLRenderDevice::createPipeline(const PipelineDesc &desc)
{
    ShaderSlot *vertex_shader = shaderSlot(desc.vertex_shader);
    ShaderSlot *fragment_shader = shaderSlot(desc.fragment_shader);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        error = "createPipeline: shader handles are invalid (or already released)";
        return kInvalidHandle;
    }
    if (desc.attributes == nullptr || desc.attribute_count == 0 || desc.vertex_stride == 0) {
        error = "createPipeline: a vertex layout is required";
        return kInvalidHandle;
    }
    if (!gl_context.makeCurrent()) {
        error = "createPipeline: no current context";
        return kInvalidHandle;
    }

    const GLuint program = gfx.glCreateProgram();
    gfx.glAttachShader(program, vertex_shader->id);
    gfx.glAttachShader(program, fragment_shader->id);
    gfx.glLinkProgram(program);

    // The shaders stay owned by their handles, so detaching them here is safe and keeps the program
    // independent of their lifetime
    gfx.glDetachShader(program, vertex_shader->id);
    gfx.glDetachShader(program, fragment_shader->id);

    GLint linked = GL_FALSE;
    gfx.glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint log_length = 0;
        gfx.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        std::string info(static_cast<std::size_t>(log_length > 0 ? log_length : 1), '\0');
        gfx.glGetProgramInfoLog(program, log_length, nullptr, &info[0]);
        LOG_ERROR() << "program link failed: " << sanitizedInfoLog(info);
        gfx.glDeleteProgram(program);
        error = "program link failed";
        return kInvalidHandle;
    }

    PipelineSlot slot;
    slot.program = program;
    slot.attributes.assign(desc.attributes, desc.attributes + desc.attribute_count);
    slot.vertex_stride = desc.vertex_stride;
    slot.depth_test = desc.depth_test;
    slot.depth_write = desc.depth_write;
    pipelines.push_back(slot);
    return static_cast<PipelineHandle>(pipelines.size());
}

void GLRenderDevice::destroyPipeline(PipelineHandle pipeline)
{
    PipelineSlot *slot = pipelineSlot(pipeline);
    if (slot == nullptr) {
        return;
    }
    if (gl_context.makeCurrent()) {
        gfx.glDeleteProgram(slot->program);
    }
    if (bound_pipeline == slot) {
        bound_pipeline = nullptr;
    }
    *slot = PipelineSlot{};
}

ICommandList &GLRenderDevice::getCommandList()
{
    return *command_list;
}

void GLRenderDevice::beginFrame()
{
    gl_context.makeCurrent();
    command_list->begin();
}

void GLRenderDevice::endFrame()
{
    command_list->end();
    gl_context.swapBuffers();
}

void GLRenderDevice::waitIdle()
{
    if (gl_context.makeCurrent()) {
        gfx.glFinish();
    }
}

void GLRenderDevice::reportErrors(const char *stage)
{
    if (!gfx.isLoaded() || !gl_context.makeCurrent()) {
        return;
    }

    const GLenum error = gfx.glGetError();
    if (error != GL_NO_ERROR) {
        LOG_WARNING() << "GL error " << stage << ", code = " << logHex(error, 4);
    }
}
