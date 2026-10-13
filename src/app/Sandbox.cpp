#include "app/Sandbox.h"

#include "core/geometry/generator/MeshGen.h"
#include "core/log/LogManager.h"
#include "core/platform/File.h"
#include "game/InputMapping.h"

#include <cstddef>
#include <cstdint>

namespace
{

// The demo's streamed set: props nothing per-tick computes against, so they may arrive while the loop
// is already running. Each one is a separate queue item and each uploads its own buffers, which is the
// part worth exercising - creating GPU resources after startup has to work on every backend.
constexpr std::size_t kStreamedPropCount = 3;

// How many streamed items one turn may finish. One per turn makes the arrival visible in the log and
// keeps a turn's extra cost predictable; raise it for content that is small or needed sooner.
constexpr std::size_t kStreamingItemsPerTurn = 1;

// Slot 0 of the geometry staging area is the environment set: the geometry the first tick's scene is
// built from (a terrain or a skybox would be further slots in the preload wave).
constexpr std::size_t kEnvironmentMeshSlot = 0;

}  // namespace

Sandbox::Sandbox()
{
    // Logic ticks are a callback now: the platform pump calls game_loop.advance(), which lands here
    game_loop.setTickCallback([this](std::uint64_t) { onTick(); });

    // Streaming runs inside the loop: this many items per turn of the pump
    game_loop.setStreamingItemsPerTurn(kStreamingItemsPerTurn);
}

Sandbox::~Sandbox()
{
    // releaseResources() is an explicit step because the device (and its context) must still be alive;
    // forgetting it here only leaks, it does not crash, which is why the app calls it.
}

// ---------------- resources ----------------

