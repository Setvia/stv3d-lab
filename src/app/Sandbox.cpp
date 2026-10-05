#include "Sandbox.h"

#include "core/log/LogManager.h"
#include "game/InputMapping.h"

#include <utility>

Sandbox::Sandbox()
{
    // Logic ticks and frames are callbacks now: the platform pump calls game_loop.advance(),
    // which lands here. A tick updates the world, a frame draws it (see main.cpp).
    game_loop.setTickCallback([this](std::uint64_t) { onTick(); });
}

// ---------------- resources ----------------

bool Sandbox::createResources(GLFunctions &gfx, const std::string &shaderDirectory)
{
    // (0) Fixed pipeline state: the same clear colour and depth test the Qt version used
    gfx.glClearColor(0.1f, 0.12f, 0.15f, 0.1f);
    gfx.glEnable(GL_DEPTH_TEST);

    // (1) Shader program: one program shared by every model
    const std::string vertex_path = shaderDirectory + "/basic.vert";
    const std::string fragment_path = shaderDirectory + "/basic.frag";
    const bool shader_ok = program.createFromFiles(gfx, vertex_path, fragment_path,
                                                   // explicit attribute indices (the constants live in Mesh)
                                                   {{Mesh::kAttribPos, "aPos"}, {Mesh::kAttribColor, "aColor"}});
    if (!shader_ok) {
        LOG_ERROR() << "shader program creation failed; models cannot be drawn";
        return false;
    }

    // (2) Geometry: the cube is uploaded once and shared by all models
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    MeshFactory::makeCube(vertices, indices, 1.0f);

    auto cube_mesh = std::make_shared<Mesh>();
    cube_mesh->create(gfx, vertices, indices);
    if (!cube_mesh->isValid()) {
        LOG_ERROR() << "cube mesh creation failed";
        return false;
    }
    meshes.push_back(cube_mesh);

    // (3) Models: three independent spinning cubes plus the character placeholder
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
        Model model(cube_mesh);  // * shares the same mesh
        model.setPosition(spec.position);
        model.setUniformScale(spec.scale);
        model.setSpin(spec.spin_degrees_per_second, spec.spin_axis);
        models.push_back(std::move(model));
    }

    character_model = Model(cube_mesh);
    character_model.setUniformScale(0.6f);
    character_model.setSpin(0.0f);  // the character does not spin
    syncCharacterModel();

    LOG_INFO() << "scene: " << meshes.size() << " mesh(es), " << (models.size() + 1) << " model(s)";

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
    // Mesh and ShaderProgram delete their GL objects in the destructor, so clearing is enough.
    // The context must still be current here.
    models.clear();
    meshes.clear();
    program.destroy();
}

// ---------------- per frame ----------------

void Sandbox::resize(int width, int height, GLFunctions &gfx)
{
    gfx.glViewport(0, 0, width, height);
    // Only the aspect ratio is handed to the camera; fov / near / far stay with the camera
    character.getCamera().setViewportAspect(height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f);
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
            // First person: drag right turns the view right, drag down looks down.
            // Pitch clamping happens inside Camera::yawPitch (no Euler angles are stored)
            character.getCamera().yawPitch(-dx, -dy);
        } else {
            // Third person: drag right swings the camera left, drag down raises it
            // ("grab and drag the character" feel)
            character.orbitCamera(-dx, dy);
        }
        logCameraPositionIfMoved();
    }
}

void Sandbox::render(GLFunctions &gfx)
{
    gfx.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Camera &camera = character.getCamera();
    const mat4 view_projection = camera.projectionMatrix() * camera.viewMatrix();

    program.bind();  // all models share the same shader program

    for (const Model &model : models) {
        Mesh *mesh = model.getMesh();
        if (mesh == nullptr || !mesh->isValid()) {
            continue;
        }
        // Each model has its own model matrix, so the uniform is rewritten per model
        // (the uniform location itself is cached by ShaderProgram)
        program.setMat4("uMvp", view_projection * model.modelMatrix());
        mesh->draw();
    }

    if (character_model.getMesh() != nullptr && character_model.getMesh()->isValid()) {
        program.setMat4("uMvp", view_projection * character_model.modelMatrix());
        character_model.getMesh()->draw();
    }
}

// ---------------- logic ----------------

void Sandbox::onTick()
{
    const float dt = static_cast<float>(game_loop.getFixedTickSeconds());

    // Every model spins at its own rate; a fixed step keeps that independent of the frame rate
    for (Model &model : models) {
        model.updateSpin(dt);
    }

    // Both view modes drive the camera through the character: FPV places it at the eyes, TPV at the
    // orbit offset
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
    character.setView(view);         // the character repositions the camera for the new mode
    frame_input.clearPerFrame();     // avoids "stuck keys" at the moment of switching

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
