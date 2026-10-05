#include "app/Sandbox.h"

#include "core/geometry/generator/MeshGen.h"
#include "core/log/LogManager.h"
#include "core/platform/File.h"
#include "game/InputMapping.h"

#include <cstddef>

Sandbox::Sandbox()
{
    // Logic ticks are a callback now: the platform pump calls game_loop.advance(), which lands here
    game_loop.setTickCallback([this](std::uint64_t) { onTick(); });
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

    // (1) Shaders.
    // The device says which dialect it wants, so a backend switch means loading different files
    // rather than editing this code. Only GLSL exists today; the SPIR-V (A6) and HLSL (A7) branches
    // plug in here.
    if (device.shaderLanguage() != ShaderLanguage::GLSLSource) {
        LOG_ERROR() << "this backend's shader dialect is not implemented yet: " << device.backendName();
        return false;
    }

    std::string vertex_source;
    std::string fragment_source;
    const std::string shader_directory = assetDirectory + "/shaders";
    if (!File::readTextFile(shader_directory + "/basic.vert", vertex_source)
        || !File::readTextFile(shader_directory + "/basic.frag", fragment_source)) {
        return false;
    }

    ShaderDesc vertex_desc;
    vertex_desc.stage = ShaderStage::Vertex;
    vertex_desc.code = vertex_source.data();
    vertex_desc.size = vertex_source.size();
    vertex_desc.debug_name = "basic.vert";
    vertex_shader = device.createShader(vertex_desc);

    ShaderDesc fragment_desc;
    fragment_desc.stage = ShaderStage::Fragment;
    fragment_desc.code = fragment_source.data();
    fragment_desc.size = fragment_source.size();
    fragment_desc.debug_name = "basic.frag";
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

    // (3) Geometry: CPU-side MeshData from core, uploaded through the device
    const MeshData cube = MeshGen::makeCube(1.0f);

    BufferDesc vertex_buffer_desc;
    vertex_buffer_desc.size = static_cast<std::uint32_t>(cube.vertices.size() * sizeof(Vertex));
    vertex_buffer_desc.usage = BufferUsage::Vertex;
    vertex_buffer_desc.initial_data = cube.vertices.data();

    BufferDesc index_buffer_desc;
    index_buffer_desc.size = static_cast<std::uint32_t>(cube.indices.size() * sizeof(std::uint32_t));
    index_buffer_desc.usage = BufferUsage::Index;
    index_buffer_desc.initial_data = cube.indices.data();

    GpuMesh mesh;
    mesh.vertex_buffer = device.createBuffer(vertex_buffer_desc);
    mesh.index_buffer = device.createBuffer(index_buffer_desc);
    mesh.index_count = static_cast<std::uint32_t>(cube.indices.size());
    mesh.index_format = IndexFormat::UInt32;
    if (mesh.vertex_buffer == kInvalidHandle || mesh.index_buffer == kInvalidHandle) {
        LOG_ERROR() << "geometry upload failed: " << device.lastError();
        return false;
    }
    meshes.push_back(mesh);
    const MeshId cube_mesh = 0;

    // (4) Constant buffer for the per-draw matrix (std140, slot 0 in the shader)
    uniform_buffer_size = static_cast<std::uint32_t>(sizeof(mat4));
    BufferDesc uniform_buffer_desc;
    uniform_buffer_desc.size = uniform_buffer_size;
    uniform_buffer_desc.usage = BufferUsage::Uniform;
    uniform_buffer_desc.initial_data = nullptr;  // filled per draw
    uniform_buffer = device.createBuffer(uniform_buffer_desc);
    if (uniform_buffer == kInvalidHandle) {
        LOG_ERROR() << "constant buffer creation failed: " << device.lastError();
        return false;
    }

    // (5) Models: three spinning cubes plus the character placeholder, all sharing one geometry
    struct ModelSpec
    {
        vec3 position;
        float scale;
        float spin_degrees_per_second;
        vec3 spin_axis;
    };

    const ModelSpec specs[] = {
        {vec3{-4.0f, 0.0f, -2.0f}, 1.0f, 30.0f, vec3{0.0f, 1.0f, 0.0f}},
        {vec3{4.0f, 0.0f, -2.0f}, 1.4f, 60.0f, vec3{1.0f, 0.0f, 0.0f}},
        {vec3{0.0f, 0.0f, -7.0f}, 0.7f, 90.0f, vec3{1.0f, 1.0f, 0.0f}},
    };

    for (const ModelSpec &spec : specs) {
        Model model(cube_mesh);
        model.setPosition(spec.position);
        model.setUniformScale(spec.scale);
        model.setSpin(spec.spin_degrees_per_second, spec.spin_axis);
        models.push_back(model);
    }

    character_model = Model(cube_mesh);
    character_model.setUniformScale(0.6f);
    character_model.setSpin(0.0f);  // the character does not spin
    syncCharacterModel();

    LOG_INFO() << "scene: " << meshes.size() << " mesh(es), " << (models.size() + 1) << " model(s)"
               << " | backend: " << device.backendName();

    game_loop.init();
    game_loop.start();
    LOG_INFO() << "game loop started: fixed tick = " << game_loop.getFixedTickSeconds()
               << " s, frame interval = " << game_loop.getFrameInterval() << " ms";
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
    device = nullptr;
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
                        const Model &model)
{
    if (!model.hasMesh() || model.getMesh() >= meshes.size()) {
        return;
    }

    // One constant buffer per draw. Note for the Vulkan/D3D12 backends: a frame in flight must not
    // overwrite constants the GPU may still be reading, so A6/A7 will need a small ring of buffers
    // here (or dynamic offsets) instead of updating one buffer between draws.
    const mat4 mvp = view_projection * model.modelMatrix();
    device.updateBuffer(uniform_buffer, &mvp, uniform_buffer_size, 0);

    commands.drawIndexed(meshes[model.getMesh()].index_count);
}

void Sandbox::render(IRenderDevice &device)
{
    ICommandList &commands = device.getCommandList();

    commands.clear(0.1f, 0.12f, 0.15f, 0.1f, 1.0f);

    commands.bindPipeline(pipeline);
    commands.bindVertexBuffer(meshes.empty() ? kInvalidHandle : meshes[0].vertex_buffer);
    commands.bindIndexBuffer(meshes.empty() ? kInvalidHandle : meshes[0].index_buffer, IndexFormat::UInt32);
    commands.bindUniformBuffer(uniform_buffer, 0);

    const Camera &camera = character.getCamera();
    const mat4 view_projection = camera.projectionMatrix() * camera.viewMatrix();

    for (const Model &model : models) {
        drawModel(device, commands, view_projection, model);
    }
    drawModel(device, commands, view_projection, character_model);
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
    character.setCameraOffset(vec3{0.0f, 2.0f, 5.0f});  // reset the TPV orbit offset

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