bool Sandbox::createResources(IRenderDevice &device, const std::string &assetDirectory)
{
    this->device = &device;

    // Clip space is a backend property, not a shader hack: OpenGL wants z in [-1,1] with +Y up,
    // Vulkan z in [0,1] with +Y down. The camera takes both as parameters (see core conventions).
    character.getCamera().setClipDepth(device.clipDepth());
    character.getCamera().setFlipY(device.flipY());

    // (1) Shaders.
    // The device says which dialect it wants, so a backend switch means loading different files
    // rather than editing this code: GLSL text for OpenGL, a SPIR-V binary for Vulkan.
    std::string vertex_source;
    std::string fragment_source;
    std::vector<std::uint8_t> vertex_binary;
    std::vector<std::uint8_t> fragment_binary;
    const std::string shader_directory = assetDirectory + "/shaders";

    ShaderDesc vertex_desc;
    vertex_desc.stage = ShaderStage::Vertex;
    vertex_desc.debug_name = "basic";
    ShaderDesc fragment_desc;
    fragment_desc.stage = ShaderStage::Fragment;
    fragment_desc.debug_name = "basic";

    switch (device.shaderLanguage()) {
        case ShaderLanguage::GLSLSource:
            if (!File::readTextFile(shader_directory + "/basic.vert", vertex_source)
                || !File::readTextFile(shader_directory + "/basic.frag", fragment_source)) {
                return false;
            }
            vertex_desc.code = vertex_source.data();
            vertex_desc.size = vertex_source.size();
            fragment_desc.code = fragment_source.data();
            fragment_desc.size = fragment_source.size();
            break;

        case ShaderLanguage::SPIRV:
            if (!File::readBinaryFile(shader_directory + "/basic.vert.spv", vertex_binary)
                || !File::readBinaryFile(shader_directory + "/basic.frag.spv", fragment_binary)) {
                return false;
            }
            vertex_desc.code = vertex_binary.data();
            vertex_desc.size = vertex_binary.size();
            fragment_desc.code = fragment_binary.data();
            fragment_desc.size = fragment_binary.size();
            break;

        case ShaderLanguage::HLSLSource:
            // One HLSL file holds both stages (entry points VSMain / PSMain, see shaders/basic.hlsl)
            if (!File::readTextFile(shader_directory + "/basic.hlsl", vertex_source)
                || !File::readTextFile(shader_directory + "/basic.hlsl", fragment_source)) {
                return false;
            }
            vertex_desc.code = vertex_source.data();
            vertex_desc.size = vertex_source.size();
            fragment_desc.code = fragment_source.data();
            fragment_desc.size = fragment_source.size();
            break;
    }

    vertex_shader = device.createShader(vertex_desc);
    fragment_shader = device.createShader(fragment_desc);

    if (vertex_shader == kInvalidHandle || fragment_shader == kInvalidHandle) {
        LOG_ERROR() << "shader creation failed: " << device.lastError();
        return false;
    }

    // (2) Pipeline: attribute locations and the vertex stride are described explicitly; the layout is
    // what core::Vertex actually contains (position at 0, color at 1).
    const VertexAttribute attributes[] = {
        {0, VertexFormat::Float32x3, static_cast<std::uint32_t>(offsetof(Vertex, position))},
        {1, VertexFormat::Float32x3, static_cast<std::uint32_t>(offsetof(Vertex, color))},
    };

    PipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader;
    pipeline_desc.fragment_shader = fragment_shader;
    pipeline_desc.attributes = attributes;
    pipeline_desc.attribute_count = 2;
    pipeline_desc.vertex_stride = static_cast<std::uint32_t>(sizeof(Vertex));
    pipeline_desc.topology = PrimitiveTopology::TriangleList;
    pipeline_desc.depth_test = true;
    pipeline_desc.depth_write = true;
    pipeline = device.createPipeline(pipeline_desc);
    if (pipeline == kInvalidHandle) {
        LOG_ERROR() << "pipeline creation failed: " << device.lastError();
        return false;
    }

    // (3) Scene, first wave - preload. This is the geometry the scene is built from before the loop
    // ever ticks: what a terrain, a skybox or any collision/lighting input would be part of, and the
    // reason this wave is not allowed to arrive late. It goes through the same queue as the streamed
    // content, in the blocking class, so a scene load has exactly one code path.
    LoadQueue &loads = game_loop.getLoadQueue();
    staged_meshes.assign(1 + kStreamedPropCount, MeshData{});

    loads.request(LoadPriority::Preload,
                  [this] {
                      staged_meshes[kEnvironmentMeshSlot] = MeshGen::makeCube(1.0f);
                      return true;
                  },
                  [this] { return uploadMesh(kEnvironmentMeshSlot); });

    if (!loads.runPreload()) {
        LOG_ERROR() << "environment geometry failed to load";
        return false;
    }

    const MeshId environment_mesh = static_cast<MeshId>(kEnvironmentMeshSlot);

    // (4) The character's placeholder shares that geometry: the first logic tick already moves it, so
    // it has to be resident before start(). It is the only model in the scene at this point - the props
    // below are requested now but exist later.
    character_model = Model(environment_mesh);
    character_model.setUniformScale(0.6f);
    character_model.setSpin(0.0f);  // the character does not spin
    syncCharacterModel();

    // (5) Constant buffer: one block per draw, times the number of frames the backend may run in
    // flight, each block aligned to what the device requires. All of them live in one buffer and are
    // selected with a range/dynamic offset at bind time - the portable way to feed per-draw constants.
    // The draw count covers the whole plan, not just what exists yet: every streaming request is made
    // below, before the loop starts, so the slab never has to grow while frames may still read it.
    max_draws = static_cast<std::uint32_t>(models.size() + kStreamedPropCount + 1);  // now + plan + character
    const std::uint32_t alignment = device.uniformBufferAlignment() > 0 ? device.uniformBufferAlignment() : 16;
    uniform_stride = ((static_cast<std::uint32_t>(sizeof(mat4)) + alignment - 1) / alignment) * alignment;

    BufferDesc uniform_buffer_desc;
    uniform_buffer_desc.size = uniform_stride * max_draws * device.framesInFlight();
    uniform_buffer_desc.usage = BufferUsage::Uniform;
    uniform_buffer_desc.initial_data = nullptr;  // filled every frame
    uniform_buffer = device.createBuffer(uniform_buffer_desc);
    if (uniform_buffer == kInvalidHandle) {
        LOG_ERROR() << "constant buffer creation failed: " << device.lastError();
        return false;
    }

    // (6) Scene, second wave - streaming. Props that no per-tick computation reads, so they may arrive
    // after the loop is running: the queue hands out one item per turn (GameLoop streams it between the
    // ticks and the frame), and each item produces its own payload and uploads its own buffers.
    struct PropSpec
    {
        vec3 position;
        float scale;
        float spin_degrees_per_second;
        vec3 spin_axis;
    };

    const PropSpec specs[] = {
        {vec3{-4.0f, 0.0f, -2.0f}, 1.0f, 30.0f, vec3{0.0f, 1.0f, 0.0f}},
        {vec3{4.0f, 0.0f, -2.0f}, 1.4f, 60.0f, vec3{1.0f, 0.0f, 0.0f}},
        {vec3{0.0f, 0.0f, -7.0f}, 0.7f, 90.0f, vec3{1.0f, 1.0f, 0.0f}},
    };

    for (std::size_t i = 0; i < kStreamedPropCount; ++i) {
        const PropSpec spec = specs[i];
        const std::size_t slot = kEnvironmentMeshSlot + 1 + i;

        loads.request(
            LoadPriority::Streaming,
            // produce: build the payload. A generated cube here; in an asset pipeline this is the file
            // read and the parse - the part that would move to a worker thread, leaving apply() below
            // where it is.
            [this, slot] {
                staged_meshes[slot] = MeshGen::makeCube(1.0f);
                return true;
            },
            // apply: upload the payload and put the model into the scene, on the main thread
            [this, slot, spec, i] {
                if (!uploadMesh(slot)) {
                    return false;
                }

                Model model(static_cast<MeshId>(slot));
                model.setPosition(spec.position);
                model.setUniformScale(spec.scale);
                model.setSpin(spec.spin_degrees_per_second, spec.spin_axis);
                models.push_back(model);

                LOG_INFO() << "stream: prop " << (i + 1) << "/" << kStreamedPropCount
                           << " ready at tick " << game_loop.getTickCount() << " | models " << models.size()
                           << " | pending " << game_loop.getLoadQueue().pendingCount();
                return true;
            });
    }

    LOG_INFO() << "scene: " << meshes.size() << " mesh(es) resident, " << (models.size() + 1)
               << " model(s) | " << kStreamedPropCount << " prop(s) streaming | backend: "
               << device.backendName();

    game_loop.init();
    game_loop.start();
    LOG_INFO() << "game loop started: fixed tick = " << game_loop.getFixedTickSeconds() << " s, streaming "
               << game_loop.getStreamingItemsPerTurn() << " item(s) per turn";
    LOG_INFO() << "initial camera view: " << (camera_view == CameraView::FPV ? "FPV" : "TPV")
               << " (press F5 to switch view mode)";
    return true;
}

void Sandbox::releaseResources()
{
    if (device == nullptr) {
        return;
    }

    for (const GpuMesh &mesh : meshes) {
        device->destroyBuffer(mesh.vertex_buffer);
        device->destroyBuffer(mesh.index_buffer);
    }
    meshes.clear();

    if (uniform_buffer != kInvalidHandle) {
        device->destroyBuffer(uniform_buffer);
        uniform_buffer = kInvalidHandle;
    }
    if (pipeline != kInvalidHandle) {
        device->destroyPipeline(pipeline);
        pipeline = kInvalidHandle;
    }
    if (vertex_shader != kInvalidHandle) {
        device->destroyShader(vertex_shader);
        vertex_shader = kInvalidHandle;
    }
    if (fragment_shader != kInvalidHandle) {
        device->destroyShader(fragment_shader);
        fragment_shader = kInvalidHandle;
    }

    models.clear();
    staged_meshes.clear();  // the payloads the loads produced are not needed once their buffers exist
    device = nullptr;
}

// Upload one staged payload. This is the only place that turns CPU geometry into GPU buffers, whether
// it runs in the preload wave before the loop or in a streaming step inside it.
bool Sandbox::uploadMesh(std::size_t slot)
{
    if (device == nullptr || slot >= staged_meshes.size()) {
        return false;
    }

    const MeshData &cpu_mesh = staged_meshes[slot];

    BufferDesc vertex_buffer_desc;
    vertex_buffer_desc.size = static_cast<std::uint32_t>(cpu_mesh.vertices.size() * sizeof(Vertex));
    vertex_buffer_desc.usage = BufferUsage::Vertex;
    vertex_buffer_desc.initial_data = cpu_mesh.vertices.data();

    BufferDesc index_buffer_desc;
    index_buffer_desc.size = static_cast<std::uint32_t>(cpu_mesh.indices.size() * sizeof(std::uint32_t));
    index_buffer_desc.usage = BufferUsage::Index;
    index_buffer_desc.initial_data = cpu_mesh.indices.data();

    GpuMesh gpu_mesh;
    gpu_mesh.vertex_buffer = device->createBuffer(vertex_buffer_desc);
    gpu_mesh.index_buffer = device->createBuffer(index_buffer_desc);
    gpu_mesh.index_count = static_cast<std::uint32_t>(cpu_mesh.indices.size());
    gpu_mesh.index_format = IndexFormat::UInt32;

    if (gpu_mesh.vertex_buffer == kInvalidHandle || gpu_mesh.index_buffer == kInvalidHandle) {
        LOG_ERROR() << "geometry upload failed: " << device->lastError();
        return false;
    }

    if (meshes.size() <= slot) {
        meshes.resize(slot + 1);
    }
    meshes[slot] = gpu_mesh;
    return true;
}

// ---------------- per frame ----------------

void Sandbox::resize(int width, int height)
{
    // Only the aspect ratio is handed to the camera; fov / near / far stay with the camera itself
    character.getCamera().setViewportAspect(height > 0 ? static_cast<float>(width) / static_cast<float>(height)
                                                       : 1.0f);
}

void Sandbox::handleInput(const FrameInput &input)
{
    frame_input = input;  // kept for the next logic tick (movement intent)

    if (InputMapping::viewToggleRequested(input)) {
        setCameraView(camera_view == CameraView::FPV ? CameraView::TPV : CameraView::FPV);
    }
    if (InputMapping::resetRequested(input)) {
        resetCamera();
    }

    // Wheel: TPV dollies the follow distance, FPV zooms the field of view
    if (input.wheel_steps != 0.0f) {
        Camera &camera = character.getCamera();
        if (camera_view == CameraView::TPV) {
            character.setCameraDistance(character.cameraDistance() - input.wheel_steps * wheel_step);
        } else {
            camera.setPerspective(camera.getFovYDegrees() - input.wheel_steps * 2.0f,
                                  camera.getNearPlane(), camera.getFarPlane());
        }
        logCameraPositionIfMoved();
    }

    // Left-button drag: FPV free look, TPV orbit around the character
    if (input.mouse_left_down && (input.mouse_delta_x != 0 || input.mouse_delta_y != 0)) {
        const float dx = static_cast<float>(input.mouse_delta_x) * orbit_speed;
        const float dy = static_cast<float>(input.mouse_delta_y) * orbit_speed;

        if (camera_view == CameraView::FPV) {
            character.getCamera().yawPitch(-dx, -dy);
        } else {
            character.orbitCamera(-dx, dy);
        }
        logCameraPositionIfMoved();
    }
}

void Sandbox::drawModel(IRenderDevice &device, ICommandList &commands, const mat4 &view_projection,
                        const Model &model, std::uint32_t block_index)
{
    if (!model.hasMesh() || model.getMesh() >= meshes.size()) {
        return;
    }

    // Geometry is bound per draw: streamed objects own the buffers they arrived with, so there is no
    // single "current mesh" to bind once per frame.
    const GpuMesh &mesh = meshes[model.getMesh()];
    commands.bindVertexBuffer(mesh.vertex_buffer);
    commands.bindIndexBuffer(mesh.index_buffer, mesh.index_format);

    // Write this draw's matrix into its own aligned block, then bind exactly that range. Nothing is
    // overwritten that a frame in flight could still be reading.
    const mat4 mvp = view_projection * model.modelMatrix();
    const std::uint32_t offset = block_index * uniform_stride;
    device.updateBuffer(uniform_buffer, &mvp, static_cast<std::uint32_t>(sizeof(mat4)), offset);
    commands.bindUniformBuffer(uniform_buffer, 0, offset, static_cast<std::uint32_t>(sizeof(mat4)));

    commands.drawIndexed(mesh.index_count);
}

void Sandbox::render(IRenderDevice &device)
{
    ICommandList &commands = device.getCommandList();

    commands.clear(0.1f, 0.12f, 0.15f, 0.1f, 1.0f);

    if (meshes.empty()) {
        return;
    }

    commands.bindPipeline(pipeline);

    const Camera &camera = character.getCamera();
    const mat4 view_projection = camera.projectionMatrix() * camera.viewMatrix();

    // This frame owns one set of constant blocks
    const std::uint32_t frame_base = frame_index * max_draws;
    std::uint32_t block = 0;
    for (const Model &model : models) {
        drawModel(device, commands, view_projection, model, frame_base + block);
        ++block;
    }
    drawModel(device, commands, view_projection, character_model, frame_base + block);

    frame_index = (frame_index + 1) % device.framesInFlight();
}

// ---------------- logic ----------------

void Sandbox::onTick()
{
    const float dt = static_cast<float>(game_loop.getFixedTickSeconds());

    // Every model spins at its own rate; a fixed step keeps that independent of the frame rate
    for (Model &model : models) {
        model.updateSpin(dt);
    }

    updateCharacter(dt);
}

void Sandbox::updateCharacter(float dt)
{
    if (dt <= 0.0f) {
        return;
    }

    character.getController().setInput(InputMapping::characterInputFromKeys(frame_input));
    character.update(dt);  // advance the position, then place the camera for the current view mode
    syncCharacterModel();
    logCameraPositionIfMoved();
}

void Sandbox::resetCamera()
{
    character.setPosition(vec3{0.0f, 0.0f, 0.0f});
    character.setCameraOffset(vec3{0.0f, 0.4f, 5.0f});  // reset the TPV orbit offset (target-relative)

    Camera &camera = character.getCamera();
    camera.setOrientation(quat{});  // identity quaternion = looking down -Z, up is +Y
    camera.setPerspective(90.0f, 0.1f, 100.0f);
    character.syncCamera();  // reposition for the current view mode
    syncCharacterModel();

    has_logged_camera = false;  // make the next frame log for sure
    logCameraPositionIfMoved();
}

void Sandbox::setCameraView(CameraView view)
{
    if (camera_view == view) {
        return;
    }

    camera_view = view;
    character.setView(view);      // the character repositions the camera for the new mode
    frame_input.clearPerFrame();  // avoids "stuck keys" at the moment of switching

    LOG_INFO() << "camera view: " << (camera_view == CameraView::FPV ? "FPV" : "TPV");
    logCameraPositionIfMoved();
}

// Character placeholder: follows the character (the cube centre is raised to waist height)
void Sandbox::syncCharacterModel()
{
    if (!character_model.hasMesh()) {
        return;
    }
    character_model.setPosition(character.getPosition() + vec3{0.0f, 0.9f, 0.0f});
}

// Camera pose log: one line when the position or the orientation changed past a threshold
// (an FPV head turn counts even though the position does not change)
void Sandbox::logCameraPositionIfMoved()
{
    const Camera &camera = character.getCamera();
    const vec3 position = camera.getPosition();
    const vec3 forward_dir = camera.forward();

    const bool position_changed = !has_logged_camera
                                  || (position - last_logged_camera_position).length() >= log_move_threshold;
    const bool forward_changed = !has_logged_camera
                                 || forward_dir.dot(last_logged_camera_forward) < 0.999f;
    if (!position_changed && !forward_changed) {
        return;
    }

    has_logged_camera = true;
    last_logged_camera_position = position;
    last_logged_camera_forward = forward_dir;

    LOG_INFO() << "camera[" << (camera_view == CameraView::FPV ? "FPV" : "TPV") << "]: eye("
               << logFixed(position.x, 2) << ", " << logFixed(position.y, 2) << ", "
               << logFixed(position.z, 2) << ") forward(" << logFixed(forward_dir.x, 3) << ", "
               << logFixed(forward_dir.y, 3) << ", " << logFixed(forward_dir.z, 3) << ")";
}
